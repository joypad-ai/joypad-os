// rf24g_host.c - SF30 2.4G link state machine, scheduler, pairing
//
// See rf24g_host.h for the public API and sf30_protocol.h for the protocol
// this implements. This file is the third of three layers (rf24g_hal ->
// nrf24l01 -> here) so the protocol logic is testable independently of the
// chip and the chip independently of the board.
//
// ---- Why this is interrupt-driven, not polled ----------------------------
//
// core0_main() (src/main.c) is an unthrottled while(1) running LED, player,
// storage, input, output and app tasks. Any one of them stalling past a
// ~10ms dwell loses a packet -- that stall is exactly what capped the
// ESP8266 reference receiver at 9-10 pkt/s while its Wi-Fi stack ran
// alongside a polled radioPump(). So the radio here runs entirely off
// interrupts:
//
//   - nRF24 IRQ pin, falling edge (on_radio_irq): drains the RX FIFO (3
//     deep, keeps the newest valid frame), decodes it, pushes it to a
//     lock-free ring buffer, waits ACK_GUARD_US for the hardware auto-ACK to
//     clear the antenna, then retunes and re-arms the dwell alarm.
//   - A hardware alarm (on_alarm) fires when a dwell expires with nothing
//     received: advances the index, retunes anyway, counts a miss.
//
// Both handlers -- and everything they call, transitively -- are
// __not_in_flash_func. This is defense in depth, NOT fault avoidance: every
// flash write in this codebase disables core-0 interrupts for the entire
// XIP-off window, so the radio ISR can't fire mid-write regardless of where
// its code lives. This is a second, independent guarantee against that
// window ever narrowing -- if it did, a flash-resident handler pulled in
// mid-erase would hard-fault instead of working by accident. A ~45ms sector
// erase costs roughly 3-9 missed dwells at the 10070us dwell period;
// MAX_MISSES (128, ~1.29s) tolerates that without dropping the link.
//
// __not_in_flash_func places a function's OWN code in RAM, not its callees',
// so it's repeated down the whole ISR/alarm call chain (this file,
// nrf24l01.c, rf24g_hal_rp2040.c). Code NOT reachable from that chain (init,
// the public task/stats/pairing entry points) is free to live in flash.
//
// rf24g_host_task(), called from core 0, does NO radio I/O -- it only drains
// the ring buffer, builds input_event_t's and calls router_submit_input(),
// plus pairing-confirmation and per-second stats bookkeeping.

#include "rf24g_host.h"
#include "rf24g_hal.h"       // must precede sf30_protocol.h: this is where
                             // __not_in_flash_func gets pulled in (see
                             // rf24g_hal.h's header comment), and
                             // sf30_identity()/sf30_next_idx() below need it
#include "sf30_protocol.h"
#include "nrf24l01.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "platform/platform.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// TUNING CONSTANTS -- all empirically derived on real hardware against a
// real SF30 2.4G controller. Do not "fix" these to look more plausible;
// several already-plausible-looking values (10100, 10204) were measured and
// then found wrong -- see DWELL_US.
// ============================================================================

// Dwell period: the transmitter's real hop rate, not a round number. Median
// interval between ADJACENT arrivals in a 128-frame contiguous trace, spread
// 10040-10110us. A naive sum(inter-arrival)/sum(counter-delta) estimator
// looks more rigorous but is wrong -- it averages in pairs that span a
// blackout, exactly where the counter and the clock disagree (measured
// 10204us against the true 10070us). The period estimate in this file is
// seeded with this constant and then refined the same restricted way -- see
// rf24g_slot_t.period_us.
#define DWELL_US 10070

// Extra late margin on the hop deadline, beyond a dwell period. Packets
// arrive at the very end of the dwell (90% in its last eighth once retune
// timing is right), so waiting a bit past the nominal deadline catches the
// tail before giving up and advancing anyway.
#define HOP_GUARD_US 1500

// Hold off the post-receive channel change until the hardware auto-ACK has
// left the antenna. The ACK starts ~130us after the packet ends and is
// itself ~300us at 250kbps; retuning inside that window cuts it off or sends
// it on the wrong channel -- and the controller is waiting on exactly that
// ACK before it will start hopping. Measured plateau across 0-500us with a
// real nRF24L01+ (unlike the OEM dongle's slow bit-banged SPI, which never
// gets there fast enough to matter); kept at the value everything else was
// tuned around.
#define ACK_GUARD_US 500

// Consecutive missed dwells, while following a CONFIRMED slot, before
// dropping it entirely. Two full hop sequences, ~1.4s -- long enough to ride
// out a stall without abandoning a link that is still there.
#define MAX_MISSES 128

// One full sweep's worth of hops before SEARCH gives up and returns to PARK.
// About half a second, matching the original dongle's own sweep length.
#define SEARCH_HOPS 52

// Misses tolerated on an UNCONFIRMED acquisition (a contact frame that
// hasn't yet been followed by a second good frame) before giving up on it.
// Acquisition is best-effort and retries rather than being ridden all the
// way to MAX_MISSES.
#define ACQUIRE_MISSES 16

// PARK re-arms RF_CH to the same park channel on this cadence, mirroring the
// original dongle's own ~500 re-arms/sec, in case the re-arm itself is what
// keeps its receiver hot for a probe.
#define PARK_REARM_US 2000

// ============================================================================
// STATE
// ============================================================================

typedef enum {
    RF24G_LINK_PARK,
    RF24G_LINK_SEARCH,
    RF24G_LINK_LINKED,
    RF24G_LINK_PAIRING,
} rf24g_link_state_t;

#define RF24G_HIST_BUCKETS 6

