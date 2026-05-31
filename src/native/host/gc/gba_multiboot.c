// gba_multiboot.c - Joybus multiboot uploader for GBA
//
// Implements the Kawasedo handshake + stream cipher that Nintendo's
// GameCube BIOS and libogc JOYBOOT() use to push a .gba ROM into a GBA
// over the GC↔GBA link cable. Reference: AxioDL/jbus Endpoint.cpp.
//
// Operates Core-0-only at controller init time. Not flash-resident
// hostile (CYW43 isn't running yet during gc_host init), so plain
// flash-resident code is fine.

#include "gba_multiboot.h"
#include "joybus.h"
#include "usb/usbd/usbd.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "pico/time.h"

// JOYSTAT bits — interpreted per Doridian/Joybus-PIO (proven on RP2040)
#define JSTAT_PSF1 0x20  // GBA's BIOS multiboot loader handshake bit
#define JSTAT_PSF0 0x10  // GBA's BIOS multiboot loader handshake bit
#define JSTAT_SEND 0x08  // GBA has data ready in JOY_TRANS for us to READ
#define JSTAT_RECV 0x02  // GBA hasn't drained our last WRITE yet — wait
// Bits that should never be set in a healthy multiboot exchange. If any of
// these are seen the GBA is in an error state (or it's not a GBA).
#define JSTAT_VALID_MASK 0xC5

// GBA Type ID returned in joybus probe (LE: 00 04)
#define GBA_TYPE_ID 0x0400

// Encryption / CRC magic constants (Kawasedo cipher)
#define MAGIC_SEDO 0x6f646573u  // "sedo"
#define MAGIC_KAWA 0x6177614bu  // "Kawa"
#define MAGIC_BY   0x20796220u  // " by "
#define CRC_POLY   0xa1c1u
#define CRC_SEED   0x15a0u

// Joybus is 250 kHz: ~5μs/bit, ~40μs/byte. The GBA's BIOS multiboot handler
// is slower than a GC controller — its SIO interrupt handler takes ~ms to
// drain JOY_RECV and update state. Bumped from 500µs to 1ms per byte after
// observing intermittent mid-stream timeouts at the original headroom.
// MUST apply to the first byte (first_byte_can_timeout=true) — without it,
// missing responses hang the main loop forever (joybus_send_receive uses
// false, which is unsafe for probe-style traffic).
#define JB_TIMEOUT_US 5000

// ============================================================================
// Joybus command primitives (one round-trip each)
//
// These bypass joybus_send_receive() because that helper sets
// first_byte_can_timeout=false — fine for known-present devices, fatal for
// probes since a missing response blocks the main loop indefinitely.
// ============================================================================

// Per-call debug ring; gba_mb_upload sets/clears the tag to label transfers.
static const char* jb_dbg_tag = NULL;

static int jb_xfer(joybus_port_t* p, uint8_t* msg, uint msg_len,
                   uint8_t* resp, uint resp_len)
{
    if (msg_len > 0) joybus_send_bytes(p, msg, msg_len);
    // Zero the response buffer so partial reads are visible as 00s rather
    // than uninitialised stack bytes pretending to be data.
    for (uint i = 0; i < resp_len; i++) resp[i] = 0;
    uint got = joybus_receive_bytes(p, resp, resp_len, JB_TIMEOUT_US, true);
    if (jb_dbg_tag && got != resp_len) {
        printf("[gba_mb] %s: tx[%u]=", jb_dbg_tag, msg_len);
        for (uint i = 0; i < msg_len && i < 8; i++) printf("%02x ", msg[i]);
        printf("rx %u/%u: ", got, resp_len);
        for (uint i = 0; i < resp_len && i < 8; i++) printf("%02x ", resp[i]);
        printf("\n");
    }
    return (got == resp_len) ? 0 : -1;
}

static int jb_status(joybus_port_t* p, uint8_t cmd, uint8_t r3[3])
{
    return jb_xfer(p, &cmd, 1, r3, 3);
}

