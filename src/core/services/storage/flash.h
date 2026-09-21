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
#define CUSTOM_PROFILE_BUTTON_COUNT 26
#define CUSTOM_PROFILE_MAX_COUNT 4

// Highest valid value of flash_t.dpad_mode. Single source of truth for both
// the setter (flash_set_dpad_mode) and the load-time sanitiser
// (flash_sanitize_record) — those two disagreed once (#242 raised the setter
// to 3 for the d-pad slider's 4th position while the sanitiser still clamped
// at 2, so every reboot reverted it AND wiped router_saved with it).
#define FLASH_DPAD_MODE_MAX 3

// wireless_policy values — which output wins when USB and BLE could both be
// live (see the field comment in flash_t).
#define WIRELESS_POLICY_BOTH 0
#define WIRELESS_POLICY_USB  1
#define WIRELESS_POLICY_BLE  2
#define WIRELESS_POLICY_MAX  2

// Button mapping values:
// 0x00 = passthrough (no remap, keep original button)
// 0x01-0x1A = remap to JP_BUTTON_* (1-based: 1=B1, ... 18=A2, ... 24=F2, 25=L5, 26=R5)
// 0xFF = disabled (button press ignored)
#define BUTTON_MAP_MAX_TARGET 26  // Max valid remap target (R5)
#define BUTTON_MAP_PASSTHROUGH 0x00
#define BUTTON_MAP_DISABLED    0xFF

// Custom profile structure (56 bytes)
// Stored in flash, user-configurable via web config
typedef struct {
    char name[CUSTOM_PROFILE_NAME_LEN];  // 12 bytes, null-terminated
    uint8_t button_map[CUSTOM_PROFILE_BUTTON_COUNT]; // 26 bytes
    // Button indices (bit position): 0=B1, 1=B2, 2=B3, 3=B4, 4=L1, 5=R1, 6=L2, 7=R2,
    //                 8=S1, 9=S2, 10=L3, 11=R3, 12=DU, 13=DD, 14=DL, 15=DR, 16=A1, 17=A2,
    //                 18=A3, 19=A4, 20=L4, 21=R4, 22=F1, 23=F2, 24=L5, 25=R5
    uint8_t left_stick_sens;   // 0-200 (100 = 1.0x, 50 = 0.5x, 200 = 2.0x)
    uint8_t right_stick_sens;  // 0-200
    uint8_t flags;             // Bit 0: swap sticks, Bit 1: invert LY, Bit 2: invert RY,
                               // Bit 3: invert LX, Bit 4: invert RX
    uint8_t socd_mode;         // SOCD cleaning mode (0=passthrough, 1=neutral, 2=up-priority, 3=last-win)
    uint8_t l2_threshold;      // Analog L2 → digital threshold; 0 = use default (128)
    uint8_t r2_threshold;      // Analog R2 → digital threshold; 0 = use default (128)
    // Per-profile turbo / auto-fire (web-configurable). Backward-compatible:
    // carved from the former reserved[] which was zero-initialized, so old
    // records read as rate 0 (off) with an empty mask — no schema bump needed.
    uint8_t autofire_rate;     // 0 = off, 1..6 = AUTOFIRE_RATE ladder index (profile.h)
    uint8_t turbo_mask[4];     // 32-bit LE bitfield; bit i = physical button index i
                               // (0=B1 .. 25=R5, same index space as button_map)
    // Generic device/output mode, interpreted by the active app's output driver
    // (e.g. usb2pce: 0=2-button, 1=6-button, 2=3-button Sel, 3=3-button Run). The
    // app reports its mode names via profile_config_t. Carved from reserved[] →
    // zero-init means "mode 0" (the app's default) on old flashes; no schema bump.
    uint8_t output_mode;
    uint8_t reserved[6];       // 1 + 4 + 1 + 6 = 12, so struct stays exactly 56 bytes
} custom_profile_t;

// Turbo bit accessors, keyed on physical (input) button index 0..25.
static inline bool custom_profile_turbo_get(const custom_profile_t* p, uint8_t i) {
    if (i >= CUSTOM_PROFILE_BUTTON_COUNT) return false;
    return (p->turbo_mask[i >> 3] >> (i & 7)) & 1u;
}
static inline void custom_profile_turbo_set(custom_profile_t* p, uint8_t i, bool on) {
    if (i >= CUSTOM_PROFILE_BUTTON_COUNT) return;
    uint8_t bit = 1u << (i & 7);
    if (on) p->turbo_mask[i >> 3] |= bit;
    else    p->turbo_mask[i >> 3] &= ~bit;
}

