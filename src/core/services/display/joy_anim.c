// joy_anim.c - Joy Animated Character
// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Robert Dale Smith
//
// Parametric face animation with keyframe interpolation.
// Joy is a controller-shaped character whose eyes and mouth
// react to firmware events (connect, disconnect, button press, etc).

#include "joy_anim.h"
#include "display.h"
#include "platform/platform.h"
#include <string.h>

// ============================================================================
// BODY BITMAP — Rasterized from logo_solid_black.svg (66x56, filled silhouette)
// ============================================================================
// Row-major, MSB=leftmost pixel, 9 bytes per row, 504 bytes total.
// Interior holes (d-pad, eyes, mouth) are filled — animation draws them on top.

#define JOY_BODY_W  66
#define JOY_BODY_H  56
#define JOY_BODY_X  31  // Centered on 128-wide display
#define JOY_BODY_Y  4   // Centered on 64-tall display

static const uint8_t joy_body[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // row  0: (empty)
    0x00, 0x00, 0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x00,  // row  1: hat top  (3px)
    0x00, 0x00, 0x00, 0x01, 0xF0, 0x00, 0x00, 0x00, 0x00,  // row  2:          (5px)
    0x00, 0x00, 0x00, 0x01, 0xF0, 0x00, 0x00, 0x00, 0x00,  // row  3:          (5px)
    0x00, 0x00, 0x00, 0x01, 0xF0, 0x00, 0x00, 0x00, 0x00,  // row  4:          (5px)
    0x00, 0x00, 0x00, 0x00, 0xE0, 0x00, 0x00, 0x00, 0x00,  // row  5: hat bot  (3px)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // row  6: (empty)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // row  7: (empty)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // row  8: (empty)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // row  9: gap
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // row 10
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // row 11
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // row 12
    0x00, 0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0x00, 0x00,  // row 13: body top
    0x00, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80, 0x00,  // row 14
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0x00,  // row 15
    0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00,  // row 16
    0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xE0, 0x00,  // row 17
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x00,  // row 18
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x00,  // row 19
    0x03, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x00,  // row 20
    0x07, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0x00,  // row 21
    0x07, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0x00,  // row 22
    0x07, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0x00,  // row 23
    0x07, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0x00,  // row 24
    0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8, 0x00,  // row 25
    0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,  // row 26
    0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,  // row 27
    0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,  // row 28
    0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC, 0x00,  // row 29
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0x00,  // row 30
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0x00,  // row 31
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0x00,  // row 32
    0x1F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0x00,  // row 33
    0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0x00,  // row 34
    0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,  // row 35
    0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,  // row 36
    0x3F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,  // row 37
    0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,  // row 38
    0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,  // row 39
    0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,  // row 40
    0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,  // row 41
    0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x80,  // row 42
    0xFF, 0xFF, 0xFC, 0x00, 0x00, 0x0F, 0xFF, 0xFF, 0xC0,  // row 43: grips
    0xFF, 0xFF, 0xF0, 0x00, 0x00, 0x03, 0xFF, 0xFF, 0xC0,  // row 44
    0xFF, 0xFF, 0xE0, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xC0,  // row 45
    0xFF, 0xFF, 0xC0, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xC0,  // row 46
    0xFF, 0xFF, 0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xC0,  // row 47
    0xFF, 0xFF, 0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xC0,  // row 48
    0xFF, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x7F, 0xFF, 0xC0,  // row 49
    0x7F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x7F, 0xFF, 0x80,  // row 50
    0x7F, 0xFF, 0x80, 0x00, 0x00, 0x00, 0x7F, 0xFF, 0x80,  // row 51
    0x3F, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFF, 0x00,  // row 52
    0x1F, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xFE, 0x00,  // row 53
    0x0F, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xFC, 0x00,  // row 54
    0x03, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x03, 0xF0, 0x00,  // row 55: grip bottoms
};

// ============================================================================
// FACE LAYOUT (pixel positions relative to display origin)
// ============================================================================
// Feature positions derived from SVG rasterization at 66x56:
//   D-pad:              bitmap-relative center (18, 26) → LEFT EYE
//   4-button diamond:   bitmap-relative center (47, 27) → RIGHT EYE
//   Mouth:              centered between them at (33, 36)

