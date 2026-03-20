// lodgenet_host.c - LodgeNet Controller Host Driver
//
// Implements the proprietary LodgeNet serial protocol to read N64 and
// GameCube hotel gaming controllers. Protocol reverse-engineered from
// HoriGC_Trimmed.SFC (SNES-based controller tester ROM).
//
// Protocol summary:
//   3-wire: +5V, CLOCK (host out), DATA (controller out), GND
//   Frame: double-strobe start + 64 data bits clocked on falling edge
//   Data: 8 bytes — buttons(2) + sticks(4) + triggers(2)
//   Rate: polled at 60Hz, ~3.4ms per frame, ~20kHz clock

#include "lodgenet_host.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include <stdio.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

static bool initialized = false;
static uint8_t pin_clock;
static uint8_t pin_data;

// Connection tracking
static bool connected = false;
static uint32_t last_valid_ms = 0;
static uint8_t disconnect_count = 0;
#define DISCONNECT_THRESHOLD 30   // Require N consecutive failures before disconnect

// Change detection
static uint32_t prev_buttons = 0xFFFFFFFF;
static uint32_t prev_analog = 0xFFFFFFFF;

// Calibration (analog center values captured at startup)
static bool calibrated = false;
static uint8_t cal_lx, cal_ly, cal_rx, cal_ry;

// Raw frame data from controller
static uint8_t frame[8];

// ============================================================================
// PROTOCOL IMPLEMENTATION
// ============================================================================

// Read one complete 64-bit frame from the controller.
// Returns true if data was received (DATA line not stuck high/low).
static bool lodgenet_read_frame(uint8_t out[8])
{
    // ── Init strobe: double-pulse start signal ──
    // Pattern: LOW(6.7µs) HIGH(6.0µs) LOW(6.7µs) HIGH(22.8µs settling)
    // This tells the controller/adapter to latch its state.

    gpio_put(pin_clock, 0);
    busy_wait_us(7);
    gpio_put(pin_clock, 1);
    busy_wait_us(6);
    gpio_put(pin_clock, 0);
    busy_wait_us(7);
    gpio_put(pin_clock, 1);
    busy_wait_us(23);

    // ── Clock out 64 bits (8 bytes x 8 bits) ──
    // Data sampled on falling edge, LSB first within each byte.
    // Clock: ~26µs low, ~24µs high per bit.

    uint8_t ones = 0;   // Track all-ones (no controller)
    uint8_t zeros = 0;  // Track all-zeros (shorted/stuck)

    for (int byte_idx = 0; byte_idx < 8; byte_idx++) {
        uint8_t val = 0;

        for (int bit = 0; bit < 8; bit++) {
            // Falling edge — controller presents data
            gpio_put(pin_clock, 0);
            busy_wait_us(2);            // Data setup time

            // Sample DATA line
            uint8_t d = gpio_get(pin_data) ? 1 : 0;

            // Shift in LSB first (matches original SNES ROR chain)
            val = (val >> 1) | (d << 7);

            busy_wait_us(24);           // Remainder of low phase

            // Rising edge
            gpio_put(pin_clock, 1);
            busy_wait_us(24);           // High phase hold
        }

        out[byte_idx] = val;

        if (val == 0xFF) ones++;
        if (val == 0x00) zeros++;
    }

    // Leave clock high (idle state)
    gpio_put(pin_clock, 1);

    // Detect no-controller: all 0xFF (floating data with pull-up)
    // or all 0x00 (shorted/stuck low)
    if (ones == 8 || zeros == 8) {
        return false;
    }

    return true;
}

// ============================================================================
// BUTTON MAPPING
// ============================================================================

