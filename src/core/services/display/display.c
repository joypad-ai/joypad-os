// display.c - OLED Display Driver
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "display.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// SH1106 COMMANDS
// ============================================================================

#define SH1106_SET_CONTRAST         0x81
#define SH1106_DISPLAY_ALL_ON_RESUME 0xA4
#define SH1106_DISPLAY_ALL_ON       0xA5
#define SH1106_NORMAL_DISPLAY       0xA6
#define SH1106_INVERT_DISPLAY       0xA7
#define SH1106_DISPLAY_OFF          0xAE
#define SH1106_DISPLAY_ON           0xAF
#define SH1106_SET_DISPLAY_OFFSET   0xD3
#define SH1106_SET_COM_PINS         0xDA
#define SH1106_SET_VCOM_DETECT      0xDB
#define SH1106_SET_DISPLAY_CLOCK    0xD5
#define SH1106_SET_PRECHARGE        0xD9
#define SH1106_SET_MULTIPLEX        0xA8
#define SH1106_SET_LOW_COLUMN       0x00
#define SH1106_SET_HIGH_COLUMN      0x10
#define SH1106_SET_START_LINE       0x40
#define SH1106_MEMORY_MODE          0x20
#define SH1106_SET_PAGE_ADDR        0xB0
#define SH1106_COM_SCAN_INC         0xC0
#define SH1106_COM_SCAN_DEC         0xC8
#define SH1106_SEG_REMAP            0xA0
#define SH1106_CHARGE_PUMP          0x8D

// SH1106 has 132 columns, but only 128 are visible (offset by 2)
#define SH1106_COL_OFFSET           2

// ============================================================================
// 6x8 FONT
// ============================================================================

// Arrow glyphs (chars 1-4: up, down, left, right)
static const uint8_t font_arrows[][6] = {
    {0x04,0x02,0x7F,0x02,0x04,0x00}, // char 1: ↑ up
    {0x10,0x20,0x7F,0x20,0x10,0x00}, // char 2: ↓ down
    {0x08,0x1C,0x2A,0x08,0x08,0x00}, // char 3: ← left
    {0x08,0x08,0x2A,0x1C,0x08,0x00}, // char 4: → right
};

