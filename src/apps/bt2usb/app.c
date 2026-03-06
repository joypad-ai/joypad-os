// app.c - BT2USB App Entry Point
// Bluetooth to USB HID gamepad adapter for Pico W
//
// Uses Pico W's built-in CYW43 Bluetooth to receive controllers,
// outputs as USB HID device.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "bt/transport/bt_transport.h"
#include "bt/btstack/btstack_host.h"
#include "core/services/leds/leds.h"

#include "tusb.h"
#include "platform/platform.h"
#include <stdio.h>

#ifdef BTSTACK_USE_ESP32
#include "driver/gpio.h"
extern const bt_transport_t bt_transport_esp32;
// Status LED GPIO (Seeed XIAO ESP32-S3 = GPIO 21, active low)
#ifndef STATUS_LED_GPIO
#define STATUS_LED_GPIO 21
#endif
#ifndef STATUS_LED_ACTIVE_LOW
#define STATUS_LED_ACTIVE_LOW 1
#endif
#elif defined(BTSTACK_USE_NRF)
extern const bt_transport_t bt_transport_nrf;
// nRF: LED status handled by ws2812_nrf.c (RGB LEDs driven via neopixel API)
#ifdef OLED_I2C_DISPLAY
#include "core/services/display/display.h"
#include "core/input_event.h"
#include "core/buttons.h"
#endif
#else
#include "pico/cyw43_arch.h"
extern const bt_transport_t bt_transport_cyw43;
#endif

// ============================================================================
// LED STATUS
// ============================================================================

static uint32_t led_last_toggle = 0;
static bool led_state = false;

// Update LED based on connection status
// - Blink (0.8s): No device connected (scanning, connecting, or idle)
// - Solid on: Device connected
static void platform_led_set(bool on)
{
#ifdef BTSTACK_USE_ESP32
    gpio_set_level(STATUS_LED_GPIO, (on ^ STATUS_LED_ACTIVE_LOW) ? 1 : 0);
#elif defined(BTSTACK_USE_NRF)
    // No-op: RGB LEDs driven by ws2812_nrf.c via neopixel_task()
    (void)on;
#else
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
#endif
}

static void led_status_update(void)
{
    uint32_t now = platform_time_ms();

    if (btstack_classic_get_connection_count() > 0) {
        // Device connected - solid on
        if (!led_state) {
            platform_led_set(true);
            led_state = true;
        }
    } else {
        // No device connected - blink
        if (now - led_last_toggle >= 400) {
            led_state = !led_state;
            platform_led_set(led_state);
            led_last_toggle = now;
        }
    }
}

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
            // Start/extend 60-second BT scan for additional devices
            printf("[app:bt2usb] Starting BT scan (60s)...\n");
            btstack_host_start_timed_scan(60000);
            break;

        case BUTTON_EVENT_DOUBLE_CLICK: {
            // Double-click to cycle USB output mode
            printf("[app:bt2usb] Double-click - switching USB output mode...\n");
            tud_task_ext(1, false);
            platform_sleep_ms(50);
            tud_task_ext(1, false);

            usb_output_mode_t next = usbd_get_next_mode();
            printf("[app:bt2usb] Switching to %s\n", usbd_get_mode_name(next));
            usbd_set_mode(next);
            break;
        }

        case BUTTON_EVENT_TRIPLE_CLICK:
            // Triple-click to reset to default HID mode
            printf("[app:bt2usb] Triple-click - resetting to HID mode...\n");
            if (!usbd_reset_to_hid()) {
                printf("[app:bt2usb] Already in HID mode\n");
            }
            break;

        case BUTTON_EVENT_HOLD:
            // Long press to disconnect all devices and clear all bonds
            printf("[app:bt2usb] Disconnecting all devices and clearing bonds...\n");
            btstack_host_disconnect_all_devices();
            btstack_host_delete_all_bonds();
            break;

        default:
            break;
    }
}

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

// BT2USB has no InputInterface - BT transport handles input internally
// via bthid drivers that call router_submit_input()

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = 0;
    return NULL;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

