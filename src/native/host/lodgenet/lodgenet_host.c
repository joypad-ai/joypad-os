// lodgenet_host.c - LodgeNet Controller Host Driver
//
// PIO-based implementation of the LodgeNet proprietary serial protocol.
// Supports MCU protocol (N64/GC controllers) and SR protocol (SNES controllers)
// with auto-detection between them.
//
// Reference: lodgenet-gc-adapter-rp2040 (PIO, all 3 controller types)

#include "lodgenet_host.h"
#include "lodgenet.pio.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include <stdio.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

static bool initialized = false;
static uint8_t pin_clock;
static uint8_t pin_data;
static uint8_t pin_clock2;
static uint8_t pin_vcc;

// PIO resources — single SM, programs swapped on protocol change
static PIO pio_hw;
static uint pio_sm;
static uint pio_offset;
static const pio_program_t *current_program = NULL;

// Protocol and device state
typedef enum {
    PROTO_MCU,   // N64/GC microcontroller
    PROTO_SR,    // SNES shift register
} proto_mode_t;

static proto_mode_t proto = PROTO_MCU;
static lodgenet_device_t device_type = LODGENET_DEVICE_NONE;

// Connection tracking — matches reference exactly
static bool connected = false;
static uint8_t fail_count = 0;
static uint8_t good_count = 0;

// MCU encoded d-pad state (for virtual button detection)
static uint8_t last_dpad = 0;
static uint8_t last_menu = 0;

// Poll throttle
static uint32_t last_poll_us = 0;
#define MCU_POLL_INTERVAL_US 16000  // ~60Hz for N64/GC
#define SR_POLL_INTERVAL_US  7620   // ~131Hz for SNES (matches reference: (8ms*1000-380)/1)

// N64 stick scaling
#define N64_STICK_MAX 80

// ============================================================================
// PIO PROTOCOL MANAGEMENT — matches reference load_mcu/sr_protocol()
// ============================================================================

static void load_mcu_protocol(void)
{
    pio_sm_set_enabled(pio_hw, pio_sm, false);
    if (current_program)
        pio_remove_program(pio_hw, current_program, pio_offset);
    pio_offset = pio_add_program(pio_hw, &lodgenet_mcu_program);
    current_program = &lodgenet_mcu_program;
    lodgenet_mcu_pio_init(pio_hw, pio_sm, pio_offset, pin_clock, pin_data);
    proto = PROTO_MCU;
}

static void load_sr_protocol(void)
{
    pio_sm_set_enabled(pio_hw, pio_sm, false);
    if (current_program)
        pio_remove_program(pio_hw, current_program, pio_offset);
    pio_offset = pio_add_program(pio_hw, &lodgenet_sr_program);
    current_program = &lodgenet_sr_program;
    lodgenet_sr_pio_init(pio_hw, pio_sm, pio_offset, pin_clock, pin_clock2, pin_data);
    proto = PROTO_SR;
}

// ============================================================================
// MCU PROTOCOL — matches reference mcu_read() exactly
// ============================================================================

static bool mcu_read(uint8_t *bytes, uint num_bytes, bool *is_gc)
{
    while (!pio_sm_is_rx_fifo_empty(pio_hw, pio_sm))
        pio_sm_get(pio_hw, pio_sm);

    pio_sm_put(pio_hw, pio_sm, num_bytes * 8 - 1);

    uint32_t timeout_us = num_bytes * 8 * 50 + 5000;
    uint32_t start = time_us_32();

    for (uint i = 0; i < num_bytes; i++) {
        while (pio_sm_is_rx_fifo_empty(pio_hw, pio_sm)) {
            if ((time_us_32() - start) > timeout_us) {
                *is_gc = false;
                return false;
            }
        }
        bytes[i] = (uint8_t)(pio_sm_get(pio_hw, pio_sm));
    }

    bool has_mcu = (bytes[1] & 0x80) != 0;
    *is_gc = (bytes[1] & 0x40) != 0;
    bool forced_fail = *is_gc && (bytes[1] & 0x01);
    return has_mcu && !forced_fail;
}

// ============================================================================
// SR PROTOCOL — matches reference sr_read() exactly
// ============================================================================

