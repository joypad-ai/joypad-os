// pce_host.c - Native PCEngine / TurboGrafx-16 Host Driver
//
// See pce_host.h for the protocol summary.  This driver bit-bangs the mux
// directly (the PCE pad has no clock to track, so PIO is unnecessary) and
// polls at ~60 Hz from pce_host_task().

#include "pce_host.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include <stdio.h>

// Enable best-effort reading of the 6-button Avenue Pad 6 extended bank.
// Gated by an all-zero signature nibble, so 2-button pads are unaffected.
#ifndef PCE_ENABLE_6BUTTON
#define PCE_ENABLE_6BUTTON 1
#endif

// Bits within the assembled "normal" byte (active-low, 0 = pressed) — matches
// the layout used by the PCEngine *device* driver for consistency.
#define PCE_BIT_UP     0
#define PCE_BIT_RIGHT  1
#define PCE_BIT_DOWN   2
#define PCE_BIT_LEFT   3
#define PCE_BIT_I      4
#define PCE_BIT_II     5
#define PCE_BIT_SELECT 6
#define PCE_BIT_RUN    7

// Bits within the 6-button extended nibble (active-low).
#define PCE_EXT_BIT_III 0
#define PCE_EXT_BIT_IV  1
#define PCE_EXT_BIT_V   2
#define PCE_EXT_BIT_VI  3

#define PCE_POLL_INTERVAL_US 16666   // ~60 Hz; also gives the 6-button bank
                                     // counter the idle gap it needs to reset.
#define PCE_SETTLE_US        6       // mux + cable settling per level change
#define PCE_DEBOUNCE_US      300000  // 300 ms presence debounce (connect/disconnect)

#define PCE_DEV_ADDR 0xF0            // virtual device address for this port

static struct {
    bool     initialized;
    uint64_t next_poll_us;
    uint8_t  normal;       // active-low: d-pad + I/II/Select/Run
    uint8_t  ext_nibble;   // active-low: III/IV/V/VI (valid when six_button)
    bool     six_button;   // latched once the extended signature is seen
    bool     connected;    // debounced presence state
    bool     present_raw;  // last raw presence sample (drives debounce timer)
    uint64_t present_change_us;
} s_pce;

static inline void pce_settle(void)
{
    busy_wait_us_32(PCE_SETTLE_US);
}

// Sample the four consecutive data GPIOs as a nibble (raw, active-low).
static inline uint8_t pce_read_nibble(void)
{
    return (uint8_t)((gpio_get_all() >> PCE_PIN_D0) & 0x0Fu);
}

void pce_host_init(void)
{
    printf("[pce_host] Initializing PCEngine host\n");

    // Outputs: SEL, CLR — start in the "normal read" state (CLR low).
    gpio_init(PCE_PIN_SEL);
    gpio_init(PCE_PIN_CLR);
    gpio_set_dir(PCE_PIN_SEL, GPIO_OUT);
    gpio_set_dir(PCE_PIN_CLR, GPIO_OUT);
    gpio_put(PCE_PIN_SEL, 0);
    gpio_put(PCE_PIN_CLR, 0);

    // Inputs: D0..D3 with pull-DOWNS. A connected pad's 74HC157 mux actively
    // drives released buttons HIGH (push-pull), so any high bit means a pad is
    // present; an empty port floats to all-zero. This is what makes presence
    // detection possible (with pull-ups, idle and empty are indistinguishable).
    for (uint pin = PCE_PIN_D0; pin < PCE_PIN_D0 + 4; pin++) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_down(pin);
    }

    s_pce.initialized = true;
    s_pce.next_poll_us = time_us_64();
    s_pce.normal = 0x00;       // empty/idle (pull-down) until a pad drives high
    s_pce.ext_nibble = 0x0F;   // nothing pressed
    s_pce.six_button = false;
    s_pce.connected = false;
    s_pce.present_raw = false;
    s_pce.present_change_us = time_us_64();

    printf("[pce_host] PCEngine host ready (SEL=%d CLR=%d D0=%d 6btn=%d)\n",
           PCE_PIN_SEL, PCE_PIN_CLR, PCE_PIN_D0, PCE_ENABLE_6BUTTON);
}

// Perform one full mux scan, updating s_pce.normal / ext_nibble / six_button.
static void pce_scan(void)
{
    // --- Bank 0: normal read --------------------------------------------
    gpio_put(PCE_PIN_CLR, 0);   // hold CLR low: active read, bank 0
    gpio_put(PCE_PIN_SEL, 1);   // SEL high -> d-pad nibble
    pce_settle();
    uint8_t dpad = pce_read_nibble();   // bit0=Up,1=Right,2=Down,3=Left

    gpio_put(PCE_PIN_SEL, 0);   // SEL low -> button nibble
    pce_settle();
    uint8_t btns = pce_read_nibble();   // bit0=I,1=II,2=Select,3=Run

    s_pce.normal = (uint8_t)((btns << 4) | dpad);

#if PCE_ENABLE_6BUTTON
    // --- Bank 1: 6-button extended read ---------------------------------
    // Pulse CLR to advance the pad's internal bank counter.  On a 2-button
    // pad this simply re-addresses bank 0 (harmless re-read); on a 6-button
    // pad it exposes III..VI with an all-zero signature in the button nibble.
    gpio_put(PCE_PIN_CLR, 1);
    pce_settle();
    gpio_put(PCE_PIN_CLR, 0);
    pce_settle();

    // Match the PCEngine device driver's extended-bank layout: the all-zero
    // signature appears in the d-pad nibble (SEL high); III..VI appear in the
    // button nibble (SEL low).
    gpio_put(PCE_PIN_SEL, 1);   // SEL high -> signature nibble (0x0 = 6-button)
    pce_settle();
    uint8_t sig = pce_read_nibble();

    gpio_put(PCE_PIN_SEL, 0);   // SEL low -> III/IV/V/VI button nibble
    pce_settle();
    uint8_t ext = pce_read_nibble();

    // Only treat as 6-button when the d-pad-position nibble reads the all-zero
    // signature while the real d-pad (normal bank) does not — avoids misreading
    // a 2-button pad (whose extended re-read just mirrors the normal d-pad).
    if (sig == 0x00 && dpad != 0x00) {
        s_pce.six_button = true;
        s_pce.ext_nibble = ext;
    } else {
        s_pce.six_button = false;
        s_pce.ext_nibble = 0x0F;
    }
#endif
}

