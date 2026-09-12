// ps4_auth_link.c - Dual-chip PS4/DS4 auth passthrough bridge. See header.
#include "ps4_auth_link.h"
#include "uart_peer.h"
#include <string.h>
#include <stdio.h>

// Auth-trace logging. Both MCUs route stdio to uart0/GP0 (the peer link is on
// uart1/GP24-25), and A's uart0 is independent of USB CDC — so these lines are
// visible over a 115200 serial tap on GP0 even in PS4 mode (which has no CDC).
// Logs only handshake EDGES (not every status poll) to stay near-silent during
// gameplay. Tap A's GP0 to see the console side, B's GP0 for the DS4 side.
#define PS4L_LOG(...) printf(__VA_ARGS__)

#ifndef DISABLE_USB_HOST
#include "usb/usbh/hid/devices/vendors/sony/sony_ds4.h"  // ds4_auth_* (host side B)
#endif

#define PS4L_PAGE  56
#define PS4L_NPAGES 19               // signature pages
#define PS4L_SIG   (PS4L_PAGE * PS4L_NPAGES)  // 1064

// Report IDs (match ps4_mode / GP2040).
#define PS4L_F1 0xF1
#define PS4L_F2 0xF2

// Standard reflected CRC-32 (poly 0xEDB88320, init/xor 0xFFFFFFFF), computed
// over [report_id .. payload] — identical to what the console validates.
static uint32_t ps4l_crc32(uint8_t report_id, const uint8_t* data, uint16_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t first = report_id;
    for (int pass = 0; pass < 2; pass++) {
        const uint8_t* p = (pass == 0) ? &first : data;
        uint16_t n = (pass == 0) ? 1 : len;
        for (uint16_t i = 0; i < n; i++) {
            crc ^= p[i];
            for (int b = 0; b < 8; b++)
                crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

// ---- Device side (A) state: the signature B streams back, served to console ----
static uint8_t a_sig[PS4L_SIG];
static uint8_t a_nonce_id;
static bool    a_ready;
static uint8_t a_page_returning;
static bool    a_diag_buzzed;   // diag: buzzed B once when console read the full sig this round

void ps4_auth_link_device_send_nonce(const uint8_t* data, uint16_t len) {
    // console 0xF0 payload: [nonce_id][page][0][nonce_chunk(56)]
    if (len < 59) return;
    uint8_t nid = data[0], page = data[1];
    if (page == 0) { a_ready = false; a_page_returning = 0; a_diag_buzzed = false;
        PS4L_LOG("[PS4L-A] console nonce id=%u START (re)auth\n", nid); }
    if (page == 4) PS4L_LOG("[PS4L-A] console nonce id=%u all 5 pages relayed->B\n", nid);
    a_nonce_id = nid;
    uint8_t f[2 + PS4L_PAGE];
    f[0] = nid; f[1] = page;
    memcpy(&f[2], &data[3], PS4L_PAGE);
    uart_peer_send_frame(UART_PEER_MSG_PS4_NONCE, f, sizeof(f));
}

void ps4_auth_link_device_reset(void) {
    a_ready = false;
    a_page_returning = 0;
    PS4L_LOG("[PS4L-A] console 0xF3 reset\n");
    uart_peer_send_frame(UART_PEER_MSG_PS4_RESET, NULL, 0);
}

uint16_t ps4_auth_link_get_signature(uint8_t* buf, uint16_t max_len) {
    if (max_len < 63) return 0;
    memset(buf, 0, max_len);
    uint8_t page = a_page_returning;
    buf[0] = a_nonce_id; buf[1] = page; buf[2] = 0;
    if (a_ready && page < PS4L_NPAGES)
        memcpy(&buf[3], &a_sig[page * PS4L_PAGE], PS4L_PAGE);
    if (page == 0) PS4L_LOG("[PS4L-A] console fetching 0xF1 sig (ready=%u)\n", a_ready);
    if (page == PS4L_NPAGES - 1)
        PS4L_LOG("[PS4L-A] console fetched last 0xF1 page (ready=%u)\n", a_ready);
    uint32_t crc = ps4l_crc32(PS4L_F1, buf, 59);
    buf[59] = (uint8_t)(crc);       buf[60] = (uint8_t)(crc >> 8);
    buf[61] = (uint8_t)(crc >> 16); buf[62] = (uint8_t)(crc >> 24);
    // Only advance once the signature is actually present. Across the link the
    // console can poll 0xF1 during the latency window before B has streamed the
    // signature; advancing then would desync the page sequence (console gets
    // blanks/out-of-order pages -> rejects -> retry storm). Hold page 0 until
    // ready, then walk 0..18 and hold the last page for retries.
    if (a_ready && page < PS4L_NPAGES - 1) a_page_returning++;
    return 63;
}

uint16_t ps4_auth_link_get_status(uint8_t* buf, uint16_t max_len) {
    if (max_len < 15) return 0;
    memset(buf, 0, max_len);
    buf[0] = a_nonce_id;
    buf[1] = a_ready ? 0 : 16;   // 0 = ready, 16 = still signing
    static int last_logged = -1;  // log only when the status the console sees flips
    int cur = a_ready ? 0 : 16;
    if (cur != last_logged) {
        PS4L_LOG("[PS4L-A] console 0xF2 status -> %s (id=%u)\n",
                 a_ready ? "READY" : "signing", a_nonce_id);
        last_logged = cur;
    }
    uint32_t crc = ps4l_crc32(PS4L_F2, buf, 11);
    buf[11] = (uint8_t)(crc);       buf[12] = (uint8_t)(crc >> 8);
    buf[13] = (uint8_t)(crc >> 16); buf[14] = (uint8_t)(crc >> 24);
    return 15;
}

// ---- Host side (B): stream the DS4's signed pages back to A once ready ----
void ps4_auth_link_host_task(void) {
#ifndef DISABLE_USB_HOST
    // -1 idle, 0..18 streaming a page per call, -2 done (until DS4 re-signs).
    static int send_page = -1;
    bool ready = ds4_auth_signature_ready();
    if (!ready) { send_page = -1; return; }   // re-arms for the next signing
    if (send_page == -1) {                     // just became ready -> start
        send_page = 0;
        PS4L_LOG("[PS4L-B] DS4 signed -> streaming 19 pages to A\n");
    }
    if (send_page >= 0 && send_page < PS4L_NPAGES) {
        uint8_t f[1 + PS4L_PAGE];
        f[0] = (uint8_t)send_page;
        ds4_auth_copy_raw_page((uint8_t)send_page, &f[1]);
        uart_peer_send_frame(UART_PEER_MSG_PS4_SIG, f, sizeof(f));
        send_page++;
        if (send_page == PS4L_NPAGES) {
            uint8_t nid = ds4_auth_get_nonce_id();
            uart_peer_send_frame(UART_PEER_MSG_PS4_READY, &nid, 1);
            PS4L_LOG("[PS4L-B] streamed sig + READY id=%u\n", nid);
            send_page = -2;   // done for this signature
        }
    }
#endif
}

void ps4_auth_link_on_frame(uint8_t type, const uint8_t* payload, uint16_t plen) {
    switch (type) {
#ifndef DISABLE_USB_HOST
        case UART_PEER_MSG_PS4_NONCE:   // B: feed relayed nonce page into the DS4
            if (plen >= 2 + PS4L_PAGE) {
                if (payload[1] == 0)
                    PS4L_LOG("[PS4L-B] got nonce id=%u -> feeding DS4\n", payload[0]);
                ds4_auth_feed_nonce_page(payload[0], payload[1], &payload[2]);
            }
            break;
        case UART_PEER_MSG_PS4_RESET:   // B: reset the DS4 handshake
            PS4L_LOG("[PS4L-B] reset DS4 handshake\n");
            ds4_auth_reset();
            break;
        case UART_PEER_MSG_PS4_DIAG_BUZZ:  // B: diag — console read full sig, pulse DS4
            PS4L_LOG("[PS4L-B] console read full sig -> diag pulse\n");
            ds4_auth_diag_pulse();
            break;
#endif
        case UART_PEER_MSG_PS4_SIG:     // A: store a signed page from B
            if (plen >= 1 + PS4L_PAGE && payload[0] < PS4L_NPAGES)
                memcpy(&a_sig[payload[0] * PS4L_PAGE], &payload[1], PS4L_PAGE);
            break;
        case UART_PEER_MSG_PS4_READY:   // A: signature complete
            if (plen >= 1) { a_nonce_id = payload[0]; a_ready = true; a_page_returning = 0;
                PS4L_LOG("[PS4L-A] B signature READY id=%u -> can serve console\n", payload[0]); }
            break;
        default:
            break;
    }
}
