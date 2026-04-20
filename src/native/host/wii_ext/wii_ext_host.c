// wii_ext_host.c - Native Wii extension controller host driver (HW I2C transport)

#include "wii_ext_host.h"
#include "lib/wii_ext/wii_ext.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "core/services/leds/leds.h"
#include "core/services/profiles/profile.h"
#include "platform/platform_i2c.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

// Device address range for native inputs. SNES/LodgeNet use 0xF0+, N64/3DO
// 0xE0+, GC/UART 0xD0+. Wii claims 0xC0+ per the plan.
#define WII_DEV_ADDR_BASE       0xC0

// Poll / retry cadence.
#define WII_POLL_INTERVAL_US    2000      // ~500 Hz when connected
#define WII_RETRY_INTERVAL_US   250000    // 250 ms when hunting for a slave

// ---- State ------------------------------------------------------------------

// Per-port state for up to 2 I2C buses.
typedef struct {
    bool             initialized;
    uint8_t          pin_sda;
    uint8_t          pin_scl;
    platform_i2c_t   bus;
    wii_ext_t        ext;
    wii_ext_transport_t transport;

    uint32_t         last_poll_us;
    uint32_t         last_retry_us;
    bool             prev_connected;
} wii_port_t;

#define WII_MAX_PORTS 2

static wii_port_t ports[WII_MAX_PORTS];
static uint8_t    num_ports = 0;

// Merged output state (tracks change detection for the combined event).
static uint32_t         prev_buttons = 0;
static uint64_t         prev_analog  = 0;

// Profile-cycle hotkey state: MINUS + DU/DD held ≥ 2s triggers cycle.
#define WII_HOTKEY_HOLD_US   2000000
static uint32_t         hotkey_combo_start_us = 0;
static uint32_t         hotkey_combo_mask     = 0;  // which combo is being tracked
static bool             hotkey_fired          = false;

// LED scan-blink state (toggles each retry while hunting for a slave).
static bool             led_scan_state        = false;

// Per-accessory LED colors (dim so the status LED doesn't overpower the
// player-index indication layered on top by core/services/leds).
#define LED_WII_NUNCHUCK_R    0
#define LED_WII_NUNCHUCK_G   16
#define LED_WII_NUNCHUCK_B   24
#define LED_WII_CLASSIC_R     0
#define LED_WII_CLASSIC_G     0
#define LED_WII_CLASSIC_B    40

// ---- Auto-calibrating stick range -------------------------------------------
// Nunchuck / Classic sticks rarely reach full 0-255. Track min/max per axis
// per port and scale output to full range. Center (128) is preserved.

#define WII_STICK_INIT_MIN  40   // Conservative initial min (widens on use)
#define WII_STICK_INIT_MAX  215  // Conservative initial max (widens on use)
#define WII_STICK_DEADZONE  3    // Ignore values within ±3 of center

static struct {
    uint8_t min;
    uint8_t max;
} wii_stick_range[WII_MAX_PORTS][4];  // [port][0=LX, 1=LY, 2=RX, 3=RY]
static bool wii_stick_range_init = false;

static void wii_stick_range_reset(uint8_t port) {
    for (int a = 0; a < 4; a++) {
        wii_stick_range[port][a].min = WII_STICK_INIT_MIN;
        wii_stick_range[port][a].max = WII_STICK_INIT_MAX;
    }
    wii_stick_range_init = true;
}

static uint8_t wii_stick_scale(uint8_t raw, uint8_t port, uint8_t axis) {
    if (raw < 128 - WII_STICK_DEADZONE || raw > 128 + WII_STICK_DEADZONE) {
        if (raw < wii_stick_range[port][axis].min) wii_stick_range[port][axis].min = raw;
        if (raw > wii_stick_range[port][axis].max) wii_stick_range[port][axis].max = raw;
    }

    uint8_t lo = wii_stick_range[port][axis].min;
    uint8_t hi = wii_stick_range[port][axis].max;

    if (hi <= lo || hi - lo < 20) return raw;

    if (raw <= lo) return 0;
    if (raw >= hi) return 255;
    return (uint8_t)(((uint16_t)(raw - lo) * 255) / (hi - lo));
}

// ---- wii_ext transport vtable (thin wrappers over platform_i2c) -------------

