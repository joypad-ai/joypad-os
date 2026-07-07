// main.c - nRF52840 entry point
//
// Zephyr entry point for bt2usb/usb2usb apps on nRF52840 boards.
// bt2usb: BTstack runs in its own Zephyr thread (created by bt_transport_nrf.c).
// usb2usb: USB host via MAX3421E SPI, no Bluetooth.
// Main thread handles USB device, app logic, LED, and storage.

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/sys/onoff.h>
#include <zephyr/fatal.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <nrfx.h>

#include "tusb.h"
#include "platform/platform.h"
#include "core/app_registry.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "pad/pad_input.h"
#include "core/services/players/manager.h"
#include "core/services/leds/leds.h"
#include "core/services/storage/storage.h"
#ifdef CONFIG_CONTROLLER_BTUSB
#include "imu_nrf.h"
#include "bt/ble_output/ble_output.h"
#endif

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
const OutputInterface* native_output = NULL;

// ============================================================================
// FAULT HANDLER — Zephyr's fault dump goes to UART console automatically.
// We just turn on an LED as visual indicator and halt.
// ============================================================================
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    (void)reason; (void)esf;
#ifdef BOARD_FEATHER_NRF52840
    // Blue LED on Feather = P1.10, active high
    NRF_P1->DIRSET = (1U << 10);
    NRF_P1->OUTSET = (1U << 10);  // LED on (active high)
#else
    // Blue LED on XIAO BLE = P0.06, active low
    NRF_P0->DIRSET = (1U << 6);
    NRF_P0->OUTCLR = (1U << 6);   // LED on (active low)
#endif
    for (;;) { __WFI(); }
}

// ============================================================================
// BT CONTROLLER ASSERT HANDLER
// ============================================================================

void bt_ctlr_assert_handle(char *file, uint32_t line)
{
    printf("[BT] Controller assert: %s:%u\n", file, (unsigned)line);
}

// ============================================================================
// USB POWER + IRQ SETUP
// ============================================================================

// TinyUSB's dcd_nrf5x.c requires tusb_hal_nrf_power_event() to be called
// with VBUS power events to start the USB peripheral.

extern void dcd_int_handler(uint8_t rhport);
extern void tusb_hal_nrf_power_event(uint32_t event);

// USBD interrupt handler
static void usbd_isr(const void *arg)
{
    (void)arg;
    dcd_int_handler(0);
}

// HFCLK must stay running for USB to work. MPSL (BLE radio stack) manages
// HFCLK and will stop it when the radio is idle, breaking USB. We request
// HFCLK through Zephyr's onoff manager so MPSL keeps it running.
static struct onoff_client hfclk_cli;

static void usb_hfclk_request(void)
{
    struct onoff_manager *mgr =
        z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
    sys_notify_init_spinwait(&hfclk_cli.notify);
    int err = onoff_request(mgr, &hfclk_cli);
    if (err < 0) {
        printf("[usb] HFCLK request failed: %d\n", err);
        return;
    }
    // Wait for HFCLK to stabilize
    int res;
    while (sys_notify_fetch_result(&hfclk_cli.notify, &res) == -EAGAIN) {
        k_yield();
    }
    printf("[usb] HFCLK running\n");
}