static int jb_read4(joybus_port_t* p, uint8_t r5[5])
{
    uint8_t cmd = 0x14;
    return jb_xfer(p, &cmd, 1, r5, 5);
}

static int jb_write4(joybus_port_t* p, uint32_t w, uint8_t* joystat)
{
    uint8_t cmd[5] = { 0x15,
                       (uint8_t)(w),
                       (uint8_t)(w >> 8),
                       (uint8_t)(w >> 16),
                       (uint8_t)(w >> 24) };
    return jb_xfer(p, cmd, 5, joystat, 1);
}

// ============================================================================
// Polled WRITE / READ — Doridian/Joybus-PIO style
//
// The cable's level-shifter MCU translates joybus to/from the GBA's SIO. It
// can only accept the next WRITE once the GBA has drained the previous one
// (JSTAT.RECV clear). Likewise a READ only returns valid data once the GBA
// has filled JOY_TRANS (JSTAT.SEND set). Sending blind, as the original
// stash did, races the cable+BIOS and stalls partway through the stream.
// ============================================================================

#define GBA_DELAY_US 70
#define GBA_POLL_TIMEOUT_MS 1000

// Send STATUS (or RESET if reset=true), returning the 16-bit type and the
// JSTAT byte. Returns true on success, false on bus timeout.
static bool gba_handshake(joybus_port_t* port, bool reset,
                          uint16_t* out_type, uint8_t* out_jstat)
{
    uint8_t cmd = reset ? 0xFF : 0x00;
    uint8_t r3[3];
    if (jb_xfer(port, &cmd, 1, r3, 3) < 0) return false;
    *out_type  = (uint16_t)r3[0] | ((uint16_t)r3[1] << 8);
    *out_jstat = r3[2];
    return true;
}

// Poll until the GBA is ready to accept a WRITE: JSTAT.RECV clear, no error
// bits set, type still 0x0400. Returns 0 on ready, negative on error.
static int gba_wait_write_ready(joybus_port_t* port)
{
    absolute_time_t deadline = make_timeout_time_ms(GBA_POLL_TIMEOUT_MS);
    while (1) {
        uint16_t type; uint8_t js;
        if (!gba_handshake(port, false, &type, &js)) return -1;
        if (type != GBA_TYPE_ID)        return -2;
        if (js & JSTAT_VALID_MASK)      return -3;
        if (!(js & JSTAT_RECV))         return 0;  // ready
        if (time_reached(deadline))     return -4;
        sleep_us(GBA_DELAY_US);
    }
}

// Poll until the GBA has data ready: JSTAT.SEND set.
static int gba_wait_read_ready(joybus_port_t* port)
{
    absolute_time_t deadline = make_timeout_time_ms(GBA_POLL_TIMEOUT_MS);
    while (1) {
        uint16_t type; uint8_t js;
        if (!gba_handshake(port, false, &type, &js)) return -1;
        if (type != GBA_TYPE_ID)        return -2;
        if (js & JSTAT_VALID_MASK)      return -3;
        if (js & JSTAT_SEND)            return 0;  // ready
        if (time_reached(deadline))     return -4;
        sleep_us(GBA_DELAY_US);
    }
}

// Poll-then-WRITE 4 bytes. Returns 0 on success, negative on error.
static int gba_write(joybus_port_t* port, uint32_t word)
{
    int e = gba_wait_write_ready(port);
    if (e < 0) return e;
    uint8_t js;
    if (jb_write4(port, word, &js) < 0) return -10;
    if (js & JSTAT_VALID_MASK)          return -11;
    return 0;
}

// Poll-then-READ 4 bytes. Returns 0 on success, negative on error.
static int gba_read(joybus_port_t* port, uint32_t* out_word)
{
    int e = gba_wait_read_ready(port);
    if (e < 0) return e;
    uint8_t r5[5];
    if (jb_read4(port, r5) < 0) return -20;
    if (r5[4] & JSTAT_VALID_MASK) return -21;
    *out_word = (uint32_t)r5[0]
              | ((uint32_t)r5[1] << 8)
              | ((uint32_t)r5[2] << 16)
              | ((uint32_t)r5[3] << 24);
    return 0;
}

