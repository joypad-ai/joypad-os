// pce_host.c - Native PCEngine / TurboGrafx-16 Host Driver
//
// See pce_host.h for the protocol summary.  This driver bit-bangs the mux
// directly (the PCE pad has no clock to track, so PIO is unnecessary) and
// polls at ~60 Hz from pce_host_task().
//
// Supports a single pad or a PCEngine multitap (up to 5 players). Each poll
// pulses CLR to reset the tap to player 1, then sweeps players via SEL cycles
// (the inverse of the device-side plex.pio). 6-button is read for player 1.

#include "pce_host.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include <stdio.h>

// Enable best-effort reading of the 6-button Avenue Pad 6 extended bank (player
// 1 only). Gated by an all-zero signature nibble, so 2-button pads are
// unaffected.
#ifndef PCE_ENABLE_6BUTTON
#define PCE_ENABLE_6BUTTON 1
#endif

// Periodic raw-nibble + sticky "seen" dump for characterizing real multitap
// hardware. Off by default (noisy); flip to 1 when bringing up a new tap. Emits
// over UART stdio AND the USB CDC config port so it's readable with a plain
// serial terminal (the sticky "seen" mask latches every press from boot, so it
// decouples a button press from the moment of capture).
#ifndef PCE_DEBUG_MULTITAP
#define PCE_DEBUG_MULTITAP 0
#endif

#if PCE_DEBUG_MULTITAP
#include "cdc/cdc.h"
#endif

#define PCE_MAX_PLAYERS 5            // PCEngine multitap supports up to 5

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

#define PCE_POLL_INTERVAL_US 16666   // ~60 Hz; also gives the multitap/6-button
                                     // bank counter the idle gap it needs.
#define PCE_SETTLE_US        6       // mux + cable settling per level change
#define PCE_DEBOUNCE_US      300000  // 300 ms presence debounce per port

#define PCE_DEV_ADDR 0xF0            // base virtual device address (port N = 0xF0+N)

static struct {
    bool     initialized;
    uint64_t next_poll_us;

    // Per-port (player) state
    uint8_t  normal[PCE_MAX_PLAYERS];           // active-low: d-pad + I/II/Sel/Run
    bool     connected[PCE_MAX_PLAYERS];        // debounced presence
    bool     present_raw[PCE_MAX_PLAYERS];      // last raw presence sample
    uint64_t present_change_us[PCE_MAX_PLAYERS];

    // 6-button extended state (player 1 only for now)
    uint8_t  ext_nibble;   // active-low: III/IV/V/VI (valid when six_button)
    bool     six_button;   // latched once the extended signature is seen

#if PCE_DEBUG_MULTITAP
    uint8_t  dbg_seen[PCE_MAX_PLAYERS];   // sticky OR of pressed bits per slot
#endif
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
    s_pce.ext_nibble = 0x0F;   // nothing pressed
    s_pce.six_button = false;
    uint64_t now = time_us_64();
    for (int p = 0; p < PCE_MAX_PLAYERS; p++) {
        s_pce.normal[p] = 0x00;   // empty/idle (pull-down) until a pad drives high
        s_pce.connected[p] = false;
        s_pce.present_raw[p] = false;
        s_pce.present_change_us[p] = now;
    }

    printf("[pce_host] PCEngine host ready (SEL=%d CLR=%d D0=%d 6btn=%d multitap=%d)\n",
           PCE_PIN_SEL, PCE_PIN_CLR, PCE_PIN_D0, PCE_ENABLE_6BUTTON, PCE_MAX_PLAYERS);
}

