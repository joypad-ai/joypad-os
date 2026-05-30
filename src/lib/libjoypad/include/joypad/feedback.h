// joypad/feedback.h
// Unified output / feedback API.
//
// Consumers construct a joypad_feedback_t describing what they want the device
// to do (rumble strength, lightbar color, player LED, trigger effects) and
// libjoypad's per-controller drivers translate it into the right vendor
// output report. Unsupported features on a given controller are silent no-ops.

#ifndef JOYPAD_FEEDBACK_H
#define JOYPAD_FEEDBACK_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Trigger identifiers (for adaptive trigger control)
// ============================================================================

typedef enum {
    JOYPAD_TRIGGER_LEFT  = 0,
    JOYPAD_TRIGGER_RIGHT = 1,
} joypad_trigger_id_t;

// ============================================================================
// Adaptive trigger modes (DS5 vocabulary; subset of what DS5 supports)
// ============================================================================
// Each mode interprets `params` differently. Drivers translate to the
// vendor-specific effect byte stream.

typedef enum {
    JOYPAD_TRIGGER_MODE_OFF                  = 0,
    JOYPAD_TRIGGER_MODE_RESISTANCE           = 1,  // Constant resistance from position
    JOYPAD_TRIGGER_MODE_WEAPON               = 2,  // Stiff until pulled past breakpoint
    JOYPAD_TRIGGER_MODE_VIBRATION            = 3,  // Vibrate at frequency past position
    JOYPAD_TRIGGER_MODE_SLOPE_FEEDBACK       = 4,  // Resistance ramps with travel
    JOYPAD_TRIGGER_MODE_MULTIPLE_POSITION    = 5,  // Multi-zone resistance
    JOYPAD_TRIGGER_MODE_VENDOR_RAW           = 0xFF, // Pass `params` directly to vendor protocol
} joypad_trigger_mode_t;

typedef struct {
    joypad_trigger_mode_t mode;
    uint8_t params[10];    // Mode-specific parameters; unused bytes ignored.
                           // For RESISTANCE: [0]=start (0-9), [1]=force (0-8)
                           // For WEAPON: [0]=start, [1]=end, [2]=force
                           // For VIBRATION: [0]=start, [1]=force, [2]=frequency_hz
                           // For VENDOR_RAW: full effect byte stream as-is
} joypad_adaptive_trigger_t;

// ============================================================================
// RGB color
// ============================================================================

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} joypad_rgb_t;

// ============================================================================
// joypad_feedback_t — full desired output state
// ============================================================================
// Consumers fill in what they want to change and call joypad_set_feedback().
// Driver decides whether sending a whole-report update or a delta is better.
//
// The "_dirty" flags say "this field was set by the consumer this frame."
// Fields not marked dirty retain whatever the device is currently doing.

typedef struct {
    // --- Rumble ---
    bool     rumble_dirty;
    uint8_t  rumble_low;      // 0-255, low-frequency motor (heavy / left motor)
    uint8_t  rumble_high;     // 0-255, high-frequency motor (light / right motor)

    // --- Trigger rumble (Xbox One Elite, DS5) ---
    bool     trigger_rumble_dirty;
    uint8_t  trigger_rumble_left;
    uint8_t  trigger_rumble_right;

    // --- Adaptive triggers (DS5) ---
    bool                       adaptive_left_dirty;
    joypad_adaptive_trigger_t  adaptive_left;
    bool                       adaptive_right_dirty;
    joypad_adaptive_trigger_t  adaptive_right;

    // --- Lightbar / RGB (DS4 lightbar, DS5 lightbar) ---
    bool         lightbar_dirty;
    joypad_rgb_t lightbar;

    // --- Player index ---
    // Controller decides how to display: Switch Pro uses 4 LEDs (1=just LED1,
    // 2=LED1+2, ...). DS4/DS5 may color the lightbar. Xbox flashes the guide.
    // Drivers handle the per-controller display logic.
    bool    player_index_dirty;
    uint8_t player_index;       // 0 = none / clear; 1..N = player slot

    // --- Mic LED (DS5) ---
    bool    mic_led_dirty;
    uint8_t mic_led_mode;       // 0=off, 1=on, 2=pulse (DS5 vocabulary)

    // --- Speaker volume (DS4/DS5) ---
    bool    speaker_volume_dirty;
    uint8_t speaker_volume;     // 0-255

    // --- Headset volume (DS4/DS5) ---
    bool    headset_volume_dirty;
    uint8_t headset_volume;     // 0-255

    // Room to grow without breaking ABI — append fields below this line.
} joypad_feedback_t;

// ============================================================================
// Helpers
// ============================================================================

// Initialize feedback to "no changes requested."
static inline void joypad_feedback_init(joypad_feedback_t* f) {
    joypad_feedback_t zero = {0};
    *f = zero;
}

#endif // JOYPAD_FEEDBACK_H
