// platform_esp32.c - ESP32-S3 platform implementation
//
// Wraps ESP-IDF APIs for the platform HAL.

#include "platform/platform.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "soc/rtc_cntl_reg.h"
#include "esp32s3/rom/usb/chip_usb_dw_wrapper.h"
#include "esp32s3/rom/usb/usb_persist.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

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

void platform_reboot_bootloader(void)
{
    printf("[platform] Entering USB DFU bootloader...\n");
    chip_usb_set_persist_flags(USBDC_BOOT_DFU);
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
    while (1) { vTaskDelay(portMAX_DELAY); }
}

void platform_clear_usb_persist(void)
{
    chip_usb_set_persist_flags(0);
}
