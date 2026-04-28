// platform_sd_rp2040.c - RP2040 SD-over-SPI driver
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Robert Dale Smith
//
// Bit-perfect SD SPI-mode driver, implementing platform_sd.h.
// Following the SDA Physical Layer Simplified Spec section 7.
//
// Init flow:
//   1. Set SPI clock to ~100-400 kHz (cards must boot here)
//   2. Send 80+ dummy clocks with CS deasserted (power-up)
//   3. CMD0 (GO_IDLE) → expect R1=0x01 (idle)
//   4. CMD8 (SEND_IF_COND) → distinguishes v1 vs v2+
//   5. Loop ACMD41 (SD_SEND_OP_COND) until card exits idle
//   6. CMD58 (READ_OCR) → check CCS bit (block-addressed vs byte-addressed)
//   7. Bump SPI clock to run_freq_hz
// Read/write use CMD17/24 (single-block) and CMD18/25 (multi-block).

#include "platform/platform_sd.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// SD command numbers (subset we use).
#define CMD0   (0x40 | 0)   // GO_IDLE_STATE
#define CMD1   (0x40 | 1)   // SEND_OP_COND (legacy MMC/SDSC)
#define CMD8   (0x40 | 8)   // SEND_IF_COND (v2+ identification)
#define CMD9   (0x40 | 9)   // SEND_CSD
#define CMD12  (0x40 | 12)  // STOP_TRANSMISSION
#define CMD16  (0x40 | 16)  // SET_BLOCKLEN
#define CMD17  (0x40 | 17)  // READ_SINGLE_BLOCK
#define CMD18  (0x40 | 18)  // READ_MULTIPLE_BLOCK
#define CMD24  (0x40 | 24)  // WRITE_BLOCK
#define CMD25  (0x40 | 25)  // WRITE_MULTIPLE_BLOCK
#define CMD55  (0x40 | 55)  // APP_CMD prefix
#define CMD58  (0x40 | 58)  // READ_OCR
#define ACMD41 (0x40 | 41)  // SD_SEND_OP_COND

// Data tokens.
#define TOKEN_BLOCK_START          0xFE  // single-block + first multi-block
#define TOKEN_MULTI_BLOCK_START    0xFC
#define TOKEN_MULTI_BLOCK_STOP     0xFD

#define DATA_RESP_MASK             0x1F
#define DATA_RESP_OK               0x05

#define CARD_TYPE_SDSC             0x01  // byte-addressed (uses byte * 512 in cmds)
#define CARD_TYPE_SDHC             0x02  // block-addressed (uses LBA directly)

struct platform_sd {
    spi_inst_t* spi;
    uint8_t sck, mosi, miso, cs, cd;
    uint32_t run_freq_hz;
    uint8_t card_type;
    uint32_t block_count;
    bool initialized;
};

static struct platform_sd g_sd_instance;

static inline void cs_low(struct platform_sd* d)  { gpio_put(d->cs, 0); }
static inline void cs_high(struct platform_sd* d) { gpio_put(d->cs, 1); }

static uint8_t spi_xfer_byte(struct platform_sd* d, uint8_t v) {
    uint8_t rx;
    spi_write_read_blocking(d->spi, &v, &rx, 1);
    return rx;
}

static void spi_send_dummy_clocks(struct platform_sd* d, int n) {
    uint8_t ff = 0xFF;
    for (int i = 0; i < n; i++) {
        spi_write_blocking(d->spi, &ff, 1);
    }
}

// Send a command and read R1 response. Returns 0xFF on timeout.
static uint8_t sd_send_cmd(struct platform_sd* d, uint8_t cmd, uint32_t arg) {
    // Drain any pending busy state.
    spi_xfer_byte(d, 0xFF);

    uint8_t pkt[6] = {
        cmd,
        (uint8_t)(arg >> 24),
        (uint8_t)(arg >> 16),
        (uint8_t)(arg >> 8),
        (uint8_t)(arg & 0xFF),
        0x01,
    };
    // CRC only required for CMD0 (0x95) and CMD8 (0x87); other cards
    // typically ignore it in SPI mode but we send the right value anyway.
    if (cmd == CMD0)      pkt[5] = 0x95;
    else if (cmd == CMD8) pkt[5] = 0x87;

    spi_write_blocking(d->spi, pkt, 6);

    // Read R1 (first byte with bit 7 cleared, up to 16 tries).
    uint8_t r;
    for (int i = 0; i < 16; i++) {
        r = spi_xfer_byte(d, 0xFF);
        if (!(r & 0x80)) return r;
    }
    return 0xFF;
}

static uint8_t sd_send_acmd(struct platform_sd* d, uint8_t cmd, uint32_t arg) {
    sd_send_cmd(d, CMD55, 0);
    return sd_send_cmd(d, cmd, arg);
}

// Wait for a data start token (0xFE) — returns true on success.
static bool wait_token(struct platform_sd* d, uint8_t want, uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!time_reached(deadline)) {
        uint8_t t = spi_xfer_byte(d, 0xFF);
        if (t == want) return true;
        if (t != 0xFF) {
            // Some other byte (likely an error token).
            return false;
        }
    }
    return false;
}

