// app.c - Universal Controller App
// Pad buttons → USB HID Gamepad output
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/button/button.h"
#include "core/services/leds/neopixel/ws2812.h"
#include "core/services/speaker/speaker.h"
#include "core/services/display/display.h"
#include "core/services/codes/codes.h"
#include "core/services/profiles/profile.h"
#include "native/device/uart/uart_device.h"
#include "native/host/uart/uart_host.h"
#include "pad/pad_input.h"
#include "usb/usbd/usbd.h"
#include "core/buttons.h"
#include "tusb.h"
#include "pico/stdlib.h"
#include <stdio.h>

// UART linking state
static bool uart_link_enabled = false;

// Track last displayed mode to avoid unnecessary redraws
static usb_output_mode_t last_displayed_mode = 0xFF;
static uint8_t last_rumble = 0;
static uint32_t last_buttons = 0;  // Track button state for edge detection

// Button name lookup table (matches JP_BUTTON_* bit positions)
typedef struct {
    uint32_t mask;
    const char* name;
} button_name_t;

// Arrow characters for display (1=up, 2=down, 3=left, 4=right)
#define ARROW_UP    "\x01"
#define ARROW_DOWN  "\x02"
#define ARROW_LEFT  "\x03"
#define ARROW_RIGHT "\x04"

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

// ============================================================================
// CONTROLLER TYPE CONFIGURATION
// ============================================================================

#if defined(CONTROLLER_TYPE_FISHERPRICE)
    #include "pad/configs/fisherprice.h"
    #define PAD_CONFIG pad_config_fisherprice
    #define CONTROLLER_NAME "Fisher Price"
#elif defined(CONTROLLER_TYPE_FISHERPRICE_ANALOG)
    #include "pad/configs/fisherprice.h"
    #define PAD_CONFIG pad_config_fisherprice_analog
    #define CONTROLLER_NAME "Fisher Price Analog"
#elif defined(CONTROLLER_TYPE_ALPAKKA)
    #include "pad/configs/alpakka.h"
    #define PAD_CONFIG pad_config_alpakka
    #define CONTROLLER_NAME "Alpakka"
#elif defined(CONTROLLER_TYPE_MACROPAD)
    #include "pad/configs/macropad.h"
    #define PAD_CONFIG pad_config_macropad
    #define CONTROLLER_NAME "MacroPad"
#else
    #error "No controller type defined! Define one of: CONTROLLER_TYPE_FISHERPRICE, CONTROLLER_TYPE_ALPAKKA, etc."
#endif

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
            printf("[app:controller] Button click - current mode: %s\n",
                   usbd_get_mode_name(usbd_get_mode()));
            break;

        case BUTTON_EVENT_DOUBLE_CLICK: {
            printf("[app:controller] Button double-click - switching USB output mode...\n");
            // Flush USB and give stack time to transmit
            tud_task();
            sleep_ms(50);
            tud_task();

            // Cycle to next mode: HID → XInput → PS3 → PS4 → Switch → HID
            usb_output_mode_t current = usbd_get_mode();
            usb_output_mode_t next;
            switch (current) {
                case USB_OUTPUT_MODE_HID:
                    next = USB_OUTPUT_MODE_XINPUT;
                    break;
                case USB_OUTPUT_MODE_XINPUT:
                    next = USB_OUTPUT_MODE_PS3;
                    break;
                case USB_OUTPUT_MODE_PS3:
                    next = USB_OUTPUT_MODE_PS4;
                    break;
                case USB_OUTPUT_MODE_PS4:
                    next = USB_OUTPUT_MODE_SWITCH;
                    break;
                case USB_OUTPUT_MODE_SWITCH:
                default:
                    next = USB_OUTPUT_MODE_HID;
                    break;
            }
            printf("[app:controller] Switching from %s to %s\n",
                   usbd_get_mode_name(current), usbd_get_mode_name(next));
            tud_task();
            sleep_ms(50);
            tud_task();

            usbd_set_mode(next);  // This will reset the device
            break;
        }

        default:
            break;
    }
}

// ============================================================================
// KONAMI CODE CALLBACK
// ============================================================================

