// app.c - NUON2USB App
//
// Reads a Nuon controller via polyface protocol and outputs USB HID.
// Core 1: polyface host — enumerate controller, poll buttons/analog.
// Core 0: USB device output, UART debug.

#include "app.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/nuon/nuon_device.h"
#include "usb/usbd/usbd.h"
#include "polyface_clock.pio.h"
#include "polyface_host_send.pio.h"
#include "pico/bootrom.h"
#include "pico/stdio.h"
#include "hardware/clocks.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// POLYFACE HOST (runs on Core 1)
// ============================================================================

static volatile bool clock_running = false;

// Reverse button mapping: Nuon → JP (inverse of map_nuon_buttons in nuon_device.c)
static uint32_t map_nuon_to_jp(uint16_t nuon)
{
    uint32_t jp = 0;
    if (nuon & NUON_BUTTON_A)       jp |= JP_BUTTON_B1;   // A → Cross
    if (nuon & NUON_BUTTON_C_DOWN)  jp |= JP_BUTTON_B2;   // C-Down → Circle
    if (nuon & NUON_BUTTON_B)       jp |= JP_BUTTON_B3;   // B → Square
    if (nuon & NUON_BUTTON_C_LEFT)  jp |= JP_BUTTON_B4;   // C-Left → Triangle
    if (nuon & NUON_BUTTON_L)       jp |= JP_BUTTON_L1;
    if (nuon & NUON_BUTTON_R)       jp |= JP_BUTTON_R1;
    if (nuon & NUON_BUTTON_C_UP)    jp |= JP_BUTTON_L2;   // C-Up → L2
    if (nuon & NUON_BUTTON_C_RIGHT) jp |= JP_BUTTON_R2;   // C-Right → R2
    if (nuon & NUON_BUTTON_NUON)    jp |= JP_BUTTON_S1;   // Nuon/Z → Select
    if (nuon & NUON_BUTTON_START)   jp |= JP_BUTTON_S2;
    if (nuon & NUON_BUTTON_UP)      jp |= JP_BUTTON_DU;
    if (nuon & NUON_BUTTON_DOWN)    jp |= JP_BUTTON_DD;
    if (nuon & NUON_BUTTON_LEFT)    jp |= JP_BUTTON_DL;
    if (nuon & NUON_BUTTON_RIGHT)   jp |= JP_BUTTON_DR;
    return jp;
}

// Build and send a polyface command
static inline void __no_inline_not_in_flash_func(pf_put)(
    uint8_t type, uint8_t dataA, uint8_t dataS, uint8_t dataC)
{
    uint32_t desired = ((uint32_t)(type & 1) << 25) |
                       ((uint32_t)(dataA & 0xFF) << 17) |
                       ((uint32_t)(dataS & 0x7F) << 9) |
                       ((uint32_t)(dataC & 0x7F) << 1);
    pio_sm_put_blocking(pio, sm1, __rev(desired));
}

// Read next packet from PIO FIFO (blocks until available)
static inline void __no_inline_not_in_flash_func(pf_read)(uint32_t *w0, uint32_t *w1)
{
    while (pio_sm_get_rx_fifo_level(pio, sm2) < 2)
        tight_loop_contents();
    *w0 = pio_sm_get(pio, sm2);
    *w1 = pio_sm_get(pio, sm2);
}

// Send command and wait for device response (ctrl=0). Skip echoes (ctrl=1).
static uint32_t __no_inline_not_in_flash_func(pf_transact)(
    uint8_t dataA, uint8_t dataS, uint8_t dataC)
{
    pf_put(1, dataA, dataS, dataC);

    absolute_time_t timeout = make_timeout_time_ms(50);
    while (!time_reached(timeout)) {
        if (pio_sm_get_rx_fifo_level(pio, sm2) < 2) {
            tight_loop_contents();
            continue;
        }
        uint32_t w0 = pio_sm_get(pio, sm2);
        uint32_t w1 = pio_sm_get(pio, sm2);
        busy_wait_us(500);
        if ((w0 & 1) == 0) return w1;
    }
    return 0;
}