typedef struct {
    // Pipe 0 is always enabled -- no "claimed" flag needed here. Purely
    // link/scheduling state; s_seen tracks whether ever heard from this boot.
    //
    // The `volatile` fields below cross the ISR/core-0 boundary (written in
    // handle_frame()/on_alarm(), read or read-and-cleared from ordinary
    // core-0 task functions), same pattern as s_seen/s_unlink_pending below.
    // The rest never leave ISR/alarm context and don't need it.
    volatile bool linked;  // has received at least one ordinary (non-probe) frame
    bool     confirmed;    // a SECOND frame arrived after the acquisition jump

    uint8_t  hop_idx;      // this slot's predicted/last-known hop index
    uint8_t  last_ctr;     // last raw byte10 (hop index at the time of RX)
    uint32_t last_rx_us;   // rf24g_hal_time_us() of the last reception
    volatile uint32_t period_us;  // measured inter-arrival period; DWELL_US until measured
    uint32_t burst_dt_us;  // accumulator: sum of dt over consecutive dc==1 pairs
    uint32_t burst_n;      // accumulator: count of the same -- see handle_frame()
    volatile uint16_t misses;  // consecutive missed dwells while this slot is followed

    // Diagnostics -- see rf24g_host_print_stats().
    volatile uint32_t pkts_total;
    volatile uint32_t pkts_window;
    uint32_t pps;
    uint16_t inter_hist[RF24G_HIST_BUCKETS];
} rf24g_slot_t;

static bool s_initialized = false;

// Both volatile: written from ISR/alarm context, read from core-0 task
// functions (rf24g_host_is_pairing(), rf24g_host_state_name(),
// rf24g_host_print_stats()).
static volatile rf24g_link_state_t s_state  = RF24G_LINK_PARK;
static volatile uint8_t            s_cur_ch = SF30_PARK_CH;
static bool               s_following  = false;  // dwell is following the
                                                   // link? false in PARK/
                                                   // SEARCH/PAIRING
static uint8_t            s_sweep_idx  = 0;    // SEARCH sweep position
static uint16_t           s_sweep_hops = 0;

// The controller's link/scheduling state.
static rf24g_slot_t s_slot;

// FNV-1a hash of the board's factory unique ID, computed once in
// rf24g_host_init_pins() -- see sf30_protocol.h's "Receiver identity". Comes
// out identical on every boot, so a controller stays paired across a reboot
// or reflash and needs re-pairing only if it moves to a different board.
static uint8_t s_receiver_id[4];

// Whether the controller has ever linked this boot -- diagnostic only, for
// rf24g_host_print_stats(); the pipe is always open regardless. ISR-side
// single writer (handle_frame()), core-0 reader.
static volatile bool s_seen = false;

static uint32_t s_stats_window_ms = 0;

// ---- RX ring buffer (ISR producer, rf24g_host_task() consumer) -----------
// Single-producer/single-consumer, power-of-two size so the index wrap is a
// mask. Decoding happens here (in the ISR) so the ring only ever carries the
// two raw button bytes -- router_submit_input() itself is never called from
// interrupt context.
#define RF24G_RING_SIZE 16   // must stay a power of 2

typedef struct {
    uint8_t raw_b2;
    uint8_t raw_b3;
} rf24g_rx_item_t;

static volatile rf24g_rx_item_t s_ring[RF24G_RING_SIZE];
static volatile uint8_t s_ring_head = 0;   // ISR writes
static volatile uint8_t s_ring_tail = 0;   // rf24g_host_task() reads

// Whether the ISR unlinked the controller and its player registration still
// needs tearing down. remove_players_by_address() is flash-resident, so the
// ISR can't call it directly (see file header); it sets this flag instead,
// and rf24g_host_task() does the actual removal on core 0.
static volatile bool s_unlink_pending = false;   // ISR sets, task clears

static void __not_in_flash_func(ring_push)(uint8_t b2, uint8_t b3)
{
    uint8_t next = (uint8_t)((s_ring_head + 1) & (RF24G_RING_SIZE - 1));
    if (next == s_ring_tail) return;   // full -- drop rather than corrupt
    s_ring[s_ring_head].raw_b2 = b2;
    s_ring[s_ring_head].raw_b3 = b3;
    s_ring_head = next;
}

// ---- Pairing rendezvous state ---------------------------------------------
// Driven from the same alarm as the link scheduler (on_alarm dispatches to
// pairing_on_alarm() while s_state == RF24G_LINK_PAIRING) and the same IRQ
// (on_radio_irq dispatches to pairing_on_rx_irq()).

#define PAIR_CYCLES         5
#define PAIR_PER_CYCLE     25
#define PAIR_SLOT_US    20000     // reply cadence
#define PAIR_LISTEN_US 500000     // listening window between TX cycles
#define PAIR_QUIET_US  250000     // no request for this long => burst is over
#define PAIR_FIRST_TX_US 346000   // first reply this long after the LAST request
#define PAIR_WAIT_US 45000000     // give up if no controller ever asks
#define PAIR_CONFIRM_US 8000000   // a claim is provisional until frames arrive
#define PAIR_POLL_US     5000     // WAIT-phase polling granularity

typedef enum { PAIR_WAIT, PAIR_TX, PAIR_RX } pair_phase_t;

static pair_phase_t s_pair_phase;
static uint8_t  s_pair_cycle;
static uint8_t  s_pair_idx;
static uint32_t s_pair_last_req_us;
static uint32_t s_pair_started_us;
static uint16_t s_pair_requests;
static uint16_t s_pair_cycle_reqs;
static uint8_t  s_pair_reply[SF30_PAIR_RSP_LEN];

// Tracks whether pairing_end() accepted a claim not yet confirmed by an
// actual frame arriving -- "controller stopped asking" isn't proof it took
// the address, so pairing_confirm_task() separately watches pkts_total for
// PAIR_CONFIRM_US. Purely diagnostic: the pipe is already open regardless,
// so there's nothing to roll back. "Never confirmed" does NOT mean "never
// taken": a controller that already held this address, or won't transmit
// until power-cycled, stays silent here yet links on its next power-on.
//
// Deadline uses rf24g_hal_time_us(), not platform_time_ms() (flash-resident,
// unsafe from on_alarm() -- see file header).
static volatile bool     s_pair_prov_active = false;
static volatile uint32_t s_pair_prov_pkts = 0;
static volatile uint32_t s_pair_prov_deadline_us = 0;

