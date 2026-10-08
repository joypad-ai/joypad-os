// switch2_ble.c - Switch 2 Pro Controller BLE output (see switch2_ble.h)
// SPDX-License-Identifier: Apache-2.0
//
// Threading: every BTstack call runs in the BTstack context (ATT callbacks, HCI
// handler, run-loop timers). The main loop only publishes the controller state into
// a seqlocked snapshot and marshals sync/wake requests with
// btstack_run_loop_execute_on_main_thread — required on ESP32/nRF where BTstack has
// its own thread, harmless on the single-context Pico W.
//
// Encryption: the console never runs SMP. After 0x15/0x03 it sends LL_ENC_REQ with
// EDIV = 0 / Rand = 0 and expects the LTK derived during 0x15. We hand that key to
// BTstack's SM through an le_device_db entry for the console (secure-connection style,
// null EDIV/Rand), so SM answers the LTK request itself. SM resolves the peer against
// le_device_db once at connect, when a first-time console isn't in it yet, so after
// finalising we re-arm that lookup for the live connection.

#include "switch2_ble.h"
#include "platform/platform.h"

#ifdef CONFIG_SWITCH2_BLE_OUTPUT

#include "switch2_proto.h"
#include "switch2_gatt_db.h"
#include "core/input_event.h"
#include "core/router/router.h"

#include "btstack_defines.h"
#include "btstack_event.h"
#include "btstack_run_loop.h"
#include "btstack_tlv.h"
#include "btstack_util.h"
#include "bluetooth_data_types.h"
#include "gap.h"
#include "hci.h"
#include "hci_dump.h"
#include <stdarg.h>
#include "l2cap.h"
#include "ble/att_db.h"
#include "ble/gatt_client.h"
#include "ble/att_server.h"
#include "ble/le_device_db.h"
#include "ble/sm.h"

#include <stdio.h>

#if defined(BTSTACK_USE_NRF)
#include <nrfx.h>   // NRF_FICR (factory device address)
#endif
#include <string.h>

// Forward declarations (feedback.h drags in TinyUSB types on some builds)
extern void feedback_set_rumble(uint8_t player_index, uint8_t left, uint8_t right);
extern void feedback_set_led_player(uint8_t player_index, uint8_t player_num);
// Central-side BT host; absent on peripheral-only builds (ESP32 universal).
extern void btstack_host_suppress_scan_latched(bool suppress) __attribute__((weak));
// Defined by the nRF app next to the patched link layer; absent on other
// platforms, so the address is checked before writing (a weak *variable*
// resolves to address 0 when undefined, which would be a null store).
extern unsigned char joypad_le_interval_min __attribute__((weak));

#define TLV_TAG_SW2_BOND   (((uint32_t)'S' << 24) | ((uint32_t)'W' << 16) | ((uint32_t)'2' << 8) | 'B')
#define BOND_MAGIC         0xB2
#define RSP_QUEUE_LEN      4
#define RSP_MAX            (SW2_RSP2_PREFIX_LEN + SW2_MAX_RSP_LEN)
// 30 ms. A real pad measures 21.25 ms, but 30 ms is what was on air the one time
// a console actually connected to us, and espp/zhantss work at 20-40 ms, so the
// exact value is not the variable -- do not change it without a reason.
#define ADV_INTERVAL       0x0030

typedef struct {
    uint8_t magic;
    uint8_t host_addr[6];   // console identity address from 0x15/0x01 (wire order, for adverts)
    uint8_t peer_type;      // console connection address (BTstack bd_addr_t order)
    uint8_t peer_addr[6];
    uint8_t ltk[16];        // wire order
} __attribute__((packed)) sw2_bond_t;

typedef struct {
    uint16_t handle;
    uint16_t len;
    uint8_t  data[RSP_MAX];
} sw2_notification_t;

static switch2_proto_t s_proto;
static sw2_bond_t      s_bond;
static volatile bool   s_bonded;

static volatile hci_con_handle_t s_con = HCI_CON_HANDLE_INVALID;
static bd_addr_t s_peer_addr;
static uint8_t   s_peer_type;
static bool      s_encrypted;
static bool      s_input_notify;
static bool      s_wake_latched;

// Outgoing notifications: command responses first, then the input report.
static sw2_notification_t s_rsp[RSP_QUEUE_LEN];
static uint8_t  s_rsp_head, s_rsp_count;
static bool     s_input_due;
static bool     s_send_requested;   // registration queued in att_server (never add it twice)
static btstack_context_callback_registration_t s_send_reg;
static uint8_t  s_last_input[SW2_INPUT_REPORT_LEN];

// Writable CCC / vendor-descriptor values, echoed back on read.
static const uint16_t k_rw_handles[] = {
    SW2_H_COMMON_INPUT_CCC, SW2_H_COMMON_INPUT_RATE, SW2_H_INPUT_CCC, SW2_H_INPUT_RATE,
    SW2_H_RESPONSE1_CCC, SW2_H_RESPONSE1_DESC, SW2_H_RESPONSE2_CCC, SW2_H_RESPONSE2_DESC,
    SW2_H_UNKNOWN_22_CCC, SW2_H_UNKNOWN_22_DESC, SW2_H_UNKNOWN_26_CCC, SW2_H_UNKNOWN_26_RATE,
#ifdef SW2_H_AUDIO_IN_CCC
    SW2_H_AUDIO_IN_CCC, SW2_H_AUDIO_IN_RATE,   // only when the audio attributes are built
#endif
};
static uint16_t s_rw_values[sizeof(k_rw_handles) / sizeof(k_rw_handles[0])];

// Controller state: main loop writes, BTstack context reads.
static volatile uint32_t s_in_seq;
static switch2_input_t   s_in_shared;
static uint32_t          s_last_buttons;   // main loop only (wake edge detect)

static uint8_t s_rumble_l, s_rumble_r;

