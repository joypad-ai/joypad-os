// app.c — gc2eth M3: CH9120 ↔ Dolphin GBA wire protocol ↔ joybus_bridge
//
// Boot sequence:
//   1. gc_host_init()           — joybus PIO on GP_DATA (GP2 here)
//   2. joybus_bridge_start()    — take ownership of the joybus port
//   3. ch9120_init()            — CH9120 as TCP server on 54970
//   4. main loop                — parse Dolphin frames, proxy to joybus
//
// Wire protocol (verified from Dolphin SI_DeviceGBA.cpp):
//   cmd  | tx total | rx total
//   0xFF | 1        | 3        (RESET)
//   0x00 | 1        | 3        (STATUS)
//   0x14 | 1        | 5        (READ)
//   0x15 | 5        | 1        (WRITE)
//   other| 1        | 1        (default per Dolphin source)

#include "app.h"
#include "ch9120.h"
#include "wshare_ch9120.h"  // Waveshare's official CH9120 driver, verbatim
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/host/gc/gc_host.h"
#include "native/host/gc/joybus_bridge.h"
#include "native/host/gc/gba_multiboot.h"
#include "usb/usbd/usbd.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Multiboot intercept-replay
//
// Pure passthrough is unworkable: each Dolphin WRITE→GBA→Dolphin roundtrip
// is at least joybus_xfer (~250µs) + TCP RTT, and Madden 2003's per-game
// timeout fires before 3000+ WRITEs can complete. So we intercept:
//
//   1. Forward the handshake (RESET, STATUS, READ session_key, WRITE our_key)
//      to the real GBA so we can snoop the session_key it generates.
//   2. After our_key, switch to CAPTURE mode. Buffer every WRITE locally,
//      decrypting the encrypted body (offsets >= 0xC0) to plaintext using
//      the snooped session_key. Reply with a fake "OK" jstat so Dolphin
//      keeps streaming at line rate (no joybus per WRITE).
//   3. When Dolphin sends READ (the post-upload crc_reply READ), capture
//      is done. Run native gba_mb_upload(plaintext) which resets the GBA
//      and pushes the ROM at the joybus-native ~750ms rate.
//   4. Return to passthrough so Dolphin's normal-play traffic reaches
//      the (now booted) GBA.
// ============================================================================

#define MAGIC_SEDO 0x6f646573u  // matches gba_multiboot.c
#define MAGIC_KAWA 0x6177614bu
#define MAGIC_BY   0x20796220u

typedef enum {
    MB_PASSTHROUGH = 0,    // forward everything to GBA
    MB_AWAIT_OUR_KEY,      // saw session_key READ, expect first WRITE next
    MB_CAPTURE,            // buffering Dolphin's WRITEs, faking acks
    MB_REPLAY_DONE,        // post-replay; pass everything through
} mb_state_t;

static mb_state_t s_mb_state    = MB_PASSTHROUGH;
static uint32_t   s_session_key = 0;       // snooped from GBA's READ response
static uint32_t   s_byte_offset = 0;       // i in the cipher formula
static int        s_capture_writes = 0;
static absolute_time_t s_last_capture_byte;
static bool       s_armed = false;         // STATUS confirmed BIOS multiboot wait

// USB device stack: include via outputs[] so main.c initializes it BEFORE
// app_init (at default 125MHz). app_init then changes clock to 130MHz +
// re-runs stdio_init_all to recompute baud divisor. Matches gc2usb's
// proven init order — earlier attempts that deferred USB init until
// AFTER the clock change failed to enumerate.
extern const OutputInterface usbd_output_interface;
static const OutputInterface* s_outputs[] = { &usbd_output_interface };

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = 0;
    return NULL;
}

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = 1;
    return s_outputs;
}

// Dolphin GBA joybus TCP port = 0xD6BA = "dolphin gba"
#define DOLPHIN_GBA_PORT  0xD6BA
// CH9120 UART data-mode baud. Matches Waveshare's official demo's
// Transport_BAUD_RATE constant.
#define CH9120_DATA_BAUD  115200

