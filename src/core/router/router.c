// router.c
// USBRetro Core Router Implementation
//
// Zero-latency event-driven routing system.
// Replaces console-specific post_input_event() with unified routing.

#include "router.h"
#include "core/services/players/manager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================================
// OUTPUT STATE (replaces players[] array)
// ============================================================================

// Output state per output type (GameCube, PCEngine, 3DO, etc.)
// Each output has up to MAX_PLAYERS_PER_OUTPUT player slots
static output_state_t router_outputs[MAX_OUTPUTS][MAX_PLAYERS_PER_OUTPUT];

// Router configuration (set at init)
static router_config_t router_config;

// Active output count (for broadcast mode)
static output_target_t active_outputs[MAX_OUTPUTS];
static uint8_t active_output_count = 0;

// ============================================================================
// TRANSFORMATION STATE (Phase 5)
// ============================================================================

// Mouse-to-analog accumulators (per output, per player)
static mouse_accumulator_t mouse_accumulators[MAX_OUTPUTS][MAX_PLAYERS_PER_OUTPUT];

// Instance merging state (per output, per player)
static instance_merge_t instance_merges[MAX_OUTPUTS][MAX_PLAYERS_PER_OUTPUT];

// ============================================================================
// MERGE_BLEND STATE - Per-device input tracking for proper blending
// ============================================================================

#define MAX_BLEND_DEVICES 8  // Max devices to track for blending

typedef struct {
    uint8_t dev_addr;
    int8_t instance;
    bool active;
    input_event_t state;
} blend_device_state_t;

// Per-output blend state (tracks each device's contribution)
static blend_device_state_t blend_devices[MAX_OUTPUTS][MAX_BLEND_DEVICES];

// ============================================================================
// ROUTING TABLE (Phase 6)
// ============================================================================

// Routing table for N:M input-to-output mapping
static route_entry_t routing_table[MAX_ROUTES];
static uint8_t route_count = 0;

// ============================================================================
// OUTPUT TAPS (Push-based notification)
// ============================================================================

static router_tap_callback_t output_taps[MAX_OUTPUTS] = {NULL};

// ============================================================================
// INITIALIZATION
// ============================================================================

void router_init(const router_config_t* config) {
    if (!config) {
        printf("[router] ERROR: NULL config\n");
        return;
    }

    // Copy configuration
    router_config = *config;

    printf("[router] Initializing router\n");
    printf("[router]   Mode: %s\n",
        config->mode == ROUTING_MODE_SIMPLE ? "SIMPLE" :
        config->mode == ROUTING_MODE_MERGE ? "MERGE" :
        config->mode == ROUTING_MODE_BROADCAST ? "BROADCAST" : "CONFIGURABLE");

    if (config->mode == ROUTING_MODE_MERGE) {
        printf("[router]   Merge mode: %s\n",
            config->merge_mode == MERGE_PRIORITY ? "PRIORITY" :
            config->merge_mode == MERGE_BLEND ? "BLEND" : "ALL");
        printf("[router]   Merge all inputs: %s\n", config->merge_all_inputs ? "YES" : "NO");
    }

    // Initialize output states
    for (uint8_t output = 0; output < MAX_OUTPUTS; output++) {
        for (uint8_t player = 0; player < MAX_PLAYERS_PER_OUTPUT; player++) {
            init_input_event(&router_outputs[output][player].current_state);
            router_outputs[output][player].updated = false;
            router_outputs[output][player].player_id = player;
            router_outputs[output][player].source = INPUT_SOURCE_USB_HOST;  // Default

            // Initialize transformation state
            mouse_accumulators[output][player].accum_x = 0;
            mouse_accumulators[output][player].accum_y = 0;
            mouse_accumulators[output][player].drain_rate = config->mouse_drain_rate;

            instance_merges[output][player].active = false;
            instance_merges[output][player].instance_count = 0;
            instance_merges[output][player].root_instance = 0;
        }

        // Initialize blend device tracking
        for (uint8_t i = 0; i < MAX_BLEND_DEVICES; i++) {
            blend_devices[output][i].active = false;
            blend_devices[output][i].dev_addr = 0;
            blend_devices[output][i].instance = -1;
            init_input_event(&blend_devices[output][i].state);
        }
    }

    // Initialize routing table
    router_clear_routes();

    printf("[router] Initialized successfully\n");
    if (config->transform_flags) {
        printf("[router]   Transformations enabled: 0x%02x\n", config->transform_flags);
        if (config->transform_flags & TRANSFORM_MOUSE_TO_ANALOG)
            printf("[router]     - Mouse-to-analog (drain_rate=%d)\n", config->mouse_drain_rate);
        if (config->transform_flags & TRANSFORM_MERGE_INSTANCES)
            printf("[router]     - Instance merging\n");
        if (config->transform_flags & TRANSFORM_SPINNER)
            printf("[router]     - Spinner accumulation\n");
    }
}

