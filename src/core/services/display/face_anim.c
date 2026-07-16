// face_anim.c - Procedural, interrupt-anytime face engine. See face_anim.h.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Robert Dale Smith

#include "face_anim.h"
#include "display.h"
#include <math.h>
#include <string.h>

// ============================================================================
// STATE
// ============================================================================

static int face_w = 128, face_h = 64;

static face_pose cur, target, vel;     // cur eased toward target; vel per field
static face_emotion cur_emo = FACE_EMO_NEUTRAL;
static face_style_id cur_style = FACE_STYLE_CLASSIC;

static float speak_env = 0.0f;         // lip-sync drive, decays on its own
static uint32_t last_ms = 0;
static uint32_t last_look_ms = 0;      // when gaze was last steered externally

// blink (transient, independent of emotion)
static bool blinking = false;
static uint32_t blink_start_ms = 0, next_blink_ms = 1500;
#define BLINK_MS 140

// pupil micro-jitter (keeps eyes alive when idle; classic style uses it)
static float pj_x = 0, pj_y = 0, pj_tx = 0, pj_ty = 0;
static uint32_t next_pj_ms = 0;

// idle life
static float breathe_t = 0.0f;
static uint32_t next_wander_ms = 3000;
static uint32_t rng = 0x2545F491u;

static inline uint32_t rnd(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng;
}
static inline float rndf(void) { return (float)(rnd() & 0xFFFF) / 65535.0f; }  // 0..1

// Emotion target poses. Compact table — this is the entire "vocabulary".
static const face_pose EMO[FACE_EMO_COUNT] = {
    // eyeL eyeR  gx   gy   pup  brow browH mOpen mCurve squash
    [FACE_EMO_NEUTRAL]    = {0.90f,0.90f, 0,   0,   0.45f, 0,   0,    0.06f, 0.15f,  0.00f},
    [FACE_EMO_HAPPY]      = {0.15f,0.15f, 0,  -0.05f,0.45f, 0,   0.05f,0.08f, 1.00f,  0.05f},
    [FACE_EMO_SAD]        = {0.72f,0.72f, 0,   0.18f,0.40f,-0.65f,-0.15f,0.05f,-0.65f,-0.10f},
    [FACE_EMO_ANGRY]      = {0.85f,0.85f, 0,  -0.05f,0.35f, 0.85f,-0.20f,0.12f,-0.40f, 0.00f},
    [FACE_EMO_SURPRISED]  = {1.00f,1.00f, 0,  -0.05f,1.00f, 0,   0.90f,0.30f, 0.00f,  0.10f},
    [FACE_EMO_SLEEPY]     = {0.24f,0.24f, 0,   0.12f,0.35f,-0.10f,-0.35f,0.10f,-0.10f,-0.20f},
    [FACE_EMO_SUSPICIOUS] = {0.42f,0.42f, 0.30f,0,  0.45f, 0.20f,-0.05f,0.05f,-0.15f, 0.00f},
    [FACE_EMO_EXCITED]    = {0.15f,0.15f, 0,  -0.05f,0.60f, 0,   0.30f,0.75f, 1.00f,  0.15f},
    [FACE_EMO_LOVE]       = {0.45f,0.45f, 0,   0.05f,0.60f, 0,   0.05f,0.00f, 0.60f,  0.05f},
    [FACE_EMO_WINK]       = {0.90f,0.60f, 0,   0,   0.45f, 0,   0.05f,0.06f, 0.30f,  0.00f},
};

// ============================================================================
// SPRING
// ============================================================================

// Exponential ease toward the target: x moves a fraction (1-e^-rate*dt) of the
// remaining gap each step. Unconditionally stable and overshoot-free at any
// framerate (an explicit-Euler spring oscillated/blew up here). Because it
// tracks a *target* (not a timeline), changing tgt mid-motion just bends the
// trajectory — that's the interrupt-anytime property. `rate` = closing speed
// per second (higher = snappier).
static inline void ease(float* x, float tgt, float rate, float dt) {
    *x += (tgt - *x) * (1.0f - expf(-rate * dt));
}

#define SPR(field, rate) ease(&cur.field, target.field, (rate), dt)

// ============================================================================
// PUBLIC: control
// ============================================================================

void face_init(int canvas_w, int canvas_h) {
    face_w = canvas_w; face_h = canvas_h;
    cur = target = EMO[FACE_EMO_NEUTRAL];
    memset(&vel, 0, sizeof(vel));
    cur_emo = FACE_EMO_NEUTRAL;
    speak_env = 0; last_ms = 0; blinking = false; next_blink_ms = 1500;
    breathe_t = 0; next_wander_ms = 3000;
}

void face_set_style(face_style_id s) { if (s < FACE_STYLE_COUNT) cur_style = s; }
face_style_id face_get_style(void) { return cur_style; }

void face_set_emotion(face_emotion e) {
    if (e >= FACE_EMO_COUNT) return;
    cur_emo = e;
    // Adopt the emotion pose as the new target, but keep whatever gaze the
    // caller has steered (gaze is its own concern).
    float gx = target.gaze_x, gy = target.gaze_y;
    target = EMO[e];
    target.gaze_x = gx; target.gaze_y = gy;
}
face_emotion face_get_emotion(void) { return cur_emo; }

void face_look(float x, float y) {
    if (x < -1) x = -1; if (x > 1) x = 1;
    if (y < -1) y = -1; if (y > 1) y = 1;
    target.gaze_x = x; target.gaze_y = y;
    last_look_ms = last_ms;
}

