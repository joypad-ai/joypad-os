// arcade_host.c - Native Arcade Controller Host Driver
//
// Polls native Arcade controllers and submits input events to the router.

#include "arcade_host.h"
#include "core/services/hotkeys/hotkeys.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "hardware/structs/sio.h"
#include "hardware/gpio.h"
#include <stdio.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

static arcade_controller_t arcade_pads[ARCADE_MAX_PORTS];
static bool initialized = false;

// Track previous state for edge detection
static uint32_t prev_buttons[ARCADE_MAX_PORTS] = {0};

// ============================================================================
// Internal GPIO Functions
// ============================================================================

// Initialize GPIO pins
static void arcadepad_gpio_init(arcade_controller_t* pad)
{
    gpio_init_mask(pad->gpio_mask);
    gpio_set_dir_in_masked(pad->gpio_mask);

    for (int i = 0; i < 32; i++) {
        if (pad->gpio_mask & (1 << i)) {
            gpio_pull_up(i);
        }
    }
}

// Read device data
static uint32_t arcadepad_read(arcade_controller_t* pad)
{
    // Read all GPIO
    uint32_t raw_data = gpio_get_all();

    // Return only masked pins
    return (~raw_data) & pad->gpio_mask;
}


// ============================================================================
// Arcade Pad API  Functions
// ============================================================================
void arcadepad_init(arcade_controller_t* pad, arcade_config_t* conf)
{
    pad->type = ARCADEPAD_NONE;
    pad->gpio_mask = 0;

    // Pin Mask
    pad->mask_du = GPIO_MASK(conf->pin_du);
    pad->mask_dd = GPIO_MASK(conf->pin_dd);
    pad->mask_dr = GPIO_MASK(conf->pin_dr);
    pad->mask_dl = GPIO_MASK(conf->pin_dl);
    pad->mask_p1 = GPIO_MASK(conf->pin_p1);
    pad->mask_p2 = GPIO_MASK(conf->pin_p2);
    pad->mask_p3 = GPIO_MASK(conf->pin_p3);
    pad->mask_p4 = GPIO_MASK(conf->pin_p4);
    pad->mask_k1 = GPIO_MASK(conf->pin_k1);
    pad->mask_k2 = GPIO_MASK(conf->pin_k2);
    pad->mask_k3 = GPIO_MASK(conf->pin_k3);
    pad->mask_k4 = GPIO_MASK(conf->pin_k4);
    pad->mask_s1 = GPIO_MASK(conf->pin_s1);
    pad->mask_s2 = GPIO_MASK(conf->pin_s2);
    pad->mask_a1 = GPIO_MASK(conf->pin_a1);
    pad->mask_a2 = GPIO_MASK(conf->pin_a2);
    pad->mask_l3 = GPIO_MASK(conf->pin_l3);
    pad->mask_r3 = GPIO_MASK(conf->pin_r3);
    pad->mask_l4 = GPIO_MASK(conf->pin_l4);
    pad->mask_r4 = GPIO_MASK(conf->pin_r4);

    pad->gpio_mask = (pad->mask_du | pad->mask_dd | pad->mask_dr | pad->mask_dl |
                     pad->mask_p1 | pad->mask_p2 | pad->mask_p3 | pad->mask_p4 |
                     pad->mask_k1 | pad->mask_k2 | pad->mask_k3 | pad->mask_k4 |
                     pad->mask_s1 | pad->mask_s2 | pad->mask_a1 | pad->mask_a2 |
                     pad->mask_l3 | pad->mask_r3 | pad->mask_l4 | pad->mask_r4);

#if ARCADE_PAD_DEBUG
    printf("[arcade_host] MASK_DU:  0x%08X\n", pad->mask_du);
    printf("[arcade_host] MASK_DD:  0x%08X\n", pad->mask_dd);
    printf("[arcade_host] MASK_DR:  0x%08X\n", pad->mask_dr);
    printf("[arcade_host] MASK_DL:  0x%08X\n", pad->mask_dl);
    printf("[arcade_host] MASK_P1:  0x%08X\n", pad->mask_p1);
    printf("[arcade_host] MASK_P2:  0x%08X\n", pad->mask_p2);
    printf("[arcade_host] MASK_P3:  0x%08X\n", pad->mask_p3);
    printf("[arcade_host] MASK_P4:  0x%08X\n", pad->mask_p4);
    printf("[arcade_host] MASK_K1:  0x%08X\n", pad->mask_k1);
    printf("[arcade_host] MASK_K2:  0x%08X\n", pad->mask_k2);
    printf("[arcade_host] MASK_K3:  0x%08X\n", pad->mask_k3);
    printf("[arcade_host] MASK_K4:  0x%08X\n", pad->mask_k4);
    printf("[arcade_host] MASK_S1:  0x%08X\n", pad->mask_s1);
    printf("[arcade_host] MASK_S2:  0x%08X\n", pad->mask_s2);
    printf("[arcade_host] MASK_A1:  0x%08X\n", pad->mask_a1);
    printf("[arcade_host] MASK_A2:  0x%08X\n", pad->mask_a2);
    printf("[arcade_host] MASK_L3:  0x%08X\n", pad->mask_l3);
    printf("[arcade_host] MASK_R3:  0x%08X\n", pad->mask_r3);
    printf("[arcade_host] MASK_L4:  0x%08X\n", pad->mask_l4);
    printf("[arcade_host] MASK_R4:  0x%08X\n", pad->mask_r4);
    printf("[arcade_host] GPIO_MASK: 0x%08X\n", pad->gpio_mask);
#endif

    // Buttons
    pad->button_p1  = false;
    pad->button_p2  = false;
    pad->button_p3  = false;
    pad->button_p4  = false;
    pad->button_k1  = false;
    pad->button_k2  = false;
    pad->button_k3  = false;
    pad->button_k4  = false;

    pad->button_s1  = false;
    pad->button_s2  = false;
    pad->button_a1  = false;
    pad->button_a2  = false;

    pad->button_l3  = false;
    pad->button_r3  = false;

    pad->dpad_up    = false;
    pad->dpad_down  = false;
    pad->dpad_left  = false;
    pad->dpad_right = false;

    pad->dpad_mode  = DPAD_MODE_DPAD;

    pad->last_read  = 0;
}

