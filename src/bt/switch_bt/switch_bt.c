// switch_bt.c - Switch Pro Controller BT Classic HID-device role (see switch_bt.h)
// SPDX-License-Identifier: Apache-2.0
//
// The BTstack Classic HID-device transport for the switch_proto engine. Structure
// follows BTstack's hid_*_demo device pattern; the Pro Controller HID report
// descriptor below is the published hardware report map (dekuNukem; identical across
// every Pro Controller and emulator — a functional fact, not copied code).

#include "switch_bt.h"

#ifdef CONFIG_BT_CLASSIC_OUTPUT

#include "switch_proto.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "platform/platform.h"

#include "btstack.h"
#include "classic/hid_device.h"
#include "classic/device_id_server.h"

#include "pico/rand.h"
#include <stdio.h>
#include <string.h>

// Nintendo identity the Switch matches on.
#define SW_VID        0x057E
#define SW_PID        0x2009
#define SW_COD        0x002508   // peripheral / gamepad
#define SW_NAME       "Pro Controller"
#define SEND_INTERVAL_MS 16      // ~60 Hz report cadence (Pro Controller default)


// Published Pro Controller BT HID report descriptor (134 bytes): input reports
// 0x21/0x30 (48B vendor) + 0x3F (basic HID), output reports 0x01/0x10 (48B vendor).
static const uint8_t k_hid_descriptor[] = {
    0x05,0x01, 0x09,0x05, 0xA1,0x01,
        0x85,0x21, 0x06,0x01,0xFF, 0x09,0x21, 0x15,0x00, 0x25,0x00, 0x75,0x08, 0x95,0x30, 0x81,0x02,
        0x85,0x30, 0x09,0x30, 0x15,0x00, 0x25,0x00, 0x75,0x08, 0x95,0x30, 0x81,0x02,
        0x85,0x3F, 0x05,0x09, 0x19,0x01, 0x29,0x10, 0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x10, 0x81,0x02,
        0x05,0x01, 0x09,0x39, 0x15,0x00, 0x25,0x07, 0x75,0x04, 0x95,0x01, 0x81,0x42,
        0x75,0x04, 0x95,0x01, 0x81,0x03,
        0x09,0x30, 0x09,0x31, 0x09,0x33, 0x09,0x34, 0x15,0x00, 0x27,0xFF,0xFF,0x00,0x00, 0x75,0x10, 0x95,0x04, 0x81,0x02,
        0x85,0x01, 0x06,0x01,0xFF, 0x09,0x01, 0x15,0x00, 0x27,0xFF,0xFF,0x00,0x00, 0x75,0x08, 0x95,0x30, 0x91,0x02,
        0x85,0x10, 0x09,0x10, 0x15,0x00, 0x27,0xFF,0xFF,0x00,0x00, 0x75,0x08, 0x95,0x30, 0x91,0x02,
    0xC0
};

static switch_proto_t   s_proto;
static switch_input_t   s_input;
static uint16_t         s_hid_cid;
static bool             s_connected;

static uint8_t          s_hid_sdp[300];
static uint8_t          s_devid_sdp[100];
static btstack_packet_callback_registration_t s_hci_cb;
static btstack_timer_source_t s_send_timer;   // report pump, runs in BTstack context
static btstack_timer_source_t s_reconnect_timer; // pages the bonded console while idle
static bd_addr_t              s_console_addr;   // last/paired console (page target)
static bool                   s_have_console;   // s_console_addr is valid
#define RECONNECT_INTERVAL_MS 5000               // how often we re-page while disconnected
// Persistent handshake counters — dumped periodically so we can see what happened
// during a connection even though the live CDC log drops out under BT load.
static uint32_t s_stat_connects, s_stat_tx, s_stat_rx;
static uint8_t  s_stat_last_rx_sub = 0xff;

static void reconnect_timer_start(void);   // defined below; started once the radio is up

