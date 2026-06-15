// amiga_device.c - Amiga/Atari/C64 DE9 output driver for JoypadOS
//
// Supports three platforms selected via BOOTSEL button:
//   Amiga    — CD32 7-button serial + 2-button joystick + quadrature mouse
//   C64      — 1-button joystick + quadrature mouse
//   Atari ST — 1-button joystick + Atari ST quadrature mouse
//
// CD32 implementation based on SukkoPera's OpenPSX2AmigaPadAdapter (GPL v3).
//
// Button events:
//   HOLD (1.5s)        — cycle platform (Amiga → C64 → Atari ST → Amiga)
//   DOUBLE_CLICK       — enter/exit DPI adjustment mode
//   In DPI mode: L mouse / gamepad B1 = decrease, R mouse / gamepad B2 = increase

#include "amiga_device.h"
#include "amiga_buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
// Minimal BOOTSEL button reader — avoids button service GP7 conflict
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"

static bool __no_inline_not_in_flash_func(read_bootsel_button)(void) {
    const uint CS_PIN_INDEX = 1;
    uint32_t flags = save_and_disable_interrupts();
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    2u << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,  // GPIO_OVERRIDE_LOW = 2
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 10000; ++i);
#if PICO_RP2350
    bool state = !(sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS);
#else
    bool state = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));
#if PICO_RP2350
    state = !(sio_hw->gpio_hi_in & SIO_GPIO_HI_IN_QSPI_CSN_BITS);
#endif
#endif
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    0u << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,  // GPIO_OVERRIDE_NORMAL = 0
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    restore_interrupts(flags);
    return state;
}
#include "core/services/storage/flash.h"
#include "core/services/leds/leds.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/timer.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// FLASH STORAGE LAYOUT (uses reserved[] bytes in flash_t)
// reserved[0] = platform (0=Amiga, 1=C64, 2=Atari ST)
// reserved[1] = DPI divider for Amiga
// reserved[2] = DPI divider for C64
// reserved[3] = DPI divider for Atari ST
// ============================================================================

#define FLASH_PLATFORM_IDX  0
#define FLASH_DPI_AMIGA_IDX 1
#define FLASH_DPI_C64_IDX   2
#define FLASH_DPI_ATST_IDX  3

// ============================================================================
// QUADRATURE TABLE
// ============================================================================

static const uint8_t QUAD_TABLE[4] = { 0b00, 0b01, 0b11, 0b10 };
#define QUAD_STEPS 4

// ============================================================================
// INTERNAL STATE
// ============================================================================

static volatile amiga_state_t amiga_state = {
    .buttons = 0,
    .mode    = AMIGA_MODE_JOYSTICK,
};

// Current platform
static amiga_platform_t current_platform = AMIGA_PLATFORM_AMIGA;

// DPI dividers per platform
static uint8_t dpi[AMIGA_PLATFORM_COUNT] = { DPI_DEFAULT, DPI_DEFAULT, DPI_DEFAULT };

// DPI adjustment mode
static bool dpi_adjust_mode = false;

// CD32 shift registers
static volatile uint8_t buttons_live = 0xFF;
static volatile uint8_t buttons_isr  = 0xFF;

// Mouse state
static volatile int16_t mouse_accum_x    = 0;
static volatile int16_t mouse_accum_y    = 0;
static volatile int16_t mouse_accum_wheel = 0;
static volatile uint32_t mouse_buttons   = 0;
static volatile bool mouse_active        = false;
static volatile bool device_connected    = false;  // track first connection for LED
static volatile bool gamepad_seen        = false;  // true once a real gamepad event arrives

// Quadrature state
static uint8_t quad_x = 0;
static uint8_t quad_y = 0;

// CD32 detection
static volatile bool cd32_detected       = false;
static volatile bool cd32_transfer_active = false;

// C1351 proportional mouse state (C64 platform only)
#define C1351_OFFSET        200
#define C1351_POS_CENTER    128
#define C1351_POS_MIN       63
#define C1351_POS_MAX       192

static volatile bool c1351_busy = false;  // true while alarm is pending
static volatile int16_t c1351_pos_x = C1351_POS_CENTER;
static volatile int16_t c1351_pos_y = C1351_POS_CENTER;
static volatile int16_t c1351_accum_x = 0;
static volatile int16_t c1351_accum_y = 0;
static int c1351_alarm_x = -1;
static int c1351_alarm_y = -1;

// Turbo state
#define TURBO_HZ            8
#define TURBO_BLINK_MS      3000
#define STICK_DEADZONE      80

static uint32_t turbo_mask        = 0;
static bool     turbo_state       = false;
static uint32_t turbo_blink_ms    = 0;
static uint8_t  turbo_blink_count = 0;
static repeating_timer_t turbo_timer;
static bool turbo_timer_running   = false;
static bool turbo_timer_cb(repeating_timer_t *rt);  // forward declaration

// ============================================================================
// FLASH HELPERS
// ============================================================================

