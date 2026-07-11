// ps4_local_auth.c - Local RSA-PSS signing for PS4 authentication
//
// Uses mbedTLS RSA-2048 with PSS padding (SHA-256) to sign the PS4's
// 280-byte nonce challenge. Key material is loaded from a dedicated flash
// sector at startup via ps4_auth_flash.h.
//
// Signing is offloaded to Core 1 via core1_idle_hook() to avoid blocking
// Core 0's USB/CYW43 polling for the ~3.4 seconds the RSA operation takes.
// Core 0's ps4_local_auth_task() sets s_core1_signing and calls __sev()
// to wake Core 1, then returns immediately.  Core 1 signs, sets
// s_signature_ready, and idles until the next nonce arrives.
//
// Flash writes (event log) are only performed by Core 0 — never Core 1 —
// because flash_safe_execute() requires Core 0 to be the initiator.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "ps4_local_auth.h"
#include "core/services/storage/ps4_auth_flash.h"
#include "core/services/storage/ps4_event_log.h"
#include "core/services/storage/flash.h"

#include "platform/platform.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"
#include "mbedtls/bignum.h"
#include "mbedtls/md.h"
#include "pico/rand.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/structs/vreg_and_chip_reset.h"
#include "hardware/structs/watchdog.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// ============================================================================
// LOGGING
// ============================================================================

static bool s_log_enabled = false;

// Conditional log — only writes to flash event log when enabled via settings
static inline void ps4_log(const char *msg)
{
    if (s_log_enabled) ps4_log(msg);
}

// ============================================================================
// CONSTANTS
// ============================================================================

#define NONCE_SIZE          280   // 5 pages × 56 bytes
#define SIG_RESPONSE_SIZE  1064   // 19 pages × 56 bytes
#define PAGE_SIZE            56   // bytes per auth page
#define NONCE_PAGES           5
#define SIG_PAGES            19

// Auth response buffer layout
#define RSP_RSA_SIG_OFFSET    0    // 256 bytes: RSA-PSS signature of SHA256(nonce)
#define RSP_SERIAL_OFFSET   256    // 16 bytes:  device serial
#define RSP_N_OFFSET        272    // 256 bytes: public modulus N
#define RSP_E_OFFSET        528    // 256 bytes: public exponent E (zero-padded)
#define RSP_DEVICE_SIG_OFFSET 784  // 256 bytes: Sony device signature (sig.bin)
#define RSP_PAD_OFFSET     1040    // 24 bytes:  zeros

// ============================================================================
// STATE
// ============================================================================

static mbedtls_rsa_context  s_rsa;
static bool                 s_rsa_valid = false;

// Nonce accumulation (Core 0 only)
static uint8_t  s_nonce[NONCE_SIZE];
static uint8_t  s_nonce_pages_received = 0;
static uint8_t  s_nonce_id = 0;

// Nonce snapshot handed to Core 1 (copied by Core 0 before kicking off signing,
// so that a new nonce arriving on Core 0 can't corrupt in-progress signing)
static uint8_t  s_sign_nonce[NONCE_SIZE];

// Signing state — volatile because they cross the Core 0 / Core 1 boundary
static volatile bool s_signing_requested = false;  // Core 0: nonce ready, needs sign
static volatile bool s_core1_signing     = false;  // Core 0→1: sign in progress
static volatile bool s_signature_ready   = false;  // Core 1→0: sign complete
static volatile int  s_sign_ret          = 0;       // Core 1→0: mbedtls return code

// Response buffer (1064 bytes) — written by Core 1, read by Core 0 after s_signature_ready
static uint8_t  s_response[SIG_RESPONSE_SIZE];

// Page cursor (for sequential GET_REPORT 0xF1 calls from console, Core 0 only)
static uint8_t  s_page_cursor = 0;

// Signing timeout tracker (Core 0 only)
static uint32_t s_sign_start_ms = 0;

