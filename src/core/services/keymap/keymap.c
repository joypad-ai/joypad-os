// keymap.c - Configurable HID keyboard-key -> JP_BUTTON_* mapping (see keymap.h)

#include "keymap.h"
#include "core/buttons.h"

// HID Keyboard/Keypad usage IDs (Usage Page 0x07) — a fixed USB-HID standard, so
// we hardcode the few we need rather than depend on tinyusb headers in core.
#define HID_KEY_A            0x04
#define HID_KEY_D            0x07
#define HID_KEY_E            0x08
#define HID_KEY_I            0x0C
#define HID_KEY_J            0x0D
#define HID_KEY_K            0x0E
#define HID_KEY_L            0x0F
#define HID_KEY_O            0x12
#define HID_KEY_Q            0x14
#define HID_KEY_S            0x16
#define HID_KEY_U            0x18
#define HID_KEY_W            0x1A
#define HID_KEY_ENTER        0x28
#define HID_KEY_ESCAPE       0x29
#define HID_KEY_TAB          0x2B
#define HID_KEY_SPACE        0x2C
#define HID_KEY_ARROW_RIGHT  0x4F
#define HID_KEY_ARROW_LEFT   0x50
#define HID_KEY_ARROW_DOWN   0x51
#define HID_KEY_ARROW_UP     0x52

// Built-in default map. Directional keys (arrows AND WASD) -> D-pad, so a
// MouthPad `.mkprofile` that emits either direction style drives the gamepad
// D-pad. Face/shoulder/start/select use a clear, documented layout. This is the
// out-of-box default; keymap_set() can replace it (e.g. user-configured).
static const keymap_entry_t keymap_default[] = {
    // D-pad — arrow keys
    { HID_KEY_ARROW_UP,     JP_BUTTON_DU },
    { HID_KEY_ARROW_DOWN,   JP_BUTTON_DD },
    { HID_KEY_ARROW_LEFT,   JP_BUTTON_DL },
    { HID_KEY_ARROW_RIGHT,  JP_BUTTON_DR },
    // D-pad — WASD (so a WASD profile also drives the D-pad)
    { HID_KEY_W,            JP_BUTTON_DU },
    { HID_KEY_S,            JP_BUTTON_DD },
    { HID_KEY_A,            JP_BUTTON_DL },
    { HID_KEY_D,            JP_BUTTON_DR },
    // Face buttons
    { HID_KEY_SPACE,        JP_BUTTON_B1 },   // A / Cross
    { HID_KEY_J,            JP_BUTTON_B1 },
    { HID_KEY_K,            JP_BUTTON_B2 },   // B / Circle
    { HID_KEY_L,            JP_BUTTON_B3 },   // X / Square
    { HID_KEY_I,            JP_BUTTON_B4 },   // Y / Triangle
    // Shoulders / triggers
    { HID_KEY_Q,            JP_BUTTON_L1 },
    { HID_KEY_E,            JP_BUTTON_R1 },
    { HID_KEY_U,            JP_BUTTON_L2 },
    { HID_KEY_O,            JP_BUTTON_R2 },
    // Start / Select / Guide
    { HID_KEY_ENTER,        JP_BUTTON_S2 },   // Start
    { HID_KEY_ESCAPE,       JP_BUTTON_S1 },   // Select / Back
    { HID_KEY_TAB,          JP_BUTTON_A1 },   // Guide / Home
};
#define KEYMAP_DEFAULT_COUNT (sizeof(keymap_default) / sizeof(keymap_default[0]))

static const keymap_entry_t* active_map   = keymap_default;
static uint8_t               active_count  = KEYMAP_DEFAULT_COUNT;

void keymap_set(const keymap_entry_t* map, uint8_t count)
{
    if (map && count) {
        active_map   = map;
        active_count = count;
    } else {
        active_map   = keymap_default;
        active_count = KEYMAP_DEFAULT_COUNT;
    }
}

uint32_t keymap_keys_to_buttons(const uint8_t* keys, uint8_t nkeys, uint8_t modifier)
{
    (void)modifier;   // reserved for future modifier->button entries
    uint32_t buttons = 0;
    if (!keys) return 0;
    for (uint8_t i = 0; i < nkeys; i++) {
        uint8_t kc = keys[i];
        if (kc == 0) continue;
        for (uint8_t e = 0; e < active_count; e++) {
            if (active_map[e].keycode == kc) {
                buttons |= active_map[e].button;
                break;
            }
        }
    }
    return buttons;
}
