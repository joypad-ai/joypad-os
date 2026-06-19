// app.c - LodgeNet2N64 App Entry Point
// LodgeNet hotel gaming controller to N64 console adapter
//
// Reads LodgeNet N64/GameCube/SNES controllers via proprietary serial
// protocol and routes input to N64 console via joybus PIO protocol.
//
// PIO allocation:
//   LodgeNet host: PIO1 (lodgenet.pio programs, swapped at runtime)
//   N64 device:    PIO0 (joybus.pio, Core 1 timing-critical)

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/host/lodgenet/lodgenet_host.h"
#include "native/device/n64/n64_device.h"
#include "core/services/leds/leds.h"
#include "platform/platform.h"
#include "pico/bootrom.h"
#include <stdio.h>

// Stubs for CDC commands (lodgenet2n64 has no USB device stack)
void cdc_commands_send_input_event(uint32_t buttons, const uint8_t* axes) { (void)buttons; (void)axes; }
void cdc_commands_send_output_event(uint32_t buttons, const uint8_t* axes) { (void)buttons; (void)axes; }
void cdc_commands_send_connect_event(uint8_t port, const char* name, uint16_t vid, uint16_t pid) { (void)port; (void)name; (void)vid; (void)pid; }
void cdc_commands_send_disconnect_event(uint8_t port) { (void)port; }

// N64 device diagnostics
extern volatile bool n64_console_active;
extern volatile bool n64_router_has_data;
extern volatile bool n64_player_assigned;

// ============================================================================
// LED STATUS
// ============================================================================

static uint32_t led_last_toggle = 0;
static bool led_state = false;

// LED patterns:
//   Fast blink  (100ms): N64 console not communicating
//   Slow blink  (400ms): N64 OK but no LodgeNet controller connected
//   Solid on:            N64 OK + controller connected + data flowing
static void led_status_update(void)
{
    uint32_t now = platform_time_ms();

    if (!n64_console_active) {
        // N64 console not communicating - fast blink
        if (now - led_last_toggle >= 100) {
            led_state = !led_state;
            gpio_put(BOARD_LED_PIN, led_state);
            led_last_toggle = now;
        }
    } else if (lodgenet_host_is_connected()) {
        // N64 OK + controller connected - solid on
        if (!led_state) {
            gpio_put(BOARD_LED_PIN, true);
            led_state = true;
        }
    } else {
        // N64 OK but no controller - slow blink
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
        [OUTPUT_TARGET_N64] = &n64_profile_set,
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

extern const OutputInterface n64_output_interface;

static const OutputInterface* output_interfaces[] = {
    &n64_output_interface,
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
    // N64 late init: flash, profiles, GPIO — deferred from n64_init() so Core 1
    // could start listening for console probes as early as possible.
    n64_late_init();

    printf("[app:lodgenet2n64] Initializing LodgeNet2N64 v%s\n", JOYPAD_VERSION);

    // Initialize LodgeNet host driver with custom pins
    lodgenet_host_init_pins(LODGENET_PIN_CLOCK, LODGENET_PIN_DATA,
                            LODGENET_PIN_CLOCK2, LODGENET_PIN_VCC);

    // Initialize board LED
    gpio_init(BOARD_LED_PIN);
    gpio_set_dir(BOARD_LED_PIN, GPIO_OUT);

    // Configure router for LodgeNet -> N64 routing
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_N64] = N64_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);

    // Add route: LodgeNet input -> N64 output
    router_add_route(INPUT_SOURCE_NATIVE_LODGENET, OUTPUT_TARGET_N64, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system
    profile_init(&app_profile_config);

    printf("[app:lodgenet2n64] Initialization complete\n");
    printf("[app:lodgenet2n64]   Routing: LodgeNet -> N64\n");
    printf("[app:lodgenet2n64]   LodgeNet pins: CLK=%d DATA=%d VCC=%d CLK2=%d\n",
           LODGENET_PIN_CLOCK, LODGENET_PIN_DATA, LODGENET_PIN_VCC, LODGENET_PIN_CLOCK2);
    printf("[app:lodgenet2n64]   N64 joybus pin: GPIO%d\n", N64_DATA_PIN);
}

// ============================================================================
// APP TASK (Called from main loop)
// ============================================================================

// Profile indices (must match order in profiles.h)
#define PROFILE_N64       0
#define PROFILE_GC        1
#define PROFILE_SNES_DPAD 2
#define PROFILE_SNES_STICK 3

static bool snes_stick_mode = false;

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
            case LODGENET_DEVICE_N64:
                profile_set_active(OUTPUT_TARGET_N64, PROFILE_N64);
                snes_stick_mode = false;
                printf("[app:lodgenet2n64] Profile: N64\n");
                break;
            case LODGENET_DEVICE_GC:
                profile_set_active(OUTPUT_TARGET_N64, PROFILE_GC);
                snes_stick_mode = false;
                printf("[app:lodgenet2n64] Profile: GC\n");
                break;
            case LODGENET_DEVICE_SNES:
                snes_stick_mode = false;
                profile_set_active(OUTPUT_TARGET_N64, PROFILE_SNES_DPAD);
                printf("[app:lodgenet2n64] Profile: SNES (d-pad)\n");
                break;
            default:
                break;
        }
    }
    if (type == LODGENET_DEVICE_NONE) {
        last_type = LODGENET_DEVICE_NONE;
    }

    // SNES Order button (A2) toggles between d-pad and stick mode
    if (type == LODGENET_DEVICE_SNES) {
        static bool order_was_pressed = false;
        uint32_t raw_buttons = lodgenet_host_get_buttons();
        bool order_pressed = (raw_buttons & JP_BUTTON_A2) != 0;
        if (order_pressed && !order_was_pressed) {
            snes_stick_mode = !snes_stick_mode;
            profile_set_active(OUTPUT_TARGET_N64,
                snes_stick_mode ? PROFILE_SNES_STICK : PROFILE_SNES_DPAD);
            printf("[app:lodgenet2n64] SNES mode: %s\n",
                   snes_stick_mode ? "stick" : "d-pad");
        }
        order_was_pressed = order_pressed;
    }

    // Update LED status
    led_status_update();
}