// Call after tusb_init() to trigger USB enumeration.
// Uses dynamic interrupt registration and unconditionally fires power
// events (bt2usb is always USB-powered, so VBUS is always present).
static void usb_power_init(void)
{
    // Request HFCLK through Zephyr's clock manager (keeps MPSL aware)
    usb_hfclk_request();

    // Register USBD ISR dynamically (runtime, not via static ISR table)
    irq_connect_dynamic(USBD_IRQn, 2, usbd_isr, NULL, 0);

    // Reset USBD to clean state (bootloader may have left it active)
    if (NRF_USBD->ENABLE) {
        NRF_USBD->USBPULLUP = 0;
        __ISB(); __DSB();
        NVIC_DisableIRQ(USBD_IRQn);
        NRF_USBD->INTENCLR = NRF_USBD->INTEN;
        NRF_USBD->ENABLE = 0;
        __ISB(); __DSB();
    }

    // Log USBREGSTATUS for debugging
    uint32_t usb_reg = NRF_POWER->USBREGSTATUS;
    printf("[usb] USBREGSTATUS=0x%08x VBUS=%d OUTRDY=%d\n",
           (unsigned)usb_reg,
           !!(usb_reg & POWER_USBREGSTATUS_VBUSDETECT_Msk),
           !!(usb_reg & POWER_USBREGSTATUS_OUTPUTRDY_Msk));

    // Always fire both events — bt2usb is USB-powered so VBUS is present.
    // Don't gate on USBREGSTATUS since some boards may not report it.
    printf("[usb] Firing DETECTED event\n");
    tusb_hal_nrf_power_event(0);  // USB_EVT_DETECTED

    printf("[usb] Firing READY event\n");
    tusb_hal_nrf_power_event(2);  // USB_EVT_READY

    // Belt and suspenders: ensure USBD IRQ is enabled
    irq_enable(USBD_IRQn);

    printf("[usb] USBD init complete, pullup=%d\n",
           !!(NRF_USBD->USBPULLUP));
}

#if defined(CONFIG_CONTROLLER_BTUSB) && defined(CONFIG_BOARD_XIAO_BLE)
// ============================================================================
// BATTERY PROTECTION + IDLE DEEP-SLEEP
// ============================================================================
// On battery the firmware otherwise runs full-tilt forever (IMU 100 Hz + BLE +
// ~1 kHz loop = several mA) with no low-voltage cutoff. That flattened a LiPo to
// 1.7 V and destroyed it. Guard against it: drop to System OFF when the cell hits
// a safe floor (prevents the over-discharge that ruins the battery) or after
// being idle+disconnected (saves power). Wakes on the XIAO D1 button.
#define PWR_WAKE_GPIO        3       // P0.03 = XIAO D1 / user button
#define PWR_WAKE_ACTIVE_HIGH false   // active-low (pull-up)
#define PWR_LOW_BATT_MV      3300u   // safe LiPo floor — huge margin over ~2.5 V danger
#define PWR_IDLE_TIMEOUT_MS  (10u * 60u * 1000u)  // disconnected+idle this long → sleep
#define PWR_CHECK_MS         3000u

static void power_task(void)
{
    static uint32_t last_check = 0;
    static uint8_t  low_count = 0;
    uint32_t now = platform_time_ms();

    // On USB: charging and must stay enumerated — never sleep.
    if (platform_usb_powered()) {
        low_count = 0;
        return;
    }

    if ((uint32_t)(now - last_check) < PWR_CHECK_MS) return;
    last_check = now;

    // Critical: low-voltage cutoff. Debounced so a transient TX load sag doesn't
    // trip it. Fires regardless of connection state — over-discharge is forever.
    int mv = platform_battery_millivolts();
    if (mv > 0 && (uint32_t)mv < PWR_LOW_BATT_MV) {
        if (++low_count >= 3) {
            printf("[power] battery %d mV < %u — System OFF to protect the cell\n",
                   mv, PWR_LOW_BATT_MV);
            platform_deep_sleep(PWR_WAKE_GPIO, PWR_WAKE_ACTIVE_HIGH);
        }
        return;
    }
    low_count = 0;

    // Power saving: no real user input for a while → sleep, EVEN WHILE CONNECTED.
    // A controller left paired-but-idle to a host must not sit at full connected
    // draw and bleed the cell down. Activity = buttons / physical sticks / the
    // pad being moved (see pad_input); a static tilt or noise does not count.
    uint32_t last_active = pad_input_last_activity_ms();
    if ((uint32_t)(now - last_active) > PWR_IDLE_TIMEOUT_MS) {
        printf("[power] idle %us on battery — System OFF\n",
               (unsigned)((now - last_active) / 1000u));
        platform_deep_sleep(PWR_WAKE_GPIO, PWR_WAKE_ACTIVE_HIGH);
    }
}
#endif  // CONFIG_CONTROLLER_BTUSB && CONFIG_BOARD_XIAO_BLE

