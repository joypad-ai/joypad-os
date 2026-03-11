// main.c - ESP32-S3 entry point
//
// FreeRTOS entry point for Joypad apps on ESP32-S3.
// BTstack runs in its own FreeRTOS task (created by btstack_run_loop_freertos).
// Main task handles USB device, app logic, LED, and storage.

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "driver/uart.h"
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

#ifdef CONFIG_MAX3421
extern bool max3421_host_init(void);
extern void max3421_host_enable_int(void);
extern bool max3421_is_detected(void);
extern void max3421_poll(void);
#endif

static const OutputInterface** outputs = NULL;
static uint8_t output_count = 0;
static const InputInterface** inputs = NULL;
static uint8_t input_count = 0;
const OutputInterface* active_output = NULL;

// Redirect stdout/stderr to UART1 on header TX/RX pins.
// ESP-IDF console UART0 custom pin remapping doesn't work reliably on ESP32-S3
// when USB OTG owns the default UART0 pins, so we use UART1 explicitly.
#ifdef BOARD_FEATHER_ESP32S3
static int uart1_write(void* cookie, const char* data, int size)
{
    (void)cookie;
    return uart_write_bytes(UART_NUM_1, data, size);
}

static void uart1_console_init(void)
{
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NUM_1, &cfg);
    uart_set_pin(UART_NUM_1, 39, 38, -1, -1);
    uart_driver_install(UART_NUM_1, 256, 0, 0, NULL, 0);

    // Redirect stdout and stderr to UART1
    stdout = funopen(NULL, NULL, uart1_write, NULL, NULL);
    setvbuf(stdout, NULL, _IOLBF, 0);
    stderr = funopen(NULL, NULL, uart1_write, NULL, NULL);
    setvbuf(stderr, NULL, _IOLBF, 0);

    // Redirect ESP_LOG to use our new stdout
    esp_log_set_vprintf(vprintf);
}
#endif

void app_main(void)
{
#ifdef BOARD_FEATHER_ESP32S3
    uart1_console_init();
#endif

    // Print reset reason to help diagnose silent reboots
    esp_reset_reason_t reason = esp_reset_reason();
    const char* reason_str[] = {
        "UNKNOWN", "POWERON", "EXT", "SW", "PANIC", "INT_WDT",
        "TASK_WDT", "WDT", "DEEPSLEEP", "BROWNOUT", "SDIO",
        "USB", "JTAG", "EFUSE", "PWR_GLITCH", "CPU_LOCKUP"
    };
    printf("[reset] reason: %d (%s)\n", reason,
           (reason < 16) ? reason_str[reason] : "?");

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

    ESP_LOGI(TAG, "Starting Joypad on ESP32-S3...");

    // Initialize shared services
    leds_init();
    storage_init();
    players_init();
    app_init();

#ifdef CONFIG_MAX3421
    // Initialize MAX3421E SPI host (must be before tusb_init/input init)
    if (!max3421_host_init()) {
        ESP_LOGW(TAG, "MAX3421E not detected - USB host disabled");
    }
#endif

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

#ifdef CONFIG_MAX3421
    // Enable MAX3421E interrupt now that TinyUSB host is initialized
    if (max3421_is_detected()) {
        max3421_host_enable_int();
    }
#endif

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

#ifdef CONFIG_MAX3421
        // Process deferred MAX3421E interrupts from main loop context.
        // Cannot call hcd_int_handler from ISR — TinyUSB code is in flash, not IRAM.
        max3421_poll();
#endif

        // Yield to other FreeRTOS tasks (BTstack runs in its own task)
        vTaskDelay(1);
    }
}
