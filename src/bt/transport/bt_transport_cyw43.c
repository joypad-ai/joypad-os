// bt_transport_cyw43.c - Pico W CYW43 Bluetooth Transport
// Implements bt_transport_t using BTstack with Pico W's built-in CYW43 Bluetooth
//
// Supports two modes:
//   - Central (bt2usb): scans/connects BT controllers via btstack_host
//   - Peripheral (universal): advertises as BLE gamepad via ble_output
// Mode is selected via bt_cyw43_set_post_init() callback before bt_init().

#include "bt_transport.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// POST-INIT CALLBACK (set by app before bt_init)
// ============================================================================

typedef void (*bt_cyw43_post_init_fn)(void);
static bt_cyw43_post_init_fn post_init_callback = NULL;

void bt_cyw43_set_post_init(bt_cyw43_post_init_fn fn)
{
    post_init_callback = fn;
}

// ============================================================================
// CENTRAL MODE SUPPORT (bt2usb — btstack_host + bthid)
// Only linked when btstack_host.c and bthid.c are in the build.
// ============================================================================

typedef struct {
    bool active;
    uint8_t bd_addr[6];
    char name[48];
    uint8_t class_of_device[3];
    uint16_t vendor_id;
    uint16_t product_id;
    bool hid_ready;
    bool is_ble;
} btstack_classic_conn_info_t;

__attribute__((weak)) void btstack_host_init_hid_handlers(void) {}
__attribute__((weak)) void btstack_host_process(void) {}
__attribute__((weak)) void bthid_task(void) {}
__attribute__((weak)) void btstack_host_power_on(void) {}
__attribute__((weak)) bool btstack_host_is_powered_on(void) { return false; }
__attribute__((weak)) void btstack_host_start_scan(void) {}
__attribute__((weak)) void btstack_host_stop_scan(void) {}
__attribute__((weak)) bool btstack_host_is_scanning(void) { return false; }
__attribute__((weak)) uint8_t btstack_classic_get_connection_count(void) { return 0; }
__attribute__((weak)) bool btstack_classic_get_connection(uint8_t idx, btstack_classic_conn_info_t *info) { (void)idx; (void)info; return false; }
__attribute__((weak)) bool btstack_classic_send_set_report_type(uint8_t idx, uint8_t type, uint8_t id, const uint8_t *data, uint16_t len) { (void)idx; (void)type; (void)id; (void)data; (void)len; return false; }
__attribute__((weak)) bool btstack_classic_send_report(uint8_t idx, uint8_t id, const uint8_t *data, uint16_t len) { (void)idx; (void)id; (void)data; (void)len; return false; }
__attribute__((weak)) void btstack_host_transport_process(void) {}

// BTstack includes (must come before CYW43 includes)
#include "btstack_run_loop.h"
#include "hci.h"
#include "hci_transport.h"

// Pico W CYW43 includes
#include "pico/cyw43_arch.h"
#include "pico/btstack_cyw43.h"
#include "pico/btstack_hci_transport_cyw43.h"
#include "pico/async_context.h"
#ifdef CONFIG_SWITCH2_BLE_OUTPUT
#include "btstack_event.h"
#include "pico/btstack_chipset_cyw43.h"
#include "switch2_ble/switch2_ble.h"
#include "ble_output/ble_output.h"
#endif

// ============================================================================
// CYW43 TRANSPORT STATE
// ============================================================================

static bt_connection_t cyw43_connections[BT_MAX_CONNECTIONS];
static bool cyw43_initialized = false;

// ============================================================================
// TRANSPORT IMPLEMENTATION
// ============================================================================


#ifdef CONFIG_SWITCH2_BLE_OUTPUT
// Claiming the Nintendo public address on CYW43.
//
// It must be re-asserted from inside BTstack's init sequence, NOT before power-on:
// pico-sdk's hci_transport_cyw43_open() does its own
//     cyw43_hal_get_mac(0, &addr); addr[5]++; hci_set_bd_addr(addr);
// and that open() runs as part of hci_power_control(HCI_POWER_ON), i.e. after any
// call we make first. It silently overwrote our address with the OTP WiFi MAC + 1,
// so the 0xFC01 that BTstack sent carried the OTP address -- which is why the
// command reported success while the address appeared never to change. The chip
// was always willing; the transport was clobbering us.
//
// READ_LOCAL_VERSION_INFORMATION completes early in the init sequence and well
// before HCI_INIT_SET_BD_ADDR (which follows READ_LOCAL_SUPPORTED_COMMANDS), so
// setting it here lands in the command BTstack actually sends. This mirrors what
// bt_transport_nrf.c does on its own controller.
uint8_t bt_cyw43_addr_claim_status = 0xFF;

