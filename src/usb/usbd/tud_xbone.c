// tud_xbone.c - Xbox One TinyUSB device driver
// SPDX-License-Identifier: MIT
// Based on GP2040-CE implementation (gp2040-ce.info)

#include "tud_xbone.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>

// Forward declaration for auth passthrough check (weak - returns false if not linked)
__attribute__((weak)) bool xbone_auth_is_available(void) { return false; }

// ============================================================================
// CONFIGURATION
// ============================================================================

#define XBONE_OUT_SIZE         64
#define XBONE_ITF_COUNT        8
#define XBONE_TX_BUFSIZE       64
#define XBONE_RX_BUFSIZE       64

#define REPORT_QUEUE_SIZE      16
#define REPORT_QUEUE_INTERVAL  15    // ms between queued reports
#define ANNOUNCE_DELAY         500   // ms minimum before sending announce
#define ANNOUNCE_MAX_WAIT      5000  // ms maximum wait for auth controller
#define ACK_WAIT_TIMEOUT       2000  // ms to wait for ACK

// Vendor request types
#define USB_SETUP_DEVICE_TO_HOST       0x80
#define USB_SETUP_HOST_TO_DEVICE       0x00
#define USB_SETUP_TYPE_VENDOR          0x40
#define USB_SETUP_RECIPIENT_DEVICE     0x00

#define REQ_GET_OS_FEATURE_DESCRIPTOR  0x20
#define DESC_EXTENDED_COMPATIBLE_ID    0x0004

// ============================================================================
// STATE
// ============================================================================

typedef struct {
    uint8_t itf_num;
    uint8_t ep_in;
    uint8_t ep_out;
    CFG_TUSB_MEM_ALIGN uint8_t epin_buf[XBONE_TX_BUFSIZE];
    CFG_TUSB_MEM_ALIGN uint8_t epout_buf[XBONE_RX_BUFSIZE];
} xbone_interface_t;

typedef struct {
    uint8_t report[XBONE_ENDPOINT_SIZE];
    uint16_t len;
} report_queue_item_t;

// Interface tracking
static xbone_interface_t xbone_itf[XBONE_ITF_COUNT];

// Driver state
static xbone_driver_state_t driver_state = XBONE_STATE_IDLE;
static bool xbox_powered_on = false;
static bool waiting_ack = false;
static uint32_t waiting_ack_timeout = 0;
static uint32_t timer_announce = 0;
static uint32_t last_report_queue_sent = 0;

// XGIP protocol handlers
static xgip_t outgoing_xgip;
static xgip_t incoming_xgip;

// Report queue (simple ring buffer)
static report_queue_item_t report_queue[REPORT_QUEUE_SIZE];
static uint8_t queue_head = 0;
static uint8_t queue_tail = 0;
static uint8_t queue_count = 0;

// Auth passthrough
static xbone_auth_t auth_data = { 0 };

// Auth ready marker
static const uint8_t auth_ready[] = { 0x01, 0x00 };

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static uint8_t get_index_by_itfnum(uint8_t itf_num)
{
    for (uint8_t i = 0; i < XBONE_ITF_COUNT; i++) {
        if (itf_num == xbone_itf[i].itf_num) return i;
    }
    return 0xFF;
}

static void queue_report(uint8_t* report, uint16_t len)
{
    if (queue_count >= REPORT_QUEUE_SIZE) {
        return;  // Queue full
    }

    memcpy(report_queue[queue_tail].report, report, len);
    report_queue[queue_tail].len = len;

    queue_tail = (queue_tail + 1) % REPORT_QUEUE_SIZE;
    queue_count++;
}

static bool dequeue_report(uint8_t* report, uint16_t* len)
{
    if (queue_count == 0) {
        return false;
    }

    memcpy(report, report_queue[queue_head].report, report_queue[queue_head].len);
    *len = report_queue[queue_head].len;

    queue_head = (queue_head + 1) % REPORT_QUEUE_SIZE;
    queue_count--;
    return true;
}

static void set_ack_wait(void)
{
    waiting_ack = true;
    waiting_ack_timeout = to_ms_since_boot(get_absolute_time());
}

static bool send_report_internal(uint8_t* report, uint16_t len)
{
    xbone_interface_t* p_xbone = NULL;

    for (uint8_t i = 0; i < XBONE_ITF_COUNT; i++) {
        if (xbone_itf[i].ep_in != 0) {
            p_xbone = &xbone_itf[i];
            break;
        }
    }

    if (p_xbone == NULL) {
        return false;
    }

    if (tud_ready() && (p_xbone->ep_in != 0) && (!usbd_edpt_busy(0, p_xbone->ep_in))) {
        usbd_edpt_claim(0, p_xbone->ep_in);
        bool ret = usbd_edpt_xfer(0, p_xbone->ep_in, report, len);
        usbd_edpt_release(0, p_xbone->ep_in);
        return ret;
    }

    return false;
}