// Profile flags
#define PROFILE_FLAG_SWAP_STICKS  (1 << 0)
#define PROFILE_FLAG_INVERT_LY    (1 << 1)
#define PROFILE_FLAG_INVERT_RY    (1 << 2)
#define PROFILE_FLAG_INVERT_LX    (1 << 3)
#define PROFILE_FLAG_INVERT_RX    (1 << 4)

// ============================================================================
// Flash Settings Structure
// ============================================================================
//
// Schema versioning: bump FLASH_SCHEMA_VERSION whenever fields are added,
// removed, or reinterpreted. On load, a magic-OK record with mismatched
// schema_version is treated as stale and wiped — see flash_init().
//
// Pre-versioning records (v1.9.0 and v2.0.0) have schema_version == 0
// because the byte was reserved and zero-initialized. Bumping to 1 forces
// a one-time wipe for those users; subsequent bumps wipe their own range.
//
// v2 (v2.4.0): custom_profile_t.button_map grew 18 → 26 bytes (into former
// reserved space) so L4/R4/F1/F2/L5/R5 are remappable; the struct stays
// 56 bytes but those 8 bytes are reinterpreted, so wipe stale records.
#define FLASH_SCHEMA_VERSION 2

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
    uint8_t ble_output_mode;     // BLE output mode — ble_output_mode_t
                                 // (0=Standard composite, 1=Xbox BLE, 2=SInput)
    uint8_t router_saved;        // Non-zero if router settings were explicitly saved
    uint8_t routing_mode;        // Router mode (0=simple, 1=merge, 2=broadcast)
    uint8_t merge_mode;          // Merge mode (0=priority, 1=blend, 2=all)
    uint8_t dpad_mode;           // D-pad mode — 0..FLASH_DPAD_MODE_MAX
                                 // (0=normal, 1=dpad<->Lstick, 2=dpad<->Rstick, 3=Lstick<->Rstick)
    uint8_t bt_input_enabled;    // BT Central scanning (0=off, 1=on)

    // Native output pin overrides
    uint8_t joybus_data_pin;     // 0 = compile-time default, 1-28 = override GPIO
    uint8_t wii_sda_pin;         // 0 = compile-time default, 1-28 = override GPIO
    uint8_t wii_scl_pin;         // 0 = compile-time default, 1-28 = override GPIO
    uint8_t wii_mode;            // 0 = compile-time default, 1+ = wii_device_emulation_t + 1

    // Schema version (1 byte) — must equal FLASH_SCHEMA_VERSION on load.
    // Was reserved[0] in pre-v2.1 firmware (always zero) → reads as 0 on
    // upgrade from v1.9.0 / v2.0.0, triggering a one-time wipe.
    uint8_t schema_version;

    // Swap shoulder buttons: L1<->L2 and R1<->R2 (0=off, 1=on). Carved from
    // reserved[] so the 256-byte layout is unchanged; old flashes read 0=off.
    uint8_t shoulder_swap;

    // Built-in profiles hidden from the hotkey cycle: bit i = built-in index i
    // disabled. Carved from reserved[] (zero-init → none disabled on old flashes,
    // no schema bump). Web-config toggle; disabled built-ins are still selectable
    // directly but skipped by SELECT+Up/Down.
    uint8_t builtin_disabled_mask;

    // Non-zero once the user has explicitly picked a BLE output mode
    // (BLE.MODE.SET / web config). Apps that force a default BLE mode
    // (universal → SInput) honor ble_output_mode when this is set.
    // Carved from reserved[] — zero on old flashes = never selected.
    uint8_t ble_mode_saved;

    // Which output wins when USB and BLE could both be live (dual-output apps
    // like universal). WIRELESS_POLICY_*: 0=both active (default),
    // 1=USB dominant (BT yields while a USB data host is connected),
    // 2=BLE dominant (USB input reports suppressed while a BLE host is
    // connected). Carved from reserved[] — zero on old flashes = both.
    uint8_t wireless_policy;

    // Reserved for future global settings (5 bytes)
    uint8_t reserved[5];

    // Custom profiles (4 x 56 = 224 bytes)
    custom_profile_t profiles[CUSTOM_PROFILE_MAX_COUNT];
} flash_t;