// pairing_end() runs in ISR/alarm context and must not call printf itself
// (printf is flash-resident, like everything else that chain avoids -- see
// the file header). It leaves the outcome here instead; rf24g_host_task()
// (ordinary core-0 context) reports it on its next call.
typedef enum { PAIR_RESULT_NONE, PAIR_RESULT_ACCEPTED, PAIR_RESULT_REJECTED } pair_result_t;
static volatile pair_result_t s_pair_result = PAIR_RESULT_NONE;

// ============================================================================
// FORWARD DECLARATIONS (the ISR/alarm call graph is mutually recursive)
// ============================================================================
static void __not_in_flash_func(on_radio_irq)(void);
static void __not_in_flash_func(on_alarm)(void);
static void __not_in_flash_func(enter_park)(void);
static void __not_in_flash_func(enter_search)(void);
static void __not_in_flash_func(schedule_next)(void);
static void __not_in_flash_func(pairing_on_rx_irq)(void);
static void __not_in_flash_func(pairing_on_alarm)(void);
static void __not_in_flash_func(pairing_listen)(void);
static void __not_in_flash_func(pairing_switch_tx)(void);
static void __not_in_flash_func(pairing_send_reply)(void);
static void __not_in_flash_func(pairing_end)(bool claim);
static void __not_in_flash_func(apply_pipe_addresses)(void);
static void __not_in_flash_func(drop_slot_link)(void);

// ============================================================================
// PIPE ADDRESSING
// ============================================================================

#define RF24G_PIPE_MASK 0x01   // pipe 0 only -- see the EN_RXADDR comment below

static void __not_in_flash_func(apply_pipe_addresses)(void)
{
    uint8_t addr0[5];
    sf30_identity(s_receiver_id, addr0);

    nrf24_write_reg_buf(NRF24_REG_RX_ADDR_P0, addr0, NRF24_ADDR_LEN);

    // The receiver's identity is permanent, derived once from the board's
    // unique ID (see sf30_protocol.h's "Receiver identity"), so pipe 0 is
    // enabled UNCONDITIONALLY -- there is no "claimed" bitmap to gate this
    // on.
    nrf24_write_reg(NRF24_REG_EN_RXADDR, RF24G_PIPE_MASK);

    // TX_ADDR only matters for the auto-ACK path, which sources from the
    // matching pipe's own address regardless -- set to the receiver's
    // identity for parity with the OEM dongle, which writes it on every
    // boot even though it never transmits a payload during an ordinary
    // link.
    nrf24_write_reg_buf(NRF24_REG_TX_ADDR, addr0, NRF24_ADDR_LEN);
}

// ============================================================================
// RADIO BRING-UP
// ============================================================================

static void radio_init(void)
{
    nrf24_flush_rx();
    nrf24_flush_tx();
    nrf24_write_reg(NRF24_REG_STATUS,
                     (uint8_t)(NRF24_STATUS_RX_DR | NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT));

    nrf24_write_reg(NRF24_REG_RF_SETUP, 0x27);      // 250 kbps, max PA
    nrf24_write_reg(NRF24_REG_SETUP_AW, 0x03);      // 5-byte addresses
    nrf24_write_reg(NRF24_REG_SETUP_RETR, 0x12);    // ARD 500us, 2 retries
    nrf24_write_reg(NRF24_REG_EN_AA, 0x3F);         // auto-ACK, all pipes --
                                                     // the controller is the
                                                     // PTX and expects one

    // ACTIVATE unlocks FEATURE/DYNPD on chip families that ship them
    // write-protected until toggled once. Harmless on a genuine nRF24L01+.
    nrf24_activate();

    // -------------------------------------------------------------------
    // FEATURE = 0x06, NOT 0x07. This is the single most important line in
    // this file.
    //
    // Bit 0 of FEATURE is EN_DYN_ACK, which enables the NO_ACK mechanism.
    // The SF30 controller's unlinked probe frames ARE sent NO_ACK. A genuine
    // nRF24L01+ that honours EN_DYN_ACK therefore REFUSES to acknowledge
    // them -- the controller then waits forever for the ACK it needs before
    // it will start hopping, and the link never comes up. The OEM dongle's
    // BK2425 is an nRF24 clone that does NOT implement EN_DYN_ACK the same
    // way: it ACKs the probe regardless of the flag, which is exactly why
    // copying its FEATURE=0x07 register dump verbatim is the wrong instinct
    // on real silicon -- register-identical is not behaviour-identical.
    //
    // Measured A/B, cold controller, no OEM dongle present:
    //
    //   FEATURE 0x07 (EN_DYN_ACK set, matches the dongle):  3 probes, 0 links
    //   FEATURE 0x06 (EN_DYN_ACK clear):    1 probe, LINKED 47s of 51, 1500 pkts
    //
    // Do NOT "correct" this to match the dongle's own register set. It
    // looks like a bug. It is the fix.
    // -------------------------------------------------------------------
    nrf24_write_reg(NRF24_REG_FEATURE,
                     (uint8_t)(NRF24_FEATURE_EN_DPL | NRF24_FEATURE_EN_ACK_PAY));
    nrf24_write_reg(NRF24_REG_DYNPD, 0x3F);   // dynamic payload, all pipes --
                                               // frames are 13 bytes, not the
                                               // 32 RX_PW_Pn would claim

    apply_pipe_addresses();

    nrf24_set_channel(SF30_PARK_CH);
    s_cur_ch = SF30_PARK_CH;
    nrf24_power_up_rx();
    rf24g_hal_ce(true);
}

// ============================================================================
// LINK STATE MACHINE -- PARK / SEARCH / LINKED
// ============================================================================

// Drop CE around every channel change so the ~130us PLL settle happens
// inside the dwell rather than eating into receive time. A genuine
// nRF24L01+ does not re-lock cleanly on a live retune the way the OEM
// dongle's BK2425 does with CE held high throughout -- measured ~4x yield
// with the drop.
static void __not_in_flash_func(radio_retune)(uint8_t ch)
{
    rf24g_hal_ce(false);
    nrf24_set_channel(ch);
    rf24g_hal_ce(true);
    s_cur_ch = ch;
}

