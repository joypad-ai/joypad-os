// ws2812_nrf.c - RGB LED driver for nRF52840 boards
//
// XIAO nRF52840:    3 discrete LEDs (R=P0.26, G=P0.30, B=P0.06), active LOW
// Feather nRF52840:  WS2812 NeoPixel on P0.16 via PWM3 + EasyDMA
//                    + discrete blue LED on P1.10 (fallback alive indicator)

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#ifdef BOARD_FEATHER_NRF52840

// ============================================================================
// Feather nRF52840: WS2812 NeoPixel via PWM + EasyDMA
//                   + discrete blue LED on P1.10 (alive indicator)
//
// Uses the nRF52840 PWM peripheral with DMA to generate WS2812 waveforms.
// Hardware-timed: no CPU involvement during transmission, fully BLE-safe.
// Matches the approach used by Adafruit NeoPixel (Arduino) and CircuitPython.
// ============================================================================

#include <nrfx.h>
#include <hal/nrf_gpio.h>

#define BLUE_LED_PIN       10  // P1.10, active HIGH
#define NEOPIXEL_POWER_PIN 14  // P1.14
#define NEOPIXEL_PIN       16  // P0.16

// PWM WS2812 timing at 16 MHz clock (62.5 ns per tick)
// COUNTERTOP = 20 → period = 1.25 μs = 800 kHz WS2812 frequency
// Bit 15 (0x8000) = DUTY_NORMAL: pin HIGH for compare ticks, LOW for rest
#define PWM_TOP   20
#define T0H_DUTY  (6  | 0x8000)  // 375 ns HIGH  (spec 220-380)
#define T1H_DUTY  (13 | 0x8000)  // 812 ns HIGH  (spec 580-1000)
#define T_LOW     (0  | 0x8000)  // pin LOW entire cycle (reset/idle)

// 24 bits per GRB pixel + 1 trailing LOW cycle
static uint16_t pwm_seq[25] __attribute__((aligned(4)));

// Send one WS2812 pixel via PWM3 + EasyDMA (hardware-timed, BLE-safe)
//
// Feather nRF52840 NeoPixel is powered from 3.3V via P1.14 load switch.
// At 3.3V VDD, the red LED has a low Vf (~2.0V) and draws significantly
// more current than green (~3.0V) or blue (~3.2V). When red exceeds ~64
// and other channels are active, the resulting current droops VDD enough
// to shut off the higher-Vf green/blue LEDs (they appear as red-only).
// Green+blue alone works at any value (CYAN at 255+255 is fine).
// When red is combined with green/blue, a lower cap is needed because
// the red current droop reduces headroom for the higher-Vf LEDs.
// Additionally, green/blue LEDs don't visibly conduct below ~40 PWM value
// at 3.3V due to their high Vf (~3.0-3.2V). Red starts conducting much
// earlier (~5-10), causing a visible "dim red" flash during brightness
// ramps instead of the intended color. Suppress red when G/B are below
// their visibility threshold to prevent this.
#define NEO_RED_MAX       60   // Red-only (no G/B competition)
#define NEO_RED_MIXED_MAX 20   // Red when G or B is also active
#define NEO_GB_VIS_THRESH 40   // Green/blue visibility threshold at 3.3V