static void load_settings(void) {
    flash_t* s = flash_get_settings();
    if (!s) return;

    uint8_t p = s->reserved[FLASH_PLATFORM_IDX];
    if (p < AMIGA_PLATFORM_COUNT) current_platform = (amiga_platform_t)p;

    for (int i = 0; i < AMIGA_PLATFORM_COUNT; i++) {
        uint8_t d = s->reserved[FLASH_DPI_AMIGA_IDX + i];
        dpi[i] = (d >= DPI_MIN && d <= DPI_MAX) ? d : DPI_DEFAULT;
    }

    printf("[amiga] Loaded — platform=%d dpi=%d/%d/%d\n",
           current_platform, dpi[0], dpi[1], dpi[2]);
}

static void save_settings(void) {
    flash_t* s = flash_get_settings();
    if (!s) return;

    s->reserved[FLASH_PLATFORM_IDX]  = (uint8_t)current_platform;
    s->reserved[FLASH_DPI_AMIGA_IDX] = dpi[AMIGA_PLATFORM_AMIGA];
    s->reserved[FLASH_DPI_C64_IDX]   = dpi[AMIGA_PLATFORM_C64];
    s->reserved[FLASH_DPI_ATST_IDX]  = dpi[AMIGA_PLATFORM_ATARI_ST];

    flash_save(s);
    printf("[amiga] Saved — platform=%d dpi=%d/%d/%d\n",
           current_platform, dpi[0], dpi[1], dpi[2]);
}

// ============================================================================
// LED HELPERS
// ============================================================================

static void update_led(void) {
    if (dpi_adjust_mode) {
        leds_set_color(LED_DPI_R, LED_DPI_G, LED_DPI_B);
        return;
    }
    switch (current_platform) {
        case AMIGA_PLATFORM_AMIGA:
            leds_set_color(LED_AMIGA_R, LED_AMIGA_G, LED_AMIGA_B);
            break;
        case AMIGA_PLATFORM_C64:
            leds_set_color(LED_C64_R, LED_C64_G, LED_C64_B);
            break;
        case AMIGA_PLATFORM_ATARI_ST:
            leds_set_color(LED_ATARI_ST_R, LED_ATARI_ST_G, LED_ATARI_ST_B);
            break;
        default:
            break;
    }
}

// ============================================================================
// HELPER: Build CD32 byte from button bitmap
// ============================================================================

// ============================================================================
// C1351 PROPORTIONAL MOUSE TIMING (C64 platform)
//
// Protocol: SID pulls POTX low every 512 C64 clocks (~256us).
// We detect this falling edge, immediately pull both POT pins low,
// then release each pin after (OFFSET + position) * 0.5us.
// SID measures the time from its release to our release = position.
//
// We use two hardware alarms: one per axis.
// Timer resolution: 1us (hardware timer), close enough to 0.5us ticks.
// ============================================================================

static volatile bool c1351_x_done = false;
static volatile bool c1351_y_done = false;

static void __not_in_flash_func(c1351_alarm_x_irq)(uint alarm_num) {
    (void)alarm_num;
    // alarm_x always releases JOYMODE (POTY = DE9 pin 5)
    gpio_set_dir(AMIGA_PIN_JOYMODE, GPIO_IN);
    gpio_pull_up(AMIGA_PIN_JOYMODE);
    c1351_x_done = true;
    if (c1351_y_done) { c1351_busy = false; c1351_x_done = false; c1351_y_done = false; }
}

static void __not_in_flash_func(c1351_alarm_y_irq)(uint alarm_num) {
    (void)alarm_num;
    // alarm_y always releases DATA (POTX = DE9 pin 9)
    gpio_set_dir(AMIGA_PIN_DATA, GPIO_IN);
    gpio_pull_up(AMIGA_PIN_DATA);
    c1351_y_done = true;
    if (c1351_x_done) { c1351_busy = false; c1351_x_done = false; c1351_y_done = false; }
}

static void __not_in_flash_func(c1351_release_all)(void) {
    // Emergency release — force both pins high and clear all state
    gpio_set_dir(AMIGA_PIN_JOYMODE, GPIO_IN);
    gpio_pull_up(AMIGA_PIN_JOYMODE);
    gpio_set_dir(AMIGA_PIN_DATA, GPIO_IN);
    gpio_pull_up(AMIGA_PIN_DATA);
    c1351_busy = false;
    c1351_x_done = false;
    c1351_y_done = false;
}

static void c1351_init_alarms(void) {
    if (c1351_alarm_x >= 0) return;  // already initialized
    c1351_alarm_x = hardware_alarm_claim_unused(false);
    c1351_alarm_y = hardware_alarm_claim_unused(false);
    if (c1351_alarm_x >= 0) hardware_alarm_set_callback(c1351_alarm_x, c1351_alarm_x_irq);
    if (c1351_alarm_y >= 0) hardware_alarm_set_callback(c1351_alarm_y, c1351_alarm_y_irq);
}

