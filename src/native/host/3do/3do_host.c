// 3do_host.c - Native 3DO Controller Host Driver
//
// Master mode implementation - generates CLK to read 3DO controllers
// Parses controller data and submits to router via router_submit_input()

#include "3do_host.h"
#include "3do_host.pio.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

static PIO tdo_pio = pio0;
static uint tdo_sm = 0;
static uint tdo_clk_pin = TDO_HOST_PIN_CLK;
static uint tdo_data_pin = TDO_HOST_PIN_DATA;
static bool initialized = false;

// Controller state for each slot in daisy chain
static tdo_controller_t controllers[TDO_HOST_MAX_CONTROLLERS];
static uint8_t controller_count = 0;

// Previous button state for change detection
static uint32_t prev_buttons[TDO_HOST_MAX_CONTROLLERS] = {0};

// Raw read buffer (max 201 bytes like device driver)
static uint8_t read_buffer[201];

// ============================================================================
// 3DO PBUS DEVICE IDS
// ============================================================================

// Device ID is in upper nibble of first byte for joypads
// Joypad: bits 7:5 = 0b100 (value 4 in upper 3 bits)
#define TDO_ID_JOYPAD_MASK    0xE0
#define TDO_ID_JOYPAD_VALUE   0x80  // 0b100xxxxx

// Joystick: 3-byte ID header
#define TDO_ID_JOYSTICK_0     0x01
#define TDO_ID_JOYSTICK_1     0x7B
#define TDO_ID_JOYSTICK_2     0x08

// Mouse ID
#define TDO_ID_MOUSE          0x49

// Lightgun ID
#define TDO_ID_LIGHTGUN       0x4D

// Arcade/JAMMA ID
#define TDO_ID_ARCADE         0xC0

// ============================================================================
// RAW DATA READ
// ============================================================================

// Read raw bytes from 3DO daisy chain
// Returns number of bytes read
static uint8_t tdo_read_raw(uint8_t* buffer, uint8_t max_bytes) {
    uint8_t bytes_read = 0;

    // Read bytes one at a time
    // PIO handles the bit-level clocking
    while (bytes_read < max_bytes) {
        uint32_t data = tdo_host_read_bits(tdo_pio, tdo_sm, 8);
        buffer[bytes_read] = (uint8_t)(data & 0xFF);

        // Check for end of chain (string of zeros)
        if (buffer[bytes_read] == 0x00) {
            // Peek ahead to verify end
            if (bytes_read > 0 && buffer[bytes_read - 1] == 0x00) {
                break;  // Two consecutive zeros = end of chain
            }
        }

        bytes_read++;
    }

    return bytes_read;
}

// ============================================================================
// CONTROLLER PARSING
// ============================================================================

// Parse joypad report (2 bytes)
// Byte 0: [A][Left][Right][Up][Down][ID2][ID1][ID0]
// Byte 1: [Tail1][Tail0][L][R][X][P][C][B]
static void parse_joypad(tdo_controller_t* ctrl, const uint8_t* data) {
    ctrl->type = TDO_DEVICE_JOYPAD;
    ctrl->raw_report_size = 2;
    memcpy(ctrl->raw_report, data, 2);

    uint8_t byte0 = data[0];
    uint8_t byte1 = data[1];

    // Byte 0 - buttons and D-pad (active HIGH in 3DO protocol)
    ctrl->button_a   = (byte0 & 0x80) != 0;
    ctrl->dpad_left  = (byte0 & 0x40) != 0;
    ctrl->dpad_right = (byte0 & 0x20) != 0;
    ctrl->dpad_up    = (byte0 & 0x10) != 0;
    ctrl->dpad_down  = (byte0 & 0x08) != 0;

    // Byte 1 - more buttons
    ctrl->button_l = (byte1 & 0x20) != 0;
    ctrl->button_r = (byte1 & 0x10) != 0;
    ctrl->button_x = (byte1 & 0x08) != 0;
    ctrl->button_p = (byte1 & 0x04) != 0;
    ctrl->button_c = (byte1 & 0x02) != 0;
    ctrl->button_b = (byte1 & 0x01) != 0;

    // Clear analog/mouse fields
    ctrl->analog_x = 128;
    ctrl->analog_y = 128;
    ctrl->analog_z = 128;
    ctrl->throttle = 128;
    ctrl->mouse_dx = 0;
    ctrl->mouse_dy = 0;
    ctrl->fire = false;
}

