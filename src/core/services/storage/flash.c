// core/services/storage/flash.c - Persistent settings storage in flash memory
//
// Uses dual-sector journaled storage for BT-safe writes:
// - Two 4KB sectors = 32 x 256-byte slots total
// - Each save writes to next empty slot (page program only, ~1ms)
// - When one sector fills, erase the OTHER sector and continue there
// - This allows sector erases while valid data remains readable
// - No need to defer erases for BT - always safe to erase inactive sector

#include "core/services/storage/flash.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "tusb.h"
#include <string.h>
#include <stdio.h>

// BT connection check (weak symbol - overridden when BT is enabled)
// Note: With dual-sector design, we no longer need to defer erases for BT
__attribute__((weak)) uint8_t btstack_classic_get_connection_count(void) { return 0; }

// Helper to flush debug output before critical sections
static void flush_output(void)
{
#if CFG_TUD_ENABLED
    tud_task();
    sleep_ms(20);
    tud_task();
#else
    sleep_ms(20);
#endif
}

// Flash memory layout
// - RP2040/RP2350 flash is memory-mapped at XIP_BASE (0x10000000)
// - BTstack uses 8KB (2 sectors) for Bluetooth bond storage
// - We use TWO sectors before BTstack for settings storage (dual-sector journal)
// - Flash writes require erasing entire 4KB sectors
// - Flash page writes are 256-byte aligned
//
// Layout differs by platform:
// - RP2040: BTstack at end of flash (last 2 sectors)
// - RP2350 (A2): BTstack 1 sector from end (due to RP2350-E10 errata)
//
// Dual-sector layout (from end):
//   [... code ...] [Sector B] [Sector A] [BTstack 8KB] [end]
// Sector A is at the original offset (preserves existing user data on upgrade)

#define SETTINGS_MAGIC 0x47435052  // "GCPR" - GameCube Profile
#define BTSTACK_FLASH_SIZE (FLASH_SECTOR_SIZE * 2)  // 8KB for BTstack

#if PICO_RP2350 && PICO_RP2350_A2_SUPPORTED
// RP2350 layout: [... | sector B | sector A | btstack (2 sectors) | reserved (1 sector)]
#define FLASH_SECTOR_A_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE - BTSTACK_FLASH_SIZE - FLASH_SECTOR_SIZE)
#define FLASH_SECTOR_B_OFFSET (FLASH_SECTOR_A_OFFSET - FLASH_SECTOR_SIZE)
#else
// RP2040 layout: [... | sector B | sector A | btstack (2 sectors)]
#define FLASH_SECTOR_A_OFFSET (PICO_FLASH_SIZE_BYTES - BTSTACK_FLASH_SIZE - FLASH_SECTOR_SIZE)
#define FLASH_SECTOR_B_OFFSET (FLASH_SECTOR_A_OFFSET - FLASH_SECTOR_SIZE)
#endif

// Journal configuration
#define JOURNAL_SLOT_SIZE FLASH_PAGE_SIZE  // 256 bytes per slot
#define SLOTS_PER_SECTOR (FLASH_SECTOR_SIZE / JOURNAL_SLOT_SIZE)  // 16 slots per sector
#define TOTAL_SLOT_COUNT (SLOTS_PER_SECTOR * 2)  // 32 slots total
#define SAVE_DEBOUNCE_MS 5000  // Wait 5 seconds after last change before writing

// Pending save state
static bool save_pending = false;
static absolute_time_t last_change_time;
static flash_t pending_settings;
static uint32_t current_sequence = 0;  // Current sequence number

// Runtime settings (loaded on init, updated on save)
static flash_t runtime_settings;
static bool runtime_settings_loaded = false;

// Get flash offset for a slot index (0-31)
// Slots 0-15 are in sector A, slots 16-31 are in sector B
static uint32_t get_slot_offset(uint8_t slot_index)
{
    if (slot_index < SLOTS_PER_SECTOR) {
        return FLASH_SECTOR_A_OFFSET + (slot_index * JOURNAL_SLOT_SIZE);
    } else {
        return FLASH_SECTOR_B_OFFSET + ((slot_index - SLOTS_PER_SECTOR) * JOURNAL_SLOT_SIZE);
    }
}

// Get pointer to a journal slot (0-31)
static const flash_t* get_slot(uint8_t slot_index)
{
    return (const flash_t*)(XIP_BASE + get_slot_offset(slot_index));
}

// Check if a slot is empty (erased state = 0xFFFFFFFF)
static bool is_slot_empty(uint8_t slot_index)
{
    const flash_t* slot = get_slot(slot_index);
    return slot->sequence == 0xFFFFFFFF;
}