static void __not_in_flash_func(drop_slot_link)(void)
{
    if (!s_slot.linked) return;
    s_slot.linked    = false;
    s_slot.confirmed = false;
    s_slot.misses    = 0;
    // Deferred to rf24g_host_task() -- see s_unlink_pending. This runs in
    // ISR context via on_alarm()/enter_park(), where a call into
    // flash-resident manager.c would break the RAM-residency discipline
    // this file's header documents (see the header for why that discipline
    // is kept even though interrupts being off for the whole flash write
    // means it wouldn't actually fault today).
    s_unlink_pending = true;
}

// PARK has NO sweep timer -- the dongle parks INDEFINITELY until it hears
// something, and only sweeps after a contact frame fails to become a link.
// A periodic sweep is actively harmful: it takes the radio off 2447MHz,
// which is the only channel a cold controller ever transmits on, measured
// 2 probes/90s sweeping against 16/120s parked.
static void __not_in_flash_func(enter_park)(void)
{
    s_state      = RF24G_LINK_PARK;
    s_following  = false;
    s_sweep_hops = 0;

    // No CE drop here -- the channel isn't changing (this is a re-arm to the
    // SAME park channel), and the dongle itself never taps CE across
    // thousands of park re-arms. Dropping CE would cost ~130us of RX
    // settling on every one of those for nothing.
    nrf24_set_channel(SF30_PARK_CH);
    s_cur_ch = SF30_PARK_CH;
    nrf24_power_up_rx();     // restore PRX mode in case pairing left us in PTX
    rf24g_hal_ce(true);

    drop_slot_link();

    rf24g_hal_alarm_schedule(rf24g_hal_time_us() + PARK_REARM_US, on_alarm);
}

// SEARCH is a second, independent way in: sweeping and hopping both
// traverse the same 16 channels, so a coincidence with a controller that is
// already hopping (or an already-linked one whose receiver went away) is
// guaranteed within a bounded time, with no contact packet required at all.
static void __not_in_flash_func(enter_search)(void)
{
    s_state      = RF24G_LINK_SEARCH;
    s_following  = false;
    s_sweep_hops = 0;

    s_sweep_idx = sf30_next_idx(s_sweep_idx);
    radio_retune(SF30_HOP_TABLE[s_sweep_idx]);
    rf24g_hal_alarm_schedule(rf24g_hal_time_us() + DWELL_US, on_alarm);
}

#define RF24G_HIST_ADD(s, dt_us) do { \
    uint32_t _ms = (dt_us) / 1000; \
    uint8_t _b = (_ms < 8) ? 0 : (_ms < 9) ? 1 : (_ms < 11) ? 2 : \
                 (_ms < 13) ? 3 : (_ms < 20) ? 4 : 5; \
    if ((s)->inter_hist[_b] < 0xFFFF) (s)->inter_hist[_b]++; \
} while (0)

// If the controller is linked, retune to its predicted next hop index and
// arm the alarm for its deadline (predicted arrival = last_rx_us +
// period_us, deadline = predicted + HOP_GUARD_US). If it isn't linked,
// there is nothing to follow -- park.
static void __not_in_flash_func(schedule_next)(void)
{
    if (!s_slot.linked) {
        enter_park();
        return;
    }

    uint32_t now = rf24g_hal_time_us();

    s_following = true;
    radio_retune(SF30_HOP_TABLE[s_slot.hop_idx]);

    uint32_t deadline = s_slot.last_rx_us + s_slot.period_us + HOP_GUARD_US;
    if ((int32_t)(deadline - now) < 0) deadline = now;   // already due
    rf24g_hal_alarm_schedule(deadline, on_alarm);
}

// RAM-resident unsigned 32-bit divide, for handle_frame()'s period_us
// update below ONLY. A variable-divisor `/` on a Cortex-M0+ (no hardware
// divide instruction) compiles to a libcall, `__wrap___aeabi_uidiv` here,
// and that symbol is flash-resident -- a call into it from the radio ISR is
// exactly the kind of flash branch this file's header comment prohibits.
// (Contrast RF24G_HIST_ADD's `dt_us / 1000` above: a CONSTANT divisor, which
// the compiler turns into a multiply-shift with no libcall at all -- that
// one needs no change.)
//
// This is a plain restoring shift-subtract divide: about 30 cycles for a
// 32-bit quotient. Cost is a non-issue -- handle_frame() already spins for
// ACK_GUARD_US (500us) on every frame, and this runs at the same ~100
// times/s a linked controller delivers frames. Do NOT swap this for
// pico-sdk's hardware divider (SIO on RP2040): this app also builds for
// RP2350 (pico2/pico2_w), whose ARM cores have no SIO divide peripheral, and
// the code must be identical on both. Do NOT put the `/` operator back
// either, no matter how "obviously fine" it looks in a diff -- it isn't.
static uint32_t __not_in_flash_func(rf24g_div_u32)(uint32_t num, uint32_t den)
{
    // den == 0 cannot happen at the one call site below -- burst_n is
    // incremented before it is ever used as a divisor -- but this is ISR
    // code, so guard it anyway rather than trust that invariant forever.
    // Returning num (i.e. behaving like den==1) rather than 0 means a
    // hypothetical hit here leaves period_us at a plausible large value
    // instead of snapping it to 0, which would make schedule_next() treat
    // the next arrival as already due and retune with no dwell at all.
    if (den == 0) return num;

    uint32_t quotient = 0;
    for (int8_t bit = 31; bit >= 0; bit--) {
        if ((num >> bit) >= den) {
            num -= den << bit;
            quotient |= (1u << bit);
        }
    }
    return quotient;
}

