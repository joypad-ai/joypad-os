// face_anim.h - Procedural, interrupt-anytime face engine.
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Robert Dale Smith
//
// The face is a *pose* (a handful of continuous parameters) that is always
// spring-eased toward a *target* pose. Emotions are just named target poses;
// you can change the target at any instant and the face smoothly redirects
// from wherever it currently is — no timeline to finish. Procedural overlays
// (blinks, breathing, gaze saccades) layer on top so it's never static.
//
// Rendering is decoupled from motion: the engine produces an abstract pose,
// and a swappable *style* draws it (classic eyes, Taby, Astro Bot, ...). All
// styles consume the same pose, so one engine drives many looks.
//
// Draws via display.h (display_clear + display_pixel), so it works on the
// 128x64 OLED and the AMOLED alike.

#ifndef FACE_ANIM_H
#define FACE_ANIM_H

#include <stdint.h>
#include <stdbool.h>

// Abstract facial rig — the full set of things a style can render. A style
// may ignore parameters it doesn't use (e.g. Astro has no mouth).
typedef struct {
    float eye_open_l, eye_open_r;  // 0 shut .. 1 wide
    float gaze_x, gaze_y;          // -1..1 pupil / look offset
    float pupil;                   // 0 small .. 1 dilated
    float brow;                    // -1 sad(inner down) .. +1 angry(inner down-in)
    float brow_h;                  // -1 low .. +1 raised (surprise)
    float mouth_open;              // 0 closed .. 1 open (drives lip-sync)
    float mouth_curve;             // -1 frown .. +1 smile
    float squash;                  // -1 squashed(wide/short) .. +1 stretched
} face_pose;

// Color classes styles may draw with (backends map them to real colors;
// mono backends render every class the same).
#define FACE_COLOR_MAIN   1
#define FACE_COLOR_ACCENT 2

typedef enum {
    FACE_EMO_NEUTRAL,
    FACE_EMO_HAPPY,
    FACE_EMO_SAD,
    FACE_EMO_ANGRY,
    FACE_EMO_SURPRISED,
    FACE_EMO_SLEEPY,
    FACE_EMO_SUSPICIOUS,
    FACE_EMO_EXCITED,
    FACE_EMO_LOVE,
    FACE_EMO_COUNT,
} face_emotion;

typedef enum {
    FACE_STYLE_CLASSIC,   // rounded-ellipse eyes (the current look)
    FACE_STYLE_TABY,       // big round eyes + expressive mouth
    FACE_STYLE_ASTRO,      // Astro-Bot-style visor eyes
    FACE_STYLE_COUNT,
} face_style_id;

// Initialize for a canvas of the given size (styles lay themselves out
// relative to it, so any resolution works).
void face_init(int canvas_w, int canvas_h);

// Swap the rendering style at any time. The pose/motion is unaffected.
void face_set_style(face_style_id style);
face_style_id face_get_style(void);

// Set the emotional target. The face springs toward it from wherever it is.
void face_set_emotion(face_emotion emo);
face_emotion face_get_emotion(void);

// Steer gaze. x,y in -1..1 (0 = center). Persistent target until changed.
void face_look(float x, float y);

// Drive the mouth from a speaking envelope (0 quiet .. 1 loud) for lip-sync.
// Set 0 (or stop calling) when not speaking.
void face_set_speaking(float envelope);

// Fire a one-shot blink now (transient; independent of emotion).
void face_blink(void);

// Advance springs + overlays. Call every frame with a millisecond clock.
void face_tick(uint32_t now_ms);

// Clear the framebuffer and draw the current pose in the current style.
void face_render(void);

#endif // FACE_ANIM_H
