// input_event.h
// Unified Input Event System for USBRetro
// Supports all device types with extensible analog axis arrays

#ifndef INPUT_EVENT_H
#define INPUT_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ============================================================================
// Device Type Classification
// ============================================================================

typedef enum {
    INPUT_TYPE_NONE = 0,        // Uninitialized / disconnected
    INPUT_TYPE_GAMEPAD,         // Standard gamepad (Xbox, PlayStation, Switch, etc.)
    INPUT_TYPE_FLIGHTSTICK,     // Flight stick with 3+ axes (Thrustmaster, Logitech, etc.)
    INPUT_TYPE_WHEEL,           // Racing wheel with pedals (Logitech G29, Thrustmaster, etc.)
    INPUT_TYPE_MOUSE,           // Mouse with relative motion
    INPUT_TYPE_KEYBOARD,        // Keyboard with keys only
    INPUT_TYPE_LIGHTGUN,        // Light gun with absolute position
    INPUT_TYPE_ARCADE_STICK,    // Arcade stick (8-way joystick + buttons)
} input_device_type_t;

// ============================================================================
// Controller Button Layout Classification
// ============================================================================
// Used to describe the physical button arrangement on 6-button controllers.
// Device drivers set this based on the controller type, and console output
// code can transform button mappings to match the target console layout.
//
// GP2040-CE canonical mapping (our internal standard):
//   Top row:    [B3][B4][R1]
//   Bottom row: [B1][B2][R2]
//
// Physical layouts:
//   SEGA_6BUTTON:  Top [X][Y][Z],   Bottom [A][B][C]
//   PCE_6BUTTON:   Top [IV][V][VI], Bottom [III][II][I]
//   ASTROCITY:     Top [A][B][C],   Bottom [D][E][F]
//   3DO_3BUTTON:   Single row [A][B][C]

typedef enum {
    LAYOUT_UNKNOWN = 0,         // Unknown or default (4-face button modern gamepad)
    LAYOUT_MODERN_4FACE,        // SNES/PlayStation style (no 6-button row)
    LAYOUT_SEGA_6BUTTON,        // Genesis/Saturn: Bottom [A][B][C], Top [X][Y][Z]
    LAYOUT_PCE_6BUTTON,         // PCEngine Avenue Pad: Bottom [III][II][I], Top [IV][V][VI]
    LAYOUT_ASTROCITY,           // Astrocity: Bottom [D][E][F], Top [A][B][C]
    LAYOUT_3DO_3BUTTON,         // 3DO: Single row [A][B][C] (maps to bottom row only)
} controller_layout_t;

// ============================================================================
// Analog Axis Indices (matches USB HID usage order)
// ============================================================================

typedef enum {
    ANALOG_X = 0,       // Left stick X / Flight stick X / Steering wheel
    ANALOG_Y = 1,       // Left stick Y / Flight stick Y
    ANALOG_Z = 2,       // Right stick X / Rudder / Twist
    ANALOG_RX = 3,      // Right stick X (alt) / Throttle slider
    ANALOG_RY = 4,      // Right stick Y (alt)
    ANALOG_RZ = 5,      // Triggers / Brake pedal
    ANALOG_SLIDER = 6,  // Throttle / Gas pedal
    ANALOG_DIAL = 7,    // Extra slider / Clutch pedal
} analog_axis_index_t;

// ============================================================================
// Unified Input Event Structure
// ============================================================================

typedef struct {
    // Device identification
    uint8_t dev_addr;           // USB device address
    int8_t instance;            // Instance number (for multi-controller devices)
    input_device_type_t type;   // Device type classification
    controller_layout_t layout; // Physical button layout (for 6-button controllers)

    // Digital inputs
    uint32_t buttons;           // Button bitmap (USBR_BUTTON_* defines from globals.h)
    uint32_t keys;              // Keyboard keys (modifier + scancodes)

    // Absolute analog inputs (0-255, centered at 128)
    // All values are normalized regardless of device type
    uint8_t analog[8];          // 8 analog axes (see analog_axis_index_t)
                                // [0] = X-axis (Left stick X / Flight stick X / Steering)
                                // [1] = Y-axis (Left stick Y / Flight stick Y)
                                // [2] = Z-axis (Right stick X / Rudder / Twist)
                                // [3] = RX-axis (Right stick X alt / Throttle)
                                // [4] = RY-axis (Right stick Y alt)
                                // [5] = RZ-axis (Triggers / Brake)
                                // [6] = Slider (Throttle / Gas pedal)
                                // [7] = Dial (Extra slider / Clutch)

    // Relative inputs (mouse, spinner, trackball)
    int8_t delta_x;             // Horizontal delta (-127 to +127)
    int8_t delta_y;             // Vertical delta (-127 to +127)
    int8_t delta_wheel;         // Scroll wheel delta

    // Hat switches / D-pad alternatives (encoded as 8-direction)
    uint8_t hat[4];             // Up to 4 hat switches
                                // Values: 0-7 = direction, 0xFF = centered
                                // 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW

    // Controller capabilities
    uint8_t button_count;       // Number of face buttons (2, 3, 4, 6, etc.)
    bool has_rumble;            // Device supports rumble
    bool has_force_feedback;    // Device supports force feedback
} input_event_t;

// ============================================================================
// Helper Functions
// ============================================================================

// Initialize event with safe defaults
static inline void init_input_event(input_event_t* event) {
    memset(event, 0, sizeof(input_event_t));

    // Buttons are active-high (1 = pressed), so 0x00000000 = all released
    event->buttons = 0x00000000;

    // Set analog axes to centered position (128 = neutral)
    for (int i = 0; i < 8; i++) {
        event->analog[i] = 128;
    }

    // Set hat switches to centered
    for (int i = 0; i < 4; i++) {
        event->hat[i] = 0xFF;
    }

    event->type = INPUT_TYPE_NONE;
    event->layout = LAYOUT_MODERN_4FACE;  // Default to modern 4-face (Xbox/PS/Switch style)
    event->button_count = 4;  // Default to 4 face buttons
}

