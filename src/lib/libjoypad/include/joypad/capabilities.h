// joypad/capabilities.h
// Per-device capability discovery.
//
// joypad_caps_t describes what a connected device supports so consumers
// (game UI, plugin glue) can branch on capability rather than vendor/model.
// Returned by joypad_get_caps(device, &caps) once a device is identified.
//
// Design principle: name capabilities by what the user can do with them,
// not by which controller has them. "has_adaptive_triggers" not "is_ds5".
// Unsupported features on a given controller are simply false here; output
// calls for those features become silent no-ops at runtime.

#ifndef JOYPAD_CAPABILITIES_H
#define JOYPAD_CAPABILITIES_H

#include <stdint.h>
#include <stdbool.h>
#include <joypad/layouts.h>

// ============================================================================
// Capability mask bits (input axes the device produces)
// ============================================================================
// Mirror of analog_axis_index_t but as a bitfield so caps can express "this
// controller exposes LX/LY/L2 only" (e.g. flight stick) or "all seven axes."

#define JOYPAD_AXIS_LX  (1u << 0)
#define JOYPAD_AXIS_LY  (1u << 1)
#define JOYPAD_AXIS_RX  (1u << 2)
#define JOYPAD_AXIS_RY  (1u << 3)
#define JOYPAD_AXIS_L2  (1u << 4)
#define JOYPAD_AXIS_R2  (1u << 5)
#define JOYPAD_AXIS_RZ  (1u << 6)

// ============================================================================
// joypad_caps_t
// ============================================================================

typedef struct {
    // --- Identity ---
    uint16_t vendor_id;
    uint16_t product_id;
    const char* vendor_name;       // "Sony", "Nintendo", "Microsoft", "8BitDo", ...
    const char* product_name;      // "DualSense", "Switch Pro Controller", "Xbox Wireless", ...
    controller_layout_t layout;    // Physical button arrangement / glyph hint

    // --- Input capabilities ---
    uint8_t  axes_mask;            // Bitmask of JOYPAD_AXIS_* — which sticks/triggers exist
    uint8_t  button_count;         // Number of face buttons (e.g. 4 modern, 6 Sega, 3 3DO)
    bool     has_dpad;
    bool     has_keyboard;         // Reports HID keyboard data (kb_modifier / kb_keys)
    bool     has_mouse;            // Reports relative pointer deltas

    // --- Motion ---
    bool     has_motion;           // Has gyroscope and/or accelerometer
    bool     has_gyro;
    bool     has_accel;
    uint16_t gyro_range_dps;       // Full-scale gyro range in degrees per second
    uint16_t accel_range_milli_g;  // Full-scale accel range in milli-g (e.g. 4000 = ±4g)

    // --- Touchpad ---
    bool     has_touchpad;
    uint8_t  num_touchpoints;      // DS4/DS5: 2
    uint16_t touch_max_x;          // DS5: 1919
    uint16_t touch_max_y;          // DS5: 1079
    bool     touchpad_has_click;   // Can be pressed as a button

    // --- Pressure-sensitive buttons (DS3) ---
    bool     has_pressure;

    // --- Output / feedback capabilities ---
    bool     has_rumble;                // At least one rumble motor
    bool     has_dual_rumble;           // Independent low/high-freq motors (DS4/DS5/Xbox)
    bool     has_trigger_rumble;        // Actuators in L2/R2 (Xbox One Elite, DS5)
    bool     has_adaptive_triggers;     // DS5 trigger resistance/effect control
    bool     has_lightbar;              // Full RGB lightbar (DS4/DS5)
    bool     has_player_leds;           // Discrete player-index LEDs (Switch Pro: 4)
    uint8_t  num_player_leds;           // How many discrete LEDs (Switch Pro: 4, others: 0)
    bool     has_mic_led;               // DS5 mic mute LED
    bool     has_speaker;               // DS4/DS5 speaker
    bool     has_headset_jack;          // DS4/DS5

    // --- Power / status ---
    bool     reports_battery_level;
    bool     reports_charging_state;

    // --- Connection info ---
    bool     is_wireless;               // BT or 2.4 GHz dongle (false = USB wired)

    // Room to grow without breaking ABI — append fields below this line.
} joypad_caps_t;

// ============================================================================
// Helpers
// ============================================================================

// Initialize a caps struct to all-zero / no-capabilities baseline.
// Drivers fill in what they support; everything else stays false.
static inline void joypad_caps_init(joypad_caps_t* c) {
    // Memset-via-field-assignment to avoid pulling in <string.h> unnecessarily;
    // compilers optimize this to memset under -O1+.
    joypad_caps_t zero = {0};
    *c = zero;
    c->layout = LAYOUT_UNKNOWN;
}

#endif // JOYPAD_CAPABILITIES_H
