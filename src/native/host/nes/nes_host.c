#include "nes_host.h"
#include "nes_host.pio.h"
#include "native/host/host_interface.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "pico/time.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"

#define NES_BTN_INDEX_A       0
#define NES_BTN_INDEX_B       1
#define NES_BTN_INDEX_SELECT  2
#define NES_BTN_INDEX_START   3
#define NES_BTN_INDEX_UP      4
#define NES_BTN_INDEX_DOWN    5
#define NES_BTN_INDEX_LEFT    6
#define NES_BTN_INDEX_RIGHT   7
#define NES_BTN_COUNT         8

typedef struct
{
    PIO pio;
    uint sm;
    uint8_t irq_flag;
    uint8_t prev_buttons;
} tick_ctx_t;

static tick_ctx_t *s_ctx;

// Fractional period accumulator so 60 Hz stays accurate over time.
static const int32_t PERIOD_US_INT = 1000000 / 60; // 16666
static const int32_t PERIOD_US_REM = 1000000 % 60; // 40 (so 16666*60 + 40*? = 1e6)
static int32_t frac_accum = 0;

static repeating_timer_t nes_timer;

static bool nes_timer_cb(repeating_timer_t *rt)
{
    tick_ctx_t *ctx = (tick_ctx_t*)rt->user_data;

    // Force/set PIO IRQ flag N (CPU -> PIO). PIO will see it in `wait 1 irq N`.
    // Write-one-to-set on irq_force.
    ctx->pio->irq_force = (1u << ctx->irq_flag);

    // Reschedule with fractional correction to maintain 60.000 Hz on average.
    // 1/60 s = 16666 + 40/60 us, so add +1 us on 40 out of every 60 callbacks.
    frac_accum += PERIOD_US_REM;
    int32_t adj = 0;
    if (frac_accum >= 60) { adj = 1; frac_accum -= 60; }
    rt->delay_us = -(int64_t)(PERIOD_US_INT + adj);

    return true;
}

static inline void nes_sm_init(PIO pio, uint sm, uint offset)
{
    pio_sm_set_enabled(pio, sm, false);

    pio_sm_config c = nes_host_program_get_default_config(offset);
    sm_config_set_in_pins(&c, NES_PIN_DATA0);
    sm_config_set_sideset_pins(&c, NES_PIN_CLOCK); // side bit 0 → GPIO2 (clock), bit 1 → GPIO3 (latch)
    sm_config_set_in_shift(&c, true, true, 8);

    pio_gpio_init(pio, NES_PIN_LATCH);
    pio_gpio_init(pio, NES_PIN_CLOCK);
    pio_gpio_init(pio, NES_PIN_DATA0);
    gpio_pull_up(NES_PIN_DATA0);

    float div = (float)clock_get_hz(clk_sys) / 1e6f; // 1 MHz instruction rate
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_consecutive_pindirs(pio, sm, NES_PIN_CLOCK, 1, true);   // CLK out
    pio_sm_set_consecutive_pindirs(pio, sm, NES_PIN_LATCH, 1, true); // LATCH out
    pio_sm_set_consecutive_pindirs(pio, sm, NES_PIN_DATA0, 1, false); // DATA in

    // Clean start
    pio_interrupt_clear(pio, 0);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_restart(pio, sm);
    pio_sm_clkdiv_restart(pio, sm);

    pio_sm_set_enabled(pio, sm, true);
}

static void __isr pio0_irq0_handler(void) {
    tick_ctx_t *ctx = s_ctx;
    if(!ctx) { return; }

    while(!pio_sm_is_rx_fifo_empty(ctx->pio, ctx->sm)) {
        uint32_t word = pio_sm_get(ctx->pio, ctx->sm);
        uint8_t raw = (word >> 24) & 0xFF;
        uint8_t buttons = ~raw; // Flip from active low to active high
        ctx->prev_buttons = buttons;
    }
}

static void enable_fifo_irq(tick_ctx_t *ctx) {
    s_ctx = ctx;

    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_irq0_handler);
    irq_set_enabled(PIO0_IRQ_0, true);

    enum pio_interrupt_source src = pio_get_rx_fifo_not_empty_interrupt_source(ctx->sm);
    pio_set_irq0_source_enabled(ctx->pio, src, true);
}

void nes_host_init(void)
{
    printf("[nes_host] Initializing NES host\n");
    PIO pio = pio0;
    uint sm = pio_claim_unused_sm(pio, true);
    if(sm < 0) {
        printf("[nes_host] Error claiming State Machine\n");
    } else {
        printf("[nes_host] State Machine claimed\n");
    }

    uint offset = pio_add_program(pio, &nes_host_program);
    if(offset < 0) {
        printf("[nes_host] Error adding PIO program\n");
    } else {
        printf("[nes_host] PIO Program added\n");
    }

    nes_sm_init(pio, sm, offset);

    static tick_ctx_t ctx;
    ctx.pio = pio;
    ctx.sm = sm;
    ctx.irq_flag = 0;
    ctx.prev_buttons = 0xFF;
    enable_fifo_irq(&ctx);

    bool ok = add_repeating_timer_us(-(PERIOD_US_INT), nes_timer_cb, &ctx, &nes_timer);
    if(!ok) {
        printf("[nes_host] No timer slots available. Initialization failed\n");
    } else {
        printf("[nes_host] Repeating Timer initialized\n");
    }
}

void nes_host_task(void)
{
    uint8_t b = s_ctx->prev_buttons;
    uint32_t buttons = 0;

    if (b & (1 << NES_BTN_INDEX_B))      buttons |= JP_BUTTON_B1;
    if (b & (1 << NES_BTN_INDEX_A))      buttons |= JP_BUTTON_B2;
    if (b & (1 << NES_BTN_INDEX_SELECT)) buttons |= JP_BUTTON_S1;
    if (b & (1 << NES_BTN_INDEX_START))  buttons |= JP_BUTTON_S2;
    if (b & (1 << NES_BTN_INDEX_UP))     buttons |= JP_BUTTON_DU;
    if (b & (1 << NES_BTN_INDEX_DOWN))   buttons |= JP_BUTTON_DD;
    if (b & (1 << NES_BTN_INDEX_LEFT))   buttons |= JP_BUTTON_DL;
    if (b & (1 << NES_BTN_INDEX_RIGHT))  buttons |= JP_BUTTON_DR;

    input_event_t event;
    init_input_event(&event);

    int port = 0; 

    event.dev_addr = 0xF0 + port; // port number // TODO : Check the address number here
    event.instance = 0; // Instance number for multi controller devices
    event.type = INPUT_TYPE_GAMEPAD;
    event.transport = INPUT_TRANSPORT_NATIVE;
    event.layout = LAYOUT_UNKNOWN;
    event.buttons = buttons;
    event.analog[ANALOG_LX] = 128;
    event.analog[ANALOG_LY] = 128;
    event.analog[ANALOG_RX] = 128;
    event.analog[ANALOG_RY] = 128;

    router_submit_input(&event);
}

bool nes_host_is_connected(void)
{
    // TODO : Implement connection and disconnection logic and report acordingly
    return true;
}

// ============================================================================
// INPUT INTERFACE (for app declaration)
// ============================================================================

static uint8_t nes_get_device_count(void) {
    // TODO : implement connect and disconnect tracking of device(s)
    return NES_MAX_PORTS;
}

const InputInterface nes_input_interface = {
    .name = "NES",
    .source = INPUT_SOURCE_NATIVE_NES,
    .init = nes_host_init,
    .task = nes_host_task,
    .is_connected = nes_host_is_connected,
    .get_device_count = nes_get_device_count
};