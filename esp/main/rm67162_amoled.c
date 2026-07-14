// RM67162 AMOLED driver for LilyGo T-Display S3 AMOLED *Plus* (240x536).
// This "Plus" board is the RM67162_AMOLED_SPI variant: single-line SPI with a
// DC pin (NOT QSPI), and the panel power rail is gated by a PMIC-enable GPIO
// (IO38) that MUST be driven high or the panel stays dark. Pins/behavior taken
// from LilyGo-AMOLED-Series (BOARD_AMOLED_191_SPI). Only built for the board.
#include "rm67162_amoled.h"

#ifdef BOARD_LILYGO_TDISPLAY_S3_AMOLED

#include <string.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

// Pin map — LilyGo T-Display S3 AMOLED Plus (single-SPI RM67162)
#define PIN_SCK     47
#define PIN_MOSI    18   // display.d0
#define PIN_DC      7    // display.d1 (data/command)
#define PIN_CS      6
#define PIN_RST     17
#define PIN_PMIC_EN 38   // PMICEnPins — drive HIGH to power the AMOLED rail

#define AMOLED_SPI_HOST  SPI2_HOST
#define AMOLED_SPI_HZ    75000000   // LilyGo runs this panel at 75MHz

// Band buffer for fills: 40 rows at a time (240*40*2 = 19200 bytes).
#define BAND_ROWS 40

static const char* TAG = "amoled";
static esp_lcd_panel_io_handle_t s_io = NULL;
static uint16_t* s_band = NULL;
static uint16_t* s_band2 = NULL;  // second buffer for pipelined blits
static int s_face_shift = 0;      // physical-centering shift, in panel pixels

// Shift the blitted image along the panel's long axis (positive/negative in
// panel pixels). Compensates for the module's off-center active area (the
// touch-circle strip on one end of the glass).
void amoled_set_shift(int panel_px) { s_face_shift = panel_px; }
static SemaphoreHandle_t s_done = NULL;   // signalled when a tx_color completes

static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t* ed, void* ctx)
{
    (void)io; (void)ed; (void)ctx;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &hp);
    return hp == pdTRUE;
}

// Push one color rect and block until its DMA finishes, so the source buffer
// can be safely reused for the next rect (prevents mid-transfer corruption).
static void tx_color_sync(uint8_t cmd, const void* data, size_t len)
{
    esp_lcd_panel_io_tx_color(s_io, cmd, data, len);
    xSemaphoreTake(s_done, portMAX_DELAY);
}

typedef struct {
    uint8_t cmd;
    uint8_t data[2];
    uint8_t len;   // low 7 bits = param bytes; 0x80 = delay 120ms after
} lcd_cmd_t;

// RM67162 single-SPI init sequence (exact, from LilyGo-AMOLED-Series
// rm67162_spi_cmd). The APGE page setup + 0x53=0x20 (brightness-control enable)
// are required or the panel stays dark even with brightness set.
static const lcd_cmd_t s_init[] = {
    {0xFE, {0x04}, 0x01}, // SET APGE3
    {0x6A, {0x00}, 0x01},
    {0xFE, {0x05}, 0x01}, // SET APGE4
    {0xFE, {0x07}, 0x01}, // SET APGE6
    {0x07, {0x4F}, 0x01},
    {0xFE, {0x01}, 0x01}, // SET APGE0
    {0x2A, {0x02}, 0x01},
    {0x2B, {0x73}, 0x01},
    {0xFE, {0x0A}, 0x01}, // SET APGE9
    {0x29, {0x10}, 0x01},
    {0xFE, {0x00}, 0x01},
    {0x51, {0xFF}, 0x01}, // brightness max
    {0x53, {0x20}, 0x01}, // brightness control enable (critical)
    {0x35, {0x00}, 0x01}, // TE on
    {0x3A, {0x75}, 0x01}, // pixel format 16bpp (SPI)
    {0xC4, {0x80}, 0x01},
    {0x11, {0x00}, 0x80}, // sleep out (+120ms)
    {0x29, {0x00}, 0x80}, // display on (+120ms)
};

static void set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint8_t col[4] = { x1 >> 8, x1 & 0xFF, x2 >> 8, x2 & 0xFF };
    uint8_t row[4] = { y1 >> 8, y1 & 0xFF, y2 >> 8, y2 & 0xFF };
    esp_lcd_panel_io_tx_param(s_io, 0x2A, col, 4);
    esp_lcd_panel_io_tx_param(s_io, 0x2B, row, 4);
}

void amoled_brightness(uint8_t level)
{
    if (!s_io) return;
    uint8_t v = level;
    esp_lcd_panel_io_tx_param(s_io, 0x51, &v, 1);
}