static int io_write(void *ctx, uint8_t addr, const uint8_t *data, uint16_t len) {
    return platform_i2c_write((platform_i2c_t)ctx, addr, data, len);
}
static int io_read(void *ctx, uint8_t addr, uint8_t *data, uint16_t len) {
    return platform_i2c_read((platform_i2c_t)ctx, addr, data, len);
}
static void io_delay(uint32_t us) {
    busy_wait_us(us);
}

// ---- Event mapping ----------------------------------------------------------

static void map_nunchuck(const wii_ext_state_t *s, input_event_t *ev) {
    if (s->buttons & WII_BTN_Z) ev->buttons |= JP_BUTTON_B1;
    if (s->buttons & WII_BTN_C) ev->buttons |= JP_BUTTON_B2;

    uint8_t raw_x = (uint8_t)(s->analog[WII_AXIS_LX] >> 2);
    uint8_t raw_y = (uint8_t)(255 - (s->analog[WII_AXIS_LY] >> 2));
    ev->analog[ANALOG_LX] = wii_stick_scale(raw_x, 0, 0);
    ev->analog[ANALOG_LY] = wii_stick_scale(raw_y, 0, 1);
    ev->analog[ANALOG_RX] = 128;
    ev->analog[ANALOG_RY] = 128;
    ev->analog[ANALOG_L2] = 0;
    ev->analog[ANALOG_R2] = 0;

    ev->layout = LAYOUT_WII_NUNCHUCK;
    ev->button_count = 2;

    if (s->has_accel) {
        ev->accel[0] = s->accel[0];
        ev->accel[1] = s->accel[1];
        ev->accel[2] = s->accel[2];
        ev->has_motion = true;
        ev->accel_range = 2000;
    }
}

// Guitar (GH3 + GHWT): frets on face buttons, strum on d-pad, whammy on RT.
static void map_guitar(const wii_ext_state_t *s, input_event_t *ev) {
    if (s->buttons & WII_BTN_GH_GREEN)      ev->buttons |= JP_BUTTON_B1;
    if (s->buttons & WII_BTN_GH_RED)        ev->buttons |= JP_BUTTON_B2;
    if (s->buttons & WII_BTN_GH_YELLOW)     ev->buttons |= JP_BUTTON_B3;
    if (s->buttons & WII_BTN_GH_BLUE)       ev->buttons |= JP_BUTTON_B4;
    if (s->buttons & WII_BTN_GH_ORANGE)     ev->buttons |= JP_BUTTON_L1;
    if (s->buttons & WII_BTN_GH_STRUM_UP)   ev->buttons |= JP_BUTTON_DU;
    if (s->buttons & WII_BTN_GH_STRUM_DOWN) ev->buttons |= JP_BUTTON_DD;
    if (s->buttons & WII_BTN_MINUS)         ev->buttons |= JP_BUTTON_S1;
    if (s->buttons & WII_BTN_PLUS)          ev->buttons |= JP_BUTTON_S2;

    ev->analog[ANALOG_LX] = (uint8_t)(s->analog[WII_AXIS_LX] >> 2);
    ev->analog[ANALOG_LY] = (uint8_t)(255 - (s->analog[WII_AXIS_LY] >> 2));
    ev->analog[ANALOG_RX] = 128;
    ev->analog[ANALOG_RY] = 128;
    ev->analog[ANALOG_L2] = 0;
    ev->analog[ANALOG_R2] = (uint8_t)(s->analog[WII_AXIS_RT] >> 2);  // whammy

    ev->layout = LAYOUT_WII_GUITAR;
    ev->button_count = 5;
}

