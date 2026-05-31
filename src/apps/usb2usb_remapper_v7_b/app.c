// app.c - usb2usb dual: HOST/PRODUCER side (B)
//
// Native USB host reads controllers (through the on-board SL2.1A hub); the
// router taps each event and the uart_peer producer ships it over the
// inter-MCU UART link to the device side (A). Feedback (rumble/LED) arrives
// back over the link and is applied to the per-player feedback state.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/players/feedback.h"
#include "usb/usbh/usbh.h"
#include "uart_peer/uart_peer.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include <stdio.h>

// ============================================================================
// INPUT INTERFACES — native USB host
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
// OUTPUT INTERFACE — UART peer link
// ============================================================================

static void link_output_init(void)
{
    uart_peer_config_t cfg = {
        .uart_inst = LINK_UART_INST,
        .tx_pin = LINK_TX_PIN,
        .rx_pin = LINK_RX_PIN,
        .cts_pin = LINK_CTS_PIN,
        .rts_pin = LINK_RTS_PIN,
        .baud = LINK_BAUD,
        .flow_control = LINK_FLOW_CTRL,
    };
    uart_peer_init(&cfg);
    printf("[usb2usb_remapper_v7_b] UART peer link up (host/producer)\n");
}

static void link_output_task(void)
{
    // Pump the link (drain TX ring, decode incoming feedback frames).
    uart_peer_task();

    // Apply any feedback (rumble/LED) the device side sent back.
    uart_peer_status_t st;
    if (uart_peer_get_status(&st)) {
        uint8_t p = st.player_number ? (uint8_t)(st.player_number - 1) : 0;
        feedback_set_rumble(p, st.rumble_left, st.rumble_right);
        feedback_set_led_rgb(p, st.led_color[0], st.led_color[1], st.led_color[2]);
    }
}

static uint8_t link_output_get_rumble(void)
{
    feedback_state_t* fb = feedback_get_state(0);
    return fb ? fb->rumble.left : 0;
}

static uint8_t link_output_get_player_led(void)
{
    feedback_state_t* fb = feedback_get_state(0);
    return fb ? fb->led.pattern : 0;
}

static const OutputInterface link_output_interface = {
    .name = "UART Peer Link",
    .target = OUTPUT_TARGET_UART,
    .init = link_output_init,
    .core1_task = NULL,
    .task = link_output_task,
    .get_rumble = link_output_get_rumble,
    .get_player_led = link_output_get_player_led,
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
};

static const OutputInterface* output_interfaces[] = {
    &link_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// APP INIT / TASK
// ============================================================================

void app_init(void)
{
    printf("[app:usb2usb_remapper_v7_b] Initializing %s v%s\n", APP_NAME, APP_VERSION);

    feedback_init();

    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_UART] = UART_OUTPUT_PLAYERS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_FLAGS,
    };
    router_init(&router_cfg);

    // USB host -> UART link, with the producer tap serializing each event.
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_UART, 0);
    router_set_tap(OUTPUT_TARGET_UART, uart_peer_producer_tap);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:usb2usb_remapper_v7_b] Routing: USB host -> UART peer link\n");
}

void app_task(void)
{
    // Output interface task pumps the link.

    // Diagnostic heartbeat: report B liveness + USB host device count to A
    // every ~200ms so the device side can surface it over CDC during bring-up.
    static uint32_t last_dbg_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_dbg_ms >= 200) {
        last_dbg_ms = now;

        uart_peer_debug_t dbg = {
            .magic = 0xDB,
            .dev_count = 0,
            .last_vid = 0,
            .last_pid = 0,
            .uptime_ms = now,
        };
        for (uint8_t daddr = 1; daddr <= CFG_TUH_DEVICE_MAX; daddr++) {
            if (tuh_mounted(daddr)) {
                dbg.dev_count++;
                uint16_t vid = 0, pid = 0;
                tuh_vid_pid_get(daddr, &vid, &pid);
                dbg.last_vid = vid;
                dbg.last_pid = pid;
            }
        }
        uart_peer_send_debug(&dbg);
    }
}
