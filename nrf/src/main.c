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
#ifdef CONFIG_UNIVERSAL
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
const InputInterface* native_input = NULL;

// ============================================================================
// FAULT HANDLER — Zephyr's fault dump goes to UART console automatically.
// We turn on an LED as visual indicator and halt. On boards with no UART
// wired (USB dongles) the console dump is unreachable, so we also stash the
// fault PC/LR in __noinit RAM — it survives the next replug's soft boot and
// main() prints it, so the crash site is readable over CDC after the fact.
// ============================================================================
#define FAULT_CRUMB_MAGIC 0xFA17C4B5u
__noinit static uint32_t fault_crumb_magic;
__noinit static uint32_t fault_crumb_reason;
__noinit static uint32_t fault_crumb_pc;
__noinit static uint32_t fault_crumb_lr;

// RAM copy for this boot: the noinit magic is consumed (cleared) at boot so
// safe boot applies only to the single boot right after a fault, but the
// crumb keeps re-printing all uptime — printf only reaches CDC once the log
// redirect is installed and a client connects.
static bool     crumb_present = false;
static uint32_t crumb_reason, crumb_pc, crumb_lr;

// Returns true if the previous boot faulted (call once, at boot).
static bool fault_crumb_consume(void)
{
    if (fault_crumb_magic == FAULT_CRUMB_MAGIC) {
        crumb_present = true;
        crumb_reason = fault_crumb_reason;
        crumb_pc = fault_crumb_pc;
        crumb_lr = fault_crumb_lr;
    }
    fault_crumb_magic = 0;
    return crumb_present;
}

// BT stage trace: a tiny noinit event ring written from the BLE peripheral
// path (ble_output.c calls bt_diag_mark). Survives even a hardware LOCKUP
// reset that bypasses every software handler, so after a silent reset the
// boot log shows how far pairing got. Codes are defined at the call sites.
#define BT_TRACE_MAGIC 0xB7D1A600u
#define BT_TRACE_N 64
__noinit static uint32_t bt_trace_magic;
__noinit static uint32_t bt_trace_ring[BT_TRACE_N];
__noinit static uint32_t bt_trace_idx;

void bt_diag_mark(uint32_t code)
{
    if (bt_trace_magic != BT_TRACE_MAGIC) {
        for (int i = 0; i < BT_TRACE_N; i++) bt_trace_ring[i] = 0;
        bt_trace_idx = 0;
        bt_trace_magic = BT_TRACE_MAGIC;
    }
    bt_trace_ring[bt_trace_idx % BT_TRACE_N] = code;
    bt_trace_idx++;
    // Live view for app-level milestones (0xC...) and low-rate LE meta events
    // (0xE03E: connection complete, param/PHY updates). Per-packet marks
    // (other HCI events, ACL 0xACC0) stay ring-only — printf in the
    // cooperative BTstack thread is a polled-UART stall.
    if ((code >> 28) == 0xC || (code >> 16) == 0xE03E) {
        printf("[btdiag] mark %08x\n", (unsigned)code);
    }
}

// Snapshot of the ring from before this boot's reset, for periodic reprint
// (CDC log clients usually connect well after boot).
static uint32_t bt_trace_snap[BT_TRACE_N];
static uint32_t bt_trace_snap_n = 0, bt_trace_snap_total = 0;

static void bt_trace_consume(void)
{
    if (bt_trace_magic != BT_TRACE_MAGIC || bt_trace_idx == 0) return;
    uint32_t n = bt_trace_idx < BT_TRACE_N ? bt_trace_idx : BT_TRACE_N;
    uint32_t start = bt_trace_idx - n;
    for (uint32_t i = 0; i < n; i++) {
        bt_trace_snap[i] = bt_trace_ring[(start + i) % BT_TRACE_N];
    }
    bt_trace_snap_n = n;
    bt_trace_snap_total = bt_trace_idx;
    // Reset the ring for this boot's marks
    bt_trace_idx = 0;
    for (int i = 0; i < BT_TRACE_N; i++) bt_trace_ring[i] = 0;
}

