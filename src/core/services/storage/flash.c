// core/services/storage/flash.c - Persistent settings storage in flash memory

#include "core/services/storage/flash.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "tusb.h"
#include <string.h>
#include <stdio.h>

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
// - RP2040 flash is memory-mapped at XIP_BASE (0x10000000)
// - BTstack uses the last 8KB (2 sectors) for Bluetooth bond storage
// - We use the sector before that for settings storage
// - Flash writes require erasing entire 4KB sectors
// - Flash writes must be 256-byte aligned

#define SETTINGS_MAGIC 0x47435052  // "GCPR" - GameCube Profile
// BTstack uses last 8KB (2 sectors) for BT bond storage
// We use the sector before that for our settings
#define BTSTACK_FLASH_RESERVED (FLASH_SECTOR_SIZE * 2)  // 8KB for BTstack
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - BTSTACK_FLASH_RESERVED - FLASH_SECTOR_SIZE)
#define SAVE_DEBOUNCE_MS 5000  // Wait 5 seconds after last change before writing

// Pending save state
static bool save_pending = false;
static absolute_time_t last_change_time;
static flash_t pending_settings;

void flash_init(void)
{
    save_pending = false;
}

// Load settings from flash (returns true if valid settings found)
bool flash_load(flash_t* settings)
{
    // Flash is memory-mapped at XIP_BASE, so we can read it directly
    const flash_t* flash_settings = (const flash_t*)(XIP_BASE + FLASH_TARGET_OFFSET);

    // Validate magic number
    if (flash_settings->magic != SETTINGS_MAGIC) {
        return false;  // No valid settings in flash
    }

    // Copy settings from flash to RAM
    memcpy(settings, flash_settings, sizeof(flash_t));
    return true;
}

// Save settings to flash (debounced - actual write happens after delay)
void flash_save(const flash_t* settings)
{
    // Store settings and mark as pending
    memcpy(&pending_settings, settings, sizeof(flash_t));
    pending_settings.magic = SETTINGS_MAGIC;  // Ensure magic is set
    save_pending = true;
    last_change_time = get_absolute_time();
}

// Flash write function executed in RAM (safe from XIP conflicts)
static void __no_inline_not_in_flash_func(flash_write_worker)(void* param)
{
    flash_t* write_settings = (flash_t*)param;

    // Erase the settings sector (4KB)
    flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);

    // Write settings (must be 256-byte aligned)
    flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t*)write_settings, FLASH_PAGE_SIZE);
}

// Force immediate save (bypasses debouncing - use sparingly)
void flash_save_now(const flash_t* settings)
{
    // Use static to ensure it persists during flash operations
    static flash_t write_settings;
    memcpy(&write_settings, settings, sizeof(flash_t));
    write_settings.magic = SETTINGS_MAGIC;

    printf("[flash] Saving to flash at offset 0x%X...\n", FLASH_TARGET_OFFSET);
    printf("[flash] magic=0x%08X, profile=%d, usb_mode=%d\n",
           write_settings.magic, write_settings.active_profile_index,
           write_settings.usb_output_mode);
    flush_output();  // Flush debug output before flash operation

    // Try flash_safe_execute first (handles multicore safely)
    int result = flash_safe_execute(flash_write_worker, &write_settings, UINT32_MAX);

    if (result != PICO_OK) {
        printf("[flash] flash_safe_execute failed (%d), trying direct write...\n", result);
        flush_output();

        // Fallback: direct flash write with interrupts disabled
        // Safe when Core 1 is not running or not accessing flash
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
        flash_range_program(FLASH_TARGET_OFFSET, (const uint8_t*)&write_settings, FLASH_PAGE_SIZE);
        restore_interrupts(ints);

        printf("[flash] Direct write complete\n");
    } else {
        printf("[flash] Write complete\n");
    }

    // Verify the write
    const flash_t* verify = (const flash_t*)(XIP_BASE + FLASH_TARGET_OFFSET);
    printf("[flash] Verify: magic=0x%08X, profile=%d, usb_mode=%d\n",
           verify->magic, verify->active_profile_index, verify->usb_output_mode);
    flush_output();

    save_pending = false;
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
        // Debounce period elapsed - perform the write
        flash_save_now(&pending_settings);
    }
}
