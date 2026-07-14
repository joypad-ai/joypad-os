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
        next_blink_ms = now_ms + 1500 + (uint32_t)(rndf() * 3500.0f);
    }

    // Idle gaze wander — whenever nothing external is steering the gaze.
    if ((now_ms - last_look_ms) > 1500 && now_ms >= next_wander_ms) {
        target.gaze_x = (rndf() * 2.0f - 1.0f) * 0.55f;
        target.gaze_y = (rndf() * 2.0f - 1.0f) * 0.30f;
        next_wander_ms = now_ms + 1600 + (uint32_t)(rndf() * 2200.0f);
    }

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
    int ew = (int)(face_w * 0.11f);
    int eh = (int)(face_h * 0.34f);
    int cxl = (int)(face_w * 0.35f), cxr = (int)(face_w * 0.65f);
    int cy = (int)(face_h * 0.5f + bob + p->gaze_y * face_h * 0.06f);
    float shear = 0.16f;
    int gx = (int)(p->gaze_x * ew * 0.7f);
    struct { int cx; float open; float sh; bool inner_left; } E[2] = {
        {cxl, p->eye_open_l, -shear, false}, {cxr, p->eye_open_r, shear, true}
    };
    for (int i = 0; i < 2; i++) {
        int h = (int)(eh * E[i].open);
        if (h < 2) h = 2;
        int w = (int)(ew * (1.0f - 0.10f * p->brow_h * 0));  // width steady
        fill_ellipse(E[i].cx, cy, w, h, E[i].sh, true);
        // pupil (drawn OFF) — boba dot offset by gaze
        int pr = (int)(w * (0.42f + 0.18f * p->pupil));
        if (pr > 1 && E[i].open > 0.35f) {
            int pdx = gx, pdy = (int)(p->gaze_y * h * 0.3f);
            fill_ellipse(E[i].cx + pdx, cy + pdy, pr, pr, E[i].sh, false);
        }
        // smile via bottom-arc cut
        int depth = (int)(p->mouth_curve * h * 0.9f);
        if (depth > 0) cut_smile_arc(E[i].cx, cy + h, w, depth);
        // brow: sad/angry lowers inner-top (cut OFF a wedge)
        if (p->brow > 0.05f) {
            int bd = (int)(p->brow * h * 0.7f);
            cut_smile_arc(E[i].cx, cy - h + bd, w, bd);  // top cut approximation
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


static void style_astro(const face_pose* p, float bob) {
    // Bright vertical-capsule eyes on the "visor". Very squash-responsive.
    int ew = (int)(face_w * 0.075f);
    int eh = (int)(face_h * 0.30f);
    float sq = p->squash;                    // stretch tall / squash wide
    int cxl = (int)(face_w * 0.37f), cxr = (int)(face_w * 0.63f);
    int gx = (int)(p->gaze_x * face_w * 0.05f);
    int cy = (int)(face_h * 0.5f + bob + p->gaze_y * face_h * 0.08f);
    for (int i = 0; i < 2; i++) {
        int cx = (i ? cxr : cxl) + gx;
        float open = i ? p->eye_open_r : p->eye_open_l;
        int hw = (int)(ew * (1.0f - 0.25f * sq));
        int hh = (int)(eh * (1.0f + 0.25f * sq) * open);
        if (hh < hw) hh = hw;               // never shorter than round
        fill_capsule(cx, cy, hw, hh, true);
        // happy → carve a ^ (upward arc) from the bottom
        int depth = (int)(p->mouth_curve * hh * 1.2f);
        if (depth > 0) cut_smile_arc(cx, cy + hh, hw, depth);
    }
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