static void on_code_detected(const char* code_name)
{
    printf("[app:controller] Code detected: %s\n", code_name);

    // Visual feedback - use profile indicator (flashes LEDs)
    neopixel_indicate_profile(3);  // Flash 4 times

    // Audio feedback - play victory melody
    if (speaker_is_initialized()) {
        speaker_tone(880, 200);    // A5
        sleep_ms(100);
        speaker_tone(1047, 200);   // C6
        sleep_ms(100);
        speaker_tone(1319, 255);   // E6
        sleep_ms(200);
        speaker_stop();
    }

    // Display feedback - show on marquee
    if (display_is_initialized()) {
        display_marquee_add("KONAMI!");
    }
}

// ============================================================================
// UART LINK TAP (sends local inputs to linked controller)
// ============================================================================

// Router tap callback - sends local inputs to linked controller via UART
// Filters out UART-received inputs (dev_addr >= 0xD0) to prevent loops
static void uart_link_tap(output_target_t output, uint8_t player_index,
                          const input_event_t* event)
{
    (void)output;

    if (!uart_link_enabled) return;

    // Don't resend inputs that came from UART (dev_addr 0xD0+ range)
    // This prevents infinite loops between linked controllers
    if (event->dev_addr >= 0xD0) return;

    // Send local input to linked controller
    uart_device_queue_input(event, player_index);
}

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &pad_input_interface,
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
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:controller] Initializing %s Controller v%s\n", CONTROLLER_NAME, APP_VERSION);

    // Initialize button service
    button_init();
    button_set_callback(on_button_event);

    // Initialize codes service (Konami code detection)
    codes_set_callback(on_code_detected);

    // Register pad device configuration BEFORE interface init
    int dev_idx = pad_input_add_device(&PAD_CONFIG);

    if (dev_idx < 0) {
        printf("[app:controller] ERROR: Failed to register pad device!\n");
        return;
    }

    printf("[app:controller] Pad config: %s\n", PAD_CONFIG.name);

    // Set custom LED colors from pad config if defined
    if (PAD_CONFIG.led_count > 0) {
        neopixel_set_custom_colors(PAD_CONFIG.led_colors, PAD_CONFIG.led_count);
        if (neopixel_has_custom_colors()) {
            printf("[app:controller] Using custom LED colors (%d LEDs)\n", PAD_CONFIG.led_count);
        }
    }

    // Initialize speaker for haptic/rumble feedback if configured
    if (PAD_CONFIG.speaker_pin != PAD_PIN_DISABLED) {
        speaker_init(PAD_CONFIG.speaker_pin, PAD_CONFIG.speaker_enable_pin);
        printf("[app:controller] Speaker initialized for rumble feedback\n");
    }

    // Initialize display if configured
    if (PAD_CONFIG.display_spi >= 0) {
        display_config_t disp_cfg = {
            .spi_inst = PAD_CONFIG.display_spi,
            .pin_sck = PAD_CONFIG.display_sck,
            .pin_mosi = PAD_CONFIG.display_mosi,
            .pin_cs = PAD_CONFIG.display_cs,
            .pin_dc = PAD_CONFIG.display_dc,
            .pin_rst = PAD_CONFIG.display_rst,
        };
        display_init(&disp_cfg);
        printf("[app:controller] Display initialized\n");
    }

    // Initialize UART link if QWIIC pins are configured
    if (PAD_CONFIG.qwiic_tx != PAD_PIN_DISABLED && PAD_CONFIG.qwiic_rx != PAD_PIN_DISABLED) {
        // Initialize UART host (receives inputs from linked controller)
        uart_host_init_pins(PAD_CONFIG.qwiic_tx, PAD_CONFIG.qwiic_rx, UART_PROTOCOL_BAUD_DEFAULT);
        uart_host_set_mode(UART_HOST_MODE_NORMAL);

        // Initialize UART device (sends inputs to linked controller)
        uart_device_init_pins(PAD_CONFIG.qwiic_tx, PAD_CONFIG.qwiic_rx, UART_PROTOCOL_BAUD_DEFAULT);
        uart_device_set_mode(UART_DEVICE_MODE_ON_CHANGE);

        uart_link_enabled = true;
        printf("[app:controller] UART link enabled on QWIIC (TX=%d, RX=%d)\n",
               PAD_CONFIG.qwiic_tx, PAD_CONFIG.qwiic_rx);
    }

    // Configure router for Pad → USB Device
    router_config_t router_cfg = {
        .mode = ROUTING_MODE_SIMPLE,
        .merge_mode = MERGE_PRIORITY,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = 1,
        },
        .merge_all_inputs = false,
        .transform_flags = 0,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);

    // Initialize profile system with button combos (S1+S2=A1)
    static const profile_config_t profile_cfg = {
        .output_profiles = { NULL },
        .shared_profiles = &controller_profile_set,
    };
    profile_init(&profile_cfg);

    // Add route: Pad → USB Device
    router_add_route(INPUT_SOURCE_GPIO, OUTPUT_TARGET_USB_DEVICE, 0);

    // Set up router tap for UART linking (if enabled)
    if (uart_link_enabled) {
        router_set_tap(OUTPUT_TARGET_USB_DEVICE, uart_link_tap);
    }

    printf("[app:controller] Initialization complete\n");
    printf("[app:controller]   Routing: Pad → USB Device (HID Gamepad)\n");
    if (uart_link_enabled) {
        printf("[app:controller]   UART Link: Enabled (connect via QWIIC to merge inputs)\n");
    }
    printf("[app:controller]   Double-click encoder button to switch USB mode\n");
}

