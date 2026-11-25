// rp_config.c - PS Remote Play provisioning config, persisted to a dedicated
// flash sector so WiFi + PSN sign-in survive a reboot ("sign in once").
// SPDX-License-Identifier: Apache-2.0

#include "rp_config.h"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "core/services/storage/flash_layout.h"

#include <string.h>
#include <stdio.h>
#include <stddef.h>

// Dedicated store: one 4KB sector from flash_layout.h. On RP2040 this is the
// same -24KB as before. On RP2350 the old -24KB landed on the sector the PS4
// auth store (and, on controller builds, pad config) also used, so it moves
// one sector down and the old copy is carried forward once at init.
// XIP-mapped at XIP_BASE + offset.
#define RP_CFG_FLASH_OFFSET     FLASH_LAYOUT_RP_CONFIG_OFFSET
#define RP_CFG_FLASH_OLD_OFFSET FLASH_LAYOUT_RP_CONFIG_OLD_OFFSET
#define RP_CFG_MAGIC         0x52504346u   // "RPCF"
#define RP_CFG_VERSION       2u            // v2 added auto_connect (appended field)

typedef struct {
    uint32_t    magic;
    uint32_t    version;
    uint32_t    crc;         // over the config bytes only
    rp_config_t cfg;
} rp_persist_t;

_Static_assert(sizeof(rp_persist_t) <= FLASH_PAGE_SIZE,
               "rp_persist_t must fit in one 256-byte flash page");

static rp_config_t cfg;

static uint32_t crc32(const uint8_t* d, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
    }
    return ~c;
}

// Recompute the derived have_* flags from the actual field contents.
static void recompute_flags(void)
{
    cfg.have_wifi = cfg.wifi_ssid[0] != '\0';
    bool have_ip = cfg.ps5_ip[0] != '\0';
    bool have_acct = false;
    for (int i = 0; i < RP_ACCOUNT_LEN; i++) if (cfg.account_id[i]) { have_acct = true; break; }
    bool have_keys = false;
    for (int i = 0; i < RP_KEY_LEN; i++) if (cfg.rp_key[i]) { have_keys = true; break; }
    cfg.have_registration = have_ip && have_acct && have_keys;
}

void rp_config_init(void)
{
    memset(&cfg, 0, sizeof(cfg));
    const rp_persist_t* p = (const rp_persist_t*)(XIP_BASE + RP_CFG_FLASH_OFFSET);
    bool moved = false;
    if (RP_CFG_FLASH_OLD_OFFSET != RP_CFG_FLASH_OFFSET && p->magic != RP_CFG_MAGIC) {
        const rp_persist_t* old = (const rp_persist_t*)(XIP_BASE + RP_CFG_FLASH_OLD_OFFSET);
        if (old->magic == RP_CFG_MAGIC) { p = old; moved = true; }
    }
    if (p->magic == RP_CFG_MAGIC && p->version == RP_CFG_VERSION &&
        p->crc == crc32((const uint8_t*)&p->cfg, sizeof(rp_config_t))) {
        memcpy(&cfg, &p->cfg, sizeof(cfg));
        recompute_flags();
        printf("[rp_config] loaded from flash: wifi=%s account=%s auto=%d\n",
               cfg.have_wifi ? cfg.wifi_ssid : "(none)",
               cfg.have_registration ? "linked" : "(partial)", cfg.auto_connect);
    } else if (p->magic == RP_CFG_MAGIC && p->version == 1u &&
               p->crc == crc32((const uint8_t*)&p->cfg, offsetof(rp_config_t, auto_connect))) {
        // v1 store (no auto_connect field): keep the user's wifi/registration,
        // default auto_connect off. crc was computed over the shorter v1 struct.
        memcpy(&cfg, &p->cfg, offsetof(rp_config_t, auto_connect));
        cfg.auto_connect = false;
        recompute_flags();
        printf("[rp_config] migrated v1->v2 (auto_connect=off)\n");
    } else {
        printf("[rp_config] no saved config (blank flash)\n");
        moved = false;
    }
    if (moved) {
        printf("[rp_config] moved from the old shared sector\n");
        rp_config_save();
    }
}

rp_config_t* rp_config_get(void) { return &cfg; }

// Erase+program the store sector. Must run entirely from RAM with interrupts
// off: XIP is disabled during the flash op, so any flash fetch would hang.
// usb2wifi has no core1 flash-resident task and CYW43 runs on core0, so a brief
// stall here (rare — only on provisioning changes) is safe.
static void __not_in_flash_func(rp_flash_write)(const uint8_t* page)
{
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(RP_CFG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(RP_CFG_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

void rp_config_save(void)
{
    recompute_flags();
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    rp_persist_t* p = (rp_persist_t*)page;
    p->magic = RP_CFG_MAGIC;
    p->version = RP_CFG_VERSION;
    p->cfg = cfg;
    p->crc = crc32((const uint8_t*)&p->cfg, sizeof(rp_config_t));
    rp_flash_write(page);
    printf("[rp_config] saved to flash\n");
}

void rp_config_clear(void)
{
    memset(&cfg, 0, sizeof(cfg));
    rp_config_save();   // rewrite the store as blank (magic + zeroed cfg)
    printf("[rp_config] cleared\n");
}

bool rp_config_set_wifi(const char* ssid, const char* pass)
{
    if (!ssid || ssid[0] == '\0' || strlen(ssid) >= RP_SSID_MAX) return false;
    if (!pass || strlen(pass) >= RP_PASS_MAX) return false;
    strncpy(cfg.wifi_ssid, ssid, RP_SSID_MAX - 1); cfg.wifi_ssid[RP_SSID_MAX - 1] = '\0';
    strncpy(cfg.wifi_pass, pass, RP_PASS_MAX - 1); cfg.wifi_pass[RP_PASS_MAX - 1] = '\0';
    cfg.have_wifi = true;
    printf("[rp_config] wifi set: ssid=%s\n", cfg.wifi_ssid);
    return true;
}

bool rp_config_set_ps5_ip(const char* ip)
{
    if (!ip || ip[0] == '\0' || strlen(ip) >= RP_IP_MAX) return false;
    strncpy(cfg.ps5_ip, ip, RP_IP_MAX - 1); cfg.ps5_ip[RP_IP_MAX - 1] = '\0';
    printf("[rp_config] ps5 ip set: %s\n", cfg.ps5_ip);
    return true;
}

void rp_config_set_auto_connect(bool en)
{
    if (cfg.auto_connect == en) return;   // no-op → skip the flash write
    cfg.auto_connect = en;
    rp_config_save();
    printf("[rp_config] auto_connect = %d\n", en);
}

bool rp_config_set_account_id(const uint8_t* id8)
{
    if (!id8) return false;
    memcpy(cfg.account_id, id8, RP_ACCOUNT_LEN);
    return true;
}

bool rp_config_set_keys(const uint8_t* rp_key16, const uint8_t* regist16)
{
    if (!rp_key16 || !regist16) return false;
    memcpy(cfg.rp_key, rp_key16, RP_KEY_LEN);
    memcpy(cfg.regist_key, regist16, RP_REGIST_LEN);
    recompute_flags();
    return true;
}
