// snes_host.c - Native SNES/NES Controller Host Driver
//
// Polls native SNES/NES controllers via the SNESpad library and submits
// input events to the router.

#include "snes_host.h"
#include "snespad_c.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include <stdio.h>

// ============================================================================
// D-PAD MODE (S1+S2+direction hotkeys, GP2040-CE compatible)
// ============================================================================

typedef enum {
    DPAD_MODE_DPAD,        // D-pad → d-pad buttons (default)
    DPAD_MODE_LEFT_STICK,  // D-pad → left analog stick
    DPAD_MODE_RIGHT_STICK, // D-pad → right analog stick
} dpad_mode_t;

// ============================================================================
// INTERNAL STATE
// ============================================================================

static snespad_t snes_pads[SNES_MAX_PORTS];
static bool initialized = false;

// Track previous state for edge detection (buttons + analog packed)
static uint32_t prev_buttons[SNES_MAX_PORTS] = {0};
static uint32_t prev_analog[SNES_MAX_PORTS] = {0};

// D-pad mode state (shared across all ports)
static dpad_mode_t dpad_mode = DPAD_MODE_DPAD;

// S1+S2 combo state (per port)
static bool prev_s1s2_held[SNES_MAX_PORTS] = {false};
static bool combo_used[SNES_MAX_PORTS] = {false};       // Direction combo fired during hold
static bool combo_had_other[SNES_MAX_PORTS] = {false};   // Non-S1S2 buttons seen during hold

// ============================================================================
// BUTTON MAPPING: SNES → USBR
// ============================================================================

// Map SNES controller state to USBR button format
// Uses Switch-style layout (matches GP2040-CE Switch column)
static uint32_t map_snes_to_usbr(const snespad_t* pad)
{
    uint32_t buttons = 0x00000000;  // USBR uses active-high

    // Face buttons - Switch layout
    if (pad->button_b)      buttons |= JP_BUTTON_B1;  // SNES B → B1 (Switch B)
    if (pad->button_a)      buttons |= JP_BUTTON_B2;  // SNES A → B2 (Switch A)
    if (pad->button_y)      buttons |= JP_BUTTON_B3;  // SNES Y → B3 (Switch Y)
    if (pad->button_x)      buttons |= JP_BUTTON_B4;  // SNES X → B4 (Switch X)

    // Shoulder buttons
    if (pad->button_l)      buttons |= JP_BUTTON_L1;  // SNES L → L1
    if (pad->button_r)      buttons |= JP_BUTTON_R1;  // SNES R → R1

    // System buttons
    if (pad->button_start)  buttons |= JP_BUTTON_S2;  // Start → S2 (Plus)
    if (pad->button_select) buttons |= JP_BUTTON_S1;  // Select → S1 (Minus)

    // D-pad
    if (pad->direction_up)    buttons |= JP_BUTTON_DU;
    if (pad->direction_down)  buttons |= JP_BUTTON_DD;
    if (pad->direction_left)  buttons |= JP_BUTTON_DL;
    if (pad->direction_right) buttons |= JP_BUTTON_DR;

    return buttons;
}

