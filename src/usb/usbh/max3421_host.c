// max3421_host.c - MAX3421E SPI USB Host initialization
//
// Provides initialization, probe, and diagnostics for the MAX3421E.
// Also overrides TinyUSB's BSP tuh_max3421_spi_xfer_api() via --wrap
// to fix a bug where family.c hardcodes spi0 instead of MAX3421_SPI.
//
// Pin configuration is passed via compile definitions from CMakeLists.txt:
//   MAX3421_SPI, MAX3421_CS_PIN, MAX3421_INTR_PIN,
//   MAX3421_SCK_PIN, MAX3421_MOSI_PIN, MAX3421_MISO_PIN

#include "tusb.h"

#if CFG_TUH_MAX3421

#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <stdio.h>

// ISR defined in TinyUSB family.c BSP
extern void max3421_int_handler(uint gpio, uint32_t event_mask);

// Detection status (queryable after init)
static bool max3421_detected = false;
static uint8_t max3421_revision = 0;

bool max3421_is_detected(void) { return max3421_detected; }
uint8_t max3421_get_revision(void) { return max3421_revision; }

// SPI register write helper
// MAX3421E command byte: reg[4:0] << 3 | dir[1] (dir=1 write, bit 1)
static void max3421_reg_write(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)((reg << 3) | 0x02), val };

    gpio_put(MAX3421_CS_PIN, false);
    spi_write_blocking(MAX3421_SPI, tx, 2);
    gpio_put(MAX3421_CS_PIN, true);
}

// SPI register read helper
// MAX3421E command byte: reg[4:0] << 3 | dir[1] (dir=0 read)
static uint8_t max3421_reg_read(uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(reg << 3), 0x00 };
    uint8_t rx[2] = { 0, 0 };

    gpio_put(MAX3421_CS_PIN, false);
    spi_write_read_blocking(MAX3421_SPI, tx, rx, 2);
    gpio_put(MAX3421_CS_PIN, true);

    return rx[1];
}

// MAX3421E boots in half-duplex SPI mode where MISO only shows interrupt
// status. Must set FDUPSPI bit in PINCTL to enable full-duplex before reads.
#define MAX3421_REG_PINCTL   17
#define MAX3421_REG_REVISION 18
#define MAX3421_FDUPSPI      0x10

static bool max3421_probe(void)
{
    // Switch to full-duplex SPI (write works in both modes)
    max3421_reg_write(MAX3421_REG_PINCTL, MAX3421_FDUPSPI);

    uint8_t r1 = max3421_reg_read(MAX3421_REG_REVISION);
    uint8_t r2 = max3421_reg_read(MAX3421_REG_REVISION);
    uint8_t r3 = max3421_reg_read(MAX3421_REG_REVISION);

    printf("[max3421] Probe: rev reads 0x%02X 0x%02X 0x%02X\n", r1, r2, r3);

    // All three must match and upper nibble must be 0x1 (MAX3421E chip ID)
    if (r1 != r2 || r2 != r3) return false;
    if ((r1 & 0xF0) != 0x10) return false;

    return true;
}

bool max3421_host_init(void)
{
    printf("[max3421] Initializing SPI host\n");
    printf("[max3421] SCK=%d MOSI=%d MISO=%d CS=%d INT=%d\n",
           MAX3421_SCK_PIN, MAX3421_MOSI_PIN, MAX3421_MISO_PIN,
           MAX3421_CS_PIN, MAX3421_INTR_PIN);

    // CS pin - manual control, active low (deassert before SPI init)
    gpio_init(MAX3421_CS_PIN);
    gpio_set_dir(MAX3421_CS_PIN, GPIO_OUT);
    gpio_put(MAX3421_CS_PIN, true);

    // INT pin - input with pull-up, active low
    // Interrupt is NOT enabled here — enabled after tusb_init() configures the chip
    gpio_init(MAX3421_INTR_PIN);
    gpio_set_dir(MAX3421_INTR_PIN, GPIO_IN);
    gpio_pull_up(MAX3421_INTR_PIN);

    // SPI init
    spi_init(MAX3421_SPI, 4 * 1000 * 1000);
    gpio_set_function(MAX3421_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MAX3421_MOSI_PIN, GPIO_FUNC_SPI);
    gpio_set_function(MAX3421_MISO_PIN, GPIO_FUNC_SPI);

    // Verify MAX3421E is present via write-readback test
    if (!max3421_probe()) {
        printf("[max3421] ERROR: chip not detected — check wiring\n");
        spi_deinit(MAX3421_SPI);
        max3421_detected = false;
        return false;
    }

    max3421_revision = max3421_reg_read(18);
    printf("[max3421] Chip detected, revision: 0x%02X\n", max3421_revision);
    max3421_detected = true;
    printf("[max3421] Initialization complete\n");
    return true;
}

void max3421_host_enable_int(void)
{
    // Register falling edge interrupt — call AFTER tusb_init() so the
    // MAX3421E INT output is properly configured and won't float
    gpio_set_irq_enabled_with_callback(MAX3421_INTR_PIN,
                                       GPIO_IRQ_EDGE_FALL,
                                       true,
                                       max3421_int_handler);
    printf("[max3421] INT enabled on GPIO %d\n", MAX3421_INTR_PIN);
}

// ============================================================================
// TinyUSB BSP SPI transfer override (--wrap)
// ============================================================================
// TinyUSB's rp2040 family.c has a bug: tuh_max3421_spi_xfer_api() hardcodes
// spi0 in the write+read path instead of using MAX3421_SPI. Since we can't
// modify the submodule, we use --wrap to intercept and provide the correct
// implementation. The linker flag is set in CMakeLists.txt.

bool __wrap_tuh_max3421_spi_xfer_api(uint8_t rhport, uint8_t const* tx_buf,
                                      uint8_t* rx_buf, size_t xfer_bytes)
{
    (void) rhport;

    if (tx_buf == NULL && rx_buf == NULL) {
        return false;
    }

    int ret;

    if (tx_buf == NULL) {
        ret = spi_read_blocking(MAX3421_SPI, 0, rx_buf, xfer_bytes);
    } else if (rx_buf == NULL) {
        ret = spi_write_blocking(MAX3421_SPI, tx_buf, xfer_bytes);
    } else {
        ret = spi_write_read_blocking(MAX3421_SPI, tx_buf, rx_buf, xfer_bytes);
    }

    return ret == (int) xfer_bytes;
}

// Read key registers for diagnostics (callable from CDC command handler)
// HRSL bit 7 (JSTATUS) = full-speed device connected
// HRSL bit 6 (KSTATUS) = low-speed device connected
void max3421_get_diag(uint8_t *out_hirq, uint8_t *out_mode,
                      uint8_t *out_hrsl, uint8_t *out_int_pin)
{
    if (!max3421_detected) {
        *out_hirq = 0;
        *out_mode = 0;
        *out_hrsl = 0;
        *out_int_pin = 0;
        return;
    }
    *out_hirq = max3421_reg_read(25);   // HIRQ
    *out_mode = max3421_reg_read(27);   // MODE
    *out_hrsl = max3421_reg_read(31);   // HRSL
    *out_int_pin = gpio_get(MAX3421_INTR_PIN) ? 1 : 0;
}

#endif // CFG_TUH_MAX3421