static const uint8_t font_6x8[] = {
    0x00,0x00,0x00,0x00,0x00,0x00, // space
    0x00,0x00,0x5F,0x00,0x00,0x00, // !
    0x00,0x07,0x00,0x07,0x00,0x00, // "
    0x14,0x7F,0x14,0x7F,0x14,0x00, // #
    0x24,0x2A,0x7F,0x2A,0x12,0x00, // $
    0x23,0x13,0x08,0x64,0x62,0x00, // %
    0x36,0x49,0x56,0x20,0x50,0x00, // &
    0x00,0x00,0x07,0x00,0x00,0x00, // '
    0x00,0x1C,0x22,0x41,0x00,0x00, // (
    0x00,0x41,0x22,0x1C,0x00,0x00, // )
    0x14,0x08,0x3E,0x08,0x14,0x00, // *
    0x08,0x08,0x3E,0x08,0x08,0x00, // +
    0x00,0x50,0x30,0x00,0x00,0x00, // ,
    0x08,0x08,0x08,0x08,0x08,0x00, // -
    0x00,0x60,0x60,0x00,0x00,0x00, // .
    0x20,0x10,0x08,0x04,0x02,0x00, // /
    0x3E,0x51,0x49,0x45,0x3E,0x00, // 0
    0x00,0x42,0x7F,0x40,0x00,0x00, // 1
    0x42,0x61,0x51,0x49,0x46,0x00, // 2
    0x21,0x41,0x45,0x4B,0x31,0x00, // 3
    0x18,0x14,0x12,0x7F,0x10,0x00, // 4
    0x27,0x45,0x45,0x45,0x39,0x00, // 5
    0x3C,0x4A,0x49,0x49,0x30,0x00, // 6
    0x03,0x01,0x71,0x09,0x07,0x00, // 7
    0x36,0x49,0x49,0x49,0x36,0x00, // 8
    0x06,0x49,0x49,0x29,0x1E,0x00, // 9
    0x00,0x36,0x36,0x00,0x00,0x00, // :
    0x00,0x56,0x36,0x00,0x00,0x00, // ;
    0x08,0x14,0x22,0x41,0x00,0x00, // <
    0x14,0x14,0x14,0x14,0x14,0x00, // =
    0x00,0x41,0x22,0x14,0x08,0x00, // >
    0x02,0x01,0x51,0x09,0x06,0x00, // ?
    0x3E,0x41,0x5D,0x55,0x5E,0x00, // @
    0x7E,0x09,0x09,0x09,0x7E,0x00, // A
    0x7F,0x49,0x49,0x49,0x36,0x00, // B
    0x3E,0x41,0x41,0x41,0x22,0x00, // C
    0x7F,0x41,0x41,0x41,0x3E,0x00, // D
    0x7F,0x49,0x49,0x49,0x41,0x00, // E
    0x7F,0x09,0x09,0x09,0x01,0x00, // F
    0x3E,0x41,0x49,0x49,0x7A,0x00, // G
    0x7F,0x08,0x08,0x08,0x7F,0x00, // H
    0x00,0x41,0x7F,0x41,0x00,0x00, // I
    0x20,0x40,0x41,0x3F,0x01,0x00, // J
    0x7F,0x08,0x14,0x22,0x41,0x00, // K
    0x7F,0x40,0x40,0x40,0x40,0x00, // L
    0x7F,0x02,0x0C,0x02,0x7F,0x00, // M
    0x7F,0x04,0x08,0x10,0x7F,0x00, // N
    0x3E,0x41,0x41,0x41,0x3E,0x00, // O
    0x7F,0x09,0x09,0x09,0x06,0x00, // P
    0x3E,0x41,0x51,0x21,0x5E,0x00, // Q
    0x7F,0x09,0x19,0x29,0x46,0x00, // R
    0x26,0x49,0x49,0x49,0x32,0x00, // S
    0x01,0x01,0x7F,0x01,0x01,0x00, // T
    0x3F,0x40,0x40,0x40,0x3F,0x00, // U
    0x1F,0x20,0x40,0x20,0x1F,0x00, // V
    0x3F,0x40,0x38,0x40,0x3F,0x00, // W
    0x63,0x14,0x08,0x14,0x63,0x00, // X
    0x07,0x08,0x70,0x08,0x07,0x00, // Y
    0x61,0x51,0x49,0x45,0x43,0x00, // Z
    0x00,0x7F,0x41,0x41,0x00,0x00, // [
    0x02,0x04,0x08,0x10,0x20,0x00, // backslash
    0x00,0x41,0x41,0x7F,0x00,0x00, // ]
    0x04,0x02,0x01,0x02,0x04,0x00, // ^
    0x40,0x40,0x40,0x40,0x40,0x00, // _
    0x00,0x01,0x02,0x04,0x00,0x00, // `
    0x20,0x54,0x54,0x54,0x78,0x00, // a
    0x7F,0x48,0x44,0x44,0x38,0x00, // b
    0x38,0x44,0x44,0x44,0x20,0x00, // c
    0x38,0x44,0x44,0x48,0x7F,0x00, // d
    0x38,0x54,0x54,0x54,0x18,0x00, // e
    0x08,0x7E,0x09,0x01,0x02,0x00, // f
    0x08,0x54,0x54,0x54,0x3C,0x00, // g
    0x7F,0x08,0x04,0x04,0x78,0x00, // h
    0x00,0x48,0x7D,0x40,0x00,0x00, // i
    0x20,0x40,0x44,0x3D,0x00,0x00, // j
    0x7F,0x10,0x28,0x44,0x00,0x00, // k
    0x00,0x41,0x7F,0x40,0x00,0x00, // l
    0x7C,0x04,0x18,0x04,0x78,0x00, // m
    0x7C,0x08,0x04,0x04,0x78,0x00, // n
    0x38,0x44,0x44,0x44,0x38,0x00, // o
    0x7C,0x14,0x14,0x14,0x08,0x00, // p
    0x08,0x14,0x14,0x18,0x7C,0x00, // q
    0x7C,0x08,0x04,0x04,0x08,0x00, // r
    0x48,0x54,0x54,0x54,0x20,0x00, // s
    0x04,0x3F,0x44,0x40,0x20,0x00, // t
    0x3C,0x40,0x40,0x20,0x7C,0x00, // u
    0x1C,0x20,0x40,0x20,0x1C,0x00, // v
    0x3C,0x40,0x30,0x40,0x3C,0x00, // w
    0x44,0x28,0x10,0x28,0x44,0x00, // x
    0x0C,0x50,0x50,0x50,0x3C,0x00, // y
    0x44,0x64,0x54,0x4C,0x44,0x00, // z
    0x00,0x08,0x36,0x41,0x00,0x00, // {
    0x00,0x00,0x7F,0x00,0x00,0x00, // |
    0x00,0x41,0x36,0x08,0x00,0x00, // }
    0x08,0x04,0x08,0x10,0x08,0x00, // ~
};

