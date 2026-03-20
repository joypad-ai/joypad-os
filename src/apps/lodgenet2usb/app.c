// app.c - LodgeNet2USB App Entry Point
// LodgeNet hotel gaming controller to USB HID gamepad adapter
//
// Reads LodgeNet N64/GameCube controllers via proprietary 3-wire serial
// protocol and routes input to USB device output.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "native/host/lodgenet/lodgenet_host.h"
#include "core/services/leds/leds.h"
#include <stdio.h>

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
    printf("[app:lodgenet2usb] Initializing LodgeNet2USB v%s\n", APP_VERSION);

    // Initialize LodgeNet host driver
    lodgenet_host_init_pins(LODGENET_PIN_CLOCK, LODGENET_PIN_DATA);

    // Configure router for LodgeNet -> USB routing
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_NONE,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add route: LodgeNet input -> USB Device output
    router_add_route(INPUT_SOURCE_NATIVE_LODGENET, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:lodgenet2usb] Initialization complete\n");
    printf("[app:lodgenet2usb]   Routing: LodgeNet -> USB HID Gamepad\n");
    printf("[app:lodgenet2usb]   Pins: CLOCK=%d DATA=%d\n",
           LODGENET_PIN_CLOCK, LODGENET_PIN_DATA);
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // Update LED color when USB output mode changes
    static usb_output_mode_t last_led_mode = USB_OUTPUT_MODE_COUNT;
    usb_output_mode_t mode = usbd_get_mode();
    if (mode != last_led_mode) {
        uint8_t r, g, b;
        usbd_get_mode_color(mode, &r, &g, &b);
        leds_set_color(r, g, b);
        last_led_mode = mode;
    }
}
