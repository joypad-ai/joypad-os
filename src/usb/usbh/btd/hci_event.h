// hci_event.h - HCI Event Definitions
// Bluetooth Host Controller Interface event codes and structures
//
// Reference: Bluetooth Core Specification v5.3, Vol 4, Part E

#ifndef HCI_EVENT_H
#define HCI_EVENT_H

#include <stdint.h>

// ============================================================================
// HCI EVENT CODES
// ============================================================================

#define HCI_EVENT_INQUIRY_COMPLETE              0x01
#define HCI_EVENT_INQUIRY_RESULT                0x02
#define HCI_EVENT_CONNECTION_COMPLETE           0x03
#define HCI_EVENT_CONNECTION_REQUEST            0x04
#define HCI_EVENT_DISCONNECTION_COMPLETE        0x05
#define HCI_EVENT_AUTH_COMPLETE                 0x06
#define HCI_EVENT_REMOTE_NAME_COMPLETE          0x07
#define HCI_EVENT_ENCRYPT_CHANGE                0x08
#define HCI_EVENT_CHANGE_CONN_LINK_KEY_COMPLETE 0x09
#define HCI_EVENT_READ_REMOTE_FEATURES_COMPLETE 0x0B
#define HCI_EVENT_READ_REMOTE_VERSION_COMPLETE  0x0C
#define HCI_EVENT_QOS_SETUP_COMPLETE            0x0D
#define HCI_EVENT_COMMAND_COMPLETE              0x0E
#define HCI_EVENT_COMMAND_STATUS                0x0F
#define HCI_EVENT_HARDWARE_ERROR                0x10
#define HCI_EVENT_ROLE_CHANGE                   0x12
#define HCI_EVENT_NUMBER_OF_COMPLETED_PACKETS   0x13
#define HCI_EVENT_MODE_CHANGE                   0x14
#define HCI_EVENT_PIN_CODE_REQUEST              0x16
#define HCI_EVENT_LINK_KEY_REQUEST              0x17
#define HCI_EVENT_LINK_KEY_NOTIFICATION         0x18
#define HCI_EVENT_DATA_BUFFER_OVERFLOW          0x1A
#define HCI_EVENT_MAX_SLOTS_CHANGE              0x1B
#define HCI_EVENT_PAGE_SCAN_REP_MODE_CHANGE     0x20
#define HCI_EVENT_INQUIRY_RESULT_WITH_RSSI      0x22
#define HCI_EVENT_READ_REMOTE_EXT_FEATURES      0x23
#define HCI_EVENT_SYNC_CONNECTION_COMPLETE      0x2C
#define HCI_EVENT_EXTENDED_INQUIRY_RESULT       0x2F
#define HCI_EVENT_IO_CAPABILITY_REQUEST         0x31
#define HCI_EVENT_IO_CAPABILITY_RESPONSE        0x32
#define HCI_EVENT_USER_CONFIRM_REQUEST          0x33
#define HCI_EVENT_USER_PASSKEY_REQUEST          0x34
#define HCI_EVENT_SIMPLE_PAIRING_COMPLETE       0x36
#define HCI_EVENT_LINK_SUPERVISION_TIMEOUT      0x38

// LE Meta Event
#define HCI_EVENT_LE_META                       0x3E

// ============================================================================
// HCI ERROR CODES
// ============================================================================

