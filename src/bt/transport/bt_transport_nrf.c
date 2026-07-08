// bt_transport_nrf.c - nRF52840 Bluetooth Transport (Zephyr HCI)
// Implements bt_transport_t using BTstack with Zephyr's raw HCI passthrough.
//
// Supports two modes:
//   - Central (bt2usb): scans/connects BLE controllers via btstack_host
//   - Peripheral (controller_btusb): advertises as BLE gamepad via ble_output
// Mode is selected via bt_nrf_set_post_init() callback before bt_init().
//
// Based on btstack/port/zephyr/src/main.c HCI transport + run loop.

#include "bt_transport.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// POST-INIT CALLBACK (set by app before bt_init)
// ============================================================================

typedef void (*bt_nrf_post_init_fn)(void);
static bt_nrf_post_init_fn post_init_callback = NULL;

void bt_nrf_set_post_init(bt_nrf_post_init_fn fn)
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

// Zephyr includes
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/buf.h>
#include <zephyr/bluetooth/hci_raw.h>
#include <zephyr/bluetooth/hci_types.h>

// Nordic device ID for static random address
#if defined(CONFIG_SOC_SERIES_NRF52X)
#include "nrf.h"
#endif

// BTstack includes
#include "btstack_debug.h"
#include "btstack_event.h"
#include "btstack_memory.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_base.h"
#include "btstack_tlv.h"
#include "btstack_tlv_none.h"
#include "btstack_chipset_zephyr.h"
#include "bluetooth_company_id.h"
#include "hci.h"
#include "hci_dump.h"
#include "hci_dump_embedded_stdout.h"
#include "hci_transport.h"
#include "ble/le_device_db_tlv.h"
#include "btstack_tlv_nrf.h"

// ============================================================================
// ZEPHYR HCI TRANSPORT (adapted from btstack/port/zephyr/src/main.c)
// ============================================================================

static K_FIFO_DEFINE(tx_queue);
static K_FIFO_DEFINE(rx_queue);

static void (*hci_packet_handler)(uint8_t packet_type, uint8_t *packet, uint16_t size);

static void hci_transport_init(const void *transport_config)
{
    int err = bt_enable_raw(&rx_queue);
    if (err) {
        printf("[BT_NRF] bt_enable_raw failed: %d\n", err);
        return;
    }
    printf("[BT_NRF] bt_enable_raw OK\n");
}

static int hci_transport_open(void)
{
    return 0;
}

static int hci_transport_close(void)
{
    return 0;
}

static void hci_transport_register_handler(void (*handler)(uint8_t, uint8_t *, uint16_t))
{
    hci_packet_handler = handler;
}

static int hci_transport_send(uint8_t packet_type, uint8_t *packet, int size)
{
    // NCS 3.1.0: bt_buf_get_tx() prepends a 1-byte H4 packet type indicator
    // to the buffer data. bt_send() reads buf->data[0] as the H4 type and
    // forwards the rest to the HCI driver. This is transparent to us.
    struct net_buf *buf;
    switch (packet_type) {
        case HCI_COMMAND_DATA_PACKET:
            buf = bt_buf_get_tx(BT_BUF_CMD, K_NO_WAIT, packet, size);
            if (!buf) {
                printf("[BT_NRF] CMD TX alloc failed! size=%d\n", size);
                break;
            }
            bt_send(buf);
            break;
        case HCI_ACL_DATA_PACKET:
            buf = bt_buf_get_tx(BT_BUF_ACL_OUT, K_NO_WAIT, packet, size);
            if (!buf) {
                printf("[BT_NRF] ACL TX alloc failed! size=%d\n", size);
                break;
            }
            bt_send(buf);
            break;
        default:
            break;
    }
    return 0;
}

static const hci_transport_t zephyr_hci_transport = {
    "zephyr",
    &hci_transport_init,
    &hci_transport_open,
    &hci_transport_close,
    &hci_transport_register_handler,
    NULL, // can_send_packet_now
    &hci_transport_send,
    NULL, // set_baudrate
    NULL, // reset_link
};

