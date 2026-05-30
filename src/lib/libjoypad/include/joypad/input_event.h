// input_event.h
// Unified Input Event System for Joypad
// Supports all device types with extensible analog axis arrays

#ifndef INPUT_EVENT_H
#define INPUT_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <joypad/layouts.h>

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
// Transport Type (how the device is connected)
// ============================================================================

typedef enum {
    INPUT_TRANSPORT_NONE = 0,   // Empty slot / unknown
    INPUT_TRANSPORT_USB,        // USB HID/XInput device
    INPUT_TRANSPORT_BT_CLASSIC, // Bluetooth Classic (HID)
    INPUT_TRANSPORT_BT_BLE,     // Bluetooth Low Energy (HOGP)
    INPUT_TRANSPORT_NATIVE,     // Native protocol (3DO, SNES, etc.)
    INPUT_TRANSPORT_I2C,        // I2C peer (STEMMA QT / QWIIC)
    INPUT_TRANSPORT_GPIO,       // Direct GPIO buttons/analog (pad input)
} input_transport_t;

// ============================================================================
// Controller Button Layout
// ============================================================================
// controller_layout_t and the LAYOUT_* enumerators live in <joypad/layouts.h>
// (included above). Glyph-picking and layout-transform helpers also live there.

// ============================================================================
// Analog Axis Indices (internal agnostic format)
// ============================================================================
//
// All input drivers normalize controller data to this standard format.
// This is independent of USB HID or any other protocol.
//
// INTERNAL Y-AXIS CONVENTION (IMPORTANT):
// Joypad uses HID convention internally: Y-axis UP = 0, DOWN = 255
//   - 0   = stick pushed UP
//   - 128 = centered (neutral)
//   - 255 = stick pushed DOWN
//
// This matches USB HID and DirectInput (GP2040-CE compatible).
// No Y-axis inversion needed between internal format and HID output.

typedef enum {
    ANALOG_LX = 0,      // Left stick X (0=left, 128=center, 255=right)
    ANALOG_LY = 1,      // Left stick Y (0=up, 128=center, 255=down) [HID convention]
    ANALOG_RX = 2,      // Right stick X (0=left, 128=center, 255=right)
    ANALOG_RY = 3,      // Right stick Y (0=up, 128=center, 255=down) [HID convention]
    ANALOG_L2 = 4,      // Left trigger (0=released, 255=fully pressed)
    ANALOG_R2 = 5,      // Right trigger (0=released, 255=fully pressed)
    ANALOG_RZ = 6,      // RZ axis / twist (0=released, 255=fully pressed) - spinner/twist input
    ANALOG_COUNT = 7,   // Number of standard analog axes
} analog_axis_index_t;


// ============================================================================
// Unified Input Event Structure
// ============================================================================

