// flash_nrf.c - NVS-based settings storage for Seeed XIAO nRF52840
//
// Implements flash.h API using Zephyr's NVS subsystem.
// Same flash_t struct, just stored in NVS instead of raw flash.

#include "core/services/storage/flash.h"
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>

// NVS configuration
#define NVS_PARTITION       storage_partition
#define NVS_PARTITION_ID    FIXED_PARTITION_ID(NVS_PARTITION)
#define NVS_SETTINGS_KEY    1
#define SETTINGS_MAGIC      0x47435052  // "GCPR"
#define SAVE_DEBOUNCE_MS    5000

static struct nvs_fs nvs;
static bool nvs_initialized = false;
static uint32_t last_change_ms = 0;
static bool save_pending = false;
static flash_t pending_settings;
static uint32_t current_sequence = 0;

// Runtime settings
static flash_t runtime_settings;
static bool runtime_settings_loaded = false;

struct nvs_fs* flash_nrf_get_nvs(void)
{
    return nvs_initialized ? &nvs : NULL;
}

void flash_init(void)
{
    save_pending = false;

    const struct flash_area *fa;
    int rc = flash_area_open(NVS_PARTITION_ID, &fa);
    if (rc) {
        printf("[flash_nrf] Failed to open storage partition: %d\n", rc);
        return;
    }

    nvs.flash_device = flash_area_get_device(fa);
    nvs.offset = fa->fa_off;
    nvs.sector_size = 4096;
    nvs.sector_count = fa->fa_size / nvs.sector_size;
    flash_area_close(fa);

    rc = nvs_mount(&nvs);
    if (rc) {
        printf("[flash_nrf] NVS mount failed: %d\n", rc);
        return;
    }

    nvs_initialized = true;
    printf("[flash_nrf] NVS initialized (%d sectors)\n", nvs.sector_count);

    // Try to load runtime settings
    if (!flash_load(&runtime_settings)) {
        // No valid settings - initialize defaults
        memset(&runtime_settings, 0, sizeof(flash_t));
        runtime_settings.magic = SETTINGS_MAGIC;
        runtime_settings.sequence = 0;
        runtime_settings.active_profile_index = 0;
        runtime_settings.custom_profile_count = 0;
        runtime_settings.schema_version = FLASH_SCHEMA_VERSION;
    }
    runtime_settings_loaded = true;
}

void flash_factory_reset(void)
{
    if (!nvs_initialized) return;
    nvs_clear(&nvs);
    printf("[flash_nrf] Factory reset — all NVS data erased\n");
}

bool flash_load(flash_t* settings)
{
    if (!nvs_initialized) return false;

    ssize_t len = nvs_read(&nvs, NVS_SETTINGS_KEY, settings, sizeof(flash_t));
    if (len != sizeof(flash_t)) {
        printf("[flash_nrf] No saved settings (read %zd bytes)\n", len);
        return false;
    }

    if (settings->magic != SETTINGS_MAGIC) {
        printf("[flash_nrf] Invalid settings magic\n");
        return false;
    }

    if (settings->schema_version != FLASH_SCHEMA_VERSION) {
        printf("[flash_nrf] schema mismatch (stored=v%u, expected=v%u) — wiping settings\n",
               (unsigned)settings->schema_version, (unsigned)FLASH_SCHEMA_VERSION);
        current_sequence = settings->sequence;
        return false;
    }

    printf("[flash_nrf] Settings loaded\n");
    return true;
}

void flash_save(const flash_t* settings)
{
    if (!nvs_initialized) return;

    memcpy(&pending_settings, settings, sizeof(flash_t));
    pending_settings.magic = SETTINGS_MAGIC;
    pending_settings.schema_version = FLASH_SCHEMA_VERSION;
    save_pending = true;
    last_change_ms = platform_time_ms();
}

void flash_save_now(const flash_t* settings)
{
    if (!nvs_initialized) return;

    static flash_t write_settings;
    memcpy(&write_settings, settings, sizeof(flash_t));
    write_settings.magic = SETTINGS_MAGIC;
    write_settings.schema_version = FLASH_SCHEMA_VERSION;
    write_settings.sequence = ++current_sequence;

    ssize_t len = nvs_write(&nvs, NVS_SETTINGS_KEY, &write_settings, sizeof(flash_t));
    if (len < 0) {
        printf("[flash_nrf] NVS write failed: %zd\n", len);
        return;
    }

    printf("[flash_nrf] Saved to NVS (seq=%lu)\n", (unsigned long)write_settings.sequence);
    save_pending = false;
}