static void ws2812_send_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    if (r > 0 && (g > 0 || b > 0)) {
        // Gradually ramp red from 0 to NEO_RED_MIXED_MAX as the brighter
        // of G/B rises from threshold to 2x threshold. This prevents both
        // the "dim red" flash (red visible before G/B conduct) and a hard
        // color pop at the transition.
        uint8_t max_gb = g > b ? g : b;
        uint8_t allowed;
        if (max_gb < NEO_GB_VIS_THRESH) {
            allowed = 0;
        } else {
            allowed = ((uint16_t)(max_gb - NEO_GB_VIS_THRESH)
                       * NEO_RED_MIXED_MAX) / NEO_GB_VIS_THRESH;
            if (allowed > NEO_RED_MIXED_MAX) allowed = NEO_RED_MIXED_MAX;
        }
        if (r > allowed) r = allowed;
    } else if (r > NEO_RED_MAX) {
        r = NEO_RED_MAX;
    }
    uint8_t grb[3] = { g, r, b };

    // Expand each bit to a 16-bit PWM duty cycle value
    int idx = 0;
    for (int byte = 0; byte < 3; byte++) {
        uint8_t val = grb[byte];
        for (int bit = 7; bit >= 0; bit--) {
            pwm_seq[idx++] = (val & (1 << bit)) ? T1H_DUTY : T0H_DUTY;
        }
    }
    pwm_seq[24] = T_LOW;  // ensure pin LOW after sequence

    NRF_PWM3->ENABLE = 0;

    // Pin select: P0.16 (port 0 << 5 | pin 16 = 16)
    NRF_PWM3->PSEL.OUT[0] = NEOPIXEL_PIN;
    NRF_PWM3->PSEL.OUT[1] = 0xFFFFFFFFUL;  // disconnected
    NRF_PWM3->PSEL.OUT[2] = 0xFFFFFFFFUL;
    NRF_PWM3->PSEL.OUT[3] = 0xFFFFFFFFUL;

    NRF_PWM3->MODE = PWM_MODE_UPDOWN_Up;
    NRF_PWM3->PRESCALER = PWM_PRESCALER_PRESCALER_DIV_1;  // 16 MHz
    NRF_PWM3->COUNTERTOP = PWM_TOP;
    NRF_PWM3->LOOP = 0;  // one-shot

    NRF_PWM3->DECODER =
        (PWM_DECODER_LOAD_Common << PWM_DECODER_LOAD_Pos) |
        (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);

    NRF_PWM3->SEQ[0].PTR = (uint32_t)pwm_seq;
    NRF_PWM3->SEQ[0].CNT = 25;
    NRF_PWM3->SEQ[0].REFRESH = 0;
    NRF_PWM3->SEQ[0].ENDDELAY = 0;
    NRF_PWM3->SEQ[1].PTR = 0;
    NRF_PWM3->SEQ[1].CNT = 0;
    NRF_PWM3->SEQ[1].REFRESH = 0;
    NRF_PWM3->SEQ[1].ENDDELAY = 0;

    NRF_PWM3->ENABLE = 1;

    NRF_PWM3->EVENTS_SEQEND[0] = 0;
    NRF_PWM3->EVENTS_STOPPED = 0;

    NRF_PWM3->TASKS_SEQSTART[0] = 1;

    while (!NRF_PWM3->EVENTS_SEQEND[0]) {}

    NRF_PWM3->TASKS_STOP = 1;
    while (!NRF_PWM3->EVENTS_STOPPED) {}

    NRF_PWM3->ENABLE = 0;

    // Pin returns to GPIO control (configured as output LOW)
}

// Breathing brightness scale (0-255) — triangle wave with quadratic easing
static inline uint8_t breathing_scale(int t)
{
    int phase = t % 150;  // ~3s cycle at 20ms per tic
    int ramp = phase < 75 ? phase : (150 - phase);
    return 4 + (uint8_t)((uint32_t)ramp * ramp * 251 / 5625);
}

// Current pixel color (to avoid redundant sends)
static uint8_t cur_r, cur_g, cur_b;
static bool cur_valid = false;

static void neo_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (cur_valid && cur_r == r && cur_g == g && cur_b == b) return;
    ws2812_send_pixel(r, g, b);
    cur_r = r; cur_g = g; cur_b = b;
    cur_valid = true;
}

static void neo_set_off(void)
{
    neo_set_color(0, 0, 0);
}

// Discrete blue LED on P1.10 (alive indicator / fallback)
static const struct device *gpio1_dev;

