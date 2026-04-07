// cdi_device.c - Philips CD-i Output Device
//
// Emulates a CD-i controller connected to a CD-i console.
// Protocol: inverted 1200 baud 7N2 UART, one-directional (controller → console).
// Uses PIO for precise inverted serial timing. Runs entirely on Core 0.

#include "cdi_device.h"
#include "cdi_buttons.h"
#include "cdi_tx.pio.h"
#include "core/buttons.h"
#include "core/output_interface.h"
#include "core/router/router.h"
#include "core/services/profiles/profile.h"
#include "core/services/players/manager.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include <stdio.h>

// ============================================================================
// STATE
// ============================================================================

static PIO cdi_pio;
static uint cdi_sm;

typedef enum {
    CDI_STATE_IDLE,         // Waiting for RTS HIGH
    CDI_STATE_WAIT_CONNECT, // RTS went HIGH, waiting 100ms before sending ID
    CDI_STATE_STREAMING,    // Sending packets continuously
} cdi_state_t;

static cdi_state_t state = CDI_STATE_IDLE;
static uint32_t connect_time = 0;
static uint32_t last_packet_time = 0;

// Packet interval: 3 bytes at 1200 baud 7N2 = 3 * 10 bits * 833us = ~25ms
// Send at ~40Hz to keep the line saturated (some games require this)
#define CDI_PACKET_INTERVAL_MS 25

// Deadzone for analog stick (centered at 128)
#define CDI_STICK_DEADZONE 12

// ============================================================================
// PIO SERIAL TX
// ============================================================================

// Invert a 7-bit value for the inverted UART protocol
static inline uint8_t cdi_invert_bits(uint8_t val) {
    return (~val) & 0x7F;
}

static void cdi_send_byte(uint8_t byte) {
    // Invert data bits for the inverted UART protocol
    pio_sm_put_blocking(cdi_pio, cdi_sm, cdi_invert_bits(byte));
}

// ============================================================================
// PROFILE SYSTEM
// ============================================================================

static uint8_t cdi_get_player_count(void) {
    return router_get_player_count(OUTPUT_TARGET_CDI);
}

static uint8_t cdi_get_profile_count(void) {
    return profile_get_count(OUTPUT_TARGET_CDI);
}

static uint8_t cdi_get_active_profile(void) {
    return profile_get_active_index(OUTPUT_TARGET_CDI);
}

static void cdi_set_active_profile(uint8_t index) {
    profile_set_active(OUTPUT_TARGET_CDI, index);
}

static const char* cdi_get_profile_name(uint8_t index) {
    return profile_get_name(OUTPUT_TARGET_CDI, index);
}

// ============================================================================
// ANALOG TO DELTA CONVERSION
// ============================================================================

// Convert absolute stick position (0-255, 128=center) to signed relative delta.
// CD-i treats input as a pointing device — stick deflection = movement speed.
static int8_t analog_to_delta(uint8_t analog_value) {
    int16_t centered = (int16_t)analog_value - 128;

    // Deadzone
    if (centered > -CDI_STICK_DEADZONE && centered < CDI_STICK_DEADZONE)
        return 0;

    // Scale: full deflection (~127) maps to reasonable cursor speed
    // Divide by 4 for moderate speed; can be tuned per-app
    int16_t delta = centered / 4;
    if (delta > 127) delta = 127;
    if (delta < -127) delta = -127;
    return (int8_t)delta;
}

// ============================================================================
// PACKET ENCODING
// ============================================================================