// Apply the Pro Controller Classic identity (name + gamepad CoD). Called from the
// BT-host HCI_STATE_WORKING handler — the authoritative point after the HCI
// controller is up, so it isn't clobbered by the host's default identity. NOTE:
// EIR / limited-IAC / BD_ADDR-change were tried and removed — general discoverable
// (BTstack default) + this identity is HOJA's documented first-pairing config, and
// the CYW43 BD_ADDR can't be changed at runtime anyway.
// Write Current IAC LAP with a single IAC (opcode 0x0C3A; format "13" = num=1 + one
// 3-byte LAP). A real Pro Controller in sync mode is LIMITED discoverable, so it only
// answers the Switch's limited inquiry (and a phone's general scan can't see it).
static const hci_cmd_t sw_write_iac_lap_one = {
    HCI_OPCODE_HCI_WRITE_CURRENT_IAC_LAP_TWO_IACS, "13"
};

void switch_bt_apply_gap_identity(void)
{
    gap_set_local_name(SW_NAME);
    gap_set_class_of_device(SW_COD);
    // Stay connectable AND discoverable: connectable so a bonded Switch can page us
    // back on wake, discoverable so a new one finds us on Change Grip/Order. We do
    // NOT delete link keys here anymore — wiping the bond on every boot is what made
    // reconnection flaky (the Switch believed it was still paired and its silent
    // reconnect failed against our forgotten key). Re-pairing to a fresh/reset console
    // is now an explicit user action: switch_bt_request_sync() (single button click)
    // clears the bond and re-advertises, the firmware equivalent of the sync button.
    gap_connectable_control(1);
    gap_discoverable_control(1);
    // If we already hold a bond (persisted in flash), start paging that console right
    // away so a power-cycle reconnects on its own — no Change Grip/Order needed.
    reconnect_timer_start();
    (void)sw_write_iac_lap_one;
}

// ---- user-button "sync" (re-pair) ------------------------------------------------
// switch_bt_request_sync() is called from the main loop (button callback), but every
// BTstack/GAP call must run in the run-loop context — so we marshal the work across
// with execute_on_main_thread. do_sync() forgets the current bond and re-advertises,
// so the Switch's Change Grip/Order screen sees us as a brand-new controller. Press
// L+R on the console afterwards to complete the fresh SSP pair.
static btstack_context_callback_registration_t s_sync_cb;
static void do_sync(void* ctx)
{
    (void)ctx;
    printf("[switch_bt] SYNC: dropping bond + re-advertising for fresh pairing\n");
    if (s_connected) hid_device_disconnect(s_hid_cid);  // drop stale link so it re-pairs
    gap_delete_all_link_keys();
    s_have_console = false;   // forget the page target; wait for a fresh pair instead
    gap_connectable_control(1);
    gap_discoverable_control(1);
}

// ---- auto-reconnect (device pages the bonded console) ----------------------------
// A real Pro Controller re-pages its last console on wake rather than waiting to be
// paged — so once paired, you never revisit Change Grip/Order. We do the same: while
// disconnected and holding a bond, page the console every RECONNECT_INTERVAL_MS. The
// target is the console we last connected to this session, or (after a cold boot) the
// first entry in the flash-backed Classic link-key DB. Runs in BTstack context.
static bool reconnect_target(bd_addr_t out)
{
    if (s_have_console) { memcpy(out, s_console_addr, sizeof(bd_addr_t)); return true; }
    btstack_link_key_iterator_t it;
    if (!gap_link_key_iterator_init(&it)) return false;
    bd_addr_t addr; link_key_t key; link_key_type_t type;
    bool found = gap_link_key_iterator_get_next(&it, addr, key, &type);
    gap_link_key_iterator_done(&it);
    if (found) { memcpy(s_console_addr, addr, sizeof(bd_addr_t)); s_have_console = true;
                 memcpy(out, addr, sizeof(bd_addr_t)); }
    return found;
}
static void reconnect_timer_handler(btstack_timer_source_t* ts)
{
    if (!s_connected) {
        bd_addr_t target;
        if (reconnect_target(target)) {
            printf("[switch_bt] reconnect: paging %s\n", bd_addr_to_str(target));
            hid_device_connect(target, &s_hid_cid);
        }
    }
    btstack_run_loop_set_timer(ts, RECONNECT_INTERVAL_MS);
    btstack_run_loop_add_timer(ts);
}
static void reconnect_timer_start(void)
{
    btstack_run_loop_remove_timer(&s_reconnect_timer);   // no-op if not queued
    btstack_run_loop_set_timer_handler(&s_reconnect_timer, reconnect_timer_handler);
    btstack_run_loop_set_timer(&s_reconnect_timer, RECONNECT_INTERVAL_MS);
    btstack_run_loop_add_timer(&s_reconnect_timer);
}