// Parse joystick report (9 bytes)
// Bytes 0-2: ID (0x01, 0x7B, 0x08)
// Bytes 3-6: Analog axes
// Byte 7: D-pad and buttons
// Byte 8: More buttons
static void parse_joystick(tdo_controller_t* ctrl, const uint8_t* data) {
    ctrl->type = TDO_DEVICE_JOYSTICK;
    ctrl->raw_report_size = 9;
    memcpy(ctrl->raw_report, data, 9);

    // Skip 3-byte ID header
    ctrl->analog_x = data[3];   // X axis
    ctrl->analog_y = data[4];   // Y axis
    ctrl->analog_z = data[5];   // Z/Twist
    ctrl->throttle = data[6];   // Throttle

    uint8_t byte7 = data[7];
    uint8_t byte8 = data[8];

    // Byte 7 - D-pad and face buttons
    ctrl->dpad_left  = (byte7 & 0x80) != 0;
    ctrl->dpad_right = (byte7 & 0x40) != 0;
    ctrl->dpad_down  = (byte7 & 0x20) != 0;
    ctrl->dpad_up    = (byte7 & 0x10) != 0;
    ctrl->button_c   = (byte7 & 0x08) != 0;
    ctrl->button_b   = (byte7 & 0x04) != 0;
    ctrl->button_a   = (byte7 & 0x02) != 0;
    ctrl->fire       = (byte7 & 0x01) != 0;

    // Byte 8 - shoulder and system buttons
    ctrl->button_r = (byte8 & 0x08) != 0;
    ctrl->button_l = (byte8 & 0x04) != 0;
    ctrl->button_x = (byte8 & 0x02) != 0;
    ctrl->button_p = (byte8 & 0x01) != 0;

    // Clear mouse fields
    ctrl->mouse_dx = 0;
    ctrl->mouse_dy = 0;
    ctrl->mouse_left = false;
    ctrl->mouse_right = false;
    ctrl->mouse_middle = false;
}

// Parse mouse report (4 bytes)
// Byte 0: ID (0x49)
// Byte 1: [dy_up:4][shift][right][middle][left]
// Byte 2: [dx_up:2][dy_low:6]
// Byte 3: dx_low
static void parse_mouse(tdo_controller_t* ctrl, const uint8_t* data) {
    ctrl->type = TDO_DEVICE_MOUSE;
    ctrl->raw_report_size = 4;
    memcpy(ctrl->raw_report, data, 4);

    uint8_t byte1 = data[1];
    uint8_t byte2 = data[2];
    uint8_t byte3 = data[3];

    // Buttons
    ctrl->mouse_left   = (byte1 & 0x01) != 0;
    ctrl->mouse_middle = (byte1 & 0x02) != 0;
    ctrl->mouse_right  = (byte1 & 0x04) != 0;
    // shift button at bit 3 - could map to a modifier

    // Delta Y (10-bit signed)
    int16_t dy = ((byte1 >> 4) & 0x0F) << 6 | (byte2 & 0x3F);
    if (dy & 0x200) dy |= 0xFC00;  // Sign extend
    ctrl->mouse_dy = (int8_t)(dy > 127 ? 127 : (dy < -128 ? -128 : dy));

    // Delta X (10-bit signed)
    int16_t dx = ((byte2 >> 6) & 0x03) << 8 | byte3;
    if (dx & 0x200) dx |= 0xFC00;  // Sign extend
    ctrl->mouse_dx = (int8_t)(dx > 127 ? 127 : (dx < -128 ? -128 : dx));

    // Clear other fields
    ctrl->button_a = ctrl->mouse_left;
    ctrl->button_b = ctrl->mouse_right;
    ctrl->button_c = ctrl->mouse_middle;
    ctrl->analog_x = 128;
    ctrl->analog_y = 128;
}

