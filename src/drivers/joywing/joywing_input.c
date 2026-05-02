// joywing_input.c - Adafruit Joy FeatherWing Input Interface (multi-instance)
//
// Supports up to 2 JoyWing modules that merge into a single gamepad:
// JoyWing 0: Left stick, D-pad buttons, S1 (Select)
// JoyWing 1: Right stick, B1-B4 face buttons, S2 (Start)
// Both contribute to one merged input_event_t submitted to the router.

#include "joywing_input.h"
#include "drivers/seesaw/seesaw.h"
#include "core/buttons.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

// Joy FeatherWing button pin assignments (seesaw GPIO numbers)
#define JOYWING_BTN_A      6
#define JOYWING_BTN_B      7
#define JOYWING_BTN_X      9
#define JOYWING_BTN_Y      10
#define JOYWING_BTN_SELECT 14

// Joystick ADC channels
#define JOYWING_ADC_X      1
#define JOYWING_ADC_Y      0

// Button pin mask for GPIO bulk read
#define JOYWING_BTN_MASK ((1u << JOYWING_BTN_A) | (1u << JOYWING_BTN_B) | \
                          (1u << JOYWING_BTN_X) | (1u << JOYWING_BTN_Y) | \
                          (1u << JOYWING_BTN_SELECT))

// Device address — 0xE0 range for standalone, 0xF0 when merged with pad
#define JOYWING_MAX_INSTANCES 2

// Auto-calibrating ADC scaling — tracks min/max per axis
// Starts with a narrow assumed range and widens as extremes are seen
typedef struct {
    uint16_t min;
    uint16_t max;
    uint16_t center;
    bool calibrated;
} jw_adc_cal_t;

// 2 instances × 2 axes (X, Y)
static jw_adc_cal_t jw_cal[JOYWING_MAX_INSTANCES][2];

static void jw_cal_init(void) {
    for (int i = 0; i < JOYWING_MAX_INSTANCES; i++) {
        for (int a = 0; a < 2; a++) {
            jw_cal[i][a].min = 512;
            jw_cal[i][a].max = 512;
            jw_cal[i][a].center = 512;
            jw_cal[i][a].calibrated = false;
        }
    }
}

static uint8_t jw_scale_adc(uint8_t inst, uint8_t axis, uint16_t raw) {
    jw_adc_cal_t* cal = &jw_cal[inst][axis];

    // First reading: set center, start with narrow range
    if (!cal->calibrated) {
        cal->center = raw;
        cal->min = raw;
        cal->max = raw;
        cal->calibrated = true;
    }

    // Track actual min/max seen
    if (raw < cal->min) cal->min = raw;
    if (raw > cal->max) cal->max = raw;

    // Scale using tracked range, with margin expansion
    // Add 5% margin beyond observed extremes for smoother edge hitting
    uint16_t range = cal->max - cal->min;
    if (range < 50) {
        // Not enough data yet — use simple center-based scaling
        return 128;
    }

    int32_t margin = range / 20;  // 5% margin
    int32_t eff_min = (int32_t)cal->min + margin;
    int32_t eff_max = (int32_t)cal->max - margin;
    int32_t eff_range = eff_max - eff_min;
    if (eff_range < 50) eff_range = 50;

    if (raw <= eff_min) return 0;
    if (raw >= eff_max) return 255;
    return (uint8_t)(((int32_t)(raw - eff_min) * 255) / eff_range);
}

#define JOYWING_DEV_ADDR_STANDALONE 0xE0
#define JOYWING_DEV_ADDR_MERGED    0xF0

// When true, joywing_task skips router_submit_input (pad_input merges us)
static bool merge_with_pad = false;

// Per-instance hardware state
typedef struct {
    joywing_config_t cfg;
    seesaw_device_t seesaw;
    platform_i2c_t i2c_bus;
    bool configured;
    bool initialized;
    uint32_t last_poll;
} joywing_instance_t;

