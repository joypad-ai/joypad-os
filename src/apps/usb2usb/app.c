// app.c - USB2USB App Entry Point
// USB to USB HID gamepad adapter
//
// This file contains app-specific initialization and logic.
// The firmware calls app_init() after core system initialization.

#include "app.h"
#include "core/router/router.h"

// Build info fallbacks (normally set by CMake)
#ifndef GIT_COMMIT
#define GIT_COMMIT "not-defined"
#endif
#ifndef BUILD_TIME
#define BUILD_TIME "not-defined"
#endif
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbh/usbh.h"
#include "usb/usbd/usbd.h"

#include "bt/btstack/btstack_host.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include <stdio.h>

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
            printf("[app:usb2usb] Button click - current mode: %s\n",
                   usbd_get_mode_name(usbd_get_mode()));
            // Debug: print build info
            printf("[DEBUG] GIT_COMMIT: '%s'\n", GIT_COMMIT);
            printf("[DEBUG] BUILD_TIME: '%s'\n", BUILD_TIME);
            break;

        case BUTTON_EVENT_DOUBLE_CLICK: {
            // Double-click to cycle USB output mode
            printf("[app:usb2usb] Button double-click - switching USB output mode...\n");
            // Flush CDC and give USB stack time to transmit
            tud_task();
            sleep_ms(50);
            tud_task();

            // Cycle to next mode: HID → XInput → PS3 → PS4 → Switch → HID
            // (Skip PS Classic, Xbox modes - less common/Mac-incompatible)
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
            printf("[app:usb2usb] Switching from %s to %s\n",
                   usbd_get_mode_name(current), usbd_get_mode_name(next));
            tud_task();
            sleep_ms(50);
            tud_task();

            usbd_set_mode(next);  // This will reset the device
            break;
        }

        case BUTTON_EVENT_HOLD:
            // Long press to clear all Bluetooth bonds
            btstack_host_delete_all_bonds();
            break;

        default:
            break;
    }
}

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &usbh_input_interface,
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
    printf("[app:usb2usb] Initializing USB2USB v%s\n", JOYPAD_VERSION);

    // Initialize button service
    button_init();
    button_set_callback(on_button_event);

    // Configure router for USB2USB
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,  // Merge all USB inputs to single output
        .transform_flags = TRANSFORM_FLAGS,
        // Mouse-to-analog: Map mouse to right stick for camera control
        // Useful for accessibility (mouthpad, head tracker) alongside gamepad
        .mouse_target_x = ANALOG_Z,             // Right stick X
        .mouse_target_y = MOUSE_AXIS_DISABLED,  // Y disabled (X-only for camera pan)
        .mouse_drain_rate = 0,                  // No drain - hold position until head returns
    };
    router_init(&router_cfg);

    // Add default route: USB Host → USB Device
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:usb2usb] Initialization complete\n");
    printf("[app:usb2usb]   Routing: USB Host → USB Device (HID Gamepad)\n");
    printf("[app:usb2usb]   Player slots: %d\n", MAX_PLAYER_SLOTS);
    printf("[app:usb2usb]   Double-click button (GPIO7) to switch USB mode\n");
}

// ============================================================================
// APP TASK (Optional - called from main loop)
// ============================================================================

void app_task(void)
{
    // Process button input
    button_task();

    // Route feedback from USB device output to USB host input controllers
    // The output interface receives rumble/LED from the console/host
    // and we forward it to connected controllers via the feedback system
    if (usbd_output_interface.get_feedback) {
        output_feedback_t fb;
        if (usbd_output_interface.get_feedback(&fb)) {
            // Set feedback for all active players
            for (int i = 0; i < playersCount; i++) {
                feedback_set_rumble(i, fb.rumble_left, fb.rumble_right);
                if (fb.led_player > 0) {
                    feedback_set_led_player(i, fb.led_player);
                }
                if (fb.led_r || fb.led_g || fb.led_b) {
                    feedback_set_led_rgb(i, fb.led_r, fb.led_g, fb.led_b);
                }
            }
        }
    }
}