// Find the newest valid entry (highest sequence number) across both sectors
// Returns slot index (0-31), or -1 if no valid entries
static int find_newest_slot(void)
{
    int newest_slot = -1;
    uint32_t highest_seq = 0;

    for (uint8_t i = 0; i < TOTAL_SLOT_COUNT; i++) {
        const flash_t* slot = get_slot(i);

        // Check for valid magic and non-empty sequence
        if (slot->magic == SETTINGS_MAGIC && slot->sequence != 0xFFFFFFFF) {
            if (newest_slot == -1 || slot->sequence > highest_seq) {
                highest_seq = slot->sequence;
                newest_slot = i;
            }
        }
    }

    return newest_slot;
}

// Find the next empty slot, searching from the sector containing newest data
// Returns slot index (0-31), or -1 if both sectors are full (shouldn't happen)
static int find_empty_slot(void)
{
    int newest = find_newest_slot();

    // Determine which sector to search first (the one with newest data)
    // If no data yet, start with sector A (preserves upgrade compatibility)
    bool start_with_a = (newest < 0 || newest < SLOTS_PER_SECTOR);

    if (start_with_a) {
        // Search sector A first (slots 0-15)
        for (uint8_t i = 0; i < SLOTS_PER_SECTOR; i++) {
            if (is_slot_empty(i)) {
                return i;
            }
        }
        // Sector A full - search sector B (slots 16-31)
        for (uint8_t i = SLOTS_PER_SECTOR; i < TOTAL_SLOT_COUNT; i++) {
            if (is_slot_empty(i)) {
                return i;
            }
        }
    } else {
        // Search sector B first (slots 16-31)
        for (uint8_t i = SLOTS_PER_SECTOR; i < TOTAL_SLOT_COUNT; i++) {
            if (is_slot_empty(i)) {
                return i;
            }
        }
        // Sector B full - search sector A (slots 0-15)
        for (uint8_t i = 0; i < SLOTS_PER_SECTOR; i++) {
            if (is_slot_empty(i)) {
                return i;
            }
        }
    }

    return -1;  // Both sectors full (shouldn't happen with proper erase logic)
}

// Get which sector a slot is in (0 = A, 1 = B)
static uint8_t get_slot_sector(uint8_t slot_index)
{
    return (slot_index < SLOTS_PER_SECTOR) ? 0 : 1;
}

void flash_init(void)
{
    save_pending = false;

    // Find current sequence number from flash (searches both sectors)
    int newest = find_newest_slot();
    if (newest >= 0) {
        current_sequence = get_slot(newest)->sequence;
        printf("[flash] Found newest slot %d (sector %c, seq=%lu)\n",
               newest, (newest < SLOTS_PER_SECTOR) ? 'A' : 'B',
               (unsigned long)current_sequence);
    } else {
        current_sequence = 0;
        printf("[flash] No valid settings found, starting fresh\n");
    }

    // Load runtime settings
    if (!flash_load(&runtime_settings)) {
        // No valid settings - initialize defaults
        memset(&runtime_settings, 0, sizeof(flash_t));
        runtime_settings.magic = SETTINGS_MAGIC;
        runtime_settings.sequence = 0;
        runtime_settings.active_profile_index = 0;  // Default profile
        runtime_settings.custom_profile_count = 0;
    }
    runtime_settings_loaded = true;
}

// Load settings from flash (returns true if valid settings found)
bool flash_load(flash_t* settings)
{
    int newest = find_newest_slot();

    if (newest < 0) {
        return false;  // No valid settings in flash
    }

    // Copy settings from flash to RAM
    const flash_t* slot = get_slot(newest);
    memcpy(settings, slot, sizeof(flash_t));
    current_sequence = slot->sequence;

    return true;
}

// Save settings to flash (debounced - actual write happens after delay)
void flash_save(const flash_t* settings)
{
    // Store settings and mark as pending
    memcpy(&pending_settings, settings, sizeof(flash_t));
    pending_settings.magic = SETTINGS_MAGIC;
    save_pending = true;
    last_change_time = get_absolute_time();
}

// Page program worker - only programs one page, no erase (~1ms)
// This is safe during BT as it only takes ~1ms
typedef struct {
    uint32_t offset;
    const uint8_t* data;
} page_program_params_t;

static void __no_inline_not_in_flash_func(page_program_worker)(void* param)
{
    page_program_params_t* p = (page_program_params_t*)param;
    flash_range_program(p->offset, p->data, FLASH_PAGE_SIZE);
}

// Sector erase worker - erases entire sector (~45ms)
// With dual-sector design, we always erase the inactive sector, so this is safe
typedef struct {
    uint32_t offset;
} sector_erase_params_t;