// Perform one full multitap sweep, updating s_pce.normal[] / ext_nibble /
// six_button.
static void pce_scan(void)
{
    // Reset the (multi)tap to player 1, per the documented multitap sequence
    // (pce-devel/PCE_Controller_Info): SEL must stay HIGH across the CLR pulse,
    // so that port 1 is the *active* port afterward with SEL already high — the
    // first read is then port 1's directions (no SEL toggle). Pulsing CLR with
    // SEL low instead makes the first SEL low->high read as an "advance to next
    // port", which skips port 1 (it lands on port 2).
    gpio_put(PCE_PIN_SEL, 1);   // SEL HIGH, CLR LOW
    gpio_put(PCE_PIN_CLR, 0);
    pce_settle();
    gpio_put(PCE_PIN_CLR, 1);   // SEL HIGH, CLR HIGH (the reset pulse)
    pce_settle();
    gpio_put(PCE_PIN_CLR, 0);   // SEL HIGH, CLR LOW -> port 1 now active
    pce_settle();

    // Sweep players. Port 1 reads with SEL already high; each later port is
    // reached by toggling SEL high again (the "advance" step).
    uint8_t dpad0 = 0;
    for (int p = 0; p < PCE_MAX_PLAYERS; p++) {
        if (p > 0) {
            gpio_put(PCE_PIN_SEL, 1);   // advance to next port
            pce_settle();
        }
        // SEL is HIGH here -> d-pad nibble
        uint8_t dpad = pce_read_nibble();   // bit0=Up,1=Right,2=Down,3=Left

        gpio_put(PCE_PIN_SEL, 0);   // SEL low -> button nibble
        pce_settle();
        uint8_t btns = pce_read_nibble();   // bit0=I,1=II,2=Select,3=Run

        s_pce.normal[p] = (uint8_t)((btns << 4) | dpad);
        if (p == 0) dpad0 = dpad;
    }

#if PCE_ENABLE_6BUTTON
    // --- Player 1 6-button extended read --------------------------------
    // A fresh CLR pulse rewinds to player 1 and advances its bank. On a 2-button
    // pad this re-reads bank 0; on a 6-button pad it exposes III..VI with an
    // all-zero signature in the d-pad nibble (matches the device driver layout).
    gpio_put(PCE_PIN_CLR, 1);
    pce_settle();
    gpio_put(PCE_PIN_CLR, 0);
    pce_settle();

    gpio_put(PCE_PIN_SEL, 1);   // SEL high -> signature nibble (0x0 = 6-button)
    pce_settle();
    uint8_t sig = pce_read_nibble();

    gpio_put(PCE_PIN_SEL, 0);   // SEL low -> III/IV/V/VI button nibble
    pce_settle();
    uint8_t ext = pce_read_nibble();

    if (sig == 0x00 && dpad0 != 0x00) {
        s_pce.six_button = true;
        s_pce.ext_nibble = ext;
    } else {
        s_pce.six_button = false;
        s_pce.ext_nibble = 0x0F;
    }
#endif
}

// Map a port's active-low normal byte (+ player-1 6-button) to JP_BUTTON bits.
static uint32_t pce_decode_buttons(int port)
{
    const uint8_t n = s_pce.normal[port];   // active-low
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
    if (port == 0 && s_pce.six_button) {
        const uint8_t e = s_pce.ext_nibble;   // active-low
        if (!(e & (1 << PCE_EXT_BIT_III))) buttons |= JP_BUTTON_B3;  // III
        if (!(e & (1 << PCE_EXT_BIT_IV)))  buttons |= JP_BUTTON_B4;  // IV
        if (!(e & (1 << PCE_EXT_BIT_V)))   buttons |= JP_BUTTON_L1;  // V
        if (!(e & (1 << PCE_EXT_BIT_VI)))  buttons |= JP_BUTTON_R1;  // VI
    }
#endif
    return buttons;
}

