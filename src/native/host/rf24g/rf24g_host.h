// rf24g_host.h - SF30 2.4G wireless dongle emulation, native host driver
//
// Drives an nRF24L01+ over SPI to impersonate the 8BitDo SF30 2.4G dongle
// well enough that SF30 2.4G controllers pair and link to it, and presents
// them to the router as native gamepad input. Protocol details (radio
// config, hop table, framing, pairing rendezvous) are ground truth
// recovered by a logic-analyser tap on an original dongle -- see
// sf30_protocol.h.
//
// Physical: nRF24L01+ module on spi0, exactly one paired controller, on
// nRF24 pipe 0. The receiver's own identity is derived from the board's
// unique ID rather than persisted to flash -- it comes out the same on
// every boot by construction, so pairing survives a reboot or reflash of
// the same board but not a move to a different one.
//
// Supports exactly one controller by design: USB output only ever surfaces
// player index 0 (see app.h's USB_OUTPUT_PORTS), and two controllers
// sharing the same 64-entry hop table at independent phase can starve each
// other's dwell indefinitely if they power on close in phase.

#ifndef RF24G_HOST_H
#define RF24G_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_interface.h"

// dev_addr this driver owns. 0xC0/0xD0/0xE0/0xF0 are already claimed by
// other native hosts (Wii, GC/UART, N64/3DO/PSX/Jaguar, SNES/LodgeNet/PCE
// respectively).
#define RF24G_DEV_ADDR 0xB0

// Default GPIO pins (can be overridden by app.h before #include). Clear of
// GP23/24/25/29, which the CYW43 module claims on _w boards. SCK/MOSI/MISO
// are spi0's native pinout -- see rf24g_hal_rp2040.c.
#ifndef RF24G_PIN_MISO
#define RF24G_PIN_MISO 0
#endif
#ifndef RF24G_PIN_CE
#define RF24G_PIN_CE 4
#endif
#ifndef RF24G_PIN_CSN
#define RF24G_PIN_CSN 5
#endif
#ifndef RF24G_PIN_SCK
#define RF24G_PIN_SCK 6
#endif
#ifndef RF24G_PIN_MOSI
#define RF24G_PIN_MOSI 7
#endif
#ifndef RF24G_PIN_IRQ
#define RF24G_PIN_IRQ 8
#endif

// Initialize with the default pins above.
void rf24g_host_init(void);

// Initialize with a custom pin set.
void rf24g_host_init_pins(uint8_t sck, uint8_t mosi, uint8_t miso,
                          uint8_t csn, uint8_t ce, uint8_t irq);

// Core-0 task: drains decoded button events and submits them to the router,
// confirms/rolls back a provisional pairing claim, and updates per-second
// stats. Does NO radio I/O -- the radio runs entirely off interrupts, see
// rf24g_host.c's file header.
void rf24g_host_task(void);

// True if the controller is currently linked.
bool rf24g_host_is_connected(void);

// 1 if the controller is currently linked, 0 otherwise.
uint8_t rf24g_host_get_device_count(void);

// Begin the pairing rendezvous, offering the receiver's identity to
// whichever controller is held in pairing mode (Select ~3s, LED blinking
// rapidly). Returns false if a pairing attempt is already in progress.
bool rf24g_host_begin_pairing(void);

// True while a pairing rendezvous is in progress.
bool rf24g_host_is_pairing(void);

// Current link state, for diagnostics ("PARK", "SEARCH", "LINKED", "PAIRING").
const char* rf24g_host_state_name(void);

// printf diagnostics: link state, current channel, and the linked
// controller's packets/sec + inter-arrival histogram.
void rf24g_host_print_stats(void);

extern const InputInterface rf24g_input_interface;

#endif // RF24G_HOST_H