static joywing_instance_t instances[JOYWING_MAX_INSTANCES];
static uint8_t instance_count = 0;
// Number of instances that actually responded on I2C — drives single-JoyWing
// vs dual-JoyWing button/stick mapping. Different from instance_count which
// only reflects how many were configured.
static uint8_t initialized_count = 0;

// Single merged input event for all JoyWing instances
static input_event_t joywing_event;
static bool any_initialized = false;

void joywing_input_init_config(const joywing_config_t* config)
{
    if (instance_count >= JOYWING_MAX_INSTANCES) {
        printf("[joywing] Max instances (%d) reached\n", JOYWING_MAX_INSTANCES);
        return;
    }
    instances[instance_count].cfg = *config;
    instances[instance_count].configured = true;
    instance_count++;
}

static void joywing_init_instance(uint8_t idx)
{
    joywing_instance_t* jw = &instances[idx];
    if (!jw->configured) return;

    platform_i2c_config_t i2c_cfg = {
        .bus = jw->cfg.i2c_bus,
        .sda_pin = jw->cfg.sda_pin,
        .scl_pin = jw->cfg.scl_pin,
        .freq_hz = 400000,
    };
    jw->i2c_bus = platform_i2c_init(&i2c_cfg);
    if (!jw->i2c_bus) {
        printf("[joywing:%d] I2C init failed\n", idx);
        return;
    }

    uint8_t addr = jw->cfg.addr ? jw->cfg.addr : SEESAW_ADDR_DEFAULT;
    seesaw_init(&jw->seesaw, jw->i2c_bus, addr);

    uint8_t hw_id = seesaw_get_hw_id(&jw->seesaw);
    printf("[joywing:%d] Seesaw HW ID: 0x%02X (addr=0x%02X)\n", idx, hw_id, addr);
    if (hw_id == 0) {
        // No Seesaw responded — leave instance disabled so polling doesn't
        // keep timing out at 6 I2C ops/poll. Web config can still show
        // the JoyWing as configured; presence is decided at boot.
        printf("[joywing:%d] No device — instance disabled\n", idx);
        return;
    }

    if (!seesaw_gpio_set_input_pullup(&jw->seesaw, JOYWING_BTN_MASK)) {
        printf("[joywing:%d] GPIO config failed\n", idx);
    }

    jw->initialized = true;
    jw->last_poll = 0;
    any_initialized = true;
    initialized_count++;
    printf("[joywing:%d] Initialized\n", idx);
}

static void joywing_init(void)
{
    // Initialize ADC calibration
    jw_cal_init();

    // Initialize merged event (shared by all instances)
    init_input_event(&joywing_event);
    joywing_event.dev_addr = merge_with_pad ? JOYWING_DEV_ADDR_MERGED : JOYWING_DEV_ADDR_STANDALONE;
    joywing_event.instance = 0;
    joywing_event.type = INPUT_TYPE_GAMEPAD;
    joywing_event.transport = INPUT_TRANSPORT_GPIO;

    for (uint8_t i = 0; i < instance_count; i++) {
        joywing_init_instance(i);
    }
    printf("[joywing] %d instance(s) initialized\n", instance_count);
}