static bool wait_not_busy(struct platform_sd* d, uint32_t timeout_ms) {
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    while (!time_reached(deadline)) {
        if (spi_xfer_byte(d, 0xFF) == 0xFF) return true;
    }
    return false;
}

static uint32_t parse_csd_capacity(const uint8_t csd[16]) {
    // CSD v2 (SDHC/SDXC): C_SIZE in bits 48..69, capacity = (C_SIZE+1) * 512KB
    if ((csd[0] >> 6) == 0x01) {
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3F) << 16)
                        | ((uint32_t)csd[8] << 8)
                        | csd[9];
        return (c_size + 1) * 1024;  // in 512-byte blocks
    }
    // CSD v1 (SDSC): full formula. For tiny cards, rare to encounter.
    uint32_t c_size = (((uint32_t)(csd[6] & 0x03)) << 10)
                    | ((uint32_t)csd[7] << 2)
                    | (csd[8] >> 6);
    uint32_t c_mult = ((csd[9] & 0x03) << 1) | (csd[10] >> 7);
    uint32_t read_bl_len = csd[5] & 0x0F;
    uint32_t blocks = (c_size + 1) << (c_mult + 2);
    blocks = (blocks << read_bl_len) >> 9;
    return blocks;
}

platform_sd_t platform_sd_init(const platform_sd_config_t* cfg)
{
    if (!cfg) return NULL;
    struct platform_sd* d = &g_sd_instance;
    memset(d, 0, sizeof(*d));

    d->spi = (cfg->spi_inst == 0) ? spi0 : spi1;
    d->sck = cfg->sck_pin;
    d->mosi = cfg->mosi_pin;
    d->miso = cfg->miso_pin;
    d->cs = cfg->cs_pin;
    d->cd = cfg->cd_pin;
    d->run_freq_hz = cfg->run_freq_hz ? cfg->run_freq_hz : 12500000;

    // SPI pins — alternate function.
    spi_init(d->spi, cfg->init_freq_hz ? cfg->init_freq_hz : 200000);
    gpio_set_function(d->sck,  GPIO_FUNC_SPI);
    gpio_set_function(d->mosi, GPIO_FUNC_SPI);
    gpio_set_function(d->miso, GPIO_FUNC_SPI);

    // CS as GPIO (not SPI peripheral CS — we toggle it manually for
    // multi-byte transactions).
    gpio_init(d->cs);
    gpio_set_dir(d->cs, GPIO_OUT);
    gpio_put(d->cs, 1);

    if (d->cd != PLATFORM_SD_NO_CD) {
        gpio_init(d->cd);
        gpio_set_dir(d->cd, GPIO_IN);
        gpio_pull_up(d->cd);
    }

    // 80+ clocks with CS HIGH so the card enters native SPI mode.
    spi_send_dummy_clocks(d, 12);

    cs_low(d);
    spi_send_dummy_clocks(d, 1);

    // CMD0: enter idle state.
    uint8_t r1 = sd_send_cmd(d, CMD0, 0);
    if (r1 != 0x01) {
        printf("[sd] CMD0 failed (r1=0x%02X) — no card?\n", r1);
        cs_high(d);
        return NULL;
    }

    // CMD8: voltage check (v2+ cards ack with R7).
    bool sdv2 = false;
    r1 = sd_send_cmd(d, CMD8, 0x000001AA);
    if (r1 == 0x01) {
        uint8_t r7[4] = {0};
        for (int i = 0; i < 4; i++) r7[i] = spi_xfer_byte(d, 0xFF);
        if (r7[2] == 0x01 && r7[3] == 0xAA) sdv2 = true;
    }

    // ACMD41 loop until card leaves idle.
    absolute_time_t deadline = make_timeout_time_ms(2000);
    do {
        r1 = sd_send_acmd(d, ACMD41, sdv2 ? 0x40000000 : 0);
        if (time_reached(deadline)) {
            printf("[sd] ACMD41 timeout\n");
            cs_high(d);
            return NULL;
        }
    } while (r1 != 0x00);

    // CMD58: read OCR — check CCS bit for SDHC vs SDSC.
    d->card_type = CARD_TYPE_SDSC;
    r1 = sd_send_cmd(d, CMD58, 0);
    if (r1 == 0x00) {
        uint8_t ocr[4] = {0};
        for (int i = 0; i < 4; i++) ocr[i] = spi_xfer_byte(d, 0xFF);
        if (ocr[0] & 0x40) d->card_type = CARD_TYPE_SDHC;
    }

    // SDSC needs explicit 512-byte block length (SDHC ignores).
    if (d->card_type == CARD_TYPE_SDSC) {
        sd_send_cmd(d, CMD16, 512);
    }

    // CMD9: read CSD for capacity.
    r1 = sd_send_cmd(d, CMD9, 0);
    if (r1 == 0x00 && wait_token(d, TOKEN_BLOCK_START, 200)) {
        uint8_t csd[16];
        for (int i = 0; i < 16; i++) csd[i] = spi_xfer_byte(d, 0xFF);
        spi_xfer_byte(d, 0xFF);  // CRC byte 1
        spi_xfer_byte(d, 0xFF);  // CRC byte 2
        d->block_count = parse_csd_capacity(csd);
    }

    cs_high(d);
    spi_send_dummy_clocks(d, 1);

    // Bump to run frequency now that init is done.
    spi_set_baudrate(d->spi, d->run_freq_hz);

    d->initialized = true;
    printf("[sd] %s card, %lu blocks (%lu MB)\n",
           (d->card_type == CARD_TYPE_SDHC) ? "SDHC" : "SDSC",
           (unsigned long)d->block_count,
           (unsigned long)((d->block_count * 512ULL) / (1024UL * 1024UL)));
    return d;
}

