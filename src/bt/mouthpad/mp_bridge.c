// mp_bridge.c - MouthPad NUS <-> USB CDC bridge (see mp_bridge.h)
//
// The relay is AMBIENT: it attaches to the shared CDC command port via a demux
// filter + TX hook (cdc_register_relay) rather than owning the port. MouthPad
// relay frames (0xAA 0x55 ...) are routed to/from the BLE NUS link; every other
// byte falls through to the normal JoypadOS command parser. The demux is only
// active while a MouthPad NUS link is up, so when none is connected the CDC port
// behaves exactly as it did before MouthPad support existed.

#include "mp_bridge.h"
#include "mp_relay.h"
#include "bt/btstack/btstack_host.h"
#include "tusb.h"
#include <string.h>
#include <stdio.h>

#if CFG_TUD_CDC > 0
#include "usb/usbd/cdc/cdc.h"

// ---------------------------------------------------------------------------
// Lock-free SPSC byte ring (device->host framed bytes).
// Producer: NUS notification (BTstack context). Consumer: relay TX pump (main).
// ---------------------------------------------------------------------------
#define TO_HOST_RING_SIZE 16384           // power of two — absorbs telemetry bursts
#define TO_HOST_RING_MASK (TO_HOST_RING_SIZE - 1)

static uint8_t           to_host_buf[TO_HOST_RING_SIZE];
static volatile uint32_t to_host_head;    // write index (producer: BTstack ctx)
static volatile uint32_t to_host_tail;    // read index (consumer: main loop)

// Lossless-relay instrumentation (read via the MP.STATS command). The ONLY
// firmware-side loss point is a ring overflow (drops); USB bulk is lossless and
// the single-core SPSC ring can't corrupt. So drops==0 over a session proves
// zero data loss; high_water shows the burst margin against TO_HOST_RING_SIZE.
static volatile uint32_t mp_frames;       // NUS notifications relayed to host
static volatile uint32_t to_host_drops;   // frames dropped (ring full)
static volatile uint32_t mp_encode_fails; // NUS payloads that failed to frame
static volatile uint32_t to_host_high;    // max ring occupancy seen (bytes)

static inline uint32_t ring_count(void) { return to_host_head - to_host_tail; }

static void ring_push(const uint8_t* data, uint32_t len)
{
    // Drop the frame if it doesn't fit (better than corrupting the stream).
    // This runs in the BTstack NUS-notification context — do NOT printf here
    // (stdio is UART and blocks ~4ms, stalling BTstack + the CDC drain and
    // cascading into more drops). Just count; surfaced via MP.STATS.
    uint32_t used = ring_count();
    if (len > TO_HOST_RING_SIZE - used) {
        to_host_drops++;
        return;
    }
    for (uint32_t i = 0; i < len; i++) {
        to_host_buf[(to_host_head + i) & TO_HOST_RING_MASK] = data[i];
    }
    to_host_head += len;   // publish after the copy
    mp_frames++;
    if (used + len > to_host_high) to_host_high = used + len;
}

static mp_relay_rx_t relay_rx;

// Relay stats accessor (for the MP.STATS CDC command).
void mp_bridge_get_stats(uint32_t* frames, uint32_t* drops,
                         uint32_t* encode_fails, uint32_t* high_water, uint32_t* ring_size)
{
    if (frames)       *frames = mp_frames;
    if (drops)        *drops = to_host_drops;
    if (encode_fails) *encode_fails = mp_encode_fails;
    if (high_water)   *high_water = to_host_high;
    if (ring_size)    *ring_size = TO_HOST_RING_SIZE;
}

// ---------------------------------------------------------------------------
// device -> host: NUS notification (BTstack ctx) -> frame -> ring
// ---------------------------------------------------------------------------
static void on_nus_rx(const uint8_t* data, uint16_t len)
{
    uint8_t frame[MP_RELAY_MAX_FRAME];
    size_t n = mp_relay_encode_to_app(data, len, frame, sizeof(frame));
    if (n) ring_push(frame, (uint32_t)n);
    else   mp_encode_fails++;
}

