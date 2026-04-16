// app.c - GCUSB App Entry Point
// USB to GameCube adapter
//
// This file contains app-specific initialization and logic.
// The firmware calls app_init() after core system initialization.
//
// Mode detection:
//   GC_DATA pin HIGH (joybus pull-up from powered console) → Play mode
//                                                            (USB host → GameCube joybus output)
//   GC_DATA pin LOW  (no console: our pull-down dominates) → Config mode
//                                                            (USB device with CDC for web configuration)
//
// Detect happens BEFORE joybus PIO init claims the pin. In play mode the
// joybus init reconfigures the pin (pull-up + PIO function), overwriting
// the pull-down used here.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/leds/leds.h"
#include "native/device/gamecube/gamecube_device.h"
#include "usb/usbh/usbh.h"
#include "usb/usbd/usbd.h"
#include "pico/stdlib.h"
#include <stdio.h>

// ============================================================================
// APP PROFILE CONFIGURATION
// ============================================================================

static const profile_config_t app_profile_config = {
    .output_profiles = {
        [OUTPUT_TARGET_GAMECUBE] = &gc_profile_set,
    },
    .shared_profiles = &gc_profile_set,  // Also shared so CDC config can find profiles
};

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &usbh_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    if (gc_config_mode) {
        // Config mode: no USB host input needed
        *count = 0;
        return NULL;
    }
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

extern const OutputInterface gamecube_output_interface;

static const OutputInterface* gc_output_interfaces[] = {
    &gamecube_output_interface,
};

static const OutputInterface* cdc_output_interfaces[] = {
    &usbd_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    // Detect mode by checking GC_DATA joybus signal pin.
    // Joybus convention: console-side pull-up to 3.3V keeps the line HIGH
    // when the console is powered. With our internal pull-down (~50kΩ) the
    // ~1kΩ console pull-up easily wins → reads HIGH. With no console (or
    // console off), our pull-down dominates → reads LOW.
    //
    // Same pattern can be reused on usb2n64 (joybus) and any other adapter
    // whose console output line idles HIGH when powered.
    gpio_init(GC_DATA_PIN);
    gpio_set_dir(GC_DATA_PIN, GPIO_IN);
    gpio_pull_down(GC_DATA_PIN);
    sleep_ms(200);  // Allow line to settle / console power to stabilize

    if (!gpio_get(GC_DATA_PIN)) {
        // No console signal → config mode (USB device with CDC)
        gc_config_mode = true;
        *count = 1;
        return cdc_output_interfaces;
    }

    // GameCube signal detected → play mode (USB host → GC output).
    // joybus init will reconfigure GC_DATA_PIN (pull-up + PIO function).
    gc_config_mode = false;
    *count = sizeof(gc_output_interfaces) / sizeof(gc_output_interfaces[0]);
    return gc_output_interfaces;
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    if (gc_config_mode) {
        printf("[app:usb2gc] Config mode - CDC serial for web configuration\n");

        // Show orange LED to indicate config mode
        leds_set_color(64, 32, 0);

        // Minimal router config for CDC output
        router_config_t router_cfg = {
            .mode = ROUTING_MODE_MERGE,
            .merge_mode = MERGE_BLEND,
            .max_players_per_output = {
                [OUTPUT_TARGET_USB_DEVICE] = 1,
            },
            .merge_all_inputs = true,
        };
        router_init(&router_cfg);

        // Initialize profile system so web config can read/write profiles
        profile_init(&app_profile_config);

        return;
    }

    printf("[app:usb2gc] Initializing usb2gc v%s\n", APP_VERSION);

    // Configure router for GCUSB
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_GAMECUBE] = GAMECUBE_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,  // Merge all USB inputs to single port
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add default route: USB → GameCube
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_GAMECUBE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system with app-defined profiles
    profile_init(&app_profile_config);

    uint8_t profile_count = profile_get_count(OUTPUT_TARGET_GAMECUBE);
    const char* active_name = profile_get_name(OUTPUT_TARGET_GAMECUBE,
                                                profile_get_active_index(OUTPUT_TARGET_GAMECUBE));

    printf("[app:usb2gc] Initialization complete\n");
    printf("[app:usb2gc]   Routing: %s\n", "MERGE_BLEND (blend all USB → single GC port)");
    printf("[app:usb2gc]   Player slots: %d (FIXED mode for future 4-port)\n", MAX_PLAYER_SLOTS);
    printf("[app:usb2gc]   Profiles: %d (active: %s)\n", profile_count, active_name ? active_name : "none");
}

// ============================================================================
// APP TASK (Optional - called from main loop)
// ============================================================================

void app_task(void)
{
    if (gc_config_mode) return;  // Config mode: nothing to do here

    // Forward rumble from GameCube console to USB controllers
    if (gamecube_output_interface.get_rumble) {
        uint8_t rumble = gamecube_output_interface.get_rumble();
        for (int i = 0; i < playersCount; i++) {
            feedback_set_rumble(i, rumble, rumble);
        }
    }
}
