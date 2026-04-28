// app.c - ControllerBTUSB App Entry Point
// Modular sensor inputs → BLE gamepad + USB device output
//
// Combines the controller app's modular sensor input with usb2ble's
// BLE peripheral output. First sensor: JoyWing (seesaw I2C).

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "core/services/leds/leds.h"
#include "core/services/leds/neopixel/ws2812.h"
#include "core/services/storage/flash.h"
#include "core/services/profiles/profile.h"
#include "core/buttons.h"
#include "platform/platform.h"

#include "tusb.h"
#include <stdio.h>

#ifdef BTSTACK_USE_CYW43
#include "pico/cyw43_arch.h"
#endif

#if REQUIRE_BLE_OUTPUT
#include "bt/ble_output/ble_output.h"
#include "bt/transport/bt_transport.h"

#ifdef BTSTACK_USE_CYW43
// Pico W CYW43 BLE transport
extern const bt_transport_t bt_transport_cyw43;
typedef void (*bt_cyw43_post_init_fn)(void);
extern void bt_cyw43_set_post_init(bt_cyw43_post_init_fn fn);
#endif

#ifdef BTSTACK_USE_ESP32
// ESP32 BLE transport
extern const bt_transport_t bt_transport_esp32;
typedef void (*bt_esp32_post_init_fn)(void);
extern void bt_esp32_set_post_init(bt_esp32_post_init_fn fn);
#endif

#ifdef BTSTACK_USE_NRF
// nRF52840 BLE transport
extern const bt_transport_t bt_transport_nrf;
typedef void (*bt_nrf_post_init_fn)(void);
extern void bt_nrf_set_post_init(bt_nrf_post_init_fn fn);
#endif

// BTstack APIs for bond management (available after ble_output_late_init)
extern void gap_delete_all_link_keys(void);
extern void gap_advertisements_enable(int enabled);
extern int le_device_db_max_count(void);
extern void le_device_db_remove(int index);
#endif

#if REQUIRE_BT_INPUT
#include "bt/btstack/btstack_host.h"
#if !REQUIRE_BLE_OUTPUT
// Need gap APIs even without BLE output for bond management
extern void gap_delete_all_link_keys(void);
extern int le_device_db_max_count(void);
extern void le_device_db_remove(int index);
#endif
#endif

// USB host input (conditional)
#ifndef DISABLE_USB_HOST
#include "usb/usbh/usbh.h"
#endif

// Sensor inputs (conditional)
#ifdef SENSOR_JOYWING
#include "drivers/joywing/joywing_input.h"
#endif

#ifdef CONFIG_PAD_INPUT
#include "pad/pad_config_flash.h"
#endif
#ifdef PAD_CONFIG_BOOT_WATCHDOG
#include "hardware/watchdog.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/watchdog.h"
#endif
#ifdef SENSOR_PAD
#include "pad/pad_input.h"
#ifdef PAD_CONFIG_ABB
#include "pad/configs/abb.h"
#elif defined(PAD_CONFIG_FISHERPRICE_V1)
#include "pad/configs/fisherprice.h"
#define PAD_CONFIG_DEFAULT pad_config_fisherprice_v1
#elif defined(PAD_CONFIG_FISHERPRICE_V2)
#include "pad/configs/fisherprice.h"
#define PAD_CONFIG_DEFAULT pad_config_fisherprice_v2
#elif defined(PAD_CONFIG_ALPAKKA)
#include "pad/configs/alpakka.h"
#define PAD_CONFIG_DEFAULT pad_config_alpakka
#endif
#endif

// OLED display + animation (conditional)
#if defined(OLED_I2C_INST) || defined(OLED_I2C_DISPLAY)
#include "core/services/display/display.h"
#include "core/services/display/eyes_anim.h"
#include "core/services/display/joy_anim.h"
#include "usb/usbd/cdc/cdc_commands.h"
#ifdef OLED_BUTTON_B_PIN
#include "hardware/gpio.h"
#endif

// FeatherWing display modes — paged with the OLED's B (center) button.
// Future plan: A=up, B=confirm, C=down for menu navigation. For now B
// just cycles modes.
typedef enum {
    OLED_MODE_EYES = 0,
    OLED_MODE_JOY,
    OLED_MODE_INFO,    // USB mode + connected device + analog values + button marquee
    OLED_MODE_COUNT,
} oled_mode_t;
static oled_mode_t oled_current_mode = OLED_MODE_EYES;

// Arrow glyphs for the marquee (chars 1-4 in the built-in font).
#define ARROW_UP    "\x01"
#define ARROW_DOWN  "\x02"
#define ARROW_LEFT  "\x03"
#define ARROW_RIGHT "\x04"