void arcadepad_start(arcade_controller_t* pad)
{
#if ARCADE_PAD_DEBUG
    printf("arcadepad_start\n");
#endif

    pad->type = ARCADEPAD_CONTROLLER;

    // Reset state
    pad->button_p1  = false;
    pad->button_p2  = false;
    pad->button_p3  = false;
    pad->button_p4  = false;
    pad->button_k1  = false;
    pad->button_k2  = false;
    pad->button_k3  = false;
    pad->button_k4  = false;

    pad->button_s1  = false;
    pad->button_s2  = false;
    pad->button_a1  = false;
    pad->button_a2  = false;

    pad->button_l3  = false;
    pad->button_r3  = false;
    pad->button_l4  = false;
    pad->button_r4  = false;

    pad->dpad_up    = false;
    pad->dpad_down  = false;
    pad->dpad_left  = false;
    pad->dpad_right = false;

    pad->dpad_mode  = DPAD_MODE_DPAD;
}

void arcadepad_begin(arcade_controller_t* pad)
{
    arcadepad_gpio_init(pad);

#if ARCADE_PAD_DEBUG
    printf("arcadepad_begin\n");
#endif
}

void arcadepad_poll(arcade_controller_t* pad)
{
    int32_t state = 0;

#if ARCADE_PAD_DEBUG
    printf("arcadepad_poll: ");
#endif
    if (pad->type == ARCADEPAD_NONE) {
        arcadepad_start(pad);
        return;
    }

    state = arcadepad_read(pad);

    pad->button_p1  = (state & pad->mask_p1);
    pad->button_p2  = (state & pad->mask_p2);
    pad->button_p3  = (state & pad->mask_p3);
    pad->button_p4  = (state & pad->mask_p4);
    pad->button_k1  = (state & pad->mask_k1);
    pad->button_k2  = (state & pad->mask_k2);
    pad->button_k3  = (state & pad->mask_k3);
    pad->button_k4  = (state & pad->mask_k4);

    pad->button_s1  = (state & pad->mask_s1);
    pad->button_s2  = (state & pad->mask_s2);
    pad->button_a1  = (state & pad->mask_a1);
    pad->button_a2  = (state & pad->mask_a2);

    pad->button_l3  = (state & pad->mask_l3);
    pad->button_r3  = (state & pad->mask_r3);
    pad->button_l4  = (state & pad->mask_l4);
    pad->button_r4  = (state & pad->mask_r4);

    pad->dpad_up    = (state & pad->mask_du);
    pad->dpad_down  = (state & pad->mask_dd);
    pad->dpad_left  = (state & pad->mask_dl);
    pad->dpad_right = (state & pad->mask_dr);

#if ARCADE_PAD_DEBUG
    if (pad->last_read != (uint32_t)state) {
        printf("P1:%d P2:%d P3:%d P4:%d K1:%d K2:%d K3:%d K4:%d S1:%d S2:%d \n",
                pad->button_p1, pad->button_p2, pad->button_p3, pad->button_p4,
                pad->button_k1, pad->button_k2, pad->button_k3, pad->button_k4, 
                pad->button_s1, pad->button_s2);
    }
    pad->last_read = state;
#endif
}

