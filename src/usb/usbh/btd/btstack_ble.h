// btstack_ble.h - BTstack-based BLE handler for HID over GATT
//
// Provides a clean interface for BLE HID devices using BTstack's
// Security Manager (for LE Secure Connections) and GATT client.

#ifndef BTSTACK_BLE_H
#define BTSTACK_BLE_H

#include <stdint.h>
#include <stdbool.h>
#include "bluetooth.h"

#ifdef __cplusplus
extern "C" {
#endif

// Callback types
typedef void (*btstack_ble_report_callback_t)(uint16_t handle, const uint8_t *report, uint16_t len);
typedef void (*btstack_ble_connect_callback_t)(uint16_t handle, bool connected);

// Initialize BTstack BLE subsystem
void btstack_ble_init(void);

// Power on the Bluetooth controller
void btstack_ble_power_on(void);

// Start/stop BLE scanning
void btstack_ble_start_scan(void);
void btstack_ble_stop_scan(void);

// Connect to a BLE device
void btstack_ble_connect(bd_addr_t addr, bd_addr_type_t addr_type);

// Register callbacks
void btstack_ble_register_report_callback(btstack_ble_report_callback_t callback);
void btstack_ble_register_connect_callback(btstack_ble_connect_callback_t callback);

// Must be called from main loop
void btstack_ble_process(void);

// Status queries
bool btstack_ble_is_initialized(void);
bool btstack_ble_is_powered_on(void);
bool btstack_ble_is_scanning(void);

// Classic BT connection info (for bthid driver matching)
typedef struct {
    bool active;
    uint8_t bd_addr[6];
    char name[32];
    uint8_t class_of_device[3];
    bool hid_ready;
} btstack_classic_conn_info_t;

bool btstack_classic_get_connection(uint8_t conn_index, btstack_classic_conn_info_t* info);
uint8_t btstack_classic_get_connection_count(void);

// Classic BT output (for bthid drivers)
bool btstack_classic_send_report(uint8_t conn_index, uint8_t report_id,
                                  const uint8_t* data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif // BTSTACK_BLE_H
