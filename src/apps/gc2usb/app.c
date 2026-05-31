// app.c - GC2USB App Entry Point
// GameCube controller to USB HID gamepad adapter
//
// This app polls native GameCube controllers via joybus and outputs USB HID gamepad.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/profiles/profile.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "native/host/gc/gc_host.h"
#include "core/services/leds/leds.h"
#include "core/services/storage/flash.h"
#include "core/buttons.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include <stdio.h>

// ============================================================================
// BUTTON EVENT HANDLER (BOOTSEL on Pico, GP11 on KB2040 — see CMakeLists)
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_DOUBLE_CLICK: {
            usb_output_mode_t next = usbd_get_next_mode();
            printf("[app:gc2usb] Double-click - USB mode → %s\n",
                   usbd_get_mode_name(next));
            usbd_set_mode(next);
            break;
        }
        case BUTTON_EVENT_TRIPLE_CLICK:
            printf("[app:gc2usb] Triple-click - reset to HID mode\n");
            usbd_reset_to_hid();
            break;
        default:
            break;
    }
}

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &gc_input_interface,
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
    // Overclock to 130MHz so the joybus PIO clock divider is exactly 13.0
    // (clean integer, no jitter). At the default 125MHz the divider is 12.5
    // which introduces fractional jitter that corrupts bits during the GBA
    // multiboot upload. Reference: joybus-pio examples all run at 130MHz.
    set_sys_clock_khz(130000, true);
    // Re-init stdio so UART baud divisor recomputes for the new sys_clk
    // (otherwise serial output is garbled at the wrong baud rate).
    stdio_init_all();

    printf("[app:gc2usb] Initializing GC2USB v%s (sys_clk=130MHz)\n", APP_VERSION);

    // Configure router for GC -> USB routing
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);

    // Restore the user's last-saved D-pad mode + shoulder swap from flash so
    // the on-controller hotkeys (or the web config's ROUTER.DPAD.SET) stick
    // across reboot.
    {
        flash_t flash_data;
        if (flash_load(&flash_data) && flash_data.router_saved) {
            if (flash_data.dpad_mode <= 2) {
                router_set_dpad_mode(flash_data.dpad_mode);
            }
            router_set_shoulder_swap(flash_data.shoulder_swap != 0);
        }
    }

    // Hotkey combos, applied at the router level. The d-pad-mode hotkey uses
    // a different modifier per controller because the two share the gc2usb
    // joybus port but have different buttons:
    //   - Native GameCube (layout GAMECUBE): has Start (S2), no Select (S1),
    //     so it uses S2+direction.
    //   - GBA-as-controller (layout NINTENDO_4FACE): has both; Select (S1) is
    //     rarely used in play, so it uses S1+direction.
    // Each combo is layout-gated so GBA's Start (S2) and GC's combos don't
    // cross-trigger. Action code lives in the high byte of output_mask.
    //   Slots 0-2: S2+DD/DL/DR → D-Pad / Left Stick / Right Stick  (GameCube)
    //   Slots 3-5: S1+DD/DL/DR → D-Pad / Left Stick / Right Stick  (GBA)
    //   Slot 6:    S2+DU       → A1 (Home/Guide)                   (both)
    // S2+Up is the d-pad direction left free by the mode combos (which use
    // Down/Left/Right), and S2 (Start) exists on both controllers, so the
    // Home/Guide hotkey is the same on each — no layout gate needed.
    router_set_combo(0, JP_BUTTON_S2 | JP_BUTTON_DD, (1u << 24));
    router_set_combo(1, JP_BUTTON_S2 | JP_BUTTON_DL, (2u << 24));
    router_set_combo(2, JP_BUTTON_S2 | JP_BUTTON_DR, (3u << 24));
    router_set_combo_layout(0, LAYOUT_GAMECUBE);
    router_set_combo_layout(1, LAYOUT_GAMECUBE);
    router_set_combo_layout(2, LAYOUT_GAMECUBE);

    router_set_combo(3, JP_BUTTON_S1 | JP_BUTTON_DD, (1u << 24));
    router_set_combo(4, JP_BUTTON_S1 | JP_BUTTON_DL, (2u << 24));
    router_set_combo(5, JP_BUTTON_S1 | JP_BUTTON_DR, (3u << 24));
    router_set_combo_layout(3, LAYOUT_NINTENDO_4FACE);
    router_set_combo_layout(4, LAYOUT_NINTENDO_4FACE);
    router_set_combo_layout(5, LAYOUT_NINTENDO_4FACE);

    router_set_combo(6, JP_BUTTON_S2 | JP_BUTTON_DU, JP_BUTTON_A1);

    // GBA only: Select (S1) + Up toggles swapping L1<->L2 and R1<->R2 so the
    // GBA's L/R shoulders can act as triggers (L2/R2) instead of bumpers.
    // Action 7 = toggle shoulder swap (latched, persists to flash).
    router_set_combo(7, JP_BUTTON_S1 | JP_BUTTON_DU, (7u << 24));
    router_set_combo_layout(7, LAYOUT_NINTENDO_4FACE);

    // Add route: Native GC -> USB Device
    router_add_route(INPUT_SOURCE_NATIVE_GC, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system with GC profiles
    static const profile_config_t profile_cfg = {
        .output_profiles = { NULL },
        .shared_profiles = &gc2usb_profile_set,
    };
    profile_init(&profile_cfg);

    // Button: BOOTSEL on Pico / user button on other boards. Double-click
    // cycles USB output mode, triple-click resets to HID.
    button_init();
    button_set_callback(on_button_event);

    printf("[app:gc2usb] Initialization complete\n");
    printf("[app:gc2usb]   Routing: GC -> USB HID Gamepad\n");
    printf("[app:gc2usb]   GC data pin: GPIO%d\n", GC_DATA_PIN);
    printf("[app:gc2usb]   Profiles: %d (Select+DPad to cycle)\n", gc2usb_profile_set.profile_count);
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // Reboot-to-bootloader escape hatch on the UART console.
    int c = getchar_timeout_us(0);
    if (c == 'B') reset_usb_boot(0, 0);

    // Drive button state machine (single/double/triple click + hold detection)
    button_task();

    // Update LED color when USB output mode changes
    static usb_output_mode_t last_led_mode = USB_OUTPUT_MODE_COUNT;
    usb_output_mode_t mode = usbd_get_mode();
    if (mode != last_led_mode) {
        uint8_t r, g, b;
        usbd_get_mode_color(mode, &r, &g, &b);
        leds_set_color(r, g, b);
        last_led_mode = mode;
    }

    // Forward rumble from USB host to GC controller via feedback system
    // USB device receives rumble from host PC, GC controller reads from feedback
    if (usbd_output_interface.get_feedback) {
        output_feedback_t fb;
        if (usbd_output_interface.get_feedback(&fb) && fb.dirty) {
            // Set rumble for player 0 (GC controller)
            // Pass actual values so both on AND off commands are applied
            feedback_set_rumble(0, fb.rumble_left, fb.rumble_right);
        }
    }
}