void face_set_speaking(float env) {
    if (env < 0) env = 0; if (env > 1) env = 1;
    if (env > speak_env) speak_env = env;
}

void face_blink(void) {
    if (!blinking) { blinking = true; blink_start_ms = last_ms; }
}

// ============================================================================
// PUBLIC: tick
// ============================================================================

void face_tick(uint32_t now_ms) {
    float dt = last_ms ? (float)(now_ms - last_ms) / 1000.0f : 0.016f;
    if (dt < 0) dt = 0; if (dt > 0.05f) dt = 0.05f;
    last_ms = now_ms;

    const float R = 8.0f;    // ease rate (~0.4s to close the gap)
    SPR(eye_open_l, R); SPR(eye_open_r, R);
    SPR(gaze_x, 10.0f);  SPR(gaze_y, 10.0f);
    SPR(pupil, R);       SPR(brow, R);   SPR(brow_h, R);
    SPR(mouth_open, 13.0f); SPR(mouth_curve, R); SPR(squash, 7.0f);

    // Blink scheduling (random cadence).
    if (!blinking && now_ms >= next_blink_ms) { blinking = true; blink_start_ms = now_ms; }
    if (blinking && (now_ms - blink_start_ms) >= BLINK_MS) {
        blinking = false;
        if (rndf() < 0.35f) {
            next_blink_ms = now_ms + 180;   // double-blink (idle signature)
        } else {
            next_blink_ms = now_ms + 1500 + (uint32_t)(rndf() * 3500.0f);
        }
    }

    // Idle gaze wander — whenever nothing external is steering the gaze.
    if ((now_ms - last_look_ms) > 1500 && now_ms >= next_wander_ms) {
        target.gaze_x = (rndf() * 2.0f - 1.0f) * 0.55f;
        target.gaze_y = (rndf() * 2.0f - 1.0f) * 0.30f;
        next_wander_ms = now_ms + 1600 + (uint32_t)(rndf() * 2200.0f);
    }

    // pupil jitter: new random target every 250-1100ms, smoothed
    if (now_ms >= next_pj_ms) {
        pj_tx = (rndf() * 2.0f - 1.0f) * 2.0f;
        pj_ty = (rndf() * 2.0f - 1.0f) * 2.0f;
        next_pj_ms = now_ms + 250 + (uint32_t)(rndf() * 850.0f);
    }
    ease(&pj_x, pj_tx, 6.0f, dt);
    ease(&pj_y, pj_ty, 6.0f, dt);

    breathe_t += dt;
    speak_env -= dt * 3.0f; if (speak_env < 0) speak_env = 0;
}

// ============================================================================
// DRAW PRIMITIVES (mono, via display_pixel; bounds = face_w/face_h)
// ============================================================================

static void fill_ellipse(int cx, int cy, int rx, int ry, float shear, bool on) {
    if (rx <= 0 || ry <= 0) return;
    int rx2 = rx * rx, ry2 = ry * ry;
    for (int y = -ry; y <= ry; y++) {
        int rhs = rx2 * (ry2 - y * y);
        if (rhs < 0) continue;
        int xs = (int)(-(float)y * shear + (y < 0 ? -0.5f : 0.5f));
        for (int x = -rx; x <= rx; x++) {
            if (x * x * ry2 <= rhs) {
                int px = cx + x + xs, py = cy + y;
                if (px >= 0 && px < face_w && py >= 0 && py < face_h)
                    display_pixel((int16_t)px, (int16_t)py, on);
            }
        }
    }
}

// Vertical capsule (rounded rect, full-round ends) — for Astro eyes.
static void fill_capsule(int cx, int cy, int hw, int hh, bool on) {
    int r = hw; // full round ends
    for (int y = -hh; y <= hh; y++) {
        int py = cy + y;
        if (py < 0 || py >= face_h) continue;
        int half = hw;
        int top = hh - r, bot = -(hh - r);
        if (y > top) { int d = r - (y - top); int dd = r * r - (r - d) * (r - d); half = (dd > 0) ? (int)sqrtf((float)dd) : 0; }
        else if (y < bot) { int d = r - (bot - y); int dd = r * r - (r - d) * (r - d); half = (dd > 0) ? (int)sqrtf((float)dd) : 0; }
        for (int x = -half; x <= half; x++) {
            int px = cx + x;
            if (px >= 0 && px < face_w) display_pixel((int16_t)px, (int16_t)py, on);
        }
    }
}

// Bottom-up parabolic smile-arc cut (draws OFF), used to carve ⌒ shapes.
static void cut_smile_arc(int cx, int cy_bottom, int half_w, int depth) {
    if (depth <= 0 || half_w <= 0) return;
    int w2 = half_w * half_w;
    for (int x = -half_w; x <= half_w; x++) {
        int rise = (depth * (w2 - x * x)) / w2;
        for (int y = 0; y <= rise; y++) {
            int px = cx + x, py = cy_bottom - y;
            if (px >= 0 && px < face_w && py >= 0 && py < face_h)
                display_pixel((int16_t)px, (int16_t)py, false);
        }
    }
}