typedef struct { uint32_t mask; const char* name; } oled_button_name_t;
static const oled_button_name_t oled_button_names[] = {
    { JP_BUTTON_DU, ARROW_UP },     { JP_BUTTON_DR, ARROW_RIGHT },
    { JP_BUTTON_DD, ARROW_DOWN },   { JP_BUTTON_DL, ARROW_LEFT },
    { JP_BUTTON_B1, "B1" }, { JP_BUTTON_B2, "B2" },
    { JP_BUTTON_B3, "B3" }, { JP_BUTTON_B4, "B4" },
    { JP_BUTTON_L1, "L1" }, { JP_BUTTON_R1, "R1" },
    { JP_BUTTON_L2, "L2" }, { JP_BUTTON_R2, "R2" },
    { JP_BUTTON_S1, "S1" }, { JP_BUTTON_S2, "S2" },
    { JP_BUTTON_L3, "L3" }, { JP_BUTTON_R3, "R3" },
    { JP_BUTTON_A1, "A1" }, { JP_BUTTON_A2, "A2" },
    { 0, NULL }
};

static const char* oled_transport_str(input_transport_t t) {
    switch (t) {
        case INPUT_TRANSPORT_USB:        return "USB";
        case INPUT_TRANSPORT_BT_CLASSIC: return "BT";
        case INPUT_TRANSPORT_BT_BLE:     return "BLE";
        case INPUT_TRANSPORT_NATIVE:     return "Native";
        case INPUT_TRANSPORT_I2C:        return "I2C";
        case INPUT_TRANSPORT_GPIO:       return "GPIO";
        default:                         return "?";
    }
}
#endif

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

#if REQUIRE_BT_INPUT
static bool bt_input_enabled = false;
#endif

// Check if USB is actively connected as a gamepad (mounted + not in CDC config mode)
static bool usb_gamepad_active(void)
{
    return tud_mounted() && usbd_get_mode() != USB_OUTPUT_MODE_CDC;
}

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
#if REQUIRE_BT_INPUT
            if (bt_input_enabled) {
                // If a controller is already connected, scan for 60s then stop.
                // If nothing connected, scan indefinitely until one is found.
                extern uint8_t btstack_classic_get_connection_count(void);
                bool has_device = (btstack_classic_get_connection_count() > 0);
#ifndef DISABLE_USB_HOST
                extern uint8_t usbh_get_device_count(void);
                has_device = has_device || (usbh_get_device_count() > 0);
#endif
                if (has_device) {
                    printf("[app:controller_btusb] Starting 60s BT scan...\n");
                    btstack_host_start_timed_scan(60000);
                } else {
                    printf("[app:controller_btusb] No devices — scanning until connected...\n");
                    extern void btstack_host_suppress_scan(bool suppress);
                    btstack_host_suppress_scan(false);
                    btstack_host_start_scan();
                }
            }
#endif
#if REQUIRE_BLE_OUTPUT
            printf("[app:controller_btusb] BLE: %s (%s), USB: %s (%s)\n",
                   ble_output_get_mode_name(ble_output_get_mode()),
                   ble_output_is_connected() ? "connected" : "advertising",
                   usbd_get_mode_name(usbd_get_mode()),
                   tud_mounted() ? "mounted" : "disconnected");
#else
            printf("[app:controller_btusb] USB: %s (%s)\n",
                   usbd_get_mode_name(usbd_get_mode()),
                   tud_mounted() ? "mounted" : "disconnected");
#endif
            break;

        case BUTTON_EVENT_DOUBLE_CLICK: {
#if REQUIRE_BLE_OUTPUT
            if (ble_output_is_connected() || !usb_gamepad_active()) {
                // BLE connected or no active USB gamepad → cycle BLE mode
                ble_output_mode_t next = ble_output_get_next_mode();
                printf("[app:controller_btusb] Double-click - BLE mode → %s\n",
                       ble_output_get_mode_name(next));
                ble_output_set_mode(next);  // Saves to flash + reboots
            } else
#endif
            {
                // USB gamepad active → cycle USB output mode
                usb_output_mode_t next = usbd_get_next_mode();
                printf("[app:controller_btusb] Double-click - USB mode → %s\n",
                       usbd_get_mode_name(next));
                usbd_set_mode(next);
            }
            break;
        }

        case BUTTON_EVENT_TRIPLE_CLICK:
            // Reset USB output mode to SInput (default gamepad mode)
            printf("[app:controller_btusb] Triple-click - resetting USB mode to SInput\n");
            usbd_set_mode(USB_OUTPUT_MODE_SINPUT);
            break;

        case BUTTON_EVENT_HOLD:
#if REQUIRE_BLE_OUTPUT || REQUIRE_BT_INPUT
            printf("[app:controller_btusb] Long press - clearing BLE bonds\n");