// Poll one instance and merge its data into the shared event
static void joywing_poll_instance(uint8_t idx)
{
    joywing_instance_t* jw = &instances[idx];
    if (!jw->initialized) return;

    uint32_t now = platform_time_ms();
    if (now - jw->last_poll < 10) return;
    jw->last_poll = now;

    // Read buttons (active low)
    uint32_t gpio = seesaw_gpio_read_bulk(&jw->seesaw);
    if (gpio == 0xFFFFFFFF) return;

    // Role assignment.
    //   2 JoyWings: idx 0 = D-pad + Select + LS, idx 1 = face + Start + RS.
    //   1 JoyWing:  the only JoyWing should be a usable controller on its
    //               own — face buttons + Start + LS, NOT just a D-pad.
    bool dpad_role = (initialized_count >= 2 && idx == 0);
    bool use_left_stick = (initialized_count < 2) || (idx == 0);

    if (dpad_role) {
        // Clear this instance's button bits before setting new ones
        joywing_event.buttons &= ~(JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR | JP_BUTTON_S1);
        if (!(gpio & (1u << JOYWING_BTN_Y)))      joywing_event.buttons |= JP_BUTTON_DU;
        if (!(gpio & (1u << JOYWING_BTN_B)))      joywing_event.buttons |= JP_BUTTON_DD;
        if (!(gpio & (1u << JOYWING_BTN_X)))      joywing_event.buttons |= JP_BUTTON_DL;
        if (!(gpio & (1u << JOYWING_BTN_A)))      joywing_event.buttons |= JP_BUTTON_DR;
        if (!(gpio & (1u << JOYWING_BTN_SELECT))) joywing_event.buttons |= JP_BUTTON_S1;
    } else {
        // Face buttons + Start
        joywing_event.buttons &= ~(JP_BUTTON_B1 | JP_BUTTON_B2 | JP_BUTTON_B3 | JP_BUTTON_B4 | JP_BUTTON_S2);
        if (!(gpio & (1u << JOYWING_BTN_B)))      joywing_event.buttons |= JP_BUTTON_B1;
        if (!(gpio & (1u << JOYWING_BTN_A)))      joywing_event.buttons |= JP_BUTTON_B2;
        if (!(gpio & (1u << JOYWING_BTN_X)))      joywing_event.buttons |= JP_BUTTON_B3;
        if (!(gpio & (1u << JOYWING_BTN_Y)))      joywing_event.buttons |= JP_BUTTON_B4;
        if (!(gpio & (1u << JOYWING_BTN_SELECT))) joywing_event.buttons |= JP_BUTTON_S2;
    }

    platform_sleep_us(500);

    // Read joystick
    uint16_t raw_x = seesaw_adc_read(&jw->seesaw, JOYWING_ADC_X);
    if (raw_x != SEESAW_ADC_ERROR) {
        platform_sleep_us(500);
        uint16_t raw_y = seesaw_adc_read(&jw->seesaw, JOYWING_ADC_Y);
        if (raw_y != SEESAW_ADC_ERROR) {
            if (use_left_stick) {
                joywing_event.analog[ANALOG_LX] = jw_scale_adc(idx, 0, raw_x);
                joywing_event.analog[ANALOG_LY] = jw_scale_adc(idx, 1, raw_y);
            } else {
                joywing_event.analog[ANALOG_RX] = jw_scale_adc(idx, 0, raw_x);
                joywing_event.analog[ANALOG_RY] = jw_scale_adc(idx, 1, raw_y);
            }
        }
    }
}

void joywing_set_merge_with_pad(bool merge)
{
    merge_with_pad = merge;
}

const input_event_t* joywing_get_event(void)
{
    return any_initialized ? &joywing_event : NULL;
}

static void joywing_task(void)
{
    if (!any_initialized) return;

    // Poll all instances (each has its own rate limit)
    for (uint8_t i = 0; i < instance_count; i++) {
        joywing_poll_instance(i);
    }

    // Only submit to router when standalone (not merged with pad)
    if (!merge_with_pad) {
        router_submit_input(&joywing_event);
    }
}

static bool joywing_is_connected(void)
{
    return any_initialized;
}

static uint8_t joywing_get_device_count(void)
{
    return any_initialized ? 1 : 0;  // Always 1 merged gamepad
}

const InputInterface joywing_input_interface = {
    .name = "JoyWing",
    .source = INPUT_SOURCE_GPIO,
    .init = joywing_init,
    .task = joywing_task,
    .is_connected = joywing_is_connected,
    .get_device_count = joywing_get_device_count,
};