void switch_bt_request_sync(void)
{
    s_sync_cb.callback = &do_sync;
    s_sync_cb.context  = NULL;
    btstack_run_loop_execute_on_main_thread(&s_sync_cb);
}

// ---- engine hooks -----------------------------------------------------------------
static void hook_set_player(void* c, uint8_t player, uint8_t mask)
{ (void)c; (void)mask; printf("[switch_bt] player LED -> P%u\n", player); }
static void hook_set_imu(void* c, bool en)
{ (void)c; printf("[switch_bt] IMU %s\n", en ? "on" : "off"); }
static void hook_set_rumble(void* c, uint8_t l, uint8_t r)
{ (void)c; (void)l; (void)r; }
static void hook_store_pairing(void* c, const uint8_t mac[6], const uint8_t* key)
{ (void)c; (void)mac; (void)key; }  // BTstack owns the BR/EDR link key; nothing to persist here yet
static void hook_random(void* c, uint8_t* out, size_t n)
{ (void)c; for (size_t i = 0; i < n; i += 4) { uint32_t r = get_rand_32(); size_t k = n-i<4?n-i:4; memcpy(out+i,&r,k); } }
static void hook_shutdown(void* c)
{ (void)c; printf("[switch_bt] host requested HCI shutdown\n"); }

static const switch_proto_hooks_t s_hooks = {
    .set_player = hook_set_player, .set_imu = hook_set_imu, .set_rumble = hook_set_rumble,
    .store_pairing = hook_store_pairing, .random_bytes = hook_random, .shutdown = hook_shutdown,
};

// ---- incoming host reports (OUT: 0x01 rumble+subcmd, 0x10 rumble) ------------------
// hid_device passes the report payload without the leading report-id byte, so we
// rebuild [id | payload] for the engine (which keys off data[0]).
static void feed_output(uint16_t report_id, int size, uint8_t* report, const char* via)
{
    if (size < 0 || size > 62) return;
    uint8_t buf[64];
    buf[0] = (uint8_t)report_id;
    if (size) memcpy(&buf[1], report, (size_t)size);
    // Log received host reports: for 0x01 the subcommand id is at data[10] (=report[9]).
    uint8_t sub = (report_id == SW_OUT_ID_RUMBLE_SUBCMD && size >= 10) ? report[9] : 0xff;
    s_stat_rx++; if (sub != 0xff) s_stat_last_rx_sub = sub;
    printf("[switch_bt] RX %s id=0x%02x sub=0x%02x len=%d\n", via, report_id, sub, size + 1);
    switch_proto_handle_output(&s_proto, buf, (uint16_t)(size + 1));
    if (s_connected && s_proto.reply_pending)
        hid_device_request_can_send_now_event(s_hid_cid);
}
static void report_data_cb(uint16_t cid, hid_report_type_t type, uint16_t report_id,
                           int size, uint8_t* report)
{ (void)cid; (void)type; feed_output(report_id, size, report, "DATA"); }
// Some hosts push output reports via SET_REPORT on the control channel — handle those too.
static void set_report_cb(uint16_t cid, hid_report_type_t type, int size, uint8_t* report)
{
    (void)cid; (void)type;
    if (size < 1) return;
    feed_output(report[0], size - 1, report + 1, "SETREP");
}

// Report pump — runs in the BTstack run-loop context (NOT the main loop). Requesting
// a can-send-now from the main loop races the CYW43 async BTstack context and stalls
// everything; a BTstack timer is the correct place. Re-arms itself while connected.
static void send_timer_handler(btstack_timer_source_t* ts)
{
    if (!s_connected) return;
    hid_device_request_can_send_now_event(s_hid_cid);
    btstack_run_loop_set_timer(ts, SEND_INTERVAL_MS);
    btstack_run_loop_add_timer(ts);
}

