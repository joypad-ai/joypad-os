// psx_host.c - Native PS1 / PS2 controller host driver
//
// Bit-banged SPI-like protocol at ~250 kHz on four GPIO pins (CMD, CLK, ATT, DAT).
// ACK is unused for polling — the controller asserts it after each byte but we
// don't gate on it; a short inter-byte delay is enough.
//
// Protocol summary (psx-spx):
//   Host drops ATT low, then for each byte clocks 8 bits LSB-first:
//     - CMD is set while CLK is high
//     - Falling edge of CLK: controller samples CMD, starts driving next DAT bit
//     - DAT is sampled just before the next falling edge
//   After the last byte, host releases ATT.
//
// Poll sequence (0x01 0x42 0x00 ...):
//   byte 0: 0x01                   -> header byte (ignored)
//   byte 1: controller ID          -> 0x41 digital, 0x73 analog, 0x79 pressure
//   byte 2: 0x5A                   -> ready byte (ignored)
//   byte 3: buttons low            -> SELECT/L3/R3/START/DU/DR/DD/DL
//   byte 4: buttons high           -> L2/R2/L1/R1/TRIANGLE/CIRCLE/CROSS/SQUARE
//   byte 5..8 (analog only):       -> right X, right Y, left X, left Y
//   byte 9..20 (pressure only):    -> pressure bytes (ignored for now)

#include "psx_host.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// CONFIG
// ============================================================================

// Native dev_addr reserved for this host (distinct from PS/2 kbd at 0xE0).
#define PSX_DEV_ADDR   0xE1

// Bit time for ~250 kHz clock. 2 µs high + 2 µs low = 4 µs total per bit.
#define PSX_BIT_DELAY_US  2

// Inter-byte gap to let the controller ACK + prepare the next byte.
#define PSX_BYTE_GAP_US   15

// Digital mode response length (header + ID + ready + 2 button bytes).
#define PSX_DIGITAL_LEN   5
// Analog mode adds 4 stick bytes.
#define PSX_ANALOG_LEN    9

// Controller ID high nibble
#define PSX_ID_DIGITAL    0x41
#define PSX_ID_ANALOG     0x73
#define PSX_ID_PRESSURE   0x79
#define PSX_ID_NONE       0xFF

// ============================================================================
// INTERNAL STATE
// ============================================================================

static uint8_t pin_cmd = PSX_PIN_CMD;
static uint8_t pin_clk = PSX_PIN_CLK;
static uint8_t pin_att = PSX_PIN_ATT;
static uint8_t pin_dat = PSX_PIN_DAT;

static bool initialized = false;
static bool connected   = false;
static uint8_t last_id  = PSX_ID_NONE;

// State-change suppression
static uint32_t last_buttons = 0;
static uint8_t  last_lx = 128, last_ly = 128;
static uint8_t  last_rx = 128, last_ry = 128;
static bool     last_submitted = false;

// ============================================================================
// BIT-BANG TRANSFER
// ============================================================================

static inline uint8_t psx_xfer(uint8_t tx) {
    uint8_t rx = 0;
    for (int i = 0; i < 8; i++) {
        // Set CMD while clock is high (controller reads on next falling edge)
        gpio_put(pin_cmd, (tx >> i) & 1);
        busy_wait_us(PSX_BIT_DELAY_US);

        // Falling edge — controller latches CMD, starts driving new DAT bit
        gpio_put(pin_clk, 0);
        busy_wait_us(PSX_BIT_DELAY_US);

        // Sample DAT (now stable for this bit)
        if (gpio_get(pin_dat)) rx |= (1 << i);

        // Rising edge
        gpio_put(pin_clk, 1);
    }
    return rx;
}

// ============================================================================
// POLL + DECODE
// ============================================================================

static bool psx_poll(uint8_t* buf, size_t max_len) {
    gpio_put(pin_att, 0);
    busy_wait_us(PSX_BYTE_GAP_US);

    buf[0] = psx_xfer(0x01);            // header
    busy_wait_us(PSX_BYTE_GAP_US);
    buf[1] = psx_xfer(0x42);            // poll command -> controller ID
    busy_wait_us(PSX_BYTE_GAP_US);
    buf[2] = psx_xfer(0x00);            // 0x5A ready byte
    busy_wait_us(PSX_BYTE_GAP_US);

    // Treat 0xFF as "no controller" (DAT pulled up, no driver on the line).
    if (buf[1] == 0xFF || buf[1] == 0x00) {
        gpio_put(pin_att, 1);
        return false;
    }

    // Response length: low nibble of ID byte * 2 words = bytes to follow.
    // (0x41 -> 1 word = 2 bytes; 0x73 -> 3 words = 6 bytes including ID + ready)
    // Simpler approach: request enough bytes based on the ID.
    uint8_t id = buf[1];
    size_t total = PSX_DIGITAL_LEN;
    if (id == PSX_ID_ANALOG || id == PSX_ID_PRESSURE) total = PSX_ANALOG_LEN;
    if (total > max_len) total = max_len;

    for (size_t i = 3; i < total; i++) {
        buf[i] = psx_xfer(0x00);
        if (i + 1 < total) busy_wait_us(PSX_BYTE_GAP_US);
    }

    gpio_put(pin_att, 1);
    return true;
}

