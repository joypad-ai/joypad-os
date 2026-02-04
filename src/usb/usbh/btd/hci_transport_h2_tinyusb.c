// hci_transport_h2_tinyusb.c - BTstack HCI USB Transport for TinyUSB
//
// This implements the BTstack hci_transport_t interface using TinyUSB's
// USB host stack. Allows BTstack to communicate with USB Bluetooth dongles
// on RP2040 and other TinyUSB-supported platforms.
//
// USB Bluetooth HCI Transport (H2):
// - Control endpoint (EP0): HCI Commands (host->controller)
// - Interrupt IN (0x81): HCI Events (controller->host)
// - Bulk IN (0x82): ACL Data (controller->host)
// - Bulk OUT (0x02): ACL Data (host->controller)
//
// Uses BTstack for full Bluetooth stack integration.

#include "hci_transport_h2_tinyusb.h"
#include "tusb.h"
#include "host/usbh_pvt.h"

#include <string.h>
#include <stdio.h>

// BTstack is always enabled
#define USE_BTSTACK 1

// BTstack includes
#include "btstack_config.h"
#include "bluetooth.h"
#include "btstack_defines.h"
#include "hci_transport.h"
#include "btstack_run_loop.h"
#include "btstack_run_loop_embedded.h"

// ============================================================================
// TRANSPORT STATE
// ============================================================================

typedef struct {
    // USB device info
    uint8_t  dev_addr;          // TinyUSB device address
    uint8_t  itf_num;           // Interface number
    uint8_t  ep_evt_in;         // Interrupt IN endpoint (HCI events)
    uint8_t  ep_acl_in;         // Bulk IN endpoint (ACL data)
    uint8_t  ep_acl_out;        // Bulk OUT endpoint (ACL data)

    // Buffers
    uint8_t  cmd_buf[HCI_USB_CMD_BUF_SIZE];
    uint8_t  evt_buf[HCI_USB_EVT_BUF_SIZE];
    uint8_t  acl_in_buf[HCI_USB_ACL_BUF_SIZE];
    uint8_t  acl_out_buf[HCI_USB_ACL_BUF_SIZE];

    // State flags
    bool     connected;         // Dongle connected and configured
    bool     opened;            // Transport opened
    bool     evt_pending;       // Event endpoint transfer pending
    bool     acl_in_pending;    // ACL IN transfer pending
    bool     cmd_pending;       // Command transfer pending
    bool     acl_out_pending;   // ACL OUT transfer pending

    // Received packet info (for deferred processing)
    bool     evt_ready;         // Event packet ready for processing
    uint16_t evt_len;           // Event packet length
    bool     acl_ready;         // ACL packet ready for processing
    uint16_t acl_len;           // ACL packet length

    // BTstack packet handler
    void (*packet_handler)(uint8_t packet_type, uint8_t *packet, uint16_t size);

} hci_usb_state_t;

static hci_usb_state_t usb_state;

#if USE_BTSTACK
// Data source for BTstack run loop integration
static btstack_data_source_t transport_data_source;
#endif

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void hci_transport_h2_init(const void* transport_config);
static int  hci_transport_h2_open(void);
static int  hci_transport_h2_close(void);
static void hci_transport_h2_register_packet_handler(
    void (*handler)(uint8_t packet_type, uint8_t *packet, uint16_t size));
static int  hci_transport_h2_can_send_packet_now(uint8_t packet_type);
static int  hci_transport_h2_send_packet(uint8_t packet_type, uint8_t *packet, int size);

#if USE_BTSTACK
static void hci_transport_h2_process_data_source(btstack_data_source_t *ds, btstack_data_source_callback_type_t callback_type);
#endif

static void usb_submit_event_transfer(void);
static void usb_submit_acl_in_transfer(void);

// ============================================================================
// BTSTACK TRANSPORT INTERFACE
// ============================================================================

#if USE_BTSTACK
static const hci_transport_t hci_transport_h2_tinyusb = {
    .name                    = "H2_TINYUSB",
    .init                    = hci_transport_h2_init,
    .open                    = hci_transport_h2_open,
    .close                   = hci_transport_h2_close,
    .register_packet_handler = hci_transport_h2_register_packet_handler,
    .can_send_packet_now     = hci_transport_h2_can_send_packet_now,
    .send_packet             = hci_transport_h2_send_packet,
    .set_baudrate            = NULL,  // Not applicable for USB
    .reset_link              = NULL,  // Not applicable for USB
    .set_sco_config          = NULL,  // SCO not implemented yet
};

const hci_transport_t* hci_transport_h2_tinyusb_instance(void)
{
    return &hci_transport_h2_tinyusb;
}
#endif

// ============================================================================
// TRANSPORT IMPLEMENTATION
// ============================================================================

