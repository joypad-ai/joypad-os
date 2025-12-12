// btd.h - Bluetooth Dongle Driver
// USB Bluetooth dongle HCI layer for TinyUSB host
//
// Reference: USB_Host_Shield_2.0 BTD.cpp

#ifndef BTD_H
#define BTD_H

#include <stdint.h>
#include <stdbool.h>
#include "hci_cmd.h"
#include "hci_event.h"

// ============================================================================
// USB BLUETOOTH DONGLE IDENTIFICATION
// ============================================================================

// USB Class/Subclass/Protocol for Bluetooth
#define USB_CLASS_WIRELESS_CTRL     0xE0
#define USB_SUBCLASS_RF             0x01
#define USB_PROTOCOL_BLUETOOTH      0x01

// ============================================================================
// BUFFER SIZES
// ============================================================================

#define BTD_HCI_CMD_BUF_SIZE        256     // HCI command buffer (must fit Write_Local_Name: 3+248)
#define BTD_HCI_EVT_BUF_SIZE        64      // HCI event buffer
#define BTD_ACL_BUF_SIZE            256     // ACL data buffer
#define BTD_MAX_NAME_LEN            32      // Max device name length

// ============================================================================
// CONNECTION LIMITS
// ============================================================================

#define BTD_MAX_CONNECTIONS         4       // Max simultaneous BT connections

// ============================================================================
// BTD STATE MACHINE
// ============================================================================

typedef enum {
    BTD_STATE_INIT = 0,         // Initial state
    BTD_STATE_RESET,            // Waiting for reset complete
    BTD_STATE_READ_BD_ADDR,     // Reading local BD_ADDR
    BTD_STATE_READ_VERSION,     // Reading local version info
    BTD_STATE_READ_BUFFER_SIZE, // Reading buffer sizes
    BTD_STATE_SET_EVENT_MASK,   // Set event mask (enable connection events)
    BTD_STATE_WRITE_NAME,       // Writing local name
    BTD_STATE_WRITE_COD,        // Writing class of device
    BTD_STATE_WRITE_SSP,        // Enable Simple Pairing
    BTD_STATE_WRITE_SCAN,       // Enable scan mode
    // BLE (Bluetooth Low Energy) initialization states
    BTD_STATE_LE_WRITE_HOST_SUPPORT, // Enable LE host support
    BTD_STATE_LE_READ_BUFFER_SIZE,   // Read LE buffer sizes
    BTD_STATE_LE_SET_EVENT_MASK,     // Set LE event mask
    BTD_STATE_LE_SET_SCAN_PARAMS,    // Set LE scan parameters
    BTD_STATE_LE_SCAN_ENABLE,        // Enable LE scanning
    BTD_STATE_INQUIRY,          // Scanning for nearby devices (Classic)
    BTD_STATE_RUNNING,          // Ready for connections
    BTD_STATE_ERROR,            // Error state
} btd_state_t;

// ============================================================================
// CONNECTION STATE
// ============================================================================

typedef enum {
    BTD_CONN_DISCONNECTED = 0,
    BTD_CONN_CONNECTING,
    BTD_CONN_CONNECTED,
    BTD_CONN_L2CAP_SETUP,
    BTD_CONN_HID_READY,
} btd_conn_state_t;

// Connection info
typedef struct {
    btd_conn_state_t state;
    uint8_t  bd_addr[6];        // Remote device address
    uint16_t handle;            // ACL connection handle
    uint8_t  class_of_device[3];// Remote class of device
    char     name[BTD_MAX_NAME_LEN];  // Remote device name
    bool     is_ble;            // true = BLE connection, false = Classic BR/EDR

    // L2CAP channels (Classic BR/EDR) or ATT (BLE)
    uint16_t control_cid;       // HID Control channel (local CID) / ATT CID for BLE
    uint16_t control_dcid;      // HID Control channel (remote CID)
    uint16_t interrupt_cid;     // HID Interrupt channel (local CID)
    uint16_t interrupt_dcid;    // HID Interrupt channel (remote CID)
} btd_connection_t;

// ============================================================================
// BTD CONTEXT
// ============================================================================

