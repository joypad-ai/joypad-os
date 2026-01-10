// app.c - USB2DC App Entry Point
// USB/Bluetooth to Dreamcast adapter
//
// Routes USB HID/XInput and Bluetooth controller inputs to
// Dreamcast Maple Bus output.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/dreamcast/dreamcast_device.h"
#include "usb/usbh/usbh.h"
#include <stdio.h>

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
    &dreamcast_output_interface,
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
    printf("[app:usb2dc] Initializing USB2DC v%s\n", APP_VERSION);

    // Configure router
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_DREAMCAST] = DREAMCAST_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add route: USB → Dreamcast
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_DREAMCAST, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:usb2dc] Initialization complete\n");
    printf("[app:usb2dc]   Routing: MERGE_BLEND (all USB → single DC port)\n");
    printf("[app:usb2dc]   Player slots: %d\n", MAX_PLAYER_SLOTS);
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // Forward rumble from Dreamcast to USB controllers
    if (dreamcast_output_interface.get_rumble) {
        uint8_t rumble = dreamcast_output_interface.get_rumble();
        for (int i = 0; i < playersCount; i++) {
            feedback_set_rumble(i, rumble, rumble);
        }
    }
}