void flash_save_force(const flash_t* settings)
{
    flash_save_now(settings);
}

void flash_task(void)
{
    if (!save_pending) return;

    uint32_t now = platform_time_ms();
    if (now - last_change_ms >= SAVE_DEBOUNCE_MS) {
        flash_save_now(&pending_settings);
    }
}

void flash_on_bt_disconnect(void)
{
    // No-op on nRF52840
}

bool flash_has_pending_write(void)
{
    return save_pending;
}

// ============================================================================
// Custom Profile Helpers
// ============================================================================

void custom_profile_init(custom_profile_t* profile, const char* name)
{
    if (!profile) return;
    memset(profile, 0, sizeof(custom_profile_t));
    if (name) {
        strncpy(profile->name, name, CUSTOM_PROFILE_NAME_LEN - 1);
        profile->name[CUSTOM_PROFILE_NAME_LEN - 1] = '\0';
    }
    memset(profile->button_map, BUTTON_MAP_PASSTHROUGH, CUSTOM_PROFILE_BUTTON_COUNT);
    profile->left_stick_sens = 100;
    profile->right_stick_sens = 100;
    profile->flags = 0;
}

uint32_t custom_profile_apply_buttons(const custom_profile_t* profile, uint32_t buttons)
{
    if (!profile) return buttons;
    uint32_t output = 0;
    for (int i = 0; i < CUSTOM_PROFILE_BUTTON_COUNT; i++) {
        if (buttons & (1u << i)) {
            uint8_t mapping = profile->button_map[i];
            if (mapping == BUTTON_MAP_PASSTHROUGH) {
                output |= (1u << i);
            } else if (mapping == BUTTON_MAP_DISABLED) {
                // disabled
            } else if (mapping >= 1 && mapping <= CUSTOM_PROFILE_BUTTON_COUNT) {
                output |= (1u << (mapping - 1));
            }
        }
    }
    return output;
}

const custom_profile_t* flash_get_custom_profile(const flash_t* settings, uint8_t index)
{
    if (!settings) return NULL;
    if (index >= settings->custom_profile_count) return NULL;
    if (index >= CUSTOM_PROFILE_MAX_COUNT) return NULL;
    return &settings->profiles[index];
}

flash_t* flash_get_settings(void)
{
    if (!runtime_settings_loaded) return NULL;
    return &runtime_settings;
}

uint8_t flash_get_active_profile_index(void)
{
    if (!runtime_settings_loaded) return 0;
    return runtime_settings.active_profile_index;
}

void flash_set_active_profile_index(uint8_t index)
{
    if (!runtime_settings_loaded) return;
    runtime_settings.active_profile_index = index;
    flash_save(&runtime_settings);
}

uint8_t flash_get_total_profile_count(void)
{
    if (!runtime_settings_loaded) return 1;
    return 1 + runtime_settings.custom_profile_count;
}

const custom_profile_t* flash_get_active_custom_profile(void)
{
    if (!runtime_settings_loaded) return NULL;
    uint8_t index = runtime_settings.active_profile_index;
    if (index == 0) return NULL;
    return flash_get_custom_profile(&runtime_settings, index - 1);
}

void flash_cycle_profile_next(void)
{
    if (!runtime_settings_loaded) return;
    uint8_t total = flash_get_total_profile_count();
    if (total <= 1) return;
    uint8_t current = runtime_settings.active_profile_index;
    uint8_t next = (current + 1) % total;
    flash_set_active_profile_index(next);
}

void flash_cycle_profile_prev(void)
{
    if (!runtime_settings_loaded) return;
    uint8_t total = flash_get_total_profile_count();
    if (total <= 1) return;
    uint8_t current = runtime_settings.active_profile_index;
    uint8_t prev = (current == 0) ? (total - 1) : (current - 1);
    flash_set_active_profile_index(prev);
}