// Read an analog axis: sends CHANNEL + ANALOG, handles all echoes.
// Returns the CRC-encoded value. Extract with (resp >> 24) & 0xFF.
static uint32_t __no_inline_not_in_flash_func(pf_read_analog)(uint8_t channel)
{
    // Send CHANNEL (no response) then ANALOG (has response).
    // Both get queued in send PIO TX FIFO and sent sequentially.
    pf_put(0, 0x34, 0x01, channel);  // CHANNEL
    pf_put(1, 0x35, 0x01, 0x00);     // ANALOG

    // Read packets: expect CHANNEL echo, ANALOG echo, ANALOG response.
    // Skip all ctrl=1 (echoes), return first ctrl=0 (response).
    absolute_time_t timeout = make_timeout_time_ms(50);
    while (!time_reached(timeout)) {
        if (pio_sm_get_rx_fifo_level(pio, sm2) < 2) {
            tight_loop_contents();
            continue;
        }
        uint32_t w0 = pio_sm_get(pio, sm2);
        uint32_t w1 = pio_sm_get(pio, sm2);
        if ((w0 & 1) == 0) return w1;
    }
    return 0;
}

// Send command, drain echo, no response expected
static void __no_inline_not_in_flash_func(pf_send)(
    uint8_t type, uint8_t dataA, uint8_t dataS, uint8_t dataC)
{
    pf_put(type, dataA, dataS, dataC);
    // Wait for transmission to complete, then give device time to process
    while (pio->sm[sm1].addr != 0) tight_loop_contents();
    busy_wait_us(500);
    // Drain echo
    while (!pio_sm_is_rx_fifo_empty(pio, sm2))
        (void)pio_sm_get(pio, sm2);
}

static void __not_in_flash_func(host_core1)(void)
{
    while (!clock_running) tight_loop_contents();
    printf("[nuon2usb] Core 1: polyface host starting\n");

    while (1) {
        // ---- ENUMERATION ----
        bool enumerated = false;
        while (!enumerated) {
            pf_send(0, 0xB1, 0x00, 0x00);  // RESET
            busy_wait_us(1000);

            uint32_t resp = pf_transact(0x80, 0x00, 0x00);  // ALIVE
            if (resp == 0) { sleep_ms(200); continue; }
            printf("[nuon2usb] ALIVE: 0x%08lX\n", (unsigned long)resp);

            resp = pf_transact(0x90, 0x00, 0x00);  // MAGIC
            if (resp != 0x4A554445) {
                printf("[nuon2usb] MAGIC: 0x%08lX (bad)\n", (unsigned long)resp);
                sleep_ms(200);
                continue;
            }
            printf("[nuon2usb] MAGIC: JUDE\n");

            resp = pf_transact(0x94, 0x00, 0x00);  // PROBE
            printf("[nuon2usb] PROBE: type=%d ver=%d\n",
                   (int)((resp >> 16) & 0xFF), (int)((resp >> 24) & 0x7F));

            pf_send(0, 0xB4, 0x00, 0x00);  // BRAND id=0

            // Read device MODE to learn capabilities
            uint32_t mode_resp = pf_read_analog(0x00);  // CHANNEL=NONE → MODE
            uint8_t device_mode = (mode_resp >> 24) & 0xFF;
            printf("[nuon2usb] MODE=0x%02X\n", device_mode);

            enumerated = true;
            printf("[nuon2usb] Enumerated\n");
        }

        // ---- POLLING LOOP ----
        uint8_t disconnect_count = 0;
        while (disconnect_count < 30) {
            // Drain any stale data (read all complete packets)
            while (pio_sm_get_rx_fifo_level(pio, sm2) >= 2) {
                (void)pio_sm_get(pio, sm2);
                (void)pio_sm_get(pio, sm2);
            }

            // Buttons
            uint32_t btn_resp = pf_transact(0x30, 0x02, 0x00);
            if (btn_resp == 0) { disconnect_count++; continue; }
            disconnect_count = 0;

            uint16_t nuon_buttons = (btn_resp >> 16) & 0xFFFF;

            // Analog axes (CRC-encoded, value in upper byte)
            static uint8_t lx = 128, ly = 128, rx = 128, ry = 128;

            // Read analog axes one at a time: request, read, drain, next.
            uint32_t raw;

            // Read each axis: send CHANNEL (no response expected), then ANALOG.
            // pf_transact for ANALOG skips all ctrl=1 echoes (including CHANNEL echo).

            // Read one axis per poll, rotating through all 4 (15Hz each).
            static uint8_t axis_sel = 0;
            pf_send(0, 0x34, 0x01, 0x02 + axis_sel);
            raw = pf_transact(0x35, 0x01, 0x00);
            if (raw) {
                uint8_t val = (raw >> 24) & 0xFF;
                switch (axis_sel) {
                    case 0: lx = val; break;
                    case 1: ly = val; break;
                    case 2: rx = val; break;
                    case 3: ry = val; break;
                }
            }
            axis_sel = (axis_sel + 1) & 3;

            // Map and submit to router
            uint32_t jp_buttons = map_nuon_to_jp(nuon_buttons);

            input_event_t event;
            init_input_event(&event);
            event.dev_addr = 0xD0;
            event.instance = 0;
            event.type = INPUT_TYPE_GAMEPAD;
            event.buttons = jp_buttons;
            event.analog[ANALOG_LX] = lx;
            event.analog[ANALOG_LY] = ly;
            event.analog[ANALOG_RX] = rx;
            event.analog[ANALOG_RY] = ry;
            router_submit_input(&event);

            // Debug print (throttled)
            static uint32_t last_print = 0;
            uint32_t now = to_ms_since_boot(get_absolute_time());
            if (now - last_print >= 500) {
                last_print = now;
                printf("[poll] btn=%04X jp=%04lX lx=%d ly=%d rx=%d ry=%d\n",
                       nuon_buttons, (unsigned long)jp_buttons, lx, ly, rx, ry);
            }

            busy_wait_us(16000);  // ~60Hz
        }

        printf("[nuon2usb] Disconnected, re-enumerating\n");
    }
}