// Format a BLE address (big-endian bd_addr) as "AA:BB:CC:DD:EE:FF".
static void fmt_addr(const uint8_t a[6], char out[18])
{
    static const char hx[] = "0123456789ABCDEF";
    int p = 0;
    for (int i = 0; i < 6; i++) {
        out[p++] = hx[a[i] >> 4];
        out[p++] = hx[a[i] & 0xF];
        if (i < 5) out[p++] = ':';
    }
    out[p] = '\0';
}

// Answer a dongle-level device_info_read with the connected MouthPad's info.
static void send_device_info(void)
{
    btstack_host_mouthpad_info_t mi;
    mp_relay_device_info_t di;
    char addr[18] = "";
    memset(&di, 0, sizeof(di));
    di.firmware = "";
    di.family   = MP_RELAY_FAMILY_UNSPECIFIED;   // JoypadOS isn't an Augmental dongle
    di.board    = MP_RELAY_BOARD_UNSPECIFIED;
    if (btstack_host_get_mouthpad_info(&mi)) {
        di.name = mi.name;
        di.firmware = mi.firmware;   // real MouthPad DIS firmware revision
        fmt_addr(mi.addr, addr);
        di.address = addr;
        di.vid = mi.vid;
        di.pid = mi.pid;
    }
    uint8_t frame[320];
    size_t n = mp_relay_encode_device_info(&di, frame, sizeof frame);
    if (n) ring_push(frame, (uint32_t)n);
}

// Answer a dongle-level ble_connection_status_read.
static void send_conn_status(void)
{
    btstack_host_mouthpad_info_t mi;
    mp_relay_conn_status_t st = MP_RELAY_CONN_SEARCHING;
    uint32_t battery = 0;
    if (btstack_host_get_mouthpad_info(&mi)) {
        st = mi.ready ? MP_RELAY_CONN_CONNECTED : MP_RELAY_CONN_CONNECTING;
        battery = mi.battery;
    }
    uint8_t frame[64];
    size_t n = mp_relay_encode_conn_status(st, 0, battery, frame, sizeof frame);
    if (n) ring_push(frame, (uint32_t)n);
}

// Answer a clear_bonds_write: forget the connected MouthPad's bond.
static void send_clear_bonds(void)
{
    bool ok = btstack_host_mouthpad_clear_bond();
    uint8_t frame[16];
    size_t n = mp_relay_encode_clear_bonds_response(ok, frame, sizeof frame);
    if (n) ring_push(frame, (uint32_t)n);
}

