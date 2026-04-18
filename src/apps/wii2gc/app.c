// app.c - WII2GC App Entry Point
// Wii extension controllers (Nunchuck / Classic / Classic Pro) to GameCube
// joybus. When no GameCube console 3V3 is detected on the data pin, falls
// back to USB CDC config mode so the web config can reach the device.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/leds/leds.h"
#include "native/host/wii/wii_host.h"
#include "native/device/gamecube/gamecube_device.h"
#include "usb/usbd/usbd.h"
#include "pico/stdlib.h"
#include <stdio.h>

// ============================================================================
// PROFILE CONFIGURATION
// ============================================================================

static const profile_config_t app_profile_config = {
    .output_profiles = {
        [OUTPUT_TARGET_GAMECUBE] = &wii2gc_profile_set,
    },
    .shared_profiles = &wii2gc_profile_set,
};

// ============================================================================
// INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &wii_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    if (gc_config_mode) {
        // Config mode: no input driver required — device acts as a CDC
        // endpoint for the web-config tool.
        *count = 0;
        return NULL;
    }
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

// ============================================================================
// OUTPUT INTERFACES
// ============================================================================

extern const OutputInterface gamecube_output_interface;

static const OutputInterface* gc_outputs[]  = { &gamecube_output_interface };
static const OutputInterface* cdc_outputs[] = { &usbd_output_interface };

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    // Same detection pattern as usb2gc: probe the GC data pin for 3V3 — if
    // the console is powered and plugged in, go to play mode; otherwise
    // boot as a USB device for web-config access.
    gpio_init(GC_3V3_PIN);
    gpio_set_dir(GC_3V3_PIN, GPIO_IN);
    gpio_pull_down(GC_3V3_PIN);
    sleep_ms(200);

    if (!gpio_get(GC_3V3_PIN)) {
        gc_config_mode = true;
        *count = 1;
        return cdc_outputs;
    }

    gc_config_mode = false;
    *count = sizeof(gc_outputs) / sizeof(gc_outputs[0]);
    return gc_outputs;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void app_init(void)
{
    if (gc_config_mode) {
        printf("[app:wii2gc] Config mode - CDC serial for web configuration\n");
        leds_set_color(64, 32, 0);

        router_config_t router_cfg = {
            .mode = ROUTING_MODE_MERGE,
            .merge_mode = MERGE_BLEND,
            .max_players_per_output = {
                [OUTPUT_TARGET_USB_DEVICE] = 1,
            },
            .merge_all_inputs = true,
        };
        router_init(&router_cfg);
        profile_init(&app_profile_config);
        return;
    }

    printf("[app:wii2gc] Initializing wii2gc v%s\n", APP_VERSION);

#if defined(WII_PIN_SDA2) && WII_PIN_SDA2 != 255
    wii_host_init_dual(WII_PIN_SDA, WII_PIN_SCL, WII_PIN_SDA2, WII_PIN_SCL2);
#else
    wii_host_init_pins(WII_PIN_SDA, WII_PIN_SCL);
#endif

    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_GAMECUBE] = GAMECUBE_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_NONE,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);
    router_add_route(INPUT_SOURCE_NATIVE_WII, OUTPUT_TARGET_GAMECUBE, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    profile_init(&app_profile_config);

    printf("[app:wii2gc] Initialization complete\n");
    printf("[app:wii2gc]   Routing: Wii extension -> GameCube\n");
    printf("[app:wii2gc]   Wii pins: SDA=%d SCL=%d @ %uHz\n",
           WII_PIN_SDA, WII_PIN_SCL, (unsigned)WII_I2C_FREQ_HZ);
}

// ============================================================================
// TASK (Wii accessories have no rumble, so nothing to forward)
// ============================================================================

void app_task(void)
{
    (void)0;
}