bool platform_sd_present(platform_sd_t dev)
{
    if (!dev) return false;
    if (dev->cd != PLATFORM_SD_NO_CD) {
        // Most SD slots: switch closes when card inserted = pulls pin LOW.
        return gpio_get(dev->cd) == 0;
    }
    return dev->initialized;
}

uint32_t platform_sd_block_count(platform_sd_t dev)
{
    return dev ? dev->block_count : 0;
}

static uint32_t to_address(struct platform_sd* d, uint32_t lba) {
    return (d->card_type == CARD_TYPE_SDHC) ? lba : (lba * 512U);
}

int platform_sd_read_blocks(platform_sd_t dev, uint32_t lba,
                            uint8_t* buf, uint32_t count)
{
    if (!dev || !dev->initialized || !buf || count == 0) return -1;
    cs_low(dev);
    int rc = -1;

    if (count == 1) {
        if (sd_send_cmd(dev, CMD17, to_address(dev, lba)) != 0x00) goto done;
        if (!wait_token(dev, TOKEN_BLOCK_START, 200)) goto done;
        spi_read_blocking(dev->spi, 0xFF, buf, 512);
        spi_xfer_byte(dev, 0xFF);  // CRC
        spi_xfer_byte(dev, 0xFF);
        rc = 0;
    } else {
        if (sd_send_cmd(dev, CMD18, to_address(dev, lba)) != 0x00) goto done;
        for (uint32_t i = 0; i < count; i++) {
            if (!wait_token(dev, TOKEN_BLOCK_START, 200)) goto done;
            spi_read_blocking(dev->spi, 0xFF, buf + i * 512, 512);
            spi_xfer_byte(dev, 0xFF);
            spi_xfer_byte(dev, 0xFF);
        }
        if (sd_send_cmd(dev, CMD12, 0) > 0x01) goto done;
        rc = 0;
    }
done:
    cs_high(dev);
    spi_send_dummy_clocks(dev, 1);
    return rc;
}

int platform_sd_write_blocks(platform_sd_t dev, uint32_t lba,
                             const uint8_t* buf, uint32_t count)
{
    if (!dev || !dev->initialized || !buf || count == 0) return -1;
    cs_low(dev);
    int rc = -1;

    if (count == 1) {
        if (sd_send_cmd(dev, CMD24, to_address(dev, lba)) != 0x00) goto done;
        spi_send_dummy_clocks(dev, 1);
        uint8_t tok = TOKEN_BLOCK_START;
        spi_write_blocking(dev->spi, &tok, 1);
        spi_write_blocking(dev->spi, buf, 512);
        spi_xfer_byte(dev, 0xFF);  // dummy CRC
        spi_xfer_byte(dev, 0xFF);
        uint8_t resp = spi_xfer_byte(dev, 0xFF);
        if ((resp & DATA_RESP_MASK) != DATA_RESP_OK) goto done;
        if (!wait_not_busy(dev, 500)) goto done;
        rc = 0;
    } else {
        if (sd_send_cmd(dev, CMD25, to_address(dev, lba)) != 0x00) goto done;
        spi_send_dummy_clocks(dev, 1);
        for (uint32_t i = 0; i < count; i++) {
            uint8_t tok = TOKEN_MULTI_BLOCK_START;
            spi_write_blocking(dev->spi, &tok, 1);
            spi_write_blocking(dev->spi, buf + i * 512, 512);
            spi_xfer_byte(dev, 0xFF);
            spi_xfer_byte(dev, 0xFF);
            uint8_t resp = spi_xfer_byte(dev, 0xFF);
            if ((resp & DATA_RESP_MASK) != DATA_RESP_OK) goto done;
            if (!wait_not_busy(dev, 500)) goto done;
        }
        uint8_t stop = TOKEN_MULTI_BLOCK_STOP;
        spi_write_blocking(dev->spi, &stop, 1);
        if (!wait_not_busy(dev, 500)) goto done;
        rc = 0;
    }
done:
    cs_high(dev);
    spi_send_dummy_clocks(dev, 1);
    return rc;
}

int platform_sd_sync(platform_sd_t dev)
{
    if (!dev || !dev->initialized) return -1;
    // SPI-mode sync = wait for the card to release DO from busy.
    cs_low(dev);
    bool ok = wait_not_busy(dev, 500);
    cs_high(dev);
    spi_send_dummy_clocks(dev, 1);
    return ok ? 0 : -1;
}