static bool sr_read(uint16_t *value)
{
    while (!pio_sm_is_rx_fifo_empty(pio_hw, pio_sm))
        pio_sm_get(pio_hw, pio_sm);

    pio_sm_put(pio_hw, pio_sm, 15);

    uint32_t timeout_us = 5000;
    uint32_t start = time_us_32();

    uint8_t raw[3] = {0};
    for (int i = 0; i < 3; i++) {
        while (pio_sm_is_rx_fifo_empty(pio_hw, pio_sm)) {
            if ((time_us_32() - start) > timeout_us)
                return false;
        }
        raw[i] = (uint8_t)(pio_sm_get(pio_hw, pio_sm));
    }

    *value = ((uint16_t)raw[0] << 8) | raw[1];

    // Presence bit: LOW = controller present, HIGH = no controller
    bool present = !(raw[2] & 0x01);
    return present;
}

// ============================================================================
// BUTTON MAPPING: MCU (N64/GC) — matches reference parse_mcu()
// ============================================================================

static void submit_mcu(uint8_t *bytes, bool is_gc)
{
    uint32_t buttons = 0;

    // Common buttons (N64 + GC shared)
    if (!(bytes[0] & 0x20)) buttons |= JP_BUTTON_R1;   // Z
    if (!(bytes[0] & 0x10)) buttons |= JP_BUTTON_S2;   // Start
    if (!(bytes[1] & 0x20)) buttons |= JP_BUTTON_L2;   // L
    if (!(bytes[1] & 0x10)) buttons |= JP_BUTTON_R2;   // R

    // Encoded d-pad / virtual LodgeNet buttons — matches reference exactly
    uint8_t dpad = ~bytes[0] & 0x0F;

    uint8_t encoded_type = 0;
    if ((dpad & 0x03) == 0x03 || (dpad & 0x0C) == 0x0C) {
        if (last_dpad == 0) {
            if (dpad == 0x0F) encoded_type = 1;  // Reset
            if (dpad == 0x0C) encoded_type = 2;  // Menu
            if (dpad == 0x03) encoded_type = 3;  // *
            if (dpad == 0x0D) encoded_type = 4;  // Select
            if (dpad == 0x0B) encoded_type = 5;  // Order
            if (dpad == 0x0E) encoded_type = 6;  // #
            if (last_menu == 0)
                last_menu = encoded_type;
            else if (last_menu != encoded_type)
                encoded_type = last_menu;
        }
    } else {
        last_dpad = dpad;
        last_menu = 0;
    }

    if (last_dpad & 0x08) buttons |= JP_BUTTON_DU;
    if (last_dpad & 0x04) buttons |= JP_BUTTON_DD;
    if (last_dpad & 0x02) buttons |= JP_BUTTON_DL;
    if (last_dpad & 0x01) buttons |= JP_BUTTON_DR;

    if (encoded_type == 1) buttons |= JP_BUTTON_A4;   // Reset → A4
    if (encoded_type == 2) buttons |= JP_BUTTON_A1;   // Menu → Home
    if (encoded_type == 3) buttons |= JP_BUTTON_R4;   // * (Star) → R4
    if (encoded_type == 4) buttons |= JP_BUTTON_S1;   // Select → Back
    if (encoded_type == 5) buttons |= JP_BUTTON_A2;   // Order → Capture
    if (encoded_type == 6) buttons |= JP_BUTTON_L4;   // # (Hash) → L4

    uint8_t stick_lx, stick_ly, stick_rx, stick_ry;
    uint8_t trigger_l = 0, trigger_r = 0;

    if (is_gc) {
        if (!(bytes[0] & 0x40)) buttons |= JP_BUTTON_B2;  // A
        if (!(bytes[0] & 0x80)) buttons |= JP_BUTTON_B1;  // B
        if (!(bytes[1] & 0x04)) buttons |= JP_BUTTON_B4;  // X
        if (!(bytes[1] & 0x08)) buttons |= JP_BUTTON_B3;  // Y

        stick_lx = bytes[2];
        stick_ly = 255 - bytes[3];
        stick_rx = bytes[4];
        stick_ry = 255 - bytes[5];
        trigger_l = bytes[6];
        trigger_r = bytes[7];
        device_type = LODGENET_DEVICE_GC;
    } else { // N64
        if (!(bytes[0] & 0x80)) buttons |= JP_BUTTON_B1;  // A
        if (!(bytes[0] & 0x40)) buttons |= JP_BUTTON_B3;  // B
        if (!(bytes[1] & 0x08)) buttons |= JP_BUTTON_L3;  // C-Up
        if (!(bytes[1] & 0x04)) buttons |= JP_BUTTON_B2;  // C-Down
        if (!(bytes[1] & 0x02)) buttons |= JP_BUTTON_B4;  // C-Left
        if (!(bytes[1] & 0x01)) buttons |= JP_BUTTON_R3;  // C-Right

        int8_t raw_x = (int8_t)bytes[2];
        int8_t raw_y = (int8_t)bytes[3];
        if (raw_x > N64_STICK_MAX) raw_x = N64_STICK_MAX;
        if (raw_x < -N64_STICK_MAX) raw_x = -N64_STICK_MAX;
        if (raw_y > N64_STICK_MAX) raw_y = N64_STICK_MAX;
        if (raw_y < -N64_STICK_MAX) raw_y = -N64_STICK_MAX;

        int32_t scaled_x = ((int32_t)raw_x * 127) / N64_STICK_MAX;
        int32_t scaled_y = ((int32_t)raw_y * 127) / N64_STICK_MAX;
        stick_lx = (uint8_t)(scaled_x + 128);
        stick_ly = 255 - (uint8_t)(scaled_y + 128);

        stick_rx = 128;
        stick_ry = 128;
        device_type = LODGENET_DEVICE_N64;
    }

    input_event_t event;
    init_input_event(&event);
    event.dev_addr = 0xF0;
    event.instance = 0;
    event.type = INPUT_TYPE_GAMEPAD;
    event.transport = INPUT_TRANSPORT_NATIVE;
    event.layout = is_gc ? LAYOUT_GAMECUBE : LAYOUT_NINTENDO_N64;
    event.buttons = buttons;
    event.analog[ANALOG_LX] = stick_lx;
    event.analog[ANALOG_LY] = stick_ly;
    event.analog[ANALOG_RX] = stick_rx;
    event.analog[ANALOG_RY] = stick_ry;
    event.analog[ANALOG_L2] = trigger_l;
    event.analog[ANALOG_R2] = trigger_r;
    router_submit_input(&event);
}