// Decode + hand off a validated LINKED frame. Called only for frames that
// have already passed the probe filter in on_radio_irq().
static void __not_in_flash_func(handle_frame)(const uint8_t* buf, uint32_t now)
{
    rf24g_slot_t* s = &s_slot;
    uint8_t ctr = buf[SF30_OFF_COUNTER];

    s_seen = true;

    s->pkts_total++;
    s->pkts_window++;

    bool was_linked = s->linked;
    s->linked = true;
    if (was_linked) s->confirmed = true;   // a SECOND good frame -- acquisition holds
    s_state = RF24G_LINK_LINKED;

    // Period estimate -- accumulate ONLY from consecutive (delta-counter==1)
    // pairs; gap-spanning pairs skew it (see DWELL_US above).
    if (was_linked) {
        uint32_t dt = now - s->last_rx_us;
        uint8_t  dc = (uint8_t)(ctr - s->last_ctr) & 0x3F;
        if (dc == 1 && dt < 200000) {
            s->burst_dt_us += dt;
            s->burst_n++;
            if (s->burst_n >= 100000) { s->burst_dt_us >>= 1; s->burst_n >>= 1; }
            s->period_us = rf24g_div_u32(s->burst_dt_us, s->burst_n);
        }
        RF24G_HIST_ADD(s, dt);
    }

    s->last_ctr   = ctr;
    s->last_rx_us = now;
    s->hop_idx    = sf30_next_idx(ctr);
    s->misses     = 0;

    // Only a LINKED frame is real button state -- byte3 bit 0x20 (Start) is
    // also the controller's power button and is set on most unlinked probe
    // frames, so decoding an unlinked frame would report a phantom Start
    // press every time a controller probes. See sf30_protocol.h.
    ring_push(buf[SF30_OFF_BTN_LO], buf[SF30_OFF_BTN_HI]);

    // Let the hardware auto-ACK finish before touching RF_CH. The ACK
    // starts ~130us after the packet ends and is itself ~300us at 250kbps;
    // retuning inside that window cuts it off, and the controller is
    // waiting on exactly that ACK before it will start hopping. See
    // ACK_GUARD_US.
    uint32_t guard_deadline = now + ACK_GUARD_US;
    while ((int32_t)(rf24g_hal_time_us() - guard_deadline) < 0) { /* spin */ }

    schedule_next();
}

static void __not_in_flash_func(on_radio_irq)(void)
{
    if (s_state == RF24G_LINK_PAIRING) { pairing_on_rx_irq(); return; }

    uint32_t now = rf24g_hal_time_us();

    uint8_t last[SF30_PKT_LEN];
    bool    got = false;

    // Drain the FIFO (3 deep) and keep only the newest valid frame -- if
    // something stalled us for a few ms, several frames may have queued and
    // only the last one carries the current hop index.
    for (uint8_t i = 0; i < 3; i++) {
        if (nrf24_fifo_status() & NRF24_FIFO_RX_EMPTY) break;

        uint8_t len = nrf24_r_rx_pl_wid();
        if (len == 0 || len > NRF24_MAX_PAYLOAD) {
            nrf24_flush_rx();   // corrupt length -- discard the lot
            break;
        }

        uint8_t buf[NRF24_MAX_PAYLOAD];
        uint8_t status = nrf24_r_rx_payload(buf, len);
        uint8_t pipe = (uint8_t)((status & NRF24_STATUS_RX_P_NO_MASK) >> NRF24_STATUS_RX_P_NO_SHIFT);

        // Do NOT validate byte 12 against 0x10 -- the contact frame that
        // begins a link carries 0x31 there. Header + length + the 13-byte
        // width are enough to identify a frame; see sf30_protocol.h.
        if (len == SF30_PKT_LEN &&
            buf[SF30_OFF_HEADER] == SF30_HDR_MAGIC &&
            buf[SF30_OFF_LENGTH] == SF30_LEN_MAGIC &&
            pipe == 0) {
            // Byte loop, not memcpy: ISR context, and at -O0 a libc memcpy
            // call would be flash-resident. See nrf24l01.c's nrf24_xfer().
            for (uint8_t k = 0; k < SF30_PKT_LEN; k++) last[k] = buf[k];
            got = true;
        }
    }

    nrf24_write_reg(NRF24_REG_STATUS, NRF24_STATUS_RX_DR);

    if (!got) return;

    uint8_t ctr = last[SF30_OFF_COUNTER];

    // Is this an unlinked probe rather than a genuine hop-sequence frame?
    // A probe fails the hop_table[counter]==curCh identity, or carries the
    // 0x31 trailer. Byte 10 free-runs (pinned at 48) while probing, so it is
    // NOT a channel index yet -- following hop_table[48+1] walks away from
    // the only channel a cold controller uses.
    bool probe = !s_slot.linked &&
                 (SF30_HOP_TABLE[ctr & 0x3F] != s_cur_ch ||
                  last[SF30_OFF_TRAILER] == 0x31);

    if (probe) {
        s_slot.last_ctr = ctr;
        if (s_state != RF24G_LINK_SEARCH) enter_search();
        return;
    }

    handle_frame(last, now);
}

