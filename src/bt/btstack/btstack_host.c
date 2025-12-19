// btstack_host.c - BTstack HID Host (BLE + Classic)
//
// Transport-agnostic BTstack integration for HID devices.
// Uses BTstack's SM (Security Manager) for LE Secure Connections,
// GATT client for HID over GATT Profile (HOGP), and
// HID Host for Classic BT HID devices.

#include "btstack_host.h"
#include "btstack_config.h"
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

// ============================================================================
// BLE HID REPORT ROUTING
// ============================================================================

// Deferred processing to avoid stack overflow in BTstack callback
static uint8_t pending_ble_report[32];
static uint16_t pending_ble_report_len = 0;
static uint8_t pending_ble_conn_index = 0;
static volatile bool ble_report_pending = false;

// Forward declare the function to route BLE reports through bthid layer
static void route_ble_hid_report(uint8_t conn_index, const uint8_t* data, uint16_t len);

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
    bool is_xbox;

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
} classic_connection_t;

static struct {
    bool inquiry_active;
    classic_connection_t connections[MAX_CLASSIC_CONNECTIONS];
    // Pending incoming connection info (from HCI_EVENT_CONNECTION_REQUEST)
    bd_addr_t pending_addr;
    uint32_t pending_cod;
    char pending_name[64];
    uint16_t pending_vid;
    uint16_t pending_pid;
    bool pending_valid;
    // Pending HID connect (deferred until encryption completes)
    bd_addr_t pending_hid_addr;
    hci_con_handle_t pending_hid_handle;
    bool pending_hid_connect;
} classic_state;

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
    static uint8_t hid_packet[64];
    hid_packet[0] = 0xA1;  // DATA | INPUT header
    if (len < sizeof(hid_packet) - 1) {
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

    // Get the Pico SDK flash bank HAL instance
    const hal_flash_bank_t *hal_flash_bank_impl = pico_flash_bank_instance();

    // Initialize BTstack TLV with flash bank
    const btstack_tlv_t *btstack_tlv_impl = btstack_tlv_flash_bank_init_instance(
            &btstack_tlv_flash_bank_context,
            hal_flash_bank_impl,
            NULL);

    // Set global TLV instance
    btstack_tlv_set_instance(btstack_tlv_impl, &btstack_tlv_flash_bank_context);

    // Set up Classic BT link key storage using TLV
    const btstack_link_key_db_t *btstack_link_key_db = btstack_link_key_db_tlv_get_instance(
            btstack_tlv_impl, &btstack_tlv_flash_bank_context);
    hci_set_link_key_db(btstack_link_key_db);
    printf("[BTSTACK_HOST] Classic BT link key DB configured (flash)\n");

    // Configure BLE device DB for TLV storage
    le_device_db_tlv_configure(btstack_tlv_impl, &btstack_tlv_flash_bank_context);
    printf("[BTSTACK_HOST] BLE device DB configured (flash)\n");
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
    printf("[BTSTACK_HOST] Starting Classic inquiry...\n");
    gap_inquiry_start(INQUIRY_DURATION);
    classic_state.inquiry_active = true;
}

void btstack_host_stop_scan(void)
{
    if (hid_state.scan_active) {
        printf("[BTSTACK_HOST] Stopping BLE scan\n");
        gap_stop_scan();
        hid_state.scan_active = false;
        hid_state.state = BLE_STATE_IDLE;
    }

    if (classic_state.inquiry_active) {
        printf("[BTSTACK_HOST] Stopping Classic inquiry\n");
        gap_inquiry_stop();
        classic_state.inquiry_active = false;
    }
}

// ============================================================================
// CONNECTION
// ============================================================================

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
}

// ============================================================================
// SDP QUERY CALLBACK (for VID/PID detection)
// ============================================================================