// ============================================================================
// BUTTON MAPPING: SR (SNES) — matches reference parse_snes() exactly
// ============================================================================

static void submit_snes(uint16_t value)
{
    // LODG: M O B Y S * ↑ ↓ 1 1 ← → A X L R
    //       15 14 13 12 11 10 9 8 7 6 5 4 3 2 1 0
    uint32_t buttons = 0;

    if (!(value & 0x2000)) buttons |= JP_BUTTON_B1;  // B
    if (!(value & 0x0008)) buttons |= JP_BUTTON_B2;  // A
    if (!(value & 0x1000)) buttons |= JP_BUTTON_B3;  // Y
    if (!(value & 0x0004)) buttons |= JP_BUTTON_B4;  // X
    if (!(value & 0x0800)) buttons |= JP_BUTTON_S1;  // Select
    if (!(value & 0x0400)) buttons |= JP_BUTTON_S2;  // Start
    if (!(value & 0x0002)) buttons |= JP_BUTTON_L1;  // L
    if (!(value & 0x0001)) buttons |= JP_BUTTON_R1;  // R

    // D-pad with SOCD detection
    bool raw_up    = !(value & 0x0200);
    bool raw_down  = !(value & 0x0100);
    bool raw_left  = !(value & 0x0020);
    bool raw_right = !(value & 0x0010);

    bool ln_minus = raw_up && raw_down;
    bool ln_plus  = raw_left && raw_right;

    if (!ln_minus) {
        if (raw_up)   buttons |= JP_BUTTON_DU;
        if (raw_down) buttons |= JP_BUTTON_DD;
    }
    if (!ln_plus) {
        if (raw_left)  buttons |= JP_BUTTON_DL;
        if (raw_right) buttons |= JP_BUTTON_DR;
    }

    // LodgeNet system buttons
    if (!(value & 0x8000)) buttons |= JP_BUTTON_A1;  // Menu → Home
    if (!(value & 0x4000)) buttons |= JP_BUTTON_A2;  // Order → Capture
    if (ln_minus) buttons |= JP_BUTTON_A3;  // Minus
    if (ln_plus)  buttons |= JP_BUTTON_A4;  // Plus

    device_type = LODGENET_DEVICE_SNES;

    input_event_t event;
    init_input_event(&event);
    event.dev_addr = 0xF0;
    event.instance = 0;
    event.type = INPUT_TYPE_GAMEPAD;
    event.transport = INPUT_TRANSPORT_NATIVE;
    event.layout = LAYOUT_NINTENDO_4FACE;
    event.buttons = buttons;
    event.analog[ANALOG_LX] = 128;
    event.analog[ANALOG_LY] = 128;
    router_submit_input(&event);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void lodgenet_host_init(void)
{
    if (initialized) return;
    lodgenet_host_init_pins(LODGENET_PIN_CLOCK, LODGENET_PIN_DATA,
                            LODGENET_PIN_CLOCK2, LODGENET_PIN_VCC);
}

void lodgenet_host_init_pins(uint8_t clock, uint8_t data, uint8_t clock2, uint8_t vcc)
{
    printf("[lodgenet] init CLK=%d DATA=%d CLK2=%d VCC=%d\n", clock, data, clock2, vcc);

    pin_clock = clock;
    pin_data = data;
    pin_clock2 = clock2;
    pin_vcc = vcc;

    // VCC: output, drive HIGH to power controller
    gpio_init(pin_vcc);
    gpio_set_dir(pin_vcc, GPIO_OUT);
    gpio_put(pin_vcc, 1);

    // Clock: output, idle high, pull-up
    gpio_init(pin_clock);
    gpio_pull_up(pin_clock);
    gpio_set_dir(pin_clock, GPIO_OUT);
    gpio_put(pin_clock, 1);

    // Data: input with pull-up
    gpio_init(pin_data);
    gpio_pull_up(pin_data);
    gpio_set_dir(pin_data, GPIO_IN);

    // Clock2: output, idle high, pull-up
    gpio_init(pin_clock2);
    gpio_pull_up(pin_clock2);
    gpio_set_dir(pin_clock2, GPIO_OUT);
    gpio_put(pin_clock2, 1);

    // Claim PIO SM — with CONFIG_NO_NEOPIXEL, SM0 is free (matching reference)
    pio_hw = pio0;
    pio_sm = pio_claim_unused_sm(pio_hw, true);

    // Start with MCU protocol
    current_program = NULL;
    load_mcu_protocol();

    initialized = true;
    connected = false;
    device_type = LODGENET_DEVICE_NONE;
    fail_count = 0;
    good_count = 0;
    last_dpad = 0;
    last_menu = 0;

    printf("[lodgenet] ready PIO%d SM%d\n", pio_get_index(pio_hw), pio_sm);
}

// ============================================================================
// TASK — matches reference joybus_itf_poll() flow exactly
// ============================================================================

void lodgenet_host_task(void)
{
    if (!initialized) return;

    // SR protocol — throttled to ~125Hz (matching reference framework interval)
    if (proto == PROTO_SR) {
        uint32_t now_sr = time_us_32();
        if ((now_sr - last_poll_us) < SR_POLL_INTERVAL_US)
            return;
        last_poll_us = now_sr;
        uint16_t snes_value = 0;
        bool snes_present = sr_read(&snes_value);

        if (snes_present) {
            fail_count = 0;
            if (!connected) {
                connected = true;
                printf("[lodgenet] SNES connected\n");
            }
            submit_snes(snes_value);
        } else if (++fail_count >= 5) {
            if (connected) {
                connected = false;
                device_type = LODGENET_DEVICE_NONE;
                // Send cleared event
                input_event_t event;
                init_input_event(&event);
                event.dev_addr = 0xF0;
                event.type = INPUT_TYPE_GAMEPAD;
                event.transport = INPUT_TRANSPORT_NATIVE;
                router_submit_input(&event);
            }
            load_mcu_protocol();
            fail_count = 0;
            good_count = 0;
        }
        return;
    }

    // MCU protocol — throttled to ~60Hz
    uint32_t now = time_us_32();
    if ((now - last_poll_us) < MCU_POLL_INTERVAL_US)
        return;
    last_poll_us = now;

    uint8_t bytes[10];
    bool is_gc = false;
    bool valid = mcu_read(bytes, 10, &is_gc);

    if (valid) {
        proto = PROTO_MCU;
        fail_count = 0;
        if (++good_count >= 15) {
            if (!connected) {
                connected = true;
                printf("[lodgenet] %s connected\n", is_gc ? "GC" : "N64");
            }
            submit_mcu(bytes, is_gc);
        }
    } else {
        good_count = 0;
        if (++fail_count >= 5) {
            if (connected) {
                connected = false;
                device_type = LODGENET_DEVICE_NONE;
                input_event_t event;
                init_input_event(&event);
                event.dev_addr = 0xF0;
                event.type = INPUT_TYPE_GAMEPAD;
                event.transport = INPUT_TRANSPORT_NATIVE;
                router_submit_input(&event);
                last_dpad = 0;
                last_menu = 0;
            }
            load_sr_protocol();
            fail_count = 0;
            good_count = 0;
        }
    }
}

bool lodgenet_host_is_connected(void)
{
    return initialized && connected;
}

lodgenet_device_t lodgenet_host_get_device_type(void)
{
    return device_type;
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
