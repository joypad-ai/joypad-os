// bt_transport_esp32.c - ESP32-S3 Bluetooth Transport
// Implements bt_transport_t using BTstack with ESP32's VHCI (BLE-only)
//
// This is for the bt2usb app on ESP32-S3 - receives BLE controllers via
// built-in BLE radio, outputs as USB HID device.

#include "bt_transport.h"
#include "bt/bthid/bthid.h"
#include "bt/btstack/btstack_host.h"
#include <string.h>
#include <stdio.h>

// BTstack includes
#include "btstack_run_loop.h"

// FreeRTOS (for BTstack run loop task)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// BTstack ESP32 port
extern uint8_t btstack_init(void);

// ============================================================================
// ESP32 TRANSPORT STATE
// ============================================================================

static bt_connection_t esp32_connections[BT_MAX_CONNECTIONS];
static bool esp32_initialized = false;

// ============================================================================
// ESP32 TRANSPORT PROCESS (called by btstack_host_process)
// ============================================================================

// Override weak function in btstack_host.c to process ESP32 transport
void btstack_host_transport_process(void)
{
    // ESP32 uses FreeRTOS run loop - processing happens automatically
    // in the BTstack task. No manual polling needed.
}

// ============================================================================
// TRANSPORT IMPLEMENTATION
// ============================================================================

// Periodic timer: runs btstack_host_process() and bthid_task() inside the
// BTstack run loop context so all BTstack API calls happen in one thread.
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

    // 2. Register our HID host handlers (before run loop processes events)
    btstack_host_init_hid_handlers();

    // 3. Start periodic process timer (runs host_process + bthid_task in this context)
    btstack_run_loop_set_timer_handler(&process_timer, process_timer_handler);
    btstack_run_loop_set_timer(&process_timer, PROCESS_INTERVAL_MS);
    btstack_run_loop_add_timer(&process_timer);

    // 4. Power on Bluetooth (triggers HCI state machine)
    btstack_host_power_on();

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
    xTaskCreate(btstack_run_loop_task, "btstack", 8192, NULL,
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
