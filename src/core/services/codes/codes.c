// codes.c - Button sequence detection service

#include "core/services/codes/codes.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include <stdio.h>

// Sequence length for code detection
#define CODE_LENGTH 10

// Known sequences
#define KONAMI_CODE {JP_BUTTON_DU, JP_BUTTON_DU, JP_BUTTON_DD, JP_BUTTON_DD, \
                     JP_BUTTON_DL, JP_BUTTON_DR, JP_BUTTON_DL, JP_BUTTON_DR, \
                     JP_BUTTON_B1, JP_BUTTON_B2}

// Internal state
static uint32_t code_buffer[CODE_LENGTH] = {0};
static const uint32_t sequence_test_mode[CODE_LENGTH] = KONAMI_CODE;
static bool test_mode = false;
static uint8_t test_counter = 0;

// Previous button state for edge detection
static uint32_t prev_buttons = 0xFFFFF;

// Callback for code detection notifications
static codes_callback_t code_callback = NULL;

// Internal helpers
static void shift_buffer_and_insert(uint32_t new_value);
static void check_for_sequence_match(void);

// ============================================================================
// PUBLIC API
// ============================================================================

bool codes_is_test_mode(void)
{
    return test_mode;
}

void codes_reset_test_mode(void)
{
    test_mode = false;
    test_counter = 0;
}

uint8_t codes_get_test_counter(void)
{
    // Increment counter each call when test mode is active
    if (test_mode) {
        test_counter++;
    }
    return test_counter;
}

void codes_set_callback(codes_callback_t callback)
{
    code_callback = callback;
}

// ============================================================================
// SEQUENCE DETECTION
// ============================================================================

// Internal function that processes button state for sequence detection
static void codes_process_buttons(const input_event_t* event)
{
    if (!event) return;

    // USBR buttons use inverted logic (0 = pressed, 1 = released)
    // Invert to get positive logic (1 = pressed)
    uint32_t btns = ~event->buttons & 0x3f;  // D-pad (0x0F) + B1/B2 (0x30)
    uint32_t prev = ~prev_buttons & 0x3f;

    // Detect button press edge (new press that wasn't pressed before)
    if (btns && btns != prev) {
        uint32_t new_presses = btns & ~prev;
        if (new_presses) {
            shift_buffer_and_insert(new_presses);
            check_for_sequence_match();
        }
        prev_buttons = event->buttons;
    } else if (!btns && prev) {
        prev_buttons = event->buttons;
    }
}

// Called by console update_output() after sending data to console
// Reads button state from router (player 0) for sequence detection
void codes_task(void)
{
    // Get current button state from router (player 0)
    const input_event_t* event = router_get_output(OUTPUT_TARGET_GAMECUBE, 0);

    // Fallback to other outputs if GameCube returns NULL
    if (!event) event = router_get_output(OUTPUT_TARGET_PCENGINE, 0);
    if (!event) event = router_get_output(OUTPUT_TARGET_NUON, 0);
    if (!event) event = router_get_output(OUTPUT_TARGET_XBOXONE, 0);
    if (!event) event = router_get_output(OUTPUT_TARGET_LOOPY, 0);
    if (!event) event = router_get_output(OUTPUT_TARGET_USB_DEVICE, 0);
    if (!event) return;

    codes_process_buttons(event);
}

// Task with explicit output target (for controller app)
void codes_task_for_output(output_target_t output)
{
    const input_event_t* event = router_get_output(output, 0);
    codes_process_buttons(event);
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static void shift_buffer_and_insert(uint32_t new_value)
{
    for (int i = 0; i < CODE_LENGTH - 1; i++) {
        code_buffer[i] = code_buffer[i + 1];
    }
    code_buffer[CODE_LENGTH - 1] = new_value;
}

static void check_for_sequence_match(void)
{
    // Check for Konami code sequence
    bool match = true;
    for (int i = 0; i < CODE_LENGTH; i++) {
        if (code_buffer[i] != sequence_test_mode[i]) {
            match = false;
            break;
        }
    }

    if (match) {
        test_mode = !test_mode;
        if (test_mode) {
            printf("[codes] Konami code detected! Test mode enabled\n");
        } else {
            printf("[codes] Konami code detected! Test mode disabled\n");
            test_counter = 0;
        }

        // Notify callback
        if (code_callback) {
            code_callback("KONAMI");
        }

        // Clear buffer to prevent immediate re-trigger
        for (int i = 0; i < CODE_LENGTH; i++) {
            code_buffer[i] = 0;
        }
    }
}
