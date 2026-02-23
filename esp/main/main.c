// main.c - ESP32-S3 bt2usb entry point
//
// FreeRTOS entry point for the bt2usb app on ESP32-S3.
// BTstack runs in its own FreeRTOS task (created by btstack_run_loop_freertos).
// Main task handles USB device, app logic, LED, and storage.

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "tusb.h"
#include "esp_private/usb_phy.h"
#include "platform/platform.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/players/manager.h"
#include "core/services/leds/leds.h"
#include "core/services/storage/storage.h"

static const char *TAG = "joypad";

// App layer
extern void app_init(void);
extern void app_task(void);
extern const OutputInterface** app_get_output_interfaces(uint8_t* count);
extern const InputInterface** app_get_input_interfaces(uint8_t* count);

static const OutputInterface** outputs = NULL;
static uint8_t output_count = 0;
static const InputInterface** inputs = NULL;
static uint8_t input_count = 0;
const OutputInterface* active_output = NULL;

void app_main(void)
{
    // Initialize NVS first (needed for double-tap detection and flash settings)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Double-tap reset detection — if user tapped reset twice within 500ms,
    // reboots into TinyUF2. Uses NVS (survives power-on reset on all boards).
    extern void platform_check_double_tap(void);
    platform_check_double_tap();

    ESP_LOGI(TAG, "Starting Joypad bt2usb on ESP32-S3...");

    // Initialize shared services
    leds_init();
    storage_init();
    players_init();
    app_init();

    // Get and initialize input interfaces
    inputs = app_get_input_interfaces(&input_count);
    for (uint8_t i = 0; i < input_count; i++) {
        if (inputs[i] && inputs[i]->init) {
            ESP_LOGI(TAG, "Initializing input: %s", inputs[i]->name);
            inputs[i]->init();
        }
    }

    // Clear any stale USB persist flags from a previous DFU/bootloader attempt
    extern void platform_clear_usb_persist(void);
    platform_clear_usb_persist();

    // Initialize USB PHY for TinyUSB (must happen before tusb_init in usbd_init)
    usb_phy_config_t phy_conf = {
        .controller = USB_PHY_CTRL_OTG,
        .target = USB_PHY_TARGET_INT,
        .otg_mode = USB_OTG_MODE_DEVICE,
        .otg_speed = USB_PHY_SPEED_UNDEFINED,
    };
    usb_phy_handle_t phy_hdl = NULL;
    ESP_ERROR_CHECK(usb_new_phy(&phy_conf, &phy_hdl));
    ESP_LOGI(TAG, "USB PHY initialized");

    // Get and initialize output interfaces
    outputs = app_get_output_interfaces(&output_count);
    if (output_count > 0 && outputs[0]) {
        active_output = outputs[0];
    }
    for (uint8_t i = 0; i < output_count; i++) {
        if (outputs[i] && outputs[i]->init) {
            ESP_LOGI(TAG, "Initializing output: %s", outputs[i]->name);
            outputs[i]->init();
        }
    }

    ESP_LOGI(TAG, "Entering main loop");

    // Main loop
    while (1) {
        leds_task();
        players_task();
        storage_task();

        // Poll input interfaces
        for (uint8_t i = 0; i < input_count; i++) {
            if (inputs[i] && inputs[i]->task) {
                inputs[i]->task();
            }
        }

        // Run output interface tasks
        for (uint8_t i = 0; i < output_count; i++) {
            if (outputs[i] && outputs[i]->task) {
                outputs[i]->task();
            }
        }

        app_task();

        // Yield to other FreeRTOS tasks (BTstack runs in its own task)
        vTaskDelay(1);
    }
}