// On-demand dump of the LIVE ring (this boot's marks) — BT.TRACE command.
void bt_diag_dump(void)
{
    if (bt_trace_magic != BT_TRACE_MAGIC || bt_trace_idx == 0) {
        printf("[btdiag] live trace: empty\n");
        return;
    }
    uint32_t n = bt_trace_idx < BT_TRACE_N ? bt_trace_idx : BT_TRACE_N;
    uint32_t start = bt_trace_idx - n;
    printf("[btdiag] live trace (%u marks, oldest first):",
           (unsigned)bt_trace_idx);
    for (uint32_t i = 0; i < n; i++) {
        printf(" %08x", (unsigned)bt_trace_ring[(start + i) % BT_TRACE_N]);
    }
    printf("\n");
}

static void bt_trace_report(void)
{
    if (bt_trace_snap_n == 0) return;
    printf("[btdiag] trace before reset (%u marks, oldest first):",
           (unsigned)bt_trace_snap_total);
    for (uint32_t i = 0; i < bt_trace_snap_n; i++) {
        printf(" %08x", (unsigned)bt_trace_snap[i]);
    }
    printf("\n");
}

static void fault_crumb_report(void)
{
    if (crumb_present) {
        printf("[fault] PREVIOUS BOOT FAULTED: reason=%u pc=0x%08x lr=0x%08x\n",
               (unsigned)crumb_reason, (unsigned)crumb_pc, (unsigned)crumb_lr);
    }
}

// Newlib assert() → abort() → k_panic loses the assert location (its message
// goes to the unwired UART). Capture file+line in the crumb and reboot:
// reason=0xA5, pc=line, lr=first 4 chars of the file's basename.
void __assert_func(const char *file, int line, const char *func,
                   const char *expr)
{
    (void)func; (void)expr;
    fault_crumb_magic = FAULT_CRUMB_MAGIC;
    fault_crumb_reason = 0xA5;
    fault_crumb_pc = (uint32_t)line;
    fault_crumb_lr = 0;
    if (file) {
        const char *base = file;
        for (const char *p = file; *p; p++) {
            if (*p == '/') base = p + 1;
        }
        for (int i = 0; i < 4 && base[i]; i++) {
            fault_crumb_lr |= ((uint32_t)(uint8_t)base[i]) << (8 * i);
        }
    }
    NVIC_SystemReset();
    for (;;) { }
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
    fault_crumb_magic = FAULT_CRUMB_MAGIC;
    fault_crumb_reason = reason;
    fault_crumb_pc = esf ? esf->basic.pc : 0;
    fault_crumb_lr = esf ? esf->basic.lr : 0;
#ifdef BOARD_FEATHER_NRF52840
    // Blue LED on Feather = P1.10, active high
    NRF_P1->DIRSET = (1U << 10);
    NRF_P1->OUTSET = (1U << 10);  // LED on (active high)
#elif defined(BOARD_MAKERDIARY_NRF52840)
    // Blue LED on MDK dongle = P0.24, active low
    NRF_P0->DIRSET = (1U << 24);
    NRF_P0->OUTCLR = (1U << 24);  // LED on (active low)
#else
    // Blue LED on XIAO BLE = P0.06, active low
    NRF_P0->DIRSET = (1U << 6);
    NRF_P0->OUTCLR = (1U << 6);   // LED on (active low)
#endif
    // Hold the LED visibly, then reboot so the crumb gets printed (halting
    // forever just strands the dongle until a replug).
    for (volatile uint32_t i = 0; i < 16000000; i++) { __NOP(); }
    NVIC_SystemReset();
    for (;;) { __WFI(); }
}

// ============================================================================
// BT CONTROLLER ASSERT HANDLER
// ============================================================================

