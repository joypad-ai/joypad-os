// btstack_host.c - BTstack HID Host (BLE + Classic)
//
// Transport-agnostic BTstack integration for HID devices.
// Uses BTstack's SM (Security Manager) for LE Secure Connections,
// GATT client for HID over GATT Profile (HOGP), and
// HID Host for Classic BT HID devices.

#include "btstack_host.h"
#include "btstack_config.h"
#include "bt_device_db.h"
// Include specific BTstack headers instead of umbrella btstack.h
// (btstack.h pulls in audio codecs which need sbc_encoder.h)
#include "btstack_defines.h"
#include "btstack_event.h"
#include "btstack_run_loop.h"

// Run loop depends on transport: embedded for USB dongle, async_context for CYW43
#ifndef BTSTACK_USE_CYW43
#include "btstack_run_loop_embedded.h"
#endif

// Declare btstack_memory_init - can't include btstack_memory.h due to HID conflicts
extern void btstack_memory_init(void);

#include "bluetooth_data_types.h"
#include "bluetooth_company_id.h"
#include "bluetooth_sdp.h"
#include "ad_parser.h"
#include "gap.h"
#include "hci.h"
#include "l2cap.h"
#include "ble/sm.h"
#include "ble/gatt_client.h"
#include "ble/le_device_db.h"
#include "ble/gatt-service/hids_client.h"
#include "classic/hid_host.h"
#include "classic/sdp_client.h"
#include "classic/sdp_server.h"
#include "classic/sdp_util.h"
#include "classic/device_id_server.h"

// Link key storage: TLV (flash) based for all builds
// USB dongle uses pico_flash_bank_instance(), CYW43 uses SDK's btstack_cyw43.c setup
#ifndef BTSTACK_USE_CYW43
#include "classic/btstack_link_key_db_tlv.h"
#include "ble/le_device_db_tlv.h"
#include "btstack_tlv_flash_bank.h"
#include "pico/btstack_flash_bank.h"
#include "pico/flash.h"
#include "hardware/flash.h"
#endif

#include "hci_dump.h"
#include "hci_dump_embedded_stdout.h"

// BTHID callbacks - for classic BT HID devices
extern void bt_on_hid_ready(uint8_t conn_index);
extern void bt_on_disconnect(uint8_t conn_index);
extern void bt_on_hid_report(uint8_t conn_index, const uint8_t* data, uint16_t len);
extern void bthid_update_device_info(uint8_t conn_index, const char* name,
                                      uint16_t vendor_id, uint16_t product_id);

#include <stdio.h>
#include <string.h>

// For rumble feedback passthrough
// Note: manager.h includes tusb.h which conflicts with BTstack, so forward declare
extern int find_player_index(int dev_addr, int instance);
#include "core/services/players/feedback.h"

// ============================================================================
// FLASH HELPERS (for TLV storage)
// ============================================================================
#ifndef BTSTACK_USE_CYW43
// Erase both BTstack flash banks (8KB total at end of flash)
static void __no_inline_not_in_flash_func(flash_erase_banks_func)(void* p) {
    (void)p;
    uint32_t flash_offset = PICO_FLASH_SIZE_BYTES - (FLASH_SECTOR_SIZE * 2);
    // Erase both 4KB sectors
    flash_range_erase(flash_offset, FLASH_SECTOR_SIZE * 2);
}

// Erase BTstack flash banks using flash_safe_execute
static void btstack_erase_flash_banks(void) {
    printf("[BTSTACK_HOST] Erasing BTstack flash banks at 0x%lX...\n",
           (unsigned long)(PICO_FLASH_SIZE_BYTES - (FLASH_SECTOR_SIZE * 2)));
    int result = flash_safe_execute(flash_erase_banks_func, NULL, UINT32_MAX);
    if (result == PICO_OK) {
        printf("[BTSTACK_HOST] Flash banks erased successfully\n");
    } else {
        printf("[BTSTACK_HOST] Flash erase failed: %d\n", result);
    }
}
#endif

// ============================================================================
// BLE HID REPORT ROUTING
// ============================================================================

// Deferred processing to avoid stack overflow in BTstack callback
static uint8_t pending_ble_report[64];  // 64 bytes for Switch 2 reports
static uint16_t pending_ble_report_len = 0;
static uint8_t pending_ble_conn_index = 0;
static volatile bool ble_report_pending = false;

// Forward declare the function to route BLE reports through bthid layer
static void route_ble_hid_report(uint8_t conn_index, const uint8_t* data, uint16_t len);

// Forward declare Switch 2 functions (defined later with state machine)
static void switch2_retry_init_if_needed(void);
static void switch2_handle_feedback(void);

// ============================================================================
// CONFIGURATION
// ============================================================================

#define MAX_BLE_CONNECTIONS 2
#define SCAN_INTERVAL 0x00A0  // 100ms
#define SCAN_WINDOW   0x0050  // 50ms

// ============================================================================
// STATE
// ============================================================================

typedef enum {
    BLE_STATE_IDLE,
    BLE_STATE_SCANNING,
    BLE_STATE_CONNECTING,
    BLE_STATE_CONNECTED,
    BLE_STATE_DISCOVERING,
    BLE_STATE_READY
} ble_state_t;

typedef struct {
    bd_addr_t addr;
    bd_addr_type_t addr_type;
    hci_con_handle_t handle;
    ble_state_t state;

    // GATT discovery state
    uint16_t hid_service_start;
    uint16_t hid_service_end;
    uint16_t report_char_handle;
    uint16_t report_ccc_handle;

    // Device info
    char name[32];
    const bt_device_profile_t* profile;
    uint16_t vid;
    uint16_t pid;

    // Connection index for bthid layer (offset by MAX_CLASSIC_CONNECTIONS)
    uint8_t conn_index;
    bool hid_ready;
} ble_connection_t;

// BLE conn_index offset (BLE devices use conn_index >= this value)
#define BLE_CONN_INDEX_OFFSET MAX_CLASSIC_CONNECTIONS

typedef enum {
    GATT_IDLE,
    GATT_DISCOVERING_SERVICES,
    GATT_DISCOVERING_HID_CHARACTERISTICS,
    GATT_ENABLING_NOTIFICATIONS,
    GATT_READY
} gatt_state_t;

static struct {
    bool initialized;
    bool powered_on;
    ble_state_t state;

    // HCI transport (provided by caller)
    const hci_transport_t* hci_transport;

    // Scanning
    bool scan_active;

    // Pending connection
    bd_addr_t pending_addr;
    bd_addr_type_t pending_addr_type;
    char pending_name[32];
    const bt_device_profile_t* pending_profile;
    uint16_t pending_vid;
    uint16_t pending_pid;

    // Last connected device (for reconnection)
    bd_addr_t last_connected_addr;
    bd_addr_type_t last_connected_addr_type;
    char last_connected_name[32];
    bool has_last_connected;
    uint32_t reconnect_attempt_time;
    uint8_t reconnect_attempts;

    // Connections
    ble_connection_t connections[MAX_BLE_CONNECTIONS];

    // GATT discovery state
    gatt_state_t gatt_state;
    hci_con_handle_t gatt_handle;
    uint16_t hid_service_start;
    uint16_t hid_service_end;
    gatt_client_characteristic_t report_characteristic;  // Full HID Report characteristic

    // Callbacks
    btstack_host_report_callback_t report_callback;
    btstack_host_connect_callback_t connect_callback;

    // HIDS Client
    uint16_t hids_cid;

} hid_state;

// HID descriptor storage (shared across connections)
static uint8_t hid_descriptor_storage[512];

static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_packet_callback_registration_t sm_event_callback_registration;

// Direct notification listener for Xbox HID reports (bypasses HIDS client)
static gatt_client_notification_t xbox_hid_notification_listener;
static gatt_client_characteristic_t xbox_hid_characteristic;  // Fake characteristic for listener
static void xbox_hid_notification_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

// Direct notification listener for Switch 2 HID reports
static gatt_client_notification_t switch2_hid_notification_listener;
static gatt_client_characteristic_t switch2_hid_characteristic;
static void switch2_hid_notification_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

// ============================================================================
// CLASSIC BT HID HOST STATE
// ============================================================================

#define MAX_CLASSIC_CONNECTIONS 4
#define INQUIRY_DURATION 5  // Inquiry duration in 1.28s units

typedef struct {
    bool active;
    uint16_t hid_cid;           // BTstack HID connection ID
    bd_addr_t addr;
    char name[32];
    uint8_t class_of_device[3];
    uint16_t vendor_id;
    uint16_t product_id;
    bool hid_ready;
    const bt_device_profile_t* profile;
} classic_connection_t;

static struct {
    bool inquiry_active;
    bool use_liac;  // Alternate between GIAC and LIAC for Wiimote/Wii U Pro discovery
    classic_connection_t connections[MAX_CLASSIC_CONNECTIONS];
    // Pending incoming connection info (from HCI_EVENT_CONNECTION_REQUEST)
    bd_addr_t pending_addr;
    uint32_t pending_cod;
    char pending_name[64];
    uint16_t pending_vid;
    uint16_t pending_pid;
    bool pending_valid;
    bool pending_outgoing;  // True if we initiated the connection (hid_host_connect)
    const bt_device_profile_t* pending_profile;
    // Pending HID connect (deferred until encryption completes)
    bd_addr_t pending_hid_addr;
    hci_con_handle_t pending_hid_handle;
    bool pending_hid_connect;
    // Set after outgoing HID fails — wait for device to reconnect incoming
    uint32_t waiting_for_incoming_time;  // 0 = not waiting
} classic_state;

// ============================================================================
// WIIMOTE DIRECT L2CAP STATE
// ============================================================================
// Wiimotes don't work well with BTstack's hid_host layer.
// We bypass it and create L2CAP channels directly, like USB Host Shield does.

#ifndef PSM_HID_CONTROL
#define PSM_HID_CONTROL   0x0011
#endif
#ifndef PSM_HID_INTERRUPT
#define PSM_HID_INTERRUPT 0x0013
#endif

typedef enum {
    WIIMOTE_STATE_IDLE,
    WIIMOTE_STATE_W4_CONTROL_CONNECTED,
    WIIMOTE_STATE_W4_INTERRUPT_CONNECTED,
    WIIMOTE_STATE_CONNECTED
} wiimote_state_t;

typedef struct {
    bool active;
    wiimote_state_t state;
    bd_addr_t addr;
    hci_con_handle_t acl_handle;
    uint16_t control_cid;
    uint16_t interrupt_cid;
    char name[32];
    uint8_t class_of_device[3];
    uint16_t vendor_id;
    uint16_t product_id;
    int conn_index;  // Index in classic_state.connections for bthid routing
    bool using_hid_host;  // True if reconnected via HID Host (not direct L2CAP)
    uint16_t hid_host_cid;  // HID Host CID for sending (when using_hid_host is true)
    bool hid_host_ready;  // True when HID Host is ready to send (after DESCRIPTOR_AVAILABLE)
} wiimote_connection_t;

static wiimote_connection_t wiimote_conn;

// Forward declaration
static void wiimote_l2cap_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

// SDP query state
static uint8_t sdp_attribute_value[32];
static const uint16_t sdp_attribute_value_buffer_size = sizeof(sdp_attribute_value);

// Classic HID descriptor storage
static uint8_t classic_hid_descriptor_storage[512];

// SDP Device ID record buffer (needed for DS4/DS5 reconnection)
static uint8_t device_id_sdp_service_buffer[100];

// Find classic connection by hid_cid
static classic_connection_t* find_classic_connection_by_cid(uint16_t hid_cid) {
    for (int i = 0; i < MAX_CLASSIC_CONNECTIONS; i++) {
        if (classic_state.connections[i].active && classic_state.connections[i].hid_cid == hid_cid) {
            return &classic_state.connections[i];
        }
    }
    return NULL;
}

// Get conn_index for classic connection
static int get_classic_conn_index(uint16_t hid_cid) {
    for (int i = 0; i < MAX_CLASSIC_CONNECTIONS; i++) {
        if (classic_state.connections[i].active && classic_state.connections[i].hid_cid == hid_cid) {
            return i;  // conn_index matches array index
        }
    }
    return -1;
}

// Find free classic connection slot
static classic_connection_t* find_free_classic_connection(void) {
    for (int i = 0; i < MAX_CLASSIC_CONNECTIONS; i++) {
        if (!classic_state.connections[i].active) {
            return &classic_state.connections[i];
        }
    }
    return NULL;
}

// ============================================================================
// BLE CONNECTION HELPERS
// ============================================================================

// Get BLE connection by conn_index
static ble_connection_t* find_ble_connection_by_conn_index(uint8_t conn_index) {
    if (conn_index < BLE_CONN_INDEX_OFFSET) return NULL;
    uint8_t ble_index = conn_index - BLE_CONN_INDEX_OFFSET;
    if (ble_index >= MAX_BLE_CONNECTIONS) return NULL;
    if (hid_state.connections[ble_index].handle == 0) return NULL;
    return &hid_state.connections[ble_index];
}

// Get conn_index for BLE connection
static int get_ble_conn_index_by_handle(hci_con_handle_t handle) {
    for (int i = 0; i < MAX_BLE_CONNECTIONS; i++) {
        if (hid_state.connections[i].handle == handle) {
            return BLE_CONN_INDEX_OFFSET + i;
        }
    }
    return -1;
}

// Route BLE HID report through bthid layer
static void route_ble_hid_report(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    // Build BTHID-compatible packet: DATA|INPUT header + report
    // Buffer needs to hold 1 byte header + up to 64 bytes of report data
    static uint8_t hid_packet[65];
    hid_packet[0] = 0xA1;  // DATA | INPUT header
    if (len <= sizeof(hid_packet) - 1) {
        memcpy(hid_packet + 1, data, len);
        bt_on_hid_report(conn_index, hid_packet, len + 1);
    }
}

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void sm_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void hids_client_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void hid_host_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static ble_connection_t* find_connection_by_handle(hci_con_handle_t handle);
static ble_connection_t* find_free_connection(void);
static void start_hids_client(ble_connection_t *conn);
static void register_ble_hid_listener(hci_con_handle_t con_handle);
static void register_switch2_hid_listener(hci_con_handle_t con_handle);

// ============================================================================
// INITIALIZATION
// ============================================================================

// Internal function to set up HID handlers (used by both init paths)
static void setup_hid_handlers(void)
{
    printf("[BTSTACK_HOST] Init L2CAP...\n");
    l2cap_init();

    printf("[BTSTACK_HOST] Init SM...\n");
    sm_init();

    // Configure SM - bonding like Bluepad32
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING);
    sm_set_encryption_key_size_range(7, 16);

    printf("[BTSTACK_HOST] Init GATT client...\n");
    gatt_client_init();

    printf("[BTSTACK_HOST] Init HIDS client...\n");
    hids_client_init(hid_descriptor_storage, sizeof(hid_descriptor_storage));

    printf("[BTSTACK_HOST] Init LE Device DB...\n");
    le_device_db_init();

    // Initialize classic BT HID Host
    printf("[BTSTACK_HOST] Init Classic HID Host...\n");
    memset(&classic_state, 0, sizeof(classic_state));
    // Set security level BEFORE hid_host_init (it registers L2CAP services with this level)
    gap_set_security_level(LEVEL_0);  // DS3 doesn't support SSP
    hid_host_init(classic_hid_descriptor_storage, sizeof(classic_hid_descriptor_storage));
    hid_host_register_packet_handler(hid_host_packet_handler);

    // SDP server - needed for DS4/DS5 reconnection (they query Device ID)
    sdp_init();
    device_id_create_sdp_record(device_id_sdp_service_buffer, 0x10003,
                                DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH,
                                BLUETOOTH_COMPANY_ID_BLUEKITCHEN_GMBH, 1, 1);
    sdp_register_service(device_id_sdp_service_buffer);
    printf("[BTSTACK_HOST] SDP server initialized\n");

    // Allow sniff mode and role switch for classic BT (improves compatibility)
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_SNIFF_MODE | LM_LINK_POLICY_ENABLE_ROLE_SWITCH);

    // Register for HCI events
    printf("[BTSTACK_HOST] Register event handlers...\n");
    hci_event_callback_registration.callback = packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // Register for SM events
    sm_event_callback_registration.callback = sm_packet_handler;
    sm_add_event_handler(&sm_event_callback_registration);

    hid_state.initialized = true;
    printf("[BTSTACK_HOST] HID handlers initialized (BLE + Classic)\n");
}

// btstack_host_init is only used for USB dongle transport
// For CYW43, use btstack_host_init_hid_handlers() after btstack_cyw43_init()
#ifndef BTSTACK_USE_CYW43

// TLV context for flash-based link key storage (must be static/persistent)
static btstack_tlv_flash_bank_t btstack_tlv_flash_bank_context;

// Set up TLV (flash) storage for persistent link keys and BLE bonding
static void setup_tlv_storage(void) {
    printf("[BTSTACK_HOST] Setting up flash-based TLV storage...\n");

    // Check for corrupted flash banks and erase if needed
    // Flash bank 0 starts at end of flash - 8KB
    uint32_t bank0_offset = PICO_FLASH_SIZE_BYTES - (FLASH_SECTOR_SIZE * 2);
    const uint8_t* bank0_ptr = (const uint8_t*)(XIP_BASE + bank0_offset);

    // BTstack TLV expects clean flash (0xFF) or valid header
    // If we see our debug pattern (0xDEADBEEF) or other garbage, erase
    bool needs_erase = false;
    if (bank0_ptr[0] == 0xDE && bank0_ptr[1] == 0xAD &&
        bank0_ptr[2] == 0xBE && bank0_ptr[3] == 0xEF) {
        printf("[BTSTACK_HOST] Detected corrupted flash bank (debug pattern)\n");
        needs_erase = true;
    }

    if (needs_erase) {
        btstack_erase_flash_banks();
    }

    // Get the Pico SDK flash bank HAL instance
    const hal_flash_bank_t *hal_flash_bank_impl = pico_flash_bank_instance();
    printf("[BTSTACK_HOST] Flash bank instance: %p\n", hal_flash_bank_impl);

    // Initialize BTstack TLV with flash bank
    const btstack_tlv_t *btstack_tlv_impl = btstack_tlv_flash_bank_init_instance(
            &btstack_tlv_flash_bank_context,
            hal_flash_bank_impl,
            NULL);
    printf("[BTSTACK_HOST] TLV instance: %p\n", btstack_tlv_impl);

    if (!btstack_tlv_impl) {
        printf("[BTSTACK_HOST] ERROR: TLV init failed!\n");
        return;
    }

    // Set global TLV instance
    btstack_tlv_set_instance(btstack_tlv_impl, &btstack_tlv_flash_bank_context);

    // Set up Classic BT link key storage using TLV
    const btstack_link_key_db_t *btstack_link_key_db = btstack_link_key_db_tlv_get_instance(
            btstack_tlv_impl, &btstack_tlv_flash_bank_context);
    printf("[BTSTACK_HOST] Link key DB instance: %p\n", btstack_link_key_db);

    if (!btstack_link_key_db) {
        printf("[BTSTACK_HOST] ERROR: Link key DB init failed!\n");
        return;
    }

    hci_set_link_key_db(btstack_link_key_db);
    printf("[BTSTACK_HOST] Classic BT link key DB configured (flash)\n");

    // Configure BLE device DB for TLV storage
    le_device_db_tlv_configure(btstack_tlv_impl, &btstack_tlv_flash_bank_context);
    printf("[BTSTACK_HOST] BLE device DB configured (flash)\n");

    // Debug: check bank state
    printf("[BTSTACK_HOST] TLV context: current_bank=%d write_offset=0x%lX\n",
           btstack_tlv_flash_bank_context.current_bank,
           (unsigned long)btstack_tlv_flash_bank_context.write_offset);
}

void btstack_host_init(const void* transport)
{
    if (hid_state.initialized) {
        printf("[BTSTACK_HOST] Already initialized\n");
        return;
    }

    if (!transport) {
        printf("[BTSTACK_HOST] ERROR: No HCI transport provided\n");
        return;
    }

    printf("[BTSTACK_HOST] Initializing BTstack...\n");

    memset(&hid_state, 0, sizeof(hid_state));
    hid_state.hci_transport = (const hci_transport_t*)transport;

    // HCI dump disabled - too verbose (logs every ACL packet)
    // printf("[BTSTACK_HOST] Init HCI dump (for logging)...\n");
    // hci_dump_init(hci_dump_embedded_stdout_get_instance());

    printf("[BTSTACK_HOST] Init memory pools...\n");
    btstack_memory_init();

    printf("[BTSTACK_HOST] Init run loop...\n");
    btstack_run_loop_init(btstack_run_loop_embedded_get_instance());

    printf("[BTSTACK_HOST] Init HCI with provided transport...\n");
    hci_init(transport, NULL);

    // Set up flash-based TLV storage for persistent link keys and BLE bonds
    setup_tlv_storage();

    // Set up HID handlers
    setup_hid_handlers();
    printf("[BTSTACK_HOST] Initialized OK\n");
}
#endif

