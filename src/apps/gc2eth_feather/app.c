// app.c — gc2eth_feather: Adafruit Feather RP2040 USB Host + W5500 PoE
// FeatherWing. Same intercept-replay logic as src/apps/gc2eth/app.c; only
// the transport driver differs (W5500 SPI instead of CH9120 UART).
//
// Wire protocol (verified from Dolphin SI_DeviceGBA.cpp):
//   cmd  | tx total | rx total
//   0xFF | 1        | 3        (RESET)
//   0x00 | 1        | 3        (STATUS)
//   0x14 | 1        | 5        (READ)
//   0x15 | 5        | 1        (WRITE)
//   other| 1        | 1        (default per Dolphin source)

#include "app.h"
#include "w5500.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/host/gc/gc_host.h"
#include "native/host/gc/joybus_bridge.h"
#include "native/host/gc/gba_multiboot.h"
#include "usb/usbd/usbd.h"
#include "usb/usbd/cdc/cdc.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>

// Direct CDC debug output. The project's cdc.c owns the only CDC
// interface, so pico-sdk's stdio_usb doesn't get routed properly even
// when pico_enable_stdio_usb(1). Writing via cdc_data_write_str bypasses
// stdio and goes straight to the CDC TX endpoint — guaranteed visible
// to whatever's monitoring /dev/cu.usbmodem*.
static void dbg(const char* line) {
    cdc_data_write_str(line);
    cdc_data_flush();
}

// ============================================================================
// Multiboot intercept-replay — identical algorithm to gc2eth (CH9120 build).
// Comments live there in detail; here just the bits unique to the W5500 port.
// ============================================================================
#define MAGIC_SEDO 0x6f646573u
#define MAGIC_KAWA 0x6177614bu
#define MAGIC_BY   0x20796220u

typedef enum {
    MB_PASSTHROUGH = 0,
    MB_AWAIT_OUR_KEY,
    MB_CAPTURE,
    MB_REPLAY_DONE,
} mb_state_t;

static mb_state_t s_mb_state    = MB_PASSTHROUGH;
static uint32_t   s_session_key = 0;
static uint32_t   s_byte_offset = 0;
static int        s_capture_writes = 0;
static bool       s_armed = false;

// USB device stack via outputs[] so main.c brings it up at default clock,
// then app_init overclocks to 130 MHz + re-runs stdio_init_all. Same
// pattern as gc2usb / gc2eth.
extern const OutputInterface usbd_output_interface;
static const OutputInterface* s_outputs[] = { &usbd_output_interface };

const InputInterface** app_get_input_interfaces(uint8_t* count) { *count = 0; return NULL; }
const OutputInterface** app_get_output_interfaces(uint8_t* count) {
    *count = 1; return s_outputs;
}

// ============================================================================
// W5500 derived params + small helpers
// ============================================================================
static const uint8_t s_subnet[4]  = { W5500_SUBNET };
static const uint8_t s_local_ip[4] = {
    W5500_LOCAL_IP_A, W5500_LOCAL_IP_B, W5500_LOCAL_IP_C, W5500_LOCAL_IP_D };
static const uint8_t s_gateway[4] = {
    W5500_GATEWAY_A, W5500_GATEWAY_B, W5500_GATEWAY_C, W5500_GATEWAY_D };

// MAC derived from the RP2040's unique board ID — first byte forced to
// 0x02 (locally-administered, unicast). This way two Feathers on the
// same LAN don't collide.
static uint8_t s_mac[6];
static inline uint8_t hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return 0;
}
static void derive_mac(void) {
    extern void platform_get_serial(char* out, size_t n);  // from core/platform
    char id[17] = {0};
    platform_get_serial(id, sizeof(id));
    s_mac[0] = 0x02;
    for (int i = 0; i < 5; i++) {
        s_mac[i+1] = (hex_nibble(id[i*2 + 1]) << 4) | hex_nibble(id[i*2 + 2]);
    }
}

static w5500_pins_t s_pins;