// Encode and send a 3-byte CD-i MANEUVER packet
static void cdi_send_packet(int8_t dx, int8_t dy, bool btn1, bool btn2) {
    uint8_t x = (uint8_t)dx;
    uint8_t y = (uint8_t)dy;

    uint8_t b0 = 0xC0;
    b0 |= (x >> 6) & 0x03;
    b0 |= ((y >> 4) & 0x0C);
    if (btn1) b0 |= (1 << 5);
    if (btn2) b0 |= (1 << 4);

    uint8_t b1 = 0x80 | (x & 0x3F);
    uint8_t b2 = 0x80 | (y & 0x3F);

    cdi_send_byte(b0);
    cdi_send_byte(b1);
    cdi_send_byte(b2);
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void cdi_init(void) {
    // RTS pin: input, pull-down (idle LOW when console is off)
    gpio_init(CDI_RTS_PIN);
    gpio_set_dir(CDI_RTS_PIN, GPIO_IN);
    gpio_pull_down(CDI_RTS_PIN);

    // Claim PIO for TX
    cdi_pio = pio0;
    cdi_sm = pio_claim_unused_sm(cdi_pio, true);
    uint offset = pio_add_program(cdi_pio, &cdi_tx_program);
    cdi_tx_program_init(cdi_pio, cdi_sm, offset, CDI_TX_PIN);

    // Profile system callback
    profile_set_player_count_callback(cdi_get_player_count);

    state = CDI_STATE_IDLE;

    printf("[cdi] Initialized: TX=GPIO%d RTS=GPIO%d PIO%d SM%d\n",
           CDI_TX_PIN, CDI_RTS_PIN, pio_get_index(cdi_pio), cdi_sm);
}

// ============================================================================
// TASK (Core 0 — called from main loop)
// ============================================================================

void cdi_task(void) {
    bool rts = gpio_get(CDI_RTS_PIN);
    uint32_t now = to_ms_since_boot(get_absolute_time());

    switch (state) {
        case CDI_STATE_IDLE:
            if (rts) {
                // RTS went HIGH — console is ready
                connect_time = now;
                state = CDI_STATE_WAIT_CONNECT;
                printf("[cdi] RTS asserted, connecting...\n");
            }
            break;

        case CDI_STATE_WAIT_CONNECT:
            if (!rts) {
                state = CDI_STATE_IDLE;
                break;
            }
            // Wait 100ms then send device ID
            if (now - connect_time >= 100) {
                cdi_send_byte(CDI_DEVICE_MANEUVER);
                state = CDI_STATE_STREAMING;
                last_packet_time = now;
                printf("[cdi] Connected as MANEUVER device\n");
            }
            break;

        case CDI_STATE_STREAMING:
            if (!rts) {
                state = CDI_STATE_IDLE;
                printf("[cdi] RTS deasserted, disconnected\n");
                break;
            }

            // Send packets at ~40Hz
            if (now - last_packet_time < CDI_PACKET_INTERVAL_MS) break;
            last_packet_time = now;

            // Get input from router
            const input_event_t* event = router_get_output(OUTPUT_TARGET_CDI, 0);

            int8_t dx = 0, dy = 0;
            bool btn1 = false, btn2 = false;

            if (event && playersCount > 0) {
                // Apply profile
                const profile_t* profile = profile_get_active(OUTPUT_TARGET_CDI);
                profile_output_t output;
                profile_apply(profile,
                              event->buttons,
                              event->analog[ANALOG_LX], event->analog[ANALOG_LY],
                              event->analog[ANALOG_RX], event->analog[ANALOG_RY],
                              event->analog[ANALOG_L2], event->analog[ANALOG_R2],
                              event->analog[ANALOG_RZ],
                              &output);

                // Convert stick to delta
                dx = analog_to_delta(output.left_x);
                dy = analog_to_delta(output.left_y);

                // D-pad overrides analog with fixed-speed delta
                if (output.buttons & JP_BUTTON_DL) dx = -32;
                if (output.buttons & JP_BUTTON_DR) dx = 32;
                if (output.buttons & JP_BUTTON_DU) dy = -32;
                if (output.buttons & JP_BUTTON_DD) dy = 32;

                // Buttons
                btn1 = (output.buttons & CDI_BUTTON_1) != 0;
                btn2 = (output.buttons & CDI_BUTTON_2) != 0;

                // Profile switch combo
                profile_check_switch_combo(event->buttons);
            }

            // Always send packet (CD-i expects continuous stream)
            cdi_send_packet(dx, dy, btn1, btn2);
            break;
    }
}

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

const OutputInterface cdi_output_interface = {
    .name = "CD-i",
    .target = OUTPUT_TARGET_CDI,
    .init = cdi_init,
    .core1_task = NULL,         // No Core 1 needed (1200 baud is slow)
    .task = cdi_task,
    .get_rumble = NULL,         // CD-i has no rumble
    .get_player_led = NULL,
    .get_profile_count = cdi_get_profile_count,
    .get_active_profile = cdi_get_active_profile,
    .set_active_profile = cdi_set_active_profile,
    .get_profile_name = cdi_get_profile_name,
    .get_trigger_threshold = NULL,
};