void amoled_init(void)
{
    // Power the panel rail (PMIC enable) + configure RST. Without PMIC_EN high
    // the AMOLED has no power and stays black regardless of SPI activity.
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_RST) | (1ULL << PIN_PMIC_EN),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(PIN_PMIC_EN, 1);   // AMOLED power ON

    // Reset pulse.
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = AMOLED_W * BAND_ROWS * 2 + 64,
    };
    esp_err_t err = spi_bus_initialize(AMOLED_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) { ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err)); return; }

    // Counting (not binary): with pipelined blits two transfers can complete
    // back-to-back; a binary semaphore would drop the second give and deadlock.
    s_done = xSemaphoreCreateCounting(16, 0);
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = PIN_CS,
        .dc_gpio_num = PIN_DC,
        .spi_mode = 0,
        .pclk_hz = AMOLED_SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_done,
    };
    err = esp_lcd_new_panel_io_spi(AMOLED_SPI_HOST, &io_cfg, &s_io);
    if (err != ESP_OK) { ESP_LOGE(TAG, "new_panel_io_spi: %s", esp_err_to_name(err)); s_io = NULL; return; }

    s_band = heap_caps_malloc(AMOLED_W * BAND_ROWS * 2, MALLOC_CAP_DMA);
    s_band2 = heap_caps_malloc(AMOLED_W * BAND_ROWS * 2, MALLOC_CAP_DMA);
    if (!s_band || !s_band2) { ESP_LOGE(TAG, "band alloc failed"); return; }

    for (size_t i = 0; i < sizeof(s_init) / sizeof(s_init[0]); i++) {
        esp_lcd_panel_io_tx_param(s_io, s_init[i].cmd, s_init[i].data, s_init[i].len & 0x7F);
        if (s_init[i].len & 0x80) vTaskDelay(pdMS_TO_TICKS(120));
    }
    ESP_LOGI(TAG, "RM67162 SPI init done (%dx%d)", AMOLED_W, AMOLED_H);
}

void amoled_fill(uint16_t rgb565)
{
    if (!s_io || !s_band) return;
    // Panel wants big-endian RGB565; swap so high byte ships first.
    uint16_t sw = (uint16_t)((rgb565 >> 8) | (rgb565 << 8));
    for (int i = 0; i < AMOLED_W * BAND_ROWS; i++) s_band[i] = sw;

    for (uint16_t y = 0; y < AMOLED_H; y += BAND_ROWS) {
        uint16_t rows = (y + BAND_ROWS <= AMOLED_H) ? BAND_ROWS : (AMOLED_H - y);
        set_window(0, y, AMOLED_W - 1, y + rows - 1);
        tx_color_sync(0x2C, s_band, (size_t)AMOLED_W * rows * 2);
    }
}

void amoled_push(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t* data)
{
    if (!s_io) return;
    set_window(x, y, x + w - 1, y + h - 1);
    tx_color_sync(0x2C, data, (size_t)w * h * 2);
}

void amoled_blit_mono(const uint8_t* mono, int w, int h, uint16_t color)
{
    if (!s_io || !s_band) return;
    // Landscape: the w-wide eyes canvas spans the panel's AMOLED_H rows, the
    // h-tall canvas spans the panel's AMOLED_W columns. Precompute nearest-
    // neighbor maps so the per-pixel inner loop is just LUT lookups + a bit test.
    static uint16_t xlut[AMOLED_H];  // panel row -> eyes col
    static uint16_t ylut[AMOLED_W];  // panel col -> eyes row
    static int lut_w = 0, lut_h = 0;
    if (lut_w != w || lut_h != h) {
        for (int py = 0; py < AMOLED_H; py++) xlut[py] = (uint16_t)((py * w) / AMOLED_H);
        for (int px = 0; px < AMOLED_W; px++) ylut[px] = (uint16_t)((px * h) / AMOLED_W);
        lut_w = w; lut_h = h;
    }
    uint16_t on = (uint16_t)((color >> 8) | (color << 8));  // byte-swapped for panel

    for (uint16_t y = 0; y < AMOLED_H; y += BAND_ROWS) {
        uint16_t rows = (y + BAND_ROWS <= AMOLED_H) ? BAND_ROWS : (AMOLED_H - y);
        for (uint16_t r = 0; r < rows; r++) {
            uint16_t ex = xlut[y + r];
            uint16_t* out = &s_band[r * AMOLED_W];
            for (uint16_t px = 0; px < AMOLED_W; px++) {
                uint32_t idx = (uint32_t)ylut[px] * w + ex;   // eyes pixel index
                out[px] = (mono[idx >> 3] & (1u << (idx & 7))) ? on : 0x0000;
            }
        }
        set_window(0, y, AMOLED_W - 1, y + rows - 1);
        tx_color_sync(0x2C, s_band, (size_t)AMOLED_W * rows * 2);
    }
}

