// wii_host.c - Native Wii extension controller host driver (HW I2C transport)

#include "wii_host.h"
#include "lib/wii_ext/wii_ext.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
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

static bool             initialized = false;
static uint8_t          pin_sda = WII_PIN_SDA;
static uint8_t          pin_scl = WII_PIN_SCL;
static platform_i2c_t   bus = NULL;
static wii_ext_t        ext;

static uint32_t         last_poll_us = 0;
static uint32_t         last_retry_us = 0;
static bool             prev_connected = false;
static uint32_t         prev_buttons = 0;
static uint64_t         prev_analog  = 0;

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

static wii_ext_transport_t ext_transport;

// ---- Event mapping ----------------------------------------------------------

static void map_nunchuck(const wii_ext_state_t *s, input_event_t *ev) {
    if (s->buttons & WII_BTN_C) ev->buttons |= JP_BUTTON_B1;
    if (s->buttons & WII_BTN_Z) ev->buttons |= JP_BUTTON_B2;

    ev->analog[ANALOG_LX] = (uint8_t)(s->analog[WII_AXIS_LX] >> 2);
    // Nunchuck native Y is Y-up = 255; invert to HID's 0=up convention.
    ev->analog[ANALOG_LY] = (uint8_t)(255 - (s->analog[WII_AXIS_LY] >> 2));
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

    ev->analog[ANALOG_LX] = (uint8_t)(s->analog[WII_AXIS_LX] >> 2);
    ev->analog[ANALOG_LY] = (uint8_t)(255 - (s->analog[WII_AXIS_LY] >> 2));
    ev->analog[ANALOG_RX] = (uint8_t)(s->analog[WII_AXIS_RX] >> 2);
    ev->analog[ANALOG_RY] = (uint8_t)(255 - (s->analog[WII_AXIS_RY] >> 2));
    ev->analog[ANALOG_L2] = (uint8_t)(s->analog[WII_AXIS_LT] >> 2);
    ev->analog[ANALOG_R2] = (uint8_t)(s->analog[WII_AXIS_RT] >> 2);

    ev->layout = (s->type == WII_EXT_TYPE_CLASSIC_PRO)
                 ? LAYOUT_WII_CLASSIC_PRO : LAYOUT_WII_CLASSIC;
    ev->button_count = 4;
}

// ---- Public API -------------------------------------------------------------

void wii_host_init(void) {
    if (initialized) return;
    wii_host_init_pins(WII_PIN_SDA, WII_PIN_SCL);
}

void wii_host_init_pins(uint8_t sda, uint8_t scl) {
    pin_sda = sda;
    pin_scl = scl;

    platform_i2c_config_t cfg = {
        // Prefer I2C0 when GP12/GP13 (the board default pair on KB2040),
        // otherwise fall back to I2C1 which supports most other pairs.
        .bus     = (sda == 12 || sda == 16 || sda == 20 || sda == 0
                    || sda == 4 || sda == 8) ? 0 : 1,
        .sda_pin = sda,
        .scl_pin = scl,
        .freq_hz = WII_I2C_FREQ_HZ,
    };
    bus = platform_i2c_init(&cfg);
    if (!bus) {
        printf("[wii_host] ERROR: platform_i2c_init failed (SDA=%d SCL=%d)\n",
               sda, scl);
        return;
    }

    ext_transport.write    = io_write;
    ext_transport.read     = io_read;
    ext_transport.delay_us = io_delay;
    ext_transport.ctx      = bus;
    wii_ext_attach(&ext, &ext_transport);

    initialized = true;
    last_poll_us = 0;
    last_retry_us = 0;
    prev_connected = false;
    prev_buttons = 0;
    prev_analog = 0;

    printf("[wii_host] ready SDA=%d SCL=%d @ %uHz (bus %d)\n",
           sda, scl, (unsigned)WII_I2C_FREQ_HZ, cfg.bus);
}

void wii_host_task(void) {
    if (!initialized) return;

    uint32_t now = time_us_32();

    if (!ext.ready) {
        if ((now - last_retry_us) < WII_RETRY_INTERVAL_US && last_retry_us != 0) {
            return;
        }
        last_retry_us = now;
        if (wii_ext_start(&ext)) {
            printf("[wii_host] detected type=%d id=%02X:%02X:%02X:%02X:%02X:%02X\n",
                   (int)ext.type,
                   ext.id[0], ext.id[1], ext.id[2],
                   ext.id[3], ext.id[4], ext.id[5]);
        }
        return;
    }

    if ((now - last_poll_us) < WII_POLL_INTERVAL_US) return;
    last_poll_us = now;

    wii_ext_state_t state;
    if (!wii_ext_poll(&ext, &state)) {
        if (prev_connected) {
            printf("[wii_host] disconnected\n");
            prev_connected = false;
        }
        return;
    }
    if (!prev_connected) {
        printf("[wii_host] connected type=%d\n", (int)state.type);
        prev_connected = true;
        // Reset change-suppression sentinels so the very first event after
        // (re)connect always submits — the router needs at least one event
        // with non-default state to register the player and record its name.
        // Using 0xFF... guarantees a mismatch against any real event.
        prev_buttons = 0xFFFFFFFFu;
        prev_analog  = 0xFFFFFFFFFFFFFFFFull;
    }

    input_event_t ev;
    init_input_event(&ev);
    ev.dev_addr  = WII_DEV_ADDR_BASE;
    ev.instance  = 0;
    ev.type      = INPUT_TYPE_GAMEPAD;
    ev.transport = INPUT_TRANSPORT_NATIVE;

    switch (state.type) {
        case WII_EXT_TYPE_NUNCHUCK:
            map_nunchuck(&state, &ev);
            break;
        case WII_EXT_TYPE_CLASSIC:
        case WII_EXT_TYPE_CLASSIC_PRO:
            map_classic(&state, &ev);
            break;
        default:
            return;
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
    return initialized && ext.ready;
}

int wii_host_get_ext_type(void) {
    return (int)ext.type;
}

// ---- InputInterface ---------------------------------------------------------

static uint8_t wii_get_device_count(void) {
    return wii_host_is_connected() ? 1 : 0;
}

const InputInterface wii_input_interface = {
    .name             = "Wii",
    .source           = INPUT_SOURCE_NATIVE_WII,
    .init             = wii_host_init,
    .task             = wii_host_task,
    .is_connected     = wii_host_is_connected,
    .get_device_count = wii_get_device_count,
};