static const OutputInterface* output_interfaces[] = {
    &usbd_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// OLED DISPLAY (XIAO Expansion Board - SSD1306 128x64 I2C)
// ============================================================================

#ifdef OLED_I2C_DISPLAY

// Arrow characters for display (1=up, 2=down, 3=left, 4=right)
#define ARROW_UP    "\x01"
#define ARROW_DOWN  "\x02"
#define ARROW_LEFT  "\x03"
#define ARROW_RIGHT "\x04"

typedef struct {
    uint32_t mask;
    const char* name;
} button_name_t;

static const button_name_t button_names[] = {
    { JP_BUTTON_DU, ARROW_UP },
    { JP_BUTTON_DR, ARROW_RIGHT },
    { JP_BUTTON_DD, ARROW_DOWN },
    { JP_BUTTON_DL, ARROW_LEFT },
    { JP_BUTTON_B1, "B1" },
    { JP_BUTTON_B2, "B2" },
    { JP_BUTTON_B3, "B3" },
    { JP_BUTTON_B4, "B4" },
    { JP_BUTTON_L1, "L1" },
    { JP_BUTTON_R1, "R1" },
    { JP_BUTTON_L2, "L2" },
    { JP_BUTTON_R2, "R2" },
    { JP_BUTTON_S1, "S1" },
    { JP_BUTTON_S2, "S2" },
    { JP_BUTTON_L3, "L3" },
    { JP_BUTTON_R3, "R3" },
    { JP_BUTTON_A1, "A1" },
    { 0, NULL }
};

static const char* transport_str(input_transport_t t) {
    switch (t) {
        case INPUT_TRANSPORT_BT_CLASSIC: return "BT";
        case INPUT_TRANSPORT_BT_BLE:     return "BLE";
        default:                         return "?";
    }
}

static void oled_init(void) {
    display_i2c_config_t cfg = {
        .i2c_inst = 0,
        .pin_sda  = 0,     // Configured by devicetree on nRF
        .pin_scl  = 0,
        .addr     = 0x3C,
    };
    display_init_ssd1306_i2c(&cfg);
    printf("[app:bt2usb] OLED display initialized (SSD1306 I2C)\n");
}

static input_event_t oled_cached_event;
static bool oled_has_event = false;

static void oled_update_display(void) {
    static uint32_t last_update = 0;
    static uint32_t last_buttons = 0;
    uint32_t now = platform_time_ms();

    // Cache latest router output
    if (playersCount > 0 && players[0].dev_addr >= 0) {
        const input_event_t* ev = router_get_output(OUTPUT_TARGET_USB_DEVICE, 0);
        if (ev) {
            oled_cached_event = *ev;
            oled_has_event = true;
        }
    }

    // Feed button presses to marquee (edge detection)
    uint32_t buttons = oled_has_event ? oled_cached_event.buttons : 0;
    uint32_t newly_pressed = ~last_buttons & buttons;
    last_buttons = buttons;
    for (int i = 0; button_names[i].name != NULL; i++) {
        if (newly_pressed & button_names[i].mask) {
            display_marquee_add(button_names[i].name);
        }
    }

    if (now - last_update < 50) return;  // 20fps max
    last_update = now;

    display_clear();

    // Line 1 (large, y=0): USB output mode
    usb_output_mode_t mode = usbd_get_mode();
    display_text_large(0, 0, usbd_get_mode_name(mode));

    // Separator
    display_hline(0, 17, DISPLAY_WIDTH);

    // Lines 2-4: Controller info
    if (playersCount > 0 && players[0].dev_addr >= 0) {
        const char* name = get_player_name(0);
        if (name) {
            display_text(0, 20, name);
        }

        char info[22];
        snprintf(info, sizeof(info), "%s dev:%d P%d/%d",
                 transport_str(players[0].transport),
                 players[0].dev_addr,
                 players[0].player_number, playersCount);
        display_text(0, 30, info);

        if (oled_has_event) {
            char line[22];
            snprintf(line, sizeof(line), "L:%02X,%02X R:%02X,%02X T:%02X,%02X",
                     oled_cached_event.analog[ANALOG_LX], oled_cached_event.analog[ANALOG_LY],
                     oled_cached_event.analog[ANALOG_RX], oled_cached_event.analog[ANALOG_RY],
                     oled_cached_event.analog[ANALOG_L2], oled_cached_event.analog[ANALOG_R2]);
            display_text(0, 40, line);
        }
    } else {
        display_text(0, 28, "No controller");
    }

    // Bottom (y=52): Button marquee
    display_marquee_tick();
    display_marquee_render(52);

    display_update();
}

#endif // OLED_I2C_DISPLAY

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:bt2usb] Initializing BT2USB v%s\n", APP_VERSION);
#ifdef BTSTACK_USE_ESP32
    printf("[app:bt2usb] ESP32-S3 BLE -> USB HID\n");
    // Init status LED GPIO
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_cfg);
    gpio_set_level(STATUS_LED_GPIO, STATUS_LED_ACTIVE_LOW ? 1 : 0);  // Start OFF