// Cached flash data for response assembly
static uint8_t  s_serial[16];
static uint8_t  s_device_sig[256];

// ============================================================================
// CRC32 (standard IEEE 802.3, poly 0xEDB88320)
// Used to append integrity checks to 0xF1 and 0xF2 responses, matching the
// PS4 protocol as implemented in GP2040-CE.
// ============================================================================

static uint32_t ps4_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else         crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

// ============================================================================
// RNG — xorshift128+ PRNG seeded from hardware once at init.
//
// get_rand_32() (pico_rand) samples the ROSC via sleep_until() and is
// safe at startup but can block indefinitely after ~8 minutes when the
// CYW43/BT interrupt timing drifts — causing RSA signing to hang.
//
// The PRNG is seeded with two get_rand_64() calls during init (plenty of
// hardware entropy), then generates bytes purely from arithmetic.  PSS
// blinding and salt bytes don't require NIST-grade entropy; the PS4 only
// validates the final RSA-PSS signature.
// ============================================================================

static uint64_t s_prng_s0 = 0;
static uint64_t s_prng_s1 = 0;

static void prng_seed(void)
{
    // Called once from ps4_local_auth_init() — safe to call get_rand_64 here.
    s_prng_s0 = get_rand_64();
    s_prng_s1 = get_rand_64();
    // Ensure non-zero state (xorshift128+ is undefined for all-zero state).
    if (!s_prng_s0 && !s_prng_s1) {
        s_prng_s0 = 0xDEADBEEFCAFEBABEULL;
        s_prng_s1 = 0xC0FFEE1234567890ULL;
    }
}

// xorshift128+ — period 2^128 - 1, passes BigCrush.
static inline uint64_t prng_next(void)
{
    uint64_t s1 = s_prng_s0;
    const uint64_t s0 = s_prng_s1;
    s_prng_s0 = s0;
    s1 ^= s1 << 23;
    s_prng_s1 = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5);
    return s_prng_s1 + s0;
}

static int rng_fn(void *ctx, unsigned char *output, size_t len)
{
    (void)ctx;
    size_t i = 0;
    while (i < len) {
        uint64_t r = prng_next();
        size_t chunk = (len - i < 8) ? (len - i) : 8;
        memcpy(output + i, &r, chunk);
        i += chunk;
    }
    return 0;
}

// ============================================================================
// CORE 1 SIGNING
//
// ps4_do_sign() runs on Core 1 via core1_idle_hook().  It must NOT:
//   - Call ps4_log() or any flash API (flash_safe_execute must
//     be initiated by Core 0 while Core 1 handles the lockout interrupt).
//   - Access s_nonce directly (use s_sign_nonce, the snapshot made by Core 0).
// ============================================================================

// SRAM-based crash detection — survives PSM (LOCKUP) resets, not power-on resets.
// PSM reset only resets CPU register state; SRAM contents are preserved.
// Startup code only zeros .bss (__bss_start__ … __bss_end__); .uninitialized_data
// is never touched by crt0, so it retains its value across warm/PSM resets.
//
// Use a magic number to distinguish "valid crash breadcrumb left by last run"
// from "random SRAM contents on power-on boot".
#define CRASH_SRAM_MAGIC 0xC0DE1234u
typedef struct {
    uint32_t magic;
    uint32_t step;
} crash_detect_t;
static crash_detect_t s_crash_detect
    __attribute__((section(".uninitialized_data")));

// Watchdog scratch[6] breadcrumbs — may be cleared by PSM reset on some RP2040 revisions.
// Kept for reference; SRAM breadcrumbs above are more reliable.
#define SIGN_SCRATCH watchdog_hw->scratch[6]

