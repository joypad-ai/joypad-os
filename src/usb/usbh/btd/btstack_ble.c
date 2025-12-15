// btstack_ble.c - BTstack-based BLE handler for HID over GATT
//
// Uses BTstack's SM (Security Manager) for LE Secure Connections
// and GATT client for HID over GATT Profile (HOGP).

#include "btstack_ble.h"
#include "btstack_config.h"
// Include specific BTstack headers instead of umbrella btstack.h
// (btstack.h pulls in audio codecs which need sbc_encoder.h)
#include "btstack_defines.h"
#include "btstack_event.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_embedded.h"

// Declare btstack_memory_init - can't include btstack_memory.h due to HID conflicts
extern void btstack_memory_init(void);

#include "bluetooth_data_types.h"
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
#include "hci_dump.h"
#include "hci_dump_embedded_stdout.h"

// BTHID callbacks - for classic BT HID devices
extern void bt_on_hid_ready(uint8_t conn_index);
extern void bt_on_disconnect(uint8_t conn_index);
extern void bt_on_hid_report(uint8_t conn_index, const uint8_t* data, uint16_t len);

// Forward declare transport functions to avoid TinyUSB/BTstack HID header conflict
// (hci_transport_h2_tinyusb.h includes tusb.h which defines conflicting HID types)
// hci_transport_t is already defined via btstack includes
const hci_transport_t * hci_transport_h2_tinyusb_instance(void);
void hci_transport_h2_tinyusb_process(void);

#include <stdio.h>
#include <string.h>

// Input system integration
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"

// ============================================================================
// XBOX BLE HID REPORT PARSING
// ============================================================================

// Xbox BLE controller button masks (verified from testing)
#define XBOX_BLE_A               0x0001
#define XBOX_BLE_B               0x0002
#define XBOX_BLE_X               0x0008
#define XBOX_BLE_Y               0x0010
#define XBOX_BLE_LEFT_SHOULDER   0x0040  // LB
#define XBOX_BLE_RIGHT_SHOULDER  0x0080  // RB
#define XBOX_BLE_BACK            0x0400  // View button
#define XBOX_BLE_START           0x0800  // Menu button
#define XBOX_BLE_GUIDE           0x1000  // Xbox button
#define XBOX_BLE_LEFT_THUMB      0x2000  // L3
#define XBOX_BLE_RIGHT_THUMB     0x4000  // R3

// Xbox BLE HID input report layout (16 bytes, NO report_id prefix)
// Parse bytes directly to avoid struct alignment issues
// Bytes: 0-1:lx, 2-3:ly, 4-5:rx, 6-7:ry, 8-9:lt, 10-11:rt, 12:hat, 13-14:buttons, 15:pad

// Controller state
static input_event_t xbox_ble_event;
static bool xbox_ble_initialized = false;

// Deferred processing to avoid stack overflow in BTstack callback
static uint8_t pending_report[16];
static volatile bool report_pending = false;

// Forward declarations
static void process_xbox_ble_report(const uint8_t* data, uint16_t len);

