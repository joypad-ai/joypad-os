// ps4_auth_link.h - Dual-chip PS4/DS4 auth passthrough bridge over uart_peer.
//
// The console-facing PS4 output mode runs on the DEVICE MCU (A); the genuine
// DualShock 4 that answers the auth challenge is on the HOST MCU (B). This
// module relays the handshake across the inter-MCU link (mirrors p5general_link):
//   A: forwards each console nonce page (0xF0) to B; serves 0xF1 signature /
//      0xF2 status back to the console from B's data, framed + CRC'd here.
//   B: feeds relayed nonce pages into the real DS4 (ds4_auth), and streams the
//      19 signed pages back to A once the DS4 has signed.
// The ds4_auth-driving parts compile only where USB host exists (B); the serve
// parts are used on A. Linking this on a single-chip build is inert.
#pragma once
#include <stdint.h>
#include <stdbool.h>

// --- Device side (A), called from ps4_mode ---
void     ps4_auth_link_device_send_nonce(const uint8_t* data, uint16_t len); // 0xF0 page in
void     ps4_auth_link_device_reset(void);                                    // 0xF3
uint16_t ps4_auth_link_get_signature(uint8_t* buf, uint16_t max_len);         // 0xF1 (+CRC)
uint16_t ps4_auth_link_get_status(uint8_t* buf, uint16_t max_len);            // 0xF2 (+CRC)

// --- Host side (B) ---
void ps4_auth_link_host_task(void);   // stream signed pages to A when DS4 is ready

// RX apply — strong override of the weak hook in uart_peer.c.
void ps4_auth_link_on_frame(uint8_t type, const uint8_t* payload, uint16_t plen);