// ============================================================================
// APP TASK
// ============================================================================

// Update display with current mode and status
static void update_display(uint8_t rumble, uint32_t buttons)
{
    if (!display_is_initialized()) return;

    usb_output_mode_t mode = usbd_get_mode();
    bool needs_update = false;

    // Check if mode changed
    if (mode != last_displayed_mode) {
        last_displayed_mode = mode;
        needs_update = true;

        display_clear();

        // Draw mode name in large text at top
        const char* mode_name = usbd_get_mode_name(mode);
        display_text_large(4, 4, mode_name);

        // Draw separator line
        display_hline(0, 24, 128);

        // Draw labels
        display_text(4, 28, "Rumble:");
    }

    // Update rumble bar if changed significantly
    if (needs_update || (rumble / 8) != (last_rumble / 8)) {
        last_rumble = rumble;

        // Clear rumble bar area and redraw
        display_fill_rect(4, 38, 120, 10, false);
        display_progress_bar(4, 38, 120, 10, (rumble * 100) / 255);

        needs_update = true;
    }

    // Detect newly pressed buttons
    // Buttons use active-high in router (1 = pressed, 0 = released)
    // Detect rising edge: was not pressed (0), now pressed (1)
    uint32_t newly_pressed = ~last_buttons & buttons;
    last_buttons = buttons;

    // Add newly pressed buttons to marquee
    bool button_added = false;
    for (int i = 0; button_names[i].name != NULL; i++) {
        if (newly_pressed & button_names[i].mask) {
            display_marquee_add(button_names[i].name);
            button_added = true;
        }
    }

    // Update marquee animation (handles scrolling and fade timeout)
    bool marquee_changed = display_marquee_tick();

    // Render marquee if anything changed
    if (button_added || marquee_changed) {
        display_marquee_render(54);  // Render at bottom of display
        needs_update = true;
    }

    if (needs_update) {
        display_update();
    }
}

void app_task(void)
{
    // Process button input for mode switching
    button_task();

    // Process codes detection (Konami code, etc.)
    codes_task_for_output(OUTPUT_TARGET_USB_DEVICE);

    // Process UART link communication (if enabled)
    if (uart_link_enabled) {
        uart_host_task();   // Receive inputs from linked controller
        uart_device_task(); // Send inputs to linked controller
    }

    // Get rumble value
    uint8_t rumble = 0;
    if (usbd_output_interface.get_rumble) {
        rumble = usbd_output_interface.get_rumble();
    }

    // Handle rumble feedback via speaker (if initialized)
    if (speaker_is_initialized()) {
        speaker_set_rumble(rumble);
    }

    // Get current button state from router output
    uint32_t buttons = 0xFFFFFFFF;  // All released (active-low)
    const input_event_t* event = router_get_output(OUTPUT_TARGET_USB_DEVICE, 0);
    if (event) {
        buttons = event->buttons;
    }

    // Update display
    update_display(rumble, buttons);
}
