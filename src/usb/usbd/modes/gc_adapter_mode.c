// gc_adapter_mode.c - GameCube Adapter USB device mode with custom class driver
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Emulates Nintendo GameCube Controller Adapter for Wii U/Switch.
// Uses a custom USB class driver (like HOJA) for reliable OUT endpoint handling.

#include "tusb_config.h"

#if CFG_TUD_GC_ADAPTER

#include "tusb.h"
#include "device/usbd_pvt.h"  // For usbd_class_driver_t, usbd_edpt_xfer, etc.
#include "../usbd_mode.h"
#include "../usbd.h"
#include "descriptors/gc_adapter_descriptors.h"
#include "core/buttons.h"
#include "core/output_interface.h"
#include <string.h>

// ============================================================================
// CONSTANTS
// ============================================================================

#define GC_TX_BUFSIZE  37  // Input report size (report ID + 36 bytes data)
#define GC_RX_BUFSIZE  6   // Output report size (matches HOJA)

// ============================================================================
// CUSTOM CLASS DRIVER INTERFACE STATE
// ============================================================================

typedef struct {
    uint8_t itf_num;
    uint8_t ep_in;
    uint8_t ep_out;
    uint8_t itf_protocol;
    uint8_t protocol_mode;
    uint8_t idle_rate;
    uint16_t report_desc_len;

    CFG_TUSB_MEM_ALIGN uint8_t epin_buf[GC_TX_BUFSIZE];
    CFG_TUSB_MEM_ALIGN uint8_t epout_buf[GC_RX_BUFSIZE];

    tusb_hid_descriptor_hid_t const* hid_descriptor;
} gc_interface_t;

static gc_interface_t _gc_itf;

// Forward declaration
void gc_adapter_mode_handle_output(uint8_t report_id, const uint8_t* data, uint16_t len);

// ============================================================================
// REPORT STATE
// ============================================================================

static gc_adapter_in_report_t gc_adapter_report;
static gc_adapter_out_report_t gc_adapter_rumble;
static bool gc_adapter_rumble_available = false;

// ============================================================================
// CUSTOM CLASS DRIVER IMPLEMENTATION
// ============================================================================

static void gc_driver_init(void)
{
    tu_memclr(&_gc_itf, sizeof(_gc_itf));
}

static void gc_driver_reset(uint8_t rhport)
{
    (void)rhport;
    tu_memclr(&_gc_itf, sizeof(_gc_itf));
}

static uint16_t gc_driver_open(uint8_t rhport, tusb_desc_interface_t const* desc_itf, uint16_t max_len)
{
    // Only open if we're in GC adapter mode
    if (usbd_get_mode() != USB_OUTPUT_MODE_GC_ADAPTER) return 0;

    // Calculate driver length: interface + HID + endpoints
    uint16_t const drv_len = (uint16_t)(sizeof(tusb_desc_interface_t) +
                                         sizeof(tusb_hid_descriptor_hid_t) +
                                         desc_itf->bNumEndpoints * sizeof(tusb_desc_endpoint_t));
    TU_ASSERT(max_len >= drv_len, 0);

    // Already opened?
    TU_ASSERT(_gc_itf.ep_in == 0, 0);

    uint8_t const* p_desc = (uint8_t const*)desc_itf;

    // HID descriptor
    p_desc = tu_desc_next(p_desc);
    TU_ASSERT(HID_DESC_TYPE_HID == tu_desc_type(p_desc), 0);
    _gc_itf.hid_descriptor = (tusb_hid_descriptor_hid_t const*)p_desc;

    // Endpoint descriptors
    p_desc = tu_desc_next(p_desc);
    TU_ASSERT(usbd_open_edpt_pair(rhport, p_desc, desc_itf->bNumEndpoints,
                                   TUSB_XFER_INTERRUPT, &_gc_itf.ep_out, &_gc_itf.ep_in), 0);

    if (desc_itf->bInterfaceSubClass == HID_SUBCLASS_BOOT) {
        _gc_itf.itf_protocol = desc_itf->bInterfaceProtocol;
    }

    _gc_itf.protocol_mode = HID_PROTOCOL_REPORT;
    _gc_itf.itf_num = desc_itf->bInterfaceNumber;
    _gc_itf.report_desc_len = tu_unaligned_read16((uint8_t const*)_gc_itf.hid_descriptor +
                                                   offsetof(tusb_hid_descriptor_hid_t, wReportLength));

    // Arm OUT endpoint for receiving (critical - use exact buffer size like HOJA)
    if (_gc_itf.ep_out) {
        usbd_edpt_xfer(rhport, _gc_itf.ep_out, _gc_itf.epout_buf, sizeof(_gc_itf.epout_buf));
    }

    return drv_len;
}