static void packet_handler(uint8_t type, uint16_t channel, uint8_t* packet, uint16_t size)
{
    (void)channel; (void)size;
    if (type != HCI_EVENT_PACKET) return;
    switch (hci_event_packet_get_type(packet)) {
    case HCI_EVENT_HID_META:
        switch (hci_event_hid_meta_get_subevent_code(packet)) {
        case HID_SUBEVENT_CONNECTION_OPENED:
            if (hid_subevent_connection_opened_get_status(packet) != ERROR_CODE_SUCCESS) {
                s_connected = false; break;
            }
            s_hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
            s_connected = true;
            s_proto.report_mode = SW_MODE_SIMPLE;   // fresh session; host will promote
            s_stat_connects++;
            // Remember this console as the page target for future auto-reconnects.
            hid_subevent_connection_opened_get_bd_addr(packet, s_console_addr);
            s_have_console = true;
            btstack_run_loop_remove_timer(&s_reconnect_timer);   // connected; stop paging
            printf("[switch_bt] Switch connected (cid=0x%04x)\n", s_hid_cid);
            // Start the report pump in the BTstack context (self-re-arming).
            btstack_run_loop_set_timer_handler(&s_send_timer, send_timer_handler);
            btstack_run_loop_set_timer(&s_send_timer, SEND_INTERVAL_MS);
            btstack_run_loop_add_timer(&s_send_timer);
            break;
        case HID_SUBEVENT_CONNECTION_CLOSED:
            printf("[switch_bt] Switch disconnected\n");
            s_connected = false;
            btstack_run_loop_remove_timer(&s_send_timer);
            gap_discoverable_control(1);
            gap_connectable_control(1);
            reconnect_timer_start();   // resume paging the bonded console
            break;
        case HID_SUBEVENT_CAN_SEND_NOW: {
            uint8_t msg[66];
            // hid_device_send_interrupt_message is a raw l2cap_send — it does NOT add
            // the HIDP header, so we must: [0xA1 = DATA|INPUT][reportID][payload].
            // Without this the Switch gets malformed reports, ignores them, and never
            // starts the subcommand handshake (rx stayed 0 while tx climbed to 1000s).
            msg[0] = 0xA1;
            int n = switch_proto_build_input(&s_proto, &s_input, &msg[1]);
            if (n > 0) {
                s_stat_tx++;
                if (msg[1] == SW_IN_ID_SUBCMD_REPLY)
                    printf("[switch_bt] TX 0x21 ack=0x%02x sub=0x%02x\n", msg[14], msg[15]);
                hid_device_send_interrupt_message(s_hid_cid, msg, (uint16_t)(n + 1));
            }
            break;
        }
        default: break;
        }
        break;
    default: break;
    }
}

void switch_bt_init(void)
{
    bd_addr_t local;
    // Best-effort local MAC for the device-info/SPI responses; refreshed after HCI up.
    memset(local, 0, sizeof(local));
    switch_proto_init(&s_proto, local, /*usb=*/false, &s_hooks, NULL);
    memset(&s_input, 0, sizeof(s_input));
    s_input.lx = s_input.ly = s_input.rx = s_input.ry = 128;
    s_input.battery = 8;
}

void switch_bt_late_init(void)
{
    // Runs early (from ble_output_late_init) — its main job is to keep ble_output
    // from bringing up the BLE GATT stack for this mode. The actual HID-device
    // registration happens in switch_bt_register_device(), called from
    // setup_hid_handlers() AFTER l2cap_init()/sdp_init() — registering before
    // l2cap_init would be silently wiped. Stop the BT-HID host scan here so it
    // doesn't fight for the radio.
    printf("[switch_bt] Switch-BT mode active (device registration deferred to BT init)\n");
    // Latched: a plain suppress is cleared again by the host's idle safety net and
    // by any timed scan, which resumes inquiry behind the live console link.
    extern void btstack_host_suppress_scan_latched(bool suppress);
    btstack_host_suppress_scan_latched(true);

    // (BD_ADDR is set from apply_gap_identity via the BCM vendor command once HCI is
    // up — hci_set_bd_addr's chipset path is skipped under HAVE_HOST_CONTROLLER_API.)
}