// ============================================================================
// TINYUSB CLASS DRIVER CALLBACKS
// ============================================================================

static void xbone_init(void)
{
    xgip_init(&outgoing_xgip);
    xgip_init(&incoming_xgip);

    timer_announce = to_ms_since_boot(get_absolute_time());
    xbox_powered_on = false;

    // Clear queue
    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;

    driver_state = XBONE_STATE_READY_ANNOUNCE;
    memset(&xbone_itf, 0, sizeof(xbone_itf));
}

static void xbone_reset(uint8_t rhport)
{
    (void)rhport;
    xbone_init();
}

static uint16_t xbone_open(uint8_t rhport, tusb_desc_interface_t const* itf_desc, uint16_t max_len)
{
    uint16_t drv_len = 0;

    if (itf_desc->bInterfaceClass != TUSB_CLASS_VENDOR_SPECIFIC) {
        return 0;
    }

    drv_len = sizeof(tusb_desc_interface_t) +
              (itf_desc->bNumEndpoints * sizeof(tusb_desc_endpoint_t));

    if (max_len < drv_len) {
        return 0;
    }

    // Find available interface slot
    xbone_interface_t* p_xbone = NULL;
    for (uint8_t i = 0; i < XBONE_ITF_COUNT; i++) {
        if (xbone_itf[i].ep_in == 0 && xbone_itf[i].ep_out == 0) {
            p_xbone = &xbone_itf[i];
            break;
        }
    }

    if (p_xbone == NULL) {
        return 0;
    }

    uint8_t const* p_desc = (uint8_t const*)itf_desc;

    // Check for Xbox One interface (subclass 0x47, protocol 0xD0)
    if (itf_desc->bInterfaceSubClass == 0x47 && itf_desc->bInterfaceProtocol == 0xD0) {
        p_desc = tu_desc_next(p_desc);
        TU_ASSERT(usbd_open_edpt_pair(rhport, p_desc, itf_desc->bNumEndpoints,
                                       TUSB_XFER_INTERRUPT, &p_xbone->ep_out, &p_xbone->ep_in), 0);

        p_xbone->itf_num = itf_desc->bInterfaceNumber;

        // Prepare OUT endpoint for receiving
        if (p_xbone->ep_out) {
            if (!usbd_edpt_xfer(rhport, p_xbone->ep_out, p_xbone->epout_buf, sizeof(p_xbone->epout_buf))) {
                TU_LOG1("XBONE: Failed to start OUT transfer\r\n");
            }
        }
    }

    return drv_len;
}

static bool xbone_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request)
{
    return true;
}

static bool xbone_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    (void)result;

    xbone_interface_t* p_xbone = NULL;
    for (uint8_t i = 0; i < XBONE_ITF_COUNT; i++) {
        if (ep_addr == xbone_itf[i].ep_out || ep_addr == xbone_itf[i].ep_in) {
            p_xbone = &xbone_itf[i];
            break;
        }
    }

    if (p_xbone == NULL) {
        return false;
    }

    if (ep_addr == p_xbone->ep_out) {
        // Parse incoming packet
        xgip_parse(&incoming_xgip, p_xbone->epout_buf, xferred_bytes);

        // Send ACK if required
        if (xgip_ack_required(&incoming_xgip)) {
            queue_report(xgip_generate_ack(&incoming_xgip), xgip_get_packet_length(&incoming_xgip));
        }

        uint8_t cmd = xgip_get_command(&incoming_xgip);

        if (cmd == GIP_ACK_RESPONSE) {
            waiting_ack = false;
        } else if (cmd == GIP_DEVICE_DESCRIPTOR) {
            // Console requested descriptor
            xgip_reset(&outgoing_xgip);
            xgip_set_attributes(&outgoing_xgip, GIP_DEVICE_DESCRIPTOR,
                               xgip_get_sequence(&incoming_xgip), 1, 1, 0);
            xgip_set_data(&outgoing_xgip, xbone_gip_descriptor, sizeof(xbone_gip_descriptor));
            driver_state = XBONE_STATE_SEND_DESCRIPTOR;
        } else if (cmd == GIP_POWER_MODE_DEVICE_CONFIG || cmd == GIP_CMD_WAKEUP || cmd == GIP_CMD_RUMBLE) {
            xbox_powered_on = true;
        } else if (cmd == GIP_AUTH || cmd == GIP_FINAL_AUTH) {
            // Check for auth complete marker
            if (xgip_get_data_length(&incoming_xgip) == 2 &&
                memcmp(xgip_get_data(&incoming_xgip), auth_ready, sizeof(auth_ready)) == 0) {
                auth_data.auth_completed = true;
            }

            // Forward auth to dongle when complete packet received
            if (!xgip_is_chunked(&incoming_xgip) ||
                (xgip_is_chunked(&incoming_xgip) && xgip_end_of_chunk(&incoming_xgip))) {

                xbone_auth_set_data(xgip_get_data(&incoming_xgip),
                                   xgip_get_data_length(&incoming_xgip),
                                   xgip_get_sequence(&incoming_xgip),
                                   xgip_get_command(&incoming_xgip),
                                   XBONE_AUTH_SEND_CONSOLE_TO_DONGLE);
                xgip_reset(&incoming_xgip);
            }
        }

        // Ready for next packet
        TU_ASSERT(usbd_edpt_xfer(rhport, p_xbone->ep_out, p_xbone->epout_buf,
                                  sizeof(p_xbone->epout_buf)));
    }

    return true;
}