void pce_host_task(void)
{
    if (!s_pce.initialized) return;

    uint64_t now = time_us_64();
    if ((int64_t)(now - s_pce.next_poll_us) < 0) return;
    s_pce.next_poll_us = now + PCE_POLL_INTERVAL_US;

    pce_scan();

    // --- Presence detection (pull-down reads) ---------------------------
    // A connected pad always drives at least one line high (you can't hold all
    // 4 directions + 4 buttons at once); an empty port reads all-zero. Debounce
    // the raw signal before flipping connect state.
    bool present_now = (s_pce.normal != 0x00);
    if (present_now != s_pce.present_raw) {
        s_pce.present_raw = present_now;
        s_pce.present_change_us = now;
    }
    bool stable = (uint64_t)(now - s_pce.present_change_us) >= PCE_DEBOUNCE_US;

    if (stable && present_now && !s_pce.connected) {
        s_pce.connected = true;
        printf("[pce_host] Controller connected\n");
    } else if (stable && !present_now && s_pce.connected) {
        s_pce.connected = false;
        printf("[pce_host] Controller disconnected\n");
        remove_players_by_address(PCE_DEV_ADDR, 0);  // release slot -> idle LED
    }

    // Don't submit (or auto-register a player) while no pad is present. This is
    // also why pull-down all-zero must never be treated as "all buttons down".
    if (!s_pce.connected) return;

    const uint8_t n = s_pce.normal;   // active-low
    uint32_t buttons = 0;

    if (!(n & (1 << PCE_BIT_UP)))     buttons |= JP_BUTTON_DU;
    if (!(n & (1 << PCE_BIT_RIGHT)))  buttons |= JP_BUTTON_DR;
    if (!(n & (1 << PCE_BIT_DOWN)))   buttons |= JP_BUTTON_DD;
    if (!(n & (1 << PCE_BIT_LEFT)))   buttons |= JP_BUTTON_DL;
    if (!(n & (1 << PCE_BIT_I)))      buttons |= JP_BUTTON_B2;  // I  -> B2 (matches PCE output interface)
    if (!(n & (1 << PCE_BIT_II)))     buttons |= JP_BUTTON_B1;  // II -> B1
    if (!(n & (1 << PCE_BIT_SELECT))) buttons |= JP_BUTTON_S1;  // Select
    if (!(n & (1 << PCE_BIT_RUN)))    buttons |= JP_BUTTON_S2;  // Run (Start)

#if PCE_ENABLE_6BUTTON
    if (s_pce.six_button) {
        const uint8_t e = s_pce.ext_nibble;   // active-low
        if (!(e & (1 << PCE_EXT_BIT_III))) buttons |= JP_BUTTON_B3;  // III
        if (!(e & (1 << PCE_EXT_BIT_IV)))  buttons |= JP_BUTTON_B4;  // IV
        if (!(e & (1 << PCE_EXT_BIT_V)))   buttons |= JP_BUTTON_L1;  // V
        if (!(e & (1 << PCE_EXT_BIT_VI)))  buttons |= JP_BUTTON_R1;  // VI
    }
#endif

    input_event_t event;
    init_input_event(&event);
    event.dev_addr = PCE_DEV_ADDR;
    event.instance = 0;
    event.type = INPUT_TYPE_GAMEPAD;
    event.transport = INPUT_TRANSPORT_NATIVE;
    event.layout = LAYOUT_UNKNOWN;
    event.buttons = buttons;
    event.analog[ANALOG_LX] = 128;
    event.analog[ANALOG_LY] = 128;
    event.analog[ANALOG_RX] = 128;
    event.analog[ANALOG_RY] = 128;
    router_submit_input(&event);
}

// Presence is detected via pull-down reads (see pce_host_task): a connected
// pad drives lines high, an empty port reads all-zero.
bool pce_host_is_connected(void)
{
    return s_pce.connected;
}

// ============================================================================
// INPUT INTERFACE (for app declaration)
// ============================================================================

static uint8_t pce_get_device_count(void)
{
    return s_pce.connected ? 1 : 0;
}

const InputInterface pce_input_interface = {
    .name = "PCEngine",
    .source = INPUT_SOURCE_NATIVE_PCE,
    .init = pce_host_init,
    .task = pce_host_task,
    .is_connected = pce_host_is_connected,
    .get_device_count = pce_get_device_count,
};
