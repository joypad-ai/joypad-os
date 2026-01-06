// cdc.c - USB CDC (Virtual Serial Port) implementation
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "cdc.h"
#include "cdc_protocol.h"
#include "cdc_commands.h"
#include "../usbd.h"
#include "core/services/storage/flash.h"
#include "tusb.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if CFG_TUD_CDC > 0

// Command buffer for text commands (legacy support)
#define CMD_BUFFER_SIZE 64
static char cmd_buffer[CMD_BUFFER_SIZE];
static uint8_t cmd_pos = 0;

// Protocol mode detection
static bool binary_mode = false;  // Switch to binary after receiving 0xAA

// Debug output enabled flag (runtime toggle)
static bool debug_enabled = true;

// ============================================================================
// STDIO DRIVER (routes printf to CDC debug port)
// ============================================================================

#if CFG_TUD_CDC >= 2

static void cdc_stdio_out_chars(const char *buf, int len)
{
    if (!debug_enabled) return;
    if (!tud_cdc_n_connected(CDC_PORT_DEBUG)) return;

    // Write in chunks to avoid blocking
    int remaining = len;
    while (remaining > 0) {
        int available = (int)tud_cdc_n_write_available(CDC_PORT_DEBUG);
        if (available == 0) {
            tud_cdc_n_write_flush(CDC_PORT_DEBUG);
            break;  // Don't block waiting for space
        }
        int to_write = remaining < available ? remaining : available;
        int written = (int)tud_cdc_n_write(CDC_PORT_DEBUG, buf, to_write);
        buf += written;
        remaining -= written;
    }
    tud_cdc_n_write_flush(CDC_PORT_DEBUG);
}

static void cdc_stdio_out_flush(void)
{
    if (tud_cdc_n_connected(CDC_PORT_DEBUG)) {
        tud_cdc_n_write_flush(CDC_PORT_DEBUG);
    }
}

static stdio_driver_t cdc_stdio_driver = {
    .out_chars = cdc_stdio_out_chars,
    .out_flush = cdc_stdio_out_flush,
    .in_chars = NULL,  // No input on debug port
    .set_chars_available_callback = NULL,
    .next = NULL,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF,
#endif
};

#endif // CFG_TUD_CDC >= 2

// ============================================================================
// INITIALIZATION
// ============================================================================

void cdc_init(void)
{
    debug_enabled = true;
    binary_mode = false;

    // Initialize binary protocol command handlers
    cdc_commands_init();

#if CFG_TUD_CDC >= 2
    // Register CDC debug port as stdio output
    stdio_set_driver_enabled(&cdc_stdio_driver, true);
#endif
}