// ============================================================================
// INPUT TRANSFORMATIONS (Phase 5)
// ============================================================================

// Mouse-to-analog: Accumulate mouse deltas into analog stick positions
// This replaces the console-specific accumulator code in Xbox/PCEngine/Loopy
static void transform_mouse_to_analog(input_event_t* event, output_target_t output, int player_index) {
    if (event->type != INPUT_TYPE_MOUSE) return;
    if (player_index < 0 || player_index >= MAX_PLAYERS_PER_OUTPUT) return;

    mouse_accumulator_t* accum = &mouse_accumulators[output][player_index];

    // Accumulate mouse deltas (handle signed 8-bit deltas)
    if (event->delta_x >= 128)
        accum->accum_x -= (256 - event->delta_x);
    else
        accum->accum_x += event->delta_x;

    if (event->delta_y >= 128)
        accum->accum_y -= (256 - event->delta_y);
    else
        accum->accum_y += event->delta_y;

    // Clamp accumulator to [-127, 127]
    if (accum->accum_x > 127) accum->accum_x = 127;
    if (accum->accum_x < -127) accum->accum_x = -127;
    if (accum->accum_y > 127) accum->accum_y = 127;
    if (accum->accum_y < -127) accum->accum_y = -127;

    // Convert accumulated deltas to analog stick positions (centered at 128)
    event->analog[ANALOG_X] = 128 + accum->accum_x;  // Left stick X
    event->analog[ANALOG_Y] = 128 + accum->accum_y;  // Left stick Y

    // Drain accumulator gradually (move back toward center)
    if (accum->accum_x > 0) {
        accum->accum_x -= (accum->accum_x > accum->drain_rate) ? accum->drain_rate : accum->accum_x;
    } else if (accum->accum_x < 0) {
        accum->accum_x += (-accum->accum_x > accum->drain_rate) ? accum->drain_rate : -accum->accum_x;
    }

    if (accum->accum_y > 0) {
        accum->accum_y -= (accum->accum_y > accum->drain_rate) ? accum->drain_rate : accum->accum_y;
    } else if (accum->accum_y < 0) {
        accum->accum_y += (-accum->accum_y > accum->drain_rate) ? accum->drain_rate : -accum->accum_y;
    }

    // Clear delta fields (no longer needed, analog values now set)
    event->delta_x = 0;
    event->delta_y = 0;
}

// Instance merging: Merge multi-instance devices (Joy-Con Grip, etc.)
// TODO Phase 5: Implement Joy-Con Grip merging
static void transform_merge_instances(input_event_t* event, output_target_t output, int player_index) {
    if (player_index < 0 || player_index >= MAX_PLAYERS_PER_OUTPUT) return;

    // TODO: Detect multi-instance devices (instance == -1 flag from device driver)
    // TODO: Merge button states and analog inputs from both instances
    // TODO: Present as single unified controller

    (void)event;  // Suppress unused warning for now
    (void)output;
}

