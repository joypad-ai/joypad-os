// app.c - USB2USB App Entry Point
// USB to USB HID gamepad adapter
//
// This file contains app-specific initialization and logic.
// The firmware calls app_init() after core system initialization.

#include "app.h"
#include "core/router/router.h"

// Build info fallbacks (normally set by CMake)
#ifndef GIT_COMMIT
#define GIT_COMMIT "not-defined"
#endif
#ifndef BUILD_TIME
#define BUILD_TIME "not-defined"
#endif
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#ifndef DISABLE_USB_HOST
#include "usb/usbh/usbh.h"
#endif
#include "usb/usbd/usbd.h"

#ifdef ENABLE_BTSTACK
#include "bt/btstack/btstack_host.h"
#include "bt/transport/bt_transport.h"
#endif
#include "core/services/leds/leds.h"

#ifdef BTSTACK_USE_NRF
extern const bt_transport_t bt_transport_nrf;
#endif

#ifdef BTSTACK_USE_ESP32
extern const bt_transport_t bt_transport_esp32;
#endif

#ifdef BTSTACK_USE_CYW43
#include "pico/cyw43_arch.h"
extern const bt_transport_t bt_transport_cyw43;

// CYW43 onboard LED state (Pico W has no NeoPixel)
static uint32_t cyw43_led_last_toggle = 0;
static bool cyw43_led_state = false;

// Blink when idle, solid when connected
static void cyw43_led_update(int devices)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (devices > 0) {
        if (!cyw43_led_state) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            cyw43_led_state = true;
        }
    } else {
        if (now - cyw43_led_last_toggle >= 400) {
            cyw43_led_state = !cyw43_led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, cyw43_led_state ? 1 : 0);
            cyw43_led_last_toggle = now;
        }
    }
}
#endif

#ifdef I2C_PEER_ENABLED
#include "i2c_peer/i2c_peer.h"
#endif
#include "core/buttons.h"
#include "tusb.h"
#if !defined(PLATFORM_NRF) && !defined(PLATFORM_ESP32)
#include "pico/stdlib.h"
#endif
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

#if defined(OLED_I2C_INST) || defined(OLED_I2C_DISPLAY)
#include "core/services/display/display.h"
#include "core/services/display/joy_anim.h"
#ifdef OLED_I2C_INST
#include "hardware/gpio.h"
#endif

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
    { JP_BUTTON_A2, "A2" },
    { 0, NULL }
};
// Display pages (paged with FeatherWing buttons A/C)
typedef enum {
    OLED_PAGE_JOY = 0,      // Joy animated character
    OLED_PAGE_INFO,         // Controller info display
    OLED_PAGE_COUNT
} oled_page_t;

static oled_page_t oled_current_page = OLED_PAGE_JOY;

// Page navigation helpers
static void oled_page_up(void) {
    if (oled_current_page == 0)
        oled_current_page = OLED_PAGE_COUNT - 1;
    else
        oled_current_page--;
    printf("[app] OLED page → %d\n", oled_current_page);
}
static void oled_page_down(void) {
    oled_current_page = (oled_current_page + 1) % OLED_PAGE_COUNT;
    printf("[app] OLED page → %d\n", oled_current_page);
}

#endif // OLED_I2C_INST || OLED_I2C_DISPLAY

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
#ifdef ENABLE_BTSTACK
            if (bt_is_ready()) {
                printf("[app:usb2usb] Starting BT scan (60s)...\n");
                btstack_host_start_timed_scan(60000);
            } else
