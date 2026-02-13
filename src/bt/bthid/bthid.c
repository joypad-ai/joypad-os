// bthid.c - Bluetooth HID Layer Implementation
// Handles Bluetooth HID devices and routes reports to device-specific drivers

#include "bthid.h"
#include "bt/transport/bt_transport.h"
#include "devices/generic/bthid_gamepad.h"
#include "devices/vendors/sony/ds3_bt.h"
#include "devices/vendors/sony/ds4_bt.h"
#include "devices/vendors/sony/ds5_bt.h"
#include "core/services/storage/flash.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// SONY REPORT IDS (for reclassification)
// ============================================================================

#define SONY_REPORT_ID_BASIC    0x01    // DS3/DS4 basic mode
#define SONY_REPORT_ID_DS4      0x11    // DS4 full BT report
#define SONY_REPORT_ID_DS5      0x31    // DS5 full BT report

// ============================================================================
// CONFIGURATION
// ============================================================================

#define BTHID_MAX_DRIVERS       16  // Max registered drivers

// ============================================================================
// STATIC DATA
// ============================================================================

static bthid_device_t devices[BTHID_MAX_DEVICES];
static const bthid_driver_t* drivers[BTHID_MAX_DRIVERS];
static uint8_t driver_count = 0;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static bthid_device_t* find_or_create_device(uint8_t conn_index);
static const bthid_driver_t* find_driver(const char* name, const uint8_t* cod,
                                          uint16_t vendor_id, uint16_t product_id,
                                          bool is_ble);
static bthid_device_type_t classify_device(const uint8_t* class_of_device);
static bool try_reclassify_sony_device(bthid_device_t* device, uint8_t report_id);

// ============================================================================
// INITIALIZATION
// ============================================================================

void bthid_init(void)
{
    memset(devices, 0, sizeof(devices));
    driver_count = 0;
    printf("[BTHID] Initialized\n");
}

// ============================================================================
// DRIVER REGISTRATION
// ============================================================================

void bthid_register_driver(const bthid_driver_t* driver)
{
    if (driver_count < BTHID_MAX_DRIVERS) {
        drivers[driver_count++] = driver;
        printf("[BTHID] Registered driver: %s\n", driver->name);
    } else {
        printf("[BTHID] Driver registry full, cannot add: %s\n", driver->name);
    }
}

// ============================================================================
// TASK
// ============================================================================

static bool bthid_task_debug_done = false;

void bthid_task(void)
{
    // Run device driver tasks
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (devices[i].active && devices[i].driver) {
            if (!bthid_task_debug_done) {
                printf("[BTHID] Task loop: dev %d active, driver=%p, drv->task=%p\n",
                       i, devices[i].driver,
                       ((const bthid_driver_t*)devices[i].driver)->task);
                bthid_task_debug_done = true;
            }
            const bthid_driver_t* drv = (const bthid_driver_t*)devices[i].driver;
            if (drv->task) {
                drv->task(&devices[i]);
            }
        }
    }
}

// ============================================================================
// DEVICE MANAGEMENT
// ============================================================================

static bthid_device_t* find_or_create_device(uint8_t conn_index)
{
    // Look for existing device
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (devices[i].active && devices[i].conn_index == conn_index) {
            return &devices[i];
        }
    }

    // Find free slot
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!devices[i].active) {
            memset(&devices[i], 0, sizeof(devices[i]));
            devices[i].active = true;
            devices[i].conn_index = conn_index;
            devices[i].player_index = 0xFF;  // Unassigned
            return &devices[i];
        }
    }

    return NULL;
}

static void remove_device(uint8_t conn_index)
{
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (devices[i].active && devices[i].conn_index == conn_index) {
            if (devices[i].driver) {
                const bthid_driver_t* drv = (const bthid_driver_t*)devices[i].driver;
                if (drv->disconnect) {
                    drv->disconnect(&devices[i]);
                }
            }
            memset(&devices[i], 0, sizeof(devices[i]));
            printf("[BTHID] Device removed from slot %d\n", i);
            break;
        }
    }
}