// Block-recv `n` bytes from socket 0 with a microsecond budget. Returns
// bytes copied. Mirrors ch9120_recv_bytes() semantics so the protocol
// state machine below reads naturally.
static int sock_recv_blocking(uint8_t* dst, int n, uint32_t timeout_us) {
    int got = 0;
    absolute_time_t deadline = make_timeout_time_us(timeout_us);
    while (got < n) {
        size_t r = w5500_sock0_recv(dst + got, (size_t)(n - got));
        if (r > 0) {
            got += (int)r;
            continue;
        }
        if (timeout_us == 0) break;
        if (time_reached(deadline)) break;
        tight_loop_contents();
    }
    return got;
}

// Non-blocking 1-byte recv. Returns -1 if no byte available.
static int sock_recv_byte_nb(void) {
    uint8_t b;
    size_t r = w5500_sock0_recv(&b, 1);
    return (r == 1) ? (int)b : -1;
}

// ============================================================================
// App init
// ============================================================================
void app_init(void)
{
    set_sys_clock_khz(130000, true);  // joybus PIO timing
    stdio_init_all();                  // recompute baud divisors

    // dbg() writes go into TinyUSB's CDC TX buffer. They flush once the
    // main loop starts pumping tud_task. No blocking sleep needed — the
    // 2.5s delay I had here previously starved USB enumeration and caused
    // the host to force-reset the device back into bootloader.
    dbg("\r\n[gc2eth_feather] " JOYPAD_VERSION " starting\r\n");
    dbg("[gc2eth_feather]   board: " BOARD "\r\n");
    {
        char buf[128];
        snprintf(buf, sizeof buf,
                 "[gc2eth_feather]   joybus=GP%d  W5500 SPI: SCK=GP%d MOSI=GP%d MISO=GP%d CS=GP%d\r\n",
                 GC_DATA_PIN, W5500_PIN_SCK, W5500_PIN_MOSI, W5500_PIN_MISO, W5500_PIN_CS);
        dbg(buf);
    }

    gc_host_init();
    joybus_bridge_init();
    if (!joybus_bridge_start()) {
        printf("[app:gc2eth_feather] !! joybus_bridge_start failed\n");
    }

    derive_mac();
    s_pins.spi      = W5500_SPI;
    s_pins.pin_sck  = W5500_PIN_SCK;
    s_pins.pin_mosi = W5500_PIN_MOSI;
    s_pins.pin_miso = W5500_PIN_MISO;
    s_pins.pin_cs   = W5500_PIN_CS;
    s_pins.pin_rst  = W5500_PIN_RST;
    s_pins.spi_hz   = W5500_SPI_HZ;

    if (!w5500_init(&s_pins, s_mac, s_local_ip, s_subnet, s_gateway)) {
        printf("[app:gc2eth_feather] !! w5500_init failed\n");
        return;
    }
    static const uint8_t s_dest_ip[4] = {
        W5500_DEST_IP_A, W5500_DEST_IP_B, W5500_DEST_IP_C, W5500_DEST_IP_D
    };
    if (!w5500_sock0_connect_tcp(s_dest_ip, W5500_DEST_PORT, W5500_LISTEN_PORT)) {
        printf("[app:gc2eth_feather] !! initial sock0 connect failed\n");
        // not fatal — task loop will retry
    }
    // Clock-sync socket: Dolphin won't progress past initial multiboot
    // handshake without this second connection established.
    if (!w5500_sock1_connect_tcp(s_dest_ip, W5500_CLOCK_PORT,
                                 W5500_CLOCK_LOCAL_PORT)) {
        printf("[app:gc2eth_feather] !! initial sock1 (clock) connect failed\n");
    }

    printf("[app:gc2eth_feather] ready — sock0=>%u.%u.%u.%u:%u  sock1=>:%u\n",
           s_dest_ip[0], s_dest_ip[1], s_dest_ip[2], s_dest_ip[3],
           W5500_DEST_PORT, W5500_CLOCK_PORT);
}

// ============================================================================
// Dolphin protocol + intercept-replay
// ============================================================================
static void dolphin_cmd_lengths(uint8_t cmd, int* tx_total, int* rx_len) {
    switch (cmd) {
        case 0xFF: case 0x00: *tx_total = 1; *rx_len = 3; break;
        case 0x14:            *tx_total = 1; *rx_len = 5; break;
        case 0x15:            *tx_total = 5; *rx_len = 1; break;
        default:              *tx_total = 1; *rx_len = 1; break;
    }
}