// Map NES controller state to USBR button format
// Uses Switch-style layout (matches GP2040-CE Switch column)
static uint32_t map_nes_to_usbr(const snespad_t* pad)
{
    uint32_t buttons = 0x00000000;  // USBR uses active-high

    // NES has only A, B, Start, Select, D-pad
    if (pad->button_b)      buttons |= JP_BUTTON_B1;  // NES B → B1 (Switch B)
    if (pad->button_a)      buttons |= JP_BUTTON_B2;  // NES A → B2 (Switch A)

    if (pad->button_start)  buttons |= JP_BUTTON_S2;
    if (pad->button_select) buttons |= JP_BUTTON_S1;

    if (pad->direction_up)    buttons |= JP_BUTTON_DU;
    if (pad->direction_down)  buttons |= JP_BUTTON_DD;
    if (pad->direction_left)  buttons |= JP_BUTTON_DL;
    if (pad->direction_right) buttons |= JP_BUTTON_DR;

    return buttons;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void snes_host_init(void)
{
    // Skip if already initialized (app may have called init_pins with custom config)
    if (initialized) return;

    snes_host_init_pins(SNES_PIN_CLOCK, SNES_PIN_LATCH, SNES_PIN_DATA0,
                        SNES_PIN_DATA1, SNES_PIN_IOBIT);
}

void snes_host_init_pins(uint8_t clock, uint8_t latch, uint8_t data0,
                         uint8_t data1, uint8_t iobit)
{
    printf("[snes_host] Initializing SNES host driver\n");
    printf("[snes_host]   CLK=%d, LATCH=%d, D0=%d, D1=%d, IOBIT=%d\n",
           clock, latch, data0, data1, iobit);

    // Initialize SNESpad for port 0 (direct connection or multitap port 1)
    // TODO: Add multitap support - ports 1-3 would use different data pins
    //   Port 0: DATA0 (controller 1 or multitap 1)
    //   Port 1: DATA1 (controller 2 or multitap 2) - needs IOBIT toggle
    //   Port 2: DATA0 after IOBIT toggle (multitap 3)
    //   Port 3: DATA1 after IOBIT toggle (multitap 4)
    snespad_init(&snes_pads[0], clock, latch, data0, data1, iobit);
    snespad_begin(&snes_pads[0]);
    snespad_start(&snes_pads[0]);
    prev_buttons[0] = 0xFFFFFFFF;

    // Mark other ports as not initialized until multitap support is added
    for (int i = 1; i < SNES_MAX_PORTS; i++) {
        snes_pads[i].type = SNESPAD_NONE;
        prev_buttons[i] = 0xFFFFFFFF;
    }

    initialized = true;
    printf("[snes_host] Initialization complete (port 0 active, ports 1-3 reserved for multitap)\n");
}

void snes_host_task(void)
{
    if (!initialized) return;

    // Currently only port 0 is active (direct connection)
    // TODO: Expand when multitap support is added
    for (int port = 0; port < 1; port++) {  // Only poll port 0 for now
        snespad_t* pad = &snes_pads[port];

        // Poll the controller
        snespad_poll(pad);

        // Skip if no device connected
        if (pad->type == SNESPAD_NONE) {
            continue;
        }

        // Map buttons based on device type
        uint32_t buttons;
        uint8_t analog_1x = 128;  // Center
        uint8_t analog_1y = 128;
        uint8_t analog_2x = 128;
        uint8_t analog_2y = 128;

        switch (pad->type) {
            case SNESPAD_CONTROLLER:
                buttons = map_snes_to_usbr(pad);
                break;

            case SNESPAD_NES:
                buttons = map_nes_to_usbr(pad);
                break;

            case SNESPAD_MOUSE:
                // Mouse: use mouse_x/y as analog stick position
                buttons = 0x00000000;
                if (pad->button_a) buttons |= JP_BUTTON_B1;
                if (pad->button_b) buttons |= JP_BUTTON_B2;
                analog_1x = pad->mouse_x;
                analog_1y = pad->mouse_y;
                break;

            case SNESPAD_KEYBOARD:
                // Keyboard: could map to buttons, but skip for now
                continue;

            default:
                continue;
        }

        // =================================================================
        // S1+S2 combo detection (Home button + d-pad mode switching)
        // While S1+S2 held alone: inject A1 (Home) continuously.
        // While S1+S2 held + direction: trigger d-pad mode switch.
        // =================================================================
        bool s1s2_held = (buttons & (JP_BUTTON_S1 | JP_BUTTON_S2)) == (JP_BUTTON_S1 | JP_BUTTON_S2);

        if (s1s2_held) {
            if (!prev_s1s2_held[port]) {
                combo_used[port] = false;
                combo_had_other[port] = false;
            }

            uint32_t dpad_bits = buttons & (JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);
            uint32_t other_bits = buttons & ~(JP_BUTTON_S1 | JP_BUTTON_S2 | JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);

            if (other_bits) combo_had_other[port] = true;

            // Check for d-pad mode switch (only fire once per hold)
            if (!combo_used[port] && dpad_bits) {
                if (dpad_bits == JP_BUTTON_DD) {
                    dpad_mode = DPAD_MODE_DPAD;
                    printf("[snes_host] D-pad mode: D-PAD (default)\n");
                    combo_used[port] = true;
                } else if (dpad_bits == JP_BUTTON_DL) {
                    dpad_mode = DPAD_MODE_LEFT_STICK;
                    printf("[snes_host] D-pad mode: LEFT STICK\n");
                    combo_used[port] = true;
                } else if (dpad_bits == JP_BUTTON_DR) {
                    dpad_mode = DPAD_MODE_RIGHT_STICK;
                    printf("[snes_host] D-pad mode: RIGHT STICK\n");
                    combo_used[port] = true;
                }
            }

            prev_s1s2_held[port] = true;

            if (!combo_used[port] && !combo_had_other[port]) {
                // S1+S2 alone → inject Home (held continuously)
                buttons = JP_BUTTON_A1;
                // Fall through to change detection and submit
            } else {
                // Direction combo fired or other buttons present — suppress
                prev_buttons[port] = buttons;
                continue;
            }
        } else if (prev_s1s2_held[port]) {
            // S1+S2 just released — let natural button state through
            prev_s1s2_held[port] = false;
        }

        // =================================================================
        // Apply d-pad mode (remap d-pad to analog stick if needed)
        // =================================================================
        if (dpad_mode != DPAD_MODE_DPAD) {
            uint32_t dpad_bits = buttons & (JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);
            buttons &= ~(JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);

            uint8_t ax = 128, ay = 128;
            if (dpad_bits & JP_BUTTON_DL) ax = 0;
            else if (dpad_bits & JP_BUTTON_DR) ax = 255;
            if (dpad_bits & JP_BUTTON_DU) ay = 0;
            else if (dpad_bits & JP_BUTTON_DD) ay = 255;

            if (dpad_mode == DPAD_MODE_LEFT_STICK) {
                analog_1x = ax;
                analog_1y = ay;
            } else {
                analog_2x = ax;
                analog_2y = ay;
            }
        }

        // Only submit if state changed (check buttons and analog)
        uint32_t analog_packed = (analog_1x << 24) | (analog_1y << 16) | (analog_2x << 8) | analog_2y;
        if (buttons == prev_buttons[port] && analog_packed == prev_analog[port]) {
            continue;
        }
        prev_buttons[port] = buttons;
        prev_analog[port] = analog_packed;

        // Build input event
        input_event_t event;
        init_input_event(&event);

        event.dev_addr = 0xF0 + port;  // Use 0xF0+ range for native inputs
        event.instance = 0;
        event.type = INPUT_TYPE_GAMEPAD;
        event.buttons = buttons;
        event.analog[ANALOG_LX] = analog_1x;
        event.analog[ANALOG_LY] = analog_1y;
        event.analog[ANALOG_RX] = analog_2x;
        event.analog[ANALOG_RY] = analog_2y;

        // Submit to router
        router_submit_input(&event);
    }
}

void snes_host_set_rumble(uint8_t port, uint8_t left, uint8_t right)
{
    if (!initialized || port >= SNES_MAX_PORTS) return;

    // Only send rumble to SNES controllers — mouse and keyboard use IOBit
    // for other purposes (speed cycling, caps lock LED)
    snespad_t* pad = &snes_pads[port];
    if (pad->type != SNESPAD_CONTROLLER) return;

    snespad_set_rumble(pad, left, right);
}

int8_t snes_host_get_device_type(uint8_t port)
{
    if (!initialized || port >= SNES_MAX_PORTS) {
        return -1;
    }
    return snes_pads[port].type;
}

bool snes_host_is_connected(void)
{
    if (!initialized) return false;

    for (int i = 0; i < SNES_MAX_PORTS; i++) {
        if (snes_pads[i].type != SNESPAD_NONE) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// INPUT INTERFACE (for app declaration)
// ============================================================================

static uint8_t snes_get_device_count(void)
{
    uint8_t count = 0;
    for (int i = 0; i < SNES_MAX_PORTS; i++) {
        if (snes_pads[i].type != SNESPAD_NONE) {
            count++;
        }
    }
    return count;
}

const InputInterface snes_input_interface = {
    .name = "SNES",
    .source = INPUT_SOURCE_NATIVE_SNES,
    .init = snes_host_init,
    .task = snes_host_task,
    .is_connected = snes_host_is_connected,
    .get_device_count = snes_get_device_count,
};