#endif
            {
                printf("[app:usb2usb] current mode: %s\n",
                       usbd_get_mode_name(usbd_get_mode()));
            }
            break;

        case BUTTON_EVENT_DOUBLE_CLICK: {
            // Double-click to cycle USB output mode
            printf("[app:usb2usb] Double-click - switching USB output mode...\n");
#ifdef PLATFORM_ESP32
            tud_task_ext(1, false);
#else
            tud_task();
#endif
            platform_sleep_ms(50);
#ifdef PLATFORM_ESP32
            tud_task_ext(1, false);
#else
            tud_task();
#endif

            usb_output_mode_t next = usbd_get_next_mode();
            printf("[app:usb2usb] Switching to %s\n", usbd_get_mode_name(next));
            usbd_set_mode(next);
#if defined(OLED_I2C_INST) || defined(OLED_I2C_DISPLAY)
            joy_anim_event(JOY_EVENT_MODE_SWITCH);
#endif
            break;
        }

        case BUTTON_EVENT_TRIPLE_CLICK:
            // Triple-click to reset to default HID mode
            printf("[app:usb2usb] Triple-click - resetting to HID mode...\n");
            if (!usbd_reset_to_hid()) {
                printf("[app:usb2usb] Already in HID mode\n");
            }
            break;

        case BUTTON_EVENT_HOLD:
#ifdef ENABLE_BTSTACK
            // Long press to disconnect all devices and clear all bonds
            if (bt_is_ready()) {
                printf("[app:usb2usb] Disconnecting all devices and clearing bonds...\n");
                btstack_host_disconnect_all_devices();
            }
            btstack_host_delete_all_bonds();
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
#ifndef DISABLE_USB_HOST
    &usbh_input_interface,
#endif
#ifdef I2C_PEER_ENABLED
    &i2c_peer_input_interface,
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
    &usbd_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// OLED DISPLAY
// ============================================================================

#if defined(OLED_I2C_INST) || defined(OLED_I2C_DISPLAY)

static const char* transport_str(input_transport_t t) {
    switch (t) {
        case INPUT_TRANSPORT_USB:        return "USB";
        case INPUT_TRANSPORT_BT_CLASSIC: return "BT";
        case INPUT_TRANSPORT_BT_BLE:     return "BLE";
        case INPUT_TRANSPORT_NATIVE:     return "Native";
        case INPUT_TRANSPORT_I2C:        return "I2C";
        default:                         return "?";
    }
}

#ifdef OLED_I2C_INST
static void oled_init(void) {
    display_i2c_config_t cfg = {
        .i2c_inst = OLED_I2C_INST,
        .pin_sda  = OLED_I2C_SDA_PIN,
        .pin_scl  = OLED_I2C_SCL_PIN,
        .addr     = OLED_I2C_ADDR,
    };
    display_init_i2c(&cfg);

    // FeatherWing buttons: A (GPIO 9), B (GPIO 6), C (GPIO 5)
    // Button A conflicts with MAX3421E INT on USB host builds — only init if available
#ifdef OLED_BUTTON_A_PIN
    gpio_init(OLED_BUTTON_A_PIN);
    gpio_set_dir(OLED_BUTTON_A_PIN, GPIO_IN);
    gpio_pull_up(OLED_BUTTON_A_PIN);
#endif
    gpio_init(OLED_BUTTON_B_PIN);
    gpio_set_dir(OLED_BUTTON_B_PIN, GPIO_IN);
    gpio_pull_up(OLED_BUTTON_B_PIN);

    gpio_init(OLED_BUTTON_C_PIN);
    gpio_set_dir(OLED_BUTTON_C_PIN, GPIO_IN);
    gpio_pull_up(OLED_BUTTON_C_PIN);

    joy_anim_init();
    joy_anim_event(JOY_EVENT_BOOT);

    printf("[app:usb2usb] OLED FeatherWing initialized (I2C%d, buttons"
#ifdef OLED_BUTTON_A_PIN
           " A=%d"
#endif
           " B=%d C=%d)\n",
           OLED_I2C_INST,
#ifdef OLED_BUTTON_A_PIN
           OLED_BUTTON_A_PIN,
#endif
           OLED_BUTTON_B_PIN, OLED_BUTTON_C_PIN);
}

