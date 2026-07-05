#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "core/buttons.h"

// Neo Geo has 6 remappable buttons:
// B1/A, B2/B, B3/C, B4/D, B5/Select, B6/K3
// Start, Coin, and D-Pad are not remappable.
#define NEOGEO_REMAP_BUTTON_COUNT 6

// The 3-button combo that triggers remap mode on plug-in.
// Square + Triangle + R1 (matches Undamned adapter convention).
#define NEOGEO_REMAP_TRIGGER_MASK (JP_BUTTON_B3 | JP_BUTTON_B4 | JP_BUTTON_R1)

// How long the combo must be held before remap mode activates (ms)
#define NEOGEO_REMAP_HOLD_MS 1000

// How long after controller connect the trigger combo will be checked (ms).
// After this window closes the combo is ignored until the next plug-in.
#define NEOGEO_REMAP_BOOT_WINDOW_MS 3000

// Timeout in ms to abort remap if user stops pressing during collection
#define NEOGEO_REMAP_TIMEOUT_MS 5000

// Neo Geo output button indices — these are the slots being assigned
typedef enum {
    NEOGEO_BTN_A      = 0,  // B1 / P1
    NEOGEO_BTN_B      = 1,  // B2 / P2
    NEOGEO_BTN_C      = 2,  // B3 / P3
    NEOGEO_BTN_D      = 3,  // B4 / K1
    NEOGEO_BTN_SELECT = 4,  // B5 / K2
    NEOGEO_BTN_K3     = 5,  // B6 / K3
} neogeo_btn_index_t;

// Remap table: for each Neo Geo output button, which JP_BUTTON_* triggers it
typedef struct {
    uint32_t buttons[NEOGEO_REMAP_BUTTON_COUNT];
} neogeo_remap_t;

// Default mapping matches the usb2neogeo Default profile:
// Square→A, Triangle→B, R1→C, Cross→D, Circle→Select, R2→K3
static const neogeo_remap_t neogeo_remap_default = {
    .buttons = {
        [NEOGEO_BTN_A]      = JP_BUTTON_B3,  // Square   → A
        [NEOGEO_BTN_B]      = JP_BUTTON_B4,  // Triangle → B
        [NEOGEO_BTN_C]      = JP_BUTTON_R1,  // R1       → C
        [NEOGEO_BTN_D]      = JP_BUTTON_B1,  // Cross    → D
        [NEOGEO_BTN_SELECT] = JP_BUTTON_B2,  // Circle   → Select
        [NEOGEO_BTN_K3]     = JP_BUTTON_R2,  // R2       → K3
    }
};

typedef enum {
    REMAP_STATE_IDLE,
    REMAP_STATE_HOLDING,     // combo held, counting down to activate
    REMAP_STATE_WAITING,     // activated, waiting for buttons to be released
    REMAP_STATE_COLLECTING,  // collecting button presses 1-6
    REMAP_STATE_DONE,
} neogeo_remap_state_t;

typedef struct {
    neogeo_remap_state_t state;
    neogeo_remap_t       pending;          // being built during collection
    uint8_t              count;            // how many buttons collected so far
    uint32_t             last_buttons;     // debounce: buttons held last frame
    uint32_t             last_activity_ms;
    uint32_t             mapped_mask;      // bitmask of already-assigned input buttons
    uint32_t             error_flash_ms;   // timestamp of last duplicate press (0 = none)
    bool                 boot_checked;     // true once plug-in window has closed
} neogeo_remap_ctx_t;

// Public API
void    neogeo_remap_ctx_init(neogeo_remap_ctx_t *ctx);
bool    neogeo_remap_update(neogeo_remap_ctx_t *ctx, uint32_t current_buttons,
                            neogeo_remap_t *remap_out);
uint8_t neogeo_remap_apply(const neogeo_remap_t *remap, uint32_t buttons);