// Register the Classic HID *device* role + SDP. MUST be called after l2cap_init()
// and sdp_init() (from setup_hid_handlers), or the L2CAP service registration is
// discarded. GAP identity (name/CoD/EIR) + BD_ADDR are applied later from the
// HCI_STATE_WORKING handler via switch_bt_apply_gap_identity().
void switch_bt_register_device(void)
{
    printf("[switch_bt] registering Switch Pro HID device (SDP + L2CAP)\n");

    // SDP: HID service record (gamepad subclass) + Device ID (Nintendo VID/PID).
    memset(s_hid_sdp, 0, sizeof(s_hid_sdp));
    hid_sdp_record_t rec = {
        .hid_device_subclass      = 0x08,    // gamepad/joystick
        .hid_country_code         = 0x00,
        .hid_virtual_cable        = 0,
        .hid_remote_wake          = 1,
        .hid_reconnect_initiate   = 1,       // controller initiates reconnect to the Switch
        .hid_normally_connectable = 1,
        .hid_boot_device          = 0,
        .hid_ssr_host_max_latency = 0xFFFF,
        .hid_ssr_host_min_timeout = 0xFFFF,
        .hid_supervision_timeout  = 3200,
        .hid_descriptor           = k_hid_descriptor,
        .hid_descriptor_size      = sizeof(k_hid_descriptor),
        .device_name              = SW_NAME,
    };
    hid_create_sdp_record(s_hid_sdp, sdp_create_service_record_handle(), &rec);
    sdp_register_service(s_hid_sdp);

    memset(s_devid_sdp, 0, sizeof(s_devid_sdp));
    device_id_create_sdp_record(s_devid_sdp, sdp_create_service_record_handle(),
                                DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH, SW_VID, SW_PID, 0x0100);
    sdp_register_service(s_devid_sdp);

    // HID device role (registers the L2CAP control/interrupt services).
    hid_device_init(/*boot_protocol=*/0, sizeof(k_hid_descriptor), k_hid_descriptor);
    s_hci_cb.callback = &packet_handler;
    hci_add_event_handler(&s_hci_cb);
    hid_device_register_packet_handler(&packet_handler);
    hid_device_register_report_data_callback(&report_data_cb);
    hid_device_register_set_report_callback(&set_report_cb);
}

void switch_bt_task(void)
{
    // Periodically log the ACTUAL radio BD_ADDR (after the async Read BD_ADDR
    // completes) so we can confirm the BCM Write BD_ADDR took effect, and keep
    // s_proto.mac in sync with the real radio address (device-info/SPI must match).
    static uint32_t last_addr_log = 0;
    uint32_t tnow = platform_time_ms();
    if (last_addr_log == 0 || (uint32_t)(tnow - last_addr_log) >= 10000) {
        last_addr_log = tnow;
        bd_addr_t radio; gap_local_bd_addr(radio);
        if (radio[0] | radio[1] | radio[2]) memcpy(s_proto.mac, radio, 6);
        printf("[switch_bt] STAT conn=%lu tx=%lu rx=%lu lastsub=0x%02x mode=0x%02x connected=%d\n",
               (unsigned long)s_stat_connects, (unsigned long)s_stat_tx, (unsigned long)s_stat_rx,
               s_stat_last_rx_sub, s_proto.report_mode, s_connected);
    }
    // Pull merged controller state (routed to the BLE peripheral target).
    const input_event_t* ev = router_get_output(OUTPUT_TARGET_BLE_PERIPHERAL, 0);
    if (ev) {
        s_input.buttons = ev->buttons;
        s_input.lx = ev->analog[ANALOG_LX];
        s_input.ly = ev->analog[ANALOG_LY];
        s_input.rx = ev->analog[ANALOG_RX];
        s_input.ry = ev->analog[ANALOG_RY];
    }

    // NOTE: the report pump lives in send_timer_handler (BTstack context). Do NOT
    // call any hid_device_* / BTstack API from here — this runs on the main loop and
    // would race the CYW43 async context (that stalled the pump after one report).
}

bool switch_bt_is_connected(void) { return s_connected; }

#endif // CONFIG_BT_CLASSIC_OUTPUT