// Verify size at compile time
_Static_assert(sizeof(flash_t) == 256, "flash_t must be exactly 256 bytes");
_Static_assert(sizeof(custom_profile_t) == 56, "custom_profile_t must be exactly 56 bytes");

// ============================================================================
// Loaded-record sanitisation
// ============================================================================
//
// A stored record is only as trustworthy as its writers. magic and
// schema_version are stamped by flash_save() itself, so a record written from
// incoherent data passes both load-time checks — and because settings survive
// firmware updates (see the file header), it keeps passing them forever. A
// fixed writer cannot heal a device that already carries one; that repair has
// to happen on the read side, once, here.
//
// Every field below has a valid range that is enforced by its setter and does
// not depend on the build, so clamping an out-of-range value to 0 ("unset /
// compile-time default") is a no-op on any record this firmware would itself
// have produced. Ranges are taken from the enums and the setters, not from the
// struct comments — ble_output_mode's comment claimed 0-1 while
// BLE_MODE_COUNT has been 3 since BLE_MODE_SINPUT landed.
//
// Deliberately NOT checked:
//   active_profile_index  profile.c round-trips the *built-in* profile index
//                         through this byte, which is not bounded by the 0-4
//                         custom range (see #217).
//   usb_output_mode,      valid range depends on the build, and a settings
//   wii_mode              record outlives a reflash to a different app.
//   reserved[]            progressively carved into named fields.
//
// Returns the number of fields corrected; 0 means the record was coherent.
static inline unsigned flash_sanitize_record(flash_t* s)
{
    unsigned fixed = 0;

#define FLASH_CLAMP_(field, max) \
    do { if (s->field > (max)) { s->field = 0; fixed++; } } while (0)

    FLASH_CLAMP_(wiimote_orient_mode, 2);                     // auto/horiz/vert
    FLASH_CLAMP_(custom_profile_count, CUSTOM_PROFILE_MAX_COUNT);
    FLASH_CLAMP_(ble_output_mode, 3);                         // BLE_MODE_COUNT - 1 (STANDARD/XBOX/SINPUT/SWITCH_BT)
    FLASH_CLAMP_(routing_mode, 2);                            // simple/merge/broadcast
    FLASH_CLAMP_(merge_mode, 2);                              // priority/blend/all
    FLASH_CLAMP_(dpad_mode, FLASH_DPAD_MODE_MAX);             // incl. Lstick<->Rstick
    FLASH_CLAMP_(bt_input_enabled, 1);
    FLASH_CLAMP_(shoulder_swap, 1);
    FLASH_CLAMP_(wireless_policy, WIRELESS_POLICY_MAX);      // both/USB/BLE
    FLASH_CLAMP_(joybus_data_pin, 28);                        // 0=default, 1-28=GPIO
    FLASH_CLAMP_(wii_sda_pin, 29);                            // stored as GPIO+1
    FLASH_CLAMP_(wii_scl_pin, 29);

#undef FLASH_CLAMP_

    // Two fields have no in-band invalid value, so they can't be range-checked
    // on their own — but an incoherent record is exactly what turns them on:
    //   router_saved         gates the router settings gc2usb / universal
    //                        / bt2wiiext restore at boot (0 on a clean record).
    //   builtin_disabled_mask hides built-in profiles from the SELECT+D-pad
    //                        cycle; its valid range is build-dependent (one bit
    //                        per built-in profile), so it can't be clamped, but
    //                        a garbage value is what produced the "phantom
    //                        disabled profile" seen on churned records.
    // If anything else in the record failed its range, it wasn't written
    // deliberately — reset both so those features fall back to defaults instead
    // of applying a plausible-looking stray value.
    if (fixed) {
        if (s->router_saved)          { s->router_saved = 0;          fixed++; }
        if (s->builtin_disabled_mask) { s->builtin_disabled_mask = 0; fixed++; }
    }

    return fixed;
}

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

// Diagnostic: number of committed flash writes since boot (see flash.c).
uint32_t flash_get_write_count(void);

// Force immediate save, ignoring BT-active check (use before device reset)
void flash_save_force(const flash_t* settings);

// Factory reset — erase all stored data (settings, bonds, pad config)
void flash_factory_reset(void);

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