#if REQUIRE_BT_INPUT
            btstack_host_disconnect_all_devices();
            btstack_host_delete_all_bonds();
#endif
            gap_delete_all_link_keys();
            for (int i = 0; i < le_device_db_max_count(); i++) {
                le_device_db_remove(i);
            }
#if REQUIRE_BLE_OUTPUT
            printf("[app:controller_btusb] Bonds cleared, restarting advertising\n");
            gap_advertisements_enable(1);
#endif
#else
            printf("[app:controller_btusb] Long press (no BLE on this board)\n");
#endif
            break;

        default:
            break;
    }
}

// SInput RGB LED override: when true, host-sent RGB commands control NeoPixel
static bool sinput_rgb_override = false;

#if REQUIRE_BT_INPUT
// Post-init callback: set up BLE Central (input scanning) if enabled
static void bt_central_post_init(void)
{
#if REQUIRE_BLE_OUTPUT
    ble_output_late_init();
#endif
    // Always init HID handlers so bond management (forget, status) works
    // even when BT scanning is disabled. Only start scanning if enabled.
    btstack_host_init_hid_handlers();
    if (bt_input_enabled) {
        btstack_host_start_timed_scan(60000);
        printf("[app:controller_btusb] BT Central enabled, scanning...\n");
    } else {
        printf("[app:controller_btusb] BT Central disabled\n");
    }
}
#endif

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
#ifndef DISABLE_USB_HOST
    &usbh_input_interface,
#endif
#ifdef SENSOR_PAD
    &pad_input_interface,
#endif
#ifdef SENSOR_JOYWING
    &joywing_input_interface,
#endif
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

static const OutputInterface* output_interfaces[] = {
#if REQUIRE_BLE_OUTPUT
    &ble_output_interface,
#endif
    &usbd_output_interface,
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
    printf("[app:controller_btusb] Initializing ControllerBTUSB v%s\n", APP_VERSION);

    // Initialize button service
    button_init();
    button_set_callback(on_button_event);

    // Initialize pad config storage (needed for CDC commands on all platforms)
#ifdef CONFIG_PAD_INPUT
    pad_config_flash_init();
#ifdef PAD_CONFIG_BOOT_WATCHDOG
    // Boot-attempts counter lives in watchdog scratch (preserved across
    // soft / watchdog reboots). If the prior boot crashed, the HW
    // watchdog (enabled below) reboots the chip without resetting the
    // counter, so we accumulate failures here. After 3 in a row, wipe
    // the saved pad config so we can recover; reset to 0 once the main
    // loop has been running long enough to call init "succeeded".
    {
        uint32_t attempts = watchdog_hw->scratch[0];
        if (attempts >= 3) {
            pad_config_reset();
            printf("[app:controller_btusb] Boot watchdog: %u failed boots — wiped pad config\n",
                   (unsigned)attempts);
            attempts = 0;
        }
        watchdog_hw->scratch[0] = attempts + 1;
    }
    // Enable HW watchdog (8s) — if init hangs (e.g. seesaw I2C never
    // ACKs), the chip reboots and the counter above accumulates.
    watchdog_enable(8000, true);
#endif
    {
        // Apply LED config and settings from pad config
        const pad_device_config_t* led_cfg = pad_config_load_runtime();
        if (led_cfg) {
            if (led_cfg->led_pin >= 0) {
                neopixel_set_pin(led_cfg->led_pin);
                neopixel_init();  // Reinitialize with new pin
            } else if (led_cfg->led_pin == -1) {
                neopixel_disable();
            }
            sinput_rgb_override = led_cfg->sinput_rgb;
        }
    }
#endif

    // Configure pad input (GPIO buttons) — flash config overrides compile-time default
#ifdef SENSOR_PAD
    const pad_device_config_t* pad_cfg = pad_config_load_runtime();
#ifdef PAD_CONFIG_ABB
    if (!pad_cfg) pad_cfg = &pad_config_abb;
#elif defined(PAD_CONFIG_DEFAULT)
    if (!pad_cfg) pad_cfg = &PAD_CONFIG_DEFAULT;
#endif
    if (pad_cfg) {
        pad_input_add_device(pad_cfg);
        printf("[app:controller_btusb] Pad: %s (%s)\n", pad_cfg->name,
               pad_config_has_custom() ? "flash" : "default");
#ifndef DISABLE_USB_HOST
        // Set PIO-USB D+ pin from pad config (before usbh_init runs)
        if (pad_cfg->usb_host_dp >= 0) {
            usbh_set_pio_dp_pin(pad_cfg->usb_host_dp);
        }
#endif
    }
#endif

    // Configure sensor inputs
#ifdef SENSOR_JOYWING
    {
        // Use runtime config if available, else compile-time defaults
        bool jw_configured = false;
#ifdef CONFIG_PAD_INPUT
        {
            // Load pad config from flash for JoyWing settings
            const pad_device_config_t* jw_pad_cfg;
#ifdef SENSOR_PAD
            jw_pad_cfg = pad_cfg;
#else
            jw_pad_cfg = pad_config_load_runtime();
#endif
            if (jw_pad_cfg) {
            // Flash config exists — use it (even if all JoyWings are disabled)
            jw_configured = true;  // Don't fall back to compile-time defaults
            for (int i = 0; i < 2; i++) {
                if (jw_pad_cfg->joywing[i].sda >= 0) {
                    joywing_config_t jw_cfg = {
                        .i2c_bus = jw_pad_cfg->joywing[i].i2c_bus,
                        .sda_pin = jw_pad_cfg->joywing[i].sda,
                        .scl_pin = jw_pad_cfg->joywing[i].scl,
                        .addr = jw_pad_cfg->joywing[i].addr,
                    };
                    joywing_input_init_config(&jw_cfg);
                    printf("[app:controller_btusb] JoyWing %d (bus=%d, SDA=%d, SCL=%d, addr=0x%02X)\n",
                           i, jw_cfg.i2c_bus, jw_cfg.sda_pin, jw_cfg.scl_pin, jw_pad_cfg->joywing[i].addr);
                }
            }
            }
        }
#endif
        if (!jw_configured) {
            printf("[app:controller_btusb] No JoyWing config in flash (configure via web config)\n");
        }

#ifdef SENSOR_PAD
        // When both pad and JoyWing are active, merge JoyWing into pad's event
        if (jw_configured) {
            joywing_set_merge_with_pad(true);
            printf("[app:controller_btusb] JoyWing merging with pad input\n");
        }
#endif
    }
#endif

    // Configure router: merge all sensor inputs to outputs
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
#if REQUIRE_BLE_OUTPUT
            [OUTPUT_TARGET_BLE_PERIPHERAL] = 1,
#endif
            [OUTPUT_TARGET_USB_DEVICE] = 1,
        },
        .merge_all_inputs = true,
        .transform_flags = TRANSFORM_FLAGS,
    };

    // Override router settings from flash (only if user has explicitly saved)
    {
        flash_t flash_data;
        if (flash_load(&flash_data) && flash_data.router_saved) {
            if (flash_data.routing_mode <= 2) router_cfg.mode = flash_data.routing_mode;
            if (flash_data.merge_mode <= 2) router_cfg.merge_mode = flash_data.merge_mode;
            if (flash_data.dpad_mode <= 2) router_set_dpad_mode(flash_data.dpad_mode);
#if REQUIRE_BT_INPUT
            bt_input_enabled = flash_data.bt_input_enabled != 0;
#endif
        }
    }

    router_init(&router_cfg);

    // Load combo hotkeys from pad config into router