void bt_ctlr_assert_handle(char *file, uint32_t line)
{
    // SDC asserts can fire from radio ISR context; printf-and-return leaves
    // the controller in an undefined state (observed as a hard lockup, USB
    // gone). Record it in the fault crumb (reason 0xB7 tag; pc=line, lr=first
    // 4 chars of the file name) and reboot — next boot is a safe boot that
    // reports it over CDC.
    fault_crumb_magic = FAULT_CRUMB_MAGIC;
    fault_crumb_reason = 0xB7;
    fault_crumb_pc = line;
    fault_crumb_lr = 0;
    if (file) {
        for (int i = 0; i < 4 && file[i]; i++) {
            fault_crumb_lr |= ((uint32_t)(uint8_t)file[i]) << (8 * i);
        }
    }
    NVIC_SystemReset();
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

#if defined(CONFIG_UNIVERSAL) && defined(CONFIG_BOARD_XIAO_BLE)
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
#endif  // CONFIG_UNIVERSAL && CONFIG_BOARD_XIAO_BLE

// ============================================================================
// MAIN
// ============================================================================

int main(void)
{
#if defined(CONFIG_UNIVERSAL)
    printf("[joypad] Starting universal on Adafruit Feather nRF52840...\n");
#elif defined(CONFIG_BTUSB2USB)
    printf("[joypad] Starting btusb2usb on Adafruit Feather nRF52840...\n");
#elif defined(CONFIG_USB2USB)
    printf("[joypad] Starting usb2usb on Adafruit Feather nRF52840...\n");
#elif defined(BOARD_FEATHER_NRF52840)
    printf("[joypad] Starting bt2usb on Adafruit Feather nRF52840...\n");
#elif defined(BOARD_MAKERDIARY_NRF52840)
    printf("[joypad] Starting bt2usb on Makerdiary nRF52840 MDK USB Dongle...\n");
#else
    printf("[joypad] Starting bt2usb on Seeed XIAO nRF52840...\n");
#endif

    // Safe boot: the previous boot hard-faulted. If the fault is in early
    // init (storage/NVS, BT bring-up) a normal boot crash-loops before USB
    // ever enumerates and the crumb is unreadable. Skip storage/BT init on
    // the single boot after a fault so USB comes up and the crumb reaches
    // the CDC log; the boot after that is normal again.
    bool safe_boot = fault_crumb_consume();
    fault_crumb_report();
    bt_trace_consume();
    bt_trace_report();
    if (safe_boot) {
        printf("[joypad] SAFE BOOT after fault — skipping storage/BT init\n");
    }

    // Initialize shared services
    leds_init();
    if (!safe_boot) storage_init();
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

    // Get and initialize input interfaces (skipped in safe boot: BT init
    // reads the same NVS the crash may involve)
    inputs = app_get_input_interfaces(&input_count);
    for (uint8_t i = 0; i < input_count && !safe_boot; i++) {
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

#ifdef CONFIG_UNIVERSAL
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

#ifdef CONFIG_UNIVERSAL
        imu_task();  // sample onboard IMU → router (throttled to ~100 Hz)
#endif

#if defined(CONFIG_UNIVERSAL) && defined(CONFIG_BOARD_XIAO_BLE)
        power_task();  // low-battery cutoff + idle deep-sleep (protects the cell)
#endif

        // Diagnostic: re-announce a stashed fault crumb every 5s so it reaches
        // a CDC log client no matter when it connects.
        {
            static uint32_t crumb_last_ms = 0;
            uint32_t crumb_now = platform_time_ms();
            if ((uint32_t)(crumb_now - crumb_last_ms) >= 5000u) {
                crumb_last_ms = crumb_now;
                fault_crumb_report();
                bt_trace_report();
            }
        }

        // Yield to other Zephyr threads (BTstack runs in its own thread)
        k_msleep(1);
    }

    return 0;
}