// ============================================================================
// STATE
// ============================================================================

static bool initialized = false;
static spi_inst_t* spi = NULL;
static uint8_t pin_cs = 0;
static uint8_t pin_dc = 0;
static uint8_t pin_rst = 0;

// Framebuffer (128x64 = 1024 bytes, organized as 8 pages of 128 bytes)
static uint8_t framebuffer[DISPLAY_HEIGHT / 8][DISPLAY_WIDTH];

// ============================================================================
// LOW-LEVEL SPI FUNCTIONS
// ============================================================================

static inline void cs_select(void) {
    gpio_put(pin_cs, 0);
}

static inline void cs_deselect(void) {
    gpio_put(pin_cs, 1);
}

static void write_cmd(uint8_t cmd) {
    gpio_put(pin_dc, 0);  // Command mode
    cs_select();
    spi_write_blocking(spi, &cmd, 1);
    cs_deselect();
}

static void write_data(const uint8_t* data, size_t len) {
    gpio_put(pin_dc, 1);  // Data mode
    cs_select();
    spi_write_blocking(spi, data, len);
    cs_deselect();
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void display_init(const display_config_t* config) {
    // Select SPI instance
    spi = (config->spi_inst == 0) ? spi0 : spi1;
    pin_cs = config->pin_cs;
    pin_dc = config->pin_dc;
    pin_rst = config->pin_rst;

    // Initialize SPI at 10MHz
    spi_init(spi, 10 * 1000 * 1000);
    gpio_set_function(config->pin_sck, GPIO_FUNC_SPI);
    gpio_set_function(config->pin_mosi, GPIO_FUNC_SPI);

    // Initialize control pins
    gpio_init(pin_cs);
    gpio_set_dir(pin_cs, GPIO_OUT);
    gpio_put(pin_cs, 1);

    gpio_init(pin_dc);
    gpio_set_dir(pin_dc, GPIO_OUT);

    gpio_init(pin_rst);
    gpio_set_dir(pin_rst, GPIO_OUT);

    // Reset display
    gpio_put(pin_rst, 1);
    sleep_ms(10);
    gpio_put(pin_rst, 0);
    sleep_ms(10);
    gpio_put(pin_rst, 1);
    sleep_ms(10);

    // Initialize SH1106
    write_cmd(SH1106_DISPLAY_OFF);
    write_cmd(SH1106_SET_DISPLAY_CLOCK);
    write_cmd(0x80);  // Default clock
    write_cmd(SH1106_SET_MULTIPLEX);
    write_cmd(0x3F);  // 64 lines
    write_cmd(SH1106_SET_DISPLAY_OFFSET);
    write_cmd(0x00);
    write_cmd(SH1106_SET_START_LINE | 0x00);
    write_cmd(SH1106_CHARGE_PUMP);
    write_cmd(0x14);  // Enable charge pump
    write_cmd(SH1106_SEG_REMAP | 0x01);  // Flip horizontally
    write_cmd(SH1106_COM_SCAN_DEC);      // Flip vertically
    write_cmd(SH1106_SET_COM_PINS);
    write_cmd(0x12);
    write_cmd(SH1106_SET_CONTRAST);
    write_cmd(0xCF);
    write_cmd(SH1106_SET_PRECHARGE);
    write_cmd(0xF1);
    write_cmd(SH1106_SET_VCOM_DETECT);
    write_cmd(0x40);
    write_cmd(SH1106_DISPLAY_ALL_ON_RESUME);
    write_cmd(SH1106_NORMAL_DISPLAY);
    write_cmd(SH1106_DISPLAY_ON);

    // Clear framebuffer and display
    display_clear();
    display_update();

    initialized = true;
    printf("[display] Initialized SH1106 128x64 OLED\n");
}

bool display_is_initialized(void) {
    return initialized;
}

// ============================================================================
// DISPLAY CONTROL
// ============================================================================

void display_clear(void) {
    memset(framebuffer, 0, sizeof(framebuffer));
}

void display_update(void) {
    if (!initialized) return;

    for (uint8_t page = 0; page < 8; page++) {
        write_cmd(SH1106_SET_PAGE_ADDR | page);
        write_cmd(SH1106_SET_LOW_COLUMN | (SH1106_COL_OFFSET & 0x0F));
        write_cmd(SH1106_SET_HIGH_COLUMN | (SH1106_COL_OFFSET >> 4));
        write_data(framebuffer[page], DISPLAY_WIDTH);
    }
}

void display_invert(bool invert) {
    if (!initialized) return;
    write_cmd(invert ? SH1106_INVERT_DISPLAY : SH1106_NORMAL_DISPLAY);
}

void display_set_contrast(uint8_t contrast) {
    if (!initialized) return;
    write_cmd(SH1106_SET_CONTRAST);
    write_cmd(contrast);
}

// ============================================================================
// DRAWING PRIMITIVES
// ============================================================================

void display_pixel(uint8_t x, uint8_t y, bool on) {
    if (x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) return;

    uint8_t page = y / 8;
    uint8_t bit = y % 8;

    if (on) {
        framebuffer[page][x] |= (1 << bit);
    } else {
        framebuffer[page][x] &= ~(1 << bit);
    }
}

void display_hline(uint8_t x, uint8_t y, uint8_t w) {
    for (uint8_t i = 0; i < w; i++) {
        display_pixel(x + i, y, true);
    }
}

void display_vline(uint8_t x, uint8_t y, uint8_t h) {
    for (uint8_t i = 0; i < h; i++) {
        display_pixel(x, y + i, true);
    }
}

void display_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h) {
    display_hline(x, y, w);
    display_hline(x, y + h - 1, w);
    display_vline(x, y, h);
    display_vline(x + w - 1, y, h);
}