static void __not_in_flash_func(on_alarm)(void)
{
    if (s_state == RF24G_LINK_PAIRING) { pairing_on_alarm(); return; }

    uint32_t now = rf24g_hal_time_us();

    switch (s_state) {
    case RF24G_LINK_PARK:
        nrf24_set_channel(SF30_PARK_CH);
        rf24g_hal_alarm_schedule(now + PARK_REARM_US, on_alarm);
        break;

    case RF24G_LINK_SEARCH:
        s_sweep_idx = sf30_next_idx(s_sweep_idx);
        radio_retune(SF30_HOP_TABLE[s_sweep_idx]);
        if (++s_sweep_hops >= SEARCH_HOPS) {
            enter_park();
        } else {
            rf24g_hal_alarm_schedule(now + DWELL_US, on_alarm);
        }
        break;

    case RF24G_LINK_LINKED: {
        if (!s_following) { enter_park(); break; }
        rf24g_slot_t* s = &s_slot;
        s->misses++;

        bool give_up_to_search = !s->confirmed && s->misses > ACQUIRE_MISSES;
        bool give_up_to_park   = s->misses > MAX_MISSES;

        if (give_up_to_search || give_up_to_park) {
            drop_slot_link();
            if (give_up_to_search) enter_search(); else enter_park();
            break;
        }

        // Free-run: advance this slot's index by one block-aware step and
        // retune anyway, so a stall self-heals the moment a packet lands
        // (every received frame re-syncs both index AND phase -- see
        // handle_frame()).
        s->hop_idx = sf30_next_idx(s->hop_idx);

        // Advance by exactly one period, NOT to `now` -- `now` already
        // includes HOP_GUARD_US, so re-anchoring there slips the schedule on
        // every miss until the dwell no longer contains the transmitter's
        // slot at all. Staying on the transmitter's own grid is drift-free;
        // a received frame still re-syncs both in handle_frame().
        s->last_rx_us += s->period_us;

        // Catch up in whole periods if a long stall (e.g. a flash erase,
        // which runs with interrupts off) left the anchor over a period
        // behind. Bounded, with a fallback below, because this is alarm-ISR
        // context where an unbounded loop is a hang, not a slowdown. 8
        // periods (~81ms) comfortably covers the ~45ms worst-case erase
        // stall this exists to absorb.
        for (uint8_t catchup = 0;
             catchup < 8 && (int32_t)((s->last_rx_us + s->period_us) - now) < 0;
             catchup++) {
            s->last_rx_us += s->period_us;
        }

        // Still behind after the bound: take the phase slip once rather than
        // spin further. The next received frame re-syncs regardless.
        if ((int32_t)((s->last_rx_us + s->period_us) - now) < 0)
            s->last_rx_us = now;

        schedule_next();
        break;
    }

    default:
        break;
    }
}

// ============================================================================
// CORE-0 TASK -- ring drain, button decode, router submission
// ============================================================================

static void submit_event(uint8_t b2, uint8_t b3)
{
    uint32_t buttons = 0;

    // Up sets a bit in BOTH bytes (byte2:0x80 and byte3:0x40 together) --
    // treat either as Up, never surface byte3:0x40 as its own button.
    if (sf30_up_pressed(b2, b3)) buttons |= JP_BUTTON_DU;
    if (b2 & SF30_DOWN)  buttons |= JP_BUTTON_DD;
    if (b2 & SF30_LEFT)  buttons |= JP_BUTTON_DL;
    if (b2 & SF30_RIGHT) buttons |= JP_BUTTON_DR;

    if (b2 & SF30_B) buttons |= JP_BUTTON_B1;
    if (b2 & SF30_A) buttons |= JP_BUTTON_B2;
    if (b2 & SF30_Y) buttons |= JP_BUTTON_B3;
    if (b2 & SF30_X) buttons |= JP_BUTTON_B4;

    if (b3 & SF30_L)      buttons |= JP_BUTTON_L1;
    if (b3 & SF30_R)      buttons |= JP_BUTTON_R1;
    if (b3 & SF30_SELECT) buttons |= JP_BUTTON_S1;
    if (b3 & SF30_START)  buttons |= JP_BUTTON_S2;

    input_event_t event;
    init_input_event(&event);
    event.dev_addr  = RF24G_DEV_ADDR;
    event.instance  = 0;
    event.type      = INPUT_TYPE_GAMEPAD;
    event.transport = INPUT_TRANSPORT_NATIVE;
    event.layout    = LAYOUT_NINTENDO_4FACE;
    event.buttons   = buttons;
    // No analog axes -- the SF30 2.4G has no sticks. init_input_event()
    // already centred analog[] at 128.
    router_submit_input(&event);
}

static void pairing_confirm_task(void)
{
    // Report the immediate outcome of the last pairing_end() call -- left
    // there instead of printf'd directly since that runs in ISR/alarm
    // context. "accepted" here just means the controller stopped asking,
    // which is the offer going provisional, not confirmed -- see below.
    pair_result_t result = s_pair_result;
    if (result != PAIR_RESULT_NONE) {
        s_pair_result = PAIR_RESULT_NONE;
        if (result == PAIR_RESULT_ACCEPTED)
            printf("[rf24g] pairing: paired -- confirming\n");
        else
            printf("[rf24g] pairing: not accepted\n");
    }

    if (!s_pair_prov_active || s_state == RF24G_LINK_PAIRING) return;

    if (s_slot.pkts_total != s_pair_prov_pkts) {
        s_pair_prov_active = false;
        printf("[rf24g] pairing confirmed -- frames arriving\n");
        return;
    }

    if ((int32_t)(rf24g_hal_time_us() - s_pair_prov_deadline_us) < 0) return;

    // Diagnostic only -- see the comment by s_pair_prov_active's
    // declaration. The pipe was already open before this pairing attempt
    // and stays open regardless of whether it ever confirms.
    //
    // Report the OBSERVATION, not a cause. "No frames" has several causes
    // that are indistinguishable from here, because they all look like
    // silence to the radio: the controller rejected the offer; it already
    // held this address and sat in pairing mode (which transmits no data
    // frames) so its counter never moved; or it took the address but won't
    // transmit until power-cycled. s_seen cannot disambiguate either: it is
    // only set in handle_frame(), so in precisely this "no frames"
    // situation it is still clear.
    printf("[rf24g] pairing: no frames yet after %ums -- expected if this "
           "controller was already paired here or is still in pairing "
           "mode. Power-cycle the controller; if it still doesn't link, "
           "pair again.\n",
           (unsigned)(PAIR_CONFIRM_US / 1000));
    s_pair_prov_active = false;
}