// Process a complete command line
static void cdc_process_command(const char* cmd)
{
    char response[128];

    // MODE? - Query current mode
    if (strcmp(cmd, "MODE?") == 0) {
        usb_output_mode_t mode = usbd_get_mode();
        snprintf(response, sizeof(response), "MODE=%d (%s)\r\n",
                 (int)mode, usbd_get_mode_name(mode));
        cdc_data_write_str(response);
    }
    // MODE=N - Set mode by number
    else if (strncmp(cmd, "MODE=", 5) == 0) {
        const char* value = cmd + 5;
        int mode_num = -1;

        // Try parsing as number first
        if (value[0] >= '0' && value[0] <= '9') {
            mode_num = atoi(value);
        }
        // Try parsing mode names
        else if (strcasecmp(value, "HID") == 0 || strcasecmp(value, "DINPUT") == 0) {
            mode_num = USB_OUTPUT_MODE_HID;
        }
        else if (strcasecmp(value, "XOG") == 0 || strcasecmp(value, "XBOX_OG") == 0 ||
                 strcasecmp(value, "XBOX") == 0) {
            mode_num = USB_OUTPUT_MODE_XBOX_ORIGINAL;
        }
        else if (strcasecmp(value, "XAC") == 0 || strcasecmp(value, "ADAPTIVE") == 0) {
            mode_num = USB_OUTPUT_MODE_XAC;
        }

        if (mode_num >= 0 && mode_num < USB_OUTPUT_MODE_COUNT) {
            usb_output_mode_t current = usbd_get_mode();
            if ((usb_output_mode_t)mode_num == current) {
                snprintf(response, sizeof(response), "OK: Already in mode %d (%s)\r\n",
                         mode_num, usbd_get_mode_name((usb_output_mode_t)mode_num));
                cdc_data_write_str(response);
            } else {
                snprintf(response, sizeof(response), "OK: Switching to mode %d (%s)...\r\n",
                         mode_num, usbd_get_mode_name((usb_output_mode_t)mode_num));
                cdc_data_write_str(response);
                cdc_data_flush();
                // This will trigger a device reset
                usbd_set_mode((usb_output_mode_t)mode_num);
            }
        } else {
            snprintf(response, sizeof(response), "ERR: Invalid mode '%s'\r\n", value);
            cdc_data_write_str(response);
        }
    }
    // MODES - List available modes
    else if (strcmp(cmd, "MODES") == 0 || strcmp(cmd, "MODES?") == 0) {
        cdc_data_write_str("Available modes:\r\n");
        cdc_data_write_str("  0: DInput - default\r\n");
        cdc_data_write_str("  1: Xbox Original (XID)\r\n");
        cdc_data_write_str("  2: XInput\r\n");
        cdc_data_write_str("  3: PS3\r\n");
        cdc_data_write_str("  4: PS4\r\n");
        cdc_data_write_str("  5: Switch\r\n");
        cdc_data_write_str("  6: PS Classic\r\n");
        cdc_data_write_str("  7: Xbox One\r\n");
        cdc_data_write_str("  8: XAC Compat (not in toggle)\r\n");
    }
    // VERSION or VER? - Query firmware version
    else if (strcmp(cmd, "VERSION") == 0 || strcmp(cmd, "VER?") == 0) {
        cdc_data_write_str("Joypad USB Device\r\n");
    }
    // FLASH? - Check raw flash contents
    else if (strcmp(cmd, "FLASH?") == 0) {
        flash_t flash_data;
        if (flash_load(&flash_data)) {
            snprintf(response, sizeof(response),
                     "Flash: magic=0x%08X, profile=%d, usb_mode=%d\r\n",
                     (unsigned int)flash_data.magic,
                     flash_data.active_profile_index,
                     flash_data.usb_output_mode);
            cdc_data_write_str(response);
        } else {
            cdc_data_write_str("Flash: No valid data (magic mismatch)\r\n");
        }
    }
    // HELP
    else if (strcmp(cmd, "HELP") == 0 || strcmp(cmd, "?") == 0) {
        cdc_data_write_str("Commands:\r\n");
        cdc_data_write_str("  MODE?     - Query current output mode\r\n");
        cdc_data_write_str("  MODE=N    - Set output mode (0-5 or name)\r\n");
        cdc_data_write_str("  MODES     - List available modes\r\n");
        cdc_data_write_str("  VERSION   - Show firmware version\r\n");
        cdc_data_write_str("  HELP      - Show this help\r\n");
    }
    // Unknown command
    else if (strlen(cmd) > 0) {
        snprintf(response, sizeof(response), "ERR: Unknown command '%s'\r\n", cmd);
        cdc_data_write_str(response);
    }
}

void cdc_task(void)
{
    cdc_protocol_t* proto = cdc_commands_get_protocol();

    // Process incoming data on the data port
    while (cdc_data_available() > 0) {
        int32_t ch = cdc_data_read_byte();
        if (ch < 0) break;

        // Check for binary protocol sync byte
        if (ch == CDC_SYNC_BYTE && !binary_mode) {
            binary_mode = true;
            cmd_pos = 0;  // Clear any pending text
        }

        if (binary_mode) {
            // Binary framed protocol
            cdc_protocol_rx_byte(proto, (uint8_t)ch);
        } else {
            // Legacy text protocol
            // Handle end of line (CR or LF)
            if (ch == '\r' || ch == '\n') {
                if (cmd_pos > 0) {
                    cmd_buffer[cmd_pos] = '\0';
                    cdc_process_command(cmd_buffer);
                    cmd_pos = 0;
                }
            }
            // Handle backspace
            else if (ch == '\b' || ch == 0x7F) {
                if (cmd_pos > 0) {
                    cmd_pos--;
                }
            }
            // Accumulate characters
            else if (cmd_pos < CMD_BUFFER_SIZE - 1) {
                cmd_buffer[cmd_pos++] = (char)ch;
            }
        }
    }
}

// ============================================================================
// DATA PORT (CDC 0)
// ============================================================================

bool cdc_data_connected(void)
{
    return tud_cdc_n_connected(CDC_PORT_DATA);
}

uint32_t cdc_data_available(void)
{
    return tud_cdc_n_available(CDC_PORT_DATA);
}

uint32_t cdc_data_read(uint8_t* buffer, uint32_t bufsize)
{
    return tud_cdc_n_read(CDC_PORT_DATA, buffer, bufsize);
}

