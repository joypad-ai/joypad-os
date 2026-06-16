// flash_wch.c - Settings storage for CH32V307 (SRAM-retained across soft reset)
//
// Implements the flash.h API with an SRAM backing store placed in the .noinit
// section, which the startup .bss-zero loop skips and a CH32 NVIC_SystemReset
// does NOT clear. This lets the USB output mode survive the reboot that
// usbd_set_mode() triggers to re-enumerate (the USBHS only re-enumerates cleanly
// via a full reset). It does NOT survive a power cycle — a real on-chip-flash
// backend can replace these bodies later for true persistence. Mirrors
// flash_nrf.c's contract so the shared usbd/router/cdc code links unchanged.
//
// IMPORTANT (see memory "ESP/nRF Flash Stub Sync"): every flash_set_*/flash_get_*
// declared in flash.h MUST have a definition here or the link fails.

#include "core/services/storage/flash.h"
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

#define SETTINGS_MAGIC   0x47435052  // "GCPR"

// .noinit: retained across a soft reset (not zeroed by startup) so the saved USB
// mode survives the mode-switch reboot. Garbage on a cold power-on — validated below.
__attribute__((section(".noinit"))) static flash_t runtime_settings;
static bool runtime_settings_loaded = false;

static void flash_set_defaults(void)
{
    memset(&runtime_settings, 0, sizeof(flash_t));
    runtime_settings.magic = SETTINGS_MAGIC;
    runtime_settings.sequence = 0;
    runtime_settings.active_profile_index = 0;
    runtime_settings.custom_profile_count = 0;
    runtime_settings.schema_version = FLASH_SCHEMA_VERSION;
}

void flash_init(void)
{
    // If the retained buffer already holds valid settings, it survived a soft
    // reset (e.g. the mode-switch reboot) — keep it. Otherwise it's cold-boot
    // garbage; initialise to defaults.
    if (runtime_settings.magic == SETTINGS_MAGIC &&
        runtime_settings.schema_version == FLASH_SCHEMA_VERSION) {
        runtime_settings_loaded = true;
        printf("[flash_wch] Retained settings across reset (mode=%u)\n",
               (unsigned)runtime_settings.usb_output_mode);
        return;
    }
    flash_set_defaults();
    runtime_settings_loaded = true;
    printf("[flash_wch] Cold boot — SRAM-retained defaults\n");
}

void flash_factory_reset(void)
{
    flash_set_defaults();
    runtime_settings_loaded = true;
    printf("[flash_wch] Factory reset (defaults)\n");
}

bool flash_load(flash_t* settings)
{
    // Nothing persisted — caller falls back to defaults.
    (void)settings;
    return false;
}

void flash_save(const flash_t* settings)
{
    if (!settings) return;
    memcpy(&runtime_settings, settings, sizeof(flash_t));
    runtime_settings.magic = SETTINGS_MAGIC;
    runtime_settings.schema_version = FLASH_SCHEMA_VERSION;
    runtime_settings_loaded = true;
}

void flash_save_now(const flash_t* settings)   { flash_save(settings); }
void flash_save_force(const flash_t* settings) { flash_save(settings); }
void flash_task(void) {}
void flash_on_bt_disconnect(void) {}
bool flash_has_pending_write(void) { return false; }

// ============================================================================
// Custom Profile Helpers (identical contract to flash_nrf.c)
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
    return runtime_settings_loaded ? &runtime_settings : NULL;
}

uint8_t flash_get_active_profile_index(void)
{
    return runtime_settings_loaded ? runtime_settings.active_profile_index : 0;
}

void flash_set_active_profile_index(uint8_t index)
{
    if (!runtime_settings_loaded) return;
    runtime_settings.active_profile_index = index;
}

void flash_set_active_profile_index_deferred(uint8_t index)
{
    flash_set_active_profile_index(index);
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
    flash_set_active_profile_index((current + 1) % total);
}

void flash_cycle_profile_prev(void)
{
    if (!runtime_settings_loaded) return;
    uint8_t total = flash_get_total_profile_count();
    if (total <= 1) return;
    uint8_t current = runtime_settings.active_profile_index;
    flash_set_active_profile_index((current == 0) ? (total - 1) : (current - 1));
}

void flash_set_dpad_mode(uint8_t mode)
{
    if (mode > 2) return;
    if (!runtime_settings_loaded) return;
    runtime_settings.dpad_mode = mode;
    runtime_settings.router_saved = 1;
}

void flash_set_shoulder_swap(uint8_t on)
{
    if (!runtime_settings_loaded) return;
    runtime_settings.shoulder_swap = on ? 1 : 0;
    runtime_settings.router_saved = 1;
}

void flash_set_deadzone(uint8_t dz)
{
    if (dz > 127) dz = 127;
    if (!runtime_settings_loaded) return;
    runtime_settings.deadzone = dz;
    runtime_settings.router_saved = 1;
}

// ----------------------------------------------------------------------------
// RAM-only ephemeral state for joypad-live (PROFILE.SELECT/APPLY, OVERLAY.SET)
// ----------------------------------------------------------------------------

static runtime_overlay_t overlay_slot;
static bool              overlay_active_flag = false;
static custom_profile_t  ephemeral_profile;
static bool              ephemeral_active = false;
static int8_t            ephemeral_active_idx = -1;

void flash_select_active_profile_index(uint8_t index)
{
    ephemeral_active = false;
    if (!runtime_settings_loaded) return;
    uint8_t max_index = runtime_settings.custom_profile_count;
    if (index > max_index) index = max_index;
    ephemeral_active_idx = (int8_t)index;
}

void flash_apply_ephemeral_profile(const custom_profile_t* cp)
{
    if (!cp) { ephemeral_active = false; return; }
    ephemeral_profile = *cp;
    ephemeral_active = true;
}

void flash_clear_ephemeral_profile(void) { ephemeral_active = false; }
bool flash_has_ephemeral_profile(void)   { return ephemeral_active; }

void flash_set_overlay(const runtime_overlay_t* o)
{
    if (!o) { overlay_active_flag = false; return; }
    overlay_slot = *o;
    overlay_active_flag = true;
}

void flash_clear_overlay(void) { overlay_active_flag = false; }

const runtime_overlay_t* flash_get_overlay(void)
{
    return overlay_active_flag ? &overlay_slot : NULL;
}
