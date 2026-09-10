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

    swd_init();
    dp_init();

    core_select(0);
    core_reset_halt();
    core_select(1);
    core_reset_halt();
    core_select(0);

    // Write B's flash from A's XIP image, retrying the whole sequence if the SWD
    // transfer errors (marginal links can fail mid-bulk-transfer). The flash
    // engine reads the source pointer sequentially, so streaming from XIP is
    // fine — no need to stage the image in RAM.
    int wrc = -1;
    for (int tries = 0; tries < 4; tries++) {
        wrc = rp2040_add_flash_bit(0, b_image, (int)b_length);
        rp2040_add_flash_bit(0xffffffff, NULL, 0);
        if (wrc == 0) break;
        // Re-establish the debug connection before retrying.
        dp_init();
        core_select(0);
        core_reset_halt();
    }
    scratch[0] = 0xB0000000u | (wrc == 0 ? 3u : 2u);
    scratch[1] = b_length;

    // Reboot B (the freshly-flashed target), then reboot ourselves (A) so the
    // bootloader hands control to A's flash image.
    watchdog_reboot_target();
    watchdog_reboot(0, 0, 0);

    while (true) {
        __wfi();
    }
    return 0;
}