// ============================================================================
// Kawasedo session-key/auth-init derivation — Doridian's version
// (palette=3, speed=0 hardcoded into the 0x380000 constant)
// ============================================================================

static uint32_t calculate_gc_key(uint32_t rom_len)
{
    uint32_t size = (rom_len - 0x200) >> 3;
    uint32_t res1 = (size & 0x3F80) << 1;
    res1 |= (size & 0x4000) << 2;
    res1 |= (size & 0x7F);
    res1 |= 0x380000;
    uint32_t res2 = res1 >> 8;
    res2 += res1 >> 16;
    res2 += res1;
    res2 <<= 24;
    res2 |= res1;
    res2 |= 0x80808080u;
    if ((res2 & 0x200) == 0) res2 ^= MAGIC_KAWA;
    else                     res2 ^= MAGIC_SEDO;
    return res2;
}

static uint32_t gba_crc_step(uint32_t crc, uint32_t value)
{
    for (int i = 0; i < 32; i++) {
        if ((crc ^ value) & 1) crc = (crc >> 1) ^ CRC_POLY;
        else                   crc = (crc >> 1);
        value >>= 1;
    }
    return crc;
}

// ============================================================================
// Public API
// ============================================================================

bool gba_mb_detect(joybus_port_t* port)
{
    uint8_t cmd = 0x00;
    uint8_t r[3];
    if (jb_xfer(port, &cmd, 1, r, 3) < 0) return false;
    uint16_t id = (uint16_t)r[0] | ((uint16_t)r[1] << 8);
    return id == GBA_TYPE_ID;
}

bool gba_mb_in_multiboot_wait(joybus_port_t* port)
{
    // STATUS handshake; expect GBA type + JSTAT.PSF0 set (BIOS multiboot
    // ready). Distinguishes a freshly-power-cycled GBA in BIOS wait state
    // from a GBA already running a payload (joypad / hello / etc), which
    // also responds with the GBA type but does NOT set PSF0.
    uint8_t cmd = 0x00;
    uint8_t r[3];
    if (jb_xfer(port, &cmd, 1, r, 3) < 0) return false;
    uint16_t id = (uint16_t)r[0] | ((uint16_t)r[1] << 8);
    if (id != GBA_TYPE_ID) return false;
    return (r[2] & JSTAT_PSF0) != 0;
}

int gba_send_splash_cmd(joybus_port_t* port, uint8_t mode_id)
{
    // Magic 0xCAFE55XX where XX is the mode ID. GBA payload edge-
    // triggers on REG_JOYRE matching this prefix. Sent as a single
    // joybus WRITE (0x15) — the GBA's JOY_RECV register latches the
    // 4-byte word and the payload reads it next frame.
    uint32_t word = 0xCAFE5500u | (uint32_t)mode_id;
    uint8_t  jstat;
    return jb_write4(port, word, &jstat);
}

