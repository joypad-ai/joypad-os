// bt_transport_esp32.c - ESP32-S3 Bluetooth Transport
// Implements bt_transport_t using BTstack with ESP32's VHCI (BLE-only)
//
// Supports two modes:
//   - Central (bt2usb): scans/connects BLE controllers via btstack_host
//   - Peripheral (universal): advertises as BLE gamepad via ble_output
// Mode is selected via bt_esp32_set_post_init() callback before bt_init().

#include "bt_transport.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// BTstack includes
#include "btstack_run_loop.h"

// FreeRTOS (for BTstack run loop task)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// BTstack ESP32 port
extern uint8_t btstack_init(void);

// BTstack HCI (for power on in peripheral mode)
#include "hci.h"

// ============================================================================
// POST-INIT CALLBACK (set by app before bt_init)
// ============================================================================

typedef void (*bt_esp32_post_init_fn)(void);
static bt_esp32_post_init_fn post_init_callback = NULL;

void bt_esp32_set_post_init(bt_esp32_post_init_fn fn)
{
    post_init_callback = fn;
}

// ============================================================================
// ESP32 TRANSPORT STATE
// ============================================================================

static bt_connection_t esp32_connections[BT_MAX_CONNECTIONS];
static bool esp32_initialized = false;

// ============================================================================
// CENTRAL MODE SUPPORT (bt2usb — btstack_host + bthid)
// Only linked when btstack_host.c and bthid.c are in the build.
// ============================================================================

// ---- Central-mode function declarations (weak stubs for peripheral-only builds) ----
// When btstack_host.c is linked (central mode), these strong symbols override the stubs.

typedef struct {
    bool active;
    uint8_t bd_addr[6];
    char name[48];
    uint8_t class_of_device[3];
    uint16_t vendor_id;
    uint16_t product_id;
    bool hid_ready;
    bool is_ble;
} btstack_classic_conn_info_t;

__attribute__((weak)) void btstack_host_init_hid_handlers(void) {}
__attribute__((weak)) void btstack_host_process(void) {}
__attribute__((weak)) void bthid_task(void) {}
__attribute__((weak)) void btstack_host_power_on(void) {}
__attribute__((weak)) bool btstack_host_is_powered_on(void) { return false; }
__attribute__((weak)) void btstack_host_start_scan(void) {}
__attribute__((weak)) void btstack_host_stop_scan(void) {}
__attribute__((weak)) bool btstack_host_is_scanning(void) { return false; }
__attribute__((weak)) uint8_t btstack_classic_get_connection_count(void) { return 0; }
__attribute__((weak)) bool btstack_classic_get_connection(uint8_t idx, btstack_classic_conn_info_t *info) { (void)idx; (void)info; return false; }
__attribute__((weak)) bool btstack_classic_send_set_report_type(uint8_t idx, uint8_t type, uint8_t id, const uint8_t *data, uint16_t len) { (void)idx; (void)type; (void)id; (void)data; (void)len; return false; }
__attribute__((weak)) bool btstack_classic_send_report(uint8_t idx, uint8_t id, const uint8_t *data, uint16_t len) { (void)idx; (void)id; (void)data; (void)len; return false; }

// ============================================================================
// TRANSPORT IMPLEMENTATION
// ============================================================================

// Periodic timer: runs btstack_host_process() and bthid_task() inside the
// BTstack run loop context so all BTstack API calls happen in one thread.
// In peripheral mode, the timer still runs but the weak stubs are no-ops.
static btstack_timer_source_t process_timer;
#define PROCESS_INTERVAL_MS 10

static void process_timer_handler(btstack_timer_source_t *ts)
{
    btstack_host_process();
    bthid_task();

    // Re-arm timer
    btstack_run_loop_set_timer(ts, PROCESS_INTERVAL_MS);
    btstack_run_loop_add_timer(ts);
}

