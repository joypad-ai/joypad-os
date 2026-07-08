// cdc.c - USB CDC (Virtual Serial Port) implementation
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "cdc.h"
#include "cdc_protocol.h"
#include "cdc_commands.h"
#include "../usbd.h"
#include "core/services/storage/flash.h"
#include "platform/platform.h"
#include "tusb.h"
#if defined(CONFIG_GC2USB) || (CFG_TUD_VENDOR && defined(CONFIG_JOYBUS_BRIDGE))
#include "hardware/gpio.h"  // gpio_get for JOYPIN?/GBADETECT diagnostics (RP2040 only)
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef CONFIG_GC2ETH
#include "apps/gc2eth/ch9120.h"
#include "apps/gc2eth/app.h"
#endif

#ifdef CONFIG_GC2ETH_FEATHER
#include "apps/gc2eth_feather/w5500.h"
#include "apps/gc2eth_feather/app.h"
#endif

#if CFG_TUD_CDC > 0

// Command buffer for text commands (legacy support)
#define CMD_BUFFER_SIZE 64
static char cmd_buffer[CMD_BUFFER_SIZE];
static uint8_t cmd_pos = 0;

// Protocol mode detection
static bool binary_mode = false;  // Switch to binary after receiving 0xAA

// ----------------------------------------------------------------------------
// Optional relay demux hooks (registered by mp_bridge when a NUS-relay-capable
// BLE app is built). Keeps cdc.c decoupled from BT/MouthPad code: an RX filter
// gets first look at each incoming byte and returns true if it consumed it as
// part of a relay frame (e.g. MouthPad 0xAA 0x55 frames); a periodic hook drains
// relay TX to CDC. Bytes the filter doesn't claim fall through to the normal
// JoypadOS command parser, so when no relay device is connected CDC behaves
// exactly as before.
typedef bool (*cdc_rx_filter_fn)(uint8_t byte);
typedef void (*cdc_task_hook_fn)(void);
static cdc_rx_filter_fn cdc_rx_filter = NULL;
static cdc_task_hook_fn cdc_task_hook = NULL;

void cdc_register_relay(cdc_rx_filter_fn filter, cdc_task_hook_fn hook)
{
    cdc_rx_filter = filter;
    cdc_task_hook = hook;
}

// Weak hook called once from cdc_init(). A relay module (mp_bridge, linked only
// in BLE apps) provides a strong override that calls cdc_register_relay(). The
// weak default makes non-relay builds a no-op, so the MouthPad NUS<->CDC relay
// is ambient in every BLE+CDC app with zero per-app wiring.
__attribute__((weak)) void cdc_relay_late_init(void) {}

// ============================================================================
// INITIALIZATION
// ============================================================================

void cdc_init(void)
{
    binary_mode = false;

    // Initialize binary protocol command handlers
    cdc_commands_init();

    // Let an optional relay module (e.g. MouthPad NUS<->CDC) attach itself.
    cdc_relay_late_init();
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
#ifdef CONFIG_NGC
        cdc_data_write_str("Available modes:\r\n");
        cdc_data_write_str("  13: CDC (config only)\r\n");
#else
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
#endif
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
        cdc_data_write_str("  BOOTSEL   - Reboot into UF2 bootloader\r\n");
#ifdef CONFIG_GC2ETH
        cdc_data_write_str("  IP?       - Query CH9120 assigned IP address\r\n");
        cdc_data_write_str("  TCP?      - Query CH9120 TCP connection status\r\n");
#endif
#ifdef CONFIG_GC2ETH_FEATHER
        cdc_data_write_str("  W5500?    - Dump W5500 chip state (version, link, IP, MAC, sock0)\r\n");
        cdc_data_write_str("  TCP?      - Query sock0 status register\r\n");
        cdc_data_write_str("  FRAMES?   - Show Dolphin command activity counters\r\n");
#endif
#if CFG_TUD_VENDOR
        cdc_data_write_str("  GBALINK?  - GBA Link USB bridge stats (frames, joybus timeouts)\r\n");
#endif
    }
