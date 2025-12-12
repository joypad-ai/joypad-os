// btd.c - Bluetooth Dongle Driver Implementation
// USB Bluetooth dongle HCI layer for TinyUSB host
//
// Reference: USB_Host_Shield_2.0 BTD.cpp

#include "btd.h"
#include "btd_linkkey.h"
#include "l2cap.h"
#include "tusb.h"
#include "host/usbh_pvt.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// STATIC DATA
// ============================================================================

static btd_t btd_ctx;

// Pending connection info (stored during inquiry, used when connection completes)
static struct {
    uint8_t bd_addr[6];
    uint8_t class_of_device[3];
    bool valid;
} pending_connection;

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void btd_process_event(const uint8_t* data, uint16_t len);
static void btd_state_machine(void);
static btd_connection_t* btd_find_connection_by_handle(uint16_t handle);
static btd_connection_t* btd_find_connection_by_bdaddr(const uint8_t* bd_addr);
static btd_connection_t* btd_alloc_connection(void);

// ============================================================================
// BTD INITIALIZATION
// ============================================================================

void btd_init(void)
{
    memset(&btd_ctx, 0, sizeof(btd_ctx));
    btd_ctx.state = BTD_STATE_INIT;
    btd_ctx.le_connecting = false;  // Explicit init

    // Mark all connections as disconnected
    for (int i = 0; i < BTD_MAX_CONNECTIONS; i++) {
        btd_ctx.connections[i].state = BTD_CONN_DISCONNECTED;
        btd_ctx.connections[i].handle = 0xFFFF;
    }

    // Initialize link key storage
    btd_linkkey_init();

    // Initialize L2CAP layer
    l2cap_init();

    printf("[BTD] Initialized (le_connecting=%d)\n", btd_ctx.le_connecting);
}

// ============================================================================
// BTD TASK (Main Loop)
// ============================================================================

// Heartbeat counter for debug
static uint32_t btd_task_counter = 0;

// Forward declaration for glue layer task
extern void btd_glue_task(void);

// Forward declaration for endpoint busy check
extern bool usbh_edpt_busy(uint8_t dev_addr, uint8_t ep_addr);

void btd_task(void)
{
    // Handle link key flash saves (debounced)
    btd_linkkey_task();

    // Process pending L2CAP connections
    btd_glue_task();

    if (!btd_ctx.dongle_connected) {
        return;
    }

    // Retry pending ACL OUT if endpoint is now free
    if (btd_ctx.acl_out_pending && !usbh_edpt_busy(btd_ctx.dev_addr, btd_ctx.ep_acl_out)) {
        btd_ctx.acl_out_pending = false;
        btd_ctx.acl_credits--;
        usbh_edpt_xfer(btd_ctx.dev_addr, btd_ctx.ep_acl_out,
                       btd_ctx.acl_out_buf, btd_ctx.acl_out_pending_len);
    }

    // Heartbeat every ~5 seconds (assuming 1ms task rate)
    btd_task_counter++;
    if (btd_task_counter % 5000 == 0 && btd_ctx.state == BTD_STATE_RUNNING) {
        printf("[BTD] Scanning... (evt_pending=%d)\n", btd_ctx.evt_pending);
    }

    // Handle delayed BLE connection (after scan has stopped)
    static uint16_t le_connect_delay = 0;
    if (btd_ctx.le_connecting && btd_ctx.le_connect_pending && !btd_ctx.pending_cmd) {
        le_connect_delay++;
        if (le_connect_delay >= 100) {  // ~100ms delay
            le_connect_delay = 0;
            btd_ctx.le_connect_pending = false;  // Clear pending, keep le_connecting
            printf("[BTD] Initiating delayed BLE connection...\n");
            btd_hci_le_create_connection(btd_ctx.le_pending_addr, btd_ctx.le_pending_addr_type);
        }
    } else {
        le_connect_delay = 0;
    }

    // Re-queue event endpoint if not pending (polling model like USB Host Shield)
    if (!btd_ctx.evt_pending) {
        btd_ctx.evt_pending = true;
        // Clear buffer to avoid processing stale data
        memset(btd_ctx.evt_buf, 0, sizeof(btd_ctx.evt_buf));
        usbh_edpt_xfer(btd_ctx.dev_addr, btd_ctx.ep_evt, btd_ctx.evt_buf, sizeof(btd_ctx.evt_buf));
    }

    // Run state machine
    btd_state_machine();
}

// ============================================================================
// STATE MACHINE
// ============================================================================

// Counter for init delay (like USB Host Shield's hci_num_reset_loops)
static uint16_t init_delay_counter = 0;
#define BTD_INIT_DELAY_LOOPS 100  // Wait this many task loops before starting

static void btd_state_machine(void)
{
    // Don't send new commands if we're waiting for a response
    if (btd_ctx.pending_cmd) {
        return;
    }

    // Debug: show state transitions for LE init
    static btd_state_t last_state = BTD_STATE_INIT;
    if (btd_ctx.state != last_state) {
        printf("[BTD] State transition: %d -> %d\n", last_state, btd_ctx.state);
        last_state = btd_ctx.state;
    }

    switch (btd_ctx.state) {
        case BTD_STATE_INIT:
            // Wait for dongle to stabilize after enumeration (like USB Host Shield)
            if (init_delay_counter < BTD_INIT_DELAY_LOOPS) {
                init_delay_counter++;
                return;
            }
            init_delay_counter = 0;
            printf("[BTD] Starting initialization...\n");
            btd_hci_reset();
            btd_ctx.state = BTD_STATE_RESET;
            break;

        case BTD_STATE_RESET:
            // Waiting for reset complete event
            break;

        case BTD_STATE_READ_BD_ADDR:
            btd_hci_read_bd_addr();
            break;

        case BTD_STATE_READ_VERSION:
            btd_hci_read_local_version();
            break;

        case BTD_STATE_READ_BUFFER_SIZE:
            btd_hci_read_buffer_size();
            break;

        case BTD_STATE_SET_EVENT_MASK:
            btd_hci_set_event_mask();
            break;

        case BTD_STATE_WRITE_NAME:
            btd_hci_write_local_name("Joypad BT");
            break;

        case BTD_STATE_WRITE_COD:
            // Class of Device: Major=Peripheral (0x05), Minor=Gamepad (0x08)
            // Matches USB Host Shield - some controllers may be picky about this
            btd_hci_write_class_of_device(0x000508);
            break;

        case BTD_STATE_WRITE_SSP:
            btd_hci_write_simple_pairing_mode(true);
            break;

        case BTD_STATE_WRITE_SCAN:
            // Enable page scan only - allows paired devices to connect to us
            // but we're not discoverable (we scan for devices, they don't scan for us)
            btd_hci_write_scan_enable(HCI_SCAN_PAGE_ONLY);
            break;

        // BLE (Bluetooth Low Energy) initialization states
        case BTD_STATE_LE_WRITE_HOST_SUPPORT:
            // Enable BLE on the dongle (Bluetooth 4.0+ requirement)
            printf("[BTD] Attempting to enable BLE (LE Host Support)...\n");
            btd_hci_write_le_host_support(true);
            break;

        case BTD_STATE_LE_READ_BUFFER_SIZE:
            printf("[BTD] Reading LE buffer size...\n");
            btd_hci_le_read_buffer_size();
            break;

        case BTD_STATE_LE_SET_EVENT_MASK:
            printf("[BTD] Setting LE event mask...\n");
            btd_hci_le_set_event_mask();
            break;

        case BTD_STATE_LE_SET_SCAN_PARAMS:
            // Use extended scanning for BT 5.0+ (HCI version >= 9)
            if (btd_ctx.hci_version >= 9) {
                printf("[BTD] Setting LE EXTENDED scan parameters (BT 5.0+)...\n");
                btd_hci_le_set_ext_scan_parameters(HCI_LE_SCAN_ACTIVE, 0x0060, 0x0030);
            } else {
                printf("[BTD] Setting LE legacy scan parameters...\n");
                btd_hci_le_set_scan_parameters(HCI_LE_SCAN_ACTIVE, 0x0060, 0x0030, true);
            }
            break;

        case BTD_STATE_LE_SCAN_ENABLE:
            // Use extended scanning for BT 5.0+ (HCI version >= 9)
            // Disable duplicate filtering to ensure we see all advertisements
            if (btd_ctx.hci_version >= 9) {
                printf("[BTD] Enabling LE EXTENDED scanning (BT 5.0+, no dup filter)...\n");
                btd_hci_le_set_ext_scan_enable(true, false);  // No duplicate filter
            } else {
                printf("[BTD] Enabling LE legacy scanning...\n");
                btd_hci_le_set_scan_enable(true, false);  // No duplicate filter
            }
            break;

        case BTD_STATE_INQUIRY:
            // Start scanning for nearby Classic Bluetooth devices
            btd_hci_inquiry();
            break;

        case BTD_STATE_RUNNING:
            // Normal operation - handle connections
            break;

        case BTD_STATE_ERROR:
            // Error state - do nothing
            break;
    }
}

// ============================================================================
// HCI COMMAND SENDING
// ============================================================================

bool btd_send_hci_cmd(uint16_t opcode, const uint8_t* params, uint8_t param_len)
{
    if (!btd_ctx.dongle_connected) {
        return false;
    }

    // Build HCI command packet
    btd_ctx.cmd_buf[0] = opcode & 0xFF;
    btd_ctx.cmd_buf[1] = (opcode >> 8) & 0xFF;
    btd_ctx.cmd_buf[2] = param_len;

    if (param_len > 0 && params != NULL) {
        memcpy(&btd_ctx.cmd_buf[3], params, param_len);
    }

    uint16_t total_len = 3 + param_len;

    // Send via control transfer (USB HCI command)
    // bmRequestType: 0x20 (Class, Host-to-Device, Interface)
    // bRequest: 0x00
    tusb_control_request_t request = {
        .bmRequestType_bit = {
            .recipient = TUSB_REQ_RCPT_INTERFACE,
            .type = TUSB_REQ_TYPE_CLASS,
            .direction = TUSB_DIR_OUT
        },
        .bRequest = 0,
        .wValue = 0,
        .wIndex = btd_ctx.itf_num,
        .wLength = total_len
    };

    tuh_xfer_t xfer = {
        .daddr = btd_ctx.dev_addr,
        .ep_addr = 0,
        .setup = &request,
        .buffer = btd_ctx.cmd_buf,
        .complete_cb = NULL,
        .user_data = 0
    };

    btd_ctx.pending_cmd = 1;
    btd_ctx.pending_opcode = opcode;

    bool result = tuh_control_xfer(&xfer);
    if (!result) {
        printf("[BTD] Failed to send HCI command 0x%04X\n", opcode);
        btd_ctx.pending_cmd = 0;
        btd_ctx.pending_opcode = 0;
    }

    return result;
}

