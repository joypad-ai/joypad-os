// nrf24l01.c - see nrf24l01.h for the API rationale.
//
// Every SPI transaction is [command byte][data...], clocked out over
// rf24g_hal_spi_xfer() with CSN framing handled by the HAL. The first byte
// clocked back on ANY command is STATUS -- nrf24_xfer() below returns it
// unconditionally so callers that need it (nrf24_r_rx_payload(), for the
// pipe number) don't need a separate NOP.
//
// Flash-safety: every function in this file is reachable from
// rf24g_host.c's radio ISR / dwell-alarm chain (register writes/reads
// happen throughout receive and retune), so every function here carries
// __not_in_flash_func -- not just the ones the ISR calls directly, since
// the attribute places only a function's own code in RAM, not its callees'.
// This is defense in depth, not fault avoidance -- see rf24g_host.c's file
// header for the full rationale.

#include "nrf24l01.h"
#include "rf24g_hal.h"
#include <string.h>   // memcmp, in nrf24_probe() only (core 0, never the ISR)

static uint8_t __not_in_flash_func(nrf24_xfer)(uint8_t cmd, const uint8_t* tx,
                                                uint8_t* rx, uint8_t len)
{
    uint8_t txbuf[NRF24_MAX_PAYLOAD + 1];
    uint8_t rxbuf[NRF24_MAX_PAYLOAD + 1];

    // Byte loops rather than memcpy/memset, and the destination pointers are
    // `volatile`-qualified -- NOT just plain byte loops. `len` is a runtime
    // value, so a naive "for (i) buf[i] = x[i]" reads like it must compile to
    // per-byte load/store pairs, but it doesn't: GCC's loop-idiom
    // recognition (-ftree-loop-distribute-patterns, on by default from -O2)
    // pattern-matches exactly that shape and silently rewrites it back into a
    // real call to __wrap_memcpy/__wrap_memset -- confirmed by disassembly,
    // reachable from the radio ISR. Qualifying the write side as `volatile`
    // defeats the idiom-recognition pass: a volatile store is an observable
    // side effect the compiler is not allowed to express as a bulk
    // memcpy/memset call, or to reorder/merge with its neighbours. At
    // len <= 32 the loop costs nothing worth measuring either way.
    //
    // If this function is touched again: re-verify with disassembly on BOTH
    // `pico` (RP2040/Cortex-M0+) and `pico2` (RP2350) builds -- the two
    // architectures' loop-idiom recognition thresholds differ, and a fixed
    // trip count is not immune (see sf30_protocol.h's sf30_identity()).
    volatile uint8_t *t = txbuf;
    t[0] = cmd;
    for (uint8_t i = 0; i < len; i++)
        t[1 + i] = tx ? tx[i] : 0xFF;   // 0xFF = dummy clock bytes for a read

    rf24g_hal_spi_xfer(txbuf, rxbuf, (size_t)len + 1);

    if (rx) {
        volatile uint8_t *r = rx;
        for (uint8_t i = 0; i < len; i++) r[i] = rxbuf[1 + i];
    }
    return rxbuf[0];   // STATUS, clocked out with the command byte itself
}

void __not_in_flash_func(nrf24_write_reg)(uint8_t reg, uint8_t val)
{
    nrf24_xfer((uint8_t)(NRF24_CMD_W_REGISTER | (reg & 0x1F)), &val, NULL, 1);
}

uint8_t __not_in_flash_func(nrf24_read_reg)(uint8_t reg)
{
    uint8_t val = 0;
    nrf24_xfer((uint8_t)(NRF24_CMD_R_REGISTER | (reg & 0x1F)), NULL, &val, 1);
    return val;
}

void __not_in_flash_func(nrf24_write_reg_buf)(uint8_t reg, const uint8_t* buf, uint8_t len)
{
    nrf24_xfer((uint8_t)(NRF24_CMD_W_REGISTER | (reg & 0x1F)), buf, NULL, len);
}

void __not_in_flash_func(nrf24_read_reg_buf)(uint8_t reg, uint8_t* buf, uint8_t len)
{
    nrf24_xfer((uint8_t)(NRF24_CMD_R_REGISTER | (reg & 0x1F)), NULL, buf, len);
}