static uint8_t s_adv_data[3 + 2 + SW2_MFR_DATA_LEN];   // exactly 31 bytes
// Bring-up knob: advertise a different controller product id (0 = leave the
// Pro Controller 2 default from switch2_proto). Set over CDC and held steady --
// unlike the rotating sweep this replaces, which never left any one identity on
// air long enough for a console to act on it.
static uint16_t s_pid_override;
// Scan response carrying the name, as espp does. A real pad sends none (its
// SCAN_RSP is AdvA only, and the console is a passive scanner that never asks),
// so this is a harmless difference rather than a gate -- espp pairs with it.
static const uint8_t k_scan_rsp[] = {
    15, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'P', 'r', 'o', ' ', 'C', 'o', 'n', 't', 'r', 'o', 'l', 'l', 'e', 'r',
};

static btstack_packet_callback_registration_t s_hci_cb;
static btstack_timer_source_t s_input_timer;

// A console that connects and then says nothing used to hold the link open
// forever: we stop advertising while connected and nothing tears a dead link
// down (BLE.DROP only walks the central's table, and btstack_host deliberately
// ignores our peripheral handle). One silent connection therefore made the
// adapter permanently invisible to the pairing screen. Give up on a link that
// produces no ATT traffic and no encryption, and go back on air.
// Liveness must not be judged by our read/write callbacks: BTstack answers
// service and characteristic discovery out of the static DB without ever calling
// them, so a central can enumerate our whole table while both counters stay 0.
// (Proved by connecting from a Mac: discovery was in flight when an earlier
// 8 s version of this watchdog killed the link.) The negotiated ATT MTU is the
// honest signal -- it stays at the 23-byte default until a peer actually talks.
#define SW2_STALL_TIMEOUT_MS 20000
static uint32_t s_conn_start_ms;
static uint16_t s_stall_drops;
static bool     s_drop_requested;
// Passive observers only: we never request these (see the connect handler), but if a
// controller negotiates a PHY or data-length change we record what it settled on.
static uint8_t  s_phy_tx, s_phy_rx;   // 1 = 1M, 2 = 2M; 0 = never negotiated
static uint16_t s_dle_tx;             // negotiated max tx octets
static void mtu_cb_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size);
static void reset_session(void);
static void adv_update(void);
static btstack_context_callback_registration_t s_sync_reg, s_wake_reg;
static volatile bool s_sync_queued, s_wake_queued;

// ---------------------------------------------------------------------------
// Bond persistence (BTstack TLV) + SM key installation
// ---------------------------------------------------------------------------

static const btstack_tlv_t *tlv(void **ctx)
{
    const btstack_tlv_t *impl = NULL;
    btstack_tlv_get_instance(&impl, ctx);
    return impl;
}

static void bond_load(void)
{
    void *ctx = NULL;
    const btstack_tlv_t *impl = tlv(&ctx);
    s_bonded = false;
    if (!impl) return;
    int n = impl->get_tag(ctx, TLV_TAG_SW2_BOND, (uint8_t *)&s_bond, sizeof(s_bond));
    s_bonded = (n == (int)sizeof(s_bond)) && s_bond.magic == BOND_MAGIC;
    if (s_bonded) {
        printf("[switch2] bonded to console %s\n", bd_addr_to_str(s_bond.peer_addr));
    }
}

static void bond_store(void)
{
    void *ctx = NULL;
    const btstack_tlv_t *impl = tlv(&ctx);
    if (impl) impl->store_tag(ctx, TLV_TAG_SW2_BOND, (const uint8_t *)&s_bond, sizeof(s_bond));
}

static int le_db_find(uint8_t type, const bd_addr_t addr)
{
    for (int i = 0; i < le_device_db_max_count(); i++) {
        int t = BD_ADDR_TYPE_UNKNOWN;
        bd_addr_t a;
        sm_key_t irk;
        le_device_db_info(i, &t, a, irk);
        if (t == type && memcmp(a, addr, 6) == 0) return i;
    }
    return -1;
}

// Make sure SM can answer the console's LTK request (EDIV/Rand = 0) with our key.
static void le_db_install(void)
{
    if (!s_bonded) return;
    sm_key_t ltk_db;
    reverse_128(s_bond.ltk, ltk_db);   // SM keeps keys big-endian and reverses for HCI
    int idx = le_db_find(s_bond.peer_type, s_bond.peer_addr);
    if (idx >= 0) {
        sm_key_t have;
        le_device_db_encryption_get(idx, NULL, NULL, have, NULL, NULL, NULL, NULL);
        if (memcmp(have, ltk_db, 16) == 0) return;
    } else {
        sm_key_t no_irk;
        memset(no_irk, 0, sizeof(no_irk));
        idx = le_device_db_add(s_bond.peer_type, s_bond.peer_addr, no_irk);
        if (idx < 0) { printf("[switch2] le_device_db full — console LTK not installed\n"); return; }
    }
    uint8_t zero_rand[8] = {0};
    le_device_db_encryption_set(idx, 0, zero_rand, ltk_db, 16, /*authenticated=*/1,
                                /*authorized=*/0, /*secure_connection=*/1);
}

static void bond_forget(void)
{
    if (s_bonded) {
        int idx = le_db_find(s_bond.peer_type, s_bond.peer_addr);
        if (idx >= 0) le_device_db_remove(idx);
    }
    void *ctx = NULL;
    const btstack_tlv_t *impl = tlv(&ctx);
    if (impl && impl->delete_tag) impl->delete_tag(ctx, TLV_TAG_SW2_BOND);
    memset(&s_bond, 0, sizeof(s_bond));
    s_bonded = false;
    s_wake_latched = false;
}

// ---------------------------------------------------------------------------
// Advertising
// ---------------------------------------------------------------------------