// Parse all controllers from raw buffer
// Returns number of controllers found
static uint8_t parse_controllers(const uint8_t* buffer, uint8_t buffer_size) {
    uint8_t count = 0;
    uint8_t offset = 0;

    while (offset < buffer_size && count < TDO_HOST_MAX_CONTROLLERS) {
        uint8_t byte0 = buffer[offset];

        // Check for end of chain
        if (byte0 == 0x00) {
            bool is_end = true;
            for (uint8_t i = offset; i < buffer_size && i < offset + 4; i++) {
                if (buffer[i] != 0x00) {
                    is_end = false;
                    break;
                }
            }
            if (is_end) break;
        }

        tdo_controller_t* ctrl = &controllers[count];
        memset(ctrl, 0, sizeof(tdo_controller_t));

        // Check device type by ID
        uint8_t id_nibble = (byte0 >> 4) & 0x0F;

        // Joypad: upper 3 bits of first nibble are non-zero
        if ((id_nibble & 0x0C) != 0) {
            if (offset + 2 <= buffer_size) {
                parse_joypad(ctrl, &buffer[offset]);
                offset += 2;
                count++;
            } else break;
        }
        // Joystick: 3-byte ID header
        else if (byte0 == TDO_ID_JOYSTICK_0 &&
                 offset + 2 < buffer_size &&
                 buffer[offset + 1] == TDO_ID_JOYSTICK_1 &&
                 buffer[offset + 2] == TDO_ID_JOYSTICK_2) {
            if (offset + 9 <= buffer_size) {
                parse_joystick(ctrl, &buffer[offset]);
                offset += 9;
                count++;
            } else break;
        }
        // Mouse
        else if (byte0 == TDO_ID_MOUSE) {
            if (offset + 4 <= buffer_size) {
                parse_mouse(ctrl, &buffer[offset]);
                offset += 4;
                count++;
            } else break;
        }
        // Lightgun
        else if (byte0 == TDO_ID_LIGHTGUN) {
            // Skip for now - 4 bytes
            ctrl->type = TDO_DEVICE_LIGHTGUN;
            offset += 4;
            count++;
        }
        // Arcade
        else if (byte0 == TDO_ID_ARCADE) {
            // Skip for now - 2 bytes
            ctrl->type = TDO_DEVICE_ARCADE;
            offset += 2;
            count++;
        }
        // Unknown - skip 1 byte
        else {
            offset++;
        }
    }

    // Mark remaining slots as empty
    for (uint8_t i = count; i < TDO_HOST_MAX_CONTROLLERS; i++) {
        controllers[i].type = TDO_DEVICE_NONE;
    }

    return count;
}

// ============================================================================
// USBR BUTTON MAPPING
// ============================================================================