// ---------------------------------------------------------------------------
// host -> device: classified relay request from the reconstructor.
//   PASSTHROUGH      -> write inner NUS payload to the MouthPad
//   DEVICE_INFO_READ -> answer device_info_response (dongle-level)
//   CONN_STATUS_READ -> answer ble_connection_status_response (dongle-level)
// ---------------------------------------------------------------------------
static void on_relay_request(mp_relay_req_t type, const uint8_t* data, uint16_t len, void* ctx)
{
    (void)ctx;
    switch (type) {
    case MP_RELAY_REQ_PASSTHROUGH:
        if (data && len) btstack_host_mouthpad_nus_send(data, len);
        break;
    case MP_RELAY_REQ_DEVICE_INFO_READ:
        send_device_info();
        break;
    case MP_RELAY_REQ_CONN_STATUS_READ:
        send_conn_status();
        break;
    case MP_RELAY_REQ_CLEAR_BONDS:
        send_clear_bonds();
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// CDC RX demux filter. Returns true if the byte was consumed as part of a
// MouthPad relay frame; false to let the JoypadOS command parser handle it.
//
// Both framings begin with 0xAA (JoypadOS = 0xAA + LE len; MouthPad = 0xAA 0x55
// + BE len), so we use a 1-byte lookahead: hold 0xAA, and on the next byte
// commit to a relay frame iff it's 0x55, otherwise replay the buffered 0xAA to
// the command parser. Inside a relay frame we count payload+CRC bytes from its
// BE length so we know exactly when to hand the stream back to the parser.
// ---------------------------------------------------------------------------
enum { DX_IDLE, DX_GOT_AA, DX_IN_FRAME };
static uint8_t  dx_state = DX_IDLE;
static uint8_t  dx_after;          // bytes seen after the 0xAA 0x55 magic
static uint8_t  dx_lenbuf[2];      // BE length bytes
static uint16_t dx_remaining;      // payload+CRC bytes left in the frame

static bool relay_cdc_filter(uint8_t b)
{
    // Demux whenever a host is on the CDC port, NOT only when a MouthPad is
    // connected. The desktop utility polls dongle-level queries (device_info /
    // ble_connection_status) the moment it connects — including while no MouthPad
    // is paired, when it expects a "searching" status. Gating on the MouthPad
    // link made those polls go unanswered, so the utility showed "disconnected"
    // while the dongle was actually scanning. Passthrough to a missing MouthPad
    // is a safe no-op (btstack_host_mouthpad_nus_send returns false). With no host
    // connected there's no CDC RX, so this is inert.
    if (!tud_cdc_connected()) {
        dx_state = DX_IDLE;
        return false;
    }

    switch (dx_state) {
    case DX_IDLE:
        if (b == 0xAA) { dx_state = DX_GOT_AA; return true; }   // hold, disambiguate
        return false;

    case DX_GOT_AA:
        if (b == 0x55) {
            dx_state = DX_IN_FRAME;
            dx_after = 0;
            uint8_t magic[2] = { 0xAA, 0x55 };
            mp_relay_rx_feed(&relay_rx, magic, 2);
            return true;
        }
        // Not a relay frame: replay the buffered 0xAA, then let this byte fall
        // through to the command parser too.
        dx_state = DX_IDLE;
        cdc_feed_command_byte(0xAA);
        return false;

    case DX_IN_FRAME:
        mp_relay_rx_feed(&relay_rx, &b, 1);
        if (dx_after < 2) {
            dx_lenbuf[dx_after] = b;
            if (++dx_after == 2) {
                uint16_t plen = ((uint16_t)dx_lenbuf[0] << 8) | dx_lenbuf[1];
                if (plen > MP_RELAY_MAX_NUS_PAYLOAD + 16) {
                    dx_state = DX_IDLE;        // implausible — abort, resync
                } else {
                    dx_remaining = plen + 2;   // payload + 2-byte CRC
                }
            }
        } else if (dx_remaining == 0 || --dx_remaining == 0) {
            dx_state = DX_IDLE;                 // frame complete
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// device -> host TX pump: drain framed bytes from the ring to CDC. Registered
// as the cdc_task hook, so it runs every cdc_task() iteration.
// ---------------------------------------------------------------------------
static void relay_cdc_pump(void)
{
    if (!tud_cdc_connected()) return;
    while (ring_count() > 0) {
        uint32_t avail = tud_cdc_write_available();
        if (avail == 0) break;
        uint32_t chunk = ring_count();
        if (chunk > avail) chunk = avail;
        uint8_t tmp[64];
        if (chunk > sizeof(tmp)) chunk = sizeof(tmp);
        for (uint32_t i = 0; i < chunk; i++) {
            tmp[i] = to_host_buf[(to_host_tail + i) & TO_HOST_RING_MASK];
        }
        uint32_t wrote = tud_cdc_write(tmp, chunk);
        to_host_tail += wrote;
        if (wrote < chunk) break;
    }
    tud_cdc_write_flush();
}

void mp_bridge_init(void)
{
    to_host_head = to_host_tail = 0;
    dx_state = DX_IDLE;
    mp_relay_rx_init(&relay_rx, on_relay_request, NULL);
    btstack_host_set_mouthpad_nus_rx_cb(on_nus_rx);
    cdc_register_relay(relay_cdc_filter, relay_cdc_pump);
    printf("[MP_BRIDGE] Relay attached to CDC (ambient; demux gated on MouthPad link)\n");
}

// Strong override of the weak cdc.c hook: when this module is linked (BLE apps),
// the relay attaches itself automatically during cdc_init().
void cdc_relay_late_init(void)
{
    mp_bridge_init();
}

// Deprecated: the relay now runs through cdc_task() (filter + TX pump). Kept as
// a no-op so any remaining caller still links.
void mp_bridge_task(void) {}

#else  // CFG_TUD_CDC == 0 — no CDC port, relay is inert

void mp_bridge_init(void) {}
void mp_bridge_task(void) {}

#endif
