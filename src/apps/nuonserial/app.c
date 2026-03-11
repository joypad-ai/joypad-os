// app.c - Nuon Serial Adapter App
// Polyface serial device for Nuon homebrew debug logging
//
// Presents as a Polyface TYPE=0x04 (SERIAL) on the Nuon controller port.
// Nuon homebrew writes to SDATA register → RP2040 relays over USB CDC to PC.
// PC can send data back → Nuon reads from SDATA register.

#include "app.h"
#include "core/router/router.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/nuon/nuon_serial_device.h"
#include "tusb.h"
#include "pico/bootrom.h"
#include <stdio.h>

// ============================================================================
// APP INPUT INTERFACES (none — no USB host needed)
// ============================================================================

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = 0;
    return NULL;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

static const OutputInterface* output_interfaces[] = {
    &nuon_serial_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:nuonserial] Initializing Nuon Serial Adapter v%s\n", APP_VERSION);

    // Minimal router config — no input routing needed
    router_config_t router_cfg = {
        .mode = ROUTING_MODE_SIMPLE,
    };
    router_init(&router_cfg);

    // Initialize TinyUSB device stack for CDC
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(0, &dev_init);

    printf("[app:nuonserial] USB CDC initialized\n");
    printf("[app:nuonserial] Ready — Nuon SDATA writes will appear on CDC serial\n");
}

// ============================================================================
// APP TASK — Bridge tx_fifo → USB CDC and USB CDC → rx_fifo
// ============================================================================

void app_task(void)
{
    // Check UART for bootloader command ('B')
    int c = getchar_timeout_us(0);
    if (c == 'B') reset_usb_boot(0, 0);

    // Process TinyUSB device events
    tud_task();

    if (!tud_cdc_connected()) return;

    // Nuon → PC: drain tx_fifo to USB CDC
    int byte;
    while ((byte = nuonser_tx_read()) >= 0) {
        tud_cdc_write_char((char)byte);
    }
    tud_cdc_write_flush();

    // PC → Nuon: drain USB CDC to rx_fifo
    while (tud_cdc_available()) {
        uint8_t c = tud_cdc_read_char();
        nuonser_rx_write(c);
    }
}