static void sdp_query_vid_pid_callback(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
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

            // Parse name from advertising data
            char name[32] = {0};
            ad_context_t context;
            for (ad_iterator_init(&context, adv_len, adv_data); ad_iterator_has_more(&context); ad_iterator_next(&context)) {
                uint8_t type = ad_iterator_get_data_type(&context);
                uint8_t len = ad_iterator_get_data_len(&context);
                const uint8_t *data = ad_iterator_get_data(&context);

                if ((type == BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME ||
                     type == BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME) && len < sizeof(name)) {
                    memcpy(name, data, len);
                    name[len] = 0;
                    break;
                }
            }

            // Log all BLE advertisements with names for debugging
            if (name[0] != 0) {
                printf("[BTSTACK_HOST] BLE adv: %02X:%02X:%02X:%02X:%02X:%02X name=\"%s\"\n",
                       addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], name);
            }

            // Check for controllers by name
            bool is_xbox = (strstr(name, "Xbox") != NULL);
            bool is_nintendo = (strstr(name, "Pro Controller") != NULL ||
                               strstr(name, "Joy-Con") != NULL);
            bool is_stadia = (strstr(name, "Stadia") != NULL);
            bool is_controller = is_xbox || is_nintendo || is_stadia;

            // Auto-connect to supported BLE controllers by name
            if (hid_state.state == BLE_STATE_SCANNING && is_controller) {
                if (is_xbox || is_stadia) {
                    printf("[BTSTACK_HOST] BLE controller: %02X:%02X:%02X:%02X:%02X:%02X name=\"%s\"\n",
                           addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], name);
                    printf("[BTSTACK_HOST] Connecting to %s...\n", is_xbox ? "Xbox" : "Stadia");
                    strncpy(hid_state.pending_name, name, sizeof(hid_state.pending_name) - 1);
                    hid_state.pending_name[sizeof(hid_state.pending_name) - 1] = '\0';
                    btstack_host_connect_ble(addr, addr_type);
                }
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

            // Log all inquiry results for debugging (gamepads highlighted)
            printf("[BTSTACK_HOST] Inquiry: %02X:%02X:%02X:%02X:%02X:%02X COD=0x%06X%s\n",
                   addr[5], addr[4], addr[3], addr[2], addr[1], addr[0],
                   (unsigned)cod, (is_gamepad || is_joystick) ? " [GAMEPAD]" : "");

            // Auto-connect to gamepads
            if ((is_gamepad || is_joystick) && classic_state.inquiry_active) {
                printf("[BTSTACK_HOST] Classic gamepad found, connecting...\n");
                btstack_host_stop_scan();  // Stop inquiry

                uint16_t hid_cid;
                uint8_t status = hid_host_connect(addr, HID_PROTOCOL_MODE_REPORT, &hid_cid);
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
                    }
                } else {
                    printf("[BTSTACK_HOST] hid_host_connect failed: %d\n", status);
                }
            }
            break;
        }

        case GAP_EVENT_INQUIRY_COMPLETE:
            classic_state.inquiry_active = false;
            // Restart inquiry after it completes (if we're still in scan mode)
            if (hid_state.state == BLE_STATE_SCANNING) {
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
            memcpy(classic_state.pending_addr, addr, 6);
            classic_state.pending_cod = cod;
            classic_state.pending_name[0] = '\0';  // Clear, will be filled by remote name request
            classic_state.pending_vid = 0;
            classic_state.pending_pid = 0;
            classic_state.pending_valid = true;
            // BTstack should auto-accept via gap_ssp_set_auto_accept(1) set at init
            break;
        }

        case HCI_EVENT_CONNECTION_COMPLETE: {
            uint8_t status = hci_event_connection_complete_get_status(packet);
            hci_con_handle_t handle = hci_event_connection_complete_get_connection_handle(packet);
            bd_addr_t addr;
            hci_event_connection_complete_get_bd_addr(packet, addr);
            printf("[BTSTACK_HOST] Connection complete: status=%d handle=0x%04X addr=%02X:%02X:%02X:%02X:%02X:%02X\n",
                   status, handle, addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

            // For incoming connections, request security level 2 after ACL is up
            // This triggers authentication/pairing for devices that support it (DS4/DS5)
            // while allowing DS3 (which doesn't support SSP) to work with L2CAP level 0
            if (status == 0) {
                if (classic_state.pending_valid &&
                    bd_addr_cmp(addr, classic_state.pending_addr) == 0) {
                    uint32_t cod = classic_state.pending_cod;
                    printf("[BTSTACK_HOST] Incoming ACL complete, COD=0x%06X - requesting auth\n", cod);

                    // For incoming connections, let the device initiate L2CAP/HID channels
                    // DS3 (0x000508) and DS4/DS5 (0x002508) all initiate themselves on reconnect
                    // We just need encryption to succeed, then wait for HID_SUBEVENT_INCOMING_CONNECTION
                    // Keep pending_valid=true so COD is available in HID_SUBEVENT_INCOMING_CONNECTION

                    // Request remote name for driver matching (we don't have it from inquiry)
                    gap_remote_name_request(addr, 0, 0);

                    // Query VID/PID via SDP (PnP Information service)
                    sdp_client_query_uuid16(&sdp_query_vid_pid_callback, addr,
                                            BLUETOOTH_SERVICE_CLASS_PNP_INFORMATION);
                }
                // Request authentication (Bluepad32 pattern)
                gap_request_security_level(handle, LEVEL_2);
            }
            break;
        }

        case L2CAP_EVENT_INCOMING_CONNECTION: {
            uint16_t psm = l2cap_event_incoming_connection_get_psm(packet);
            uint16_t cid = l2cap_event_incoming_connection_get_local_cid(packet);
            hci_con_handle_t handle = l2cap_event_incoming_connection_get_handle(packet);
            printf("[BTSTACK_HOST] L2CAP incoming: PSM=0x%04X cid=0x%04X handle=0x%04X\n", psm, cid, handle);
            break;
        }

        case L2CAP_EVENT_CHANNEL_OPENED: {
            uint8_t status = l2cap_event_channel_opened_get_status(packet);
            uint16_t psm = l2cap_event_channel_opened_get_psm(packet);
            uint16_t cid = l2cap_event_channel_opened_get_local_cid(packet);
            printf("[BTSTACK_HOST] L2CAP opened: status=%d PSM=0x%04X cid=0x%04X\n", status, psm, cid);
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
                        conn->is_xbox = (strstr(conn->name, "Xbox") != NULL);

                        printf("[BTSTACK_HOST] Connection stored: name='%s'\n", conn->name);

                        // Request pairing (SM will handle Secure Connections)
                        printf("[BTSTACK_HOST] Requesting pairing...\n");
                        sm_request_pairing(handle);
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

            if (name_status == 0) {
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
                        }
                        break;
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
            }

            hid_state.state = BLE_STATE_IDLE;

            // Try to reconnect to last connected device if we have one stored
            if (hid_state.has_last_connected && hid_state.reconnect_attempts < 5) {
                hid_state.reconnect_attempts++;
                printf("[BTSTACK_HOST] Attempting reconnection to stored device (attempt %d)...\n",
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
            } else {
                // Resume scanning for new devices
                btstack_host_start_scan();
            }
            break;
        }

        case HCI_EVENT_LINK_KEY_REQUEST: {
            bd_addr_t req_addr;
            reverse_bytes(&packet[2], req_addr, 6);

            // BTstack handles link key lookup via the registered link key DB (TLV flash storage)
            // This is just for logging - hci.c will query the DB and send the appropriate reply
            hci_connection_t *conn = hci_connection_for_bd_addr_and_type(req_addr, BD_ADDR_TYPE_ACL);
            printf("[BTSTACK_HOST] Link key request: %02X:%02X:%02X:%02X:%02X:%02X conn=%s\n",
                   req_addr[0], req_addr[1], req_addr[2], req_addr[3], req_addr[4], req_addr[5],
                   conn ? "YES" : "NO");
            break;
        }

        case HCI_EVENT_LINK_KEY_NOTIFICATION: {
            bd_addr_t notif_addr;
            reverse_bytes(&packet[2], notif_addr, 6);
            link_key_type_t key_type = (link_key_type_t)packet[24];
            // BTstack stores link key via the registered link key DB (TLV flash storage)
            // This is just for logging - hci.c already stored the key
            printf("[BTSTACK_HOST] Link key notification: %02X:%02X:%02X:%02X:%02X:%02X type=%d (stored to flash)\n",
                   notif_addr[0], notif_addr[1], notif_addr[2], notif_addr[3], notif_addr[4], notif_addr[5], key_type);
            break;
        }

        case HCI_EVENT_ENCRYPTION_CHANGE: {
            hci_con_handle_t handle = hci_event_encryption_change_get_connection_handle(packet);
            uint8_t status = hci_event_encryption_change_get_status(packet);
            uint8_t enabled = hci_event_encryption_change_get_encryption_enabled(packet);

            printf("[BTSTACK_HOST] Encryption change: handle=0x%04X status=0x%02X enabled=%d\n",
                   handle, status, enabled);

            if (status == 0 && enabled) {
                // Check if we have a pending HID connect for this handle
                if (classic_state.pending_hid_connect &&
                    classic_state.pending_hid_handle == handle) {
                    printf("[BTSTACK_HOST] Encryption complete, initiating HID connection\n");
                    uint16_t hid_cid;
                    uint8_t err = hid_host_connect(classic_state.pending_hid_addr,
                                                   HID_PROTOCOL_MODE_REPORT, &hid_cid);
                    if (err == ERROR_CODE_SUCCESS) {
                        printf("[BTSTACK_HOST] hid_host_connect initiated, cid=0x%04X\n", hid_cid);
                    } else {
                        printf("[BTSTACK_HOST] hid_host_connect failed: %d\n", err);
                    }
                    classic_state.pending_hid_connect = false;
                }
            }
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

                    // Xbox controllers: use fast-path with known handle 0x001E
                    // Other controllers: do proper GATT discovery
                    bool is_xbox = (strstr(conn->name, "Xbox") != NULL);
                    if (is_xbox) {
                        printf("[BTSTACK_HOST] Xbox detected - using fast-path HID listener\n");
                        register_ble_hid_listener(handle);
                    } else {
                        printf("[BTSTACK_HOST] Non-Xbox BLE controller - starting GATT discovery\n");
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

                    bool is_xbox = (strstr(conn->name, "Xbox") != NULL);
                    if (is_xbox) {
                        printf("[BTSTACK_HOST] Xbox detected - using fast-path HID listener\n");
                        register_ble_hid_listener(handle);
                    } else {
                        printf("[BTSTACK_HOST] Non-Xbox BLE controller - starting GATT discovery\n");
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
    printf("[BTSTACK_HOST] Calling bt_on_hid_ready(%d) for BLE device '%s'\n", conn->conn_index, conn->name);
    bt_on_hid_ready(conn->conn_index);
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
                        classic_state.pending_valid = false;
                        printf("[BTSTACK_HOST] Using pending COD: 0x%06X\n", (unsigned)classic_state.pending_cod);
                    }
                }
            }
            break;
        }

        case HID_SUBEVENT_CONNECTION_OPENED: {
            uint16_t hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
            uint8_t status = hid_subevent_connection_opened_get_status(packet);

            if (status != ERROR_CODE_SUCCESS) {
                printf("[BTSTACK_HOST] HID connection failed, status=0x%02X\n", status);
                // Remove connection slot
                classic_connection_t* conn = find_classic_connection_by_cid(hid_cid);
                if (conn) {
                    memset(conn, 0, sizeof(*conn));
                }
                return;
            }

            printf("[BTSTACK_HOST] HID connection opened, cid=0x%04X\n", hid_cid);

            // Mark connection as ready (HID channels established)
            classic_connection_t* conn = find_classic_connection_by_cid(hid_cid);
            if (conn) {
                conn->hid_ready = true;

                // For outgoing connections, query SDP for VID/PID if we don't have it yet
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
            }
            break;
        }

        case HID_SUBEVENT_DESCRIPTOR_AVAILABLE: {
            uint16_t hid_cid = hid_subevent_descriptor_available_get_hid_cid(packet);
            uint8_t status = hid_subevent_descriptor_available_get_status(packet);

            printf("[BTSTACK_HOST] HID descriptor available, cid=0x%04X status=0x%02X\n", hid_cid, status);

            // Notify bthid layer that device is ready
            int conn_index = get_classic_conn_index(hid_cid);
            if (conn_index >= 0) {
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

    return hid_host_send_report(conn->hid_cid, report_id, data, len) == ERROR_CODE_SUCCESS;
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
        info->vendor_id = 0;   // TODO: Get from BLE Device ID if needed
        info->product_id = 0;
        info->hid_ready = conn->hid_ready;

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
// BOND MANAGEMENT
// ============================================================================

void btstack_host_delete_all_bonds(void)
{
    printf("[BTSTACK_HOST] Deleting all Bluetooth bonds...\n");

    // Delete all Classic BT link keys
    gap_delete_all_link_keys();
    printf("[BTSTACK_HOST] Classic BT link keys deleted\n");

    // Delete all BLE bonds by re-initializing the LE device database
    // le_device_db_init() clears all stored bonds
    int ble_count = le_device_db_count();
    le_device_db_init();
    printf("[BTSTACK_HOST] BLE bonds deleted (was %d devices)\n", ble_count);

    printf("[BTSTACK_HOST] All bonds cleared. Devices will need to re-pair.\n");
}