static void __no_inline_not_in_flash_func(sector_erase_worker)(void* param)
{
    sector_erase_params_t* p = (sector_erase_params_t*)param;
    flash_range_erase(p->offset, FLASH_SECTOR_SIZE);
}

// Write a single page to flash (BT-safe, ~1ms)
static bool flash_write_page(uint8_t slot_index, const flash_t* settings)
{
    static flash_t write_buffer;  // Static to persist during flash ops
    memcpy(&write_buffer, settings, sizeof(flash_t));

    uint32_t offset = get_slot_offset(slot_index);

    page_program_params_t params = {
        .offset = offset,
        .data = (const uint8_t*)&write_buffer
    };

    // Try flash_safe_execute first
    int result = flash_safe_execute(page_program_worker, &params, UINT32_MAX);

    if (result != PICO_OK) {
        // Fallback: direct write with interrupts disabled briefly
        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(offset, (const uint8_t*)&write_buffer, FLASH_PAGE_SIZE);
        restore_interrupts(ints);
    }

    return true;
}

// Erase a specific sector (0 = A, 1 = B)
// Safe to call anytime - we only erase the sector without valid data
static void flash_erase_sector(uint8_t sector)
{
    uint32_t offset = (sector == 0) ? FLASH_SECTOR_A_OFFSET : FLASH_SECTOR_B_OFFSET;
    printf("[flash] Erasing sector %c at offset 0x%lX...\n",
           (sector == 0) ? 'A' : 'B', (unsigned long)offset);
    flush_output();

    sector_erase_params_t params = { .offset = offset };

    // Try flash_safe_execute first
    int result = flash_safe_execute(sector_erase_worker, &params, UINT32_MAX);

    if (result != PICO_OK) {
        printf("[flash] flash_safe_execute failed (%d), trying direct erase...\n", result);
        flush_output();

        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(offset, FLASH_SECTOR_SIZE);
        restore_interrupts(ints);
    }

    printf("[flash] Sector erase complete\n");
}

// Force immediate save (bypasses debouncing)
// With dual-sector design, this is always safe - we erase the OTHER sector
void flash_save_now(const flash_t* settings)
{
    static flash_t write_settings;
    memcpy(&write_settings, settings, sizeof(flash_t));
    write_settings.magic = SETTINGS_MAGIC;
    write_settings.sequence = ++current_sequence;

    // Find next empty slot
    int slot = find_empty_slot();

    if (slot < 0) {
        // Both sectors full - find newest slot and erase the OTHER sector
        int newest = find_newest_slot();
        uint8_t newest_sector = (newest >= 0) ? get_slot_sector(newest) : 0;
        uint8_t erase_sector = (newest_sector == 0) ? 1 : 0;

        printf("[flash] Both sectors full, erasing sector %c\n",
               (erase_sector == 0) ? 'A' : 'B');
        flash_erase_sector(erase_sector);

        // Write to first slot of erased sector
        slot = (erase_sector == 0) ? 0 : SLOTS_PER_SECTOR;
    }

    printf("[flash] Writing to slot %d (seq=%lu) at offset 0x%lX\n",
           slot, (unsigned long)write_settings.sequence,
           (unsigned long)get_slot_offset(slot));

    flash_write_page(slot, &write_settings);

    // Verify the write
    const flash_t* verify = get_slot(slot);
    printf("[flash] Verify: magic=0x%08lX, seq=%lu, profile=%d, usb_mode=%d, orient=%d\n",
           (unsigned long)verify->magic, (unsigned long)verify->sequence,
           verify->active_profile_index, verify->usb_output_mode,
           verify->wiimote_orient_mode);

    save_pending = false;
}

// Force immediate save - same as flash_save_now() with dual-sector design
// Kept for API compatibility
void flash_save_force(const flash_t* settings)
{
    flash_save_now(settings);
}

// Task function to handle debounced flash writes (call from main loop)
void flash_task(void)
{
    if (!save_pending) {
        return;
    }

    // Check if debounce time has elapsed
    int64_t time_since_change = absolute_time_diff_us(last_change_time, get_absolute_time());
    if (time_since_change >= (SAVE_DEBOUNCE_MS * 1000)) {
        flash_save_now(&pending_settings);
    }
}

// Called when BT disconnects - kept for API compatibility
// With dual-sector design, no deferred erases needed
void flash_on_bt_disconnect(void)
{
    // No-op with dual-sector design
}

// Check if there's a pending write waiting
bool flash_has_pending_write(void)
{
    return save_pending;
}

// ============================================================================
// Custom Profile Helpers
// ============================================================================