void btstack_host_init_hid_handlers(void)
{
    if (hid_state.initialized) {
        printf("[BTSTACK_HOST] HID handlers already initialized\n");
        return;
    }

    printf("[BTSTACK_HOST] Initializing HID handlers (BTstack already initialized externally)...\n");

    memset(&hid_state, 0, sizeof(hid_state));
    // Note: hci_transport is not set here since BTstack was initialized externally

    // Set up HID handlers (BTstack core already initialized by btstack_cyw43_init or similar)
    setup_hid_handlers();
    printf("[BTSTACK_HOST] HID handlers initialized OK\n");
}

void btstack_host_power_on(void)
{
    printf("[BTSTACK_HOST] power_on called, initialized=%d\n", hid_state.initialized);

    if (!hid_state.initialized) {
        printf("[BTSTACK_HOST] ERROR: Not initialized\n");
        return;
    }

    printf("[BTSTACK_HOST] HCI state before power_on: %d\n", hci_get_state());
    printf("[BTSTACK_HOST] Calling hci_power_control(HCI_POWER_ON)...\n");
    int err = hci_power_control(HCI_POWER_ON);
    printf("[BTSTACK_HOST] hci_power_control returned %d, state now: %d\n", err, hci_get_state());
}

// ============================================================================
// SCANNING
// ============================================================================

static uint32_t scan_timeout_end = 0;  // 0 = no timeout (indefinite scan)

// Pending BLE gamepad: when we see a gamepad appearance but no name in the ADV packet,
// stash the address and wait for the scan response (which typically contains the name).
// This prevents connecting to Xbox controllers as "Generic BLE Gamepad".
static struct {
    bool valid;
    bd_addr_t addr;
    bd_addr_type_t addr_type;
    uint16_t appearance;
    uint32_t timestamp;  // For expiry
} pending_ble_gamepad;

void btstack_host_start_scan(void)
{
    if (!hid_state.powered_on) {
        printf("[BTSTACK_HOST] Not powered on yet\n");
        return;
    }

    if (hid_state.scan_active || classic_state.inquiry_active) {
        return;  // Already scanning
    }

    printf("[BTSTACK_HOST] Starting BLE scan...\n");
    gap_set_scan_params(1, SCAN_INTERVAL, SCAN_WINDOW, 0);
    gap_start_scan();
    hid_state.scan_active = true;
    hid_state.state = BLE_STATE_SCANNING;

    // Also start classic BT inquiry
    // Alternate between GIAC (general) and LIAC (limited) to discover Wiimotes/Wii U Pro
    // which use Limited Discoverable mode when SYNC button is pressed
    uint32_t lap = classic_state.use_liac ? GAP_IAC_LIMITED_INQUIRY : GAP_IAC_GENERAL_INQUIRY;
    printf("[BTSTACK_HOST] Starting Classic inquiry (LAP=%s)...\n",
           classic_state.use_liac ? "LIAC" : "GIAC");
    gap_inquiry_set_lap(lap);
    gap_inquiry_start(INQUIRY_DURATION);
    classic_state.inquiry_active = true;
}

void btstack_host_stop_scan(void)
{
    // Always set state to idle to prevent scanning from restarting
    hid_state.state = BLE_STATE_IDLE;

    if (hid_state.scan_active) {
        printf("[BTSTACK_HOST] Stopping BLE scan\n");
        gap_stop_scan();
        hid_state.scan_active = false;
    }

    if (classic_state.inquiry_active) {
        printf("[BTSTACK_HOST] Stopping Classic inquiry\n");
        gap_inquiry_stop();
        classic_state.inquiry_active = false;
    }
}

void btstack_host_start_timed_scan(uint32_t timeout_ms)
{
    scan_timeout_end = btstack_run_loop_get_time_ms() + timeout_ms;
    printf("[BTSTACK_HOST] Starting timed scan (%lums)\n", (unsigned long)timeout_ms);
    btstack_host_start_scan();
}

// ============================================================================
// CONNECTION
// ============================================================================

#define BLE_CONNECT_TIMEOUT_MS 10000  // 10s timeout for BLE connection attempts

