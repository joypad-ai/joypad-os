// bt_transport.h - Bluetooth Transport Abstraction
// Provides a common interface for different BT transports (USB dongle, native BLE, etc.)

#ifndef BT_TRANSPORT_H
#define BT_TRANSPORT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// CONSTANTS
// ============================================================================

// 4 Classic + 2 BLE connections (BLE uses conn_index 4-5)
// Apps can override this by defining BT_MAX_CONNECTIONS in app.h before including this header
#ifndef BT_MAX_CONNECTIONS
#define BT_MAX_CONNECTIONS      6
#endif
#define BT_MAX_NAME_LEN         32

// ============================================================================
// CONNECTION INFO
// ============================================================================

typedef struct {
    uint8_t  bd_addr[6];            // Bluetooth device address
    char     name[BT_MAX_NAME_LEN]; // Device name
    uint8_t  class_of_device[3];    // Class of Device (for Classic BT)
    uint16_t vendor_id;             // Vendor ID (from SDP Device ID query)
    uint16_t product_id;            // Product ID (from SDP Device ID query)
    uint16_t control_cid;           // HID Control channel (local CID)
    uint16_t interrupt_cid;         // HID Interrupt channel (local CID)
    bool     connected;             // Connection active
    bool     hid_ready;             // HID channels established
} bt_connection_t;

// ============================================================================
// TRANSPORT INTERFACE
// ============================================================================

typedef struct {
    // Name of this transport (for debug)
    const char* name;

    // Initialize the transport
    void (*init)(void);

    // Periodic task (call from main loop)
    void (*task)(void);

    // Check if transport is ready
    bool (*is_ready)(void);

    // Get connection count
    uint8_t (*get_connection_count)(void);

    // Get connection info by index
    const bt_connection_t* (*get_connection)(uint8_t index);

    // Send data on HID Control channel (output reports, feature reports)
    bool (*send_control)(uint8_t conn_index, const uint8_t* data, uint16_t len);

    // Send data on HID Interrupt channel (output reports for rumble/LED)
    bool (*send_interrupt)(uint8_t conn_index, const uint8_t* data, uint16_t len);

    // Disconnect a device
    void (*disconnect)(uint8_t conn_index);

    // Enable/disable pairing mode
    void (*set_pairing_mode)(bool enable);

    // Check if in pairing mode
    bool (*is_pairing_mode)(void);

} bt_transport_t;

// ============================================================================
// ACTIVE TRANSPORT
// ============================================================================

// The currently active transport (set at init time)
extern const bt_transport_t* bt_transport;

// Initialize the Bluetooth subsystem with specified transport
void bt_init(const bt_transport_t* transport);

// Convenience wrappers
static inline void bt_task(void) {
    if (bt_transport && bt_transport->task) {
        bt_transport->task();
    }
}

static inline bool bt_is_ready(void) {
    return bt_transport && bt_transport->is_ready && bt_transport->is_ready();
}

static inline uint8_t bt_get_connection_count(void) {
    return bt_transport && bt_transport->get_connection_count
           ? bt_transport->get_connection_count() : 0;
}

static inline const bt_connection_t* bt_get_connection(uint8_t index) {
    return bt_transport && bt_transport->get_connection
           ? bt_transport->get_connection(index) : NULL;
}

static inline bool bt_send_control(uint8_t conn_index, const uint8_t* data, uint16_t len) {
    return bt_transport && bt_transport->send_control
           ? bt_transport->send_control(conn_index, data, len) : false;
}

static inline bool bt_send_interrupt(uint8_t conn_index, const uint8_t* data, uint16_t len) {
    return bt_transport && bt_transport->send_interrupt
           ? bt_transport->send_interrupt(conn_index, data, len) : false;
}

static inline void bt_disconnect(uint8_t conn_index) {
    if (bt_transport && bt_transport->disconnect) {
        bt_transport->disconnect(conn_index);
    }
}

static inline void bt_set_pairing_mode(bool enable) {
    if (bt_transport && bt_transport->set_pairing_mode) {
        bt_transport->set_pairing_mode(enable);
    }
}

static inline bool bt_is_pairing_mode(void) {
    return bt_transport && bt_transport->is_pairing_mode
           ? bt_transport->is_pairing_mode() : false;
}

// ============================================================================
// TRANSPORT CALLBACKS (implemented by BTHID layer)
// ============================================================================

// Called when HID channels are ready on a connection
extern void bt_on_hid_ready(uint8_t conn_index);

// Called when a connection is lost
extern void bt_on_disconnect(uint8_t conn_index);

// Called when HID data is received on interrupt channel
extern void bt_on_hid_report(uint8_t conn_index, const uint8_t* data, uint16_t len);

#endif // BT_TRANSPORT_H