// Decode PS1/PS2 button bytes (active-low: 0 = pressed) into JP_BUTTON_* bitmap.
static uint32_t decode_buttons(uint8_t lo, uint8_t hi) {
    uint32_t b = 0;

    // Low byte: SELECT L3 R3 START UP RIGHT DOWN LEFT
    if (!(lo & 0x01)) b |= JP_BUTTON_S1;   // SELECT
    if (!(lo & 0x02)) b |= JP_BUTTON_L3;   // L3
    if (!(lo & 0x04)) b |= JP_BUTTON_R3;   // R3
    if (!(lo & 0x08)) b |= JP_BUTTON_S2;   // START
    if (!(lo & 0x10)) b |= JP_BUTTON_DU;   // D-Up
    if (!(lo & 0x20)) b |= JP_BUTTON_DR;   // D-Right
    if (!(lo & 0x40)) b |= JP_BUTTON_DD;   // D-Down
    if (!(lo & 0x80)) b |= JP_BUTTON_DL;   // D-Left

    // High byte: L2 R2 L1 R1 TRIANGLE CIRCLE CROSS SQUARE
    if (!(hi & 0x01)) b |= JP_BUTTON_L2;
    if (!(hi & 0x02)) b |= JP_BUTTON_R2;
    if (!(hi & 0x04)) b |= JP_BUTTON_L1;
    if (!(hi & 0x08)) b |= JP_BUTTON_R1;
    if (!(hi & 0x10)) b |= JP_BUTTON_B4;   // Triangle -> B4
    if (!(hi & 0x20)) b |= JP_BUTTON_B2;   // Circle   -> B2
    if (!(hi & 0x40)) b |= JP_BUTTON_B1;   // Cross    -> B1
    if (!(hi & 0x80)) b |= JP_BUTTON_B3;   // Square   -> B3

    return b;
}

// ============================================================================
// PIN SETUP
// ============================================================================

static void psx_gpio_init(void) {
    // CMD/CLK/ATT are outputs; DAT is an input with a pull-up.
    gpio_init(pin_cmd);
    gpio_set_dir(pin_cmd, GPIO_OUT);
    gpio_put(pin_cmd, 1);

    gpio_init(pin_clk);
    gpio_set_dir(pin_clk, GPIO_OUT);
    gpio_put(pin_clk, 1);

    gpio_init(pin_att);
    gpio_set_dir(pin_att, GPIO_OUT);
    gpio_put(pin_att, 1);

    gpio_init(pin_dat);
    gpio_set_dir(pin_dat, GPIO_IN);
    gpio_pull_up(pin_dat);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void psx_host_init(void) {
    psx_host_init_pins(PSX_PIN_CMD, PSX_PIN_CLK, PSX_PIN_ATT, PSX_PIN_DAT);
}

void psx_host_init_pins(uint8_t cmd, uint8_t clk, uint8_t att, uint8_t dat) {
    if (initialized) return;

    pin_cmd = cmd;
    pin_clk = clk;
    pin_att = att;
    pin_dat = dat;

    printf("[psx_host] Initialising PS1/PS2 host CMD=GP%u CLK=GP%u ATT=GP%u DAT=GP%u\n",
           pin_cmd, pin_clk, pin_att, pin_dat);

    psx_gpio_init();
    initialized = true;
    connected = false;
    last_id = PSX_ID_NONE;
    last_submitted = false;
}

void psx_host_task(void) {
    if (!initialized) return;

    uint8_t buf[PSX_ANALOG_LEN] = {0};
    bool ok = psx_poll(buf, sizeof(buf));

    if (!ok) {
        if (connected) {
            printf("[psx_host] Controller disconnected\n");
            connected = false;
            last_id = PSX_ID_NONE;
        }
        return;
    }

    if (!connected || buf[1] != last_id) {
        printf("[psx_host] Controller detected, ID=0x%02x\n", buf[1]);
        last_id = buf[1];
        connected = true;
    }

    uint32_t buttons = decode_buttons(buf[3], buf[4]);
    uint8_t rx = 128, ry = 128, lx = 128, ly = 128;

    if (last_id == PSX_ID_ANALOG || last_id == PSX_ID_PRESSURE) {
        // Stick order is RX, RY, LX, LY per psx-spx. PSX sticks are already
        // HID-compatible (0=up/left, 128=center, 255=down/right).
        rx = buf[5];
        ry = buf[6];
        lx = buf[7];
        ly = buf[8];
    }

    if (last_submitted &&
        buttons == last_buttons &&
        lx == last_lx && ly == last_ly &&
        rx == last_rx && ry == last_ry) {
        return;
    }
    last_buttons = buttons;
    last_lx = lx; last_ly = ly;
    last_rx = rx; last_ry = ry;
    last_submitted = true;

    input_event_t event;
    init_input_event(&event);
    event.dev_addr  = PSX_DEV_ADDR;
    event.instance  = 0;
    event.type      = INPUT_TYPE_GAMEPAD;
    event.transport = INPUT_TRANSPORT_NATIVE;
    event.layout    = LAYOUT_MODERN_4FACE;
    event.buttons   = buttons;
    event.analog[ANALOG_LX] = lx;
    event.analog[ANALOG_LY] = ly;
    event.analog[ANALOG_RX] = rx;
    event.analog[ANALOG_RY] = ry;

    router_submit_input(&event);
}

bool psx_host_is_connected(void) {
    return connected;
}

// ============================================================================
// INPUT INTERFACE
// ============================================================================

static uint8_t psx_get_device_count(void) {
    return connected ? 1 : 0;
}

const InputInterface psx_input_interface = {
    .name = "PS1/PS2",
    .source = INPUT_SOURCE_NATIVE_PSX,
    .init = psx_host_init,
    .task = psx_host_task,
    .is_connected = psx_host_is_connected,
    .get_device_count = psx_get_device_count,
};