void pce_host_task(void)
{
    if (!s_pce.initialized) return;

    uint64_t now = time_us_64();
    if ((int64_t)(now - s_pce.next_poll_us) < 0) return;
    s_pce.next_poll_us = now + PCE_POLL_INTERVAL_US;

    pce_scan();

#if PCE_DEBUG_MULTITAP
    // Latch every button ever seen per slot (sticky), so a press registers no
    // matter when it happens — decouples the user's press from the capture.
    for (int p = 0; p < PCE_MAX_PLAYERS; p++) {
        if (s_pce.normal[p] != 0x00) {            // ignore floating (all-low) slots
            s_pce.dbg_seen[p] |= (uint8_t)(~s_pce.normal[p]);
        }
    }
    static uint64_t dbg_next = 0;
    if ((int64_t)(now - dbg_next) >= 0) {
        dbg_next = now + 250000;  // ~4 Hz
        char dbg[100];
        snprintf(dbg, sizeof(dbg),
                 "PCE raw:%02X %02X %02X %02X %02X seen:%02X %02X %02X %02X %02X\r\n",
                 s_pce.normal[0], s_pce.normal[1], s_pce.normal[2], s_pce.normal[3], s_pce.normal[4],
                 s_pce.dbg_seen[0], s_pce.dbg_seen[1], s_pce.dbg_seen[2],
                 s_pce.dbg_seen[3], s_pce.dbg_seen[4]);
        printf("%s", dbg);
        cdc_data_write_str(dbg);
    }
#endif

    for (int p = 0; p < PCE_MAX_PLAYERS; p++) {
        // Per-port presence. The multitap drives empty AND released ports to
        // 0xFF, so an idle pad is indistinguishable from an empty slot by level
        // alone. We register a port on real ACTIVITY (a press: not 0x00, not
        // 0xFF) and keep it alive while plugged. A 0x00 read is a floating slot
        // past the tap's range (absent).
        uint8_t n = s_pce.normal[p];
        bool floating = (n == 0x00);
        bool active   = (n != 0x00 && n != 0xFF);

        // Slot 0 is the primary controller (a directly-connected pad OR multitap
        // port 1). Slots >0 are additional multitap ports — but a direct pad has
        // no tap to advance, so it echoes its data onto every slot. Only register
        // a higher slot when its press DIFFERS from slot 0, which rejects those
        // echoes (and idle/empty tap ports, which read 0xFF == slot 0).
        bool connect_press = active && (p == 0 || n != s_pce.normal[0]);

        // Connect instantly on the first real press — no hold required.
        if (connect_press && !s_pce.connected[p]) {
            s_pce.connected[p] = true;
            printf("[pce_host] Player %d connected\n", p + 1);
        }

        // Disconnect only after the slot floats (0x00) for the debounce window.
        if (floating) {
            if (!s_pce.present_raw[p]) {
                s_pce.present_raw[p] = true;
                s_pce.present_change_us[p] = now;
            }
        } else {
            s_pce.present_raw[p] = false;   // driven (FF or a press) -> still there
        }
        if (s_pce.connected[p] && s_pce.present_raw[p] &&
            (uint64_t)(now - s_pce.present_change_us[p]) >= PCE_DEBOUNCE_US) {
            s_pce.connected[p] = false;
            s_pce.present_raw[p] = false;
            printf("[pce_host] Player %d disconnected\n", p + 1);
            remove_players_by_address(PCE_DEV_ADDR + p, 0);
        }

        if (!s_pce.connected[p]) continue;

        input_event_t event;
        init_input_event(&event);
        event.dev_addr = PCE_DEV_ADDR + p;
        event.instance = 0;
        event.type = INPUT_TYPE_GAMEPAD;
        event.transport = INPUT_TRANSPORT_NATIVE;
        event.layout = LAYOUT_UNKNOWN;
        // A floating (0x00) read is active-low "all pressed" — never emit that;
        // hold neutral until the port debounces out.
        event.buttons = floating ? 0 : pce_decode_buttons(p);
        event.analog[ANALOG_LX] = 128;
        event.analog[ANALOG_LY] = 128;
        event.analog[ANALOG_RX] = 128;
        event.analog[ANALOG_RY] = 128;
        router_submit_input(&event);
    }
}

// Presence is detected via pull-down reads (see pce_host_task): a connected pad
// drives lines high, an empty port reads all-zero.
bool pce_host_is_connected(void)
{
    for (int p = 0; p < PCE_MAX_PLAYERS; p++) {
        if (s_pce.connected[p]) return true;
    }
    return false;
}

// ============================================================================
// INPUT INTERFACE (for app declaration)
// ============================================================================

static uint8_t pce_get_device_count(void)
{
    uint8_t count = 0;
    for (int p = 0; p < PCE_MAX_PLAYERS; p++) {
        if (s_pce.connected[p]) count++;
    }
    return count;
}

const InputInterface pce_input_interface = {
    .name = "PCEngine",
    .source = INPUT_SOURCE_NATIVE_PCE,
    .init = pce_host_init,
    .task = pce_host_task,
    .is_connected = pce_host_is_connected,
    .get_device_count = pce_get_device_count,
};