// Mouth: a curved band that follows a smile/frown arc, thickening when open.
// curve>0 lifts the ends (smile), curve<0 drops them (frown). open_h is the
// vertical opening; clamped so a stray value can't fill the screen.
static void draw_mouth(int cx, int cy, int half_w, int thick, float curve, int open_h) {
    if (half_w <= 0) return;
    if (open_h < 0) open_h = 0;
    if (open_h > face_h / 3) open_h = face_h / 3;      // hard safety clamp
    int w2 = half_w * half_w;
    int lift = (int)(curve * half_w * 0.45f);          // vertical rise at the ends
    int band = thick + open_h;
    for (int x = -half_w; x <= half_w; x++) {
        int arc = (lift * (x * x)) / w2;               // 0 at center, |lift| at ends
        int yc = cy - arc;                              // ends rise for smile
        for (int y = -band / 2; y <= band / 2; y++) {
            int px = cx + x, py = yc + y;
            if (px >= 0 && px < face_w && py >= 0 && py < face_h)
                display_pixel((int16_t)px, (int16_t)py, true);
        }
    }
}

// Closed "happy" eye: a thick arched curve (◠). Taby uses this whenever it's
// content/talking (most of the animation).
static void draw_closed_eye(int cx, int cy, int w, int depth, int thick) {
    if (w <= 0) return;
    int w2 = w * w;
    for (int x = -w; x <= w; x++) {
        int rise = (depth * (w2 - x * x)) / w2;   // max at center, 0 at ends
        int yc = cy - depth / 2 + rise;             // concave-up smile (dips in middle)
        for (int t = -thick; t <= thick; t++) {
            int px = cx + x, py = yc + t;
            if (px >= 0 && px < face_w && py >= 0 && py < face_h)
                display_pixel((int16_t)px, (int16_t)py, true);
        }
    }
}

// ============================================================================
// STYLES — each draws the same pose its own way
// ============================================================================

// Effective (render-time) pose: cur + overlays (blink, breathe, speak).
static void effective(face_pose* p, float* bob_out) {
    *p = cur;
    // blink: fold both eyes toward shut over the blink window
    if (blinking) {
        uint32_t e = last_ms - blink_start_ms;
        float half = BLINK_MS / 2.0f;
        float amt = (e < half) ? (e / half) : ((BLINK_MS - e) / half);
        if (amt < 0) amt = 0; if (amt > 1) amt = 1;
        p->eye_open_l *= (1.0f - amt);
        p->eye_open_r *= (1.0f - amt);
    }
    // lip-sync: mouth opens to the louder of pose vs speech envelope
    if (speak_env > p->mouth_open) p->mouth_open = speak_env;
    // breathing bob + subtle squash
    *bob_out = sinf(breathe_t * 1.6f) * (face_h * 0.012f);
    p->squash += sinf(breathe_t * 1.6f) * 0.03f;
}