bool gba_mb_payload_already_running(joybus_port_t* port)
{
    // The cable's level-shifter MCU caches state across firmware
    // restarts and the FIRST joybus xfer after our PIO init often
    // returns stale data. Retry a few times before declaring
    // "no payload running" so we don't spuriously fire the heavy
    // multiboot upload after a firmware-only reboot.
    //
    // Polarity check: GBA type + PSF0 cleared means it's PAST the
    // BIOS multiboot stage. A successful JOY-bus READ confirms it
    // responds in JOY mode.
    extern void gba_mb_detect_log_reset(void);
    extern void gba_mb_detect_log_printf(const char* fmt, ...);
    gba_mb_detect_log_reset();
    gba_mb_detect_log_printf("starting payload-running probe\n");
    for (int attempt = 0; attempt < 4; attempt++) {
        uint8_t cmd = 0x00;
        uint8_t r[3] = {0};
        int rc = jb_xfer(port, &cmd, 1, r, 3);
        gba_mb_detect_log_printf("[%d] STATUS rc=%d id=%02x%02x jstat=%02x\n",
                                 attempt, rc, r[1], r[0], r[2]);
        if (rc >= 0) {
            uint16_t id = (uint16_t)r[0] | ((uint16_t)r[1] << 8);
            if (id == GBA_TYPE_ID && !(r[2] & JSTAT_PSF0)) {
                // Confirm via READ — needs its own small retry loop
                // because READ also tends to glitch immediately after
                // a fresh PIO setup.
                for (int rd = 0; rd < 3; rd++) {
                    uint8_t buf[4];
                    int rrc = gba_input_read(port, buf);
                    gba_mb_detect_log_printf("  READ[%d] rc=%d\n", rd, rrc);
                    if (rrc >= 0) {
                        gba_mb_detect_log_printf("→ RUNNING\n");
                        return true;
                    }
                    sleep_ms(2);
                }
            }
        }
        sleep_ms(5);
    }
    gba_mb_detect_log_printf("→ not running, will multiboot\n");
    return false;
}

// ──────────────────────────────────────────────────────────────────
// Detect probe log buffer — printf alone goes to UART (not visible
// over the USB CDC port in default kb2040 builds). Capture probe
// traces into a static buffer that the GBADETECT CDC command dumps
// alongside the bool result.
// ──────────────────────────────────────────────────────────────────
#include <stdarg.h>
#define DETECT_LOG_CAP 2048
static char     s_detect_log[DETECT_LOG_CAP];
static uint16_t s_detect_log_len = 0;

void gba_mb_detect_log_reset(void)
{
    s_detect_log_len = 0;
    s_detect_log[0]  = '\0';
}

void gba_mb_detect_log_printf(const char* fmt, ...)
{
    if (s_detect_log_len >= DETECT_LOG_CAP - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s_detect_log + s_detect_log_len,
                      DETECT_LOG_CAP - s_detect_log_len, fmt, ap);
    va_end(ap);
    if (n > 0) s_detect_log_len += (uint16_t)n;
    if (s_detect_log_len >= DETECT_LOG_CAP) s_detect_log_len = DETECT_LOG_CAP - 1;
}

const char* gba_mb_detect_log_get(void) { return s_detect_log; }

