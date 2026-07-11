// eyes_anim.c - Animated Cartoon Eyes
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Robert Dale Smith
//
// Two filled rounded-rectangle eyes rendered to a 128x64 mono display.
// A pseudo-3D cylinder model gives the eyes parallax depth as gaze
// shifts left/right — eye width contracts on the side rotating away,
// expands on the side rotating toward the viewer.
//
// Emotions are expressed by the eye shape itself: per-eye height (wink),
// width modulation (squint/suspicious), bottom arc (smile), inner-edge
// wedge cut (angry/sad brow), and slight vertical bias (surprise lift,
// sleepy droop).

#include "eyes_anim.h"
#include "display.h"
#include "platform/platform.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// GEOMETRY
// ============================================================================

// EYES_SCALE renders the whole face at N× the base 128x64 canvas so a
// high-resolution panel (e.g. the AMOLED) draws smooth eyes instead of a
// nearest-neighbor upscale. Every pixel-space constant below is multiplied
// by it; angles/ratios are not. Defaults to 1 (unchanged 128x64 OLEDs).
#ifndef EYES_SCALE
#define EYES_SCALE          1
#endif

#define SCREEN_W            (DISPLAY_WIDTH  * EYES_SCALE)   // 128 * S
#define SCREEN_H            (DISPLAY_HEIGHT * EYES_SCALE)   // 64 * S

#define EYE_BASE_W          (36 * EYES_SCALE)
#define EYE_BASE_H          (48 * EYES_SCALE)
#define EYE_CORNER_R        (7  * EYES_SCALE)

// Centers 38px apart (was 44). With eye width 36 the inner edges nearly
// touch (~2px gap) — gives the cute "wide face" character look instead
// of two distant eyes.
#define EYE_L_HOME_X        (45 * EYES_SCALE)
#define EYE_R_HOME_X        (83 * EYES_SCALE)
#define EYE_HOME_Y          (32 * EYES_SCALE)

// Cylinder model (see header comment). Radius scaled with the new eye
// size so parallax sweep matches the eye dimension.
#define CYL_RADIUS          (36.0f * EYES_SCALE)
#define CYL_EYE_THETA       0.55f
#define CYL_ROT_RANGE       0.55f
#define LOOK_RANGE_Y        (6.0f * EYES_SCALE)

#define LOOK_SMOOTH         0.18f

// ============================================================================
// KEYFRAME / STATE
// ============================================================================

typedef struct {
    uint16_t time_ms;
    int8_t   dy;          // shared vertical bias (sleepy droop, surprise rise)
    uint8_t  height_l;    // 0..120 percent of EYE_BASE_H per eye
    uint8_t  height_r;
    uint8_t  width_pct;   // 50..100 width scale (squint, suspicious narrowing)
    int8_t   curve_l;     // 0..100 per-eye downward bottom arc (smile / curve-blink)
    int8_t   curve_r;
    int8_t   brow;        // -8..+8 inner brow tilt (- sad, + angry)
} eyes_keyframe_t;

typedef struct {
    const eyes_keyframe_t* frames;
    uint8_t count;
    uint16_t duration_ms;
    uint16_t hold_ms;       // While "held" mode is active, anim_time is
                            // clamped here. Ignored if 0 (non-holdable
                            // states like BOOT/IDLE/SLEEP/ACTIVE).
    bool loop;
    eyes_state_t next_state;
} eyes_anim_def_t;

// --- BOOT: open from closed ---
static const eyes_keyframe_t kf_boot[] = {
    {    0,  0,   0,   0, 100,   0,   0,  0 },
    {  250,  0,  60,  60, 100,   0,   0,  0 },
    {  500,  0, 100, 100, 100,   0,   0,  0 },
    {  900,  0, 100, 100, 100,   0,   0,  0 },
};

// --- IDLE: occasional double-blink. Closed shape uses a deep smile-arc
// cut from the bottom rather than collapsing to a flat line — gives the
// ⌒ "Starboy-style" curved blink that retains character even when shut.
static const eyes_keyframe_t kf_idle[] = {
    {    0, 0, 100, 100, 100,   0,   0,  0 },
    { 3200, 0, 100, 100, 100,   0,   0,  0 },
    { 3260, 0,  55,  55, 100, 100, 100,  0 },
    { 3340, 0,  55,  55, 100, 100, 100,  0 },
    { 3400, 0, 100, 100, 100,   0,   0,  0 },
    { 5000, 0, 100, 100, 100,   0,   0,  0 },
    { 5070, 0,  55,  55, 100, 100, 100,  0 },
    { 5160, 0,  55,  55, 100, 100, 100,  0 },
    { 5230, 0, 100, 100, 100,   0,   0,  0 },
    { 6000, 0, 100, 100, 100,   0,   0,  0 },
};

// --- SLEEP: half-lidded slow breathing ---
static const eyes_keyframe_t kf_sleep[] = {
    {    0, 2,  18,  18, 100,   0,   0,  0 },
    { 1800, 2,  10,  10, 100,   0,   0,  0 },
    { 3600, 2,  18,  18, 100,   0,   0,  0 },
};

