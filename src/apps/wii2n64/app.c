// app.c - WII2N64 App Entry Point
// Wii Nunchuck / Classic / Classic Pro → N64 console output.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/host/wii/wii_host.h"
#include "native/device/n64/n64_device.h"
#include <stdio.h>

static const profile_config_t app_profile_config = {
    .output_profiles = {
        [OUTPUT_TARGET_N64] = &wii2n64_profile_set,
    },
    .shared_profiles = &wii2n64_profile_set,
};

static const InputInterface* input_interfaces[] = {
    &wii_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

extern const OutputInterface n64_output_interface;

static const OutputInterface* output_interfaces[] = {
    &n64_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

void app_init(void)
{
    printf("[app:wii2n64] Initializing wii2n64 v%s\n", APP_VERSION);

#if defined(WII_PIN_SDA2) && WII_PIN_SDA2 != 255
    wii_host_init_dual(WII_PIN_SDA, WII_PIN_SCL, WII_PIN_SDA2, WII_PIN_SCL2);
#else
    wii_host_init_pins(WII_PIN_SDA, WII_PIN_SCL);
#endif

    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_N64] = N64_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_NONE,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);
    router_add_route(INPUT_SOURCE_NATIVE_WII, OUTPUT_TARGET_N64, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    profile_init(&app_profile_config);

    printf("[app:wii2n64] Initialization complete\n");
    printf("[app:wii2n64]   Routing: Wii extension -> N64\n");
    printf("[app:wii2n64]   Wii pins: SDA=%d SCL=%d @ %uHz\n",
           WII_PIN_SDA, WII_PIN_SCL, (unsigned)WII_I2C_FREQ_HZ);
}

void app_task(void)
{
    (void)0;
}