static void oled_handle_buttons(void) {
    static bool last_a = true, last_b = true, last_c = true;  // Active-low (pull-up)
    static uint32_t debounce_a = 0, debounce_b = 0, debounce_c = 0;
    uint32_t now = platform_time_ms();

    bool b = gpio_get(OLED_BUTTON_B_PIN);
    bool c = gpio_get(OLED_BUTTON_C_PIN);

    // Button A: page up (when available — conflicts with MAX3421E INT on USB host builds)
#ifdef OLED_BUTTON_A_PIN
    bool a = gpio_get(OLED_BUTTON_A_PIN);
    if (!a && last_a && (now - debounce_a > 200)) {
        debounce_a = now;
        oled_page_up();
    }
    last_a = a;
#endif

    // Button B: cycle USB output mode
    if (!b && last_b && (now - debounce_b > 200)) {
        debounce_b = now;
        printf("[app:usb2usb] OLED Button B - switching USB output mode...\n");
        tud_task();
        platform_sleep_ms(50);
        tud_task();
        usb_output_mode_t next = usbd_get_next_mode();
        printf("[app:usb2usb] Switching to %s\n", usbd_get_mode_name(next));
        usbd_set_mode(next);
    }
    last_b = b;

    // Button C: page down
    if (!c && last_c && (now - debounce_c > 200)) {
        debounce_c = now;
        oled_page_down();
    }
    last_c = c;
}
#elif defined(OLED_I2C_DISPLAY)
static void oled_init(void) {
    display_i2c_config_t cfg = {
        .i2c_inst = 0,
        .pin_sda  = 0,     // Configured by devicetree on nRF
        .pin_scl  = 0,
        .addr     = 0x3C,
    };
    display_init_i2c(&cfg);  // SH1107 FeatherWing OLED
    joy_anim_init();
    joy_anim_event(JOY_EVENT_BOOT);
    printf("[app:usb2usb] OLED display initialized (SH1107 I2C)\n");
}
#endif

static input_event_t oled_cached_event;
static bool oled_has_event = false;
// joy_display_mode removed — replaced by oled_current_page (paged with A/C buttons)
static int oled_last_player_count = 0;
static uint32_t oled_last_activity_ms = 0;

static void oled_update_display(void) {
    static uint32_t last_update = 0;
    static uint32_t last_buttons = 0;
    uint32_t now = platform_time_ms();

    // Cache latest router output (don't gate on display throttle — always consume)
    if (playersCount > 0 && players[0].dev_addr >= 0) {
        const input_event_t* ev = router_get_output(OUTPUT_TARGET_USB_DEVICE, 0);
        if (ev) {
            oled_cached_event = *ev;
            oled_has_event = true;
        }
    }

    // Detect connect/disconnect transitions for Joy
    if (playersCount > 0 && oled_last_player_count == 0) {
        joy_anim_event(JOY_EVENT_CONNECT);
        oled_last_activity_ms = now;
    }
    if (playersCount == 0 && oled_last_player_count > 0) {
        joy_anim_event(JOY_EVENT_DISCONNECT);
        oled_has_event = false;
    }
    oled_last_player_count = playersCount;

    // Feed analog stick to Joy's look direction
    if (oled_has_event && playersCount > 0) {
        float lx = (float)oled_cached_event.analog[ANALOG_LX] / 255.0f;
        float ly = (float)oled_cached_event.analog[ANALOG_LY] / 255.0f;
        joy_anim_set_look(lx, ly);
    }

    // Feed button presses to marquee and Joy (edge detection)
    uint32_t buttons = oled_has_event ? oled_cached_event.buttons : 0;
    uint32_t newly_pressed = ~last_buttons & buttons;
    last_buttons = buttons;
    for (int i = 0; button_names[i].name != NULL; i++) {
        if (newly_pressed & button_names[i].mask) {
            display_marquee_add(button_names[i].name);
            joy_anim_event(JOY_EVENT_BUTTON_PRESS);
            oled_last_activity_ms = now;
        }
    }

    // Track activity for idle timeout
    if (oled_has_event && playersCount > 0) {
        uint8_t lx = oled_cached_event.analog[ANALOG_LX];
        uint8_t ly = oled_cached_event.analog[ANALOG_LY];
        // Analog stick moved away from center
        if (lx < 108 || lx > 148 || ly < 108 || ly > 148) {
            oled_last_activity_ms = now;
        }
    }

    // Idle timeout → sleep
    if (now - oled_last_activity_ms > 30000 &&
        joy_anim_get_state() == JOY_STATE_IDLE) {
        joy_anim_event(JOY_EVENT_IDLE_TIMEOUT);
    }

    if (now - last_update < 50) return;  // 20fps max
    last_update = now;

    display_clear();

    if (oled_current_page == OLED_PAGE_JOY) {
        // Joy character takes the full screen — no text overlays
        joy_anim_tick(now);
        joy_anim_render();
    } else {
        // Controller info display (existing layout)
        usb_output_mode_t mode = usbd_get_mode();
        display_text_large(0, 0, usbd_get_mode_name(mode));

        display_hline(0, 17, DISPLAY_WIDTH);

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
        }

        // Button marquee at bottom
        display_marquee_tick();
        display_marquee_render(52);

        // Tick Joy even when not displayed (keeps state machine running)
        joy_anim_tick(now);
    }

    display_update();
}

