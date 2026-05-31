// psx_host.c - Native PS1 / PS2 controller host driver (PIO + DMA)
//
// The PSX SIO transaction is clocked entirely in hardware by a PIO state machine
// (see psx.pio) and read by a DMA channel, so the CPU never bit-bangs and never
// blocks — the input task just checks whether the latest transaction finished,
// parses it, and kicks off the next one. This keeps USB output smooth (an
// earlier CPU bit-bang blocked the main loop ~ms per poll -> very laggy).
//
// Poll response (0x01 0x42 0x00 ...):
//   buf[0]: header           buf[1]: ID (0x41 digital / 0x73 analog / 0x79 DS2)
//   buf[2]: 0x5A ready       buf[3]: buttons low   buf[4]: buttons high
//   buf[5..8] (analog only):  right X, right Y, left X, left Y

#include "psx_host.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "core/services/players/feedback.h"
#include "core/services/button/button.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "psx.pio.h"
#include <string.h>

// ============================================================================
// CONFIG
// ============================================================================

#define PSX_DEV_ADDR     0xE1
#define PSX_HW_BYTES     21         // full transaction: 3 cmd + 18 data (covers
                                    // DS2 0x79 pressure block: id,5A,btn1,btn2,
                                    // RX,RY,LX,LY + 12 pressure bytes)

#define PSX_ID_DIGITAL   0x41
#define PSX_ID_ANALOG    0x73
#define PSX_ID_PRESSURE  0x79
#define PSX_ID_NEGCON    0x23   // Namco neGcon: twist + analog I/II/L
#define PSX_ID_FLIGHT    0x53   // Analog Joystick / Dual Analog "flight" mode: analog, no Select/L3/R3
#define PSX_ID_GUNCON    0x63   // Namco GunCon light gun: buttons + 16-bit screen X/Y
#define PSX_ID_JOGCON    0xE3   // Namco JogCon: paddle wheel + force-feedback motor
#define PSX_ID_MOUSE     0x12   // PlayStation Mouse (SCPH-1090): 2 buttons + dx/dy

// GunCon screen coordinates -> 0-255 right-stick aim. Approximate NTSC visible
// range (HSYNC dots / VSYNC scanlines); exact values depend on the CRT and the
// game's video timing, so these are the calibration knobs if aim is off.
#define GUNCON_X_MIN  0x004D
#define GUNCON_X_MAX  0x01CD
#define GUNCON_Y_MIN  0x0020
#define GUNCON_Y_MAX  0x0108
#define PSX_ID_CONFIG    0xF3   // pad is in config/escape mode (not a real report)
#define NEGCON_BTN_THRESH 0x40  // I/II/L analog press level that latches a button

// ============================================================================
// STATE
// ============================================================================

static uint8_t pin_cmd = PSX_PIN_CMD;
static uint8_t pin_clk = PSX_PIN_CLK;
static uint8_t pin_att = PSX_PIN_ATT;
static uint8_t pin_dat = PSX_PIN_DAT;

static PIO       psx_pio = pio0;
static uint      psx_sm;
static int       psx_dma = -1;
static uint32_t  psx_rx[PSX_HW_BYTES];   // DMA target: one byte per word (bits 31-24)

// Filled by the DMA-completion ISR, consumed by the task.
static volatile uint8_t  psx_snap[PSX_HW_BYTES];
static volatile bool     psx_new;
static volatile bool     psx_busy;          // a transaction is in flight
static absolute_time_t   psx_next_poll;     // when the next poll may start
static absolute_time_t   psx_next_config;   // throttle analog-enable retries
static absolute_time_t   psx_settle_until;  // hold off config until power stabilizes
static absolute_time_t   psx_a1_hold_until;  // hold A1 after an ANALOG-button toggle
static uint8_t           config_attempts;   // capped so non-DS2 pads stop retrying

// The bit-bang config (esp. set_bytes_large) is flaky and often needs many
// tries before the DS2 latches into 0x79 pressure mode, so retry persistently
// (~5s) before giving up on a non-pressure pad.
#define PSX_MAX_CONFIG_ATTEMPTS   50
#define PSX_CONFIG_INTERVAL_US    100000
// A freshly hot-swapped pad browns out while its capacitors charge; configuring
// during that window leaves it in a state that rejects set_bytes_large and won't
// recover. Wait for power to settle after a pad first appears before configuring.
#define PSX_CONFIG_SETTLE_US      300000

