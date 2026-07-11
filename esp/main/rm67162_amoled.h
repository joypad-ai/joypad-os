// RM67162 AMOLED QSPI driver for LilyGo T-Display S3 AMOLED (240x536, 1.91").
// Native ESP-IDF (no Arduino). Only built for BOARD_LILYGO_TDISPLAY_S3_AMOLED.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Panel native geometry (portrait). Landscape is 536x240 via rotation.
#define AMOLED_W 240
#define AMOLED_H 536

// Initialize QSPI bus + RM67162 panel. Safe to call once at boot.
void amoled_init(void);

// Fill the entire panel with a single RGB565 color.
void amoled_fill(uint16_t rgb565);

// Push a rectangle of RGB565 pixels (native-order, will be byte-swapped for panel).
void amoled_push(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data);

// Blit a packed 1-bit monochrome buffer (w x h, row-major, LSB-first, bit set
// = lit) scaled to fill the panel in landscape orientation. Lit pixels use
// `color` (RGB565), rest black.
void amoled_blit_mono(const uint8_t* mono, int w, int h, uint16_t color);

// Set display brightness (0x00-0xFF).
void amoled_brightness(uint8_t level);

#ifdef __cplusplus
}
#endif
