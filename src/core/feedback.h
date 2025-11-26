// feedback.h
// USBRetro canonical feedback definitions
//
// Controller-independent format for rumble and LED feedback that device
// drivers map to their specific hardware capabilities.
//
// Similar to how USBR_BUTTON_* provides controller-independent button input,
// this provides controller-independent feedback output.

#ifndef CORE_FEEDBACK_H
#define CORE_FEEDBACK_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// RUMBLE MOTORS
// ============================================================================
// Most controllers have 2 motors: heavy (left) and light (right)
// Some have additional motors (triggers, HD rumble, etc.)

typedef struct {
    uint8_t left;           // Heavy/low-frequency motor (0-255)
    uint8_t right;          // Light/high-frequency motor (0-255)
    uint8_t left_trigger;   // Left trigger motor (Xbox, DualSense)
    uint8_t right_trigger;  // Right trigger motor (Xbox, DualSense)
} feedback_rumble_t;

// ============================================================================
// LED PATTERNS
// ============================================================================
// Player indicator LEDs (1-4 style) and RGB lightbar

// Player LED patterns (bitmask for 4 LEDs)
#define FEEDBACK_LED_NONE       0x00
#define FEEDBACK_LED_PLAYER1    0x01    // LED 1 on
#define FEEDBACK_LED_PLAYER2    0x02    // LED 2 on
#define FEEDBACK_LED_PLAYER3    0x04    // LED 3 on
#define FEEDBACK_LED_PLAYER4    0x08    // LED 4 on
#define FEEDBACK_LED_ALL        0x0F    // All LEDs on

// Common patterns
#define FEEDBACK_LED_BLINK_SLOW     0x10    // Blink slowly
#define FEEDBACK_LED_BLINK_FAST     0x20    // Blink rapidly
#define FEEDBACK_LED_PULSE          0x40    // Fade in/out

typedef struct {
    uint8_t pattern;        // LED pattern (FEEDBACK_LED_* flags)
    uint8_t r, g, b;        // RGB color (for lightbar controllers)
    uint8_t brightness;     // Overall brightness (0-255)
} feedback_led_t;

// ============================================================================
// ADAPTIVE TRIGGERS (DualSense, Xbox Series)
// ============================================================================

typedef enum {
    TRIGGER_MODE_OFF = 0,       // No resistance
    TRIGGER_MODE_RIGID,         // Constant resistance
    TRIGGER_MODE_PULSE,         // Pulsing resistance
    TRIGGER_MODE_RIGID_A,       // Rigid in region A
    TRIGGER_MODE_RIGID_B,       // Rigid in region B
    TRIGGER_MODE_RIGID_AB,      // Rigid in both regions
    TRIGGER_MODE_PULSE_A,       // Pulse in region A
    TRIGGER_MODE_PULSE_B,       // Pulse in region B
    TRIGGER_MODE_PULSE_AB,      // Pulse in both regions
    TRIGGER_MODE_CALIBRATION,   // Calibration mode
} trigger_effect_mode_t;

typedef struct {
    trigger_effect_mode_t mode;
    uint8_t start_position;     // Where effect starts (0-255)
    uint8_t end_position;       // Where effect ends (0-255)
    uint8_t strength;           // Effect strength (0-255)
} feedback_trigger_t;

// ============================================================================
// COMBINED FEEDBACK STATE (per player)
// ============================================================================

typedef struct {
    feedback_rumble_t rumble;
    feedback_led_t led;
    feedback_trigger_t left_trigger;
    feedback_trigger_t right_trigger;

    // Flags indicating what's changed (for efficient updates)
    bool rumble_dirty;
    bool led_dirty;
    bool triggers_dirty;
} feedback_state_t;

// ============================================================================
// FEEDBACK API
// ============================================================================

// Initialize feedback system
void feedback_init(void);

// Get feedback state for a player (0-based index)
feedback_state_t* feedback_get_state(uint8_t player_index);

// Set rumble for a player
void feedback_set_rumble(uint8_t player_index, uint8_t left, uint8_t right);
void feedback_set_rumble_ext(uint8_t player_index, const feedback_rumble_t* rumble);

// Set LED for a player
void feedback_set_led_player(uint8_t player_index, uint8_t player_num);  // Simple player number
void feedback_set_led_rgb(uint8_t player_index, uint8_t r, uint8_t g, uint8_t b);
void feedback_set_led(uint8_t player_index, const feedback_led_t* led);

// Set adaptive triggers for a player
void feedback_set_trigger(uint8_t player_index, bool left, const feedback_trigger_t* trigger);

// Clear all feedback for a player
void feedback_clear(uint8_t player_index);

// Clear dirty flags after device has applied feedback
void feedback_clear_dirty(uint8_t player_index);

// ============================================================================
// DEVICE CAPABILITY FLAGS
// ============================================================================
// Device drivers report their capabilities so feedback can be adapted

#define FEEDBACK_CAP_RUMBLE_BASIC   0x0001  // Basic 2-motor rumble
#define FEEDBACK_CAP_RUMBLE_TRIGGER 0x0002  // Trigger motors
#define FEEDBACK_CAP_RUMBLE_HD      0x0004  // HD rumble (Switch)
#define FEEDBACK_CAP_LED_PLAYER     0x0010  // Player indicator LEDs
#define FEEDBACK_CAP_LED_RGB        0x0020  // RGB lightbar
#define FEEDBACK_CAP_TRIGGER_ADAPT  0x0040  // Adaptive triggers

#endif // CORE_FEEDBACK_H
