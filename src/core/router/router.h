// router.h
// Joypad Core Router - Zero-latency N:M input/output routing
//
// Replaces console-specific post_input_event() with unified routing system.
// Supports 4 modes: Simple (1:1), Merge (N:1), Broadcast (1:N), Configurable (N:M)

#ifndef ROUTER_H
#define ROUTER_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_event.h"

// ============================================================================
// ROUTING MODES
// ============================================================================

typedef enum {
    ROUTING_MODE_SIMPLE,        // 1:1 fixed (GCUSB, USB2PCE, current products)
    ROUTING_MODE_MERGE,         // N:1 merge inputs (Super3D0USB, current GCUSB all→port1)
    ROUTING_MODE_BROADCAST,     // 1:N broadcast (MultiOut: USB → GC+USBD+BLE)
    ROUTING_MODE_CONFIGURABLE,  // N:M user-defined (Universal app)
} routing_mode_t;

typedef enum {
    MERGE_PRIORITY,             // High priority wins (Super3D0USB: USB > SNES)
    MERGE_BLEND,                // Blend button states (OR buttons together)
    MERGE_ALL,                  // Latest active wins (current GCUSB behavior)
} merge_mode_t;

// ============================================================================
// INPUT/OUTPUT SOURCES
// ============================================================================

typedef enum {
    INPUT_SOURCE_USB_HOST,
    INPUT_SOURCE_BLE_CENTRAL,
    INPUT_SOURCE_WIFI,              // WiFi JOCP input
    INPUT_SOURCE_NATIVE_SNES,
    INPUT_SOURCE_NATIVE_N64,
    INPUT_SOURCE_NATIVE_GC,
    INPUT_SOURCE_NATIVE_3DO,
    INPUT_SOURCE_GPIO,
    INPUT_SOURCE_SENSORS,
} input_source_t;

typedef enum {
    OUTPUT_TARGET_NONE = -1,        // No output configured
    OUTPUT_TARGET_GAMECUBE = 0,
    OUTPUT_TARGET_PCENGINE,
    OUTPUT_TARGET_3DO,
    OUTPUT_TARGET_NUON,
    OUTPUT_TARGET_XBOXONE,
    OUTPUT_TARGET_LOOPY,
    OUTPUT_TARGET_DREAMCAST,
    OUTPUT_TARGET_GPIO,
    OUTPUT_TARGET_USB_DEVICE,
    OUTPUT_TARGET_BLE_PERIPHERAL,
    OUTPUT_TARGET_UART,             // UART bridge to ESP32/other MCU
    OUTPUT_TARGET_COUNT             // Must be last — used to size arrays
} output_target_t;

// ============================================================================
// INPUT TRANSFORMATIONS
// ============================================================================

// Transformation flags (enable/disable per console)
typedef enum {
    TRANSFORM_NONE              = 0x00,
    TRANSFORM_MOUSE_TO_ANALOG   = 0x01,  // Convert mouse deltas → analog stick (for Xbox, etc.)
    TRANSFORM_MERGE_INSTANCES   = 0x02,  // Merge multi-instance devices (Joy-Con Grip)
    TRANSFORM_SPINNER           = 0x04,  // Accumulate X-axis for spinners (Nuon, etc.)
} transformation_flags_t;

// Mouse-to-analog accumulator state (per player)
typedef struct {
    int16_t accum_x;        // Accumulated X delta
    int16_t accum_y;        // Accumulated Y delta
    uint8_t drain_rate;     // How fast to drain per frame (0 = NO drain/hold, >0 = drain rate)
    uint8_t target_x;       // Target analog axis for X (ANALOG_LX, ANALOG_RX, etc.)
    uint8_t target_y;       // Target analog axis for Y (0xFF = disabled)
} mouse_accumulator_t;

// Special value to disable an axis in mouse-to-analog transform
#define MOUSE_AXIS_DISABLED 0xFF

// Instance merging state (for Joy-Con Grip, etc.)
typedef struct {
    bool active;            // Is this a merged device?
    uint8_t instance_count; // How many instances are merged
    uint8_t root_instance;  // Root instance ID
} instance_merge_t;

// ============================================================================
// ROUTER CONFIGURATION
// ============================================================================

#define MAX_OUTPUTS OUTPUT_TARGET_COUNT
#define MAX_PLAYERS_PER_OUTPUT 8

typedef struct {
    routing_mode_t mode;
    merge_mode_t merge_mode;
    uint8_t max_players_per_output[MAX_OUTPUTS];  // Per-output limits (GC=4, 3DO=8, PCE=5)
    bool merge_all_inputs;                        // Merge all inputs to single output (current GC)

    // Input transformations (Phase 5)
    uint8_t transform_flags;                      // Which transformations to enable (bitfield)
    uint8_t mouse_drain_rate;                     // Mouse accumulator drain rate (0 = NO drain/hold, >0 = drain)
    uint8_t mouse_target_x;                       // Target axis for mouse X (default: ANALOG_LX)
    uint8_t mouse_target_y;                       // Target axis for mouse Y (MOUSE_AXIS_DISABLED to disable)
} router_config_t;