// The discovery advertisement is STATIC. Sniffer captures of a real Pro
// Controller 2 pairing with a real console (ndeadly/switch2_controller_research,
// captures/nrf52840) show one single payload repeated through the whole pairing
// -- 301 identical ADV_INDs -- byte-for-byte the same one it sends while idle.
// It carries no sync-button flag, no counter and no checksum, so there is
// nothing here to vary: the pad advertises one thing and waits. Two things
// tried here before that must not come back:
//
//   * Rotating VID/PID "identities" every few seconds. That puts the genuine
//     Pro 2 advertisement on air only a fraction of the time, and restarts the
//     advertiser on each rotation -- while the console can take ~26 s to act.
//   * Setting the wake flag while unbonded. Byte 0x0B = 0x81 appears only
//     together with the console's address at 0x0C; flag-set-with-zero-address
//     occurs in no capture, and the console dispatches on that flag.
//
// The console is also a PASSIVE scanner: in those captures it sends no SCAN_REQ
// at all, and its CONNECT_IND follows an ADV_IND by 0.53 ms. Scan requests
// counted below are therefore other hosts in the room, never the console -- do
// not read them as "the console is looking at us".

// Scan-request counters, incremented by the patched link layer (radio ISR),
// without flooding the diagnostic ring.
volatile uint32_t joypad_scan_req_count;
volatile uint32_t joypad_scan_req_last;
// Connection requests seen at the radio, counted before the link layer decides
// whether to accept one. A console that never appears here is filtering our
// advertisement; one that appears but never reaches ull_peripheral.c is being
// turned down locally, and joypad_conn_req_why says which check failed.
volatile uint32_t joypad_conn_req_count;
volatile uint32_t joypad_conn_req_last;
volatile uint32_t joypad_conn_req_why;
// Which swept identity is on air right now, so a connection request can be
// attributed to the advertisement that drew it.
volatile uint8_t joypad_adv_variant;
// Connection-establishment outcomes, so a link that dies between the link
// layer accepting a CONNECT_IND and BTstack reporting a connection is visible.
static uint16_t s_cc_count, s_cc_itvl;
static uint8_t  s_cc_status, s_cc_role;
static uint16_t s_disc_count; static uint8_t s_disc_reason;
// ATT traffic seen from the console: did it discover and use our GATT table?
static uint16_t s_att_reads, s_att_writes, s_att_last_read, s_att_last_write;
static uint16_t s_att_txn_cbs;   // prepared-write/transaction callbacks (handle 0)
// ATT-layer events. The read/write callbacks above only fire for DYNAMIC
// attribute values, so a console that discovers our whole table generates none
// of them -- their being zero says nothing about whether ATT is alive. An MTU
// exchange does: it proves the console opened ATT and talked to us.
static uint16_t s_att_events, s_att_mtu;

static void adv_update(void)
{
    // Unbonded: the one static discovery advertisement, always. The wake flag
    // needs the console's address beside it, which we do not have yet.
    switch2_adv_kind_t kind = !s_bonded ? SW2_ADV_PAIRING
                            : s_wake_latched ? SW2_ADV_WAKE : SW2_ADV_RECONNECT;
    s_adv_data[0] = 2;
    s_adv_data[1] = BLUETOOTH_DATA_TYPE_FLAGS;
    s_adv_data[2] = 0x06;   // LE general discoverable, BR/EDR not supported
    s_adv_data[3] = 1 + SW2_MFR_DATA_LEN;
    s_adv_data[4] = BLUETOOTH_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA;
    switch2_build_mfr_data(kind, s_bond.host_addr, &s_adv_data[5]);
    if (s_pid_override) {
        s_adv_data[5 + 7] = (uint8_t)(s_pid_override & 0xff);
        s_adv_data[5 + 8] = (uint8_t)(s_pid_override >> 8);
    }
    joypad_adv_variant = (uint8_t)kind;
    // Stop/set/start so a variant switch (reconnect -> wake) reliably goes on air.
    gap_advertisements_enable(0);
    gap_advertisements_set_data(sizeof(s_adv_data), s_adv_data);
    gap_advertisements_enable(1);
    printf("[switch2] advertising: %s\n",
           kind == SW2_ADV_PAIRING ? "pairing"
           : kind == SW2_ADV_WAKE ? "wake" : "reconnect");
}

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------

static void can_send_now(void *context);

static void request_send(void)
{
    if (s_con == HCI_CON_HANDLE_INVALID || s_send_requested) return;
    s_send_requested = true;
    s_send_reg.callback = &can_send_now;
    s_send_reg.context = NULL;
    att_server_request_to_send_notification(&s_send_reg, s_con);
}

static void snapshot_input(switch2_input_t *out)
{
    uint32_t seq;
    do {
        seq = s_in_seq;
        __sync_synchronize();
        *out = s_in_shared;
        __sync_synchronize();
    } while ((seq & 1u) || seq != s_in_seq);
}

static void can_send_now(void *context)
{
    (void)context;
    s_send_requested = false;
    if (s_con == HCI_CON_HANDLE_INVALID) return;

    if (s_rsp_count) {
        sw2_notification_t *n = &s_rsp[s_rsp_head];
        att_server_notify(s_con, n->handle, n->data, n->len);
        s_rsp_head = (uint8_t)((s_rsp_head + 1) % RSP_QUEUE_LEN);
        s_rsp_count--;
    } else if (s_input_due) {
        // Built at send time so the report carries the freshest state.
        switch2_input_t in;
        snapshot_input(&in);
        switch2_proto_build_input(&s_proto, &in, s_last_input);
        att_server_notify(s_con, SW2_H_INPUT, s_last_input, sizeof(s_last_input));
        s_input_due = false;
    }
    if (s_rsp_count || s_input_due) request_send();
}

static void queue_response(uint16_t handle, uint16_t prefix, const uint8_t *rsp, uint16_t len)
{
    if (s_rsp_count == RSP_QUEUE_LEN) {
        printf("[switch2] response queue full, dropping %02x/%02x\n", rsp[0], rsp[3]);
        return;
    }
    sw2_notification_t *n = &s_rsp[(s_rsp_head + s_rsp_count) % RSP_QUEUE_LEN];
    n->handle = handle;
    memset(n->data, 0, prefix);
    memcpy(&n->data[prefix], rsp, len);
    n->len = (uint16_t)(prefix + len);
    s_rsp_count++;
    request_send();
}

