// platform_esp32.c - ESP32-S3 platform implementation
//
// Wraps ESP-IDF APIs for the platform HAL.
// Includes double-tap reset detection for TinyUF2 bootloader entry.

#include "platform/platform.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "soc/rtc_cntl_reg.h"
#include "esp32s3/rom/usb/chip_usb_dw_wrapper.h"
#include "esp32s3/rom/usb/usb_persist.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define DBL_TAP_DELAY_MS    500

uint32_t platform_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

uint32_t platform_time_us(void)
{
    return (uint32_t)esp_timer_get_time();
}

void platform_sleep_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void platform_get_serial(char* buf, size_t len)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(buf, len, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void platform_get_unique_id(uint8_t* buf, size_t len)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    // Pad to 8 bytes to match RP2040 unique ID size
    uint8_t id[8] = {0};
    memcpy(id, mac, 6);
    id[6] = mac[0] ^ 0x55;
    id[7] = mac[1] ^ 0xAA;
    size_t copy_len = len < sizeof(id) ? len : sizeof(id);
    memcpy(buf, id, copy_len);
}

void platform_reboot(void)
{
    esp_restart();
}

// Called after detection window expires — clears the NVS flag so a single
// reset doesn't falsely trigger on next boot.
static void dbl_tap_timer_cb(void *arg)
{
    (void)arg;
    nvs_handle_t nvs;
    if (nvs_open("platform", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, "dbl_tap");
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

// Check for double-tap reset and set up detection window.
// Must be called after nvs_flash_init() in app_main().
//
// Uses NVS flag (survives power-on reset on all boards, unlike RTC registers).
// When detected, enters TinyUF2 via non-persistent hint register — device
// boots normally on the next reset.
//
// Flow:
//   1. Boot → NVS flag set   → user double-tapped → reboot into TinyUF2
//   2. Boot → NVS flag clear → set flag, clear after 500ms
//   3. If user resets again within 500ms → step 1 triggers
void platform_check_double_tap(void)
{
    nvs_handle_t nvs;
    if (nvs_open("platform", NVS_READWRITE, &nvs) != ESP_OK) return;

    uint8_t dbl_tap = 0;
    nvs_get_u8(nvs, "dbl_tap", &dbl_tap);

    if (dbl_tap) {
        nvs_erase_key(nvs, "dbl_tap");
        nvs_commit(nvs);
        nvs_close(nvs);
        printf("[platform] Double-tap reset detected\n");
        platform_reboot_bootloader();
        // does not return
    }

    // Start detection window
    nvs_set_u8(nvs, "dbl_tap", 1);
    nvs_commit(nvs);
    nvs_close(nvs);

    const esp_timer_create_args_t args = {
        .callback = dbl_tap_timer_cb,
        .name = "dbl_tap"
    };
    esp_timer_handle_t timer;
    esp_timer_create(&args, &timer);
    esp_timer_start_once(timer, DBL_TAP_DELAY_MS * 1000);
}

void platform_reboot_bootloader(void)
{
    // TinyUF2's custom bootloader checks RTC_CNTL_STORE6_REG for hint 0x11F2
    // on SW reset, and boots factory (TinyUF2) instead of the app.
    // Format: bit31 set + hint duplicated in bits[30:16] and bits[14:0].
    // Non-persistent — next normal boot reads otadata and boots ota_0.
    printf("[platform] Rebooting into TinyUF2...\n");
    REG_WRITE(RTC_CNTL_STORE6_REG, 0x80000000 | (0x11F2 << 16) | 0x11F2);
    esp_restart();
    while (1) { vTaskDelay(portMAX_DELAY); }
}

void platform_clear_usb_persist(void)
{
    chip_usb_set_persist_flags(0);
}