// Convert old post_globals() parameters to input_event_t (for migration)
static inline void gamepad_to_input_event(
    input_event_t* event,
    uint8_t dev_addr,
    int8_t instance,
    uint32_t buttons,
    uint8_t analog_1x, uint8_t analog_1y,
    uint8_t analog_2x, uint8_t analog_2y,
    uint8_t analog_l, uint8_t analog_r,
    uint32_t keys,
    uint8_t quad_x)  // Ignored - consoles accumulate delta_x into spinner
{
    init_input_event(event);

    event->dev_addr = dev_addr;
    event->instance = instance;
    event->type = INPUT_TYPE_GAMEPAD;
    event->buttons = buttons;
    event->keys = keys;

    // Map to standard gamepad layout
    event->analog[ANALOG_X] = analog_1x;      // Left stick X
    event->analog[ANALOG_Y] = analog_1y;      // Left stick Y
    event->analog[ANALOG_Z] = analog_2x;      // Right stick X
    event->analog[ANALOG_RX] = analog_2y;     // Right stick Y
    event->analog[ANALOG_RZ] = analog_l;      // Left trigger
    event->analog[ANALOG_SLIDER] = analog_r;  // Right trigger
}

// Convert old post_mouse_globals() parameters to input_event_t (for migration)
static inline void mouse_to_input_event(
    input_event_t* event,
    uint8_t dev_addr,
    int8_t instance,
    uint16_t buttons,
    uint8_t delta_x,
    uint8_t delta_y,
    uint8_t spinner)  // Ignored - consoles accumulate delta_x into spinner
{
    init_input_event(event);

    event->dev_addr = dev_addr;
    event->instance = instance;
    event->type = INPUT_TYPE_MOUSE;
    event->buttons = buttons;
    event->delta_x = (int8_t)delta_x;
    event->delta_y = (int8_t)delta_y;
}

// ============================================================================
// Layout Transform Functions
// ============================================================================
// These functions transform button mappings from one physical layout to another.
// Device drivers output canonical GP2040-CE mapping, and console output code
// uses these transforms to match the target console's expected layout.
//
// GP2040-CE Canonical (internal standard):
//   Top row:    [B3][B4][R1]   (USBR: B3, B4, R1)
//   Bottom row: [B1][B2][R2]   (USBR: B1, B2, R2)
//
// For 6-button layouts, the mapping is:
//   Position:    Left-Bot  Mid-Bot  Right-Bot  Left-Top  Mid-Top  Right-Top
//   GP2040-CE:   B1        B2       R2         B3        B4       R1
//   PCEngine:    III       II       I          IV        V        VI
//   Genesis:     A         B        C          X         Y        Z
//   Astrocity:   D         E        F          A         B        C

// Button masks for 6-button face buttons (excludes D-pad, Start, Select, etc.)
#define LAYOUT_6BTN_MASK (0x0B230)  // B1|B2|B3|B4|R1|R2

// Helper to extract a button, returning its state (active-high: 1 = pressed)
#define EXTRACT_BTN(buttons, mask) (((buttons) & (mask)) ? 1 : 0)

// Transform buttons from source layout to PCEngine 6-button layout
// PCEngine expects: Bottom [III][II][I], Top [IV][V][VI]
// where III=leftmost, I=rightmost (numbers decrease left to right)
static inline uint32_t transform_to_pce_layout(uint32_t buttons, controller_layout_t source) {
    // If source is already PCE layout or unknown, no transform needed
    if (source == LAYOUT_PCE_6BUTTON || source == LAYOUT_UNKNOWN || source == LAYOUT_MODERN_4FACE) {
        return buttons;
    }

    // Extract 6-button states (active-high: 1 = pressed)
    // GP2040-CE canonical positions:
    //   Bottom: B1 (left), B2 (mid), R2 (right)
    //   Top:    B3 (left), B4 (mid), R1 (right)

    // For SEGA_6BUTTON and ASTROCITY, the physical positions match GP2040-CE,
    // so they map 1:1 to PCEngine positions:
    //   PCE III = B1 (left-bottom)
    //   PCE II  = B2 (mid-bottom)
    //   PCE I   = R2 (right-bottom)
    //   PCE IV  = B3 (left-top)
    //   PCE V   = B4 (mid-top)
    //   PCE VI  = R1 (right-top)

    // Since USBR uses GP2040-CE naming and PCE uses the same physical positions,
    // the button bits are already correct - no transformation needed for
    // SEGA_6BUTTON or ASTROCITY when targeting PCEngine.

    // For 3DO (single row), only bottom row is used, top row ignored
    if (source == LAYOUT_3DO_3BUTTON) {
        // 3DO A/B/C maps to PCE III/II/I (bottom row)
        // Top row buttons should be cleared/ignored
        // No actual bit transformation needed - 3DO uses bottom row only
        return buttons;
    }

    return buttons;
}

// Check if a controller has a 6-button layout (two rows of 3)
static inline bool layout_has_6_buttons(controller_layout_t layout) {
    return (layout == LAYOUT_SEGA_6BUTTON ||
            layout == LAYOUT_PCE_6BUTTON ||
            layout == LAYOUT_ASTROCITY);
}

// Check if a controller has a 3-button single row layout
static inline bool layout_has_3_buttons(controller_layout_t layout) {
    return (layout == LAYOUT_3DO_3BUTTON);
}

#endif // INPUT_EVENT_H
