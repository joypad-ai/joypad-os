// btstack_config.h - BTstack configuration for TinyUSB HCI transport on RP2040
//
// This is a minimal configuration for using BTstack with a USB Bluetooth
// dongle via TinyUSB on RP2040-based boards.

#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

// ============================================================================
// PORT FEATURES
// ============================================================================

// Use embedded run loop (no RTOS)
#define HAVE_EMBEDDED_TIME_MS

// We have Pico SDK time functions
#define HAVE_BTSTACK_STDIN

// We don't have malloc - use static allocation
// #define HAVE_MALLOC

// Printf works
#define HAVE_PRINTF
#define ENABLE_PRINTF_HEXDUMP

// ============================================================================
// BTSTACK FEATURES
// ============================================================================

// Enable BLE (Bluetooth Low Energy)
#define ENABLE_BLE
#define ENABLE_LE_CENTRAL
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_SECURE_CONNECTIONS

// Enable Classic Bluetooth
#define ENABLE_CLASSIC

// Enable logging
#define ENABLE_LOG_ERROR
#define ENABLE_LOG_INFO
#define ENABLE_LOG_DEBUG  // Enable for verbose logging

// Use software AES (no hardware crypto on RP2040 for BT)
#define ENABLE_SOFTWARE_AES128

// Enable micro-ecc for LE Secure Connections P-256
#define ENABLE_MICRO_ECC_FOR_LE_SECURE_CONNECTIONS

// ============================================================================
// BUFFER SIZES
// ============================================================================

// HCI ACL payload size (standard BT is 1021, but we use smaller for memory)
#define HCI_ACL_PAYLOAD_SIZE 256

// Pre-buffer for L2CAP/BNEP headers
#define HCI_INCOMING_PRE_BUFFER_SIZE 14

// ============================================================================
// MEMORY POOLS (static allocation)
// ============================================================================

// Number of HCI connections (Classic + BLE)
#define MAX_NR_HCI_CONNECTIONS 2

// Number of L2CAP channels (Classic HID needs Control + Interrupt + SDP per device)
#define MAX_NR_L2CAP_CHANNELS 8

// Number of L2CAP services
#define MAX_NR_L2CAP_SERVICES 3

// Number of GATT clients (for BLE devices)
#define MAX_NR_GATT_CLIENTS 1

// Number of whitelist entries
#define MAX_NR_WHITELIST_ENTRIES 2

// LE Device DB entries (for bonding storage)
#define MAX_NR_LE_DEVICE_DB_ENTRIES 2

// Link keys storage (Classic BT)
#define NVM_NUM_LINK_KEYS 2
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 4
// Note: Don't define NVM_NUM_DEVICE_DB_ENTRIES - we use le_device_db_memory.c

// ============================================================================
// HID SUPPORT
// ============================================================================

// Enable HID Host (for game controllers)
#define ENABLE_HID_HOST

// Number of HID Host connections (Classic BT HID devices)
#define MAX_NR_HID_HOST_CONNECTIONS 2

// Number of HIDS clients (BLE HID Service clients)
#define MAX_NR_HIDS_CLIENTS 1

#endif // BTSTACK_CONFIG_H