#if CFG_TUD_VENDOR
    // GBALINK? — diagnostics for the vendor-bulk GBA Link bridge
    // (USB_OUTPUT_MODE_GBA_LINK). Tells us whether Dolphin's libusb is
    // actually pushing bytes through (frames>0) and what joybus is
    // returning to those commands.
    else if (strcmp(cmd, "GBALINK?") == 0) {
        extern uint32_t gba_link_mode_get_frames(void);
        extern uint32_t gba_link_mode_get_short_tx(void);
        extern uint32_t gba_link_mode_get_joybus_to(void);
        extern uint8_t  gba_link_mode_get_last_cmd(void);
        extern int      gba_link_mode_get_last_n(void);
        extern void     gba_link_mode_get_last_rx(uint8_t out[5]);
        uint8_t lr[5];
        gba_link_mode_get_last_rx(lr);
        snprintf(response, sizeof(response),
                 "frames=%lu short_tx=%lu joybus_to=%lu  "
                 "last_cmd=0x%02x n=%d rx=%02x%02x%02x%02x%02x\r\n",
                 (unsigned long)gba_link_mode_get_frames(),
                 (unsigned long)gba_link_mode_get_short_tx(),
                 (unsigned long)gba_link_mode_get_joybus_to(),
                 gba_link_mode_get_last_cmd(),
                 gba_link_mode_get_last_n(),
                 lr[0], lr[1], lr[2], lr[3], lr[4]);
        cdc_data_write_str(response);
    }
#endif
#if CFG_TUD_VENDOR && defined(CONFIG_JOYBUS_BRIDGE)
    // JOYTEST — drive a single joybus RESET (0xFF) over the bridge and
    // report the raw 3-byte rx. Bypasses the USB vendor pipe entirely so
    // we can confirm whether joybus itself is reaching the GBA.
    else if (strcmp(cmd, "JOYTEST") == 0) {
        extern int joybus_bridge_xfer(const uint8_t*, uint16_t,
                                      uint8_t*, uint16_t, uint32_t);
        uint8_t tx[1] = {0xFF};
        uint8_t rx[3] = {0xAA, 0xAA, 0xAA};
        int n = joybus_bridge_xfer(tx, 1, rx, 3, /*to_us=*/5000);
        snprintf(response, sizeof(response),
                 "n=%d rx=%02x%02x%02x (0xAA = untouched)\r\n",
                 n, rx[0], rx[1], rx[2]);
        cdc_data_write_str(response);
    }
    // GBALINK! — per-command-type timing dump. Shows min/max/avg joybus
    // xfer wall-clock time and retry/timeout counts, broken out by cmd
    // type. Use to figure out whether timeouts cluster on RESET (cold
    // start), STATUS (handshake polls), or WRITE (body burst).
    else if (strcmp(cmd, "GBALINK!") == 0) {
        extern void gba_link_mode_get_timing(int idx, uint32_t* min_us,
                                             uint32_t* max_us, uint32_t* avg_us,
                                             uint32_t* count, uint32_t* timeouts,
                                             uint32_t* retries);
        extern uint32_t gba_link_mode_get_write_bad_jstat(uint8_t* last);
        static const char* names[4] = {"RESET", "STATUS", "READ ", "WRITE"};
        uint32_t mn, mx, av, ct, to, rt;
        for (int i = 0; i < 4; i++) {
            gba_link_mode_get_timing(i, &mn, &mx, &av, &ct, &to, &rt);
            snprintf(response, sizeof(response),
                     "%s n=%-6lu avg=%-5luus min=%-5luus max=%-6luus "
                     "retries=%-5lu to=%-4lu\r\n",
                     names[i],
                     (unsigned long)ct, (unsigned long)av,
                     (unsigned long)mn, (unsigned long)mx,
                     (unsigned long)rt, (unsigned long)to);
            cdc_data_write_str(response);
        }
        uint8_t last_bad = 0;
        uint32_t bad = gba_link_mode_get_write_bad_jstat(&last_bad);
        snprintf(response, sizeof(response),
                 "WRITE bad_jstat count=%lu last=0x%02x "
                 "(any of 0xC5 bits set = GBA flagged WRITE invalid)\r\n",
                 (unsigned long)bad, last_bad);
        cdc_data_write_str(response);
    }
    // GBALINK0 — reset timing + frame counters so a fresh session can
    // be measured without prior session noise.
    else if (strcmp(cmd, "GBALINK0") == 0) {
        extern void gba_link_mode_reset_timing(void);
        gba_link_mode_reset_timing();
        cdc_data_write_str("GBALINK telemetry reset\r\n");
    }
    // JOYPIN? — sample the joybus data pin (open-drain, idle should be
    // high with internal pull-up). Confirms wiring + GBA presence.
    else if (strcmp(cmd, "JOYPIN?") == 0) {
#ifndef GC_PIN_DATA
#define GC_PIN_DATA 4
#endif
        int level = gpio_get(GC_PIN_DATA);
        snprintf(response, sizeof(response),
                 "gpio%d level=%d (1=idle/pulled-up, 0=line held low)\r\n",
                 GC_PIN_DATA, level);
        cdc_data_write_str(response);
    }