void btstack_host_connect_ble(bd_addr_t addr, bd_addr_type_t addr_type)
{
    printf("[BTSTACK_HOST] Connecting to %02X:%02X:%02X:%02X:%02X:%02X\n",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    // Stop scanning first
    btstack_host_stop_scan();

    // Save pending connection info
    memcpy(hid_state.pending_addr, addr, 6);
    hid_state.pending_addr_type = addr_type;
    hid_state.state = BLE_STATE_CONNECTING;
    hid_state.reconnect_attempt_time = btstack_run_loop_get_time_ms();

    // Create connection
    uint8_t status = gap_connect(addr, addr_type);
    printf("[BTSTACK_HOST] gap_connect returned status=%d\n", status);
}

// ============================================================================
// CALLBACKS
// ============================================================================

void btstack_host_register_report_callback(btstack_host_report_callback_t callback)
{
    hid_state.report_callback = callback;
}

void btstack_host_register_connect_callback(btstack_host_connect_callback_t callback)
{
    hid_state.connect_callback = callback;
}

// ============================================================================
// MAIN LOOP
// ============================================================================


// Transport-specific process function (weak, overridden by transport)
__attribute__((weak)) void btstack_host_transport_process(void) {
    // Default: no-op, transport should override
}

void btstack_host_process(void)
{
    if (!hid_state.initialized) return;

    // Process transport-specific tasks (e.g., USB polling, CYW43 async context)
    btstack_host_transport_process();

#ifndef BTSTACK_USE_CYW43
    // Process BTstack run loop multiple times to let packets flow through HCI->L2CAP->ATT->GATT
    // Note: CYW43 uses async_context run loop, processed by cyw43_arch_poll() in transport
    for (int i = 0; i < 5; i++) {
        btstack_run_loop_embedded_execute_once();
    }
#endif

    // Process any pending BLE HID report (deferred from BTstack callback to avoid stack overflow)
    if (ble_report_pending) {
        ble_report_pending = false;
        route_ble_hid_report(pending_ble_conn_index, pending_ble_report, pending_ble_report_len);
    }

    // Retry Switch 2 init if stuck (no ACK received)
    switch2_retry_init_if_needed();

    // Handle Switch 2 rumble/LED feedback passthrough
    switch2_handle_feedback();

    // Check scan timeout
    if (scan_timeout_end > 0 && btstack_host_is_scanning()) {
        if (btstack_run_loop_get_time_ms() >= scan_timeout_end) {
            printf("[BTSTACK_HOST] Timed scan expired\n");
            scan_timeout_end = 0;
            btstack_host_stop_scan();
        }
    }

    // BLE connection attempt timeout — gap_connect() has no built-in timeout,
    // so if the target device is powered off, we'd be stuck in CONNECTING forever.
    // Cancel after BLE_CONNECT_TIMEOUT_MS. Set state to IDLE immediately to prevent
    // this check from re-triggering on the next tick. The LE_CONNECTION_COMPLETE
    // error event from the cancel will handle retry/resume logic.
    if (hid_state.state == BLE_STATE_CONNECTING &&
        hid_state.reconnect_attempt_time != 0 &&
        (btstack_run_loop_get_time_ms() - hid_state.reconnect_attempt_time) >= BLE_CONNECT_TIMEOUT_MS) {
        printf("[BTSTACK_HOST] BLE connection attempt timed out after %dms\n", BLE_CONNECT_TIMEOUT_MS);
        gap_connect_cancel();
        hid_state.state = BLE_STATE_IDLE;
        hid_state.reconnect_attempt_time = 0;
    }

    // Timeout for "waiting for incoming reconnection" after outgoing Classic HID failure.
    // If the device doesn't reconnect within 30s, give up and resume scanning.
    if (classic_state.waiting_for_incoming_time != 0 &&
        (btstack_run_loop_get_time_ms() - classic_state.waiting_for_incoming_time) >= 30000) {
        printf("[BTSTACK_HOST] Incoming reconnection timeout, resuming scan\n");
        classic_state.waiting_for_incoming_time = 0;
    }

    // Safety net: if idle with no active connections and not scanning, resume scan.
    // This catches edge cases where the state machine gets stuck (e.g., gap_connect_cancel
    // doesn't generate an error event, or a disconnect handler didn't restart scanning).
    // Skip if:
    //   - waiting for incoming Classic reconnection (outgoing HID failed)
    //   - Classic connection setup in progress (name request, HID connect pending)
    if (hid_state.state == BLE_STATE_IDLE &&
        hid_state.reconnect_attempt_time == 0 &&
        !hid_state.scan_active &&
        classic_state.waiting_for_incoming_time == 0 &&
        !classic_state.pending_valid &&
        btstack_classic_get_connection_count() == 0) {
        printf("[BTSTACK_HOST] Safety: idle with no connections, resuming scan\n");
        hid_state.reconnect_attempts = 0;
        btstack_host_start_scan();
    }
}

// ============================================================================
// SDP QUERY CALLBACK (for VID/PID detection)
// ============================================================================

static void sdp_query_vid_pid_callback(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event_type = hci_event_packet_get_type(packet);

    // Debug: log connection-related HCI events for Wiimote troubleshooting
    if (wiimote_conn.active && event_type >= 0x01 && event_type <= 0x20) {
        printf("[BTSTACK_HOST] HCI event: 0x%02X\n", event_type);
    }

    switch (event_type) {
        case SDP_EVENT_QUERY_ATTRIBUTE_VALUE: {
            uint16_t attr_len = sdp_event_query_attribute_byte_get_attribute_length(packet);
            if (attr_len <= sdp_attribute_value_buffer_size) {
                uint16_t offset = sdp_event_query_attribute_byte_get_data_offset(packet);
                sdp_attribute_value[offset] = sdp_event_query_attribute_byte_get_data(packet);

                // Check if we got all bytes for this attribute
                if (offset + 1 == attr_len) {
                    uint16_t attr_id = sdp_event_query_attribute_byte_get_attribute_id(packet);
                    uint16_t value;
                    if (de_element_get_uint16(sdp_attribute_value, &value)) {
                        if (attr_id == BLUETOOTH_ATTRIBUTE_VENDOR_ID) {
                            classic_state.pending_vid = value;
                            printf("[BTSTACK_HOST] SDP VID: 0x%04X\n", value);
                        } else if (attr_id == BLUETOOTH_ATTRIBUTE_PRODUCT_ID) {
                            classic_state.pending_pid = value;
                            printf("[BTSTACK_HOST] SDP PID: 0x%04X\n", value);
                        }
                    }
                }
            }
            break;
        }
        case SDP_EVENT_QUERY_COMPLETE:
            printf("[BTSTACK_HOST] SDP query complete: VID=0x%04X PID=0x%04X\n",
                   classic_state.pending_vid, classic_state.pending_pid);

            // Update the connection struct with VID/PID
            if (classic_state.pending_vid || classic_state.pending_pid) {
                for (int i = 0; i < MAX_CLASSIC_CONNECTIONS; i++) {
                    classic_connection_t* conn = &classic_state.connections[i];
                    if (conn->active && memcmp(conn->addr, classic_state.pending_addr, 6) == 0) {
                        conn->vendor_id = classic_state.pending_vid;
                        conn->product_id = classic_state.pending_pid;
                        printf("[BTSTACK_HOST] Updated conn[%d] VID/PID: 0x%04X/0x%04X\n",
                               i, conn->vendor_id, conn->product_id);

                        // Notify bthid to re-evaluate driver selection with new VID/PID
                        bthid_update_device_info(i, conn->name,
                                                  classic_state.pending_vid,
                                                  classic_state.pending_pid);
                        break;
                    }
                }

                // Also update wiimote_conn if active and address matches
                if (wiimote_conn.active && memcmp(wiimote_conn.addr, classic_state.pending_addr, 6) == 0) {
                    wiimote_conn.vendor_id = classic_state.pending_vid;
                    wiimote_conn.product_id = classic_state.pending_pid;
                    printf("[BTSTACK_HOST] Updated wiimote VID/PID: 0x%04X/0x%04X\n",
                           wiimote_conn.vendor_id, wiimote_conn.product_id);
                }
            }
            break;
    }
}

// ============================================================================
// HCI EVENT HANDLER
// ============================================================================

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event_type = hci_event_packet_get_type(packet);

    // Debug: log key HCI events to debug Wiimote reconnection
    // 0x04=CONNECTION_COMPLETE, 0x05=DISCONNECTION_COMPLETE, 0x06=AUTH_COMPLETE
    // 0x08=ENCRYPTION_CHANGE, 0x17=LINK_KEY_REQUEST, 0x18=LINK_KEY_NOTIFICATION
    // 0x16=PIN_CODE_REQUEST, 0x04=CONNECTION_REQUEST (offset differs)
    if (event_type == 0x17 || event_type == 0x18 || event_type == 0x06 ||
        event_type == 0x08 || event_type == 0x16) {
        printf("[BTSTACK_HOST] >>> HCI Event 0x%02X (size=%d)\n", event_type, size);
    }

    // Debug: catch GATT notifications at the global level
    if (event_type == GATT_EVENT_NOTIFICATION) {
        printf("[BTSTACK_HOST] >>> RAW GATT NOTIFICATION! len=%d\n", size);
    }

    switch (event_type) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("[BTSTACK_HOST] HCI working\n");
                hid_state.powered_on = true;

                // Reset scan state (in case of reconnect)
                hid_state.scan_active = false;
                classic_state.inquiry_active = false;

                // Set master role policy for incoming connections
                // Wiimotes (including Wii U Pro) REQUIRE us to be master
                // This must be set early, before any connection requests arrive
                hci_set_master_slave_policy(0);  // 0 = always try to become master
                printf("[BTSTACK_HOST] Set master role policy\n");

                // Print our local BD_ADDR
                bd_addr_t local_addr;
                gap_local_bd_addr(local_addr);
                printf("[BTSTACK_HOST] Local BD_ADDR: %02X:%02X:%02X:%02X:%02X:%02X\n",
                       local_addr[0], local_addr[1], local_addr[2],
                       local_addr[3], local_addr[4], local_addr[5]);

                // Print chip info (see hci_transport_h2_tinyusb.h for dongle compatibility guide)
                uint16_t manufacturer = hci_get_manufacturer();
                printf("[BTSTACK_HOST] Chip Manufacturer: 0x%04X", manufacturer);
                switch (manufacturer) {
                    case 0x000A: printf(" (CSR) - OK\n"); break;
                    case 0x000D: printf(" (TI)\n"); break;
                    case 0x000F: printf(" (Broadcom) - OK\n"); break;
                    case 0x001D: printf(" (Qualcomm)\n"); break;
                    case 0x0046: printf(" (MediaTek)\n"); break;
                    case 0x005D: printf(" (Realtek) - NEEDS FIRMWARE!\n"); break;
                    case 0x0002: printf(" (Intel)\n"); break;
                    default: printf("\n"); break;
                }

                // Set local name (for devices that want to see us)
                gap_set_local_name("Joypad Adapter");

                // Set class of device to Computer (Desktop Workstation)
                // This helps Sony controllers recognize us as a valid host
                gap_set_class_of_device(0x000104);  // Major: Computer, Minor: Desktop

                // Enable SSP (Secure Simple Pairing) on the controller
                extern const hci_cmd_t hci_write_simple_pairing_mode;
                hci_send_cmd(&hci_write_simple_pairing_mode, 1);

                // Enable bonding for Classic BT
                gap_set_bondable_mode(1);
                // Set IO capability for "just works" pairing (no PIN required)
                gap_ssp_set_io_capability(SSP_IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
                // Request bonding during SSP (required for BTstack to store link keys!)
                gap_ssp_set_authentication_requirement(SSP_IO_AUTHREQ_MITM_PROTECTION_NOT_REQUIRED_DEDICATED_BONDING);
                // Auto-accept incoming SSP pairing requests
                gap_ssp_set_auto_accept(1);

                // Make host discoverable and connectable for incoming connections
                // Required for Sony controllers (DS3, DS4, DS5) which initiate connections
                gap_discoverable_control(1);
                gap_connectable_control(1);

                // Auto-start scanning
                btstack_host_start_scan();
            }
            break;

        case GAP_EVENT_ADVERTISING_REPORT: {
            bd_addr_t addr;
            gap_event_advertising_report_get_address(packet, addr);
            bd_addr_type_t addr_type = gap_event_advertising_report_get_address_type(packet);
            int8_t rssi = gap_event_advertising_report_get_rssi(packet);
            uint8_t adv_len = gap_event_advertising_report_get_data_length(packet);
            const uint8_t *adv_data = gap_event_advertising_report_get_data(packet);
            uint8_t adv_event_type = gap_event_advertising_report_get_advertising_event_type(packet);

            // Parse name, appearance, and manufacturer data from advertising data
            char name[32] = {0};
            uint16_t mfr_company_id = 0;
            uint16_t sw2_vid = 0;
            uint16_t sw2_pid = 0;
            uint16_t appearance = 0;
            bool has_hid_uuid = false;

            ad_context_t context;
            for (ad_iterator_init(&context, adv_len, adv_data); ad_iterator_has_more(&context); ad_iterator_next(&context)) {
                uint8_t type = ad_iterator_get_data_type(&context);
                uint8_t len = ad_iterator_get_data_len(&context);
                const uint8_t *data = ad_iterator_get_data(&context);

                if ((type == BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME ||
                     type == BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME) && len < sizeof(name)) {
                    memcpy(name, data, len);
                    name[len] = 0;
                }

                // Parse GAP Appearance (2 bytes, little-endian)
                if (type == BLUETOOTH_DATA_TYPE_APPEARANCE && len >= 2) {
                    appearance = data[0] | (data[1] << 8);
                }

                // Check for HID service UUID (0x1812) in service class lists
                if ((type == BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS ||
                     type == BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS) && len >= 2) {
                    for (int i = 0; i + 1 < len; i += 2) {
                        uint16_t uuid16 = data[i] | (data[i + 1] << 8);
                        if (uuid16 == 0x1812) {  // HID Service
                            has_hid_uuid = true;
                            break;
                        }
                    }
                }

                // Check for Switch 2 controller via manufacturer data
                // Company ID 0x0553 (Nintendo for Switch 2)
                // BlueRetro uses data[1] for company ID, data[6] for VID - their data includes length byte
                // BTstack iterator strips length+type, so we use data[0] for company ID, data[5] for VID
                if (type == BLUETOOTH_DATA_TYPE_MANUFACTURER_SPECIFIC_DATA && len >= 2) {
                    mfr_company_id = data[0] | (data[1] << 8);
                    if (mfr_company_id == 0x0553) {
                        // Debug: print raw manufacturer data
                        printf("[SW2_BLE] Mfr data (%d bytes):", len);
                        for (int i = 0; i < len && i < 12; i++) {
                            printf(" %02X", data[i]);
                        }
                        printf("\n");
                        if (len >= 9) {
                            // VID at bytes 5-6, PID at bytes 7-8 (relative to after company ID)
                            // This matches BlueRetro's offsets accounting for length byte difference
                            sw2_vid = data[5] | (data[6] << 8);
                            sw2_pid = data[7] | (data[8] << 8);
                        }
                        printf("[BTSTACK_HOST] Switch 2 controller detected! VID=0x%04X PID=0x%04X\n",
                               sw2_vid, sw2_pid);
                    }
                }
            }

            // Log all BLE advertisements with names for debugging
            if (name[0] != 0) {
                printf("[BTSTACK_HOST] BLE adv: %02X:%02X:%02X:%02X:%02X:%02X name=\"%s\"\n",
                       addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], name);
            }

            // Merge with pending gamepad data: if we previously saw a gamepad appearance
            // for this address but no name (ADV packet), and now we have the name
            // (SCAN_RSP), use the merged data to identify the device properly.
            if (pending_ble_gamepad.valid &&
                memcmp(addr, pending_ble_gamepad.addr, 6) == 0) {
                // Carry appearance from the ADV packet if not in this packet
                if (appearance == 0) {
                    appearance = pending_ble_gamepad.appearance;
                }
                if (name[0]) {
                    // Got a name for the pending gamepad — clear pending and proceed
                    pending_ble_gamepad.valid = false;
                }
            }

            // Identify device by name and/or manufacturer company ID
            const bt_device_profile_t* profile = bt_device_lookup(name, mfr_company_id);
            bool is_known_controller = (profile != &BT_PROFILE_DEFAULT);

            // Generic BLE gamepad detection (fallback for unknown controllers)
            // Only triggers when no specific driver matched by name/manufacturer.
            // Uses GAP Appearance: 0x03C3 = Joystick, 0x03C4 = Gamepad
            // Does NOT match on HID UUID alone to avoid connecting to keyboards/mice.
            // Excludes controllers that use classic BT (DS4, DS3, DS5) — they advertise
            // BLE but must connect via classic for proper driver support.
            bool is_generic_ble_gamepad = false;
            if (!is_known_controller && !profile->classic_only &&
                (appearance == 0x03C3 || appearance == 0x03C4)) {
                // If no name yet and this isn't already a scan response, defer connection
                // to wait for the scan response which typically contains the device name.
                // This prevents connecting to Xbox controllers as "Generic BLE Gamepad".
                // Only defer once per address — if we already deferred and the scan response
                // didn't bring a name (or never arrived), connect as generic on the next ADV.
                if (!name[0] && adv_event_type != 0x04) {
                    if (!pending_ble_gamepad.valid ||
                        memcmp(addr, pending_ble_gamepad.addr, 6) != 0) {
                        pending_ble_gamepad.valid = true;
                        memcpy(pending_ble_gamepad.addr, addr, 6);
                        pending_ble_gamepad.addr_type = addr_type;
                        pending_ble_gamepad.appearance = appearance;
                        pending_ble_gamepad.timestamp = btstack_run_loop_get_time_ms();
                        printf("[BTSTACK_HOST] Gamepad appearance 0x%04X with no name, waiting for scan response...\n",
                               appearance);
                        break;
                    }
                    // Second ADV with no name for same address — proceed as generic
                    pending_ble_gamepad.valid = false;
                }
                is_generic_ble_gamepad = true;
                printf("[BTSTACK_HOST] Generic BLE gamepad detected: \"%s\" appearance=0x%04X hid_uuid=%d\n",
                       name, appearance, has_hid_uuid);
            }

            bool is_controller = is_known_controller || is_generic_ble_gamepad;

            // Auto-connect to supported BLE controllers (skip classic-only devices)
            if (hid_state.state == BLE_STATE_SCANNING && is_controller &&
                (profile->ble != BT_BLE_NONE || is_generic_ble_gamepad)) {
                printf("[BTSTACK_HOST] BLE controller: %02X:%02X:%02X:%02X:%02X:%02X name=\"%s\"\n",
                       addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], name);
                // Determine display name from profile and PID
                const char* type_str;
                if (profile == &BT_PROFILE_SWITCH2) {
                    switch (sw2_pid) {
                        case 0x2066: type_str = "Switch 2 Joy-Con L"; break;
                        case 0x2067: type_str = "Switch 2 Joy-Con R"; break;
                        case 0x2069: type_str = "Switch 2 Pro"; break;
                        case 0x2073: type_str = "Switch 2 GameCube"; break;
                        default:     type_str = "Switch 2 Controller"; break;
                    }
                } else if (!is_generic_ble_gamepad) {
                    type_str = profile->name;
                } else {
                    type_str = "Generic BLE Gamepad";
                }
                printf("[BTSTACK_HOST] Connecting to %s...\n", type_str);
                // Use advertised name if available, otherwise use device type as fallback
                if (name[0]) {
                    strncpy(hid_state.pending_name, name, sizeof(hid_state.pending_name) - 1);
                } else {
                    strncpy(hid_state.pending_name, type_str, sizeof(hid_state.pending_name) - 1);
                }
                hid_state.pending_name[sizeof(hid_state.pending_name) - 1] = '\0';
                hid_state.pending_profile = profile;
                hid_state.pending_vid = sw2_vid;
                hid_state.pending_pid = sw2_pid;
                btstack_host_connect_ble(addr, addr_type);
            }
            break;
        }

        // Classic BT inquiry result
        case GAP_EVENT_INQUIRY_RESULT: {
            bd_addr_t addr;
            gap_event_inquiry_result_get_bd_addr(packet, addr);
            uint32_t cod = gap_event_inquiry_result_get_class_of_device(packet);

            // Parse name from extended inquiry response if available
            char name[240] = {0};
            if (gap_event_inquiry_result_get_name_available(packet)) {
                int name_len = gap_event_inquiry_result_get_name_len(packet);
                if (name_len > 0 && name_len < (int)sizeof(name)) {
                    memcpy(name, gap_event_inquiry_result_get_name(packet), name_len);
                    name[name_len] = 0;
                }
            }

            // Class of Device: Major=0x05 (Peripheral), Minor bits indicate type
            uint8_t major_class = (cod >> 8) & 0x1F;
            uint8_t minor_class = (cod >> 2) & 0x3F;
            bool is_gamepad = (major_class == 0x05) && ((minor_class & 0x0F) == 0x02);  // Gamepad
            bool is_joystick = (major_class == 0x05) && ((minor_class & 0x0F) == 0x01); // Joystick

            // Identify device by name
            const bt_device_profile_t* profile = bt_device_lookup_by_name(name);
            bool is_wiimote_family = (profile->classic == BT_CLASSIC_DIRECT_L2CAP);

            // Log all inquiry results for debugging (gamepads highlighted)
            const char* type_str = "";
            if (is_wiimote_family) type_str = " [WIIMOTE]";
            else if (is_gamepad || is_joystick) type_str = " [GAMEPAD]";
            printf("[BTSTACK_HOST] Inquiry: %02X:%02X:%02X:%02X:%02X:%02X COD=0x%06X%s %s\n",
                   addr[5], addr[4], addr[3], addr[2], addr[1], addr[0],
                   (unsigned)cod, type_str, name);

            // Auto-connect to gamepads and Wiimotes
            if ((is_gamepad || is_joystick || is_wiimote_family) && classic_state.inquiry_active) {
                printf("[BTSTACK_HOST] Classic gamepad found, connecting...\n");
                btstack_host_stop_scan();  // Stop inquiry

                // Save pending info for PIN code handler and deferred connection
                memcpy(classic_state.pending_addr, addr, 6);
                classic_state.pending_cod = cod;
                strncpy(classic_state.pending_name, name, sizeof(classic_state.pending_name) - 1);
                classic_state.pending_name[sizeof(classic_state.pending_name) - 1] = '\0';
                classic_state.pending_profile = profile;
                classic_state.pending_valid = true;
                classic_state.pending_outgoing = true;  // We initiated this connection

                // If name is unavailable, request it and defer connection to
                // REMOTE_NAME_REQUEST_COMPLETE. Wiimote-family devices (Wii U Pro,
                // Wiimote) need the name to route through the correct connection
                // path (direct L2CAP vs HID Host), and their name is not always
                // included in the Extended Inquiry Response.
                if (!name[0]) {
                    printf("[BTSTACK_HOST] Name unavailable at inquiry, requesting before connect...\n");
                    classic_state.pending_hid_connect = true;
                    gap_remote_name_request(addr, 0, 0);
                    break;
                }

                if (profile->classic == BT_CLASSIC_DIRECT_L2CAP) {
                    // Wiimotes/Wii U Pro don't work well with BTstack's hid_host layer
                    // Use direct L2CAP channel creation instead (like USB Host Shield)
                    printf("[BTSTACK_HOST] %s detected, using direct L2CAP approach\n", profile->name);
                    classic_state.pending_hid_connect = true;

                    // Initialize wiimote connection state
                    memset(&wiimote_conn, 0, sizeof(wiimote_conn));
                    wiimote_conn.active = true;
                    wiimote_conn.state = WIIMOTE_STATE_IDLE;
                    memcpy(wiimote_conn.addr, addr, 6);
                    strncpy(wiimote_conn.name, name, sizeof(wiimote_conn.name) - 1);
                    wiimote_conn.class_of_device[0] = cod & 0xFF;
                    wiimote_conn.class_of_device[1] = (cod >> 8) & 0xFF;
                    wiimote_conn.class_of_device[2] = (cod >> 16) & 0xFF;

                    // Allocate classic connection slot for bthid routing
                    classic_connection_t* conn = find_free_classic_connection();
                    if (conn) {
                        int conn_index = conn - classic_state.connections;
                        memset(conn, 0, sizeof(*conn));
                        conn->active = true;
                        conn->hid_cid = 0xFFFF;  // Special marker for direct L2CAP
                        memcpy(conn->addr, addr, 6);
                        strncpy(conn->name, name, sizeof(conn->name) - 1);
                        conn->class_of_device[0] = cod & 0xFF;
                        conn->class_of_device[1] = (cod >> 8) & 0xFF;
                        conn->class_of_device[2] = (cod >> 16) & 0xFF;
                        conn->profile = profile;
                        wiimote_conn.conn_index = conn_index;
                        printf("[BTSTACK_HOST] %s conn_index=%d\n", profile->name, conn_index);
                    }

                    // Create ACL connection directly (gap_connect will trigger HCI connection)
                    // We'll create L2CAP channels after encryption completes
                    printf("[BTSTACK_HOST] Creating ACL connection to %s...\n", profile->name);
                    uint8_t status = gap_connect(addr, BD_ADDR_TYPE_ACL);
                    if (status != ERROR_CODE_SUCCESS && status != ERROR_CODE_COMMAND_DISALLOWED) {
                        printf("[BTSTACK_HOST] gap_connect failed: 0x%02X\n", status);
                        wiimote_conn.active = false;
                        classic_state.pending_hid_connect = false;
                    }
                } else {
                    // Non-Wiimote: use normal hid_host_connect
                    // Use profile's hid_mode to determine SDP bypass
                    hid_protocol_mode_t mode = (profile->hid_mode == BT_HID_MODE_FALLBACK)
                        ? HID_PROTOCOL_MODE_REPORT_WITH_FALLBACK_TO_BOOT
                        : HID_PROTOCOL_MODE_REPORT;
                    uint16_t hid_cid;
                    uint8_t status = hid_host_connect(addr, mode, &hid_cid);
                    if (status == ERROR_CODE_SUCCESS) {
                        printf("[BTSTACK_HOST] hid_host_connect started, cid=0x%04X\n", hid_cid);

                        // Allocate connection slot
                        classic_connection_t* conn = find_free_classic_connection();
                        if (conn) {
                            memset(conn, 0, sizeof(*conn));
                            conn->active = true;
                            conn->hid_cid = hid_cid;
                            memcpy(conn->addr, addr, 6);
                            strncpy(conn->name, name, sizeof(conn->name) - 1);
                            conn->class_of_device[0] = cod & 0xFF;
                            conn->class_of_device[1] = (cod >> 8) & 0xFF;
                            conn->class_of_device[2] = (cod >> 16) & 0xFF;
                            conn->profile = profile;
                        }
                    } else {
                        printf("[BTSTACK_HOST] hid_host_connect failed: %d\n", status);
                    }
                }
            }
            break;
        }

        case GAP_EVENT_INQUIRY_COMPLETE:
            classic_state.inquiry_active = false;
            // Restart inquiry after it completes (if we're still in scan mode)
            // Toggle between GIAC and LIAC to discover all device types
            if (hid_state.state == BLE_STATE_SCANNING) {
                classic_state.use_liac = !classic_state.use_liac;
                uint32_t lap = classic_state.use_liac ? GAP_IAC_LIMITED_INQUIRY : GAP_IAC_GENERAL_INQUIRY;
                printf("[BTSTACK_HOST] Restarting inquiry (LAP=%s)...\n",
                       classic_state.use_liac ? "LIAC" : "GIAC");
                gap_inquiry_set_lap(lap);
                gap_inquiry_start(INQUIRY_DURATION);
                classic_state.inquiry_active = true;
            }
            break;

        // Classic BT incoming connection request (DS3 connects this way)
        case HCI_EVENT_CONNECTION_REQUEST: {
            bd_addr_t addr;
            hci_event_connection_request_get_bd_addr(packet, addr);
            uint32_t cod = hci_event_connection_request_get_class_of_device(packet);
            uint8_t link_type = hci_event_connection_request_get_link_type(packet);
            printf("[BTSTACK_HOST] Incoming connection: %02X:%02X:%02X:%02X:%02X:%02X COD=0x%06X link=%d\n",
                   addr[0], addr[1], addr[2], addr[3], addr[4], addr[5], (unsigned)cod, link_type);

            // Save pending connection info for use when HID connection is established
            // Note: device name is not available yet at CONNECTION_REQUEST time.
            // Wiimote detection is deferred to CONNECTION_COMPLETE or later when
            // name resolution completes. Global master role policy (set at startup)
            // already ensures we become master for all connections.
            memcpy(classic_state.pending_addr, addr, 6);
            classic_state.pending_cod = cod;
            classic_state.pending_name[0] = '\0';  // Clear, will be filled by remote name request
            classic_state.pending_vid = 0;
            classic_state.pending_pid = 0;
            classic_state.pending_valid = true;
            classic_state.pending_outgoing = false;  // Device initiated this connection
            classic_state.waiting_for_incoming_time = 0;  // Device reconnected
            // BTstack will auto-accept with the current master_slave_policy
            break;
        }

        case HCI_EVENT_CONNECTION_COMPLETE: {
            uint8_t status = hci_event_connection_complete_get_status(packet);
            hci_con_handle_t handle = hci_event_connection_complete_get_connection_handle(packet);
            bd_addr_t addr;
            hci_event_connection_complete_get_bd_addr(packet, addr);
            printf("[BTSTACK_HOST] Connection complete: status=%d handle=0x%04X addr=%02X:%02X:%02X:%02X:%02X:%02X\n",
                   status, handle, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

            // Handle connection complete for both incoming and outgoing connections
            if (status == 0) {
                if (classic_state.pending_valid &&
                    bd_addr_cmp(addr, classic_state.pending_addr) == 0) {
                    uint32_t cod = classic_state.pending_cod;

                    if (classic_state.pending_outgoing) {
                        // Outgoing connection (we initiated)
                        printf("[BTSTACK_HOST] Outgoing ACL complete, COD=0x%06X\n", cod);

                        // For Wiimotes, store ACL handle and do L2CAP-specific setup
                        if (classic_state.pending_hid_connect && wiimote_conn.active) {
                            wiimote_conn.acl_handle = handle;
                            printf("[BTSTACK_HOST] Wiimote: stored ACL handle=0x%04X\n", handle);

                            // Request remote name if we don't have it from inquiry
                            if (wiimote_conn.name[0] == '\0') {
                                gap_remote_name_request(addr, 0, 0);
                            }

                            // Query VID/PID via SDP
                            sdp_client_query_uuid16(&sdp_query_vid_pid_callback, addr,
                                                    BLUETOOTH_SERVICE_CLASS_PNP_INFORMATION);
                        }

                        // Request authentication for all outgoing connections.
                        // Switch Pro and other SSP devices need this before HID L2CAP
                        // channels can be established. Also triggers PIN exchange for
                        // legacy devices (Wiimote/Wii U Pro).
                        gap_request_security_level(handle, LEVEL_2);
                    } else {
                        // Incoming connection (device connected to us)
                        printf("[BTSTACK_HOST] Incoming ACL complete, COD=0x%06X\n", cod);

                        // Detect direct L2CAP device by pending profile (if available from prior inquiry).
                        // For incoming reconnections, pending_name is typically empty at
                        // this point — detection is deferred to HID_SUBEVENT_CONNECTION_OPENED
                        // or REMOTE_NAME_REQUEST_COMPLETE when the name becomes available.
                        const bt_device_profile_t* incoming_profile = classic_state.pending_profile;
                        if (!incoming_profile && classic_state.pending_name[0]) {
                            incoming_profile = bt_device_lookup_by_name(classic_state.pending_name);
                        }
                        bool is_direct_l2cap = (incoming_profile &&
                                                incoming_profile->classic == BT_CLASSIC_DIRECT_L2CAP);

                        if (is_direct_l2cap) {
                            // Wiimote/Wii U Pro reconnection - check role and link key
                            printf("[BTSTACK_HOST] %s detected (incoming reconnection)\n",
                                   incoming_profile->name);

                            // Wiimotes require master role - check and request if needed
                            hci_role_t current_role = gap_get_role(handle);
                            printf("[BTSTACK_HOST] Wiimote: role=%s\n",
                                   current_role == HCI_ROLE_MASTER ? "MASTER" :
                                   current_role == HCI_ROLE_SLAVE ? "SLAVE" : "UNKNOWN");
                            if (current_role != HCI_ROLE_MASTER) {
                                printf("[BTSTACK_HOST] Wiimote: requesting master role switch\n");
                                gap_request_role(addr, HCI_ROLE_MASTER);
                            }

                            // Check if we have a stored link key
                            link_key_t link_key;
                            link_key_type_t key_type;
                            bool have_key = gap_get_link_key_for_bd_addr(addr, link_key, &key_type);
                            printf("[BTSTACK_HOST] Wiimote: have_key=%d type=%d\n", have_key, have_key ? key_type : -1);

                            // Store info for when L2CAP events come in
                            memset(&wiimote_conn, 0, sizeof(wiimote_conn));
                            wiimote_conn.active = true;
                            wiimote_conn.state = WIIMOTE_STATE_IDLE;
                            wiimote_conn.conn_index = -1;  // Not assigned yet
                            memcpy(wiimote_conn.addr, addr, 6);
                            wiimote_conn.acl_handle = handle;
                            memcpy(wiimote_conn.class_of_device, &cod, 3);
                            if (classic_state.pending_name[0]) {
                                strncpy(wiimote_conn.name, classic_state.pending_name, sizeof(wiimote_conn.name) - 1);
                            }

                            // Request remote name for driver matching (need to distinguish Wii U Pro from Wiimote)
                            gap_remote_name_request(addr, 0, 0);

                            // For incoming connections (reconnection), let HID Host handle L2CAP
                            // Don't create outgoing L2CAP - it can conflict with HID Host
                            if (have_key) {
                                printf("[BTSTACK_HOST] Wiimote: handle=0x%04X, have key, waiting for HID Host\n", handle);
                                // Stop scanning now - we have an incoming connection
                                btstack_host_stop_scan();
                                // HID Host will receive HID_SUBEVENT_INCOMING_CONNECTION
                                // and we'll accept it there
                            } else {
                                // No key - this is a new pairing, wait for device to initiate
                                printf("[BTSTACK_HOST] Wiimote: handle=0x%04X, no key, waiting for pairing\n", handle);
                            }
                        }

                        if (!is_direct_l2cap) {
                            // Standard incoming connection flow (DS3, DS4, DS5, or unknown device).
                            // If this is actually a Wiimote reconnection where the name wasn't
                            // available yet, it will be detected later when the name resolves
                            // (see REMOTE_NAME_REQUEST_COMPLETE and HID_SUBEVENT_CONNECTION_OPENED).

                            // Request remote name for driver matching (we don't have it from inquiry)
                            gap_remote_name_request(addr, 0, 0);

                            // Query VID/PID via SDP (PnP Information service)
                            sdp_client_query_uuid16(&sdp_query_vid_pid_callback, addr,
                                                    BLUETOOTH_SERVICE_CLASS_PNP_INFORMATION);

                            // Request authentication — required for DS4/DS5/DS3 and harmless
                            // for Wiimotes that already have a stored link key
                            gap_request_security_level(handle, LEVEL_2);
                        }
                    }
                }
            }
            break;
        }

        case L2CAP_EVENT_INCOMING_CONNECTION: {
            uint16_t psm = l2cap_event_incoming_connection_get_psm(packet);
            uint16_t cid = l2cap_event_incoming_connection_get_local_cid(packet);
            hci_con_handle_t handle = l2cap_event_incoming_connection_get_handle(packet);
            bd_addr_t addr;
            l2cap_event_incoming_connection_get_address(packet, addr);
            printf("[BTSTACK_HOST] L2CAP incoming: PSM=0x%04X cid=0x%04X handle=0x%04X\n", psm, cid, handle);

            // For Wiimotes during reconnection, we create outgoing L2CAP channels ourselves.
            // If the Wiimote also tries to create incoming channels, decline them at L2CAP level
            // to force the Wiimote to use our outgoing channels.
            if (wiimote_conn.active && wiimote_conn.acl_handle == handle &&
                (psm == PSM_HID_CONTROL || psm == PSM_HID_INTERRUPT)) {
                // If we're already creating outgoing channels (reconnection), decline incoming
                if (wiimote_conn.state >= WIIMOTE_STATE_W4_CONTROL_CONNECTED) {
                    printf("[BTSTACK_HOST] Wiimote: declining incoming L2CAP PSM=0x%04X (using outgoing channels)\n", psm);
                    l2cap_decline_connection(cid);
                    break;
                }
                // Fresh pairing or reconnection via HID Host - capture CID for direct L2CAP sending
                // HID Host will accept, but we need the CID to bypass hid_host_send_report
                printf("[BTSTACK_HOST] Wiimote: L2CAP incoming PSM=0x%04X cid=0x%04X (HID Host will accept)\n", psm, cid);
                if (psm == PSM_HID_CONTROL) {
                    wiimote_conn.control_cid = cid;
                    wiimote_conn.state = WIIMOTE_STATE_W4_CONTROL_CONNECTED;
                    printf("[BTSTACK_HOST] Wiimote: captured control CID=0x%04X from incoming\n", cid);
                } else {
                    wiimote_conn.interrupt_cid = cid;
                    wiimote_conn.state = WIIMOTE_STATE_W4_INTERRUPT_CONNECTED;
                    printf("[BTSTACK_HOST] Wiimote: captured interrupt CID=0x%04X from incoming\n", cid);
                }
            }
            break;
        }

        case L2CAP_EVENT_CHANNEL_OPENED: {
            uint8_t status = l2cap_event_channel_opened_get_status(packet);
            uint16_t psm = l2cap_event_channel_opened_get_psm(packet);
            uint16_t cid = l2cap_event_channel_opened_get_local_cid(packet);
            bd_addr_t l2cap_addr;
            l2cap_event_channel_opened_get_address(packet, l2cap_addr);
            printf("[BTSTACK_HOST] L2CAP opened: status=%d PSM=0x%04X cid=0x%04X addr=%s\n",
                   status, psm, cid, bd_addr_to_str(l2cap_addr));

            // Capture L2CAP CIDs for Wiimote connections (for direct L2CAP sending)
            // HID Host handles receiving, but we need direct L2CAP CIDs for sending
            // Note: bt_on_hid_ready is called from HID_SUBEVENT_CONNECTION_OPENED
            if (status == 0 && wiimote_conn.active &&
                memcmp(l2cap_addr, wiimote_conn.addr, 6) == 0) {
                if (psm == PSM_HID_CONTROL) {
                    wiimote_conn.control_cid = cid;
                    printf("[BTSTACK_HOST] Wiimote: captured control CID=0x%04X for direct sending\n", cid);
                } else if (psm == PSM_HID_INTERRUPT) {
                    wiimote_conn.interrupt_cid = cid;
                    printf("[BTSTACK_HOST] Wiimote: captured interrupt CID=0x%04X for direct sending\n", cid);
                }
            }
            break;
        }

        case HCI_EVENT_LE_META: {
            uint8_t subevent = hci_event_le_meta_get_subevent_code(packet);

            switch (subevent) {
                case HCI_SUBEVENT_LE_CONNECTION_COMPLETE: {
                    hci_con_handle_t handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                    uint8_t status = hci_subevent_le_connection_complete_get_status(packet);

                    if (status != 0) {
                        printf("[BTSTACK_HOST] Connection failed: 0x%02X\n", status);
                        hid_state.state = BLE_STATE_IDLE;

                        // If reconnection attempt failed, try again or resume scanning
                        if (hid_state.has_last_connected && hid_state.reconnect_attempts < 5) {
                            hid_state.reconnect_attempts++;
                            printf("[BTSTACK_HOST] Retrying reconnection (attempt %d)...\n",
                                   hid_state.reconnect_attempts);
                            btstack_host_connect_ble(hid_state.last_connected_addr, hid_state.last_connected_addr_type);
                        } else {
                            printf("[BTSTACK_HOST] Reconnection failed, resuming scan\n");
                            btstack_host_start_scan();
                        }
                        break;
                    }

                    printf("[BTSTACK_HOST] Connected! handle=0x%04X\n", handle);

                    // Find or create connection entry
                    ble_connection_t *conn = find_free_connection();
                    if (conn) {
                        memcpy(conn->addr, hid_state.pending_addr, 6);
                        conn->addr_type = hid_state.pending_addr_type;
                        conn->handle = handle;
                        conn->state = BLE_STATE_CONNECTED;
                        // Copy the name from pending connection
                        strncpy(conn->name, hid_state.pending_name, sizeof(conn->name) - 1);
                        conn->name[sizeof(conn->name) - 1] = '\0';
                        conn->profile = hid_state.pending_profile;
                        conn->vid = hid_state.pending_vid;
                        conn->pid = hid_state.pending_pid;

                        printf("[BTSTACK_HOST] Connection stored: name='%s' profile=%s vid=0x%04X pid=0x%04X\n",
                               conn->name, conn->profile ? conn->profile->name : "default",
                               conn->vid, conn->pid);

                        // Route based on BLE strategy
                        if (conn->profile && conn->profile->ble == BT_BLE_CUSTOM) {
                            printf("[BTSTACK_HOST] %s: Skipping SM pairing, using direct ATT setup\n",
                                   conn->profile->name);
                            register_switch2_hid_listener(handle);
                        } else {
                            // Request pairing (SM will handle Secure Connections)
                            printf("[BTSTACK_HOST] Requesting pairing...\n");
                            sm_request_pairing(handle);
                        }
                    }

                    hid_state.state = BLE_STATE_CONNECTED;
                    break;
                }

                case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
                    printf("[BTSTACK_HOST] Connection update complete\n");
                    break;
            }
            break;
        }

        case HCI_EVENT_REMOTE_NAME_REQUEST_COMPLETE: {
            bd_addr_t name_addr;
            hci_event_remote_name_request_complete_get_bd_addr(packet, name_addr);
            uint8_t name_status = hci_event_remote_name_request_complete_get_status(packet);

            if (name_status != 0) {
                printf("[BTSTACK_HOST] Remote name request failed: status=%d\n", name_status);

                // If we deferred a connection waiting for the name, fall back to
                // standard HID Host connect. This handles DS4, DS3, and other
                // controllers that may not respond to name requests.
                if (classic_state.pending_valid &&
                    classic_state.pending_outgoing &&
                    classic_state.pending_hid_connect &&
                    memcmp(name_addr, classic_state.pending_addr, 6) == 0) {
                    printf("[BTSTACK_HOST] Deferred connect: name failed, falling back to HID Host\n");
                    classic_state.pending_hid_connect = false;

                    uint16_t hid_cid;
                    uint8_t status = hid_host_connect(name_addr, HID_PROTOCOL_MODE_REPORT, &hid_cid);
                    if (status == ERROR_CODE_SUCCESS) {
                        printf("[BTSTACK_HOST] hid_host_connect started, cid=0x%04X\n", hid_cid);
                        classic_connection_t* conn = find_free_classic_connection();
                        if (conn) {
                            memset(conn, 0, sizeof(*conn));
                            conn->active = true;
                            conn->hid_cid = hid_cid;
                            memcpy(conn->addr, name_addr, 6);
                            conn->class_of_device[0] = classic_state.pending_cod & 0xFF;
                            conn->class_of_device[1] = (classic_state.pending_cod >> 8) & 0xFF;
                            conn->class_of_device[2] = (classic_state.pending_cod >> 16) & 0xFF;
                        }
                    } else {
                        printf("[BTSTACK_HOST] hid_host_connect failed: %d\n", status);
                    }
                }
                break;
            }

            {
                const char* name = hci_event_remote_name_request_complete_get_remote_name(packet);
                printf("[BTSTACK_HOST] Remote name: %s\n", name);

                // Store name if this is our pending incoming connection
                if (classic_state.pending_valid &&
                    memcmp(name_addr, classic_state.pending_addr, 6) == 0) {
                    strncpy(classic_state.pending_name, name, sizeof(classic_state.pending_name) - 1);
                    classic_state.pending_name[sizeof(classic_state.pending_name) - 1] = '\0';
                }

                // Also update any active connection with this address
                for (int i = 0; i < MAX_CLASSIC_CONNECTIONS; i++) {
                    classic_connection_t* conn = &classic_state.connections[i];
                    if (conn->active && memcmp(conn->addr, name_addr, 6) == 0) {
                        if (conn->name[0] == '\0') {
                            strncpy(conn->name, name, sizeof(conn->name) - 1);
                            conn->name[sizeof(conn->name) - 1] = '\0';
                            printf("[BTSTACK_HOST] Updated conn[%d] name: %s\n", i, conn->name);

                            // Update profile from name if not already set
                            if (!conn->profile || conn->profile == &BT_PROFILE_DEFAULT) {
                                conn->profile = bt_device_lookup_by_name(name);
                            }

                            if (conn->hid_ready) {
                                // Set VID/PID from profile defaults (Wiimote-family lacks PnP SDP)
                                const bt_device_profile_t* name_profile = bt_device_lookup_by_name(name);
                                if (name_profile->default_vid) {
                                    conn->vendor_id = name_profile->default_vid;
                                    uint16_t pid = bt_device_wiimote_pid_from_name(name);
                                    if (pid) {
                                        conn->product_id = pid;
                                        printf("[BTSTACK_HOST] Late %s detection, PID=0x%04X\n",
                                               name_profile->name, pid);
                                    }
                                }
                                // Notify BTHID of late name arrival — allows driver
                                // re-evaluation for devices matched as generic because
                                // name wasn't available at connection time
                                bthid_update_device_info(i, conn->name,
                                                         conn->vendor_id, conn->product_id);
                            }
                        }
                        break;
                    }
                }

                // Also update wiimote_conn if active
                if (wiimote_conn.active && memcmp(wiimote_conn.addr, name_addr, 6) == 0) {
                    if (wiimote_conn.name[0] == '\0') {
                        strncpy(wiimote_conn.name, name, sizeof(wiimote_conn.name) - 1);
                        wiimote_conn.name[sizeof(wiimote_conn.name) - 1] = '\0';
                        printf("[BTSTACK_HOST] Updated wiimote name: %s\n", wiimote_conn.name);
                    }
                }

                // Late direct-L2CAP device detection for incoming reconnections: if the name
                // resolves to a direct-L2CAP device and wiimote_conn wasn't set up at
                // CONNECTION_COMPLETE (because name was unknown), set it up now so
                // HID_SUBEVENT_INCOMING_CONNECTION can route it correctly.
                const bt_device_profile_t* late_profile = bt_device_lookup_by_name(name);
                if (!wiimote_conn.active &&
                    late_profile->classic == BT_CLASSIC_DIRECT_L2CAP &&
                    classic_state.pending_valid &&
                    !classic_state.pending_outgoing &&
                    memcmp(name_addr, classic_state.pending_addr, 6) == 0) {
                    printf("[BTSTACK_HOST] Late %s detection from name resolution (incoming reconnection)\n",
                           late_profile->name);
                    memset(&wiimote_conn, 0, sizeof(wiimote_conn));
                    wiimote_conn.active = true;
                    wiimote_conn.state = WIIMOTE_STATE_IDLE;
                    wiimote_conn.conn_index = -1;
                    memcpy(wiimote_conn.addr, name_addr, 6);
                    memcpy(wiimote_conn.class_of_device, &classic_state.pending_cod, 3);
                    strncpy(wiimote_conn.name, name, sizeof(wiimote_conn.name) - 1);
                    wiimote_conn.name[sizeof(wiimote_conn.name) - 1] = '\0';
                }

                // Deferred outgoing connection: name was unavailable at inquiry time,
                // so we requested it before connecting. Now that the name has resolved,
                // connect using the appropriate path (direct L2CAP vs HID Host).
                if (classic_state.pending_valid &&
                    classic_state.pending_outgoing &&
                    classic_state.pending_hid_connect &&
                    memcmp(name_addr, classic_state.pending_addr, 6) == 0) {
                    // Update pending name and re-lookup profile
                    strncpy(classic_state.pending_name, name, sizeof(classic_state.pending_name) - 1);
                    classic_state.pending_name[sizeof(classic_state.pending_name) - 1] = '\0';
                    classic_state.pending_hid_connect = false;

                    const bt_device_profile_t* deferred_profile = bt_device_lookup_by_name(name);
                    classic_state.pending_profile = deferred_profile;

                    if (deferred_profile->classic == BT_CLASSIC_DIRECT_L2CAP) {
                        printf("[BTSTACK_HOST] Deferred connect: %s detected, using direct L2CAP\n",
                               deferred_profile->name);
                        classic_state.pending_hid_connect = true;

                        memset(&wiimote_conn, 0, sizeof(wiimote_conn));
                        wiimote_conn.active = true;
                        wiimote_conn.state = WIIMOTE_STATE_IDLE;
                        memcpy(wiimote_conn.addr, name_addr, 6);
                        strncpy(wiimote_conn.name, name, sizeof(wiimote_conn.name) - 1);
                        wiimote_conn.class_of_device[0] = classic_state.pending_cod & 0xFF;
                        wiimote_conn.class_of_device[1] = (classic_state.pending_cod >> 8) & 0xFF;
                        wiimote_conn.class_of_device[2] = (classic_state.pending_cod >> 16) & 0xFF;

                        classic_connection_t* conn = find_free_classic_connection();
                        if (conn) {
                            int conn_index = conn - classic_state.connections;
                            memset(conn, 0, sizeof(*conn));
                            conn->active = true;
                            conn->hid_cid = 0xFFFF;
                            memcpy(conn->addr, name_addr, 6);
                            strncpy(conn->name, name, sizeof(conn->name) - 1);
                            conn->class_of_device[0] = classic_state.pending_cod & 0xFF;
                            conn->class_of_device[1] = (classic_state.pending_cod >> 8) & 0xFF;
                            conn->class_of_device[2] = (classic_state.pending_cod >> 16) & 0xFF;
                            conn->profile = deferred_profile;
                            wiimote_conn.conn_index = conn_index;
                        }

                        uint8_t status = gap_connect(name_addr, BD_ADDR_TYPE_ACL);
                        if (status != ERROR_CODE_SUCCESS && status != ERROR_CODE_COMMAND_DISALLOWED) {
                            printf("[BTSTACK_HOST] gap_connect failed: 0x%02X\n", status);
                            wiimote_conn.active = false;
                            classic_state.pending_hid_connect = false;
                        }
                    } else {
                        printf("[BTSTACK_HOST] Deferred connect: %s, using HID Host\n",
                               deferred_profile->name);
                        // Use profile's hid_mode for SDP bypass
                        hid_protocol_mode_t mode = (deferred_profile->hid_mode == BT_HID_MODE_FALLBACK)
                            ? HID_PROTOCOL_MODE_REPORT_WITH_FALLBACK_TO_BOOT
                            : HID_PROTOCOL_MODE_REPORT;
                        uint16_t hid_cid;
                        uint8_t status = hid_host_connect(name_addr, mode, &hid_cid);
                        if (status == ERROR_CODE_SUCCESS) {
                            printf("[BTSTACK_HOST] hid_host_connect started, cid=0x%04X\n", hid_cid);
                            classic_connection_t* conn = find_free_classic_connection();
                            if (conn) {
                                memset(conn, 0, sizeof(*conn));
                                conn->active = true;
                                conn->hid_cid = hid_cid;
                                memcpy(conn->addr, name_addr, 6);
                                strncpy(conn->name, name, sizeof(conn->name) - 1);
                                conn->class_of_device[0] = classic_state.pending_cod & 0xFF;
                                conn->class_of_device[1] = (classic_state.pending_cod >> 8) & 0xFF;
                                conn->class_of_device[2] = (classic_state.pending_cod >> 16) & 0xFF;
                                conn->profile = deferred_profile;
                            }
                        } else {
                            printf("[BTSTACK_HOST] hid_host_connect failed: %d\n", status);
                        }
                    }
                }
            }
            break;
        }

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            hci_con_handle_t handle = hci_event_disconnection_complete_get_connection_handle(packet);
            uint8_t reason = hci_event_disconnection_complete_get_reason(packet);

            printf("[BTSTACK_HOST] Disconnected: handle=0x%04X reason=0x%02X\n", handle, reason);

            ble_connection_t *conn = find_connection_by_handle(handle);
            if (conn && conn->conn_index > 0) {
                // Notify bthid layer before clearing connection
                // conn_index for BLE uses BLE_CONN_INDEX_OFFSET to distinguish from Classic
                printf("[BTSTACK_HOST] BLE disconnect: notifying bthid (conn_index=%d)\n", conn->conn_index);
                bt_on_disconnect(conn->conn_index);
                memset(conn, 0, sizeof(*conn));

                // BLE disconnect — manage BLE state and reconnection
                hid_state.state = BLE_STATE_IDLE;

                // Try to reconnect to last connected device if we have one stored
                if (hid_state.has_last_connected && hid_state.reconnect_attempts < 5) {
                    hid_state.reconnect_attempts++;
                    printf("[BTSTACK_HOST] Attempting BLE reconnection to stored device (attempt %d)...\n",
                           hid_state.reconnect_attempts);
                    printf("[BTSTACK_HOST] Connecting to %02X:%02X:%02X:%02X:%02X:%02X name='%s'\n",
                           hid_state.last_connected_addr[5], hid_state.last_connected_addr[4],
                           hid_state.last_connected_addr[3], hid_state.last_connected_addr[2],
                           hid_state.last_connected_addr[1], hid_state.last_connected_addr[0],
                           hid_state.last_connected_name);
                    // Copy stored name to pending so it's available when connection completes
                    strncpy(hid_state.pending_name, hid_state.last_connected_name, sizeof(hid_state.pending_name) - 1);
                    hid_state.pending_name[sizeof(hid_state.pending_name) - 1] = '\0';
                    btstack_host_connect_ble(hid_state.last_connected_addr, hid_state.last_connected_addr_type);
                } else if (btstack_classic_get_connection_count() == 0) {
                    // Resume scanning only if no devices remain
                    btstack_host_start_scan();
                }
            } else {
                // Classic BT disconnect — don't touch BLE state.
                // Classic reconnection/scanning is handled by HID_SUBEVENT_CONNECTION_CLOSED
                // or the outgoing HID failure handler. If we're waiting for an incoming
                // reconnection, don't restart scanning here.
                printf("[BTSTACK_HOST] Classic disconnect: handle=0x%04X (BLE state unchanged)\n", handle);
            }
            break;
        }

        case HCI_EVENT_LINK_KEY_REQUEST: {
            bd_addr_t req_addr;
            reverse_bytes(&packet[2], req_addr, 6);

            // Check if we have a stored link key
            link_key_t link_key;
            link_key_type_t key_type;
            bool have_key = gap_get_link_key_for_bd_addr(req_addr, link_key, &key_type);

            hci_connection_t *conn = hci_connection_for_bd_addr_and_type(req_addr, BD_ADDR_TYPE_ACL);
            printf("[BTSTACK_HOST] Link key request: %02X:%02X:%02X:%02X:%02X:%02X conn=%s have_key=%d type=%d\n",
                   req_addr[0], req_addr[1], req_addr[2], req_addr[3], req_addr[4], req_addr[5],
                   conn ? "YES" : "NO", have_key, have_key ? key_type : -1);

            // BTstack's hci.c handles this automatically - it will look up the key and respond
            // If no key is found, it sends negative reply which triggers PIN request for legacy pairing
            break;
        }

        // Legacy PIN code request - needed for Wiimote/Wii U Pro Controller
        // These devices don't support SSP and require a PIN code derived from BD_ADDR
        case HCI_EVENT_PIN_CODE_REQUEST: {
            bd_addr_t pin_addr;
            hci_event_pin_code_request_get_bd_addr(packet, pin_addr);
            printf("[BTSTACK_HOST] PIN code request: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   pin_addr[0], pin_addr[1], pin_addr[2], pin_addr[3], pin_addr[4], pin_addr[5]);

            // Check if device needs BD_ADDR-based PIN (Wiimote/Wii U Pro)
            bool needs_bdaddr_pin = false;
            if (classic_state.pending_valid &&
                bd_addr_cmp(pin_addr, classic_state.pending_addr) == 0) {
                const bt_device_profile_t* pin_profile = classic_state.pending_profile;
                if (!pin_profile && classic_state.pending_name[0]) {
                    pin_profile = bt_device_lookup_by_name(classic_state.pending_name);
                }
                if (pin_profile && pin_profile->pin_type == BT_PIN_BDADDR) {
                    needs_bdaddr_pin = true;
                }
            }
            // Also check wiimote_conn state (may have been set up during inquiry)
            if (!needs_bdaddr_pin && wiimote_conn.active &&
                memcmp(pin_addr, wiimote_conn.addr, 6) == 0) {
                needs_bdaddr_pin = true;
            }

            if (needs_bdaddr_pin) {
                // Wiimote PIN: host's BD_ADDR reversed (when using SYNC button)
                // The PIN is 6 bytes, which is the BD_ADDR in reverse byte order
                bd_addr_t local_addr;
                gap_local_bd_addr(local_addr);
                uint8_t pin[6];
                pin[0] = local_addr[5];
                pin[1] = local_addr[4];
                pin[2] = local_addr[3];
                pin[3] = local_addr[2];
                pin[4] = local_addr[1];
                pin[5] = local_addr[0];
                printf("[BTSTACK_HOST] BD_ADDR PIN device detected, sending PIN (host BD_ADDR reversed)\n");
                gap_pin_code_response_binary(pin_addr, pin, 6);
            } else {
                // No BD_ADDR PIN needed - reject (SSP devices shouldn't ask for PIN)
                printf("[BTSTACK_HOST] PIN request rejected (no BD_ADDR PIN profile)\n");
                gap_pin_code_negative(pin_addr);
            }
            break;
        }

        case HCI_EVENT_LINK_KEY_NOTIFICATION: {
            bd_addr_t notif_addr;
            reverse_bytes(&packet[2], notif_addr, 6);
            link_key_t link_key;
            memcpy(link_key, &packet[8], 16);
            link_key_type_t key_type = (link_key_type_t)packet[24];

            printf("[BTSTACK_HOST] Link key notification: %02X:%02X:%02X:%02X:%02X:%02X type=%d\n",
                   notif_addr[0], notif_addr[1], notif_addr[2], notif_addr[3], notif_addr[4], notif_addr[5], key_type);

            // Explicitly store the link key (BTstack's auto-storage may not work for legacy pairing)
            gap_store_link_key_for_bd_addr(notif_addr, link_key, key_type);
            break;
        }

        case HCI_EVENT_AUTHENTICATION_COMPLETE: {
            uint8_t status = packet[2];
            hci_con_handle_t handle = little_endian_read_16(packet, 3);
            printf("[BTSTACK_HOST] Authentication complete: handle=0x%04X status=0x%02X\n", handle, status);
            break;
        }

        case HCI_EVENT_ENCRYPTION_CHANGE: {
            hci_con_handle_t handle = hci_event_encryption_change_get_connection_handle(packet);
            uint8_t status = hci_event_encryption_change_get_status(packet);
            uint8_t enabled = hci_event_encryption_change_get_encryption_enabled(packet);

            printf("[BTSTACK_HOST] Encryption change: handle=0x%04X status=0x%02X enabled=%d\n",
                   handle, status, enabled);

            // For Wiimotes, create L2CAP control channel after encryption is enabled
            // This handles both initial pairing (state=IDLE) and reconnection (state=W4_CONTROL_CONNECTED)
            if (status == 0 && enabled && wiimote_conn.active &&
                wiimote_conn.acl_handle == handle &&
                (wiimote_conn.state == WIIMOTE_STATE_IDLE ||
                 wiimote_conn.state == WIIMOTE_STATE_W4_CONTROL_CONNECTED) &&
                wiimote_conn.control_cid == 0) {

                printf("[BTSTACK_HOST] Wiimote: encryption enabled, creating HID Control channel (PSM 0x11)...\n");

                uint16_t control_cid;
                uint8_t l2cap_status = l2cap_create_channel(wiimote_l2cap_packet_handler,
                                                            wiimote_conn.addr,
                                                            PSM_HID_CONTROL,
                                                            0xFFFF,  // MTU
                                                            &control_cid);
                if (l2cap_status == ERROR_CODE_SUCCESS) {
                    wiimote_conn.control_cid = control_cid;
                    wiimote_conn.state = WIIMOTE_STATE_W4_CONTROL_CONNECTED;
                    printf("[BTSTACK_HOST] Wiimote: L2CAP control channel request sent, cid=0x%04X\n", control_cid);
                } else {
                    printf("[BTSTACK_HOST] Wiimote: l2cap_create_channel failed: 0x%02X\n", l2cap_status);
                    wiimote_conn.active = false;
                    classic_state.pending_hid_connect = false;
                }
            }
            break;
        }

        case GAP_EVENT_SECURITY_LEVEL: {
            hci_con_handle_t handle = gap_event_security_level_get_handle(packet);
            gap_security_level_t level = gap_event_security_level_get_security_level(packet);
            printf("[BTSTACK_HOST] Security level update: handle=0x%04X level=%d\n", handle, level);
            break;
        }

        case HCI_EVENT_ROLE_CHANGE: {
            uint8_t status = hci_event_role_change_get_status(packet);
            bd_addr_t addr;
            hci_event_role_change_get_bd_addr(packet, addr);
            uint8_t role = hci_event_role_change_get_role(packet);
            printf("[BTSTACK_HOST] Role change: %02X:%02X:%02X:%02X:%02X:%02X status=%d role=%s\n",
                   addr[0], addr[1], addr[2], addr[3], addr[4], addr[5],
                   status, role == 0 ? "MASTER" : "SLAVE");
            break;
        }
    }
}

