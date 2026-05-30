// sony_ds5.c
//
// Firmware glue for Sony DualSense controllers. The pure parser and feedback
// builder live in libjoypad (src/lib/libjoypad/src/devices/usb/hid/sony/ds5.c
// and joypad/devices/sony/ds5.h). This file does the firmware-side wiring:
// TinyUSB lifecycle, router submission, player slot LED, console-output
// safety (ensureAllNonZero), and touchpad-swipe state for spinner mapping.
//
// See .dev/docs/libjoypad.md for the boundary plan.

#include "sony_ds5.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "platform/platform.h"
#include "app_config.h"
#include <joypad/devices/sony/ds5.h>
#include <joypad/feedback.h>

// Touchpad-swipe state for spinner / camera mapping. Firmware-side because
// libjoypad's parser is stateless; downstream consumers that want raw
// touchpad coordinates can read event.touch[].
static uint16_t tpadLastPos;
static bool     tpadDragging;

// DualSense instance state (per-port feedback cache)
typedef struct TU_ATTR_PACKED {
    uint8_t rumble;
    uint8_t player;
    uint8_t led_r, led_g, led_b;
} ds5_instance_t;

typedef struct TU_ATTR_PACKED {
    ds5_instance_t instances[CFG_TUH_HID];
} ds5_device_t;

static ds5_device_t ds5_devices[MAX_DEVICES] = { 0 };

// VID/PID check — delegates to libjoypad.
bool is_sony_ds5(uint16_t vid, uint16_t pid) {
    return joypad_is_sony_ds5(vid, pid);
}

// USB HID input report → router submission.
//
// libjoypad does the wire-format decode (buttons, analog axes, motion,
// touchpad, battery). This wrapper layers on firmware-specific touches:
//   - dev_addr / instance assignment
//   - touchpad horizontal-swipe delta (for spinner mapping)
//   - keep raw HID values, no nonzero clamping at this layer
void input_sony_ds5(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    input_event_t event;
    if (!joypad_parse_sony_ds5(report, len, &event)) {
        return;
    }

    event.dev_addr = dev_addr;
    event.instance = instance;
    event.timestamp_us = (uint64_t)platform_time_ms() * 1000ULL;

    // Touchpad horizontal-swipe delta — mouse-like delta_x for spinner /
    // camera control. Only meaningful while a finger is down. libjoypad's
    // parser fills event.touch[]; we derive the swipe from successive
    // f1.x positions across calls.
    if (event.has_touch && event.touch[0].active) {
        uint16_t tx = event.touch[0].x;
        if (tpadDragging) {
            int16_t delta = (int16_t)tx - (int16_t)tpadLastPos;
            if (delta >  12) delta =  12;
            if (delta < -12) delta = -12;
            event.delta_x = (int8_t)delta;
        }
        tpadLastPos = tx;
        tpadDragging = true;
    } else {
        tpadDragging = false;
    }

    router_submit_input(&event);
}