static void hci_transport_h2_init(const void* transport_config)
{
    (void)transport_config;
    printf("[HCI_USB] >>> hci_transport_h2_init called\n");

    // Preserve USB connection info AND packet handler if already set
    // (BTstack calls register_packet_handler from hci_init, then init/open from power_on)
    uint8_t saved_dev_addr = usb_state.dev_addr;
    uint8_t saved_itf_num = usb_state.itf_num;
    uint8_t saved_ep_evt_in = usb_state.ep_evt_in;
    uint8_t saved_ep_acl_in = usb_state.ep_acl_in;
    uint8_t saved_ep_acl_out = usb_state.ep_acl_out;
    bool was_connected = usb_state.connected;
    void (*saved_packet_handler)(uint8_t, uint8_t*, uint16_t) = usb_state.packet_handler;

    memset(&usb_state, 0, sizeof(usb_state));

    // Restore preserved state
    usb_state.dev_addr = saved_dev_addr;
    usb_state.itf_num = saved_itf_num;
    usb_state.ep_evt_in = saved_ep_evt_in;
    usb_state.ep_acl_in = saved_ep_acl_in;
    usb_state.ep_acl_out = saved_ep_acl_out;
    usb_state.connected = was_connected;
    usb_state.packet_handler = saved_packet_handler;

    printf("[HCI_USB] Transport initialized (connected=%d, handler=%p)\n",
           was_connected, (void*)saved_packet_handler);
}

static int hci_transport_h2_open(void)
{
    printf("[HCI_USB] >>> hci_transport_h2_open called\n");
    if (!usb_state.connected) {
        printf("[HCI_USB] Cannot open - no dongle connected\n");
        return -1;
    }

    if (usb_state.opened) {
        printf("[HCI_USB] Already opened\n");
        return 0;
    }

#if USE_BTSTACK
    // Register data source with BTstack run loop for polling
    btstack_run_loop_set_data_source_handler(&transport_data_source,
                                              hci_transport_h2_process_data_source);
    btstack_run_loop_enable_data_source_callbacks(&transport_data_source,
                                                   DATA_SOURCE_CALLBACK_POLL);
    btstack_run_loop_add_data_source(&transport_data_source);
#endif

    // Start receiving events and ACL data
    usb_submit_event_transfer();
    usb_submit_acl_in_transfer();

    usb_state.opened = true;
    printf("[HCI_USB] Transport opened\n");

    return 0;
}

static int hci_transport_h2_close(void)
{
    if (!usb_state.opened) {
        return 0;
    }

#if USE_BTSTACK
    btstack_run_loop_remove_data_source(&transport_data_source);
#endif

    usb_state.opened = false;
    printf("[HCI_USB] Transport closed\n");

    return 0;
}

static void hci_transport_h2_register_packet_handler(
    void (*handler)(uint8_t packet_type, uint8_t *packet, uint16_t size))
{
    printf("[HCI_USB] register_packet_handler called: %p\n", (void*)handler);
    usb_state.packet_handler = handler;
}

static int hci_transport_h2_can_send_packet_now(uint8_t packet_type)
{
    if (!usb_state.connected || !usb_state.opened) {
        return 0;
    }

    switch (packet_type) {
        case HCI_COMMAND_DATA_PACKET:
            return !usb_state.cmd_pending;
        case HCI_ACL_DATA_PACKET:
            return !usb_state.acl_out_pending;
        default:
            return 0;
    }
}

// Callback when HCI command control transfer completes
static void hci_cmd_complete_cb(tuh_xfer_t* xfer)
{
    // printf("[HCI_USB] cmd_complete_cb result=%d\n", xfer->result);
    usb_state.cmd_pending = false;
    if (xfer->result != XFER_RESULT_SUCCESS) {
        printf("[HCI_USB] Command control xfer failed: %d\n", xfer->result);
        return;
    }
#if USE_BTSTACK
    // Emit packet sent event so BTstack releases buffer and can send next
    if (usb_state.packet_handler) {
        static const uint8_t packet_sent_event[] = { HCI_EVENT_TRANSPORT_PACKET_SENT, 0 };
        usb_state.packet_handler(HCI_EVENT_PACKET, (uint8_t*)packet_sent_event, sizeof(packet_sent_event));
    }
#endif
}