// Left eye center — the d-pad
#define EYE_L_CX  (JOY_BODY_X + 18)  // 49
#define EYE_L_CY  (JOY_BODY_Y + 26)  // 30

// Right eye center — the 4-button diamond cluster
#define EYE_R_CX  (JOY_BODY_X + 47)  // 78
#define EYE_R_CY  (JOY_BODY_Y + 27)  // 31

// D-pad cross dimensions (left eye pupil)
#define DPAD_ARM_LEN  7   // Half-length of each arm
#define DPAD_ARM_W    2   // Half-width of each arm (5px wide)

// Button diamond dimensions (right eye pupil)
#define BTN_SPACING   5   // Distance from center to each button
#define BTN_RADIUS    2   // Each button circle radius

// Eyelid — big round circle that covers the eye features when blinking
// eye_l_r / eye_r_r in keyframes: 0=closed (full eyelid), max=open (no eyelid)
#define EYE_OPEN_MAX  6   // Fully open — no eyelid visible
#define EYE_LID_R     9   // Eyelid circle radius when fully closed (covers d-pad + buttons)
#define EYE_DEFAULT_R EYE_OPEN_MAX

// Mouth — centered horizontally on the body, below both eyes
#define MOUTH_CX  (JOY_BODY_X + 33)  // 64 — body center
#define MOUTH_CY  (JOY_BODY_Y + 36)  // 40
#define MOUTH_DEFAULT_W  5  // half-width

// Look direction range (max eye offset in pixels)
#define LOOK_RANGE  3
#define LOOK_SMOOTH 0.2f

// ============================================================================
// KEYFRAME SYSTEM
// ============================================================================

typedef struct {
    uint16_t time_ms;
    int8_t eye_l_dx, eye_l_dy;
    int8_t eye_r_dx, eye_r_dy;
    uint8_t eye_l_r;
    uint8_t eye_r_r;
    int8_t mouth_curve;    // +smile, -frown
    uint8_t mouth_w;       // half-width
    int8_t body_dy;        // bounce offset
} joy_keyframe_t;

typedef struct {
    const joy_keyframe_t* frames;
    uint8_t count;
    uint16_t duration_ms;
    bool loop;
    joy_state_t next_state;  // For one-shot: transition to this when done
} joy_anim_def_t;

// Keyframe eye values: 0=closed (eyelid covers all), 6=fully open (EYE_OPEN_MAX)

// --- BOOT: eyes open up, slight bounce ---
static const joy_keyframe_t kf_boot[] = {
    {    0,  0, 0,  0, 0,  0, 0,   0, 4, -2 },  // Eyes closed, body up
    {  300,  0, 0,  0, 0,  3, 3,   0, 4,  1 },  // Eyes opening, bounce down
    {  600,  0, 0,  0, 0,  6, 6,   2, 5,  0 },  // Eyes wide open, slight smile
    { 1000,  0, 0,  0, 0,  6, 6,   1, 5,  0 },  // Settle to normal
};

// --- IDLE: blink cycle with resting smile ---
static const joy_keyframe_t kf_idle[] = {
    {    0,  0, 0,  0, 0,  6, 6,   1, 5,  0 },  // Open, slight smile
    { 3000,  0, 0,  0, 0,  6, 6,   1, 5,  0 },  // Hold open
    { 3050,  0, 0,  0, 0,  0, 0,   1, 5,  0 },  // Blink shut
    { 3150,  0, 0,  0, 0,  0, 0,   1, 5,  0 },  // Hold shut
    { 3200,  0, 0,  0, 0,  6, 6,   1, 5,  0 },  // Open
    { 4000,  0, 0,  0, 0,  6, 6,   1, 5,  0 },  // Pause before loop
};

// --- SLEEP: nearly closed slits, slow breathing ---
static const joy_keyframe_t kf_sleep[] = {
    {    0,  0, 1,  0, 1,  1, 1,   0, 4,  0 },  // Tiny slits, droopy
    { 1500,  0, 1,  0, 1,  1, 1,   0, 4,  1 },  // Breathe down
    { 3000,  0, 1,  0, 1,  1, 1,   0, 4,  0 },  // Breathe up
};