static bool gc_driver_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const* request)
{
    TU_VERIFY(request->bmRequestType_bit.recipient == TUSB_REQ_RCPT_INTERFACE);
    TU_VERIFY(request->wIndex == _gc_itf.itf_num);

    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_STANDARD) {
        // Standard requests
        if (stage == CONTROL_STAGE_SETUP) {
            uint8_t const desc_type = tu_u16_high(request->wValue);

            if (request->bRequest == TUSB_REQ_GET_DESCRIPTOR) {
                if (desc_type == HID_DESC_TYPE_HID) {
                    TU_VERIFY(_gc_itf.hid_descriptor);
                    TU_VERIFY(tud_control_xfer(rhport, request,
                              (void*)(uintptr_t)_gc_itf.hid_descriptor,
                              _gc_itf.hid_descriptor->bLength));
                } else if (desc_type == HID_DESC_TYPE_REPORT) {
                    TU_VERIFY(tud_control_xfer(rhport, request,
                              (void*)(uintptr_t)gc_adapter_report_descriptor,
                              _gc_itf.report_desc_len));
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }
    } else if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_CLASS) {
        // Class requests
        switch (request->bRequest) {
            case HID_REQ_CONTROL_GET_REPORT:
                if (stage == CONTROL_STAGE_SETUP) {
                    // Not implemented for GC adapter
                    return false;
                }
                break;

            case HID_REQ_CONTROL_SET_REPORT:
                if (stage == CONTROL_STAGE_SETUP) {
                    TU_VERIFY(request->wLength <= sizeof(_gc_itf.epout_buf));
                    tud_control_xfer(rhport, request, _gc_itf.epout_buf, request->wLength);
                } else if (stage == CONTROL_STAGE_ACK) {
                    uint8_t const report_id = tu_u16_low(request->wValue);
                    gc_adapter_mode_handle_output(report_id, _gc_itf.epout_buf, request->wLength);
                }
                break;

            case HID_REQ_CONTROL_SET_IDLE:
                if (stage == CONTROL_STAGE_SETUP) {
                    _gc_itf.idle_rate = tu_u16_high(request->wValue);
                    tud_control_status(rhport, request);
                }
                break;

            case HID_REQ_CONTROL_GET_IDLE:
                if (stage == CONTROL_STAGE_SETUP) {
                    tud_control_xfer(rhport, request, &_gc_itf.idle_rate, 1);
                }
                break;

            case HID_REQ_CONTROL_GET_PROTOCOL:
                if (stage == CONTROL_STAGE_SETUP) {
                    tud_control_xfer(rhport, request, &_gc_itf.protocol_mode, 1);
                }
                break;

            case HID_REQ_CONTROL_SET_PROTOCOL:
                if (stage == CONTROL_STAGE_SETUP) {
                    tud_control_status(rhport, request);
                } else if (stage == CONTROL_STAGE_ACK) {
                    _gc_itf.protocol_mode = (uint8_t)request->wValue;
                }
                break;

            default:
                return false;
        }
    } else {
        return false;
    }

    return true;
}

static bool gc_driver_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes)
{
    (void)result;
    (void)xferred_bytes;

    if (ep_addr == _gc_itf.ep_out) {
        // OUT transfer complete - received data from host
        gc_adapter_mode_handle_output(0, _gc_itf.epout_buf, (uint16_t)xferred_bytes);

        // Re-arm OUT endpoint immediately (critical - like HOJA does)
        usbd_edpt_xfer(rhport, _gc_itf.ep_out, _gc_itf.epout_buf, sizeof(_gc_itf.epout_buf));
    }

    return true;
}