void rf24g_host_task(void)
{
    if (!s_initialized) return;

    // Tear down the player registration the ISR flagged. Done here, on
    // core 0, because remove_players_by_address() is flash-resident -- see
    // s_unlink_pending.
    if (s_unlink_pending) {
        s_unlink_pending = false;
        remove_players_by_address(RF24G_DEV_ADDR, 0);
    }

    while (s_ring_tail != s_ring_head) {
        rf24g_rx_item_t item = s_ring[s_ring_tail];   // volatile struct copy
        s_ring_tail = (uint8_t)((s_ring_tail + 1) & (RF24G_RING_SIZE - 1));
        submit_event(item.raw_b2, item.raw_b3);
    }

    pairing_confirm_task();

    uint32_t now_ms = platform_time_ms();
    if ((uint32_t)(now_ms - s_stats_window_ms) >= 1000) {
        s_slot.pps         = s_slot.pkts_window;
        s_slot.pkts_window = 0;
        s_stats_window_ms = now_ms;
    }
}

// ============================================================================
// PAIRING RENDEZVOUS -- driven from the same alarm/IRQ as the link
// scheduler, dispatched on s_state == RF24G_LINK_PAIRING.
// ============================================================================

static void __not_in_flash_func(pairing_listen)(void)
{
    nrf24_flush_rx();
    nrf24_flush_tx();
    nrf24_write_reg_buf(NRF24_REG_RX_ADDR_P0, SF30_ADDR_P1, NRF24_ADDR_LEN);
    nrf24_write_reg_buf(NRF24_REG_TX_ADDR,    SF30_ADDR_P1, NRF24_ADDR_LEN);
    nrf24_write_reg(NRF24_REG_EN_RXADDR, 0x01);   // pipe 0 only while pairing
    nrf24_set_channel(SF30_PARK_CH);
    s_cur_ch = SF30_PARK_CH;
    nrf24_power_up_rx();
    rf24g_hal_ce(true);
    nrf24_write_reg(NRF24_REG_STATUS,
                     (uint8_t)(NRF24_STATUS_RX_DR | NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT));
}

static void __not_in_flash_func(pairing_switch_tx)(void)
{
    nrf24_write_reg(NRF24_REG_EN_RXADDR, 0x00);   // not receiving while transmitting
    nrf24_power_up_tx();
}

// ACK counts are not a reliable success signal here -- a run that
// successfully bound a controller got nearly none of its replies
// acknowledged. So this doesn't wait for TX_DS/MAX_RT; the 20ms slot
// cadence is what the controller's listen window actually requires.
static void __not_in_flash_func(pairing_send_reply)(void)
{
    nrf24_flush_tx();
    nrf24_write_reg(NRF24_REG_STATUS,
                     (uint8_t)(NRF24_STATUS_TX_DS | NRF24_STATUS_MAX_RT));
    nrf24_w_tx_payload(s_pair_reply, SF30_PAIR_RSP_LEN);
    rf24g_hal_ce(true);
    uint32_t deadline = rf24g_hal_time_us() + 15;   // >=10us CE pulse triggers TX
    while ((int32_t)(rf24g_hal_time_us() - deadline) < 0) { /* spin */ }
    rf24g_hal_ce(false);
}

static void __not_in_flash_func(pairing_end)(bool claim)
{
    if (claim) {
        // Start (or restart) the confirmation watch -- see the comment by
        // s_pair_prov_active's declaration. No claim bookkeeping is needed
        // here any more: the pipe has been open since boot regardless of
        // the outcome, so this only decides what pairing_confirm_task()
        // reports, not what the radio hears.
        s_pair_prov_active      = true;
        s_pair_prov_pkts        = s_slot.pkts_total;
        s_pair_prov_deadline_us = rf24g_hal_time_us() + PAIR_CONFIRM_US;
    }
    s_pair_result = claim ? PAIR_RESULT_ACCEPTED : PAIR_RESULT_REJECTED;

    // Restores the normal RX addresses / EN_RXADDR that pairing_listen() and
    // pairing_switch_tx() overwrote for the rendezvous exchange -- needed
    // whether or not `claim` is true, since the radio was reconfigured
    // either way.
    apply_pipe_addresses();
    enter_park();
}

static void __not_in_flash_func(pairing_on_rx_irq)(void)
{
    uint16_t reqs = 0;

    for (uint8_t i = 0; i < 3; i++) {
        if (nrf24_fifo_status() & NRF24_FIFO_RX_EMPTY) break;
        uint8_t len = nrf24_r_rx_pl_wid();
        if (len == 0 || len > NRF24_MAX_PAYLOAD) { nrf24_flush_rx(); break; }
        uint8_t buf[NRF24_MAX_PAYLOAD];
        nrf24_r_rx_payload(buf, len);
        if (len == SF30_PAIR_REQ_LEN && buf[0] == SF30_PAIR_HDR) reqs++;
    }
    nrf24_write_reg(NRF24_REG_STATUS, NRF24_STATUS_RX_DR);

    if (!reqs) return;

    uint32_t now = rf24g_hal_time_us();
    s_pair_requests   += reqs;
    s_pair_last_req_us = now;
    if (s_pair_phase == PAIR_RX) s_pair_cycle_reqs += reqs;
}

static void __not_in_flash_func(pairing_on_alarm)(void)
{
    uint32_t now = rf24g_hal_time_us();

    switch (s_pair_phase) {
    case PAIR_WAIT:
        if (s_pair_requests && (now - s_pair_last_req_us) >= PAIR_QUIET_US) {
            // The controller burst-transmits its requests and only THEN
            // listens -- answering the first request talks over the rest of
            // its own burst while it is deaf to us. Wait for quiet, then
            // hit the window the OEM dongle hits: first reply 346ms after
            // the LAST request, never the first.
            s_pair_phase = PAIR_TX;
            s_pair_idx   = 0;
            s_pair_cycle = 0;
            pairing_switch_tx();
            rf24g_hal_alarm_schedule(s_pair_last_req_us + PAIR_FIRST_TX_US, on_alarm);
        } else if ((now - s_pair_started_us) >= PAIR_WAIT_US) {
            pairing_end(false);
        } else {
            rf24g_hal_alarm_schedule(now + PAIR_POLL_US, on_alarm);
        }
        break;

    case PAIR_TX:
        pairing_send_reply();
        if (++s_pair_idx >= PAIR_PER_CYCLE) {
            s_pair_phase     = PAIR_RX;
            s_pair_cycle_reqs = 0;
            pairing_listen();
            rf24g_hal_alarm_schedule(now + PAIR_LISTEN_US, on_alarm);
        } else {
            rf24g_hal_alarm_schedule(now + PAIR_SLOT_US, on_alarm);
        }
        break;

    case PAIR_RX:
        if (++s_pair_cycle >= PAIR_CYCLES) {
            // Only the LAST cycle decides. A controller that accepts still
            // re-requests once or twice first (both in our exchange and the
            // dongle's own), so counting an earlier cycle's requests would
            // call a successful pairing a failure.
            pairing_end(s_pair_cycle_reqs == 0);
        } else {
            s_pair_phase = PAIR_TX;
            s_pair_idx   = 0;
            pairing_switch_tx();
            rf24g_hal_alarm_schedule(now, on_alarm);
        }
        break;
    }
}