// --- HAPPY: squinted joyful arc ---
static const eyes_keyframe_t kf_happy[] = {
    {    0,  0, 100, 100, 100,   0,   0,  0 },
    {  120, -2,  70,  70, 100,  60,  60,  0 },
    {  300, -2,  60,  60, 100,  80,  80,  0 },
    {  600,  0,  90,  90, 100,  30,  30,  0 },
    {  900,  0, 100, 100, 100,   0,   0,  0 },
};

// --- SAD: droopy + inner brows down ---
static const eyes_keyframe_t kf_sad[] = {
    {    0,  0, 100, 100, 100,   0,   0,  0 },
    {  250,  2,  80,  80, 100,   0,   0, -3 },
    {  900,  2,  72,  72, 100,   0,   0, -4 },
    { 1500,  1,  90,  90, 100,   0,   0, -2 },
    { 2000,  0, 100, 100, 100,   0,   0,  0 },
};

// --- ACTIVE ---
static const eyes_keyframe_t kf_active[] = {
    { 0, 0, 100, 100, 100, 0, 0, 0 },
};

// --- SURPRISED: pop wide for a beat ---
static const eyes_keyframe_t kf_surprised[] = {
    {    0,  0, 100, 100, 100, 0, 0, 0 },
    {   60, -2, 118, 118,  90, 0, 0, 0 },
    {  280, -2, 118, 118,  90, 0, 0, 0 },
    {  450,  0, 100, 100, 100, 0, 0, 0 },
};

// --- ANGRY: triangular eye shape ---
static const eyes_keyframe_t kf_angry[] = {
    {    0,  0, 100, 100, 100, 0, 0,   0 },
    {   60,  1,  90,  90,  95, 0, 0,  60 },
    {  140,  1,  85,  85,  92, 0, 0,  90 },
    {  900,  1,  85,  85,  92, 0, 0,  90 },
    { 1100,  0,  92,  92,  98, 0, 0,  40 },
    { 1500,  0, 100, 100, 100, 0, 0,   0 },
};

// --- SUSPICIOUS: narrowed slits ---
static const eyes_keyframe_t kf_suspicious[] = {
    {    0,  0, 100, 100, 100, 0, 0, 0 },
    {  200,  1,  35,  35,  95, 0, 0, 0 },
    { 1100,  1,  35,  35,  95, 0, 0, 0 },
    { 1500,  0, 100, 100, 100, 0, 0, 0 },
};

// --- WINK_L: left curve-closes (⌒), right stays open ---
static const eyes_keyframe_t kf_wink_l[] = {
    {    0,  0, 100, 100, 100,   0, 0, 0 },
    {   80, -1,  55, 100, 100, 100, 0, 0 },
    {  450, -1,  55, 100, 100, 100, 0, 0 },
    {  600,  0, 100, 100, 100,   0, 0, 0 },
};

// --- WINK_R: right curve-closes (⌒), left stays open ---
static const eyes_keyframe_t kf_wink_r[] = {
    {    0,  0, 100, 100, 100, 0,   0, 0 },
    {   80, -1, 100,  55, 100, 0, 100, 0 },
    {  450, -1, 100,  55, 100, 0, 100, 0 },
    {  600,  0, 100, 100, 100, 0,   0, 0 },
};

// --- EXCITED: bouncy double-pop ---
static const eyes_keyframe_t kf_excited[] = {
    {    0,  0, 100, 100, 100,  0,  0, 0 },
    {   80, -2, 115, 115, 100, 20, 20, 0 },
    {  220,  0,  90,  90, 100, 30, 30, 0 },
    {  340, -2, 115, 115, 100, 20, 20, 0 },
    {  500,  0, 105, 105, 100, 40, 40, 0 },
    {  900,  0, 100, 100, 100,  0,  0, 0 },
};

// --- CONFUSED: asymmetric ---
static const eyes_keyframe_t kf_confused[] = {
    {    0,  0, 100, 100, 100, 0, 0,  0 },
    {  200,  0,  70, 105, 100, 0, 0,  2 },
    { 1000, -1,  70, 105, 100, 0, 0,  2 },
    { 1400,  0, 100, 100, 100, 0, 0,  0 },
};