#endif
    // GBADETECT — run the "is the GBA payload already running?" probe
    // on demand and dump the result. Useful for debugging the
    // firmware-restart-without-multiboot path.
#ifdef CONFIG_GC2USB
    else if (strcmp(cmd, "GBADETECT") == 0) {
        // Dump the most recent gc_host_task autoboot probe log + current
        // state. The CDC handler runs interleaved with gc_host_task on
        // the same core, so a CDC-initiated probe races joybus PIO with
        // the host loop's own xfers — every read times out. Reading the
        // log gc_host_task wrote (when it had the bus to itself) is the
        // only honest way to see what the detect actually saw.
        extern const char* gba_mb_detect_log_get(void);
        extern bool gc_host_gba_boot_attempted(uint8_t port);
        extern uint16_t gc_host_gba_read_fail_streak(uint8_t port);
#ifndef GC_PIN_DATA
#define GC_PIN_DATA 4
#endif
        int pin_level = gpio_get(GC_PIN_DATA);
        const char* log = gba_mb_detect_log_get();
        snprintf(response, sizeof(response),
                 "GBADETECT: boot_attempted[0]=%d  read_fail_streak=%u  "
                 "data_pin(gpio%d)=%d (1=idle, 0=held-low)\r\n",
                 gc_host_gba_boot_attempted(0) ? 1 : 0,
                 (unsigned)gc_host_gba_read_fail_streak(0),
                 GC_PIN_DATA, pin_level);
        cdc_data_write_str(response);
        if (log && log[0]) {
            cdc_data_write_str("--- last gc_host_task probe ---\r\n");
            cdc_data_write_str(log);
            cdc_data_write_str("--- end ---\r\n");
        } else {
            cdc_data_write_str("(no probe log: gc_host_task hasn't run "
                               "gba_mb_payload_already_running yet)\r\n");
        }
    }
    // GBARESET — force gc_host_task to redo its GBA autoboot decision on
    // the next iteration. Clears gba_boot_attempted[0] and zeroes the
    // probe throttle so the probe fires immediately. Use this to capture
    // a fresh probe trace via GBADETECT without rebooting the firmware.
    else if (strcmp(cmd, "GBARESET") == 0) {
        extern void gc_host_gba_reset_boot_attempted(uint8_t port);
        gc_host_gba_reset_boot_attempted(0);
        cdc_data_write_str("GBARESET: cleared boot_attempted[0], probe "
                           "will fire on next gc_host_task tick\r\n");
    }
#endif
    // BOOTSEL — drop into UF2 bootloader via the platform HAL so this
    // works on both RP2040 (reset_usb_boot) and ESP32-S3 (TinyUF2).
    else if (strcmp(cmd, "BOOTSEL") == 0) {
        cdc_data_write_str("Rebooting to bootloader...\r\n");
        cdc_data_flush();
        platform_reboot_bootloader();
    }