static void ps4_do_sign(void)
{
    // Step 1: SHA-256 of the first 256 bytes of the nonce snapshot
    printf("[ps4_sign C1] step1 SHA256 start\n");
    uint8_t hash[32];
    SIGN_SCRATCH = 1;
    s_crash_detect.step = 1;
    mbedtls_sha256(s_sign_nonce, 256, hash, 0);
    printf("[ps4_sign C1] step1 SHA256 done\n");

    // Step 2: RSA-PSS sign (result 256 bytes)
    printf("[ps4_sign C1] step2 RSA-PSS start\n");
    uint8_t rsa_sig[256];
    SIGN_SCRATCH = 2;
    s_crash_detect.step = 2;
    int ret = mbedtls_rsa_rsassa_pss_sign(
        &s_rsa,
        rng_fn,
        NULL,
        MBEDTLS_MD_SHA256,
        sizeof(hash),
        hash,
        rsa_sig
    );

    printf("[ps4_sign C1] step2 RSA-PSS ret=%d\n", ret);
    SIGN_SCRATCH = 3;
    s_crash_detect.step = 3;

    if (ret == 0) {
        // Step 3: Assemble the 1064-byte response buffer
        printf("[ps4_sign C1] step3 assemble response\n");
        memset(s_response, 0, sizeof(s_response));
        memcpy(s_response + RSP_RSA_SIG_OFFSET,    rsa_sig,    256);
        memcpy(s_response + RSP_SERIAL_OFFSET,     s_serial,    16);
        mbedtls_rsa_export_raw(&s_rsa,
            s_response + RSP_N_OFFSET, 256,
            NULL, 0, NULL, 0, NULL, 0,
            s_response + RSP_E_OFFSET, 256);
        memcpy(s_response + RSP_DEVICE_SIG_OFFSET, s_device_sig, 256);
        printf("[ps4_sign C1] step3 done\n");
    } else {
        // On failure, return a zeroed buffer — PS4 will reject but won't hang
        printf("[ps4_sign C1] FAIL ret=%d\n", ret);
        memset(s_response, 0, sizeof(s_response));
    }

    s_sign_ret    = ret;
    s_page_cursor = 0;
    SIGN_SCRATCH  = 0;             // Clear watchdog scratch breadcrumb
    s_crash_detect.step = 0;       // Clear SRAM breadcrumb — signing completed normally
    __dmb();                       // Ensure response + sign_ret visible before flag
    s_signature_ready = true;      // Signal Core 0 (atomic store, Cortex-M0+ is TSO)
    printf("[ps4_sign C1] signature_ready=true\n");
}

