// platform_gpio_nrf.c - nRF52840 GPIO/ADC implementation (Zephyr)
// SPDX-License-Identifier: Apache-2.0

#include "platform/platform_gpio.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <stdio.h>

// nRF52840 has two GPIO ports: P0 (0-31) and P1 (32-47 mapped as 32+pin)
// Zephyr gpio_dt_spec requires a device, but for dynamic pin config we use
// the raw nrfx GPIO driver via Zephyr's gpio API.

static const struct device* gpio0_dev = NULL;
static const struct device* gpio1_dev = NULL;

// Reject pins that are not usable GPIO on the nRF52840, so a bad/garbage
// value from a saved custom pad config can't fault or hang early boot
// (which soft-bricks the board — the config lives in NVS and survives
// reflash). nRF52840 GPIO range: P0.00-P0.31 (0-31), P1.00-P1.15 (32-47).
// P0.00/P0.01 are the LFXO 32.768 kHz crystal pins (XL1/XL2) on every
// nRF52840 board that uses the external low-freq crystal — driving them as
// GPIO breaks LFCLK and wedges BLE/RTC.
static bool platform_gpio_pin_usable(uint8_t pin) {
    if (pin > 47) return false;          // beyond P1.15
    if (pin == 0 || pin == 1) return false;  // LFXO crystal (XL1/XL2)
    return true;
}

static const struct device* get_gpio_dev(uint8_t pin) {
    if (pin < 32) {
        if (!gpio0_dev) {
            gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
        }
        return gpio0_dev;
    } else {
        if (!gpio1_dev) {
            gpio1_dev = DEVICE_DT_GET(DT_NODELABEL(gpio1));
        }
        return gpio1_dev;
    }
}

void platform_gpio_init_input(uint8_t pin, bool pull_up) {
    if (!platform_gpio_pin_usable(pin)) {
        printf("[gpio] Refusing unusable pin %d (out of range or LFXO crystal)\n", pin);
        return;
    }
    const struct device* dev = get_gpio_dev(pin);
    if (!device_is_ready(dev)) {
        printf("[gpio] Device not ready for pin %d\n", pin);
        return;
    }

    uint8_t hw_pin = pin < 32 ? pin : (pin - 32);
    gpio_flags_t flags = GPIO_INPUT | (pull_up ? GPIO_PULL_UP : GPIO_PULL_DOWN);
    gpio_pin_configure(dev, hw_pin, flags);
}

bool platform_gpio_get(uint8_t pin) {
    const struct device* dev = get_gpio_dev(pin);
    uint8_t hw_pin = pin < 32 ? pin : (pin - 32);
    return gpio_pin_get(dev, hw_pin) != 0;
}

void platform_gpio_init_output(uint8_t pin) {
    if (!platform_gpio_pin_usable(pin)) {
        printf("[gpio] Refusing unusable pin %d (out of range or LFXO crystal)\n", pin);
        return;
    }
    const struct device* dev = get_gpio_dev(pin);
    if (!device_is_ready(dev)) {
        printf("[gpio] Device not ready for pin %d\n", pin);
        return;
    }
    uint8_t hw_pin = pin < 32 ? pin : (pin - 32);
    gpio_pin_configure(dev, hw_pin, GPIO_OUTPUT_INACTIVE);
}

void platform_gpio_put(uint8_t pin, bool on) {
    const struct device* dev = get_gpio_dev(pin);
    uint8_t hw_pin = pin < 32 ? pin : (pin - 32);
    gpio_pin_set(dev, hw_pin, on ? 1 : 0);
}

// ADC — nRF52840 SAADC with AIN0-AIN7
#ifdef CONFIG_ADC
static const struct device* adc_dev = NULL;
static bool adc_inited_hw = false;
#endif

void platform_adc_init(void) {
#ifdef CONFIG_ADC
    if (adc_inited_hw) return;
    adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
    if (!device_is_ready(adc_dev)) {
        printf("[gpio] ADC device not ready\n");
        adc_dev = NULL;
        return;
    }
    adc_inited_hw = true;
#else
    printf("[gpio] ADC not available on this board\n");
#endif
}

void platform_adc_init_channel(uint8_t channel) {
#ifdef CONFIG_ADC
    if (!adc_dev || channel > 7) return;

    // Acquisition time must scale with the source impedance: the nRF SAADC
    // sample cap needs longer to settle for high-impedance sources. Analog
    // sticks (AIN0-3) are low-impedance pots — 10us is plenty and keeps polling
    // fast. Higher channels (AIN4-7) are used for sense inputs like the XIAO
    // battery divider (AIN7, ~337kOhm Thevenin from 1M||510k), which needs
    // ~40us; at 10us it under-reads ~5% (a full 4.2V cell reads ~4.0V).
    uint16_t acq_us = (channel >= 4) ? 40 : 10;

    struct adc_channel_cfg cfg = {
        .gain = ADC_GAIN_1_6,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, acq_us),
        .channel_id = channel,
#if defined(CONFIG_ADC_NRFX_SAADC)
        .input_positive = SAADC_CH_PSELP_PSELP_AnalogInput0 + channel,
#endif
    };
    adc_channel_setup(adc_dev, &cfg);
#else
    (void)channel;
#endif
}

uint16_t platform_adc_read(uint8_t channel) {
#ifdef CONFIG_ADC
    if (!adc_dev) return 2048;

    int16_t sample = 0;
    struct adc_sequence seq = {
        .channels = BIT(channel),
        .buffer = &sample,
        .buffer_size = sizeof(sample),
        .resolution = 12,
    };

    int err = adc_read(adc_dev, &seq);
    if (err < 0) return 2048;

    if (sample < 0) sample = 0;
    return (uint16_t)sample;
#else
    (void)channel;
    return 2048;
#endif
}