// Input pacing: one report per connection event, re-armed at the live interval
// (15 ms while pairing, 5 ms once the console tightens the link).
static void input_timer_handler(btstack_timer_source_t *ts)
{
    if (s_con == HCI_CON_HANDLE_INVALID) return;

    // Runs in run-loop context, so this is also where an externally requested
    // drop is executed -- callers only set the flag.
    if (s_drop_requested) {
        s_drop_requested = false;
        printf("[switch2] dropping console link on request\n");
        gap_disconnect(s_con);
        btstack_run_loop_set_timer(ts, 15);
        btstack_run_loop_add_timer(ts);
        return;
    }
    if (!s_encrypted && s_att_writes == 0 && s_att_reads == 0 &&
        att_server_get_mtu(s_con) <= ATT_DEFAULT_MTU &&
        (btstack_run_loop_get_time_ms() - s_conn_start_ms) > SW2_STALL_TIMEOUT_MS) {
        s_stall_drops++;
        printf("[switch2] console connected but silent for %ums — dropping to re-advertise\n",
               SW2_STALL_TIMEOUT_MS);
        gap_disconnect(s_con);
        // gap_disconnect does not always take: observed stall_drops climbing to 10
        // while only one DISCONNECTION_COMPLETE ever arrived, leaving s_con pointing
        // at a link that was already gone. We then believed we were connected, never
        // re-advertised, and went silent for good. After a couple of ignored
        // requests, tear the session down locally and get back on air.
        if ((s_stall_drops % 3) == 0) {
            printf("[switch2] disconnect ignored — forcing local teardown\n");
            s_con = HCI_CON_HANDLE_INVALID;
            btstack_run_loop_remove_timer(ts);
            reset_session();
            adv_update();
            return;
        }
        // Push the deadline out so a disconnect that does not complete cannot make
        // this log line (and the disconnect request) repeat every timer tick.
        s_conn_start_ms = btstack_run_loop_get_time_ms();
        btstack_run_loop_set_timer(ts, 15);
        btstack_run_loop_add_timer(ts);
        return;
    }
    if (s_encrypted && s_input_notify && !s_input_due) {
        s_input_due = true;
        request_send();
    }
    uint16_t units = gap_le_connection_interval(s_con);   // 1.25 ms units
    uint32_t ms = units ? (uint32_t)(units * 5u + 3u) / 4u : 15u;
    if (ms < 5) ms = 5;
    btstack_run_loop_set_timer(ts, ms);
    btstack_run_loop_add_timer(ts);
}

// ---------------------------------------------------------------------------
// Command channel
// ---------------------------------------------------------------------------

static void on_paired(void)
{
    // A different console may be taking over: drop the old key first.
    if (s_bonded && (s_bond.peer_type != s_peer_type || memcmp(s_bond.peer_addr, s_peer_addr, 6) != 0)) {
        int idx = le_db_find(s_bond.peer_type, s_bond.peer_addr);
        if (idx >= 0) le_device_db_remove(idx);
    }
    s_bond.magic = BOND_MAGIC;
    memcpy(s_bond.host_addr, s_proto.host_addr, 6);
    s_bond.peer_type = s_peer_type;
    memcpy(s_bond.peer_addr, s_peer_addr, 6);
    memcpy(s_bond.ltk, s_proto.ltk, 16);
    s_bonded = true;
    bond_store();
    le_db_install();

    // SM looked the console up at connect (and missed); look again so the LTK
    // request that follows this reply finds the key we just installed.
    hci_connection_t *conn = hci_connection_for_handle(s_con);
    if (conn) conn->sm_connection.sm_irk_lookup_state = IRK_LOOKUP_W4_READY;
    printf("[switch2] paired with console %s\n", bd_addr_to_str(s_peer_addr));
}

static void handle_command(const uint8_t *cmd, uint16_t len, uint16_t rsp_handle, uint16_t prefix)
{
    uint8_t rsp[SW2_MAX_RSP_LEN];
    uint8_t ev = SW2_EVT_NONE;
    uint16_t n = switch2_proto_handle_command(&s_proto, cmd, len, rsp, &ev);
    printf("[switch2] cmd %02x/%02x -> %s\n", cmd[0], len > 3 ? cmd[3] : 0, n ? "reply" : "none");
    if (n) queue_response(rsp_handle, prefix, rsp, n);
    if (ev & SW2_EVT_PAIRED) on_paired();
    if (ev & SW2_EVT_PLAYER_LED) {
        uint8_t player = switch2_player_from_leds(s_proto.player_leds);
        printf("[switch2] player %u (leds 0x%x)\n", player, s_proto.player_leds);
        if (player) feedback_set_led_player(0, player);
    }
}

static void handle_rumble(const uint8_t *lra32)
{
    // Commands on 0x0016 carry an all-zero LRA prefix (state byte 0): no rumble data,
    // not "stop" — only frames with a live state byte update the motors.
    if (lra32[0] == 0 && lra32[16] == 0) return;
    uint8_t l, r;
    switch2_rumble_decode(lra32, &l, &r);
    if (l == s_rumble_l && r == s_rumble_r) return;
    s_rumble_l = l;
    s_rumble_r = r;
    feedback_set_rumble(0, l, r);
}