// Process Xbox BLE HID report and submit to router
static void process_xbox_ble_report(const uint8_t* data, uint16_t len)
{
    static int count = 0;
    count++;

    if (len < 16) return;

    // Initialize once
    if (!xbox_ble_initialized) {
        init_input_event(&xbox_ble_event);
        xbox_ble_event.type = INPUT_TYPE_GAMEPAD;
        xbox_ble_event.dev_addr = 0xBE;
        xbox_ble_event.instance = 0;
        xbox_ble_event.button_count = 10;
        xbox_ble_initialized = true;
        printf("[XBOX_BLE] INIT OK\n");
    }

    // Parse bytes directly to avoid alignment issues
    // Layout: 0-1:lx, 2-3:ly, 4-5:rx, 6-7:ry, 8-9:lt, 10-11:rt, 12:hat, 13-14:buttons
    // Sticks are UNSIGNED 0-65535 (0=left/up, 32768=center, 65535=right/down)
    uint16_t raw_lx = (uint16_t)(data[0] | (data[1] << 8));
    uint16_t raw_ly = (uint16_t)(data[2] | (data[3] << 8));
    uint16_t raw_rx = (uint16_t)(data[4] | (data[5] << 8));
    uint16_t raw_ry = (uint16_t)(data[6] | (data[7] << 8));
    uint16_t raw_lt = (uint16_t)(data[8] | (data[9] << 8));
    uint16_t raw_rt = (uint16_t)(data[10] | (data[11] << 8));
    uint8_t hat = data[12];
    uint16_t btn = (uint16_t)(data[13] | (data[14] << 8));

    // Scale sticks from uint16 (0-65535) to uint8 (0-255)
    // Y axis: 0=up, 65535=down, but HID convention is 0=up, 255=down, so no inversion needed
    uint8_t lx = raw_lx >> 8;
    uint8_t ly = raw_ly >> 8;
    uint8_t rx = raw_rx >> 8;
    uint8_t ry = raw_ry >> 8;
    // Triggers are 10-bit (0-1023), scale to 8-bit
    uint8_t lt = raw_lt >> 2;
    uint8_t rt = raw_rt >> 2;

    uint32_t buttons = 0;

    // D-pad from hat: 0=center, 1=N, 2=NE, 3=E, 4=SE, 5=S, 6=SW, 7=W, 8=NW
    if (hat == 1 || hat == 2 || hat == 8) buttons |= JP_BUTTON_DU;
    if (hat >= 2 && hat <= 4)             buttons |= JP_BUTTON_DR;
    if (hat >= 4 && hat <= 6)             buttons |= JP_BUTTON_DD;
    if (hat >= 6 && hat <= 8)             buttons |= JP_BUTTON_DL;

    // Face buttons and others
    if (btn & XBOX_BLE_A)              buttons |= JP_BUTTON_B1;
    if (btn & XBOX_BLE_B)              buttons |= JP_BUTTON_B2;
    if (btn & XBOX_BLE_X)              buttons |= JP_BUTTON_B3;
    if (btn & XBOX_BLE_Y)              buttons |= JP_BUTTON_B4;
    if (btn & XBOX_BLE_LEFT_SHOULDER)  buttons |= JP_BUTTON_L1;
    if (btn & XBOX_BLE_RIGHT_SHOULDER) buttons |= JP_BUTTON_R1;
    if (lt > 100)                      buttons |= JP_BUTTON_L2;
    if (rt > 100)                      buttons |= JP_BUTTON_R2;
    if (btn & XBOX_BLE_BACK)           buttons |= JP_BUTTON_S1;
    if (btn & XBOX_BLE_START)          buttons |= JP_BUTTON_S2;
    if (btn & XBOX_BLE_LEFT_THUMB)     buttons |= JP_BUTTON_L3;
    if (btn & XBOX_BLE_RIGHT_THUMB)    buttons |= JP_BUTTON_R3;
    if (btn & XBOX_BLE_GUIDE)          buttons |= JP_BUTTON_A1;

    // Fill event struct
    xbox_ble_event.buttons = buttons;
    xbox_ble_event.analog[ANALOG_X] = lx;
    xbox_ble_event.analog[ANALOG_Y] = ly;
    xbox_ble_event.analog[ANALOG_Z] = rx;
    xbox_ble_event.analog[ANALOG_RX] = ry;
    xbox_ble_event.analog[ANALOG_RZ] = lt;
    xbox_ble_event.analog[ANALOG_SLIDER] = rt;

    // Submit to router on every report
    router_submit_input(&xbox_ble_event);
}

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
} ble_connection_t;

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

    // Scanning
    bool scan_active;

    // Pending connection
    bd_addr_t pending_addr;
    bd_addr_type_t pending_addr_type;

    // Connections
    ble_connection_t connections[MAX_BLE_CONNECTIONS];

    // GATT discovery state
    gatt_state_t gatt_state;
    hci_con_handle_t gatt_handle;
    uint16_t hid_service_start;
    uint16_t hid_service_end;
    gatt_client_characteristic_t report_characteristic;  // Full HID Report characteristic

    // Callbacks
    btstack_ble_report_callback_t report_callback;
    btstack_ble_connect_callback_t connect_callback;

    // HIDS Client
    uint16_t hids_cid;

} ble_state;

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
    bool hid_ready;
} classic_connection_t;

static struct {
    bool inquiry_active;
    classic_connection_t connections[MAX_CLASSIC_CONNECTIONS];
} classic_state;

// Classic HID descriptor storage
static uint8_t classic_hid_descriptor_storage[512];

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
// FORWARD DECLARATIONS
// ============================================================================

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void sm_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void hids_client_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void hid_host_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static ble_connection_t* find_connection_by_handle(hci_con_handle_t handle);
static ble_connection_t* find_free_connection(void);
static void start_hids_client(ble_connection_t *conn);
static void register_xbox_hid_listener(hci_con_handle_t con_handle);