#elif defined(BTSTACK_USE_NRF)
    printf("[app:bt2usb] Seeed XIAO nRF52840 BLE -> USB HID\n");
    // RGB LEDs initialized by ws2812_nrf.c via leds_init()
#ifdef OLED_I2C_DISPLAY
    oled_init();
#endif
#else
    printf("[app:bt2usb] Pico W built-in Bluetooth -> USB HID\n");
#endif

    // Initialize button service (uses BOOTSEL button on Pico W)
    button_init();
    button_set_callback(on_button_event);

    // Configure router for BT2USB
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,  // Merge all BT inputs to single output
        .transform_flags = TRANSFORM_FLAGS,
    };
    router_init(&router_cfg);

    // Add default route: BLE Central → USB Device
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize Bluetooth transport
    // Must use bt_init() to set global transport pointer and register drivers
    printf("[app:bt2usb] Initializing Bluetooth...\n");
#ifdef BTSTACK_USE_ESP32
    bt_init(&bt_transport_esp32);
#elif defined(BTSTACK_USE_NRF)
    bt_init(&bt_transport_nrf);
#else
    bt_init(&bt_transport_cyw43);
#endif

    printf("[app:bt2usb] Initialization complete\n");
    printf("[app:bt2usb]   Routing: Bluetooth -> USB Device (HID Gamepad)\n");
    printf("[app:bt2usb]   Player slots: %d\n", MAX_PLAYER_SLOTS);
    printf("[app:bt2usb]   Click BOOTSEL for 60s BT scan\n");
    printf("[app:bt2usb]   Hold BOOTSEL to disconnect all + clear bonds\n");
    printf("[app:bt2usb]   Double-click BOOTSEL to switch USB mode\n");
}

// ============================================================================
// APP TASK (Called from main loop)
// ============================================================================

void app_task(void)
{
    // Process button input
    button_task();

    // Update LED color when USB output mode changes
    static usb_output_mode_t last_led_mode = USB_OUTPUT_MODE_COUNT;
    usb_output_mode_t mode = usbd_get_mode();
    if (mode != last_led_mode) {
        uint8_t r, g, b;
        usbd_get_mode_color(mode, &r, &g, &b);
        leds_set_color(r, g, b);
        last_led_mode = mode;
    }

    // Process Bluetooth transport
    bt_task();

    // Update LED status
    leds_set_connected_devices(btstack_classic_get_connection_count());
    led_status_update();

    // Route feedback from USB device output to BT controllers
    if (usbd_output_interface.get_feedback) {
        output_feedback_t fb;
        if (usbd_output_interface.get_feedback(&fb)) {
            for (int i = 0; i < playersCount; i++) {
                feedback_set_rumble(i, fb.rumble_left, fb.rumble_right);
                if (fb.led_player > 0) {
                    feedback_set_led_player(i, fb.led_player);
                }
                if (fb.led_r || fb.led_g || fb.led_b) {
                    feedback_set_led_rgb(i, fb.led_r, fb.led_g, fb.led_b);
                }
            }
        }
    }

#ifdef OLED_I2C_DISPLAY
    oled_update_display();
#endif
}