// Custom class driver structure
static const usbd_class_driver_t gc_class_driver = {
#if CFG_TUSB_DEBUG >= 2
    .name = "GC_ADAPTER",
#endif
    .init = gc_driver_init,
    .reset = gc_driver_reset,
    .open = gc_driver_open,
    .control_xfer_cb = gc_driver_control_xfer_cb,
    .xfer_cb = gc_driver_xfer_cb,
    .sof = NULL,
};

// ============================================================================
// MODE INTERFACE IMPLEMENTATION
// ============================================================================

static void gc_adapter_mode_init(void)
{
    memset(&gc_adapter_report, 0, sizeof(gc_adapter_in_report_t));
    gc_adapter_report.report_id = GC_ADAPTER_REPORT_ID_INPUT;

    // Initialize all ports with rumble available but no controller
    // Real adapter uses all zeros for unconnected ports (status 0x04)
    // Ports are marked CONNECTED (0x14) when they receive input
    for (int i = 0; i < 4; i++) {
        gc_adapter_report.port[i].status = GC_ADAPTER_STATUS_RUMBLE;
        // All analog values stay 0 for unconnected ports (matches real adapter)
    }

    memset(&gc_adapter_rumble, 0, sizeof(gc_adapter_out_report_t));
    gc_adapter_rumble_available = false;
}

static bool gc_adapter_mode_is_ready(void)
{
    uint8_t const rhport = 0;
    bool ready = tud_ready() && (_gc_itf.ep_in != 0) && !usbd_edpt_busy(rhport, _gc_itf.ep_in);
    return ready;
}

static bool gc_adapter_mode_send_report(uint8_t player_index,
                                         const input_event_t* event,
                                         const profile_output_t* profile_out,
                                         uint32_t buttons)
{
    (void)event;
    uint8_t const rhport = 0;

    uint8_t port = player_index;
    if (port >= 4) port = 0;

    // Mark this port as connected (0x14 = controller connected + rumble power)
    gc_adapter_report.port[port].status = GC_ADAPTER_STATUS_CONNECTED;

    // Map buttons
    gc_adapter_report.port[port].a = (buttons & JP_BUTTON_B2) ? 1 : 0;
    gc_adapter_report.port[port].b = (buttons & JP_BUTTON_B1) ? 1 : 0;
    gc_adapter_report.port[port].x = (buttons & JP_BUTTON_B4) ? 1 : 0;
    gc_adapter_report.port[port].y = (buttons & JP_BUTTON_B3) ? 1 : 0;
    gc_adapter_report.port[port].z = (buttons & JP_BUTTON_R1) ? 1 : 0;
    gc_adapter_report.port[port].l = (buttons & JP_BUTTON_L2) ? 1 : 0;
    gc_adapter_report.port[port].r = (buttons & JP_BUTTON_R2) ? 1 : 0;
    gc_adapter_report.port[port].start = (buttons & JP_BUTTON_S2) ? 1 : 0;
    gc_adapter_report.port[port].dpad_up = (buttons & JP_BUTTON_DU) ? 1 : 0;
    gc_adapter_report.port[port].dpad_down = (buttons & JP_BUTTON_DD) ? 1 : 0;
    gc_adapter_report.port[port].dpad_left = (buttons & JP_BUTTON_DL) ? 1 : 0;
    gc_adapter_report.port[port].dpad_right = (buttons & JP_BUTTON_DR) ? 1 : 0;

    // Analog sticks (Y inverted for GC)
    gc_adapter_report.port[port].stick_x = profile_out->left_x;
    gc_adapter_report.port[port].stick_y = 255 - profile_out->left_y;
    gc_adapter_report.port[port].cstick_x = profile_out->right_x;
    gc_adapter_report.port[port].cstick_y = 255 - profile_out->right_y;

    // Analog triggers
    gc_adapter_report.port[port].trigger_l = profile_out->l2_analog;
    gc_adapter_report.port[port].trigger_r = profile_out->r2_analog;

    if (gc_adapter_report.port[port].trigger_l == 0 && (buttons & JP_BUTTON_L2))
        gc_adapter_report.port[port].trigger_l = 0xFF;
    if (gc_adapter_report.port[port].trigger_r == 0 && (buttons & JP_BUTTON_R2))
        gc_adapter_report.port[port].trigger_r = 0xFF;

    // Send using our custom driver endpoint
    if (!usbd_edpt_claim(rhport, _gc_itf.ep_in)) {
        return false;
    }

    // Copy report to buffer with report ID
    _gc_itf.epin_buf[0] = GC_ADAPTER_REPORT_ID_INPUT;
    memcpy(_gc_itf.epin_buf + 1, gc_adapter_report.port, sizeof(gc_adapter_report.port));

    return usbd_edpt_xfer(rhport, _gc_itf.ep_in, _gc_itf.epin_buf, sizeof(gc_adapter_in_report_t));
}