#define HCI_SUCCESS                             0x00
#define HCI_ERR_UNKNOWN_COMMAND                 0x01
#define HCI_ERR_UNKNOWN_CONNECTION              0x02
#define HCI_ERR_HARDWARE_FAILURE                0x03
#define HCI_ERR_PAGE_TIMEOUT                    0x04
#define HCI_ERR_AUTH_FAILURE                    0x05
#define HCI_ERR_PIN_OR_KEY_MISSING              0x06
#define HCI_ERR_MEMORY_EXCEEDED                 0x07
#define HCI_ERR_CONNECTION_TIMEOUT              0x08
#define HCI_ERR_CONNECTION_LIMIT_EXCEEDED       0x09
#define HCI_ERR_ACL_CONNECTION_EXISTS           0x0B
#define HCI_ERR_COMMAND_DISALLOWED              0x0C
#define HCI_ERR_REJECTED_LIMITED_RESOURCES      0x0D
#define HCI_ERR_REJECTED_SECURITY               0x0E
#define HCI_ERR_REJECTED_PERSONAL_DEVICE        0x0F
#define HCI_ERR_HOST_TIMEOUT                    0x10
#define HCI_ERR_UNSUPPORTED_FEATURE             0x11
#define HCI_ERR_INVALID_PARAMETERS              0x12
#define HCI_ERR_REMOTE_USER_TERMINATED          0x13
#define HCI_ERR_REMOTE_LOW_RESOURCES            0x14
#define HCI_ERR_REMOTE_POWER_OFF                0x15
#define HCI_ERR_LOCAL_HOST_TERMINATED           0x16
#define HCI_ERR_REPEATED_ATTEMPTS               0x17
#define HCI_ERR_PAIRING_NOT_ALLOWED             0x18
#define HCI_ERR_UNSUPPORTED_REMOTE_FEATURE      0x1A
#define HCI_ERR_UNSPECIFIED_ERROR               0x1F
#define HCI_ERR_ROLE_CHANGE_NOT_ALLOWED         0x21
#define HCI_ERR_LMP_RESPONSE_TIMEOUT            0x22
#define HCI_ERR_INSTANT_PASSED                  0x28
#define HCI_ERR_PAIRING_WITH_UNIT_KEY           0x29
#define HCI_ERR_CHANNEL_CLASSIFICATION          0x2E
#define HCI_ERR_CONTROLLER_BUSY                 0x3A
#define HCI_ERR_CONN_INTERVAL_UNACCEPTABLE      0x3B
#define HCI_ERR_ADVERTISING_TIMEOUT             0x3C
#define HCI_ERR_CONN_TERMINATED_MIC_FAILURE     0x3D
#define HCI_ERR_CONN_FAILED_TO_ESTABLISH        0x3E

// ============================================================================
// DISCONNECT REASON CODES
// ============================================================================

#define HCI_DISCONNECT_AUTH_FAILURE             0x05
#define HCI_DISCONNECT_REMOTE_USER              0x13
#define HCI_DISCONNECT_REMOTE_LOW_RESOURCES     0x14
#define HCI_DISCONNECT_REMOTE_POWER_OFF         0x15
#define HCI_DISCONNECT_LOCAL_HOST               0x16
#define HCI_DISCONNECT_REPEATED_ATTEMPTS        0x17

// ============================================================================
// LINK TYPES
// ============================================================================

#define HCI_LINK_TYPE_SCO                       0x00
#define HCI_LINK_TYPE_ACL                       0x01
#define HCI_LINK_TYPE_ESCO                      0x02

// ============================================================================
// HCI EVENT STRUCTURES
// ============================================================================

// Generic HCI event header
typedef struct __attribute__((packed)) {
    uint8_t event_code;
    uint8_t param_len;
    uint8_t params[];
} hci_event_t;

// Command Complete event
typedef struct __attribute__((packed)) {
    uint8_t  num_hci_cmd_packets;
    uint16_t opcode;
    uint8_t  return_params[];
} hci_event_cmd_complete_t;

// Command Status event
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint8_t  num_hci_cmd_packets;
    uint16_t opcode;
} hci_event_cmd_status_t;

// Connection Complete event
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint16_t handle;
    uint8_t  bd_addr[6];
    uint8_t  link_type;
    uint8_t  encryption_enabled;
} hci_event_conn_complete_t;

// Connection Request event
typedef struct __attribute__((packed)) {
    uint8_t bd_addr[6];
    uint8_t class_of_device[3];
    uint8_t link_type;
} hci_event_conn_request_t;

// Disconnection Complete event
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint16_t handle;
    uint8_t  reason;
} hci_event_disconn_complete_t;

// PIN Code Request event
typedef struct __attribute__((packed)) {
    uint8_t bd_addr[6];
} hci_event_pin_code_request_t;

// Link Key Request event
typedef struct __attribute__((packed)) {
    uint8_t bd_addr[6];
} hci_event_link_key_request_t;

// Link Key Notification event
typedef struct __attribute__((packed)) {
    uint8_t bd_addr[6];
    uint8_t link_key[16];
    uint8_t key_type;
} hci_event_link_key_notification_t;

// IO Capability Request event
typedef struct __attribute__((packed)) {
    uint8_t bd_addr[6];
} hci_event_io_cap_request_t;

// IO Capability Response event
typedef struct __attribute__((packed)) {
    uint8_t bd_addr[6];
    uint8_t io_capability;
    uint8_t oob_data_present;
    uint8_t auth_requirements;
} hci_event_io_cap_response_t;

// User Confirmation Request event
typedef struct __attribute__((packed)) {
    uint8_t  bd_addr[6];
    uint32_t numeric_value;
} hci_event_user_confirm_request_t;