static const eyes_anim_def_t anims[EYES_STATE_COUNT] = {
    // hold_ms = the keyframe time at which "peak" is reached; while held,
    // anim_time stays pinned here. 0 = not holdable.
    [EYES_STATE_BOOT]       = { kf_boot,       4,  900,    0, false, EYES_STATE_IDLE   },
    [EYES_STATE_IDLE]       = { kf_idle,      10, 6000,    0, true,  EYES_STATE_IDLE   },
    [EYES_STATE_SLEEP]      = { kf_sleep,      3, 3600,    0, true,  EYES_STATE_SLEEP  },
    [EYES_STATE_HAPPY]      = { kf_happy,      5,  900,  300, false, EYES_STATE_ACTIVE },
    [EYES_STATE_SAD]        = { kf_sad,        5, 2000,  900, false, EYES_STATE_IDLE   },
    [EYES_STATE_ACTIVE]     = { kf_active,     1, 1000,    0, true,  EYES_STATE_ACTIVE },
    [EYES_STATE_SURPRISED]  = { kf_surprised,  4,  450,   60, false, EYES_STATE_ACTIVE },
    [EYES_STATE_ANGRY]      = { kf_angry,      6, 1500,  140, false, EYES_STATE_ACTIVE },
    [EYES_STATE_SUSPICIOUS] = { kf_suspicious, 4, 1500,  200, false, EYES_STATE_ACTIVE },
    [EYES_STATE_WINK_L]     = { kf_wink_l,     4,  600,   80, false, EYES_STATE_ACTIVE },
    [EYES_STATE_WINK_R]     = { kf_wink_r,     4,  600,   80, false, EYES_STATE_ACTIVE },
    [EYES_STATE_EXCITED]    = { kf_excited,    6,  900,  500, false, EYES_STATE_ACTIVE },
    [EYES_STATE_CONFUSED]   = { kf_confused,   4, 1400,  200, false, EYES_STATE_ACTIVE },
};

// "Fun" pool the random button reaction picks from. Order is intentional:
// the first few are weighted higher so happy/excited dominate, with the
// less-flattering reactions sprinkled in for personality.
static const eyes_state_t reaction_pool[] = {
    EYES_STATE_HAPPY, EYES_STATE_HAPPY, EYES_STATE_HAPPY,
    EYES_STATE_EXCITED, EYES_STATE_EXCITED,
    EYES_STATE_SURPRISED,
    EYES_STATE_WINK_L, EYES_STATE_WINK_R,
    EYES_STATE_ANGRY,
    EYES_STATE_SUSPICIOUS,
    EYES_STATE_CONFUSED,
};
#define REACTION_POOL_COUNT (sizeof(reaction_pool) / sizeof(reaction_pool[0]))

static eyes_state_t current_state;
static uint32_t state_start_ms;

static struct {
    int8_t  dy;
    uint8_t height_l, height_r;
    uint8_t width_pct;
    int8_t  curve_l, curve_r;
    int8_t  brow;
} cur, prev;

static float look_tx = 0.5f, look_ty = 0.5f;
static float look_sx = 0.5f, look_sy = 0.5f;

// External eyelid drive (e.g. L2/R2 triggers). 0 = open, 1 = fully closed.
// Applied as a multiplier on top of the keyframe height per eye.
static float eyelid_l = 0.0f, eyelid_r = 0.0f;

// When true and the current state has a non-zero hold_ms, anim_time is
// clamped to hold_ms — pinning the emotion at its peak frame for as long
// as the app keeps it set. Cleared on state change so a new emotion
// always plays from the start.
static bool emotion_held = false;

// Per-state pupil dilation/constriction (% of base size). Snaps on state
// change but smoothed via pupil_scale_current so the transition isn't a
// hard pop. Values picked to read at a glance: surprised/excited eyes
// dilate, angry/suspicious eyes constrict.
static const uint8_t pupil_scale_for_state[EYES_STATE_COUNT] = {
    [EYES_STATE_BOOT]       = 100,
    [EYES_STATE_IDLE]       = 100,
    [EYES_STATE_SLEEP]      = 100,
    [EYES_STATE_HAPPY]      = 110,
    [EYES_STATE_SAD]        =  90,
    [EYES_STATE_ACTIVE]     = 100,
    [EYES_STATE_SURPRISED]  = 140,
    [EYES_STATE_ANGRY]      =  55,
    [EYES_STATE_SUSPICIOUS] =  70,
    [EYES_STATE_WINK_L]     = 100,
    [EYES_STATE_WINK_R]     = 100,
    [EYES_STATE_EXCITED]    = 130,
    [EYES_STATE_CONFUSED]   = 100,
};
static float pupil_scale_target  = 1.0f;
static float pupil_scale_current = 1.0f;

// Ambient pupil micro-jitter — small random saccade target every few
// hundred ms, smoothed in. Keeps the pupils alive even when nothing
// external is driving them. Independent of the gaze track.
static float pupil_jitter_x = 0.0f, pupil_jitter_y = 0.0f;
static float pupil_jitter_tx = 0.0f, pupil_jitter_ty = 0.0f;
static uint32_t next_pupil_jitter_ms = 0;
#define PUPIL_JITTER_RANGE_PX   (2.0f * EYES_SCALE)
#define PUPIL_JITTER_MIN_MS     250
#define PUPIL_JITTER_MAX_MS     1100
#define PUPIL_JITTER_SMOOTH     0.16f

static uint32_t last_activity_ms;

