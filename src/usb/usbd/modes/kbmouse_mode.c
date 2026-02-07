// kbmouse_mode.c - Keyboard/Mouse USB device mode
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "tusb.h"
#include "../usbd_mode.h"
#include "../usbd.h"
#include "../kbmouse/kbmouse.h"
#include "descriptors/kbmouse_descriptors.h"
#include "descriptors/sinput_descriptors.h"
#include "core/buttons.h"
#include <string.h>

// ============================================================================
// STATE
// ============================================================================

static kbmouse_keyboard_report_t kbmouse_kb_report;
static kbmouse_mouse_report_t kbmouse_mouse_report;

// ============================================================================
// MODE INTERFACE IMPLEMENTATION
// ============================================================================

static void kbmouse_mode_init(void)
{
    kbmouse_init();
    memset(&kbmouse_kb_report, 0, sizeof(kbmouse_kb_report));
    memset(&kbmouse_mouse_report, 0, sizeof(kbmouse_mouse_report));
}

static bool kbmouse_mode_is_ready(void)
{
    // Both keyboard and mouse interfaces must be ready
    return tud_hid_n_ready(ITF_NUM_HID_KEYBOARD) &&
           tud_hid_n_ready(ITF_NUM_HID_MOUSE);
}

static bool kbmouse_mode_send_report(uint8_t player_index,
                                      const input_event_t* event,
                                      const profile_output_t* profile_out,
                                      uint32_t buttons)
{
    (void)player_index;
    (void)event;

    // Convert gamepad to keyboard/mouse reports
    kbmouse_convert(buttons, profile_out, &kbmouse_kb_report, &kbmouse_mouse_report);

    // Send keyboard and mouse on separate interfaces (no report IDs needed)
    bool kb_ok = tud_hid_n_keyboard_report(ITF_NUM_HID_KEYBOARD, 0,
                                            kbmouse_kb_report.modifier,
                                            kbmouse_kb_report.keycode);
    bool mouse_ok = tud_hid_n_mouse_report(ITF_NUM_HID_MOUSE, 0,
                                            kbmouse_mouse_report.buttons,
                                            kbmouse_mouse_report.x,
                                            kbmouse_mouse_report.y,
                                            kbmouse_mouse_report.wheel,
                                            kbmouse_mouse_report.pan);

    return kb_ok || mouse_ok;
}

// Special handling for when no new input - still need to send mouse for continuous movement
bool kbmouse_mode_send_idle_mouse(void)
{
    if (!tud_hid_n_ready(ITF_NUM_HID_MOUSE)) return false;

    return tud_hid_n_mouse_report(ITF_NUM_HID_MOUSE, 0,
                                   kbmouse_mouse_report.buttons,
                                   kbmouse_mouse_report.x,
                                   kbmouse_mouse_report.y,
                                   kbmouse_mouse_report.wheel,
                                   kbmouse_mouse_report.pan);
}

static void kbmouse_mode_handle_output(uint8_t report_id, const uint8_t* data, uint16_t len)
{
    // Keyboard LED output report (1 byte)
    // bit 0 = NumLock, bit 1 = CapsLock, bit 2 = ScrollLock
    // In composite mode, report_id is 0 (no report IDs in standalone descriptors)
    (void)report_id;
    if (len >= 1) {
        kbmouse_set_led_state(data[0]);
    }
}

static const uint8_t* kbmouse_mode_get_device_descriptor(void)
{
    // Share SInput device descriptor (same composite USB device)
    return (const uint8_t*)&sinput_device_descriptor;
}

static const uint8_t* kbmouse_mode_get_config_descriptor(void)
{
    // Composite config descriptor is built in usbd.c (desc_configuration_sinput)
    return NULL;
}

static const uint8_t* kbmouse_mode_get_report_descriptor(void)
{
    // Not used - composite mode routes by interface in tud_hid_descriptor_report_cb
    return NULL;
}

// ============================================================================
// MODE EXPORT
// ============================================================================

const usbd_mode_t kbmouse_mode = {
    .name = "KB/Mouse",
    .mode = USB_OUTPUT_MODE_KEYBOARD_MOUSE,

    .get_device_descriptor = kbmouse_mode_get_device_descriptor,
    .get_config_descriptor = kbmouse_mode_get_config_descriptor,
    .get_report_descriptor = kbmouse_mode_get_report_descriptor,

    .init = kbmouse_mode_init,
    .send_report = kbmouse_mode_send_report,
    .is_ready = kbmouse_mode_is_ready,

    .handle_output = kbmouse_mode_handle_output,
    .get_rumble = NULL,
    .get_feedback = NULL,
    .get_report = NULL,

    .get_class_driver = NULL,  // Uses built-in HID class driver
    .task = NULL,
};
