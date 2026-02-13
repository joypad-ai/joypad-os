// bthid.h - Bluetooth HID Layer
// Handles Bluetooth HID devices and routes reports to device-specific drivers

#ifndef BTHID_H
#define BTHID_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// CONSTANTS
// ============================================================================

#define BTHID_MAX_DEVICES       4   // Max simultaneous BT HID devices
#define BTHID_MAX_NAME_LEN      32  // Max device name length

// ============================================================================
// HID REPORT TYPES (Bluetooth HID spec)
// ============================================================================

#define BTHID_REPORT_TYPE_INPUT     0x01
#define BTHID_REPORT_TYPE_OUTPUT    0x02
#define BTHID_REPORT_TYPE_FEATURE   0x03

// HID Transaction header types (high nibble)
#define BTHID_TRANS_HANDSHAKE       0x00
#define BTHID_TRANS_HID_CONTROL     0x10
#define BTHID_TRANS_GET_REPORT      0x40
#define BTHID_TRANS_SET_REPORT      0x50
#define BTHID_TRANS_GET_PROTOCOL    0x60
#define BTHID_TRANS_SET_PROTOCOL    0x70
#define BTHID_TRANS_DATA            0xA0

// Handshake result codes
#define BTHID_HANDSHAKE_SUCCESS     0x00
#define BTHID_HANDSHAKE_NOT_READY   0x01
#define BTHID_HANDSHAKE_ERR_INVALID 0x02
#define BTHID_HANDSHAKE_ERR_UNSUPPORTED 0x03
#define BTHID_HANDSHAKE_ERR_INVALID_PARAM 0x04
#define BTHID_HANDSHAKE_ERR_UNKNOWN 0x0E
#define BTHID_HANDSHAKE_ERR_FATAL   0x0F

// Protocol modes
#define BTHID_PROTOCOL_BOOT         0x00
#define BTHID_PROTOCOL_REPORT       0x01

// ============================================================================
// DEVICE TYPES (based on Class of Device)
// ============================================================================

typedef enum {
    BTHID_DEVICE_UNKNOWN = 0,
    BTHID_DEVICE_KEYBOARD,
    BTHID_DEVICE_MOUSE,
    BTHID_DEVICE_GAMEPAD,
    BTHID_DEVICE_JOYSTICK,
} bthid_device_type_t;

// ============================================================================
// DEVICE STATE
// ============================================================================

typedef struct {
    bool active;                        // Device slot in use
    uint8_t conn_index;                 // Transport connection index
    uint8_t bd_addr[6];                 // Device address
    char name[BTHID_MAX_NAME_LEN];      // Device name
    bthid_device_type_t type;           // Device type
    uint8_t player_index;               // Assigned player slot (-1 if none)
    uint16_t vendor_id;                 // USB VID (from SDP/manufacturer data)
    uint16_t product_id;                // USB PID (from SDP/manufacturer data)

    bool is_ble;                        // True if BLE (not Classic BT)

    // Device driver info
    const void* driver;                 // Pointer to device driver interface
    void* driver_data;                  // Driver-specific data
} bthid_device_t;

// ============================================================================
// DEVICE DRIVER INTERFACE
// ============================================================================

typedef struct {
    const char* name;

    // Check if this driver handles a device (by VID/PID, name, COD, or transport)
    // Priority: VID/PID match > name match > COD match
    bool (*match)(const char* device_name, const uint8_t* class_of_device,
                  uint16_t vendor_id, uint16_t product_id, bool is_ble);

    // Initialize driver for a device
    bool (*init)(bthid_device_t* device);

    // Process incoming HID report
    void (*process_report)(bthid_device_t* device, const uint8_t* data, uint16_t len);

    // Periodic task (for output reports, rumble, etc.)
    void (*task)(bthid_device_t* device);

    // Device disconnected
    void (*disconnect)(bthid_device_t* device);

} bthid_driver_t;

// ============================================================================
// BTHID API
// ============================================================================

// Initialize BTHID layer
void bthid_init(void);

// Periodic task (call from main loop)
void bthid_task(void);

// Get device by connection index
bthid_device_t* bthid_get_device(uint8_t conn_index);

// Get device count
uint8_t bthid_get_device_count(void);

// Re-evaluate driver for a device (call when VID/PID or name becomes available)
void bthid_update_device_info(uint8_t conn_index, const char* name,
                               uint16_t vendor_id, uint16_t product_id);

// ============================================================================
// DRIVER REGISTRATION
// ============================================================================

// Register a device driver
void bthid_register_driver(const bthid_driver_t* driver);

// ============================================================================
// OUTPUT REPORTS
// ============================================================================

// Send output report (rumble, LEDs, etc.)
bool bthid_send_output_report(uint8_t conn_index, uint8_t report_id,
                               const uint8_t* data, uint16_t len);

// Send feature report
bool bthid_send_feature_report(uint8_t conn_index, uint8_t report_id,
                                const uint8_t* data, uint16_t len);

#endif // BTHID_H