// Idle-wander state. When nothing external is steering the gaze AND we're
// in IDLE, eyes drift to a new gentle target every few seconds (saccades).
#define IDLE_WAKE_TIMEOUT_MS  1500
#define WANDER_MIN_MS         2000
#define WANDER_MAX_MS         4500
#define WANDER_RANGE          0.30f
static uint32_t last_external_look_ms;
static uint32_t next_wander_ms;
static uint32_t rng_state = 0xCAFEBABEu;
static inline uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

// ============================================================================
// HELPERS
// ============================================================================

static inline int8_t lerp8(int8_t a, int8_t b, uint16_t t256) {
    return (int8_t)(a + (((int16_t)(b - a) * (int16_t)t256) >> 8));
}

static inline uint8_t lerpu8(uint8_t a, uint8_t b, uint16_t t256) {
    int16_t diff = (int16_t)b - (int16_t)a;
    return (uint8_t)((int16_t)a + ((diff * (int16_t)t256) >> 8));
}

static void set_state(eyes_state_t s, uint32_t now_ms) {
    current_state = s;
    state_start_ms = now_ms;
    emotion_held = false;  // New state always plays from the start.
    pupil_scale_target = (float)pupil_scale_for_state[s] / 100.0f;
}

// Filled rounded rectangle with optional shear. Computed scanline-by-
// scanline so we can shift each row in x for the shear effect (the old
// rect+circle composition couldn't be tilted).
//
// shear>0 shifts top rows in +x and bottom rows in -x.
static void fill_rrect(int x, int y, int w, int h, int r, float shear, bool on) {
    if (w <= 0 || h <= 0) return;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r < 0) r = 0;
    int cy = y + h / 2;

    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= SCREEN_H) continue;

        // Untilted x range for this row (in 0..w-1 space).
        int xl, xr;
        if (r == 0) {
            xl = 0; xr = w - 1;
        } else if (row >= r && row < h - r) {
            xl = 0; xr = w - 1;
        } else {
            int dy;
            if (row < r) dy = r - 1 - row;        // distance from top arc center
            else         dy = row - (h - r);      // distance from bottom arc center
            int dx2 = r * r - dy * dy;
            if (dx2 < 0) continue;
            int dx = 0;
            // Integer sqrt — small range so a quick increment loop is fine.
            while ((dx + 1) * (dx + 1) <= dx2) dx++;
            xl = r - dx;
            xr = w - 1 - r + dx;
        }

        int y_from_center = py - cy;
        int x_shift = (int)(-(float)y_from_center * shear
                            + (y_from_center < 0 ? -0.5f : 0.5f));
        int px_l = x + xl + x_shift;
        int px_r = x + xr + x_shift;
        if (px_l < 0) px_l = 0;
        if (px_r > SCREEN_W - 1) px_r = SCREEN_W - 1;
        for (int px = px_l; px <= px_r; px++) {
            display_pixel((int16_t)px, (int16_t)py, on);
        }
    }
}

// Filled ellipse — true oval rather than the previous rounded-rect.
// Uses the standard (x/rx)^2 + (y/ry)^2 <= 1 test, scaled by ry^2 to
// stay in integer math.
//
// `shear` (radians-ish; really tan-of-tilt) tilts the ellipse: positive
// shifts top rows in +x and bottom rows in -x. Used to give the eyes a
// slight outward-leaning tilt so they look more characterful.
static void fill_ellipse(int cx, int cy, int rx, int ry, float shear, bool on) {
    if (rx <= 0 || ry <= 0) return;
    int rx2 = rx * rx;
    int ry2 = ry * ry;
    if (ry2 == 0) return;
    for (int y = -ry; y <= ry; y++) {
        int rhs = rx2 * (ry2 - y * y);
        if (rhs < 0) continue;
        int x_shift = (int)(-(float)y * shear + (y < 0 ? -0.5f : 0.5f));
        for (int x = -rx; x <= rx; x++) {
            if (x * x * ry2 <= rhs) {
                int px = cx + x + x_shift, py = cy + y;
                if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                    display_pixel((int16_t)px, (int16_t)py, on);
                }
            }
        }
    }
}

// Smile arc cut — bottom-up parabolic notch. shear/eye_cy needed so the
// cut follows the tilted ellipse: each row's x is shifted to match the
// ellipse's center at that y (which has been displaced by the shear).
static void cut_smile_arc(int cx, int cy_bottom, int half_w, int depth,
                          float shear, int eye_cy) {
    if (depth <= 0 || half_w <= 0) return;
    int w2 = half_w * half_w;
    for (int x = -half_w; x <= half_w; x++) {
        int rise = (depth * (w2 - x * x)) / w2;
        for (int y = 0; y <= rise; y++) {
            int py = cy_bottom - y;
            int y_from_center = py - eye_cy;
            int x_shift = (int)(-(float)y_from_center * shear
                                + (y_from_center < 0 ? -0.5f : 0.5f));
            int px = cx + x + x_shift;
            if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                display_pixel((int16_t)px, (int16_t)py, false);
            }
        }
    }
}

