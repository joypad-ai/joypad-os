#ifndef DEVICE_INTERFACE_H
#define DEVICE_INTERFACE_H

#include <stdint.h>
#include <stdbool.h>
#include "core/services/players/feedback.h"

// ============================================================================
// LEGACY DEVICE OUTPUT CONFIG (deprecated - use feedback_state_t instead)
// ============================================================================
// This struct is being phased out. Device drivers should migrate to using
// feedback_get_state(player_index) to get the canonical feedback_state_t.

typedef struct {
    int player_index;          // Display player index (for LED patterns)
    uint8_t rumble;            // Rumble intensity (0-255, 0=off) - legacy, use max of left/right
    uint8_t rumble_left;       // Left motor intensity (0-255, heavy/low-frequency)
    uint8_t rumble_right;      // Right motor intensity (0-255, light/high-frequency)
    uint8_t leds;              // LED pattern/state
    uint8_t trigger_threshold; // Adaptive trigger threshold (0=disabled, 1-255=threshold)
    uint8_t test;              // Test pattern counter (0=disabled)
} device_output_config_t;

// ============================================================================
// DEVICE INTERFACE
// ============================================================================

typedef struct {
    const char* name;

    // Device identification
    bool (*is_device)(uint16_t vid, uint16_t pid);
    bool (*check_descriptor)(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len);

    // Input processing
    void (*process)(uint8_t dev_addr, uint8_t instance, const uint8_t *report, uint16_t len);

    // Output/feedback task (called periodically)
    // Legacy: receives device_output_config_t
    // New drivers should use feedback_get_state(player_index) internally
    void (*task)(uint8_t dev_addr, uint8_t instance, device_output_config_t* config);

    // Lifecycle
    bool (*init)(uint8_t dev_addr, uint8_t instance);
    void (*unmount)(uint8_t dev_addr, uint8_t instance);

    // Device capabilities (optional, NULL = unknown)
    uint16_t (*get_capabilities)(void);  // Returns FEEDBACK_CAP_* flags
} DeviceInterface;

#endif // DEVICE_INTERFACE_H
