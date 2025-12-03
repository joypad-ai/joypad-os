// display.h - OLED Display Driver
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// SH1106 128x64 OLED display driver over SPI.
// Used on MacroPad RP2040.

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

// Display dimensions
#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 64

// Display pin configuration
typedef struct {
    uint8_t spi_inst;   // SPI instance (0 or 1)
    uint8_t pin_sck;    // SPI clock
    uint8_t pin_mosi;   // SPI data out
    uint8_t pin_cs;     // Chip select
    uint8_t pin_dc;     // Data/Command
    uint8_t pin_rst;    // Reset
} display_config_t;

// Initialize display with pin configuration
void display_init(const display_config_t* config);

// Clear display
void display_clear(void);

// Update display (send framebuffer to OLED)
void display_update(void);

// Set pixel at x,y (0=off, 1=on)
void display_pixel(uint8_t x, uint8_t y, bool on);

// Draw text at position (using built-in 6x8 font)
void display_text(uint8_t x, uint8_t y, const char* text);

// Draw large text (12x16 font, for mode display)
void display_text_large(uint8_t x, uint8_t y, const char* text);

// Draw horizontal line
void display_hline(uint8_t x, uint8_t y, uint8_t w);

// Draw vertical line
void display_vline(uint8_t x, uint8_t y, uint8_t h);

// Draw rectangle outline
void display_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h);

// Draw filled rectangle
void display_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on);

// Draw progress bar (for rumble visualization)
void display_progress_bar(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t percent);

// Check if display is initialized
bool display_is_initialized(void);

// Invert display colors
void display_invert(bool invert);

// Set display contrast (0-255)
void display_set_contrast(uint8_t contrast);

// ============================================================================
// MARQUEE (scrolling text)
// ============================================================================

// Add text to the marquee scroll buffer
void display_marquee_add(const char* text);

// Update marquee animation (call periodically, returns true if display needs update)
bool display_marquee_tick(void);

// Render marquee at specified y position
void display_marquee_render(uint8_t y);

// Clear the marquee buffer
void display_marquee_clear(void);

#endif // DISPLAY_H