#ifdef CONFIG_PAD_INPUT
    {
        const pad_device_config_t* cfg = pad_config_load_runtime();
        if (cfg) {
            for (int c = 0; c < PAD_COMBO_MAX && c < ROUTER_COMBO_MAX; c++) {
                if (cfg->combo[c].input_mask) {
                    router_set_combo(c, cfg->combo[c].input_mask, cfg->combo[c].output_mask);
                }
            }
        }
    }
#endif

#if REQUIRE_BLE_OUTPUT
    // Route: GPIO (sensors) → BLE Peripheral
    router_add_route(INPUT_SOURCE_GPIO, OUTPUT_TARGET_BLE_PERIPHERAL, 0);
#endif

    // Route: GPIO (sensors) → USB Device (CDC config + wired gamepad)
    router_add_route(INPUT_SOURCE_GPIO, OUTPUT_TARGET_USB_DEVICE, 0);

#ifndef DISABLE_USB_HOST
    // Route: USB Host controllers → USB Device
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_USB_DEVICE, 0);
#if REQUIRE_BLE_OUTPUT
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_BLE_PERIPHERAL, 0);
#endif
#endif

#if REQUIRE_BT_INPUT
    // Route: BLE Central (scanned controllers) → USB Device
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_USB_DEVICE, 0);
#if REQUIRE_BLE_OUTPUT
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_BLE_PERIPHERAL, 0);
#endif
#endif

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

#if REQUIRE_BLE_OUTPUT || REQUIRE_BT_INPUT
    // Initialize BLE transport with post-init callback.
    // Post-init runs in BTstack task context after HCI is ready.
#if REQUIRE_BLE_OUTPUT
    ble_output_init();  // Load BLE mode from flash before BTstack starts
#endif

    // Select post-init callback