// Convert 3DO controller state to USBR button format (active-low)
static uint32_t map_3do_to_usbr(const tdo_controller_t* ctrl) {
    uint32_t buttons = 0x00000000;  // All released (active-high)

    // 3DO is active-HIGH, USBR is active-LOW
    // Clear bit = pressed

    // Face buttons
    // 3DO A → USBR B3 (like TDO_BUTTON_A in 3do_buttons.h)
    // 3DO B → USBR B1
    // 3DO C → USBR B2
    if (ctrl->button_a) buttons |= JP_BUTTON_B3;
    if (ctrl->button_b) buttons |= JP_BUTTON_B1;
    if (ctrl->button_c) buttons |= JP_BUTTON_B2;

    // Shoulder buttons
    if (ctrl->button_l) buttons |= JP_BUTTON_L1;
    if (ctrl->button_r) buttons |= JP_BUTTON_R1;

    // System buttons
    if (ctrl->button_x) buttons |= JP_BUTTON_S1;  // X/Stop → Select
    if (ctrl->button_p) buttons |= JP_BUTTON_S2;  // P/Play → Start

    // D-pad
    if (ctrl->dpad_up)    buttons |= JP_BUTTON_DU;
    if (ctrl->dpad_down)  buttons |= JP_BUTTON_DD;
    if (ctrl->dpad_left)  buttons |= JP_BUTTON_DL;
    if (ctrl->dpad_right) buttons |= JP_BUTTON_DR;

    // Joystick fire → L2
    if (ctrl->fire) buttons |= JP_BUTTON_L2;

    return buttons;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void tdo_host_init(void) {
    tdo_host_init_pins(TDO_HOST_PIN_CLK, TDO_HOST_PIN_DATA);
}

void tdo_host_init_pins(uint8_t clk_pin, uint8_t data_pin) {
    printf("[3do_host] Initializing 3DO host driver\n");
    printf("[3do_host]   CLK=%d, DATA=%d\n", clk_pin, data_pin);

    tdo_clk_pin = clk_pin;
    tdo_data_pin = data_pin;

    // Claim PIO and state machine
    tdo_pio = pio0;
    tdo_sm = pio_claim_unused_sm(tdo_pio, true);

    // Load PIO program
    uint offset = pio_add_program(tdo_pio, &tdo_host_read_program);
    tdo_host_read_program_init(tdo_pio, tdo_sm, offset, clk_pin, data_pin);

    // Initialize controller state
    for (int i = 0; i < TDO_HOST_MAX_CONTROLLERS; i++) {
        memset(&controllers[i], 0, sizeof(tdo_controller_t));
        controllers[i].type = TDO_DEVICE_NONE;
        controllers[i].analog_x = 128;
        controllers[i].analog_y = 128;
        controllers[i].analog_z = 128;
        controllers[i].throttle = 128;
        prev_buttons[i] = 0xFFFFFFFF;
    }

    controller_count = 0;
    initialized = true;

    printf("[3do_host] Initialization complete\n");
}

void tdo_host_task(void) {
    if (!initialized) return;

    // Read raw data from daisy chain
    // Use a reasonable max (enough for 8 joysticks = 72 bytes)
    uint8_t bytes_read = tdo_read_raw(read_buffer, 80);

    if (bytes_read == 0) {
        // No controllers connected
        controller_count = 0;
        return;
    }

    // Parse controllers from raw data
    controller_count = parse_controllers(read_buffer, bytes_read);

    // Submit each controller to router
    for (uint8_t i = 0; i < controller_count; i++) {
        tdo_controller_t* ctrl = &controllers[i];

        if (ctrl->type == TDO_DEVICE_NONE) continue;

        // Convert to USBR button format
        uint32_t buttons = map_3do_to_usbr(ctrl);

        // Only submit if state changed
        if (buttons == prev_buttons[i] &&
            ctrl->type != TDO_DEVICE_MOUSE &&
            ctrl->type != TDO_DEVICE_JOYSTICK) {
            continue;
        }
        prev_buttons[i] = buttons;

        // Build input event
        input_event_t event;
        init_input_event(&event);

        // Use 0xE0+ range for 3DO native inputs (0xF0+ is SNES)
        event.dev_addr = 0xE0 + i;
        event.instance = 0;
        event.buttons = buttons;

        if (ctrl->type == TDO_DEVICE_MOUSE) {
            event.type = INPUT_TYPE_MOUSE;
            event.delta_x = ctrl->mouse_dx;
            event.delta_y = ctrl->mouse_dy;
        } else if (ctrl->type == TDO_DEVICE_JOYSTICK) {
            event.type = INPUT_TYPE_FLIGHTSTICK;
            event.analog[ANALOG_LX] = ctrl->analog_x;
            event.analog[ANALOG_LY] = ctrl->analog_y;
            event.analog[ANALOG_RX] = ctrl->analog_z;
            event.analog[ANALOG_RY] = ctrl->throttle;
        } else {
            event.type = INPUT_TYPE_GAMEPAD;
        }

        // Submit to router
        router_submit_input(&event);
    }
}

tdo_device_type_t tdo_host_get_device_type(uint8_t slot) {
    if (!initialized || slot >= TDO_HOST_MAX_CONTROLLERS) {
        return TDO_DEVICE_NONE;
    }
    return controllers[slot].type;
}

const tdo_controller_t* tdo_host_get_controller(uint8_t slot) {
    if (!initialized || slot >= TDO_HOST_MAX_CONTROLLERS) {
        return NULL;
    }
    return &controllers[slot];
}

bool tdo_host_is_connected(void) {
    if (!initialized) return false;
    return controller_count > 0;
}

uint8_t tdo_host_get_controller_count(void) {
    return controller_count;
}