// ============================================================================
// HOTKEY CallBack
// ============================================================================

static void dpad_callback(uint8_t player, uint32_t held_ms) {
    (void)held_ms;
    arcade_controller_t* pad = &arcade_pads[player];
    pad->dpad_mode = DPAD_MODE_DPAD;
}

static void lstick_callback(uint8_t player, uint32_t held_ms) {
    (void)held_ms;
    arcade_controller_t* pad = &arcade_pads[player];
    pad->dpad_mode = DPAD_MODE_LEFT_STICK;
}

static void rstick_callback(uint8_t player, uint32_t held_ms) {
    (void)held_ms;
    arcade_controller_t* pad = &arcade_pads[player];
    pad->dpad_mode = DPAD_MODE_RIGHT_STICK;
}

// ============================================================================
// BUTTON MAPPING: Arcade → JoyPad
// ============================================================================

// Map Arcade controller state to JoyPad button format
// Uses Switch-style layout (matches GP2040-CE Switch column)
static uint32_t map_arcade_to_joypad(const arcade_controller_t* pad)
{
    uint32_t buttons = 0x00000000;  // JoyPad uses active-high

    // Arcade layout
    if (pad->button_p1)      buttons |= JP_BUTTON_B3;  // P1 → B3 (Square/X)
    if (pad->button_p2)      buttons |= JP_BUTTON_B4;  // P2 → B4 (Triangle/Y)
    if (pad->button_p3)      buttons |= JP_BUTTON_R1;  // P3 → R1
    if (pad->button_p4)      buttons |= JP_BUTTON_L1;  // P4 → L1
    if (pad->button_k1)      buttons |= JP_BUTTON_B1;  // K1 → B1 (Cross/A)
    if (pad->button_k2)      buttons |= JP_BUTTON_B2;  // K2 → B2 (Circle/B)
    if (pad->button_k3)      buttons |= JP_BUTTON_R2;  // K3 → R2
    if (pad->button_k4)      buttons |= JP_BUTTON_L2;  // K4 → L2

    // System buttons
    if (pad->button_s1)      buttons |= JP_BUTTON_S1;  // Coin → S1 (Select)
    if (pad->button_s2)      buttons |= JP_BUTTON_S2;  // Start → S2 (Start)
    if (pad->button_a1)      buttons |= JP_BUTTON_A1;
    if (pad->button_a2)      buttons |= JP_BUTTON_A2;

    // Extra buttons
    if (pad->button_l3)      buttons |= JP_BUTTON_L3;
    if (pad->button_r3)      buttons |= JP_BUTTON_R3;
    if (pad->button_l4)      buttons |= JP_BUTTON_L4;
    if (pad->button_r4)      buttons |= JP_BUTTON_R4;

    // D-pad
    if (pad->dpad_up)        buttons |= JP_BUTTON_DU;
    if (pad->dpad_down)      buttons |= JP_BUTTON_DD;
    if (pad->dpad_left)      buttons |= JP_BUTTON_DL;
    if (pad->dpad_right)     buttons |= JP_BUTTON_DR;

    return buttons;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void arcade_host_init(void)
{
    // Skip if already initialized (app may have called init_pins with custom config)
    if (initialized) return;

    arcade_config_t config = PORT_CONFIG_INIT;

    arcade_host_init_pins(&config);
}

void arcade_host_init_pins(arcade_config_t* conf)
{
    printf("[arcade_host] Initializing Arcade host driver\n");
    printf("[arcade_host]   DU=%d DD=%d DR=%d DL=%d\n", 
           conf->pin_du, conf->pin_dd, conf->pin_dl, conf->pin_dr);
    printf("[arcade_host]   P1=%d P2=%d P3=%d P4=%d\n", 
           conf->pin_p1, conf->pin_p2, conf->pin_p3, conf->pin_p4);
    printf("[arcade_host]   K1=%d K2=%d K3=%d K4=%d\n", 
           conf->pin_k1, conf->pin_k2, conf->pin_k3, conf->pin_k4);
    printf("[arcade_host]   S1=%d S2=%d A1=%d A2=%d\n", 
           conf->pin_s1, conf->pin_s2, conf->pin_a1, conf->pin_a2);
    printf("[arcade_host]   L3=%d R3=%d L4=%d R4=%d\n", 
           conf->pin_l3, conf->pin_r3, conf->pin_l4, conf->pin_r4);

    // Initialize Arcade for port 0 direct connection
    arcadepad_init(&arcade_pads[0], conf);
    arcadepad_begin(&arcade_pads[0]);
    arcadepad_start(&arcade_pads[0]);
    prev_buttons[0] = 0xFFFFFFFF;

    // Register hotkeys
    // Long hold (2s) triggers power button
    HotkeyDef dpad_mode = {
        .buttons = DPAD_MODE_DP_COMBO_MASK,
        .duration_ms = DPAD_MODE_HOLD_DURATION,
        .trigger = HOTKEY_TRIGGER_ON_HOLD,
        .callback = dpad_callback,
        .global = false
    };
    hotkeys_register(&dpad_mode);

    // Quick tap (release before 2s) triggers stop button
    HotkeyDef lstick_mode = {
        .buttons = DPAD_MODE_LS_COMBO_MASK,
        .duration_ms = DPAD_MODE_HOLD_DURATION,
        .trigger = HOTKEY_TRIGGER_ON_HOLD,
        .callback = lstick_callback,
        .global = false
    };
    hotkeys_register(&lstick_mode);

    // Quick tap (release before 2s) triggers stop button
    HotkeyDef rstick_mode = {
        .buttons = DPAD_MODE_RS_COMBO_MASK,
        .duration_ms = DPAD_MODE_HOLD_DURATION,
        .trigger = HOTKEY_TRIGGER_ON_HOLD,
        .callback = rstick_callback,
        .global = false
    };
    hotkeys_register(&rstick_mode);

    initialized = true;
    printf("[arcade_host] Initialization complete port 0 active\n");
}

void arcade_host_task(void)
{
    if (!initialized) return;

    // Only port 0 is active direct connection
    arcade_controller_t* pad = &arcade_pads[0];

    // Poll the controller
    arcadepad_poll(pad);
    // Skip if no device connected
    if (pad->type == ARCADEPAD_NONE) {
        return;
    }
    // Map buttons based on device type
    uint32_t buttons;
    uint8_t analog_1x = 128;  // Center
    uint8_t analog_1y = 128;
    uint8_t analog_2x = 128;
    uint8_t analog_2y = 128;

    buttons = map_arcade_to_joypad(pad);
    hotkeys_check(buttons, 0);
    // =================================================================
    // Apply d-pad mode (remap d-pad to analog stick if needed)
    // =================================================================
    if (pad->dpad_mode != DPAD_MODE_DPAD) {
        uint32_t dpad_bits = buttons & (JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);
        buttons &= ~(JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);

        uint8_t ax = 128, ay = 128;
        if (dpad_bits & JP_BUTTON_DL) ax = 0;
        else if (dpad_bits & JP_BUTTON_DR) ax = 255;
        if (dpad_bits & JP_BUTTON_DU) ay = 0;
        else if (dpad_bits & JP_BUTTON_DD) ay = 255;

        if (pad->dpad_mode == DPAD_MODE_LEFT_STICK) {
            analog_1x = ax;
            analog_1y = ay;
        } else {
            analog_2x = ax;
            analog_2y = ay;
        }
    }

    // Only submit if state changed
    if (buttons == prev_buttons[0]) {
        return;
    }
    prev_buttons[0] = buttons;

    // Build input event
    input_event_t event;
    init_input_event(&event);

    event.dev_addr = 0xF0;  // Use 0xF0+ range for native inputs
    event.instance = 0;
    event.type = INPUT_TYPE_ARCADE_STICK;
    event.buttons = buttons;
    event.analog[ANALOG_LX] = analog_1x;
    event.analog[ANALOG_LY] = analog_1y;
    event.analog[ANALOG_RX] = analog_2x;
    event.analog[ANALOG_RY] = analog_2y;
    event.analog[ANALOG_L2] = (buttons & JP_BUTTON_L2) ? 255 : 0;
    event.analog[ANALOG_R2] = (buttons & JP_BUTTON_R2) ? 255 : 0;

    // Submit to router
    router_submit_input(&event);
}

int8_t arcade_host_get_device_type(uint8_t port)
{
    if (!initialized || port >= ARCADE_MAX_PORTS) {
        return -1;
    }
    return arcade_pads[port].type;
}

bool arcade_host_is_connected(void)
{
    // Parallel negative logic, always return true
    return true;
}

// ============================================================================
// INPUT INTERFACE (for app declaration)
// ============================================================================

static uint8_t arcade_get_device_count(void)
{
    uint8_t count = 0;
    for (int i = 0; i < ARCADE_MAX_PORTS; i++) {
        if (arcade_pads[i].type != ARCADEPAD_NONE) {
            count++;
        }
    }
    return count;
}

const InputInterface arcade_input_interface = {
    .name = "ARCADE",
    .source = INPUT_SOURCE_NATIVE_ARCADE,
    .init = arcade_host_init,
    .task = arcade_host_task,
    .is_connected = arcade_host_is_connected,
    .get_device_count = arcade_get_device_count,
};
