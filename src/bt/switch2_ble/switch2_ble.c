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
#include "l2cap.h"
#include "ble/att_db.h"
#include "ble/att_server.h"
#include "ble/le_device_db.h"
#include "ble/sm.h"

#include <stdio.h>
#include <string.h>

// Forward declarations (feedback.h drags in TinyUSB types on some builds)
extern void feedback_set_rumble(uint8_t player_index, uint8_t left, uint8_t right);
extern void feedback_set_led_player(uint8_t player_index, uint8_t player_num);
// Central-side BT host; absent on peripheral-only builds (ESP32 universal).
extern void btstack_host_suppress_scan(bool suppress) __attribute__((weak));

#define TLV_TAG_SW2_BOND   (((uint32_t)'S' << 24) | ((uint32_t)'W' << 16) | ((uint32_t)'2' << 8) | 'B')
#define BOND_MAGIC         0xB2
#define RSP_QUEUE_LEN      4
#define RSP_MAX            (SW2_RSP2_PREFIX_LEN + SW2_MAX_RSP_LEN)
#define ADV_INTERVAL       0x0030   // 30 ms, like a real pad

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
    SW2_H_AUDIO_IN_CCC, SW2_H_AUDIO_IN_RATE,
};
static uint16_t s_rw_values[sizeof(k_rw_handles) / sizeof(k_rw_handles[0])];

// Controller state: main loop writes, BTstack context reads.
static volatile uint32_t s_in_seq;
static switch2_input_t   s_in_shared;
static uint32_t          s_last_buttons;   // main loop only (wake edge detect)

static uint8_t s_rumble_l, s_rumble_r;

static uint8_t s_adv_data[3 + 2 + SW2_MFR_DATA_LEN];   // exactly 31 bytes
static const uint8_t k_scan_rsp[] = {
    15, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'P', 'r', 'o', ' ', 'C', 'o', 'n', 't', 'r', 'o', 'l', 'l', 'e', 'r',
};

static btstack_packet_callback_registration_t s_hci_cb;
static btstack_timer_source_t s_input_timer;
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

static void adv_update(void)
{
    switch2_adv_kind_t kind = !s_bonded ? SW2_ADV_PAIRING
                            : s_wake_latched ? SW2_ADV_WAKE : SW2_ADV_RECONNECT;
    s_adv_data[0] = 2;
    s_adv_data[1] = BLUETOOTH_DATA_TYPE_FLAGS;
    s_adv_data[2] = 0x06;   // LE general discoverable, BR/EDR not supported
    s_adv_data[3] = 1 + SW2_MFR_DATA_LEN;
    s_adv_data[4] = BLUETOOTH_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA;
    switch2_build_mfr_data(kind, s_bond.host_addr, &s_adv_data[5]);
    // Stop/set/start so a variant switch (reconnect -> wake) reliably goes on air.
    gap_advertisements_enable(0);
    gap_advertisements_set_data(sizeof(s_adv_data), s_adv_data);
    gap_advertisements_enable(1);
    printf("[switch2] advertising: %s\n",
           kind == SW2_ADV_PAIRING ? "pairing" : kind == SW2_ADV_WAKE ? "wake" : "reconnect");
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
    if (mode != ATT_TRANSACTION_MODE_NONE) return 0;

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
    case HCI_EVENT_META_GAP:
        if (hci_event_gap_meta_get_subevent_code(packet) != GAP_SUBEVENT_LE_CONNECTION_COMPLETE) break;
        if (gap_subevent_le_connection_complete_get_status(packet) != ERROR_CODE_SUCCESS) break;
        if (gap_subevent_le_connection_complete_get_role(packet) != HCI_ROLE_SLAVE) break;
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
            printf("[switch2] console %s connected (type %u), interval %u.%02u ms, we are %s (type %u)\n",
                   bd_addr_to_str(s_peer_addr), s_peer_type, itvl * 125u / 100u, (itvl * 125u) % 100u,
                   bd_addr_to_str(own), own_type);
        }
        btstack_run_loop_set_timer_handler(&s_input_timer, &input_timer_handler);
        btstack_run_loop_set_timer(&s_input_timer, 15);
        btstack_run_loop_add_timer(&s_input_timer);
        break;

    case HCI_EVENT_LE_META:
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
        if (hci_event_disconnection_complete_get_connection_handle(packet) != s_con) break;
        printf("[switch2] console disconnected (reason 0x%02x)\n",
               hci_event_disconnection_complete_get_reason(packet));
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
    if (!s_bonded || s_con != HCI_CON_HANDLE_INVALID || s_wake_latched) return;
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

void switch2_ble_late_init(void)
{
    printf("[switch2] Switch 2 Pro Controller BLE mode\n");
    // We are a controller now: stop the BLE-central scan fighting for the radio.
    if (btstack_host_suppress_scan) btstack_host_suppress_scan(true);

    l2cap_init();
    sm_init();
    // The console does its own pairing over 0x15; SM only answers its LTK request.
    // Never send a Security Request — a real pad doesn't and the console drops the link.
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);

    att_server_init(switch2_gatt_db, att_read_cb, att_write_cb);

    s_hci_cb.callback = &hci_handler;
    hci_add_event_handler(&s_hci_cb);

    bond_load();
    le_db_install();

    // Public address (never the per-mode static random ble_output uses): the console
    // stores it during 0x15 and matches it on reconnect.
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

    // Like a real pad: pressing a button while bonded + disconnected wakes the console.
    if (in.buttons && !s_last_buttons && s_bonded && s_con == HCI_CON_HANDLE_INVALID)
        request_wake();
    s_last_buttons = in.buttons;
}

bool switch2_ble_is_connected(void)
{
    return s_con != HCI_CON_HANDLE_INVALID && s_encrypted;
}

#endif // CONFIG_SWITCH2_BLE_OUTPUT
