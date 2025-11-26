// snes_host.c - Native SNES/NES Controller Host Driver
//
// Polls native SNES/NES controllers via the SNESpad library and submits
// input events to the router.

#include "snes_host.h"
#include "native/host/host_interface.h"
#include "snespad_c.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include <stdio.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

static snespad_t snes_pads[SNES_MAX_PORTS];
static bool initialized = false;

// Track previous state for edge detection
static uint32_t prev_buttons[SNES_MAX_PORTS] = {0};

// ============================================================================
// BUTTON MAPPING: SNES → USBR
// ============================================================================

// Map SNES controller state to USBR button format
static uint32_t map_snes_to_usbr(const snespad_t* pad)
{
    uint32_t buttons = 0xFFFFFFFF;  // USBR uses active-low

    // Face buttons
    if (pad->button_a)      buttons &= ~USBR_BUTTON_B1;  // SNES A → B1
    if (pad->button_b)      buttons &= ~USBR_BUTTON_B2;  // SNES B → B2
    if (pad->button_x)      buttons &= ~USBR_BUTTON_B4;  // SNES X → B4
    if (pad->button_y)      buttons &= ~USBR_BUTTON_B3;  // SNES Y → B3

    // Shoulder buttons
    if (pad->button_l)      buttons &= ~USBR_BUTTON_L1;  // SNES L → L1
    if (pad->button_r)      buttons &= ~USBR_BUTTON_R1;  // SNES R → R1

    // System buttons
    if (pad->button_start)  buttons &= ~USBR_BUTTON_S2;  // Start → S2
    if (pad->button_select) buttons &= ~USBR_BUTTON_S1;  // Select → S1

    // D-pad
    if (pad->direction_up)    buttons &= ~USBR_BUTTON_DU;
    if (pad->direction_down)  buttons &= ~USBR_BUTTON_DD;
    if (pad->direction_left)  buttons &= ~USBR_BUTTON_DL;
    if (pad->direction_right) buttons &= ~USBR_BUTTON_DR;

    return buttons;
}

// Map NES controller state to USBR button format
static uint32_t map_nes_to_usbr(const snespad_t* pad)
{
    uint32_t buttons = 0xFFFFFFFF;  // USBR uses active-low

    // NES has only A, B, Start, Select, D-pad
    if (pad->button_a)      buttons &= ~USBR_BUTTON_B1;  // NES A → B1
    if (pad->button_b)      buttons &= ~USBR_BUTTON_B2;  // NES B → B2

    if (pad->button_start)  buttons &= ~USBR_BUTTON_S2;
    if (pad->button_select) buttons &= ~USBR_BUTTON_S1;

    if (pad->direction_up)    buttons &= ~USBR_BUTTON_DU;
    if (pad->direction_down)  buttons &= ~USBR_BUTTON_DD;
    if (pad->direction_left)  buttons &= ~USBR_BUTTON_DL;
    if (pad->direction_right) buttons &= ~USBR_BUTTON_DR;

    return buttons;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void snes_host_init(void)
{
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
                buttons = 0xFFFFFFFF;
                if (pad->button_a) buttons &= ~USBR_BUTTON_B1;
                if (pad->button_b) buttons &= ~USBR_BUTTON_B2;
                analog_1x = pad->mouse_x;
                analog_1y = pad->mouse_y;
                break;

            case SNESPAD_KEYBOARD:
                // Keyboard: could map to buttons, but skip for now
                continue;

            default:
                continue;
        }

        // Only submit if state changed
        if (buttons == prev_buttons[port]) {
            continue;
        }
        prev_buttons[port] = buttons;

        // Build input event
        input_event_t event;
        init_input_event(&event);

        event.dev_addr = 0xF0 + port;  // Use 0xF0+ range for native inputs
        event.instance = 0;
        event.type = INPUT_TYPE_GAMEPAD;
        event.buttons = buttons;
        event.analog[ANALOG_X] = analog_1x;
        event.analog[ANALOG_Y] = analog_1y;
        event.analog[ANALOG_Z] = analog_2x;
        event.analog[ANALOG_RX] = analog_2y;

        // Submit to router
        router_submit_input(&event);
    }
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

static uint8_t snes_host_get_port_count(void)
{
    return SNES_MAX_PORTS;
}

// Generic init_pins wrapper for HostInterface
// pins[0]=clock, pins[1]=latch, pins[2]=data0, pins[3]=data1, pins[4]=iobit
static void snes_host_init_pins_generic(const uint8_t* pins, uint8_t pin_count)
{
    if (pin_count >= 5) {
        snes_host_init_pins(pins[0], pins[1], pins[2], pins[3], pins[4]);
    } else {
        snes_host_init();  // Use defaults
    }
}

// ============================================================================
// HOST INTERFACE
// ============================================================================

const HostInterface snes_host_interface = {
    .name = "SNES",
    .init = snes_host_init,
    .init_pins = snes_host_init_pins_generic,
    .task = snes_host_task,
    .is_connected = snes_host_is_connected,
    .get_device_type = snes_host_get_device_type,
    .get_port_count = snes_host_get_port_count,
};