// Simple Pairing Complete event
typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t bd_addr[6];
} hci_event_simple_pairing_complete_t;

// Remote Name Request Complete event
typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t bd_addr[6];
    char    remote_name[248];
} hci_event_remote_name_complete_t;

// Inquiry Result event (per device)
typedef struct __attribute__((packed)) {
    uint8_t  bd_addr[6];
    uint8_t  page_scan_rep_mode;
    uint8_t  reserved1;
    uint8_t  reserved2;
    uint8_t  class_of_device[3];
    uint16_t clock_offset;
} hci_inquiry_result_t;

// Inquiry Result with RSSI event (per device)
typedef struct __attribute__((packed)) {
    uint8_t  bd_addr[6];
    uint8_t  page_scan_rep_mode;
    uint8_t  reserved;
    uint8_t  class_of_device[3];
    uint16_t clock_offset;
    int8_t   rssi;
} hci_inquiry_result_rssi_t;

// Extended Inquiry Result event
typedef struct __attribute__((packed)) {
    uint8_t  num_responses;  // Always 1 for EIR
    uint8_t  bd_addr[6];
    uint8_t  page_scan_rep_mode;
    uint8_t  reserved;
    uint8_t  class_of_device[3];
    uint16_t clock_offset;
    int8_t   rssi;
    uint8_t  eir_data[240];
} hci_event_extended_inquiry_result_t;

// Number of Completed Packets event
typedef struct __attribute__((packed)) {
    uint8_t  num_handles;
    // Followed by: handle[num_handles], num_completed[num_handles]
} hci_event_num_completed_packets_t;

// Read BD_ADDR complete return params
typedef struct __attribute__((packed)) {
    uint8_t status;
    uint8_t bd_addr[6];
} hci_return_read_bd_addr_t;

// Read Local Version complete return params
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint8_t  hci_version;
    uint16_t hci_revision;
    uint8_t  lmp_version;
    uint16_t manufacturer;
    uint16_t lmp_subversion;
} hci_return_read_local_version_t;

// Read Buffer Size complete return params
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint16_t acl_data_packet_length;
    uint8_t  sco_data_packet_length;
    uint16_t total_num_acl_packets;
    uint16_t total_num_sco_packets;
} hci_return_read_buffer_size_t;

// ============================================================================
// LE META EVENT SUB-EVENTS
// ============================================================================

#define HCI_LE_EVT_CONNECTION_COMPLETE          0x01
#define HCI_LE_EVT_ADVERTISING_REPORT           0x02
#define HCI_LE_EVT_CONNECTION_UPDATE_COMPLETE   0x03
#define HCI_LE_EVT_READ_REMOTE_FEATURES         0x04
#define HCI_LE_EVT_LONG_TERM_KEY_REQUEST        0x05
#define HCI_LE_EVT_REMOTE_CONN_PARAM_REQUEST    0x06
#define HCI_LE_EVT_DATA_LENGTH_CHANGE           0x07
#define HCI_LE_EVT_READ_LOCAL_P256_PUB_KEY      0x08
#define HCI_LE_EVT_GENERATE_DHKEY_COMPLETE      0x09
#define HCI_LE_EVT_ENHANCED_CONN_COMPLETE       0x0A
#define HCI_LE_EVT_DIRECTED_ADV_REPORT          0x0B
#define HCI_LE_EVT_PHY_UPDATE_COMPLETE          0x0C
#define HCI_LE_EVT_EXTENDED_ADV_REPORT          0x0D

// ============================================================================
// LE ADVERTISING REPORT EVENT TYPES
// ============================================================================

#define HCI_LE_ADV_REPORT_IND                   0x00  // Connectable undirected
#define HCI_LE_ADV_REPORT_DIRECT_IND            0x01  // Connectable directed
#define HCI_LE_ADV_REPORT_SCAN_IND              0x02  // Scannable undirected
#define HCI_LE_ADV_REPORT_NONCONN_IND           0x03  // Non-connectable undirected
#define HCI_LE_ADV_REPORT_SCAN_RSP              0x04  // Scan response

// ============================================================================
// LE EVENT STRUCTURES
// ============================================================================

// LE Meta Event header
typedef struct __attribute__((packed)) {
    uint8_t subevent;
    uint8_t params[];
} hci_le_meta_event_t;

// LE Connection Complete event
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint16_t handle;
    uint8_t  role;                  // 0x00 = Master, 0x01 = Slave
    uint8_t  peer_addr_type;
    uint8_t  peer_addr[6];
    uint16_t conn_interval;         // N * 1.25ms
    uint16_t conn_latency;
    uint16_t supervision_timeout;   // N * 10ms
    uint8_t  master_clock_accuracy;
} hci_le_conn_complete_t;

