// app.c - USB2UART App Entry Point
// USB to UART bridge for ESP32 communication
//
// Reads USB controllers and sends state over UART to ESP32.
// Receives feedback (rumble, LED) from ESP32 and applies to controllers.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/players/feedback.h"
#include "native/device/uart/uart_device.h"
#include "usb/usbh/usbh.h"
#include <stdio.h>

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &usbh_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void uart_output_init(void);
static void uart_output_task(void);
static uint8_t uart_output_get_rumble(void);
static uint8_t uart_output_get_player_led(void);
static void uart_rumble_handler(uint8_t player_index, uint8_t left_motor,
                                 uint8_t right_motor, uint16_t duration_ms);
static void uart_led_handler(uint8_t player_index, uint8_t pattern,
                              uint8_t r, uint8_t g, uint8_t b);

// ============================================================================
// UART OUTPUT INTERFACE
// ============================================================================

// Output interface for UART bridge
static const OutputInterface uart_output_interface = {
    .name = "UART Bridge",
    .target = OUTPUT_TARGET_UART,
    .init = uart_output_init,
    .core1_task = NULL,            // No core1 needed
    .task = uart_output_task,
    .get_rumble = uart_output_get_rumble,
    .get_player_led = uart_output_get_player_led,
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
};

static const OutputInterface* output_interfaces[] = {
    &uart_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// UART OUTPUT IMPLEMENTATION
// ============================================================================

static void uart_output_init(void)
{
    printf("[uart_output] Initializing UART output\n");

    // Initialize UART device (sends controller data to ESP32)
    uart_device_init_pins(UART_TX_PIN, UART_RX_PIN, UART_BAUD);

    // Set mode to send on state change (efficient)
    uart_device_set_mode(UART_DEVICE_MODE_ON_CHANGE);

    // Set up rumble callback - when ESP32 sends rumble, apply to controller
    uart_device_set_rumble_callback(uart_rumble_handler);

    // Set up LED callback - when ESP32 sends LED, apply to controller
    uart_device_set_led_callback(uart_led_handler);

    printf("[uart_output] UART bridge ready (TX=%d, RX=%d, %d baud)\n",
           UART_TX_PIN, UART_RX_PIN, UART_BAUD);
}

static void uart_output_task(void)
{
    // Process UART communication
    uart_device_task();
}

// Legacy interface - returns player 0's rumble
static uint8_t uart_output_get_rumble(void)
{
    feedback_state_t* fb = feedback_get_state(0);
    return fb ? fb->rumble.left : 0;
}

// Legacy interface - returns player 0's LED
static uint8_t uart_output_get_player_led(void)
{
    feedback_state_t* fb = feedback_get_state(0);
    return fb ? fb->led.pattern : 0;
}

// ============================================================================
// FEEDBACK HANDLERS (from ESP32)
// ============================================================================

// Called when ESP32 sends rumble command
static void uart_rumble_handler(uint8_t player_index, uint8_t left_motor,
                                 uint8_t right_motor, uint16_t duration_ms)
{
    (void)duration_ms;  // TODO: Implement timed rumble

    // Apply to per-player feedback state
    feedback_set_rumble(player_index, left_motor, right_motor);

    printf("[uart_output] Rumble P%d: L=%d R=%d\n",
           player_index, left_motor, right_motor);
}

// Called when ESP32 sends LED command
static void uart_led_handler(uint8_t player_index, uint8_t pattern,
                              uint8_t r, uint8_t g, uint8_t b)
{
    // Apply to per-player feedback state
    feedback_set_led_rgb(player_index, r, g, b);

    printf("[uart_output] LED P%d: pattern=%d RGB=(%d,%d,%d)\n",
           player_index, pattern, r, g, b);
}

// ============================================================================
// ROUTER TAP (sends inputs to UART)
// ============================================================================

// Called by router when input events occur
static void uart_router_tap(output_target_t output, uint8_t player_index,
                            const input_event_t* event)
{
    (void)output;

    // Queue input for UART transmission
    uart_device_queue_input(event, player_index);
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:usb2uart] Initializing USB2UART v%s\n", JOYPAD_VERSION);

    // Initialize player feedback system
    feedback_init();

    // Configure router for USB2UART
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_UART] = UART_OUTPUT_PLAYERS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_FLAGS,
    };
    router_init(&router_cfg);

    // Add route: USB → UART
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_UART, 0);

    // Register tap to send inputs over UART
    router_set_tap(OUTPUT_TARGET_UART, uart_router_tap);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:usb2uart] Initialization complete\n");
    printf("[app:usb2uart]   Routing: USB → UART (to ESP32)\n");
    printf("[app:usb2uart]   Player slots: %d (FIXED mode)\n", MAX_PLAYER_SLOTS);
    printf("[app:usb2uart]   Feedback: per-player rumble/LED from ESP32\n");
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // Nothing extra needed - output interface task handles everything
}