// ============================================================================
// SPECIFIC HCI COMMANDS
// ============================================================================

bool btd_hci_reset(void)
{
    printf("[BTD] Sending HCI_Reset\n");
    return btd_send_hci_cmd(HCI_RESET, NULL, 0);
}

bool btd_hci_read_bd_addr(void)
{
    printf("[BTD] Sending HCI_Read_BD_ADDR\n");
    return btd_send_hci_cmd(HCI_READ_BD_ADDR, NULL, 0);
}

bool btd_hci_read_local_version(void)
{
    printf("[BTD] Sending HCI_Read_Local_Version_Info\n");
    return btd_send_hci_cmd(HCI_READ_LOCAL_VERSION_INFO, NULL, 0);
}

bool btd_hci_read_buffer_size(void)
{
    printf("[BTD] Sending HCI_Read_Buffer_Size\n");
    return btd_send_hci_cmd(HCI_READ_BUFFER_SIZE, NULL, 0);
}

bool btd_hci_set_event_mask(void)
{
    printf("[BTD] Sending HCI_Set_Event_Mask\n");
    // Event mask - enables most events including connection events
    // Bits enable: Inquiry Complete, Inquiry Result, Connection Complete,
    // Connection Request, Disconnection Complete, Auth Complete, Remote Name,
    // Encryption Change, PIN Request, Link Key Request, Link Key Notification,
    // Command Complete, Command Status, and many more
    // Byte 7 bit 5 (bit 61) = LE Meta Event - CRITICAL for BLE!
    uint8_t event_mask[8] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F, 0xFF, 0x20
    };
    return btd_send_hci_cmd(HCI_SET_EVENT_MASK, event_mask, 8);
}