static void c1351_free_alarms(void) {
    if (c1351_alarm_x >= 0) { hardware_alarm_cancel(c1351_alarm_x); hardware_alarm_unclaim(c1351_alarm_x); c1351_alarm_x = -1; }
    if (c1351_alarm_y >= 0) { hardware_alarm_cancel(c1351_alarm_y); hardware_alarm_unclaim(c1351_alarm_y); c1351_alarm_y = -1; }
}

static void c1351_update_position(int8_t dx, int8_t dy) {
    // Accumulate deltas — ISR drains one unit per SID sample for smooth movement
    c1351_accum_x += dx;
    c1351_accum_y += dy;
}

static uint8_t __not_in_flash_func(build_cd32_byte)(uint32_t buttons) {
    uint8_t b = 0xFF;
    if (buttons & JP_BUTTON_B1) b &= ~(1 << CD32_BIT_RED);
    if (buttons & JP_BUTTON_B2) b &= ~(1 << CD32_BIT_BLUE);
    if (buttons & JP_BUTTON_B3) b &= ~(1 << CD32_BIT_GREEN);
    if (buttons & JP_BUTTON_B4) b &= ~(1 << CD32_BIT_YELLOW);
    if (buttons & JP_BUTTON_R1) b &= ~(1 << CD32_BIT_RFRONT);
    if (buttons & JP_BUTTON_L1) b &= ~(1 << CD32_BIT_LFRONT);
    if (buttons & JP_BUTTON_S2) b &= ~(1 << CD32_BIT_PAUSE);
    return b;
}

// ============================================================================
// GPIO HELPERS — open-collector style
// ============================================================================

// NOTE: BSS138 N-channel FET circuit on USB2AMI board inverts logic:
// GPIO HIGH → FET ON → DE9 line pulled LOW → signal asserted (pressed)
// GPIO LOW  → FET OFF → DE9 line HIGH (via Amiga pull-up) → released
static inline void __not_in_flash_func(pin_press)(uint pin) {
    gpio_put(pin, 0);
    gpio_set_dir(pin, GPIO_OUT);
}

static inline void __not_in_flash_func(pin_release)(uint pin) {
    gpio_put(pin, 1);
    gpio_set_dir(pin, GPIO_OUT);
}

static inline void __not_in_flash_func(set_dpad)(uint32_t buttons) {
    if (buttons & JP_BUTTON_DU) pin_press(AMIGA_PIN_UP);    else pin_release(AMIGA_PIN_UP);
    if (buttons & JP_BUTTON_DD) pin_press(AMIGA_PIN_DOWN);  else pin_release(AMIGA_PIN_DOWN);
    if (buttons & JP_BUTTON_DL) pin_press(AMIGA_PIN_LEFT);  else pin_release(AMIGA_PIN_LEFT);
    if (buttons & JP_BUTTON_DR) pin_press(AMIGA_PIN_RIGHT); else pin_release(AMIGA_PIN_RIGHT);
}

// ============================================================================
// QUADRATURE HELPERS
// Amiga/C64: H = RIGHT+DOWN, V = LEFT+UP
// Atari ST:  same pinout, different phase relationship
// ============================================================================

static inline void set_quad_x(uint8_t state) {
    if (state & 0x01) pin_press(AMIGA_PIN_RIGHT); else pin_release(AMIGA_PIN_RIGHT);
    if (state & 0x02) pin_press(AMIGA_PIN_DOWN);  else pin_release(AMIGA_PIN_DOWN);
}

static inline void set_quad_y(uint8_t state) {
    if (state & 0x01) pin_press(AMIGA_PIN_LEFT); else pin_release(AMIGA_PIN_LEFT);
    if (state & 0x02) pin_press(AMIGA_PIN_UP);   else pin_release(AMIGA_PIN_UP);
}

// Atari ST uses inverted phase
static inline void set_quad_x_st(uint8_t state) {
    if (state & 0x02) pin_press(AMIGA_PIN_RIGHT); else pin_release(AMIGA_PIN_RIGHT);
    if (state & 0x01) pin_press(AMIGA_PIN_DOWN);  else pin_release(AMIGA_PIN_DOWN);
}

static inline void set_quad_y_st(uint8_t state) {
    if (state & 0x02) pin_press(AMIGA_PIN_LEFT); else pin_release(AMIGA_PIN_LEFT);
    if (state & 0x01) pin_press(AMIGA_PIN_UP);   else pin_release(AMIGA_PIN_UP);
}

// ============================================================================
// JOYMODE + CLK GPIO INTERRUPT (Amiga CD32 only)
// ============================================================================

static volatile bool amiga_initialized = false;