// process usb hid output reports
void output_sony_ds5(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
    // Build feedback request from canonical sources: feedback_state_t (player
    // LED / RGB) + config-driven adaptive triggers + rumble flag.
    joypad_feedback_t fb;
    joypad_feedback_init(&fb);

    // --- Adaptive triggers from console-output threshold config ---
    if (config->trigger_threshold > 0) {
        int32_t perc_threshold = (config->trigger_threshold * 100) / 255;
        uint8_t start = (uint8_t)((perc_threshold * 0x94) / 100);
        uint8_t effect = (uint8_t)(start + ((0xb4 - start) * perc_threshold) / 100);

        fb.adaptive_left_dirty = true;
        fb.adaptive_left.mode = JOYPAD_TRIGGER_MODE_RESISTANCE;
        fb.adaptive_left.params[0] = start;
        fb.adaptive_left.params[1] = effect;

        fb.adaptive_right_dirty = true;
        fb.adaptive_right.mode = JOYPAD_TRIGGER_MODE_RESISTANCE;
        fb.adaptive_right.params[0] = start;
        fb.adaptive_right.params[1] = effect;
    }

    // --- Lightbar RGB from feedback system (player color), or app fallback ---
    uint8_t r_val, g_val, b_val;
    int8_t player_idx = find_player_index(dev_addr, instance);
    feedback_state_t* feedback = (player_idx >= 0) ? feedback_get_state(player_idx) : NULL;

    if (feedback && (feedback->led.r || feedback->led.g || feedback->led.b)) {
        r_val = feedback->led.r;
        g_val = feedback->led.g;
        b_val = feedback->led.b;
    } else {
        switch (config->player_index + 1) {
            case 1: r_val = LED_P1_R; g_val = LED_P1_G; b_val = LED_P1_B; break;
            case 2: r_val = LED_P2_R; g_val = LED_P2_G; b_val = LED_P2_B; break;
            case 3: r_val = LED_P3_R; g_val = LED_P3_G; b_val = LED_P3_B; break;
            case 4: r_val = LED_P4_R; g_val = LED_P4_G; b_val = LED_P4_B; break;
            case 5: r_val = LED_P5_R; g_val = LED_P5_G; b_val = LED_P5_B; break;
            case 6: r_val = LED_P6_R; g_val = LED_P6_G; b_val = LED_P6_B; break;
            case 7: r_val = LED_P7_R; g_val = LED_P7_G; b_val = LED_P7_B; break;
            default: r_val = LED_DEFAULT_R; g_val = LED_DEFAULT_G; b_val = LED_DEFAULT_B; break;
        }
    }

    fb.lightbar_dirty = true;
    fb.lightbar.r = r_val;
    fb.lightbar.g = g_val;
    fb.lightbar.b = b_val;

    // --- Player index for 5-LED bar ---
    fb.player_index_dirty = true;
    fb.player_index = (uint8_t)(config->player_index + 1);

    // --- Test pattern override ---
    if (config->player_index + 1 && config->test) {
        fb.lightbar.r = config->test;
        fb.lightbar.g = (uint8_t)(config->test + 64);
        fb.lightbar.b = (uint8_t)(config->test + 128);
    }

    // --- Rumble ---
    fb.rumble_dirty = true;
    fb.rumble_low  = config->rumble ? 192 : 0;
    fb.rumble_high = config->rumble ? 192 : 0;

    // Only send when something actually changed (or test mode forces it).
    if (ds5_devices[dev_addr].instances[instance].rumble != config->rumble ||
        ds5_devices[dev_addr].instances[instance].led_r  != fb.lightbar.r   ||
        ds5_devices[dev_addr].instances[instance].led_g  != fb.lightbar.g   ||
        ds5_devices[dev_addr].instances[instance].led_b  != fb.lightbar.b   ||
        config->test)
    {
        ds5_devices[dev_addr].instances[instance].rumble = config->rumble;
        ds5_devices[dev_addr].instances[instance].led_r  = fb.lightbar.r;
        ds5_devices[dev_addr].instances[instance].led_g  = fb.lightbar.g;
        ds5_devices[dev_addr].instances[instance].led_b  = fb.lightbar.b;

        uint8_t buf[JOYPAD_SONY_DS5_FEEDBACK_PAYLOAD_LEN];
        uint16_t n = joypad_build_sony_ds5_feedback(&fb, buf, sizeof(buf));
        if (n > 0) {
            tuh_hid_send_report(dev_addr, instance, JOYPAD_SONY_DS5_FEEDBACK_REPORT_ID, buf, n);
        }
    }
}

// process usb hid output reports
void task_sony_ds5(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
    const uint32_t interval_ms = 20;
    static uint32_t start_ms = 0;

    uint32_t current_time_ms = platform_time_ms();
    if (current_time_ms - start_ms >= interval_ms) {
        start_ms = current_time_ms;
        output_sony_ds5(dev_addr, instance, config);
    }
}

// resets default values in case devices are hotswapped
void unmount_sony_ds5(uint8_t dev_addr, uint8_t instance) {
    ds5_devices[dev_addr].instances[instance].rumble = 0;
    ds5_devices[dev_addr].instances[instance].player = 0xff;
}

DeviceInterface sony_ds5_interface = {
    .name = "Sony DualSense",
    .is_device = is_sony_ds5,
    .process = input_sony_ds5,
    .task = task_sony_ds5,
    .unmount = unmount_sony_ds5,
};
