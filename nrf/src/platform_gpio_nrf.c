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

    struct adc_channel_cfg cfg = {
        .gain = ADC_GAIN_1_6,
        .reference = ADC_REF_INTERNAL,
        .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10),
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