static int hci_transport_h2_send_packet(uint8_t packet_type, uint8_t *packet, int size)
{
    // printf("[HCI_USB] send_packet type=%d size=%d connected=%d opened=%d\n",
    //        packet_type, size, usb_state.connected, usb_state.opened);

    if (!usb_state.connected || !usb_state.opened) {
        printf("[HCI_USB] Send failed - not connected/opened\n");
        return -1;
    }

    switch (packet_type) {
        case HCI_COMMAND_DATA_PACKET: {
            if (usb_state.cmd_pending) {
                printf("[HCI_USB] Command send failed - busy\n");
                return -1;
            }
            if (size > HCI_USB_CMD_BUF_SIZE) {
                printf("[HCI_USB] Command too large: %d\n", size);
                return -1;
            }
            // printf("[HCI_USB] Sending HCI command: %02X %02X\n", packet[0], packet[1]);

            // Copy to buffer (must persist until transfer completes)
            memcpy(usb_state.cmd_buf, packet, size);

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
                .wIndex = usb_state.itf_num,
                .wLength = (uint16_t)size
            };

            tuh_xfer_t xfer = {
                .daddr = usb_state.dev_addr,
                .ep_addr = 0,
                .setup = &request,
                .buffer = usb_state.cmd_buf,
                .complete_cb = hci_cmd_complete_cb,
                .user_data = 0
            };

            usb_state.cmd_pending = true;

            if (!tuh_control_xfer(&xfer)) {
                printf("[HCI_USB] Failed to send command\n");
                usb_state.cmd_pending = false;
                return -1;
            }

            return 0;
        }

        case HCI_ACL_DATA_PACKET: {
            if (usb_state.acl_out_pending) {
                printf("[HCI_USB] ACL send failed - busy\n");
                return -1;
            }
            if (size > HCI_USB_ACL_BUF_SIZE) {
                printf("[HCI_USB] ACL packet too large: %d\n", size);
                return -1;
            }

            // Copy to buffer
            memcpy(usb_state.acl_out_buf, packet, size);

            // Send via bulk OUT endpoint
            usb_state.acl_out_pending = true;

            if (!usbh_edpt_xfer(usb_state.dev_addr, usb_state.ep_acl_out,
                                usb_state.acl_out_buf, (uint16_t)size)) {
                printf("[HCI_USB] Failed to send ACL data\n");
                usb_state.acl_out_pending = false;
                return -1;
            }

            return 0;
        }

        default:
            printf("[HCI_USB] Unknown packet type: %d\n", packet_type);
            return -1;
    }
}

// ============================================================================
// RUN LOOP INTEGRATION
// ============================================================================

#if USE_BTSTACK
static void hci_transport_h2_process_data_source(btstack_data_source_t *ds,
                                                  btstack_data_source_callback_type_t callback_type)
{
    (void)ds;
    (void)callback_type;

    // Process any ready packets
    hci_transport_h2_tinyusb_process();
}
#endif

void hci_transport_h2_tinyusb_process(void)
{
    // Deliver any received event packets
    if (usb_state.evt_ready && usb_state.packet_handler) {
        usb_state.evt_ready = false;
        // printf("[HCI_USB] Delivering event: %d bytes [%02X %02X %02X %02X %02X %02X]\n",
        //        usb_state.evt_len,
        //        usb_state.evt_buf[0], usb_state.evt_buf[1], usb_state.evt_buf[2],
        //        usb_state.evt_buf[3], usb_state.evt_buf[4], usb_state.evt_buf[5]);
        usb_state.packet_handler(HCI_EVENT_PACKET, usb_state.evt_buf, usb_state.evt_len);
        // printf("[HCI_USB] Event delivered OK\n");

        // Submit next event transfer
        usb_submit_event_transfer();

#if USE_BTSTACK
        // After delivering event, trigger BTstack to process (may want to send next command)
        btstack_run_loop_embedded_execute_once();
#endif
    }

    // Deliver any received ACL packets
    if (usb_state.acl_ready && usb_state.packet_handler) {
        usb_state.acl_ready = false;

        // Debug: Show ACL packet with L2CAP/ATT header
        if (usb_state.acl_len >= 9) {
            // Debug disabled - too much output causes buffer issues
            // uint16_t handle = usb_state.acl_in_buf[0] | ((usb_state.acl_in_buf[1] & 0x0F) << 8);
            // uint16_t l2cap_len = usb_state.acl_in_buf[4] | (usb_state.acl_in_buf[5] << 8);
            // uint16_t l2cap_cid = usb_state.acl_in_buf[6] | (usb_state.acl_in_buf[7] << 8);
            // uint8_t att_opcode = usb_state.acl_in_buf[8];
            // printf("[HCI_USB] ACL h=0x%04X L2CAP len=%d cid=0x%04X ATT=0x%02X\n",
            //        handle, l2cap_len, l2cap_cid, att_opcode);
        }

        usb_state.packet_handler(HCI_ACL_DATA_PACKET, usb_state.acl_in_buf, usb_state.acl_len);

        // Submit next ACL transfer
        usb_submit_acl_in_transfer();

#if USE_BTSTACK
        // After delivering ACL, trigger BTstack to process (GATT notifications, etc.)
        btstack_run_loop_embedded_execute_once();
#endif
    }

#if !USE_BTSTACK
    // Process queued test commands (standalone mode)
    extern bool test_cmd_pending;
    extern void test_send_next_command(void);
    if (test_cmd_pending && !usb_state.cmd_pending) {
        test_cmd_pending = false;
        test_send_next_command();
    }
#endif
}

