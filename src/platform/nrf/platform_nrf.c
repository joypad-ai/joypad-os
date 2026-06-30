// platform_nrf.c - Platform HAL for Seeed XIAO nRF52840 (Zephyr)
//
// Implements platform.h using Zephyr kernel APIs.
// Mirrors platform_esp32.c but for nRF Connect SDK.

#include "platform/platform.h"
#include "platform/platform_gpio.h"
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/drivers/hwinfo.h>
#include <nrfx.h>
#include <hal/nrf_gpio.h>

// ============================================================================
// TIME
// ============================================================================

uint32_t platform_time_ms(void)
{
    return k_uptime_get_32();
}

uint32_t platform_time_us(void)
{
    return (uint32_t)k_ticks_to_us_floor64(k_uptime_ticks());
}

void platform_sleep_us(uint32_t us)
{
    k_busy_wait(us);
}

void platform_sleep_ms(uint32_t ms)
{
    k_msleep(ms);
}

// ============================================================================
// IDENTITY
// ============================================================================

void platform_get_serial(char* buf, size_t len)
{
    uint8_t id[8];
    ssize_t id_len = hwinfo_get_device_id(id, sizeof(id));
    if (id_len <= 0) {
        snprintf(buf, len, "000000000000");
        return;
    }

    size_t pos = 0;
    for (int i = 0; i < id_len && pos + 2 < len; i++) {
        pos += snprintf(buf + pos, len - pos, "%02x", id[i]);
    }
}

void platform_get_unique_id(uint8_t* buf, size_t len)
{
    ssize_t id_len = hwinfo_get_device_id(buf, len);
    if (id_len < (ssize_t)len) {
        memset(buf + (id_len > 0 ? id_len : 0), 0, len - (id_len > 0 ? id_len : 0));
    }
}

// ============================================================================
// REBOOT
// ============================================================================

void platform_reboot(void)
{
    sys_reboot(SYS_REBOOT_COLD);
}

// ============================================================================
// TINYUSB PLATFORM (USB host stack requires these)
// ============================================================================

#ifdef CONFIG_MAX3421
#include "tusb.h"

uint32_t tusb_time_millis_api(void)
{
    return k_uptime_get_32();
}
#endif

void platform_reboot_bootloader(void)
{
    // Adafruit nRF52 UF2 bootloader checks GPREGRET for magic value 0x57
    // on boot. If set, it enters UF2 mass storage mode instead of the app.
    // Nordic DFU bootloader uses 0xB1 instead.
    NRF_POWER->GPREGRET = 0x57;
    sys_reboot(SYS_REBOOT_COLD);
}

// ============================================================================
// POWER / DEEP SLEEP
// ============================================================================