// Drums: pads on face/shoulder buttons, velocity in event->pressure[].
static void map_drums(const wii_ext_state_t *s, input_event_t *ev) {
    if (s->buttons & WII_BTN_DRUM_RED)    ev->buttons |= JP_BUTTON_B1;
    if (s->buttons & WII_BTN_DRUM_YELLOW) ev->buttons |= JP_BUTTON_B3;
    if (s->buttons & WII_BTN_DRUM_BLUE)   ev->buttons |= JP_BUTTON_B4;
    if (s->buttons & WII_BTN_DRUM_GREEN)  ev->buttons |= JP_BUTTON_B2;
    if (s->buttons & WII_BTN_DRUM_ORANGE) ev->buttons |= JP_BUTTON_L1;
    if (s->buttons & WII_BTN_DRUM_BASS)   ev->buttons |= JP_BUTTON_R1;
    if (s->buttons & WII_BTN_MINUS)       ev->buttons |= JP_BUTTON_S1;
    if (s->buttons & WII_BTN_PLUS)        ev->buttons |= JP_BUTTON_S2;

    // Velocity → pressure[] — same slot layout as DualShock 3.
    // DS3 order: up, right, down, left, l2, r2, l1, r1, triangle, circle, cross, square
    // We map drum pads onto that approximate order via the face/shoulder
    // indices — PS3 mode consumers see pressure per-button.
    ev->pressure[10] = s->pad_velocity[WII_DRUM_PAD_RED];    // cross
    ev->pressure[8]  = s->pad_velocity[WII_DRUM_PAD_YELLOW]; // triangle
    ev->pressure[9]  = s->pad_velocity[WII_DRUM_PAD_BLUE];   // circle
    ev->pressure[11] = s->pad_velocity[WII_DRUM_PAD_GREEN];  // square
    ev->pressure[6]  = s->pad_velocity[WII_DRUM_PAD_ORANGE]; // l1 (cymbal)
    ev->pressure[7]  = s->pad_velocity[WII_DRUM_PAD_BASS];   // r1
    ev->has_pressure = true;

    ev->analog[ANALOG_LX] = 128;
    ev->analog[ANALOG_LY] = 128;
    ev->analog[ANALOG_RX] = 128;
    ev->analog[ANALOG_RY] = 128;
    ev->analog[ANALOG_L2] = 0;
    ev->analog[ANALOG_R2] = 0;

    ev->layout = LAYOUT_WII_DRUMS;
    ev->button_count = 6;
}

// DJ Hero turntable: rotations on sticks, crossfader on L2, effects on R2.
static void map_turntable(const wii_ext_state_t *s, input_event_t *ev) {
    if (s->buttons & WII_BTN_DJ_LEFT_GREEN)  ev->buttons |= JP_BUTTON_B1;
    if (s->buttons & WII_BTN_DJ_LEFT_RED)    ev->buttons |= JP_BUTTON_B2;
    if (s->buttons & WII_BTN_DJ_LEFT_BLUE)   ev->buttons |= JP_BUTTON_B3;
    if (s->buttons & WII_BTN_DJ_RIGHT_GREEN) ev->buttons |= JP_BUTTON_B4;
    if (s->buttons & WII_BTN_DJ_RIGHT_RED)   ev->buttons |= JP_BUTTON_L1;
    if (s->buttons & WII_BTN_DJ_RIGHT_BLUE)  ev->buttons |= JP_BUTTON_R1;
    if (s->buttons & WII_BTN_DJ_EUPHORIA)    ev->buttons |= JP_BUTTON_A1;
    if (s->buttons & WII_BTN_MINUS)          ev->buttons |= JP_BUTTON_S1;
    if (s->buttons & WII_BTN_PLUS)           ev->buttons |= JP_BUTTON_S2;

    ev->analog[ANALOG_LX] = (uint8_t)(s->analog[WII_AXIS_LX] >> 2);
    ev->analog[ANALOG_LY] = 128;
    ev->analog[ANALOG_RX] = (uint8_t)(s->analog[WII_AXIS_RX] >> 2);
    ev->analog[ANALOG_RY] = 128;
    ev->analog[ANALOG_L2] = (uint8_t)(s->analog[WII_AXIS_LT] >> 2);  // crossfader
    ev->analog[ANALOG_R2] = (uint8_t)(s->analog[WII_AXIS_RT] >> 2);  // effects dial

    ev->layout = LAYOUT_WII_TURNTABLE;
    ev->button_count = 6;
}