static int att_write_cb(hci_con_handle_t con, uint16_t handle, uint16_t mode,
                        uint16_t offset, uint8_t *buf, uint16_t size)
{
    (void)con; (void)offset;
    // Counted so a console that connects and then goes quiet can be told apart
    // from one that is actually talking to our GATT table. Without this, a live
    // link with no pairing progress gives no clue whether the console ever
    // discovered our services or wrote a single command.
    if (mode != ATT_TRANSACTION_MODE_NONE) {
        // Prepared-write bookkeeping, not a console write. BTstack uses handle 0 with
        // VALIDATE/EXECUTE/CANCEL for transaction queues, and it fires CANCEL from
        // att_clear_transaction_queue() on every disconnect -- counting those as
        // writes made a console that never touched our GATT look like it had, and
        // overwrote s_att_last_write with 0. Count them apart.
        s_att_txn_cbs++;
        return 0;
    }
    s_att_writes++;
    s_att_last_write = handle;

    switch (handle) {
    case SW2_H_VIB_COMMAND:   // [00][L lra 16][R lra 16] + optional command
        if (size >= SW2_VIB_CMD_PREFIX_LEN) handle_rumble(&buf[1]);
        if (size >= SW2_VIB_CMD_PREFIX_LEN + SW2_CMD_HEADER_LEN)
            handle_command(&buf[SW2_VIB_CMD_PREFIX_LEN], (uint16_t)(size - SW2_VIB_CMD_PREFIX_LEN),
                           SW2_H_RESPONSE2, SW2_RSP2_PREFIX_LEN);
        return 0;
    case SW2_H_COMMAND:
        if (size >= SW2_CMD_HEADER_LEN) handle_command(buf, size, SW2_H_RESPONSE1, 0);
        return 0;
    case SW2_H_VIBRATION:
        if (size >= SW2_VIB_CMD_PREFIX_LEN) handle_rumble(&buf[1]);
        return 0;
    default:
        break;
    }

    for (size_t i = 0; i < sizeof(k_rw_handles) / sizeof(k_rw_handles[0]); i++) {
        if (k_rw_handles[i] != handle || size < 2) continue;
        s_rw_values[i] = little_endian_read_16(buf, 0);
        if (handle == SW2_H_INPUT_CCC) {
            s_input_notify = (s_rw_values[i] & 0x0001) != 0;
            printf("[switch2] input stream %s\n", s_input_notify ? "enabled" : "disabled");
        }
    }
    return 0;
}

