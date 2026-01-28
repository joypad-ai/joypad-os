// router.c
// Joypad Core Router Implementation
//
// Zero-latency event-driven routing system.
// Replaces console-specific post_input_event() with unified routing.

#include "router.h"
#include "core/services/players/manager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// CDC input streaming (optional, for web config)
#ifdef CONFIG_USB
#include "usb/usbd/cdc/cdc_commands.h"
#endif

// Device name lookup for USB HID
#ifdef CONFIG_USB_HOST
#include "usb/usbh/hid/hid_registry.h"
#include "tusb.h"
extern int hid_get_ctrl_type(uint8_t dev_addr, uint8_t instance);
#endif

// Device name lookup for Bluetooth
#ifdef ENABLE_BTSTACK
#include "bt/bthid/bthid.h"
#endif

// ============================================================================
// AUTO-ASSIGN CONFIGURATION
// ============================================================================

// Log tag for consistent logging
#define LOG_TAG "[ROUTER]"

// Threshold for analog stick movement to trigger player auto-assign
// Value is distance from center (128). Range 0-127.
// 50 means stick must move to < 78 or > 178 to trigger (about 40% deflection)
#define ANALOG_ASSIGN_THRESHOLD 50

// Check if any analog stick is moved beyond threshold
// Returns true if left or right stick is deflected significantly
static inline bool analog_beyond_threshold(const input_event_t* event) {
    // Check left stick X/Y and right stick X/Y (first 4 analog axes)
    for (int i = 0; i < 4; i++) {
        int deflection = (int)event->analog[i] - 128;
        if (deflection < 0) deflection = -deflection;  // abs()
        if (deflection > ANALOG_ASSIGN_THRESHOLD) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// DEVICE NAME LOOKUP
// ============================================================================

// Get device name based on transport type and device address
// Returns pointer to static string or device name buffer
static const char* get_device_name(const input_event_t* event) {
    switch (event->transport) {
#ifdef CONFIG_USB_HOST
        case INPUT_TRANSPORT_USB: {
            int ctrl_type = hid_get_ctrl_type(event->dev_addr, event->instance);
            if (ctrl_type >= 0 && ctrl_type < CONTROLLER_TYPE_COUNT &&
                device_interfaces[ctrl_type] && device_interfaces[ctrl_type]->name) {
                // Check for Switch 2 variants by PID
                if (ctrl_type == CONTROLLER_SWITCH2) {
                    uint16_t vid, pid;
                    tuh_vid_pid_get(event->dev_addr, &vid, &pid);
                    if (pid == 0x2073) {  // Switch 2 GameCube PID
                        return "Switch 2 GameCube";
                    }
                }
                return device_interfaces[ctrl_type]->name;
            }
            return "USB Device";
        }
#endif
#ifdef ENABLE_BTSTACK
        case INPUT_TRANSPORT_BT_CLASSIC:
        case INPUT_TRANSPORT_BT_BLE: {
            bthid_device_t* bt_dev = bthid_get_device(event->dev_addr);
            if (bt_dev) {
                // Check for Switch 2 variants by PID (Nintendo VID 0x057E)
                if (bt_dev->vendor_id == 0x057E) {
                    if (bt_dev->product_id == 0x2073) {
                        return "Switch 2 GameCube";
                    } else if (bt_dev->product_id == 0x2069) {
                        return "Switch 2 Pro";
                    } else if (bt_dev->product_id == 0x2066) {
                        return "Switch 2 Joy-Con L";
                    } else if (bt_dev->product_id == 0x2067) {
                        return "Switch 2 Joy-Con R";
                    }
                }
                // Use driver's friendly name if available
                if (bt_dev->driver) {
                    const bthid_driver_t* driver = (const bthid_driver_t*)bt_dev->driver;
                    if (driver->name) {
                        return driver->name;
                    }
                }
                // Fallback to raw Bluetooth device name
                if (bt_dev->name[0]) {
                    return bt_dev->name;
                }
            }
            return "BT Device";
        }
#endif
        case INPUT_TRANSPORT_NATIVE:
            return "Native";
        default:
            return "Unknown";
    }
}

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
        printf(LOG_TAG "ERROR: NULL config\n");
        return;
    }

    // Copy configuration
    router_config = *config;

    printf(LOG_TAG "Initializing router\n");
    printf(LOG_TAG "  Mode: %s\n",
        config->mode == ROUTING_MODE_SIMPLE ? "SIMPLE" :
        config->mode == ROUTING_MODE_MERGE ? "MERGE" :
        config->mode == ROUTING_MODE_BROADCAST ? "BROADCAST" : "CONFIGURABLE");

    if (config->mode == ROUTING_MODE_MERGE) {
        printf(LOG_TAG "  Merge mode: %s\n",
            config->merge_mode == MERGE_PRIORITY ? "PRIORITY" :
            config->merge_mode == MERGE_BLEND ? "BLEND" : "ALL");
        printf(LOG_TAG "  Merge all inputs: %s\n", config->merge_all_inputs ? "YES" : "NO");
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
            mouse_accumulators[output][player].target_x = config->mouse_target_x;
            mouse_accumulators[output][player].target_y = config->mouse_target_y;

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

    printf(LOG_TAG "Initialized successfully\n");
    if (config->transform_flags) {
        printf(LOG_TAG "  Transformations enabled: 0x%02x\n", config->transform_flags);
        if (config->transform_flags & TRANSFORM_MOUSE_TO_ANALOG) {
            printf(LOG_TAG "    - Mouse-to-analog (target_x=%d, target_y=%d, drain=%d)\n",
                   config->mouse_target_x, config->mouse_target_y, config->mouse_drain_rate);
        }
        if (config->transform_flags & TRANSFORM_MERGE_INSTANCES)
            printf(LOG_TAG "    - Instance merging\n");
        if (config->transform_flags & TRANSFORM_SPINNER)
            printf(LOG_TAG "    - Spinner accumulation\n");
    }
}

// ============================================================================
// INPUT TRANSFORMATIONS (Phase 5)
// ============================================================================

// Mouse-to-analog: Accumulate mouse deltas into analog stick positions
// Configurable target axes and drain behavior for different use cases:
// - Left stick (default): mouse controls movement
// - Right stick: mouse controls camera (e.g., mouthpad for accessibility)
// - drain_rate=0: hold position until input returns to center (no auto-drain)
static void transform_mouse_to_analog(input_event_t* event, output_target_t output, int player_index) {
    if (event->type != INPUT_TYPE_MOUSE) return;
    if (player_index < 0 || player_index >= MAX_PLAYERS_PER_OUTPUT) return;

    mouse_accumulator_t* accum = &mouse_accumulators[output][player_index];

    // Accumulate X-axis if enabled
    if (accum->target_x != MOUSE_AXIS_DISABLED) {
        // Handle signed 8-bit deltas
        if (event->delta_x >= 128)
            accum->accum_x -= (256 - event->delta_x);
        else
            accum->accum_x += event->delta_x;

        // Clamp to [-127, 127]
        if (accum->accum_x > 127) accum->accum_x = 127;
        if (accum->accum_x < -127) accum->accum_x = -127;

        // Convert to analog position (centered at 128)
        event->analog[accum->target_x] = 128 + accum->accum_x;

        // Drain toward center (only if drain_rate > 0)
        if (accum->drain_rate > 0) {
            if (accum->accum_x > 0) {
                accum->accum_x -= (accum->accum_x > accum->drain_rate) ? accum->drain_rate : accum->accum_x;
            } else if (accum->accum_x < 0) {
                accum->accum_x += (-accum->accum_x > accum->drain_rate) ? accum->drain_rate : -accum->accum_x;
            }
        }
    }

    // Accumulate Y-axis if enabled
    if (accum->target_y != MOUSE_AXIS_DISABLED) {
        // Handle signed 8-bit deltas
        if (event->delta_y >= 128)
            accum->accum_y -= (256 - event->delta_y);
        else
            accum->accum_y += event->delta_y;

        // Clamp to [-127, 127]
        if (accum->accum_y > 127) accum->accum_y = 127;
        if (accum->accum_y < -127) accum->accum_y = -127;

        // Convert to analog position (centered at 128)
        event->analog[accum->target_y] = 128 + accum->accum_y;

        // Drain toward center (only if drain_rate > 0)
        if (accum->drain_rate > 0) {
            if (accum->accum_y > 0) {
                accum->accum_y -= (accum->accum_y > accum->drain_rate) ? accum->drain_rate : accum->accum_y;
            } else if (accum->accum_y < 0) {
                accum->accum_y += (-accum->accum_y > accum->drain_rate) ? accum->drain_rate : -accum->accum_y;
            }
        }
    }

    // Clear delta fields (analog values now set)
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
        printf(LOG_TAG "ERROR: Routing table full (%d routes)\n", MAX_ROUTES);
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
    printf(LOG_TAG "Route added: %s → %s (priority=%d)\n",
        input == INPUT_SOURCE_USB_HOST ? "USB" : "?",
        output == OUTPUT_TARGET_GAMECUBE ? "GameCube" :
        output == OUTPUT_TARGET_PCENGINE ? "PCEngine" :
        output == OUTPUT_TARGET_NUON ? "Nuon" :
        output == OUTPUT_TARGET_XBOXONE ? "XboxOne" :
        output == OUTPUT_TARGET_NEOGEO ? "NEOGEO" :
        output == OUTPUT_TARGET_LOOPY ? "Loopy" : "?",
        priority);

    return true;
}

// Add route with filters (advanced)
bool router_add_route_filtered(const route_entry_t* route) {
    if (!route || route_count >= MAX_ROUTES) {
        printf(LOG_TAG "ERROR: Cannot add filtered route\n");
        return false;
    }

    routing_table[route_count] = *route;
    routing_table[route_count].active = true;
    route_count++;

    printf(LOG_TAG "Filtered route added (dev_addr=%d, instance=%d, player=%d)\n",
        route->input_dev_addr, route->input_instance, route->output_player_id);

    return true;
}

// Remove route by index
void router_remove_route(uint8_t route_index) {
    if (route_index >= MAX_ROUTES || !routing_table[route_index].active) return;

    routing_table[route_index].active = false;
    printf(LOG_TAG "Route %d removed\n", route_index);
}

// Clear all routes
void router_clear_routes(void) {
    for (uint8_t i = 0; i < MAX_ROUTES; i++) {
        routing_table[i].active = false;
    }
    route_count = 0;
    printf(LOG_TAG "All routes cleared\n");
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
        // Check if any button pressed or analog stick moved beyond threshold
        uint32_t buttons_pressed = event->buttons | event->keys;
        bool analog_active = analog_beyond_threshold(event);
        if (buttons_pressed || analog_active) {
            const char* device_name = get_device_name(event);
            player_index = add_player(event->dev_addr, event->instance, event->transport, device_name);
            if (player_index >= 0) {
                printf(LOG_TAG "Player %d assigned: %s (dev_addr=%d, instance=%d)\n",
                    player_index + 1, device_name, event->dev_addr, event->instance);
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
        router_outputs[output][player_index].source = INPUT_SOURCE_USB_HOST;

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
        bool analog_active = analog_beyond_threshold(event);
        if (buttons_pressed || analog_active || event->type == INPUT_TYPE_MOUSE) {
            const char* device_name = get_device_name(event);
            player_index = add_player(event->dev_addr, event->instance, event->transport, device_name);
            if (player_index >= 0) {
                printf(LOG_TAG "Player %d assigned in merge mode: %s (dev_addr=%d, instance=%d)\n",
                    player_index + 1, device_name, event->dev_addr, event->instance);
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
                // Note: deltas are cleared here but accumulated fresh from blend devices
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

                    // Analog: use furthest from center for sticks, max for triggers
                    // New format: [0]=LX, [1]=LY, [2]=RX, [3]=RY, [4]=L2, [5]=R2
                    for (int j = 0; j < ANALOG_COUNT; j++) {
                        if (j >= ANALOG_L2) {
                            // Triggers (L2, R2): use max value
                            if (dev->analog[j] > out->current_state.analog[j]) {
                                out->current_state.analog[j] = dev->analog[j];
                            }
                        } else {
                            // Sticks (LX, LY, RX, RY): use furthest from center
                            int8_t cur_delta = (int8_t)(out->current_state.analog[j] - 128);
                            int8_t dev_delta = (int8_t)(dev->analog[j] - 128);
                            if (abs(dev_delta) > abs(cur_delta)) {
                                out->current_state.analog[j] = dev->analog[j];
                            }
                        }
                    }

                    // Mouse deltas: accumulate from all, then clear device to prevent re-adding
                    out->current_state.delta_x += dev->delta_x;
                    out->current_state.delta_y += dev->delta_y;
                    dev->delta_x = 0;
                    dev->delta_y = 0;

                    // Motion: use first device that has motion data
                    if (dev->has_motion && !out->current_state.has_motion) {
                        out->current_state.has_motion = true;
                        out->current_state.accel[0] = dev->accel[0];
                        out->current_state.accel[1] = dev->accel[1];
                        out->current_state.accel[2] = dev->accel[2];
                        out->current_state.gyro[0] = dev->gyro[0];
                        out->current_state.gyro[1] = dev->gyro[1];
                        out->current_state.gyro[2] = dev->gyro[2];
                    }

                    // Pressure: use first device that has pressure data
                    if (dev->has_pressure && !out->current_state.has_pressure) {
                        out->current_state.has_pressure = true;
                        for (int j = 0; j < 12; j++) {
                            out->current_state.pressure[j] = dev->pressure[j];
                        }
                    }

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
void router_submit_input(const input_event_t* event) {
    if (!event) return;
    if (route_count == 0) return;

    // Stream input to CDC for web config (if enabled)
#ifdef CONFIG_USB
    cdc_commands_send_input_event(event->buttons, event->analog);
#endif

    // Find first active route to determine output target
    output_target_t output = OUTPUT_TARGET_USB_DEVICE;
    for (uint8_t i = 0; i < MAX_ROUTES; i++) {
        if (routing_table[i].active) {
            output = routing_table[i].output;
            break;
        }
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
            if (active_output_count > 0) {
                for (uint8_t i = 0; i < active_output_count; i++) {
                    router_simple_mode(event, active_outputs[i]);
                }
            } else {
                router_simple_mode(event, output);
            }
            break;

        case ROUTING_MODE_CONFIGURABLE:
            {
                route_entry_t matches[MAX_ROUTES];
                uint8_t match_count = router_find_routes(event, matches, MAX_ROUTES);

                if (match_count == 0) {
                    router_simple_mode(event, output);
                } else {
                    for (uint8_t i = 0; i < match_count; i++) {
                        output_target_t target = matches[i].output;
                        uint8_t target_player = matches[i].output_player_id;

                        if (target_player != 0xFF && target_player < MAX_PLAYERS_PER_OUTPUT) {
                            input_event_t transformed = *event;
                            apply_transformations(&transformed, target, target_player);

                            router_outputs[target][target_player].current_state = transformed;
                            router_outputs[target][target_player].updated = true;
                            router_outputs[target][target_player].source = INPUT_SOURCE_USB_HOST;

                            if (output_taps[target]) {
                                output_taps[target](target, target_player, &transformed);
                            }
                        } else {
                            router_simple_mode(event, target);
                        }
                    }
                }
            }
            break;
    }
}

// ============================================================================
// OUTPUT RETRIEVAL (Core 1 - Poll or Event Driven)
// ============================================================================

// Static buffer for returning copies (so we can clear original deltas)
static input_event_t router_output_copy[MAX_OUTPUTS][MAX_PLAYERS_PER_OUTPUT];

const input_event_t* __not_in_flash_func(router_get_output)(output_target_t output, uint8_t player_id) {
    if (output >= MAX_OUTPUTS || player_id >= MAX_PLAYERS_PER_OUTPUT) {
        return NULL;
    }

    if (router_outputs[output][player_id].updated) {
        router_outputs[output][player_id].updated = false;  // Mark as read
        
        // Copy to static buffer so caller gets the deltas
        router_output_copy[output][player_id] = router_outputs[output][player_id].current_state;
        
        // Clear deltas from original (they've been consumed)
        router_outputs[output][player_id].current_state.delta_x = 0;
        router_outputs[output][player_id].current_state.delta_y = 0;
        
        return &router_output_copy[output][player_id];
    }

    // No update - return NULL (don't re-process same deltas)
    return NULL;
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
    printf(LOG_TAG "Merge mode set: %s\n",
        mode == MERGE_PRIORITY ? "PRIORITY" :
        mode == MERGE_BLEND ? "BLEND" : "ALL");
}

void router_set_active_outputs(output_target_t* outputs, uint8_t count) {
    if (!outputs || count > MAX_OUTPUTS) return;

    active_output_count = count;
    for (uint8_t i = 0; i < count; i++) {
        active_outputs[i] = outputs[i];
    }

    printf(LOG_TAG "Active outputs set: count=%d\n", count);
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
        printf(LOG_TAG "Tap %s for output %d\n",
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

// Reset all output states to neutral (call when all controllers disconnect)
void router_reset_outputs(void) {
    printf(LOG_TAG "Resetting all outputs to neutral\n");

    // Reset all output states
    for (uint8_t output = 0; output < MAX_OUTPUTS; output++) {
        for (uint8_t player = 0; player < MAX_PLAYERS_PER_OUTPUT; player++) {
            init_input_event(&router_outputs[output][player].current_state);
            router_outputs[output][player].updated = true;  // Signal that state changed
        }

        // Clear blend device tracking
        for (uint8_t i = 0; i < MAX_BLEND_DEVICES; i++) {
            blend_devices[output][i].active = false;
            blend_devices[output][i].dev_addr = 0;
            blend_devices[output][i].instance = -1;
            init_input_event(&blend_devices[output][i].state);
        }
    }
}

// Clean up router state when a device disconnects
void router_device_disconnected(uint8_t dev_addr, int8_t instance) {
    printf(LOG_TAG "Device disconnected: dev_addr=%d, instance=%d\n", dev_addr, instance);

    // Find the player index for this device
    int player_index = find_player_index(dev_addr, instance);

    // Find first active route to determine output target
    output_target_t output = OUTPUT_TARGET_USB_DEVICE;
    for (uint8_t i = 0; i < MAX_ROUTES; i++) {
        if (routing_table[i].active) {
            output = routing_table[i].output;
            break;
        }
    }

    // Clear blend device tracking for this device (MERGE_BLEND mode)
    for (uint8_t out = 0; out < MAX_OUTPUTS; out++) {
        for (uint8_t i = 0; i < MAX_BLEND_DEVICES; i++) {
            if (blend_devices[out][i].active &&
                blend_devices[out][i].dev_addr == dev_addr &&
                blend_devices[out][i].instance == instance) {
                blend_devices[out][i].active = false;
                blend_devices[out][i].dev_addr = 0;
                blend_devices[out][i].instance = -1;
                init_input_event(&blend_devices[out][i].state);
                printf(LOG_TAG "Cleared blend device slot %d for output %d\n", i, out);
            }
        }
    }

    // For MERGE mode, all inputs go to player 0 - re-blend remaining devices
    if (router_config.mode == ROUTING_MODE_MERGE) {
        output_state_t* out_state = &router_outputs[output][0];
        init_input_event(&out_state->current_state);

        if (router_config.merge_mode == MERGE_BLEND) {
            // Re-blend all remaining active devices
            for (uint8_t i = 0; i < MAX_BLEND_DEVICES; i++) {
                if (!blend_devices[output][i].active) continue;

                input_event_t* dev = &blend_devices[output][i].state;

                // Buttons: OR together
                out_state->current_state.buttons |= dev->buttons;
                out_state->current_state.keys |= dev->keys;

                // Analog: use furthest from center for sticks, max for triggers
                // Format: [0]=LX, [1]=LY, [2]=RX, [3]=RY, [4]=L2, [5]=R2
                for (int j = 0; j < ANALOG_COUNT; j++) {
                    if (j >= ANALOG_L2) {
                        // Triggers: use max value
                        if (dev->analog[j] > out_state->current_state.analog[j]) {
                            out_state->current_state.analog[j] = dev->analog[j];
                        }
                    } else {
                        // Sticks: use furthest from center
                        int8_t cur_delta = (int8_t)(out_state->current_state.analog[j] - 128);
                        int8_t dev_delta = (int8_t)(dev->analog[j] - 128);
                        if (abs(dev_delta) > abs(cur_delta)) {
                            out_state->current_state.analog[j] = dev->analog[j];
                        }
                    }
                }
            }
        }

        out_state->updated = true;

        // Always notify tap with current state (zeroed or re-blended)
        if (output_taps[output]) {
            output_taps[output](output, 0, &out_state->current_state);
        }

        printf(LOG_TAG "Updated merged output (player 0)\n");
    } else {
        // SIMPLE/BROADCAST mode: clear this player's specific output state
        if (player_index >= 0 && player_index < MAX_PLAYERS_PER_OUTPUT) {
            init_input_event(&router_outputs[output][player_index].current_state);
            router_outputs[output][player_index].updated = true;

            // Notify tap if registered (sends zeroed state to USB/UART output)
            if (output_taps[output]) {
                output_taps[output](output, player_index, &router_outputs[output][player_index].current_state);
            }

            printf(LOG_TAG "Cleared output state for player %d\n", player_index);
        }
    }
}
