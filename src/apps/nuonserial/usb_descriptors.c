// usb_descriptors.c - Minimal USB CDC descriptors for Nuon Serial Adapter
//
// TinyUSB device callbacks for CDC-only USB device.
// This app doesn't use usbd.c — it provides its own minimal descriptors.

#include "tusb.h"
#include "pico/unique_id.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// DEVICE DESCRIPTOR
// ============================================================================

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x2E8A,   // Raspberry Pi
    .idProduct          = 0x10C8,   // Nuon Serial Adapter
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

const uint8_t* tud_descriptor_device_cb(void)
{
    return (const uint8_t*)&desc_device;
}

// ============================================================================
// CONFIGURATION DESCRIPTOR
// ============================================================================

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, CONFIG_TOTAL_LEN, 0, 100),
    TUD_CDC_DESCRIPTOR(0, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

const uint8_t* tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

// ============================================================================
// STRING DESCRIPTORS
// ============================================================================

static char serial_str[17];

static const char* string_desc[] = {
    [0] = "\x09\x04",             // English
    [1] = "Joypad",               // Manufacturer
    [2] = "Nuon Serial Adapter",  // Product
    [3] = serial_str,             // Serial (filled at runtime)
    [4] = "CDC",                  // CDC interface
};

static uint16_t _desc_str[32 + 1];

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc[0], 2);
        chr_count = 1;
    } else {
        if (index == 3 && serial_str[0] == 0) {
            // Fill serial number from board unique ID
            pico_unique_board_id_t board_id;
            pico_get_unique_board_id(&board_id);
            for (int i = 0; i < 8; i++) {
                sprintf(&serial_str[i * 2], "%02X", board_id.id[i]);
            }
        }

        if (index >= sizeof(string_desc) / sizeof(string_desc[0])) return NULL;
        const char* str = string_desc[index];
        if (!str) return NULL;

        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;

        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
