// xbone_auth.c - Xbox One Auth Dongle Host Driver
// SPDX-License-Identifier: MIT
// Based on GP2040-CE implementation (gp2040-ce.info)

#include "xbone_auth.h"
#include "usb/usbd/drivers/xgip_protocol.h"
#include "usb/usbd/drivers/tud_xbone.h"
#include "platform/platform.h"
#include "tusb.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define REPORT_QUEUE_SIZE      16
#define REPORT_QUEUE_INTERVAL  15  // ms

// Power-on and rumble commands for dongle initialization
static const uint8_t xb1_power_on[] = {
    0x06, 0x62, 0x45, 0xb8, 0x77, 0x26, 0x2c, 0x55,
    0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f
};
static const uint8_t xb1_power_on_single[] = { 0x00 };
static const uint8_t xb1_rumble_on[] = {
    0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xeb
};

// ============================================================================
// STATE
// ============================================================================

typedef struct {
    uint8_t report[64];
    uint16_t len;
} report_queue_item_t;

static uint8_t xbone_dev_addr = 0;
static uint8_t xbone_instance = 0;
static bool dongle_ready = false;

static xgip_t incoming_xgip;
static xgip_t outgoing_xgip;

// Report queue
static report_queue_item_t report_queue[REPORT_QUEUE_SIZE];
static uint8_t queue_head = 0;
static uint8_t queue_tail = 0;
static uint8_t queue_count = 0;
static uint32_t last_report_queue_sent = 0;

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void queue_host_report(uint8_t* report, uint16_t len)
{
    if (queue_count >= REPORT_QUEUE_SIZE) {
        return;
    }

    memcpy(report_queue[queue_tail].report, report, len);
    report_queue[queue_tail].len = len;

    queue_tail = (queue_tail + 1) % REPORT_QUEUE_SIZE;
    queue_count++;
}

// Forward declaration - implemented by xinput host or similar
extern bool tuh_xinput_send_report(uint8_t dev_addr, uint8_t instance,
                                   uint8_t const* report, uint16_t len);