static volatile uint32_t g_frames_seen = 0;
static volatile uint8_t  g_last_cmd = 0;
static volatile uint8_t  g_last_rx[5] = {0};
static volatile int      g_last_n = 0;
static volatile uint32_t g_short_tx = 0;

// Phase 2: STATUS cache. Refreshes every N polls (default 4 — chosen
// from FFCC trace where ~14-15 polls separate RECV-bit transitions, so
// 4 keeps Dolphin's view of RECV transitions ≤4 polls stale = ~4ms).
static uint8_t  s_status_cache_rx[3] = {0,0,0};
static bool     s_status_cache_have  = false;
static uint16_t s_status_polls_since = 0;
static uint16_t s_status_refresh_n   = 4;
static volatile uint32_t s_status_cache_hits = 0;
static volatile uint32_t s_status_cache_miss = 0;
uint32_t gc2eth_status_cache_hits(void) { return s_status_cache_hits; }
uint32_t gc2eth_status_cache_miss(void) { return s_status_cache_miss; }

// Speculative pre-send of STATUS replies (Phase 2.5). After answering
// a STATUS request, we push more 3-byte STATUS replies into Dolphin's
// TCP recv buffer for the next-N expected STATUS requests. Each future
// STATUS request then completes with 0 TCP RTT on Dolphin's side
// (data already buffered). Only refills when the cached jstat is
// "boring" (no RECV bit set) — when we see RECV set, Dolphin is about
// to issue a READ and pre-sent bytes would corrupt that.
//
// Set spec_depth=0 to disable. Higher = more potential speedup but
// brittle if Dolphin sends unexpected non-STATUS cmds.
static uint8_t  s_spec_depth = 0;
static uint8_t  s_spec_outstanding = 0;
static volatile uint32_t s_spec_corruption = 0;

void gc2eth_spec_set_depth(uint8_t n) { s_spec_depth = n; }
uint8_t gc2eth_spec_get_depth(void) { return s_spec_depth; }
uint8_t gc2eth_spec_get_outstanding(void) { return s_spec_outstanding; }
uint32_t gc2eth_spec_get_corruption(void) { return s_spec_corruption; }

// WRITE-ack cache (Phase 3). In games like Madden the WRITE→jstat reply
// is constant across many commands (always 0x32 in the play-call loop).
// Mirroring the STATUS cache: serve up to N consecutive WRITEs from a
// cached 1-byte reply before forcing a refresh. N=0 disables.
static uint8_t  s_write_cache_rx     = 0;
static bool     s_write_cache_have   = false;
static uint16_t s_write_polls_since  = 0;
static uint16_t s_write_refresh_n    = 0;     // off by default; opt-in
static volatile uint32_t s_write_cache_hits = 0;
static volatile uint32_t s_write_cache_miss = 0;

void gc2eth_write_cache_set_n(uint16_t n) {
    s_write_refresh_n = n;
    s_write_polls_since = n;  // expire so first call refreshes
}
uint16_t gc2eth_write_cache_get_n(void) { return s_write_refresh_n; }
uint32_t gc2eth_write_cache_hits(void) { return s_write_cache_hits; }
uint32_t gc2eth_write_cache_miss(void) { return s_write_cache_miss; }

// READ-reply cache (Phase 4). Risky: READ replies can carry actual
// game-state changes (e.g. a GBA button press the game wants to see),
// so cached/stale replies delay input responsiveness. Use sparingly.
static uint8_t  s_read_cache_rx[5]   = {0,0,0,0,0};
static bool     s_read_cache_have    = false;
static uint16_t s_read_polls_since   = 0;
static uint16_t s_read_refresh_n     = 0;     // off by default
static volatile uint32_t s_read_cache_hits = 0;
static volatile uint32_t s_read_cache_miss = 0;

void gc2eth_read_cache_set_n(uint16_t n) {
    s_read_refresh_n = n;
    s_read_polls_since = n;
}
uint16_t gc2eth_read_cache_get_n(void) { return s_read_refresh_n; }
uint32_t gc2eth_read_cache_hits(void) { return s_read_cache_hits; }
uint32_t gc2eth_read_cache_miss(void) { return s_read_cache_miss; }

