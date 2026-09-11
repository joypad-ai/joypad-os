// app.c - 24G2USB App Entry Point
// 8BitDo SF30 2.4G wireless receiver to USB HID gamepad adapter
//
// Drives an nRF24L01+ over SPI, impersonating the 8BitDo SF30 2.4G dongle
// well enough that SF30 2.4G controllers pair and link to it, and routes
// linked controllers to USB device output.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "rf24g_host.h"
#include "core/services/leds/leds.h"
#include "platform/platform.h"
#include <stdio.h>

#include "tusb.h"

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &rf24g_input_interface,
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
// BUTTON EVENT HANDLER (BOOTSEL)
// ============================================================================
//
// Gesture map:
//   DOUBLE_CLICK -> cycle USB output mode (SInput/XInput/PS3/PS4/Switch/KB-Mouse)
//   TRIPLE_CLICK  -> reset to default HID mode
//   HOLD (~1.5s)  -> begin radio pairing
//
// DOUBLE_CLICK/TRIPLE_CLICK are already the house convention for output-mode
// switching (see pce2usb, bt2usb), so HOLD is the gesture left free for
// pairing here -- it doesn't collide with either.

static void on_button_event(button_event_t event)
{
    switch (event) {

        case BUTTON_EVENT_DOUBLE_CLICK: {
            usb_output_mode_t next = usbd_get_next_mode();
            printf("[app:24g2usb] Double-click - switching USB mode -> %s\n",
                   usbd_get_mode_name(next));
            usbd_set_mode(next);
            break;
        }

        case BUTTON_EVENT_TRIPLE_CLICK:
            printf("[app:24g2usb] Triple-click - resetting to HID mode...\n");
            if (!usbd_reset_to_hid()) {
                printf("[app:24g2usb] Already in HID mode\n");
            }
            break;

        case BUTTON_EVENT_HOLD: {
            // The receiver's address is permanent and its pipe is always
            // open (see rf24g_host.c's apply_pipe_addresses()), so
            // re-offering it is always a legitimate re-pair (a replacement
            // controller, or the same one after a factory reset), not an
            // error -- including when the controller currently linked is
            // the one being re-paired.
            if (rf24g_host_is_pairing()) {
                printf("[app:24g2usb] Hold - pairing already in progress\n");
                break;
            }
            printf("[app:24g2usb] Hold - starting pairing...\n");
            rf24g_host_begin_pairing();
            break;
        }

        default:
            break;
    }
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:24g2usb] Initializing 24G2USB v%s\n", JOYPAD_VERSION);

    button_init();
    button_set_callback(on_button_event);

    rf24g_host_init_pins(RF24G_PIN_SCK, RF24G_PIN_MOSI, RF24G_PIN_MISO,
                         RF24G_PIN_CSN, RF24G_PIN_CE, RF24G_PIN_IRQ);

    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_NONE,
    };
    router_init(&router_cfg);

    router_add_route(INPUT_SOURCE_NATIVE_24G, OUTPUT_TARGET_USB_DEVICE, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:24g2usb] Initialization complete\n");
    printf("[app:24g2usb]   Routing: SF30 2.4G -> USB HID Gamepad\n");
    printf("[app:24g2usb]   Pins: SCK=%d MOSI=%d MISO=%d CSN=%d CE=%d IRQ=%d\n",
           RF24G_PIN_SCK, RF24G_PIN_MOSI, RF24G_PIN_MISO,
           RF24G_PIN_CSN, RF24G_PIN_CE, RF24G_PIN_IRQ);
    printf("[app:24g2usb]   Hold BOOTSEL ~1.5s to pair a new controller\n");
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
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

    uint8_t linked = rf24g_host_get_device_count();
    leds_set_connected_devices(linked);
    leds_set_pairing(rf24g_host_is_pairing());

#ifdef RF24G_STATS_VERBOSE
    // Periodic radio diagnostics on UART/CDC: packets/sec, measured frame
    // period and an inter-arrival histogram. Opt-in only -- this app has no
    // pico_enable_stdio_usb, so printf goes to UART0 and blocks the caller
    // (uart_putc spins on a full TX FIFO) whether or not anything is
    // listening on GP0/GP1. At ~190 chars/line this line-per-second call
    // costs ~16ms of main-loop stall EVERY second whenever anything is
    // linked, which is itself enough to cost a dwell -- exactly what this
    // driver's interrupt-driven design exists to avoid elsewhere. Define
    // RF24G_STATS_VERBOSE to get it back for measuring `pps`/`period`/
    // `misses`. Once per second matches the 1s window pps is accumulated
    // over, and it stays quiet while nothing is linked so an idle adapter
    // doesn't spam the console.
    if (linked > 0 || rf24g_host_is_pairing()) {
        static uint32_t last_stats_ms = 0;
        uint32_t now_ms = platform_time_ms();
        if ((uint32_t)(now_ms - last_stats_ms) >= 1000) {
            last_stats_ms = now_ms;
            rf24g_host_print_stats();
        }
    }
#endif
}