// ============================================================================
// SM EVENT HANDLER
// ============================================================================

static void sm_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event_type = hci_event_packet_get_type(packet);

    switch (event_type) {
        case SM_EVENT_JUST_WORKS_REQUEST:
            printf("[BTSTACK_HOST] SM: Just Works request\n");
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            break;

        case SM_EVENT_PAIRING_STARTED:
            printf("[BTSTACK_HOST] SM: Pairing started\n");
            break;

        case SM_EVENT_PAIRING_COMPLETE: {
            hci_con_handle_t handle = sm_event_pairing_complete_get_handle(packet);
            uint8_t status = sm_event_pairing_complete_get_status(packet);
            printf("[BTSTACK_HOST] SM: Pairing complete, handle=0x%04X status=0x%02X\n", handle, status);

            if (status == ERROR_CODE_SUCCESS) {
                printf("[BTSTACK_HOST] SM: Pairing successful!\n");
                ble_connection_t* conn = find_connection_by_handle(handle);
                if (conn) {
                    // Store for reconnection
                    memcpy(hid_state.last_connected_addr, conn->addr, 6);
                    hid_state.last_connected_addr_type = conn->addr_type;
                    strncpy(hid_state.last_connected_name, conn->name, sizeof(hid_state.last_connected_name) - 1);
                    hid_state.last_connected_name[sizeof(hid_state.last_connected_name) - 1] = '\0';
                    hid_state.has_last_connected = true;
                    hid_state.reconnect_attempts = 0;
                    printf("[BTSTACK_HOST] Stored device for reconnection: %02X:%02X:%02X:%02X:%02X:%02X name='%s'\n",
                           conn->addr[5], conn->addr[4], conn->addr[3], conn->addr[2], conn->addr[1], conn->addr[0],
                           hid_state.last_connected_name);

                    // Route based on BLE strategy
                    if (conn->profile && conn->profile->ble == BT_BLE_DIRECT_ATT) {
                        printf("[BTSTACK_HOST] %s detected - using fast-path HID listener\n",
                               conn->profile->name);
                        register_ble_hid_listener(handle);
                    } else if (conn->profile && conn->profile->ble == BT_BLE_CUSTOM) {
                        printf("[BTSTACK_HOST] %s detected - using fast-path notification enable\n",
                               conn->profile->name);
                        register_switch2_hid_listener(handle);
                    } else {
                        printf("[BTSTACK_HOST] BLE controller - starting GATT discovery\n");
                        start_hids_client(conn);
                    }
                }
            } else {
                printf("[BTSTACK_HOST] SM: Pairing FAILED\n");
            }
            break;
        }

        case SM_EVENT_REENCRYPTION_STARTED:
            printf("[BTSTACK_HOST] SM: Re-encryption started\n");
            break;

        case SM_EVENT_REENCRYPTION_COMPLETE: {
            hci_con_handle_t handle = sm_event_reencryption_complete_get_handle(packet);
            uint8_t status = sm_event_reencryption_complete_get_status(packet);
            printf("[BTSTACK_HOST] SM: Re-encryption complete, handle=0x%04X status=0x%02X\n", handle, status);
            if (status == ERROR_CODE_SUCCESS) {
                printf("[BTSTACK_HOST] SM: Re-encryption successful!\n");
                ble_connection_t* conn = find_connection_by_handle(handle);
                if (conn) {
                    // Reset reconnect counter on successful re-encryption
                    hid_state.reconnect_attempts = 0;

                    // Update stored device info (in case address type changed or for reconnection)
                    memcpy(hid_state.last_connected_addr, conn->addr, 6);
                    hid_state.last_connected_addr_type = conn->addr_type;
                    if (conn->name[0] != '\0') {
                        strncpy(hid_state.last_connected_name, conn->name, sizeof(hid_state.last_connected_name) - 1);
                        hid_state.last_connected_name[sizeof(hid_state.last_connected_name) - 1] = '\0';
                    }
                    hid_state.has_last_connected = true;

                    // Route based on BLE strategy
                    if (conn->profile && conn->profile->ble == BT_BLE_DIRECT_ATT) {
                        printf("[BTSTACK_HOST] %s detected - using fast-path HID listener\n",
                               conn->profile->name);
                        register_ble_hid_listener(handle);
                    } else if (conn->profile && conn->profile->ble == BT_BLE_CUSTOM) {
                        printf("[BTSTACK_HOST] %s detected - using fast-path notification enable\n",
                               conn->profile->name);
                        register_switch2_hid_listener(handle);
                    } else {
                        printf("[BTSTACK_HOST] BLE controller - starting GATT discovery\n");
                        start_hids_client(conn);
                    }
                }
            } else {
                // Re-encryption failed - remote likely lost bonding info
                // Delete local bonding and request fresh pairing
                printf("[BTSTACK_HOST] SM: Re-encryption failed, deleting bond and re-pairing...\n");
                bd_addr_t addr;
                sm_event_reencryption_complete_get_address(packet, addr);
                bd_addr_type_t addr_type = sm_event_reencryption_complete_get_addr_type(packet);
                gap_delete_bonding(addr_type, addr);
                sm_request_pairing(handle);
            }
            break;
        }
    }
}

