// app.c - USB2NEOGEO TE App Entry Point
//
// Stripped-down usb2neogeo for tournament use:
//   - Single default profile, no profile switching
//   - Consecutive remap only (SELECT held 2s then press 6 buttons)
//   - Runtime mapping auto-clears on controller disconnect
//   - No alt remap mode, no auto fire

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/services/profiles/runtime_profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/gpio/gpio_device.h"
#include "usb/usbh/usbh.h"
#include <stdio.h>

static gpio_device_config_t gpio_gpio_config[GPIO_MAX_PLAYERS] = {
    [0] = {
        .pin_du = P1_NEOGEO_DU_PIN,
        .pin_dd = P1_NEOGEO_DD_PIN,
        .pin_dl = P1_NEOGEO_DL_PIN,
        .pin_dr = P1_NEOGEO_DR_PIN,

        // Action Buttons
        .pin_b1 = P1_NEOGEO_B4_PIN,
        .pin_b2 = P1_NEOGEO_B5_PIN,
        .pin_b3 = P1_NEOGEO_B1_PIN,
        .pin_b4 = P1_NEOGEO_B2_PIN,
        .pin_l1 = GPIO_DISABLED,
        .pin_r1 = P1_NEOGEO_B3_PIN,
        .pin_l2 = GPIO_DISABLED,
        .pin_r2 = P1_NEOGEO_B6_PIN,

        // Meta Buttons
        .pin_s1 = P1_NEOGEO_S1_PIN,
        .pin_s2 = P1_NEOGEO_S2_PIN,
        .pin_a1 = GPIO_DISABLED,
        .pin_a2 = GPIO_DISABLED,

        // Extra Buttons
        .pin_l3 = GPIO_DISABLED,
        .pin_r3 = GPIO_DISABLED,
        .pin_l4 = GPIO_DISABLED,
        .pin_r4 = GPIO_DISABLED,
    },
    [1] = PORT_CONFIG_INIT
};

// ============================================================================
// APP PROFILE CONFIGURATION
// ============================================================================

static const profile_config_t app_profile_config = {
    .output_profiles = {
        [OUTPUT_TARGET_GPIO] = &neogeo_te_profile_set,
    },
    .shared_profiles = NULL,
};

static const runtime_profile_config_t app_runtime_profile_config = {
    .output_configs = {
        [OUTPUT_TARGET_GPIO] = &neogeo_te_runtime_output_config,
    },
};

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

extern const OutputInterface gpio_output_interface;

static const OutputInterface* output_interfaces[] = {
    &gpio_output_interface,
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
    printf("[app:usb2neogeo_te] Initializing USB2NEOGEO TE v%s\n", JOYPAD_VERSION);

    gpio_device_init_pins(gpio_gpio_config, false);

    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_GPIO] = NEOGEO_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_GPIO, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    profile_init(&app_profile_config);
    runtime_profile_init(&app_runtime_profile_config);

    printf("[app:usb2neogeo_te] Initialization complete\n");
    printf("[app:usb2neogeo_te]   Single profile: default 1L6B layout\n");
    printf("[app:usb2neogeo_te]   Remap: consecutive mode only, clears on disconnect\n");
}

// ============================================================================
// APP TASK (called in main loop)
// ============================================================================

void app_task(void)
{
    // No app-specific task work needed
}
