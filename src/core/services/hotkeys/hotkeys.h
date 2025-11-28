#ifndef HOTKEYS_H
#define HOTKEYS_H

#include <stdint.h>
#include <stdbool.h>

// Maximum number of registered hotkeys
#define MAX_HOTKEYS 8

// Hotkey trigger types
typedef enum {
    HOTKEY_TRIGGER_ON_HOLD,     // Trigger after duration_ms while held
    HOTKEY_TRIGGER_ON_RELEASE,  // Trigger on release if held >= duration_ms
    HOTKEY_TRIGGER_ON_TAP,      // Trigger on release if held < duration_ms (quick tap)
} HotkeyTriggerType;

// Callback function type for hotkey triggers
// player: player index (0-based), or 0xFF for global triggers
// held_ms: how long the combo was held (useful for tap vs hold detection)
typedef void (*hotkey_callback_t)(uint8_t player, uint32_t held_ms);

// Hotkey definition structure
typedef struct {
    uint32_t buttons;              // Button mask to match (all must be pressed)
    uint16_t duration_ms;          // Duration threshold (interpretation depends on trigger type)
    HotkeyTriggerType trigger;     // When to trigger the callback
    hotkey_callback_t callback;
    bool global;                   // If true, checks combined input from all players
} HotkeyDef;

// Register a hotkey combo
// Returns hotkey ID on success, -1 if registry is full
int hotkeys_register(const HotkeyDef* hotkey);

// Unregister a hotkey by ID
void hotkeys_unregister(int hotkey_id);

// Clear all registered hotkeys
void hotkeys_clear(void);

// Check hotkeys against current input state
// Called from update_output() or similar per-frame function
// buttons: current button state for the player (active-high: 1 = pressed)
// player: player index (0-based)
void hotkeys_check(uint32_t buttons, uint8_t player);

// Call once per frame after all players have been checked (for global hotkeys)
void hotkeys_check_global(void);

// Reset hold timers (call on player disconnect)
void hotkeys_reset_player(uint8_t player);

#endif // HOTKEYS_H
