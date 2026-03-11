// max3421_host_esp32.c - MAX3421E USB Host for ESP32-S3
//
// Implements TinyUSB MAX3421E platform callbacks using ESP-IDF SPI driver.
// Used with the Adafruit MAX3421E FeatherWing (#5858) on Feather ESP32-S3.
//
// Uses spi_device_polling_transmit() with pre-acquired bus for ISR-safe
// SPI transfers (TinyUSB calls tuh_max3421_spi_xfer_api from ISR context).
//
// Feather ESP32-S3 pin mapping:
//   SPI2: SCK=GPIO36, MOSI=GPIO35, MISO=GPIO37
//   CS:   D10 = GPIO10
//   IRQ:  D9  = GPIO9 (active low, falling edge)

#include "tusb.h"

#if CFG_TUH_MAX3421

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include <stdio.h>

// ============================================================================
// PIN DEFINITIONS (Feather ESP32-S3 + MAX3421E FeatherWing)
// ============================================================================

#define SPI_HOST_ID   SPI2_HOST
#define SPI_CLK_HZ    8000000   // 8MHz

#define PIN_SCK       36
#define PIN_MOSI      35
#define PIN_MISO      37
#define PIN_CS        10        // D10
#define PIN_INT       9         // D9 (active low)

// ============================================================================
// STATE
// ============================================================================

static spi_device_handle_t spi_dev;
static volatile bool int_enabled = false;

// ISR → main loop deferral flag
static volatile bool int_pending = false;

// Debug counters
static volatile uint32_t isr_count = 0;
static volatile uint32_t missed_edge_count = 0;

// Detection status
static bool max3421_detected = false;
static uint8_t max3421_revision = 0;

bool max3421_is_detected(void) { return max3421_detected; }
uint8_t max3421_get_revision(void) { return max3421_revision; }

// ============================================================================
// CS CONTROL (direct GPIO register, ISR-safe)
// ============================================================================

static inline void cs_assert(void)   { gpio_set_level(PIN_CS, 0); }
static inline void cs_deassert(void) { gpio_set_level(PIN_CS, 1); }

// ============================================================================
// SPI TRANSFER (ISR-safe with pre-acquired bus)
// ============================================================================

static void spi_xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(spi_dev, &t);
}

// ============================================================================
// TinyUSB PLATFORM CALLBACKS
// ============================================================================

void tuh_max3421_spi_cs_api(uint8_t rhport, bool active)
{
    (void)rhport;
    if (active) cs_assert(); else cs_deassert();
}

bool tuh_max3421_spi_xfer_api(uint8_t rhport, uint8_t const *tx_buf,
                               uint8_t *rx_buf, size_t xfer_bytes)
{
    (void)rhport;
    spi_xfer(tx_buf, rx_buf, xfer_bytes);
    return true;
}

// INT enable/disable — TinyUSB calls this to gate interrupts during SPI xfer.
// When re-enabling, checks for missed edge (INT already active = no falling edge).
// Re-entrancy guard prevents infinite recursion from spi_unlock→int_api→hcd_int_handler→spi_lock/unlock→int_api.
void tuh_max3421_int_api(uint8_t rhport, bool enabled)
{
    (void)rhport;
    if (enabled && !int_enabled) {
        gpio_intr_enable(PIN_INT);
        int_enabled = true;

        // Missed-edge check: if INT is already active (low), no falling edge
        // will occur. Set flag for max3421_poll() to handle safely.
        // Cannot call hcd_int_handler here — we're inside spi_unlock,
        // mutex is still held, recursive call would deadlock.
        if (gpio_get_level(PIN_INT) == 0) {
            missed_edge_count++;
            int_pending = true;
        }
    } else if (!enabled && int_enabled) {
        gpio_intr_disable(PIN_INT);
        int_enabled = false;
    }
}

// ============================================================================
// GPIO INTERRUPT HANDLER
// ============================================================================

// GPIO ISR: just set flag, defer hcd_int_handler to main loop.
// On ESP32, hcd_int_handler and all TinyUSB code is in flash (not IRAM),
// so calling it from ISR context crashes on flash cache miss.
static void IRAM_ATTR max3421_int_isr(void *arg)
{
    (void)arg;
    isr_count++;
    int_pending = true;
}

// Called from main loop to process deferred MAX3421E interrupt
void max3421_poll(void)
{
    if (!max3421_detected) return;

    bool need_process = int_pending;
    if (need_process) {
        int_pending = false;
    }

    // Also check for missed edges (INT already low but no ISR fired)
    if (!need_process && int_enabled && gpio_get_level(PIN_INT) == 0) {
        need_process = true;
    }

    if (need_process) {
        hcd_int_handler(1, false);
    }
}

// ============================================================================
// SPI REGISTER ACCESS (for probe only — before interrupts are enabled)
// ============================================================================

static void max3421_reg_write(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)((reg << 3) | 0x02), val };
    cs_assert();
    spi_xfer(tx, NULL, 2);
    cs_deassert();
}

