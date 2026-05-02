// usbd_mode.h - USB Device output mode interface
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#ifndef USBD_MODE_H
#define USBD_MODE_H

#include <stdint.h>
#include <stdbool.h>
#include "usbd.h"
#include "core/input_event.h"
#include "core/output_interface.h"
#include "core/services/profiles/profile.h"
#include "tusb.h"
#include "device/usbd_pvt.h"  // For usbd_class_driver_t

// Mode interface - each USB output mode implements this
typedef struct {
    const char* name;                   // Display name (e.g., "DInput", "XInput")
    usb_output_mode_t mode;             // Mode enum value

    // === Descriptors ===
    const uint8_t* (*get_device_descriptor)(void);
    const uint8_t* (*get_config_descriptor)(void);
    const uint8_t* (*get_report_descriptor)(void);  // NULL if not HID-based

    // === Lifecycle ===
    void (*init)(void);                 // Initialize state to neutral values

    // === Report Sending ===
    // Returns true if report was sent successfully
    bool (*send_report)(uint8_t player_index,
                        const input_event_t* event,
                        const profile_output_t* profile_out,
                        uint32_t buttons);

    // Ready check - returns true if USB is ready to send
    bool (*is_ready)(void);
    bool (*is_ready_itf)(uint8_t itf);

    // === Feedback (optional - NULL if not supported) ===
    // Handle output report from host (rumble, LEDs)
    void (*handle_output)(uint8_t report_id, const uint8_t* data, uint16_t len);
    void (*handle_output_itf)(uint8_t itf, uint8_t report_id, const uint8_t* data, uint16_t len);

    // Get simple rumble value (0-255), legacy interface
    uint8_t (*get_rumble)(void);

    // Get full feedback state (rumble L/R, LEDs)
    bool (*get_feedback)(output_feedback_t* fb);

    // === HID Feature Reports (optional - NULL if not needed) ===
    // Handle GET_REPORT requests
    uint16_t (*get_report)(uint8_t report_id, hid_report_type_t report_type,
                           uint8_t* buffer, uint16_t reqlen);

    uint16_t (*get_report_itf)(uint8_t itf, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t* buffer, uint16_t reqlen);

    // === Custom Class Driver (optional - NULL for built-in HID) ===
    const usbd_class_driver_t* (*get_class_driver)(void);

    // === Mode-specific task (optional - NULL if not needed) ===
    // Called periodically from usbd_task()
    void (*task)(void);

} usbd_mode_t;

// Mode registry - populated by usbd_register_modes()
extern const usbd_mode_t* usbd_modes[USB_OUTPUT_MODE_COUNT];

// Get current active mode
const usbd_mode_t* usbd_get_current_mode(void);

// Register all modes (called from usbd_init)
void usbd_register_modes(void);

// ============================================================================
// MODE DECLARATIONS
// ============================================================================

extern const usbd_mode_t hid_mode;
extern const usbd_mode_t sinput_mode;
#if CFG_TUD_XINPUT
extern const usbd_mode_t xinput_mode;
#endif
extern const usbd_mode_t switch_mode;
extern const usbd_mode_t ps3_mode;
// PS3 auth feature report handler (called from tud_hid_set_report_cb)
void ps3_mode_set_feature_report(uint8_t itf, uint8_t report_id, const uint8_t* buffer, uint16_t bufsize);
extern const usbd_mode_t psclassic_mode;
extern const usbd_mode_t ps4_mode;
// PS4 auth feature report handler (called from tud_hid_set_report_cb)
void ps4_mode_set_feature_report(uint8_t report_id, const uint8_t* buffer, uint16_t bufsize);
extern const usbd_mode_t xid_mode;
extern const usbd_mode_t xbone_mode;
extern const usbd_mode_t xac_mode;
extern const usbd_mode_t kbmouse_mode;
// KB/Mouse mode helper for idle mouse reports
bool kbmouse_mode_send_idle_mouse(void);
extern const usbd_mode_t pcemini_mode;
#if CFG_TUD_GC_ADAPTER
extern const usbd_mode_t gc_adapter_mode;
// Get per-port rumble (for multi-controller GC Adapter mode)
uint8_t gc_adapter_mode_get_port_rumble(uint8_t port);
#endif

#endif // USBD_MODE_H