#endif // OLED_I2C_INST || OLED_I2C_DISPLAY

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:usb2usb] Initializing USB2USB v%s\n", JOYPAD_VERSION);

    // Initialize button service
    button_init();
    button_set_callback(on_button_event);

    // Configure router for USB2USB
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,  // Merge all USB inputs to single output
        .transform_flags = TRANSFORM_FLAGS,
        // Mouse-to-analog: Map mouse to right stick for camera control
        // Useful for accessibility (mouthpad, head tracker) alongside gamepad
        .mouse_target_x = ANALOG_RY,            // Right stick X
        .mouse_target_y = MOUSE_AXIS_DISABLED,  // Y disabled (X-only for camera pan)
        .mouse_drain_rate = 0,                  // No drain - hold position until head returns
    };
    router_init(&router_cfg);

    // Add default route: USB Host → USB Device
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_USB_DEVICE, 0);

#ifdef I2C_PEER_ENABLED
    // Add I2C peer route and initialize master
    router_add_route(INPUT_SOURCE_I2C_PEER, OUTPUT_TARGET_USB_DEVICE, 0);
    {
        i2c_peer_config_t peer_cfg = {
            .i2c_inst = OLED_I2C_INST,
            .sda_pin = OLED_I2C_SDA_PIN,
            .scl_pin = OLED_I2C_SCL_PIN,
            .addr = I2C_PEER_DEFAULT_ADDR,
            .skip_i2c_init = true,  // OLED already initialized I2C bus
        };
        i2c_peer_master_init(&peer_cfg);
    }
#endif

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Set boot LED color (visible during potentially blocking USB host init)
    leds_set_color(80, 80, 80);

#if defined(OLED_I2C_INST) || defined(OLED_I2C_DISPLAY)
    oled_init();
#endif

#ifdef BTSTACK_USE_CYW43
    // Initialize CYW43 built-in Bluetooth before USB host/device init.
    // CYW43 SPI claims PIO1 and DMA channels 0-1 dynamically.
    // PIO-USB uses PIO0 and a high DMA channel (10) to avoid conflicts.
    printf("[app:usb2usb] Initializing CYW43 Bluetooth...\n");
    bt_init(&bt_transport_cyw43);
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_USB_DEVICE, 0);
#endif

#ifdef BTSTACK_USE_NRF
    // Initialize nRF52840 built-in BLE (BTstack runs in its own Zephyr thread)
    printf("[app:usb2usb] Initializing nRF BLE...\n");
    bt_init(&bt_transport_nrf);
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_USB_DEVICE, 0);
#endif