// ============================================================================
// CLASS DRIVER STRUCT
// ============================================================================

static const usbd_class_driver_t xbone_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "XBONE",
#endif
    .init = xbone_init,
    .reset = xbone_reset,
    .open = xbone_open,
    .control_xfer_cb = xbone_control_xfer_cb,
    .xfer_cb = xbone_xfer_cb,
    .sof = NULL
};

const usbd_class_driver_t* tud_xbone_class_driver(void)
{
    return &xbone_driver;
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool tud_xbone_ready(void)
{
    for (uint8_t i = 0; i < XBONE_ITF_COUNT; i++) {
        if (xbone_itf[i].ep_in != 0) {
            return tud_ready() && !usbd_edpt_busy(0, xbone_itf[i].ep_in);
        }
    }
    return false;
}

bool tud_xbone_send_report(gip_input_report_t* report)
{
    // Set up GIP header
    report->header.command = GIP_INPUT_REPORT;
    report->header.internal = 0;
    report->header.sequence = 0;
    report->header.length = sizeof(gip_input_report_t) - sizeof(gip_header_t);

    return send_report_internal((uint8_t*)report, sizeof(gip_input_report_t));
}

void tud_xbone_update(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());

    // Process report queue
    if (queue_count > 0 && (now - last_report_queue_sent) > REPORT_QUEUE_INTERVAL) {
        uint8_t report[XBONE_ENDPOINT_SIZE];
        uint16_t len;

        if (dequeue_report(report, &len)) {
            if (send_report_internal(report, len)) {
                last_report_queue_sent = now;
            } else {
                // Failed - re-queue at front (by adjusting head back)
                queue_head = (queue_head + REPORT_QUEUE_SIZE - 1) % REPORT_QUEUE_SIZE;
                queue_count++;
                busy_wait_ms(REPORT_QUEUE_INTERVAL);
            }
        }
    }

    // Don't proceed if waiting for ACK
    if (waiting_ack) {
        if ((now - waiting_ack_timeout) < ACK_WAIT_TIMEOUT) {
            return;
        }
        // ACK timeout - continue anyway
        waiting_ack = false;
    }

    switch (driver_state) {
        case XBONE_STATE_READY_ANNOUNCE:
            // Wait for minimum delay AND (auth controller ready OR max wait exceeded)
            if (now - timer_announce > ANNOUNCE_DELAY) {
                bool auth_ready = xbone_auth_is_available();
                bool max_wait_exceeded = (now - timer_announce > ANNOUNCE_MAX_WAIT);

                if (!auth_ready && !max_wait_exceeded) {
                    // Still waiting for auth controller
                    static uint32_t last_wait_log = 0;
                    if (now - last_wait_log > 1000) {
                        printf("[tud_xbone] Waiting for auth passthrough controller...\n");
                        last_wait_log = now;
                    }
                    break;
                }

                if (auth_ready) {
                    printf("[tud_xbone] Auth passthrough controller ready, announcing to console\n");
                } else {
                    printf("[tud_xbone] Auth passthrough timeout, announcing without controller\n");
                }

                xgip_reset(&outgoing_xgip);
                xgip_set_attributes(&outgoing_xgip, GIP_ANNOUNCE, 1, 1, 0, 0);

                // Copy announce packet with timestamp
                uint8_t announce[sizeof(xbone_announce_packet)];
                memcpy(announce, xbone_announce_packet, sizeof(announce));
                memcpy(&announce[3], &now, 3);  // Insert timestamp

                xgip_set_data(&outgoing_xgip, announce, sizeof(announce));
                queue_report(xgip_generate_packet(&outgoing_xgip),
                            xgip_get_packet_length(&outgoing_xgip));

                driver_state = XBONE_STATE_WAIT_DESCRIPTOR_REQUEST;
            }
            break;

        case XBONE_STATE_SEND_DESCRIPTOR:
            queue_report(xgip_generate_packet(&outgoing_xgip),
                        xgip_get_packet_length(&outgoing_xgip));

            if (xgip_end_of_chunk(&outgoing_xgip)) {
                driver_state = XBONE_STATE_SETUP_AUTH;
            }

            if (xgip_get_packet_ack(&outgoing_xgip)) {
                set_ack_wait();
            }
            break;

        case XBONE_STATE_SETUP_AUTH:
            // Handle auth passthrough from dongle to console
            if (auth_data.state == XBONE_AUTH_SEND_DONGLE_TO_CONSOLE) {
                bool is_chunked = (auth_data.length > GIP_MAX_CHUNK_SIZE);
                xgip_reset(&outgoing_xgip);
                xgip_set_attributes(&outgoing_xgip, auth_data.auth_type,
                                   auth_data.sequence, 1, is_chunked, 1);
                xgip_set_data(&outgoing_xgip, auth_data.buffer, auth_data.length);
                auth_data.state = XBONE_AUTH_WAIT_DONGLE_TO_CONSOLE;
            } else if (auth_data.state == XBONE_AUTH_WAIT_DONGLE_TO_CONSOLE) {
                queue_report(xgip_generate_packet(&outgoing_xgip),
                            xgip_get_packet_length(&outgoing_xgip));

                if (!xgip_is_chunked(&outgoing_xgip) || xgip_end_of_chunk(&outgoing_xgip)) {
                    auth_data.state = XBONE_AUTH_IDLE;
                }

                if (xgip_get_packet_ack(&outgoing_xgip)) {
                    set_ack_wait();
                }
            }
            break;

        case XBONE_STATE_IDLE:
        case XBONE_STATE_WAIT_DESCRIPTOR_REQUEST:
        default:
            break;
    }
}