void amoled_blit_idx8(const uint8_t* fb, int w, int h,
                      uint16_t main565, uint16_t accent565)
{
    if (!s_io || !s_band) return;
    // Geometry LUTs (panel row -> canvas col, panel col -> canvas row).
    static uint16_t xlut[AMOLED_H];
    static uint16_t ylut[AMOLED_W];
    static int lut_w = 0, lut_h = 0;
    if (lut_w != w || lut_h != h) {
        for (int py = 0; py < AMOLED_H; py++) xlut[py] = (uint16_t)((py * w) / AMOLED_H);
        for (int px = 0; px < AMOLED_W; px++) ylut[px] = (uint16_t)((px * h) / AMOLED_W);
        lut_w = w; lut_h = h;
    }
    // Color LUT: index = main_count*5 + accent_count (each 0..4 of 4 samples).
    // Blends black -> main/accent proportionally, output byte-swapped RGB565.
    static uint16_t clut[25];
    static uint16_t cm = 0, ca = 0;
    static bool clut_ready = false;
    if (!clut_ready || cm != main565 || ca != accent565) {
        int mr = (main565 >> 11) & 31, mg = (main565 >> 5) & 63, mb = main565 & 31;
        int ar = (accent565 >> 11) & 31, ag = (accent565 >> 5) & 63, ab = accent565 & 31;
        for (int cw = 0; cw <= 4; cw++) {
            for (int cr = 0; cr <= 4 - cw; cr++) {
                int r = (cw * mr + cr * ar) / 4;
                int g = (cw * mg + cr * ag) / 4;
                int b = (cw * mb + cr * ab) / 4;
                uint16_t v = (uint16_t)((r << 11) | (g << 5) | b);
                clut[cw * 5 + cr] = (uint16_t)((v >> 8) | (v << 8));
            }
        }
        cm = main565; ca = accent565; clut_ready = true;
    }

    // Pipelined: one window for the whole frame, first band as RAMWR (0x2C),
    // the rest as memory-write-continue (0x3C). While one band buffer is in
    // DMA flight, the other is being filled — compute overlaps transfer.
    set_window(0, 0, AMOLED_W - 1, AMOLED_H - 1);
    uint16_t* bufs[2] = { s_band, s_band2 };
    int cur = 0, pending = 0;
    bool first = true;
    for (uint16_t y = 0; y < AMOLED_H; y += BAND_ROWS) {
        uint16_t rows = (y + BAND_ROWS <= AMOLED_H) ? BAND_ROWS : (AMOLED_H - y);
        uint16_t* band = bufs[cur];
        int cshift = (s_face_shift * w) / AMOLED_H;   // panel px -> canvas px
        for (uint16_t r = 0; r < rows; r++) {
            int ex = xlut[y + r] + cshift;
            if (ex < 0 || ex >= w) {                  // shifted past the canvas: black row
                memset(&band[r * AMOLED_W], 0, AMOLED_W * 2);
                continue;
            }
            int ex2 = (ex + 1 < w) ? ex + 1 : ex;
            // Canvas is column-major (x*h + y): both sampled columns are
            // walked sequentially as px advances — cache-friendly in PSRAM.
            const uint8_t* c0 = fb + (size_t)ex  * h;
            const uint8_t* c1 = fb + (size_t)ex2 * h;
            uint16_t* out = &band[r * AMOLED_W];
            for (uint16_t px = 0; px < AMOLED_W; px++) {
                int ey = ylut[px];
                int ey2 = (ey + 1 < h) ? ey + 1 : ey;
                uint8_t v0 = c0[ey];
                uint8_t v1 = c1[ey];
                uint8_t v2 = c0[ey2];
                uint8_t v3 = c1[ey2];
                int cw = (v0 == 1) + (v1 == 1) + (v2 == 1) + (v3 == 1);
                int cr = (v0 == 2) + (v1 == 2) + (v2 == 2) + (v3 == 2);
                out[px] = clut[cw * 5 + cr];
            }
        }
        if (pending == 2) { xSemaphoreTake(s_done, portMAX_DELAY); pending--; }
        esp_lcd_panel_io_tx_color(s_io, first ? 0x2C : 0x3C, band,
                                  (size_t)AMOLED_W * rows * 2);
        pending++; first = false; cur ^= 1;
    }
    while (pending) { xSemaphoreTake(s_done, portMAX_DELAY); pending--; }
}

#endif // BOARD_LILYGO_TDISPLAY_S3_AMOLED