gba_mb_result_t gba_mb_upload(joybus_port_t* port,
                              const uint8_t* rom, uint32_t len,
                              int palette, int speed, int channel)
{
    (void)palette; (void)speed;  // Doridian's calculate_gc_key hardcodes 3/0
    if (!rom || len == 0 || len >= 0x40000) return GBA_MB_ERR_PARAMS;
    if (rom[0xac] == 0)                     return GBA_MB_ERR_PARAMS;

    // Scan the rom for the GBA-side mode marker "JPMD\xff\0\0\0". If
    // found, we'll substitute the current USB output mode into the
    // mode byte (offset +4) during body streaming so the splash on the
    // GBA can render the matching badge. Not found = older payload
    // without splash support; skip silently.
    int32_t marker_off = -1;
    {
        for (uint32_t k = 0; k + 8 <= len; k++) {
            if (rom[k] == 'J' && rom[k + 1] == 'P' &&
                rom[k + 2] == 'M' && rom[k + 3] == 'D') {
                marker_off = (int32_t)k;
                break;
            }
        }
    }
    const uint8_t mode_byte = (marker_off >= 0)
                              ? (uint8_t)usbd_get_mode() : 0xFF;
    if (marker_off >= 0) {
        printf("[gba_mb] mode marker at offset 0x%x → patching mode=%d\n",
               (unsigned)marker_off, (int)mode_byte);
    }

    uint16_t type;
    uint8_t  js;

    // Step 1: RESET handshake. The cable's level-shifter MCU can be
    // wedged after a firmware reboot (it survives our reset on USB
    // power and may still be in mid-transaction state from before).
    // A single RESET often gets dropped on the floor; retry a few
    // times with increasing delays to give it a chance to drain its
    // state machine before we declare PROBE failure.
    bool reset_ok = false;
    for (int retry = 0; retry < 5; retry++) {
        sleep_ms(20 + retry * 30);  // 20, 50, 80, 110, 140 ms
        if (gba_handshake(port, true, &type, &js) && type == GBA_TYPE_ID) {
            reset_ok = true;
            break;
        }
    }
    if (!reset_ok) return GBA_MB_ERR_PROBE;
    sleep_ms(10);
    if (!gba_handshake(port, false, &type, &js))  return GBA_MB_ERR_RESET;
    if (type != GBA_TYPE_ID)                       return GBA_MB_ERR_RESET;
    if (js & JSTAT_VALID_MASK)                     return GBA_MB_ERR_RESET;
    if (!(js & JSTAT_PSF0))                        return GBA_MB_ERR_RESET;
    printf("[gba_mb] handshake ok: js=%02x\n", js);

    sleep_us(GBA_DELAY_US);

    // Step 2: READ the GBA's session-key seed directly. We already know SEND
    // is set from the handshake above — running gba_read's pre-poll causes
    // the GBA to stop responding (re-issuing STATUS right after our STATUS
    // seems to wedge it). Use jb_read4 directly.
    uint32_t session_key = 0;
    for (int rt = 0; rt < 20; rt++) {
        uint8_t r5[5];
        if (jb_read4(port, r5) < 0) {
            printf("[gba_mb] session_key read timeout at retry %d\n", rt);
            return GBA_MB_ERR_RESET;
        }
        if (r5[4] & JSTAT_VALID_MASK) {
            printf("[gba_mb] session_key read bad jstat=%02x at retry %d\n", r5[4], rt);
            return GBA_MB_ERR_RESET;
        }
        session_key = (uint32_t)r5[0]
                    | ((uint32_t)r5[1] << 8)
                    | ((uint32_t)r5[2] << 16)
                    | ((uint32_t)r5[3] << 24);
        if (session_key != 0) {
            printf("[gba_mb] session_key seed = %08lx (retry %d, jstat=%02x)\n",
                   (unsigned long)session_key, rt, r5[4]);
            break;
        }
        sleep_ms(5);
    }
    if (session_key == 0) {
        printf("[gba_mb] session_key stayed 0 after retries\n");
        return GBA_MB_ERR_RESET;
    }
    sleep_ms(2);  // longer settle before first WRITE — GBA's BIOS needs to
                  // transition from "we just sent challenge" to "ready to receive"
    session_key ^= MAGIC_SEDO;

    // Step 3: WRITE our_key (auth_init derived from rom_len). Send directly
    // without pre-poll — same trick as the READ above; doing STATUS right
    // after READ wedges the bus on this GBA/cable combo.
    uint32_t our_key = calculate_gc_key(len);
    printf("[gba_mb] our_key = %08lx\n", (unsigned long)our_key);
    if (jb_write4(port, our_key, &js) < 0) {
        printf("[gba_mb] our_key WRITE timeout\n");
        return GBA_MB_ERR_TRANSFER;
    }
    if (js & JSTAT_VALID_MASK) {
        printf("[gba_mb] our_key WRITE bad jstat=%02x\n", js);
        return GBA_MB_ERR_TRANSFER;
    }
    printf("[gba_mb] our_key WRITE ok js=%02x\n", js);

    // Inline write helper. On WRITE timeout (lost ack), peek JSTAT via STATUS
    // to determine whether the GBA actually processed our data. If JSTAT.RECV
    // is set, the GBA already drained JOY_RECV — proceed. Otherwise retry the
    // WRITE once. NO printf during streaming (UART jitter affects PIO timing).
    #define GBA_INLINE_WRITE(_word, _label)                                    \
        do {                                                                   \
            sleep_us(GBA_DELAY_US);                                            \
            if (jb_write4(port, (_word), &js) < 0) {                           \
                uint8_t _r3[3];                                                \
                if (jb_status(port, 0x00, _r3) >= 0 && (_r3[2] & JSTAT_RECV)) { \
                    js = _r3[2];                                               \
                } else {                                                       \
                    sleep_us(GBA_DELAY_US);                                    \
                    if (jb_write4(port, (_word), &js) < 0) {                   \
                        printf("[gba_mb] %s WRITE failed at i=%lu\n",          \
                               (_label), (unsigned long)__i_label);            \
                        return GBA_MB_ERR_TRANSFER;                            \
                    }                                                          \
                }                                                              \
            }                                                                  \
            if (js & JSTAT_VALID_MASK) {                                       \
                printf("[gba_mb] %s WRITE bad jstat=%02x at i=%lu\n",          \
                       (_label), js, (unsigned long)__i_label);                \
                return GBA_MB_ERR_TRANSFER;                                    \
            }                                                                  \
        } while (0)

    // Step 4: stream the unencrypted header (offsets 0..0xBF) — NO per-byte
    // printf during streaming. Each printf is ~3ms of UART TX which can
    // jitter PIO timing and cause individual writes to drop off the wire.
    {
        uint32_t __i_label;
        for (uint32_t i = 0; i < 0xC0; i += 4) {
            __i_label = i;
            uint32_t word = (uint32_t)rom[i]
                          | ((uint32_t)rom[i + 1] << 8)
                          | ((uint32_t)rom[i + 2] << 16)
                          | ((uint32_t)rom[i + 3] << 24);
            GBA_INLINE_WRITE(word, "header");
        }
    }

    // Step 5: stream the encrypted body (0xC0..rom_len)
    uint32_t fcrc = CRC_SEED;
    uint32_t i;
    {
        uint32_t __i_label;
        for (i = 0xC0; i < len; i += 4) {
            __i_label = i;
            uint32_t plaintext = (uint32_t)rom[i]
                               | ((uint32_t)rom[i + 1] << 8)
                               | ((uint32_t)rom[i + 2] << 16)
                               | ((uint32_t)rom[i + 3] << 24);
            // Channel ID at offset 0xC4
            if (i == 0xC4) plaintext = ((uint32_t)(channel & 0xff)) << 8;

            // Mode-marker substitution. The marker byte is at
            // (marker_off + 4); replace its position in the plaintext
            // word before CRC + encryption so both host CRC and GBA
            // RAM see the patched value.
            if (marker_off >= 0) {
                uint32_t mb_pos = (uint32_t)marker_off + 4;
                if (mb_pos >= i && mb_pos < i + 4) {
                    uint32_t shift = (mb_pos - i) * 8;
                    plaintext = (plaintext & ~(0xFFu << shift))
                              | ((uint32_t)mode_byte << shift);
                }
            }

            fcrc = gba_crc_step(fcrc, plaintext);
            session_key = (session_key * MAGIC_KAWA) + 1;
            uint32_t encrypted = plaintext ^ session_key;
            encrypted ^= ((~(i + (0x20u << 20))) + 1u);
            encrypted ^= MAGIC_BY;

            GBA_INLINE_WRITE(encrypted, "body");

            // Pump USB device task every 256 bytes (64 words) to prevent
            // panic from EP-claim-while-stack-starved during the long upload.
            // No-op for builds without a USB device (e.g. gc2dc, native input
            // + native output, no usbd to pump).
#ifdef CONFIG_USB
            if ((i & 0xFF) == 0) usbd_task();
#endif
        }
    }

    // Step 6: final CRC word (encrypted with next session_key step)
    {
        uint32_t __i_label = i;
        uint32_t final_word = (fcrc & 0xFFFF) | (len << 16);
        session_key = (session_key * MAGIC_KAWA) + 1;
        final_word ^= session_key;
        final_word ^= ((~(i + (0x20u << 20))) + 1u);
        final_word ^= MAGIC_BY;
        GBA_INLINE_WRITE(final_word, "fcrc");
        printf("[gba_mb] final fcrc=%04lx js=%02x\n",
               (unsigned long)(fcrc & 0xFFFF), js);
    }

    // Step 7: READ the CRC reply (the BIOS's final CRC). Doridian doesn't
    // echo this back — the value is just discarded; the next default
    // handshake completes the boot.
    sleep_us(GBA_DELAY_US);
    {
        uint8_t r5[5];
        if (jb_read4(port, r5) < 0) {
            printf("[gba_mb] crc_reply READ timeout\n");
            return GBA_MB_ERR_FINALIZE;
        }
        printf("[gba_mb] crc_reply bytes=%02x %02x %02x %02x js=%02x\n",
               r5[0], r5[1], r5[2], r5[3], r5[4]);
    }


    // Step 8: default handshake — poll for payload's game-code write.
    // Generous timeout (consoleDemoInit / VBlank-wait take time).
    sleep_ms(500);
    {
        uint32_t expected = (uint32_t)rom[0xAC]
                          | ((uint32_t)rom[0xAD] << 8)
                          | ((uint32_t)rom[0xAE] << 16)
                          | ((uint32_t)rom[0xAF] << 24);
        bool got_payload = false;
        uint8_t r5[5] = {0};
        for (int p = 0; p < 200; p++) {  // 200 polls × 10ms = 2s
            if (jb_read4(port, r5) < 0) {
                sleep_ms(10);
                continue;
            }
            uint32_t v = (uint32_t)r5[0]
                       | ((uint32_t)r5[1] << 8)
                       | ((uint32_t)r5[2] << 16)
                       | ((uint32_t)r5[3] << 24);
            if (v == expected) {
                printf("[gba_mb] handshake-read got payload code at poll %d (js=%02x)\n",
                       p, r5[4]);
                got_payload = true;
                break;
            }
            if (p < 3 || (p % 50) == 0) {
                printf("[gba_mb] handshake-read poll %d: %02x %02x %02x %02x js=%02x\n",
                       p, r5[0], r5[1], r5[2], r5[3], r5[4]);
            }
            sleep_ms(10);
        }
        if (!got_payload) {
            printf("[gba_mb] payload code never appeared — proceeding anyway\n");
        }
        sleep_us(GBA_DELAY_US);
        if (jb_write4(port, expected, &js) < 0) {
            printf("[gba_mb] default-handshake WRITE timeout\n");
            return GBA_MB_ERR_FINALIZE;
        }
        printf("[gba_mb] handshake-write code=%08lx js=%02x\n",
               (unsigned long)expected, js);
    }

    return GBA_MB_OK;
}

gba_mb_result_t gba_mb_boot_embedded(joybus_port_t* port, int channel)
{
    if (gba_payload_len == 0)  return GBA_MB_ERR_NO_PAYLOAD;
    if (!gba_mb_detect(port))  return GBA_MB_ERR_PROBE;
    return gba_mb_upload(port, gba_payload, gba_payload_len,
                         /*palette*/3, /*speed*/0, channel);
}

int gba_input_read(joybus_port_t* port, uint8_t out[4])
{
    // No STATUS pre-poll: the GBA payload's tight-loop input doesn't
    // service the SIO interrupt that would update STATUS responses.
    // But READ (0x14) goes directly to JOY_TRANS hardware which is
    // updated by the payload's STR writes.
    uint8_t r5[5];
    if (jb_read4(port, r5) < 0)        return -1;
    if (r5[4] & JSTAT_VALID_MASK)      return -2;
    out[0] = r5[0];
    out[1] = r5[1];
    out[2] = r5[2];
    out[3] = r5[3];
    return 0;
}