#ifdef CONFIG_GC2ETH
    // IP? — read CH9120's assigned IP. Drops the chip back into config
    // mode briefly to query, so doesn't work mid-Dolphin-session.
    else if (strcmp(cmd, "IP?") == 0) {
        uint8_t ip[4];
        if (ch9120_read_ip(ip)) {
            snprintf(response, sizeof(response),
                     "IP=%u.%u.%u.%u  port=54970\r\n",
                     ip[0], ip[1], ip[2], ip[3]);
            cdc_data_write_str(response);
        } else {
            cdc_data_write_str("ERR: CH9120 read_ip failed\r\n");
        }
    }
    // TCP? — non-intrusive: just reads the TCPCS pin state.
    else if (strcmp(cmd, "TCP?") == 0) {
        snprintf(response, sizeof(response), "TCP=%s\r\n",
                 ch9120_is_connected() ? "connected" : "idle");
        cdc_data_write_str(response);
    }
    // FRAMES? — Dolphin command activity counter. Tells us whether
    // CH9120 is actually forwarding TCP bytes through to UART1.
    else if (strcmp(cmd, "FRAMES?") == 0) {
        gc2eth_diag_t d;
        gc2eth_get_diag(&d);
        snprintf(response, sizeof(response),
                 "frames=%lu last_cmd=0x%02x last_n=%d "
                 "last_rx=%02x%02x%02x%02x%02x\r\n",
                 (unsigned long)d.frames_seen, d.last_cmd, d.last_n,
                 d.last_rx[0], d.last_rx[1], d.last_rx[2],
                 d.last_rx[3], d.last_rx[4]);
        cdc_data_write_str(response);
    }
    // POKE — push a sentinel byte sequence over UART1 to the CH9120
    // chip. If the chip is forwarding UART→TCP correctly, the probe
    // listener will see it. Tests the OPPOSITE direction from FRAMES.
    else if (strcmp(cmd, "POKE") == 0) {
        const uint8_t poke[] = { 0xDE, 0xAD, 0xBE, 0xEF };
        ch9120_send_bytes(poke, sizeof(poke));
        cdc_data_write_str("poked DEADBEEF to UART1\r\n");
    }