int32_t cdc_data_read_byte(void)
{
    uint8_t ch;
    if (tud_cdc_n_read(CDC_PORT_DATA, &ch, 1) == 1) {
        return ch;
    }
    return -1;
}

uint32_t cdc_data_write(const uint8_t* buffer, uint32_t bufsize)
{
    if (!tud_cdc_n_connected(CDC_PORT_DATA)) {
        return 0;
    }
    uint32_t written = tud_cdc_n_write(CDC_PORT_DATA, buffer, bufsize);
    tud_cdc_n_write_flush(CDC_PORT_DATA);
    return written;
}

uint32_t cdc_data_write_str(const char* str)
{
    return cdc_data_write((const uint8_t*)str, strlen(str));
}

void cdc_data_flush(void)
{
    tud_cdc_n_write_flush(CDC_PORT_DATA);
}

// ============================================================================
// DEBUG PORT (CDC 1)
// ============================================================================

bool cdc_debug_connected(void)
{
#if CFG_TUD_CDC >= 2
    return tud_cdc_n_connected(CDC_PORT_DEBUG);
#else
    return false;
#endif
}

int cdc_debug_printf(const char* format, ...)
{
#if CFG_TUD_CDC >= 2
    if (!debug_enabled || !tud_cdc_n_connected(CDC_PORT_DEBUG)) {
        return 0;
    }

    char buffer[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (len > 0) {
        uint32_t written = tud_cdc_n_write(CDC_PORT_DEBUG, buffer, len);
        tud_cdc_n_write_flush(CDC_PORT_DEBUG);
        return written;
    }
    return 0;
#else
    (void)format;
    return 0;
#endif
}

uint32_t cdc_debug_write(const uint8_t* buffer, uint32_t bufsize)
{
#if CFG_TUD_CDC >= 2
    if (!debug_enabled || !tud_cdc_n_connected(CDC_PORT_DEBUG)) {
        return 0;
    }
    uint32_t written = tud_cdc_n_write(CDC_PORT_DEBUG, buffer, bufsize);
    tud_cdc_n_write_flush(CDC_PORT_DEBUG);
    return written;
#else
    (void)buffer;
    (void)bufsize;
    return 0;
#endif
}

void cdc_debug_flush(void)
{
#if CFG_TUD_CDC >= 2
    tud_cdc_n_write_flush(CDC_PORT_DEBUG);
#endif
}

void cdc_debug_set_enabled(bool enabled)
{
    debug_enabled = enabled;
}

bool cdc_debug_is_enabled(void)
{
    return debug_enabled;
}

// ============================================================================
// TINYUSB CDC CALLBACKS
// ============================================================================

// Invoked when CDC interface received data from host
void tud_cdc_rx_cb(uint8_t itf)
{
    (void)itf;
    // Data available - will be read via cdc_data_read()
}

// Invoked when CDC TX is complete
void tud_cdc_tx_complete_cb(uint8_t itf)
{
    (void)itf;
}

// Invoked when CDC line state changed (DTR/RTS)
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
    (void)itf;
    (void)dtr;
    (void)rts;
}

// Invoked when CDC line coding changed (baud, parity, etc)
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* p_line_coding)
{
    (void)itf;
    (void)p_line_coding;
}

#else // CFG_TUD_CDC == 0

// Stub implementations when CDC is disabled
void cdc_init(void) {}
void cdc_task(void) {}
bool cdc_data_connected(void) { return false; }
uint32_t cdc_data_available(void) { return 0; }
uint32_t cdc_data_read(uint8_t* buffer, uint32_t bufsize) { (void)buffer; (void)bufsize; return 0; }
int32_t cdc_data_read_byte(void) { return -1; }
uint32_t cdc_data_write(const uint8_t* buffer, uint32_t bufsize) { (void)buffer; (void)bufsize; return 0; }
uint32_t cdc_data_write_str(const char* str) { (void)str; return 0; }
void cdc_data_flush(void) {}
bool cdc_debug_connected(void) { return false; }
int cdc_debug_printf(const char* format, ...) { (void)format; return 0; }
uint32_t cdc_debug_write(const uint8_t* buffer, uint32_t bufsize) { (void)buffer; (void)bufsize; return 0; }
void cdc_debug_flush(void) {}
void cdc_debug_set_enabled(bool enabled) { (void)enabled; }
bool cdc_debug_is_enabled(void) { return false; }

#endif // CFG_TUD_CDC