// Override of the weak core1_idle_hook() in main.c.
// Called from Core 1's idle loop after waking from __wfe().
// When PS4 auth is not in use (no s_core1_signing), returns immediately.
void core1_idle_hook(void)
{
    // Only run when Core 0 has queued a signing request and it's not done yet
    if (!s_core1_signing || s_signature_ready || !s_rsa_valid) return;
    ps4_do_sign();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool ps4_local_auth_init(void)
{
    // Load log setting from flash (before any log calls)
    {
        flash_t flash_data;
        if (flash_load(&flash_data)) {
            s_log_enabled = (flash_data.ps4_auth_log == 1);
        }
    }

    // ---- Reset-source diagnostics ----
    // Log what caused the last reset to understand crash type.
    {
        bool wdog = watchdog_caused_reboot();
        bool wdog_en = watchdog_enable_caused_reboot();
        uint32_t chip_reset = vreg_and_chip_reset_hw->chip_reset;
        // RP2040 bits: [20]=HAD_PSM_RESTART, [16]=HAD_RUN, [8]=HAD_POR
        bool had_por  = (chip_reset >>  8) & 1;
        bool had_psm  = (chip_reset >> 20) & 1;
        bool had_run  = (chip_reset >> 16) & 1;
        char rst[48];
        snprintf(rst, sizeof(rst), "RST por=%d psm=%d run=%d wd=%d",
                 had_por, had_psm, had_run, wdog);
        ps4_log(rst);
        printf("[ps4_auth] %s wdog_en=%d chip_reset=0x%08lx\n",
               rst, wdog_en, (unsigned long)chip_reset);
    }

    // Log crash location from previous boot.
    //
    // SRAM breadcrumb (.uninitialized_data) — reliable across PSM/LOCKUP resets
    // because crt0 never zeroes that section. Only cleared by power-on reset.
    // step: 1=before sha256, 2=before rsa, 3=rsa returned but crashed in assembly
    {
        uint32_t sram_magic = s_crash_detect.magic;
        uint32_t sram_step  = (sram_magic == CRASH_SRAM_MAGIC) ? s_crash_detect.step : 0;
        char msg[48];
        snprintf(msg, sizeof(msg), "SRAM magic=%08lx step=%lu",
                 (unsigned long)sram_magic, (unsigned long)sram_step);
        ps4_log(msg);
        printf("[ps4_auth] %s\n", msg);
        s_crash_detect.magic = CRASH_SRAM_MAGIC;
        s_crash_detect.step  = 0;
    }

    // Watchdog scratch[6] — may or may not survive PSM reset depending on RP2040 revision.
    // Keep as fallback; prefer SRAM breadcrumb above.
    {
        uint32_t step = SIGN_SCRATCH;
        if (step >= 1 && step <= 3) {
            char msg[40];
            snprintf(msg, sizeof(msg), "CRASH WD step=%lu", (unsigned long)step);
            ps4_log(msg);
        }
        SIGN_SCRATCH = 0;
    }

    // Overclock to 250 MHz — halves RSA signing time (~1.7 s vs ~3.4 s at 125 MHz),
    // giving margin within the PS4's auth challenge window.
    set_sys_clock_khz(250000, true);

    // Free any previous RSA context from a prior init cycle.
    mbedtls_rsa_free(&s_rsa);

    s_rsa_valid = false;
    s_signing_requested = false;
    s_core1_signing     = false;
    s_signature_ready   = false;
    s_sign_ret          = 0;
    s_nonce_pages_received = 0;
    s_page_cursor = 0;

    // Seed PRNG from hardware once — safe at init, avoids blocking later.
    prng_seed();

    ps4_auth_data_t auth;
    if (!ps4_auth_flash_load(&auth)) {
        ps4_log("INIT FAIL no key");
        return false;
    }

    // Cache response fields
    memcpy(s_serial,     auth.serial, sizeof(s_serial));
    memcpy(s_device_sig, auth.sig,    sizeof(s_device_sig));

    // Import RSA key components into mbedTLS context
    mbedtls_rsa_init(&s_rsa);
    mbedtls_rsa_set_padding(&s_rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);

    mbedtls_mpi N, P, Q, E;
    mbedtls_mpi_init(&N);
    mbedtls_mpi_init(&P);
    mbedtls_mpi_init(&Q);
    mbedtls_mpi_init(&E);

    int ret = 0;
    ret |= mbedtls_mpi_read_binary(&N, auth.rsa_n, sizeof(auth.rsa_n));
    ret |= mbedtls_mpi_read_binary(&E, auth.rsa_e, sizeof(auth.rsa_e));
    ret |= mbedtls_mpi_read_binary(&P, auth.rsa_p, sizeof(auth.rsa_p));
    ret |= mbedtls_mpi_read_binary(&Q, auth.rsa_q, sizeof(auth.rsa_q));

    if (ret != 0) {
        printf("[ps4_local_auth] Failed to read RSA key components (%d)\n", ret);
        ps4_log("INIT FAIL key read");
        goto cleanup;
    }

    // Diagnostic: print bit lengths and first 4 bytes of each component
    printf("[ps4_local_auth] N: %u bits, [0..3]: %02x%02x%02x%02x\n",
           (unsigned)mbedtls_mpi_bitlen(&N),
           auth.rsa_n[0], auth.rsa_n[1], auth.rsa_n[2], auth.rsa_n[3]);
    printf("[ps4_local_auth] P: %u bits, [0..3]: %02x%02x%02x%02x\n",
           (unsigned)mbedtls_mpi_bitlen(&P),
           auth.rsa_p[0], auth.rsa_p[1], auth.rsa_p[2], auth.rsa_p[3]);
    printf("[ps4_local_auth] Q: %u bits, [0..3]: %02x%02x%02x%02x\n",
           (unsigned)mbedtls_mpi_bitlen(&Q),
           auth.rsa_q[0], auth.rsa_q[1], auth.rsa_q[2], auth.rsa_q[3]);

    // Check N == P*Q (diagnostic — reports mismatch, proceeds regardless)
    {
        mbedtls_mpi PQ;
        mbedtls_mpi_init(&PQ);
        if (mbedtls_mpi_mul_mpi(&PQ, &P, &Q) == 0) {
            int pq_eq = (mbedtls_mpi_cmp_mpi(&PQ, &N) == 0);
            printf("[ps4_local_auth] P*Q == N: %s (P*Q bits: %u, N bits: %u)\n",
                   pq_eq ? "YES" : "NO",
                   (unsigned)mbedtls_mpi_bitlen(&PQ),
                   (unsigned)mbedtls_mpi_bitlen(&N));
        }
        mbedtls_mpi_free(&PQ);
    }

    // Import P, Q, E — do NOT import N; let mbedtls_rsa_complete() compute
    // N = P*Q internally. This avoids a spurious KEY_CHECK_FAILED when the
    // stored N byte-sequence doesn't exactly match what mbedTLS derives from
    // P*Q (e.g. due to leading-zero differences in upload encoding).
    ret = mbedtls_rsa_import(&s_rsa, NULL, &P, &Q, NULL, &E);
    if (ret != 0) {
        printf("[ps4_local_auth] mbedtls_rsa_import failed (%d)\n", ret);
        { char lm[32]; snprintf(lm, sizeof(lm), "INIT FAIL import %d", ret); ps4_log(lm); }
        goto cleanup;
    }

    ret = mbedtls_rsa_complete(&s_rsa);
    if (ret != 0) {
        printf("[ps4_local_auth] mbedtls_rsa_complete failed (%d)\n", ret);
        { char lm[32]; snprintf(lm, sizeof(lm), "INIT FAIL complete %d", ret); ps4_log(lm); }
        goto cleanup;
    }

    ret = mbedtls_rsa_check_privkey(&s_rsa);
    if (ret != 0) {
        printf("[ps4_local_auth] RSA private key check failed (%d)\n", ret);
        { char lm[32]; snprintf(lm, sizeof(lm), "INIT FAIL privkey %d", ret); ps4_log(lm); }
        goto cleanup;
    }

    s_rsa_valid = true;
    printf("[ps4_local_auth] RSA key loaded, local auth available\n");
    ps4_log("INIT ok RSA2048");

cleanup:
    mbedtls_mpi_free(&N);
    mbedtls_mpi_free(&P);
    mbedtls_mpi_free(&Q);
    mbedtls_mpi_free(&E);

    if (!s_rsa_valid) {
        mbedtls_rsa_free(&s_rsa);
    }

    // Zero sensitive key material from stack
    memset(&auth, 0, sizeof(auth));

    return s_rsa_valid;
}

bool ps4_local_auth_reload(void)
{
    return ps4_local_auth_init();
}

bool ps4_local_auth_is_available(void)
{
    return s_rsa_valid;
}

// ============================================================================
// NONCE RECEPTION
// ============================================================================

void ps4_local_auth_send_nonce_page(const uint8_t *data, uint16_t len)
{
    if (!s_rsa_valid || !data || len < 3) return;

    // data[0] = nonce_id, data[1] = page, data[2] = padding, data[3..58] = 56 bytes
    uint8_t nonce_id = data[0];
    uint8_t page     = data[1];

    if (page >= NONCE_PAGES) return;

    if (page == 0) {
        // New auth session — reset state
        s_nonce_id = nonce_id;
        s_nonce_pages_received = 0;
        s_signing_requested = false;
        s_signature_ready   = false;
        s_page_cursor       = 0;
        memset(s_nonce, 0, sizeof(s_nonce));
        printf("[ps4_local_auth] New auth session, nonce_id=%d\n", nonce_id);
    }

    // Copy 56 bytes of nonce data (data[3..58])
    uint16_t data_offset = 3;
    uint16_t avail = (len > data_offset) ? (len - data_offset) : 0;
    uint16_t copy_len = (avail < PAGE_SIZE) ? avail : PAGE_SIZE;

    memcpy(s_nonce + page * PAGE_SIZE, data + data_offset, copy_len);
    s_nonce_pages_received++;

    printf("[ps4_local_auth] Received nonce page %d/%d\n",
           s_nonce_pages_received, NONCE_PAGES);

    if (s_nonce_pages_received >= NONCE_PAGES) {
        printf("[ps4_local_auth] All nonce pages received, requesting sign\n");
        s_signing_requested = true;
        char logmsg[32];
        snprintf(logmsg, sizeof(logmsg), "NONCE done id=%u", s_nonce_id);
        ps4_log(logmsg);
    }
}

// ============================================================================
// SIGNING TASK
// ============================================================================

void ps4_local_auth_task(void)
{
    // ---- Check if Core 1 just finished signing ----
    if (s_core1_signing && s_signature_ready) {
        s_core1_signing = false;
        s_sign_start_ms = 0;  // Reset timeout tracker
        if (s_sign_ret != 0) {
            printf("[ps4_local_auth] RSA sign failed on Core 1 (%d)\n", s_sign_ret);
            char logmsg[32];
            snprintf(logmsg, sizeof(logmsg), "SIGN FAIL ret=%d", s_sign_ret);
            ps4_log(logmsg);
        } else {
            printf("[ps4_local_auth] Core 1 signing complete\n");
            ps4_log("SIGN done");
        }
        return;
    }

    // ---- Wait while Core 1 is signing ----
    if (s_core1_signing) {
        // Track signing duration for timeout detection
        if (s_sign_start_ms == 0) s_sign_start_ms = platform_time_ms();
        uint32_t elapsed = platform_time_ms() - s_sign_start_ms;
        if (elapsed > 8000 && (elapsed % 5000) < 50) {
            // Log every ~5s after 8s timeout
            char msg[40];
            snprintf(msg, sizeof(msg), "SIGN TIMEOUT %lums", (unsigned long)elapsed);
            ps4_log(msg);
        }
        return;
    }

    // ---- Dispatch signing (Core 0 blocking for debug) ----
    if (!s_signing_requested || !s_rsa_valid) return;

    s_signing_requested = false;
    s_signature_ready   = false;
    s_sign_ret          = 0;
    s_page_cursor       = 0;

    // Snapshot the nonce
    memcpy(s_sign_nonce, s_nonce, NONCE_SIZE);

    printf("[ps4_local_auth] Signing on Core 0 (blocking, nonce_id=%d)...\n", s_nonce_id);
    ps4_log("SIGN start C0");
    s_sign_start_ms = platform_time_ms();

    // Sign directly on Core 0 — blocks the main loop but eliminates all
    // multicore timing issues for debugging.
    ps4_do_sign();

    uint32_t duration = platform_time_ms() - s_sign_start_ms;
    s_sign_start_ms = 0;

    if (s_sign_ret != 0) {
        printf("[ps4_local_auth] Sign failed ret=%d (%lums)\n", s_sign_ret, (unsigned long)duration);
        char logmsg[40];
        snprintf(logmsg, sizeof(logmsg), "SIGN FAIL ret=%d %lums", s_sign_ret, (unsigned long)duration);
        ps4_log(logmsg);
    } else {
        printf("[ps4_local_auth] Sign OK (%lums)\n", (unsigned long)duration);
        char logmsg[32];
        snprintf(logmsg, sizeof(logmsg), "SIGN done %lums", (unsigned long)duration);
        ps4_log(logmsg);
    }
    s_signature_ready = true;
}

// ============================================================================
// STATUS AND RETRIEVAL
// ============================================================================

uint8_t ps4_local_auth_get_status(void)
{
    if (s_signature_ready) return 0x00;  // Ready
    return 0x10;                          // Still signing (16)
}

uint16_t ps4_local_auth_get_status_report(uint8_t *buffer, uint16_t maxlen)
{
    // Response is exactly 15 bytes: [nonce_id, status, 0×9, CRC32]
    // GP2040-CE: CRC32([0xF2, nonce_id, status, 0×9]) over 12 bytes → appended at [11..14]
    if (!buffer || maxlen < 15) return 0;

    uint8_t status = ps4_local_auth_get_status();

    uint8_t temp[16];
    memset(temp, 0, sizeof(temp));
    temp[0] = 0xF2;               // report ID (used in CRC but not returned)
    temp[1] = s_nonce_id;
    temp[2] = status;
    // temp[3..11] = 9 zeros (already zero from memset)
    uint32_t crc = ps4_crc32(temp, 12);
    memcpy(&temp[12], &crc, 4);

    memcpy(buffer, &temp[1], 15);

    // Log status transitions (first poll while signing, and first ready)
    static uint8_t s_last_logged_status = 0xFF;
    if (status != s_last_logged_status) {
        char msg[32];
        snprintf(msg, sizeof(msg), "F2 status=0x%02X id=%u", status, s_nonce_id);
        ps4_log(msg);
        s_last_logged_status = status;
    }

    return 15;
}

uint16_t ps4_local_auth_get_next_page(uint8_t *buffer, uint16_t maxlen)
{
    // Response is exactly 63 bytes: [nonce_id, chunk, 0, data[56], CRC32[4]]
    // GP2040-CE: CRC32([0xF1, nonce_id, chunk, 0, data[56]]) over 60 bytes → appended at [59..62]
    if (!buffer || maxlen < 63) return 0;

    uint8_t page = (s_page_cursor < SIG_PAGES) ? s_page_cursor : (SIG_PAGES - 1);

    uint8_t temp[64];
    memset(temp, 0, sizeof(temp));
    temp[0] = 0xF1;           // report ID (used in CRC but not returned)
    temp[1] = s_nonce_id;
    temp[2] = page;
    temp[3] = 0x00;           // padding
    memcpy(&temp[4], s_response + (page * PAGE_SIZE), PAGE_SIZE);  // 56 bytes
    uint32_t crc = ps4_crc32(temp, 60);
    memcpy(&temp[60], &crc, 4);

    memcpy(buffer, &temp[1], 63);

    if (s_page_cursor < SIG_PAGES) {
        s_page_cursor++;
    }

    // Log first and last page delivery
    if (page == 0) {
        ps4_log("F1 page=0 (first)");
    } else if (page == SIG_PAGES - 1) {
        char msg[32];
        snprintf(msg, sizeof(msg), "F1 page=%u (last)", page);
        ps4_log(msg);
    }

    return 63;
}

void ps4_local_auth_reset(void)
{
    s_signing_requested    = false;
    s_core1_signing        = false;
    s_signature_ready      = false;
    s_sign_ret             = 0;
    s_nonce_pages_received = 0;
    s_page_cursor          = 0;
    s_nonce_id             = 0;
    memset(s_nonce,       0, sizeof(s_nonce));
    memset(s_sign_nonce,  0, sizeof(s_sign_nonce));
    memset(s_response,    0, sizeof(s_response));
}

void ps4_local_auth_set_log_enabled(bool enabled)
{
    s_log_enabled = enabled;
}

bool ps4_local_auth_get_log_enabled(void)
{
    return s_log_enabled;
}