bthid_device_t* bthid_get_device(uint8_t conn_index)
{
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (devices[i].active && devices[i].conn_index == conn_index) {
            return &devices[i];
        }
    }
    return NULL;
}

uint8_t bthid_get_device_count(void)
{
    uint8_t count = 0;
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (devices[i].active) {
            count++;
        }
    }
    return count;
}

// ============================================================================
// DEVICE INFO UPDATE (VID/PID available after SDP query)
// ============================================================================

void bthid_update_device_info(uint8_t conn_index, const char* name,
                               uint16_t vendor_id, uint16_t product_id)
{
    bthid_device_t* device = bthid_get_device(conn_index);
    if (!device) {
        return;
    }

    // Update name if provided
    if (name && name[0]) {
        strncpy(device->name, name, BTHID_MAX_NAME_LEN - 1);
        device->name[BTHID_MAX_NAME_LEN - 1] = '\0';
    }

    // Update VID/PID if provided
    if (vendor_id) device->vendor_id = vendor_id;
    if (product_id) device->product_id = product_id;

    // Re-evaluate the driver if currently using generic gamepad.
    // This handles late-arriving info: name from remote name request,
    // VID/PID from SDP query, or both.
    const bthid_driver_t* current = (const bthid_driver_t*)device->driver;
    if (current == &bthid_gamepad_driver) {
        const bt_connection_t* conn = bt_get_connection(conn_index);
        const uint8_t* cod = conn ? conn->class_of_device : NULL;

        const bthid_driver_t* new_driver = NULL;
        for (int i = 0; i < driver_count; i++) {
            if (drivers[i] != &bthid_gamepad_driver &&
                drivers[i]->match && drivers[i]->match(device->name, cod,
                                                        device->vendor_id, device->product_id,
                                                        device->is_ble)) {
                new_driver = drivers[i];
                break;
            }
        }

        if (new_driver) {
            printf("[BTHID] Re-selecting driver: %s -> %s (name=%s VID=0x%04X PID=0x%04X)\n",
                   current->name, new_driver->name, device->name,
                   device->vendor_id, device->product_id);

            if (current->disconnect) {
                current->disconnect(device);
            }
            device->driver_data = NULL;
            device->driver = new_driver;
            if (new_driver->init) {
                new_driver->init(device);
            }
        }
    }
}

// ============================================================================
// DRIVER MATCHING
// ============================================================================

static const bthid_driver_t* find_driver(const char* name, const uint8_t* cod,
                                          uint16_t vendor_id, uint16_t product_id,
                                          bool is_ble)
{
    // First try registered drivers
    for (int i = 0; i < driver_count; i++) {
        if (drivers[i]->match && drivers[i]->match(name, cod, vendor_id, product_id, is_ble)) {
            return drivers[i];
        }
    }
    return NULL;
}

static bthid_device_type_t classify_device(const uint8_t* class_of_device)
{
    if (!class_of_device) {
        return BTHID_DEVICE_UNKNOWN;
    }

    // Class of Device format:
    // cod[0]: Minor Device Class + Format Type
    // cod[1]: Major Service Class (low byte) + Major Device Class
    // cod[2]: Major Service Class (high byte)

    uint8_t major_class = (class_of_device[1] >> 0) & 0x1F;
    uint8_t minor_class = (class_of_device[0] >> 2) & 0x3F;

    // Major class 0x05 = Peripheral
    if (major_class == 0x05) {
        // Minor class bits 7-6 indicate device type
        uint8_t peripheral_type = (minor_class >> 4) & 0x03;

        switch (peripheral_type) {
            case 0x01:  // Keyboard
                return BTHID_DEVICE_KEYBOARD;
            case 0x02:  // Pointing device
                return BTHID_DEVICE_MOUSE;
            case 0x03:  // Combo keyboard/pointing
                return BTHID_DEVICE_KEYBOARD;
            default:
                break;
        }

        // Check minor bits 5-0 for gamepad/joystick
        uint8_t device_subtype = minor_class & 0x0F;
        if (device_subtype == 0x01) {
            return BTHID_DEVICE_JOYSTICK;
        } else if (device_subtype == 0x02) {
            return BTHID_DEVICE_GAMEPAD;
        }
    }

    return BTHID_DEVICE_UNKNOWN;
}