bool btd_hci_inquiry(void)
{
    printf("[BTD] Sending HCI_Inquiry (scanning for devices...)\n");
    hci_inquiry_params_t params = {
        .lap = {0x33, 0x8B, 0x9E},  // GIAC (General Inquiry Access Code)
        .inquiry_length = 0x30,     // 48 * 1.28s = 61.44 seconds (max, like USB Host Shield)
        .num_responses = 0x00       // Unlimited responses
    };
    return btd_send_hci_cmd(HCI_INQUIRY, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_inquiry_cancel(void)
{
    printf("[BTD] Sending HCI_Inquiry_Cancel\n");
    return btd_send_hci_cmd(HCI_INQUIRY_CANCEL, NULL, 0);
}

bool btd_hci_create_connection(const uint8_t* bd_addr, uint8_t page_scan_rep_mode, uint16_t clock_offset)
{
    char addr_str[18];
    btd_bd_addr_to_str(bd_addr, addr_str);
    printf("[BTD] Creating connection to %s\n", addr_str);

    hci_create_conn_params_t params;
    memcpy(params.bd_addr, bd_addr, 6);
    params.packet_type = 0xCC18;  // DM1, DH1, DM3, DH3, DM5, DH5
    params.page_scan_rep_mode = page_scan_rep_mode;
    params.reserved = 0;
    params.clock_offset = clock_offset | 0x8000;  // Set clock offset valid bit
    params.allow_role_switch = 0x01;  // Allow role switch
    return btd_send_hci_cmd(HCI_CREATE_CONNECTION, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_write_local_name(const char* name)
{
    printf("[BTD] Sending HCI_Write_Local_Name: %s\n", name);
    uint8_t params[248];
    memset(params, 0, sizeof(params));
    strncpy((char*)params, name, 247);
    return btd_send_hci_cmd(HCI_WRITE_LOCAL_NAME, params, 248);
}

bool btd_hci_write_class_of_device(uint32_t cod)
{
    printf("[BTD] Sending HCI_Write_Class_Of_Device: 0x%06lX\n", (unsigned long)cod);
    uint8_t params[3] = {
        cod & 0xFF,
        (cod >> 8) & 0xFF,
        (cod >> 16) & 0xFF
    };
    return btd_send_hci_cmd(HCI_WRITE_CLASS_OF_DEVICE, params, 3);
}

bool btd_hci_write_scan_enable(uint8_t mode)
{
    printf("[BTD] Sending HCI_Write_Scan_Enable: 0x%02X\n", mode);
    return btd_send_hci_cmd(HCI_WRITE_SCAN_ENABLE, &mode, 1);
}

bool btd_hci_write_simple_pairing_mode(bool enable)
{
    printf("[BTD] Sending HCI_Write_Simple_Pairing_Mode: %d\n", enable);
    uint8_t mode = enable ? 1 : 0;
    return btd_send_hci_cmd(HCI_WRITE_SIMPLE_PAIRING_MODE, &mode, 1);
}

bool btd_hci_accept_connection(const uint8_t* bd_addr, uint8_t role)
{
    printf("[BTD] Accepting connection from ");
    char addr_str[18];
    btd_bd_addr_to_str(bd_addr, addr_str);
    printf("%s\n", addr_str);

    hci_accept_conn_params_t params;
    memcpy(params.bd_addr, bd_addr, 6);
    params.role = role;
    return btd_send_hci_cmd(HCI_ACCEPT_CONNECTION_REQUEST, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_reject_connection(const uint8_t* bd_addr, uint8_t reason)
{
    uint8_t params[7];
    memcpy(params, bd_addr, 6);
    params[6] = reason;
    return btd_send_hci_cmd(HCI_REJECT_CONNECTION_REQUEST, params, 7);
}

bool btd_hci_disconnect(uint16_t handle, uint8_t reason)
{
    printf("[BTD] Disconnecting handle 0x%04X\n", handle);
    hci_disconnect_params_t params = {
        .handle = handle,
        .reason = reason
    };
    return btd_send_hci_cmd(HCI_DISCONNECT, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_pin_code_reply(const uint8_t* bd_addr, const char* pin, uint8_t pin_len)
{
    hci_pin_code_reply_params_t params;
    memset(&params, 0, sizeof(params));
    memcpy(params.bd_addr, bd_addr, 6);
    params.pin_length = pin_len;
    memcpy(params.pin_code, pin, pin_len);
    return btd_send_hci_cmd(HCI_PIN_CODE_REQUEST_REPLY, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_pin_code_neg_reply(const uint8_t* bd_addr)
{
    return btd_send_hci_cmd(HCI_PIN_CODE_REQUEST_NEG_REPLY, bd_addr, 6);
}

bool btd_hci_link_key_reply(const uint8_t* bd_addr, const uint8_t* link_key)
{
    hci_link_key_reply_params_t params;
    memcpy(params.bd_addr, bd_addr, 6);
    memcpy(params.link_key, link_key, 16);
    return btd_send_hci_cmd(HCI_LINK_KEY_REQUEST_REPLY, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_link_key_neg_reply(const uint8_t* bd_addr)
{
    return btd_send_hci_cmd(HCI_LINK_KEY_REQUEST_NEG_REPLY, bd_addr, 6);
}

bool btd_hci_user_confirm_reply(const uint8_t* bd_addr)
{
    return btd_send_hci_cmd(HCI_USER_CONFIRM_REQUEST_REPLY, bd_addr, 6);
}

bool btd_hci_io_capability_reply(const uint8_t* bd_addr)
{
    hci_io_cap_reply_params_t params;
    memcpy(params.bd_addr, bd_addr, 6);
    params.io_capability = HCI_IO_CAP_NO_INPUT_NO_OUTPUT;
    params.oob_data_present = 0;
    params.auth_requirements = HCI_AUTH_MITM_NOT_REQUIRED;
    return btd_send_hci_cmd(HCI_IO_CAPABILITY_REQUEST_REPLY, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_authentication_requested(uint16_t handle)
{
    printf("[BTD] Requesting authentication for handle 0x%04X\n", handle);
    uint8_t params[2] = { handle & 0xFF, (handle >> 8) & 0xFF };
    return btd_send_hci_cmd(HCI_AUTH_REQUESTED, params, 2);
}

bool btd_hci_set_connection_encryption(uint16_t handle, bool enable)
{
    printf("[BTD] Setting encryption for handle 0x%04X: %s\n", handle, enable ? "ON" : "OFF");
    uint8_t params[3] = { handle & 0xFF, (handle >> 8) & 0xFF, enable ? 1 : 0 };
    return btd_send_hci_cmd(HCI_SET_CONNECTION_ENCRYPTION, params, 3);
}

bool btd_hci_remote_name_request(const uint8_t* bd_addr)
{
    char addr_str[18];
    btd_bd_addr_to_str(bd_addr, addr_str);
    printf("[BTD] Requesting remote name from %s\n", addr_str);

    hci_remote_name_request_params_t params;
    memcpy(params.bd_addr, bd_addr, 6);
    params.page_scan_rep_mode = 0x01;  // R1
    params.reserved = 0;
    params.clock_offset = 0;
    return btd_send_hci_cmd(HCI_REMOTE_NAME_REQUEST, (uint8_t*)&params, sizeof(params));
}

// ============================================================================
// HCI LE COMMANDS (Bluetooth Low Energy)
// ============================================================================

bool btd_hci_write_le_host_support(bool enable)
{
    printf("[BTD] Setting LE Host Support: %s\n", enable ? "enabled" : "disabled");
    uint8_t params[2] = {
        enable ? 0x01 : 0x00,  // LE Supported Host
        0x00                    // Simultaneous LE and BR/EDR (unused, set to 0)
    };
    return btd_send_hci_cmd(HCI_WRITE_LE_HOST_SUPPORT, params, 2);
}

bool btd_hci_le_read_buffer_size(void)
{
    printf("[BTD] Reading LE Buffer Size\n");
    return btd_send_hci_cmd(HCI_LE_READ_BUFFER_SIZE, NULL, 0);
}

bool btd_hci_le_set_event_mask(void)
{
    printf("[BTD] Setting LE Event Mask (including Extended Advertising)\n");
    // LE Event Mask bits (little-endian):
    // Byte 0: Bit 0-7
    //   Bit 0: LE Connection Complete
    //   Bit 1: LE Advertising Report (legacy)
    //   Bit 2: LE Connection Update Complete
    //   Bit 3: LE Read Remote Features Complete
    //   Bit 4: LE Long Term Key Request
    // Byte 1: Bit 8-15
    //   Bit 12: LE Extended Advertising Report (BT 5.0+) = byte 1, bit 4 = 0x10
    //
    // Enable: bits 0-4 (0x1F) + bit 12 (0x10 in byte 1)
    uint8_t le_event_mask[8] = {
        0x1F, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    return btd_send_hci_cmd(HCI_LE_SET_EVENT_MASK, le_event_mask, 8);
}

bool btd_hci_le_set_scan_parameters(uint8_t scan_type, uint16_t interval,
                                     uint16_t window, bool filter_duplicates)
{
    printf("[BTD] Setting LE Scan Parameters: type=%d interval=0x%04X window=0x%04X\n",
           scan_type, interval, window);
    (void)filter_duplicates;  // Used in scan enable, not parameters

    hci_le_set_scan_params_t params = {
        .scan_type = scan_type,
        .scan_interval = interval,
        .scan_window = window,
        .own_address_type = HCI_LE_ADDR_PUBLIC,
        .filter_policy = HCI_LE_SCAN_FILTER_ACCEPT_ALL
    };
    return btd_send_hci_cmd(HCI_LE_SET_SCAN_PARAMETERS, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_le_set_scan_enable(bool enable, bool filter_duplicates)
{
    printf("[BTD] %s LE Scanning (filter_dups=%d)\n",
           enable ? "Enabling" : "Disabling", filter_duplicates);

    hci_le_set_scan_enable_t params = {
        .enable = enable ? 0x01 : 0x00,
        .filter_duplicates = filter_duplicates ? 0x01 : 0x00
    };
    return btd_send_hci_cmd(HCI_LE_SET_SCAN_ENABLE, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_le_create_connection(const uint8_t* peer_addr, uint8_t peer_addr_type)
{
    char addr_str[18];
    btd_bd_addr_to_str(peer_addr, addr_str);

    // Mark that we're connecting (prevents multiple connection attempts)
    btd_ctx.le_connecting = true;

    // Use Extended Create Connection for BT 5.0+ (HCI version >= 9)
    if (btd_ctx.hci_version >= 9) {
        printf("[BTD] Creating LE EXTENDED connection to %s (addr_type=%d)\n", addr_str, peer_addr_type);

        hci_le_ext_create_conn_t params = {
            .filter_policy = 0x00,        // Use peer address
            .own_addr_type = HCI_LE_ADDR_PUBLIC,
            .peer_addr_type = peer_addr_type,
            .initiating_phys = 0x01,      // 1M PHY only
            .phy_1m = {
                .scan_interval = 0x0060,      // 60ms
                .scan_window = 0x0030,        // 30ms
                .conn_interval_min = 0x0018,  // 30ms
                .conn_interval_max = 0x0028,  // 50ms
                .conn_latency = 0x0000,
                .supervision_timeout = 0x00C8, // 2000ms (200 * 10ms) - longer timeout
                .min_ce_length = 0x0000,
                .max_ce_length = 0x0000
            }
        };
        memcpy(params.peer_addr, peer_addr, 6);

        return btd_send_hci_cmd(HCI_LE_EXT_CREATE_CONNECTION, (uint8_t*)&params, sizeof(params));
    }

    // Legacy Create Connection for older controllers
    printf("[BTD] Creating LE connection to %s (addr_type=%d)\n", addr_str, peer_addr_type);

    hci_le_create_conn_t params = {
        .scan_interval = 0x0060,      // 60ms
        .scan_window = 0x0030,        // 30ms
        .filter_policy = 0x00,        // Use peer address, not white list
        .peer_addr_type = peer_addr_type,
        .own_addr_type = HCI_LE_ADDR_PUBLIC,
        .conn_interval_min = 0x0018,  // 30ms (24 * 1.25ms)
        .conn_interval_max = 0x0028,  // 50ms (40 * 1.25ms)
        .conn_latency = 0x0000,       // No slave latency
        .supervision_timeout = 0x002C, // 440ms (44 * 10ms)
        .min_ce_length = 0x0000,
        .max_ce_length = 0x0000
    };
    memcpy(params.peer_addr, peer_addr, 6);

    return btd_send_hci_cmd(HCI_LE_CREATE_CONNECTION, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_le_create_connection_cancel(void)
{
    printf("[BTD] Cancelling LE connection attempt\n");
    return btd_send_hci_cmd(HCI_LE_CREATE_CONNECTION_CANCEL, NULL, 0);
}

bool btd_hci_le_connection_update(uint16_t handle, uint16_t interval_min,
                                   uint16_t interval_max, uint16_t latency,
                                   uint16_t timeout)
{
    printf("[BTD] Updating LE connection 0x%04X\n", handle);

    hci_le_conn_update_t params = {
        .handle = handle,
        .conn_interval_min = interval_min,
        .conn_interval_max = interval_max,
        .conn_latency = latency,
        .supervision_timeout = timeout,
        .min_ce_length = 0x0000,
        .max_ce_length = 0x0000
    };
    return btd_send_hci_cmd(HCI_LE_CONNECTION_UPDATE, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_le_start_encryption(uint16_t handle, const uint8_t* random,
                                  uint16_t ediv, const uint8_t* ltk)
{
    printf("[BTD] Starting LE encryption for handle 0x%04X\n", handle);

    hci_le_start_encryption_t params;
    params.handle = handle;
    memcpy(params.random, random, 8);
    params.ediv = ediv;
    memcpy(params.ltk, ltk, 16);
    return btd_send_hci_cmd(HCI_LE_START_ENCRYPTION, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_le_ltk_reply(uint16_t handle, const uint8_t* ltk)
{
    printf("[BTD] LE LTK reply for handle 0x%04X\n", handle);

    uint8_t params[18];
    params[0] = handle & 0xFF;
    params[1] = (handle >> 8) & 0xFF;
    memcpy(&params[2], ltk, 16);
    return btd_send_hci_cmd(HCI_LE_LTK_REQUEST_REPLY, params, 18);
}

bool btd_hci_le_ltk_neg_reply(uint16_t handle)
{
    printf("[BTD] LE LTK negative reply for handle 0x%04X\n", handle);

    uint8_t params[2] = { handle & 0xFF, (handle >> 8) & 0xFF };
    return btd_send_hci_cmd(HCI_LE_LTK_REQUEST_NEG_REPLY, params, 2);
}

// ============================================================================
// HCI LE EXTENDED COMMANDS (Bluetooth 5.0+)
// ============================================================================

bool btd_hci_le_set_ext_scan_parameters(uint8_t scan_type, uint16_t interval, uint16_t window)
{
    printf("[BTD] Setting LE Extended Scan Parameters: type=%d interval=0x%04X window=0x%04X\n",
           scan_type, interval, window);

    hci_le_set_ext_scan_params_t params = {
        .own_addr_type = HCI_LE_ADDR_PUBLIC,
        .filter_policy = HCI_LE_SCAN_FILTER_ACCEPT_ALL,
        .scanning_phys = 0x01,  // Scan on 1M PHY only
        .scan_type = scan_type,
        .scan_interval = interval,
        .scan_window = window
    };
    return btd_send_hci_cmd(HCI_LE_SET_EXT_SCAN_PARAMETERS, (uint8_t*)&params, sizeof(params));
}

bool btd_hci_le_set_ext_scan_enable(bool enable, bool filter_duplicates)
{
    printf("[BTD] %s LE Extended Scanning (filter_dups=%d)\n",
           enable ? "Enabling" : "Disabling", filter_duplicates);

    hci_le_set_ext_scan_enable_t params = {
        .enable = enable ? 0x01 : 0x00,
        .filter_duplicates = filter_duplicates ? 0x01 : 0x00,
        .duration = 0x0000,  // Scan continuously
        .period = 0x0000     // No periodic scanning
    };
    return btd_send_hci_cmd(HCI_LE_SET_EXT_SCAN_ENABLE, (uint8_t*)&params, sizeof(params));
}

// ============================================================================
// ACL DATA SENDING
// ============================================================================

bool btd_send_acl_data(uint16_t handle, uint8_t pb_flag, uint8_t bc_flag,
                       const uint8_t* data, uint16_t len)
{
    if (!btd_ctx.dongle_connected || btd_ctx.acl_credits == 0) {
        return false;
    }

    // Build ACL header
    uint16_t hdr = (handle & 0x0FFF) | ((pb_flag & 0x03) << 12) | ((bc_flag & 0x03) << 14);

    btd_ctx.acl_out_buf[0] = hdr & 0xFF;
    btd_ctx.acl_out_buf[1] = (hdr >> 8) & 0xFF;
    btd_ctx.acl_out_buf[2] = len & 0xFF;
    btd_ctx.acl_out_buf[3] = (len >> 8) & 0xFF;

    if (len > 0 && data != NULL) {
        memcpy(&btd_ctx.acl_out_buf[4], data, len);
    }

    uint16_t total_len = 4 + len;

    // Check if endpoint is busy - if so, mark as pending for later
    if (usbh_edpt_busy(btd_ctx.dev_addr, btd_ctx.ep_acl_out)) {
        btd_ctx.acl_out_pending = true;
        btd_ctx.acl_out_pending_len = total_len;
        return true;  // Return success, will be sent from task loop
    }

    // Send via bulk OUT
    btd_ctx.acl_credits--;
    return usbh_edpt_xfer(btd_ctx.dev_addr, btd_ctx.ep_acl_out, btd_ctx.acl_out_buf, total_len);
}

// ============================================================================
// HCI EVENT PROCESSING
// ============================================================================

static void btd_process_event(const uint8_t* data, uint16_t len)
{
    if (len < 2) return;

    const hci_event_t* evt = (const hci_event_t*)data;

    // Debug: show ALL events when in running state (except common ones)
    if (btd_ctx.state == BTD_STATE_RUNNING &&
        evt->event_code != HCI_EVENT_NUMBER_OF_COMPLETED_PACKETS &&
        evt->event_code != 0x00) {
        printf("[BTD] Event received: code=0x%02X len=%d\n", evt->event_code, evt->param_len);
    }

    switch (evt->event_code) {
        case HCI_EVENT_COMMAND_COMPLETE: {
            const hci_event_cmd_complete_t* cc = (const hci_event_cmd_complete_t*)evt->params;

            // Only process if this is the response we're waiting for
            if (cc->opcode != btd_ctx.pending_opcode) {
                // Stale or duplicate event, ignore
                return;
            }

            btd_ctx.pending_cmd = 0;  // Command completed, allow next
            btd_ctx.pending_opcode = 0;

            printf("[BTD] Command Complete: opcode=0x%04X\n", cc->opcode);

            // Handle specific command completions
            switch (cc->opcode) {
                case HCI_RESET:
                    printf("[BTD] Reset complete\n");
                    btd_ctx.state = BTD_STATE_READ_BD_ADDR;
                    break;

                case HCI_READ_BD_ADDR: {
                    const hci_return_read_bd_addr_t* ret = (const hci_return_read_bd_addr_t*)cc->return_params;
                    if (ret->status == HCI_SUCCESS) {
                        memcpy(btd_ctx.bd_addr, ret->bd_addr, 6);
                        char addr_str[18];
                        btd_bd_addr_to_str(btd_ctx.bd_addr, addr_str);
                        printf("[BTD] Local BD_ADDR: %s\n", addr_str);
                        // Cache for DS3 pairing (persists even when dongle unplugged)
                        btd_linkkey_set_local_addr(btd_ctx.bd_addr);
                    }
                    btd_ctx.state = BTD_STATE_READ_VERSION;
                    break;
                }

                case HCI_READ_LOCAL_VERSION_INFO: {
                    const hci_return_read_local_version_t* ret = (const hci_return_read_local_version_t*)cc->return_params;
                    if (ret->status == HCI_SUCCESS) {
                        btd_ctx.hci_version = ret->hci_version;
                        btd_ctx.manufacturer = ret->manufacturer;
                        // HCI versions: 6=BT4.0, 7=BT4.1, 8=BT4.2, 9=BT5.0, 10=BT5.1, 11=BT5.2, 12=BT5.3, 13=BT5.4
                        const char* bt_version = "Unknown";
                        switch (ret->hci_version) {
                            case 6: bt_version = "4.0 (BLE supported)"; break;
                            case 7: bt_version = "4.1 (BLE supported)"; break;
                            case 8: bt_version = "4.2 (BLE supported)"; break;
                            case 9: bt_version = "5.0 (BLE+Extended Adv)"; break;
                            case 10: bt_version = "5.1"; break;
                            case 11: bt_version = "5.2"; break;
                            case 12: bt_version = "5.3"; break;
                            case 13: bt_version = "5.4"; break;
                            default: if (ret->hci_version < 6) bt_version = "<4.0 (NO BLE)"; break;
                        }
                        printf("[BTD] Bluetooth Version: %s (HCI=%d, Manufacturer=0x%04X)\n",
                               bt_version, ret->hci_version, ret->manufacturer);
                    }
                    btd_ctx.state = BTD_STATE_READ_BUFFER_SIZE;
                    break;
                }

                case HCI_READ_BUFFER_SIZE: {
                    const hci_return_read_buffer_size_t* ret = (const hci_return_read_buffer_size_t*)cc->return_params;
                    if (ret->status == HCI_SUCCESS) {
                        btd_ctx.acl_mtu = ret->acl_data_packet_length;
                        btd_ctx.acl_credits = ret->total_num_acl_packets;
                        printf("[BTD] ACL MTU: %d, Credits: %d\n",
                               btd_ctx.acl_mtu, btd_ctx.acl_credits);
                    }
                    btd_ctx.state = BTD_STATE_SET_EVENT_MASK;
                    break;
                }

                case HCI_SET_EVENT_MASK:
                    printf("[BTD] Event mask set\n");
                    btd_ctx.state = BTD_STATE_WRITE_NAME;
                    break;

                case HCI_WRITE_LOCAL_NAME:
                    btd_ctx.state = BTD_STATE_WRITE_COD;
                    break;

                case HCI_WRITE_CLASS_OF_DEVICE:
                    btd_ctx.state = BTD_STATE_WRITE_SSP;
                    break;

                case HCI_WRITE_SIMPLE_PAIRING_MODE:
                    btd_ctx.state = BTD_STATE_WRITE_SCAN;
                    break;

                case HCI_WRITE_SCAN_ENABLE:
                    btd_ctx.scan_enabled = true;
                    printf("[BTD] Scan enabled - continuing to BLE setup\n");
                    // Start ACL endpoint now that basic initialization is done
                    printf("[BTD] Starting ACL IN endpoint 0x%02X\n", btd_ctx.ep_acl_in);
                    if (!usbh_edpt_xfer(btd_ctx.dev_addr, btd_ctx.ep_acl_in,
                                       btd_ctx.acl_in_buf, sizeof(btd_ctx.acl_in_buf))) {
                        printf("[BTD] Failed to start ACL endpoint!\n");
                    }
                    // Continue to BLE initialization
                    btd_ctx.state = BTD_STATE_LE_WRITE_HOST_SUPPORT;
                    break;

                case HCI_INQUIRY_CANCEL:
                    // Check if this was for a pending BLE connection
                    if (btd_ctx.le_connect_pending) {
                        printf("[BTD] Inquiry cancelled, now stopping scan...\n");
                        // Stop extended scanning - connection will be initiated when scan stops
                        if (btd_ctx.hci_version >= 9) {
                            btd_hci_le_set_ext_scan_enable(false, false);
                        } else {
                            btd_hci_le_set_scan_enable(false, false);
                        }
                    } else {
                        // Inquiry was cancelled for other reason, go to running state
                        btd_ctx.state = BTD_STATE_RUNNING;
                    }
                    break;

                // LE Command Complete handlers
                case HCI_WRITE_LE_HOST_SUPPORT: {
                    uint8_t status = cc->return_params[0];
                    if (status == HCI_SUCCESS) {
                        printf("[BTD] LE Host Support enabled\n");
                        btd_ctx.le_supported = true;
                        btd_ctx.le_enabled = true;
                        btd_ctx.state = BTD_STATE_LE_READ_BUFFER_SIZE;
                    } else {
                        printf("[BTD] LE not supported (status=0x%02X), skipping BLE init\n", status);
                        btd_ctx.le_supported = false;
                        btd_ctx.state = BTD_STATE_INQUIRY;
                    }
                    break;
                }

                case HCI_LE_READ_BUFFER_SIZE: {
                    const hci_return_le_read_buffer_size_t* ret =
                        (const hci_return_le_read_buffer_size_t*)cc->return_params;
                    if (ret->status == HCI_SUCCESS) {
                        btd_ctx.le_acl_mtu = ret->le_acl_data_packet_length;
                        btd_ctx.le_acl_credits = ret->total_num_le_acl_packets;
                        printf("[BTD] LE Buffer Size: MTU=%d, Credits=%d\n",
                               btd_ctx.le_acl_mtu, btd_ctx.le_acl_credits);
                    }
                    btd_ctx.state = BTD_STATE_LE_SET_EVENT_MASK;
                    break;
                }

                case HCI_LE_SET_EVENT_MASK:
                    printf("[BTD] LE Event Mask set\n");
                    btd_ctx.state = BTD_STATE_LE_SET_SCAN_PARAMS;
                    break;

                case HCI_LE_SET_SCAN_PARAMETERS:
                    printf("[BTD] LE Scan Parameters set\n");
                    btd_ctx.state = BTD_STATE_LE_SCAN_ENABLE;
                    break;

                case HCI_LE_SET_SCAN_ENABLE: {
                    uint8_t status = cc->return_params[0];
                    if (status == HCI_SUCCESS) {
                        btd_ctx.le_scan_enabled = true;
                        printf("[BTD] *** BLE SCANNING ACTIVE *** - now starting Classic inquiry\n");
                    } else {
                        printf("[BTD] LE Scan enable FAILED (status=0x%02X) - BLE devices won't be detected\n", status);
                        btd_ctx.le_scan_enabled = false;
                    }
                    // Move to classic inquiry regardless
                    btd_ctx.state = BTD_STATE_INQUIRY;
                    break;
                }

                case HCI_LE_CREATE_CONNECTION_CANCEL:
                    printf("[BTD] LE Connection attempt cancelled\n");
                    break;

                // Extended scanning (BT 5.0+)
                case HCI_LE_SET_EXT_SCAN_PARAMETERS:
                    printf("[BTD] LE Extended Scan Parameters set\n");
                    btd_ctx.state = BTD_STATE_LE_SCAN_ENABLE;
                    break;

                case HCI_LE_SET_EXT_SCAN_ENABLE: {
                    uint8_t status = cc->return_params[0];
                    if (status == HCI_SUCCESS) {
                        // Check if this was a scan disable for pending connection
                        if (btd_ctx.le_connect_pending) {
                            btd_ctx.le_scan_enabled = false;
                            printf("[BTD] Scan stopped, connection will be initiated shortly...\n");
                            // Don't connect immediately - let task loop handle it after a delay
                            btd_ctx.le_connecting = true;  // Signal task to connect
                            btd_ctx.state = BTD_STATE_RUNNING;  // Go to running, don't restart scans
                            break;
                        }
                        btd_ctx.le_scan_enabled = true;
                        printf("[BTD] *** BLE EXTENDED SCANNING ACTIVE *** - now starting Classic inquiry\n");
                        btd_ctx.state = BTD_STATE_INQUIRY;
                    } else {
                        printf("[BTD] LE Extended Scan command FAILED (status=0x%02X)\n", status);
                        btd_ctx.le_scan_enabled = false;
                        btd_ctx.le_connect_pending = false;  // Clear pending on failure
                        btd_ctx.state = BTD_STATE_INQUIRY;
                    }
                    break;
                }
            }
            break;
        }

        case HCI_EVENT_COMMAND_STATUS: {
            const hci_event_cmd_status_t* cs = (const hci_event_cmd_status_t*)evt->params;
            // Only process if this is the response we're waiting for
            if (cs->opcode != btd_ctx.pending_opcode && btd_ctx.pending_opcode != 0) {
                // Stale or duplicate event, ignore
                return;
            }
            btd_ctx.pending_cmd = 0;  // Command acknowledged, allow next
            btd_ctx.pending_opcode = 0;
            if (cs->status != HCI_SUCCESS) {
                printf("[BTD] Command Status error: 0x%02X for opcode 0x%04X\n",
                       cs->status, cs->opcode);
                // If inquiry failed, go to running state anyway
                if (cs->opcode == HCI_INQUIRY) {
                    btd_ctx.state = BTD_STATE_RUNNING;
                }
            } else if (cs->opcode == HCI_INQUIRY) {
                printf("[BTD] Inquiry started - searching for devices...\n");
                // Move to running state - inquiry results come as events
                btd_ctx.state = BTD_STATE_RUNNING;
            } else if (cs->opcode == HCI_CREATE_CONNECTION) {
                printf("[BTD] Connection attempt started\n");
            }
            break;
        }

        case HCI_EVENT_INQUIRY_COMPLETE: {
            printf("[BTD] Inquiry complete - restarting scan\n");
            // Restart inquiry to keep scanning for devices
            btd_ctx.state = BTD_STATE_INQUIRY;
            break;
        }

        case HCI_EVENT_INQUIRY_RESULT: {
            // Standard inquiry result (may contain multiple devices)
            uint8_t num_responses = evt->params[0];
            const hci_inquiry_result_t* results = (const hci_inquiry_result_t*)&evt->params[1];
            printf("[BTD] Inquiry result: %d device(s)\n", num_responses);

            for (int i = 0; i < num_responses; i++) {
                char addr_str[18];
                btd_bd_addr_to_str(results[i].bd_addr, addr_str);
                uint8_t major_class = (results[i].class_of_device[1] & 0x1F);
                printf("[BTD] Device %d: %s, COD: %02X%02X%02X (major=%d)\n",
                       i, addr_str,
                       results[i].class_of_device[2],
                       results[i].class_of_device[1],
                       results[i].class_of_device[0],
                       major_class);

                // Only connect to Peripheral devices (major class 5 = gamepads, keyboards, mice)
                if (major_class != 5) {
                    printf("[BTD] Skipping non-peripheral device (major=%d)\n", major_class);
                    continue;
                }

                // If we have a stored link key, don't initiate - wait for device to connect to us
                // This avoids race conditions with devices that prefer to initiate reconnection
                if (btd_linkkey_find(results[i].bd_addr)) {
                    printf("[BTD] Known device (has stored key), waiting for it to connect...\n");
                    continue;
                }

                printf("[BTD] New peripheral found, attempting connection...\n");
                // Store pending connection info for later use
                memcpy(pending_connection.bd_addr, results[i].bd_addr, 6);
                memcpy(pending_connection.class_of_device, results[i].class_of_device, 3);
                pending_connection.valid = true;

                btd_hci_inquiry_cancel();  // Stop inquiry
                btd_hci_create_connection(results[i].bd_addr,
                                         results[i].page_scan_rep_mode,
                                         results[i].clock_offset);
                return;  // Only connect to first peripheral found
            }
            break;
        }

        case HCI_EVENT_INQUIRY_RESULT_WITH_RSSI: {
            // Inquiry result with RSSI (may contain multiple devices)
            uint8_t num_responses = evt->params[0];
            const hci_inquiry_result_rssi_t* results = (const hci_inquiry_result_rssi_t*)&evt->params[1];
            printf("[BTD] Inquiry result (RSSI): %d device(s)\n", num_responses);

            for (int i = 0; i < num_responses; i++) {
                char addr_str[18];
                btd_bd_addr_to_str(results[i].bd_addr, addr_str);
                uint8_t major_class = (results[i].class_of_device[1] & 0x1F);
                printf("[BTD] Device %d: %s, COD: %02X%02X%02X (major=%d), RSSI: %d\n",
                       i, addr_str,
                       results[i].class_of_device[2],
                       results[i].class_of_device[1],
                       results[i].class_of_device[0],
                       major_class,
                       results[i].rssi);

                // Only connect to Peripheral devices (major class 5 = gamepads, keyboards, mice)
                if (major_class != 5) {
                    printf("[BTD] Skipping non-peripheral device (major=%d)\n", major_class);
                    continue;
                }

                // If we have a stored link key, don't initiate - wait for device to connect to us
                // This avoids race conditions with devices that prefer to initiate reconnection
                if (btd_linkkey_find(results[i].bd_addr)) {
                    printf("[BTD] Known device (has stored key), waiting for it to connect...\n");
                    continue;
                }

                printf("[BTD] New peripheral found, attempting connection...\n");
                // Store pending connection info for later use
                memcpy(pending_connection.bd_addr, results[i].bd_addr, 6);
                memcpy(pending_connection.class_of_device, results[i].class_of_device, 3);
                pending_connection.valid = true;

                btd_hci_inquiry_cancel();  // Stop inquiry
                btd_hci_create_connection(results[i].bd_addr,
                                         results[i].page_scan_rep_mode,
                                         results[i].clock_offset);
                return;  // Only connect to first peripheral found
            }
            break;
        }

        case HCI_EVENT_EXTENDED_INQUIRY_RESULT: {
            const hci_event_extended_inquiry_result_t* eir = (const hci_event_extended_inquiry_result_t*)evt->params;
            char addr_str[18];
            btd_bd_addr_to_str(eir->bd_addr, addr_str);
            uint8_t major_class = (eir->class_of_device[1] & 0x1F);
            printf("[BTD] Device (EIR): %s, COD: %02X%02X%02X (major=%d), RSSI: %d\n",
                   addr_str,
                   eir->class_of_device[2],
                   eir->class_of_device[1],
                   eir->class_of_device[0],
                   major_class,
                   eir->rssi);

            // Only connect to Peripheral devices (major class 5 = gamepads, keyboards, mice)
            if (major_class != 5) {
                printf("[BTD] Skipping non-peripheral device (major=%d)\n", major_class);
                break;
            }

            // If we have a stored link key, don't initiate - wait for device to connect to us
            // This avoids race conditions with devices that prefer to initiate reconnection
            if (btd_linkkey_find(eir->bd_addr)) {
                printf("[BTD] Known device (has stored key), waiting for it to connect...\n");
                break;
            }

            printf("[BTD] New peripheral found, attempting connection...\n");
            // Store pending connection info for later use
            memcpy(pending_connection.bd_addr, eir->bd_addr, 6);
            memcpy(pending_connection.class_of_device, eir->class_of_device, 3);
            pending_connection.valid = true;

            btd_hci_inquiry_cancel();  // Stop inquiry
            btd_hci_create_connection(eir->bd_addr,
                                     eir->page_scan_rep_mode,
                                     eir->clock_offset);
            break;
        }

        case HCI_EVENT_CONNECTION_REQUEST: {
            const hci_event_conn_request_t* req = (const hci_event_conn_request_t*)evt->params;
            char addr_str[18];
            btd_bd_addr_to_str(req->bd_addr, addr_str);
            printf("[BTD] Connection request from %s, COD: %02X%02X%02X, Type: %d\n",
                   addr_str, req->class_of_device[2], req->class_of_device[1],
                   req->class_of_device[0], req->link_type);

            // Store pending connection info for incoming connections
            memcpy(pending_connection.bd_addr, req->bd_addr, 6);
            memcpy(pending_connection.class_of_device, req->class_of_device, 3);
            pending_connection.valid = true;

            // Accept ACL connections
            if (req->link_type == HCI_LINK_TYPE_ACL) {
                printf("[BTD] Accepting connection from %s\n", addr_str);
                // Cancel inquiry if running to avoid command conflicts
                if (btd_ctx.state == BTD_STATE_INQUIRY) {
                    btd_hci_inquiry_cancel();
                }
                btd_hci_accept_connection(req->bd_addr, HCI_ROLE_SLAVE);
            }
            break;
        }

        case HCI_EVENT_CONNECTION_COMPLETE: {
            const hci_event_conn_complete_t* cc = (const hci_event_conn_complete_t*)evt->params;
            char addr_str[18];
            btd_bd_addr_to_str(cc->bd_addr, addr_str);

            if (cc->status == HCI_SUCCESS) {
                printf("[BTD] Connection complete: %s, handle=0x%04X\n", addr_str, cc->handle);

                // Stop scanning while connected
                btd_ctx.state = BTD_STATE_RUNNING;

                // Allocate connection slot
                btd_connection_t* conn = btd_alloc_connection();
                if (conn) {
                    conn->state = BTD_CONN_CONNECTED;
                    memcpy(conn->bd_addr, cc->bd_addr, 6);
                    conn->handle = cc->handle;
                    conn->name[0] = '\0';  // Clear name, will be filled by remote name request

                    // Copy class of device from pending connection if addresses match
                    if (pending_connection.valid &&
                        memcmp(pending_connection.bd_addr, cc->bd_addr, 6) == 0) {
                        memcpy(conn->class_of_device, pending_connection.class_of_device, 3);
                        pending_connection.valid = false;
                    }

                    btd_ctx.num_connections++;

                    // Request remote name to identify the device
                    btd_hci_remote_name_request(cc->bd_addr);

                    // Notify higher layer
                    uint8_t idx = conn - btd_ctx.connections;
                    btd_on_connection(idx);
                }
            } else {
                printf("[BTD] Connection failed: %s, status=0x%02X\n", addr_str, cc->status);
                // Restart inquiry to keep scanning for devices
                printf("[BTD] Restarting inquiry after failed connection\n");
                btd_ctx.state = BTD_STATE_INQUIRY;
            }
            break;
        }

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            const hci_event_disconn_complete_t* dc = (const hci_event_disconn_complete_t*)evt->params;
            printf("[BTD] Disconnection: handle=0x%04X, reason=0x%02X\n", dc->handle, dc->reason);

            btd_connection_t* conn = btd_find_connection_by_handle(dc->handle);
            if (conn) {
                uint8_t idx = conn - btd_ctx.connections;
                conn->state = BTD_CONN_DISCONNECTED;
                conn->handle = 0xFFFF;
                btd_ctx.num_connections--;

                btd_on_disconnection(idx);
            }

            // Restart scanning for devices after disconnect
            if (btd_ctx.num_connections == 0) {
                printf("[BTD] All devices disconnected, restarting inquiry\n");
                btd_ctx.state = BTD_STATE_INQUIRY;
            }
            break;
        }

        case HCI_EVENT_PIN_CODE_REQUEST: {
            const hci_event_pin_code_request_t* pin = (const hci_event_pin_code_request_t*)evt->params;
            char addr_str[18];
            btd_bd_addr_to_str(pin->bd_addr, addr_str);
            printf("[BTD] PIN code request from %s\n", addr_str);

            // Use default PIN "0000" for legacy pairing
            btd_hci_pin_code_reply(pin->bd_addr, "0000", 4);
            break;
        }

        case HCI_EVENT_LINK_KEY_REQUEST: {
            const hci_event_link_key_request_t* lk = (const hci_event_link_key_request_t*)evt->params;
            char addr_str[18];
            btd_bd_addr_to_str(lk->bd_addr, addr_str);
            printf("[BTD] Link key request from %s\n", addr_str);

            // Look up stored link key
            const uint8_t* stored_key = btd_linkkey_find(lk->bd_addr);
            if (stored_key) {
                printf("[BTD] Found stored link key, replying\n");
                btd_hci_link_key_reply(lk->bd_addr, stored_key);
            } else {
                // Check if this is a DS3 - they don't do normal pairing
                // DS3 expects host to have a link key (can be zeros)
                // DS3 COD is 0x000508, name might not be available yet
                btd_connection_t* conn = btd_find_connection_by_bdaddr(lk->bd_addr);
                bool is_ds3 = false;
                if (conn) {
                    // Check COD: DS3 is 0x000508 (bytes: 08 05 00)
                    if (conn->class_of_device[0] == 0x08 &&
                        conn->class_of_device[1] == 0x05 &&
                        conn->class_of_device[2] == 0x00) {
                        is_ds3 = true;
                    }
                    // Also check name if available
                    if (conn->name[0] && strstr(conn->name, "PLAYSTATION(R)3")) {
                        is_ds3 = true;
                    }
                }

                if (is_ds3) {
                    printf("[BTD] DS3 detected - using zero link key\n");
                    static const uint8_t zero_key[16] = {0};
                    btd_hci_link_key_reply(lk->bd_addr, zero_key);
                } else {
                    printf("[BTD] No stored link key, triggering new pairing\n");
                    btd_hci_link_key_neg_reply(lk->bd_addr);
                }
            }
            break;
        }

        case HCI_EVENT_LINK_KEY_NOTIFICATION: {
            const hci_event_link_key_notification_t* lkn = (const hci_event_link_key_notification_t*)evt->params;
            char addr_str[18];
            btd_bd_addr_to_str(lkn->bd_addr, addr_str);
            printf("[BTD] Link key notification from %s, type=%d\n", addr_str, lkn->key_type);

            // Store link key for future reconnection
            btd_linkkey_store(lkn->bd_addr, lkn->link_key, lkn->key_type);
            break;
        }

        case HCI_EVENT_IO_CAPABILITY_REQUEST: {
            const hci_event_io_cap_request_t* io = (const hci_event_io_cap_request_t*)evt->params;
            char addr_str[18];
            btd_bd_addr_to_str(io->bd_addr, addr_str);
            printf("[BTD] IO capability request from %s\n", addr_str);

            btd_hci_io_capability_reply(io->bd_addr);
            break;
        }

        case HCI_EVENT_USER_CONFIRM_REQUEST: {
            const hci_event_user_confirm_request_t* uc = (const hci_event_user_confirm_request_t*)evt->params;
            char addr_str[18];
            btd_bd_addr_to_str(uc->bd_addr, addr_str);
            printf("[BTD] User confirmation request from %s, value=%lu\n",
                   addr_str, (unsigned long)uc->numeric_value);

            // Auto-accept (no display/keyboard)
            btd_hci_user_confirm_reply(uc->bd_addr);
            break;
        }

        case HCI_EVENT_IO_CAPABILITY_RESPONSE: {
            const hci_event_io_cap_response_t* io = (const hci_event_io_cap_response_t*)evt->params;
            char addr_str[18];
            btd_bd_addr_to_str(io->bd_addr, addr_str);
            printf("[BTD] IO capability response from %s: io=%d oob=%d auth=%d\n",
                   addr_str, io->io_capability, io->oob_data_present, io->auth_requirements);
            break;
        }

        case HCI_EVENT_SIMPLE_PAIRING_COMPLETE: {
            const hci_event_simple_pairing_complete_t* sp = (const hci_event_simple_pairing_complete_t*)evt->params;
            char addr_str[18];
            btd_bd_addr_to_str(sp->bd_addr, addr_str);
            printf("[BTD] Simple pairing complete: %s, status=0x%02X\n", addr_str, sp->status);
            if (sp->status != HCI_SUCCESS) {
                printf("[BTD] SSP failed - device may need to be in pairing mode\n");
            }
            break;
        }

        case HCI_EVENT_AUTH_COMPLETE: {
            uint8_t status = evt->params[0];
            uint16_t handle = evt->params[1] | (evt->params[2] << 8);
            printf("[BTD] Authentication complete: handle=0x%04X, status=0x%02X\n", handle, status);

            btd_connection_t* conn = btd_find_connection_by_handle(handle);
            if (conn) {
                uint8_t idx = conn - btd_ctx.connections;
                btd_on_auth_complete(idx, status);
            }
            break;
        }

        case HCI_EVENT_ENCRYPT_CHANGE: {
            uint8_t status = evt->params[0];
            uint16_t handle = evt->params[1] | (evt->params[2] << 8);
            uint8_t enabled = evt->params[3];
            printf("[BTD] Encryption change: handle=0x%04X, status=0x%02X, enabled=%d\n",
                   handle, status, enabled);

            btd_connection_t* conn = btd_find_connection_by_handle(handle);
            if (conn) {
                uint8_t idx = conn - btd_ctx.connections;
                btd_on_encryption_change(idx, status, enabled != 0);
            }
            break;
        }

        case HCI_EVENT_NUMBER_OF_COMPLETED_PACKETS: {
            // Replenish ACL credits
            const hci_event_num_completed_packets_t* ncp = (const hci_event_num_completed_packets_t*)evt->params;
            const uint8_t* ptr = (const uint8_t*)(ncp + 1);
            for (int i = 0; i < ncp->num_handles; i++) {
                // uint16_t handle = ptr[0] | (ptr[1] << 8);
                uint16_t completed = ptr[2] | (ptr[3] << 8);
                btd_ctx.acl_credits += completed;
                ptr += 4;
            }
            break;
        }

        case HCI_EVENT_REMOTE_NAME_COMPLETE: {
            const hci_event_remote_name_complete_t* rn = (const hci_event_remote_name_complete_t*)evt->params;
            char addr_str[18];
            btd_bd_addr_to_str(rn->bd_addr, addr_str);

            btd_connection_t* conn = btd_find_connection_by_bdaddr(rn->bd_addr);
            if (rn->status == HCI_SUCCESS) {
                printf("[BTD] Remote name from %s: %s\n", addr_str, rn->remote_name);

                // Store name in connection record
                if (conn) {
                    strncpy(conn->name, rn->remote_name, BTD_MAX_NAME_LEN - 1);
                    conn->name[BTD_MAX_NAME_LEN - 1] = '\0';

                    // Notify higher layer - auth can now proceed
                    uint8_t idx = conn - btd_ctx.connections;
                    btd_on_remote_name_complete(idx, conn->name);
                }
            } else {
                printf("[BTD] Remote name request failed for %s: 0x%02X\n", addr_str, rn->status);
                // Still notify so auth can proceed with empty name
                if (conn) {
                    uint8_t idx = conn - btd_ctx.connections;
                    btd_on_remote_name_complete(idx, "");
                }
            }
            break;
        }

        case HCI_EVENT_LE_META: {
            // LE Meta Event - contains BLE sub-events
            if (evt->param_len < 1) break;

            const hci_le_meta_event_t* le_meta = (const hci_le_meta_event_t*)evt->params;
            printf("[BTD] LE Meta Event: subevent=0x%02X, param_len=%d\n",
                   le_meta->subevent, evt->param_len);

            switch (le_meta->subevent) {
                case HCI_LE_EVT_CONNECTION_COMPLETE: {
                    const hci_le_conn_complete_t* le_conn = (const hci_le_conn_complete_t*)le_meta->params;
                    char addr_str[18];
                    btd_bd_addr_to_str(le_conn->peer_addr, addr_str);

                    // Connection attempt finished (success or failure)
                    btd_ctx.le_connecting = false;
                    btd_ctx.le_connect_pending = false;

                    if (le_conn->status == HCI_SUCCESS) {
                        printf("[BTD] LE Connection complete: %s, handle=0x%04X, role=%s\n",
                               addr_str, le_conn->handle,
                               le_conn->role == 0 ? "Master" : "Slave");

                        // Allocate connection slot for BLE device
                        btd_connection_t* conn = btd_alloc_connection();
                        if (conn) {
                            conn->state = BTD_CONN_CONNECTED;
                            memcpy(conn->bd_addr, le_conn->peer_addr, 6);
                            conn->handle = le_conn->handle;
                            conn->is_ble = true;
                            conn->name[0] = '\0';
                            // BLE uses ATT on fixed CID 0x0004
                            conn->control_cid = 0x0004;

                            btd_ctx.num_connections++;
                            btd_ctx.le_scan_enabled = false;  // Stop scanning when connected

                            uint8_t idx = conn - btd_ctx.connections;
                            printf("[BTD] *** BLE CONNECTED TO XBOX! conn_idx=%d ***\n", idx);
                            btd_on_le_connection(idx);
                        }
                    } else {
                        printf("[BTD] LE Connection failed: %s, status=0x%02X\n",
                               addr_str, le_conn->status);
                    }
                    break;
                }

                case HCI_LE_EVT_ADVERTISING_REPORT: {
                    const hci_le_adv_report_t* adv = (const hci_le_adv_report_t*)le_meta->params;
                    const uint8_t* ptr = (const uint8_t*)(adv + 1);

                    for (int i = 0; i < adv->num_reports; i++) {
                        const hci_le_adv_report_entry_t* entry = (const hci_le_adv_report_entry_t*)ptr;
                        const uint8_t* adv_data = ptr + sizeof(hci_le_adv_report_entry_t);
                        int8_t rssi = (int8_t)adv_data[entry->data_length];

                        char addr_str[18];
                        btd_bd_addr_to_str(entry->addr, addr_str);
                        printf("[BTD] LE Adv: %s type=%d addr_type=%d rssi=%d len=%d\n",
                               addr_str, entry->event_type, entry->addr_type, rssi, entry->data_length);

                        // Notify higher layer of advertisement
                        btd_on_le_adv_report(entry->addr, entry->addr_type, rssi,
                                             adv_data, entry->data_length);

                        // Move to next report
                        ptr += sizeof(hci_le_adv_report_entry_t) + entry->data_length + 1;
                    }
                    break;
                }

                case HCI_LE_EVT_CONNECTION_UPDATE_COMPLETE: {
                    const hci_le_conn_update_complete_t* update = (const hci_le_conn_update_complete_t*)le_meta->params;
                    printf("[BTD] LE Connection update: handle=0x%04X status=0x%02X interval=%d latency=%d timeout=%d\n",
                           update->handle, update->status, update->conn_interval,
                           update->conn_latency, update->supervision_timeout);
                    break;
                }

                case HCI_LE_EVT_LONG_TERM_KEY_REQUEST: {
                    const hci_le_ltk_request_t* ltk_req = (const hci_le_ltk_request_t*)le_meta->params;
                    printf("[BTD] LE LTK request: handle=0x%04X\n", ltk_req->handle);
                    // For now, reject - we don't have bonding support yet
                    btd_hci_le_ltk_neg_reply(ltk_req->handle);
                    break;
                }

                case HCI_LE_EVT_EXTENDED_ADV_REPORT: {
                    // Extended Advertising Report (Bluetooth 5.0+)
                    uint8_t num_reports = le_meta->params[0];
                    const uint8_t* ptr = &le_meta->params[1];

                    for (int i = 0; i < num_reports; i++) {
                        const hci_le_ext_adv_report_entry_t* entry = (const hci_le_ext_adv_report_entry_t*)ptr;
                        const uint8_t* adv_data = ptr + sizeof(hci_le_ext_adv_report_entry_t);

                        char addr_str[18];
                        btd_bd_addr_to_str(entry->addr, addr_str);
                        printf("[BTD] LE Extended Adv: %s evt=0x%04X addr_type=%d rssi=%d len=%d\n",
                               addr_str, entry->event_type, entry->addr_type, entry->rssi, entry->data_length);

                        // Parse AD structures to extract device name
                        char dev_name[32] = {0};
                        if (entry->data_length > 0) {
                            uint8_t ad_offset = 0;
                            while (ad_offset < entry->data_length) {
                                uint8_t ad_len = adv_data[ad_offset];
                                if (ad_len == 0 || ad_offset + 1 + ad_len > entry->data_length) break;
                                uint8_t ad_type = adv_data[ad_offset + 1];
                                // 0x08 = Shortened Local Name, 0x09 = Complete Local Name
                                if ((ad_type == 0x08 || ad_type == 0x09) && ad_len > 1) {
                                    uint8_t name_len = ad_len - 1;
                                    if (name_len > sizeof(dev_name) - 1) name_len = sizeof(dev_name) - 1;
                                    memcpy(dev_name, &adv_data[ad_offset + 2], name_len);
                                    dev_name[name_len] = '\0';
                                    printf("[BTD]   Name: %s\n", dev_name);
                                    break;
                                }
                                ad_offset += ad_len + 1;
                            }
                        }

                        // Check for Xbox controller and initiate connection
                        if (dev_name[0] && strstr(dev_name, "Xbox") != NULL) {
                            printf("[BTD] *** XBOX CONTROLLER FOUND! ***\n");
                            // Check if connectable and not already connecting/pending
                            if ((entry->event_type & HCI_LE_EXT_ADV_EVT_CONNECTABLE) &&
                                !btd_ctx.le_connecting && !btd_ctx.le_connect_pending) {
                                // Save address and cancel inquiry first
                                memcpy(btd_ctx.le_pending_addr, entry->addr, 6);
                                btd_ctx.le_pending_addr_type = entry->addr_type;
                                btd_ctx.le_connect_pending = true;
                                printf("[BTD] Canceling inquiry to connect to Xbox...\n");
                                // Cancel classic inquiry first - then scan will be stopped in callback
                                btd_hci_inquiry_cancel();
                            }
                        }

                        // Check if connectable
                        if (entry->event_type & HCI_LE_EXT_ADV_EVT_CONNECTABLE) {
                            printf("[BTD]   -> Connectable BLE device!\n");
                        }

                        // Notify higher layer of advertisement
                        btd_on_le_adv_report(entry->addr, entry->addr_type, entry->rssi,
                                             adv_data, entry->data_length);

                        // Move to next report
                        ptr += sizeof(hci_le_ext_adv_report_entry_t) + entry->data_length;
                    }
                    break;
                }

                default:
                    printf("[BTD] Unhandled LE sub-event: 0x%02X\n", le_meta->subevent);
                    break;
            }
            break;
        }

        default:
            if (evt->event_code != 0x00) {  // Ignore empty/invalid events
                printf("[BTD] Unhandled event: 0x%02X\n", evt->event_code);
            }
            break;
    }
}

// ============================================================================
// CONNECTION MANAGEMENT HELPERS
// ============================================================================

static btd_connection_t* btd_find_connection_by_handle(uint16_t handle)
{
    for (int i = 0; i < BTD_MAX_CONNECTIONS; i++) {
        if (btd_ctx.connections[i].handle == handle) {
            return &btd_ctx.connections[i];
        }
    }
    return NULL;
}

static btd_connection_t* btd_find_connection_by_bdaddr(const uint8_t* bd_addr)
{
    for (int i = 0; i < BTD_MAX_CONNECTIONS; i++) {
        if (memcmp(btd_ctx.connections[i].bd_addr, bd_addr, 6) == 0) {
            return &btd_ctx.connections[i];
        }
    }
    return NULL;
}

static btd_connection_t* btd_alloc_connection(void)
{
    for (int i = 0; i < BTD_MAX_CONNECTIONS; i++) {
        if (btd_ctx.connections[i].state == BTD_CONN_DISCONNECTED) {
            return &btd_ctx.connections[i];
        }
    }
    return NULL;
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool btd_is_ready(void)
{
    return btd_ctx.dongle_connected && btd_ctx.state == BTD_STATE_RUNNING;
}

uint8_t btd_get_connection_count(void)
{
    return btd_ctx.num_connections;
}

const btd_connection_t* btd_get_connection(uint8_t index)
{
    if (index >= BTD_MAX_CONNECTIONS) {
        return NULL;
    }
    return &btd_ctx.connections[index];
}

// Mutable accessor for internal use (btd_glue.c)
btd_connection_t* btd_get_connection_mutable(uint8_t index)
{
    if (index >= BTD_MAX_CONNECTIONS) {
        return NULL;
    }
    return &btd_ctx.connections[index];
}

void btd_set_pairing_mode(bool enable)
{
    btd_ctx.pairing_mode = enable;
    // Update scan mode
    if (btd_ctx.state == BTD_STATE_RUNNING) {
        btd_hci_write_scan_enable(enable ? HCI_SCAN_INQUIRY_AND_PAGE : HCI_SCAN_PAGE_ONLY);
    }
}

bool btd_is_pairing_mode(void)
{
    return btd_ctx.pairing_mode;
}

void btd_disconnect(uint8_t index)
{
    if (index >= BTD_MAX_CONNECTIONS) return;

    btd_connection_t* conn = &btd_ctx.connections[index];
    if (conn->state != BTD_CONN_DISCONNECTED && conn->handle != 0xFFFF) {
        btd_hci_disconnect(conn->handle, HCI_DISCONNECT_LOCAL_HOST);
    }
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void btd_bd_addr_to_str(const uint8_t* bd_addr, char* str)
{
    sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
            bd_addr[5], bd_addr[4], bd_addr[3],
            bd_addr[2], bd_addr[1], bd_addr[0]);
}

void btd_print_state(void)
{
    const char* state_names[] = {
        "INIT", "RESET", "READ_BD_ADDR", "READ_VERSION", "READ_BUFFER_SIZE",
        "SET_EVENT_MASK", "WRITE_NAME", "WRITE_COD", "WRITE_SSP", "WRITE_SCAN",
        "LE_WRITE_HOST_SUPPORT", "LE_READ_BUFFER_SIZE", "LE_SET_EVENT_MASK",
        "LE_SET_SCAN_PARAMS", "LE_SCAN_ENABLE",
        "INQUIRY", "RUNNING", "ERROR"
    };

    printf("[BTD] State: %s\n", state_names[btd_ctx.state]);
    printf("[BTD] Dongle connected: %d\n", btd_ctx.dongle_connected);
    printf("[BTD] Connections: %d\n", btd_ctx.num_connections);
}

const uint8_t* btd_get_local_bd_addr(void)
{
    // First try live address if dongle is connected
    if (btd_ctx.dongle_connected && btd_ctx.state >= BTD_STATE_INQUIRY) {
        // Check if BD_ADDR is valid (not all zeros)
        bool valid = false;
        for (int i = 0; i < 6; i++) {
            if (btd_ctx.bd_addr[i] != 0) {
                valid = true;
                break;
            }
        }
        if (valid) {
            return btd_ctx.bd_addr;
        }
    }

    // Fall back to cached address from flash
    // This allows DS3 pairing even when dongle isn't currently connected
    return btd_linkkey_get_local_addr();
}

bool btd_is_available(void)
{
    // Live dongle connected
    if (btd_ctx.dongle_connected && btd_ctx.state >= BTD_STATE_INQUIRY) {
        return true;
    }
    // Or we have a cached address from a previously-connected dongle
    return btd_linkkey_get_local_addr() != NULL;
}

// ============================================================================
// TINYUSB CLASS DRIVER IMPLEMENTATION
// ============================================================================

bool btd_driver_init(void)
{
    btd_init();
    return true;
}

bool btd_driver_deinit(void)
{
    return true;
}

bool btd_driver_open(uint8_t rhport, uint8_t dev_addr,
                     tusb_desc_interface_t const* desc_itf, uint16_t max_len)
{
    (void)rhport;

    const tusb_desc_interface_t* itf = desc_itf;

    // Check for Bluetooth class (0xE0/01/01)
    if (itf->bInterfaceClass != USB_CLASS_WIRELESS_CTRL ||
        itf->bInterfaceSubClass != USB_SUBCLASS_RF ||
        itf->bInterfaceProtocol != USB_PROTOCOL_BLUETOOTH) {
        return false;
    }

    // Only claim Interface 0 (HCI). Skip other BT interfaces (SCO, etc.)
    if (itf->bInterfaceNumber != 0) {
        return false;
    }

    // Only handle one dongle at a time
    if (btd_ctx.dongle_connected) {
        return false;
    }

    printf("[BTD] Bluetooth dongle detected at dev_addr=%d\n", dev_addr);

    btd_ctx.dev_addr = dev_addr;
    btd_ctx.itf_num = itf->bInterfaceNumber;

    // Parse endpoint descriptors
    uint16_t drv_len = sizeof(tusb_desc_interface_t);
    const uint8_t* p_desc = (const uint8_t*)desc_itf + drv_len;

    while (drv_len < max_len) {
        if (p_desc[1] == TUSB_DESC_ENDPOINT) {
            const tusb_desc_endpoint_t* ep = (const tusb_desc_endpoint_t*)p_desc;

            if (ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT &&
                tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) {
                // HCI Event endpoint (Interrupt IN)
                btd_ctx.ep_evt = ep->bEndpointAddress;
                printf("[BTD] Event EP: 0x%02X\n", btd_ctx.ep_evt);
            }
            else if (ep->bmAttributes.xfer == TUSB_XFER_BULK) {
                if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) {
                    // ACL IN endpoint
                    btd_ctx.ep_acl_in = ep->bEndpointAddress;
                    printf("[BTD] ACL IN EP: 0x%02X\n", btd_ctx.ep_acl_in);
                } else {
                    // ACL OUT endpoint
                    btd_ctx.ep_acl_out = ep->bEndpointAddress;
                    printf("[BTD] ACL OUT EP: 0x%02X\n", btd_ctx.ep_acl_out);
                }
            }

            // Open the endpoint
            if (!tuh_edpt_open(dev_addr, ep)) {
                printf("[BTD] Failed to open endpoint 0x%02X\n", ep->bEndpointAddress);
            }
        }

        drv_len += p_desc[0];
        p_desc += p_desc[0];
    }

    return true;
}

bool btd_driver_set_config(uint8_t dev_addr, uint8_t itf_num)
{
    (void)itf_num;

    printf("[BTD] Configuration set for dev_addr=%d\n", dev_addr);

    // Reset state machine state
    btd_ctx.dongle_connected = true;
    btd_ctx.state = BTD_STATE_INIT;
    btd_ctx.pending_cmd = 0;  // Ensure clean state
    btd_ctx.le_connecting = false;  // Reset BLE connection state
    printf("[BTD] Reset le_connecting to %d\n", btd_ctx.le_connecting);

    // Tell USBH we're done with enumeration FIRST
    usbh_driver_set_config_complete(dev_addr, btd_ctx.itf_num);

    // Start receiving HCI events (ACL will be started after init completes)
    btd_ctx.evt_pending = true;
    usbh_edpt_xfer(dev_addr, btd_ctx.ep_evt, btd_ctx.evt_buf, sizeof(btd_ctx.evt_buf));

    return true;
}

bool btd_driver_xfer_cb(uint8_t dev_addr, uint8_t ep_addr,
                        xfer_result_t result, uint32_t xferred_bytes)
{
    (void)dev_addr;

    if (result != XFER_RESULT_SUCCESS) {
        printf("[BTD] Transfer failed on EP 0x%02X: result=%d\n", ep_addr, result);
        if (ep_addr == btd_ctx.ep_evt) {
            btd_ctx.evt_pending = false;  // Allow retry
        }
        return false;
    }

    if (ep_addr == btd_ctx.ep_evt) {
        // HCI Event received
        btd_ctx.evt_pending = false;  // Transfer completed, will be re-queued by btd_task

        // Debug: show raw event data when in running state
        if (btd_ctx.state == BTD_STATE_RUNNING && xferred_bytes > 0) {
            printf("[BTD] Event: code=0x%02X len=%lu\n", btd_ctx.evt_buf[0], (unsigned long)xferred_bytes);
        }

        // Only process if we got actual data with a valid event code
        if (xferred_bytes >= 2 && btd_ctx.evt_buf[0] != 0x00) {
            btd_process_event(btd_ctx.evt_buf, xferred_bytes);
            // pending_cmd is cleared inside btd_process_event for Command Complete/Status
        }
        // If empty or invalid, don't clear pending_cmd - wait for real response
    }
    else if (ep_addr == btd_ctx.ep_acl_in) {
        // ACL data received
        if (xferred_bytes > 4) {
            // Parse ACL header
            uint16_t hdr = btd_ctx.acl_in_buf[0] | (btd_ctx.acl_in_buf[1] << 8);
            uint16_t handle = hdr & 0x0FFF;
            uint16_t data_len = btd_ctx.acl_in_buf[2] | (btd_ctx.acl_in_buf[3] << 8);

            btd_connection_t* conn = btd_find_connection_by_handle(handle);
            if (conn) {
                uint8_t idx = conn - btd_ctx.connections;
                btd_on_acl_data(idx, &btd_ctx.acl_in_buf[4], data_len);
            }
        }
        // Continue receiving ACL data
        usbh_edpt_xfer(dev_addr, btd_ctx.ep_acl_in, btd_ctx.acl_in_buf, sizeof(btd_ctx.acl_in_buf));
    }

    return true;
}

void btd_driver_close(uint8_t dev_addr)
{
    printf("[BTD] Dongle disconnected (dev_addr=%d)\n", dev_addr);

    btd_ctx.dongle_connected = false;
    btd_ctx.state = BTD_STATE_INIT;
    btd_ctx.pending_cmd = 0;
    btd_ctx.num_connections = 0;
    init_delay_counter = 0;  // Reset for next connection

    // Mark all connections as disconnected
    for (int i = 0; i < BTD_MAX_CONNECTIONS; i++) {
        if (btd_ctx.connections[i].state != BTD_CONN_DISCONNECTED) {
            btd_ctx.connections[i].state = BTD_CONN_DISCONNECTED;
            btd_on_disconnection(i);
        }
    }
}

// ============================================================================
// TINYUSB CLASS DRIVER STRUCTURE
// ============================================================================

const usbh_class_driver_t usbh_btd_driver = {
    .name = "BTD",
    .init = btd_driver_init,
    .deinit = btd_driver_deinit,
    .open = btd_driver_open,
    .set_config = btd_driver_set_config,
    .xfer_cb = btd_driver_xfer_cb,
    .close = btd_driver_close,
};

// ============================================================================
// WEAK CALLBACK IMPLEMENTATIONS (Override in higher layers)
// ============================================================================

__attribute__((weak)) void btd_on_connection(uint8_t conn_index)
{
    printf("[BTD] Connection %d established (weak handler)\n", conn_index);
}

__attribute__((weak)) void btd_on_disconnection(uint8_t conn_index)
{
    printf("[BTD] Connection %d lost (weak handler)\n", conn_index);
}

__attribute__((weak)) void btd_on_acl_data(uint8_t conn_index, const uint8_t* data, uint16_t len)
{
    printf("[BTD] ACL data on connection %d: %d bytes (weak handler)\n", conn_index, len);
}

__attribute__((weak)) void btd_on_remote_name_complete(uint8_t conn_index, const char* name)
{
    printf("[BTD] Remote name complete for connection %d: %s (weak handler)\n", conn_index, name);
}

__attribute__((weak)) void btd_on_le_adv_report(const uint8_t* addr, uint8_t addr_type,
                                                  int8_t rssi, const uint8_t* data, uint8_t data_len)
{
    (void)addr; (void)addr_type; (void)rssi; (void)data; (void)data_len;
    // Advertising reports are high-frequency, don't log by default in weak handler
}

__attribute__((weak)) void btd_on_le_connection(uint8_t conn_index)
{
    printf("[BTD] LE Connection %d established (weak handler)\n", conn_index);
}
