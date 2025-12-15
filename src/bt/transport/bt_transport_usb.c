// bt_transport_usb.c - USB Bluetooth Dongle Transport
// Implements bt_transport_t using BTstack

#include "bt_transport.h"
#include "bt/bthid/bthid.h"
#include "usb/usbh/btd/btstack_ble.h"
#include <string.h>
#include <stdio.h>

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
    printf("[BT_USB] Transport init (BTstack)\n");
    btstack_ble_init();
    printf("[BT_USB] BTstack initialized, waiting for dongle...\n");
}

static uint32_t task_counter = 0;

static void usb_transport_task(void)
{
    task_counter++;
    if (task_counter == 1) {
        printf("[BT_USB] task started (BTstack)\n");
    }
    btstack_ble_process();
    bthid_task();  // Run BT HID device driver tasks
}

static bool usb_transport_is_ready(void)
{
    return btstack_ble_is_powered_on();
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
    conn->connected = info.active;
    conn->hid_ready = info.hid_ready;

    return conn;
}

static bool usb_transport_send_control(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    // Classic BT: parse SET_REPORT header and forward to BTstack
    if (len >= 2) {
        // data[0] = transaction type | report type, data[1] = report_id
        uint8_t report_id = data[1];
        return btstack_classic_send_report(conn_index, report_id, data + 2, len - 2);
    }
    return false;
}

static bool usb_transport_send_interrupt(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
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
    // TODO: Implement disconnect in btstack_ble.c
    (void)conn_index;
}

static void usb_transport_set_pairing_mode(bool enable)
{
    if (enable) {
        btstack_ble_start_scan();
    } else {
        btstack_ble_stop_scan();
    }
}

static bool usb_transport_is_pairing_mode(void)
{
    return btstack_ble_is_scanning();
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