// Apply transformations to input event (modifies event in-place)
static void apply_transformations(input_event_t* event, output_target_t output, int player_index) {
    if (!router_config.transform_flags) return;  // No transformations enabled

    // Apply mouse-to-analog transformation
    if (router_config.transform_flags & TRANSFORM_MOUSE_TO_ANALOG) {
        transform_mouse_to_analog(event, output, player_index);
    }

    // Apply instance merging
    if (router_config.transform_flags & TRANSFORM_MERGE_INSTANCES) {
        transform_merge_instances(event, output, player_index);
    }

    // TODO: TRANSFORM_SPINNER (Nuon spinner accumulation)
}

// ============================================================================
// ROUTING TABLE MANAGEMENT (Phase 6)
// ============================================================================

// Add simple route (input → output)
bool router_add_route(input_source_t input, output_target_t output, uint8_t priority) {
    if (route_count >= MAX_ROUTES) {
        printf("[router] ERROR: Routing table full (%d routes)\n", MAX_ROUTES);
        return false;
    }

    routing_table[route_count].input = input;
    routing_table[route_count].output = output;
    routing_table[route_count].priority = priority;
    routing_table[route_count].active = true;
    routing_table[route_count].input_dev_addr = 0;      // Wildcard
    routing_table[route_count].input_instance = -1;     // Wildcard
    routing_table[route_count].output_player_id = 0xFF; // Auto-assign

    route_count++;
    printf("[router] Route added: %s → %s (priority=%d)\n",
        input == INPUT_SOURCE_USB_HOST ? "USB" : "?",
        output == OUTPUT_TARGET_GAMECUBE ? "GameCube" :
        output == OUTPUT_TARGET_PCENGINE ? "PCEngine" :
        output == OUTPUT_TARGET_NUON ? "Nuon" :
        output == OUTPUT_TARGET_XBOXONE ? "XboxOne" :
        output == OUTPUT_TARGET_LOOPY ? "Loopy" : "?",
        priority);

    return true;
}

// Add route with filters (advanced)
bool router_add_route_filtered(const route_entry_t* route) {
    if (!route || route_count >= MAX_ROUTES) {
        printf("[router] ERROR: Cannot add filtered route\n");
        return false;
    }

    routing_table[route_count] = *route;
    routing_table[route_count].active = true;
    route_count++;

    printf("[router] Filtered route added (dev_addr=%d, instance=%d, player=%d)\n",
        route->input_dev_addr, route->input_instance, route->output_player_id);

    return true;
}

// Remove route by index
void router_remove_route(uint8_t route_index) {
    if (route_index >= MAX_ROUTES || !routing_table[route_index].active) return;

    routing_table[route_index].active = false;
    printf("[router] Route %d removed\n", route_index);
}

// Clear all routes
void router_clear_routes(void) {
    for (uint8_t i = 0; i < MAX_ROUTES; i++) {
        routing_table[i].active = false;
    }
    route_count = 0;
    printf("[router] All routes cleared\n");
}

// Get number of active routes
uint8_t router_get_route_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < MAX_ROUTES; i++) {
        if (routing_table[i].active) count++;
    }
    return count;
}

// Get route by index
const route_entry_t* router_get_route(uint8_t route_index) {
    if (route_index >= MAX_ROUTES || !routing_table[route_index].active) {
        return NULL;
    }
    return &routing_table[route_index];
}

// Find matching routes for an input event
// Returns number of matches found (fills matches array)
static uint8_t router_find_routes(const input_event_t* event, route_entry_t* matches, uint8_t max_matches) {
    uint8_t match_count = 0;

    for (uint8_t i = 0; i < MAX_ROUTES && match_count < max_matches; i++) {
        if (!routing_table[i].active) continue;

        // Check if input source matches (wildcard = INPUT_SOURCE_USB_HOST)
        if (routing_table[i].input != INPUT_SOURCE_USB_HOST) {
            // TODO: Add other input sources (BLE, GPIO, etc.)
            continue;
        }

        // Check device address filter (0 = wildcard)
        if (routing_table[i].input_dev_addr != 0 &&
            routing_table[i].input_dev_addr != event->dev_addr) {
            continue;
        }

        // Check instance filter (-1 = wildcard)
        if (routing_table[i].input_instance != -1 &&
            routing_table[i].input_instance != event->instance) {
            continue;
        }

        // Match found!
        matches[match_count++] = routing_table[i];
    }

    return match_count;
}