static void style_classic(const face_pose* p, float bob) {
    // Faithful port of the original eyes_anim look: ellipse eyes on a
    // pseudo-3D cylinder (width foreshortens on the side rotating away),
    // boba pupils with inward bias, outward shear, smile-arc fold, brow cuts.
    // Old geometry was authored on a 128x64 canvas; k converts to canvas px.
    float k = (float)face_h / 64.0f;
    float gx = p->gaze_x, gy = p->gaze_y;
    float rot = gx * 0.55f;                       // CYL_ROT_RANGE
    float ang_l = rot - 0.55f, ang_r = rot + 0.55f;  // CYL_EYE_THETA
    float cylr = 36.0f * k;

    float w_l = 36.0f * k * fabsf(cosf(ang_l));
    float w_r = 36.0f * k * fabsf(cosf(ang_r));
    if (w_l < 4 * k) w_l = 4 * k;
    if (w_r < 4 * k) w_r = 4 * k;

    int cx0 = face_w / 2;
    float x_l = (float)(cx0 - 19.0f * k) + sinf(ang_l) * cylr - sinf(-0.55f) * cylr;
    float x_r = (float)(cx0 + 19.0f * k) + sinf(ang_r) * cylr - sinf(0.55f) * cylr;
    int ecy = (int)(face_h * 0.5f + bob + gy * 6.0f * k);

    int pdx = (int)((gx * 5.0f + pj_x) * k), pdy = (int)((gy * 4.0f + pj_y) * k);

    for (int i = 0; i < 2; i++) {
        float open = i ? p->eye_open_r : p->eye_open_l;
        // happy fold keeps the eye at ~60% with a deep smile-arc cut
        float h_pct = open;
        int curve = 0;
        if (p->mouth_curve > 0.30f) {
            curve = (int)(p->mouth_curve * 85.0f);
            if (h_pct < 0.58f) h_pct = 0.58f * p->mouth_curve + h_pct * (1.0f - p->mouth_curve);
        }
        // Curve-blink (the old classic signature): a closing eye folds into
        // the ⌒ smile-arc instead of squashing flat — keeps character shut.
        if (open < 0.55f && p->mouth_curve <= 0.30f) {
            float shut = 1.0f - (open / 0.55f);          // 0 open .. 1 shut
            h_pct = 0.55f + (open / 0.55f) * (h_pct - 0.55f);
            if (h_pct > 1.0f) h_pct = 1.0f;
            curve = (int)(100.0f * shut);
        }
        int eh = (int)(48.0f * k * h_pct);
        if (eh < 2) eh = 2;
        int ew = (int)((i ? w_r : w_l) + 0.5f);
        int cx = (int)((i ? x_r : x_l) + 0.5f);
        float shear = i ? 0.18f : -0.18f;
        int rx = ew / 2, ry = eh / 2;
        if (rx < 2) rx = 2;
        if (ry < 1) ry = 1;
        fill_ellipse(cx, ecy, rx, ry, shear, true);

        // boba pupil (drawn off) — inward bias, gaze tracking, clamped inside
        int base_r = (ew < eh ? ew : eh) / 3;
        if (base_r < (int)(2 * k)) base_r = (int)(2 * k);
        if (base_r > (int)(10 * k)) base_r = (int)(10 * k);
        int pr = (int)(base_r * (0.8f + 0.4f * p->pupil));
        if (pr >= 1 && (open > 0.35f || curve < 40)) {
            int margin = (int)k + 1;
            int max_dx = rx - pr - margin, max_dy = ry - pr - margin;
            if (max_dx < 0) max_dx = 0;
            if (max_dy < 0) max_dy = 0;
            int inward = (int)(3.0f * k) * (i ? -1 : 1);
            int dx = pdx + inward, dy = pdy;
            if (dx > max_dx) dx = max_dx;
            if (dx < -max_dx) dx = -max_dx;
            if (dy > max_dy) dy = max_dy;
            if (dy < -max_dy) dy = -max_dy;
            int xshift = (int)(-(float)dy * shear + (dy < 0 ? -0.5f : 0.5f));
            // pupil in the accent class: backends map it to a darker shade
            // of the eye color (reads as an iris, not a hole)
            display_set_color(FACE_COLOR_ACCENT);
            fill_ellipse(cx + dx + xshift, ecy + dy, pr, pr, 0.0f, true);
            display_set_color(FACE_COLOR_MAIN);
        }

        // smile-arc fold from the bottom
        if (curve > 0) {
            int depth = (curve * eh) / 100;
            cut_smile_arc(cx, ecy + ry, rx, depth);
        }
        // brow cuts: angry = triangular top cut toward center; sad = inner wedge
        if (p->brow > 0.1f) {
            int t = (int)(p->brow * 90.0f);
            int max_depth = ((eh - 2) * (t > 100 ? 100 : t)) / 100;
            bool inner_left = (i == 1);
            for (int x = 0; x < ew && max_depth > 0; x++) {
                int dist = inner_left ? (ew - 1 - x) : x;
                int depth = (dist * max_depth) / (ew - 1 > 0 ? ew - 1 : 1);
                for (int y = 0; y < depth; y++) {
                    int py = ecy - ry + y;
                    int yfc = py - ecy;
                    int xs = (int)(-(float)yfc * shear + (yfc < 0 ? -0.5f : 0.5f));
                    int px = cx - rx + x + xs;
                    if (px >= 0 && px < face_w && py >= 0 && py < face_h)
                        display_pixel((int16_t)px, (int16_t)py, false);
                }
            }
        }
    }
}

// Blush: short slanted accent-color ticks beside an eye (Taby blush face).
static void draw_blush_ticks(int cx, int cy, int len, int dir) {
    display_set_color(FACE_COLOR_ACCENT);
    int gap = len / 2 + 2;
    for (int t = 0; t < 3; t++) {
        int x0 = cx + dir * t * gap;
        for (int k = 0; k < len; k++) {
            int px = x0 + dir * (k / 2), py = cy + k;
            if (px >= 0 && px < face_w && py >= 0 && py < face_h) {
                display_pixel((int16_t)px, (int16_t)py, true);
                if (px + 1 < face_w) display_pixel((int16_t)(px + 1), (int16_t)py, true);
                if (px + 2 < face_w) display_pixel((int16_t)(px + 2), (int16_t)py, true);
            }
        }
    }
    display_set_color(FACE_COLOR_MAIN);
}

// Cat mouth (ω): two small ‿ bumps side by side.
static void draw_omega(int cx, int cy, int w, int thick) {
    int r = w / 2; if (r < 2) r = 2;
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        int bx = cx + sgn * r;
        int r2 = r * r;
        for (int x = -r; x <= r; x++) {
            int rise = ((r / 2 + 1) * (r2 - x * x)) / r2;
            int yc = cy - r / 4 + rise;              // ‿ per lobe
            for (int t = 0; t < thick; t++) {
                int px = bx + x, py = yc + t;
                if (px >= 0 && px < face_w && py >= 0 && py < face_h)
                    display_pixel((int16_t)px, (int16_t)py, true);
            }
        }
    }
}

// Small "o" mouth: ring outline.
static void draw_o_mouth(int cx, int cy, int r, int thick) {
    fill_ellipse(cx, cy, r, r, 0.0f, true);
    fill_ellipse(cx, cy, r - thick, r - thick, 0.0f, false);
}