// LE Advertising Report (single report entry)
typedef struct __attribute__((packed)) {
    uint8_t event_type;             // HCI_LE_ADV_REPORT_*
    uint8_t addr_type;              // 0x00 = public, 0x01 = random
    uint8_t addr[6];
    uint8_t data_length;
    // Followed by: data[data_length], rssi (int8_t)
} hci_le_adv_report_entry_t;

// LE Advertising Report event
typedef struct __attribute__((packed)) {
    uint8_t num_reports;
    // Followed by: hci_le_adv_report_entry_t entries[num_reports]
} hci_le_adv_report_t;

// LE Connection Update Complete event
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint16_t handle;
    uint16_t conn_interval;
    uint16_t conn_latency;
    uint16_t supervision_timeout;
} hci_le_conn_update_complete_t;

// LE Read Remote Features Complete event
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint16_t handle;
    uint8_t  features[8];
} hci_le_read_remote_features_complete_t;

// LE Long Term Key Request event
typedef struct __attribute__((packed)) {
    uint16_t handle;
    uint8_t  random[8];
    uint16_t ediv;
} hci_le_ltk_request_t;

// LE Read Buffer Size complete return params
typedef struct __attribute__((packed)) {
    uint8_t  status;
    uint16_t le_acl_data_packet_length;
    uint8_t  total_num_le_acl_packets;
} hci_return_le_read_buffer_size_t;

// LE Extended Advertising Report (Bluetooth 5.0+)
// Event type bits:
#define HCI_LE_EXT_ADV_EVT_CONNECTABLE      0x0001
#define HCI_LE_EXT_ADV_EVT_SCANNABLE        0x0002
#define HCI_LE_EXT_ADV_EVT_DIRECTED         0x0004
#define HCI_LE_EXT_ADV_EVT_SCAN_RSP         0x0008
#define HCI_LE_EXT_ADV_EVT_LEGACY           0x0010
#define HCI_LE_EXT_ADV_EVT_DATA_COMPLETE    0x0000
#define HCI_LE_EXT_ADV_EVT_DATA_INCOMPLETE  0x0020
#define HCI_LE_EXT_ADV_EVT_DATA_TRUNCATED   0x0040

typedef struct __attribute__((packed)) {
    uint16_t event_type;
    uint8_t  addr_type;
    uint8_t  addr[6];
    uint8_t  primary_phy;
    uint8_t  secondary_phy;
    uint8_t  advertising_sid;
    int8_t   tx_power;
    int8_t   rssi;
    uint16_t periodic_adv_interval;
    uint8_t  direct_addr_type;
    uint8_t  direct_addr[6];
    uint8_t  data_length;
    // Followed by: data[data_length]
} hci_le_ext_adv_report_entry_t;

// ============================================================================
// BLE AD (Advertising Data) TYPES
// ============================================================================

#define BLE_AD_TYPE_FLAGS                       0x01
#define BLE_AD_TYPE_INCOMPLETE_16BIT_UUIDS      0x02
#define BLE_AD_TYPE_COMPLETE_16BIT_UUIDS        0x03
#define BLE_AD_TYPE_INCOMPLETE_32BIT_UUIDS      0x04
#define BLE_AD_TYPE_COMPLETE_32BIT_UUIDS        0x05
#define BLE_AD_TYPE_INCOMPLETE_128BIT_UUIDS     0x06
#define BLE_AD_TYPE_COMPLETE_128BIT_UUIDS       0x07
#define BLE_AD_TYPE_SHORTENED_LOCAL_NAME        0x08
#define BLE_AD_TYPE_COMPLETE_LOCAL_NAME         0x09
#define BLE_AD_TYPE_TX_POWER_LEVEL              0x0A
#define BLE_AD_TYPE_CLASS_OF_DEVICE             0x0D
#define BLE_AD_TYPE_SERVICE_DATA_16BIT          0x16
#define BLE_AD_TYPE_APPEARANCE                  0x19
#define BLE_AD_TYPE_MANUFACTURER_SPECIFIC       0xFF

// ============================================================================
// BLE GAP APPEARANCE VALUES (for gamepads)
// ============================================================================

#define BLE_APPEARANCE_UNKNOWN                  0x0000
#define BLE_APPEARANCE_GAMEPAD                  0x03C4
#define BLE_APPEARANCE_JOYSTICK                 0x03C5

#endif // HCI_EVENT_H