// ============================================================================
// GATT CLIENT
// ============================================================================

static void gatt_client_callback(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event_type = hci_event_packet_get_type(packet);

    switch (event_type) {
        case GATT_EVENT_SERVICE_QUERY_RESULT: {
            gatt_client_service_t service;
            gatt_event_service_query_result_get_service(packet, &service);
            printf("[BTSTACK_HOST] GATT: Service 0x%04X-0x%04X UUID=0x%04X\n",
                   service.start_group_handle, service.end_group_handle,
                   service.uuid16);
            // Save HID service handles (UUID 0x1812)
            if (service.uuid16 == 0x1812) {
                hid_state.hid_service_start = service.start_group_handle;
                hid_state.hid_service_end = service.end_group_handle;
                printf("[BTSTACK_HOST] Found HID Service!\n");
            }
            break;
        }

        case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT: {
            gatt_client_characteristic_t characteristic;
            gatt_event_characteristic_query_result_get_characteristic(packet, &characteristic);
            printf("[BTSTACK_HOST] GATT: Char handle=0x%04X value=0x%04X end=0x%04X props=0x%02X UUID=0x%04X\n",
                   characteristic.start_handle, characteristic.value_handle,
                   characteristic.end_handle, characteristic.properties, characteristic.uuid16);
            // Save first Report characteristic (UUID 0x2A4D) with Notify property
            if (characteristic.uuid16 == 0x2A4D && (characteristic.properties & 0x10) &&
                hid_state.report_characteristic.value_handle == 0) {
                hid_state.report_characteristic = characteristic;
                printf("[BTSTACK_HOST] Found HID Report characteristic!\n");
            }
            break;
        }

        case GATT_EVENT_QUERY_COMPLETE: {
            uint8_t status = gatt_event_query_complete_get_att_status(packet);
            printf("[BTSTACK_HOST] GATT: Query complete, status=0x%02X, gatt_state=%d\n",
                   status, hid_state.gatt_state);

            if (status != 0) break;

            // State machine for GATT discovery
            if (hid_state.gatt_state == GATT_DISCOVERING_SERVICES) {
                if (hid_state.hid_service_start != 0) {
                    // Found HID, now discover its characteristics
                    printf("[BTSTACK_HOST] Discovering HID characteristics...\n");
                    hid_state.gatt_state = GATT_DISCOVERING_HID_CHARACTERISTICS;
                    gatt_client_discover_characteristics_for_handle_range_by_uuid16(
                        gatt_client_callback, hid_state.gatt_handle,
                        hid_state.hid_service_start, hid_state.hid_service_end,
                        0x2A4D);  // HID Report UUID
                } else {
                    printf("[BTSTACK_HOST] No HID service found!\n");
                }
            } else if (hid_state.gatt_state == GATT_DISCOVERING_HID_CHARACTERISTICS) {
                if (hid_state.report_characteristic.value_handle != 0) {
                    // Found Report char, enable notifications
                    printf("[BTSTACK_HOST] Enabling notifications on 0x%04X (end=0x%04X)...\n",
                           hid_state.report_characteristic.value_handle,
                           hid_state.report_characteristic.end_handle);
                    hid_state.gatt_state = GATT_ENABLING_NOTIFICATIONS;
                    gatt_client_write_client_characteristic_configuration(
                        gatt_client_callback, hid_state.gatt_handle,
                        &hid_state.report_characteristic,
                        GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
                } else {
                    printf("[BTSTACK_HOST] No HID Report characteristic found!\n");
                }
            } else if (hid_state.gatt_state == GATT_ENABLING_NOTIFICATIONS) {
                printf("[BTSTACK_HOST] Notifications enabled! Ready for HID reports.\n");
                hid_state.gatt_state = GATT_READY;
            }
            break;
        }

        case GATT_EVENT_NOTIFICATION: {
            hci_con_handle_t con_handle = gatt_event_notification_get_handle(packet);
            uint16_t value_handle = gatt_event_notification_get_value_handle(packet);
            uint16_t value_length = gatt_event_notification_get_value_length(packet);
            const uint8_t *value = gatt_event_notification_get_value(packet);

            // BLE HID Report characteristic (Xbox uses handle 0x001E)
            // Route through bthid layer
            if (value_handle == 0x001E && value_length >= 1) {
                int conn_index = get_ble_conn_index_by_handle(con_handle);
                if (conn_index >= 0) {
                    route_ble_hid_report(conn_index, value, value_length);
                }
            }
            break;
        }
    }
}

// ============================================================================
// DIRECT XBOX HID NOTIFICATION HANDLER
// ============================================================================

// Handle notifications directly from gatt_client listener API
static void ble_hid_notification_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != GATT_EVENT_NOTIFICATION) return;

    hci_con_handle_t con_handle = gatt_event_notification_get_handle(packet);
    uint16_t value_handle = gatt_event_notification_get_value_handle(packet);
    uint16_t value_length = gatt_event_notification_get_value_length(packet);
    const uint8_t *value = gatt_event_notification_get_value(packet);

    // Debug: log all notifications to identify chatpad/keyboard reports
    static uint16_t last_handle = 0;
    static uint16_t last_len = 0;
    if (value_handle != last_handle || value_length != last_len) {
        printf("[BTSTACK_HOST] BLE notif: handle=0x%04X len=%d data=%02X %02X %02X %02X\n",
               value_handle, value_length,
               value_length > 0 ? value[0] : 0,
               value_length > 1 ? value[1] : 0,
               value_length > 2 ? value[2] : 0,
               value_length > 3 ? value[3] : 0);
        last_handle = value_handle;
        last_len = value_length;
    }

    // Accept HID report notifications - filter by reasonable gamepad report length
    if (value_length < 10 || value_length > sizeof(pending_ble_report)) return;

    // Get conn_index for this BLE connection
    int conn_index = get_ble_conn_index_by_handle(con_handle);
    if (conn_index < 0) return;

    // Defer processing to main loop to avoid stack overflow
    memcpy(pending_ble_report, value, value_length);
    pending_ble_report_len = value_length;
    pending_ble_conn_index = (uint8_t)conn_index;
    ble_report_pending = true;
}

// Register direct listener for BLE HID notifications and notify bthid layer
static void register_ble_hid_listener(hci_con_handle_t con_handle)
{
    printf("[BTSTACK_HOST] Registering BLE HID listener for handle 0x%04X\n", con_handle);

    // Find the BLE connection
    ble_connection_t* conn = find_connection_by_handle(con_handle);
    if (!conn) {
        printf("[BTSTACK_HOST] ERROR: No connection for handle 0x%04X\n", con_handle);
        return;
    }

    // Assign conn_index if not already set
    int ble_index = -1;
    for (int i = 0; i < MAX_BLE_CONNECTIONS; i++) {
        if (&hid_state.connections[i] == conn) {
            ble_index = i;
            break;
        }
    }
    if (ble_index < 0) return;

    conn->conn_index = BLE_CONN_INDEX_OFFSET + ble_index;
    conn->hid_ready = true;

    // Set up a fake characteristic structure with just the value_handle
    // Xbox BLE HID Report characteristic value handle is 0x001E
    memset(&xbox_hid_characteristic, 0, sizeof(xbox_hid_characteristic));
    xbox_hid_characteristic.value_handle = 0x001E;
    xbox_hid_characteristic.end_handle = 0x001F;  // Approximate

    // Register to listen for notifications on the HID report characteristic
    gatt_client_listen_for_characteristic_value_updates(
        &xbox_hid_notification_listener,
        ble_hid_notification_handler,
        con_handle,
        &xbox_hid_characteristic);

    printf("[BTSTACK_HOST] BLE HID listener registered, conn_index=%d\n", conn->conn_index);

    // Notify bthid layer that device is ready
    btstack_host_stop_scan();
    scan_timeout_end = 0;
    printf("[BTSTACK_HOST] Calling bt_on_hid_ready(%d) for BLE device '%s'\n", conn->conn_index, conn->name);
    bt_on_hid_ready(conn->conn_index);
}

// ============================================================================
// SWITCH 2 BLE HID NOTIFICATION HANDLER
// ============================================================================

// Switch 2 ATT handles (from protocol documentation)
#define SW2_INPUT_REPORT_HANDLE     0x000A  // Input reports via notification
#define SW2_CCC_HANDLE              0x000B  // Client Characteristic Configuration
#define SW2_OUTPUT_REPORT_HANDLE    0x0012  // Rumble output
#define SW2_CMD_HANDLE              0x0014  // Command output
#define SW2_ACK_CCC_HANDLE          0x001B  // ACK notification CCC

// Switch 2 command constants
#define SW2_CMD_PAIRING             0x15
#define SW2_CMD_SET_LED             0x09
#define SW2_CMD_READ_SPI            0x02
#define SW2_REQ_TYPE_REQ            0x91
#define SW2_REQ_INT_BLE             0x01
#define SW2_SUBCMD_SET_LED          0x07
#define SW2_SUBCMD_READ_SPI         0x04
// Pairing subcmds - sent in order: STEP1 -> STEP2 -> STEP3 -> STEP4
// Note: Response ACK contains same subcmd as request
#define SW2_SUBCMD_PAIRING_STEP1    0x01  // Send BD address
#define SW2_SUBCMD_PAIRING_STEP2    0x04  // Send magic bytes 1
#define SW2_SUBCMD_PAIRING_STEP3    0x02  // Send magic bytes 2
#define SW2_SUBCMD_PAIRING_STEP4    0x03  // Complete pairing

// Init state machine states (matching BlueRetro's sequence)
typedef enum {
    SW2_INIT_IDLE = 0,
    SW2_INIT_READ_INFO,             // Read device info from SPI
    SW2_INIT_READ_LTK,              // Read LTK to check if paired
    SW2_INIT_PAIR_STEP1,            // Pairing step 1 (BD addr)
    SW2_INIT_PAIR_STEP2,            // Pairing step 2
    SW2_INIT_PAIR_STEP3,            // Pairing step 3
    SW2_INIT_PAIR_STEP4,            // Pairing step 4
    SW2_INIT_SET_LED,               // Set player LED
    SW2_INIT_DONE                   // Init complete
} sw2_init_state_t;

// Handle Switch 2 HID notifications
static void switch2_hid_notification_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != GATT_EVENT_NOTIFICATION) return;

    hci_con_handle_t con_handle = gatt_event_notification_get_handle(packet);
    uint16_t value_handle = gatt_event_notification_get_value_handle(packet);
    uint16_t value_length = gatt_event_notification_get_value_length(packet);
    const uint8_t *value = gatt_event_notification_get_value(packet);

    // Debug first notification
    static bool sw2_notif_debug = false;
    if (!sw2_notif_debug) {
        printf("[SW2_BLE] Notification: handle=0x%04X len=%d data=%02X %02X %02X %02X\n",
               value_handle, value_length,
               value_length > 0 ? value[0] : 0,
               value_length > 1 ? value[1] : 0,
               value_length > 2 ? value[2] : 0,
               value_length > 3 ? value[3] : 0);
        sw2_notif_debug = true;
    }

    // Switch 2 input reports are 64 bytes on handle 0x000A
    if (value_handle != SW2_INPUT_REPORT_HANDLE) return;
    if (value_length < 16 || value_length > sizeof(pending_ble_report)) return;

    // Get conn_index for this BLE connection
    int conn_index = get_ble_conn_index_by_handle(con_handle);
    if (conn_index < 0) return;

    // Defer processing to main loop to avoid stack overflow
    memcpy(pending_ble_report, value, value_length);
    pending_ble_report_len = value_length;
    pending_ble_conn_index = (uint8_t)conn_index;
    ble_report_pending = true;
}

// Forward declarations for Switch 2
static void switch2_send_next_init_cmd(hci_con_handle_t con_handle);