bool hci_transport_h2_tinyusb_is_connected(void)
{
    return usb_state.connected;
}

// ============================================================================
// STANDALONE TEST MODE
// ============================================================================

#if !USE_BTSTACK

// Forward declaration
static void test_packet_handler(uint8_t packet_type, uint8_t *packet, uint16_t size);

// Initialize standalone test mode
void hci_transport_h2_tinyusb_test_init(void)
{
    printf("[HCI_USB] Test mode initialized\n");
    usb_state.packet_handler = test_packet_handler;
}

// Send HCI Reset command (test)
void hci_transport_h2_tinyusb_test_reset(void)
{
    if (!usb_state.connected) {
        printf("[HCI_USB] Cannot send reset - not connected\n");
        return;
    }

    // HCI_Reset command: opcode 0x0C03, no parameters
    uint8_t cmd[] = { 0x03, 0x0C, 0x00 };  // opcode LE, param_len

    printf("[HCI_USB] Sending HCI_Reset...\n");
    hci_transport_h2_send_packet(HCI_COMMAND_DATA_PACKET, cmd, sizeof(cmd));
}

// Test state machine
static enum {
    TEST_STATE_IDLE,
    TEST_STATE_RESET_SENT,
    TEST_STATE_READ_BD_ADDR_SENT,
    TEST_STATE_READ_VERSION_SENT,
    TEST_STATE_READ_BUFFER_SIZE_SENT,
    TEST_STATE_SET_EVENT_MASK_SENT,
    TEST_STATE_LE_SET_EVENT_MASK_SENT,
    TEST_STATE_LE_SET_SCAN_PARAMS_SENT,
    TEST_STATE_LE_SCAN_ENABLED,
    TEST_STATE_DONE
} test_state = TEST_STATE_IDLE;

// Pending command to send (queued for next process cycle)
bool test_cmd_pending = false;

// Send next test command based on state (called from process loop)
void test_send_next_command(void)
{
    switch (test_state) {
        case TEST_STATE_RESET_SENT: {
            // Send Read_BD_ADDR: opcode 0x1009
            uint8_t cmd[] = { 0x09, 0x10, 0x00 };
            printf("[HCI_USB] Sending Read_BD_ADDR...\n");
            hci_transport_h2_send_packet(HCI_COMMAND_DATA_PACKET, cmd, sizeof(cmd));
            test_state = TEST_STATE_READ_BD_ADDR_SENT;
            break;
        }
        case TEST_STATE_READ_BD_ADDR_SENT: {
            // Send Read_Local_Version_Information: opcode 0x1001
            uint8_t cmd[] = { 0x01, 0x10, 0x00 };
            printf("[HCI_USB] Sending Read_Local_Version...\n");
            hci_transport_h2_send_packet(HCI_COMMAND_DATA_PACKET, cmd, sizeof(cmd));
            test_state = TEST_STATE_READ_VERSION_SENT;
            break;
        }
        case TEST_STATE_READ_VERSION_SENT: {
            // Send Read_Buffer_Size: opcode 0x1005
            uint8_t cmd[] = { 0x05, 0x10, 0x00 };
            printf("[HCI_USB] Sending Read_Buffer_Size...\n");
            hci_transport_h2_send_packet(HCI_COMMAND_DATA_PACKET, cmd, sizeof(cmd));
            test_state = TEST_STATE_READ_BUFFER_SIZE_SENT;
            break;
        }
        case TEST_STATE_READ_BUFFER_SIZE_SENT: {
            // Set Event Mask: opcode 0x0C01
            // Enable LE Meta Event (bit 61) = byte[7] = 0x20
            uint8_t cmd[] = { 0x01, 0x0C, 0x08,
                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3F };
            printf("[HCI_USB] Sending Set_Event_Mask (enable LE)...\n");
            hci_transport_h2_send_packet(HCI_COMMAND_DATA_PACKET, cmd, sizeof(cmd));
            test_state = TEST_STATE_SET_EVENT_MASK_SENT;
            break;
        }
        case TEST_STATE_SET_EVENT_MASK_SENT: {
            // LE Set Event Mask: opcode 0x2001
            // Enable all LE events
            uint8_t cmd[] = { 0x01, 0x20, 0x08,
                0xFF, 0xFF, 0xFF, 0x1F, 0x00, 0x00, 0x00, 0x00 };
            printf("[HCI_USB] Sending LE_Set_Event_Mask...\n");
            hci_transport_h2_send_packet(HCI_COMMAND_DATA_PACKET, cmd, sizeof(cmd));
            test_state = TEST_STATE_LE_SET_EVENT_MASK_SENT;
            break;
        }
        case TEST_STATE_LE_SET_EVENT_MASK_SENT: {
            // LE Set Scan Parameters: opcode 0x200B
            // Active scan, 100ms interval, 50ms window, public addr, no filter
            uint8_t cmd[] = { 0x0B, 0x20, 0x07,
                0x01,           // Active scan
                0xA0, 0x00,     // Interval: 160 * 0.625ms = 100ms
                0x50, 0x00,     // Window: 80 * 0.625ms = 50ms
                0x00,           // Own addr type: public
                0x00 };         // Filter: accept all
            printf("[HCI_USB] Sending LE_Set_Scan_Parameters...\n");
            hci_transport_h2_send_packet(HCI_COMMAND_DATA_PACKET, cmd, sizeof(cmd));
            test_state = TEST_STATE_LE_SET_SCAN_PARAMS_SENT;
            break;
        }
        case TEST_STATE_LE_SET_SCAN_PARAMS_SENT: {
            // LE Set Scan Enable: opcode 0x200C
            uint8_t cmd[] = { 0x0C, 0x20, 0x02,
                0x01,           // Enable
                0x00 };         // No duplicate filter
            printf("[HCI_USB] Sending LE_Set_Scan_Enable...\n");
            hci_transport_h2_send_packet(HCI_COMMAND_DATA_PACKET, cmd, sizeof(cmd));
            test_state = TEST_STATE_LE_SCAN_ENABLED;
            break;
        }
        case TEST_STATE_LE_SCAN_ENABLED:
            printf("[HCI_USB] === BLE scanning active - waiting for advertisements ===\n");
            test_state = TEST_STATE_DONE;
            break;
        default:
            break;
    }
}