void app_init(void)
{
    // Joybus PIO timing is calibrated for 130 MHz (see gc2usb, CLAUDE.md).
    // At pico-sdk's default 125 MHz, joybus bit timing is off by ~4%,
    // which is fine for STATUS/RESET handshake (the GBA chimes) but
    // breaks the Kawasedo cipher + CRC step mid-upload. Must run BEFORE
    // any UART init so the baud divider gets the new clock.
    set_sys_clock_khz(130000, true);
    // Re-init stdio so UART baud divisor recomputes for the new sys_clk
    // (same fix gc2usb applies — without it stdio output garbles).
    stdio_init_all();

    printf("\n[app:gc2eth] %s v%s starting\n", APP_NAME, JOYPAD_VERSION);
    printf("[app:gc2eth]   board: %s\n", BOARD);
    printf("[app:gc2eth]   GC data pin: GPIO%d\n", GC_DATA_PIN);
    printf("[app:gc2eth]   CH9120: UART TX=GPIO%d RX=GPIO%d  "
           "TCPCS=GPIO%d CFG0=GPIO%d RST=GPIO%d\n",
           CH9120_UART_TX_PIN, CH9120_UART_RX_PIN,
           CH9120_TCPCS_PIN, CH9120_CFG0_PIN, CH9120_RSTI_PIN);

    // Joybus first — gc_host_init lays down the PIO program and
    // configures the data pin (GP_DATA = GP2 here).
    gc_host_init();

    joybus_bridge_init();
    if (!joybus_bridge_start()) {
        printf("[app:gc2eth] !! joybus_bridge_start failed (gc_host not ready?)\n");
    }

    // Use Waveshare's verbatim CH9120 driver — known good.
    CH9120_init();
    printf("[app:gc2eth] M3: Dolphin GBA bridge running\n");

}

// Look up tx total + rx length for a Dolphin joybus cmd byte, per the
// switch in CSIDevice_GBA::RunBuffer.
static void dolphin_cmd_lengths(uint8_t cmd, int* tx_total, int* rx_len)
{
    switch (cmd) {
        case 0xFF:  // RESET
        case 0x00:  // STATUS
            *tx_total = 1; *rx_len = 3; break;
        case 0x14:  // READ
            *tx_total = 1; *rx_len = 5; break;
        case 0x15:  // WRITE
            *tx_total = 5; *rx_len = 1; break;
        default:
            *tx_total = 1; *rx_len = 1; break;
    }
}

// Diagnostic counters — printed by the heartbeat so we can see whether
// the bridge is being driven at all without spamming a line per byte.
static volatile uint32_t g_frames_seen = 0;
static volatile uint8_t  g_last_cmd = 0;
static volatile uint8_t  g_last_rx[5] = {0};
static volatile int      g_last_n = 0;
static volatile uint32_t g_short_tx = 0;

void gc2eth_get_diag(gc2eth_diag_t* out)
{
    out->frames_seen = g_frames_seen;
    out->last_cmd    = g_last_cmd;
    out->last_n      = g_last_n;
    for (int i = 0; i < 5; i++) out->last_rx[i] = g_last_rx[i];
}

// Run the captured plaintext ROM through the native uploader. Blocks ~750ms.
// CRITICAL: trim last 4 bytes — encrypted fcrc handshake word, not ROM.
static volatile int s_last_replay_rc = 99;
static void run_native_replay(void)
{
    uint32_t bytes = joybus_bridge_mb_size();
    if (bytes >= 4) joybus_bridge_mb_trim(4);
    s_last_replay_rc = joybus_bridge_mb_upload(/*channel=*/0);
    s_mb_state = MB_REPLAY_DONE;
}