// ============================================================================
// INITIALIZATION
// ============================================================================

void btstack_ble_init(void)
{
    if (ble_state.initialized) {
        printf("[BTSTACK_BLE] Already initialized\n");
        return;
    }

    printf("[BTSTACK_BLE] Initializing BTstack...\n");

    memset(&ble_state, 0, sizeof(ble_state));

    // HCI dump disabled - too verbose (logs every ACL packet)
    // printf("[BTSTACK_BLE] Init HCI dump (for logging)...\n");
    // hci_dump_init(hci_dump_embedded_stdout_get_instance());

    printf("[BTSTACK_BLE] Init memory pools...\n");
    btstack_memory_init();

    printf("[BTSTACK_BLE] Init run loop...\n");
    btstack_run_loop_init(btstack_run_loop_embedded_get_instance());

    printf("[BTSTACK_BLE] Init HCI...\n");
    hci_init(hci_transport_h2_tinyusb_instance(), NULL);

    printf("[BTSTACK_BLE] Init L2CAP...\n");
    l2cap_init();

    printf("[BTSTACK_BLE] Init SM...\n");
    sm_init();

    // Configure SM - bonding like Bluepad32
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_BONDING);
    sm_set_encryption_key_size_range(7, 16);

    printf("[BTSTACK_BLE] Init GATT client...\n");
    gatt_client_init();

    printf("[BTSTACK_BLE] Init HIDS client...\n");
    hids_client_init(hid_descriptor_storage, sizeof(hid_descriptor_storage));

    printf("[BTSTACK_BLE] Init LE Device DB...\n");
    le_device_db_init();

    // Initialize classic BT HID Host
    printf("[BTSTACK] Init Classic HID Host...\n");
    memset(&classic_state, 0, sizeof(classic_state));
    hid_host_init(classic_hid_descriptor_storage, sizeof(classic_hid_descriptor_storage));
    hid_host_register_packet_handler(hid_host_packet_handler);

    // Allow sniff mode and role switch for classic BT (improves compatibility)
    gap_set_default_link_policy_settings(LM_LINK_POLICY_ENABLE_SNIFF_MODE | LM_LINK_POLICY_ENABLE_ROLE_SWITCH);

    // Register for HCI events
    printf("[BTSTACK_BLE] Register event handlers...\n");
    hci_event_callback_registration.callback = packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // Register for SM events
    sm_event_callback_registration.callback = sm_packet_handler;
    sm_add_event_handler(&sm_event_callback_registration);

    ble_state.initialized = true;
    printf("[BTSTACK] Initialized OK (BLE + Classic)\n");
}

void btstack_ble_power_on(void)
{
    printf("[BTSTACK_BLE] power_on called, initialized=%d\n", ble_state.initialized);

    if (!ble_state.initialized) {
        printf("[BTSTACK_BLE] Calling init first...\n");
        btstack_ble_init();
    }

    printf("[BTSTACK_BLE] HCI state before power_on: %d\n", hci_get_state());
    printf("[BTSTACK_BLE] Calling hci_power_control(HCI_POWER_ON)...\n");
    int err = hci_power_control(HCI_POWER_ON);
    printf("[BTSTACK_BLE] hci_power_control returned %d, state now: %d\n", err, hci_get_state());
}

// ============================================================================
// SCANNING
// ============================================================================

void btstack_ble_start_scan(void)
{
    if (!ble_state.powered_on) {
        printf("[BTSTACK] Not powered on yet\n");
        return;
    }

    if (ble_state.scan_active || classic_state.inquiry_active) {
        printf("[BTSTACK] Scan already active\n");
        return;
    }

    printf("[BTSTACK] Starting BLE scan...\n");
    gap_set_scan_params(1, SCAN_INTERVAL, SCAN_WINDOW, 0);
    gap_start_scan();
    ble_state.scan_active = true;
    ble_state.state = BLE_STATE_SCANNING;

    // Also start classic BT inquiry
    printf("[BTSTACK] Starting Classic inquiry...\n");
    gap_inquiry_start(INQUIRY_DURATION);
    classic_state.inquiry_active = true;
}

void btstack_ble_stop_scan(void)
{
    if (ble_state.scan_active) {
        printf("[BTSTACK] Stopping BLE scan\n");
        gap_stop_scan();
        ble_state.scan_active = false;
        ble_state.state = BLE_STATE_IDLE;
    }

    if (classic_state.inquiry_active) {
        printf("[BTSTACK] Stopping Classic inquiry\n");
        gap_inquiry_stop();
        classic_state.inquiry_active = false;
    }
}

