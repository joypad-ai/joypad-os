// flash_layout.h - every persistent flash sector on RP2040 / RP2350, in one place
// SPDX-License-Identifier: Apache-2.0
//
// Each user of persistent flash takes its offset from here, so two stores can
// never be placed on the same sector by accident. The static asserts below
// fail the build if any two regions overlap or leave the flash.
//
// Layout, counted back from FLASH_LAYOUT_END:
//   [... firmware ...]
//   [PS4 auth      1 sector]   END - 7 sectors
//   [Remote Play   1 sector]   END - 6 sectors
//   [Pad config    1 sector]   END - 5 sectors
//   [Settings B    1 sector]   END - 4 sectors
//   [Settings A    1 sector]   END - 3 sectors
//   [BTstack       2 sectors]  END - 2 sectors
//   [END]
// On RP2350 (A2 support, RP2350-E10) END is one sector before the end of
// flash; on RP2040 it is the end of flash.
//
// Settings A/B, pad config and BTstack sit where they always have. Before this
// header the PS4 auth store used the pad config sector, and on RP2350 the
// Remote Play store did too; the *_OLD_OFFSET values are those former homes,
// read once at load so existing data is carried forward.

#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

#ifndef FLASH_SECTOR_SIZE
#include "hardware/flash.h"
#endif

#if PICO_RP2350 && PICO_RP2350_A2_SUPPORTED
#define FLASH_LAYOUT_END            (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#else
#define FLASH_LAYOUT_END            (PICO_FLASH_SIZE_BYTES)
#endif

#define FLASH_LAYOUT_SECTORS_BACK(n) (FLASH_LAYOUT_END - (n) * FLASH_SECTOR_SIZE)

#define FLASH_LAYOUT_BTSTACK_SIZE        (2 * FLASH_SECTOR_SIZE)
#define FLASH_LAYOUT_BTSTACK_OFFSET      FLASH_LAYOUT_SECTORS_BACK(2)
#define FLASH_LAYOUT_SETTINGS_A_OFFSET   FLASH_LAYOUT_SECTORS_BACK(3)
#define FLASH_LAYOUT_SETTINGS_B_OFFSET   FLASH_LAYOUT_SECTORS_BACK(4)
#define FLASH_LAYOUT_PAD_CONFIG_OFFSET   FLASH_LAYOUT_SECTORS_BACK(5)
#define FLASH_LAYOUT_RP_CONFIG_OFFSET    FLASH_LAYOUT_SECTORS_BACK(6)
#define FLASH_LAYOUT_PS4_AUTH_OFFSET     FLASH_LAYOUT_SECTORS_BACK(7)

// Lowest offset any store uses: the firmware image must end below this.
#define FLASH_LAYOUT_LOWEST_OFFSET       FLASH_LAYOUT_PS4_AUTH_OFFSET

// Former homes, read-only, for the one-time carry-forward.
#define FLASH_LAYOUT_PS4_AUTH_OLD_OFFSET  FLASH_LAYOUT_SECTORS_BACK(5)
#define FLASH_LAYOUT_RP_CONFIG_OLD_OFFSET (PICO_FLASH_SIZE_BYTES - 6 * FLASH_SECTOR_SIZE)

// [a, a+la) and [b, b+lb) do not overlap
#define FLASH_LAYOUT_DISJOINT(a, la, b, lb) (((a) + (la) <= (b)) || ((b) + (lb) <= (a)))

#define FLASH_LAYOUT_S FLASH_SECTOR_SIZE
_Static_assert(FLASH_LAYOUT_DISJOINT(FLASH_LAYOUT_PS4_AUTH_OFFSET, FLASH_LAYOUT_S, FLASH_LAYOUT_PAD_CONFIG_OFFSET, FLASH_LAYOUT_S),
               "flash layout: PS4 auth overlaps pad config");
