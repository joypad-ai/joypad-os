// app.c - usb2usb dual: DEVICE/CONSUMER side (A)
//
// The inter-MCU UART link feeds input events into the router under
// INPUT_SOURCE_UART_PEER; the router drives the USB device output (all output
// modes). Feedback (rumble/LED the host PC requested) is sent back to the host
// side (B) over the link so it can drive the controllers.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/players/feedback.h"
#include "usb/usbd/usbd.h"
#include "usb/usbd/cdc/cdc_commands.h"
#include "usb/usbd/cdc/cdc_protocol.h"
#include "uart_peer/uart_peer.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// INTERFACES — UART link in, USB device out
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &uart_peer_input_interface,
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

// ============================================================================
// APP INIT / TASK
// ============================================================================

void app_init(void)
{
    printf("[app:usb2usb_remapper_v7_a] Initializing %s v%s\n", APP_NAME, APP_VERSION);

    feedback_init();

    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,
        .transform_flags = TRANSFORM_FLAGS,
    };
    router_init(&router_cfg);

    // UART link -> USB device.
    router_add_route(INPUT_SOURCE_UART_PEER, OUTPUT_TARGET_USB_DEVICE, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Bring up the inter-MCU link (consumer side).
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

    printf("[app:usb2usb_remapper_v7_a] Routing: UART peer link -> USB device\n");
}

void app_task(void)
{
    // Pump the link (RX events were submitted to the router via the input
    // interface task; this also drains TX). Then push feedback back to B.
    uart_peer_task();

    // Send feedback (rumble/LED requested by the host PC) to the host side at
    // ~125 Hz so it can drive the controllers.
    static uint32_t last_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_ms >= 8) {
        last_ms = now;

        uart_peer_status_t st;
        memset(&st, 0, sizeof(st));
        st.flags = UART_PEER_STATUS_FLAG_CONNECTED;
        st.player_number = 1;
        if (usbd_output_interface.get_rumble) {
            uint8_t r = usbd_output_interface.get_rumble();
            st.rumble_left = r;
            st.rumble_right = r;
        }
        if (usbd_output_interface.get_player_led) {
            st.led_player = usbd_output_interface.get_player_led();
        }
        uart_peer_send_status(&st);
    }

    // Surface B's diagnostic heartbeat over CDC so bring-up tooling can see
    // whether B is alive, the link is up, and how many USB host devices B sees.
    uart_peer_debug_t dbg;
    if (uart_peer_get_debug(&dbg)) {
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "{\"type\":\"peer\",\"magic\":%u,\"devs\":%u,\"vid\":\"%04X\",\"pid\":\"%04X\",\"up\":%lu}",
                 dbg.magic, dbg.dev_count, dbg.last_vid, dbg.last_pid,
                 (unsigned long)dbg.uptime_ms);
        cdc_protocol_send_event(cdc_commands_get_protocol(), buf);
    }
}