// Taby talking mouth: bean-shaped white outline (gentle top, round bottom)
// with a white TEETH band across the top of the interior and a red TONGUE
// rising from the bottom. open_t 0..1 scales teeth recede / tongue grow.
static void draw_open_mouth(int cx, int cy, int hw, int hh, int rim, float open_t) {
    if (hw <= 0 || hh <= 0) return;
    int rt = (int)(hh * 0.70f), rb = hh;
    bool solid = (rim <= 0);          // grin: all teeth, no interior/tongue
    int ihw = hw - rim, ihh = hh - rim;
    // teeth band bottom (interior coords): fills interior when barely open,
    // recedes to the top ~35% when wide open
    float tf = 0.40f - 0.10f * open_t;   // teeth = top ~1/3 of interior
    int teeth_bot = solid ? (cy + hh) : (cy - ihh + (int)(2.0f * ihh * tf));
    // tongue ellipse, bottom-anchored, grows with open_t
    float ta = ihw * 0.90f;
    float tb = solid ? 0.0f : ihh * (0.55f + 0.55f * open_t);
    float tcy = cy + ihh * 0.92f;
    for (int y = -hh; y <= hh; y++) {
        int py = cy + y; if (py < 0 || py >= face_h) continue;
        // outer half-width at this row
        int half = hw;
        if (y < 0 && -y > hh - rt) {
            int dy = (-y) - (hh - rt); int v = rt * rt - dy * dy;
            half = (hw - rt) + (v > 0 ? (int)sqrtf((float)v) : 0);
        } else if (y > 0 && y > hh - rb) {
            int dy = y - (hh - rb); int v = rb * rb - dy * dy;
            half = (hw - rb) + (v > 0 ? (int)sqrtf((float)v) : 0);
        }
        // interior half-width at this row
        int ihalf = -1;
        if (y >= -ihh && y <= ihh) {
            int irt = (int)(ihh * 0.70f), irb = ihh;
            ihalf = ihw;
            if (y < 0 && -y > ihh - irt) {
                int dy = (-y) - (ihh - irt); int v = irt * irt - dy * dy;
                ihalf = (ihw - irt) + (v > 0 ? (int)sqrtf((float)v) : 0);
            } else if (y > 0 && y > ihh - irb) {
                int dy = y - (ihh - irb); int v = irb * irb - dy * dy;
                ihalf = (ihw - irb) + (v > 0 ? (int)sqrtf((float)v) : 0);
            }
        }
        for (int x = -half; x <= half; x++) {
            int px = cx + x;
            if (px < 0 || px >= face_w) continue;
            if (ihalf < 0 || x < -ihalf || x > ihalf) {
                display_set_color(FACE_COLOR_MAIN);            // rim
                display_pixel((int16_t)px, (int16_t)py, true);
            } else if (py <= teeth_bot) {
                // teeth: inset from the lip by a dark seam; chamfered bottom
                // corners so they read as teeth, not part of the lip
                int seam = rim > 0 ? rim : 0;
                int tw = ihalf - seam;
                int dy_b = teeth_bot - py;
                int cr = ihh / 4;
                if (dy_b < cr) tw -= (cr - dy_b);
                if (x >= -tw && x <= tw) {
                    display_set_color(FACE_COLOR_MAIN);        // teeth
                    display_pixel((int16_t)px, (int16_t)py, true);
                }
            } else {
                float dx = (float)x / ta, dy2 = (float)(py - tcy) / (tb > 1 ? tb : 1);
                if (tb > 1 && dx * dx + dy2 * dy2 <= 1.0f) {
                    display_set_color(FACE_COLOR_ACCENT);      // tongue
                    display_pixel((int16_t)px, (int16_t)py, true);
                }
            }
        }
    }
    display_set_color(FACE_COLOR_MAIN);
}