static void deliver_controller_packet(struct net_buf *buf)
{
    // NCS 3.1.0: buf->data[0] is the H4 packet type indicator byte.
    // Strip it before passing to BTstack (which expects raw HCI data).
    // bt_buf_get_type() is deprecated in NCS 3.1.0 and destructively
    // modifies the buffer, so we read the H4 byte directly instead.
    uint8_t h4_type = net_buf_pull_u8(buf);
    uint16_t size = buf->len;
    uint8_t *packet = buf->data;

    switch (h4_type) {
        case BT_HCI_H4_EVT:  // 0x04
            hci_packet_handler(HCI_EVENT_PACKET, packet, size);
            break;
        case BT_HCI_H4_ACL:  // 0x02
            hci_packet_handler(HCI_ACL_DATA_PACKET, packet, size);
            break;
        default:
            printf("[BT_NRF] RX unknown H4 type 0x%02x\n", h4_type);
            break;
    }
    net_buf_unref(buf);
}

// ============================================================================
// ZEPHYR RUN LOOP (adapted from btstack/port/zephyr/src/main.c)
// ============================================================================

static uint32_t run_loop_get_time_ms(void)
{
    return k_uptime_get_32();
}

static void run_loop_set_timer(btstack_timer_source_t *ts, uint32_t timeout_in_ms)
{
    ts->timeout = k_uptime_get_32() + 1 + timeout_in_ms;
}

static void run_loop_execute_on_main_thread(btstack_context_callback_registration_t *callback_registration)
{
    btstack_run_loop_base_add_callback(callback_registration);
}

static void run_loop_execute(void)
{
    while (1) {
        uint32_t now = k_uptime_get_32();

        btstack_run_loop_base_process_timers(now);
        btstack_run_loop_base_execute_callbacks();

        int32_t timeout_ms = btstack_run_loop_base_get_time_until_timeout(now);
        if (timeout_ms < 0) {
            timeout_ms = 10;
        }

        struct net_buf *buf = k_fifo_get(&rx_queue, K_MSEC(timeout_ms));
        if (buf) {
            deliver_controller_packet(buf);
        }
    }
}

static void run_loop_init(void)
{
    btstack_run_loop_base_init();
}

static const btstack_run_loop_t btstack_run_loop_zephyr = {
    .init = &run_loop_init,
    .set_timer = &run_loop_set_timer,
    .add_timer = &btstack_run_loop_base_add_timer,
    .remove_timer = &btstack_run_loop_base_remove_timer,
    .execute = &run_loop_execute,
    .dump_timer = &btstack_run_loop_base_dump_timer,
    .get_time_ms = &run_loop_get_time_ms,
    .execute_on_main_thread = &run_loop_execute_on_main_thread,
};

// ============================================================================
// NRF STATIC RANDOM ADDRESS
// ============================================================================

static bd_addr_t local_addr = { 0 };

static void nrf_get_static_random_addr(bd_addr_t addr)
{
#if defined(CONFIG_SOC_SERIES_NRF52X)
    big_endian_store_16(addr, 0, NRF_FICR->DEVICEADDR[1] | 0xc000);
    big_endian_store_32(addr, 2, NRF_FICR->DEVICEADDR[0]);
#endif
}

// ============================================================================
// BTSTACK EVENT HANDLER (address + chipset detection)
// ============================================================================