// Returns true if a Dolphin command was processed this call,
// false if no byte was pending. Caller can loop to drain.
static bool process_dolphin_frame_one(void)
{
    int cmd_byte = ch9120_recv_byte_timeout(0);
    if (cmd_byte < 0) return false;
    g_frames_seen++;
    g_last_cmd = (uint8_t)cmd_byte;

    uint8_t tx[5] = { (uint8_t)cmd_byte };
    int tx_total = 1, rx_len = 1;
    dolphin_cmd_lengths(tx[0], &tx_total, &rx_len);

    if (tx_total > 1) {
        int got = ch9120_recv_bytes(tx + 1, tx_total - 1, 5000);
        if (got != tx_total - 1) {
            g_short_tx++;
            return false;
        }
    }

    // RESET (0xFF) → restart the state machine. Many games skip RESET
    // when STATUS already shows PSF0 (BIOS multiboot wait), so we DON'T
    // require RESET to arm the intercept; we instead arm it from STATUS
    // responses below. RESET just resets state.
    if (tx[0] == 0xFF) {
        s_mb_state = MB_PASSTHROUGH;
        s_session_key = 0;
        s_byte_offset = 0;
        s_capture_writes = 0;
        s_armed = false;
        joybus_bridge_mb_reset();
    }

    // Intercept-replay enabled. Proven via tools/gba-bridge/eth-multiboot.js
    // — successfully boots embedded payload onto GBA through TCP+chip+joybus
    // stack. The chip's per-byte forwarding latency was the speed bottleneck
    // until PKT_LEN=1 + PKT_TIMEOUT=0 was set in wshare_ch9120.c.
    static const bool s_intercept_enabled = true;

    // ---- CAPTURE MODE: short-circuit WRITEs, fake acks ----
    if (s_intercept_enabled && s_mb_state == MB_CAPTURE) {
        if (tx[0] == 0x15) {
            // Buffer the WRITE. Header (offset < 0xC0) is plaintext as-is.
            // Body (offset >= 0xC0) is encrypted — decrypt to plaintext.
            uint32_t word = (uint32_t)tx[1]
                          | ((uint32_t)tx[2] << 8)
                          | ((uint32_t)tx[3] << 16)
                          | ((uint32_t)tx[4] << 24);
            uint32_t plaintext;
            if (s_byte_offset < 0xC0) {
                plaintext = word;
            } else {
                // Inverse of gba_mb_upload's encryption (line ~370):
                //   session_key = (session_key * MAGIC_KAWA) + 1
                //   encrypted = plaintext ^ session_key
                //             ^ ((~(i + 0x2000000)) + 1) ^ MAGIC_BY
                s_session_key = (s_session_key * MAGIC_KAWA) + 1;
                plaintext = word ^ s_session_key
                          ^ ((~(s_byte_offset + (0x20u << 20))) + 1u)
                          ^ MAGIC_BY;
            }
            uint8_t pt_bytes[4] = {
                (uint8_t)(plaintext),
                (uint8_t)(plaintext >> 8),
                (uint8_t)(plaintext >> 16),
                (uint8_t)(plaintext >> 24),
            };
            joybus_bridge_mb_append(pt_bytes, 4);
            s_byte_offset += 4;
            s_capture_writes++;
            s_last_capture_byte = get_absolute_time();

            // Reply with a clean jstat so Dolphin sends the next WRITE
            // immediately. JSTAT bits 0xC5 are the "error" mask; 0x00 is
            // a benign "GBA accepted, ready for next" response.
            uint8_t fake = 0x00;
            ch9120_send_bytes(&fake, 1);
            g_last_n = 1;
            g_last_rx[0] = fake;
            return true;
        }

        if (tx[0] == 0x00) {
            // STATUS poll during capture. Match the JS reference daemon's
            // jstat byte exactly: 0x12 = PSF0 (0x10) | RECV (0x02). Looks
            // like "BIOS multiboot ready, still draining your last WRITE"
            // — the heartbeat shape Madden's BIOS-state polling expects.
            uint8_t fake[3] = { 0x00, 0x04, 0x12 };
            ch9120_send_bytes(fake, 3);
            g_last_n = 3;
            for (int i = 0; i < 3; i++) g_last_rx[i] = fake[i];
            return true;
        }

        if (tx[0] == 0x14) {
            // READ during capture = post-upload crc_reply READ. Trigger
            // native replay; send fake zeros as crc_reply (per JS daemon).
            // No printf — would block UART0 during the protocol burst.
            run_native_replay();
            uint8_t fake[5] = {0};
            ch9120_send_bytes(fake, rx_len);
            g_last_n = rx_len;
            for (int i = 0; i < 5; i++) g_last_rx[i] = 0;
            return true;
        }
        // Other commands (rare): fall through to passthrough.
    }

    // ---- HANDSHAKE WATCHING + PASSTHROUGH ----
    uint8_t rx[5];
    int n = joybus_bridge_xfer(tx, (uint16_t)tx_total,
                               rx, (uint16_t)rx_len,
                               /*timeout_us=*/1000);
    if (n < 0) {
        for (int i = 0; i < rx_len; i++) rx[i] = 0;
        n = rx_len;
    } else {
        for (int i = n; i < rx_len; i++) rx[i] = 0;
    }
    ch9120_send_bytes(rx, rx_len);

    // After we forward each command, watch for state transitions.
    // Arm intercept whenever STATUS shows the GBA is in BIOS multiboot
    // wait (PSF0 bit set in jstat). This catches both:
    //   - Cold-boot path (Madden polls STATUS, gets PSF0, proceeds)
    //   - Post-RESET path (RESET handler reset state, then STATUS shows PSF0)
    // Without this, games that skip RESET (BIOS already in wait state)
    // never trigger our state machine and we just slow-passthrough until
    // their per-game timeout.
    if (s_intercept_enabled && s_mb_state == MB_PASSTHROUGH
        && (tx[0] == 0x00 || tx[0] == 0xFF) && n >= 3) {
        uint16_t type  = (uint16_t)rx[0] | ((uint16_t)rx[1] << 8);
        uint8_t  jstat = rx[2];
        if (type == 0x0400 && (jstat & 0x10)) {
            s_armed = true;
        }
    }

    if (s_intercept_enabled && s_armed && s_mb_state == MB_PASSTHROUGH
        && tx[0] == 0x14 && n >= 5) {
        // GBA's READ response in BIOS multiboot wait returns the session_key
        // seed in bytes [0..3] (LE). First non-zero seed transitions us
        // to AWAIT_OUR_KEY.
        uint32_t seed = (uint32_t)rx[0]
                      | ((uint32_t)rx[1] << 8)
                      | ((uint32_t)rx[2] << 16)
                      | ((uint32_t)rx[3] << 24);
        if (seed != 0) {
            s_session_key = seed ^ MAGIC_SEDO;
            s_mb_state = MB_AWAIT_OUR_KEY;
        }
    } else if (s_intercept_enabled && s_mb_state == MB_AWAIT_OUR_KEY && tx[0] == 0x15) {
        // First WRITE after session_key READ = our_key (already forwarded
        // above). Now switch to CAPTURE for subsequent WRITEs.
        s_mb_state = MB_CAPTURE;
        s_byte_offset = 0;
        s_capture_writes = 0;
        joybus_bridge_mb_reset();
        s_last_capture_byte = get_absolute_time();
    }

    g_last_n = n;
    for (int i = 0; i < 5; i++) g_last_rx[i] = (i < rx_len) ? rx[i] : 0;
    return true;
}