// ============================================================================
// MAIN
// ============================================================================

int main(void)
{
#if defined(CONFIG_CONTROLLER_BTUSB)
    printf("[joypad] Starting controller_btusb on Adafruit Feather nRF52840...\n");
#elif defined(CONFIG_BTUSB2USB)
    printf("[joypad] Starting btusb2usb on Adafruit Feather nRF52840...\n");
#elif defined(CONFIG_USB2USB)
    printf("[joypad] Starting usb2usb on Adafruit Feather nRF52840...\n");
#elif defined(BOARD_FEATHER_NRF52840)
    printf("[joypad] Starting bt2usb on Adafruit Feather nRF52840...\n");
#else
    printf("[joypad] Starting bt2usb on Seeed XIAO nRF52840...\n");
#endif

    // Initialize shared services
    leds_init();
    storage_init();
    players_init();
    app_init();

#ifdef CONFIG_MAX3421
    // Initialize MAX3421E SPI host (must be before tusb_init/input init)
    {
        extern bool max3421_host_init(void);
        if (!max3421_host_init()) {
            printf("[joypad] MAX3421E not detected - USB host disabled\n");
        }
    }
#endif

    // Get and initialize input interfaces
    inputs = app_get_input_interfaces(&input_count);
    for (uint8_t i = 0; i < input_count; i++) {
        if (inputs[i] && inputs[i]->init) {
            printf("[joypad] Initializing input: %s\n", inputs[i]->name);
            inputs[i]->init();
        }
    }

    // Get and initialize output interfaces
    outputs = app_get_output_interfaces(&output_count);
    if (output_count > 0 && outputs[0]) {
        active_output = outputs[0];
    }
    for (uint8_t i = 0; i < output_count; i++) {
        if (outputs[i] && outputs[i]->init) {
            printf("[joypad] Initializing output: %s\n", outputs[i]->name);
            outputs[i]->init();
        }
    }

    printf("[joypad] tusb_inited=%d\n", tud_inited());

    // Publish active interfaces so shared code (CDC, router) can introspect.
    app_registry_set(inputs, input_count, outputs, output_count);

    // Trigger USB enumeration (handles VBUS already present at boot)
    usb_power_init();

#ifdef CONFIG_MAX3421
    // Enable MAX3421E interrupt now that TinyUSB host is initialized
    {
        extern void max3421_host_enable_int(void);
        max3421_host_enable_int();
    }
#endif

#ifdef CONFIG_CONTROLLER_BTUSB
    // Onboard IMU (XIAO Sense LSM6DS3TR-C) — after USB is up, so a wedged I2C
    // bus can never block enumeration. No-op if the board has no IMU.
    imu_init();
#endif

    printf("[joypad] Entering main loop\n");

#ifdef CONFIG_MAX3421
    uint32_t diag_time = 0;
#endif

    // Main loop
    while (1) {
        // Poll TinyUSB device (non-blocking)
        tud_task_ext(0, false);

#ifdef CONFIG_MAX3421
        // Process TinyUSB host events (MAX3421E ISR handles SPI directly)
        tuh_task_ext(0, false);

        // Periodic diagnostic (every 5s for first 60s)
        {
            extern void max3421_print_diag(void);
            uint32_t now = platform_time_ms();
            if (diag_time == 0 || (now - diag_time >= 5000 && now < 60000)) {
                max3421_print_diag();
                diag_time = now;
            }
        }
#endif

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

#ifdef CONFIG_CONTROLLER_BTUSB
        imu_task();  // sample onboard IMU → router (throttled to ~100 Hz)
#endif

#if defined(CONFIG_CONTROLLER_BTUSB) && defined(CONFIG_BOARD_XIAO_BLE)
        power_task();  // low-battery cutoff + idle deep-sleep (protects the cell)
#endif

        // Yield to other Zephyr threads (BTstack runs in its own thread)
        k_msleep(1);
    }

    return 0;
}