typedef struct {
    // Device identification
    uint8_t dev_addr;           // Device address (USB: 1-127, BT: conn_index, Native: port)
    int8_t instance;            // Instance number (for multi-controller devices)
    input_device_type_t type;   // Device type classification
    input_transport_t transport; // Connection type (USB, BT, native)
    controller_layout_t layout; // Physical button layout (for 6-button controllers)

    // Microsecond timestamp when this event was observed. Filled by the
    // consumer from its monotonic clock — libjoypad parsers leave this at 0
    // because they have no platform clock. Used by game engines for input
    // correlation (audio sync, haptics timing, replay determinism).
    //   - firmware: platform_time_ms() * 1000 at the TinyUSB callback
    //   - desktop:  clock_gettime(CLOCK_MONOTONIC) when hid_read returns
    //   - web:      event.timeStamp * 1000 (ms -> us)
    //   - Unreal:   FPlatformTime::Seconds() * 1e6
    uint64_t timestamp_us;

    // Digital inputs
    uint32_t buttons;           // Button bitmap (JP_BUTTON_* defines from globals.h)
    uint32_t keys;              // Keyboard keys (modifier + scancodes, lossy gamepad-mapping encoding)

    // Raw USB HID keyboard state (preserved for output paths that need
    // full keyboard fidelity — e.g. 3DO PS/2 emulation). The legacy
    // `keys` field above is shaped for gamepad mapping and is too lossy
    // for general keyboard work.
    uint8_t kb_modifier;        // HID modifier mask (LCTRL=0x01, LSHIFT=0x02, ..., RGUI=0x80)
    uint8_t kb_keys[6];         // Up to 6 simultaneously pressed HID usage IDs (Page 0x07)

    // Absolute analog inputs (0-255, centered at 128 for sticks, 0 for triggers)
    // All values are normalized regardless of device type
    uint8_t analog[ANALOG_COUNT]; // Standard analog axes (see analog_axis_index_t)
                                  // [0] = LX (Left stick X)
                                  // [1] = LY (Left stick Y)
                                  // [2] = RX (Right stick X)
                                  // [3] = RY (Right stick Y)
                                  // [4] = L2 (Left trigger)
                                  // [5] = R2 (Right trigger)
                                  // [6] = RZ (Twist/spinner)

    // Relative inputs (mouse, spinner, trackball)
    int8_t delta_x;             // Horizontal delta (-127 to +127)
    int8_t delta_y;             // Vertical delta (-127 to +127)
    int8_t delta_wheel;         // Scroll wheel delta

    // Hat switches / D-pad alternatives (encoded as 8-direction)
    uint8_t hat[4];             // Up to 4 hat switches
                                // Values: 0-7 = direction, 0xFF = centered
                                // 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW

    // Chatpad / keyboard accessory (Xbox 360 Chatpad, etc.)
    uint8_t chatpad[3];         // [0]=modifier, [1]=key1, [2]=key2
                                // Modifier bits: see CHATPAD_MOD_* defines
                                // Key values: see CHATPAD_KEY_* defines
    bool has_chatpad;           // Chatpad data is valid

    // Controller capabilities
    uint8_t button_count;       // Number of face buttons (2, 3, 4, 6, etc.)
    bool has_rumble;            // Device supports rumble
    bool has_force_feedback;    // Device supports force feedback

    // Motion data (SIXAXIS/DualShock/DualSense)
    // Accelerometer: raw sensor values, typically ~512 center for DS3, signed for DS4/DS5
    // Gyroscope: angular velocity, DS3 only has Z axis (X/Y remain 0)
    int16_t accel[3];           // Accelerometer X, Y, Z
    int16_t gyro[3];            // Gyroscope X, Y, Z
    uint16_t gyro_range;        // Gyro full-scale range in dps (e.g., 100 for DS3, 2000 for DS4/DS5)
    uint16_t accel_range;       // Accel full-scale range in milli-g (e.g., 2000 for DS3, 4000 for DS4/DS5)
    bool has_motion;            // Motion data is valid

    // Pressure-sensitive button data (DS3)
    // Order: up, right, down, left, l2, r2, l1, r1, triangle, circle, cross, square
    uint8_t pressure[12];       // 0x00 = released, 0xFF = fully pressed
    bool has_pressure;          // Pressure data is valid

    // Touchpad (DS4/DualSense: 2-finger capacitive, 0-1919 x 0-942)
    struct {
        uint16_t x;
        uint16_t y;
        bool active;
    } touch[2];
    bool has_touch;             // Touch data is valid

    // Battery level
    uint8_t battery_level;      // 0-100 percent (0 = unknown/not reported)
    bool battery_charging;      // True if charging / cable connected
} input_event_t;

// ============================================================================
// Helper Functions
// ============================================================================

// Initialize event with safe defaults
static inline void init_input_event(input_event_t* event) {
    memset(event, 0, sizeof(input_event_t));

    // Buttons are active-high (1 = pressed), so 0x00000000 = all released
    event->buttons = 0x00000000;

    // Set analog axes to appropriate defaults:
    // - Sticks (0-3): centered at 128
    // - Triggers (4-5): start at 0 (not pressed)
    // - RZ (6): start at 0 (not pressed)
    event->analog[ANALOG_LX] = 128;
    event->analog[ANALOG_LY] = 128;
    event->analog[ANALOG_RX] = 128;
    event->analog[ANALOG_RY] = 128;
    event->analog[ANALOG_L2] = 0;
    event->analog[ANALOG_R2] = 0;
    event->analog[ANALOG_RZ] = 0;

    // Set hat switches to centered
    for (int i = 0; i < 4; i++) {
        event->hat[i] = 0xFF;
    }

    // Clear chatpad data
    event->chatpad[0] = 0;
    event->chatpad[1] = 0;
    event->chatpad[2] = 0;
    event->has_chatpad = false;

    event->type = INPUT_TYPE_NONE;
    event->layout = LAYOUT_MODERN_4FACE;  // Default to modern 4-face (Xbox/PS/Switch style)
    event->button_count = 4;  // Default to 4 face buttons

    // Clear motion data
    event->has_motion = false;
    event->gyro_range = 2000;   // Default to DS4/DS5 range (±2000 dps)
    event->accel_range = 4000;  // Default to DS4/DS5 range (±4g in milli-g)
    for (int i = 0; i < 3; i++) {
        event->accel[i] = 0;
        event->gyro[i] = 0;
    }

    // Clear pressure data
    event->has_pressure = false;
    for (int i = 0; i < 12; i++) {
        event->pressure[i] = 0;
    }

    // Clear touch data
    event->has_touch = false;
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
    event->analog[ANALOG_LX] = analog_1x;
    event->analog[ANALOG_LY] = analog_1y;
    event->analog[ANALOG_RX] = analog_2x;
    event->analog[ANALOG_RY] = analog_2y;
    event->analog[ANALOG_L2] = analog_l;
    event->analog[ANALOG_R2] = analog_r;
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
// Layout Transforms
// ============================================================================
// LAYOUT_6BTN_MASK, EXTRACT_BTN, transform_to_pce_layout,
// layout_has_6_buttons, and layout_has_3_buttons live in <joypad/layouts.h>
// (included at the top of this file). New code should include layouts.h
// directly when only layout helpers are needed.

#endif // INPUT_EVENT_H