// Map LodgeNet controller data to JP_BUTTON_* format.
//
// Button bit assignments decoded from the test ROM's table-driven
// diagnostic at $8962. The test ROM individually verifies each button
// by checking for exactly one bit set while all others are released.
//
// Raw data is active-low (0 = pressed) and shifted LSB-first.
// The caller inverts bytes (XOR $FF) before passing to this function,
// so here 1 = pressed.
//
// Byte 0 ($3E in SNES ZP):
//   bit 0: A button         (test sub 4: $3E == $01)
//   bit 1: B button         (test sub 3: $3E == $02)
//   bit 2: X button         (not individually tested — inferred position)
//   bit 3: Start            (test sub 2: $3E == $08)
//   bit 4: D-pad Up         (test sub 11: $3E == $10)
//   bit 5: D-pad Down       (test sub 12: $3E == $20)
//   bit 6: D-pad Left       (test sub 13: $3E == $40)
//   bit 7: D-pad Right      (test sub 14: $3E == $80)
//
// Byte 1 ($40 in SNES ZP):
//   bit 0: Y button         (not individually tested — inferred position)
//   bit 1: (unknown)
//   bit 2: Z trigger        (test sub 19: $40 == $84, i.e. bit2 + bit7)
//   bit 3: (unknown)
//   bit 4: R shoulder        (test sub 1: $40 == $90, i.e. bit4 + bit7)
//   bit 5: L shoulder        (test sub 0: $40 == $A0, i.e. bit5 + bit7)
//   bit 6: (unknown)
//   bit 7: Always set        (connection indicator — not a button)