void gc2eth_status_cache_set_n(uint16_t n) {
    s_status_refresh_n = n;
    s_status_polls_since = n;  // force refresh on next STATUS
}
uint16_t gc2eth_status_cache_get_n(void) { return s_status_refresh_n; }

// ----- Trace ring (Phase 1: profiling) ----------------------------------
// Sized to cover ~2 sec at 60Hz × ~10 cmds/frame; bigger if RAM allows.
// Each entry is 12B → 8K entries = 96 KB. Way too big. Cap at 4K → 48KB
// (RP2040 has 264KB SRAM — fits comfortably).
#define TRACE_SIZE 4096
static gc2eth_trace_entry_t s_trace[TRACE_SIZE];
static volatile uint32_t    s_trace_pos    = 0;
static volatile bool        s_trace_armed  = false;
static uint32_t             s_trace_last_us = 0;

bool gc2eth_trace_armed(void) { return s_trace_armed; }

void gc2eth_trace_start(void) {
    s_trace_pos = 0;
    s_trace_last_us = time_us_32();
    s_trace_armed = true;
}

void gc2eth_trace_stop(void) { s_trace_armed = false; }

void gc2eth_trace_record(uint8_t cmd, int n, const uint8_t* rx, int rx_len) {
    if (!s_trace_armed) return;
    if (s_trace_pos >= TRACE_SIZE) { s_trace_armed = false; return; }
    uint32_t now = time_us_32();
    gc2eth_trace_entry_t* e = &s_trace[s_trace_pos++];
    e->delta_us = now - s_trace_last_us;
    e->cmd      = cmd;
    e->n        = (int8_t)((n < -128) ? -128 : (n > 127 ? 127 : n));
    for (int i = 0; i < 5; i++) e->rx[i] = (i < rx_len) ? rx[i] : 0;
    s_trace_last_us = now;
}

uint32_t gc2eth_trace_count(void) { return s_trace_pos; }

bool gc2eth_trace_get(uint32_t idx, gc2eth_trace_entry_t* out) {
    if (idx >= s_trace_pos) return false;
    *out = s_trace[idx];
    return true;
}

void gc2eth_get_diag(gc2eth_diag_t* out) {
    out->frames_seen = g_frames_seen;
    out->last_cmd    = g_last_cmd;
    out->last_n      = g_last_n;
    for (int i = 0; i < 5; i++) out->last_rx[i] = g_last_rx[i];
}

static void run_native_replay(void) {
    uint32_t bytes = joybus_bridge_mb_size();
    if (bytes >= 4) joybus_bridge_mb_trim(4);
    (void)joybus_bridge_mb_upload(/*channel=*/0);
    s_mb_state = MB_REPLAY_DONE;
}