static void style_taby(const face_pose* p, float bob) {
    // heytaby Taby, geometry measured from the reference video (fractions of
    // canvas HEIGHT, scaled 1.2x to fill the panel). All x-positions hang off
    // the canvas center so canvas aspect never distorts the face; the whole
    // face (mouth included) shifts with gaze.
    float Hf = (float)face_h;
    int cx0 = face_w / 2;
    int gx = (int)(p->gaze_x * Hf * 0.11f);
    int gy = (int)(p->gaze_y * Hf * 0.05f);

    int ehw  = (int)(Hf * 0.144f);            // eye half-width
    int ehh  = (int)(Hf * 0.211f);            // eye half-height (open)
    int eoff = (int)(Hf * 0.463f);            // eye center offset from middle
    int ecy  = (int)(Hf * 0.478f + bob) + gy;
    int thin = (int)(Hf * 0.020f) + 1;

    for (int i = 0; i < 2; i++) {
        int cx = cx0 + (i ? eoff : -eoff) + gx;
        float open = i ? p->eye_open_r : p->eye_open_l;
        if (open < 0.40f && p->mouth_curve > 0.45f) {
            // happy squint only: the ‿ closed-eye arc
            draw_closed_eye(cx, ecy - (int)(ehh * 0.25f), (int)(ehw * 1.25f),
                            (int)(ehh * 0.55f), thin);
        } else {
            // blink / sleepy / neutral: the eye squashes vertically all the
            // way to a thin sliver — no shape swap, so blinks read naturally
            int hh = (int)(ehh * (0.06f + 0.94f * open));
            if (hh < thin) hh = thin;
            // dome top; bottom scoops UPWARD at the center (concave, anime
            // lower-lid) — the corners hang lower than the middle (per the
            // reference frames)
            int rtop = ehw; if (rtop > hh) rtop = hh;
            int barc = (int)(ehw * 0.20f);            // scoop depth (subtle dip)
            if (barc > hh / 2) barc = hh / 2;
            float shear = (i ? 0.045f : -0.045f);
            for (int y = -hh; y <= hh; y++) {
                int py = ecy + y; if (py < 0 || py >= face_h) continue;
                int half = ehw;
                int scoop = 0;
                if (y < 0 && -y > hh - rtop) {
                    int dy = (-y) - (hh - rtop); int v = rtop * rtop - dy * dy;
                    half = (ehw - rtop) + (v > 0 ? (int)sqrtf((float)v) : 0);
                } else if (y > hh - barc) {
                    // concave scoop: middle of the row is cut away, widening
                    // toward the bottom row (parabolic lower lid)
                    float t = 1.0f - (float)(hh - y) / (float)barc;  // 0..1
                    scoop = (int)((ehw - 1) * sqrtf(t));
                }
                int xs = (int)(-(float)y * shear + (y < 0 ? -0.5f : 0.5f));
                for (int x = -half; x <= half; x++) {
                    if (scoop && x > -scoop && x < scoop) continue;
                    int px = cx + x + xs;
                    if (px >= 0 && px < face_w) display_pixel((int16_t)px, (int16_t)py, true);
                }
            }
        }
        // thin floating brow, anchored high like the video; rises w/ surprise
        int bcy = (int)(Hf * 0.121f) + gy / 2 - (int)(p->brow_h * Hf * 0.05f);
        float bcurve = -(0.5f + p->brow_h * 0.5f - p->brow * 0.7f);  // arch
        draw_mouth(cx, bcy, (int)(Hf * 0.092f), thin, bcurve, 0);
    }

    // mouth — follows gaze with the face. Rest = thin ‿ smile arc; talking
    // opens through a teeth grin into the open teeth+tongue mouth (video-exact
    // sequence). Never an oval.
    int mcx = cx0 + gx;
    int mcy = (int)(Hf * 0.784f + bob) + gy;
    float mo = p->mouth_open;
    if (cur_emo == FACE_EMO_LOVE) {
        // Taby blush face: ω mouth + pink blush ticks beside the eyes
        draw_omega(mcx, mcy - (int)(Hf * 0.02f), (int)(Hf * 0.10f), thin);
        int bl = (int)(Hf * 0.07f);
        int bcy2 = ecy + (int)(ehh * 0.55f);
        draw_blush_ticks(cx0 - eoff - (int)(ehw * 1.9f) + gx, bcy2, bl, +1);
        draw_blush_ticks(cx0 + eoff + (int)(ehw * 1.9f) + gx, bcy2, bl, -1);
        return;
    }
    if (cur_emo == FACE_EMO_SURPRISED && mo < 0.55f) {
        // surprise onset: the little "o" mouth
        draw_o_mouth(mcx, mcy, (int)(Hf * 0.045f) + 2, thin + 1);
        return;
    }
    if (mo < 0.12f) {
        float ceff = (p->mouth_curve >= 0.0f)
                         ? (0.45f + p->mouth_curve * 0.55f)
                         : (p->mouth_curve * 0.8f);      // frown when sad
        draw_mouth(mcx, mcy, (int)(Hf * 0.19f), thin + 1, ceff, 0);
    } else if (mo < 0.45f) {
        // teeth grin: white flat stadium with dark corner notches (mid-talk)
        int hw = (int)(Hf * 0.185f);
        int hh = (int)(Hf * 0.058f);
        draw_open_mouth(mcx, mcy, hw, hh, 0, 0.0f);   // rim=0 -> solid white
        // dark crescents cutting in from each end (teeth vs lip separation)
        fill_ellipse(mcx - hw, mcy + hh / 3, (int)(hh * 0.85f), (int)(hh * 0.60f), 0.0f, false);
        fill_ellipse(mcx + hw, mcy + hh / 3, (int)(hh * 0.85f), (int)(hh * 0.60f), 0.0f, false);
    } else {
        float open_t = (mo - 0.45f) / 0.55f;             // 0..1 fully open
        int hw = (int)(Hf * (0.16f + 0.055f * open_t));
        int hh = (int)(Hf * (0.065f + 0.100f * open_t));
        int rim = (int)(Hf * 0.024f) + 1;
        draw_open_mouth(mcx, mcy, hw, hh, rim, open_t);
    }
}


// Heart size relative to the eye box (bigger scale = smaller heart; the
// implicit heart spans ~|1.15| in its own units).
#define HEART_SCALE 1.15f

// Astro shape context — one description of the eyes' TRUE current shape
// (disc, squint dome, or heart) that BOTH the LED clip and the drop-shadow
// derive from, so they can never disagree.
typedef struct {
    float excx[2];        // eye centers x
    float ecy;            // eye center y
    float rx, inv_rx;     // horizontal radius
    float ry_e[2];        // per-eye vertical radius (blink squash)
    float inv_ry_e[2];
    float ry_base;
    // Concave bottom carve (squint/wink — "basically a smile"): a cutter
    // circle centered cut_m[e] below the eye center (normalized units) with
    // radius cut_r[e] eats the disc's bottom into an upward arc, leaving
    // horns at the sides. cut_r 0 = no carve.
    float cut_m[2], cut_r[2];
    bool  hearts;         // LOVE: the shape is a heart
    float rlut[64];       // hearts: polar boundary radius per angle bin
} astro_ctx;

// Classic implicit heart, upright (v = up): (u^2+v^2-1)^3 <= u^2 * v^3.
static inline bool heart_inside(float u, float v) {
    float a = u * u + v * v - 1.0f;
    return a * a * a <= u * u * v * v * v;
}