// Set active custom profile index (immediate flash write — blocks ~50 ms
// with interrupts disabled). Use for PROFILE.SET from the web config and
// other rare deliberate-config paths where landing before a reboot
// matters more than not stalling a hot path.
void flash_set_active_profile_index(uint8_t index);

// Same effect as flash_set_active_profile_index, but the flash write is
// debounced (~5 s) instead of immediate. Safe to call from hot paths
// like the SELECT+D-pad cycle hotkey where back-to-back immediate
// writes would block the USB host / console-output timing and hang
// the firmware.
void flash_set_active_profile_index_deferred(uint8_t index);

// Ephemeral variant of the above: update RAM only, do not write to flash.
// For live-control flows (joypad-live) where many switches per session
// would otherwise burn flash. Persistent boot default is unchanged.
void flash_select_active_profile_index(uint8_t index);

// ============================================================================
// Runtime Overlay (OVERLAY.SET / OVERLAY.CLEAR)
// ============================================================================
// A RAM-only "live tweak" layer applied on top of whatever profile is active
// (built-in, custom, or PROFILE.APPLY ephemeral). Fields set to 0 mean
// "no change" so the overlay is strictly additive. Unlike PROFILE.APPLY,
// the overlay does NOT replace the active button_map — it just adds stick /
// SOCD / threshold transforms.
//
// Example: "invert stick X while keeping the current profile's button remap"
//   OVERLAY.SET {"flags": 8}        // PROFILE_FLAG_INVERT_LX
//   OVERLAY.CLEAR                   // remove the tweak

typedef struct {
    uint8_t flags;             // OR'd with profile flags (SWAP_STICKS, INVERT_*)
    uint8_t left_stick_sens;   // 0 = no change; 1..200 replaces (100 = 1.0x)
    uint8_t right_stick_sens;  // 0 = no change; 1..200 replaces
    uint8_t socd_mode;         // 0 = no change; 1..3 overrides
    uint8_t l2_threshold;      // 0 = no change; 1..255 overrides
    uint8_t r2_threshold;      // 0 = no change; 1..255 overrides
} runtime_overlay_t;

// Copy o into the overlay slot and activate it. Pass NULL to deactivate.
void flash_set_overlay(const runtime_overlay_t* o);

// Deactivate the overlay (idempotent).
void flash_clear_overlay(void);

// Returns the active overlay, or NULL if none is set.
const runtime_overlay_t* flash_get_overlay(void);

// Get total profile count (1 default + custom_profile_count)
uint8_t flash_get_total_profile_count(void);

// Get active custom profile (returns NULL for index 0/default or if invalid)
const custom_profile_t* flash_get_active_custom_profile(void);

// Cycle to next/previous profile (wraps around)
void flash_cycle_profile_next(void);
void flash_cycle_profile_prev(void);

// ============================================================================
// Ephemeral Runtime Profile Override (PROFILE.APPLY)
// ============================================================================
// RAM-only profile slot that supersedes the flash-stored active profile while
// set. Not persisted, not counted in PROFILE.LIST. Designed for crowd-control
// / live-remap workflows where many unique maps flow in over short windows —
// no flash wear, no 4-slot ceiling. Any explicit profile selection
// (PROFILE.SET / SELECT+D-pad cycling) automatically clears it.

// Copy the given profile into the ephemeral slot and activate it.
// Pass NULL to deactivate (equivalent to flash_clear_ephemeral_profile).
void flash_apply_ephemeral_profile(const custom_profile_t* cp);

// Deactivate the ephemeral slot; flash_get_active_custom_profile() resumes
// returning the flash-stored active profile.
void flash_clear_ephemeral_profile(void);

// True if an ephemeral profile is currently overriding the flash-stored one.
bool flash_has_ephemeral_profile(void);

// Persist the system-wide D-pad mode (0=dpad, 1=left stick, 2=right stick).
// Marks router_saved=1 so apps that restore on boot know the value was
// explicitly chosen (vs the default-zero from a freshly-erased flash).
void flash_set_dpad_mode(uint8_t mode);

// Persist the shoulder-swap toggle (L1<->L2, R1<->R2). Marks router_saved=1.
void flash_set_shoulder_swap(uint8_t on);

// Built-in profiles hidden from the SELECT+Up/Down cycle (bit i = built-in i).
uint8_t flash_get_builtin_disabled_mask(void);
void flash_set_builtin_disabled_mask(uint8_t mask);

#endif // FLASH_H