// ============================================================================
// INPUT SUBMISSION (Core 0 - Event Driven)
// ============================================================================

// SIMPLE MODE: Direct 1:1 pass-through (zero overhead, can be inlined)
static inline void router_simple_mode(const input_event_t* event, output_target_t output) {
    // Find or add player
    int player_index = find_player_index(event->dev_addr, event->instance);

    if (player_index < 0) {
        // Check if any button pressed (auto-assign on first press, active-high)
        uint32_t buttons_pressed = event->buttons | event->keys;
        if (buttons_pressed) {
            player_index = add_player(event->dev_addr, event->instance);
            if (player_index >= 0) {
                printf("[router] Player %d assigned (dev_addr=%d, instance=%d)\n",
                    player_index + 1, event->dev_addr, event->instance);
            }
        }
    }

    if (player_index >= 0 && player_index < router_config.max_players_per_output[output]) {
        // Create local copy for transformation
        input_event_t transformed = *event;

        // Apply transformations (mouse-to-analog, instance merging, etc.)
        apply_transformations(&transformed, output, player_index);

        // Store transformed event (atomic write)
        router_outputs[output][player_index].current_state = transformed;
        router_outputs[output][player_index].updated = true;
        router_outputs[output][player_index].source = INPUT_SOURCE_USB_HOST;  // Default for now

        // Notify tap if registered (for push-based outputs like UART)
        if (output_taps[output]) {
            output_taps[output](output, player_index, &transformed);
        }
    }
}

// MERGE MODE: Multiple inputs → single output
static inline void router_merge_mode(const input_event_t* event, output_target_t output) {
    // Register player if not already registered (for LED and rumble support)
    int player_index = find_player_index(event->dev_addr, event->instance);
    if (player_index < 0) {
        uint32_t buttons_pressed = event->buttons | event->keys;
        if (buttons_pressed || event->type == INPUT_TYPE_MOUSE) {
            player_index = add_player(event->dev_addr, event->instance);
            if (player_index >= 0) {
                printf("[router] Player %d assigned in merge mode (dev_addr=%d, instance=%d)\n",
                    player_index + 1, event->dev_addr, event->instance);
            }
        }
    }

    // Only process if player is registered
    if (player_index < 0) return;

    // Create local copy for transformation
    input_event_t transformed = *event;

    // Apply transformations (mouse-to-analog, instance merging, etc.)
    apply_transformations(&transformed, output, 0);  // Always player 0 in merge mode

    switch (router_config.merge_mode) {
        case MERGE_ALL:
            // Latest active input wins (overwrites previous state)
            router_outputs[output][0].current_state = transformed;
            break;

        case MERGE_BLEND: {
            // Blend button states together from ALL active devices
            // 1. Update this device's state in blend_devices[]
            // 2. Re-blend all active devices into output

            // Find or create slot for this device
            int slot = -1;
            for (int i = 0; i < MAX_BLEND_DEVICES; i++) {
                if (blend_devices[output][i].active &&
                    blend_devices[output][i].dev_addr == transformed.dev_addr &&
                    blend_devices[output][i].instance == transformed.instance) {
                    slot = i;
                    break;
                }
            }
            if (slot < 0) {
                // Find empty slot
                for (int i = 0; i < MAX_BLEND_DEVICES; i++) {
                    if (!blend_devices[output][i].active) {
                        slot = i;
                        blend_devices[output][i].active = true;
                        blend_devices[output][i].dev_addr = transformed.dev_addr;
                        blend_devices[output][i].instance = transformed.instance;
                        break;
                    }
                }
            }

            if (slot >= 0) {
                // Update this device's state
                blend_devices[output][slot].state = transformed;

                // Now re-blend ALL active devices
                output_state_t* out = &router_outputs[output][0];

                // Start with neutral state (all buttons released)
                init_input_event(&out->current_state);

                // Blend all active devices
                bool first = true;
                for (int i = 0; i < MAX_BLEND_DEVICES; i++) {
                    if (!blend_devices[output][i].active) continue;

                    input_event_t* dev = &blend_devices[output][i].state;

                    // Buttons: OR together (active-high, 1 = pressed)
                    out->current_state.buttons |= dev->buttons;

                    // Keys: OR together (active-high)
                    out->current_state.keys |= dev->keys;

                    // Analog: use furthest from center
                    for (int j = 0; j < 6; j++) {
                        int8_t cur_delta = (int8_t)(out->current_state.analog[j] - 128);
                        int8_t dev_delta = (int8_t)(dev->analog[j] - 128);
                        if (abs(dev_delta) > abs(cur_delta)) {
                            out->current_state.analog[j] = dev->analog[j];
                        }
                    }

                    // Mouse deltas: accumulate from all
                    out->current_state.delta_x += dev->delta_x;
                    out->current_state.delta_y += dev->delta_y;

                    // Use metadata from first active device
                    if (first) {
                        out->current_state.dev_addr = dev->dev_addr;
                        out->current_state.instance = dev->instance;
                        out->current_state.type = dev->type;
                        first = false;
                    }
                }
            }
            break;
        }

        case MERGE_PRIORITY:
            // High priority input wins, low priority fallback
            // Used by Super3D0USB (USB priority, SNES fallback)
            // Check if this source has higher priority than current
            if (router_outputs[output][0].source <= INPUT_SOURCE_USB_HOST) {
                // USB has highest priority (0), always wins
                router_outputs[output][0].current_state = transformed;
            }
            // Lower priority sources only update if no USB input active
            // TODO: Track activity timeout for priority fallback
            break;
    }

    router_outputs[output][0].updated = true;
    router_outputs[output][0].source = INPUT_SOURCE_USB_HOST;

    // Notify tap if registered (for push-based outputs like UART)
    if (output_taps[output]) {
        output_taps[output](output, 0, &router_outputs[output][0].current_state);
    }
}