uint8_t __not_in_flash_func(nrf24_nop)(void)
{
    return nrf24_xfer(NRF24_CMD_NOP, NULL, NULL, 0);
}

uint8_t __not_in_flash_func(nrf24_fifo_status)(void)
{
    return nrf24_read_reg(NRF24_REG_FIFO_STATUS);
}

void __not_in_flash_func(nrf24_flush_rx)(void)
{
    nrf24_xfer(NRF24_CMD_FLUSH_RX, NULL, NULL, 0);
}

void __not_in_flash_func(nrf24_flush_tx)(void)
{
    nrf24_xfer(NRF24_CMD_FLUSH_TX, NULL, NULL, 0);
}

uint8_t __not_in_flash_func(nrf24_r_rx_pl_wid)(void)
{
    uint8_t w = 0;
    nrf24_xfer(NRF24_CMD_R_RX_PL_WID, NULL, &w, 1);
    return w;
}

uint8_t __not_in_flash_func(nrf24_r_rx_payload)(uint8_t* buf, uint8_t len)
{
    return nrf24_xfer(NRF24_CMD_R_RX_PAYLOAD, NULL, buf, len);
}

void __not_in_flash_func(nrf24_w_tx_payload)(const uint8_t* buf, uint8_t len)
{
    nrf24_xfer(NRF24_CMD_W_TX_PAYLOAD, buf, NULL, len);
}

void __not_in_flash_func(nrf24_activate)(void)
{
    uint8_t code = 0x73;
    nrf24_xfer(NRF24_CMD_ACTIVATE, &code, NULL, 1);
}

void __not_in_flash_func(nrf24_set_channel)(uint8_t ch)
{
    nrf24_write_reg(NRF24_REG_RF_CH, ch);
}

void __not_in_flash_func(nrf24_power_up_rx)(void)
{
    // CONFIG 0x3F: PWR_UP + PRIM_RX + 16-bit CRC, with RX_DR left UNMASKED.
    // The OEM dongle uses 0x7F (everything masked) because it polls STATUS
    // in a tight loop instead of using the IRQ line at all. This driver is
    // interrupt-driven (rf24g_host.c), so RX_DR has to actually reach the
    // IRQ pin. TX_DS/MAX_RT stay masked; nothing here watches them while
    // receiving.
    nrf24_write_reg(NRF24_REG_CONFIG,
                     (uint8_t)(NRF24_CONFIG_MASK_TX_DS | NRF24_CONFIG_MASK_MAX_RT |
                               NRF24_CONFIG_EN_CRC | NRF24_CONFIG_CRCO |
                               NRF24_CONFIG_PWR_UP | NRF24_CONFIG_PRIM_RX));
}

void __not_in_flash_func(nrf24_power_up_tx)(void)
{
    // CONFIG 0x7E: PTX, everything masked -- matches the dongle's own
    // pairing-reply CONFIG write. The pairing state machine polls its own
    // timing deadlines rather than TX_DS/MAX_RT -- see rf24g_host.c's
    // pairing_send_reply().
    nrf24_write_reg(NRF24_REG_CONFIG,
                     (uint8_t)(NRF24_CONFIG_MASK_RX_DR | NRF24_CONFIG_MASK_TX_DS |
                               NRF24_CONFIG_MASK_MAX_RT | NRF24_CONFIG_EN_CRC |
                               NRF24_CONFIG_CRCO | NRF24_CONFIG_PWR_UP));
}

void __not_in_flash_func(nrf24_power_down)(void)
{
    nrf24_write_reg(NRF24_REG_CONFIG,
                     (uint8_t)(NRF24_CONFIG_EN_CRC | NRF24_CONFIG_CRCO));
}

bool nrf24_probe(void)
{
    static const uint8_t pattern[NRF24_ADDR_LEN] = { 0xC2, 0xC2, 0xC2, 0xC2, 0xC2 };
    uint8_t readback[NRF24_ADDR_LEN] = { 0 };

    nrf24_write_reg_buf(NRF24_REG_RX_ADDR_P1, pattern, NRF24_ADDR_LEN);
    nrf24_read_reg_buf(NRF24_REG_RX_ADDR_P1, readback, NRF24_ADDR_LEN);

    return memcmp(pattern, readback, NRF24_ADDR_LEN) == 0;
}
