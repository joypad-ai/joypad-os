// app.c - PSX2USB App Entry Point
// PS1 / PS2 controller -> USB HID gamepad adapter

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "native/host/psx/psx_host.h"
#include "core/services/leds/leds.h"
#include <stdio.h>

static const InputInterface* input_interfaces[] = {
    &psx_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

static const OutputInterface* output_interfaces[] = {
    &usbd_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

void app_init(void)
{
    printf("[app:psx2usb] Initializing PSX2USB v%s\n", APP_VERSION);

    psx_host_init_pins(PSX_PIN_CMD, PSX_PIN_CLK, PSX_PIN_ATT, PSX_PIN_DAT);

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

    router_add_route(INPUT_SOURCE_NATIVE_PSX, OUTPUT_TARGET_USB_DEVICE, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:psx2usb] Routing: PS1/PS2 -> USB HID Gamepad\n");
    printf("[app:psx2usb] Pins: CMD=GP%d CLK=GP%d ATT=GP%d DAT=GP%d\n",
           PSX_PIN_CMD, PSX_PIN_CLK, PSX_PIN_ATT, PSX_PIN_DAT);
}

void app_task(void)
{
    static usb_output_mode_t last_led_mode = USB_OUTPUT_MODE_COUNT;
    usb_output_mode_t mode = usbd_get_mode();
    if (mode != last_led_mode) {
        uint8_t r, g, b;
        usbd_get_mode_color(mode, &r, &g, &b);
        leds_set_color(r, g, b);
        last_led_mode = mode;
    }
}
