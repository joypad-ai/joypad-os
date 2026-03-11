// nuon_serial_device.h - Nuon Polyface Serial Adapter Device
//
// Emulates a Polyface TYPE=0x04 (SERIAL) peripheral on the Nuon controller port.
// Provides SDATA register (0x43) as a bidirectional FIFO bridge to USB CDC.

#ifndef NUON_SERIAL_DEVICE_H
#define NUON_SERIAL_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"
#include "polyface_read.pio.h"
#include "polyface_send.pio.h"
#include "pico/stdlib.h"

// GPIO pins (same as nuon_device.h)
#ifndef DATAIO_PIN
#define DATAIO_PIN        2
#endif
#define CLKIN_PIN         DATAIO_PIN + 1

// Polyface protocol constants
#define PACKET_TYPE_READ  1
#define PACKET_TYPE_WRITE 0

// PROBE identity: TYPE=3 (ORBIT chip version — same as all consumer Polyface)
// Device function (serial) is determined by MODE/CONFIG after branding, not TYPE.
#define NUONSER_DEFCFG    1
#define NUONSER_VERSION   11
#define NUONSER_TYPE      3       // ORBIT chip version (consumer Polyface)
#define NUONSER_MFG       0
#define NUONSER_CRC16     0x8005
#define NUONSER_MAGIC     0x4A554445  // "JUDE"

// Serial register addresses (Polyface 0x4? range)
#define NUONSER_REG_BAUD      0x40
#define NUONSER_REG_FLAGS0    0x41
#define NUONSER_REG_FLAGS1    0x42
#define NUONSER_REG_SDATA     0x43
#define NUONSER_REG_SSTATUS   0x44
#define NUONSER_REG_RSTATUS   0x45
#define NUONSER_REG_ERROR_CLR 0x46

// FIFO sizes
#define NUONSER_TX_FIFO_SIZE  256   // Nuon→PC direction (SDATA writes)
#define NUONSER_RX_FIFO_SIZE  32    // PC→Nuon direction (SDATA reads)

// SSTATUS bits
#define NUONSER_SOF_ERROR     (1 << 5)  // Send overflow

// RSTATUS bits
#define NUONSER_ROF_ERROR     (1 << 5)  // Receive overflow
#define NUONSER_RUF_ERROR     (1 << 6)  // Receive underflow (read when empty)

// ============================================================================
// TX FIFO (Nuon → PC): Nuon writes to SDATA, app reads and sends over CDC
// ============================================================================

// Read a byte from the TX FIFO (returns -1 if empty)
int nuonser_tx_read(void);

// Number of bytes available to read from TX FIFO
uint16_t nuonser_tx_available(void);

// ============================================================================
// RX FIFO (PC → Nuon): App writes from CDC, Nuon reads from SDATA
// ============================================================================

// Write a byte to the RX FIFO (returns false if full)
bool nuonser_rx_write(uint8_t byte);

// Number of bytes available for Nuon to read from RX FIFO
uint16_t nuonser_rx_available(void);

// ============================================================================
// Device interface
// ============================================================================

// Byte-reverse intrinsic (declared in nuon_device.h, provided by CMSIS)
extern uint32_t __rev(uint32_t);

void nuonser_init(void);
void __not_in_flash_func(nuonser_core1_task)(void);

// Output interface for app integration
#include "core/output_interface.h"
extern const OutputInterface nuon_serial_output_interface;

#endif // NUON_SERIAL_DEVICE_H