// Normalized distance of a canvas pixel from eye e's shape boundary
// (1.0 = on the boundary; >1 outside). Used per-DOT (sqrt/atan ok here).
static float astro_eye_d(const astro_ctx* c, float pxf, float pyf, int e) {
    float nx = (pxf - c->excx[e]) * c->inv_rx;
    float ny = (pyf - c->ecy) * c->inv_ry_e[e];
    if (c->hearts) {
        float r = sqrtf(nx * nx + ny * ny);
        int bin = (int)((atan2f(-ny, nx) + (float)M_PI) * (64.0f / (2.0f * (float)M_PI)));
        if (bin < 0) bin = 0; if (bin > 63) bin = 63;
        float rb = c->rlut[bin];
        return rb > 0.001f ? r / rb : 9e9f;
    }
    float d = sqrtf(nx * nx + ny * ny);
    if (c->cut_r[e] > 0.0f) {
        // concave bottom (smile arc): points inside the cutter are outside
        // the shape by their depth past its boundary — the shadow follows
        // the arc
        float cdy = ny - c->cut_m[e];
        float dc = sqrtf(nx * nx + cdy * cdy);
        if (dc < c->cut_r[e]) {
            float dcut = 1.0f + (c->cut_r[e] - dc);
            if (dcut > d) d = dcut;
        }
    }
    return d;
}

// Is a canvas pixel inside the lit shape? The edge is a PURE outline that
// CLIPS the LEDs — edge dots render the geometric intersection. Runs per
// pixel: no sqrt/div/atan (hearts use the implicit polynomial directly).
static bool astro_px_in(const astro_ctx* c, float pxf, float pyf) {
    for (int e = 0; e < 2; e++) {
        float nx = (pxf - c->excx[e]) * c->inv_rx;
        float ny = (pyf - c->ecy) * c->inv_ry_e[e];
        if (c->hearts) {
            if (heart_inside(nx * HEART_SCALE, -ny * HEART_SCALE)) return true;
            continue;
        }
        if (nx * nx + ny * ny > 1.0f) continue;
        // concave bottom (smile arc): carved where the cutter circle reaches
        if (c->cut_r[e] > 0.0f) {
            float cdy = ny - c->cut_m[e];
            if (nx * nx + cdy * cdy < c->cut_r[e] * c->cut_r[e]) continue;
        }
        return true;
    }
    return false;
}

// Shadow intensity at a canvas point: a soft drop-shadow hugging the true
// shape's boundary, strongest at the edge and gone `range_px` (canvas px)
// out. Per-eye (nearest wins, NOT summed): each eye carries its own halo —
// summing brightened the gap between them into a stretched bridge.
static float astro_glow(const astro_ctx* c, float pxf, float pyf,
                        float range_px) {
    float g = 0.0f;
    for (int e = 0; e < 2; e++) {
        float d = astro_eye_d(c, pxf, pyf, e);
        float dist = (d - 1.0f) * 0.5f * (c->rx + c->ry_e[e]);  // ~px past edge
        if (dist < range_px) {
            float f = 1.0f - (dist < 0.0f ? 0.0f : dist) / range_px;
            if (f > g) g = f;
        }
    }
    return g * g;
}