static uint8_t max3421_reg_read(uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(reg << 3), 0x00 };
    uint8_t rx[2] = { 0, 0 };
    cs_assert();
    spi_xfer(tx, rx, 2);
    cs_deassert();
    return rx[1];
}

#define MAX3421_REG_PINCTL   17
#define MAX3421_REG_REVISION 18
#define MAX3421_FDUPSPI      0x10

static bool max3421_probe(void)
{
    max3421_reg_write(MAX3421_REG_PINCTL, MAX3421_FDUPSPI);

    uint8_t r1 = max3421_reg_read(MAX3421_REG_REVISION);
    uint8_t r2 = max3421_reg_read(MAX3421_REG_REVISION);
    uint8_t r3 = max3421_reg_read(MAX3421_REG_REVISION);

    printf("[max3421] Probe: rev 0x%02X 0x%02X 0x%02X\n", r1, r2, r3);

    if (r1 != r2 || r2 != r3) return false;
    if ((r1 & 0xF0) != 0x10) return false;

    return true;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

bool max3421_host_init(void)
{
    // Idempotent — return success if already initialized
    if (max3421_detected) return true;

    printf("[max3421] Init SPI host (ESP32-S3)\n");

    // CS pin: output, deasserted (high) — managed manually, not by SPI driver
    gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cs_cfg);
    cs_deassert();

    // Initialize SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    esp_err_t err = spi_bus_initialize(SPI_HOST_ID, &bus_cfg, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        printf("[max3421] SPI bus init failed: %d\n", err);
        return false;
    }

    // Add SPI device (no CS — we manage it manually for ISR safety)
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_CLK_HZ,
        .mode = 0,                     // CPOL=0, CPHA=0
        .spics_io_num = -1,            // Manual CS
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };
    // Full-duplex: MAX3421E needs simultaneous TX/RX
    dev_cfg.flags = 0;

    err = spi_bus_add_device(SPI_HOST_ID, &dev_cfg, &spi_dev);
    if (err != ESP_OK) {
        printf("[max3421] SPI device add failed: %d\n", err);
        return false;
    }

    // Pre-acquire bus for ISR-safe polling transmit
    err = spi_device_acquire_bus(spi_dev, portMAX_DELAY);
    if (err != ESP_OK) {
        printf("[max3421] SPI bus acquire failed: %d\n", err);
        return false;
    }

    // Probe MAX3421E
    if (!max3421_probe()) {
        printf("[max3421] ERROR: chip not detected\n");
        max3421_detected = false;
        return false;
    }

    max3421_revision = max3421_reg_read(MAX3421_REG_REVISION);
    printf("[max3421] Chip rev 0x%02X\n", max3421_revision);
    max3421_detected = true;

    // INT pin: input, active low, falling edge — configure but don't enable yet
    gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << PIN_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,   // Don't enable yet
    };
    gpio_config(&int_cfg);

    // Install ISR service (may already be installed by other drivers)
    esp_err_t isr_err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        printf("[max3421] ISR service install failed: %d\n", isr_err);
        return false;
    }
    gpio_isr_handler_add(PIN_INT, max3421_int_isr, NULL);

    return true;
}

void max3421_host_enable_int(void)
{
    gpio_set_intr_type(PIN_INT, GPIO_INTR_NEGEDGE);
    gpio_intr_enable(PIN_INT);
    int_enabled = true;

    int raw = gpio_get_level(PIN_INT);
    printf("[max3421] INT on, pin=%d\n", raw);

    // If INT already active (low), defer to max3421_poll() in main loop.
    // Cannot call hcd_int_handler directly here — spi_unlock would call
    // tuh_max3421_int_api(true) which does missed-edge check while mutex held.
    if (raw == 0) {
        int_pending = true;
    }
}

// ============================================================================
// DIAGNOSTICS
// ============================================================================

void max3421_get_diag(uint8_t *out_hirq, uint8_t *out_mode,
                      uint8_t *out_hrsl, uint8_t *out_int_pin)
{
    if (!max3421_detected) {
        *out_hirq = 0; *out_mode = 0; *out_hrsl = 0; *out_int_pin = 0;
        return;
    }
    // Disable interrupts during SPI reads to prevent bus contention
    bool was_enabled = int_enabled;
    if (was_enabled) gpio_intr_disable(PIN_INT);
    *out_hirq = max3421_reg_read(25);   // HIRQ
    *out_mode = max3421_reg_read(27);   // MODE
    *out_hrsl = max3421_reg_read(31);   // HRSL
    *out_int_pin = (gpio_get_level(PIN_INT) == 0) ? 1 : 0;
    if (was_enabled) gpio_intr_enable(PIN_INT);
}

void max3421_print_diag(void)
{
    int pin = gpio_get_level(PIN_INT);
    printf("[m] isr=%u me=%u pin=%d\n",
           (unsigned)isr_count, (unsigned)missed_edge_count, pin);
}

#endif // CFG_TUH_MAX3421
