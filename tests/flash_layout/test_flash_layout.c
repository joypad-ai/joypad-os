// Host-side check that no two persistent flash stores share a sector.
//
// Build and run for both platforms:
//   gcc -std=c11 -Wall -I../../src test_flash_layout.c -o t && ./t
//   gcc -std=c11 -Wall -I../../src -DPICO_RP2350=1 -DPICO_RP2350_A2_SUPPORTED=1 test_flash_layout.c -o t && ./t
// Add -DOLD_LAYOUT to model the offsets from before flash_layout.h (fails).
// Pass criterion: prints "ALL PASS".

#include <stdio.h>
#include <stdint.h>

#define FLASH_SECTOR_SIZE     4096u
#define PICO_FLASH_SIZE_BYTES (2u * 1024u * 1024u)
#ifndef PICO_RP2350
#define PICO_RP2350 0
#define PICO_RP2350_A2_SUPPORTED 0
#endif
#define FLASH_LAYOUT_HOST_TEST 1

#ifndef OLD_LAYOUT
#include "core/services/storage/flash_layout.h"
#define BT_OFF   FLASH_LAYOUT_BTSTACK_OFFSET
#define SET_A    FLASH_LAYOUT_SETTINGS_A_OFFSET
#define SET_B    FLASH_LAYOUT_SETTINGS_B_OFFSET
#define PAD_CFG  FLASH_LAYOUT_PAD_CONFIG_OFFSET
#define RP_CFG   FLASH_LAYOUT_RP_CONFIG_OFFSET
#define PS4_AUTH FLASH_LAYOUT_PS4_AUTH_OFFSET
#else
// The offsets as written in flash.c, pad_config_storage_rp2040.c,
// ps4_auth_flash.c and rp_config.c before this change.
#define S FLASH_SECTOR_SIZE
#if PICO_RP2350 && PICO_RP2350_A2_SUPPORTED
#define SET_A (PICO_FLASH_SIZE_BYTES - S - 2 * S - S)
#define BT_OFF (PICO_FLASH_SIZE_BYTES - S - 2 * S)
#else
#define SET_A (PICO_FLASH_SIZE_BYTES - 2 * S - S)
#define BT_OFF (PICO_FLASH_SIZE_BYTES - 2 * S)
#endif
#define SET_B    (SET_A - S)
#define PAD_CFG  (SET_B - S)
#define PS4_AUTH (SET_B - S)
#define RP_CFG   (PICO_FLASH_SIZE_BYTES - 6 * S)
#endif

typedef struct { const char *name; uint32_t off, len; } region_t;

int main(void)
{
    const region_t r[] = {
        { "btstack",     BT_OFF,   2 * FLASH_SECTOR_SIZE },
        { "settings A",  SET_A,    FLASH_SECTOR_SIZE },
        { "settings B",  SET_B,    FLASH_SECTOR_SIZE },
        { "pad config",  PAD_CFG,  FLASH_SECTOR_SIZE },
        { "remote play", RP_CFG,   FLASH_SECTOR_SIZE },
        { "ps4 auth",    PS4_AUTH, FLASH_SECTOR_SIZE },
    };
    const int n = (int)(sizeof(r) / sizeof(r[0]));
    int fails = 0;
    printf("platform: %s\n", (PICO_RP2350 && PICO_RP2350_A2_SUPPORTED) ? "RP2350 (A2)" : "RP2040");
    for (int i = 0; i < n; i++) {
        printf("  %-12s 0x%06lX\n", r[i].name, (unsigned long)r[i].off);
        if (r[i].off % FLASH_SECTOR_SIZE || r[i].off + r[i].len > PICO_FLASH_SIZE_BYTES) {
            printf("FAIL %s is misaligned or past the end of flash\n", r[i].name);
            fails++;
        }
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (!(r[i].off + r[i].len <= r[j].off || r[j].off + r[j].len <= r[i].off)) {
                printf("FAIL %s and %s share flash\n", r[i].name, r[j].name);
                fails++;
            }
#ifndef OLD_LAYOUT
    // Unchanged homes: settings, pad config and BTstack must not move, or
    // existing users lose their saved data on update.
    {
        const uint32_t S2 = FLASH_SECTOR_SIZE;
        const uint32_t end = (PICO_RP2350 && PICO_RP2350_A2_SUPPORTED)
                           ? PICO_FLASH_SIZE_BYTES - S2 : PICO_FLASH_SIZE_BYTES;
        if (SET_A != end - 3 * S2 || SET_B != end - 4 * S2 || PAD_CFG != end - 5 * S2 ||
            BT_OFF != end - 2 * S2) {
            printf("FAIL an existing store moved\n");
            fails++;
        }
        if (FLASH_LAYOUT_PS4_AUTH_OLD_OFFSET != end - 5 * S2 ||
            FLASH_LAYOUT_RP_CONFIG_OLD_OFFSET != PICO_FLASH_SIZE_BYTES - 6 * S2) {
            printf("FAIL old offsets used for the carry-forward are wrong\n");
            fails++;
        }
    }
#endif
    printf("%s\n", fails ? "SOME FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