static void __not_in_flash_func(amiga_gpio_irq)(uint gpio, uint32_t events) {
    if (!amiga_initialized) return;
    if (gpio == AMIGA_PIN_JOYMODE) {
        if (events & GPIO_IRQ_EDGE_FALL) {
            if (current_platform == AMIGA_PLATFORM_C64 && mouse_active) {
                // C1351 proportional mouse: SID is starting a POT measurement
                if (c1351_alarm_x < 0) c1351_init_alarms();  // lazy init
                c1351_busy = true;
                c1351_x_done = false;
                c1351_y_done = false;

                // Clamp accumulator to prevent overflow
                if (c1351_accum_x >  127) c1351_accum_x =  127;
                if (c1351_accum_x < -127) c1351_accum_x = -127;
                if (c1351_accum_y >  127) c1351_accum_y =  127;
                if (c1351_accum_y < -127) c1351_accum_y = -127;

                // Drain one unit from accumulator per SID sample
                if (c1351_accum_x > 0) { c1351_pos_x++; c1351_accum_x--; }
                else if (c1351_accum_x < 0) { c1351_pos_x--; c1351_accum_x++; }
                if (c1351_pos_x < C1351_POS_MIN) c1351_pos_x += 128;
                else if (c1351_pos_x > C1351_POS_MAX) c1351_pos_x -= 128;

                if (c1351_accum_y > 0) { c1351_pos_y++; c1351_accum_y--; }
                else if (c1351_accum_y < 0) { c1351_pos_y--; c1351_accum_y++; }
                if (c1351_pos_y < C1351_POS_MIN) c1351_pos_y += 128;
                else if (c1351_pos_y > C1351_POS_MAX) c1351_pos_y -= 128;

                // Pull both POT pins LOW immediately
                gpio_put(AMIGA_PIN_JOYMODE, 0); gpio_set_dir(AMIGA_PIN_JOYMODE, GPIO_OUT);
                gpio_put(AMIGA_PIN_DATA,    0); gpio_set_dir(AMIGA_PIN_DATA,    GPIO_OUT);

                // Schedule pin releases: each pin released at its own delay
                // JOYMODE = POTY (pos_y), DATA = POTX (pos_x)
                uint32_t delay_poty = C1351_OFFSET + (uint32_t)c1351_pos_y;
                uint32_t delay_potx = C1351_OFFSET + (uint32_t)c1351_pos_x;
                uint64_t now = time_us_64();
                // alarm_x fires at smaller delay, alarm_y at larger (clears busy)
                if (c1351_alarm_x >= 0) hardware_alarm_set_target(c1351_alarm_x, now + delay_poty);
                if (c1351_alarm_y >= 0) hardware_alarm_set_target(c1351_alarm_y, now + delay_potx);

            } else if (amiga_state.mode == AMIGA_MODE_JOYSTICK &&
                current_platform == AMIGA_PLATFORM_AMIGA &&
                !mouse_active) {
                // CD32 mode entry
                amiga_state.mode = AMIGA_MODE_CD32;
                cd32_transfer_active = true;
                // cd32_detected set on first CLK edge (confirms CD32 not C64 SID poll)
                gpio_set_dir(AMIGA_PIN_CLK, GPIO_IN);
                buttons_isr = buttons_live >> 1;
                gpio_set_dir(AMIGA_PIN_DATA, GPIO_OUT);
                gpio_put(AMIGA_PIN_DATA, (buttons_live & 0x01) ? 1 : 0);
                gpio_set_irq_enabled(AMIGA_PIN_CLK, GPIO_IRQ_EDGE_RISE, true);
            }
        } else if (events & GPIO_IRQ_EDGE_RISE) {
            cd32_transfer_active = false;
            if (amiga_state.mode == AMIGA_MODE_CD32) {
                gpio_set_irq_enabled(AMIGA_PIN_CLK, GPIO_IRQ_EDGE_RISE, false);
                if (amiga_state.buttons & JP_BUTTON_B1) pin_press(AMIGA_PIN_CLK);
                else pin_release(AMIGA_PIN_CLK);
                if (amiga_state.buttons & JP_BUTTON_B2) pin_press(AMIGA_PIN_DATA);
                else pin_release(AMIGA_PIN_DATA);
                amiga_state.mode = AMIGA_MODE_JOYSTICK;
            }
        }
    } else if (gpio == AMIGA_PIN_CLK) {
        cd32_detected = true;  // confirmed CD32 console (not C64 SID poll)
        if (buttons_isr & 0x01) gpio_put(AMIGA_PIN_DATA, 1);
        else                    gpio_put(AMIGA_PIN_DATA, 0);
        buttons_isr >>= 1;
    }
}

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

// ============================================================================
// CORE 1 TASK
// ============================================================================