// ============================================================================
// SONY DEVICE RECLASSIFICATION
// Detect DS4 vs DS5 by report ID and swap drivers if needed
// ============================================================================

static bool try_reclassify_sony_device(bthid_device_t* device, uint8_t report_id)
{
    const bthid_driver_t* current = (const bthid_driver_t*)device->driver;
    const bthid_driver_t* new_driver = NULL;

    // Check if reclassification is needed
    if (report_id == SONY_REPORT_ID_DS5 && current != &ds5_bt_driver) {
        // Got DS5 report but not using DS5 driver
        new_driver = &ds5_bt_driver;
        printf("[BTHID] Reclassify: report 0x%02X -> DS5 driver\n", report_id);
    } else if (report_id == SONY_REPORT_ID_DS4 && current != &ds4_bt_driver) {
        // Got DS4 full report but not using DS4 driver
        new_driver = &ds4_bt_driver;
        printf("[BTHID] Reclassify: report 0x%02X -> DS4 driver\n", report_id);
    }

    if (new_driver) {
        // Disconnect old driver
        if (current && current->disconnect) {
            current->disconnect(device);
        }

        // Clear driver data
        device->driver_data = NULL;

        // Initialize new driver
        device->driver = new_driver;
        if (new_driver->init) {
            new_driver->init(device);
        }

        printf("[BTHID] Reclassification complete: now using %s\n", new_driver->name);
        return true;
    }

    return false;
}

// ============================================================================
// TRANSPORT CALLBACKS
// Override weak implementations in bt_transport.c
// ============================================================================

void bt_on_hid_ready(uint8_t conn_index)
{
    printf("[BTHID] HID ready on connection %d\n", conn_index);

    const bt_connection_t* conn = bt_get_connection(conn_index);
    if (!conn) {
        return;
    }

    bthid_device_t* device = find_or_create_device(conn_index);
    if (!device) {
        printf("[BTHID] No free device slots\n");
        return;
    }

    // Copy device info
    memcpy(device->bd_addr, conn->bd_addr, 6);
    strncpy(device->name, conn->name, BTHID_MAX_NAME_LEN - 1);
    device->name[BTHID_MAX_NAME_LEN - 1] = '\0';
    device->type = classify_device(conn->class_of_device);
    device->vendor_id = conn->vendor_id;
    device->product_id = conn->product_id;
    device->is_ble = conn->is_ble;

    char addr_str[18];
    sprintf(addr_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            device->bd_addr[5], device->bd_addr[4], device->bd_addr[3],
            device->bd_addr[2], device->bd_addr[1], device->bd_addr[0]);

    printf("[BTHID] Device: %s (%s), type=%d, VID=0x%04X PID=0x%04X\n",
           device->name[0] ? device->name : "Unknown",
           addr_str, device->type, conn->vendor_id, conn->product_id);

    // Find matching driver (VID/PID takes priority over name/COD)
    const bthid_driver_t* driver = find_driver(device->name, conn->class_of_device,
                                               conn->vendor_id, conn->product_id,
                                               device->is_ble);
    if (driver) {
        printf("[BTHID] Using driver: %s\n", driver->name);
        device->driver = driver;
        if (driver->init) {
            driver->init(device);
        }
    } else {
        printf("[BTHID] No specific driver found, using generic gamepad\n");
        device->driver = &bthid_gamepad_driver;
        if (bthid_gamepad_driver.init) {
            bthid_gamepad_driver.init(device);
        }
    }

    // Debug: confirm device state directly from array
    printf("[BTHID] Setup complete: devices[0].active=%d, devices[0].driver=%p\n",
           devices[0].active, devices[0].driver);
}

