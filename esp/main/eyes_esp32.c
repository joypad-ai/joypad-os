// eyes_esp32.c - Animated eyes on the LilyGo T-Display S3 AMOLED.
//
// Provides the minimal display.h backend the shared eyes engine needs
// (display_clear + display_pixel over a scaled 1-bit framebuffer), plus a
// FreeRTOS task that ticks the animation and blits it to the AMOLED. The
// eyes engine renders at EYES_SCALE x the base 128x64 canvas so the panel
// shows smooth eyes instead of a nearest-neighbor upscale. Only built for
// the LilyGo board (see main/CMakeLists.txt), which also sets EYES_SCALE.
#ifdef BOARD_LILYGO_TDISPLAY_S3_AMOLED

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "display.h"
#include "eyes_anim.h"
#include "rm67162_amoled.h"
#include "platform/platform.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef EYES_SCALE
#define EYES_SCALE 4
#endif
#define EYES_W (128 * EYES_SCALE)
#define EYES_H (64 * EYES_SCALE)

// Packed 1-bit framebuffer, row-major, LSB-first (matches amoled_blit_mono).
static uint8_t s_fb[(EYES_W * EYES_H + 7) / 8];

// --- display.h backend (only clear + pixel are used by eyes_anim) ---
void display_clear(void) { memset(s_fb, 0, sizeof(s_fb)); }

void display_pixel(int16_t x, int16_t y, bool on)
{
    if ((unsigned)x >= EYES_W || (unsigned)y >= EYES_H) return;
    uint32_t idx = (uint32_t)y * EYES_W + x;
    if (on) s_fb[idx >> 3] |= (1u << (idx & 7));
    else    s_fb[idx >> 3] &= ~(1u << (idx & 7));
}

static void eyes_task(void* arg)
{
    (void)arg;
    amoled_init();
    eyes_anim_init();
    eyes_anim_event(EYES_EVENT_BOOT);

    eyes_anim_render();
    amoled_blit_mono(s_fb, EYES_W, EYES_H, 0x07FF);   // cyan eyes on black

    for (;;) {
        uint32_t now = platform_time_ms();
        if (eyes_anim_tick(now)) {
            eyes_anim_render();
            amoled_blit_mono(s_fb, EYES_W, EYES_H, 0x07FF);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void eyes_start(void)
{
    xTaskCreate(eyes_task, "eyes", 8192, NULL, 3, NULL);
}

#endif // BOARD_LILYGO_TDISPLAY_S3_AMOLED