void display_fill_rect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, bool on) {
    for (uint8_t i = 0; i < h; i++) {
        for (uint8_t j = 0; j < w; j++) {
            display_pixel(x + j, y + i, on);
        }
    }
}

void display_progress_bar(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t percent) {
    if (percent > 100) percent = 100;
    display_rect(x, y, w, h);
    uint8_t fill_w = ((w - 2) * percent) / 100;
    if (fill_w > 0) {
        display_fill_rect(x + 1, y + 1, fill_w, h - 2, true);
    }
}

// ============================================================================
// TEXT RENDERING
// ============================================================================

void display_text(uint8_t x, uint8_t y, const char* text) {
    while (*text && x < DISPLAY_WIDTH - 6) {
        char c = *text++;
        const uint8_t* glyph;

        // Check for arrow characters (1-4)
        if (c >= 1 && c <= 4) {
            glyph = font_arrows[c - 1];
        } else if (c >= 32 && c <= 126) {
            glyph = &font_6x8[(c - 32) * 6];
        } else {
            glyph = &font_6x8[('?' - 32) * 6];
        }

        for (uint8_t i = 0; i < 6; i++) {
            uint8_t col = glyph[i];
            for (uint8_t j = 0; j < 8; j++) {
                display_pixel(x + i, y + j, (col >> j) & 1);
            }
        }
        x += 6;
    }
}

void display_text_large(uint8_t x, uint8_t y, const char* text) {
    // 2x scale of 6x8 font = 12x16
    while (*text && x < DISPLAY_WIDTH - 12) {
        char c = *text++;
        if (c < 32 || c > 126) c = '?';

        const uint8_t* glyph = &font_6x8[(c - 32) * 6];
        for (uint8_t i = 0; i < 6; i++) {
            uint8_t col = glyph[i];
            for (uint8_t j = 0; j < 8; j++) {
                bool on = (col >> j) & 1;
                // Draw 2x2 pixel block
                display_pixel(x + i*2, y + j*2, on);
                display_pixel(x + i*2 + 1, y + j*2, on);
                display_pixel(x + i*2, y + j*2 + 1, on);
                display_pixel(x + i*2 + 1, y + j*2 + 1, on);
            }
        }
        x += 12;
    }
}

// ============================================================================
// MARQUEE (button history with push-scroll)
// ============================================================================

#define MARQUEE_BUFFER_SIZE 128  // Characters in scroll buffer
#define MARQUEE_FADE_MS 3000     // Fade out after 3 seconds of inactivity
#define MARQUEE_SCROLL_SPEED 3   // Pixels per animation frame