// Real consoles poll the pad once per frame and leave ATT high (deselected) for
// milliseconds between polls. Hammering back-to-back (only ~32 us recovery) makes
// the controller return stale/garbage analog -> choppy. Pace polls like the
// reference (PS2X read_delay): ~3 ms gives ~1.7 ms of recovery per poll.
// The 21-byte transaction takes ~2.8ms at our clock; 4.5ms pacing leaves ~1.7ms
// of ATT-high recovery between polls (~222 Hz), the gap that keeps reads clean.
#define PSX_POLL_INTERVAL_US  4500

static bool      initialized = false;
static bool      connected   = false;
static uint8_t   last_id     = 0xFF;   // last raw id (incl. 0xF3 config mode)
static uint8_t   last_real_id = 0xFF;  // last decodable id (ignores 0xF3 flicker)

// state-change suppression
static uint32_t  last_buttons = 0;
static uint8_t   last_lx = 128, last_ly = 128, last_rx = 128, last_ry = 128;
static bool      last_submitted = false;


// ============================================================================
// TRANSACTION
// ============================================================================

// DualShock rumble motor levels sent in each poll's command bytes (small = on/off,
// large = PWM). Only take effect after enable_rumble (0x4D) maps them; set from the
// output's feedback state in psx_host_task.
static uint8_t psx_rumble_small = 0;   // command byte 2: small motor, 0 off / nonzero on
static uint8_t psx_rumble_large = 0;   // command byte 3: large motor, 0-255 PWM

// Arm the DMA for one transaction and trigger the PIO. Non-blocking.
// Always reads the full 21-byte frame (4 command + 17 data). At 500 kHz the
// inter-poll recovery gap dominates the rate, so a shorter read wouldn't help.
static void psx_start_xfer(void) {
    psx_busy = true;
    dma_channel_set_write_addr(psx_dma, psx_rx, false);
    dma_channel_set_trans_count(psx_dma, PSX_HW_BYTES, true);   // arm + start
    // Command word, shifted LSB-first by the PIO: byte0=0x01, byte1=0x42,
    // byte2=small motor, byte3=large motor (the rumble control bytes).
    pio_sm_put(psx_pio, psx_sm,
               0x01u | (0x42u << 8) |
               ((uint32_t)psx_rumble_small << 16) | ((uint32_t)psx_rumble_large << 24));
}

// ---------------------------------------------------------------------------
// Analog enable/lock (bit-banged config sequence)
// ---------------------------------------------------------------------------
// PS1/PS2 pads power up in digital mode (ID 0x41) until the host puts them in
// analog mode. Send the standard PS2X config sequence:
//   enter_config -> set_mode(analog=0x01, lock=0x03) -> exit_config
// The lock byte (0x03) stops the controller's ANALOG button from toggling it
// back to digital. Config is rare (connect / mode-drop), so a blocking bit-bang
// is fine. It runs only when the poll PIO is parked (psx_busy == false), so we
// just borrow the pins (PIO -> SIO -> PIO) without tearing the SM down.

// Config is bit-banged slowly: there is no ACK wire, so the inter-byte gap must
// be long enough for the controller to keep up across a whole command. A fast
// gap let early commands (set_mode, whose analog bit is byte 3) through but
// dropped mid-command bytes of set_bytes_large (its pressure bitmask is bytes
// 3-5), so DS2 pressure mode never engaged. ~10us/phase + ~120us inter-byte.
static uint8_t psx_bb_byte(uint8_t out) {
    uint8_t in = 0;
    for (int i = 0; i < 8; i++) {
        gpio_put(pin_cmd, (out >> i) & 1);          // CMD bit, LSB first
        busy_wait_us(8);                             // CMD setup BEFORE falling edge
        gpio_put(pin_clk, 0);                        // CLK falling edge (pad latches CMD)
        busy_wait_us(14);                            // let DAT settle
        if (gpio_get(pin_dat)) in |= (1u << i);      // sample DAT
        gpio_put(pin_clk, 1);                         // CLK rising edge
        busy_wait_us(14);
    }
    gpio_put(pin_cmd, 1);
    busy_wait_us(250);                               // inter-byte ACK recovery
    return in;
}

static void psx_bb_cmd(const uint8_t* cmd, int len) {
    gpio_put(pin_att, 0);                            // select
    busy_wait_us(14);
    for (int i = 0; i < len; i++) psx_bb_byte(cmd[i]);
    gpio_put(pin_att, 1);                            // deselect
    busy_wait_us(600);                               // inter-command recovery (DS2 needs time)
}

