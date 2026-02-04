// usbh.c - USB Host Layer
//
// Provides unified USB host handling across HID, X-input, and Bluetooth protocols.
// Device drivers read per-player feedback state from feedback_get_state().

#include "usbh.h"
#include "tusb.h"
#include "core/services/players/manager.h"
#include "core/services/codes/codes.h"
#include <stdio.h>

#if defined(CONFIG_USB) && CFG_TUH_RPI_PIO_USB
#include "pio_usb.h"
#include "hardware/gpio.h"
#endif

// HID protocol handlers
extern void hid_init(void);
extern void hid_task(void);

// X-input protocol handlers
extern void xinput_task(void);

// BTstack (Bluetooth) protocol handlers
#if CFG_TUH_BTD
#include "btd/hci_transport_h2_tinyusb.h"
#include "bt/transport/bt_transport.h"
extern const bt_transport_t bt_transport_usb;

// Runtime flag: only run BTstack loop when BT hardware is actually present.
// Avoids ~1-3ms overhead per main loop iteration when no dongle is connected.
static bool bt_hardware_present = false;

void usbh_set_bt_available(bool available)
{
    bt_hardware_present = available;
}
#endif

// PIO USB pin definitions (configurable per board)
#if defined(CONFIG_PIO_USB_DP_PIN)
    // Use CMake-configured pin (e.g., rp2040zero, custom boards)
    #define PIO_USB_DP_PIN      CONFIG_PIO_USB_DP_PIN
#elif defined(ADAFRUIT_FEATHER_RP2040_USB_HOST)
    // Feather RP2040 USB Host board
    #define PIO_USB_VBUS_PIN    18  // VBUS enable for USB-A port
    #define PIO_USB_DP_PIN      16  // D+ pin for PIO USB
#endif

void usbh_init(void)
{
    printf("[usbh] Initializing USB host\n");

    hid_init();

#if defined(CONFIG_USB) && CFG_TUH_RPI_PIO_USB
    // Dual USB mode: Host on rhport 1 (PIO USB for boards with separate host port)

#ifdef PIO_USB_VBUS_PIN
    // Enable VBUS power for USB-A port (required on Feather RP2040 USB Host)
    gpio_init(PIO_USB_VBUS_PIN);
    gpio_set_dir(PIO_USB_VBUS_PIN, GPIO_OUT);
    gpio_put(PIO_USB_VBUS_PIN, 1);
    printf("[usbh] Enabled VBUS on GPIO %d\n", PIO_USB_VBUS_PIN);
#endif

    // Configure PIO USB to use PIO1 (PIO0 is used by NeoPixel)
    pio_usb_configuration_t pio_cfg = {
        .pin_dp = PIO_USB_DP_PIN_DEFAULT,
        .pio_tx_num = 1,      // Use PIO1 for TX
        .sm_tx = 0,
        .tx_ch = 0,
        .pio_rx_num = 1,      // Use PIO1 for RX
        .sm_rx = 1,
        .sm_eop = 2,
        .alarm_pool = NULL,
        .debug_pin_rx = -1,
        .debug_pin_eop = -1,
        .skip_alarm_pool = false,
        .pinout = PIO_USB_PINOUT_DPDM,
    };

#ifdef PIO_USB_DP_PIN
    pio_cfg.pin_dp = PIO_USB_DP_PIN;
#endif

    // Configure TinyUSB PIO USB driver before initialization
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

    tusb_rhport_init_t host_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL  // PIO USB is Full Speed only
    };
    tusb_init(1, &host_init);
#elif defined(CONFIG_USB)
    // CONFIG_USB but no PIO USB - shouldn't happen but handle gracefully
    printf("[usbh] Warning: CONFIG_USB without PIO USB support\n");
#else
    // Single USB mode: Host on rhport 0 (native USB)
    tusb_init();
#endif

#if CFG_TUH_BTD
    // Initialize Bluetooth transport (for USB BT dongle support)
    bt_init(&bt_transport_usb);

    // Pico W has onboard BT - enable BTstack loop at startup
#if defined(CYW43_WL_GPIO_ON) || defined(PICO_CYW43_SUPPORTED)
    bt_hardware_present = true;
#endif
#endif

    printf("[usbh] Initialization complete\n");
}

void usbh_task(void)
{
    // TinyUSB host polling
    tuh_task();

#if CFG_TUH_XINPUT
    xinput_task();
#endif

#if CFG_TUH_HID
    hid_task();
#endif

#if CFG_TUH_BTD
    if (bt_hardware_present) {
        hci_transport_h2_tinyusb_process();
        bt_task();
    }
#endif
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

void tuh_mount_cb(uint8_t dev_addr)
{
    printf("A device with address %d is mounted\r\n", dev_addr);
}

void tuh_umount_cb(uint8_t dev_addr)
{
    printf("A device with address %d is unmounted\r\n", dev_addr);

    remove_players_by_address(dev_addr, -1);

    // Reset test mode when device disconnects
    codes_reset_test_mode();
}

//--------------------------------------------------------------------+
// Input Interface
//--------------------------------------------------------------------+

const InputInterface usbh_input_interface = {
    .name = "USB Host",
    .source = INPUT_SOURCE_USB_HOST,
    .init = usbh_init,
    .task = usbh_task,
    .is_connected = NULL,       // TODO: Track connected device count
    .get_device_count = NULL,   // TODO: Return connected device count
};
