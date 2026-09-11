// flash_b_app.c - flash the host-side (B) RP2040 over SWD from A's RUNNING app.
//
// This is the reliable counterpart to the boot-time relay (flash_b_side.c).
// Running from the fully-booted app means: a stable, known 200 MHz clock (so
// the derived SWD timing is deterministic), the watchdog actually works (A is
// never left dark), and progress can be logged live over CDC. B's image is
// read from A's XIP flash where combine_uf2.py staged it: [magic][len][image].
//
// Trigger via the FLASH.B CDC command. A blocks in its main loop for the ~10-20s
// this takes; that's fine for a one-shot reflash. On completion B is reset over
// SWD so it boots the freshly-written image; A keeps running throughout.

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/watchdog.h"
#include "hardware/regs/psm.h"
#include "adi.h"
#include "flash.h"
#include "swd.h"

#define B_IMAGE_OFFSET 0x40000u
#define B_IMAGE_MAGIC  0x42494D47u  // "BIMG" (keep in sync with combine_uf2.py)

static uint8_t app_stage[65536];   // one 64K SWD chunk (A app BSS)

// Reboot the SWD target (B) via its watchdog, using SWD memory writes, so B
// runs the image we just wrote. Mirrors the relay's watchdog_reboot_target().
static void reboot_target_b(void) {
    mem_write32(WATCHDOG_BASE + WATCHDOG_CTRL_OFFSET + REG_ALIAS_CLR_BITS,
                WATCHDOG_CTRL_ENABLE_BITS);
    mem_write32(WATCHDOG_BASE + WATCHDOG_SCRATCH4_OFFSET, 0);
    mem_write32(PSM_BASE + PSM_WDSEL_OFFSET + REG_ALIAS_SET_BITS,
                PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS));
    mem_write32(WATCHDOG_BASE + WATCHDOG_CTRL_OFFSET + REG_ALIAS_CLR_BITS,
                WATCHDOG_CTRL_PAUSE_JTAG_BITS | WATCHDOG_CTRL_PAUSE_DBG0_BITS |
                WATCHDOG_CTRL_PAUSE_DBG1_BITS);
    mem_write32(WATCHDOG_BASE + WATCHDOG_CTRL_OFFSET + REG_ALIAS_SET_BITS,
                WATCHDOG_CTRL_TRIGGER_BITS);
}

// logf may be NULL. Returns 0 on success, negative on failure (stage encoded).
int flash_b_app(void (*logf)(const char*)) {
    char msg[96];
    #define LOG(...) do { if (logf) { snprintf(msg, sizeof(msg), __VA_ARGS__); logf(msg); } } while (0)

    const volatile uint32_t* hdr = (const volatile uint32_t*)(XIP_BASE + B_IMAGE_OFFSET);
    const uint8_t* b_image = (const uint8_t*)(XIP_BASE + B_IMAGE_OFFSET + 8);
    uint32_t b_magic  = hdr[0];
    uint32_t b_length = hdr[1];

    LOG("FLASH.B: magic=0x%08lx len=%lu", (unsigned long)b_magic, (unsigned long)b_length);
    if (b_magic != B_IMAGE_MAGIC || b_length == 0 || b_length > 0x200000u) {
        LOG("FLASH.B: bad image header at 0x%08x", (unsigned)(XIP_BASE + B_IMAGE_OFFSET));
        return -1;
    }

    if (swd_init() != SWD_OK)  { LOG("FLASH.B: swd_init failed"); return -2; }
    if (dp_init()  != SWD_OK)  { LOG("FLASH.B: dp_init failed");  return -3; }
    core_select(0); core_reset_halt();
    core_select(1); core_reset_halt();
    core_select(0);
    LOG("FLASH.B: SWD up, cores halted");

    int wrc = -1;
    for (int tries = 0; tries < 3; tries++) {
        wrc = 0;
        uint32_t off = 0;
        while (off < b_length) {
            uint32_t n = (b_length - off) < 65536u ? (b_length - off) : 65536u;
            const uint32_t* s = (const uint32_t*)(b_image + off);
            uint32_t* d = (uint32_t*)app_stage;
            for (uint32_t i = 0; i < n / 4; i++) d[i] = s[i];   // XIP -> RAM
            wrc = rp2040_add_flash_bit(off, app_stage, (int)n);
            if (wrc != 0) { LOG("FLASH.B: write failed at off=%lu (try %d)", (unsigned long)off, tries); break; }
            off += n;
            LOG("FLASH.B: %lu/%lu", (unsigned long)off, (unsigned long)b_length);
        }
        if (wrc == 0) {
            wrc = rp2040_add_flash_bit(0xffffffff, NULL, 0);   // flush residual
            if (wrc == 0) break;
        }
        dp_init(); core_select(0); core_reset_halt();          // re-establish, retry
    }

    if (wrc != 0) { LOG("FLASH.B: FAILED after retries"); return -4; }

    LOG("FLASH.B: OK, rebooting B");
    reboot_target_b();
    return 0;
    #undef LOG
}