static void psx_send_config(void) {
    static const uint8_t enter_config[]    = {0x01, 0x43, 0x00, 0x01, 0x00};
    static const uint8_t type_read[]       = {0x01, 0x45, 0x00, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A};
    // byte4 = lock: 0x00 (unlocked) not 0x03. Locking lets a failed first config
    // (e.g. mid-boot on hot-swap) latch the pad in 0x73 and reject re-config, so
    // it gets stuck without pressure. Unlocked, a later retry can still upgrade it.
    static const uint8_t set_mode[]        = {0x01, 0x44, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
    // Map poll command byte 2 -> small motor (0x00), byte 3 -> large motor (0x01),
    // so the rumble bytes in psx_start_xfer's command word actually drive the motors.
    static const uint8_t enable_rumble[]   = {0x01, 0x4D, 0x00, 0x00, 0x01};
    static const uint8_t set_bytes_large[] = {0x01, 0x4F, 0x00, 0xFF, 0xFF, 0x03, 0x00, 0x00, 0x00};
    static const uint8_t exit_config[]     = {0x01, 0x43, 0x00, 0x00, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A};

    // Borrow the pins from the (parked) PIO. CRITICAL: pre-load the SIO output
    // registers to the idle state (CLK/CMD/ATT high, DAT input) BEFORE switching
    // the pin mux to SIO. If we switch the mux first, the pins momentarily drive
    // stale/low SIO values -> a spurious CLK/ATT edge that desyncs the controller
    // and corrupts the config (the cause of the flaky DS2 pressure-enable).
    gpio_put(pin_clk, 1);
    gpio_put(pin_cmd, 1);
    gpio_put(pin_att, 1);
    gpio_set_dir(pin_clk, true);
    gpio_set_dir(pin_cmd, true);
    gpio_set_dir(pin_att, true);
    gpio_set_dir(pin_dat, false);
    gpio_set_function(pin_clk, GPIO_FUNC_SIO);
    gpio_set_function(pin_cmd, GPIO_FUNC_SIO);
    gpio_set_function(pin_att, GPIO_FUNC_SIO);
    gpio_set_function(pin_dat, GPIO_FUNC_SIO);
    busy_wait_us(100);

    psx_bb_cmd(enter_config,    sizeof(enter_config));
    psx_bb_cmd(type_read,       sizeof(type_read));       // read type (primes the pad)
    psx_bb_cmd(set_mode,        sizeof(set_mode));        // analog + lock
    psx_bb_cmd(enable_rumble,   sizeof(enable_rumble));   // map the two motor bytes
    // Send the DS2 full-pressure command several times — the mask bytes are the
    // flaky part, so repeating raises the odds all bytes land in one config pass.
    psx_bb_cmd(set_bytes_large, sizeof(set_bytes_large));
    psx_bb_cmd(set_bytes_large, sizeof(set_bytes_large));
    psx_bb_cmd(set_bytes_large, sizeof(set_bytes_large));
    // Exit twice: a flaky single exit can leave the pad in config mode (0xF3),
    // which a DS1 would otherwise stay stuck in once the retry cap is reached.
    psx_bb_cmd(exit_config,     sizeof(exit_config));
    psx_bb_cmd(exit_config,     sizeof(exit_config));

    // Hand the pins back to the PIO (still parked at `pull`).
    pio_gpio_init(psx_pio, pin_clk);
    pio_gpio_init(psx_pio, pin_att);
    pio_gpio_init(psx_pio, pin_cmd);
    pio_gpio_init(psx_pio, pin_dat);
    pio_sm_set_consecutive_pindirs(psx_pio, psx_sm, pin_clk, 2, true);
    pio_sm_set_consecutive_pindirs(psx_pio, psx_sm, pin_cmd, 1, true);
    pio_sm_set_consecutive_pindirs(psx_pio, psx_sm, pin_dat, 1, false);
}

// DMA-completion ISR: snapshot the bytes and mark the transaction done. The
// task consumes psx_snap and PACES the next poll (recovery gap), so we do NOT
// re-fire here — back-to-back polling overwhelms the controller.
static void __isr psx_dma_isr(void) {
    if (!(dma_hw->ints0 & (1u << psx_dma))) return;   // not our channel
    dma_hw->ints0 = 1u << psx_dma;                      // clear

    for (int i = 0; i < PSX_HW_BYTES; i++) psx_snap[i] = (uint8_t)(psx_rx[i] >> 24);
    psx_new = true;
    psx_busy = false;   // transaction complete; task schedules the next one
}

// Map the controller ID byte to a layout (drives the web-config name + SInput
// face style). Unknown IDs fall back to a generic Sony-style 4-face pad.
static controller_layout_t psx_layout_for_id(uint8_t id) {
    switch (id) {
        case PSX_ID_DIGITAL:  return LAYOUT_PSX_DIGITAL;
        case PSX_ID_ANALOG:   return LAYOUT_PSX_DUALSHOCK;
        case PSX_ID_FLIGHT:   return LAYOUT_PSX_FLIGHTSTICK;
        case PSX_ID_GUNCON:   return LAYOUT_PSX_GUNCON;
        case PSX_ID_JOGCON:   return LAYOUT_PSX_JOGCON;
        case PSX_ID_PRESSURE: return LAYOUT_PSX_DUALSHOCK2;
        case PSX_ID_NEGCON:   return LAYOUT_PSX_NEGCON;
        default:              return LAYOUT_PSX_DUALSHOCK;
    }
}

// Scale a GunCon screen coordinate (lo..hi) to a 0-255 analog-stick value.
static uint8_t guncon_scale(uint16_t v, uint16_t lo, uint16_t hi) {
    if (v <= lo) return 0;
    if (v >= hi) return 255;
    return (uint8_t)((uint32_t)(v - lo) * 255u / (hi - lo));
}

// Decode PS1/PS2 button bytes (active-low: 0 = pressed) into JP_BUTTON_* bitmap.
static uint32_t decode_buttons(uint8_t lo, uint8_t hi) {
    uint32_t b = 0;
    if (!(lo & 0x01)) b |= JP_BUTTON_S1;   // SELECT
    if (!(lo & 0x02)) b |= JP_BUTTON_L3;
    if (!(lo & 0x04)) b |= JP_BUTTON_R3;
    if (!(lo & 0x08)) b |= JP_BUTTON_S2;   // START
    if (!(lo & 0x10)) b |= JP_BUTTON_DU;
    if (!(lo & 0x20)) b |= JP_BUTTON_DR;
    if (!(lo & 0x40)) b |= JP_BUTTON_DD;
    if (!(lo & 0x80)) b |= JP_BUTTON_DL;
    if (!(hi & 0x01)) b |= JP_BUTTON_L2;
    if (!(hi & 0x02)) b |= JP_BUTTON_R2;
    if (!(hi & 0x04)) b |= JP_BUTTON_L1;
    if (!(hi & 0x08)) b |= JP_BUTTON_R1;
    if (!(hi & 0x10)) b |= JP_BUTTON_B4;   // Triangle
    if (!(hi & 0x20)) b |= JP_BUTTON_B2;   // Circle
    if (!(hi & 0x40)) b |= JP_BUTTON_B1;   // Cross
    if (!(hi & 0x80)) b |= JP_BUTTON_B3;   // Square
    return b;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void psx_host_init(void) {
    psx_host_init_pins(PSX_PIN_CMD, PSX_PIN_CLK, PSX_PIN_ATT, PSX_PIN_DAT);
}

void psx_host_init_pins(uint8_t cmd, uint8_t clk, uint8_t att, uint8_t dat) {
    if (initialized) return;
    pin_cmd = cmd; pin_clk = clk; pin_att = att; pin_dat = dat;

    // PIO pin groups: side-set = {CLK, ATT} (consecutive, base=CLK); set = DAT
    // alone (the PIO drives it high then releases each bit for the active pull-up);
    // out = CMD; in = DAT. CLK/ATT on side-set so DAT can own the set group even
    // when CMD sits between ATT and DAT in the GPIO map (e.g. QT Py 26/27/28/29).
    uint offset = pio_add_program(psx_pio, &psx_program);
    psx_sm = pio_claim_unused_sm(psx_pio, true);
    pio_sm_config c = psx_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, pin_clk);        // side-set = CLK, ATT (2 bits)
    sm_config_set_set_pins(&c, pin_dat, 1);         // set = DAT (driven each bit)
    sm_config_set_out_pins(&c, pin_cmd, 1);
    sm_config_set_in_pins(&c, pin_dat);
    sm_config_set_out_shift(&c, true, false, 32);   // shift right (LSB first), manual pull
    sm_config_set_in_shift(&c, true, true, 8);      // shift right, autopush every byte

    pio_gpio_init(psx_pio, pin_clk);
    pio_gpio_init(psx_pio, pin_att);
    pio_gpio_init(psx_pio, pin_cmd);
    pio_gpio_init(psx_pio, pin_dat);
    gpio_pull_up(pin_dat);
    // Weak (2mA) drive on DAT: enough to charge the line on a released '1' bit, but
    // too weak to override the controller's pull-down on a '0' bit -> safe active
    // pull-up with only ~2mA of brief contention. See psx.pio.
    gpio_set_drive_strength(pin_dat, GPIO_DRIVE_STRENGTH_2MA);

    pio_sm_set_consecutive_pindirs(psx_pio, psx_sm, pin_clk, 2, true);   // CLK, ATT out
    pio_sm_set_consecutive_pindirs(psx_pio, psx_sm, pin_cmd, 1, true);   // CMD out
    pio_sm_set_consecutive_pindirs(psx_pio, psx_sm, pin_dat, 1, false);  // DAT in (PIO drives per-bit)

    // 500 kHz instruction rate. A fast clock alone corrupts old analog pads (their
    // DAT can't rise in time at the 0x00/0xFF extremes on the weak internal
    // pull-up), but the PIO's active pull-up (driving DAT high each bit) fixes the
    // rise, so we keep the fast clock for responsive DS2 pressure / NegCon AND read
    // the SCPH-110 cleanly. ~2.8 ms per 21-byte poll; 4.5 ms pacing leaves ~1.7 ms
    // of ATT-high recovery. Non-blocking.
    sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 500000.0f);

    pio_sm_init(psx_pio, psx_sm, offset, &c);
    pio_sm_set_enabled(psx_pio, psx_sm, true);

    // DMA: drain the PIO RX FIFO into psx_rx, paced by the RX DREQ.
    psx_dma = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(psx_dma);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_dreq(&dc, pio_get_dreq(psx_pio, psx_sm, false));
    dma_channel_configure(psx_dma, &dc, psx_rx, &psx_pio->rxf[psx_sm], PSX_HW_BYTES, false);

    // Fire the ISR on each completed transaction so the next one starts with no
    // main-loop gap. Shared handler so it coexists with other DMA_IRQ_0 users.
    dma_channel_set_irq0_enabled(psx_dma, true);
    irq_add_shared_handler(DMA_IRQ_0, psx_dma_isr,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(DMA_IRQ_0, true);

    initialized = true;
    psx_next_poll = get_absolute_time();   // task fires the first paced poll
}

static void psx_process(const uint8_t* buf);

void psx_host_task(void) {
    if (!initialized) return;

    // 0) Pull the latest rumble from the output's feedback state into the motor
    //    bytes the next poll sends. Heavy/low motor -> large (PWM), light/high
    //    motor -> small (on/off). A JogCon is excluded: it drives its own motor
    //    bytes for the wheel recenter (set in psx_process), not host rumble.
    if (last_real_id != PSX_ID_JOGCON) {
        feedback_state_t* fb = feedback_get_state(0);
        if (fb) {
            psx_rumble_large = fb->rumble.left;
            psx_rumble_small = (fb->rumble.right > 0) ? 0xFF : 0x00;
        }
    }

    // 1) Consume the latest completed transaction (if any).
    if (psx_new) {
        psx_new = false;
        uint8_t buf[PSX_HW_BYTES];
        for (int i = 0; i < PSX_HW_BYTES; i++) buf[i] = psx_snap[i];
        psx_process(buf);
    }

    // 2) Configure DualShock-family pads: enable analog + lock + DS2 pressure.
    //    Runs while the pad is digital (0x41) or plain-analog (0x73) and not yet
    //    in pressure mode (0x79). Capped so a DS1 / analog-only pad (which can't
    //    reach 0x79) doesn't reconfigure forever. NegCon (0x23) is left alone.
    //    Safe to bit-bang here: !psx_busy means the PIO is parked.
    if (connected && config_attempts < PSX_MAX_CONFIG_ATTEMPTS &&
        (last_id == PSX_ID_DIGITAL || last_id == PSX_ID_ANALOG || last_id == PSX_ID_CONFIG) &&
        !psx_busy && time_reached(psx_settle_until) && time_reached(psx_next_config)) {
        psx_next_config = make_timeout_time_us(PSX_CONFIG_INTERVAL_US);
        config_attempts++;
        psx_send_config();
    }

    // 3) Pace the next poll: re-fire only after the recovery interval and when no
    //    transaction is in flight, giving the controller console-like recovery
    //    (ATT stays high in between) instead of being hammered back-to-back.
    if (!psx_busy && time_reached(psx_next_poll)) {
        psx_next_poll = make_timeout_time_us(PSX_POLL_INTERVAL_US);
        psx_start_xfer();
    }
}

// Decode one completed transaction and submit a router event on state change.
static void psx_process(const uint8_t* buf) {
    uint8_t id = buf[1];
    uint8_t prev_real = last_real_id;   // for ANALOG-button toggle detection
    // Decode only genuine controller IDs. A config-mode response (0xF3, seen when a
    // flaky exit_config briefly leaves the pad in config mode) is "present" but must
    // NOT reach decode_buttons — it produces phantom multi-button presses. Instead
    // keep it connected so config keeps firing and exits it. This bit a DS1, which
    // never reaches 0x79 so it re-configs continuously, unlike a DS2 that latches
    // 0x79 and stops; a DS1 left stuck in config mode would otherwise read as gone.
    bool decodable = (id == PSX_ID_DIGITAL || id == PSX_ID_ANALOG ||
                      id == PSX_ID_FLIGHT || id == PSX_ID_PRESSURE ||
                      id == PSX_ID_NEGCON || id == PSX_ID_GUNCON ||
                      id == PSX_ID_JOGCON);
    bool is_mouse = (id == PSX_ID_MOUSE);   // PlayStation Mouse: relative pointer
    bool present = decodable || is_mouse || id == PSX_ID_CONFIG;

    if (present) {
        connected = true;
        last_id = id;   // includes 0xF3 so the config trigger can exit config mode
        // Reset the retry cap/settle only for a genuinely new pad or a real id
        // change — NOT the transient 0x73<->0xF3 flicker our own config churn
        // causes. Resetting on that flicker keeps config_attempts from ever
        // reaching the cap, so an analog-only pad (DS1 / SCPH-110, which can't
        // reach 0x79) configures forever and starves the stick polls.
        if (decodable && id != last_real_id) {
            last_real_id = id;
            config_attempts = 0;
            psx_settle_until = make_timeout_time_us(PSX_CONFIG_SETTLE_US);
        }
    }

    // ANALOG-button -> A1 (Guide/PS). DS1/DS2 don't report the ANALOG button as a
    // bit; it toggles digital<->analog. Since config keeps the pad in analog, a pad
    // that was analog suddenly reporting digital (0x41) means ANALOG was pressed (a
    // real digital-only pad is never analog first). Hold A1 briefly so a console's
    // PS-button init registers while config flips it back to analog.
    if (id == PSX_ID_DIGITAL &&
        (prev_real == PSX_ID_ANALOG || prev_real == PSX_ID_PRESSURE)) {
        psx_a1_hold_until = make_timeout_time_us(200000);   // ~200 ms
    }

    if (decodable) {
        uint32_t buttons = decode_buttons(buf[3], buf[4]);
        uint8_t rx = 128, ry = 128, lx = 128, ly = 128;
        uint8_t pressure[12] = {0};   // up,right,down,left,l2,r2,l1,r1,tri,cir,cross,sq
        bool has_pressure = false;
        if (last_id == PSX_ID_NEGCON) {
            // neGcon: only the twist is a real axis -> left stick X (steering).
            // I, II, L are analog/pressure buttons: latch them past a threshold
            // for digital output (SInput) AND pass the analog level as DS3-style
            // pressure (PS3 output transmits it; SInput ignores it).
            // A/B/R/Start/d-pad already decode from buf[3]/buf[4].
            lx = buf[5];                                        // twist
            if (buf[6] > NEGCON_BTN_THRESH) buttons |= JP_BUTTON_B1;  // I  -> Cross
            if (buf[7] > NEGCON_BTN_THRESH) buttons |= JP_BUTTON_B3;  // II -> Square
            if (buf[8] > NEGCON_BTN_THRESH) buttons |= JP_BUTTON_L1;  // L  -> L1
            // DS3 has a pressure byte per face/d-pad button. I/II/L are analog;
            // the rest are digital, so report full-on-press for them.
            has_pressure = true;
            if (buttons & JP_BUTTON_DU) pressure[0] = 0xFF;
            if (buttons & JP_BUTTON_DR) pressure[1] = 0xFF;
            if (buttons & JP_BUTTON_DD) pressure[2] = 0xFF;
            if (buttons & JP_BUTTON_DL) pressure[3] = 0xFF;
            if (buttons & JP_BUTTON_R1) pressure[7] = 0xFF;   // R
            if (buttons & JP_BUTTON_B4) pressure[8] = 0xFF;   // B -> Triangle
            if (buttons & JP_BUTTON_B2) pressure[9] = 0xFF;   // A -> Circle
            pressure[10] = buf[6];   // I  -> Cross  (analog)
            pressure[11] = buf[7];   // II -> Square (analog)
            pressure[6]  = buf[8];   // L  -> L1     (analog)
        } else if (last_id == PSX_ID_ANALOG || last_id == PSX_ID_FLIGHT ||
                   last_id == PSX_ID_PRESSURE) {
            rx = buf[5]; ry = buf[6]; lx = buf[7]; ly = buf[8];
            if (last_id == PSX_ID_PRESSURE) {
                // DualShock 2 full mode: 12 pressure bytes follow the sticks.
                // DS2 wire order (buf[9..20]):
                //   Right,Left,Up,Down, Tri,Cir,Cross,Sq, L1,R1,L2,R2
                // event->pressure order:
                //   up,right,down,left, l2,r2,l1,r1, tri,cir,cross,sq
                has_pressure = true;
                pressure[0]  = buf[11];  // up
                pressure[1]  = buf[9];   // right
                pressure[2]  = buf[12];  // down
                pressure[3]  = buf[10];  // left
                pressure[4]  = buf[19];  // l2
                pressure[5]  = buf[20];  // r2
                pressure[6]  = buf[17];  // l1
                pressure[7]  = buf[18];  // r1
                pressure[8]  = buf[13];  // triangle
                pressure[9]  = buf[14];  // circle
                pressure[10] = buf[15];  // cross
                pressure[11] = buf[16];  // square
            }
        } else if (last_id == PSX_ID_GUNCON) {
            // GunCon light gun: same reply shape as DualShock. Buttons already
            // decoded (A->Start, B->Cross, Trigger->Circle). The analog bytes are
            // 16-bit screen coordinates: X=HSYNC dots, Y=VSYNC scanlines. Map the
            // aim to the right stick (RX/RY). Off-screen / no light is signalled by
            // X==0x0001 (Y 0x05 unexpected light, 0x0A no light) -> center the aim.
            uint16_t gx = ((uint16_t)buf[6] << 8) | buf[5];
            uint16_t gy = ((uint16_t)buf[8] << 8) | buf[7];
            if (gx == 0x0001) {
                rx = 128; ry = 128;   // off-screen: neutral aim
            } else {
                rx = guncon_scale(gx, GUNCON_X_MIN, GUNCON_X_MAX);
                ry = guncon_scale(gy, GUNCON_Y_MIN, GUNCON_Y_MAX);
            }
        } else if (last_id == PSX_ID_JOGCON) {
            // JogCon paddle wheel -> left-stick X. buf[5] is the wheel offset from
            // its power-on center: 0x00 center, 0x01..0x7F clockwise (right),
            // 0x80..0xFF counter-clockwise (left); buf[6] counts full rotations.
            // Cap at half a turn each way (per PsxNewLib), then center at 128.
            uint8_t w = buf[5];
            uint8_t pos;
            if (buf[6] < 0x80) pos = (w < 0x80) ? w : 0x7F;   // CW, cap right
            else               pos = (w > 0x80) ? w : 0x81;   // CCW, cap left
            lx = (uint8_t)(pos + 0x80);

            // Recenter force-feedback (EXPERIMENTAL — JogCon FF protocol is
            // undocumented). Drive the motor opposite the wheel offset, ramped by
            // distance, through the rumble motor bytes (small=CW, large=CCW guess).
            uint8_t mag = (w == 0) ? 0 : (w < 0x80 ? w : (uint8_t)(0x100 - w));
            if (mag < 6) {
                psx_rumble_small = 0; psx_rumble_large = 0;   // deadzone near center
            } else {
                uint8_t force = (mag > 0x40) ? 0xFF : (uint8_t)(mag << 2);
                psx_rumble_small = (w >= 0x80) ? force : 0;   // wheel left  -> push CW
                psx_rumble_large = (w <  0x80) ? force : 0;   // wheel right -> push CCW
            }
        }

        // PS1/PS2 pads have no PS/Guide (Home) button, which some consoles need to
        // wake/init the controller. Map Select+Start (held together) to A1 (Guide),
        // suppressing Select/Start so the host sees a clean PS-button press.
        if ((buttons & JP_BUTTON_S1) && (buttons & JP_BUTTON_S2)) {
            buttons |= JP_BUTTON_A1;
            buttons &= ~(JP_BUTTON_S1 | JP_BUTTON_S2);
        }

        // Hold A1 for the window after an ANALOG-button toggle was detected.
        if (!time_reached(psx_a1_hold_until)) buttons |= JP_BUTTON_A1;

        // Board's user button (BOOTSEL on QT Py/KB2040, GPIO 7 on Feather etc.)
        // -> A1 (Guide/PS) while held. Lets the adapter trigger a Home/PS button
        // press without a Select+Start or ANALOG-button toggle. Double/triple
        // clicks still cycle the USB output mode through the button service.
        if (button_is_pressed()) buttons |= JP_BUTTON_A1;

        // Pressure controllers (neGcon) stream every poll so analog pressure
        // updates continuously, not just when a button bit toggles.
        bool changed = !last_submitted || has_pressure ||
            buttons != last_buttons ||
            lx != last_lx || ly != last_ly || rx != last_rx || ry != last_ry;
        if (changed) {
            last_buttons = buttons;
            last_lx = lx; last_ly = ly; last_rx = rx; last_ry = ry;
            last_submitted = true;

            input_event_t e;
            init_input_event(&e);
            e.dev_addr  = PSX_DEV_ADDR;
            e.instance  = 0;
            e.type      = INPUT_TYPE_GAMEPAD;
            e.transport = INPUT_TRANSPORT_NATIVE;
            e.layout    = psx_layout_for_id(last_id);
            e.buttons   = buttons;
            e.analog[ANALOG_LX] = lx; e.analog[ANALOG_LY] = ly;
            e.analog[ANALOG_RX] = rx; e.analog[ANALOG_RY] = ry;
            if (has_pressure) {
                e.has_pressure = true;
                for (int i = 0; i < 12; i++) e.pressure[i] = pressure[i];
            }
            router_submit_input(&e);
        }
    } else if (is_mouse) {
        // PlayStation Mouse (0x12): buf[4] buttons (bit3=Left, bit2=Right, active
        // low), buf[5]=dx, buf[6]=dy (signed). Emit a relative-mouse event; only
        // submit on movement or a button change (relative pointer).
        uint32_t mb = 0;
        if (!(buf[4] & 0x08)) mb |= JP_BUTTON_B1;   // Left  -> mouse button 1
        if (!(buf[4] & 0x04)) mb |= JP_BUTTON_B2;   // Right -> mouse button 2
        int8_t mdx = (int8_t)buf[5];
        int8_t mdy = (int8_t)buf[6];
        if (mb != last_buttons || mdx != 0 || mdy != 0) {
            last_buttons = mb;
            last_submitted = true;

            input_event_t e;
            init_input_event(&e);
            e.dev_addr  = PSX_DEV_ADDR;
            e.instance  = 0;
            e.type      = INPUT_TYPE_MOUSE;
            e.transport = INPUT_TRANSPORT_NATIVE;
            e.layout    = LAYOUT_PSX_MOUSE;
            e.buttons   = mb;
            e.delta_x   = mdx;
            e.delta_y   = mdy;
            router_submit_input(&e);
        }
    } else if ((id == 0xFF || id == 0x00) && connected) {
        // Genuine "no controller": the data line idles high (0xFF) or low (0x00).
        // Any other non-known byte is a transient (e.g. 0xF3 config mode) — leave it
        // and last_id alone so config keeps running and we stay connected.
        connected = false;
        last_id = 0xFF;
        last_real_id = 0xFF;
        config_attempts = 0;   // reconfigure on the next controller
    }
}

bool psx_host_is_connected(void) {
    return connected;
}


// ============================================================================
// INPUT INTERFACE
// ============================================================================

static uint8_t psx_get_device_count(void) {
    return connected ? 1 : 0;
}

const InputInterface psx_input_interface = {
    .name = "PS1/PS2",
    .source = INPUT_SOURCE_NATIVE_PSX,
    .init = psx_host_init,
    .task = psx_host_task,
    .is_connected = psx_host_is_connected,
    .get_device_count = psx_get_device_count,
};
