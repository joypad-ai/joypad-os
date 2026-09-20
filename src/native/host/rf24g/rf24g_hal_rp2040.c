// rf24g_hal_rp2040.c - RP2040/RP2350 HAL for the rf24g nRF24L01+ driver
//
// spi0 (the wiring app.h picks -- SCK=6/MOSI=7/MISO=0 is spi0's native
// pinout, a straight hardware peripheral rather than bit-banged), plain-GPIO
// CE/CSN/IRQ, and one claimed hardware_alarm for the dwell scheduler. Every
// pico-sdk call this driver needs lives in this one file -- see rf24g_hal.h.
//
// Every function below except rf24g_hal_init() and rf24g_hal_irq_attach() is
// reachable from rf24g_host.c's radio ISR / dwell-alarm chain, so it carries
// __not_in_flash_func -- see rf24g_host.c's file header for why this is
// defense in depth, not fault avoidance. One known, accepted exception:
// rf24g_hal_alarm_schedule() below calls flash-resident time_us_64() and
// hardware_alarm_set_target() (confirmed by disassembly on both `pico` and
// `pico2` builds) despite being __not_in_flash_func itself -- those two
// callees are veneers into flash and are left that way, same as the
// pico-sdk IRQ entry dispatchers that dispatch into this file's trampolines
// in the first place.

#if defined(PICO_PLATFORM) || defined(PICO_RP2040) || defined(PICO_RP2350)

#include "rf24g_hal.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/time.h"

// Never schedule the alarm for "now" -- see rf24g_hal_alarm_schedule(). A few
// microseconds is comfortably longer than the register write that arms it,
// and negligible against the 10070us dwell it paces.
#define RF24G_ALARM_MIN_US      8
#define RF24G_ALARM_MAX_TRIES   8
#define RF24G_ALARM_FALLBACK_US 1000

static uint8_t s_pin_csn = 0xFF;
static uint8_t s_pin_ce  = 0xFF;
static uint8_t s_pin_irq = 0xFF;
static int8_t  s_alarm_num = -1;

static rf24g_hal_irq_cb_t   s_irq_cb   = NULL;
static rf24g_hal_alarm_cb_t s_alarm_cb = NULL;

static void __not_in_flash_func(gpio_irq_trampoline)(uint gpio, uint32_t events)
{
    (void)events;
    if (gpio == s_pin_irq && s_irq_cb) s_irq_cb();
}

static void __not_in_flash_func(alarm_trampoline)(uint alarm_num)
{
    (void)alarm_num;
    if (s_alarm_cb) s_alarm_cb();
}

void rf24g_hal_init(uint8_t sck, uint8_t mosi, uint8_t miso,
                     uint8_t csn, uint8_t ce, uint8_t irq)
{
    s_pin_csn = csn;
    s_pin_ce  = ce;
    s_pin_irq = irq;

    // 8 MHz -- the nRF24L01+ tolerates up to 10, per the app.h pin comment.
    spi_init(spi0, 8 * 1000 * 1000);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(sck,  GPIO_FUNC_SPI);
    gpio_set_function(mosi, GPIO_FUNC_SPI);
    gpio_set_function(miso, GPIO_FUNC_SPI);

    // CSN: plain GPIO, idle high. The nRF24 command byte is framed by CSN
    // going low, not by a hardware chip-select signal -- same pattern as
    // w5500.c's cs_low()/cs_high().
    gpio_init(csn);
    gpio_set_dir(csn, GPIO_OUT);
    gpio_put(csn, 1);

    // CE: plain GPIO, starts low (standby). Held high continuously while
    // listening, dropped briefly around every channel change -- see
    // rf24g_host.c's radio_retune().
    gpio_init(ce);
    gpio_set_dir(ce, GPIO_OUT);
    gpio_put(ce, 0);

    // IRQ: input, active low. Most nRF24L01+ breakout boards carry their own
    // pull-up on this line; an internal one costs nothing and protects an
    // unpopulated board.
    gpio_init(irq);
    gpio_set_dir(irq, GPIO_IN);
    gpio_pull_up(irq);

    int alarm = hardware_alarm_claim_unused(true);
    s_alarm_num = (int8_t)alarm;
    hardware_alarm_set_callback((uint)s_alarm_num, alarm_trampoline);
}

void __not_in_flash_func(rf24g_hal_ce)(bool level)
{
    gpio_put(s_pin_ce, level);
}

void __not_in_flash_func(rf24g_hal_spi_xfer)(const uint8_t* tx, uint8_t* rx, size_t len)
{
    gpio_put(s_pin_csn, 0);
    if (tx && rx)      spi_write_read_blocking(spi0, tx, rx, len);
    else if (rx)       spi_read_blocking(spi0, 0xFF, rx, len);
    else if (tx)       spi_write_blocking(spi0, tx, len);
    gpio_put(s_pin_csn, 1);
}

void rf24g_hal_irq_attach(rf24g_hal_irq_cb_t cb)
{
    s_irq_cb = cb;
    gpio_set_irq_enabled_with_callback(s_pin_irq, GPIO_IRQ_EDGE_FALL, true,
                                       gpio_irq_trampoline);
}

uint32_t __not_in_flash_func(rf24g_hal_time_us)(void)
{
    return time_us_32();
}

void __not_in_flash_func(rf24g_hal_alarm_schedule)(uint32_t at_us, rf24g_hal_alarm_cb_t cb)
{
    s_alarm_cb = cb;

    // at_us is on the rf24g_hal_time_us() (== time_us_32()) timebase, but
    // hardware_alarm_set_target() wants an absolute_time_t (64-bit us since
    // boot). Compute the delta in 32-bit arithmetic -- correct even across a
    // time_us_32() wrap, since a dwell deadline is always within tens of
    // milliseconds of "now" -- then project it onto the 64-bit clock.
    uint32_t now = time_us_32();
    int32_t delta = (int32_t)(at_us - now);

    // hardware_alarm_set_target() returns true when the target is already in
    // the past -- and in that case it does NOT arm the alarm, so the callback
    // never fires. A deadline that has already slipped is exactly the case
    // this scheduler hits when an ISR ran late, and a silently-unarmed alarm
    // stalls the link forever: in PARK or SEARCH no RX interrupt is coming to
    // re-arm it. So never ask for "now", and if the target is still missed,
    // push it further out and retry.
    if (delta < RF24G_ALARM_MIN_US) delta = RF24G_ALARM_MIN_US;

    for (uint8_t tries = 0; tries < RF24G_ALARM_MAX_TRIES; tries++) {
        if (!hardware_alarm_set_target((uint)s_alarm_num,
                                       make_timeout_time_us((uint64_t)delta))) {
            return;   // armed
        }
        delta += RF24G_ALARM_MIN_US;
    }

    // Last resort: arm well clear of now, which cannot be missed. Losing the
    // alarm entirely would stall the scheduler with nothing to restart it, so
    // a late hop beats no hop. (No printf here -- this runs in ISR context,
    // and stdio is flash-resident; see the __not_in_flash_func rationale in
    // rf24g_hal.h.)
    hardware_alarm_set_target((uint)s_alarm_num,
                              make_timeout_time_us(RF24G_ALARM_FALLBACK_US));
}

#endif // PICO_PLATFORM || PICO_RP2040 || PICO_RP2350