// Taiko: drum surfaces on face buttons.
static void map_taiko(const wii_ext_state_t *s, input_event_t *ev) {
    if (s->buttons & WII_BTN_TAIKO_L_FACE) ev->buttons |= JP_BUTTON_B1;
    if (s->buttons & WII_BTN_TAIKO_R_FACE) ev->buttons |= JP_BUTTON_B2;
    if (s->buttons & WII_BTN_TAIKO_L_RIM)  ev->buttons |= JP_BUTTON_B3;
    if (s->buttons & WII_BTN_TAIKO_R_RIM)  ev->buttons |= JP_BUTTON_B4;
    if (s->buttons & WII_BTN_MINUS)        ev->buttons |= JP_BUTTON_S1;
    if (s->buttons & WII_BTN_PLUS)         ev->buttons |= JP_BUTTON_S2;

    ev->analog[ANALOG_LX] = 128;
    ev->analog[ANALOG_LY] = 128;
    ev->analog[ANALOG_RX] = 128;
    ev->analog[ANALOG_RY] = 128;
    ev->analog[ANALOG_L2] = 0;
    ev->analog[ANALOG_R2] = 0;

    ev->layout = LAYOUT_WII_TAIKO;
    ev->button_count = 4;
}

// uDraw tablet: absolute tablet position surfaces via touch[0].
static void map_udraw(const wii_ext_state_t *s, input_event_t *ev) {
    if (s->buttons & WII_BTN_A) ev->buttons |= JP_BUTTON_B1;
    if (s->buttons & WII_BTN_B) ev->buttons |= JP_BUTTON_B2;
    if (s->buttons & WII_BTN_MINUS) ev->buttons |= JP_BUTTON_S1;
    if (s->buttons & WII_BTN_PLUS)  ev->buttons |= JP_BUTTON_S2;

    ev->analog[ANALOG_LX] = (uint8_t)(s->analog[WII_AXIS_LX] >> 2);
    ev->analog[ANALOG_LY] = (uint8_t)(s->analog[WII_AXIS_LY] >> 2);
    ev->analog[ANALOG_RX] = 128;
    ev->analog[ANALOG_RY] = 128;
    ev->analog[ANALOG_L2] = (uint8_t)(s->analog[WII_AXIS_LT] >> 2);
    ev->analog[ANALOG_R2] = 0;

    ev->touch[0].x = s->tablet_x;
    ev->touch[0].y = s->tablet_y;
    ev->touch[0].active = s->tablet_active;
    ev->has_touch = s->tablet_active;

    ev->layout = LAYOUT_WII_UDRAW;
    ev->button_count = 2;
}

// MotionPlus standalone: no buttons / analogs, just gyro passthrough.
static void map_motionplus(const wii_ext_state_t *s, input_event_t *ev) {
    ev->analog[ANALOG_LX] = 128;
    ev->analog[ANALOG_LY] = 128;
    ev->analog[ANALOG_RX] = 128;
    ev->analog[ANALOG_RY] = 128;
    ev->analog[ANALOG_L2] = 0;
    ev->analog[ANALOG_R2] = 0;

    if (s->has_gyro) {
        ev->gyro[0] = s->gyro[0];
        ev->gyro[1] = s->gyro[1];
        ev->gyro[2] = s->gyro[2];
        ev->has_motion = true;
        ev->gyro_range = 2000;  // normalized-to-fast-mode range
    }

    ev->layout = LAYOUT_WII_MOTIONPLUS;
    ev->button_count = 0;
}