bool platform_usb_powered(void)
{
    return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

int platform_battery_millivolts(void)
{
#if defined(CONFIG_ADC) && defined(CONFIG_BOARD_XIAO_BLE)
    // XIAO nRF52840: battery is read on AIN7 (P0.31) through a ~1/3 resistor
    // divider that's gated by P0.14 — held LOW to connect it. Keep P0.14 LOW
    // (driving it HIGH can push P0.31 past its 3.6 V limit). The SAADC is
    // configured for gain 1/6 + 0.6 V internal ref → full-scale 3.6 V at the
    // pin over 12 bits, so Vadc_mv = raw * 3600 / 4095, and the divider gives
    // VBAT = Vadc * (1.0 + 0.51)/0.51 ≈ Vadc * 2.96.
    static bool inited = false;
    if (!inited) {
        nrf_gpio_cfg_output(14);
        nrf_gpio_pin_clear(14);       // P0.14 LOW: enable the divider
        platform_adc_init();
        platform_adc_init_channel(7); // AIN7 = P0.31
        inited = true;
        k_msleep(2);
    }
    uint16_t raw = platform_adc_read(7);
    uint32_t vadc_mv = ((uint32_t)raw * 3600u) / 4095u;
    uint32_t vbat_mv = (vadc_mv * 296u) / 100u;   // 1M/510k divider → *2.96

    // The nRF SAADC internal 0.6V reference has a few % gain tolerance and
    // reads low. Single-point trim calibrated against a known-full LP103454 on
    // the charger (4.2V), which read ~4.06V uncorrected → factor ~1.033. Adjust
    // XIAO_VBAT_TRIM_NUM/DEN if a BAT+ multimeter reading says otherwise.
#ifndef XIAO_VBAT_TRIM_NUM
#define XIAO_VBAT_TRIM_NUM 1033
#define XIAO_VBAT_TRIM_DEN 1000
#endif
    vbat_mv = (vbat_mv * XIAO_VBAT_TRIM_NUM) / XIAO_VBAT_TRIM_DEN;
    return (int)vbat_mv;
#else
    return -1;
#endif
}

int platform_battery_charging(void)
{
#if defined(CONFIG_BOARD_XIAO_BLE)
    // Only meaningful while on external power; on battery the charger/LED
    // circuit is unpowered and the status pin floats.
    if (!platform_usb_powered()) return 0;
    // P0.17 = the XIAO charge-status / LED line off the BQ25100 CHG output:
    // pulled LOW while actively charging (LED lit), released HIGH when charging
    // completes or is idle. Read-only — high-Z input, doesn't disturb the LED.
    static bool inited = false;
    if (!inited) {
        nrf_gpio_cfg_input(17, NRF_GPIO_PIN_NOPULL);
        inited = true;
    }
    return nrf_gpio_pin_read(17) ? 0 : 1;  // HIGH=done(0), LOW=charging(1)
#else
    return -1;  // no charge-status pin on this board
#endif
}

uint32_t platform_last_reset_reason(void)
{
    // RESETREAS accumulates bits until cleared (write-1-to-clear). Read once at
    // first call (the boot cause) and clear so the next boot is fresh.
    // Bits: 0x01 PIN, 0x02 DOG(watchdog), 0x04 SREQ(soft), 0x08 LOCKUP,
    //       0x10000 OFF (System OFF GPIO/DETECT wake), 0x100000 VBUS wake.
    static uint32_t cached = 0xFFFFFFFFUL;
    if (cached == 0xFFFFFFFFUL) {
        cached = NRF_POWER->RESETREAS;
        NRF_POWER->RESETREAS = 0xFFFFFFFFUL;
    }
    return cached;
}

bool platform_deep_sleep(uint8_t wake_gpio, bool wake_active_high)
{
    // Never power down while USB-powered: USB needs the device awake, and the
    // host is also charging the battery. The caller falls back to advertising.
    if (platform_usb_powered()) {
        return false;
    }

    printf("[platform] Entering System OFF (deep sleep), wake on GPIO %u (%s)\n",
           (unsigned)wake_gpio, wake_active_high ? "active-high" : "active-low");

    // Clear any latched GPIO DETECT events so a stale latch doesn't wake us
    // immediately on entering System OFF.
    NRF_P0->LATCH = 0xFFFFFFFFUL;
    NRF_P1->LATCH = 0xFFFFFFFFUL;

    // Hold the wake pin at its IDLE level with the matching internal pull, and
    // SENSE on its PRESSED level — otherwise it wakes immediately. For an
    // active-high button (idle low, press high): pull-down + sense-high. For an
    // active-low button (idle high, press low): pull-up + sense-low. Uses the
    // absolute nRF pin index (P0.00-31 = 0-31, P1.00-15 = 32-47), the same
    // numbering the pad config uses.
    if (wake_active_high) {
        nrf_gpio_cfg_input(wake_gpio, NRF_GPIO_PIN_PULLDOWN);
        nrf_gpio_cfg_sense_set(wake_gpio, NRF_GPIO_PIN_SENSE_HIGH);
    } else {
        nrf_gpio_cfg_input(wake_gpio, NRF_GPIO_PIN_PULLUP);
        nrf_gpio_cfg_sense_set(wake_gpio, NRF_GPIO_PIN_SENSE_LOW);
    }

    // Let pending log/USB-detach settle, then enter System OFF. Wake from
    // System OFF resets the SoC, so this is effectively off until the button.
    k_msleep(50);
    sys_poweroff();

    // sys_poweroff() does not return.
    return true;
}