// Enhanced test packet handler - parses responses and chains commands
static void test_packet_handler(uint8_t packet_type, uint8_t *packet, uint16_t size)
{
    if (packet_type == HCI_EVENT_PACKET && size >= 2) {
        uint8_t event_code = packet[0];
        uint8_t param_len = packet[1];

        // Command Complete (0x0E)
        if (event_code == 0x0E && size >= 6) {
            uint16_t opcode = packet[3] | (packet[4] << 8);
            uint8_t status = packet[5];

            // Clear cmd_pending - command has completed
            usb_state.cmd_pending = false;

            printf("[HCI_USB] Command Complete: opcode=0x%04X status=%d\n", opcode, status);

            if (status != 0) {
                printf("[HCI_USB] !!! Command failed with status %d !!!\n", status);
                return;
            }

            // Parse specific responses and queue next command
            switch (opcode) {
                case 0x0C03:  // HCI_Reset
                    printf("[HCI_USB]   Reset OK\n");
                    test_state = TEST_STATE_RESET_SENT;
                    test_cmd_pending = true;  // Queue next command
                    break;

                case 0x1009:  // Read_BD_ADDR
                    if (size >= 12) {
                        printf("[HCI_USB]   BD_ADDR: %02X:%02X:%02X:%02X:%02X:%02X\n",
                               packet[11], packet[10], packet[9],
                               packet[8], packet[7], packet[6]);
                    }
                    test_cmd_pending = true;
                    break;

                case 0x1001:  // Read_Local_Version_Information
                    if (size >= 14) {
                        uint8_t hci_ver = packet[6];
                        uint16_t hci_rev = packet[7] | (packet[8] << 8);
                        uint8_t lmp_ver = packet[9];
                        uint16_t manufacturer = packet[10] | (packet[11] << 8);
                        uint16_t lmp_subver = packet[12] | (packet[13] << 8);

                        printf("[HCI_USB]   HCI Version: %d.%d\n", hci_ver, hci_rev);
                        printf("[HCI_USB]   LMP Version: %d.%d\n", lmp_ver, lmp_subver);
                        printf("[HCI_USB]   Manufacturer: 0x%04X", manufacturer);

                        // Common manufacturer IDs
                        switch (manufacturer) {
                            case 0x000A: printf(" (CSR)\n"); break;
                            case 0x000D: printf(" (TI)\n"); break;
                            case 0x000F: printf(" (Broadcom)\n"); break;
                            case 0x001D: printf(" (Qualcomm)\n"); break;
                            case 0x0046: printf(" (MediaTek)\n"); break;
                            case 0x005D: printf(" (Realtek)\n"); break;
                            case 0x0002: printf(" (Intel)\n"); break;
                            default: printf("\n"); break;
                        }
                    }
                    test_cmd_pending = true;
                    break;

                case 0x1005:  // Read_Buffer_Size
                    if (size >= 14) {
                        uint16_t acl_mtu = packet[6] | (packet[7] << 8);
                        uint8_t sco_mtu = packet[8];
                        uint16_t acl_pkts = packet[9] | (packet[10] << 8);
                        uint16_t sco_pkts = packet[11] | (packet[12] << 8);

                        printf("[HCI_USB]   ACL MTU: %d, Packets: %d\n", acl_mtu, acl_pkts);
                        printf("[HCI_USB]   SCO MTU: %d, Packets: %d\n", sco_mtu, sco_pkts);
                    }
                    test_cmd_pending = true;
                    break;

                case 0x0C01:  // Set_Event_Mask
                    printf("[HCI_USB]   Event Mask set OK\n");
                    test_cmd_pending = true;
                    break;

                case 0x2001:  // LE_Set_Event_Mask
                    printf("[HCI_USB]   LE Event Mask set OK\n");
                    test_cmd_pending = true;
                    break;

                case 0x200B:  // LE_Set_Scan_Parameters
                    printf("[HCI_USB]   LE Scan Parameters set OK\n");
                    test_cmd_pending = true;
                    break;

                case 0x200C:  // LE_Set_Scan_Enable
                    printf("[HCI_USB]   LE Scan Enable OK\n");
                    test_cmd_pending = true;
                    break;

                default:
                    printf("[HCI_USB]   (unhandled opcode 0x%04X)\n", opcode);
                    break;
            }
        }
        // Command Status (0x0F)
        else if (event_code == 0x0F && size >= 6) {
            uint8_t status = packet[2];
            uint16_t opcode = packet[4] | (packet[5] << 8);
            printf("[HCI_USB] Command Status: opcode=0x%04X status=%d\n", opcode, status);
        }
        // LE Meta Event (0x3E)
        else if (event_code == 0x3E && size >= 3) {
            uint8_t subevent = packet[2];

            if (subevent == 0x02) {  // LE Advertising Report
                // Parse advertising report
                uint8_t num_reports = packet[3];
                const uint8_t* p = &packet[4];

                for (int i = 0; i < num_reports && (p - packet) < size; i++) {
                    uint8_t event_type = *p++;
                    uint8_t addr_type = *p++;
                    const uint8_t* addr = p; p += 6;
                    uint8_t data_len = *p++;
                    const uint8_t* data = p; p += data_len;
                    int8_t rssi = (int8_t)*p++;

                    printf("[HCI_USB] LE Adv: %02X:%02X:%02X:%02X:%02X:%02X",
                           addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
                    printf(" type=%d rssi=%d len=%d", event_type, rssi, data_len);

                    // Check for device name in advertising data
                    for (int j = 0; j < data_len; ) {
                        uint8_t len = data[j];
                        if (len == 0 || j + len >= data_len) break;
                        uint8_t type = data[j + 1];
                        if (type == 0x09 || type == 0x08) {  // Complete/Short Local Name
                            printf(" name=\"");
                            for (int k = 0; k < len - 1 && k < 20; k++) {
                                char c = data[j + 2 + k];
                                if (c >= 32 && c < 127) printf("%c", c);
                            }
                            printf("\"");
                            break;
                        }
                        j += len + 1;
                    }
                    printf("\n");
                }
            }
            else if (subevent == 0x0D) {  // LE Extended Advertising Report
                printf("[HCI_USB] LE Extended Adv Report (subevent 0x0D)\n");
            }
            else {
                printf("[HCI_USB] LE Meta Event subevent=0x%02X\n", subevent);
            }
        }
        // Other events
        else {
            printf("[HCI_USB] Event 0x%02X len=%d\n", event_code, param_len);
        }
    }
    else if (packet_type == HCI_ACL_DATA_PACKET && size >= 4) {
        uint16_t handle = (packet[0] | (packet[1] << 8)) & 0x0FFF;
        uint16_t len = packet[2] | (packet[3] << 8);
        printf("[HCI_USB] ACL Data: handle=0x%04X len=%d\n", handle, len);
    }
}
#endif

// ============================================================================
// USB ENDPOINT HELPERS
// ============================================================================

static void usb_submit_event_transfer(void)
{
    // printf("[HCI_USB] submit_evt: connected=%d pending=%d ep=0x%02X\n",
    //        usb_state.connected, usb_state.evt_pending, usb_state.ep_evt_in);
    if (!usb_state.connected || usb_state.evt_pending) {
        return;
    }

    usb_state.evt_pending = true;
    memset(usb_state.evt_buf, 0, sizeof(usb_state.evt_buf));

    bool ok = usbh_edpt_xfer(usb_state.dev_addr, usb_state.ep_evt_in,
                             usb_state.evt_buf, sizeof(usb_state.evt_buf));
    // printf("[HCI_USB] Submit event xfer: %s\n", ok ? "OK" : "FAIL");
    if (!ok) {
        usb_state.evt_pending = false;
    }
}

static void usb_submit_acl_in_transfer(void)
{
    if (!usb_state.connected || usb_state.acl_in_pending) {
        return;
    }

    usb_state.acl_in_pending = true;
    memset(usb_state.acl_in_buf, 0, sizeof(usb_state.acl_in_buf));

    if (!usbh_edpt_xfer(usb_state.dev_addr, usb_state.ep_acl_in,
                        usb_state.acl_in_buf, sizeof(usb_state.acl_in_buf))) {
        usb_state.acl_in_pending = false;
        printf("[HCI_USB] Failed to submit ACL IN transfer\n");
    }
}

// ============================================================================
// TINYUSB CLASS DRIVER IMPLEMENTATION
// ============================================================================

bool btstack_driver_init(void)
{
    // Preserve packet_handler - BTstack's hci_init() may have set it before TinyUSB init
    void (*saved_handler)(uint8_t, uint8_t*, uint16_t) = usb_state.packet_handler;
    memset(&usb_state, 0, sizeof(usb_state));
    usb_state.packet_handler = saved_handler;
    printf("[HCI_USB] Driver initialized (handler=%p)\n", (void*)saved_handler);
    return true;
}

bool btstack_driver_deinit(void)
{
    usb_state.connected = false;
    usb_state.opened = false;
    return true;
}

bool btstack_driver_open(uint8_t rhport, uint8_t dev_addr,
                         tusb_desc_interface_t const* desc_itf, uint16_t max_len)
{
    (void)rhport;

    // Check if this is a Bluetooth device
    // Standard BT class: 0xE0/0x01/0x01
    // Some Broadcom dongles use vendor-specific class: 0xFF/0x01/0x01
    bool is_standard_bt = (desc_itf->bInterfaceClass == USB_CLASS_WIRELESS_CTRL &&
                           desc_itf->bInterfaceSubClass == USB_SUBCLASS_RF &&
                           desc_itf->bInterfaceProtocol == USB_PROTOCOL_BLUETOOTH);

    uint16_t vid, pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    bool is_broadcom_bt = (vid == USB_VID_BROADCOM &&
                           desc_itf->bInterfaceClass == USB_CLASS_VENDOR_SPECIFIC &&
                           desc_itf->bInterfaceSubClass == USB_SUBCLASS_RF &&
                           desc_itf->bInterfaceProtocol == USB_PROTOCOL_BLUETOOTH &&
                           desc_itf->bInterfaceNumber == 0);  // Only claim interface 0

    if (!is_standard_bt && !is_broadcom_bt) {
        return false;
    }

    // Guard against double-open (dev_addr is set on first open)
    if (usb_state.dev_addr == dev_addr && usb_state.ep_evt_in != 0) {
        printf("[HCI_USB] Dongle already opened at addr %d\n", dev_addr);
        return true;
    }

    printf("[HCI_USB] Bluetooth dongle found at addr %d\n", dev_addr);

    usb_state.dev_addr = dev_addr;
    usb_state.itf_num = desc_itf->bInterfaceNumber;

    // Parse endpoints
    const uint8_t* p_desc = (const uint8_t*)desc_itf;
    const uint8_t* desc_end = p_desc + max_len;

    p_desc = tu_desc_next(p_desc);  // Skip interface descriptor

    while (p_desc < desc_end) {
        if (tu_desc_type(p_desc) == TUSB_DESC_ENDPOINT) {
            const tusb_desc_endpoint_t* ep = (const tusb_desc_endpoint_t*)p_desc;

            if (ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT) {
                // Interrupt IN = HCI Events
                if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) {
                    usb_state.ep_evt_in = ep->bEndpointAddress;
                    printf("[HCI_USB] Event IN EP: 0x%02X\n", usb_state.ep_evt_in);

                    if (!tuh_edpt_open(dev_addr, ep)) {
                        printf("[HCI_USB] Failed to open event endpoint\n");
                        return false;
                    }
                }
            }
            else if (ep->bmAttributes.xfer == TUSB_XFER_BULK) {
                // Bulk endpoints = ACL data
                if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN) {
                    usb_state.ep_acl_in = ep->bEndpointAddress;
                    printf("[HCI_USB] ACL IN EP: 0x%02X\n", usb_state.ep_acl_in);

                    if (!tuh_edpt_open(dev_addr, ep)) {
                        printf("[HCI_USB] Failed to open ACL IN endpoint\n");
                        return false;
                    }
                } else {
                    usb_state.ep_acl_out = ep->bEndpointAddress;
                    printf("[HCI_USB] ACL OUT EP: 0x%02X\n", usb_state.ep_acl_out);

                    if (!tuh_edpt_open(dev_addr, ep)) {
                        printf("[HCI_USB] Failed to open ACL OUT endpoint\n");
                        return false;
                    }
                }
            }
        }
        else if (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE) {
            // Hit next interface, stop parsing
            break;
        }

        p_desc = tu_desc_next(p_desc);
    }

    // Verify we found all required endpoints
    if (!usb_state.ep_evt_in || !usb_state.ep_acl_in || !usb_state.ep_acl_out) {
        printf("[HCI_USB] Missing required endpoints\n");
        return false;
    }

    return true;
}