static void map_classic(const wii_ext_state_t *s, input_event_t *ev) {
    // Wii Classic face layout, W3C positions (B1=south, B2=east, B3=west, B4=north):
    //       X        --> B4 (north)
    //     Y   A      --> Y=B3 (west), A=B2 (east)
    //       B        --> B1 (south)
    if (s->buttons & WII_BTN_A)     ev->buttons |= JP_BUTTON_B2;
    if (s->buttons & WII_BTN_B)     ev->buttons |= JP_BUTTON_B1;
    if (s->buttons & WII_BTN_X)     ev->buttons |= JP_BUTTON_B4;
    if (s->buttons & WII_BTN_Y)     ev->buttons |= JP_BUTTON_B3;
    if (s->buttons & WII_BTN_L)     ev->buttons |= JP_BUTTON_L1;
    if (s->buttons & WII_BTN_R)     ev->buttons |= JP_BUTTON_R1;
    if (s->buttons & WII_BTN_ZL)    ev->buttons |= JP_BUTTON_L2;
    if (s->buttons & WII_BTN_ZR)    ev->buttons |= JP_BUTTON_R2;
    if (s->buttons & WII_BTN_MINUS) ev->buttons |= JP_BUTTON_S1;
    if (s->buttons & WII_BTN_PLUS)  ev->buttons |= JP_BUTTON_S2;
    if (s->buttons & WII_BTN_HOME)  ev->buttons |= JP_BUTTON_A1;
    if (s->buttons & WII_BTN_DU)    ev->buttons |= JP_BUTTON_DU;
    if (s->buttons & WII_BTN_DD)    ev->buttons |= JP_BUTTON_DD;
    if (s->buttons & WII_BTN_DL)    ev->buttons |= JP_BUTTON_DL;
    if (s->buttons & WII_BTN_DR)    ev->buttons |= JP_BUTTON_DR;

    ev->analog[ANALOG_LX] = wii_stick_scale((uint8_t)(s->analog[WII_AXIS_LX] >> 2), 0, 0);
    ev->analog[ANALOG_LY] = wii_stick_scale((uint8_t)(255 - (s->analog[WII_AXIS_LY] >> 2)), 0, 1);
    ev->analog[ANALOG_RX] = wii_stick_scale((uint8_t)(s->analog[WII_AXIS_RX] >> 2), 0, 2);
    ev->analog[ANALOG_RY] = wii_stick_scale((uint8_t)(255 - (s->analog[WII_AXIS_RY] >> 2)), 0, 3);
    ev->analog[ANALOG_L2] = (uint8_t)(s->analog[WII_AXIS_LT] >> 2);
    ev->analog[ANALOG_R2] = (uint8_t)(s->analog[WII_AXIS_RT] >> 2);

    ev->layout = (s->type == WII_EXT_TYPE_CLASSIC_PRO)
                 ? LAYOUT_WII_CLASSIC_PRO : LAYOUT_WII_CLASSIC;
    ev->button_count = 4;
}

// ---- Port init helper -------------------------------------------------------

static bool init_port(wii_port_t *p, uint8_t sda, uint8_t scl) {
    p->pin_sda = sda;
    p->pin_scl = scl;

    platform_i2c_config_t cfg = {
        .bus     = (sda == 12 || sda == 16 || sda == 20 || sda == 0
                    || sda == 4 || sda == 8) ? 0 : 1,
        .sda_pin = sda,
        .scl_pin = scl,
        .freq_hz = WII_I2C_FREQ_HZ,
    };
    p->bus = platform_i2c_init(&cfg);
    if (!p->bus) {
        printf("[wii_host] ERROR: platform_i2c_init failed (SDA=%d SCL=%d)\n",
               sda, scl);
        return false;
    }

    p->transport.write    = io_write;
    p->transport.read     = io_read;
    p->transport.delay_us = io_delay;
    p->transport.ctx      = p->bus;
    wii_ext_attach(&p->ext, &p->transport);

    p->initialized     = true;
    p->last_poll_us    = 0;
    p->last_retry_us   = 0;
    p->prev_connected  = false;

    printf("[wii_host] port ready SDA=%d SCL=%d @ %uHz (bus %d)\n",
           sda, scl, (unsigned)WII_I2C_FREQ_HZ, cfg.bus);
    return true;
}

// ---- Public API -------------------------------------------------------------

void wii_host_init(void) {
    if (num_ports > 0) return;
    wii_host_init_pins(WII_PIN_SDA, WII_PIN_SCL);
}

void wii_host_init_pins(uint8_t sda, uint8_t scl) {
    if (num_ports > 0) return;
    memset(ports, 0, sizeof(ports));
    prev_buttons = 0;
    prev_analog  = 0;

    if (init_port(&ports[0], sda, scl)) {
        num_ports = 1;
    }
}

void wii_host_init_dual(uint8_t sda1, uint8_t scl1, uint8_t sda2, uint8_t scl2) {
    if (num_ports > 0) return;
    memset(ports, 0, sizeof(ports));
    prev_buttons = 0;
    prev_analog  = 0;

    if (init_port(&ports[0], sda1, scl1)) {
        num_ports = 1;
    }
    if (init_port(&ports[1], sda2, scl2)) {
        num_ports = 2;
        printf("[wii_host] dual-port mode: second nunchuck maps to right stick + B3/B4\n");
    }
}

// ---- Per-port poll ----------------------------------------------------------