void bt_on_disconnect(uint8_t conn_index)
{
    printf("[BTHID] Disconnect on connection %d\n", conn_index);
    remove_device(conn_index);

    // Check if we have pending flash writes now that BT may be idle
    flash_on_bt_disconnect();
}

static bool bt_on_hid_report_debug_done = false;

void bt_on_hid_report(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    if (len < 1) {
        return;
    }

    bthid_device_t* device = bthid_get_device(conn_index);
    if (!device) {
        printf("[BTHID] Report for unknown device on conn %d\n", conn_index);
        return;
    }

    // Debug first report
    if (!bt_on_hid_report_debug_done) {
        printf("[BTHID] First report: conn=%d, len=%d, data[0]=0x%02X\n",
               conn_index, len, data[0]);
        bt_on_hid_report_debug_done = true;
    }

    // Parse HID transaction header
    uint8_t header = data[0];
    uint8_t trans_type = header & 0xF0;
    uint8_t param = header & 0x0F;

    switch (trans_type) {
        case BTHID_TRANS_DATA: {
            // Data report - param indicates report type
            uint8_t report_type = param;
            const uint8_t* report_data = data + 1;
            uint16_t report_len = len - 1;

            if (report_type == BTHID_REPORT_TYPE_INPUT && report_len >= 1) {
                // Check report ID for Sony device reclassification
                // Only attempt if currently using a Sony driver or generic gamepad
                // This prevents Xbox data (which may contain 0x11/0x31 bytes) from triggering reclassification
                uint8_t report_id = report_data[0];
                const bthid_driver_t* drv = (const bthid_driver_t*)device->driver;
                bool is_sony_or_generic = (drv == &ds3_bt_driver || drv == &ds4_bt_driver ||
                                           drv == &ds5_bt_driver || drv == &bthid_gamepad_driver);
                if (is_sony_or_generic && (report_id == SONY_REPORT_ID_DS4 || report_id == SONY_REPORT_ID_DS5)) {
                    // Try to reclassify based on actual report ID
                    if (try_reclassify_sony_device(device, report_id)) {
                        // Driver was swapped - it will process this report on next iteration
                        // after its init sequence completes
                        return;
                    }
                }

                // Input report - route to driver
                if (device->driver) {
                    const bthid_driver_t* drv = (const bthid_driver_t*)device->driver;
                    if (drv->process_report) {
                        drv->process_report(device, report_data, report_len);
                    }
                }
            }
            break;
        }

        case BTHID_TRANS_HANDSHAKE:
            printf("[BTHID] Handshake: result=%d\n", param);
            break;

        default:
            printf("[BTHID] Unhandled transaction: 0x%02X\n", trans_type);
            break;
    }
}

// ============================================================================
// OUTPUT REPORTS
// ============================================================================

bool bthid_send_output_report(uint8_t conn_index, uint8_t report_id,
                               const uint8_t* data, uint16_t len)
{
    uint8_t buf[64];
    if (len + 2 > sizeof(buf)) {
        return false;
    }

    // Build DATA | OUTPUT header
    buf[0] = BTHID_TRANS_DATA | BTHID_REPORT_TYPE_OUTPUT;
    buf[1] = report_id;
    memcpy(&buf[2], data, len);

    return bt_send_interrupt(conn_index, buf, len + 2);
}

bool bthid_send_feature_report(uint8_t conn_index, uint8_t report_id,
                                const uint8_t* data, uint16_t len)
{
    uint8_t buf[64];
    if (len + 2 > sizeof(buf)) {
        return false;
    }

    // Build SET_REPORT | FEATURE header
    buf[0] = BTHID_TRANS_SET_REPORT | BTHID_REPORT_TYPE_FEATURE;
    buf[1] = report_id;
    memcpy(&buf[2], data, len);

    return bt_send_control(conn_index, buf, len + 2);
}
