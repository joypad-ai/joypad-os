// app.c - Universal Controller App
// Pad buttons → USB HID Gamepad output
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "app.h"
#include "core/router/router.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/button/button.h"
#include "core/services/leds/neopixel/ws2812.h"
#include "core/services/speaker/speaker.h"
#include "pad/pad_input.h"
#include "usb/usbd/usbd.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include <stdio.h>

// ============================================================================
// CONTROLLER TYPE CONFIGURATION
// ============================================================================

#if defined(CONTROLLER_TYPE_FISHERPRICE)
    #include "pad/configs/fisherprice.h"
    #define PAD_CONFIG pad_config_fisherprice
    #define CONTROLLER_NAME "Fisher Price"
#elif defined(CONTROLLER_TYPE_FISHERPRICE_ANALOG)
    #include "pad/configs/fisherprice.h"
    #define PAD_CONFIG pad_config_fisherprice_analog
    #define CONTROLLER_NAME "Fisher Price Analog"
#elif defined(CONTROLLER_TYPE_ALPAKKA)
    #include "pad/configs/alpakka.h"
    #define PAD_CONFIG pad_config_alpakka
    #define CONTROLLER_NAME "Alpakka"
#elif defined(CONTROLLER_TYPE_MACROPAD)
    #include "pad/configs/macropad.h"
    #define PAD_CONFIG pad_config_macropad
    #define CONTROLLER_NAME "MacroPad"
#else
    #error "No controller type defined! Define one of: CONTROLLER_TYPE_FISHERPRICE, CONTROLLER_TYPE_ALPAKKA, etc."
#endif

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
            printf("[app:controller] Button click - current mode: %s\n",
                   usbd_get_mode_name(usbd_get_mode()));
            break;

        case BUTTON_EVENT_DOUBLE_CLICK: {
            printf("[app:controller] Button double-click - switching USB output mode...\n");
            // Flush USB and give stack time to transmit
            tud_task();
            sleep_ms(50);
            tud_task();

            // Cycle to next mode: HID → XInput → PS3 → PS4 → Switch → HID
            usb_output_mode_t current = usbd_get_mode();
            usb_output_mode_t next;
            switch (current) {
                case USB_OUTPUT_MODE_HID:
                    next = USB_OUTPUT_MODE_XINPUT;
                    break;
                case USB_OUTPUT_MODE_XINPUT:
                    next = USB_OUTPUT_MODE_PS3;
                    break;
                case USB_OUTPUT_MODE_PS3:
                    next = USB_OUTPUT_MODE_PS4;
                    break;
                case USB_OUTPUT_MODE_PS4:
                    next = USB_OUTPUT_MODE_SWITCH;
                    break;
                case USB_OUTPUT_MODE_SWITCH:
                default:
                    next = USB_OUTPUT_MODE_HID;
                    break;
            }
            printf("[app:controller] Switching from %s to %s\n",
                   usbd_get_mode_name(current), usbd_get_mode_name(next));
            tud_task();
            sleep_ms(50);
            tud_task();

            usbd_set_mode(next);  // This will reset the device
            break;
        }

        default:
            break;
    }
}

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &pad_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

static const OutputInterface* output_interfaces[] = {
    &usbd_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:controller] Initializing %s Controller v%s\n", CONTROLLER_NAME, APP_VERSION);

    // Initialize button service
    button_init();
    button_set_callback(on_button_event);

    // Register pad device configuration BEFORE interface init
    int dev_idx = pad_input_add_device(&PAD_CONFIG);

    if (dev_idx < 0) {
        printf("[app:controller] ERROR: Failed to register pad device!\n");
        return;
    }

    printf("[app:controller] Pad config: %s\n", PAD_CONFIG.name);

    // Set custom LED colors from pad config if defined
    if (PAD_CONFIG.led_count > 0) {
        neopixel_set_custom_colors(PAD_CONFIG.led_colors, PAD_CONFIG.led_count);
        if (neopixel_has_custom_colors()) {
            printf("[app:controller] Using custom LED colors (%d LEDs)\n", PAD_CONFIG.led_count);
        }
    }

    // Initialize speaker for haptic/rumble feedback if configured
    if (PAD_CONFIG.speaker_pin != PAD_PIN_DISABLED) {
        speaker_init(PAD_CONFIG.speaker_pin, PAD_CONFIG.speaker_enable_pin);
        printf("[app:controller] Speaker initialized for rumble feedback\n");
    }

    // Configure router for Pad → USB Device
    router_config_t router_cfg = {
        .mode = ROUTING_MODE_SIMPLE,
        .merge_mode = MERGE_PRIORITY,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = 1,
        },
        .merge_all_inputs = false,
        .transform_flags = 0,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);

    // Add route: Pad → USB Device
    router_add_route(INPUT_SOURCE_GPIO, OUTPUT_TARGET_USB_DEVICE, 0);

    printf("[app:controller] Initialization complete\n");
    printf("[app:controller]   Routing: Pad → USB Device (HID Gamepad)\n");
    printf("[app:controller]   Double-click encoder button to switch USB mode\n");
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // Process button input for mode switching
    button_task();

    // Handle rumble feedback via speaker (if initialized)
    if (speaker_is_initialized() && usbd_output_interface.get_rumble) {
        uint8_t rumble = usbd_output_interface.get_rumble();
        speaker_set_rumble(rumble);
    }
}
