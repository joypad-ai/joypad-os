// rf24g_hal.h - Platform shim for the rf24g nRF24L01+ driver
//
// nrf24l01.c and rf24g_host.c talk to hardware ONLY through this interface
// -- no pico-sdk (or any other platform SDK) calls outside the HAL
// implementation file. That keeps sf30_protocol.h, nrf24l01.c and
// rf24g_host.c portable: a future ESP32-S3 or nRF52840 build needs a new
// rf24g_hal_<platform>.c and nothing else.
//
// RP2040/RP2350 implementation: rf24g_hal_rp2040.c (spi0 + gpio +
// hardware_alarm).
//
// Time-critical note: every rf24g_hal_* function reachable from rf24g_host.c's
// radio ISR / dwell-alarm chain (ce, spi_xfer, time_us, alarm_schedule) is
// __not_in_flash_func in rf24g_hal_rp2040.c -- see rf24g_host.c's file header
// for why. init and irq_attach are one-time setup calls from ordinary
// (flash-resident) init code and are not part of that chain.

#ifndef RF24G_HAL_H
#define RF24G_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// __not_in_flash_func (RAM function placement, for code that must survive
// running mid-flash-erase) is a pico-sdk macro, normally picked up
// transitively by files that already include hardware/*.h or pico/*.h.
// rf24g_host.c and nrf24l01.c deliberately include neither -- this header is
// the one designated portability seam -- so it is pulled in from here, the
// one place platform-specific detail is allowed to leak in. platform.h
// already no-ops this macro on ESP32/NRF/CH32; guard against a conflicting
// redefinition when both headers are included together.
#if defined(PICO_PLATFORM) || defined(PICO_RP2040) || defined(PICO_RP2350)
  #include "pico.h"
#elif !defined(__not_in_flash_func)
  #define __not_in_flash_func(func_name) func_name
#endif

// Both callback types fire from interrupt context on RP2040 -- see
// rf24g_host.c's ISR-driven scheduler and the __not_in_flash_func
// requirement it documents.
typedef void (*rf24g_hal_irq_cb_t)(void);
typedef void (*rf24g_hal_alarm_cb_t)(void);

// One-time bring-up: configures SPI + GPIO for the given pins and claims
// whatever timer/alarm resource rf24g_hal_alarm_schedule() needs. Does not
// touch the radio itself -- that is nrf24l01.c's job once this returns.
void rf24g_hal_init(uint8_t sck, uint8_t mosi, uint8_t miso,
                     uint8_t csn, uint8_t ce, uint8_t irq);

// Drive CE high/low.
void rf24g_hal_ce(bool level);

// Full-duplex SPI transaction, CSN handled internally (low -> transfer ->
// high) -- the nRF24 command byte is framed by CSN, not by a hardware
// chip-select signal. tx and/or rx may be NULL for a write-only / read-only
// transfer, matching spi_write_read_blocking()'s contract.
void rf24g_hal_spi_xfer(const uint8_t* tx, uint8_t* rx, size_t len);

// Attach the falling-edge IRQ handler on the configured IRQ pin. Call once,
// after the radio itself has been configured (so the IRQ line isn't left
// floating mid-init). `cb` runs in interrupt context.
void rf24g_hal_irq_attach(rf24g_hal_irq_cb_t cb);

// Microseconds since boot (wraps at 32 bits). Numerically identical to
// platform_time_us() on every platform this HAL targets -- rf24g_host.c uses
// whichever of the two is safe in a given context (this one inside the
// ISR/alarm chain, platform_time_us() everywhere else) and mixes the
// resulting values freely.
uint32_t rf24g_hal_time_us(void);

// Arm a one-shot deadline for absolute time `at_us` (rf24g_hal_time_us()
// timebase). Firing calls `cb` from interrupt context. There is only ever
// one dwell deadline live at a time, so a new call before the previous one
// fires simply replaces it -- callers never need to cancel explicitly.
void rf24g_hal_alarm_schedule(uint32_t at_us, rf24g_hal_alarm_cb_t cb);

#endif // RF24G_HAL_H
