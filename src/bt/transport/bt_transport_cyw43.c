// bt_transport_cyw43.c - Pico W CYW43 Bluetooth Transport
// Implements bt_transport_t using BTstack with Pico W's built-in CYW43 Bluetooth
//
// Supports two modes:
//   - Central (bt2usb): scans/connects BT controllers via btstack_host
//   - Peripheral (controller_btusb): advertises as BLE gamepad via ble_output
// Mode is selected via bt_cyw43_set_post_init() callback before bt_init().

#include "bt_transport.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// POST-INIT CALLBACK (set by app before bt_init)
// ============================================================================

typedef void (*bt_cyw43_post_init_fn)(void);
static bt_cyw43_post_init_fn post_init_callback = NULL;

void bt_cyw43_set_post_init(bt_cyw43_post_init_fn fn)
{
    post_init_callback = fn;
}

// ============================================================================
// CENTRAL MODE SUPPORT (bt2usb — btstack_host + bthid)
// Only linked when btstack_host.c and bthid.c are in the build.
// ============================================================================

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
__attribute__((weak)) void btstack_host_transport_process(void) {}

// BTstack includes (must come before CYW43 includes)
#include "btstack_run_loop.h"
#include "hci.h"
#include "hci_transport.h"

// Pico W CYW43 includes
#include "pico/cyw43_arch.h"
#include "pico/btstack_cyw43.h"
#include "pico/btstack_hci_transport_cyw43.h"
#include "pico/async_context.h"

// ============================================================================
// CYW43 TRANSPORT STATE
// ============================================================================

static bt_connection_t cyw43_connections[BT_MAX_CONNECTIONS];
static bool cyw43_initialized = false;

// ============================================================================
// TRANSPORT IMPLEMENTATION
// ============================================================================

static void cyw43_transport_init(void)
{
    memset(cyw43_connections, 0, sizeof(cyw43_connections));
    printf("[BT_CYW43] Transport init (Pico W built-in Bluetooth)\n");

    // Initialize CYW43 driver (WiFi + BT)
    if (cyw43_arch_init()) {
        printf("[BT_CYW43] ERROR: Failed to initialize CYW43\n");
        return;
    }
    printf("[BT_CYW43] CYW43 driver initialized\n");

    // Initialize BTstack with CYW43
    // This uses the Pico SDK's btstack_cyw43 integration which handles:
    // - btstack_memory_init()
    // - btstack_run_loop_init() with async_context
    // - hci_init() with CYW43 transport
    // - TLV storage setup for bonding
    async_context_t *context = cyw43_arch_async_context();
    if (!btstack_cyw43_init(context)) {
        printf("[BT_CYW43] ERROR: Failed to initialize BTstack\n");
        return;
    }
    printf("[BT_CYW43] BTstack initialized\n");

    // Post-init: app-provided callback (peripheral) or default HID host (central)
    if (post_init_callback) {
        printf("[BT_CYW43] Running app post-init callback (peripheral mode)\n");
        post_init_callback();
    } else {
        printf("[BT_CYW43] Initializing HID host handlers (central mode)\n");
        btstack_host_init_hid_handlers();
    }

    cyw43_initialized = true;
    printf("[BT_CYW43] Ready for Bluetooth connections\n");

    // Power on Bluetooth
    if (post_init_callback) {
        hci_power_control(HCI_POWER_ON);
    } else {
        btstack_host_power_on();
    }
}

static uint32_t task_counter = 0;

static void cyw43_transport_task(void)
{
    if (!cyw43_initialized) return;

    task_counter++;
    if (task_counter == 1) {
        printf("[BT_CYW43] Task started\n");
    }

    // CYW43 uses async_context for processing
#if PICO_CYW43_ARCH_POLL
    cyw43_arch_poll();
#endif

    btstack_host_process();
    bthid_task();
}

static bool cyw43_transport_is_ready(void)
{
    if (post_init_callback) {
        return cyw43_initialized;
    }
    return cyw43_initialized && btstack_host_is_powered_on();
}

static uint8_t cyw43_transport_get_connection_count(void)
{
    return btstack_classic_get_connection_count();
}

static const bt_connection_t* cyw43_transport_get_connection(uint8_t index)
{
    if (index >= BT_MAX_CONNECTIONS) {
        return NULL;
    }

    btstack_classic_conn_info_t info;
    if (!btstack_classic_get_connection(index, &info)) {
        return NULL;
    }

    // Update cached connection struct
    bt_connection_t* conn = &cyw43_connections[index];
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

static bool cyw43_transport_send_control(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    // Classic BT: parse SET_REPORT header and forward to BTstack
    if (len >= 2) {
        uint8_t header = data[0];
        uint8_t report_type = header & 0x03;
        uint8_t report_id = data[1];
        return btstack_classic_send_set_report_type(conn_index, report_type, report_id, data + 2, len - 2);
    }
    return false;
}

static bool cyw43_transport_send_interrupt(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    // Classic BT: parse DATA|OUTPUT header and forward to BTstack
    if (len >= 2) {
        uint8_t report_id = data[1];
        return btstack_classic_send_report(conn_index, report_id, data + 2, len - 2);
    }
    return false;
}

static void cyw43_transport_disconnect(uint8_t conn_index)
{
    // TODO: Implement disconnect
    (void)conn_index;
}

static void cyw43_transport_set_pairing_mode(bool enable)
{
    if (enable) {
        btstack_host_start_scan();
    } else {
        btstack_host_stop_scan();
    }
}

static bool cyw43_transport_is_pairing_mode(void)
{
    return btstack_host_is_scanning();
}

// ============================================================================
// TRANSPORT STRUCT
// ============================================================================

const bt_transport_t bt_transport_cyw43 = {
    .name = "Pico W CYW43",
    .init = cyw43_transport_init,
    .task = cyw43_transport_task,
    .is_ready = cyw43_transport_is_ready,
    .get_connection_count = cyw43_transport_get_connection_count,
    .get_connection = cyw43_transport_get_connection,
    .send_control = cyw43_transport_send_control,
    .send_interrupt = cyw43_transport_send_interrupt,
    .disconnect = cyw43_transport_disconnect,
    .set_pairing_mode = cyw43_transport_set_pairing_mode,
    .is_pairing_mode = cyw43_transport_is_pairing_mode,
};
