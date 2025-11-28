// usbd.c - USB device output
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Implements USB device mode for USBRetro, enabling the adapter to emulate
// a gamepad for USB-capable consoles. Uses TinyUSB device stack.

#include "usbd.h"
#include "descriptors/hid_descriptors.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "tusb.h"
#include <string.h>
#include <stdio.h>

// Current HID report
static usbretro_hid_report_t hid_report;

// Current output mode
static usb_output_mode_t output_mode = USB_OUTPUT_MODE_HID;

// ============================================================================
// CONVERSION HELPERS
// ============================================================================

// Convert input_event buttons to HID gamepad buttons
static uint16_t convert_buttons(uint32_t buttons)
{
    uint16_t hid_buttons = 0;

    // USBRetro uses active-high (1 = pressed), HID uses active-high (1 = pressed)
    // No inversion needed

    if (buttons & USBR_BUTTON_B1) hid_buttons |= USB_GAMEPAD_MASK_B1;
    if (buttons & USBR_BUTTON_B2) hid_buttons |= USB_GAMEPAD_MASK_B2;
    if (buttons & USBR_BUTTON_B3) hid_buttons |= USB_GAMEPAD_MASK_B3;
    if (buttons & USBR_BUTTON_B4) hid_buttons |= USB_GAMEPAD_MASK_B4;
    if (buttons & USBR_BUTTON_L1) hid_buttons |= USB_GAMEPAD_MASK_L1;
    if (buttons & USBR_BUTTON_R1) hid_buttons |= USB_GAMEPAD_MASK_R1;
    if (buttons & USBR_BUTTON_L2) hid_buttons |= USB_GAMEPAD_MASK_L2;
    if (buttons & USBR_BUTTON_R2) hid_buttons |= USB_GAMEPAD_MASK_R2;
    if (buttons & USBR_BUTTON_S1) hid_buttons |= USB_GAMEPAD_MASK_S1;
    if (buttons & USBR_BUTTON_S2) hid_buttons |= USB_GAMEPAD_MASK_S2;
    if (buttons & USBR_BUTTON_L3) hid_buttons |= USB_GAMEPAD_MASK_L3;
    if (buttons & USBR_BUTTON_R3) hid_buttons |= USB_GAMEPAD_MASK_R3;
    if (buttons & USBR_BUTTON_A1) hid_buttons |= USB_GAMEPAD_MASK_A1;
    if (buttons & USBR_BUTTON_A2) hid_buttons |= USB_GAMEPAD_MASK_A2;

    return hid_buttons;
}