static uint16_t att_read_cb(hci_con_handle_t con, uint16_t handle, uint16_t offset,
                            uint8_t *buf, uint16_t size)
{
    (void)con;
    s_att_reads++;
    s_att_last_read = handle;
    if (handle == SW2_H_INPUT || handle == SW2_H_COMMON_INPUT)
        return att_read_callback_handle_blob(s_last_input, sizeof(s_last_input), offset, buf, size);
    for (size_t i = 0; i < sizeof(k_rw_handles) / sizeof(k_rw_handles[0]); i++) {
        if (k_rw_handles[i] == handle)
            return att_read_callback_handle_little_endian_16(s_rw_values[i], offset, buf, size);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// HCI events
// ---------------------------------------------------------------------------

static void reset_session(void)
{
    s_encrypted = false;
    s_input_notify = false;
    s_input_due = false;
    s_send_requested = false;
    s_rsp_head = s_rsp_count = 0;
    memset(s_rw_values, 0, sizeof(s_rw_values));
    s_proto.pair_stage = 0;
    s_proto.features = 0;
    s_proto.counter = 0;
}

static void hci_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        // Claim the public address once the stack is actually up. Doing it in
        // late_init is too early: bt_transport_nrf re-applies random-static mode
        // when its Zephyr read-static-address command completes, which lands
        // after late_init and silently put us back on a random address — the
        // console ignores anything that isn't public.
        break;

    case HCI_EVENT_COMMAND_COMPLETE:
        // Did the controller accept our public address? A silent failure would
        // leave the nRF advertising from 00:00:00:00:00:00, which the console
        // ignores — and it looks identical to "the console didn't see us".
        {
            // Advertising can fail silently: BTstack queues the commands and
            // nothing checks the controller's status, so "advertising: pairing"
            // gets printed even when the radio refused. Surface the ones that
            // matter (set params 0x2006, set data 0x2008, enable 0x200A).
            uint16_t op = hci_event_command_complete_get_command_opcode(packet);
            if (op == 0x2006 || op == 0x2008 || op == 0x200A) {
                uint8_t st = hci_event_command_complete_get_return_parameters(packet)[0];
                if (st != ERROR_CODE_SUCCESS) {
                    printf("[switch2] adv cmd 0x%04x FAILED status 0x%02x\n", op, st);
                }
            }
        }
        break;

    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(packet) != GAP_SUBEVENT_LE_CONNECTION_COMPLETE) break;
        // Record EVERY connection-complete, including the failures. The link
        // layer counted a CONNECT_IND it accepted and yet nothing was logged
        // here, which means establishment failed after acceptance -- and the two
        // early breaks below threw that away silently. A non-zero status with no
        // trace is exactly the case worth seeing (0x3E = failed to establish).
        s_cc_count++;
        s_cc_status = gap_subevent_le_connection_complete_get_status(packet);
        s_cc_role   = gap_subevent_le_connection_complete_get_role(packet);
        s_cc_itvl   = gap_subevent_le_connection_complete_get_conn_interval(packet);
        if (s_cc_status != ERROR_CODE_SUCCESS || s_cc_role != HCI_ROLE_SLAVE) {
            printf("[switch2] connection NOT established: status 0x%02x role %u interval %u\n",
                   s_cc_status, s_cc_role, s_cc_itvl);
            break;
        }
        reset_session();
        s_con = gap_subevent_le_connection_complete_get_connection_handle(packet);
        s_peer_type = gap_subevent_le_connection_complete_get_peer_address_type(packet);
        gap_subevent_le_connection_complete_get_peer_address(packet, s_peer_addr);
        {
            // 0x15/0x01 must answer with the exact address the console connected to.
            uint8_t own_type;
            bd_addr_t own;
            gap_le_get_own_address(&own_type, own);
            reverse_bd_addr(own, s_proto.local_addr);
            uint16_t itvl = gap_subevent_le_connection_complete_get_conn_interval(packet);
            printf("[switch2] console %s (type %u) connected, interval %u.%02u ms\n",
                   bd_addr_to_str(s_peer_addr), s_peer_type,
                   itvl * 125u / 100u, (itvl * 125u) % 100u);
            printf("[switch2] our address %s (type %u — 0 public, 1 random)\n",
                   bd_addr_to_str(own), own_type);
        }
        s_conn_start_ms = btstack_run_loop_get_time_ms();
        s_drop_requested = false;

        // Nobody was starting the ATT MTU exchange on this link. The console will
        // not write its 0x15 pairing command at the default 23-byte MTU -- on nRF
        // the negotiated MTU reaches 512 and pairing proceeds, while on CYW43 it
        // stayed at 0 and the console connected, waited, and gave up. We are the
        // ATT *server* here, and a server never initiates the exchange, so send it
        // from our GATT client against our own peripheral link.
        gatt_client_send_mtu_negotiation(&mtu_cb_handler, s_con);

        // No link-layer upgrade requests here on purpose: the CYW43 reports LE 2M
        // PHY unsupported (feature bit 8 clear) and answers LE Read PHY with
        // Unknown HCI Command, while Zephyr's controller negotiates PHY and data
        // length by itself on nRF. See .dev/docs/switch2-ble-pico-w-verdict.md.
        btstack_run_loop_set_timer_handler(&s_input_timer, &input_timer_handler);
        btstack_run_loop_set_timer(&s_input_timer, 15);
        btstack_run_loop_add_timer(&s_input_timer);

        break;

    case HCI_EVENT_LE_META:
        if (hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_PHY_UPDATE_COMPLETE) {
            s_phy_tx = hci_subevent_le_phy_update_complete_get_tx_phy(packet);
            s_phy_rx = packet[6];   // no accessor for rx_phy in this BTstack
            printf("[switch2] PHY now tx=%uM rx=%uM (status 0x%02x)\n", s_phy_tx, s_phy_rx,
                   hci_subevent_le_phy_update_complete_get_status(packet));
            break;
        }
        if (hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_DATA_LENGTH_CHANGE) {
            s_dle_tx = hci_subevent_le_data_length_change_get_max_tx_octets(packet);
            printf("[switch2] data length tx=%u rx=%u octets\n", s_dle_tx,
                   hci_subevent_le_data_length_change_get_max_rx_octets(packet));
            break;
        }
        if (hci_event_le_meta_get_subevent_code(packet) == HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE &&
            hci_subevent_le_connection_update_complete_get_connection_handle(packet) == s_con) {
            // Key diagnostic: the console moves the link to 5 ms (interval 4). A radio that
            // can't hold it drops with a supervision timeout shortly after this line.
            uint16_t itvl = hci_subevent_le_connection_update_complete_get_conn_interval(packet);
            printf("[switch2] connection interval -> %u.%02u ms (status 0x%02x)\n",
                   itvl * 125u / 100u, (itvl * 125u) % 100u,
                   hci_subevent_le_connection_update_complete_get_status(packet));
        }
        break;

    case HCI_EVENT_ENCRYPTION_CHANGE:
    case HCI_EVENT_ENCRYPTION_CHANGE_V2:
        if (hci_event_encryption_change_get_connection_handle(packet) != s_con) break;
        s_encrypted = hci_event_encryption_change_get_status(packet) == ERROR_CODE_SUCCESS &&
                      hci_event_encryption_change_get_encryption_enabled(packet);
        printf("[switch2] encryption %s (status 0x%02x)\n", s_encrypted ? "ON" : "off",
               hci_event_encryption_change_get_status(packet));
        if (s_encrypted) s_wake_latched = false;   // session is up; stop waking
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE:
        // Counted for every handle, not just ours: a link torn down before we
        // ever adopted it would otherwise leave no trace at all.
        s_disc_count++;
        s_disc_reason = hci_event_disconnection_complete_get_reason(packet);
        if (hci_event_disconnection_complete_get_connection_handle(packet) != s_con) {
            printf("[switch2] disconnect on other handle (reason 0x%02x)\n", s_disc_reason);
            break;
        }
        printf("[switch2] console disconnected (reason 0x%02x)\n", s_disc_reason);
        s_con = HCI_CON_HANDLE_INVALID;
        btstack_run_loop_remove_timer(&s_input_timer);
        reset_session();
        if (s_rumble_l || s_rumble_r) { s_rumble_l = s_rumble_r = 0; feedback_set_rumble(0, 0, 0); }
        adv_update();
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Main-loop -> BTstack requests
// ---------------------------------------------------------------------------


static void do_sync(void *ctx)
{
    (void)ctx;
    s_sync_queued = false;
    printf("[switch2] SYNC: forgetting console, back to pairing advertising\n");
    bool was_connected = s_con != HCI_CON_HANDLE_INVALID;
    bond_forget();
    if (was_connected) gap_disconnect(s_con);   // re-advertises from the disconnect event
    else adv_update();
}

static void do_wake(void *ctx)
{
    (void)ctx;
    s_wake_queued = false;
    if (s_con != HCI_CON_HANDLE_INVALID || s_wake_latched) return;
    // Wakes a sleeping console we are bonded to. Unbonded there is nothing to
    // wake and no address to name, and adv_update keeps sending the plain
    // discovery advertisement regardless — see the note above it.
    if (!s_bonded) return;
    s_wake_latched = true;
    adv_update();
}

void switch2_ble_request_sync(void)
{
    if (s_sync_queued) return;
    s_sync_queued = true;
    s_sync_reg.callback = &do_sync;
    s_sync_reg.context = NULL;
    btstack_run_loop_execute_on_main_thread(&s_sync_reg);
}

static void request_wake(void)
{
    if (s_wake_queued) return;
    s_wake_queued = true;
    s_wake_reg.callback = &do_wake;
    s_wake_reg.context = NULL;
    btstack_run_loop_execute_on_main_thread(&s_wake_reg);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------



void switch2_ble_init(void)
{
    switch2_proto_init(&s_proto, NULL);
    memset(&s_in_shared, 0, sizeof(s_in_shared));
    s_in_shared.lx = s_in_shared.ly = s_in_shared.rx = s_in_shared.ry = 128;
    s_in_shared.battery_pct = 100;
}

// ATT-layer events, so "the console connected and did nothing" can be told from
// "the console discovered us and then stopped". An MTU exchange is the first
// thing a real console does over ATT.
// Result of the MTU exchange we initiate on connect. GATT_EVENT_MTU carries the
// negotiated value; record it so SWITCH2.SCAN shows a real number even when the
// att_server packet handler does not see the exchange.
static void mtu_cb_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != GATT_EVENT_MTU) return;
    s_att_mtu = gatt_event_mtu_get_MTU(packet);
    printf("[switch2] ATT MTU negotiated: %u\n", s_att_mtu);
}