// Initialize a custom profile to default values (passthrough)
void custom_profile_init(custom_profile_t* profile, const char* name)
{
    if (!profile) return;

    memset(profile, 0, sizeof(custom_profile_t));

    // Copy name (null-terminated)
    if (name) {
        strncpy(profile->name, name, CUSTOM_PROFILE_NAME_LEN - 1);
        profile->name[CUSTOM_PROFILE_NAME_LEN - 1] = '\0';
    }

    // All buttons passthrough (0x00)
    memset(profile->button_map, BUTTON_MAP_PASSTHROUGH, CUSTOM_PROFILE_BUTTON_COUNT);

    // Default sensitivities (100 = 1.0x)
    profile->left_stick_sens = 100;
    profile->right_stick_sens = 100;

    // No flags set
    profile->flags = 0;
}

// Apply button mapping from custom profile
// Returns remapped buttons, or original if profile is NULL
uint32_t custom_profile_apply_buttons(const custom_profile_t* profile, uint32_t buttons)
{
    if (!profile) return buttons;

    uint32_t output = 0;

    for (int i = 0; i < CUSTOM_PROFILE_BUTTON_COUNT; i++) {
        // Check if this input button is pressed
        if (buttons & (1u << i)) {
            uint8_t mapping = profile->button_map[i];

            if (mapping == BUTTON_MAP_PASSTHROUGH) {
                // Keep original button
                output |= (1u << i);
            } else if (mapping == BUTTON_MAP_DISABLED) {
                // Button disabled, don't output anything
            } else if (mapping >= 1 && mapping <= CUSTOM_PROFILE_BUTTON_COUNT) {
                // Remap to different button (1-based index in mapping)
                output |= (1u << (mapping - 1));
            }
        }
    }

    return output;
}

// Get custom profile by index (0-3), returns NULL if index >= count
const custom_profile_t* flash_get_custom_profile(const flash_t* settings, uint8_t index)
{
    if (!settings) return NULL;
    if (index >= settings->custom_profile_count) return NULL;
    if (index >= CUSTOM_PROFILE_MAX_COUNT) return NULL;

    return &settings->profiles[index];
}

// ============================================================================
// Custom Profile Runtime API
// ============================================================================

// Get the currently loaded flash settings (for runtime access)
flash_t* flash_get_settings(void)
{
    if (!runtime_settings_loaded) {
        return NULL;
    }
    return &runtime_settings;
}

// Get active custom profile index (0=Default/passthrough, 1-4=custom profiles)
uint8_t flash_get_active_profile_index(void)
{
    if (!runtime_settings_loaded) {
        return 0;
    }
    return runtime_settings.active_profile_index;
}

// Set active custom profile index (saves to flash with debouncing)
void flash_set_active_profile_index(uint8_t index)
{
    if (!runtime_settings_loaded) {
        return;
    }

    // Validate index (0=default, 1-N=custom profiles)
    uint8_t max_index = runtime_settings.custom_profile_count;
    if (index > max_index) {
        index = max_index;
    }

    if (runtime_settings.active_profile_index != index) {
        runtime_settings.active_profile_index = index;
        flash_save(&runtime_settings);

        printf("[flash] Active profile set to %d\n", index);
    }
}

// Get total profile count (1 default + custom_profile_count)
uint8_t flash_get_total_profile_count(void)
{
    if (!runtime_settings_loaded) {
        return 1;  // At least the default profile
    }
    return 1 + runtime_settings.custom_profile_count;
}

// Get active custom profile (returns NULL for index 0/default or if invalid)
const custom_profile_t* flash_get_active_custom_profile(void)
{
    if (!runtime_settings_loaded) {
        return NULL;
    }

    uint8_t index = runtime_settings.active_profile_index;
    if (index == 0) {
        return NULL;  // Default profile (passthrough)
    }

    // Custom profiles are stored at indices 0-3 for user indices 1-4
    return flash_get_custom_profile(&runtime_settings, index - 1);
}

// Cycle to next profile (wraps around)
void flash_cycle_profile_next(void)
{
    if (!runtime_settings_loaded) {
        return;
    }

    uint8_t total = flash_get_total_profile_count();
    if (total <= 1) {
        return;  // No custom profiles to cycle
    }

    uint8_t current = runtime_settings.active_profile_index;
    uint8_t next = (current + 1) % total;
    flash_set_active_profile_index(next);
}

// Cycle to previous profile (wraps around)
void flash_cycle_profile_prev(void)
{
    if (!runtime_settings_loaded) {
        return;
    }

    uint8_t total = flash_get_total_profile_count();
    if (total <= 1) {
        return;  // No custom profiles to cycle
    }

    uint8_t current = runtime_settings.active_profile_index;
    uint8_t prev = (current == 0) ? (total - 1) : (current - 1);
    flash_set_active_profile_index(prev);
}
