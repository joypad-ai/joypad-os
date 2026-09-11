// nrf24l01.h - nRF24L01+ register/command driver
//
// Thin register- and command-level access, modelled on
// src/apps/gc2eth_feather/w5500.c's shape (a handful of xfer/read/write
// primitives, no protocol knowledge). All hardware access goes through
// rf24g_hal.h -- this file has no pico-sdk (or other platform SDK)
// dependency, so it works unchanged on any HAL implementation.
//
// Register map and command set: nRF24L01+ datasheet. This chip family also
// covers the BK2425 clone used by the OEM 8BitDo dongle for register-dump
// comparison purposes, and the ACTIVATE unlock command some clones require
// to make FEATURE/DYNPD writable -- see nrf24_activate() and rf24g_host.c's
// FEATURE=0x06 comment.

#ifndef NRF24L01_H
#define NRF24L01_H

#include <stdint.h>
#include <stdbool.h>

// ---- Register map -------------------------------------------------------
#define NRF24_REG_CONFIG       0x00
#define NRF24_REG_EN_AA        0x01
#define NRF24_REG_EN_RXADDR    0x02
#define NRF24_REG_SETUP_AW     0x03
#define NRF24_REG_SETUP_RETR   0x04
#define NRF24_REG_RF_CH        0x05
#define NRF24_REG_RF_SETUP     0x06
#define NRF24_REG_STATUS       0x07
#define NRF24_REG_OBSERVE_TX   0x08
#define NRF24_REG_RPD          0x09
#define NRF24_REG_RX_ADDR_P0   0x0A   // 5 bytes
#define NRF24_REG_RX_ADDR_P1   0x0B   // 5 bytes
#define NRF24_REG_RX_ADDR_P2   0x0C   // 1 byte -- shares P1's upper 4 bytes
#define NRF24_REG_RX_ADDR_P3   0x0D   // 1 byte -- shares P1's upper 4 bytes
#define NRF24_REG_RX_ADDR_P4   0x0E   // 1 byte -- shares P1's upper 4 bytes
#define NRF24_REG_RX_ADDR_P5   0x0F   // 1 byte -- shares P1's upper 4 bytes
#define NRF24_REG_TX_ADDR      0x10   // 5 bytes
#define NRF24_REG_RX_PW_P0     0x11   // RX_PW_P1..P5 follow at +1 each
#define NRF24_REG_FIFO_STATUS  0x17
#define NRF24_REG_DYNPD        0x1C
#define NRF24_REG_FEATURE      0x1D

// CONFIG bits
#define NRF24_CONFIG_MASK_RX_DR  0x40
#define NRF24_CONFIG_MASK_TX_DS  0x20
#define NRF24_CONFIG_MASK_MAX_RT 0x10
#define NRF24_CONFIG_EN_CRC      0x08
#define NRF24_CONFIG_CRCO        0x04
#define NRF24_CONFIG_PWR_UP      0x02
#define NRF24_CONFIG_PRIM_RX     0x01

// STATUS bits
#define NRF24_STATUS_RX_DR         0x40
#define NRF24_STATUS_TX_DS         0x20
#define NRF24_STATUS_MAX_RT        0x10
#define NRF24_STATUS_RX_P_NO_MASK  0x0E
#define NRF24_STATUS_RX_P_NO_SHIFT 1
#define NRF24_STATUS_TX_FULL       0x01

// FIFO_STATUS bits
#define NRF24_FIFO_RX_EMPTY  0x01
#define NRF24_FIFO_TX_EMPTY  0x10

// FEATURE bits
#define NRF24_FEATURE_EN_DPL     0x04
#define NRF24_FEATURE_EN_ACK_PAY 0x02
#define NRF24_FEATURE_EN_DYN_ACK 0x01   // do NOT set this -- see rf24g_host.c

// ---- SPI command set ------------------------------------------------------
#define NRF24_CMD_R_REGISTER    0x00   // | reg (5 bit)
#define NRF24_CMD_W_REGISTER    0x20   // | reg (5 bit)
#define NRF24_CMD_R_RX_PAYLOAD  0x61
#define NRF24_CMD_W_TX_PAYLOAD  0xA0
#define NRF24_CMD_FLUSH_TX      0xE1
#define NRF24_CMD_FLUSH_RX      0xE2
#define NRF24_CMD_REUSE_TX_PL   0xE3
#define NRF24_CMD_R_RX_PL_WID   0x60
#define NRF24_CMD_ACTIVATE      0x50
#define NRF24_CMD_NOP           0xFF

#define NRF24_ADDR_LEN     5
#define NRF24_MAX_PAYLOAD 32

// ---- Register access ------------------------------------------------------
void    nrf24_write_reg(uint8_t reg, uint8_t val);
uint8_t nrf24_read_reg(uint8_t reg);
void    nrf24_write_reg_buf(uint8_t reg, const uint8_t* buf, uint8_t len);
void    nrf24_read_reg_buf(uint8_t reg, uint8_t* buf, uint8_t len);

// Clock a NOP -- the cheapest way to read STATUS (every command's first
// clocked-out byte is STATUS; NOP just doesn't do anything else).
uint8_t nrf24_nop(void);
uint8_t nrf24_fifo_status(void);

void    nrf24_flush_rx(void);
void    nrf24_flush_tx(void);

// Dynamic-payload width of the packet on top of the RX FIFO (R_RX_PL_WID).
// Returns 0 if the FIFO is empty; a corrupt read can return >32, which the
// datasheet's errata says to treat as "flush and discard".
uint8_t nrf24_r_rx_pl_wid(void);

// Pop `len` bytes off the RX FIFO. Returns STATUS as it stood on entry --
// bits 3:1 (NRF24_STATUS_RX_P_NO_MASK) name the pipe this payload arrived
// on, captured before the FIFO pop completes.
uint8_t nrf24_r_rx_payload(uint8_t* buf, uint8_t len);

void    nrf24_w_tx_payload(const uint8_t* buf, uint8_t len);

// ACTIVATE 0x50 0x73 -- unlocks FEATURE/DYNPD on chip families that ship
// them write-protected until toggled once. Harmless on a real nRF24L01+,
// where FEATURE/DYNPD are always writable; required on some clones.
void    nrf24_activate(void);

void    nrf24_set_channel(uint8_t ch);

// CONFIG transitions. PRX (power_up_rx) is the normal listening mode; PTX
// (power_up_tx) is only entered for the pairing rendezvous reply burst, see
// rf24g_host.c's pairing state machine.
void    nrf24_power_up_rx(void);
void    nrf24_power_up_tx(void);
void    nrf24_power_down(void);

// Presence check: writes a scratch pattern to RX_ADDR_P1 (5 bytes, cheap to
// clobber -- every caller rewrites every pipe address before listening
// anyway) and reads it back. There is no VERSIONR-equivalent register on
// this chip family to check against a known value, unlike w5500_init()'s
// VERSIONR read.
bool    nrf24_probe(void);

#endif // NRF24L01_H