#endif
#ifdef CONFIG_GC2ETH_FEATHER
    // W5500? — dump everything we can read back from the chip. The
    // first line we care about is VERSIONR: if it isn't 0x04 the SPI
    // wiring or clock is wrong and nothing else matters. PHYCFGR bit 0
    // is link-up.
    else if (strcmp(cmd, "W5500?") == 0) {
        w5500_diag_t d;
        w5500_get_diag(&d);
        snprintf(response, sizeof(response),
                 "VERSIONR=0x%02x PHYCFGR=0x%02x link=%s\r\n",
                 d.versionr, d.phycfgr, (d.phycfgr & 0x01) ? "up" : "down");
        cdc_data_write_str(response);
        snprintf(response, sizeof(response),
                 "IP=%u.%u.%u.%u  MAC=%02x:%02x:%02x:%02x:%02x:%02x\r\n",
                 d.sipr[0], d.sipr[1], d.sipr[2], d.sipr[3],
                 d.shar[0], d.shar[1], d.shar[2], d.shar[3], d.shar[4], d.shar[5]);
        cdc_data_write_str(response);
        snprintf(response, sizeof(response),
                 "sock0: mr=0x%02x port=%u sr=0x%02x\r\n",
                 d.sn_mr, d.sn_port, d.sn_sr);
        cdc_data_write_str(response);
    }
    // TCP? — non-intrusive: just reads Sn_SR.
    else if (strcmp(cmd, "TCP?") == 0) {
        uint8_t sr = w5500_sock0_status();
        const char* name =
            sr == 0x00 ? "CLOSED" :
            sr == 0x13 ? "INIT" :
            sr == 0x14 ? "LISTEN" :
            sr == 0x17 ? "ESTABLISHED" :
            sr == 0x1C ? "CLOSE_WAIT" : "?";
        snprintf(response, sizeof(response), "TCP=0x%02x (%s)\r\n", sr, name);
        cdc_data_write_str(response);
    }
    // STATUSCACHE — tune the STATUS-poll cache (Phase 2). N=0 disables
    // (every STATUS goes to joybus); N>0 serves up to N consecutive
    // STATUS polls from a local cache before forcing a refresh. Higher
    // N = more speedup but staler RECV-bit visibility for Dolphin.
    else if (strcmp(cmd, "STATUSCACHE?") == 0) {
        uint32_t hits = gc2eth_status_cache_hits();
        uint32_t miss = gc2eth_status_cache_miss();
        uint32_t total = hits + miss;
        unsigned pct = total ? (unsigned)((100ULL * hits) / total) : 0;
        snprintf(response, sizeof(response),
                 "status_cache_n=%u  hits=%lu miss=%lu (%u%% hit rate)\r\n",
                 (unsigned)gc2eth_status_cache_get_n(),
                 (unsigned long)hits, (unsigned long)miss, pct);
        cdc_data_write_str(response);
    }
    else if (strcmp(cmd, "READCACHE?") == 0) {
        uint32_t hits = gc2eth_read_cache_hits();
        uint32_t miss = gc2eth_read_cache_miss();
        uint32_t total = hits + miss;
        unsigned pct = total ? (unsigned)((100ULL * hits) / total) : 0;
        snprintf(response, sizeof(response),
                 "read_cache_n=%u  hits=%lu miss=%lu (%u%% hit rate)\r\n",
                 (unsigned)gc2eth_read_cache_get_n(),
                 (unsigned long)hits, (unsigned long)miss, pct);
        cdc_data_write_str(response);
    }
    else if (strncmp(cmd, "READCACHE=", 10) == 0) {
        int n = atoi(cmd + 10);
        if (n < 0) n = 0;
        if (n > 65535) n = 65535;
        gc2eth_read_cache_set_n((uint16_t)n);
        snprintf(response, sizeof(response),
                 "read_cache_n=%u  (warning: delays GBA input visibility)\r\n",
                 (unsigned)n);
        cdc_data_write_str(response);
    }
    else if (strcmp(cmd, "WRITECACHE?") == 0) {
        uint32_t hits = gc2eth_write_cache_hits();
        uint32_t miss = gc2eth_write_cache_miss();
        uint32_t total = hits + miss;
        unsigned pct = total ? (unsigned)((100ULL * hits) / total) : 0;
        snprintf(response, sizeof(response),
                 "write_cache_n=%u  hits=%lu miss=%lu (%u%% hit rate)\r\n",
                 (unsigned)gc2eth_write_cache_get_n(),
                 (unsigned long)hits, (unsigned long)miss, pct);
        cdc_data_write_str(response);
    }
    else if (strncmp(cmd, "WRITECACHE=", 11) == 0) {
        int n = atoi(cmd + 11);
        if (n < 0) n = 0;
        if (n > 65535) n = 65535;
        gc2eth_write_cache_set_n((uint16_t)n);
        snprintf(response, sizeof(response),
                 "write_cache_n=%u\r\n", (unsigned)n);
        cdc_data_write_str(response);
    }
    else if (strcmp(cmd, "SPEC?") == 0) {
        snprintf(response, sizeof(response),
                 "spec_depth=%u  outstanding=%u  corruption_events=%lu\r\n",
                 (unsigned)gc2eth_spec_get_depth(),
                 (unsigned)gc2eth_spec_get_outstanding(),
                 (unsigned long)gc2eth_spec_get_corruption());
        cdc_data_write_str(response);
    }
    else if (strncmp(cmd, "SPEC=", 5) == 0) {
        int n = atoi(cmd + 5);
        if (n < 0) n = 0;
        if (n > 255) n = 255;
        gc2eth_spec_set_depth((uint8_t)n);
        snprintf(response, sizeof(response), "spec_depth=%u\r\n", (unsigned)n);
        cdc_data_write_str(response);
    }
    else if (strncmp(cmd, "STATUSCACHE=", 12) == 0) {
        int n = atoi(cmd + 12);
        if (n < 0) n = 0;
        if (n > 65535) n = 65535;
        gc2eth_status_cache_set_n((uint16_t)n);
        snprintf(response, sizeof(response),
                 "status_cache_n=%u\r\n", (unsigned)n);
        cdc_data_write_str(response);
    }
    // TRACE.START / TRACE.STOP / TRACE.DUMP — capture (cmd, response,
    // delta_us) for every joybus exchange so we can profile the
    // command-mix during real gameplay (Phase 1 of intercept-replay).
    // Run TRACE.START, play a game for ~30 s, then TRACE.DUMP.
    else if (strcmp(cmd, "TRACE.START") == 0) {
        gc2eth_trace_start();
        cdc_data_write_str("trace armed\r\n");
    }
    else if (strcmp(cmd, "TRACE.STOP") == 0) {
        gc2eth_trace_stop();
        snprintf(response, sizeof(response),
                 "trace stopped — %lu entries captured\r\n",
                 (unsigned long)gc2eth_trace_count());
        cdc_data_write_str(response);
    }
    else if (strcmp(cmd, "TRACE.DUMP") == 0) {
        // Header makes the output greppable / parseable downstream.
        cdc_data_write_str("# idx delta_us cmd n rx0 rx1 rx2 rx3 rx4\r\n");
        uint32_t total = gc2eth_trace_count();
        for (uint32_t i = 0; i < total; i++) {
            gc2eth_trace_entry_t e;
            if (!gc2eth_trace_get(i, &e)) break;
            snprintf(response, sizeof(response),
                     "%lu %lu %02x %d %02x %02x %02x %02x %02x\r\n",
                     (unsigned long)i, (unsigned long)e.delta_us,
                     e.cmd, (int)e.n,
                     e.rx[0], e.rx[1], e.rx[2], e.rx[3], e.rx[4]);
            cdc_data_write_str(response);
        }
        snprintf(response, sizeof(response),
                 "# end (%lu entries)\r\n", (unsigned long)total);
        cdc_data_write_str(response);
    }
    // FRAMES? — Dolphin command activity counter for the W5500 path.
    else if (strcmp(cmd, "FRAMES?") == 0) {
        gc2eth_diag_t d;
        gc2eth_get_diag(&d);
        snprintf(response, sizeof(response),
                 "frames=%lu last_cmd=0x%02x last_n=%d "
                 "last_rx=%02x%02x%02x%02x%02x\r\n",
                 (unsigned long)d.frames_seen, d.last_cmd, d.last_n,
                 d.last_rx[0], d.last_rx[1], d.last_rx[2],
                 d.last_rx[3], d.last_rx[4]);
        cdc_data_write_str(response);
    }