static void att_event_handler(uint8_t type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    s_att_events++;
    if (hci_event_packet_get_type(packet) == ATT_EVENT_MTU_EXCHANGE_COMPLETE) {
        s_att_mtu = att_event_mtu_exchange_complete_get_MTU(packet);
        printf("[switch2] ATT MTU exchange complete: %u\n", s_att_mtu);
    }
}

#ifdef CONFIG_SWITCH2_ACL_TRACE
// Raw ACL trace. The ATT callbacks only fire for attributes we serve, and the ATT
// event handler only sees events -- so a console whose requests BTstack rejects
// below us is indistinguishable from a console that sends nothing at all. This
// logs every ACL packet in each direction, which settles that: if the console is
// talking, its ATT opcodes appear here even when nothing reaches our handlers.
// The console is expected to write the 0x15 pairing command straight to fixed
// handle 0x0016 without discovering (per esp-cpp/espp, verified against hardware).
static void acl_trace_packet(uint8_t packet_type, uint8_t in, uint8_t *packet, uint16_t len)
{
    // Positive control: prove the dump is wired at all. HCI events always flow, so
    // if these first lines never appear, the silence is our plumbing, not the console.
    // Reset per connection: CDC logging is only armed a few seconds after boot, so a
    // boot-time-only window is always missed.
    static uint16_t s_dump_gen, s_dump_seen;
    if (s_dump_gen != s_cc_count) { s_dump_gen = s_cc_count; s_dump_seen = 0; }
    if (s_dump_seen < 12) {
        s_dump_seen++;
        printf("[DUMP%s] type=%u len=%u b0=%02x\n", in ? "<-" : "->", packet_type, len,
               len ? packet[0] : 0);
    }
    if (packet_type != HCI_ACL_DATA_PACKET) return;
    if (len < 9) return;
    // ACL header 4 + L2CAP header 4; CID 0x0004 is ATT. Print EVERY CID: a trace that
    // only shows ATT cannot distinguish "the console sent nothing" from "the console
    // sent something on another channel" (or from the dump not being wired at all).
    uint16_t cid = (uint16_t)packet[6] | ((uint16_t)packet[7] << 8);
    printf("[ACL%s cid=%04x] op=0x%02x len=%u:", in ? "<-" : "->", cid, packet[8], len - 8);
    uint16_t n = len - 8; if (n > 20) n = 20;
    for (uint16_t i = 0; i < n; i++) printf(" %02x", packet[8 + i]);
    printf("\n");
}
static void acl_trace_msg(int level, const char *fmt, va_list ap) { (void)level; (void)fmt; (void)ap; }
static const hci_dump_t acl_trace = { NULL, acl_trace_packet, acl_trace_msg };
#endif

void switch2_ble_late_init(void)
{
    printf("[switch2] Switch 2 Pro Controller BLE mode\n");
#ifdef CONFIG_SWITCH2_ACL_TRACE
    hci_dump_init(&acl_trace);
    printf("[switch2] ACL/ATT trace enabled\n");
#endif
    // We are a controller now: stop the BLE-central scan and the Classic inquiry
    // fighting for the radio, and keep them stopped. A plain suppress_scan() is
    // cleared again by the idle safety net / BLE.DROP holdoff / timed scans, which
    // is how a Pico W ended up running GIAC inquiry and 4s gap_connect retries to a
    // bonded pad while the console was trying to pair with us.
    if (btstack_host_suppress_scan_latched) btstack_host_suppress_scan_latched(true);

    // The console drives a 5 ms connection interval (4 units), below the 7.5 ms
    // Bluetooth floor, so the patched link layer has to allow it -- but ONLY for
    // this mode. Allowing it binary-wide also loosened the central path and the
    // link layer asserted there while connecting to an ordinary BLE controller.
    // Safe to scope this way because we just latched the central's scan off: the
    // two are never active at once.
    if (&joypad_le_interval_min != NULL) joypad_le_interval_min = 4;

    l2cap_init();
    sm_init();

    // ATT MTU is raised to 512 for every mode in btstack_host.c's init (which runs
    // after this), so there is nothing to override here. A real Pro Controller 2
    // negotiates 512 and the console will not pair at 247.
    // The console does its own pairing over 0x15; SM only answers its LTK request.
    // Never send a Security Request — a real pad doesn't and the console drops the link.
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);

    att_server_init(switch2_gatt_db, att_read_cb, att_write_cb);
    att_server_register_packet_handler(&att_event_handler);

    s_hci_cb.callback = &hci_handler;
    hci_add_event_handler(&s_hci_cb);

    bond_load();
    le_db_install();
    // Address note (nRF52840): the console stores our address during 0x15 and
    // matches it on reconnect, so it must be STABLE across boots. A real pad
    // uses a PUBLIC address, and bt_transport_nrf now claims one at HCI init
    // via the BTstack chipset hook (HCI_VS_Write_BD_ADDR, 0xFC06, which
    // Zephyr's link layer implements as ll_addr_set). Writing it from here
    // instead left advertising silently off-air while every advertising
    // command still reported success -- it has to happen during init, before
    // the transport re-applies random-static addressing.
    bd_addr_t null_addr;
    memset(null_addr, 0, sizeof(null_addr));
    gap_advertisements_set_params(ADV_INTERVAL, ADV_INTERVAL, 0 /* ADV_IND */, 0, null_addr, 0x07, 0x00);
    gap_scan_response_set_data(sizeof(k_scan_rsp), (uint8_t *)k_scan_rsp);
    adv_update();
}