// ============================================================================
// OUTPUT INTERFACE (uses nuon_init for PIO setup, host_core1 for protocol)
// ============================================================================

static const OutputInterface nuon2usb_interface = {
    .name = "Nuon Host",
    .target = OUTPUT_TARGET_NUON,
    .init = nuon_init,
    .core1_task = host_core1,
    .task = NULL,
    .get_rumble = NULL,
    .get_player_led = NULL,
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
};

// ============================================================================
// APP INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = { NULL };
const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = 0;
    return input_interfaces;
}

static const OutputInterface* output_interfaces[] = {
    &nuon2usb_interface,   // Polyface host on Core 1
    &usbd_output_interface, // USB HID device output
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
    printf("[app:nuon2usb] Initializing NUON2USB v%s\n", APP_VERSION);

    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = 1,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);

    // Route: Nuon native input → USB device output
    router_add_route(INPUT_SOURCE_NATIVE_NUON, OUTPUT_TARGET_USB_DEVICE, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    static const profile_config_t profile_cfg = {
        .output_profiles = { NULL },
        .shared_profiles = NULL,
    };
    profile_init(&profile_cfg);

    // Pull-DOWN on data pin — keeps line solidly LOW between packets.
    // Prevents floating pin from generating spurious start bits on the device side.
    gpio_pull_down(DATAIO_PIN);

    // Replace device send PIO (control=0) with host send PIO (control=1)
    pio_sm_set_enabled(pio, sm1, false);
    pio_remove_program(pio, &polyface_send_program, 0);
    uint off_host_send = pio_add_program(pio, &polyface_host_send_program);
    polyface_host_send_program_init(pio, sm1, off_host_send, DATAIO_PIN);

    // Start clock on PIO1 at 1MHz
    PIO pio_clk = pio1;
    float clk_div = (float)clock_get_hz(clk_sys) / (1000000.0f * 4.0f);
    uint off_clk = pio_add_program(pio_clk, &polyface_clock_program);
    uint sm_clk = pio_claim_unused_sm(pio_clk, true);
    polyface_clock_program_init(pio_clk, sm_clk, off_clk, CLKIN_PIN, clk_div);
    clock_running = true;
    __sev();

    printf("[app:nuon2usb] Data=GPIO%d Clock=GPIO%d (1MHz)\n", DATAIO_PIN, CLKIN_PIN);
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    int c = getchar_timeout_us(0);
    if (c == 'B') reset_usb_boot(0, 0);
}
