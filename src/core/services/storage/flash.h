// core/services/storage/flash.h - Persistent settings storage in flash memory
//
// Uses journaled storage for BT-safe writes:
// - 4KB sector = 16 x 256-byte slots (ring buffer)
// - Each save writes to next empty slot (page program only, ~1ms)
// - Sector erase (~45ms) only when full AND BT is idle
// - Sequence number identifies newest entry
//
// Settings persist across power cycles and firmware updates (unless flash is erased).

#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Custom Profile Storage
// ============================================================================

#define CUSTOM_PROFILE_NAME_LEN 12
#define CUSTOM_PROFILE_BUTTON_COUNT 18
#define CUSTOM_PROFILE_MAX_COUNT 4

// Button mapping values:
// 0x00 = passthrough (no remap, keep original button)
// 0x01-0x18 = remap to JP_BUTTON_* (1-based: 1=B1, ... 18=A2, ... 23=F1, 24=F2)
// 0xFF = disabled (button press ignored)
#define BUTTON_MAP_MAX_TARGET 24  // Max valid remap target (F2)
#define BUTTON_MAP_PASSTHROUGH 0x00
#define BUTTON_MAP_DISABLED    0xFF

// Custom profile structure (56 bytes)
// Stored in flash, user-configurable via web config
typedef struct {
    char name[CUSTOM_PROFILE_NAME_LEN];  // 12 bytes, null-terminated
    uint8_t button_map[CUSTOM_PROFILE_BUTTON_COUNT]; // 18 bytes
    // Button indices: 0=B1, 1=B2, 2=B3, 3=B4, 4=L1, 5=R1, 6=L2, 7=R2,
    //                 8=S1, 9=S2, 10=L3, 11=R3, 12=DU, 13=DD, 14=DL, 15=DR, 16=A1, 17=A2
    uint8_t left_stick_sens;   // 0-200 (100 = 1.0x, 50 = 0.5x, 200 = 2.0x)
    uint8_t right_stick_sens;  // 0-200
    uint8_t flags;             // Bit 0: swap sticks, Bit 1: invert LY, Bit 2: invert RY
    uint8_t socd_mode;         // SOCD cleaning mode (0=passthrough, 1=neutral, 2=up-priority, 3=last-win)
    uint8_t reserved[22];      // Future use
} custom_profile_t;

// Profile flags
#define PROFILE_FLAG_SWAP_STICKS  (1 << 0)
#define PROFILE_FLAG_INVERT_LY    (1 << 1)
#define PROFILE_FLAG_INVERT_RY    (1 << 2)

// ============================================================================
// Flash Settings Structure
// ============================================================================

// Settings structure stored in flash (256 bytes = 1 flash page)
// 16 entries fit in one 4KB sector for journaled writes
typedef struct {
    // Header (8 bytes)
    uint32_t magic;              // Validation magic number (0x47435052 = "GCPR")
    uint32_t sequence;           // Sequence number (higher = newer, 0xFFFFFFFF = empty)

    // Global settings (4 bytes)
    uint8_t active_profile_index; // Currently selected profile (0=default, 1-4=custom)
    uint8_t usb_output_mode;     // USB device output mode (0=HID, 1=XboxOG, etc.)
    uint8_t wiimote_orient_mode; // Wiimote orientation mode (0=Auto, 1=Horizontal, 2=Vertical)
    uint8_t custom_profile_count; // Number of custom profiles (0-4)

    // Global settings (continued)
    uint8_t ble_output_mode;     // BLE output mode (0=Standard composite, 1=Xbox BLE)
    uint8_t router_saved;        // Non-zero if router settings were explicitly saved
    uint8_t routing_mode;        // Router mode (0=simple, 1=merge, 2=broadcast)
    uint8_t merge_mode;          // Merge mode (0=priority, 1=blend, 2=all)
    uint8_t dpad_mode;           // D-pad mode (0=dpad, 1=left stick, 2=right stick)
    uint8_t bt_input_enabled;    // BT Central scanning (0=off, 1=on)

    // Reserved for future global settings (14 bytes)
    uint8_t reserved[14];

    // Custom profiles (4 x 56 = 224 bytes)
    custom_profile_t profiles[CUSTOM_PROFILE_MAX_COUNT];
} flash_t;

// Verify size at compile time
_Static_assert(sizeof(flash_t) == 256, "flash_t must be exactly 256 bytes");
_Static_assert(sizeof(custom_profile_t) == 56, "custom_profile_t must be exactly 56 bytes");

// ============================================================================
// Flash API
// ============================================================================

// Initialize flash settings system
void flash_init(void);

// Load settings from flash (returns true if valid settings found)
bool flash_load(flash_t* settings);

// Save settings to flash (debounced - actual write happens after delay)
void flash_save(const flash_t* settings);

// Force immediate save (bypasses debouncing - use sparingly)
void flash_save_now(const flash_t* settings);

// Force immediate save, ignoring BT-active check (use before device reset)
void flash_save_force(const flash_t* settings);

// Task function to handle debounced flash writes (call from main loop)
void flash_task(void);

// Notify flash system that BT has disconnected (safe to write now)
void flash_on_bt_disconnect(void);

// Check if there's a pending flash write waiting for BT to be idle
bool flash_has_pending_write(void);

// ============================================================================
// Custom Profile Helpers
// ============================================================================

// Initialize a custom profile to default values (passthrough)
void custom_profile_init(custom_profile_t* profile, const char* name);

// Apply button mapping from custom profile
// Returns remapped buttons, or original if profile is NULL
uint32_t custom_profile_apply_buttons(const custom_profile_t* profile, uint32_t buttons);

// Get custom profile by index (0-3), returns NULL if index >= count
const custom_profile_t* flash_get_custom_profile(const flash_t* settings, uint8_t index);

// ============================================================================
// Custom Profile Runtime API
// ============================================================================

// Get the currently loaded flash settings (for runtime access)
flash_t* flash_get_settings(void);

// Get active custom profile index (0=Default/passthrough, 1-4=custom profiles)
uint8_t flash_get_active_profile_index(void);

// Set active custom profile index (saves to flash with debouncing)
void flash_set_active_profile_index(uint8_t index);

// Get total profile count (1 default + custom_profile_count)
uint8_t flash_get_total_profile_count(void);

// Get active custom profile (returns NULL for index 0/default or if invalid)
const custom_profile_t* flash_get_active_custom_profile(void);

// Cycle to next/previous profile (wraps around)
void flash_cycle_profile_next(void);
void flash_cycle_profile_prev(void);

#endif // FLASH_H
