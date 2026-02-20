// btstack_config_esp32.h - BTstack configuration for ESP32-S3 (BLE-only)
//
// ESP32-S3 has BLE only (no Classic BT). Uses FreeRTOS run loop and
// NVS-based TLV storage via BTstack's ESP32 port.

#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// ============================================================================
// PORT FEATURES
// ============================================================================

#define HAVE_ASSERT
#define HAVE_EMBEDDED_TIME_MS
#define HAVE_FREERTOS_INCLUDE_PREFIX
#define HAVE_FREERTOS_TASK_NOTIFICATIONS
#define HAVE_MALLOC
#define HAVE_PRINTF
#define ENABLE_PRINTF_HEXDUMP

// HCI Controller to Host Flow Control
#define ENABLE_HCI_CONTROLLER_TO_HOST_FLOW_CONTROL

// ============================================================================
// BTSTACK FEATURES
// ============================================================================

// BLE only (ESP32-S3 does not have Classic BT)
#define ENABLE_BLE
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_SECURE_CONNECTIONS

// Classic BT enabled for compilation (drivers compile but are dead code)
// The ESP32-S3 controller only supports BLE, so Classic connections
// will never succeed at runtime
#define ENABLE_CLASSIC

// Enable logging
#define ENABLE_LOG_ERROR
#define ENABLE_LOG_INFO

// Use software AES (ESP32-S3 has hardware AES but BTstack uses its own)
#define ENABLE_SOFTWARE_AES128

// Enable micro-ecc for LE Secure Connections P-256
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

// ============================================================================
// BUFFER SIZES
// ============================================================================

// ACL buffer large enough for 512 byte Characteristic
#define HCI_ACL_PAYLOAD_SIZE (512 + 4 + 3)

// Pre-buffer for L2CAP headers
#define HCI_INCOMING_PRE_BUFFER_SIZE 14

// VHCI requires 1 byte pre-buffer for packet type
#define HCI_OUTGOING_PRE_BUFFER_SIZE 1

// Host flow control buffer sizes
#define HCI_HOST_ACL_PACKET_LEN HCI_ACL_PAYLOAD_SIZE
#define HCI_HOST_ACL_PACKET_NUM 20
#define HCI_HOST_SCO_PACKET_LEN 0
#define HCI_HOST_SCO_PACKET_NUM 0

// ============================================================================
// MEMORY POOLS
// ============================================================================

// Number of HCI connections
#define MAX_NR_HCI_CONNECTIONS 2

// L2CAP channels (Classic HID needs Control + Interrupt + SDP per device)
#define MAX_NR_L2CAP_CHANNELS 8

// L2CAP services
#define MAX_NR_L2CAP_SERVICES 3

// GATT clients (for BLE devices)
#define MAX_NR_GATT_CLIENTS 1

// Whitelist entries
#define MAX_NR_WHITELIST_ENTRIES 2

// LE Device DB entries (for bonding storage)
#define MAX_NR_LE_DEVICE_DB_ENTRIES 4

// Link keys storage (Classic BT - needed for compilation)
#define NVM_NUM_LINK_KEYS 2
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 4

// NVM storage for device DB
#define NVM_NUM_DEVICE_DB_ENTRIES 4

// ============================================================================
// HID SUPPORT
// ============================================================================

// Enable HID Host (for game controllers)
#define ENABLE_HID_HOST

// Number of HID Host connections
#define MAX_NR_HID_HOST_CONNECTIONS 2

// Number of HIDS clients (BLE HID Service clients)
#define MAX_NR_HIDS_CLIENTS 1

// Number of Battery Service clients (BLE Battery Service)
#define MAX_NR_BATTERY_SERVICE_CLIENTS 1

#endif // BTSTACK_CONFIG_H