#ifdef BTSTACK_USE_ESP32
    // Initialize ESP32-S3 BLE (BTstack runs in its own FreeRTOS task)
    printf("[app:usb2usb] Initializing ESP32 BLE...\n");
    bt_init(&bt_transport_esp32);
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_USB_DEVICE, 0);
#endif

    printf("[app:usb2usb] Initialization complete\n");
    printf("[app:usb2usb]   Routing: USB Host → USB Device (HID Gamepad)\n");
    printf("[app:usb2usb]   Player slots: %d\n", MAX_PLAYER_SLOTS);
    printf("[app:usb2usb]   Double-click button (GPIO7) to switch USB mode\n");
}

// ============================================================================
// APP TASK (Optional - called from main loop)
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

#if defined(BTSTACK_USE_CYW43) || defined(BTSTACK_USE_NRF) || defined(BTSTACK_USE_ESP32)
    // Process Bluetooth transport
    bt_task();
#endif

    // Update LED with connected device count (USB HID + BT)
    // This makes LED go solid as soon as a controller is detected,
    // without waiting for button press to assign as player
    int devices = 0;
#ifndef DISABLE_USB_HOST
    for (uint8_t addr = 1; addr < MAX_DEVICES; addr++) {
        if (tuh_mounted(addr) && tuh_hid_instance_count(addr) > 0) {
            devices++;
        }
    }
#endif
#ifdef ENABLE_BTSTACK
    if (bt_is_ready()) {
        devices += btstack_classic_get_connection_count();
    }
#endif
    leds_set_connected_devices(devices);

#ifdef BTSTACK_USE_CYW43
    // Update CYW43 onboard LED (Pico W has no NeoPixel)
    // Blinks when idle, solid when a controller is connected
    cyw43_led_update(devices);
#endif

    // Route feedback from USB device output to USB host input controllers
    // The output interface receives rumble/LED from the console/host
    // and we forward it to connected controllers via the feedback system
    if (usbd_output_interface.get_feedback) {
        output_feedback_t fb;
        if (usbd_output_interface.get_feedback(&fb)) {
            // Set feedback for all active players
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

#ifdef OLED_I2C_INST
    oled_handle_buttons();
#endif
#if defined(OLED_I2C_INST) || defined(OLED_I2C_DISPLAY)
    oled_update_display();
#endif

#ifdef I2C_PEER_ENABLED
    // Send device status to I2C peer slave (~10Hz or on change)
    {
        static uint32_t last_status_send = 0;
        static i2c_peer_status_t last_status = {0};
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        i2c_peer_status_t status = {0};

        // Build status from current state
        if (playersCount > 0 && players[0].dev_addr >= 0) {
            status.flags = I2C_PEER_FLAG_CONNECTED;
            status.transport = (uint8_t)players[0].transport;
            status.player_number = (uint8_t)players[0].player_number;
            const char* name = get_player_name(0);
            if (name && name[0]) {
                status.flags |= I2C_PEER_FLAG_NAME_VALID;
                strncpy(status.name, name, sizeof(status.name) - 1);
                status.name[sizeof(status.name) - 1] = '\0';
            }
        }

        // USB output mode + color
        usb_output_mode_t cur_mode = usbd_get_mode();
        status.usb_mode = (uint8_t)cur_mode;
        usbd_get_mode_color(cur_mode, &status.mode_color[0],
                            &status.mode_color[1], &status.mode_color[2]);

        // Feedback (rumble, LEDs)
        if (usbd_output_interface.get_feedback) {
            output_feedback_t fb;
            if (usbd_output_interface.get_feedback(&fb)) {
                status.rumble_left = fb.rumble_left;
                status.rumble_right = fb.rumble_right;
                status.led_player = fb.led_player;
                status.led_color[0] = fb.led_r;
                status.led_color[1] = fb.led_g;
                status.led_color[2] = fb.led_b;
            }
        }

        // Send on change or every 100ms (~10Hz)
        if (memcmp(&status, &last_status, sizeof(status)) != 0 ||
            now_ms - last_status_send >= 100) {
            i2c_peer_master_send_status(&status);
            last_status = status;
            last_status_send = now_ms;
        }
    }
#endif
}
