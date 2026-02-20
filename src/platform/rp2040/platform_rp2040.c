// platform_rp2040.c - RP2040/RP2350 platform implementation
//
// Wraps pico-sdk APIs for the platform HAL.

#include "platform/platform.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"
#include <string.h>

uint32_t platform_time_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

uint32_t platform_time_us(void)
{
    return time_us_32();
}

void platform_sleep_ms(uint32_t ms)
{
    sleep_ms(ms);
}

void platform_get_serial(char* buf, size_t len)
{
    pico_get_unique_board_id_string(buf, len);
}

void platform_get_unique_id(uint8_t* buf, size_t len)
{
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    size_t copy_len = len < sizeof(board_id.id) ? len : sizeof(board_id.id);
    memcpy(buf, board_id.id, copy_len);
}

void platform_reboot(void)
{
    watchdog_enable(100, false);
    while (1);
}

void platform_reboot_bootloader(void)
{
    reset_usb_boot(0, 0);
}