// CCC write completion handler for Switch 2 input reports
static void switch2_ccc_write_callback(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != GATT_EVENT_QUERY_COMPLETE) return;

    uint8_t status = gatt_event_query_complete_get_att_status(packet);
    hci_con_handle_t handle = gatt_event_query_complete_get_handle(packet);

    if (status == ATT_ERROR_SUCCESS) {
        printf("[SW2_BLE] Input notifications enabled for handle 0x%04X\n", handle);

        // Now register the notification listener
        ble_connection_t* conn = find_connection_by_handle(handle);
        if (conn) {
            // Update bthid with VID/PID BEFORE calling bt_on_hid_ready
            // so driver selection has correct info
            printf("[SW2_BLE] Updating device info: VID=0x%04X PID=0x%04X\n", conn->vid, conn->pid);
            bthid_update_device_info(conn->conn_index, conn->name, conn->vid, conn->pid);

            // Notify bthid layer that device is ready
            btstack_host_stop_scan();
            scan_timeout_end = 0;
            printf("[SW2_BLE] Calling bt_on_hid_ready(%d) for Switch 2 device\n", conn->conn_index);
            bt_on_hid_ready(conn->conn_index);
        }
    } else {
        printf("[SW2_BLE] Failed to enable input notifications: status=0x%02X\n", status);
    }
}

// CCC write completion handler for Switch 2 ACK notifications
static void switch2_ack_ccc_write_callback(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != GATT_EVENT_QUERY_COMPLETE) return;

    uint8_t status = gatt_event_query_complete_get_att_status(packet);
    hci_con_handle_t handle = gatt_event_query_complete_get_handle(packet);

    if (status == ATT_ERROR_SUCCESS) {
        printf("[SW2_BLE] ACK notifications enabled for handle 0x%04X\n", handle);

        // Now enable input report notifications
        static uint8_t ccc_enable[] = { 0x01, 0x00 };
        printf("[SW2_BLE] Enabling input notifications on CCC handle 0x%04X\n", SW2_CCC_HANDLE);
        gatt_client_write_value_of_characteristic(
            switch2_ccc_write_callback, handle, SW2_CCC_HANDLE, sizeof(ccc_enable), ccc_enable);

        // Start the pairing sequence
        printf("[SW2_BLE] Starting pairing sequence\n");
        switch2_send_next_init_cmd(handle);
    } else {
        printf("[SW2_BLE] Failed to enable ACK notifications: status=0x%02X\n", status);
    }
}

// Switch 2 init state machine
static sw2_init_state_t sw2_init_state = SW2_INIT_IDLE;
static hci_con_handle_t sw2_init_handle = 0;

// ACK notification listener for Switch 2 commands
static gatt_client_notification_t switch2_ack_notification_listener;
static gatt_client_characteristic_t switch2_ack_characteristic;

// Forward declare
static void switch2_send_init_cmd(hci_con_handle_t con_handle);

static void switch2_ack_notification_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != GATT_EVENT_NOTIFICATION) return;

    uint16_t value_handle = gatt_event_notification_get_value_handle(packet);
    uint16_t value_length = gatt_event_notification_get_value_length(packet);
    const uint8_t *value = gatt_event_notification_get_value(packet);
    hci_con_handle_t con_handle = gatt_event_notification_get_handle(packet);

    // Debug: print all notifications (not just 0x001A) to see what's coming in
    static bool ack_notif_debug = false;
    if (!ack_notif_debug && value_handle != SW2_INPUT_REPORT_HANDLE) {
        printf("[SW2_BLE] ACK listener got notification: handle=0x%04X len=%d\n",
               value_handle, value_length);
        ack_notif_debug = true;
    }

    if (value_handle != 0x001A) return;  // ACK handle

    if (value_length < 4) return;
    uint8_t cmd = value[0];
    uint8_t subcmd = value[3];

    printf("[SW2_BLE] ACK: cmd=0x%02X subcmd=0x%02X state=%d len=%d\n",
           cmd, subcmd, sw2_init_state, value_length);

    // Handle ACK based on current init state
    switch (cmd) {
        case SW2_CMD_READ_SPI:
            if (sw2_init_state == SW2_INIT_READ_INFO) {
                // Got device info, extract VID/PID if needed
                if (value_length >= 34) {
                    uint16_t vid = value[30] | (value[31] << 8);
                    uint16_t pid = value[32] | (value[33] << 8);
                    printf("[SW2_BLE] Device info: VID=0x%04X PID=0x%04X\n", vid, pid);
                }
                // Skip LTK check for now, go straight to pairing
                sw2_init_state = SW2_INIT_PAIR_STEP1;
                switch2_send_init_cmd(con_handle);
            } else if (sw2_init_state == SW2_INIT_READ_LTK) {
                // Check LTK, for now just proceed to pairing
                sw2_init_state = SW2_INIT_PAIR_STEP1;
                switch2_send_init_cmd(con_handle);
            }
            break;

        case SW2_CMD_PAIRING:
            switch (subcmd) {
                case SW2_SUBCMD_PAIRING_STEP1:
                    if (sw2_init_state == SW2_INIT_PAIR_STEP1) {
                        sw2_init_state = SW2_INIT_PAIR_STEP2;
                        switch2_send_init_cmd(con_handle);
                    }
                    break;
                case SW2_SUBCMD_PAIRING_STEP2:
                    if (sw2_init_state == SW2_INIT_PAIR_STEP2) {
                        sw2_init_state = SW2_INIT_PAIR_STEP3;
                        switch2_send_init_cmd(con_handle);
                    }
                    break;
                case SW2_SUBCMD_PAIRING_STEP3:
                    if (sw2_init_state == SW2_INIT_PAIR_STEP3) {
                        sw2_init_state = SW2_INIT_PAIR_STEP4;
                        switch2_send_init_cmd(con_handle);
                    }
                    break;
                case SW2_SUBCMD_PAIRING_STEP4:
                    if (sw2_init_state == SW2_INIT_PAIR_STEP4) {
                        printf("[SW2_BLE] Pairing complete! Setting LED...\n");
                        sw2_init_state = SW2_INIT_SET_LED;
                        switch2_send_init_cmd(con_handle);
                    }
                    break;
            }
            break;

        case SW2_CMD_SET_LED:
            if (sw2_init_state == SW2_INIT_SET_LED) {
                printf("[SW2_BLE] LED set! Init done.\n");
                sw2_init_state = SW2_INIT_DONE;
            }
            break;
    }
}

static void switch2_send_init_cmd(hci_con_handle_t con_handle)
{
    printf("[SW2_BLE] Sending init cmd, state=%d\n", sw2_init_state);

    switch (sw2_init_state) {
        case SW2_INIT_READ_INFO: {
            // Read device info from SPI (BlueRetro's first step)
            uint8_t read_info[] = {
                SW2_CMD_READ_SPI,       // 0x02
                SW2_REQ_TYPE_REQ,       // 0x91
                SW2_REQ_INT_BLE,        // 0x01
                SW2_SUBCMD_READ_SPI,    // 0x04
                0x00, 0x08, 0x00, 0x00,
                0x40,                   // Read length
                0x7e, 0x00, 0x00,       // Address type
                0x00, 0x30, 0x01, 0x00  // SPI address
            };
            gatt_client_write_value_of_characteristic_without_response(
                con_handle, SW2_CMD_HANDLE, sizeof(read_info), read_info);
            printf("[SW2_BLE] READ_INFO sent\n");
            break;
        }

        case SW2_INIT_PAIR_STEP1: {
            // Pairing step 1: Send our BD address
            bd_addr_t local_addr;
            gap_local_bd_addr(local_addr);
            printf("[SW2_BLE] Pair Step 1: BD addr = %02X:%02X:%02X:%02X:%02X:%02X\n",
                   local_addr[5], local_addr[4], local_addr[3],
                   local_addr[2], local_addr[1], local_addr[0]);

            uint8_t pair1[] = {
                SW2_CMD_PAIRING,        // 0x15
                SW2_REQ_TYPE_REQ,       // 0x91
                SW2_REQ_INT_BLE,        // 0x01
                SW2_SUBCMD_PAIRING_STEP1, // 0x01
                0x00, 0x0e, 0x00, 0x00, 0x00, 0x02,
                // 6 bytes: our BD addr
                local_addr[0], local_addr[1], local_addr[2],
                local_addr[3], local_addr[4], local_addr[5],
                // 6 bytes: our BD addr - 1
                (uint8_t)(local_addr[0] - 1), local_addr[1], local_addr[2],
                local_addr[3], local_addr[4], local_addr[5],
            };
            gatt_client_write_value_of_characteristic_without_response(
                con_handle, SW2_CMD_HANDLE, sizeof(pair1), pair1);
            break;
        }

        case SW2_INIT_PAIR_STEP2: {
            // Pairing step 2: Magic bytes (from BlueRetro)
            uint8_t pair2[] = {
                SW2_CMD_PAIRING,        // 0x15
                SW2_REQ_TYPE_REQ,       // 0x91
                SW2_REQ_INT_BLE,        // 0x01
                SW2_SUBCMD_PAIRING_STEP2, // 0x04
                0x00, 0x11, 0x00, 0x00, 0x00,
                0xea, 0xbd, 0x47, 0x13, 0x89, 0x35, 0x42, 0xc6,
                0x79, 0xee, 0x07, 0xf2, 0x53, 0x2c, 0x6c, 0x31
            };
            gatt_client_write_value_of_characteristic_without_response(
                con_handle, SW2_CMD_HANDLE, sizeof(pair2), pair2);
            printf("[SW2_BLE] Pair Step 2 sent\n");
            break;
        }

        case SW2_INIT_PAIR_STEP3: {
            // Pairing step 3: More magic bytes
            uint8_t pair3[] = {
                SW2_CMD_PAIRING,        // 0x15
                SW2_REQ_TYPE_REQ,       // 0x91
                SW2_REQ_INT_BLE,        // 0x01
                SW2_SUBCMD_PAIRING_STEP3, // 0x02
                0x00, 0x11, 0x00, 0x00, 0x00,
                0x40, 0xb0, 0x8a, 0x5f, 0xcd, 0x1f, 0x9b, 0x41,
                0x12, 0x5c, 0xac, 0xc6, 0x3f, 0x38, 0xa0, 0x73
            };
            gatt_client_write_value_of_characteristic_without_response(
                con_handle, SW2_CMD_HANDLE, sizeof(pair3), pair3);
            printf("[SW2_BLE] Pair Step 3 sent\n");
            break;
        }

        case SW2_INIT_PAIR_STEP4: {
            // Pairing step 4: Completion
            uint8_t pair4[] = {
                SW2_CMD_PAIRING,        // 0x15
                SW2_REQ_TYPE_REQ,       // 0x91
                SW2_REQ_INT_BLE,        // 0x01
                SW2_SUBCMD_PAIRING_STEP4, // 0x03
                0x00, 0x01, 0x00, 0x00, 0x00
            };
            gatt_client_write_value_of_characteristic_without_response(
                con_handle, SW2_CMD_HANDLE, sizeof(pair4), pair4);
            printf("[SW2_BLE] Pair Step 4 sent\n");
            break;
        }

        case SW2_INIT_SET_LED: {
            // Set player LED
            uint8_t led_cmd[] = {
                SW2_CMD_SET_LED,        // 0x09
                SW2_REQ_TYPE_REQ,       // 0x91
                SW2_REQ_INT_BLE,        // 0x01
                SW2_SUBCMD_SET_LED,     // 0x07
                0x00, 0x08, 0x00, 0x00,
                0x01,  // Player 1 LED pattern
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            };
            gatt_client_write_value_of_characteristic_without_response(
                con_handle, SW2_CMD_HANDLE, sizeof(led_cmd), led_cmd);
            printf("[SW2_BLE] LED command sent\n");
            break;
        }

        default:
            printf("[SW2_BLE] Unknown init state: %d\n", sw2_init_state);
            break;
    }
}

static void switch2_send_next_init_cmd(hci_con_handle_t con_handle)
{
    // Start the init sequence with READ_INFO (like BlueRetro does)
    if (sw2_init_state == SW2_INIT_IDLE) {
        printf("[SW2_BLE] Starting init sequence with READ_INFO...\n");
        sw2_init_state = SW2_INIT_READ_INFO;
        switch2_send_init_cmd(con_handle);
    } else if (sw2_init_state == SW2_INIT_DONE) {
        printf("[SW2_BLE] Init already done\n");
    } else {
        // Init in progress, wait for ACK
        printf("[SW2_BLE] Init in progress (state=%d)\n", sw2_init_state);
    }
}

// Retry init if stuck (called from main loop)
static void switch2_retry_init_if_needed(void)
{
    static uint32_t retry_counter = 0;
    retry_counter++;

    if (sw2_init_state != SW2_INIT_IDLE && sw2_init_state != SW2_INIT_DONE && sw2_init_handle != 0) {
        // Retry every ~500ms (assuming ~120Hz main loop = 60 counts)
        if (retry_counter % 60 == 0) {
            printf("[SW2_BLE] Retrying init cmd (state=%d, attempt=%lu)\n",
                   sw2_init_state, (unsigned long)(retry_counter / 60));
            switch2_send_init_cmd(sw2_init_handle);
        }
    }
}

// ============================================================================
// SWITCH 2 RUMBLE/HAPTICS
// ============================================================================
// Switch 2 Pro Controller uses LRA (Linear Resonant Actuator) haptics.
// Output goes to ATT handle 0x0012.
// LRA ops format: 5 bytes per op (4-byte bitfield + 1-byte hf_amp)
// Each side (L/R) has 1 state byte + 3 ops = 16 bytes
// Total output: 1 + 16 + 16 + 9 padding = 42 bytes

// Rumble state tracking
static uint8_t sw2_last_rumble_left = 0;
static uint8_t sw2_last_rumble_right = 0;
static uint8_t sw2_rumble_tid = 0;
static uint32_t sw2_rumble_send_counter = 0;

// Player LED state tracking
static uint8_t sw2_last_player_led = 0;

// Player LED patterns (cumulative, matching joypad-web)
static const uint8_t SW2_PLAYER_LED_PATTERNS[] = {
    0x01,  // Player 1: 1 LED
    0x03,  // Player 2: 2 LEDs
    0x07,  // Player 3: 3 LEDs
    0x0F,  // Player 4: 4 LEDs
};

// Send player LED command to Switch 2 controller
static void switch2_send_player_led(hci_con_handle_t con_handle, uint8_t pattern)
{
    uint8_t led_cmd[] = {
        SW2_CMD_SET_LED,        // 0x09
        SW2_REQ_TYPE_REQ,       // 0x91
        SW2_REQ_INT_BLE,        // 0x01
        SW2_SUBCMD_SET_LED,     // 0x07
        0x00, 0x08, 0x00, 0x00,
        pattern,  // Player LED pattern
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    gatt_client_write_value_of_characteristic_without_response(
        con_handle, SW2_CMD_HANDLE, sizeof(led_cmd), led_cmd);
}

// Encode haptic data for one motor (5 bytes)
// Based on joypad-web's encodeSwitch2Haptic() function
// Format: [amplitude, frequency, amplitude, frequency, flags]
// Key: Lower frequency = more felt, higher frequency = audible tones
// freq 0x60 = felt rumble, freq 0xFE = audible (avoid this)
static void encode_haptic(uint8_t* out, uint8_t intensity)
{
    if (intensity == 0) {
        // Off state
        out[0] = 0x00;
        out[1] = 0x00;
        out[2] = 0x00;
        out[3] = 0x00;
        out[4] = 0x00;
    } else {
        // Active rumble - use low frequency for felt vibration
        // Amplitude: scale from 0x40 to 0xFF based on intensity
        uint8_t amp = 0x40 + ((intensity * 0xBF) / 255);
        // Frequency: use 0x40-0x60 range for low rumble (more felt, less audible)
        // Lower values = lower frequency = more physical sensation
        uint8_t freq = 0x40;  // Low frequency for maximum felt rumble
        out[0] = amp;   // High band amplitude
        out[1] = freq;  // High band frequency (low value = felt)
        out[2] = amp;   // Low band amplitude
        out[3] = freq;  // Low band frequency
        out[4] = 0x00;  // Flags
    }
}

// Send rumble command to Switch 2 controller via BLE
// Pro uses HD haptics, GameCube uses simple on/off
static void switch2_send_rumble(hci_con_handle_t con_handle, uint8_t left, uint8_t right)
{
    // Get connection to check PID
    ble_connection_t* conn = find_connection_by_handle(con_handle);

    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));

    // Counter with state bits
    uint8_t counter = 0x50 | (sw2_rumble_tid & 0x0F);
    sw2_rumble_tid++;

    if (conn && conn->pid == 0x2073) {
        // GameCube controller: simple on/off rumble
        // Format: byte 1 = counter, byte 2 = on/off state
        buf[1] = counter;
        buf[2] = (left || right) ? 0x01 : 0x00;

        gatt_client_write_value_of_characteristic_without_response(
            con_handle, SW2_OUTPUT_REPORT_HANDLE, 21, buf);
    } else {
        // Pro controller: HD haptics format
        // [1]: Counter (0x5X)
        // [2-6]: Left haptic (5 bytes)
        // [17]: Counter duplicate
        // [18-22]: Right haptic (5 bytes)
        buf[1] = counter;
        buf[17] = counter;  // Duplicate counter

        // Encode left motor haptic (bytes 2-6)
        encode_haptic(&buf[2], left);

        // Encode right motor haptic (bytes 18-22)
        encode_haptic(&buf[18], right);

        gatt_client_write_value_of_characteristic_without_response(
            con_handle, SW2_OUTPUT_REPORT_HANDLE, sizeof(buf), buf);
    }
}

// Check feedback system and send rumble/LED if needed (called from task loop)
static void switch2_handle_feedback(void)
{
    // Only process if we have an active Switch 2 connection
    if (sw2_init_state != SW2_INIT_DONE || sw2_init_handle == 0) return;

    sw2_rumble_send_counter++;

    // Get conn_index from HCI handle
    int conn_index = get_ble_conn_index_by_handle(sw2_init_handle);
    if (conn_index < 0) return;

    // Find player index for this device
    int player_idx = find_player_index(conn_index, 0);
    if (player_idx < 0) return;

    // Get feedback state
    feedback_state_t* fb = feedback_get_state(player_idx);
    if (!fb) return;

    // --- Handle Player LED ---
    if (fb->led_dirty) {
        // Determine LED pattern from feedback
        uint8_t led_pattern = 0x01;  // Default to player 1

        if (fb->led.pattern != 0) {
            // Use pattern bits directly (0x01=P1, 0x02=P2, 0x04=P3, 0x08=P4)
            // Convert to cumulative pattern for Switch 2
            if (fb->led.pattern & 0x08) led_pattern = SW2_PLAYER_LED_PATTERNS[3];
            else if (fb->led.pattern & 0x04) led_pattern = SW2_PLAYER_LED_PATTERNS[2];
            else if (fb->led.pattern & 0x02) led_pattern = SW2_PLAYER_LED_PATTERNS[1];
            else led_pattern = SW2_PLAYER_LED_PATTERNS[0];
        } else {
            // Use player index if no explicit pattern
            int idx = (player_idx >= 0 && player_idx < 4) ? player_idx : 0;
            led_pattern = SW2_PLAYER_LED_PATTERNS[idx];
        }

        if (led_pattern != sw2_last_player_led) {
            sw2_last_player_led = led_pattern;
            switch2_send_player_led(sw2_init_handle, led_pattern);
        }
    }

    // --- Handle Rumble ---
    bool value_changed = (fb->rumble.left != sw2_last_rumble_left ||
                          fb->rumble.right != sw2_last_rumble_right);

    // Send rumble if:
    // 1. Values changed, OR
    // 2. Rumble is active and we need periodic refresh (every ~50ms at 120Hz = 6 ticks)
    bool need_refresh = (sw2_last_rumble_left > 0 || sw2_last_rumble_right > 0) &&
                        (sw2_rumble_send_counter % 6 == 0);

    if (fb->rumble_dirty || value_changed || need_refresh) {
        sw2_last_rumble_left = fb->rumble.left;
        sw2_last_rumble_right = fb->rumble.right;

        switch2_send_rumble(sw2_init_handle, fb->rumble.left, fb->rumble.right);
    }

    // Clear dirty flags after processing
    if (fb->rumble_dirty || fb->led_dirty) {
        feedback_clear_dirty(player_idx);
    }
}