// --- HAPPY: eyes wide, smile, bounce ---
static const joy_keyframe_t kf_happy[] = {
    {    0,  0, 0,  0, 0,  6, 6,   0, 5,  0 },  // Start neutral
    {  150,  0,-1,  0,-1,  6, 6,   3, 6, -2 },  // Eyes wide, big smile, bounce up
    {  400,  0, 0,  0, 0,  6, 6,   3, 6,  1 },  // Settle
    {  800,  0, 0,  0, 0,  6, 6,   1, 5,  0 },  // Return to normal
};

// --- SAD: droopy, frown ---
static const joy_keyframe_t kf_sad[] = {
    {    0,  0, 0,  0, 0,  6, 6,   0, 5,  0 },  // Start neutral
    {  200,  0, 2,  0, 2,  3, 3,  -2, 4,  1 },  // Droop, half-close
    {  600,  0, 2,  0, 2,  2, 2,  -3, 4,  1 },  // Sad hold
    { 1200,  0, 1,  0, 1,  4, 4,  -2, 4,  0 },  // Slight recovery
    { 1800,  0, 0,  0, 0,  6, 6,   0, 5,  0 },  // Back to normal
};

// --- ACTIVE: slight smile (eyes driven by look direction) ---
static const joy_keyframe_t kf_active[] = {
    {    0,  0, 0,  0, 0,  6, 6,   1, 5,  0 },  // Open, slight smile
};

// --- ALERT: flash wide-eyed ---
static const joy_keyframe_t kf_alert[] = {
    {    0,  0, 0,  0, 0,  6, 6,   0, 5,  0 },  // Start
    {   80,  0,-1,  0,-1,  6, 6,   0, 6,  0 },  // Eyes wide
    {  250,  0,-1,  0,-1,  6, 6,   0, 6,  0 },  // Hold
    {  400,  0, 0,  0, 0,  6, 6,   0, 5,  0 },  // Return
};

// Animation definitions indexed by joy_state_t
static const joy_anim_def_t anims[JOY_STATE_COUNT] = {
    [JOY_STATE_BOOT]   = { kf_boot,   4, 1000, false, JOY_STATE_IDLE },
    [JOY_STATE_IDLE]   = { kf_idle,   6, 4000, true,  JOY_STATE_IDLE },
    [JOY_STATE_SLEEP]  = { kf_sleep,  3, 3000, true,  JOY_STATE_SLEEP },
    [JOY_STATE_HAPPY]  = { kf_happy,  4,  800, false, JOY_STATE_ACTIVE },
    [JOY_STATE_SAD]    = { kf_sad,    5, 1800, false, JOY_STATE_IDLE },
    [JOY_STATE_ACTIVE] = { kf_active, 1, 1000, true,  JOY_STATE_ACTIVE },
    [JOY_STATE_ALERT]  = { kf_alert,  4,  400, false, JOY_STATE_IDLE },
};

// ============================================================================
// STATE
// ============================================================================

static joy_state_t current_state;
static uint32_t state_start_ms;
static joy_state_t alert_return_state;  // State to return to after ALERT

// Interpolated face parameters
static struct {
    int8_t eye_l_x, eye_l_y;
    int8_t eye_r_x, eye_r_y;
    uint8_t eye_l_r, eye_r_r;
    int8_t mouth_curve;
    uint8_t mouth_w;
    int8_t body_dx;        // horizontal offset (hover rock)
    int8_t body_dy;        // vertical offset (hover bob + keyframe bounce)
} face, prev_face;

// Look direction (smoothed)
static float look_tx, look_ty;      // Target (0.0 - 1.0)
static float look_sx, look_sy;      // Smoothed current

// Idle timeout tracking
static uint32_t last_activity_ms;

// ============================================================================
// HELPERS
// ============================================================================