// Main input submission function (called by input drivers)
void __not_in_flash_func(router_submit_input)(const input_event_t* event) {
    if (!event) return;

    // Determine output target from routing table (configured by apps)
    // Apps call router_add_route() during app_init() to set up routing
    output_target_t output;

    // Use first active route to determine output
    // Apps are responsible for configuring at least one route
    if (route_count > 0) {
        // Find first active route
        bool found = false;
        for (uint8_t i = 0; i < MAX_ROUTES; i++) {
            if (routing_table[i].active) {
                output = routing_table[i].output;
                found = true;
                break;
            }
        }
        if (!found) {
            // No active routes - this is an error, app should have configured routes
            return;
        }
    } else {
        // No routes configured - app didn't call router_add_route()
        // This is a fatal error - app must configure routing
        return;
    }

    // Route based on mode
    switch (router_config.mode) {
        case ROUTING_MODE_SIMPLE:
            router_simple_mode(event, output);
            break;

        case ROUTING_MODE_MERGE:
            router_merge_mode(event, output);
            break;

        case ROUTING_MODE_BROADCAST:
            // Broadcast: 1:N - single input to multiple outputs
            // Used for multi-output products (e.g., USB → GC + USB Device + BLE)
            if (active_output_count > 0) {
                // Route to all active outputs
                for (uint8_t i = 0; i < active_output_count; i++) {
                    router_simple_mode(event, active_outputs[i]);
                }
            } else {
                // No active outputs configured, fall back to first route
                router_simple_mode(event, output);
            }
            break;

        case ROUTING_MODE_CONFIGURABLE:
            // N:M configurable routing - full flexibility
            // Each route can specify: input filters, output target, player slot
            {
                route_entry_t matches[MAX_ROUTES];
                uint8_t match_count = router_find_routes(event, matches, MAX_ROUTES);

                if (match_count == 0) {
                    // No routes found - fall back to first configured route
                    router_simple_mode(event, output);
                } else {
                    // Route to all matching outputs
                    for (uint8_t i = 0; i < match_count; i++) {
                        output_target_t target = matches[i].output;
                        uint8_t target_player = matches[i].output_player_id;

                        if (target_player != 0xFF && target_player < MAX_PLAYERS_PER_OUTPUT) {
                            // Fixed player slot assignment
                            input_event_t transformed = *event;
                            apply_transformations(&transformed, target, target_player);

                            router_outputs[target][target_player].current_state = transformed;
                            router_outputs[target][target_player].updated = true;
                            router_outputs[target][target_player].source = INPUT_SOURCE_USB_HOST;

                            // Notify tap if registered
                            if (output_taps[target]) {
                                output_taps[target](target, target_player, &transformed);
                            }
                        } else {
                            // Auto-assign player slot (standard behavior)
                            router_simple_mode(event, target);
                        }
                    }
                }
            }
            break;
    }

    // Signal Core 1 that new data is available
    // Note: In current architecture, update_output() is called by post_input_event()
    // which we're replacing. For now, Core 1 will poll router_get_output().
    // Future: Add explicit signaling mechanism.
}