#endif
    // Unknown command
    else if (strlen(cmd) > 0) {
        snprintf(response, sizeof(response), "ERR: Unknown command '%s'\r\n", cmd);
        cdc_data_write_str(response);
    }
}

// Feed one byte into the JoypadOS command parser (binary framed or legacy text).
// Exposed so a relay demux filter can replay pass-through bytes (e.g. a buffered
// 0xAA that turned out not to start a relay frame).
void cdc_feed_command_byte(uint8_t ch)
{
    cdc_protocol_t* proto = cdc_commands_get_protocol();

    // Check for binary protocol sync byte
    if (ch == CDC_SYNC_BYTE && !binary_mode) {
        binary_mode = true;
        cmd_pos = 0;  // Clear any pending text
    }

    if (binary_mode) {
        // Binary framed protocol
        cdc_protocol_rx_byte(proto, ch);
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

void cdc_task(void)
{
    // Handle rumble auto-stop, log drain, etc.
    cdc_commands_task();

    // Drain any relay TX (e.g. MouthPad NUS -> CDC) before reading.
    if (cdc_task_hook) cdc_task_hook();

    // Process incoming data on the data port.
    //
    // Pump the USB stack between drains: TinyUSB moves at most ONE endpoint
    // transaction (64B) into the RX FIFO per tud_task() call, so draining
    // once per main-loop pass caps host->device throughput at 64B/pass —
    // ~17kB/s when BT audio streaming slows the loop to ~4ms/pass, which
    // starved VOICE.SPEAK audio (needs ~26kB/s). Re-pumping while bytes
    // keep arriving lets one cdc_task call swallow the host's whole burst.
    for (int pump = 0; pump < 32; pump++) {
        bool progress = false;
        while (cdc_data_available() > 0) {
            int32_t ch = cdc_data_read_byte();
            if (ch < 0) break;
            progress = true;

            // Give the relay demux first look. If it claims the byte (part of a
            // relay frame), don't pass it to the command parser.
            if (cdc_rx_filter && cdc_rx_filter((uint8_t)ch)) continue;

            cdc_feed_command_byte((uint8_t)ch);
        }
        if (!progress) break;
        tud_task_ext(0, false);  // move the next endpoint transaction in
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
    // Check available space — drop the entire packet rather than partial write
    uint32_t avail = tud_cdc_n_write_available(CDC_PORT_DATA);
    if (avail < bufsize) {
        return 0;  // Drop packet, don't corrupt the stream
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

#endif // CFG_TUD_CDC