// Register Switch 2 notification listener and enable notifications
static void register_switch2_hid_listener(hci_con_handle_t con_handle)
{
    printf("[SW2_BLE] Registering Switch 2 HID listener for handle 0x%04X\n", con_handle);

    // Find the BLE connection
    ble_connection_t* conn = find_connection_by_handle(con_handle);
    if (!conn) {
        printf("[SW2_BLE] ERROR: No connection for handle 0x%04X\n", con_handle);
        return;
    }

    // Assign conn_index if not already set
    int ble_index = -1;
    for (int i = 0; i < MAX_BLE_CONNECTIONS; i++) {
        if (&hid_state.connections[i] == conn) {
            ble_index = i;
            break;
        }
    }
    if (ble_index < 0) return;

    conn->conn_index = BLE_CONN_INDEX_OFFSET + ble_index;
    conn->hid_ready = true;
    sw2_init_handle = con_handle;
    sw2_init_state = SW2_INIT_IDLE;

    printf("[SW2_BLE] Connection: VID=0x%04X PID=0x%04X conn_index=%d\n",
           conn->vid, conn->pid, conn->conn_index);

    // Set up ACK notification listener (handle 0x001A)
    memset(&switch2_ack_characteristic, 0, sizeof(switch2_ack_characteristic));
    switch2_ack_characteristic.value_handle = 0x001A;
    switch2_ack_characteristic.end_handle = 0x001A + 1;

    gatt_client_listen_for_characteristic_value_updates(
        &switch2_ack_notification_listener,
        switch2_ack_notification_handler,
        con_handle,
        &switch2_ack_characteristic);

    // Set up input report notification listener (handle 0x000A)
    memset(&switch2_hid_characteristic, 0, sizeof(switch2_hid_characteristic));
    switch2_hid_characteristic.value_handle = SW2_INPUT_REPORT_HANDLE;
    switch2_hid_characteristic.end_handle = SW2_INPUT_REPORT_HANDLE + 1;

    gatt_client_listen_for_characteristic_value_updates(
        &switch2_hid_notification_listener,
        switch2_hid_notification_handler,
        con_handle,
        &switch2_hid_characteristic);

    printf("[SW2_BLE] Notification listeners registered\n");

    // Enable notifications on ACK handle first (0x001B) - wait for confirmation
    static uint8_t ccc_enable[] = { 0x01, 0x00 };
    printf("[SW2_BLE] Enabling ACK notifications on CCC handle 0x%04X\n", SW2_ACK_CCC_HANDLE);
    gatt_client_write_value_of_characteristic(
        switch2_ack_ccc_write_callback, con_handle, SW2_ACK_CCC_HANDLE, sizeof(ccc_enable), ccc_enable);
}

static void start_hids_client(ble_connection_t *conn)
{
    printf("[BTSTACK_HOST] Connecting HIDS client...\n");

    conn->state = BLE_STATE_DISCOVERING;
    hid_state.gatt_handle = conn->handle;

    uint8_t status = hids_client_connect(conn->handle, hids_client_handler,
                                         HID_PROTOCOL_MODE_REPORT, &hid_state.hids_cid);

    printf("[BTSTACK_HOST] hids_client_connect returned %d, cid=0x%04X\n",
           status, hid_state.hids_cid);
}

static void hids_client_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(packet_type);  // hids_client passes HCI_EVENT_GATTSERVICE_META, not HCI_EVENT_PACKET
    UNUSED(channel);
    UNUSED(size);

    // Check the event type in the packet itself
    if (hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META) return;

    switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
        case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
            uint8_t status = gattservice_subevent_hid_service_connected_get_status(packet);
            uint8_t num_instances = gattservice_subevent_hid_service_connected_get_num_instances(packet);
            printf("[BTSTACK_HOST] HIDS connected! status=%d instances=%d\n", status, num_instances);

            if (status == ERROR_CODE_SUCCESS) {
                ble_connection_t *conn = find_connection_by_handle(hid_state.gatt_handle);
                if (conn) {
                    conn->state = BLE_STATE_READY;
                    conn->hid_ready = true;

                    // Assign conn_index if not already set
                    for (int i = 0; i < MAX_BLE_CONNECTIONS; i++) {
                        if (&hid_state.connections[i] == conn) {
                            conn->conn_index = BLE_CONN_INDEX_OFFSET + i;
                            break;
                        }
                    }

                    // Notify bthid layer that device is ready
                    btstack_host_stop_scan();
                    scan_timeout_end = 0;
                    printf("[BTSTACK_HOST] Calling bt_on_hid_ready(%d) for BLE device '%s'\n",
                           conn->conn_index, conn->name);
                    bt_on_hid_ready(conn->conn_index);
                }

                // Explicitly enable notifications
                printf("[BTSTACK_HOST] Enabling HID notifications...\n");
                uint8_t result = hids_client_enable_notifications(hid_state.hids_cid);
                printf("[BTSTACK_HOST] enable_notifications returned %d\n", result);
            }
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_SERVICE_REPORTS_NOTIFICATION: {
            uint8_t configuration = gattservice_subevent_hid_service_reports_notification_get_configuration(packet);
            printf("[BTSTACK_HOST] HID Reports Notification configured: %d\n", configuration);
            printf("[BTSTACK_HOST] Ready to receive HID reports!\n");
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_REPORT: {
            uint16_t report_len = gattservice_subevent_hid_report_get_report_len(packet);
            const uint8_t *report = gattservice_subevent_hid_report_get_report(packet);

            // Route BLE HID report through bthid layer
            int conn_index = get_ble_conn_index_by_handle(hid_state.gatt_handle);
            if (conn_index >= 0) {
                route_ble_hid_report(conn_index, report, report_len);
            }

            // Forward to callback if set
            if (hid_state.report_callback) {
                hid_state.report_callback(hid_state.gatt_handle, report, report_len);
            }
            break;
        }

        default:
            printf("[BTSTACK_HOST] GATT service subevent: 0x%02X\n",
                   hci_event_gattservice_meta_get_subevent_code(packet));
            break;
    }
}

// ============================================================================
// HELPERS
// ============================================================================

static ble_connection_t* find_connection_by_handle(hci_con_handle_t handle)
{
    for (int i = 0; i < MAX_BLE_CONNECTIONS; i++) {
        if (hid_state.connections[i].handle == handle) {
            return &hid_state.connections[i];
        }
    }
    return NULL;
}

static ble_connection_t* find_free_connection(void)
{
    for (int i = 0; i < MAX_BLE_CONNECTIONS; i++) {
        if (hid_state.connections[i].handle == 0) {
            return &hid_state.connections[i];
        }
    }
    return NULL;
}

// ============================================================================
// STATUS
// ============================================================================

bool btstack_host_is_initialized(void)
{
    return hid_state.initialized;
}

bool btstack_host_is_powered_on(void)
{
    return hid_state.powered_on;
}

bool btstack_host_is_scanning(void)
{
    return hid_state.scan_active || classic_state.inquiry_active;
}

// ============================================================================
// CLASSIC BT HID HOST PACKET HANDLER
// ============================================================================

static void hid_host_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t event_type = hci_event_packet_get_type(packet);
    if (event_type != HCI_EVENT_HID_META) return;

    uint8_t subevent = hci_event_hid_meta_get_subevent_code(packet);

    switch (subevent) {
        case HID_SUBEVENT_INCOMING_CONNECTION: {
            // Accept incoming HID connections from devices
            uint16_t hid_cid = hid_subevent_incoming_connection_get_hid_cid(packet);
            bd_addr_t incoming_addr;
            hid_subevent_incoming_connection_get_address(packet, incoming_addr);

            // For Wiimotes/Wii U Pro: accept HID Host connection for reconnection
            if (wiimote_conn.active && memcmp(incoming_addr, wiimote_conn.addr, 6) == 0) {
                printf("[BTSTACK_HOST] Wiimote HID incoming - accepting\n");
                wiimote_conn.using_hid_host = true;
                wiimote_conn.hid_host_cid = hid_cid;
                hid_host_accept_connection(hid_cid, HID_PROTOCOL_MODE_REPORT);

                // Allocate classic_connection slot for HID_SUBEVENT_CONNECTION_OPENED to find
                classic_connection_t* conn = find_free_classic_connection();
                if (conn) {
                    memset(conn, 0, sizeof(*conn));
                    conn->active = true;
                    conn->hid_cid = hid_cid;
                    memcpy(conn->addr, wiimote_conn.addr, 6);
                    memcpy(conn->class_of_device, wiimote_conn.class_of_device, 3);
                    strncpy(conn->name, wiimote_conn.name, sizeof(conn->name) - 1);
                    conn->vendor_id = 0x057E;  // Nintendo
                    // Get index for wiimote_conn
                    for (int i = 0; i < MAX_CLASSIC_CONNECTIONS; i++) {
                        if (&classic_state.connections[i] == conn) {
                            wiimote_conn.conn_index = i;
                            printf("[BTSTACK_HOST] Wiimote: allocated conn_index=%d for HID Host\n", i);
                            break;
                        }
                    }
                }
                break;
            }

            printf("[BTSTACK_HOST] HID incoming connection, cid=0x%04X - accepting\n", hid_cid);
            hid_host_accept_connection(hid_cid, HID_PROTOCOL_MODE_REPORT);

            // Allocate connection slot if needed
            if (!find_classic_connection_by_cid(hid_cid)) {
                classic_connection_t* conn = find_free_classic_connection();
                if (conn) {
                    memset(conn, 0, sizeof(*conn));
                    conn->active = true;
                    conn->hid_cid = hid_cid;
                    hid_subevent_incoming_connection_get_address(packet, conn->addr);

                    // Use pending COD and name if address matches (from HCI_EVENT_CONNECTION_REQUEST)
                    if (classic_state.pending_valid &&
                        memcmp(conn->addr, classic_state.pending_addr, 6) == 0) {
                        conn->class_of_device[0] = classic_state.pending_cod & 0xFF;
                        conn->class_of_device[1] = (classic_state.pending_cod >> 8) & 0xFF;
                        conn->class_of_device[2] = (classic_state.pending_cod >> 16) & 0xFF;
                        // Copy name if we got it from remote name request
                        if (classic_state.pending_name[0]) {
                            strncpy(conn->name, classic_state.pending_name, sizeof(conn->name) - 1);
                            conn->name[sizeof(conn->name) - 1] = '\0';
                            printf("[BTSTACK_HOST] Using pending name: %s\n", conn->name);
                        }
                        // Copy VID/PID if we got them from SDP query
                        if (classic_state.pending_vid || classic_state.pending_pid) {
                            conn->vendor_id = classic_state.pending_vid;
                            conn->product_id = classic_state.pending_pid;
                            printf("[BTSTACK_HOST] Using pending VID/PID: 0x%04X/0x%04X\n",
                                   conn->vendor_id, conn->product_id);
                        }
                        // DON'T clear pending_valid here - PIN code request may come after this
                        // It will be cleared in HID_SUBEVENT_CONNECTION_OPENED
                        printf("[BTSTACK_HOST] Using pending COD: 0x%06X\n", (unsigned)classic_state.pending_cod);
                    }
                }
            }
            break;
        }

        case HID_SUBEVENT_CONNECTION_OPENED: {
            uint16_t hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
            uint8_t status = hid_subevent_connection_opened_get_status(packet);

            // Reset security level if we elevated it for Wiimote
            if (classic_state.pending_hid_connect) {
                printf("[BTSTACK_HOST] Resetting security level to 0\n");
                gap_set_security_level(LEVEL_0);
                classic_state.pending_hid_connect = false;
            }

            // Clear pending connection info now that HID is established
            classic_state.pending_valid = false;

            if (status != ERROR_CODE_SUCCESS) {
                printf("[BTSTACK_HOST] HID connection failed, status=0x%02X\n", status);
                // Remove connection slot
                classic_connection_t* conn = find_classic_connection_by_cid(hid_cid);
                if (conn) {
                    memset(conn, 0, sizeof(*conn));
                }

                // If this was an outgoing connection, disconnect ACL and wait for
                // the device to reconnect via the incoming path. Some controllers
                // (certain DS4 HW revisions) don't accept HID L2CAP channels from
                // the host but work when they initiate the connection themselves.
                // A link key was exchanged during the failed attempt, so when the
                // device reconnects (incoming), authentication will use the stored key.
                // Don't resume scanning — otherwise we'll rediscover the device
                // still in pairing mode and loop endlessly.
                if (!hid_subevent_connection_opened_get_incoming(packet)) {
                    hci_con_handle_t con_handle = hid_subevent_connection_opened_get_con_handle(packet);
                    printf("[BTSTACK_HOST] Outgoing HID failed, disconnecting to allow incoming reconnect\n");
                    gap_disconnect(con_handle);
                    classic_state.pending_outgoing = false;
                    classic_state.pending_valid = false;
                    // Don't scan — stay connectable, wait for incoming reconnection
                    classic_state.waiting_for_incoming_time = btstack_run_loop_get_time_ms();
                }
                return;
            }

            printf("[BTSTACK_HOST] HID connection opened, cid=0x%04X\n", hid_cid);

            // Mark connection as ready (HID channels established)
            classic_connection_t* conn = find_classic_connection_by_cid(hid_cid);
            if (conn) {
                conn->hid_ready = true;

                // Check if this is a direct-L2CAP device by profile or name
                bool is_direct_l2cap = (conn->profile &&
                                        conn->profile->classic == BT_CLASSIC_DIRECT_L2CAP);
                // Also check by name if profile wasn't set (late name resolution)
                if (!is_direct_l2cap && conn->name[0]) {
                    const bt_device_profile_t* conn_profile = bt_device_lookup_by_name(conn->name);
                    if (conn_profile->classic == BT_CLASSIC_DIRECT_L2CAP) {
                        is_direct_l2cap = true;
                        conn->profile = conn_profile;
                    }
                }
                // Also check wiimote_conn state (may have been set up during inquiry)
                if (!is_direct_l2cap && wiimote_conn.active &&
                    memcmp(conn->addr, wiimote_conn.addr, 6) == 0) {
                    is_direct_l2cap = true;
                }

                if (is_direct_l2cap) {
                    // Direct L2CAP devices: HID Host handles receiving, we send via direct L2CAP
                    printf("[BTSTACK_HOST] %s HID connected via HID Host (receive via HID Host, send via L2CAP)\n",
                           conn->profile ? conn->profile->name : "Wiimote");

                    // Set default VID/PID from profile if not already set
                    if (conn->vendor_id == 0 && conn->profile && conn->profile->default_vid) {
                        conn->vendor_id = conn->profile->default_vid;
                    }
                    if (conn->product_id == 0) {
                        uint16_t pid = bt_device_wiimote_pid_from_name(conn->name);
                        if (pid) {
                            conn->product_id = pid;
                            printf("[BTSTACK_HOST] Detected %s by name, PID=0x%04X\n",
                                   conn->profile ? conn->profile->name : "device", pid);
                        }
                    }

                    // Initialize wiimote_conn if not already active (e.g., incoming
                    // reconnection where name wasn't available at CONNECTION_COMPLETE)
                    if (!wiimote_conn.active) {
                        memset(&wiimote_conn, 0, sizeof(wiimote_conn));
                        wiimote_conn.active = true;
                        wiimote_conn.state = WIIMOTE_STATE_IDLE;
                        memcpy(wiimote_conn.addr, conn->addr, 6);
                        memcpy(wiimote_conn.class_of_device, conn->class_of_device, 3);
                        wiimote_conn.using_hid_host = true;
                        wiimote_conn.hid_host_cid = hid_cid;
                    }

                    // Link wiimote_conn to this classic_connection slot for routing
                    int conn_index = get_classic_conn_index(hid_cid);
                    if (conn_index >= 0) {
                        wiimote_conn.conn_index = conn_index;
                        wiimote_conn.vendor_id = conn->vendor_id;
                        wiimote_conn.product_id = conn->product_id;
                        strncpy(wiimote_conn.name, conn->name, sizeof(wiimote_conn.name) - 1);

                        bthid_update_device_info(conn_index, conn->name,
                                                 conn->vendor_id, conn->product_id);

                        // NOTE: We previously called hid_host_get_l2cap_cids() here to get L2CAP CIDs
                        // from HID Host for direct L2CAP sending (Wiimotes don't support SET_PROTOCOL).
                        // This required a custom BTstack patch (see .dev/docs/btstack-patches.md).
                        // Removed because CI uses upstream BTstack without the patch.
                        // If Wiimote HID Host mode has issues, consider re-adding the patch.

                        printf("[BTSTACK_HOST] Wiimote: conn_index=%d control_cid=0x%04X interrupt_cid=0x%04X using_hid_host=%d\n",
                               conn_index, wiimote_conn.control_cid, wiimote_conn.interrupt_cid, wiimote_conn.using_hid_host);

                        if (wiimote_conn.using_hid_host) {
                            // Using HID Host fallback - ready to go
                            wiimote_conn.hid_host_ready = true;
                            wiimote_conn.state = WIIMOTE_STATE_CONNECTED;
                            btstack_host_stop_scan();
                            scan_timeout_end = 0;
                            printf("[BTSTACK_HOST] Wiimote: calling bt_on_hid_ready(%d) via HID Host\n", conn_index);
                            bt_on_hid_ready(conn_index);
                        } else if (wiimote_conn.control_cid != 0 && wiimote_conn.interrupt_cid != 0) {
                            printf("[BTSTACK_HOST] Wiimote: calling bt_on_hid_ready(%d) via direct L2CAP\n", conn_index);
                            bt_on_hid_ready(conn_index);
                        } else {
                            printf("[BTSTACK_HOST] Wiimote: waiting for L2CAP CIDs before ready\n");
                        }
                    }
                } else {
                    // For non-Wiimote devices, query SDP for VID/PID if we don't have it
                    if (conn->vendor_id == 0 && conn->product_id == 0) {
                        // Store pending info for SDP callback
                        memcpy(classic_state.pending_addr, conn->addr, 6);
                        classic_state.pending_vid = 0;
                        classic_state.pending_pid = 0;

                        // Query VID/PID via SDP (PnP Information service)
                        sdp_client_query_uuid16(&sdp_query_vid_pid_callback, conn->addr,
                                                BLUETOOTH_SERVICE_CLASS_PNP_INFORMATION);

                        // Also request remote name if we don't have it
                        if (conn->name[0] == '\0') {
                            gap_remote_name_request(conn->addr, 0, 0);
                        }
                    }
                    // Non-Wiimote: wait for HID_SUBEVENT_DESCRIPTOR_AVAILABLE
                }
            }
            break;
        }

        case HID_SUBEVENT_DESCRIPTOR_AVAILABLE: {
            uint16_t hid_cid = hid_subevent_descriptor_available_get_hid_cid(packet);
            uint8_t status = hid_subevent_descriptor_available_get_status(packet);

            printf("[BTSTACK_HOST] HID descriptor available, cid=0x%04X status=0x%02X\n", hid_cid, status);

            // Skip for Wiimotes - they already called bt_on_hid_ready from CONNECTION_OPENED
            if (wiimote_conn.active && wiimote_conn.hid_host_cid == hid_cid) {
                printf("[BTSTACK_HOST] Skipping bt_on_hid_ready for Wiimote (already initialized)\n");
                break;
            }

            // Notify bthid layer that device is ready (non-Wiimote devices)
            int conn_index = get_classic_conn_index(hid_cid);
            if (conn_index >= 0) {
                btstack_host_stop_scan();
                scan_timeout_end = 0;
                printf("[BTSTACK_HOST] Calling bt_on_hid_ready(%d)\n", conn_index);
                bt_on_hid_ready(conn_index);
            }
            break;
        }

        case HID_SUBEVENT_REPORT: {
            uint16_t hid_cid = hid_subevent_report_get_hid_cid(packet);
            const uint8_t* report = hid_subevent_report_get_report(packet);
            uint16_t report_len = hid_subevent_report_get_report_len(packet);

            // Debug: show raw BTstack report
            static bool btstack_report_debug_done = false;
            if (!btstack_report_debug_done && report_len >= 4) {
                printf("[BTSTACK_HOST] Raw report len=%d: %02X %02X %02X %02X\n",
                       report_len, report[0], report[1], report[2], report[3]);
                btstack_report_debug_done = true;
            }

            // Route to bthid layer
            // BTstack report already includes 0xA1 header (DATA|INPUT)
            int conn_index = get_classic_conn_index(hid_cid);
            if (conn_index >= 0 && report_len > 0) {
                bt_on_hid_report(conn_index, report, report_len);
            }
            break;
        }

        case HID_SUBEVENT_CONNECTION_CLOSED: {
            uint16_t hid_cid = hid_subevent_connection_closed_get_hid_cid(packet);
            printf("[BTSTACK_HOST] HID connection closed, cid=0x%04X\n", hid_cid);

            // Notify bthid layer
            int conn_index = get_classic_conn_index(hid_cid);
            if (conn_index >= 0) {
                bt_on_disconnect(conn_index);
            }

            // Free connection slot
            classic_connection_t* conn = find_classic_connection_by_cid(hid_cid);
            if (conn) {
                memset(conn, 0, sizeof(*conn));
            }

            // Resume scanning if no devices remain
            if (btstack_classic_get_connection_count() == 0) {
                printf("[BTSTACK_HOST] No devices connected, resuming scan\n");
                btstack_host_start_scan();
            }
            break;
        }

        case HID_SUBEVENT_SET_PROTOCOL_RESPONSE: {
            uint16_t hid_cid = hid_subevent_set_protocol_response_get_hid_cid(packet);
            uint8_t handshake = hid_subevent_set_protocol_response_get_handshake_status(packet);
            hid_protocol_mode_t mode = hid_subevent_set_protocol_response_get_protocol_mode(packet);
            printf("[BTSTACK_HOST] HID set protocol response: cid=0x%04X handshake=%d mode=%d\n",
                   hid_cid, handshake, mode);
            break;
        }

        default:
            printf("[BTSTACK_HOST] HID subevent: 0x%02X\n", subevent);
            break;
    }
}