// ============================================================================
// CONNECTION
// ============================================================================

void btstack_ble_connect(bd_addr_t addr, bd_addr_type_t addr_type)
{
    printf("[BTSTACK_BLE] Connecting to %02X:%02X:%02X:%02X:%02X:%02X\n",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    // Stop scanning first
    btstack_ble_stop_scan();

    // Save pending connection info
    memcpy(ble_state.pending_addr, addr, 6);
    ble_state.pending_addr_type = addr_type;
    ble_state.state = BLE_STATE_CONNECTING;

    // Create connection
    uint8_t status = gap_connect(addr, addr_type);
    printf("[BTSTACK_BLE] gap_connect returned status=%d\n", status);
}

// ============================================================================
// CALLBACKS
// ============================================================================

void btstack_ble_register_report_callback(btstack_ble_report_callback_t callback)
{
    ble_state.report_callback = callback;
}

void btstack_ble_register_connect_callback(btstack_ble_connect_callback_t callback)
{
    ble_state.connect_callback = callback;
}

// ============================================================================
// MAIN LOOP
// ============================================================================

static uint32_t process_counter = 0;

void btstack_ble_process(void)
{
    if (!ble_state.initialized) return;

    process_counter++;
    if ((process_counter % 100000) == 1) {
        printf("[BTSTACK_BLE] process loop %lu, powered=%d, scanning=%d\n",
               (unsigned long)process_counter, ble_state.powered_on, ble_state.scan_active);
    }

    // Process our TinyUSB transport first (delivers packets to BTstack)
    hci_transport_h2_tinyusb_process();

    // Process BTstack run loop multiple times to let packets flow through HCI->L2CAP->ATT->GATT
    for (int i = 0; i < 5; i++) {
        btstack_run_loop_embedded_execute_once();
    }

    // Process any pending HID report (deferred from BTstack callback to avoid stack overflow)
    if (report_pending) {
        report_pending = false;
        process_xbox_ble_report(pending_report, 16);
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
        printf("[BTSTACK_BLE] >>> RAW GATT NOTIFICATION! len=%d\n", size);
    }

    switch (event_type) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                printf("[BTSTACK_BLE] HCI working\n");
                ble_state.powered_on = true;

                // Auto-start scanning
                btstack_ble_start_scan();
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

            // Check for Xbox controller
            bool is_xbox = (strstr(name, "Xbox") != NULL);

            if (name[0] != 0) {
                printf("[BTSTACK_BLE] Adv: %02X:%02X:%02X:%02X:%02X:%02X rssi=%d name=\"%s\"%s\n",
                       addr[5], addr[4], addr[3], addr[2], addr[1], addr[0],
                       rssi, name, is_xbox ? " [XBOX]" : "");

                // Auto-connect to Xbox controllers
                if (is_xbox && ble_state.state == BLE_STATE_SCANNING) {
                    printf("[BTSTACK_BLE] Xbox controller found, connecting...\n");
                    btstack_ble_connect(addr, addr_type);
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

            printf("[BTSTACK] Inquiry: %02X:%02X:%02X:%02X:%02X:%02X COD=0x%06X name=\"%s\"%s\n",
                   addr[5], addr[4], addr[3], addr[2], addr[1], addr[0],
                   (unsigned)cod, name, (is_gamepad || is_joystick) ? " [GAMEPAD]" : "");

            // Auto-connect to gamepads
            if ((is_gamepad || is_joystick) && classic_state.inquiry_active) {
                printf("[BTSTACK] Classic gamepad found, connecting...\n");
                btstack_ble_stop_scan();  // Stop inquiry

                uint16_t hid_cid;
                uint8_t status = hid_host_connect(addr, HID_PROTOCOL_MODE_REPORT, &hid_cid);
                if (status == ERROR_CODE_SUCCESS) {
                    printf("[BTSTACK] hid_host_connect started, cid=0x%04X\n", hid_cid);

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
                    printf("[BTSTACK] hid_host_connect failed: %d\n", status);
                }
            }
            break;
        }

        case GAP_EVENT_INQUIRY_COMPLETE:
            printf("[BTSTACK] Inquiry complete\n");
            classic_state.inquiry_active = false;
            // Restart scanning after inquiry completes (if we're still in scan mode)
            if (ble_state.state == BLE_STATE_SCANNING) {
                printf("[BTSTACK] Restarting inquiry...\n");
                gap_inquiry_start(INQUIRY_DURATION);
                classic_state.inquiry_active = true;
            }
            break;

        case HCI_EVENT_LE_META: {
            uint8_t subevent = hci_event_le_meta_get_subevent_code(packet);

            switch (subevent) {
                case HCI_SUBEVENT_LE_CONNECTION_COMPLETE: {
                    hci_con_handle_t handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
                    uint8_t status = hci_subevent_le_connection_complete_get_status(packet);

                    if (status != 0) {
                        printf("[BTSTACK_BLE] Connection failed: 0x%02X\n", status);
                        ble_state.state = BLE_STATE_IDLE;
                        break;
                    }

                    printf("[BTSTACK_BLE] Connected! handle=0x%04X\n", handle);

                    // Find or create connection entry
                    ble_connection_t *conn = find_free_connection();
                    if (conn) {
                        memcpy(conn->addr, ble_state.pending_addr, 6);
                        conn->addr_type = ble_state.pending_addr_type;
                        conn->handle = handle;
                        conn->state = BLE_STATE_CONNECTED;

                        // Request pairing (SM will handle Secure Connections)
                        printf("[BTSTACK_BLE] Requesting pairing...\n");
                        sm_request_pairing(handle);
                    }

                    ble_state.state = BLE_STATE_CONNECTED;
                    break;
                }

                case HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE:
                    printf("[BTSTACK_BLE] Connection update complete\n");
                    break;
            }
            break;
        }

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            hci_con_handle_t handle = hci_event_disconnection_complete_get_connection_handle(packet);
            uint8_t reason = hci_event_disconnection_complete_get_reason(packet);

            printf("[BTSTACK_BLE] Disconnected: handle=0x%04X reason=0x%02X\n", handle, reason);

            ble_connection_t *conn = find_connection_by_handle(handle);
            if (conn) {
                memset(conn, 0, sizeof(*conn));
            }

            ble_state.state = BLE_STATE_IDLE;

            // Resume scanning
            btstack_ble_start_scan();
            break;
        }

        case HCI_EVENT_ENCRYPTION_CHANGE: {
            hci_con_handle_t handle = hci_event_encryption_change_get_connection_handle(packet);
            uint8_t status = hci_event_encryption_change_get_status(packet);
            uint8_t enabled = hci_event_encryption_change_get_encryption_enabled(packet);

            printf("[BTSTACK_BLE] Encryption change: handle=0x%04X status=0x%02X enabled=%d\n",
                   handle, status, enabled);

            if (status == 0 && enabled) {
                printf("[BTSTACK_BLE] Encrypted! (no action, waiting for pairing complete)\n");
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
            printf("[BTSTACK_BLE] SM: Just Works request\n");
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            break;

        case SM_EVENT_PAIRING_STARTED:
            printf("[BTSTACK_BLE] SM: Pairing started\n");
            break;

        case SM_EVENT_PAIRING_COMPLETE: {
            hci_con_handle_t handle = sm_event_pairing_complete_get_handle(packet);
            uint8_t status = sm_event_pairing_complete_get_status(packet);
            printf("[BTSTACK_BLE] SM: Pairing complete, handle=0x%04X status=0x%02X\n", handle, status);

            if (status == ERROR_CODE_SUCCESS) {
                printf("[BTSTACK_BLE] SM: Pairing successful! Registering HID listener...\n");
                register_xbox_hid_listener(handle);
            } else {
                printf("[BTSTACK_BLE] SM: Pairing FAILED\n");
            }
            break;
        }

        case SM_EVENT_REENCRYPTION_STARTED:
            printf("[BTSTACK_BLE] SM: Re-encryption started\n");
            break;

        case SM_EVENT_REENCRYPTION_COMPLETE: {
            hci_con_handle_t handle = sm_event_reencryption_complete_get_handle(packet);
            uint8_t status = sm_event_reencryption_complete_get_status(packet);
            printf("[BTSTACK_BLE] SM: Re-encryption complete, handle=0x%04X status=0x%02X\n", handle, status);
            if (status == ERROR_CODE_SUCCESS) {
                printf("[BTSTACK_BLE] SM: Re-encryption successful! Registering HID listener...\n");
                register_xbox_hid_listener(handle);
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
            printf("[BTSTACK_BLE] GATT: Service 0x%04X-0x%04X UUID=0x%04X\n",
                   service.start_group_handle, service.end_group_handle,
                   service.uuid16);
            // Save HID service handles (UUID 0x1812)
            if (service.uuid16 == 0x1812) {
                ble_state.hid_service_start = service.start_group_handle;
                ble_state.hid_service_end = service.end_group_handle;
                printf("[BTSTACK_BLE] Found HID Service!\n");
            }
            break;
        }

        case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT: {
            gatt_client_characteristic_t characteristic;
            gatt_event_characteristic_query_result_get_characteristic(packet, &characteristic);
            printf("[BTSTACK_BLE] GATT: Char handle=0x%04X value=0x%04X end=0x%04X props=0x%02X UUID=0x%04X\n",
                   characteristic.start_handle, characteristic.value_handle,
                   characteristic.end_handle, characteristic.properties, characteristic.uuid16);
            // Save first Report characteristic (UUID 0x2A4D) with Notify property
            if (characteristic.uuid16 == 0x2A4D && (characteristic.properties & 0x10) &&
                ble_state.report_characteristic.value_handle == 0) {
                ble_state.report_characteristic = characteristic;
                printf("[BTSTACK_BLE] Found HID Report characteristic!\n");
            }
            break;
        }

        case GATT_EVENT_QUERY_COMPLETE: {
            uint8_t status = gatt_event_query_complete_get_att_status(packet);
            printf("[BTSTACK_BLE] GATT: Query complete, status=0x%02X, gatt_state=%d\n",
                   status, ble_state.gatt_state);

            if (status != 0) break;

            // State machine for GATT discovery
            if (ble_state.gatt_state == GATT_DISCOVERING_SERVICES) {
                if (ble_state.hid_service_start != 0) {
                    // Found HID, now discover its characteristics
                    printf("[BTSTACK_BLE] Discovering HID characteristics...\n");
                    ble_state.gatt_state = GATT_DISCOVERING_HID_CHARACTERISTICS;
                    gatt_client_discover_characteristics_for_handle_range_by_uuid16(
                        gatt_client_callback, ble_state.gatt_handle,
                        ble_state.hid_service_start, ble_state.hid_service_end,
                        0x2A4D);  // HID Report UUID
                } else {
                    printf("[BTSTACK_BLE] No HID service found!\n");
                }
            } else if (ble_state.gatt_state == GATT_DISCOVERING_HID_CHARACTERISTICS) {
                if (ble_state.report_characteristic.value_handle != 0) {
                    // Found Report char, enable notifications
                    printf("[BTSTACK_BLE] Enabling notifications on 0x%04X (end=0x%04X)...\n",
                           ble_state.report_characteristic.value_handle,
                           ble_state.report_characteristic.end_handle);
                    ble_state.gatt_state = GATT_ENABLING_NOTIFICATIONS;
                    gatt_client_write_client_characteristic_configuration(
                        gatt_client_callback, ble_state.gatt_handle,
                        &ble_state.report_characteristic,
                        GATT_CLIENT_CHARACTERISTICS_CONFIGURATION_NOTIFICATION);
                } else {
                    printf("[BTSTACK_BLE] No HID Report characteristic found!\n");
                }
            } else if (ble_state.gatt_state == GATT_ENABLING_NOTIFICATIONS) {
                printf("[BTSTACK_BLE] Notifications enabled! Ready for HID reports.\n");
                ble_state.gatt_state = GATT_READY;
            }
            break;
        }

        case GATT_EVENT_NOTIFICATION: {
            uint16_t value_handle = gatt_event_notification_get_value_handle(packet);
            uint16_t value_length = gatt_event_notification_get_value_length(packet);
            const uint8_t *value = gatt_event_notification_get_value(packet);

            // Xbox BLE HID Report characteristic handle is 0x001E
            // Process directly here since HIDS client may not have registered listener yet
            if (value_handle == 0x001E && value_length >= 16) {
                // Build a report with report_id prepended (Xbox uses report ID 0x01)
                static uint8_t xbox_report[32];
                xbox_report[0] = 0x01;  // Report ID
                memcpy(&xbox_report[1], value, value_length > 31 ? 31 : value_length);
                process_xbox_ble_report(xbox_report, value_length + 1);
            }
            break;
        }
    }
}

// ============================================================================
// DIRECT XBOX HID NOTIFICATION HANDLER
// ============================================================================

// Handle notifications directly from gatt_client listener API
static void xbox_hid_notification_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != GATT_EVENT_NOTIFICATION) return;

    uint16_t value_handle = gatt_event_notification_get_value_handle(packet);
    uint16_t value_length = gatt_event_notification_get_value_length(packet);
    const uint8_t *value = gatt_event_notification_get_value(packet);

    // Only process Xbox HID report handle
    if (value_handle != 0x001E) return;
    if (value_length < 16) return;

    // Defer processing to main loop (same pattern as hook)
    memcpy(pending_report, value, 16);
    report_pending = true;
}

// Register direct listener for Xbox HID notifications
static void register_xbox_hid_listener(hci_con_handle_t con_handle)
{
    printf("[BTSTACK_BLE] Registering direct Xbox HID listener for handle 0x%04X\n", con_handle);

    // Set up a fake characteristic structure with just the value_handle
    // Xbox BLE HID Report characteristic value handle is 0x001E
    memset(&xbox_hid_characteristic, 0, sizeof(xbox_hid_characteristic));
    xbox_hid_characteristic.value_handle = 0x001E;
    xbox_hid_characteristic.end_handle = 0x001F;  // Approximate

    // Register to listen for notifications on the Xbox HID report characteristic
    gatt_client_listen_for_characteristic_value_updates(
        &xbox_hid_notification_listener,
        xbox_hid_notification_handler,
        con_handle,
        &xbox_hid_characteristic);

    printf("[BTSTACK_BLE] Xbox HID listener registered for value_handle 0x001E\n");
}

static void start_hids_client(ble_connection_t *conn)
{
    printf("[BTSTACK_BLE] Connecting HIDS client...\n");

    conn->state = BLE_STATE_DISCOVERING;
    ble_state.gatt_handle = conn->handle;

    uint8_t status = hids_client_connect(conn->handle, hids_client_handler,
                                         HID_PROTOCOL_MODE_REPORT, &ble_state.hids_cid);

    printf("[BTSTACK_BLE] hids_client_connect returned %d, cid=0x%04X\n",
           status, ble_state.hids_cid);
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
            printf("[BTSTACK_BLE] HIDS connected! status=%d instances=%d\n", status, num_instances);

            if (status == ERROR_CODE_SUCCESS) {
                ble_connection_t *conn = find_connection_by_handle(ble_state.gatt_handle);
                if (conn) {
                    conn->state = BLE_STATE_READY;
                }

                // Explicitly enable notifications
                printf("[BTSTACK_BLE] Enabling HID notifications...\n");
                uint8_t result = hids_client_enable_notifications(ble_state.hids_cid);
                printf("[BTSTACK_BLE] enable_notifications returned %d\n", result);
            }
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_SERVICE_REPORTS_NOTIFICATION: {
            uint8_t configuration = gattservice_subevent_hid_service_reports_notification_get_configuration(packet);
            printf("[BTSTACK_BLE] HID Reports Notification configured: %d\n", configuration);
            printf("[BTSTACK_BLE] Ready to receive HID reports!\n");
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_REPORT: {
            uint16_t report_len = gattservice_subevent_hid_report_get_report_len(packet);
            const uint8_t *report = gattservice_subevent_hid_report_get_report(packet);

            // Process as Xbox BLE controller input
            process_xbox_ble_report(report, report_len);

            // Forward to callback if set
            if (ble_state.report_callback) {
                ble_state.report_callback(ble_state.gatt_handle, report, report_len);
            }
            break;
        }

        default:
            printf("[BTSTACK_BLE] GATT service subevent: 0x%02X\n",
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
        if (ble_state.connections[i].handle == handle) {
            return &ble_state.connections[i];
        }
    }
    return NULL;
}

static ble_connection_t* find_free_connection(void)
{
    for (int i = 0; i < MAX_BLE_CONNECTIONS; i++) {
        if (ble_state.connections[i].handle == 0) {
            return &ble_state.connections[i];
        }
    }
    return NULL;
}

// ============================================================================
// STATUS
// ============================================================================

bool btstack_ble_is_initialized(void)
{
    return ble_state.initialized;
}

bool btstack_ble_is_powered_on(void)
{
    return ble_state.powered_on;
}

bool btstack_ble_is_scanning(void)
{
    return ble_state.scan_active || classic_state.inquiry_active;
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
            printf("[BTSTACK] HID incoming connection, cid=0x%04X - accepting\n", hid_cid);
            hid_host_accept_connection(hid_cid, HID_PROTOCOL_MODE_REPORT);

            // Allocate connection slot if needed
            if (!find_classic_connection_by_cid(hid_cid)) {
                classic_connection_t* conn = find_free_classic_connection();
                if (conn) {
                    memset(conn, 0, sizeof(*conn));
                    conn->active = true;
                    conn->hid_cid = hid_cid;
                    hid_subevent_incoming_connection_get_address(packet, conn->addr);
                }
            }
            break;
        }

        case HID_SUBEVENT_CONNECTION_OPENED: {
            uint16_t hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
            uint8_t status = hid_subevent_connection_opened_get_status(packet);

            if (status != ERROR_CODE_SUCCESS) {
                printf("[BTSTACK] HID connection failed, status=0x%02X\n", status);
                // Remove connection slot
                classic_connection_t* conn = find_classic_connection_by_cid(hid_cid);
                if (conn) {
                    memset(conn, 0, sizeof(*conn));
                }
                return;
            }

            printf("[BTSTACK] HID connection opened, cid=0x%04X\n", hid_cid);

            // Mark connection as ready (HID channels established)
            classic_connection_t* conn = find_classic_connection_by_cid(hid_cid);
            if (conn) {
                conn->hid_ready = true;
            }
            break;
        }

        case HID_SUBEVENT_DESCRIPTOR_AVAILABLE: {
            uint16_t hid_cid = hid_subevent_descriptor_available_get_hid_cid(packet);
            uint8_t status = hid_subevent_descriptor_available_get_status(packet);

            printf("[BTSTACK] HID descriptor available, cid=0x%04X status=0x%02X\n", hid_cid, status);

            // Notify bthid layer that device is ready
            int conn_index = get_classic_conn_index(hid_cid);
            if (conn_index >= 0) {
                printf("[BTSTACK] Calling bt_on_hid_ready(%d)\n", conn_index);
                bt_on_hid_ready(conn_index);
            }
            break;
        }

        case HID_SUBEVENT_REPORT: {
            uint16_t hid_cid = hid_subevent_report_get_hid_cid(packet);
            const uint8_t* report = hid_subevent_report_get_report(packet);
            uint16_t report_len = hid_subevent_report_get_report_len(packet);

            // Route to bthid layer
            int conn_index = get_classic_conn_index(hid_cid);
            if (conn_index >= 0 && report_len > 0) {
                // Build BTHID-compatible packet: DATA|INPUT header + report
                static uint8_t hid_packet[256];
                hid_packet[0] = 0xA1;  // DATA | INPUT header
                if (report_len < sizeof(hid_packet) - 1) {
                    memcpy(hid_packet + 1, report, report_len);
                    bt_on_hid_report(conn_index, hid_packet, report_len + 1);
                }
            }
            break;
        }

        case HID_SUBEVENT_CONNECTION_CLOSED: {
            uint16_t hid_cid = hid_subevent_connection_closed_get_hid_cid(packet);
            printf("[BTSTACK] HID connection closed, cid=0x%04X\n", hid_cid);

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
            printf("[BTSTACK] HID set protocol response: cid=0x%04X handshake=%d mode=%d\n",
                   hid_cid, handshake, mode);
            break;
        }

        default:
            printf("[BTSTACK] HID subevent: 0x%02X\n", subevent);
            break;
    }
}