// Brow shape — dual mode based on tilt sign:
//
// tilt > 0  (ANGRY): triangular cut from the TOP. Depth is 0 at the outer
//   edge and grows to (eye_h-2)*(tilt/100) at the inner edge. Result is a
//   triangle: flat bottom, vertical outer edge, hypotenuse sloping from
//   outer-top down to the inner side. tilt=100 = inner edge cut nearly to
//   the floor (just a 2px sliver remains).
//
// tilt < 0  (SAD): small wedge cut from the inner-top corner — lowers the
//   inner brow corner. Magnitude in pixels (range -8..-2 in keyframes).
// Brow cut — same shear-aware shifting as the smile cut.
// eye_cy = ellipse vertical center (where shear contributes 0 shift).
static void cut_brow(int eye_x, int eye_y, int eye_w, int eye_h, int tilt,
                     bool inner_left, float shear, int eye_cy) {
    if (tilt == 0 || eye_w <= 1 || eye_h <= 2) return;

    if (tilt > 0) {
        int t = tilt > 100 ? 100 : tilt;
        int max_depth = ((eye_h - 2) * t) / 100;
        if (max_depth < 1) return;
        for (int x = 0; x < eye_w; x++) {
            int dist_from_outer = inner_left ? (eye_w - 1 - x) : x;
            int depth = (dist_from_outer * max_depth) / (eye_w - 1);
            for (int y = 0; y < depth; y++) {
                int py = eye_y + y;
                int y_from_center = py - eye_cy;
                int x_shift = (int)(-(float)y_from_center * shear
                                    + (y_from_center < 0 ? -0.5f : 0.5f));
                int px = eye_x + x + x_shift;
                if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                    display_pixel((int16_t)px, (int16_t)py, false);
                }
            }
        }
        return;
    }

    // SAD wedge.
    int abs_tilt = -tilt;
    if (abs_tilt > eye_h / 2) abs_tilt = eye_h / 2;
    int wedge_w = abs_tilt + 4;
    if (wedge_w > eye_w) wedge_w = eye_w;
    for (int row = 0; row < abs_tilt; row++) {
        int len = wedge_w - (wedge_w * row / abs_tilt);
        if (len <= 0) continue;
        int y = eye_y + row;
        int y_from_center = y - eye_cy;
        int x_shift = (int)(-(float)y_from_center * shear
                            + (y_from_center < 0 ? -0.5f : 0.5f));
        int x_start = inner_left ? eye_x : (eye_x + eye_w - len);
        for (int dx = 0; dx < len; dx++) {
            int px = x_start + dx + x_shift;
            if (px >= 0 && px < SCREEN_W && y >= 0 && y < SCREEN_H) {
                display_pixel((int16_t)px, (int16_t)y, false);
            }
        }
    }
}

