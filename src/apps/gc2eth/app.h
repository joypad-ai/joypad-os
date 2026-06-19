// app.h — gc2eth (GameCube/GBA → Dolphin TCP via CH9120 Ethernet)
//
// See .dev/docs/gc2eth-ch9120-bridge.md for design + protocol notes.

#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_interface.h"
#include "core/output_interface.h"

#define APP_NAME    "GC2ETH"

#ifndef BOARD
#define BOARD "waveshare_rp2040_eth"
#endif

// GameCube data line (joybus). User wired GP2 on this board for the
// initial bring-up; long-term plan is to move it to GP28 (matches the
// gc2usb_pico convention and frees GP2 for SPI0 SCK if we ever swap
// the CH9120 for a W5500 board variant). Update CMakeLists's
// GC_PIN_DATA= as well when re-wiring.
#ifndef GC_PIN_DATA
#define GC_PIN_DATA  2
#endif
#define GC_DATA_PIN  GC_PIN_DATA

// CH9120 control pins (per Spotpear/Waveshare schematic for the
// CH9120 variant of RP2040-ETH).
#ifndef CH9120_UART_TX_PIN
#define CH9120_UART_TX_PIN  20  // RP2040 TX → CH9120 RXD
#endif
#ifndef CH9120_UART_RX_PIN
#define CH9120_UART_RX_PIN  21  // CH9120 TXD → RP2040 RX
#endif
#ifndef CH9120_TCPCS_PIN
#define CH9120_TCPCS_PIN    17  // CH9120 → RP2040 — ACTIVE LOW (low = TCP peer connected)
#endif
#ifndef CH9120_CFG0_PIN
#define CH9120_CFG0_PIN     18  // RP2040 → CH9120 (low = config mode)
#endif
#ifndef CH9120_RSTI_PIN
#define CH9120_RSTI_PIN     19  // RP2040 → CH9120 (active-low reset)
#endif

void app_init(void);
void app_task(void);

// Diagnostic snapshot — last seen Dolphin frame stats. Used by the
// CDC text command FRAMES? to expose runtime activity over the
// joypad-os CDC channel (which doesn't have log_streaming enabled
// by default, so a polled query is simpler than printf+streaming).
typedef struct {
    uint32_t frames_seen;
    uint8_t  last_cmd;
    int      last_n;
    uint8_t  last_rx[5];
} gc2eth_diag_t;
void gc2eth_get_diag(gc2eth_diag_t* out);

const InputInterface**  app_get_input_interfaces(uint8_t* count);
const OutputInterface** app_get_output_interfaces(uint8_t* count);

#endif // APP_H
