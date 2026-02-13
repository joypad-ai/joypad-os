// btstack_host.h - BTstack HID Host (BLE + Classic)
//
// Transport-agnostic BTstack integration for HID devices.
// Supports both BLE HID over GATT and Classic BT HID.
//
// Usage:
//   btstack_host_init(hci_transport);  // Pass HCI transport (USB dongle or CYW43)
//   btstack_host_process();            // Call from main loop

#ifndef BTSTACK_HOST_H
#define BTSTACK_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "bluetooth.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// INITIALIZATION
// ============================================================================

// Initialize BTstack with specified HCI transport
// For USB dongle: pass hci_transport_h2_tinyusb_instance()
// For Pico W: pass hci_transport_cyw43_instance()
// Note: transport is actually hci_transport_t* but we use void* to avoid header conflicts
void btstack_host_init(const void* transport);

// Initialize only the HID handlers (callbacks, state) without BTstack init
// Use this when BTstack was already initialized externally (e.g., btstack_cyw43_init)
void btstack_host_init_hid_handlers(void);

// Power on the Bluetooth controller
void btstack_host_power_on(void);

// ============================================================================
// SCANNING / PAIRING
// ============================================================================

// Start scanning for BLE and Classic BT devices
void btstack_host_start_scan(void);

// Stop scanning
void btstack_host_stop_scan(void);

// Start scanning with a timeout (auto-stops after timeout_ms)
void btstack_host_start_timed_scan(uint32_t timeout_ms);

// Connect to a BLE device
void btstack_host_connect_ble(bd_addr_t addr, bd_addr_type_t addr_type);

// ============================================================================
// CALLBACKS
// ============================================================================

typedef void (*btstack_host_report_callback_t)(uint16_t handle, const uint8_t *report, uint16_t len);
typedef void (*btstack_host_connect_callback_t)(uint16_t handle, bool connected);

void btstack_host_register_report_callback(btstack_host_report_callback_t callback);
void btstack_host_register_connect_callback(btstack_host_connect_callback_t callback);

// ============================================================================
// MAIN LOOP
// ============================================================================

// Process BTstack events - call from main loop
void btstack_host_process(void);

// ============================================================================
// STATUS
// ============================================================================

bool btstack_host_is_initialized(void);
bool btstack_host_is_powered_on(void);
bool btstack_host_is_scanning(void);

// ============================================================================
// CLASSIC BT CONNECTION INFO (for bthid driver matching)
// ============================================================================

typedef struct {
    bool active;
    uint8_t bd_addr[6];
    char name[32];
    uint8_t class_of_device[3];
    uint16_t vendor_id;
    uint16_t product_id;
    bool hid_ready;
    bool is_ble;
} btstack_classic_conn_info_t;

bool btstack_classic_get_connection(uint8_t conn_index, btstack_classic_conn_info_t* info);
uint8_t btstack_classic_get_connection_count(void);

// Classic BT output (for bthid drivers)
bool btstack_classic_send_set_report_type(uint8_t conn_index, uint8_t report_type,
                                           uint8_t report_id, const uint8_t* data, uint16_t len);
bool btstack_classic_send_set_report(uint8_t conn_index, uint8_t report_id,
                                      const uint8_t* data, uint16_t len);
bool btstack_classic_send_report(uint8_t conn_index, uint8_t report_id,
                                  const uint8_t* data, uint16_t len);

// Wiimote raw L2CAP output (Wiimotes use raw protocol, not HID transport headers)
bool btstack_wiimote_is_connection(uint8_t conn_index);
bool btstack_wiimote_can_send(uint8_t conn_index);
bool btstack_wiimote_send_raw(uint8_t conn_index, const uint8_t* data, uint16_t len);
bool btstack_wiimote_send_control(uint8_t conn_index, const uint8_t* data, uint16_t len);

// ============================================================================
// BOND MANAGEMENT
// ============================================================================

// Disconnect all active BT devices (Classic and BLE)
void btstack_host_disconnect_all_devices(void);

// Delete all stored BT bonds (Classic and BLE)
// Devices will need to re-pair after this
void btstack_host_delete_all_bonds(void);

#ifdef __cplusplus
}
#endif

#endif // BTSTACK_HOST_H