static bool process_dolphin_frame_one(void)
{
    int cmd_byte = sock_recv_byte_nb();
    if (cmd_byte < 0) return false;
    g_frames_seen++;
    g_last_cmd = (uint8_t)cmd_byte;

    uint8_t tx[5] = { (uint8_t)cmd_byte };
    int tx_total = 1, rx_len = 1;
    dolphin_cmd_lengths(tx[0], &tx_total, &rx_len);

    if (tx_total > 1) {
        int got = sock_recv_blocking(tx + 1, tx_total - 1, 5000);
        if (got != tx_total - 1) {
            g_short_tx++;
            return false;
        }
    }

    if (tx[0] == 0xFF) {
        s_mb_state = MB_PASSTHROUGH;
        s_session_key = 0;
        s_byte_offset = 0;
        s_capture_writes = 0;
        s_armed = false;
        joybus_bridge_mb_reset();
        // Speculative replies (if any) are now stale — start fresh.
        s_spec_outstanding = 0;
        s_status_cache_have = false;
    }

    // INTERCEPT DISABLED — pure passthrough only. Madden polls STATUS
    // between WRITEs and watches JSTAT.RECV bit toggle to confirm the
    // GBA is consuming the stream. Our constant fake-reply doesn't
    // produce that drain pattern, so Madden aborts within a few WRITEs.
    // Full passthrough is slower (~13K cmds × 5ms ≈ 65s) but real GBA
    // jstat transitions naturally happen on the wire. See the Mac-side
    // daemon (tools/gba-bridge/index.js) for the same conclusion.
    static const bool s_intercept_enabled = false;

    // CAPTURE mode: short-circuit WRITEs, fake responses for STATUS, trigger replay on READ.
    if (s_intercept_enabled && s_mb_state == MB_CAPTURE) {
        if (tx[0] == 0x15) {
            uint32_t word = (uint32_t)tx[1] | ((uint32_t)tx[2] << 8)
                          | ((uint32_t)tx[3] << 16) | ((uint32_t)tx[4] << 24);
            uint32_t plaintext;
            if (s_byte_offset < 0xC0) {
                plaintext = word;
            } else {
                s_session_key = (s_session_key * MAGIC_KAWA) + 1;
                plaintext = word ^ s_session_key
                          ^ ((~(s_byte_offset + (0x20u << 20))) + 1u)
                          ^ MAGIC_BY;
            }
            uint8_t pt[4] = {
                (uint8_t)plaintext, (uint8_t)(plaintext >> 8),
                (uint8_t)(plaintext >> 16), (uint8_t)(plaintext >> 24),
            };
            joybus_bridge_mb_append(pt, 4);
            s_byte_offset += 4;
            s_capture_writes++;

            uint8_t fake = 0x00;
            w5500_sock0_send(&fake, 1);
            g_last_n = 1; g_last_rx[0] = fake;
            return true;
        }
        if (tx[0] == 0x00) {
            uint8_t fake[3] = { 0x00, 0x04, 0x12 };  // type=0x0400, jstat=PSF0+RECV
            w5500_sock0_send(fake, 3);
            g_last_n = 3;
            for (int i = 0; i < 3; i++) g_last_rx[i] = fake[i];
            return true;
        }
        if (tx[0] == 0x14) {
            run_native_replay();
            uint8_t fake[5] = {0};
            w5500_sock0_send(fake, rx_len);
            g_last_n = rx_len;
            for (int i = 0; i < 5; i++) g_last_rx[i] = 0;
            return true;
        }
    }

    // STATUS handling (Phase 2 + 2.5):
    //   - Cache the last real GBA STATUS reply, serve N polls from it
    //     before forcing a refresh.
    //   - Optionally pre-send K more replies speculatively (Phase 2.5)
    //     so Dolphin's next K STATUS requests complete with 0 RTT on
    //     Dolphin's side. Only does this when the cached jstat is
    //     "boring" (no RECV bit); when we see RECV we stop refilling
    //     because Dolphin is about to issue a READ.
    if (tx[0] == 0x00 && s_status_refresh_n > 0
        && s_status_cache_have && s_status_polls_since < s_status_refresh_n) {
        // Cache hit. If Dolphin already consumed a speculative reply
        // from its recv buffer, this request doesn't need a new one.
        if (s_spec_outstanding > 0) {
            s_spec_outstanding--;
        } else {
            w5500_sock0_send((uint8_t*)s_status_cache_rx, 3);
        }
        // Refill speculative pre-sends iff the cached jstat is boring.
        bool boring = (s_status_cache_rx[2] & 0x10) == 0;
        if (boring) {
            while (s_spec_outstanding < s_spec_depth) {
                w5500_sock0_send((uint8_t*)s_status_cache_rx, 3);
                s_spec_outstanding++;
            }
        }
        s_status_polls_since++;
        s_status_cache_hits++;
        g_last_n = 3;
        for (int i = 0; i < 3; i++) g_last_rx[i] = s_status_cache_rx[i];
        return true;
    }
    // Any non-STATUS arriving while we have speculative bytes in flight
    // means Dolphin's recv buffer has stale STATUS bytes that will
    // corrupt the response we're about to send. Log it; we can't
    // recover without protocol-level help.
    if (tx[0] != 0x00 && s_spec_outstanding > 0) {
        s_spec_corruption++;
        s_spec_outstanding = 0;  // give up tracking; corruption already happened
    }

    // WRITE-ack cache (Phase 3). Mostly useful for games where WRITE
    // replies are constant (e.g. Madden's play-call WRITE → 0x32).
    // Opt-in via WRITECACHE=N.
    if (tx[0] == 0x15 && s_write_refresh_n > 0
        && s_write_cache_have && s_write_polls_since < s_write_refresh_n) {
        w5500_sock0_send(&s_write_cache_rx, 1);
        s_write_polls_since++;
        s_write_cache_hits++;
        g_last_n = 1;
        g_last_rx[0] = s_write_cache_rx;
        return true;
    }
    // READ-reply cache (Phase 4). Riskier than the others — READ can
    // carry game-state. Opt-in via READCACHE=N.
    if (tx[0] == 0x14 && s_read_refresh_n > 0
        && s_read_cache_have && s_read_polls_since < s_read_refresh_n) {
        w5500_sock0_send(s_read_cache_rx, 5);
        s_read_polls_since++;
        s_read_cache_hits++;
        g_last_n = 5;
        for (int i = 0; i < 5; i++) g_last_rx[i] = s_read_cache_rx[i];
        return true;
    }

    // PASSTHROUGH: forward command to joybus, watch for state transitions.
    uint8_t rx[5];
    // Per-command joybus timeout. READ (0x14) carries the Kawasedo
    // session_key from the GBA BIOS, which has to do a non-trivial
    // computation before replying — the 1ms budget we used for RESET /
    // STATUS isn't enough. 5ms covers READ comfortably and is still
    // fast enough that the rest of the multiboot loop completes well
    // inside Madden's per-game timeout.
    const uint32_t to_us = (tx[0] == 0x14) ? 5000 : 1000;
    int n = joybus_bridge_xfer(tx, (uint16_t)tx_total,
                               rx, (uint16_t)rx_len, to_us);
    gc2eth_trace_record(tx[0], n, rx, rx_len);
    if (n < 0) {
        for (int i = 0; i < rx_len; i++) rx[i] = 0;
        n = rx_len;
    } else {
        for (int i = n; i < rx_len; i++) rx[i] = 0;
    }
    w5500_sock0_send(rx, rx_len);

    // Update / invalidate status cache based on what just happened.
    if (tx[0] == 0x00 && rx_len >= 3) {
        // Real STATUS reply — refresh the cache.
        for (int i = 0; i < 3; i++) s_status_cache_rx[i] = rx[i];
        s_status_cache_have = true;
        s_status_polls_since = 0;
        s_status_cache_miss++;
        // If jstat is "boring" (no RECV bit) we can speculatively
        // pre-send replies for upcoming STATUS requests. Otherwise
        // (RECV set) Dolphin is about to issue a READ — don't speculate.
        if ((rx[2] & 0x10) == 0) {
            while (s_spec_outstanding < s_spec_depth) {
                w5500_sock0_send(rx, 3);
                s_spec_outstanding++;
            }
        }
    } else if (tx[0] == 0x14 || tx[0] == 0x15 || tx[0] == 0xFF) {
        // READ/WRITE/RESET can flip the GBA's RECV bit — force the next
        // STATUS to go to the wire so we observe the new state promptly.
        s_status_polls_since = s_status_refresh_n;  // expire cache
    }
    if (tx[0] == 0x15 && rx_len >= 1) {
        // Real WRITE reply — refresh write cache.
        s_write_cache_rx     = rx[0];
        s_write_cache_have   = true;
        s_write_polls_since  = 0;
        s_write_cache_miss++;
    } else if (tx[0] == 0xFF) {
        // Cipher state changes — invalidate.
        s_write_cache_have   = false;
        s_write_polls_since  = s_write_refresh_n;
    }
    if (tx[0] == 0x14 && rx_len >= 5) {
        // Real READ reply — refresh read cache.
        for (int i = 0; i < 5; i++) s_read_cache_rx[i] = rx[i];
        s_read_cache_have   = true;
        s_read_polls_since  = 0;
        s_read_cache_miss++;
    } else if (tx[0] == 0xFF) {
        s_read_cache_have   = false;
        s_read_polls_since  = s_read_refresh_n;
    }

    // Arm intercept on STATUS-with-PSF0
    if (s_intercept_enabled && s_mb_state == MB_PASSTHROUGH
        && (tx[0] == 0x00 || tx[0] == 0xFF) && n >= 3) {
        uint16_t type  = (uint16_t)rx[0] | ((uint16_t)rx[1] << 8);
        uint8_t  jstat = rx[2];
        if (type == 0x0400 && (jstat & 0x10)) s_armed = true;
    }

    // Snoop session_key on first non-zero READ response
    if (s_intercept_enabled && s_armed && s_mb_state == MB_PASSTHROUGH
        && tx[0] == 0x14 && n >= 5) {
        uint32_t seed = (uint32_t)rx[0] | ((uint32_t)rx[1] << 8)
                      | ((uint32_t)rx[2] << 16) | ((uint32_t)rx[3] << 24);
        if (seed != 0) {
            s_session_key = seed ^ MAGIC_SEDO;
            s_mb_state = MB_AWAIT_OUR_KEY;
        }
    } else if (s_intercept_enabled && s_mb_state == MB_AWAIT_OUR_KEY && tx[0] == 0x15) {
        s_mb_state = MB_CAPTURE;
        s_byte_offset = 0;
        s_capture_writes = 0;
        joybus_bridge_mb_reset();
    }

    g_last_n = n;
    for (int i = 0; i < 5; i++) g_last_rx[i] = (i < rx_len) ? rx[i] : 0;
    return true;
}