bool tud_xbone_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                       tusb_control_request_t const* request)
{
    static uint8_t buf[255];

    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    if (request->bmRequestType_bit.direction == TUSB_DIR_IN) {
        uint16_t len = request->wLength;

        // Handle Windows OS descriptor request
        if (request->bmRequestType == (USB_SETUP_DEVICE_TO_HOST | USB_SETUP_RECIPIENT_DEVICE | USB_SETUP_TYPE_VENDOR) &&
            request->bRequest == REQ_GET_OS_FEATURE_DESCRIPTOR &&
            request->wIndex == DESC_EXTENDED_COMPATIBLE_ID) {

            if (len > sizeof(xbone_os_compat_descriptor)) {
                len = sizeof(xbone_os_compat_descriptor);
            }
            memcpy(buf, &xbone_os_compat_descriptor, len);
        }

        tud_control_xfer(rhport, request, buf, len);
    } else {
        tud_control_xfer(rhport, request, buf, request->wLength);
    }

    return true;
}

// ============================================================================
// AUTH PASSTHROUGH API
// ============================================================================

xbone_auth_state_t xbone_auth_get_state(void)
{
    return auth_data.state;
}

void xbone_auth_set_data(uint8_t* data, uint16_t len, uint8_t seq,
                         uint8_t type, xbone_auth_state_t new_state)
{
    if (len > XGIP_MAX_DATA_SIZE) {
        return;
    }

    memcpy(auth_data.buffer, data, len);
    auth_data.length = len;
    auth_data.sequence = seq;
    auth_data.auth_type = type;
    auth_data.state = new_state;
}

uint8_t* xbone_auth_get_buffer(void)
{
    return auth_data.buffer;
}

uint16_t xbone_auth_get_length(void)
{
    return auth_data.length;
}

uint8_t xbone_auth_get_sequence(void)
{
    return auth_data.sequence;
}

uint8_t xbone_auth_get_type(void)
{
    return auth_data.auth_type;
}

bool xbone_auth_is_completed(void)
{
    return auth_data.auth_completed;
}

void xbone_auth_set_completed(bool completed)
{
    auth_data.auth_completed = completed;
}

bool xbone_is_powered_on(void)
{
    return xbox_powered_on;
}
