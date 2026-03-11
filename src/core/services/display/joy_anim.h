// joy_anim.h - Joy Animated Character
// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Robert Dale Smith
//
// Animated controller-face character for OLED display.
// State-driven parametric animation with keyframe interpolation.

#ifndef JOY_ANIM_H
#define JOY_ANIM_H

#include <stdint.h>
#include <stdbool.h>

// Events that trigger state transitions
typedef enum {
    JOY_EVENT_BOOT,
    JOY_EVENT_CONNECT,
    JOY_EVENT_DISCONNECT,
    JOY_EVENT_BUTTON_PRESS,
    JOY_EVENT_MODE_SWITCH,
    JOY_EVENT_RUMBLE,
    JOY_EVENT_IDLE_TIMEOUT,
} joy_event_t;

// Animation states
typedef enum {
    JOY_STATE_BOOT,
    JOY_STATE_IDLE,
    JOY_STATE_SLEEP,
    JOY_STATE_HAPPY,
    JOY_STATE_SAD,
    JOY_STATE_ACTIVE,
    JOY_STATE_ALERT,
    JOY_STATE_COUNT,
} joy_state_t;

// Initialize Joy animation system
void joy_anim_init(void);

// Advance animation. Returns true if frame changed and needs redraw.
bool joy_anim_tick(uint32_t now_ms);

// Render Joy into the display framebuffer (call after display_clear)
void joy_anim_render(void);

// Set look direction from analog stick (0.0=left/up, 0.5=center, 1.0=right/down)
void joy_anim_set_look(float x, float y);

// Trigger a state transition event
void joy_anim_event(joy_event_t event);

// Get current animation state
joy_state_t joy_anim_get_state(void);

#endif // JOY_ANIM_H