typedef struct {
    // USB device info
    uint8_t  dev_addr;          // TinyUSB device address
    uint8_t  itf_num;           // Interface number
    uint8_t  ep_evt;            // Interrupt IN endpoint (HCI events)
    uint8_t  ep_acl_in;         // Bulk IN endpoint (ACL data)
    uint8_t  ep_acl_out;        // Bulk OUT endpoint (ACL data)

    // Local device info
    uint8_t  bd_addr[6];        // Local Bluetooth address
    uint8_t  hci_version;       // HCI version
    uint16_t manufacturer;      // Manufacturer code
    uint16_t acl_mtu;           // ACL packet MTU
    uint16_t acl_credits;       // Available ACL packet credits

    // State machine
    btd_state_t state;
    uint8_t  pending_cmd;       // Waiting for command complete
    uint16_t pending_opcode;    // Opcode we're waiting for

    // Connections
    btd_connection_t connections[BTD_MAX_CONNECTIONS];
    uint8_t  num_connections;

    // Buffers
    uint8_t  cmd_buf[BTD_HCI_CMD_BUF_SIZE];
    uint8_t  evt_buf[BTD_HCI_EVT_BUF_SIZE];
    uint8_t  acl_in_buf[BTD_ACL_BUF_SIZE];
    uint8_t  acl_out_buf[BTD_ACL_BUF_SIZE];

    // Flags
    bool     dongle_connected;
    bool     scan_enabled;
    bool     pairing_mode;
    bool     evt_pending;       // Event endpoint transfer is pending
    bool     acl_out_pending;   // ACL OUT transfer pending
    uint16_t acl_out_pending_len; // Length of pending ACL data

    // BLE (Bluetooth Low Energy) support
    bool     le_supported;      // Dongle supports BLE (Bluetooth 4.0+)
    bool     le_enabled;        // BLE has been enabled
    bool     le_scan_enabled;   // BLE scanning is active
    bool     le_connecting;     // BLE connection in progress
    bool     le_connect_pending; // Waiting to connect after scan stops
    uint8_t  le_pending_addr[6]; // Address to connect to
    uint8_t  le_pending_addr_type; // Address type to connect to
    uint16_t le_acl_mtu;        // LE ACL packet MTU
    uint8_t  le_acl_credits;    // Available LE ACL packet credits
} btd_t;

// ============================================================================
// BTD API
// ============================================================================

// Initialization
void btd_init(void);

// Task - call from main loop
void btd_task(void);

// Check if dongle is connected and ready
bool btd_is_ready(void);

// Get number of connected controllers
uint8_t btd_get_connection_count(void);

// Get connection info by index
const btd_connection_t* btd_get_connection(uint8_t index);

// Enable/disable pairing mode
void btd_set_pairing_mode(bool enable);

// Check if in pairing mode
bool btd_is_pairing_mode(void);

// Disconnect a device
void btd_disconnect(uint8_t index);

// ============================================================================
// HCI COMMAND FUNCTIONS
// ============================================================================

// Low-level HCI command send
bool btd_send_hci_cmd(uint16_t opcode, const uint8_t* params, uint8_t param_len);

// Specific HCI commands
bool btd_hci_reset(void);
bool btd_hci_read_bd_addr(void);
bool btd_hci_read_local_version(void);
bool btd_hci_read_buffer_size(void);
bool btd_hci_set_event_mask(void);
bool btd_hci_inquiry(void);
bool btd_hci_inquiry_cancel(void);
bool btd_hci_create_connection(const uint8_t* bd_addr, uint8_t page_scan_rep_mode, uint16_t clock_offset);
bool btd_hci_write_local_name(const char* name);
bool btd_hci_write_class_of_device(uint32_t cod);
bool btd_hci_write_scan_enable(uint8_t mode);
bool btd_hci_write_simple_pairing_mode(bool enable);
bool btd_hci_accept_connection(const uint8_t* bd_addr, uint8_t role);
bool btd_hci_reject_connection(const uint8_t* bd_addr, uint8_t reason);
bool btd_hci_disconnect(uint16_t handle, uint8_t reason);
bool btd_hci_pin_code_reply(const uint8_t* bd_addr, const char* pin, uint8_t pin_len);
bool btd_hci_pin_code_neg_reply(const uint8_t* bd_addr);
bool btd_hci_link_key_reply(const uint8_t* bd_addr, const uint8_t* link_key);
bool btd_hci_link_key_neg_reply(const uint8_t* bd_addr);
bool btd_hci_user_confirm_reply(const uint8_t* bd_addr);
bool btd_hci_io_capability_reply(const uint8_t* bd_addr);
bool btd_hci_authentication_requested(uint16_t handle);
bool btd_hci_set_connection_encryption(uint16_t handle, bool enable);
bool btd_hci_remote_name_request(const uint8_t* bd_addr);