// Poll a single port: handle detection, connection tracking, LED, and state.
// Returns true if state was read successfully into *out.
static bool poll_port(wii_port_t *p, uint8_t port_index, wii_ext_state_t *out) {
    if (!p->initialized) return false;

    uint32_t now = time_us_32();

    if (!p->ext.ready) {
        if ((now - p->last_retry_us) < WII_RETRY_INTERVAL_US && p->last_retry_us != 0) {
            return false;
        }
        p->last_retry_us = now;

        // LED scan-blink on port 0 only (port 1 is secondary).
        if (port_index == 0) {
            led_scan_state = !led_scan_state;
            leds_set_color(led_scan_state ? 8 : 0,
                           led_scan_state ? 8 : 0,
                           led_scan_state ? 8 : 0);
        }

        if (wii_ext_start(&p->ext)) {
            printf("[wii_host] port %d: detected type=%d id=%02X:%02X:%02X:%02X:%02X:%02X\n",
                   port_index, (int)p->ext.type,
                   p->ext.id[0], p->ext.id[1], p->ext.id[2],
                   p->ext.id[3], p->ext.id[4], p->ext.id[5]);
        }
        return false;
    }

    if ((now - p->last_poll_us) < WII_POLL_INTERVAL_US) return false;
    p->last_poll_us = now;

    if (!wii_ext_poll(&p->ext, out)) {
        if (p->prev_connected) {
            printf("[wii_host] port %d: disconnected\n", port_index);
            p->prev_connected = false;
            if (port_index == 0) leds_set_color(0, 0, 0);
        }
        return false;
    }
    if (!p->prev_connected) {
        printf("[wii_host] port %d: connected type=%d\n", port_index, (int)out->type);
        p->prev_connected = true;
        wii_stick_range_reset(port_index);
        if (port_index == 0) {
            if (out->type == WII_EXT_TYPE_NUNCHUCK) {
                leds_set_color(LED_WII_NUNCHUCK_R, LED_WII_NUNCHUCK_G, LED_WII_NUNCHUCK_B);
            } else {
                leds_set_color(LED_WII_CLASSIC_R, LED_WII_CLASSIC_G, LED_WII_CLASSIC_B);
            }
        }
    }

    return true;
}

// Map second nunchuck to right stick + B2/B4.
// Dual layout: left Z=B1, left C=B3, right Z=B2, right C=B4
static void map_nunchuck_right(const wii_ext_state_t *s, input_event_t *ev) {
    if (s->buttons & WII_BTN_Z) ev->buttons |= JP_BUTTON_B2;
    if (s->buttons & WII_BTN_C) ev->buttons |= JP_BUTTON_B4;

    uint8_t raw_x = (uint8_t)(s->analog[WII_AXIS_LX] >> 2);
    uint8_t raw_y = (uint8_t)(255 - (s->analog[WII_AXIS_LY] >> 2));
    ev->analog[ANALOG_RX] = wii_stick_scale(raw_x, 1, 2);
    ev->analog[ANALOG_RY] = wii_stick_scale(raw_y, 1, 3);
}

// ---- Main task --------------------------------------------------------------