// ============================================================================
// OUTPUT RETRIEVAL (Core 1 - Poll or Event Driven)
// ============================================================================

const input_event_t* __not_in_flash_func(router_get_output)(output_target_t output, uint8_t player_id) {
    if (output >= MAX_OUTPUTS || player_id >= MAX_PLAYERS_PER_OUTPUT) {
        return NULL;
    }

    if (router_outputs[output][player_id].updated) {
        router_outputs[output][player_id].updated = false;  // Mark as read
        return &router_outputs[output][player_id].current_state;
    }

    // Return current state even if not updated (for continuous polling)
    return &router_outputs[output][player_id].current_state;
}

bool router_has_updates(output_target_t output) {
    if (output >= MAX_OUTPUTS) return false;

    for (uint8_t player = 0; player < MAX_PLAYERS_PER_OUTPUT; player++) {
        if (router_outputs[output][player].updated) {
            return true;
        }
    }
    return false;
}

uint8_t router_get_player_count(output_target_t output) {
    if (output >= MAX_OUTPUTS) return 0;

    // Return current playersCount (from player management system)
    extern int playersCount;
    return (uint8_t)playersCount;
}

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================

void router_set_merge_mode(output_target_t output, merge_mode_t mode) {
    router_config.merge_mode = mode;
    printf("[router] Merge mode set: %s\n",
        mode == MERGE_PRIORITY ? "PRIORITY" :
        mode == MERGE_BLEND ? "BLEND" : "ALL");
}

void router_set_active_outputs(output_target_t* outputs, uint8_t count) {
    if (!outputs || count > MAX_OUTPUTS) return;

    active_output_count = count;
    for (uint8_t i = 0; i < count; i++) {
        active_outputs[i] = outputs[i];
    }

    printf("[router] Active outputs set: count=%d\n", count);
}

output_target_t router_get_primary_output(void) {
    // First check active_outputs (used by BROADCAST mode)
    if (active_output_count > 0) {
        return active_outputs[0];
    }

    // Fall back to first active route's output (used by SIMPLE/MERGE modes)
    for (uint8_t i = 0; i < MAX_ROUTES; i++) {
        if (routing_table[i].active) {
            return routing_table[i].output;
        }
    }

    return OUTPUT_TARGET_NONE;
}

// ============================================================================
// OUTPUT TAPS
// ============================================================================

void router_set_tap(output_target_t output, router_tap_callback_t callback) {
    if (output >= 0 && output < MAX_OUTPUTS) {
        output_taps[output] = callback;
        printf("[router] Tap %s for output %d\n",
               callback ? "registered" : "unregistered", output);
    }
}

// ============================================================================
// DEBUG/TESTING
// ============================================================================

output_state_t* router_get_state_ptr(output_target_t output) {
    if (output >= MAX_OUTPUTS) return NULL;
    return router_outputs[output];
}