// ============================================================================
// Main task
// ============================================================================
void app_task(void)
{
    // 'B' over CDC stdio drops into UF2 bootloader. Same shortcut as gc2usb.
    int c = getchar_timeout_us(0);
    if (c == 'B') reset_usb_boot(0, 0);

    // Drain anything Dolphin has sent us.
    while (process_dolphin_frame_one()) { /* loop */ }

    // Drain (and discard) clock-sync bytes from sock1. We don't actually
    // care about their value — but we MUST consume them so the W5500's
    // RX buffer doesn't fill up and back-pressure Dolphin.
    uint8_t clock_drain[64];
    while (w5500_sock1_recv(clock_drain, sizeof(clock_drain)) > 0) { /* drop */ }

    // CLOSE_WAIT means peer hung up — close + redial. Same for CLOSED.
    // Throttle the redial so we don't spam ARP/SYN at the host when
    // nothing's listening (Dolphin isn't running yet). Redial BOTH
    // sockets in lockstep — Dolphin restarts both atomically.
    static const uint8_t s_dest_ip[4] = {
        W5500_DEST_IP_A, W5500_DEST_IP_B, W5500_DEST_IP_C, W5500_DEST_IP_D
    };
    uint8_t sr  = w5500_sock0_status();
    uint8_t sr1 = w5500_sock1_status();
    bool s0_dead = (sr  == 0x1C || sr  == 0x00);
    bool s1_dead = (sr1 == 0x1C || sr1 == 0x00);
    if (s0_dead || s1_dead) {
        static absolute_time_t next_redial;
        static bool redial_init = false;
        if (!redial_init) { next_redial = get_absolute_time(); redial_init = true; }
        if (time_reached(next_redial)) {
            printf("[app:gc2eth_feather] peer closed (sr0=0x%02x sr1=0x%02x) — redialing both\n",
                   sr, sr1);
            if (s0_dead) w5500_sock0_connect_tcp(s_dest_ip, W5500_DEST_PORT,
                                                  W5500_LISTEN_PORT);
            if (s1_dead) w5500_sock1_connect_tcp(s_dest_ip, W5500_CLOCK_PORT,
                                                  W5500_CLOCK_LOCAL_PORT);
            next_redial = make_timeout_time_ms(500);
        }
    }

    // 1 Hz status heartbeat for sanity-check via CDC (direct write so the
    // host actually sees it, bypassing the broken pico stdio_usb routing).
    static absolute_time_t next_beat;
    static bool beat_init = false;
    if (!beat_init) { next_beat = make_timeout_time_ms(1000); beat_init = true; }
    if (time_reached(next_beat)) {
        next_beat = make_timeout_time_ms(1000);
        char buf[96];
        uint8_t st = w5500_sock0_status();
        snprintf(buf, sizeof buf,
                 "[gc2eth_feather] sock0_sr=0x%02x frames=%lu armed=%d state=%d\r\n",
                 st, (unsigned long)g_frames_seen, s_armed ? 1 : 0, s_mb_state);
        dbg(buf);
    }
}