// Sine approximation using Bhaskara I's formula (smooth parabolic, no sharp corners).
// Input: 0-1023 maps to 0°-360°.  Output: -512..+512 (10-bit signed).
// Much smoother than triangle wave — the derivative is continuous at peaks.
static inline int16_t sin10(uint16_t angle) {
    angle &= 1023;
    bool neg = (angle >= 512);
    if (neg) angle -= 512;  // Map to 0-511 (half-period)
    // Parabolic arch: 4*x*(512-x) peaks at 262144 when x=256
    // Scale to 0..512
    int32_t x = (int32_t)angle;
    int32_t val = (4 * x * (512 - x)) / 512;
    if (val > 512) val = 512;
    return neg ? (int16_t)-val : (int16_t)val;
}

// Integer lerp: interpolate from a to b, t = 0..256
static inline int8_t lerp8(int8_t a, int8_t b, uint16_t t) {
    return (int8_t)(a + (((int16_t)(b - a) * t) >> 8));
}

static inline uint8_t lerpu8(uint8_t a, uint8_t b, uint16_t t) {
    return (uint8_t)(a + (((int16_t)(b - a) * (int16_t)t) >> 8));
}

static void set_state(joy_state_t state, uint32_t now_ms) {
    current_state = state;
    state_start_ms = now_ms;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void joy_anim_init(void) {
    memset(&face, 0, sizeof(face));
    face.eye_l_r = EYE_DEFAULT_R;
    face.eye_r_r = EYE_DEFAULT_R;
    face.mouth_w = MOUTH_DEFAULT_W;
    prev_face = face;

    look_tx = 0.5f;
    look_ty = 0.5f;
    look_sx = 0.5f;
    look_sy = 0.5f;

    alert_return_state = JOY_STATE_IDLE;

    uint32_t now = platform_time_ms();
    last_activity_ms = now;
    set_state(JOY_STATE_BOOT, now);
}

bool joy_anim_tick(uint32_t now_ms) {
    const joy_anim_def_t* anim = &anims[current_state];
    uint32_t elapsed = now_ms - state_start_ms;

    // Handle animation completion for one-shot anims
    if (!anim->loop && elapsed >= anim->duration_ms) {
        joy_state_t next = anim->next_state;
        if (current_state == JOY_STATE_ALERT) {
            next = alert_return_state;
        }
        set_state(next, now_ms);
        anim = &anims[current_state];
        elapsed = 0;
    }

    // Wrap looping animations
    uint16_t anim_time;
    if (anim->loop && anim->duration_ms > 0) {
        anim_time = (uint16_t)(elapsed % anim->duration_ms);
    } else {
        anim_time = (uint16_t)(elapsed < anim->duration_ms ? elapsed : anim->duration_ms);
    }

    // Find surrounding keyframes
    const joy_keyframe_t* kf0 = &anim->frames[0];
    const joy_keyframe_t* kf1 = &anim->frames[0];
    for (uint8_t i = 0; i < anim->count - 1; i++) {
        if (anim_time >= anim->frames[i].time_ms && anim_time < anim->frames[i + 1].time_ms) {
            kf0 = &anim->frames[i];
            kf1 = &anim->frames[i + 1];
            break;
        }
    }
    // If past last keyframe, hold last
    if (anim_time >= anim->frames[anim->count - 1].time_ms) {
        kf0 = kf1 = &anim->frames[anim->count - 1];
    }

    // Interpolation factor (0-256)
    uint16_t t = 0;
    if (kf0 != kf1) {
        uint16_t span = kf1->time_ms - kf0->time_ms;
        uint16_t pos = anim_time - kf0->time_ms;
        t = (uint16_t)((pos * 256) / span);
    }

    // Interpolate all parameters
    prev_face = face;
    face.eye_l_x = lerp8(kf0->eye_l_dx, kf1->eye_l_dx, t);
    face.eye_l_y = lerp8(kf0->eye_l_dy, kf1->eye_l_dy, t);
    face.eye_r_x = lerp8(kf0->eye_r_dx, kf1->eye_r_dx, t);
    face.eye_r_y = lerp8(kf0->eye_r_dy, kf1->eye_r_dy, t);
    face.eye_l_r = lerpu8(kf0->eye_l_r, kf1->eye_l_r, t);
    face.eye_r_r = lerpu8(kf0->eye_r_r, kf1->eye_r_r, t);
    face.mouth_curve = lerp8(kf0->mouth_curve, kf1->mouth_curve, t);
    face.mouth_w = lerpu8(kf0->mouth_w, kf1->mouth_w, t);
    face.body_dy = lerp8(kf0->body_dy, kf1->body_dy, t);

    // Smooth look direction
    look_sx += (look_tx - look_sx) * LOOK_SMOOTH;
    look_sy += (look_ty - look_sy) * LOOK_SMOOTH;

    // Apply look offset to eyes in ACTIVE state
    if (current_state == JOY_STATE_ACTIVE) {
        int8_t lx = (int8_t)((look_sx - 0.5f) * 2.0f * LOOK_RANGE);
        int8_t ly = (int8_t)((look_sy - 0.5f) * 2.0f * LOOK_RANGE);
        face.eye_l_x += lx;
        face.eye_l_y += ly;
        face.eye_r_x += lx;
        face.eye_r_y += ly;
    }

    // Continuous hover: slow gentle bob + gradual tilt/rock
    // Uses smooth parabolic sine with long periods for a dreamy weightless feel.
    //   Vertical bob:    ~7s period (1024 * 7ms), ±2px amplitude
    //   Horizontal rock: ~11s period (1024 * 11ms), ±1px amplitude
    {
        uint16_t phase_y = (uint16_t)((now_ms / 7) & 0x3FF);   // ~7.2s period
        uint16_t phase_x = (uint16_t)((now_ms / 11) & 0x3FF);  // ~11.3s period
        // sin10 returns -512..+512.  Scale to ±2px (bob) and ±1px (rock).
        // Use rounding: (val + 128) >> 8 for smooth ±2px transitions
        int16_t bob = sin10(phase_y);   // -512..+512
        int16_t rock = sin10(phase_x);  // -512..+512
        face.body_dy += (int8_t)((bob + (bob > 0 ? 128 : -128)) / 256);  // ±2px
        face.body_dx  = (int8_t)((rock + (rock > 0 ? 256 : -256)) / 512); // ±1px
    }

    // Check if anything changed
    return memcmp(&face, &prev_face, sizeof(face)) != 0;
}

// Draw left eye pupil: d-pad cross shape (black cutout on white body)
static void draw_dpad_eye(uint8_t cx, uint8_t cy) {
    // Vertical arm
    display_fill_rect(cx - DPAD_ARM_W, cy - DPAD_ARM_LEN,
                      DPAD_ARM_W * 2 + 1, DPAD_ARM_LEN * 2 + 1, false);
    // Horizontal arm
    display_fill_rect(cx - DPAD_ARM_LEN, cy - DPAD_ARM_W,
                      DPAD_ARM_LEN * 2 + 1, DPAD_ARM_W * 2 + 1, false);
}

// Draw right eye pupil: 4-button diamond (black cutout circles on white body)
static void draw_button_eye(uint8_t cx, uint8_t cy) {
    display_fill_circle(cx,              cy - BTN_SPACING, BTN_RADIUS, false);  // top
    display_fill_circle(cx,              cy + BTN_SPACING, BTN_RADIUS, false);  // bottom
    display_fill_circle(cx - BTN_SPACING, cy,              BTN_RADIUS, false);  // left
    display_fill_circle(cx + BTN_SPACING, cy,              BTN_RADIUS, false);  // right
}

void joy_anim_render(void) {
    int8_t dx = face.body_dx;
    int8_t dy = face.body_dy;

    // 0. Clear display so previous frame's body position doesn't ghost
    display_clear();

    // 1. Draw body bitmap (solid white silhouette from SVG)
    display_bitmap(JOY_BODY_X + dx, JOY_BODY_Y + dy, joy_body, JOY_BODY_W, JOY_BODY_H);

    // 2. Draw eye pupils (black cutouts on the white body)
    uint8_t lcx = EYE_L_CX + face.eye_l_x + dx;
    uint8_t lcy = EYE_L_CY + face.eye_l_y + dy;
    uint8_t rcx = EYE_R_CX + face.eye_r_x + dx;
    uint8_t rcy = EYE_R_CY + face.eye_r_y + dy;

    draw_dpad_eye(lcx, lcy);
    draw_button_eye(rcx, rcy);

    // 3. Draw eyelids (white filled circles that cover the pupils)
    //    eye_*_r: 0 = fully closed, EYE_OPEN_MAX = fully open
    //    When nearly closed (1-2), draw a small horizontal slit for "sleepy eyes"
    if (face.eye_l_r < EYE_OPEN_MAX) {
        uint8_t lid_r = (uint8_t)((EYE_LID_R * (EYE_OPEN_MAX - face.eye_l_r)) / EYE_OPEN_MAX);
        if (lid_r > 0) {
            display_fill_circle(lcx, lcy, lid_r, true);
            // Draw a slit when nearly closed (sleepy look)
            if (face.eye_l_r > 0 && face.eye_l_r <= 2) {
                uint8_t slit_w = (face.eye_l_r == 1) ? 3 : 5;
                display_hline(lcx - slit_w, lcy, slit_w * 2 + 1);
                // hline draws ON pixels — but we need black slit on white eyelid
                for (uint8_t i = 0; i <= slit_w * 2; i++) {
                    display_pixel(lcx - slit_w + i, lcy, false);
                }
            }
        }
    }
    if (face.eye_r_r < EYE_OPEN_MAX) {
        uint8_t lid_r = (uint8_t)((EYE_LID_R * (EYE_OPEN_MAX - face.eye_r_r)) / EYE_OPEN_MAX);
        if (lid_r > 0) {
            display_fill_circle(rcx, rcy, lid_r, true);
            if (face.eye_r_r > 0 && face.eye_r_r <= 2) {
                uint8_t slit_w = (face.eye_r_r == 1) ? 3 : 5;
                for (uint8_t i = 0; i <= slit_w * 2; i++) {
                    display_pixel(rcx - slit_w + i, rcy, false);
                }
            }
        }
    }

    // 4. Draw mouth (black parabolic curve cut out of the white body)
    if (face.mouth_w > 0) {
        int8_t w = (int8_t)face.mouth_w;
        int8_t curve = face.mouth_curve;
        int16_t w2 = (int16_t)w * w;
        for (int8_t mx = -w; mx <= w; mx++) {
            int8_t dy_mouth = 0;
            if (curve != 0 && w2 > 0) {
                dy_mouth = (int8_t)((curve * (w2 - (int16_t)mx * mx)) / w2);
            }
            display_pixel(MOUTH_CX + dx + mx, MOUTH_CY + dy + dy_mouth, false);
            display_pixel(MOUTH_CX + dx + mx, MOUTH_CY + dy + dy_mouth + 1, false);
        }
    }
}

void joy_anim_set_look(float x, float y) {
    look_tx = x;
    look_ty = y;
    last_activity_ms = platform_time_ms();
}

void joy_anim_event(joy_event_t event) {
    uint32_t now = platform_time_ms();
    last_activity_ms = now;

    switch (event) {
        case JOY_EVENT_BOOT:
            set_state(JOY_STATE_BOOT, now);
            break;

        case JOY_EVENT_CONNECT:
            set_state(JOY_STATE_HAPPY, now);
            break;

        case JOY_EVENT_DISCONNECT:
            set_state(JOY_STATE_SAD, now);
            break;

        case JOY_EVENT_BUTTON_PRESS:
            // Happy bounce on button press
            set_state(JOY_STATE_HAPPY, now);
            break;

        case JOY_EVENT_MODE_SWITCH:
            alert_return_state = current_state;
            if (alert_return_state == JOY_STATE_ALERT) {
                alert_return_state = JOY_STATE_IDLE;
            }
            set_state(JOY_STATE_ALERT, now);
            break;

        case JOY_EVENT_RUMBLE:
            // Could add a shake overlay in the future
            break;

        case JOY_EVENT_IDLE_TIMEOUT:
            // Single-step demotion. App fires this at staged timeouts:
            //   ACTIVE → IDLE   (first wake-down)
            //   IDLE   → SLEEP  (second wake-down)
            if (current_state == JOY_STATE_ACTIVE) {
                set_state(JOY_STATE_IDLE, now);
            } else if (current_state == JOY_STATE_IDLE) {
                set_state(JOY_STATE_SLEEP, now);
            }
            break;
    }
}

joy_state_t joy_anim_get_state(void) {
    return current_state;
}