void __not_in_flash_func(amiga_core1_task)(void) {
#define QUAD_STEP_US 200
    while (true) {
        if (!mouse_active || amiga_state.mode != AMIGA_MODE_JOYSTICK ||
                current_platform == AMIGA_PLATFORM_C64 ||
                (mouse_accum_x == 0 && mouse_accum_y == 0)) {
            busy_wait_us(QUAD_STEP_US);
            continue;
        }
        bool moved = false;
        if (mouse_accum_x > 0) { quad_x = (quad_x + QUAD_STEPS - 1) % QUAD_STEPS; mouse_accum_x--; moved = true; }
        else if (mouse_accum_x < 0) { quad_x = (quad_x + 1) % QUAD_STEPS; mouse_accum_x++; moved = true; }
        if (mouse_accum_y > 0) { quad_y = (quad_y + QUAD_STEPS - 1) % QUAD_STEPS; mouse_accum_y--; moved = true; }
        else if (mouse_accum_y < 0) { quad_y = (quad_y + 1) % QUAD_STEPS; mouse_accum_y++; moved = true; }
        if (moved) {
            if (current_platform == AMIGA_PLATFORM_ATARI_ST) {
                set_quad_x_st(QUAD_TABLE[quad_x]);
                set_quad_y_st(QUAD_TABLE[quad_y]);
            } else {
                set_quad_x(QUAD_TABLE[quad_x]);
                set_quad_y(QUAD_TABLE[quad_y]);
            }
        }
        busy_wait_us(QUAD_STEP_US);
    }
}

// ============================================================================
// INPUT EVENT TAP
// ============================================================================

