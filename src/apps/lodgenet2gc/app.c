// app.c - LodgeNet2GC App Entry Point
// LodgeNet hotel gaming controller to GameCube console adapter
//
// Reads LodgeNet N64/GameCube/SNES controllers via proprietary serial
// protocol and routes input to GameCube console via joybus PIO protocol.
//
// PIO allocation:
//   LodgeNet host: PIO1 (lodgenet.pio programs, swapped at runtime)
//   GC device:     PIO0 (joybus.pio, Core 1 timing-critical)

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/host/lodgenet/lodgenet_host.h"
#include "native/device/gamecube/gamecube_device.h"
#include "core/services/leds/leds.h"
#include "platform/platform.h"
#include "pico/bootrom.h"
#include <stdio.h>

// CDC commands provided by USB_DEVICE_SOURCES (cdc_commands.c)

// ============================================================================
// LED STATUS
// ============================================================================

static uint32_t led_last_toggle = 0;
static bool led_state = false;

// LED patterns:
//   Fast blink  (100ms): GC console not communicating
//   Slow blink  (400ms): GC OK but no LodgeNet controller connected
//   Solid on:            GC OK + controller connected
static void led_status_update(void)
{
    uint32_t now = platform_time_ms();
    // GC console detection: if joybus is polling, Core 1 is running
    // Use a simple heuristic — if lodgenet controller is connected and
    // data is flowing, show solid; otherwise blink.

    if (lodgenet_host_is_connected()) {
        if (!led_state) {
            gpio_put(BOARD_LED_PIN, true);
            led_state = true;
        }
    } else {
        // No controller - slow blink
        if (now - led_last_toggle >= 400) {
            led_state = !led_state;
            gpio_put(BOARD_LED_PIN, led_state);
            led_last_toggle = now;
        }
    }
}

// ============================================================================
// APP PROFILE CONFIGURATION
// ============================================================================

static const profile_config_t app_profile_config = {
    .output_profiles = {
        [OUTPUT_TARGET_GAMECUBE] = &gc_profile_set,
    },
    .shared_profiles = NULL,
};

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &lodgenet_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

extern const OutputInterface gamecube_output_interface;

static const OutputInterface* output_interfaces[] = {
    &gamecube_output_interface,
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
    printf("[app:lodgenet2gc] Initializing LodgeNet2GC v%s\n", JOYPAD_VERSION);

    // Initialize LodgeNet host driver with custom pins
    lodgenet_host_init_pins(LODGENET_PIN_CLOCK, LODGENET_PIN_DATA,
                            LODGENET_PIN_CLOCK2, LODGENET_PIN_VCC);

    // Initialize board LED
    gpio_init(BOARD_LED_PIN);
    gpio_set_dir(BOARD_LED_PIN, GPIO_OUT);

    // Configure router for LodgeNet -> GameCube routing
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_GAMECUBE] = GAMECUBE_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);

    // Add route: LodgeNet input -> GameCube output
    router_add_route(INPUT_SOURCE_NATIVE_LODGENET, OUTPUT_TARGET_GAMECUBE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system
    profile_init(&app_profile_config);

    printf("[app:lodgenet2gc] Initialization complete\n");
    printf("[app:lodgenet2gc]   Routing: LodgeNet -> GameCube\n");
    printf("[app:lodgenet2gc]   LodgeNet pins: CLK=%d DATA=%d VCC=%d CLK2=%d\n",
           LODGENET_PIN_CLOCK, LODGENET_PIN_DATA, LODGENET_PIN_VCC, LODGENET_PIN_CLOCK2);
    printf("[app:lodgenet2gc]   GC joybus pin: GPIO%d\n", GC_DATA_PIN);
}

// ============================================================================
// APP TASK (Called from main loop)
// ============================================================================

// Profile indices (must match order in profiles.h)
#define PROFILE_GC   0
#define PROFILE_N64  1
#define PROFILE_SNES 2

void app_task(void)
{
    // Check for bootloader command on CDC serial ('B' = reboot to bootloader)
    int c = getchar_timeout_us(0);
    if (c == 'B') {
        reset_usb_boot(0, 0);
    }

    // Auto-switch profile based on detected LodgeNet controller type
    static lodgenet_device_t last_type = LODGENET_DEVICE_NONE;
    lodgenet_device_t type = lodgenet_host_get_device_type();
    if (type != last_type && type != LODGENET_DEVICE_NONE) {
        last_type = type;
        switch (type) {
            case LODGENET_DEVICE_GC:
                profile_set_active(OUTPUT_TARGET_GAMECUBE, PROFILE_GC);
                printf("[app:lodgenet2gc] Profile: GC\n");
                break;
            case LODGENET_DEVICE_N64:
                profile_set_active(OUTPUT_TARGET_GAMECUBE, PROFILE_N64);
                printf("[app:lodgenet2gc] Profile: N64\n");
                break;
            case LODGENET_DEVICE_SNES:
                profile_set_active(OUTPUT_TARGET_GAMECUBE, PROFILE_SNES);
                printf("[app:lodgenet2gc] Profile: SNES\n");
                break;
            default:
                break;
        }
    }
    if (type == LODGENET_DEVICE_NONE) {
        last_type = LODGENET_DEVICE_NONE;
    }

    // Update LED status
    led_status_update();
}
