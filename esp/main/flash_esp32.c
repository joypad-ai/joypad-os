// flash_esp32.c - NVS-based flash storage for ESP32
//
// Implements the flash.h API using ESP-IDF NVS (Non-Volatile Storage).
// Same flash_t struct, just stored in NVS instead of raw flash.

#include "core/services/storage/flash.h"
#include "platform/platform.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

#define NVS_NAMESPACE "joypad"
#define NVS_KEY_SETTINGS "settings"
#define SETTINGS_MAGIC 0x47435052  // "GCPR"
#define SAVE_DEBOUNCE_MS 5000

static nvs_handle_t nvs_hdl;
static bool nvs_opened = false;
static bool save_pending = false;
static uint32_t last_change_ms = 0;
static flash_t pending_settings;
static uint32_t current_sequence = 0;

// Runtime settings
static flash_t runtime_settings;
static bool runtime_settings_loaded = false;

// BT connection check (weak symbol)
__attribute__((weak)) uint8_t btstack_classic_get_connection_count(void) { return 0; }

void flash_init(void)
{
    save_pending = false;

    // Open NVS namespace
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_hdl);
    if (err != ESP_OK) {
        printf("[flash] NVS open failed: %s\n", esp_err_to_name(err));
        return;
    }
    nvs_opened = true;

    // Load runtime settings
    if (!flash_load(&runtime_settings)) {
        memset(&runtime_settings, 0, sizeof(flash_t));
        runtime_settings.magic = SETTINGS_MAGIC;
        runtime_settings.sequence = 0;
        runtime_settings.active_profile_index = 0;
        runtime_settings.custom_profile_count = 0;
        printf("[flash] No valid settings found, starting fresh\n");
    } else {
        current_sequence = runtime_settings.sequence;
        printf("[flash] Loaded settings (seq=%lu)\n", (unsigned long)current_sequence);
    }
    runtime_settings_loaded = true;
}

bool flash_load(flash_t* settings)
{
    if (!nvs_opened) return false;

    size_t size = sizeof(flash_t);
    esp_err_t err = nvs_get_blob(nvs_hdl, NVS_KEY_SETTINGS, settings, &size);
    if (err != ESP_OK || size != sizeof(flash_t)) {
        return false;
    }

    if (settings->magic != SETTINGS_MAGIC) {
        return false;
    }

    current_sequence = settings->sequence;
    return true;
}

void flash_save(const flash_t* settings)
{
    memcpy(&pending_settings, settings, sizeof(flash_t));
    pending_settings.magic = SETTINGS_MAGIC;
    save_pending = true;
    last_change_ms = platform_time_ms();
}

void flash_save_now(const flash_t* settings)
{
    if (!nvs_opened) return;

    static flash_t write_settings;
    memcpy(&write_settings, settings, sizeof(flash_t));
    write_settings.magic = SETTINGS_MAGIC;
    write_settings.sequence = ++current_sequence;

    esp_err_t err = nvs_set_blob(nvs_hdl, NVS_KEY_SETTINGS, &write_settings, sizeof(flash_t));
    if (err != ESP_OK) {
        printf("[flash] NVS write failed: %s\n", esp_err_to_name(err));
        return;
    }

    err = nvs_commit(nvs_hdl);
    if (err != ESP_OK) {
        printf("[flash] NVS commit failed: %s\n", esp_err_to_name(err));
        return;
    }

    printf("[flash] Saved to NVS (seq=%lu)\n", (unsigned long)write_settings.sequence);
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
    // No-op on ESP32
}

bool flash_has_pending_write(void)
{
    return save_pending;
}

// ============================================================================
// Custom Profile Helpers (shared implementation from flash.c)
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
    uint8_t max_index = runtime_settings.custom_profile_count;
    if (index > max_index) index = max_index;
    if (runtime_settings.active_profile_index != index) {
        runtime_settings.active_profile_index = index;
        flash_save(&runtime_settings);
        printf("[flash] Active profile set to %d\n", index);
    }
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