void switch2_ble_task(void)
{
    const input_event_t *ev = router_get_output(OUTPUT_TARGET_BLE_PERIPHERAL, 0);
    if (!ev) return;

    switch2_input_t in;
    in.buttons = ev->buttons;
    in.lx = ev->analog[ANALOG_LX];
    in.ly = ev->analog[ANALOG_LY];
    in.rx = ev->analog[ANALOG_RX];
    in.ry = ev->analog[ANALOG_RY];
    in.l2 = ev->analog[ANALOG_L2];
    in.r2 = ev->analog[ANALOG_R2];
    int pct = router_onboard_battery_percent();
    in.battery_pct = (pct < 0) ? 100 : (uint8_t)pct;
    in.charging = false;

    s_in_seq++;
    __sync_synchronize();
    s_in_shared = in;
    __sync_synchronize();
    s_in_seq++;

    // Like a real pad, a button press while disconnected raises the 0x81 wake
    // flag, which powers a sleeping console back on. It means nothing before a
    // bond exists, so do_wake ignores it then.
    if (in.buttons && !s_last_buttons && s_con == HCI_CON_HANDLE_INVALID)
        request_wake();
    s_last_buttons = in.buttons;
}

// The Pro Controller 2's public address. OUI 94:8E:6D was taken off the air from a
// real pad that pairs with this console; a sniffer capture put our advertisement and
// that pad's side by side and the address was the ONLY difference in the whole
// packet. What provably matters: it must be PUBLIC and stable across boots, because
// the console bonds to it during 0x15. Suffix from the board id so each dongle is
// unique. (bt_transport_nrf.c has a twin that derives the suffix from FICR before
// platform init is available -- keep the OUI in sync.)
// Set by the transport when it asks the controller to take our public address, so
// SWITCH2.SCAN can show the requested identity next to the live one. Equal values
// mean the claim worked; differing values mean the controller refused it; all-zero
// means the claim never ran at all.
static bd_addr_t s_claimed_addr;

void switch2_ble_note_claimed_addr(const bd_addr_t addr)
{
    memcpy(s_claimed_addr, addr, 6);
}

void switch2_ble_get_claimed_addr(bd_addr_t addr)
{
    memcpy(addr, s_claimed_addr, 6);
}

void switch2_ble_get_public_addr(bd_addr_t addr)
{
    uint8_t id[8] = {0};
    platform_get_unique_id(id, sizeof(id));
    addr[0] = 0x94; addr[1] = 0x8E; addr[2] = 0x6D;
    addr[3] = id[5] ^ id[1];
    addr[4] = id[6] ^ id[2];
    addr[5] = id[7] ^ id[3];
    if ((addr[3] | addr[4] | addr[5]) == 0) {
        addr[3] = 0x2D; addr[4] = 0xF2; addr[5] = 0x29;
    }
#ifdef CONFIG_SWITCH2_ADDR_SALT
    // Bring-up only: a console that connected once and failed to pair will not
    // re-attempt the same address, so a fresh identity is the only way to get
    // another connection attempt without clearing its side.
    addr[5] ^= (uint8_t)CONFIG_SWITCH2_ADDR_SALT;
    addr[4] ^= (uint8_t)(CONFIG_SWITCH2_ADDR_SALT >> 8);
#endif
}

// Ask for the console link to be torn down. Safe from any context: the actual
// gap_disconnect happens in the input timer, which runs in run-loop context.
void switch2_ble_drop_link(void)
{
    if (s_con != HCI_CON_HANDLE_INVALID) s_drop_requested = true;
}

uint16_t switch2_ble_get_stall_drops(void)
{
    return s_stall_drops;
}

uint16_t switch2_ble_get_att_txn_count(void)
{
    return s_att_txn_cbs;
}

void switch2_ble_get_link_phy(uint8_t *tx_phy, uint8_t *rx_phy, uint16_t *dle_tx)
{
    *tx_phy = s_phy_tx; *rx_phy = s_phy_rx; *dle_tx = s_dle_tx;
}

void switch2_ble_set_pid(uint16_t pid)
{
    s_pid_override = pid;
    adv_update();
}

void switch2_ble_get_link_debug(uint16_t *cc_count, uint8_t *cc_status,
                                uint8_t *cc_role, uint16_t *cc_itvl,
                                uint16_t *disc_count, uint8_t *disc_reason)
{
    *cc_count = s_cc_count; *cc_status = s_cc_status; *cc_role = s_cc_role;
    *cc_itvl = s_cc_itvl;   *disc_count = s_disc_count; *disc_reason = s_disc_reason;
}

void switch2_ble_get_att_debug(uint16_t *reads, uint16_t *writes,
                               uint16_t *last_read, uint16_t *last_write,
                               uint8_t *pair_stage, uint8_t *encrypted)
{
    *reads = s_att_reads; *writes = s_att_writes;
    *last_read = s_att_last_read; *last_write = s_att_last_write;
    *pair_stage = s_proto.pair_stage;
    *encrypted = s_encrypted ? 1 : 0;
}

void switch2_ble_get_att_events(uint16_t *events, uint16_t *mtu)
{
    *events = s_att_events; *mtu = s_att_mtu;
}

uint8_t switch2_ble_get_adv_debug(uint8_t addr_out[6], uint8_t *addr_mode,
                                  uint8_t *adv_out, uint8_t adv_max)
{
    bd_addr_t local;
    gap_local_bd_addr(local);
    memcpy(addr_out, local, 6);
    *addr_mode = (uint8_t)gap_random_address_get_mode();
    uint8_t n = sizeof(s_adv_data) < adv_max ? (uint8_t)sizeof(s_adv_data) : adv_max;
    memcpy(adv_out, s_adv_data, n);
    return n;
}

bool switch2_ble_is_connected(void)
{
    return s_con != HCI_CON_HANDLE_INVALID && s_encrypted;
}

#endif // CONFIG_SWITCH2_BLE_OUTPUT