// ============================================================================
// ROUTER INITIALIZATION
// ============================================================================

// Initialize router with configuration
void router_init(const router_config_t* config);

// ============================================================================
// INPUT SUBMISSION (Core 0 - Event Driven, replaces post_input_event)
// ============================================================================

// Called immediately when input arrives (USB report, BLE notification, etc.)
// Processes event and updates output state atomically
// NOTE: This is the ONLY function input drivers should call!
void router_submit_input(const input_event_t* event);

// ============================================================================
// OUTPUT RETRIEVAL (Core 1 - Poll or Event Driven)
// ============================================================================

// Get latest input state for this output+player (returns NULL if no update)
// Lock-free read, zero-copy (returns pointer to internal state)
const input_event_t* router_get_output(output_target_t output, uint8_t player_id);

// Check if any player has new data (fast scan for multi-player outputs)
bool router_has_updates(output_target_t output);

// Get player count for this output
uint8_t router_get_player_count(output_target_t output);

// ============================================================================
// ROUTING TABLES (Phase 6)
// ============================================================================

#ifndef MAX_ROUTES
#define MAX_ROUTES 32  // Maximum number of routes in routing table
#endif

// Route entry for N:M routing
typedef struct {
    input_source_t input;       // Input source (USB_HOST, BLE, etc.)
    output_target_t output;     // Output target (GAMECUBE, PCENGINE, etc.)
    uint8_t priority;           // Priority (0 = highest, 255 = lowest)
    bool active;                // Is this route active?

    // Optional filters (0 = wildcard, matches all)
    uint8_t input_dev_addr;     // Filter by USB device address
    int8_t input_instance;      // Filter by device instance
    uint8_t output_player_id;   // Target specific player slot (0xFF = auto-assign)
} route_entry_t;

// ============================================================================
// ROUTING CONFIGURATION (called by apps at init or runtime)
// ============================================================================

// Add simple route (input → output mapping)
// Returns true if route added successfully, false if table full
bool router_add_route(input_source_t input, output_target_t output, uint8_t priority);

// Add route with filters (advanced routing)
bool router_add_route_filtered(const route_entry_t* route);

// Remove specific route by index
void router_remove_route(uint8_t route_index);

// Clear all routes (for runtime reconfiguration)
void router_clear_routes(void);

// Get number of active routes
uint8_t router_get_route_count(void);

// Get route by index (for debugging/inspection)
const route_entry_t* router_get_route(uint8_t route_index);

// Set merge mode for output
void router_set_merge_mode(output_target_t output, merge_mode_t mode);

// Set active outputs (for broadcast mode)
void router_set_active_outputs(output_target_t* outputs, uint8_t count);

// Get primary active output (first in active outputs list)
// Returns OUTPUT_TARGET_NONE if no outputs configured
output_target_t router_get_primary_output(void);

// Reset all output states to neutral (call when all controllers disconnect)
void router_reset_outputs(void);

// Clean up router state when a device disconnects
// This clears the device's output state and removes it from blend tracking
// Call this BEFORE removing the player from the player manager
void router_device_disconnected(uint8_t dev_addr, int8_t instance);

// ============================================================================
// OUTPUT TAP (Push-based notification)
// ============================================================================
// For outputs that need push notification (UART, BLE) instead of polling

// Tap callback - called when router updates output state
// output: which output target
// player_index: which player slot (0-based)
// event: the updated input event
typedef void (*router_tap_callback_t)(output_target_t output, uint8_t player_index,
                                       const input_event_t* event);

// Set tap callback for an output (NULL to disable)
// Output still stores to router_outputs[] for polling via router_get_output()
void router_set_tap(output_target_t output, router_tap_callback_t callback);

// Set tap callback with exclusive mode — output is fully push-based,
// router skips storing to router_outputs[] (avoids copy on hot path).
// Use this when the output never calls router_get_output().
void router_set_tap_exclusive(output_target_t output, router_tap_callback_t callback);

// ============================================================================
// INTERNAL STATE (exposed for debugging, don't modify directly)
// ============================================================================

// Output state structure (replaces players[] array)
typedef struct {
    input_event_t current_state;    // Latest event (atomic write)
    volatile bool updated;           // New data flag
    uint8_t player_id;               // Player slot assignment
    input_source_t source;           // Source of this input (for priority)
} output_state_t;

// Get pointer to output state array (for debugging/testing)
output_state_t* router_get_state_ptr(output_target_t output);

#endif // ROUTER_H