bool rf24g_host_begin_pairing(void)
{
    if (!s_initialized || s_state == RF24G_LINK_PAIRING)
        return false;

    uint8_t addr[5];
    sf30_identity(s_receiver_id, addr);
    sf30_pair_reply(addr, s_pair_reply);

    s_pair_phase        = PAIR_WAIT;
    s_pair_cycle        = 0;
    s_pair_idx          = 0;
    s_pair_requests     = 0;
    s_pair_cycle_reqs   = 0;
    s_pair_last_req_us  = platform_time_us();
    s_pair_started_us   = platform_time_us();
    s_state = RF24G_LINK_PAIRING;

    pairing_listen();

    printf("[rf24g] pairing: offering %02X %02X %02X %02X %02X -- "
           "hold Select on a controller until its LED blinks rapidly\n",
           addr[0], addr[1], addr[2], addr[3], addr[4]);

    rf24g_hal_alarm_schedule(platform_time_us() + PAIR_POLL_US, on_alarm);
    return true;
}

bool rf24g_host_is_pairing(void)
{
    return s_state == RF24G_LINK_PAIRING;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void rf24g_host_init(void)
{
    rf24g_host_init_pins(RF24G_PIN_SCK, RF24G_PIN_MOSI, RF24G_PIN_MISO,
                         RF24G_PIN_CSN, RF24G_PIN_CE, RF24G_PIN_IRQ);
}

void rf24g_host_init_pins(uint8_t sck, uint8_t mosi, uint8_t miso,
                          uint8_t csn, uint8_t ce, uint8_t irq)
{
    if (s_initialized) return;

    printf("[rf24g] init SCK=%d MOSI=%d MISO=%d CSN=%d CE=%d IRQ=%d\n",
           sck, mosi, miso, csn, ce, irq);

    memset(&s_slot, 0, sizeof(s_slot));
    s_slot.period_us = DWELL_US;

    // Derive this receiver's permanent identity from the board's own
    // factory-programmed unique ID -- see sf30_protocol.h's "Receiver
    // identity" and s_receiver_id's declaration above. Flash-resident and
    // core-0-only, which is safe here (this runs once at init, before
    // radio_init() below and before rf24g_hal_irq_attach() further down --
    // the ISR chain isn't live yet) but must never be called from anywhere
    // reachable from on_radio_irq()/on_alarm(), since platform_get_unique_id()
    // performs a flash operation on RP2040.
    uint8_t uid[8];
    platform_get_unique_id(uid, sizeof(uid));
    sf30_derive_receiver_id(uid, sizeof(uid), s_receiver_id);
    printf("[rf24g] receiver id %02X %02X %02X %02X (derived from board unique id)\n",
           s_receiver_id[0], s_receiver_id[1], s_receiver_id[2], s_receiver_id[3]);

    rf24g_hal_init(sck, mosi, miso, csn, ce, irq);

    if (!nrf24_probe()) {
        printf("[rf24g] WARNING: nRF24L01+ not responding -- check wiring "
               "(SPI/CE/CSN) and power-cycle the module, not just the board\n");
    }

    radio_init();
    rf24g_hal_irq_attach(on_radio_irq);

    s_initialized = true;
    enter_park();

    printf("[rf24g] parked on ch %u (%u MHz), nothing seen yet\n",
           SF30_PARK_CH, 2400 + SF30_PARK_CH);
}

bool rf24g_host_is_connected(void)
{
    return s_initialized && s_slot.linked;
}

uint8_t rf24g_host_get_device_count(void)
{
    return s_slot.linked ? 1 : 0;
}

const char* rf24g_host_state_name(void)
{
    switch (s_state) {
        case RF24G_LINK_LINKED:  return "LINKED";
        case RF24G_LINK_SEARCH:  return "SEARCH";
        case RF24G_LINK_PAIRING: return "PAIRING";
        default:                 return "PARK";
    }
}

void rf24g_host_print_stats(void)
{
    printf("[rf24g] state=%s ch=%u (%u MHz)\n",
           rf24g_host_state_name(), s_cur_ch, 2400 + s_cur_ch);

    if (!s_seen && s_slot.pkts_total == 0) return;

    printf("[rf24g]  %-6s seen=%s pps=%lu total=%lu period=%luus misses=%u\n",
           s_slot.linked ? "LINKED" : "-", s_seen ? "yes" : "no",
           (unsigned long)s_slot.pps, (unsigned long)s_slot.pkts_total,
           (unsigned long)s_slot.period_us, s_slot.misses);
    printf("[rf24g]    inter-arrival(ms): <8=%u 8-9=%u 9-11=%u 11-13=%u "
           "13-20=%u >=20=%u\n",
           s_slot.inter_hist[0], s_slot.inter_hist[1], s_slot.inter_hist[2],
           s_slot.inter_hist[3], s_slot.inter_hist[4], s_slot.inter_hist[5]);
}

const InputInterface rf24g_input_interface = {
    .name = "24G",
    .source = INPUT_SOURCE_NATIVE_24G,
    .init = rf24g_host_init,
    .task = rf24g_host_task,
    .is_connected = rf24g_host_is_connected,
    .get_device_count = rf24g_host_get_device_count,
};
