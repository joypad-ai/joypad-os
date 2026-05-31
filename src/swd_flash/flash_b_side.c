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

#include "dual_b_binary.h"
#include "adi.h"
#include "flash.h"
#include "swd.h"

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
    swd_init();
    dp_init();

    core_select(0);
    core_reset_halt();
    core_select(1);
    core_reset_halt();
    core_select(0);

    // Write B's flash from the embedded image, retrying the whole sequence if
    // the SWD transfer errors (marginal links can fail mid-bulk-transfer).
    for (int tries = 0; tries < 4; tries++) {
        int wrc = rp2040_add_flash_bit(0, dual_b_binary, dual_b_binary_length);
        rp2040_add_flash_bit(0xffffffff, NULL, 0);
        if (wrc == 0) break;
        // Re-establish the debug connection before retrying.
        dp_init();
        core_select(0);
        core_reset_halt();
    }

    // Reboot B (the freshly-flashed target), then reboot ourselves (A) so the
    // bootloader hands control to A's flash image.
    watchdog_reboot_target();
    watchdog_reboot(0, 0, 0);

    while (true) {
        __wfi();
    }
    return 0;
}
