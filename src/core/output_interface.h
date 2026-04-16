// output_interface.h
// Output abstraction for Joypad - supports native console and USB device outputs

#ifndef OUTPUT_INTERFACE_H
#define OUTPUT_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_event.h"
#include "core/router/router.h"

// Feedback state from output (agnostic representation)
typedef struct {
    uint8_t rumble_left;        // Left (heavy) motor 0-255
    uint8_t rumble_right;       // Right (light) motor 0-255
    uint8_t led_player;         // Player LED index (1-7), 0 = not set
    uint8_t led_r, led_g, led_b;// RGB LED color
    bool dirty;                 // True if feedback changed since last read
} output_feedback_t;

// Output interface - abstracts different output types (native consoles, USB device, BLE, etc.)
typedef struct {
    const char* name;                                      // Output name (e.g., "GameCube", "USB Device (XInput)")
    output_target_t target;                                // Router target type for routing table

    void (*init)(void);                                    // Initialize output hardware/protocol
    void (*task)(void);                                    // Core 0 periodic task (NULL if not needed)
    void (*core1_task)(void);                              // Core 1 loop for timing-critical output (NULL if not needed)

    // Feedback to USB input devices (rumble, LEDs) - agnostic format
    bool (*get_feedback)(output_feedback_t* fb);           // Get feedback state, returns true if available

    // Legacy single-value rumble (deprecated, use get_feedback instead)
    uint8_t (*get_rumble)(void);                           // Get rumble state (0-255), NULL = no rumble
    uint8_t (*get_player_led)(void);                       // Get player LED state, NULL = no LED override

    // Profile system (output-specific profiles)
    // Each output defines its own profile structure with console-specific button mappings
    uint8_t (*get_profile_count)(void);                    // Get number of available profiles, NULL = no profiles
    uint8_t (*get_active_profile)(void);                   // Get active profile index (0-based)
    void (*set_active_profile)(uint8_t index);             // Set active profile (triggers flash save)
    const char* (*get_profile_name)(uint8_t index);        // Get profile name for display, NULL = use index

    // Input device feedback (from current profile)
    uint8_t (*get_trigger_threshold)(void);                // Get L2/R2 threshold for adaptive triggers, NULL = 0

    // Native output configuration (web config: Output > <type> page)
    // Each device implements its own type/modes/pins schema. Returns
    // strlen of JSON written to buf, or 0 if not supported (NULL also OK).
    // Example response (JSON object body, no surrounding {} expected from caller):
    //   "type":"joybus","modes":["gamecube"],"current_mode":"gamecube",
    //   "pins":{"data":{"label":"Data","value":7,"min":0,"max":28}}
    uint16_t (*get_native_config)(char* buf, uint16_t buf_size);

    // Apply new native config (JSON body). Writes status JSON to response_buf.
    // Returns true on success. Implementer is responsible for persisting to
    // flash and (typically) requesting a reboot.
    bool (*set_native_config)(const char* json, char* response_buf, uint16_t response_size);
} OutputInterface;

// Maximum outputs per app (native console + USB device + BLE + UART)
#define MAX_OUTPUT_INTERFACES 4

// Active output interface (set at compile-time, selected in common/output.c)
extern const OutputInterface* active_output;

// Native console output for web config queries. Set by apps that have a
// console output (e.g. gamecube_output_interface for usb2gc) so the web
// config can read/write its native config (pins, modes) even in CDC config
// mode where the native output isn't actually active. NULL if the app has
// no native console output.
extern const OutputInterface* native_output;

#endif // OUTPUT_INTERFACE_H