static uint32_t map_lodgenet_buttons(const uint8_t* data)
{
    uint32_t buttons = 0;
    uint8_t b0 = data[0] ^ 0xFF;   // Invert (active low → active high)
    uint8_t b1 = data[1] ^ 0xFF;

    // Byte 0: Face buttons + D-pad
    if (b0 & 0x01) buttons |= JP_BUTTON_B2;   // A → B2 (Switch A)
    if (b0 & 0x02) buttons |= JP_BUTTON_B1;   // B → B1 (Switch B)
    if (b0 & 0x04) buttons |= JP_BUTTON_B4;   // X → B4 (Switch X)
    if (b0 & 0x08) buttons |= JP_BUTTON_S2;   // Start → S2 (Plus)
    if (b0 & 0x10) buttons |= JP_BUTTON_DU;   // D-pad Up
    if (b0 & 0x20) buttons |= JP_BUTTON_DD;   // D-pad Down
    if (b0 & 0x40) buttons |= JP_BUTTON_DL;   // D-pad Left
    if (b0 & 0x80) buttons |= JP_BUTTON_DR;   // D-pad Right

    // Byte 1: Shoulders + Z + Y (bit 7 ignored — always-on indicator)
    if (b1 & 0x01) buttons |= JP_BUTTON_B3;   // Y → B3 (Switch Y)
    if (b1 & 0x04) buttons |= JP_BUTTON_L2;   // Z → L2 (ZL trigger)
    if (b1 & 0x10) buttons |= JP_BUTTON_R1;   // R shoulder → R1
    if (b1 & 0x20) buttons |= JP_BUTTON_L1;   // L shoulder → L1

    return buttons;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void lodgenet_host_init(void)
{
    if (initialized) return;
    lodgenet_host_init_pins(LODGENET_PIN_CLOCK, LODGENET_PIN_DATA);
}

void lodgenet_host_init_pins(uint8_t clock, uint8_t data)
{
    printf("[lodgenet] Initializing LodgeNet host driver\n");
    printf("[lodgenet]   CLOCK=%d, DATA=%d\n", clock, data);

    pin_clock = clock;
    pin_data = data;

    // Clock: output, idle high
    gpio_init(pin_clock);
    gpio_set_dir(pin_clock, GPIO_OUT);
    gpio_put(pin_clock, 1);

    // Data: input with pull-up (floating = 1 when no controller)
    gpio_init(pin_data);
    gpio_set_dir(pin_data, GPIO_IN);
    gpio_pull_up(pin_data);

    initialized = true;
    connected = false;
    calibrated = false;
    disconnect_count = 0;
    prev_buttons = 0xFFFFFFFF;
    prev_analog = 0xFFFFFFFF;

    printf("[lodgenet] Initialization complete\n");
}

void lodgenet_host_task(void)
{
    if (!initialized) return;

    uint32_t now_ms = time_us_32() / 1000;

    // Read a frame
    bool ok = lodgenet_read_frame(frame);

    if (!ok) {
        // No valid data — count toward disconnect
        disconnect_count++;
        if (disconnect_count >= DISCONNECT_THRESHOLD && connected) {
            connected = false;
            calibrated = false;
            printf("[lodgenet] Controller disconnected\n");

            // Send cleared event to release any stuck buttons
            input_event_t event;
            init_input_event(&event);
            event.dev_addr = 0xF0;
            event.type = INPUT_TYPE_GAMEPAD;
            event.transport = INPUT_TRANSPORT_NATIVE;
            router_submit_input(&event);
            prev_buttons = 0;
            prev_analog = 0;
        }
        return;
    }

    // Valid data received
    disconnect_count = 0;
    last_valid_ms = now_ms;

    if (!connected) {
        connected = true;
        printf("[lodgenet] Controller connected\n");
    }

    // Calibrate analog center on first valid read
    if (!calibrated) {
        cal_lx = frame[2] ^ 0xFF;
        cal_ly = frame[3] ^ 0xFF;
        cal_rx = frame[4] ^ 0xFF;
        cal_ry = frame[5] ^ 0xFF;
        calibrated = true;
        printf("[lodgenet] Calibrated center: LX=%d LY=%d RX=%d RY=%d\n",
               cal_lx, cal_ly, cal_rx, cal_ry);
    }

    // Map buttons
    uint32_t buttons = map_lodgenet_buttons(frame);

    // Map analog axes (invert from active-low, center-calibrate, normalize to HID)
    // Raw values are active-low, so invert first
    uint8_t raw_lx = frame[2] ^ 0xFF;
    uint8_t raw_ly = frame[3] ^ 0xFF;
    uint8_t raw_rx = frame[4] ^ 0xFF;
    uint8_t raw_ry = frame[5] ^ 0xFF;
    uint8_t raw_lt = frame[6] ^ 0xFF;
    uint8_t raw_rt = frame[7] ^ 0xFF;

    // Center-calibrate sticks: subtract baseline, add 128
    // Clamp to 0-255 range
    int16_t lx = (int16_t)(raw_lx - cal_lx) + 128;
    int16_t ly = (int16_t)(raw_ly - cal_ly) + 128;
    int16_t rx = (int16_t)(raw_rx - cal_rx) + 128;
    int16_t ry = (int16_t)(raw_ry - cal_ry) + 128;

    if (lx < 0) lx = 0; if (lx > 255) lx = 255;
    if (ly < 0) ly = 0; if (ly > 255) ly = 255;
    if (rx < 0) rx = 0; if (rx > 255) rx = 255;
    if (ry < 0) ry = 0; if (ry > 255) ry = 255;

    // LodgeNet uses Nintendo convention (0=down/left?, 255=up/right?)
    // Invert Y for HID convention: 0=up, 255=down
    uint8_t stick_lx = (uint8_t)lx;
    uint8_t stick_ly = 255 - (uint8_t)ly;
    uint8_t stick_rx = (uint8_t)rx;
    uint8_t stick_ry = 255 - (uint8_t)ry;

    // Triggers are 0-255 (0 = released)
    uint8_t trigger_l = raw_lt;
    uint8_t trigger_r = raw_rt;

    // Only submit if state changed
    uint32_t analog_packed = ((uint32_t)stick_lx << 24) | ((uint32_t)stick_ly << 16) |
                             ((uint32_t)stick_rx << 8) | stick_ry;
    if (buttons == prev_buttons && analog_packed == prev_analog) {
        return;
    }
    prev_buttons = buttons;
    prev_analog = analog_packed;

    // Build and submit input event
    input_event_t event;
    init_input_event(&event);

    event.dev_addr = 0xF0;
    event.instance = 0;
    event.type = INPUT_TYPE_GAMEPAD;
    event.transport = INPUT_TRANSPORT_NATIVE;
    event.layout = LAYOUT_MODERN_4FACE;
    event.buttons = buttons;
    event.analog[ANALOG_LX] = stick_lx;
    event.analog[ANALOG_LY] = stick_ly;
    event.analog[ANALOG_RX] = stick_rx;
    event.analog[ANALOG_RY] = stick_ry;
    event.analog[ANALOG_L2] = trigger_l;
    event.analog[ANALOG_R2] = trigger_r;

    router_submit_input(&event);
}

bool lodgenet_host_is_connected(void)
{
    return initialized && connected;
}

// ============================================================================
// INPUT INTERFACE
// ============================================================================

static uint8_t lodgenet_get_device_count(void)
{
    return lodgenet_host_is_connected() ? 1 : 0;
}

const InputInterface lodgenet_input_interface = {
    .name = "LodgeNet",
    .source = INPUT_SOURCE_NATIVE_LODGENET,
    .init = lodgenet_host_init,
    .task = lodgenet_host_task,
    .is_connected = lodgenet_host_is_connected,
    .get_device_count = lodgenet_get_device_count,
};
