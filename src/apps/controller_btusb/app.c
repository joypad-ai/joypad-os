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
#include "core/buttons.h"
#include "platform/platform.h"

#include "tusb.h"
#include <stdio.h>

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

// Sensor inputs (conditional)
#ifdef SENSOR_JOYWING
#include "drivers/joywing/joywing_input.h"
#endif

#ifdef SENSOR_PAD
#include "pad/pad_input.h"
#include "pad/pad_config_flash.h"
#ifdef PAD_CONFIG_ABB
#include "pad/configs/abb.h"
#endif
#endif

// OLED display + Joy animation (conditional)
#if defined(OLED_I2C_INST) || defined(OLED_I2C_DISPLAY)
#include "core/services/display/display.h"
#include "core/services/display/joy_anim.h"
#endif

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

// Check if USB is actively connected as a gamepad (mounted + not in CDC config mode)
static bool usb_gamepad_active(void)
{
    return tud_mounted() && usbd_get_mode() != USB_OUTPUT_MODE_CDC;
}

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
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
#if REQUIRE_BLE_OUTPUT
            printf("[app:controller_btusb] Long press - clearing BLE bonds\n");
            gap_delete_all_link_keys();
            for (int i = 0; i < le_device_db_max_count(); i++) {
                le_device_db_remove(i);
            }
            printf("[app:controller_btusb] Bonds cleared, restarting advertising\n");
            gap_advertisements_enable(1);
#else
            printf("[app:controller_btusb] Long press (no BLE on this board)\n");
#endif
            break;

        default:
            break;
    }
}

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
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

    // Configure pad input (GPIO buttons) — flash config overrides compile-time default
#ifdef SENSOR_PAD
    pad_config_flash_init();
    const pad_device_config_t* pad_cfg = pad_config_load_runtime();
#ifdef PAD_CONFIG_ABB
    if (!pad_cfg) pad_cfg = &pad_config_abb;
#endif
    if (pad_cfg) {
        pad_input_add_device(pad_cfg);
        printf("[app:controller_btusb] Pad: %s (%s)\n", pad_cfg->name,
               pad_config_has_custom() ? "flash" : "default");
    }
#endif

    // Configure sensor inputs
#ifdef SENSOR_JOYWING
    joywing_config_t jw_cfg = {
        .i2c_bus = JOYWING_I2C_BUS,
        .sda_pin = JOYWING_SDA_PIN,
        .scl_pin = JOYWING_SCL_PIN,
    };
    joywing_input_init_config(&jw_cfg);
    printf("[app:controller_btusb] JoyWing sensor configured (bus=%d, SDA=%d, SCL=%d)\n",
           JOYWING_I2C_BUS, JOYWING_SDA_PIN, JOYWING_SCL_PIN);
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
    router_init(&router_cfg);

#if REQUIRE_BLE_OUTPUT
    // Route: GPIO (sensors) → BLE Peripheral
    router_add_route(INPUT_SOURCE_GPIO, OUTPUT_TARGET_BLE_PERIPHERAL, 0);
#endif

    // Route: GPIO (sensors) → USB Device (CDC config + wired gamepad)
    router_add_route(INPUT_SOURCE_GPIO, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

#if REQUIRE_BLE_OUTPUT
    // Load BLE output mode from flash BEFORE starting BTstack task.
    // The BTstack task calls ble_output_late_init() asynchronously,
    // which needs current_mode to already be set from flash settings.
    // (main.c output init loop will call this again — harmless double-init.)
    ble_output_init();

    // Initialize BLE transport in peripheral mode.
    // Set post-init callback so ble_output_late_init() runs in the BTstack task context.
#ifdef BTSTACK_USE_CYW43
    bt_cyw43_set_post_init(ble_output_late_init);
    bt_init(&bt_transport_cyw43);
#elif defined(BTSTACK_USE_ESP32)
    bt_esp32_set_post_init(ble_output_late_init);
    bt_init(&bt_transport_esp32);
#elif defined(BTSTACK_USE_NRF)
    bt_nrf_set_post_init(ble_output_late_init);
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
    joy_anim_init();
    joy_anim_event(JOY_EVENT_BOOT);
    printf("[app:controller_btusb] OLED + Joy animation initialized\n");
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
    joy_anim_init();
    joy_anim_event(JOY_EVENT_BOOT);
    printf("[app:controller_btusb] OLED + Joy animation initialized (I2C)\n");
#endif

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
    // Process button input
    button_task();

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
#endif

#if defined(OLED_I2C_INST) || defined(OLED_I2C_DISPLAY)
    // Joy animation: feed input events from JoyWing → Joy
    {
        static uint32_t last_buttons = 0;
        static bool last_connected = false;
        static uint32_t last_activity_ms = 0;

        uint32_t now = platform_time_ms();
        const input_event_t* ev = router_get_output(OUTPUT_TARGET_USB_DEVICE, 0);

        // Detect JoyWing connect/disconnect
        bool connected = (ev && ev->buttons != 0) ||
                         (ev && (ev->analog[ANALOG_LX] < 100 || ev->analog[ANALOG_LX] > 156 ||
                                 ev->analog[ANALOG_LY] < 100 || ev->analog[ANALOG_LY] > 156));
        // Latch connected once any input arrives
        static bool ever_connected = false;
        if (connected) ever_connected = true;

        if (ever_connected && !last_connected) {
            joy_anim_event(JOY_EVENT_CONNECT);
            last_connected = true;
        }

        if (ev && ever_connected) {
            // Analog stick → look direction (0-255 → 0.0-1.0)
            float lx = ev->analog[ANALOG_LX] / 255.0f;
            float ly = ev->analog[ANALOG_LY] / 255.0f;
            if (lx < 0.45f || lx > 0.55f || ly < 0.45f || ly > 0.55f) {
                joy_anim_set_look(lx, ly);
                last_activity_ms = now;
            }

            // Button press edge detection
            if (ev->buttons && ev->buttons != last_buttons) {
                joy_anim_event(JOY_EVENT_BUTTON_PRESS);
                last_activity_ms = now;
            }
            last_buttons = ev->buttons;

            // Idle timeout → sleep after 30s
            if (now - last_activity_ms > 30000 &&
                joy_anim_get_state() == JOY_STATE_IDLE) {
                joy_anim_event(JOY_EVENT_IDLE_TIMEOUT);
            }
        }

        // Tick + render
        if (joy_anim_tick(now)) {
            display_clear();
            joy_anim_render();
            display_update();
        }
    }
#endif
}