static void __not_in_flash_func(amiga_tap_callback)(output_target_t output,
                                                      uint8_t player_index,
                                                      const input_event_t* event)
{
    (void)output;
    (void)player_index;

    // Handle disconnect event
    if (event->type == INPUT_TYPE_NONE) {
        device_connected = false;
        mouse_active = false;
        gamepad_seen = false;
        dpi_adjust_mode = false;
        cd32_detected = false;
        turbo_mask = 0;
        // Re-enable JOYMODE IRQ on disconnect so next connect detects correctly
        gpio_set_irq_enabled(AMIGA_PIN_JOYMODE, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
        if (turbo_timer_running) {
            cancel_repeating_timer(&turbo_timer);
            turbo_timer_running = false;
        }
        leds_set_color(0, 0, 0);
        return;
    }

    if (event->type == INPUT_TYPE_MOUSE) {
        // Distinguish real mouse from gamepads misclassified as mice by framework
        // Real mice: have delta_x/delta_y movement, buttons are mouse buttons
        // Misclassified gamepads: have JP_BUTTON_DU/DD/DL/DR dpad bits set
        bool has_dpad = (event->buttons & (JP_BUTTON_DU | JP_BUTTON_DD |
                                           JP_BUTTON_DL | JP_BUTTON_DR)) != 0;
        if (has_dpad) {
            // This is a gamepad masquerading as a mouse — treat as gamepad
            gamepad_seen = true;
            goto handle_as_gamepad;
        }
        if (gamepad_seen) return;  // gamepad was seen first this session

        // Set LED on first connection or when switching from gamepad
        if (!mouse_active || !device_connected) {
            device_connected = true;
            turbo_mask = 0;
            update_led();
            // Re-enable JOYMODE IRQ for C1351 mouse mode
            if (current_platform == AMIGA_PLATFORM_C64) {
                gpio_set_irq_enabled(AMIGA_PIN_JOYMODE, GPIO_IRQ_EDGE_FALL, true);
            }
        }
        mouse_active = true;

        // Middle mouse button (S2) — hold 2s to enter DPI mode, tap to exit
        static uint32_t mmb_press_start = 0;
        static bool mmb_was_pressed = false;
        bool mmb_pressed = (event->buttons & JP_BUTTON_S2) != 0;

        if (mmb_pressed && !mmb_was_pressed) {
            mmb_press_start = to_ms_since_boot(get_absolute_time());
            mmb_was_pressed = true;
        } else if (!mmb_pressed && mmb_was_pressed) {
            uint32_t held = to_ms_since_boot(get_absolute_time()) - mmb_press_start;
            mmb_was_pressed = false;
            if (dpi_adjust_mode) {
                // Any MMB release exits DPI mode
                dpi_adjust_mode = false;
                update_led();
                save_settings();
                printf("[amiga] DPI adjust: OFF (DPI=%d saved)\n", dpi[current_platform]);
            } else if (held >= 2000) {
                // Hold 2s to enter DPI mode
                dpi_adjust_mode = true;
                update_led();
                printf("[amiga] DPI adjust: ON (DPI=%d)\n", dpi[current_platform]);
            }
        }

        // In DPI adjust mode, L/R mouse buttons change DPI
        if (dpi_adjust_mode) {
            static uint32_t last_dpi_change = 0;
            uint32_t now = to_ms_since_boot(get_absolute_time());
            // Debounce DPI changes — 300ms between changes
            if (now - last_dpi_change >= 300) {
                if (event->buttons & JP_BUTTON_B1) {
                    if (dpi[current_platform] > DPI_MIN) {
                        dpi[current_platform]--;
                        last_dpi_change = now;
                        printf("[amiga] DPI: %d\n", dpi[current_platform]);
                    }
                } else if (event->buttons & JP_BUTTON_B2) {
                    if (dpi[current_platform] < DPI_MAX) {
                        dpi[current_platform]++;
                        last_dpi_change = now;
                        printf("[amiga] DPI: %d\n", dpi[current_platform]);
                    }
                }
            }
            // Allow mouse movement in DPI mode so user can test changes in real time
            // Fall through to delta accumulation below
        }

        // Normal mouse operation — accumulate deltas with DPI divider
        uint8_t d = dpi[current_platform];
        if (current_platform == AMIGA_PLATFORM_C64) {
            // C1351 proportional mode — update absolute position
            c1351_update_position(event->delta_x / d, -(event->delta_y / d));
        } else {
            // Amiga/Atari ST — quadrature accumulation (Core 1 drains)
            mouse_accum_x += event->delta_x / d;
            mouse_accum_y += event->delta_y / d;
        }
        mouse_accum_wheel += event->delta_wheel;
        mouse_buttons = event->buttons;

    } else {
        handle_as_gamepad:
        // Set LED on first connection or when switching from mouse
        if (mouse_active || !device_connected) {
            device_connected = true;
            update_led();
            // Re-enable JOYMODE IRQ if switching from mouse (it was disabled for gamepad)
            if (current_platform == AMIGA_PLATFORM_C64 && mouse_active) {
                // was in mouse mode, now gamepad — JOYMODE IRQ stays disabled
            }
        }
        mouse_active = false;
        gamepad_seen = true;
        // Disable JOYMODE IRQ in C64 gamepad mode — SID polling interferes with USB enumeration
        if (current_platform == AMIGA_PLATFORM_C64) {
            gpio_set_irq_enabled(AMIGA_PIN_JOYMODE, GPIO_IRQ_EDGE_FALL, false);
        }

        // Exit DPI adjust mode if mouse disconnected
        if (dpi_adjust_mode) {
            dpi_adjust_mode = false;
            update_led();
        }

        uint32_t buttons = event->buttons;
        uint32_t physical_buttons = buttons;

        // Select + face button = turbo toggle
        static bool select_was_held = false;
        static uint32_t select_combo_handled = 0;
        if (buttons & JP_BUTTON_S1) {
            const uint32_t turbo_buttons[] = { JP_BUTTON_B1, JP_BUTTON_B2, JP_BUTTON_B3, JP_BUTTON_B4 };
            int max_turbo = (current_platform == AMIGA_PLATFORM_AMIGA) ? 4 : 1;
            for (int i = 0; i < max_turbo; i++) {
                uint32_t b = turbo_buttons[i];
                if ((buttons & b) && !(select_combo_handled & b)) {
                    turbo_mask ^= b;
                    select_combo_handled |= b;
                    turbo_blink_ms = to_ms_since_boot(get_absolute_time());
                    turbo_blink_count = 0;
                    // Start timer when first turbo button enabled, cancel when all disabled
                    if (turbo_mask != 0 && !turbo_timer_running) {
                        add_repeating_timer_us(-(1000000 / TURBO_HZ / 2), turbo_timer_cb, NULL, &turbo_timer);
                        turbo_timer_running = true;
                    } else if (turbo_mask == 0 && turbo_timer_running) {
                        cancel_repeating_timer(&turbo_timer);
                        turbo_timer_running = false;
                    }
                }
            }
            select_was_held = true;
            buttons &= ~JP_BUTTON_S1;
            buttons &= ~select_combo_handled;
            physical_buttons = buttons;
        } else {
            if (select_was_held) { select_combo_handled = 0; select_was_held = false; }
        }

        // CD32 analog stick mappings (only once CD32 console detected)
        if (current_platform == AMIGA_PLATFORM_AMIGA && cd32_detected) {
            uint8_t lx = event->analog[ANALOG_LX];
            uint8_t ly = event->analog[ANALOG_LY];
            if (ly < (128 - STICK_DEADZONE)) buttons |= JP_BUTTON_DU;
            if (ly > (128 + STICK_DEADZONE)) buttons |= JP_BUTTON_DD;
            if (lx < (128 - STICK_DEADZONE)) buttons |= JP_BUTTON_DL;
            if (lx > (128 + STICK_DEADZONE)) buttons |= JP_BUTTON_DR;

            uint8_t rx = event->analog[ANALOG_RX];
            uint8_t ry = event->analog[ANALOG_RY];
            if (ry < (128 - STICK_DEADZONE)) buttons |= JP_BUTTON_B4;  // Up    = Green
            if (rx > (128 + STICK_DEADZONE)) buttons |= JP_BUTTON_B2;  // Right = Blue
            if (ry > (128 + STICK_DEADZONE)) buttons |= JP_BUTTON_B1;  // Down  = Red
            if (rx < (128 - STICK_DEADZONE)) buttons |= JP_BUTTON_B3;  // Left  = Yellow
        }

        amiga_state.buttons = physical_buttons;
        set_dpad(buttons);

        // Build CD32 byte — turbo buttons handled by timer callback
        uint32_t effective_buttons = buttons;
        if (turbo_mask != 0) {
            uint32_t turbo_held = turbo_mask & physical_buttons;
            if (turbo_state) effective_buttons |=  turbo_held;
            else             effective_buttons &= ~turbo_held;
        }
        buttons_live = build_cd32_byte(effective_buttons);

        if (amiga_state.mode == AMIGA_MODE_JOYSTICK) {
            // Fire1 — skip if turbo active (timer handles it)
            if (!(turbo_mask & JP_BUTTON_B1)) {
                if (buttons & JP_BUTTON_B1) pin_press(AMIGA_PIN_CLK);
                else                        pin_release(AMIGA_PIN_CLK);
            }
            // Fire2 — Amiga only, skip if turbo active
            if (current_platform == AMIGA_PLATFORM_AMIGA) {
                if (!(turbo_mask & JP_BUTTON_B2)) {
                    if (buttons & JP_BUTTON_B2) pin_press(AMIGA_PIN_DATA);
                    else                        pin_release(AMIGA_PIN_DATA);
                }
            }
        }
    }
}

// ============================================================================
// TURBO TIMER CALLBACK
// ============================================================================

static bool __not_in_flash_func(turbo_timer_cb)(repeating_timer_t *rt) {
    (void)rt;
    if (turbo_mask == 0 || mouse_active || cd32_transfer_active) return true;

    turbo_state = !turbo_state;

    // Rebuild CD32 byte for all turbo buttons
    uint32_t turbo_buttons = amiga_state.buttons;
    uint32_t turbo_held = turbo_mask & amiga_state.buttons;
    if (turbo_state) turbo_buttons |=  turbo_held;
    else             turbo_buttons &= ~turbo_held;
    buttons_live = build_cd32_byte(turbo_buttons);

    // Drive CLK/DATA pins for B1/B2 in joystick mode
    if (amiga_state.mode == AMIGA_MODE_JOYSTICK) {
        if (turbo_mask & JP_BUTTON_B1) {
            if (amiga_state.buttons & JP_BUTTON_B1) {
                if (turbo_state) pin_press(AMIGA_PIN_CLK);
                else             pin_release(AMIGA_PIN_CLK);
            } else {
                pin_release(AMIGA_PIN_CLK);
            }
        }
        if (current_platform == AMIGA_PLATFORM_AMIGA && (turbo_mask & JP_BUTTON_B2)) {
            if (amiga_state.buttons & JP_BUTTON_B2) {
                if (turbo_state) pin_press(AMIGA_PIN_DATA);
                else             pin_release(AMIGA_PIN_DATA);
            } else {
                pin_release(AMIGA_PIN_DATA);
            }
        }
    }
    return true;
}

// ============================================================================
// DEVICE TASK
// ============================================================================

void amiga_device_task(void) {
    // Load settings on first task call — storage_init() has run by then
    static bool settings_loaded = false;
    if (!settings_loaded) {
        settings_loaded = true;
        load_settings();
        // Don't call update_led() here — let framework breathing animation run
        // at full range until first controller connects
    }

    // BOOTSEL button handling — only during first 10 seconds after power-on
    // After that, QSPI manipulation interferes with CD32 CLK timing
    {
#define BOOTSEL_WINDOW_MS 5000
        static uint32_t last_button_read = 0;
        static bool button_was_pressed = false;
        static bool window_open = true;

        uint32_t now = to_ms_since_boot(get_absolute_time());

        if (window_open) {
            if (now >= BOOTSEL_WINDOW_MS || cd32_detected) {
                window_open = false;
            } else if (now - last_button_read >= 50) {
                last_button_read = now;
                gpio_set_irq_enabled(AMIGA_PIN_JOYMODE, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, false);
                bool pressed = read_bootsel_button();
                gpio_set_irq_enabled(AMIGA_PIN_JOYMODE, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);

                if (pressed && !button_was_pressed) {
                    button_was_pressed = true;
                } else if (!pressed && button_was_pressed) {
                    button_was_pressed = false;
                    // Tap to cycle platform — no hold required
                    current_platform = (amiga_platform_t)((current_platform + 1) % AMIGA_PLATFORM_COUNT);
                    turbo_mask = 0;
                    cd32_detected = false;
                    c1351_free_alarms();
                    save_settings();
                    update_led();
                    printf("[amiga] Platform: %d\n", current_platform);
                }
            }
        }
    }

    // Mouse handling
    if (amiga_state.mode == AMIGA_MODE_JOYSTICK && mouse_active && !dpi_adjust_mode) {

        // Apply mouse buttons continuously
        if (mouse_buttons & JP_BUTTON_B1) pin_press(AMIGA_PIN_CLK);
        else                              pin_release(AMIGA_PIN_CLK);

        if (current_platform == AMIGA_PLATFORM_C64) {
            // C64: right button = UP pin (DE9 pin 1), DATA pin is POTY — don't touch it
            if (mouse_buttons & JP_BUTTON_B2) pin_press(AMIGA_PIN_UP);
            else                              pin_release(AMIGA_PIN_UP);
        } else {
            if (mouse_buttons & JP_BUTTON_B2) pin_press(AMIGA_PIN_DATA);
            else                              pin_release(AMIGA_PIN_DATA);
        }

        // WheelBusMouse scroll protocol (Amiga only)
        if (current_platform == AMIGA_PLATFORM_AMIGA && mouse_accum_wheel != 0) {
            int16_t steps = mouse_accum_wheel;
            mouse_accum_wheel = 0;

            gpio_set_irq_enabled(AMIGA_PIN_JOYMODE,
                GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, false);

            pin_press(AMIGA_PIN_JOYMODE);
            busy_wait_us(50);

            for (int i = 0; i < (steps < 0 ? -steps : steps); i++) {
                if (steps > 0) quad_y = (quad_y + QUAD_STEPS - 1) % QUAD_STEPS;
                else           quad_y = (quad_y + 1) % QUAD_STEPS;
                set_quad_y(QUAD_TABLE[quad_y]);
                busy_wait_us(10);
            }

            busy_wait_us(50);
            pin_release(AMIGA_PIN_JOYMODE);
            busy_wait_us(50);

            gpio_set_irq_enabled(AMIGA_PIN_JOYMODE,
                GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
        }
    }

    // C1351 watchdog — if busy flag stuck for >2ms, force-release pins
    {
        static uint32_t c1351_busy_since = 0;
        if (c1351_busy && current_platform == AMIGA_PLATFORM_C64) {
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());
            if (c1351_busy_since == 0) c1351_busy_since = now_ms;
            else if (now_ms - c1351_busy_since > 2) {
                c1351_release_all();
                c1351_busy_since = 0;
            }
        } else {
            c1351_busy_since = 0;
        }
    }

    // Turbo LED blink — double-blink every TURBO_BLINK_MS when turbo active
    if (turbo_mask != 0 && !mouse_active && !dpi_adjust_mode) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (turbo_blink_count == 0 && (now - turbo_blink_ms) >= TURBO_BLINK_MS) {
            turbo_blink_ms = now;
            turbo_blink_count = 4;
        }
        if (turbo_blink_count > 0) {
            static uint32_t last_blink_step = 0;
            if ((now - last_blink_step) >= 80) {
                last_blink_step = now;
                turbo_blink_count--;
                if (turbo_blink_count % 2 == 1) leds_set_color(0, 0, 0);
                else                             update_led();
            }
        }
    }
}