static bool send_to_dongle(uint8_t* report, uint16_t len)
{
    if (xbone_dev_addr == 0) {
        printf("[xbone_auth] send_to_dongle: no controller registered!\n");
        return false;
    }
    bool result = tuh_xinput_send_report(xbone_dev_addr, xbone_instance, report, len);
    printf("[xbone_auth] send_to_dongle: dev=%d inst=%d len=%d result=%s\n",
           xbone_dev_addr, xbone_instance, len, result ? "OK" : "FAILED");
    return result;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void xbone_auth_init(void)
{
    xgip_init(&incoming_xgip);
    xgip_init(&outgoing_xgip);

    xbone_dev_addr = 0;
    xbone_instance = 0;
    dongle_ready = false;

    queue_head = 0;
    queue_tail = 0;
    queue_count = 0;
}

bool xbone_auth_is_available(void)
{
    return dongle_ready && xbone_dev_addr != 0;
}

bool xbone_auth_is_complete(void)
{
    return xbone_auth_is_completed();
}

void xbone_auth_task(void)
{
    // Forward auth from console to controller
    xbone_auth_state_t state = xbone_auth_get_state();

    // dongle_ready is set ONLY after the real announce → descriptor → POWER_ON
    // handshake completes (see xbone_auth_source_input). We deliberately do
    // NOT mark the source ready on a timeout: forwarding auth to a source that
    // hasn't been powered on fails (and a real controller never recovers).
    if (!dongle_ready) {
        // Throttle the warning to once per second so we don't flood the log
        if (state == XBONE_AUTH_SEND_CONSOLE_TO_DONGLE) {
            static uint32_t last_warn_ms = 0;
            uint32_t now_ms = platform_time_ms();
            if (now_ms - last_warn_ms > 1000) {
                printf("[xbone_auth] WARNING: Auth request pending but dongle not initialized yet (dev_addr=%d)\n",
                       xbone_dev_addr);
                last_warn_ms = now_ms;
            }
        }
        return;
    }

    if (state == XBONE_AUTH_SEND_CONSOLE_TO_DONGLE) {
        printf("[xbone_auth] Forwarding auth challenge to controller: type=0x%02x len=%d seq=%d\n",
               xbone_auth_get_type(), xbone_auth_get_length(), xbone_auth_get_sequence());

        uint8_t is_chunked = (xbone_auth_get_length() > GIP_MAX_CHUNK_SIZE);
        uint8_t needs_ack = (xbone_auth_get_length() > 2);

        xgip_reset(&outgoing_xgip);
        xgip_set_attributes(&outgoing_xgip, xbone_auth_get_type(),
                           xbone_auth_get_sequence(), 1, is_chunked, needs_ack);
        xgip_set_data(&outgoing_xgip, xbone_auth_get_buffer(), xbone_auth_get_length());

        // Update state to waiting
        xbone_auth_set_data(xbone_auth_get_buffer(), xbone_auth_get_length(),
                           xbone_auth_get_sequence(), xbone_auth_get_type(),
                           XBONE_AUTH_WAIT_CONSOLE_TO_DONGLE);

    } else if (state == XBONE_AUTH_WAIT_CONSOLE_TO_DONGLE) {
        uint8_t* packet = xgip_generate_packet(&outgoing_xgip);
        uint8_t packet_len = xgip_get_packet_length(&outgoing_xgip);
        printf("[xbone_auth] Sending auth packet to controller: len=%d\n", packet_len);
        queue_host_report(packet, packet_len);

        if (!xgip_is_chunked(&outgoing_xgip) || xgip_end_of_chunk(&outgoing_xgip)) {
            printf("[xbone_auth] Auth challenge sent, waiting for controller response\n");
            xbone_auth_set_data(xbone_auth_get_buffer(), xbone_auth_get_length(),
                               xbone_auth_get_sequence(), xbone_auth_get_type(),
                               XBONE_AUTH_IDLE);
        }
    }

    // Process report queue - send to controller
    uint32_t now = platform_time_ms();
    if (queue_count > 0 && (now - last_report_queue_sent) > REPORT_QUEUE_INTERVAL) {
        uint8_t report[64];
        uint16_t len = report_queue[queue_head].len;
        memcpy(report, report_queue[queue_head].report, len);

        printf("[xbone_auth] Sending queued report to controller: len=%d, cmd=0x%02x\n", len, report[0]);
        if (send_to_dongle(report, len)) {
            queue_head = (queue_head + 1) % REPORT_QUEUE_SIZE;
            queue_count--;
            last_report_queue_sent = now;
        } else {
            printf("[xbone_auth] Failed to send report to controller\n");
            platform_sleep_ms(REPORT_QUEUE_INTERVAL);
        }
    }
}

void xbone_auth_register(uint8_t dev_addr, uint8_t instance)
{
    printf("[xbone_auth] Registering Xbox One controller for auth: dev_addr=%d, instance=%d\n", dev_addr, instance);
    xbone_dev_addr = dev_addr;
    xbone_instance = instance;

    xgip_reset(&incoming_xgip);
    xgip_reset(&outgoing_xgip);

    // Ready only after the real announce → descriptor → POWER_ON handshake
    // completes (matches GP2040-CE). The source drives this at its own pace.
    dongle_ready = false;
    printf("[xbone_auth] Controller mounted, waiting for ANNOUNCE handshake...\n");
}

// Called by the XInput host for every input report from an Xbox One source.
// The XInput host driver answers the source's ANNOUNCE itself (xboxone_init
// sends the power-on), and only GIP_AUTH / GIP_FINAL_AUTH reports are passed
// to xbone_auth_report_received(), so the ANNOUNCE -> descriptor -> POWER_ON
// branch there never sees its packets. A GIP source only sends input after it
// has been powered on, so the first input report is the handshake completing.
void xbone_auth_source_input(uint8_t dev_addr, uint8_t instance)
{
    if (dongle_ready || dev_addr != xbone_dev_addr || instance != xbone_instance) {
        return;
    }
    dongle_ready = true;
    printf("[xbone_auth] Source powered on (first input report), ready for auth\n");
}

void xbone_auth_unregister(uint8_t dev_addr)
{
    if (xbone_dev_addr == dev_addr) {
        printf("[xbone_auth] Unregistering dongle: dev_addr=%d\n", dev_addr);
        // Don't reset dongle_ready - Magic-X may remount but still be ready
        xbone_dev_addr = 0;
        xbone_instance = 0;
    }
}

void xbone_auth_xmount(uint8_t dev_addr, uint8_t instance,
                       uint8_t controller_type, uint8_t subtype)
{
    // Check if this looks like an Xbox One auth dongle
    // Controller type 0xFF with certain subtypes indicates auth dongle
    xbone_auth_register(dev_addr, instance);
}

void xbone_auth_report_received(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len)
{
    printf("[xbone_auth] Report received from controller: dev=%d inst=%d len=%d cmd=0x%02x bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n",
           dev_addr, instance, len, report[0],
           report[0],
           len > 1 ? report[1] : 0,
           len > 2 ? report[2] : 0,
           len > 3 ? report[3] : 0,
           len > 4 ? report[4] : 0,
           len > 5 ? report[5] : 0,
           len > 6 ? report[6] : 0,
           len > 7 ? report[7] : 0);

    if (dev_addr != xbone_dev_addr || instance != xbone_instance) {
        printf("[xbone_auth] Ignoring - not our registered controller\n");
        return;
    }

    xgip_parse(&incoming_xgip, report, len);

    if (!xgip_validate(&incoming_xgip)) {
        printf("[xbone_auth] Invalid packet, resetting\n");
        // First packet may be invalid, wait for dongle boot
        platform_sleep_ms(50);
        xgip_reset(&incoming_xgip);
        return;
    }

    uint8_t cmd = xgip_get_command(&incoming_xgip);

    // Send ACK if required by header, OR for any chunked GIP_AUTH response.
    // The MS Xbox Wireless controller (PID 0x0B12) stops sending mid-stream
    // unless we ACK every chunk during auth, even when its own needs_ack=0.
    bool force_ack = xgip_is_chunked(&incoming_xgip) &&
                     (cmd == GIP_AUTH || cmd == GIP_FINAL_AUTH);
    if (xgip_ack_required(&incoming_xgip) || force_ack) {
        printf("[xbone_auth] Sending ACK to controller (forced=%d)\n", force_ack && !xgip_ack_required(&incoming_xgip));
        queue_host_report(xgip_generate_ack(&incoming_xgip),
                         xgip_get_packet_length(&incoming_xgip));
    }

    printf("[xbone_auth] Parsed command: 0x%02x\n", cmd);

    switch (cmd) {
        case GIP_ANNOUNCE:
            // Dongle announced - request descriptor
            xgip_reset(&outgoing_xgip);
            xgip_set_attributes(&outgoing_xgip, GIP_DEVICE_DESCRIPTOR, 1, 1, false, 0);
            queue_host_report(xgip_generate_packet(&outgoing_xgip),
                             xgip_get_packet_length(&outgoing_xgip));
            break;

        case GIP_DEVICE_DESCRIPTOR:
            // Got descriptor - power on dongle
            if (xgip_end_of_chunk(&incoming_xgip) || !xgip_is_chunked(&incoming_xgip)) {
                // Send power-on commands
                xgip_reset(&outgoing_xgip);
                xgip_set_attributes(&outgoing_xgip, GIP_POWER_MODE_DEVICE_CONFIG, 2, 1, false, 0);
                xgip_set_data(&outgoing_xgip, xb1_power_on, sizeof(xb1_power_on));
                queue_host_report(xgip_generate_packet(&outgoing_xgip),
                                 xgip_get_packet_length(&outgoing_xgip));

                xgip_reset(&outgoing_xgip);
                xgip_set_attributes(&outgoing_xgip, GIP_POWER_MODE_DEVICE_CONFIG, 3, 1, false, 0);
                xgip_set_data(&outgoing_xgip, xb1_power_on_single, sizeof(xb1_power_on_single));
                queue_host_report(xgip_generate_packet(&outgoing_xgip),
                                 xgip_get_packet_length(&outgoing_xgip));

                // Send rumble command to enable dongle
                xgip_reset(&outgoing_xgip);
                xgip_set_attributes(&outgoing_xgip, GIP_CMD_RUMBLE, 1, 0, false, 0);
                xgip_set_data(&outgoing_xgip, xb1_rumble_on, sizeof(xb1_rumble_on));
                queue_host_report(xgip_generate_packet(&outgoing_xgip),
                                 xgip_get_packet_length(&outgoing_xgip));

                dongle_ready = true;
                printf("[xbone_auth] Dongle ready!\n");
            }
            break;

        case GIP_AUTH:
        case GIP_FINAL_AUTH:
            // Forward auth response to console
            printf("[xbone_auth] Got auth response from controller! cmd=0x%02x chunked=%d eoc=%d datalen=%d totalchunk=%d hdr_len=%d hdr_chunk_start=%d actual_recv=%d\n",
                   cmd, xgip_is_chunked(&incoming_xgip),
                   xgip_end_of_chunk(&incoming_xgip),
                   xgip_get_data_length(&incoming_xgip),
                   incoming_xgip.total_chunk_length,
                   incoming_xgip.header.length,
                   incoming_xgip.header.chunk_start,
                   incoming_xgip.actual_data_received);
            if (!xgip_is_chunked(&incoming_xgip) ||
                (xgip_is_chunked(&incoming_xgip) && xgip_end_of_chunk(&incoming_xgip))) {

                printf("[xbone_auth] Forwarding auth response to console: len=%d seq=%d\n",
                       xgip_get_data_length(&incoming_xgip), xgip_get_sequence(&incoming_xgip));
                xbone_auth_set_data(xgip_get_data(&incoming_xgip),
                                   xgip_get_data_length(&incoming_xgip),
                                   xgip_get_sequence(&incoming_xgip),
                                   xgip_get_command(&incoming_xgip),
                                   XBONE_AUTH_SEND_DONGLE_TO_CONSOLE);
                xgip_reset(&incoming_xgip);
            }
            break;

        case GIP_ACK_RESPONSE:
        default:
            break;
    }
}

// Weak implementation if xinput_host doesn't provide this
__attribute__((weak))
bool tuh_xinput_send_report(uint8_t dev_addr, uint8_t instance,
                            uint8_t const* report, uint16_t len)
{
    (void)dev_addr;
    (void)instance;
    (void)report;
    (void)len;
    printf("[xbone_auth] ERROR: Using weak tuh_xinput_send_report stub!\n");
    return false;
}
