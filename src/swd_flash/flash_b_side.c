// flash_b_side.c - RAM-resident stage that flashes the host-side (B) RP2040
// over SWD with an embedded image, then reboots into the device-side (A) app.
//
// Ported from jfedor2/hid-remapper firmware/src/flash_b_side.cc (which derives
// its SWD engine from essele/pico_debug). On the dual-RP2040 board only the A
// (device, USB-C) RP2040 can BOOTSEL; B (host) is reachable only via A's SWD
// lines (PIN_SWDCLK/PIN_SWDIO, board-specific). combine_uf2.py merges this RAM
// program after A's flash image so the bootloader writes A to flash, then runs
// this stage to SWD-flash B, then reboots — both chips run JoypadOS.
//
// NOTE: an SWD-triggered reset can't fully clear B's debug/power state, so after
// flashing the combined UF2 the board must be POWER-CYCLED once for B to boot
// its freshly-flashed image (same as HID-Remapper's flash-then-replug flow).

#include <hardware/regs/psm.h>
#include <hardware/regs/watchdog.h>
#include <hardware/regs/addressmap.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <pico/bootrom.h>

#include "adi.h"
#include "flash.h"
#include "swd.h"

// B's image lives in A's flash (written by the bootloader from the combined
// UF2), not in this RAM-resident relay — so B can be far larger than the
// relay's RAM. combine_uf2.py places it at XIP_BASE + B_IMAGE_OFFSET as
// [magic u32][length u32][raw image]. We read it via XIP and stream it to B
// over SWD. Keep B_IMAGE_OFFSET / B_IMAGE_MAGIC in sync with combine_uf2.py.
#define B_IMAGE_OFFSET 0x40000u
#define B_IMAGE_MAGIC  0x42494D47u  // "BIMG"

// RAM staging buffer (one 64K SWD chunk). BSS on this RAM-resident relay.
// The SWD transfer reads its source repeatedly while bit-banging; feeding it
// straight from XIP flash proved unreliable on the large image, so we bulk-copy
// each chunk XIP->RAM here first and push to B from stable RAM (as the original
// embedded-array relay did).
static uint8_t g_stage[65536];

// Reboot the SWD *target* (B) via its watchdog, using SWD memory writes.
static void watchdog_reboot_target(void) {
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

int main(void) {
    // CRITICAL: re-enter XIP first. The bootrom writes our flash blocks (A's
    // firmware + B's image) then hands control to this no_flash RAM binary with
    // flash left in exit-XIP state — so memory-mapped reads of B's image (staged
    // in A's flash) return garbage until XIP is re-established. Without this the
    // header check below fails and B is never flashed (keeps its old image).
    rom_connect_internal_flash();
    rom_flash_flush_cache();
    rom_flash_enter_cmd_xip();

    // B's image, read straight from A's XIP flash: [magic][length][raw image].
    const volatile uint32_t* hdr = (const volatile uint32_t*)(XIP_BASE + B_IMAGE_OFFSET);
    const uint8_t* b_image = (const uint8_t*)(XIP_BASE + B_IMAGE_OFFSET + 8);
    uint32_t b_magic = hdr[0];
    uint32_t b_length = hdr[1];

    // Result marker for the device side (A) to surface over CDC. SCRATCH0/1
    // survive the warm reboot (watchdog_reboot only uses SCRATCH4-7). Status in
    // the low byte of SCRATCH0: 1=bad header, 2=SWD flash failed, 3=success.
    volatile uint32_t* scratch = (volatile uint32_t*)(WATCHDOG_BASE + WATCHDOG_SCRATCH0_OFFSET);

    // Bail out (leaving B untouched) if the image header is missing/corrupt, so
    // a bad combine (or a failed XIP re-entry) can't brick B by flashing garbage.
    if (b_magic != B_IMAGE_MAGIC || b_length == 0 || b_length > 0x200000u) {
        scratch[0] = 0xB0000000u | 1u;  // bad header
        scratch[1] = b_magic;           // what we actually read (helps diagnose XIP)
        watchdog_reboot(0, 0, 0);
        while (true) { __wfi(); }
    }

    // Hardware watchdog: SWD chunk ops are sub-second, so if anything hangs the
    // board would otherwise stay dark forever. Arm an 8.3s watchdog (RP2040 max)
    // and pet it at each step; on a true hang it warm-resets A within 8.3s. A
    // warm reset PRESERVES scratch, so scratch[2] (progress) survives and A can
    // report exactly where the relay hung. Petted before every chunk below.
    scratch[2] = 0xC0000001u;   // reached: about to init SWD
    watchdog_enable(8300, 1);

    swd_init();
    dp_init();

    core_select(0);
    core_reset_halt();
    core_select(1);
    core_reset_halt();
    core_select(0);
    watchdog_update();
    scratch[2] = 0xC0000002u;   // reached: SWD up, cores halted

    // Write B's flash: copy each 64K chunk XIP->RAM, then push it to B over SWD
    // from RAM. Retry the whole sequence on SWD error (marginal links can fail
    // mid-transfer). b_length is padded to a 4K multiple by combine_uf2.py, so
    // every chunk length is 256/4K-aligned for the ROM flash program on B.
    int wrc = -1;
    for (int tries = 0; tries < 4; tries++) {
        wrc = 0;
        uint32_t off = 0;
        while (off < b_length) {
            uint32_t n = (b_length - off) < 65536u ? (b_length - off) : 65536u;
            watchdog_update();
            scratch[2] = 0xC0001000u | (off >> 12);  // progress: chunk offset (4K units)
            // Bulk XIP -> RAM (word copy; b_image is 4-byte aligned, n is 4K-aligned)
            const uint32_t* s = (const uint32_t*)(b_image + off);
            uint32_t* d = (uint32_t*)g_stage;
            for (uint32_t i = 0; i < n / 4; i++) d[i] = s[i];
            wrc = rp2040_add_flash_bit(off, g_stage, (int)n);
            if (wrc != 0) break;
            off += n;
        }
        if (wrc == 0) {
            watchdog_update();
            scratch[2] = 0xC00000F0u;  // progress: flushing residual chunk
            wrc = rp2040_add_flash_bit(0xffffffff, NULL, 0);  // flush residual chunk
            if (wrc == 0) break;
        }
        watchdog_update();
        // Re-establish the debug connection before retrying.
        dp_init();
        core_select(0);
        core_reset_halt();
    }
    scratch[0] = 0xB0000000u | (wrc == 0 ? 3u : 2u);
    scratch[1] = b_length;
    scratch[2] = (wrc == 0) ? 0xC00000FFu : scratch[2];  // FF = ran to completion

    // Reboot B (the freshly-flashed target), then reboot ourselves (A) so the
    // bootloader hands control to A's flash image.
    watchdog_reboot_target();
    watchdog_reboot(0, 0, 0);

    while (true) {
        __wfi();
    }
    return 0;
}
