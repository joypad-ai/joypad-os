// bt_transport_usb.c - USB Bluetooth Dongle Transport
// Implements bt_transport_t using BTstack with USB HCI transport

#include "bt_transport.h"
#include "bt/bthid/bthid.h"
#include "bt/btstack/btstack_host.h"
#include <string.h>
#include <stdio.h>

// USB HCI transport (TinyUSB-based)
// Forward declare to avoid header conflicts
const void* hci_transport_h2_tinyusb_instance(void);
void hci_transport_h2_tinyusb_process(void);

// ============================================================================
// USB TRANSPORT PROCESS (called by btstack_host_process)
// ============================================================================

// Override weak function in btstack_hid.c to process USB transport
void btstack_host_transport_process(void)
{
    hci_transport_h2_tinyusb_process();
}

// ============================================================================
// STATIC DATA
// ============================================================================

static bt_connection_t usb_connections[BT_MAX_CONNECTIONS];

// ============================================================================
// TRANSPORT IMPLEMENTATION
// ============================================================================

static void usb_transport_init(void)
{
    memset(usb_connections, 0, sizeof(usb_connections));
    printf("[BT_USB] Transport init (BTstack + USB HCI)\n");

    // Initialize BTstack with USB HCI transport
    btstack_host_init(hci_transport_h2_tinyusb_instance());

    printf("[BT_USB] BTstack initialized, waiting for dongle...\n");
}

static uint32_t task_counter = 0;

static void usb_transport_task(void)
{
    task_counter++;
    if (task_counter == 1) {
        printf("[BT_USB] task started (BTstack)\n");
    }
    btstack_host_process();
    bthid_task();  // Run BT HID device driver tasks
}

static bool usb_transport_is_ready(void)
{
    return btstack_host_is_powered_on();
}

static uint8_t usb_transport_get_connection_count(void)
{
    return btstack_classic_get_connection_count();
}

static const bt_connection_t* usb_transport_get_connection(uint8_t index)
{
    if (index >= BT_MAX_CONNECTIONS) {
        return NULL;
    }

    btstack_classic_conn_info_t info;
    if (!btstack_classic_get_connection(index, &info)) {
        return NULL;
    }

    // Update cached connection struct
    bt_connection_t* conn = &usb_connections[index];
    memcpy(conn->bd_addr, info.bd_addr, 6);
    strncpy(conn->name, info.name, BT_MAX_NAME_LEN - 1);
    conn->name[BT_MAX_NAME_LEN - 1] = '\0';
    memcpy(conn->class_of_device, info.class_of_device, 3);
    conn->vendor_id = info.vendor_id;
    conn->product_id = info.product_id;
    conn->connected = info.active;
    conn->hid_ready = info.hid_ready;

    return conn;
}

static bool usb_transport_send_control(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    // Classic BT: parse SET_REPORT header and forward to BTstack
    // DS3 and others use SET_REPORT on control channel
    if (len >= 2) {
        // data[0] = transaction type | report type
        // 0x52 = SET_REPORT | Output (0x50 | 0x02)
        // 0x53 = SET_REPORT | Feature (0x50 | 0x03)
        uint8_t header = data[0];
        uint8_t report_type = header & 0x03;  // Lower 2 bits = report type
        uint8_t report_id = data[1];
        return btstack_classic_send_set_report_type(conn_index, report_type, report_id, data + 2, len - 2);
    }
    return false;
}

static bool usb_transport_send_interrupt(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    // Wiimotes use raw L2CAP data (report ID as first byte, no HID transport headers)
    if (btstack_wiimote_is_connection(conn_index)) {
        return btstack_wiimote_send_raw(conn_index, data, len);
    }

    // Classic BT: parse DATA|OUTPUT header and forward to BTstack
    if (len >= 2) {
        // data[0] = 0xA2 (DATA|OUTPUT), data[1] = report_id
        uint8_t report_id = data[1];
        return btstack_classic_send_report(conn_index, report_id, data + 2, len - 2);
    }
    return false;
}

static void usb_transport_disconnect(uint8_t conn_index)
{
    // TODO: Implement disconnect in btstack_hid.c
    (void)conn_index;
}

static void usb_transport_set_pairing_mode(bool enable)
{
    if (enable) {
        btstack_host_start_scan();
    } else {
        btstack_host_stop_scan();
    }
}

static bool usb_transport_is_pairing_mode(void)
{
    return btstack_host_is_scanning();
}

// ============================================================================
// TRANSPORT STRUCT
// ============================================================================

const bt_transport_t bt_transport_usb = {
    .name = "USB Dongle",
    .init = usb_transport_init,
    .task = usb_transport_task,
    .is_ready = usb_transport_is_ready,
    .get_connection_count = usb_transport_get_connection_count,
    .get_connection = usb_transport_get_connection,
    .send_control = usb_transport_send_control,
    .send_interrupt = usb_transport_send_interrupt,
    .disconnect = usb_transport_disconnect,
    .set_pairing_mode = usb_transport_set_pairing_mode,
    .is_pairing_mode = usb_transport_is_pairing_mode,
};