// ============================================================================
// WIIMOTE DIRECT L2CAP PACKET HANDLER
// ============================================================================

static void wiimote_l2cap_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);

    switch (packet_type) {
        case HCI_EVENT_PACKET: {
            uint8_t event_type = hci_event_packet_get_type(packet);

            if (event_type == L2CAP_EVENT_CHANNEL_OPENED) {
                uint8_t status = l2cap_event_channel_opened_get_status(packet);
                uint16_t local_cid = l2cap_event_channel_opened_get_local_cid(packet);
                uint16_t psm = l2cap_event_channel_opened_get_psm(packet);

                printf("[BTSTACK_HOST] Wiimote L2CAP opened: status=%d PSM=0x%04X cid=0x%04X\n",
                       status, psm, local_cid);

                if (status != 0) {
                    printf("[BTSTACK_HOST] Wiimote: L2CAP channel failed: 0x%02X\n", status);
                    // Don't deactivate - wait for HID Host to handle via HID_SUBEVENT_INCOMING_CONNECTION
                    // (timing varies: HID incoming may come before or after L2CAP failure)
                    printf("[BTSTACK_HOST] Wiimote: waiting for HID Host fallback\n");
                    return;
                }

                if (psm == PSM_HID_CONTROL && wiimote_conn.state == WIIMOTE_STATE_W4_CONTROL_CONNECTED) {
                    // Control channel opened, now create interrupt channel
                    printf("[BTSTACK_HOST] Wiimote: Control channel connected, creating Interrupt channel (PSM 0x13)...\n");

                    uint16_t interrupt_cid;
                    uint8_t l2cap_status = l2cap_create_channel(wiimote_l2cap_packet_handler,
                                                                wiimote_conn.addr,
                                                                PSM_HID_INTERRUPT,
                                                                0xFFFF,
                                                                &interrupt_cid);
                    if (l2cap_status == ERROR_CODE_SUCCESS) {
                        wiimote_conn.interrupt_cid = interrupt_cid;
                        wiimote_conn.state = WIIMOTE_STATE_W4_INTERRUPT_CONNECTED;
                        printf("[BTSTACK_HOST] Wiimote: L2CAP interrupt channel request sent, cid=0x%04X\n", interrupt_cid);
                    } else {
                        printf("[BTSTACK_HOST] Wiimote: l2cap_create_channel (interrupt) failed: 0x%02X\n", l2cap_status);
                        wiimote_conn.active = false;
                        classic_state.pending_hid_connect = false;
                    }

                } else if (psm == PSM_HID_INTERRUPT && wiimote_conn.state == WIIMOTE_STATE_W4_INTERRUPT_CONNECTED) {
                    // Interrupt channel opened - connection complete!
                    printf("[BTSTACK_HOST] Wiimote: Interrupt channel connected - HID READY!\n");
                    wiimote_conn.state = WIIMOTE_STATE_CONNECTED;
                    classic_state.pending_hid_connect = false;

                    // Stop scanning now that we have a connected device
                    btstack_host_stop_scan();
                    scan_timeout_end = 0;

                    // Allocate classic connection slot if not already allocated (reconnection case)
                    if (wiimote_conn.conn_index < 0) {
                        classic_connection_t* conn = find_free_classic_connection();
                        if (conn) {
                            memset(conn, 0, sizeof(*conn));
                            conn->active = true;
                            conn->hid_cid = 0xFFFF;  // Mark as Wiimote (no HID Host CID)
                            memcpy(conn->addr, wiimote_conn.addr, 6);
                            strncpy(conn->name, wiimote_conn.name, sizeof(conn->name) - 1);
                            conn->vendor_id = 0x057E;  // Nintendo
                            conn->product_id = bt_device_wiimote_pid_from_name(wiimote_conn.name);
                            conn->hid_ready = true;

                            // Get index
                            for (int i = 0; i < MAX_CLASSIC_CONNECTIONS; i++) {
                                if (&classic_state.connections[i] == conn) {
                                    wiimote_conn.conn_index = i;
                                    wiimote_conn.vendor_id = conn->vendor_id;
                                    wiimote_conn.product_id = conn->product_id;
                                    printf("[BTSTACK_HOST] Wiimote: allocated conn_index=%d\n", i);
                                    break;
                                }
                            }
                        }
                    }

                    // Update the classic connection slot
                    if (wiimote_conn.conn_index >= 0 && wiimote_conn.conn_index < MAX_CLASSIC_CONNECTIONS) {
                        classic_connection_t* conn = &classic_state.connections[wiimote_conn.conn_index];
                        conn->hid_ready = true;

                        // Update bthid with device info
                        // Use SDP VID/PID if available, otherwise default to Nintendo (0x057E)
                        uint16_t vid = wiimote_conn.vendor_id ? wiimote_conn.vendor_id : 0x057E;
                        uint16_t pid = wiimote_conn.product_id;
                        printf("[BTSTACK_HOST] Wiimote: updating bthid with name='%s' VID=0x%04X PID=0x%04X\n",
                               wiimote_conn.name, vid, pid);
                        bthid_update_device_info(wiimote_conn.conn_index, wiimote_conn.name, vid, pid);

                        // Notify bthid layer
                        printf("[BTSTACK_HOST] Wiimote: calling bt_on_hid_ready(%d)\n", wiimote_conn.conn_index);
                        bt_on_hid_ready(wiimote_conn.conn_index);
                    }
                }

            } else if (event_type == L2CAP_EVENT_CHANNEL_CLOSED) {
                uint16_t local_cid = l2cap_event_channel_closed_get_local_cid(packet);
                printf("[BTSTACK_HOST] Wiimote L2CAP closed: cid=0x%04X\n", local_cid);

                if (wiimote_conn.active &&
                    (local_cid == wiimote_conn.control_cid || local_cid == wiimote_conn.interrupt_cid)) {
                    // Notify disconnect
                    if (wiimote_conn.conn_index >= 0) {
                        bt_on_disconnect(wiimote_conn.conn_index);
                        // Clear connection slot
                        if (wiimote_conn.conn_index < MAX_CLASSIC_CONNECTIONS) {
                            memset(&classic_state.connections[wiimote_conn.conn_index], 0, sizeof(classic_connection_t));
                        }
                    }
                    memset(&wiimote_conn, 0, sizeof(wiimote_conn));
                }
            }
            break;
        }

        case L2CAP_DATA_PACKET: {
            // HID data from Wiimote interrupt channel
            // Data already includes HID header (0xA1 for DATA|INPUT)
            if (wiimote_conn.active && wiimote_conn.state == WIIMOTE_STATE_CONNECTED) {
                // Route to bthid layer
                if (wiimote_conn.conn_index >= 0 && size > 0) {
                    bt_on_hid_report(wiimote_conn.conn_index, packet, size);
                }
            } else {
                printf("[BTSTACK_HOST] Wiimote data dropped: active=%d state=%d\n",
                       wiimote_conn.active, wiimote_conn.state);
            }
            break;
        }

        default:
            break;
    }
}

// ============================================================================
// CLASSIC BT OUTPUT REPORTS
// ============================================================================

// Send SET_REPORT on control channel with specified report type
// report_type: 1=Input, 2=Output, 3=Feature
bool btstack_classic_send_set_report_type(uint8_t conn_index, uint8_t report_type,
                                           uint8_t report_id, const uint8_t* data, uint16_t len)
{
    if (conn_index >= MAX_CLASSIC_CONNECTIONS) return false;

    classic_connection_t* conn = &classic_state.connections[conn_index];
    if (!conn->active || !conn->hid_ready) return false;

    // Map report type to BTstack enum
    hid_report_type_t hid_type;
    switch (report_type) {
        case 1: hid_type = HID_REPORT_TYPE_INPUT; break;
        case 2: hid_type = HID_REPORT_TYPE_OUTPUT; break;
        case 3: hid_type = HID_REPORT_TYPE_FEATURE; break;
        default: hid_type = HID_REPORT_TYPE_OUTPUT; break;
    }

    uint8_t status = hid_host_send_set_report(conn->hid_cid, hid_type, report_id, data, len);
    if (status != ERROR_CODE_SUCCESS) {
        printf("[BTSTACK_HOST] send_set_report failed: type=%d id=0x%02X status=%d\n",
               report_type, report_id, status);
    }
    return status == ERROR_CODE_SUCCESS;
}

// Send SET_REPORT on control channel (default to OUTPUT type)
bool btstack_classic_send_set_report(uint8_t conn_index, uint8_t report_id,
                                      const uint8_t* data, uint16_t len)
{
    return btstack_classic_send_set_report_type(conn_index, 2, report_id, data, len);
}

// Send DATA on interrupt channel (for regular output reports)
bool btstack_classic_send_report(uint8_t conn_index, uint8_t report_id,
                                  const uint8_t* data, uint16_t len)
{
    if (conn_index >= MAX_CLASSIC_CONNECTIONS) return false;

    classic_connection_t* conn = &classic_state.connections[conn_index];
    if (!conn->active || !conn->hid_ready) return false;

    // Check if this is a Wiimote (direct L2CAP, marked with hid_cid = 0xFFFF)
    if (conn->hid_cid == 0xFFFF && wiimote_conn.active &&
        wiimote_conn.conn_index == conn_index &&
        wiimote_conn.state == WIIMOTE_STATE_CONNECTED) {
        // Build HID packet: 0xA2 (DATA|OUTPUT) + report_id + data
        static uint8_t wiimote_send_buf[64];
        if (len + 2 > sizeof(wiimote_send_buf)) return false;
        wiimote_send_buf[0] = 0xA2;  // DATA | OUTPUT
        wiimote_send_buf[1] = report_id;
        memcpy(wiimote_send_buf + 2, data, len);
        return l2cap_send(wiimote_conn.interrupt_cid, wiimote_send_buf, len + 2) == ERROR_CODE_SUCCESS;
    }

    return hid_host_send_report(conn->hid_cid, report_id, data, len) == ERROR_CODE_SUCCESS;
}

// Check if a connection is a Wiimote (using direct L2CAP)
bool btstack_wiimote_is_connection(uint8_t conn_index)
{
    if (conn_index >= MAX_CLASSIC_CONNECTIONS) return false;
    classic_connection_t* conn = &classic_state.connections[conn_index];
    // Wiimote connections are marked with hid_cid = 0xFFFF
    return conn->active && conn->hid_cid == 0xFFFF &&
           wiimote_conn.active && wiimote_conn.conn_index == conn_index;
}

// Check if we can send on Wiimote L2CAP channel
bool btstack_wiimote_can_send(uint8_t conn_index)
{
    if (!wiimote_conn.active) {
        return false;
    }

    // Prefer direct L2CAP when we have the interrupt CID
    if (wiimote_conn.interrupt_cid != 0) {
        return l2cap_can_send_packet_now(wiimote_conn.interrupt_cid) != 0;
    }

    // Fallback to HID Host path
    if (wiimote_conn.using_hid_host && wiimote_conn.hid_host_ready) {
        return true;  // HID Host handles flow control internally
    }

    return false;
}

// Send raw L2CAP data to Wiimote on INTERRUPT channel
bool btstack_wiimote_send_raw(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    printf("[BTSTACK_HOST] wiimote_send_raw: active=%d using_hid=%d hid_ready=%d int_cid=0x%04X\n",
           wiimote_conn.active, wiimote_conn.using_hid_host, wiimote_conn.hid_host_ready, wiimote_conn.interrupt_cid);

    if (!wiimote_conn.active) {
        printf("[BTSTACK_HOST] wiimote_send_raw: no active connection\n");
        return false;
    }
    if (len == 0 || len > 64) {
        printf("[BTSTACK_HOST] wiimote_send_raw: bad len=%d\n", len);
        return false;
    }

    // Prefer direct L2CAP when we have the interrupt CID (works even with HID Host)
    // This bypasses hid_host_send_report which can fail with 0x0C if HID Host state isn't ready
    if (wiimote_conn.interrupt_cid != 0) {
        if (!l2cap_can_send_packet_now(wiimote_conn.interrupt_cid)) {
            printf("[BTSTACK_HOST] wiimote_send_raw: L2CAP not ready to send\n");
            return false;
        }

        uint8_t status = l2cap_send(wiimote_conn.interrupt_cid, data, len);
        if (status != ERROR_CODE_SUCCESS) {
            printf("[BTSTACK_HOST] wiimote_send_raw: l2cap_send failed status=0x%02X\n", status);
        } else {
            printf("[BTSTACK_HOST] wiimote_send_raw: sent %d bytes on INTR cid=0x%04X (0x%02X 0x%02X...)\n",
                   len, wiimote_conn.interrupt_cid, data[0], len > 1 ? data[1] : 0);
        }
        return status == ERROR_CODE_SUCCESS;
    }

    // Fallback to HID Host when using_hid_host but no direct CID (shouldn't happen normally)
    if (wiimote_conn.using_hid_host && wiimote_conn.hid_host_ready) {
        // Data format: first byte is 0xA2, second is report ID, rest is data
        if (len < 2) return false;
        uint8_t report_id = data[1];
        printf("[BTSTACK_HOST] wiimote_send_raw via HID Host: cid=0x%04X report=0x%02X len=%d\n",
               wiimote_conn.hid_host_cid, report_id, len - 2);
        uint8_t status = hid_host_send_report(wiimote_conn.hid_host_cid, report_id, &data[2], len - 2);
        if (status == ERROR_CODE_SUCCESS) {
            printf("[BTSTACK_HOST] wiimote_send_raw: sent %d bytes via HID Host\n", len);
        } else {
            printf("[BTSTACK_HOST] wiimote_send_raw: HID Host send failed status=0x%02X\n", status);
        }
        return status == ERROR_CODE_SUCCESS;
    }

    printf("[BTSTACK_HOST] wiimote_send_raw: no interrupt CID and HID Host not ready\n");
    return false;
}

// Send raw L2CAP data to Wiimote on CONTROL channel
bool btstack_wiimote_send_control(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    printf("[BTSTACK_HOST] wiimote_send_control: idx=%d len=%d control_cid=0x%04X using_hid_host=%d\n",
           conn_index, len, wiimote_conn.control_cid, wiimote_conn.using_hid_host);

    if (!wiimote_conn.active) {
        printf("[BTSTACK_HOST] wiimote_send_control: no active connection\n");
        return false;
    }
    if (len == 0 || len > 64) {
        printf("[BTSTACK_HOST] wiimote_send_control: bad len=%d\n", len);
        return false;
    }

    // Prefer direct L2CAP when we have the control CID (works even with HID Host)
    if (wiimote_conn.control_cid != 0) {
        if (!l2cap_can_send_packet_now(wiimote_conn.control_cid)) {
            printf("[BTSTACK_HOST] wiimote_send_control: L2CAP not ready to send\n");
            return false;
        }

        // Convert DATA format (0xA2) to SET_REPORT format (0x52) for control channel
        // Some Wii U Pro Controllers are strict and reject DATA transactions on control channel
        uint8_t send_buf[64];
        memcpy(send_buf, data, len);
        if (send_buf[0] == 0xA2) {
            send_buf[0] = 0x52;  // SET_REPORT | OUTPUT
        }

        printf("[BTSTACK_HOST] wiimote_send_control via L2CAP: cid=0x%04X len=%d hdr=0x%02X\n",
               wiimote_conn.control_cid, len, send_buf[0]);
        uint8_t status = l2cap_send(wiimote_conn.control_cid, send_buf, len);
        if (status != ERROR_CODE_SUCCESS) {
            printf("[BTSTACK_HOST] wiimote_send_control: l2cap_send failed status=0x%02X\n", status);
        }
        return status == ERROR_CODE_SUCCESS;
    }

    // Fallback to HID Host when using_hid_host but no direct CID
    if (wiimote_conn.using_hid_host && wiimote_conn.hid_host_ready) {
        // Data format: first byte is 0x52 (SET_REPORT), second is report type+ID
        if (len < 2) return false;
        uint8_t report_id = data[1];
        uint8_t status = hid_host_send_set_report(wiimote_conn.hid_host_cid, HID_REPORT_TYPE_OUTPUT,
                                                   report_id, &data[2], len - 2);
        if (status == ERROR_CODE_SUCCESS) {
            printf("[BTSTACK_HOST] wiimote_send_control: sent %d bytes via HID Host\n", len);
        }
        return status == ERROR_CODE_SUCCESS;
    }

    printf("[BTSTACK_HOST] wiimote_send_control: no control CID and HID Host not ready\n");
    return false;
}

// Get connection info for bthid driver matching (Classic or BLE)
bool btstack_classic_get_connection(uint8_t conn_index, btstack_classic_conn_info_t* info)
{
    if (!info) return false;

    // Check if this is a BLE connection (conn_index >= BLE_CONN_INDEX_OFFSET)
    if (conn_index >= BLE_CONN_INDEX_OFFSET) {
        uint8_t ble_index = conn_index - BLE_CONN_INDEX_OFFSET;
        if (ble_index >= MAX_BLE_CONNECTIONS) return false;

        ble_connection_t* conn = &hid_state.connections[ble_index];
        if (conn->handle == 0) return false;

        info->active = true;
        memcpy(info->bd_addr, conn->addr, 6);
        strncpy(info->name, conn->name, sizeof(info->name) - 1);
        info->name[sizeof(info->name) - 1] = '\0';
        // BLE devices don't have class_of_device, set to zeros
        memset(info->class_of_device, 0, 3);
        // Use VID/PID from BLE manufacturer data (e.g., Switch 2)
        info->vendor_id = conn->vid;
        info->product_id = conn->pid;
        info->hid_ready = conn->hid_ready;
        info->is_ble = true;

        return true;
    }

    // Classic connection
    if (conn_index >= MAX_CLASSIC_CONNECTIONS) return false;

    classic_connection_t* conn = &classic_state.connections[conn_index];
    if (!conn->active) return false;

    info->active = conn->active;
    memcpy(info->bd_addr, conn->addr, 6);
    strncpy(info->name, conn->name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    memcpy(info->class_of_device, conn->class_of_device, 3);
    info->vendor_id = conn->vendor_id;
    info->product_id = conn->product_id;
    info->hid_ready = conn->hid_ready;
    info->is_ble = false;

    return true;
}

// Get number of active connections (Classic + BLE)
uint8_t btstack_classic_get_connection_count(void)
{
    uint8_t count = 0;
    for (int i = 0; i < MAX_CLASSIC_CONNECTIONS; i++) {
        if (classic_state.connections[i].active) {
            count++;
        }
    }
    for (int i = 0; i < MAX_BLE_CONNECTIONS; i++) {
        if (hid_state.connections[i].handle != 0) {
            count++;
        }
    }
    return count;
}

// ============================================================================
// DISCONNECT ALL
// ============================================================================

void btstack_host_disconnect_all_devices(void)
{
    printf("[BTSTACK_HOST] Disconnecting all devices...\n");

    for (int i = 0; i < MAX_CLASSIC_CONNECTIONS; i++) {
        if (classic_state.connections[i].active && classic_state.connections[i].hid_cid) {
            hid_host_disconnect(classic_state.connections[i].hid_cid);
        }
    }
    for (int i = 0; i < MAX_BLE_CONNECTIONS; i++) {
        if (hid_state.connections[i].handle != 0) {
            gap_disconnect(hid_state.connections[i].handle);
        }
    }

    // Clear reconnection state so we don't try to reconnect to cleared devices
    hid_state.has_last_connected = false;
    hid_state.reconnect_attempts = 0;
}

// ============================================================================
// BOND MANAGEMENT
// ============================================================================

void btstack_host_delete_all_bonds(void)
{
    printf("[BTSTACK_HOST] Deleting all Bluetooth bonds...\n");

#ifndef BTSTACK_USE_CYW43
    // Erase BTstack flash banks to force clean re-initialization
    // This is more reliable than using BTstack's delete APIs when flash was corrupted
    btstack_erase_flash_banks();

    // Re-initialize the TLV context to pick up the erased banks
    const hal_flash_bank_t *flash_bank = pico_flash_bank_instance();
    btstack_tlv_flash_bank_init_instance(&btstack_tlv_flash_bank_context,
                                          flash_bank, NULL);
    printf("[BTSTACK_HOST] TLV re-initialized with clean flash banks\n");
#else
    // For CYW43, use BTstack's standard APIs
    gap_delete_all_link_keys();
    printf("[BTSTACK_HOST] Classic BT link keys deleted\n");

    int ble_count = le_device_db_count();
    le_device_db_init();
    printf("[BTSTACK_HOST] BLE bonds deleted (was %d devices)\n", ble_count);
#endif

    printf("[BTSTACK_HOST] All bonds cleared. Devices will need to re-pair.\n");
}