#if REQUIRE_BT_INPUT
    #define BT_POST_INIT bt_central_post_init
#elif REQUIRE_BLE_OUTPUT
    #define BT_POST_INIT ble_output_late_init
#endif

#ifdef BTSTACK_USE_CYW43
    bt_cyw43_set_post_init(BT_POST_INIT);
    bt_init(&bt_transport_cyw43);
#elif defined(BTSTACK_USE_ESP32)
    bt_esp32_set_post_init(BT_POST_INIT);
    bt_init(&bt_transport_esp32);
#elif defined(BTSTACK_USE_NRF)
    bt_nrf_set_post_init(BT_POST_INIT);
    bt_init(&bt_transport_nrf);
#endif
#endif

#ifdef OLED_I2C_INST
    // Initialize OLED display (RP2040 — explicit I2C config)
    {
        display_i2c_config_t oled_cfg = {
            .i2c_inst = OLED_I2C_INST,
            .pin_sda = OLED_I2C_SDA_PIN,
            .pin_scl = OLED_I2C_SCL_PIN,
            .addr = OLED_I2C_ADDR,
        };
        display_init_i2c(&oled_cfg);
    }
    if (display_is_initialized()) {
        eyes_anim_init();
        eyes_anim_event(EYES_EVENT_BOOT);
        joy_anim_init();
        joy_anim_event(JOY_EVENT_BOOT);
#ifdef OLED_BUTTON_B_PIN
        // B (center) cycles display modes. A and C set up too with
        // pull-ups so they're ready for future menu navigation.
        gpio_init(OLED_BUTTON_B_PIN);
        gpio_set_dir(OLED_BUTTON_B_PIN, GPIO_IN);
        gpio_pull_up(OLED_BUTTON_B_PIN);
#ifdef OLED_BUTTON_A_PIN
        gpio_init(OLED_BUTTON_A_PIN);
        gpio_set_dir(OLED_BUTTON_A_PIN, GPIO_IN);
        gpio_pull_up(OLED_BUTTON_A_PIN);
#endif
#ifdef OLED_BUTTON_C_PIN
        gpio_init(OLED_BUTTON_C_PIN);
        gpio_set_dir(OLED_BUTTON_C_PIN, GPIO_IN);
        gpio_pull_up(OLED_BUTTON_C_PIN);
#endif
#endif
        printf("[app:controller_btusb] OLED + eyes/joy animations initialized\n");
    } else {
        printf("[app:controller_btusb] No OLED detected — display features disabled\n");
    }
#elif defined(OLED_I2C_DISPLAY)
    // Initialize OLED display (nRF — I2C configured via devicetree)
    {
        display_i2c_config_t oled_cfg = {
            .i2c_inst = 0,
            .pin_sda = 0,
            .pin_scl = 0,
            .addr = 0x3C,
        };
        display_init_i2c(&oled_cfg);
    }
    if (display_is_initialized()) {
        eyes_anim_init();
        eyes_anim_event(EYES_EVENT_BOOT);
        printf("[app:controller_btusb] OLED + eyes animation initialized (I2C)\n");
    } else {
        printf("[app:controller_btusb] No OLED detected — display features disabled\n");
    }
#endif

    // Initialize profile system (no built-in profiles, custom only)
    static const profile_config_t profile_cfg = {
        .output_profiles = { NULL },
        .shared_profiles = NULL,
    };
    profile_init(&profile_cfg);

    printf("[app:controller_btusb] Initialization complete\n");
    printf("[app:controller_btusb]   Routing: Sensors → %sUSB Device\n",
           REQUIRE_BLE_OUTPUT ? "BLE Peripheral + " : "");
    printf("[app:controller_btusb]   Player slots: %d\n", MAX_PLAYER_SLOTS);
}

// ============================================================================
// APP TASK (Called from main loop)
// ============================================================================

