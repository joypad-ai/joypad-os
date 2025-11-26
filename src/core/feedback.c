// feedback.c
// USBRetro canonical feedback implementation
//
// Manages per-player feedback state that device drivers read and apply
// to their specific hardware.

#include "core/feedback.h"
#include "core/services/players/manager.h"
#include <string.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

static feedback_state_t feedback_states[MAX_PLAYERS];
static bool initialized = false;

// Default player colors (PS4/DualSense style)
static const uint8_t player_colors[4][3] = {
    {0x00, 0x00, 0xFF},  // Player 1: Blue
    {0xFF, 0x00, 0x00},  // Player 2: Red
    {0x00, 0xFF, 0x00},  // Player 3: Green
    {0xFF, 0x00, 0xFF},  // Player 4: Pink/Magenta
};

// ============================================================================
// PUBLIC API
// ============================================================================

void feedback_init(void)
{
    memset(feedback_states, 0, sizeof(feedback_states));

    // Initialize all players with default LED state
    for (int i = 0; i < MAX_PLAYERS; i++) {
        feedback_states[i].led.brightness = 255;
        feedback_states[i].led.pattern = FEEDBACK_LED_NONE;
    }

    initialized = true;
}

feedback_state_t* feedback_get_state(uint8_t player_index)
{
    if (player_index >= MAX_PLAYERS) return NULL;
    return &feedback_states[player_index];
}

void feedback_set_rumble(uint8_t player_index, uint8_t left, uint8_t right)
{
    if (player_index >= MAX_PLAYERS) return;

    feedback_state_t* state = &feedback_states[player_index];

    if (state->rumble.left != left || state->rumble.right != right) {
        state->rumble.left = left;
        state->rumble.right = right;
        state->rumble_dirty = true;
    }
}

void feedback_set_rumble_ext(uint8_t player_index, const feedback_rumble_t* rumble)
{
    if (player_index >= MAX_PLAYERS || rumble == NULL) return;

    feedback_state_t* state = &feedback_states[player_index];

    if (memcmp(&state->rumble, rumble, sizeof(feedback_rumble_t)) != 0) {
        state->rumble = *rumble;
        state->rumble_dirty = true;
    }
}

void feedback_set_led_player(uint8_t player_index, uint8_t player_num)
{
    if (player_index >= MAX_PLAYERS) return;

    feedback_state_t* state = &feedback_states[player_index];

    // Set player indicator pattern
    uint8_t pattern = FEEDBACK_LED_NONE;
    if (player_num >= 1 && player_num <= 4) {
        pattern = (1 << (player_num - 1));  // PLAYER1=0x01, PLAYER2=0x02, etc.
    }

    // Set RGB color based on player number
    uint8_t r = 0, g = 0, b = 0;
    if (player_num >= 1 && player_num <= 4) {
        r = player_colors[player_num - 1][0];
        g = player_colors[player_num - 1][1];
        b = player_colors[player_num - 1][2];
    }

    if (state->led.pattern != pattern ||
        state->led.r != r || state->led.g != g || state->led.b != b) {
        state->led.pattern = pattern;
        state->led.r = r;
        state->led.g = g;
        state->led.b = b;
        state->led_dirty = true;
    }
}

void feedback_set_led_rgb(uint8_t player_index, uint8_t r, uint8_t g, uint8_t b)
{
    if (player_index >= MAX_PLAYERS) return;

    feedback_state_t* state = &feedback_states[player_index];

    if (state->led.r != r || state->led.g != g || state->led.b != b) {
        state->led.r = r;
        state->led.g = g;
        state->led.b = b;
        state->led_dirty = true;
    }
}

void feedback_set_led(uint8_t player_index, const feedback_led_t* led)
{
    if (player_index >= MAX_PLAYERS || led == NULL) return;

    feedback_state_t* state = &feedback_states[player_index];

    if (memcmp(&state->led, led, sizeof(feedback_led_t)) != 0) {
        state->led = *led;
        state->led_dirty = true;
    }
}

void feedback_set_trigger(uint8_t player_index, bool left, const feedback_trigger_t* trigger)
{
    if (player_index >= MAX_PLAYERS || trigger == NULL) return;

    feedback_state_t* state = &feedback_states[player_index];
    feedback_trigger_t* target = left ? &state->left_trigger : &state->right_trigger;

    if (memcmp(target, trigger, sizeof(feedback_trigger_t)) != 0) {
        *target = *trigger;
        state->triggers_dirty = true;
    }
}

void feedback_clear(uint8_t player_index)
{
    if (player_index >= MAX_PLAYERS) return;

    feedback_state_t* state = &feedback_states[player_index];

    // Clear rumble
    if (state->rumble.left != 0 || state->rumble.right != 0 ||
        state->rumble.left_trigger != 0 || state->rumble.right_trigger != 0) {
        memset(&state->rumble, 0, sizeof(feedback_rumble_t));
        state->rumble_dirty = true;
    }

    // Clear LED (but keep brightness)
    uint8_t brightness = state->led.brightness;
    if (state->led.pattern != FEEDBACK_LED_NONE ||
        state->led.r != 0 || state->led.g != 0 || state->led.b != 0) {
        memset(&state->led, 0, sizeof(feedback_led_t));
        state->led.brightness = brightness;
        state->led_dirty = true;
    }

    // Clear triggers
    if (state->left_trigger.mode != TRIGGER_MODE_OFF ||
        state->right_trigger.mode != TRIGGER_MODE_OFF) {
        memset(&state->left_trigger, 0, sizeof(feedback_trigger_t));
        memset(&state->right_trigger, 0, sizeof(feedback_trigger_t));
        state->triggers_dirty = true;
    }
}

void feedback_clear_dirty(uint8_t player_index)
{
    if (player_index >= MAX_PLAYERS) return;

    feedback_states[player_index].rumble_dirty = false;
    feedback_states[player_index].led_dirty = false;
    feedback_states[player_index].triggers_dirty = false;
}
