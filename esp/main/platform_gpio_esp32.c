// platform_gpio_esp32.c - ESP32 GPIO/ADC implementation
// SPDX-License-Identifier: Apache-2.0

#include "platform/platform_gpio.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include <stdio.h>

static adc_oneshot_unit_handle_t adc_handle = NULL;
static bool adc_inited = false;

void platform_gpio_init_input(uint8_t pin, bool pull_up) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pull_up ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

bool platform_gpio_get(uint8_t pin) {
    return gpio_get_level(pin) != 0;
}

void platform_gpio_init_output(uint8_t pin) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(pin, 0);
}

void platform_gpio_put(uint8_t pin, bool on) {
    gpio_set_level(pin, on ? 1 : 0);
}

void platform_adc_init(void) {
    if (adc_inited) return;

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    esp_err_t err = adc_oneshot_new_unit(&init_cfg, &adc_handle);
    if (err != ESP_OK) {
        printf("[platform_gpio] ADC init failed: %d\n", err);
        return;
    }
    adc_inited = true;
}

void platform_adc_init_channel(uint8_t channel) {
    if (!adc_handle) return;

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(adc_handle, channel, &chan_cfg);
}

uint16_t platform_adc_read(uint8_t channel) {
    if (!adc_handle) return 2048;

    int raw = 0;
    esp_err_t err = adc_oneshot_read(adc_handle, channel, &raw);
    if (err != ESP_OK) return 2048;

    return (uint16_t)raw;  // 12-bit: 0-4095
}