static void btstack_event_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_COMMAND_COMPLETE:
            switch (hci_event_command_complete_get_command_opcode(packet)) {
                case HCI_OPCODE_HCI_READ_LOCAL_VERSION_INFORMATION: {
                    uint16_t manufacturer = little_endian_read_16(packet, 10);
                    switch (manufacturer) {
                        case BLUETOOTH_COMPANY_ID_NORDIC_SEMICONDUCTOR_ASA:
                        case BLUETOOTH_COMPANY_ID_THE_LINUX_FOUNDATION:
                            hci_set_chipset(btstack_chipset_zephyr_instance());
                            // The zephyr chipset's static-address read returns
                            // 00:00:00:00:00:00 here, so advertising would go
                            // out with an all-zero address and never be
                            // discoverable. Use the FICR factory static-random
                            // address directly and enable random static
                            // addressing so the BLE peripheral is on air.
                            nrf_get_static_random_addr(local_addr);
                            gap_random_address_set(local_addr);
                            gap_random_address_set_mode(GAP_RANDOM_ADDRESS_TYPE_STATIC);
                            printf("[BT_NRF] FICR static random addr %s\n",
                                   bd_addr_to_str(local_addr));
                            break;
                        default:
                            nrf_get_static_random_addr(local_addr);
                            gap_random_address_set(local_addr);
                            gap_random_address_set_mode(GAP_RANDOM_ADDRESS_TYPE_STATIC);
                            printf("[BT_NRF] FICR static random addr %s\n",
                                   bd_addr_to_str(local_addr));
                            break;
                    }
                    break;
                }
                case HCI_OPCODE_HCI_READ_BD_ADDR: {
                    // Public BD_ADDR is 00:00:00:00:00:00 on the nRF SoftDevice.
                    // Do NOT let it clobber the FICR random static address we
                    // set above (used for advertising + logging).
                    break;
                }
                case HCI_OPCODE_HCI_ZEPHYR_READ_STATIC_ADDRESS: {
                    const uint8_t *params = hci_event_command_complete_get_return_parameters(packet);
                    if (params[0] == 0 && size >= 13) {
                        bd_addr_t a;
                        reverse_48(&params[2], a);
                        // Only apply a non-zero static address; the controller
                        // returns all-zeros here, which would clobber FICR.
                        static const uint8_t zero[6] = {0};
                        if (memcmp(a, zero, 6) != 0) {
                            memcpy(local_addr, a, 6);
                            gap_random_address_set(local_addr);
                            gap_random_address_set_mode(GAP_RANDOM_ADDRESS_TYPE_STATIC);
                        }
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("[BT_NRF] BTstack up and running on %s\n", bd_addr_to_str(local_addr));
            }
            break;
        default:
            break;
    }
}

// ============================================================================
// NRF TRANSPORT STATE
// ============================================================================

static bt_connection_t nrf_connections[BT_MAX_CONNECTIONS];
static bool nrf_initialized = false;

// ============================================================================
// NRF TRANSPORT PROCESS
// ============================================================================

void btstack_host_transport_process(void)
{
    // Zephyr run loop processes HCI automatically in the BTstack thread.
}

// ============================================================================
// BTSTACK THREAD
// ============================================================================

static btstack_timer_source_t process_timer;
static btstack_packet_callback_registration_t hci_event_callback_registration;
#define PROCESS_INTERVAL_MS 10

static void process_timer_handler(btstack_timer_source_t *ts)
{
    btstack_host_process();
    bthid_task();

    btstack_run_loop_set_timer(ts, PROCESS_INTERVAL_MS);
    btstack_run_loop_add_timer(ts);
}

static void btstack_thread_entry(void *p1, void *p2, void *p3)
{
    (void)p1; (void)p2; (void)p3;
    printf("[BT_NRF] BTstack thread started — initializing...\n");

    // 1. Initialize BTstack core
    btstack_memory_init();
    btstack_run_loop_init(&btstack_run_loop_zephyr);

    // 2. Setup TLV — NVS-backed persistent storage for bond keys
    extern struct nvs_fs* flash_nrf_get_nvs(void);
    struct nvs_fs *nvs = flash_nrf_get_nvs();
    const btstack_tlv_t *btstack_tlv_impl;
    if (nvs) {
        btstack_tlv_impl = btstack_tlv_nrf_init(nvs);
        printf("[BT_NRF] Using NVS-backed TLV for bond storage\n");
    } else {
        btstack_tlv_impl = btstack_tlv_none_init_instance();
        printf("[BT_NRF] WARNING: NVS not ready, bonds will not persist!\n");
    }
    btstack_tlv_set_instance(btstack_tlv_impl, NULL);
    le_device_db_tlv_configure(btstack_tlv_impl, NULL);

    // 3. Init HCI with Zephyr transport
    hci_init(&zephyr_hci_transport, NULL);

    // 4. Register event handler for address + chipset detection
    hci_event_callback_registration.callback = &btstack_event_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // 5. Post-init: app-provided callback (peripheral) or default HID host (central)
    if (post_init_callback) {
        printf("[BT_NRF] Running app post-init callback (peripheral mode)\n");
        post_init_callback();
    } else {
        printf("[BT_NRF] Initializing HID host handlers (central mode)\n");
        btstack_host_init_hid_handlers();
    }

    // 6. Start periodic process timer
    btstack_run_loop_set_timer_handler(&process_timer, process_timer_handler);
    btstack_run_loop_set_timer(&process_timer, PROCESS_INTERVAL_MS);
    btstack_run_loop_add_timer(&process_timer);

    // 7. Power on Bluetooth
    if (post_init_callback) {
        // Peripheral mode: call hci_power_control directly (no btstack_host)
        printf("[BT_NRF] Powering on BT controller (peripheral mode)\n");
        hci_power_control(HCI_POWER_ON);
    } else {
        btstack_host_power_on();
    }

    nrf_initialized = true;
    printf("[BT_NRF] Entering BTstack run loop\n");

    // 8. Enter run loop (blocks forever)
    btstack_run_loop_execute();
}