// Controller identity, captured at init and readable later over CDC. The boot log
// cannot be used: HCI init completes before CDC logging can be armed.
// HCI/LMP version 8 = BT 4.2, 9 = BT 5.0 (where LE 2M PHY was introduced), 11 = 5.2.
uint8_t  bt_cyw43_hci_version, bt_cyw43_lmp_version;
uint16_t bt_cyw43_manufacturer;

static void addr_claim_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel; (void)size;
    if (packet_type != HCI_EVENT_PACKET) return;

    if (hci_event_packet_get_type(packet) == HCI_EVENT_COMMAND_COMPLETE) {
        uint16_t op = hci_event_command_complete_get_command_opcode(packet);
        if (op == HCI_OPCODE_HCI_READ_LOCAL_VERSION_INFORMATION) {
            // Answers "is the missing 2M PHY a firmware limit or the core?":
            // HCI/LMP version 9 = BT 5.0 (where 2M PHY was introduced), 11 = 5.2.
            bt_cyw43_hci_version  = packet[6];
            bt_cyw43_lmp_version  = packet[9];
            bt_cyw43_manufacturer = little_endian_read_16(packet, 10);
            printf("[BT_CYW43] HCI ver %u LMP ver %u manuf %u\n",
                   bt_cyw43_hci_version, bt_cyw43_lmp_version, bt_cyw43_manufacturer);
            bd_addr_t pub;
            switch2_ble_get_public_addr(pub);
            hci_set_bd_addr(pub);          // re-assert: the transport just clobbered it
            switch2_ble_note_claimed_addr(pub);
            printf("[BT_CYW43] re-claiming public addr %s after transport open\n",
                   bd_addr_to_str(pub));
        } else if (op == 0xFC01) {
            bt_cyw43_addr_claim_status = packet[5];
            printf("[BT_CYW43] vendor 0xFC01 set-bd-addr status 0x%02x\n",
                   bt_cyw43_addr_claim_status);
        }
        return;
    }

    if (hci_event_packet_get_type(packet) == BTSTACK_EVENT_STATE &&
        btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
        bd_addr_t have;
        gap_local_bd_addr(have);
        printf("[BT_CYW43] radio up with addr %s\n", bd_addr_to_str(have));
    }
}

static btstack_packet_callback_registration_t s_addr_claim_cb = {
    .callback = &addr_claim_handler,
};
#endif

static void cyw43_transport_init(void)
{
    memset(cyw43_connections, 0, sizeof(cyw43_connections));
    printf("[BT_CYW43] Transport init (Pico W built-in Bluetooth)\n");

    // Initialize CYW43 driver (WiFi + BT)
    if (cyw43_arch_init()) {
        printf("[BT_CYW43] ERROR: Failed to initialize CYW43\n");
        return;
    }
    printf("[BT_CYW43] CYW43 driver initialized\n");

    // Initialize BTstack with CYW43
    // This uses the Pico SDK's btstack_cyw43 integration which handles:
    // - btstack_memory_init()
    // - btstack_run_loop_init() with async_context
    // - hci_init() with CYW43 transport
    // - TLV storage setup for bonding
    async_context_t *context = cyw43_arch_async_context();
    if (!btstack_cyw43_init(context)) {
        printf("[BT_CYW43] ERROR: Failed to initialize BTstack\n");
        return;
    }
    printf("[BT_CYW43] BTstack initialized\n");

    // Post-init: app-provided callback (peripheral) or default HID host (central)
    if (post_init_callback) {
        printf("[BT_CYW43] Running app post-init callback (peripheral mode)\n");
        post_init_callback();
    } else {
        printf("[BT_CYW43] Initializing HID host handlers (central mode)\n");
        btstack_host_init_hid_handlers();
    }

    cyw43_initialized = true;
    printf("[BT_CYW43] Ready for Bluetooth connections\n");

#ifdef CONFIG_SWITCH2_BLE_OUTPUT
    // Switch 2 mode needs a Nintendo PUBLIC address, like a real pad. The CYW43's
    // own address is Raspberry Pi's OUI 28:CD:C1, and a console that connected to it
    // terminated the link (0x13) without sending a single ATT packet; advertising
    // from a random-static address instead drew no connection at all.
    //
    // Contrary to a stale comment in switch_bt.c, this IS possible on CYW43:
    // HAVE_HOST_CONTROLLER_API is not defined in this build, and the pico-sdk ships a
    // chipset driver whose set_bd_addr_command is the vendor opcode 0xFC01. Both
    // calls must land before HCI power-on, since hci.c only sends the address change
    // as part of its init sequence.
    if (post_init_callback && ble_output_get_mode() == BLE_MODE_SWITCH2) {
        // Registration only -- the address itself is set from the handler, because
        // the transport's open() would overwrite anything set here. The transport
        // also installs the CYW43 chipset driver, so we no longer do that either.
        hci_add_event_handler(&s_addr_claim_cb);
        printf("[BT_CYW43] Switch 2 mode: will claim a Nintendo public address\n");
    }
#endif

    // Power on Bluetooth
    if (post_init_callback) {
        hci_power_control(HCI_POWER_ON);
    } else {
        btstack_host_power_on();
    }
}