void app_task(void)
{
#ifdef PAD_CONFIG_BOOT_WATCHDOG
    // Feed the boot-attempt watchdog every iteration. After 5s of
    // running normally, mark this boot as "succeeded" by zeroing the
    // attempts counter so the next boot starts fresh.
    watchdog_update();
    static bool boot_marked_ok = false;
    if (!boot_marked_ok && platform_time_ms() > 5000) {
        watchdog_hw->scratch[0] = 0;
        boot_marked_ok = true;
        printf("[app:controller_btusb] Boot watchdog: marked successful\n");
    }
#endif

    // Process button input
    button_task();

    // Suppress BT scanning when a USB host device is connected (avoid
    // unnecessary radio activity). Unsuppress when USB device disconnects
    // so scanning resumes. User can always override via button press
    // (start_timed_scan clears suppression).
#if REQUIRE_BT_INPUT && !defined(DISABLE_USB_HOST)
    {
        extern uint8_t usbh_get_device_count(void);
        extern void btstack_host_suppress_scan(bool suppress);
        static uint8_t last_usb_host_count = 0;
        uint8_t usb_count = usbh_get_device_count();
        if (usb_count > 0 && last_usb_host_count == 0) {
            btstack_host_suppress_scan(true);
            printf("[app] USB device connected — suppressing BT scan\n");
        } else if (usb_count == 0 && last_usb_host_count > 0) {
            btstack_host_suppress_scan(false);
            printf("[app] USB device disconnected — resuming BT scan\n");
        }
        last_usb_host_count = usb_count;
    }
#endif

#if REQUIRE_BLE_OUTPUT
    // Process BLE transport
    bt_task();

    // NeoPixel: show connection state and active output mode color
    bool ble_conn = ble_output_is_connected();
    bool usb_active = usb_gamepad_active();
    leds_set_connected_devices((ble_conn || usb_active) ? 1 : 0);

    // Track state changes for LED color updates
    static bool last_ble_conn = false;
    static bool last_usb_active = false;
    static ble_output_mode_t last_ble_mode = BLE_MODE_COUNT;
    static usb_output_mode_t last_usb_mode = USB_OUTPUT_MODE_COUNT;

    ble_output_mode_t ble_mode = ble_output_get_mode();
    usb_output_mode_t usb_mode = usbd_get_mode();

    if (ble_conn != last_ble_conn || usb_active != last_usb_active ||
        ble_mode != last_ble_mode || usb_mode != last_usb_mode) {
        last_ble_conn = ble_conn;
        last_usb_active = usb_active;
        last_ble_mode = ble_mode;
        last_usb_mode = usb_mode;

        uint8_t r, g, b;
        if (ble_conn) {
            ble_output_get_mode_color(ble_mode, &r, &g, &b);
        } else if (usb_active) {
            usbd_get_mode_color(usb_mode, &r, &g, &b);
        } else {
            ble_output_get_mode_color(ble_mode, &r, &g, &b);
        }
        leds_set_color(r, g, b);
    }

    // SInput RGB LED override: host-sent RGB overrides mode color
    if (sinput_rgb_override) {
        output_feedback_t fb = {0};
        if (usbd_output_interface.get_feedback && usbd_output_interface.get_feedback(&fb)) {
            if (fb.led_r || fb.led_g || fb.led_b) {
                leds_set_color(fb.led_r, fb.led_g, fb.led_b);
            }
        }
    }
#else
    // USB-only: show USB mode color
    bool usb_active = usb_gamepad_active();
    leds_set_connected_devices(usb_active ? 1 : 0);

    static bool last_usb_active = false;
    static usb_output_mode_t last_usb_mode = USB_OUTPUT_MODE_COUNT;
    usb_output_mode_t usb_mode = usbd_get_mode();

    if (usb_active != last_usb_active || usb_mode != last_usb_mode) {
        last_usb_active = usb_active;
        last_usb_mode = usb_mode;

        uint8_t r, g, b;
        usbd_get_mode_color(usb_mode, &r, &g, &b);
        leds_set_color(r, g, b);
    }

    // SInput RGB LED override: host-sent RGB overrides mode color
    if (sinput_rgb_override) {
        output_feedback_t fb = {0};
        if (usbd_output_interface.get_feedback && usbd_output_interface.get_feedback(&fb)) {
            if (fb.led_r || fb.led_g || fb.led_b) {
                leds_set_color(fb.led_r, fb.led_g, fb.led_b);
            }
        }
    }
#endif

#if defined(OLED_I2C_INST) || defined(OLED_I2C_DISPLAY)
    // Eyes animation: feed input events from the connected pad → eyes.
    // Same per-button emotion mapping + L2/R2 eyelids + staged idle as
    // the usb2usb app, but driven by the local pad through the router.
    if (display_is_initialized()) {
        static uint32_t last_buttons = 0;
        static bool last_connected = false;
        static bool ever_connected = false;
        static uint32_t last_activity_ms = 0;
        static uint32_t last_render_ms = 0;

        // Per-button emotion table (same as usb2usb).
        static const struct { uint32_t mask; eyes_state_t state; } eyes_button_map[] = {
            { JP_BUTTON_B1, EYES_STATE_HAPPY      },
            { JP_BUTTON_B2, EYES_STATE_ANGRY      },
            { JP_BUTTON_B3, EYES_STATE_SUSPICIOUS },
            { JP_BUTTON_B4, EYES_STATE_EXCITED    },
            { JP_BUTTON_L1, EYES_STATE_WINK_L     },
            { JP_BUTTON_R1, EYES_STATE_WINK_R     },
            { JP_BUTTON_S1, EYES_STATE_SAD        },
            { JP_BUTTON_S2, EYES_STATE_SURPRISED  },
            { JP_BUTTON_A1, EYES_STATE_EXCITED    },
            { JP_BUTTON_A2, EYES_STATE_CONFUSED   },
            { JP_BUTTON_L3, EYES_STATE_CONFUSED   },
            { JP_BUTTON_R3, EYES_STATE_CONFUSED   },
        };
        static const size_t eyes_button_map_count =
            sizeof(eyes_button_map) / sizeof(eyes_button_map[0]);
        static uint32_t held_emotion_mask = 0;
        if (held_emotion_mask == 0) {
            for (size_t i = 0; i < eyes_button_map_count; i++) {
                held_emotion_mask |= eyes_button_map[i].mask;
            }
        }

        uint32_t now = platform_time_ms();
        const input_event_t* ev = router_get_output(OUTPUT_TARGET_USB_DEVICE, 0);

        bool connected = ev && (ev->buttons != 0
                                || ev->analog[ANALOG_LX] < 100 || ev->analog[ANALOG_LX] > 156
                                || ev->analog[ANALOG_LY] < 100 || ev->analog[ANALOG_LY] > 156);
        if (connected) ever_connected = true;

        if (ever_connected && !last_connected) {
            eyes_anim_event(EYES_EVENT_CONNECT);
            last_connected = true;
            last_activity_ms = now;
        }

        if (ev && ever_connected) {
            // Stick gaze + activity (±10 deadzone keeps drift quiet).
            uint8_t lx_raw = ev->analog[ANALOG_LX];
            uint8_t ly_raw = ev->analog[ANALOG_LY];
            int16_t dx = (int16_t)lx_raw - 128;
            int16_t dy = (int16_t)ly_raw - 128;
            if (dx < -10 || dx > 10 || dy < -10 || dy > 10) {
                eyes_anim_set_look((float)lx_raw / 255.0f, (float)ly_raw / 255.0f);
                last_activity_ms = now;
            }

            // L2/R2 → per-eye eyelids.
            uint8_t l2 = ev->analog[ANALOG_L2];
            uint8_t r2 = ev->analog[ANALOG_R2];
            eyes_anim_set_eyelids((float)l2 / 255.0f, (float)r2 / 255.0f);
            if (l2 > 16 || r2 > 16) last_activity_ms = now;

            // Button press emotion (rising edge), gated by table.
            uint32_t newly_pressed = ~last_buttons & ev->buttons;
            if (newly_pressed) {
                last_activity_ms = now;
                for (size_t i = 0; i < eyes_button_map_count; i++) {
                    if (newly_pressed & eyes_button_map[i].mask) {
                        eyes_anim_set_state(eyes_button_map[i].state);
                        break;
                    }
                }
                // Feed each newly-pressed button name to the marquee for INFO mode.
                for (int i = 0; oled_button_names[i].name != NULL; i++) {
                    if (newly_pressed & oled_button_names[i].mask) {
                        display_marquee_add(oled_button_names[i].name);
                    }
                }
            }
            last_buttons = ev->buttons;
            // Hold while any mapped button is still down.
            eyes_anim_set_held((ev->buttons & held_emotion_mask) != 0);
        }

        // Two-stage idle: ACTIVE → IDLE @10s, IDLE → SLEEP @70s.
        uint32_t idle_for = now - last_activity_ms;
        if (idle_for > 10000 && eyes_anim_get_state() == EYES_STATE_ACTIVE) {
            eyes_anim_event(EYES_EVENT_IDLE_TIMEOUT);
        }
        if (idle_for > 70000 && eyes_anim_get_state() == EYES_STATE_IDLE) {
            eyes_anim_event(EYES_EVENT_IDLE_TIMEOUT);
        }

#ifdef OLED_BUTTON_B_PIN
        // B-button edge detection (active-low with pull-up). Cycle the
        // display mode on press.
        {
            static bool last_b = true;
            static uint32_t debounce_b = 0;
            bool b = gpio_get(OLED_BUTTON_B_PIN);
            if (!b && last_b && (now - debounce_b > 200)) {
                debounce_b = now;
                oled_current_mode = (oled_mode_t)((oled_current_mode + 1) % OLED_MODE_COUNT);
                printf("[app:controller_btusb] OLED mode → %d\n", oled_current_mode);
            }
            last_b = b;
        }
#endif

        if (now - last_render_ms >= 100) {
            last_render_ms = now;
            switch (oled_current_mode) {
                case OLED_MODE_JOY:
                    joy_anim_tick(now);
                    joy_anim_render();
                    break;

                case OLED_MODE_INFO: {
                    display_clear();
                    usb_output_mode_t mode = usbd_get_mode();
                    display_text_large(0, 0, usbd_get_mode_name(mode));
                    display_hline(0, 17, DISPLAY_WIDTH);

                    if (playersCount > 0 && players[0].dev_addr >= 0) {
                        const char* pname = get_player_name(0);
                        if (pname) display_text(0, 20, pname);

                        char info[22];
                        snprintf(info, sizeof(info), "%s dev:%d P%d/%d",
                                 oled_transport_str(players[0].transport),
                                 players[0].dev_addr,
                                 players[0].player_number, playersCount);
                        display_text(0, 30, info);

                        if (ev) {
                            char line[22];
                            snprintf(line, sizeof(line), "L:%02X,%02X R:%02X,%02X T:%02X,%02X",
                                     ev->analog[ANALOG_LX], ev->analog[ANALOG_LY],
                                     ev->analog[ANALOG_RX], ev->analog[ANALOG_RY],
                                     ev->analog[ANALOG_L2], ev->analog[ANALOG_R2]);
                            display_text(0, 40, line);
                        }
                    }

                    display_marquee_tick();
                    display_marquee_render(52);
                    break;
                }

                case OLED_MODE_EYES:
                default:
                    eyes_anim_tick(now);
                    eyes_anim_render();
                    break;
            }
            display_update();
        } else {
            // Tick the active mode each iteration so animation timing
            // stays smooth even between renders.
            if (oled_current_mode == OLED_MODE_JOY) joy_anim_tick(now);
            else if (oled_current_mode == OLED_MODE_EYES) eyes_anim_tick(now);
        }
    }
#endif

    // ----------------------------------------------------------------
    // CYW43 onboard LED status (Pico W only — no regular GPIO LED)
    //
    // Blink:  scanning OR no BT/BLE devices connected
    // Solid:  device(s) connected, not scanning
    // Off:    BT host disabled + no connections, or onboard LED disabled
    //
    // Configurable via web config Feedback > Onboard LED toggle.
    // ----------------------------------------------------------------
#ifdef BTSTACK_USE_CYW43
    {
        static uint32_t cyw43_led_last_toggle = 0;
        static bool cyw43_led_state = false;
        uint32_t now = platform_time_ms();

        // Check if onboard LED is disabled via pad config
        static bool onboard_led_checked = false;
        static bool onboard_led_disabled = false;
        if (!onboard_led_checked) {
            const pad_device_config_t* cfg = pad_config_load_runtime();
            if (cfg && cfg->onboard_led == PAD_ONBOARD_LED_DISABLED) {
                onboard_led_disabled = true;
            }
            onboard_led_checked = true;
        }

        if (onboard_led_disabled) {
            if (cyw43_led_state) {
                cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
                cyw43_led_state = false;
            }
        } else {
            bool any_connected = false;
            bool any_searching = false;
            bool any_host_enabled = false;

#if REQUIRE_BLE_OUTPUT
            if (ble_output_is_connected()) any_connected = true;
#endif
#if REQUIRE_BT_INPUT
            if (bt_input_enabled) {
                any_host_enabled = true;
                extern uint8_t btstack_classic_get_connection_count(void);
                extern bool btstack_host_is_scanning(void);
                if (btstack_classic_get_connection_count() > 0) any_connected = true;
                if (btstack_host_is_scanning()) any_searching = true;
            }
#endif
#ifndef DISABLE_USB_HOST
            {
                extern uint8_t usbh_get_device_count(void);
                // Cache pad config check — don't reload from flash every tick
                static bool usb_host_pin_checked = false;
                static bool usb_host_pin_valid = false;
                if (!usb_host_pin_checked) {
                    const pad_device_config_t* ucfg = pad_config_load_runtime();
                    usb_host_pin_valid = ucfg && ucfg->usb_host_dp >= 0;
                    usb_host_pin_checked = true;
                }
                if (usb_host_pin_valid) {
                    any_host_enabled = true;
                    if (usbh_get_device_count() > 0) any_connected = true;
                    else any_searching = true;
                }
            }
#endif

            if (!any_host_enabled && !any_connected) {
                // Off — no host features enabled, nothing connected
                if (cyw43_led_state) {
                    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
                    cyw43_led_state = false;
                }
            } else if (any_connected && !any_searching) {
                // Solid — device(s) connected, not searching
                if (!cyw43_led_state) {
                    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
                    cyw43_led_state = true;
                }
            } else {
                // Blink — searching (BT scanning or USB host waiting)
                if (now - cyw43_led_last_toggle >= 500) {
                    cyw43_led_state = !cyw43_led_state;
                    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, cyw43_led_state ? 1 : 0);
                    cyw43_led_last_toggle = now;
                }
            }
        }
    }
#endif
}