// ============================================================================
// CLASSIC BT OUTPUT REPORTS
// ============================================================================

// Send output report via classic HID (called from bthid drivers)
bool btstack_classic_send_report(uint8_t conn_index, uint8_t report_id,
                                  const uint8_t* data, uint16_t len)
{
    if (conn_index >= MAX_CLASSIC_CONNECTIONS) return false;

    classic_connection_t* conn = &classic_state.connections[conn_index];
    if (!conn->active || !conn->hid_ready) return false;

    return hid_host_send_report(conn->hid_cid, report_id, data, len) == ERROR_CODE_SUCCESS;
}

// Get classic connection info for bthid driver matching
bool btstack_classic_get_connection(uint8_t conn_index, btstack_classic_conn_info_t* info)
{
    if (conn_index >= MAX_CLASSIC_CONNECTIONS || !info) return false;

    classic_connection_t* conn = &classic_state.connections[conn_index];
    if (!conn->active) return false;

    info->active = conn->active;
    memcpy(info->bd_addr, conn->addr, 6);
    strncpy(info->name, conn->name, sizeof(info->name) - 1);
    info->name[sizeof(info->name) - 1] = '\0';
    memcpy(info->class_of_device, conn->class_of_device, 3);
    info->hid_ready = conn->hid_ready;

    return true;
}

// Get number of active classic connections
uint8_t btstack_classic_get_connection_count(void)
{
    uint8_t count = 0;
    for (int i = 0; i < MAX_CLASSIC_CONNECTIONS; i++) {
        if (classic_state.connections[i].active) {
            count++;
        }
    }
    return count;
}