// Render a single eye with the current shape modifiers applied.
// Eye base is a filled ellipse, optionally tilted via `shear` (positive
// = top shifts +x, bottom shifts -x). Brow and smile cuts and the pupil
// all get the same shear so they stay aligned with the tilted ellipse.
//
// pupil_dx/dy = pupil offset from eye center (in pixels), driven by gaze.
static void render_eye(int x_left, int y_top, int w, int h,
                       int curve, int brow, bool inner_left,
                       int pupil_dx, int pupil_dy, float shear) {
    if (w <= 0 || h <= 0) return;

    int cx = x_left + w / 2;
    int cy = y_top + h / 2;
    int rx = w / 2;
    int ry = h / 2;
    fill_ellipse(cx, cy, rx, ry, shear, true);

    // Cuts must use the ellipse's actual painted extent (cy±ry, which
    // covers 2*ry+1 rows = h+1 rows), not the bbox h. Otherwise the
    // smile/brow cut leaves a stranded white pixel at the very bottom
    // or top center on even-h eyes.
    int eye_top = cy - ry;
    int eye_bottom = cy + ry;
    int eye_full_h = eye_bottom - eye_top + 1;

    if (curve > 0 && h > 6) {
        int depth = (eye_full_h * curve) / 100;
        if (depth > eye_full_h / 2) depth = eye_full_h / 2;
        cut_smile_arc(cx, eye_bottom, rx - 1, depth, shear, cy);
    }
    if (brow != 0 && h > 6) {
        cut_brow(x_left, eye_top, w, eye_full_h, brow, inner_left, shear, cy);
    }

    // Pupil — round dot inside the oval eye. Three things modulate size:
    //   1. base size (smaller now: min_dim / 5, capped at 5)
    //   2. eye openness — smooth fade as the eyelid closes (no hard cutoff)
    //   3. per-state dilation (caller-provided pupil_scale, e.g. 1.4 surprised)
    // Skipped only when the result rounds to <1px so a closed eye really
    // is empty.
    if (h >= 4 && w >= 6) {
        // Pupil base size is anchored to the FULL-OPEN eye, not the
        // current squinted height. Real (and cartoon) eyes don't resize
        // the pupil during a blink — the lid covers it. The eye is
        // drawn ON, then the pupil drawn OFF on top, so anything outside
        // the visible eye region is automatically clipped (drawing OFF
        // on already-OFF is a no-op).
        int base_dim = (EYE_BASE_W < EYE_BASE_H) ? EYE_BASE_W : EYE_BASE_H;
        int base_r = base_dim / 3;
        if (base_r < 2 * EYES_SCALE) base_r = 2 * EYES_SCALE;
        if (base_r > 10 * EYES_SCALE) base_r = 10 * EYES_SCALE;   // slightly smaller "boba"

        // Curve fade: pupil shrinks as the smile-arc cut grows so it
        // doesn't get clipped into a half-moon at the cut boundary.
        // Fully hidden by curve >= 50 (HAPPY peak / blink / wink), full
        // size below curve 20 (resting / mild smile).
        float curve_fade = 1.0f;
        if (curve > 20) {
            curve_fade = (50.0f - (float)curve) / 30.0f;
            if (curve_fade < 0.0f) curve_fade = 0.0f;
        }

        int pupil_r = (int)((float)base_r * pupil_scale_current * curve_fade + 0.5f);
        if (pupil_r >= 1) {
            int margin = EYES_SCALE;
            int max_dx = rx - pupil_r - margin;
            int max_dy = ry - pupil_r - margin;
            if (max_dx < 0) max_dx = 0;
            if (max_dy < 0) max_dy = 0;
            // Bias the pupils toward the face center (slightly cross-eyed
            // resting pose, like the Starboy idle reference). Inner edge
            // for the left eye on screen is its RIGHT side → push +x;
            // for the right eye, push -x.
            #define PUPIL_INWARD_BIAS_PX (3 * EYES_SCALE)
            int inward = inner_left ? -PUPIL_INWARD_BIAS_PX : +PUPIL_INWARD_BIAS_PX;
            int pdx = pupil_dx + inward, pdy = pupil_dy;
            if (pdx >  max_dx) pdx =  max_dx;
            if (pdx < -max_dx) pdx = -max_dx;
            if (pdy >  max_dy) pdy =  max_dy;
            if (pdy < -max_dy) pdy = -max_dy;
            // Shear the pupil's center into the same tilted frame as the
            // eye so it stays positioned correctly relative to the oval.
            int pupil_x_shift = (int)(-(float)pdy * shear
                                      + (pdy < 0 ? -0.5f : 0.5f));
            fill_ellipse(cx + pdx + pupil_x_shift, cy + pdy,
                         pupil_r, pupil_r, 0.0f, false);
        }
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void eyes_anim_init(void) {
    memset(&cur, 0, sizeof(cur));
    cur.height_l = cur.height_r = 100;
    cur.width_pct = 100;
    prev = cur;
    look_tx = look_ty = 0.5f;
    look_sx = look_sy = 0.5f;
    uint32_t now = platform_time_ms();
    last_activity_ms = now;
    last_external_look_ms = now;
    next_wander_ms = now + WANDER_MIN_MS;
    rng_state ^= (now ? now : 1u) * 2654435761u;
    set_state(EYES_STATE_BOOT, now);
}

bool eyes_anim_tick(uint32_t now_ms) {
    const eyes_anim_def_t* anim = &anims[current_state];
    uint32_t elapsed = now_ms - state_start_ms;

    // Hold mode: pin anim_time at hold_ms by sliding state_start_ms forward.
    // This keeps the emotion frozen at its peak frame for as long as the
    // app keeps the button held; release lets time advance naturally from
    // hold_ms onward into the release frames.
    if (emotion_held && anim->hold_ms > 0 && elapsed > anim->hold_ms) {
        state_start_ms = now_ms - anim->hold_ms;
        elapsed = anim->hold_ms;
    }

    if (!anim->loop && elapsed >= anim->duration_ms) {
        set_state(anim->next_state, now_ms);
        anim = &anims[current_state];
        elapsed = 0;
    }

    uint16_t anim_time;
    if (anim->loop && anim->duration_ms > 0) {
        anim_time = (uint16_t)(elapsed % anim->duration_ms);
    } else {
        anim_time = (uint16_t)(elapsed < anim->duration_ms ? elapsed : anim->duration_ms);
    }

    const eyes_keyframe_t* kf0 = &anim->frames[0];
    const eyes_keyframe_t* kf1 = &anim->frames[0];
    for (uint8_t i = 0; i < anim->count - 1; i++) {
        if (anim_time >= anim->frames[i].time_ms && anim_time < anim->frames[i + 1].time_ms) {
            kf0 = &anim->frames[i];
            kf1 = &anim->frames[i + 1];
            break;
        }
    }
    if (anim_time >= anim->frames[anim->count - 1].time_ms) {
        kf0 = kf1 = &anim->frames[anim->count - 1];
    }

    uint16_t t = 0;
    if (kf0 != kf1) {
        uint16_t span = kf1->time_ms - kf0->time_ms;
        uint16_t pos = anim_time - kf0->time_ms;
        t = (uint16_t)((pos * 256) / span);
    }

    prev = cur;
    cur.dy        = lerp8(kf0->dy, kf1->dy, t);
    cur.height_l  = lerpu8(kf0->height_l, kf1->height_l, t);
    cur.height_r  = lerpu8(kf0->height_r, kf1->height_r, t);
    cur.width_pct = lerpu8(kf0->width_pct, kf1->width_pct, t);
    cur.curve_l   = lerp8(kf0->curve_l, kf1->curve_l, t);
    cur.curve_r   = lerp8(kf0->curve_r, kf1->curve_r, t);
    cur.brow      = lerp8(kf0->brow, kf1->brow, t);

    if (current_state == EYES_STATE_IDLE
        && (now_ms - last_external_look_ms) > IDLE_WAKE_TIMEOUT_MS
        && (int32_t)(now_ms - next_wander_ms) >= 0) {
        uint32_t r = rng_next();
        look_tx = 0.5f + ((float)((r & 0xFFFF) / 65535.0f) * 2.0f - 1.0f) * WANDER_RANGE;
        look_ty = 0.5f + ((float)(((r >> 16) & 0xFFFF) / 65535.0f) * 2.0f - 1.0f) * WANDER_RANGE;
        uint32_t span = WANDER_MAX_MS - WANDER_MIN_MS;
        next_wander_ms = now_ms + WANDER_MIN_MS + (rng_next() % span);
    }

    look_sx += (look_tx - look_sx) * LOOK_SMOOTH;
    look_sy += (look_ty - look_sy) * LOOK_SMOOTH;

    // Pupil dilation interpolation (state change snaps the target, this
    // smooths into it so SURPRISED → ACTIVE doesn't pop).
    pupil_scale_current += (pupil_scale_target - pupil_scale_current) * 0.18f;

    // Pupil micro-jitter — pick a new target every few hundred ms and
    // ease toward it, independently of any external gaze input.
    if ((int32_t)(now_ms - next_pupil_jitter_ms) >= 0) {
        uint32_t r = rng_next();
        pupil_jitter_tx = ((float)((int)((r      ) & 0xFF) - 128) / 128.0f) * PUPIL_JITTER_RANGE_PX;
        pupil_jitter_ty = ((float)((int)((r >> 8 ) & 0xFF) - 128) / 128.0f) * PUPIL_JITTER_RANGE_PX;
        uint32_t span = PUPIL_JITTER_MAX_MS - PUPIL_JITTER_MIN_MS;
        next_pupil_jitter_ms = now_ms + PUPIL_JITTER_MIN_MS + (rng_next() % span);
    }
    pupil_jitter_x += (pupil_jitter_tx - pupil_jitter_x) * PUPIL_JITTER_SMOOTH;
    pupil_jitter_y += (pupil_jitter_ty - pupil_jitter_y) * PUPIL_JITTER_SMOOTH;

    return memcmp(&cur, &prev, sizeof(cur)) != 0
        || fabsf(look_sx - look_tx) > 0.001f
        || fabsf(look_sy - look_ty) > 0.001f
        || fabsf(pupil_scale_current - pupil_scale_target) > 0.001f
        || fabsf(pupil_jitter_x - pupil_jitter_tx) > 0.05f
        || fabsf(pupil_jitter_y - pupil_jitter_ty) > 0.05f;
}

void eyes_anim_render(void) {
    display_clear();

    float gx = (look_sx - 0.5f) * 2.0f;
    float gy = (look_sy - 0.5f) * 2.0f;
    if (gx > 1.0f) gx = 1.0f; else if (gx < -1.0f) gx = -1.0f;
    if (gy > 1.0f) gy = 1.0f; else if (gy < -1.0f) gy = -1.0f;

    float rot = gx * CYL_ROT_RANGE;
    float ang_l = rot - CYL_EYE_THETA;
    float ang_r = rot + CYL_EYE_THETA;

    // Cylinder foreshortening, then per-eye width keyframe scale on top.
    float w_scale = (float)cur.width_pct / 100.0f;
    float w_l = (float)EYE_BASE_W * fabsf(cosf(ang_l)) * w_scale;
    float w_r = (float)EYE_BASE_W * fabsf(cosf(ang_r)) * w_scale;
    if (w_l < 4.0f) w_l = 4.0f;
    if (w_r < 4.0f) w_r = 4.0f;

    float x_l_center = (float)EYE_L_HOME_X + sinf(ang_l) * CYL_RADIUS - sinf(-CYL_EYE_THETA) * CYL_RADIUS;
    float x_r_center = (float)EYE_R_HOME_X + sinf(ang_r) * CYL_RADIUS - sinf( CYL_EYE_THETA) * CYL_RADIUS;

    // Per-eye height: keyframe drives the base, external eyelid (L2/R2)
    // applies as a multiplicative ceiling so blink/wink keyframes still
    // work even while a trigger is being held.
    int eye_h_l = (int)((EYE_BASE_H * cur.height_l * (1.0f - eyelid_l)) / 100.0f + 0.5f);
    int eye_h_r = (int)((EYE_BASE_H * cur.height_r * (1.0f - eyelid_r)) / 100.0f + 0.5f);

    int eye_y_center = EYE_HOME_Y + cur.dy + (int)(gy * LOOK_RANGE_Y);

    int wl_i = (int)(w_l + 0.5f);
    int wr_i = (int)(w_r + 0.5f);
    int xl_left = (int)(x_l_center + 0.5f) - wl_i / 2;
    int xr_left = (int)(x_r_center + 0.5f) - wr_i / 2;

    // Pupil offset — gaze tracking PLUS ambient micro-jitter. The
    // cylinder rotation already moves the eye's outline, so the pupil
    // shift adds subtle extra tracking rather than driving the whole
    // gaze. The jitter keeps pupils alive even when no gaze input is
    // happening (e.g. during IDLE).
    int pupil_dx = (int)(gx * 5.0f + pupil_jitter_x);
    int pupil_dy = (int)(gy * 4.0f + pupil_jitter_y);

    // Per-eye shear: top leans outward. Left eye outward = -x → shear<0;
    // right eye outward = +x → shear>0. Magnitude is tan-of-tilt; ~0.18
    // ≈ 10° tilt — visible but not cartoonish.
    static const float EYE_SHEAR = 0.18f;

    // Left eye on screen → its inner edge is RIGHT side → inner_left=false
    if (eye_h_l > 0) {
        int yl_top = eye_y_center - eye_h_l / 2;
        render_eye(xl_left, yl_top, wl_i, eye_h_l, cur.curve_l, cur.brow,
                   false, pupil_dx, pupil_dy, -EYE_SHEAR);
    }
    // Right eye on screen → its inner edge is LEFT side → inner_left=true
    if (eye_h_r > 0) {
        int yr_top = eye_y_center - eye_h_r / 2;
        render_eye(xr_left, yr_top, wr_i, eye_h_r, cur.curve_r, cur.brow,
                   true, pupil_dx, pupil_dy, EYE_SHEAR);
    }
}

void eyes_anim_set_eyelids(float l, float r) {
    if (l < 0.0f) l = 0.0f; else if (l > 1.0f) l = 1.0f;
    if (r < 0.0f) r = 0.0f; else if (r > 1.0f) r = 1.0f;
    eyelid_l = l;
    eyelid_r = r;
}

void eyes_anim_set_look(float x, float y) {
    if (x < 0.0f) x = 0.0f; else if (x > 1.0f) x = 1.0f;
    if (y < 0.0f) y = 0.0f; else if (y > 1.0f) y = 1.0f;
    look_tx = x;
    look_ty = y;
    uint32_t now = platform_time_ms();
    last_activity_ms = now;
    last_external_look_ms = now;
    next_wander_ms = now + WANDER_MIN_MS;
}

// Pick a reaction from the weighted pool, but avoid repeating the immediately
// previous reaction so consecutive button presses don't feel canned.
static eyes_state_t pick_reaction(void) {
    static eyes_state_t prev_pick = EYES_STATE_COUNT;
    eyes_state_t pick;
    for (int tries = 0; tries < 4; tries++) {
        pick = reaction_pool[rng_next() % REACTION_POOL_COUNT];
        if (pick != prev_pick) break;
    }
    prev_pick = pick;
    return pick;
}

void eyes_anim_set_state(eyes_state_t state) {
    if ((unsigned)state >= EYES_STATE_COUNT) return;
    set_state(state, platform_time_ms());
}

void eyes_anim_set_held(bool held) {
    emotion_held = held;
}

void eyes_anim_event(eyes_event_t event) {
    uint32_t now = platform_time_ms();
    last_activity_ms = now;
    switch (event) {
        case EYES_EVENT_BOOT:          set_state(EYES_STATE_BOOT, now); break;
        case EYES_EVENT_CONNECT:       set_state(EYES_STATE_HAPPY, now); break;
        case EYES_EVENT_DISCONNECT:    set_state(EYES_STATE_SAD, now); break;
        case EYES_EVENT_BUTTON_PRESS:  set_state(pick_reaction(), now); break;
        case EYES_EVENT_RUMBLE:        set_state(EYES_STATE_SURPRISED, now); break;
        case EYES_EVENT_IDLE_TIMEOUT:
            // Single-step demotion. App fires this at staged timeouts:
            //   ACTIVE → IDLE   (first wake-down: stop tracking gaze, blink)
            //   IDLE   → SLEEP  (second: half-lid breathing)
            // App gates by state so each demotion happens at its own threshold.
            if (current_state == EYES_STATE_ACTIVE) {
                set_state(EYES_STATE_IDLE, now);
            } else if (current_state == EYES_STATE_IDLE) {
                set_state(EYES_STATE_SLEEP, now);
            }
            break;
    }
}

eyes_state_t eyes_anim_get_state(void) {
    return current_state;
}