// BTstack run loop task — mirrors the BTstack ESP32 template exactly:
// all BTstack init + run loop in a single dedicated FreeRTOS task.
static void btstack_run_loop_task(void *arg)
{
    (void)arg;
    printf("[BT_ESP32] BTstack task started — initializing...\n");

    // 1. Initialize BTstack (memory, FreeRTOS run loop, HCI/VHCI transport, TLV)
    uint8_t err = btstack_init();
    if (err) {
        printf("[BT_ESP32] ERROR: btstack_init failed: %d\n", err);
        return;
    }
    printf("[BT_ESP32] BTstack core initialized\n");

    // 2. Post-init: app-provided callback (peripheral) or default HID host (central)
    if (post_init_callback) {
        printf("[BT_ESP32] Running app post-init callback (peripheral mode)\n");
        post_init_callback();
    } else {
        printf("[BT_ESP32] Initializing HID host handlers (central mode)\n");
        btstack_host_init_hid_handlers();
    }

    // 3. Start periodic process timer (runs host_process + bthid_task in this context)
    btstack_run_loop_set_timer_handler(&process_timer, process_timer_handler);
    btstack_run_loop_set_timer(&process_timer, PROCESS_INTERVAL_MS);
    btstack_run_loop_add_timer(&process_timer);

    // 4. Power on Bluetooth (triggers HCI state machine)
    if (post_init_callback) {
        // Peripheral mode: call hci_power_control directly (no btstack_host)
        printf("[BT_ESP32] Powering on BT controller (peripheral mode)\n");
        hci_power_control(HCI_POWER_ON);
    } else {
        btstack_host_power_on();
    }

    esp32_initialized = true;
    printf("[BT_ESP32] Entering BTstack run loop\n");

    // 5. Enter run loop (blocks forever, processes all HCI/L2CAP/GATT events)
    btstack_run_loop_execute();
}

static void esp32_transport_init(void)
{
    memset(esp32_connections, 0, sizeof(esp32_connections));
    printf("[BT_ESP32] Transport init — launching BTstack task\n");

    // All BTstack initialization happens in the dedicated task (same pattern
    // as BTstack ESP32 template). This ensures BT controller init, HCI, and
    // run loop all execute in the same FreeRTOS task context.
    xTaskCreate(btstack_run_loop_task, "btstack", 12288, NULL,
                configMAX_PRIORITIES - 2, NULL);
}

static void esp32_transport_task(void)
{
    // btstack_host_process() and bthid_task() run inside the BTstack run loop
    // via process_timer_handler, not here. Calling BTstack APIs from the main
    // FreeRTOS task would race with the BTstack task.
}

static bool esp32_transport_is_ready(void)
{
    // In peripheral mode (post_init_callback set), esp32_initialized is sufficient.
    // In central mode, also check btstack_host_is_powered_on().
    if (post_init_callback) {
        return esp32_initialized;
    }
    return esp32_initialized && btstack_host_is_powered_on();
}

static uint8_t esp32_transport_get_connection_count(void)
{
    return btstack_classic_get_connection_count();
}

static const bt_connection_t* esp32_transport_get_connection(uint8_t index)
{
    if (index >= BT_MAX_CONNECTIONS) {
        return NULL;
    }

    btstack_classic_conn_info_t info;
    if (!btstack_classic_get_connection(index, &info)) {
        return NULL;
    }

    // Update cached connection struct
    bt_connection_t* conn = &esp32_connections[index];
    memcpy(conn->bd_addr, info.bd_addr, 6);
    strncpy(conn->name, info.name, BT_MAX_NAME_LEN - 1);
    conn->name[BT_MAX_NAME_LEN - 1] = '\0';
    memcpy(conn->class_of_device, info.class_of_device, 3);
    conn->vendor_id = info.vendor_id;
    conn->product_id = info.product_id;
    conn->connected = info.active;
    conn->hid_ready = info.hid_ready;
    conn->is_ble = info.is_ble;

    return conn;
}

static bool esp32_transport_send_control(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    if (len >= 2) {
        uint8_t header = data[0];
        uint8_t report_type = header & 0x03;
        uint8_t report_id = data[1];
        return btstack_classic_send_set_report_type(conn_index, report_type, report_id, data + 2, len - 2);
    }
    return false;
}

static bool esp32_transport_send_interrupt(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    if (len >= 2) {
        uint8_t report_id = data[1];
        return btstack_classic_send_report(conn_index, report_id, data + 2, len - 2);
    }
    return false;
}

static void esp32_transport_disconnect(uint8_t conn_index)
{
    (void)conn_index;
    // TODO: Implement disconnect
}

static void esp32_transport_set_pairing_mode(bool enable)
{
    if (enable) {
        btstack_host_start_scan();
    } else {
        btstack_host_stop_scan();
    }
}

static bool esp32_transport_is_pairing_mode(void)
{
    return btstack_host_is_scanning();
}

// ============================================================================
// TRANSPORT STRUCT
// ============================================================================

const bt_transport_t bt_transport_esp32 = {
    .name = "ESP32-S3 BLE",
    .init = esp32_transport_init,
    .task = esp32_transport_task,
    .is_ready = esp32_transport_is_ready,
    .get_connection_count = esp32_transport_get_connection_count,
    .get_connection = esp32_transport_get_connection,
    .send_control = esp32_transport_send_control,
    .send_interrupt = esp32_transport_send_interrupt,
    .disconnect = esp32_transport_disconnect,
    .set_pairing_mode = esp32_transport_set_pairing_mode,
    .is_pairing_mode = esp32_transport_is_pairing_mode,
};