K_THREAD_STACK_DEFINE(btstack_stack, 16384);
static struct k_thread btstack_thread_data;

// ============================================================================
// TRANSPORT IMPLEMENTATION
// ============================================================================

static void nrf_transport_init(void)
{
    memset(nrf_connections, 0, sizeof(nrf_connections));
    printf("[BT_NRF] Transport init — launching BTstack thread\n");

    k_thread_create(&btstack_thread_data, btstack_stack,
                    K_THREAD_STACK_SIZEOF(btstack_stack),
                    btstack_thread_entry, NULL, NULL, NULL,
                    K_PRIO_COOP(2), 0, K_NO_WAIT);
    k_thread_name_set(&btstack_thread_data, "btstack");
}

static void nrf_transport_task(void)
{
    // btstack_host_process() and bthid_task() run inside the BTstack thread
    // via process_timer_handler, not here.
}

static bool nrf_transport_is_ready(void)
{
    // In peripheral mode (post_init_callback set), nrf_initialized is sufficient.
    // In central mode, also check btstack_host_is_powered_on().
    if (post_init_callback) {
        return nrf_initialized;
    }
    return nrf_initialized && btstack_host_is_powered_on();
}

static uint8_t nrf_transport_get_connection_count(void)
{
    return btstack_classic_get_connection_count();
}

static const bt_connection_t* nrf_transport_get_connection(uint8_t index)
{
    if (index >= BT_MAX_CONNECTIONS) {
        return NULL;
    }

    btstack_classic_conn_info_t info;
    if (!btstack_classic_get_connection(index, &info)) {
        return NULL;
    }

    bt_connection_t* conn = &nrf_connections[index];
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

static bool nrf_transport_send_control(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    if (len >= 2) {
        uint8_t header = data[0];
        uint8_t report_type = header & 0x03;
        uint8_t report_id = data[1];
        return btstack_classic_send_set_report_type(conn_index, report_type, report_id, data + 2, len - 2);
    }
    return false;
}

static bool nrf_transport_send_interrupt(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    if (len >= 2) {
        uint8_t report_id = data[1];
        return btstack_classic_send_report(conn_index, report_id, data + 2, len - 2);
    }
    return false;
}

static void nrf_transport_disconnect(uint8_t conn_index)
{
    (void)conn_index;
}

static void nrf_transport_set_pairing_mode(bool enable)
{
    if (enable) {
        btstack_host_start_scan();
    } else {
        btstack_host_stop_scan();
    }
}

static bool nrf_transport_is_pairing_mode(void)
{
    return btstack_host_is_scanning();
}

// ============================================================================
// TRANSPORT STRUCT
// ============================================================================

const bt_transport_t bt_transport_nrf = {
    .name = "XIAO nRF52840 BLE",
    .init = nrf_transport_init,
    .task = nrf_transport_task,
    .is_ready = nrf_transport_is_ready,
    .get_connection_count = nrf_transport_get_connection_count,
    .get_connection = nrf_transport_get_connection,
    .send_control = nrf_transport_send_control,
    .send_interrupt = nrf_transport_send_interrupt,
    .disconnect = nrf_transport_disconnect,
    .set_pairing_mode = nrf_transport_set_pairing_mode,
    .is_pairing_mode = nrf_transport_is_pairing_mode,
};