static void style_astro(const face_pose* p, float bob) {
    // Astro Bot visor: a FIXED lattice of round LEDs; the image moves across
    // the dots, the dots never move. The eyes are pure circles that CLIP the
    // lattice: interior LEDs are full bright dots, edge LEDs light only the
    // pixels inside the circle (crescents), and a faint navy glow spills a
    // couple of pitches beyond the discs. Rest of the visor stays black.
    float Hf = (float)face_h;
    int cx0 = face_w / 2;
    float gx = p->gaze_x * Hf * 0.22f;
    float gy = p->gaze_y * Hf * 0.10f + bob;

    astro_ctx c;
    float eoff = Hf * 0.40f;                     // eye offset from center
    c.rx = Hf * 0.27f * (1.0f - 0.12f * p->squash);
    c.ry_base = Hf * 0.27f * (1.0f + 0.15f * p->squash);
    c.inv_rx = 1.0f / c.rx;
    c.excx[0] = cx0 - eoff + gx;
    c.excx[1] = cx0 + eoff + gx;
    c.ecy = Hf * 0.50f + gy;

    // squint fold (concave smile carve) — disabled for hearts (whole shapes)
    c.hearts = (cur_emo == FACE_EMO_LOVE);
    bool wink = (cur_emo == FACE_EMO_WINK);
    float fold = (!c.hearts && !wink && p->mouth_curve > 0.35f)
                     ? p->mouth_curve : 0.0f;
    c.cut_r[0] = c.cut_r[1] = 0.0f;
    c.cut_m[0] = c.cut_m[1] = 0.0f;
    if (fold > 0.0f) {
        // squint: both eyes carved by an upward arc whose apex rises with
        // the fold — "basically a smile", not a flat base
        float apex = 1.0f - 0.85f * fold;      // arc bottom at eye center line
        for (int e = 0; e < 2; e++) {
            c.cut_r[e] = 1.25f;
            c.cut_m[e] = apex + 1.25f;
        }
    }
    if (wink) {
        // right eye: squashed crescent with a deep smile carve; left stays
        // a full disc (open)
        c.cut_r[1] = 1.15f;
        c.cut_m[1] = 0.15f + 1.15f;
    }

    if (c.hearts) {
        // polar boundary LUT for the heart (used for the shadow distance):
        // march each angle inward until the implicit says inside
        for (int b = 0; b < 64; b++) {
            float a = -3.1415927f + (b + 0.5f) * (2.0f * 3.1415927f / 64.0f);
            float ca = cosf(a), sa = sinf(a);
            float r = 1.5f;
            while (r > 0.02f &&
                   !heart_inside(r * ca * HEART_SCALE, r * sa * HEART_SCALE))
                r -= 0.02f;
            c.rlut[b] = r;
        }
    }

    // Lattice metrics measured off the official visor: ~19 dot rows over the
    // screen height, lit dots nearly touching (fill ~0.84 of pitch).
    int pitch = (int)(Hf / 19.0f); if (pitch < 4) pitch = 4;   // LED spacing
    int dot_r = (int)(pitch * 0.42f); if (dot_r < 1) dot_r = 1;

    // per-eye vertical radius (blink/sleepy squash the disc; happy keeps it
    // big and carves instead; hearts render whole). Neutral eye_open (0.90)
    // maps to a PERFECT circle — only real blinks/sleepy squash below that.
    for (int e = 0; e < 2; e++) {
        float open = (e ? p->eye_open_r : p->eye_open_l) * (1.0f / 0.90f);
        if (open > 1.0f) open = 1.0f;
        if (c.hearts) open = 1.0f;
        c.ry_e[e] = c.ry_base * (fold > 0.0f ? (0.85f + 0.15f * open) : open);
        if (c.ry_e[e] < c.ry_base * 0.06f) c.ry_e[e] = c.ry_base * 0.06f;
        c.inv_ry_e[e] = 1.0f / c.ry_e[e];
    }

    for (int y = pitch / 2; y < face_h; y += pitch) {
        for (int x = pitch / 2; x < face_w; x += pitch) {
            // nearest-eye distance for this LED's center (shape-aware)
            float dmin = 1e9f;
            for (int e = 0; e < 2; e++) {
                float d = astro_eye_d(&c, (float)x, (float)y, e);
                if (d < dmin) dmin = d;
            }
            float dr_n = (float)dot_r / (c.rx < c.ry_base ? c.rx : c.ry_base);

            // The lattice is a CLIP FILTER over one continuous image: the
            // bright eye shape plus its soft drop-shadow. Every LED is the
            // same size; a physical LED shows ONE brightness, so shadow is
            // sampled once per dot and drawn as a dimmed-accent shade —
            // bright inside the outline, faint navy near it, black beyond.
            float shadow_px = 6.0f * pitch;
            if (dmin <= 1.0f - dr_n * 1.2f) {
                display_set_color(FACE_COLOR_MAIN);            // fully inside
                fill_ellipse(x, y, dot_r, dot_r, 0.0f, true);
                continue;
            }
            // past the shadow: (dmin-1) in px beyond the edge, minus dot reach
            if ((dmin - 1.0f) * 0.5f * (c.rx + c.ry_base) > shadow_px + dot_r)
                continue;

            // this LED's shadow shade — dots hugging the shape read dimmer
            // than lit ones, then step down as the shadow fades out
            float g = astro_glow(&c, (float)x, (float)y, shadow_px) * 0.75f;
            uint8_t shade = 0;
            if (g > 0.50f)      shade = FACE_COLOR_ACCENT_75;
            else if (g > 0.28f) shade = FACE_COLOR_ACCENT_50;
            else if (g > 0.08f) shade = FACE_COLOR_ACCENT_25;

            if (dmin >= 1.0f + dr_n) {
                // fully outside the outline: a pure shadow LED
                if (!shade) continue;
                display_set_color(shade);
                fill_ellipse(x, y, dot_r, dot_r, 0.0f, true);
                continue;
            }

            // boundary LED: the outline clips it — bright crescent inside,
            // this LED's shadow shade outside
            int r2 = dot_r * dot_r;
            for (int py = y - dot_r; py <= y + dot_r; py++) {
                if (py < 0 || py >= face_h) continue;
                for (int px2 = x - dot_r; px2 <= x + dot_r; px2++) {
                    if (px2 < 0 || px2 >= face_w) continue;
                    int ddx = px2 - x, ddy = py - y;
                    if (ddx * ddx + ddy * ddy > r2) continue;
                    if (astro_px_in(&c, (float)px2, (float)py)) {
                        display_set_color(FACE_COLOR_MAIN);
                        display_pixel((int16_t)px2, (int16_t)py, true);
                    } else if (shade) {
                        display_set_color(shade);
                        display_pixel((int16_t)px2, (int16_t)py, true);
                    }
                }
            }
        }
    }
    display_set_color(FACE_COLOR_MAIN);
}

bool face_settled(void)
{
    if (blinking || speak_env > 0.02f) return false;
    float d = 0;
    d += fabsf(cur.eye_open_l - target.eye_open_l);
    d += fabsf(cur.eye_open_r - target.eye_open_r);
    d += fabsf(cur.gaze_x - target.gaze_x) + fabsf(cur.gaze_y - target.gaze_y);
    d += fabsf(cur.mouth_open - target.mouth_open);
    d += fabsf(cur.mouth_curve - target.mouth_curve);
    d += fabsf(cur.brow_h - target.brow_h) + fabsf(cur.brow - target.brow);
    return d < 0.02f;
}

void face_render(void) {
    display_clear();
    face_pose p; float bob;
    effective(&p, &bob);
    switch (cur_style) {
        case FACE_STYLE_TABY:  style_taby(&p, bob);  break;
        case FACE_STYLE_ASTRO: style_astro(&p, bob); break;
        case FACE_STYLE_CLASSIC:
        default:               style_classic(&p, bob); break;
    }
}
