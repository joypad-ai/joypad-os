// eyes_esp32.c - Procedural face on the LilyGo T-Display S3 AMOLED.
//
// Provides the display.h backend the face engine needs (display_clear +
// display_pixel + display_set_color over an 8-bit color-class canvas in
// PSRAM), plus a FreeRTOS task that ticks face_anim and blits it to the
// AMOLED. The canvas is rendered at ~2x the panel resolution and the blit
// box-downsamples 2x2, so edges come out anti-aliased and the accent class
// (Taby's red mouth) blends properly. Only built for the LilyGo board (see
// main/CMakeLists.txt), which sets EYES_SCALE.
#ifdef BOARD_LILYGO_TDISPLAY_S3_AMOLED

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "display.h"
#include "face_anim.h"
#include "rm67162_amoled.h"
#include "platform/platform.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

// Canvas matches the panel aspect (536:240) so nothing gets stretched by the
// blit; ~1.43x supersample for anti-aliasing. 768x344 = 264KB in PSRAM.
#define EYES_W 768
#define EYES_H 344

// 8-bit color-class canvas (0=black, 1=main, 2=accent), row-major, in PSRAM.
static uint8_t* s_fb = NULL;
static uint8_t s_color = FACE_COLOR_MAIN;

// --- display.h backend ---
void display_clear(void) { if (s_fb) memset(s_fb, 0, (size_t)EYES_W * EYES_H); }

void display_set_color(uint8_t color_index) { s_color = color_index; }

void display_pixel(int16_t x, int16_t y, bool on)
{
    if (!s_fb || (unsigned)x >= EYES_W || (unsigned)y >= EYES_H) return;
    s_fb[(size_t)x * EYES_H + y] = on ? s_color : 0;   // column-major (blit-friendly)
}

// Per-style main + accent colors (RGB565).
static uint16_t style_color(face_style_id s)
{
    switch (s) {
        case FACE_STYLE_TABY:  return 0xFFFF;   // white (real Taby)
        case FACE_STYLE_ASTRO: return 0x5EBF;   // Astro core: light cyan-blue
        case FACE_STYLE_CLASSIC:
        default:               return 0x07FF;   // cyan
    }
}

static uint16_t style_accent(face_style_id s)
{
    switch (s) {
        case FACE_STYLE_TABY:  return 0xE288;   // coral-red mouth interior
        case FACE_STYLE_ASTRO: return 0x0917;   // Astro glow: faint navy —
                                                // the reference's spill-glow
                                                // barely reads on the panel
        case FACE_STYLE_CLASSIC:
        default:               return 0x0471;   // dark cyan pupil (~55% of main — visible on AMOLED)
    }
}

// ---- remote control (CDC FACE.* commands, see cdc_commands.c) ----
// While the companion drives the face, the self-demo pauses; it resumes
// after a quiet period so an unplugged bridge doesn't leave a frozen face.
static volatile uint32_t s_remote_until = 0;
#define REMOTE_HOLD_MS 15000

void face_remote_speak(int level)
{
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    face_set_speaking((float)level / 100.0f);
    s_remote_until = platform_time_ms() + REMOTE_HOLD_MS;
}

void face_remote_state(const char* state)
{
    if (strcmp(state, "think") == 0) {
        face_set_emotion(FACE_EMO_SUSPICIOUS);   // narrowed, pondering
        face_look(0.45f, -0.55f);                // glance up-and-away
    } else {                                      // "idle" / "speak"
        face_set_emotion(FACE_EMO_NEUTRAL);
        face_look(0.0f, 0.0f);
    }
    s_remote_until = platform_time_ms() + REMOTE_HOLD_MS;
}

bool face_remote_emotion(const char* name)
{
    static const struct { const char* n; face_emotion e; } M[] = {
        {"neutral", FACE_EMO_NEUTRAL},   {"happy", FACE_EMO_HAPPY},
        {"sad", FACE_EMO_SAD},           {"angry", FACE_EMO_ANGRY},
        {"surprised", FACE_EMO_SURPRISED}, {"sleepy", FACE_EMO_SLEEPY},
        {"suspicious", FACE_EMO_SUSPICIOUS}, {"excited", FACE_EMO_EXCITED},
        {"love", FACE_EMO_LOVE},
    };
    for (size_t i = 0; i < sizeof(M) / sizeof(M[0]); i++) {
        if (strcmp(name, M[i].n) == 0) {
            face_set_emotion(M[i].e);
            s_remote_until = platform_time_ms() + REMOTE_HOLD_MS;
            return true;
        }
    }
    return false;
}