void wii_host_task(void) {
    if (num_ports == 0) return;

    // Poll all ports.
    wii_ext_state_t states[WII_MAX_PORTS];
    bool            valid[WII_MAX_PORTS] = {false};

    for (uint8_t i = 0; i < num_ports; i++) {
        valid[i] = poll_port(&ports[i], i, &states[i]);
    }

    // Need at least port 0 to have data.
    if (!valid[0]) return;

    uint32_t now = time_us_32();

    // ------------------------------------------------------------------
    // Profile-cycle hotkey: MINUS (S1) + D-pad Up/Down held ≥ 2 s.
    // Only meaningful on accessories with a MINUS + D-pad (Classic/Pro).
    // Nunchuck has neither so this block no-ops for it.
    // ------------------------------------------------------------------
    {
        const uint32_t trigger_up   = WII_BTN_MINUS | WII_BTN_DU;
        const uint32_t trigger_down = WII_BTN_MINUS | WII_BTN_DD;
        uint32_t held = 0;
        if ((states[0].buttons & trigger_up)   == trigger_up)   held = trigger_up;
        if ((states[0].buttons & trigger_down) == trigger_down) held = trigger_down;

        if (held) {
            if (hotkey_combo_mask != held) {
                hotkey_combo_mask     = held;
                hotkey_combo_start_us = now;
                hotkey_fired          = false;
            } else if (!hotkey_fired &&
                       (now - hotkey_combo_start_us) >= WII_HOTKEY_HOLD_US) {
                output_target_t primary = router_get_primary_output();
                if (primary == OUTPUT_TARGET_NONE) primary = OUTPUT_TARGET_USB_DEVICE;
                if (held == trigger_up) {
                    profile_cycle_next(primary);
                    printf("[wii_host] profile: next (output=%d)\n", (int)primary);
                } else {
                    profile_cycle_prev(primary);
                    printf("[wii_host] profile: prev (output=%d)\n", (int)primary);
                }
                leds_indicate_profile(profile_get_active_index(primary));
                hotkey_fired = true;
            }
        } else {
            hotkey_combo_mask     = 0;
            hotkey_combo_start_us = 0;
            hotkey_fired          = false;
        }
    }

    // Build the merged input event from port 0.
    input_event_t ev;
    init_input_event(&ev);
    ev.dev_addr  = WII_DEV_ADDR_BASE;
    ev.instance  = 0;
    ev.type      = INPUT_TYPE_GAMEPAD;
    ev.transport = INPUT_TRANSPORT_NATIVE;

    switch (states[0].type) {
        case WII_EXT_TYPE_NUNCHUCK:    map_nunchuck(&states[0], &ev);   break;
        case WII_EXT_TYPE_CLASSIC:
        case WII_EXT_TYPE_CLASSIC_PRO: map_classic(&states[0], &ev);    break;
        case WII_EXT_TYPE_GUITAR:      map_guitar(&states[0], &ev);     break;
        case WII_EXT_TYPE_DRUMS:       map_drums(&states[0], &ev);      break;
        case WII_EXT_TYPE_TURNTABLE:   map_turntable(&states[0], &ev);  break;
        case WII_EXT_TYPE_TAIKO:       map_taiko(&states[0], &ev);      break;
        case WII_EXT_TYPE_UDRAW:       map_udraw(&states[0], &ev);      break;
        case WII_EXT_TYPE_MOTIONPLUS:  map_motionplus(&states[0], &ev); break;
        default: return;
    }

    // Merge second port if it's a nunchuck (dual-nunchuck mode).
    // Dual layout: left Z=B1, left C=B3, right Z=B2, right C=B4
    if (valid[1] && states[1].type == WII_EXT_TYPE_NUNCHUCK
                 && states[0].type == WII_EXT_TYPE_NUNCHUCK) {
        // Remap left nunchuck from single layout (C=B1, Z=B2)
        // to dual layout (Z=B1, C=B3)
        ev.buttons = 0;
        if (states[0].buttons & WII_BTN_Z) ev.buttons |= JP_BUTTON_B1;
        if (states[0].buttons & WII_BTN_C) ev.buttons |= JP_BUTTON_B3;

        map_nunchuck_right(&states[1], &ev);
        ev.layout = LAYOUT_WII_DUAL_NUNCHUCK;
        ev.button_count = 4;
    }

    uint64_t analog_sig =
          ((uint64_t)ev.analog[ANALOG_LX] <<  0)
        | ((uint64_t)ev.analog[ANALOG_LY] <<  8)
        | ((uint64_t)ev.analog[ANALOG_RX] << 16)
        | ((uint64_t)ev.analog[ANALOG_RY] << 24)
        | ((uint64_t)ev.analog[ANALOG_L2] << 32)
        | ((uint64_t)ev.analog[ANALOG_R2] << 40);
    if (ev.buttons == prev_buttons && analog_sig == prev_analog) return;
    prev_buttons = ev.buttons;
    prev_analog  = analog_sig;

    router_submit_input(&ev);
}

bool wii_host_is_connected(void) {
    return num_ports > 0 && ports[0].initialized && ports[0].ext.ready;
}

int wii_host_get_ext_type(void) {
    return num_ports > 0 ? (int)ports[0].ext.type : 0;
}

// ---- InputInterface ---------------------------------------------------------

static uint8_t wii_get_device_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < num_ports; i++) {
        if (ports[i].initialized && ports[i].ext.ready) count++;
    }
    return count;
}

const InputInterface wii_input_interface = {
    .name             = "Wii",
    .source           = INPUT_SOURCE_NATIVE_WII,
    .init             = wii_host_init,
    .task             = wii_host_task,
    .is_connected     = wii_host_is_connected,
    .get_device_count = wii_get_device_count,
};