// Convert input_event dpad to HID hat switch
static uint8_t convert_dpad_to_hat(uint32_t buttons)
{
    // USBRetro uses active-high (1 = pressed)
    uint8_t up = (buttons & USBR_BUTTON_DU) ? 1 : 0;
    uint8_t down = (buttons & USBR_BUTTON_DD) ? 1 : 0;
    uint8_t left = (buttons & USBR_BUTTON_DL) ? 1 : 0;
    uint8_t right = (buttons & USBR_BUTTON_DR) ? 1 : 0;

    if (up && right) return HID_HAT_UP_RIGHT;
    if (up && left) return HID_HAT_UP_LEFT;
    if (down && right) return HID_HAT_DOWN_RIGHT;
    if (down && left) return HID_HAT_DOWN_LEFT;
    if (up) return HID_HAT_UP;
    if (down) return HID_HAT_DOWN;
    if (left) return HID_HAT_LEFT;
    if (right) return HID_HAT_RIGHT;

    return HID_HAT_CENTER;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void usbd_init(void)
{
    printf("[usbd] Initializing USB device output\n");

    // Initialize TinyUSB device stack
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(0, &dev_init);

    // Initialize HID report to neutral state
    memset(&hid_report, 0, sizeof(usbretro_hid_report_t));
    hid_report.lx = 128;  // Center
    hid_report.ly = 128;
    hid_report.rx = 128;
    hid_report.ry = 128;
    hid_report.hat = HID_HAT_CENTER;

    printf("[usbd] Initialization complete\n");
}

void usbd_task(void)
{
    // TinyUSB device task - runs from core0 main loop
    tud_task();

    // Send HID report if device is ready
    if (tud_hid_ready()) {
        usbd_send_report(0);  // Player 1
    }
}

bool usbd_send_report(uint8_t player_index)
{
    if (!tud_hid_ready()) {
        return false;
    }

    // Get input from router
    const input_event_t* event = router_get_output(OUTPUT_TARGET_USB_DEVICE, player_index);

    if (event) {
        // Convert input event to HID report
        uint16_t buttons = convert_buttons(event->buttons);
        hid_report.buttons = buttons;
        hid_report.hat = convert_dpad_to_hat(event->buttons);

        // Analog sticks (input_event uses 0-255, HID uses 0-255)
        hid_report.lx = event->analog[ANALOG_X];
        hid_report.ly = event->analog[ANALOG_Y];
        hid_report.rx = event->analog[ANALOG_Z];
        hid_report.ry = event->analog[ANALOG_RZ];

        // PS3 pressure axes (0x00 = released, 0xFF = fully pressed)
        // USBRetro uses active-high (1 = pressed)
        hid_report.pressure_dpad_right = (event->buttons & USBR_BUTTON_DR) ? 0xFF : 0x00;
        hid_report.pressure_dpad_left  = (event->buttons & USBR_BUTTON_DL) ? 0xFF : 0x00;
        hid_report.pressure_dpad_up    = (event->buttons & USBR_BUTTON_DU) ? 0xFF : 0x00;
        hid_report.pressure_dpad_down  = (event->buttons & USBR_BUTTON_DD) ? 0xFF : 0x00;
        hid_report.pressure_triangle   = (buttons & USB_GAMEPAD_MASK_B4) ? 0xFF : 0x00;
        hid_report.pressure_circle     = (buttons & USB_GAMEPAD_MASK_B2) ? 0xFF : 0x00;
        hid_report.pressure_cross      = (buttons & USB_GAMEPAD_MASK_B1) ? 0xFF : 0x00;
        hid_report.pressure_square     = (buttons & USB_GAMEPAD_MASK_B3) ? 0xFF : 0x00;
        hid_report.pressure_l1         = (buttons & USB_GAMEPAD_MASK_L1) ? 0xFF : 0x00;
        hid_report.pressure_r1         = (buttons & USB_GAMEPAD_MASK_R1) ? 0xFF : 0x00;
        hid_report.pressure_l2         = (buttons & USB_GAMEPAD_MASK_L2) ? 0xFF : 0x00;
        hid_report.pressure_r2         = (buttons & USB_GAMEPAD_MASK_R2) ? 0xFF : 0x00;
    } else {
        // No input - send neutral state
        memset(&hid_report, 0, sizeof(hid_report));
        hid_report.hat = HID_HAT_CENTER;
        hid_report.lx = 128;
        hid_report.ly = 128;
        hid_report.rx = 128;
        hid_report.ry = 128;
    }

    return tud_hid_report(0, &hid_report, sizeof(hid_report));
}

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

const OutputInterface usbd_output_interface = {
    .name = "USB",
    .target = OUTPUT_TARGET_USB_DEVICE,
    .init = usbd_init,
    .task = usbd_task,
    .core1_task = NULL,  // Runs from core0 task - doesn't need dedicated core
    .get_rumble = NULL,  // TODO: Implement rumble from USB host
    .get_player_led = NULL,
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
};

// ============================================================================
// TINYUSB DEVICE CALLBACKS
// ============================================================================

// Device descriptor
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // USB 2.0
    .bDeviceClass       = 0x00,    // Composite device
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_DEVICE_VENDOR_ID,
    .idProduct          = USB_DEVICE_PRODUCT_ID,
    .bcdDevice          = USB_DEVICE_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

// Configuration descriptor
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_HID 0x81

static const uint8_t desc_configuration[] = {
    // Config: bus powered, max 100mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // Interface: HID gamepad
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, sizeof(hid_report_descriptor), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 1)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

// String descriptors
static const char *string_desc_arr[] = {
    (const char[]){ 0x09, 0x04 },  // 0: Language (0x0409 = English)
    USB_STRING_MANUFACTURER,        // 1: Manufacturer
    USB_STRING_PRODUCT,             // 2: Product
    USB_STRING_SERIAL,              // 3: Serial number
};

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    static uint16_t _desc_str[32];

    uint8_t chr_count;
    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
            return NULL;
        }

        const char *str = string_desc_arr[index];
        chr_count = strlen(str);
        if (chr_count > 31) chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    // First byte is length (in bytes), second byte is descriptor type
    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return _desc_str;
}

// HID Callbacks
uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf)
{
    (void)itf;
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)itf;
    (void)report_id;
    (void)report_type;

    uint16_t len = sizeof(usbretro_hid_report_t);
    if (reqlen < len) len = reqlen;
    memcpy(buffer, &hid_report, len);
    return len;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void)itf;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
    // TODO: Handle rumble feedback from USB host
}