static char marquee_buffer[MARQUEE_BUFFER_SIZE];
static uint16_t marquee_len = 0;           // Current string length
static int16_t marquee_offset = 0;         // Pixel offset for right-alignment
static int16_t marquee_target_offset = 0;  // Target offset (for smooth scroll)
static uint32_t marquee_last_activity = 0; // Last button press time
static uint32_t marquee_last_tick = 0;     // Last animation tick
static bool marquee_visible = false;       // Is marquee currently visible?

void display_marquee_clear(void) {
    marquee_buffer[0] = '\0';
    marquee_len = 0;
    marquee_offset = 0;
    marquee_target_offset = 0;
    marquee_visible = false;
}

void display_marquee_add(const char* text) {
    if (!text || !text[0]) return;

    size_t add_len = strlen(text);
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Add space separator if buffer has content
    size_t sep_len = (marquee_len > 0) ? 1 : 0;

    // Calculate new total length
    size_t new_total = marquee_len + sep_len + add_len;

    // If buffer would overflow, trim from the left
    if (new_total >= MARQUEE_BUFFER_SIZE) {
        size_t trim = new_total - MARQUEE_BUFFER_SIZE + 1;
        if (trim > marquee_len) trim = marquee_len;
        memmove(marquee_buffer, marquee_buffer + trim, marquee_len - trim);
        marquee_len -= trim;
    }

    // Add space separator
    if (marquee_len > 0) {
        marquee_buffer[marquee_len++] = ' ';
    }

    // Add new text
    memcpy(marquee_buffer + marquee_len, text, add_len);
    marquee_len += add_len;
    marquee_buffer[marquee_len] = '\0';

    // Calculate target offset to show text right-aligned
    // (text grows from right edge, pushes left)
    uint16_t text_width = marquee_len * 6;
    if (text_width > DISPLAY_WIDTH) {
        marquee_target_offset = text_width - DISPLAY_WIDTH;
    } else {
        marquee_target_offset = 0;
    }

    marquee_last_activity = now;
    marquee_visible = true;
}

bool display_marquee_tick(void) {
    if (!marquee_visible || marquee_len == 0) return false;

    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Check for fade timeout
    if (now - marquee_last_activity > MARQUEE_FADE_MS) {
        display_marquee_clear();
        return true;  // Need to clear display
    }

    // Animate scroll towards target
    if (marquee_offset != marquee_target_offset) {
        if (now - marquee_last_tick < 20) return false;  // ~50fps
        marquee_last_tick = now;

        if (marquee_offset < marquee_target_offset) {
            marquee_offset += MARQUEE_SCROLL_SPEED;
            if (marquee_offset > marquee_target_offset) {
                marquee_offset = marquee_target_offset;
            }
        }
        return true;
    }

    return false;
}

void display_marquee_render(uint8_t y) {
    // Clear marquee line area
    display_fill_rect(0, y, DISPLAY_WIDTH, 8, false);

    if (!marquee_visible || marquee_len == 0) return;

    // Calculate text width and starting position
    uint16_t text_width = marquee_len * 6;
    int16_t start_x;

    if (text_width <= DISPLAY_WIDTH) {
        // Text fits - right align it
        start_x = DISPLAY_WIDTH - text_width;
    } else {
        // Text overflows - scroll offset applies
        start_x = -marquee_offset;
    }

    // Render visible portion of text
    int16_t x = start_x;
    for (uint16_t i = 0; i < marquee_len; i++) {
        if (x >= DISPLAY_WIDTH) break;  // Past right edge
        if (x > -6) {  // At least partially visible
            char c = marquee_buffer[i];
            const uint8_t* glyph;

            // Check for arrow characters (1-4)
            if (c >= 1 && c <= 4) {
                glyph = font_arrows[c - 1];
            } else if (c >= 32 && c <= 126) {
                glyph = &font_6x8[(c - 32) * 6];
            } else {
                glyph = &font_6x8[('?' - 32) * 6];
            }

            for (int8_t col = 0; col < 6; col++) {
                int16_t px = x + col;
                if (px >= 0 && px < DISPLAY_WIDTH) {
                    uint8_t data = glyph[col];
                    for (uint8_t row = 0; row < 8; row++) {
                        if ((data >> row) & 1) {
                            display_pixel(px, y + row, true);
                        }
                    }
                }
            }
        }
        x += 6;
    }
}