static void blue_led_set(bool on)
{
    if (gpio1_dev) {
        gpio_pin_set(gpio1_dev, BLUE_LED_PIN, on ? 1 : 0);
    }
}

#else

// ============================================================================
// XIAO nRF52840: Discrete LEDs (R=P0.26, G=P0.30, B=P0.06), active LOW
// ============================================================================

#define LED_RED_PIN   26  // P0.26
#define LED_GREEN_PIN 30  // P0.30
#define LED_BLUE_PIN   6  // P0.06
#define LED_PORT_LABEL DT_NODELABEL(gpio0)

#endif  // BOARD_FEATHER_NRF52840

#ifdef BOARD_FEATHER_NRF52840
static bool neopixel_ready = false;
#else
static const struct device *led_port;
#endif

// Override color (set by app for USB mode indication)
static uint8_t override_r = 0, override_g = 0, override_b = 0;
static bool has_override_color = false;

// Timing
static uint32_t last_update_ms = 0;
static int tic = 0;

// Profile indicator state
typedef enum {
    LED_IDLE,
    LED_BLINK_ON,
    LED_BLINK_OFF,
} led_state_t;

static led_state_t led_state = LED_IDLE;
static uint8_t blinks_remaining = 0;
static uint32_t state_change_ms = 0;

#define BLINK_OFF_MS    200
#define BLINK_ON_MS     100

#ifndef BOARD_FEATHER_NRF52840
// ============================================================================
// XIAO GPIO HELPERS
// ============================================================================

static void set_rgb(bool r, bool g, bool b)
{
    if (!led_port) return;
    gpio_pin_set(led_port, LED_RED_PIN, r ? 0 : 1);
    gpio_pin_set(led_port, LED_GREEN_PIN, g ? 0 : 1);
    gpio_pin_set(led_port, LED_BLUE_PIN, b ? 0 : 1);
}

static void set_color(uint8_t r, uint8_t g, uint8_t b)
{
    set_rgb(r > 0, g > 0, b > 0);
}

static void set_off(void)
{
    set_rgb(false, false, false);
}
#endif  // !BOARD_FEATHER_NRF52840

// ============================================================================
// PUBLIC API
// ============================================================================

void neopixel_init(void)
{
#ifdef BOARD_FEATHER_NRF52840
    // Init discrete blue LED on P1.10 (alive indicator)
    gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));
    if (!device_is_ready(gpio1_dev)) {
        printf("[led_nrf] gpio1 not ready!\n");
        gpio1_dev = NULL;
    } else {
        gpio_pin_configure(gpio1_dev, BLUE_LED_PIN, GPIO_OUTPUT_LOW);

        // Enable NeoPixel power on P1.14
        gpio_pin_configure(gpio1_dev, NEOPIXEL_POWER_PIN, GPIO_OUTPUT_HIGH);
        printf("[led_nrf] NeoPixel power enabled on P1.%d\n", NEOPIXEL_POWER_PIN);
        k_msleep(10);  // power stabilization
    }

    // Configure P0.16 as output with high drive for NeoPixel data
    nrf_gpio_cfg(NEOPIXEL_PIN,
                 NRF_GPIO_PIN_DIR_OUTPUT,
                 NRF_GPIO_PIN_INPUT_DISCONNECT,
                 NRF_GPIO_PIN_NOPULL,
                 NRF_GPIO_PIN_H0H1,
                 NRF_GPIO_PIN_NOSENSE);
    nrf_gpio_pin_clear(NEOPIXEL_PIN);
    k_busy_wait(100);

    neopixel_ready = true;
    printf("[led_nrf] NeoPixel ready (PWM+EasyDMA on P0.%d)\n",
           NEOPIXEL_PIN);

    blue_led_set(true);