void app_task(void)
{
    // Reboot-to-bootloader on serial 'B'.
    int c = getchar_timeout_us(0);
    if (c == 'B') reset_usb_boot(0, 0);

    // Drain pending Dolphin commands. Loop because Dolphin may
    // burst multiple bytes per UART poll — single-byte-per-call
    // would let RX FIFO grow.
    while (process_dolphin_frame_one()) { /* keep draining */ }

    // 1 Hz heartbeat + TCPCS poll so we can see when Dolphin attaches.
    static absolute_time_t next_beat;
    static bool beat_init = false;
    static uint32_t tick = 0;
    static bool last_connected = false;
    if (!beat_init) {
        next_beat = make_timeout_time_ms(1000);
        beat_init = true;
    }
    if (time_reached(next_beat)) {
        next_beat = make_timeout_time_ms(1000);
        bool now = ch9120_is_connected();
        if (now != last_connected) {
            printf("[app:gc2eth] TCP peer %s\n", now ? "CONNECTED" : "disconnected");
            last_connected = now;
        }
        // Suppress per-tick spam while a peer is active. printf to UART0
        // stdio blocks for ms; during multiboot's 13K-WRITE burst those
        // ms cost RX bytes off UART1 (chip→RP2040), corrupting the
        // encrypted multiboot stream and hanging the GBA. Idle-only print.
        if (!now) {
            printf("[app:gc2eth] tick %lu idle frames=%lu short_tx=%lu "
                   "last_cmd=0x%02x last_n=%d last_rx=%02x%02x%02x%02x%02x\n",
                   (unsigned long)tick++,
                   (unsigned long)g_frames_seen,
                   (unsigned long)g_short_tx,
                   g_last_cmd, g_last_n,
                   g_last_rx[0], g_last_rx[1], g_last_rx[2], g_last_rx[3], g_last_rx[4]);
        }
    }
}