static uint32_t task_counter = 0;

static void cyw43_transport_task(void)
{
    if (!cyw43_initialized) return;

    task_counter++;
    if (task_counter == 1) {
        printf("[BT_CYW43] Task started\n");
    }

    // CYW43 uses async_context for processing
#if PICO_CYW43_ARCH_POLL
    cyw43_arch_poll();
#endif

    btstack_host_process();
    bthid_task();
}

static bool cyw43_transport_is_ready(void)
{
    if (post_init_callback) {
        return cyw43_initialized;
    }
    return cyw43_initialized && btstack_host_is_powered_on();
}

static uint8_t cyw43_transport_get_connection_count(void)
{
    return btstack_classic_get_connection_count();
}

static const bt_connection_t* cyw43_transport_get_connection(uint8_t index)
{
    if (index >= BT_MAX_CONNECTIONS) {
        return NULL;
    }

    btstack_classic_conn_info_t info;
    if (!btstack_classic_get_connection(index, &info)) {
        return NULL;
    }

    // Update cached connection struct
    bt_connection_t* conn = &cyw43_connections[index];
    memcpy(conn->bd_addr, info.bd_addr, 6);
    strncpy(conn->name, info.name, BT_MAX_NAME_LEN - 1);
    conn->name[BT_MAX_NAME_LEN - 1] = '\0';
    memcpy(conn->class_of_device, info.class_of_device, 3);
    conn->vendor_id = info.vendor_id;
    conn->product_id = info.product_id;
    conn->connected = info.active;
    conn->hid_ready = info.hid_ready;
    conn->is_ble = info.is_ble;

    return conn;
}

static bool cyw43_transport_send_control(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    // Classic BT: parse SET_REPORT header and forward to BTstack
    if (len >= 2) {
        uint8_t header = data[0];
        uint8_t report_type = header & 0x03;
        uint8_t report_id = data[1];
        return btstack_classic_send_set_report_type(conn_index, report_type, report_id, data + 2, len - 2);
    }
    return false;
}

static bool cyw43_transport_send_interrupt(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    // Classic BT: parse DATA|OUTPUT header and forward to BTstack
    if (len >= 2) {
        uint8_t report_id = data[1];
        return btstack_classic_send_report(conn_index, report_id, data + 2, len - 2);
    }
    return false;
}

static void cyw43_transport_disconnect(uint8_t conn_index)
{
    // TODO: Implement disconnect
    (void)conn_index;
}

static void cyw43_transport_set_pairing_mode(bool enable)
{
    if (enable) {
        btstack_host_start_scan();
    } else {
        btstack_host_stop_scan();
    }
}

static bool cyw43_transport_is_pairing_mode(void)
{
    return btstack_host_is_scanning();
}

// ============================================================================
// TRANSPORT STRUCT
// ============================================================================

const bt_transport_t bt_transport_cyw43 = {
    .name = "Pico W CYW43",
    .init = cyw43_transport_init,
    .task = cyw43_transport_task,
    .is_ready = cyw43_transport_is_ready,
    .get_connection_count = cyw43_transport_get_connection_count,
    .get_connection = cyw43_transport_get_connection,
    .send_control = cyw43_transport_send_control,
    .send_interrupt = cyw43_transport_send_interrupt,
    .disconnect = cyw43_transport_disconnect,
    .set_pairing_mode = cyw43_transport_set_pairing_mode,
    .is_pairing_mode = cyw43_transport_is_pairing_mode,
};