// ============================================================================
// DEVICE INIT
// ============================================================================

void amiga_device_init(void) {
    // All DE9 signal pins start as outputs driven LOW (FET off = line released)
    // BSS138 circuit: GPIO HIGH = FET ON = line asserted, GPIO LOW = FET OFF = released
    gpio_init(AMIGA_PIN_UP);    gpio_put(AMIGA_PIN_UP,    1); gpio_set_dir(AMIGA_PIN_UP,    GPIO_OUT);
    gpio_init(AMIGA_PIN_DOWN);  gpio_put(AMIGA_PIN_DOWN,  1); gpio_set_dir(AMIGA_PIN_DOWN,  GPIO_OUT);
    gpio_init(AMIGA_PIN_LEFT);  gpio_put(AMIGA_PIN_LEFT,  1); gpio_set_dir(AMIGA_PIN_LEFT,  GPIO_OUT);
    gpio_init(AMIGA_PIN_RIGHT); gpio_put(AMIGA_PIN_RIGHT, 1); gpio_set_dir(AMIGA_PIN_RIGHT, GPIO_OUT);
    gpio_init(AMIGA_PIN_CLK);   gpio_put(AMIGA_PIN_CLK,   1); gpio_set_dir(AMIGA_PIN_CLK,   GPIO_OUT);
    gpio_init(AMIGA_PIN_DATA);  gpio_put(AMIGA_PIN_DATA,  1); gpio_set_dir(AMIGA_PIN_DATA,  GPIO_OUT);

    // JOYMODE — input with pull-up, interrupt on both edges
    gpio_init(AMIGA_PIN_JOYMODE);
    gpio_set_dir(AMIGA_PIN_JOYMODE, GPIO_IN);
    gpio_pull_up(AMIGA_PIN_JOYMODE);
    gpio_set_irq_enabled_with_callback(AMIGA_PIN_JOYMODE,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, amiga_gpio_irq);
    gpio_set_irq_enabled(AMIGA_PIN_CLK, GPIO_IRQ_EDGE_RISE, false);

    router_set_tap(OUTPUT_TARGET_AMIGA, amiga_tap_callback);

    amiga_state.mode = AMIGA_MODE_JOYSTICK;

    amiga_initialized = true;

    printf("[amiga] Init complete\n");
}

// ============================================================================
// PUBLIC API
// ============================================================================

amiga_platform_t amiga_get_platform(void) { return current_platform; }
uint8_t amiga_get_dpi(void) { return dpi[current_platform]; }

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

const OutputInterface amiga_output_interface = {
    .name           = "Amiga/CD32",
    .init           = amiga_device_init,
    .core1_task     = amiga_core1_task,
    .task           = amiga_device_task,
    .get_rumble     = NULL,
    .get_player_led = NULL,
};