void gc_adapter_mode_handle_output(uint8_t report_id, const uint8_t* data, uint16_t len)
{
    // If report_id is 0, first byte of data is the report ID
    if (report_id == 0 && len > 0) {
        report_id = data[0];
        data++;
        len--;
    }

    if (report_id == GC_ADAPTER_REPORT_ID_RUMBLE && len >= 4) {
        gc_adapter_rumble.report_id = GC_ADAPTER_REPORT_ID_RUMBLE;
        // HOJA only checks bit 0 for rumble on/off, not the whole byte!
        // 0x01 = rumble on, 0x10 = rumble off (bit 0 not set)
        gc_adapter_rumble.rumble[0] = (data[0] & 0x01) ? 1 : 0;
        gc_adapter_rumble.rumble[1] = (data[1] & 0x01) ? 1 : 0;
        gc_adapter_rumble.rumble[2] = (data[2] & 0x01) ? 1 : 0;
        gc_adapter_rumble.rumble[3] = (data[3] & 0x01) ? 1 : 0;
        gc_adapter_rumble_available = true;
        return;
    }

    if (report_id == GC_ADAPTER_REPORT_ID_INIT) {
        // Init command received - adapter is now active
        return;
    }

    // Unknown report - ignore
}

static uint8_t gc_adapter_mode_get_rumble(void)
{
    for (int i = 0; i < 4; i++) {
        if (gc_adapter_rumble.rumble[i]) return 0xFF;
    }
    return 0;
}

uint8_t gc_adapter_mode_get_port_rumble(uint8_t port)
{
    if (port >= 4) return 0;
    return gc_adapter_rumble.rumble[port] ? 0xFF : 0;
}

static bool gc_adapter_mode_get_feedback(output_feedback_t* fb)
{
    if (!fb) return false;
    if (!gc_adapter_rumble_available) return false;

    uint8_t rumble = 0;
    for (int i = 0; i < 4; i++) {
        if (gc_adapter_rumble.rumble[i]) {
            rumble = 0xFF;
            break;
        }
    }

    fb->rumble_left = rumble;
    fb->rumble_right = rumble;
    fb->led_player = 0;
    fb->led_r = fb->led_g = fb->led_b = 0;
    fb->dirty = true;

    gc_adapter_rumble_available = false;
    return true;
}

static const uint8_t* gc_adapter_mode_get_device_descriptor(void)
{
    return (const uint8_t*)&gc_adapter_device_descriptor;
}

static const uint8_t* gc_adapter_mode_get_config_descriptor(void)
{
    return gc_adapter_config_descriptor;
}

static const uint8_t* gc_adapter_mode_get_report_descriptor(void)
{
    return gc_adapter_report_descriptor;
}

static const usbd_class_driver_t* gc_adapter_mode_get_class_driver(void)
{
    return &gc_class_driver;
}

// ============================================================================
// MODE EXPORT
// ============================================================================

const usbd_mode_t gc_adapter_mode = {
    .name = "GC Adapter",
    .mode = USB_OUTPUT_MODE_GC_ADAPTER,

    .get_device_descriptor = gc_adapter_mode_get_device_descriptor,
    .get_config_descriptor = gc_adapter_mode_get_config_descriptor,
    .get_report_descriptor = gc_adapter_mode_get_report_descriptor,

    .init = gc_adapter_mode_init,
    .send_report = gc_adapter_mode_send_report,
    .is_ready = gc_adapter_mode_is_ready,

    .handle_output = gc_adapter_mode_handle_output,
    .get_rumble = gc_adapter_mode_get_rumble,
    .get_feedback = gc_adapter_mode_get_feedback,
    .get_report = NULL,

    .get_class_driver = gc_adapter_mode_get_class_driver,  // Custom driver!
    .task = NULL,
};

#endif // CFG_TUD_GC_ADAPTER