// ============================================================================
// HCI LE COMMAND FUNCTIONS (Bluetooth Low Energy)
// ============================================================================

// LE Setup
bool btd_hci_write_le_host_support(bool enable);
bool btd_hci_le_read_buffer_size(void);
bool btd_hci_le_set_event_mask(void);

// LE Scanning
bool btd_hci_le_set_scan_parameters(uint8_t scan_type, uint16_t interval,
                                     uint16_t window, bool filter_duplicates);
bool btd_hci_le_set_scan_enable(bool enable, bool filter_duplicates);

// LE Connection
bool btd_hci_le_create_connection(const uint8_t* peer_addr, uint8_t peer_addr_type);
bool btd_hci_le_create_connection_cancel(void);
bool btd_hci_le_connection_update(uint16_t handle, uint16_t interval_min,
                                   uint16_t interval_max, uint16_t latency,
                                   uint16_t timeout);

// LE Encryption
bool btd_hci_le_start_encryption(uint16_t handle, const uint8_t* random,
                                  uint16_t ediv, const uint8_t* ltk);
bool btd_hci_le_ltk_reply(uint16_t handle, const uint8_t* ltk);
bool btd_hci_le_ltk_neg_reply(uint16_t handle);

// LE Extended Scanning (Bluetooth 5.0+)
bool btd_hci_le_set_ext_scan_parameters(uint8_t scan_type, uint16_t interval, uint16_t window);
bool btd_hci_le_set_ext_scan_enable(bool enable, bool filter_duplicates);

// ============================================================================
// ACL DATA FUNCTIONS
// ============================================================================

// Send ACL data packet
bool btd_send_acl_data(uint16_t handle, uint8_t pb_flag, uint8_t bc_flag,
                       const uint8_t* data, uint16_t len);

// ============================================================================
// CALLBACKS (implemented by higher layers)
// ============================================================================

// Called when a new connection is established
extern void btd_on_connection(uint8_t conn_index);

// Called when authentication completes
extern void btd_on_auth_complete(uint8_t conn_index, uint8_t status);

// Called when encryption changes
extern void btd_on_encryption_change(uint8_t conn_index, uint8_t status, bool enabled);

// Called when a connection is disconnected
extern void btd_on_disconnection(uint8_t conn_index);

// Called when ACL data is received
extern void btd_on_acl_data(uint8_t conn_index, const uint8_t* data, uint16_t len);

// Called when remote name request completes
extern void btd_on_remote_name_complete(uint8_t conn_index, const char* name);

// LE callbacks
// Called when a BLE advertisement is received during scanning
extern void btd_on_le_adv_report(const uint8_t* addr, uint8_t addr_type,
                                  int8_t rssi, const uint8_t* data, uint8_t data_len);

// Called when a BLE connection is established
extern void btd_on_le_connection(uint8_t conn_index);

// ============================================================================
// TINYUSB INTEGRATION
// ============================================================================

#include "tusb.h"
#include "host/usbh_pvt.h"

// TinyUSB class driver struct (register with usbh_app_driver_get_cb)
extern const usbh_class_driver_t usbh_btd_driver;

// TinyUSB class driver callbacks (called by TinyUSB)
bool btd_driver_init(void);
bool btd_driver_deinit(void);
bool btd_driver_open(uint8_t rhport, uint8_t dev_addr,
                     tusb_desc_interface_t const* desc_itf, uint16_t max_len);
bool btd_driver_set_config(uint8_t dev_addr, uint8_t itf_num);
bool btd_driver_xfer_cb(uint8_t dev_addr, uint8_t ep_addr,
                        xfer_result_t result, uint32_t xferred_bytes);
void btd_driver_close(uint8_t dev_addr);

// ============================================================================
// DEBUG / UTILITY
// ============================================================================

// Get local Bluetooth address (dongle's MAC)
// Returns pointer to 6-byte BD_ADDR, or NULL if BT not initialized
const uint8_t* btd_get_local_bd_addr(void);

// Check if BT dongle is connected and initialized
bool btd_is_available(void);

// Convert BD_ADDR to string "XX:XX:XX:XX:XX:XX"
void btd_bd_addr_to_str(const uint8_t* bd_addr, char* str);

// Print BTD state (debug)
void btd_print_state(void);

#endif // BTD_H