_Static_assert(FLASH_LAYOUT_DISJOINT(FLASH_LAYOUT_PS4_AUTH_OFFSET, FLASH_LAYOUT_S, FLASH_LAYOUT_RP_CONFIG_OFFSET, FLASH_LAYOUT_S),
               "flash layout: PS4 auth overlaps Remote Play");
_Static_assert(FLASH_LAYOUT_DISJOINT(FLASH_LAYOUT_PS4_AUTH_OFFSET, FLASH_LAYOUT_S, FLASH_LAYOUT_SETTINGS_B_OFFSET, 2 * FLASH_LAYOUT_S),
               "flash layout: PS4 auth overlaps settings");
_Static_assert(FLASH_LAYOUT_DISJOINT(FLASH_LAYOUT_PS4_AUTH_OFFSET, FLASH_LAYOUT_S, FLASH_LAYOUT_BTSTACK_OFFSET, FLASH_LAYOUT_BTSTACK_SIZE),
               "flash layout: PS4 auth overlaps BTstack");
_Static_assert(FLASH_LAYOUT_DISJOINT(FLASH_LAYOUT_RP_CONFIG_OFFSET, FLASH_LAYOUT_S, FLASH_LAYOUT_PAD_CONFIG_OFFSET, FLASH_LAYOUT_S),
               "flash layout: Remote Play overlaps pad config");
_Static_assert(FLASH_LAYOUT_DISJOINT(FLASH_LAYOUT_RP_CONFIG_OFFSET, FLASH_LAYOUT_S, FLASH_LAYOUT_SETTINGS_B_OFFSET, 2 * FLASH_LAYOUT_S),
               "flash layout: Remote Play overlaps settings");
_Static_assert(FLASH_LAYOUT_DISJOINT(FLASH_LAYOUT_RP_CONFIG_OFFSET, FLASH_LAYOUT_S, FLASH_LAYOUT_BTSTACK_OFFSET, FLASH_LAYOUT_BTSTACK_SIZE),
               "flash layout: Remote Play overlaps BTstack");
_Static_assert(FLASH_LAYOUT_DISJOINT(FLASH_LAYOUT_PAD_CONFIG_OFFSET, FLASH_LAYOUT_S, FLASH_LAYOUT_SETTINGS_B_OFFSET, 2 * FLASH_LAYOUT_S),
               "flash layout: pad config overlaps settings");
_Static_assert(FLASH_LAYOUT_DISJOINT(FLASH_LAYOUT_PAD_CONFIG_OFFSET, FLASH_LAYOUT_S, FLASH_LAYOUT_BTSTACK_OFFSET, FLASH_LAYOUT_BTSTACK_SIZE),
               "flash layout: pad config overlaps BTstack");
_Static_assert(FLASH_LAYOUT_DISJOINT(FLASH_LAYOUT_SETTINGS_B_OFFSET, 2 * FLASH_LAYOUT_S, FLASH_LAYOUT_BTSTACK_OFFSET, FLASH_LAYOUT_BTSTACK_SIZE),
               "flash layout: settings overlap BTstack");
_Static_assert(FLASH_LAYOUT_BTSTACK_OFFSET + FLASH_LAYOUT_BTSTACK_SIZE <= PICO_FLASH_SIZE_BYTES,
               "flash layout: runs past the end of flash");
_Static_assert(FLASH_LAYOUT_LOWEST_OFFSET % FLASH_SECTOR_SIZE == 0,
               "flash layout: offsets must be sector aligned");

#ifndef FLASH_LAYOUT_HOST_TEST
#include <stdbool.h>
#include <stdint.h>
// The image end is only known at link time, so this is checked at boot.
extern char __flash_binary_end;
static inline bool flash_layout_image_fits(void)
{
    return (uintptr_t)&__flash_binary_end - XIP_BASE <= FLASH_LAYOUT_LOWEST_OFFSET;
}
#endif

#endif // FLASH_LAYOUT_H
