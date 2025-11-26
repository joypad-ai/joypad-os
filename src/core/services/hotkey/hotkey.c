// hotkey.c - Hotkey detection service

#include "core/services/hotkey/hotkey.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include <stdio.h>

// Konami code sequence
#define KONAMI_CODE {USBR_BUTTON_DU, USBR_BUTTON_DU, USBR_BUTTON_DD, USBR_BUTTON_DD, \
                     USBR_BUTTON_DL, USBR_BUTTON_DR, USBR_BUTTON_DL, USBR_BUTTON_DR, \
                     USBR_BUTTON_B1, USBR_BUTTON_B2}

// Internal state
static uint32_t code_buffer[HOTKEY_LENGTH] = {0};
static const uint32_t konami_code[HOTKEY_LENGTH] = KONAMI_CODE;
static bool test_mode = false;
static uint8_t test_counter = 0;

// Previous button state for edge detection
static uint32_t codes_prev_buttons = 0xFFFFF;

// Internal helpers
static void shift_buffer_and_insert(uint32_t new_value);
static void check_for_hotkey_match(void);

// ============================================================================
// PUBLIC API
// ============================================================================

bool hotkey_is_test_mode(void)
{
    return test_mode;
}

void hotkey_reset_test_mode(void)
{
    test_mode = false;
    test_counter = 0;
}

uint8_t hotkey_get_test_counter(void)
{
    // Increment counter each call when test mode is active
    if (test_mode) {
        test_counter++;
    }
    return test_counter;
}

// ============================================================================
// HOTKEY DETECTION
// ============================================================================

// Called by console update_output() after sending data to console
// Reads button state from router (player 0) for hotkey detection
void codes_task(void)
{
    // Get current button state from router (player 0)
    const input_event_t* event = router_get_output(OUTPUT_TARGET_GAMECUBE, 0);

    // Fallback to other outputs if GameCube returns NULL
    if (!event) event = router_get_output(OUTPUT_TARGET_PCENGINE, 0);
    if (!event) event = router_get_output(OUTPUT_TARGET_NUON, 0);
    if (!event) event = router_get_output(OUTPUT_TARGET_XBOXONE, 0);
    if (!event) event = router_get_output(OUTPUT_TARGET_LOOPY, 0);
    if (!event) return;

    // USBR buttons use inverted logic (0 = pressed, 1 = released)
    // Invert to get positive logic (1 = pressed)
    uint32_t btns = ~event->buttons & 0x3f;  // D-pad (0x0F) + B1/B2 (0x30)
    uint32_t prev_btns = ~codes_prev_buttons & 0x3f;

    // Detect button press edge (new press that wasn't pressed before)
    if (btns && btns != prev_btns) {
        uint32_t new_presses = btns & ~prev_btns;
        if (new_presses) {
            shift_buffer_and_insert(new_presses);
            check_for_hotkey_match();
        }
        codes_prev_buttons = event->buttons;
    } else if (!btns && prev_btns) {
        codes_prev_buttons = event->buttons;
    }
}

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static void shift_buffer_and_insert(uint32_t new_value)
{
    for (int i = 0; i < HOTKEY_LENGTH - 1; i++) {
        code_buffer[i] = code_buffer[i + 1];
    }
    code_buffer[HOTKEY_LENGTH - 1] = new_value;
}

static void check_for_hotkey_match(void)
{
    for (int i = 0; i < HOTKEY_LENGTH; i++) {
        if (code_buffer[i] != konami_code[i]) {
            return;
        }
    }

    // Hotkey sequence matched - toggle test mode
    test_mode = !test_mode;
    if (test_mode) {
        printf("[hotkey] Test mode enabled\n");
    } else {
        printf("[hotkey] Test mode disabled\n");
        test_counter = 0;
    }
}