bool btstack_driver_set_config(uint8_t dev_addr, uint8_t itf_num)
{
    (void)itf_num;

    if (dev_addr != usb_state.dev_addr) {
        return false;
    }

    usb_state.connected = true;
    printf("[HCI_USB] Bluetooth dongle configured\n");

    // Signal usbh layer that BT hardware is present (enables BTstack loop)
    extern void usbh_set_bt_available(bool available);
    usbh_set_bt_available(true);

#if USE_BTSTACK
    // Now that dongle is connected, power on BTstack
    printf("[HCI_USB] Powering on BTstack...\n");
    extern void btstack_host_power_on(void);
    btstack_host_power_on();
#else
    // Standalone test mode - auto-init and send HCI Reset
    hci_transport_h2_tinyusb_test_init();

    // Start receiving events (need to open transport first)
    usb_state.opened = true;
    usb_submit_event_transfer();

    // Send HCI_Reset to verify communication
    hci_transport_h2_tinyusb_test_reset();
#endif

    return true;
}

bool btstack_driver_xfer_cb(uint8_t dev_addr, uint8_t ep_addr,
                            xfer_result_t result, uint32_t xferred_bytes)
{
    // Quiet - too noisy
    // printf("[HCI_USB] xfer_cb EP=0x%02X result=%d bytes=%lu\n",
    //        ep_addr, result, (unsigned long)xferred_bytes);

    if (dev_addr != usb_state.dev_addr) {
        return false;
    }

    if (result != XFER_RESULT_SUCCESS) {
        printf("[HCI_USB] Transfer failed on EP 0x%02X: %d\n", ep_addr, result);

        // Clear pending flags
        if (ep_addr == usb_state.ep_evt_in) {
            usb_state.evt_pending = false;
        } else if (ep_addr == usb_state.ep_acl_in) {
            usb_state.acl_in_pending = false;
        } else if (ep_addr == usb_state.ep_acl_out) {
            usb_state.acl_out_pending = false;
        }

        return true;
    }

    if (ep_addr == usb_state.ep_evt_in) {
        // HCI Event received
        // printf("[HCI_USB] HCI Event: %d bytes\n", (int)xferred_bytes);
        usb_state.evt_pending = false;
        usb_state.evt_len = (uint16_t)xferred_bytes;
        usb_state.evt_ready = true;

#if USE_BTSTACK
        // Trigger BTstack run loop
        btstack_run_loop_poll_data_sources_from_irq();
#endif
    }
    else if (ep_addr == usb_state.ep_acl_in) {
        // ACL Data received
        usb_state.acl_in_pending = false;
        usb_state.acl_len = (uint16_t)xferred_bytes;
        usb_state.acl_ready = true;

#if USE_BTSTACK
        // Trigger BTstack run loop
        btstack_run_loop_poll_data_sources_from_irq();
#endif
    }
    else if (ep_addr == usb_state.ep_acl_out) {
        // ACL Data sent
        usb_state.acl_out_pending = false;

        // Notify BTstack that we can send again
        if (usb_state.packet_handler) {
            // Send HCI_EVENT_TRANSPORT_PACKET_SENT
            static uint8_t packet_sent_event[] = {
                HCI_EVENT_TRANSPORT_PACKET_SENT, 0
            };
            usb_state.packet_handler(HCI_EVENT_PACKET, packet_sent_event,
                                     sizeof(packet_sent_event));
        }
    }
    else if (ep_addr == 0) {
        // Control transfer complete (command sent)
        usb_state.cmd_pending = false;

        // Notify BTstack that we can send again
        if (usb_state.packet_handler) {
            static uint8_t packet_sent_event[] = {
                HCI_EVENT_TRANSPORT_PACKET_SENT, 0
            };
            usb_state.packet_handler(HCI_EVENT_PACKET, packet_sent_event,
                                     sizeof(packet_sent_event));
        }
    }

    return true;
}

void btstack_driver_close(uint8_t dev_addr)
{
    if (dev_addr != usb_state.dev_addr) {
        return;
    }

    printf("[HCI_USB] Bluetooth dongle disconnected\n");

    usb_state.connected = false;
    usb_state.opened = false;
    usb_state.evt_pending = false;
    usb_state.acl_in_pending = false;
    usb_state.cmd_pending = false;
    usb_state.acl_out_pending = false;
}

// ============================================================================
// TINYUSB CLASS DRIVER STRUCT
// ============================================================================

const usbh_class_driver_t usbh_btstack_driver = {
    .name = "BTSTACK",
    .init = btstack_driver_init,
    .deinit = btstack_driver_deinit,
    .open = btstack_driver_open,
    .set_config = btstack_driver_set_config,
    .xfer_cb = btstack_driver_xfer_cb,
    .close = btstack_driver_close,
};