#else
    led_port = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(led_port)) {
        printf("[led_nrf] GPIO port not ready\n");
        led_port = NULL;
        return;
    }

    gpio_pin_configure(led_port, LED_RED_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(led_port, LED_GREEN_PIN, GPIO_OUTPUT_HIGH);
    gpio_pin_configure(led_port, LED_BLUE_PIN, GPIO_OUTPUT_HIGH);
    printf("[led_nrf] RGB LEDs initialized (R=P0.%d G=P0.%d B=P0.%d, active low)\n",
           LED_RED_PIN, LED_GREEN_PIN, LED_BLUE_PIN);
#endif
}

void neopixel_set_override_color(uint8_t r, uint8_t g, uint8_t b)
{
    override_r = r;
    override_g = g;
    override_b = b;
    has_override_color = true;
}

void neopixel_indicate_profile(uint8_t profile_index)
{
    if (led_state == LED_IDLE) {
        blinks_remaining = profile_index + 1;
        led_state = LED_BLINK_OFF;
        state_change_ms = k_uptime_get_32();
    }
}

bool neopixel_is_indicating(void)
{
    return led_state != LED_IDLE;
}

void neopixel_task(int pat)
{
#ifdef BOARD_FEATHER_NRF52840
    if (!neopixel_ready) return;
#else
    if (!led_port) return;
#endif

    uint32_t now = k_uptime_get_32();

    // Profile indicator state machine
    if (led_state != LED_IDLE) {
        uint32_t elapsed = now - state_change_ms;

        switch (led_state) {
            case LED_BLINK_OFF:
#ifdef BOARD_FEATHER_NRF52840
                neo_set_off();
                blue_led_set(false);
#else
                set_off();
#endif
                if (elapsed >= BLINK_OFF_MS) {
                    blinks_remaining--;
                    if (blinks_remaining > 0) {
                        led_state = LED_BLINK_ON;
                    } else {
                        led_state = LED_IDLE;
                    }
                    state_change_ms = now;
                }
                break;

            case LED_BLINK_ON:
#ifdef BOARD_FEATHER_NRF52840
                if (has_override_color) {
                    neo_set_color(override_r, override_g, override_b);
                } else {
                    neo_set_color(64, 64, 64);
                }
                blue_led_set(true);
#else
                if (has_override_color) {
                    set_color(override_r, override_g, override_b);
                } else {
                    set_rgb(true, true, true);
                }
#endif
                if (elapsed >= BLINK_ON_MS) {
                    led_state = LED_BLINK_OFF;
                    state_change_ms = now;
                }
                break;

            default:
                led_state = LED_IDLE;
                break;
        }
        return;
    }

    // Rate limit updates (~50Hz)
    if (now - last_update_ms < 20) return;
    last_update_ms = now;
    tic++;

#ifdef BOARD_FEATHER_NRF52840
    // --- Feather: NeoPixel + blue LED ---
    if (has_override_color) {
        if (pat == 0) {
            uint8_t s = breathing_scale(tic);
            neo_set_color(
                (override_r * s) / 255,
                (override_g * s) / 255,
                (override_b * s) / 255);
            blue_led_set(s > 128);
        } else {
            neo_set_color(override_r, override_g, override_b);
            blue_led_set(true);
        }
        return;
    }

    if (pat == 0) {
        // No connection: breathing blue pulse
        uint8_t s = breathing_scale(tic);
        neo_set_color(0, 0, s);
        blue_led_set(s > 128);
    } else {
        // Connected: solid blue
        neo_set_color(0, 0, 64);
        blue_led_set(true);
    }

#else
    // --- XIAO: discrete LEDs with on/off blink ---
    if (has_override_color) {
        if (pat == 0) {
            bool phase = (tic % 50) < 25;
            if (phase) {
                set_color(override_r, override_g, override_b);
            } else {
                set_off();
            }
        } else {
            set_color(override_r, override_g, override_b);
        }
        return;
    }

    if (pat == 0) {
        bool phase = (tic % 50) < 25;
        set_rgb(false, false, phase);
    } else {
        set_rgb(false, false, true);
    }
#endif
}