bool face_remote_style(const char* name)
{
    static const struct { const char* n; face_style_id st; } M[] = {
        {"classic", FACE_STYLE_CLASSIC}, {"taby", FACE_STYLE_TABY},
        {"astro", FACE_STYLE_ASTRO},
    };
    for (size_t i = 0; i < sizeof(M) / sizeof(M[0]); i++) {
        if (strcmp(name, M[i].n) == 0) {
            face_set_style(M[i].st);
            s_remote_until = platform_time_ms() + REMOTE_HOLD_MS;
            return true;
        }
    }
    return false;
}

void face_remote_look(int x_pct, int y_pct)
{
    face_look((float)x_pct / 100.0f, (float)y_pct / 100.0f);
    s_remote_until = platform_time_ms() + REMOTE_HOLD_MS;
}

static void eyes_task(void* arg)
{
    (void)arg;
    // Face role: BLE controller input is unused on this board — suppress the
    // perpetual scan (radio power + keeps 2.4GHz quiet near the controller).
    extern void btstack_host_suppress_scan(bool suppress);
    btstack_host_suppress_scan(true);

    amoled_init();
    amoled_set_shift(-15);   // center the face on the physical glass (the
                             // touch-circle strip offsets the active area)
    amoled_brightness(0xC8); // ~78%: plenty on AMOLED, meaningfully less battery
    extern bool pmu_init(void);
    pmu_init();   // battery telemetry + small-LiPo-safe charge config

    s_fb = heap_caps_malloc((size_t)EYES_W * EYES_H, MALLOC_CAP_SPIRAM);
    if (!s_fb) {
        ESP_LOGE("eyes", "canvas alloc failed (%d bytes)", EYES_W * EYES_H);
        vTaskDelete(NULL);
        return;
    }

    face_init(EYES_W, EYES_H);
    face_set_style(FACE_STYLE_TABY);

    // Self-driving demo until wired to real events: cycle emotions to show the
    // interrupt-anytime spring transitions, and rotate through the styles.
    // Mostly neutral (idle wander + blinks make it feel alive), with a short
    // emotion burst every few seconds — closer to how a companion behaves.
    static const face_emotion bursts[] = {
        FACE_EMO_HAPPY, FACE_EMO_SURPRISED, FACE_EMO_SUSPICIOUS,
        FACE_EMO_EXCITED, FACE_EMO_SAD, FACE_EMO_SLEEPY, FACE_EMO_ANGRY,
    };
    uint32_t next_emo = 4000, next_style = 0;
    int burst_i = 0, style = FACE_STYLE_TABY;
    bool in_burst = false;

    for (;;) {
        uint32_t now = platform_time_ms();
        if (now < s_remote_until) {
            next_emo = now + 2000;   // demo paused: companion is driving
        } else if (now >= next_emo) {
            if (in_burst) {
                face_set_emotion(FACE_EMO_NEUTRAL);
                next_emo = now + 3800;
            } else {
                face_set_emotion(bursts[burst_i]);
                burst_i = (burst_i + 1) % (int)(sizeof(bursts) / sizeof(bursts[0]));
                next_emo = now + 2200;
            }
            in_burst = !in_burst;
        }
        (void)style; (void)next_style;   // style rotation off while tuning Taby
        face_tick(now);
        // Idle throttle: while the face is settled (only the breathing bob
        // moves), render/blit at ~1/8 rate — the panel keeps its last frame.
        static uint8_t idle_skip = 0;
        if (!face_settled() || (++idle_skip & 7) == 0) {
            face_render();
            amoled_blit_idx8(s_fb, EYES_W, EYES_H,
                             style_color(face_get_style()),
                             style_accent(face_get_style()));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void eyes_start(void)
{
    xTaskCreate(eyes_task, "eyes", 8192, NULL, 3, NULL);
}

#endif // BOARD_LILYGO_TDISPLAY_S3_AMOLED
