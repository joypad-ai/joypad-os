// cdc_commands.c - CDC command handlers
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "cdc_commands.h"
#include "cdc_protocol.h"
#include "../usbd.h"
#include "core/services/storage/flash.h"
#include "core/services/profiles/profile.h"
#include "hardware/watchdog.h"
#include "pico/unique_id.h"
#include "pico/bootrom.h"
#include "tusb.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Optional BT support
#ifdef CONFIG_BLUETOOTH
#include "bt/btstack/btstack_host.h"
#endif

// ============================================================================
// STATE
// ============================================================================

static cdc_protocol_t protocol_ctx;
static char response_buf[CDC_MAX_PAYLOAD];

// App info (set by CMake or use defaults)
#ifndef APP_NAME
#define APP_NAME "joypad"
#endif
#ifndef JOYPAD_VERSION
#define JOYPAD_VERSION "0.0.0"
#endif
#ifndef GIT_COMMIT
#define GIT_COMMIT "unknown"
#endif
#ifndef BUILD_TIME
#define BUILD_TIME "unknown"
#endif
#ifndef BOARD_NAME
#define BOARD_NAME "unknown"
#endif

// ============================================================================
// JSON HELPERS
// ============================================================================

// Simple JSON string extractor: finds "key":"value" and returns value
// Returns NULL if not found, or pointer to value (not null-terminated)
static const char* json_get_string(const char* json, const char* key,
                                   int* out_len)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);

    const char* start = strstr(json, search);
    if (!start) return NULL;

    start += strlen(search);
    const char* end = strchr(start, '"');
    if (!end) return NULL;

    if (out_len) *out_len = end - start;
    return start;
}

// Simple JSON integer extractor: finds "key":123 and returns value
static bool json_get_int(const char* json, const char* key, int* out_val)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char* start = strstr(json, search);
    if (!start) return false;

    start += strlen(search);
    // Skip whitespace
    while (*start == ' ' || *start == '\t') start++;

    if (*start == '-' || (*start >= '0' && *start <= '9')) {
        *out_val = atoi(start);
        return true;
    }
    return false;
}

// Simple JSON bool extractor
static bool json_get_bool(const char* json, const char* key, bool* out_val)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);

    const char* start = strstr(json, search);
    if (!start) return false;

    start += strlen(search);
    while (*start == ' ' || *start == '\t') start++;

    if (strncmp(start, "true", 4) == 0) {
        *out_val = true;
        return true;
    } else if (strncmp(start, "false", 5) == 0) {
        *out_val = false;
        return true;
    }
    return false;
}

// Extract command name from JSON
static bool json_get_cmd(const char* json, char* cmd_buf, size_t buf_size)
{
    int len;
    const char* cmd = json_get_string(json, "cmd", &len);
    if (!cmd || len <= 0 || (size_t)len >= buf_size) return false;

    memcpy(cmd_buf, cmd, len);
    cmd_buf[len] = '\0';
    return true;
}

// ============================================================================
// RESPONSE HELPERS
// ============================================================================

static void send_ok(void)
{
    cdc_protocol_send_response(&protocol_ctx, "{\"ok\":true}");
}

static void send_error(const char* msg)
{
    snprintf(response_buf, sizeof(response_buf),
             "{\"ok\":false,\"error\":\"%s\"}", msg);
    cdc_protocol_send_response(&protocol_ctx, response_buf);
}

static void send_json(const char* json)
{
    cdc_protocol_send_response(&protocol_ctx, json);
}

// ============================================================================
// COMMAND HANDLERS
// ============================================================================

static void cmd_info(const char* json)
{
    (void)json;

    char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    pico_get_unique_board_id_string(serial, sizeof(serial));

    snprintf(response_buf, sizeof(response_buf),
             "{\"app\":\"%s\",\"version\":\"%s\",\"board\":\"%s\",\"serial\":\"%s\",\"commit\":\"%s\",\"build\":\"%s\"}",
             APP_NAME, JOYPAD_VERSION, BOARD_NAME, serial, GIT_COMMIT, BUILD_TIME);
    printf("[CDC] INFO response: %s\n", response_buf);
    send_json(response_buf);
}

static void cmd_ping(const char* json)
{
    (void)json;
    send_ok();
}

static void cmd_reboot(const char* json)
{
    (void)json;
    send_ok();
    // Flush response
    tud_task();
    sleep_ms(50);
    tud_task();
    // Reboot
    watchdog_enable(100, false);
    while(1);
}

static void cmd_bootsel(const char* json)
{
    (void)json;
    send_ok();
    // Flush response
    tud_task();
    sleep_ms(50);
    tud_task();
    // Reboot into BOOTSEL/UF2 bootloader mode
    reset_usb_boot(0, 0);
}

static void cmd_mode_get(const char* json)
{
    (void)json;
    usb_output_mode_t mode = usbd_get_mode();
    snprintf(response_buf, sizeof(response_buf),
             "{\"mode\":%d,\"name\":\"%s\"}",
             (int)mode, usbd_get_mode_name(mode));
    send_json(response_buf);
}

static void cmd_mode_set(const char* json)
{
    int mode;
    if (!json_get_int(json, "mode", &mode)) {
        send_error("missing mode");
        return;
    }

    if (mode < 0 || mode >= USB_OUTPUT_MODE_COUNT) {
        send_error("invalid mode");
        return;
    }

    usb_output_mode_t current = usbd_get_mode();
    if ((usb_output_mode_t)mode == current) {
        snprintf(response_buf, sizeof(response_buf),
                 "{\"mode\":%d,\"name\":\"%s\",\"reboot\":false}",
                 mode, usbd_get_mode_name((usb_output_mode_t)mode));
        send_json(response_buf);
        return;
    }

    snprintf(response_buf, sizeof(response_buf),
             "{\"mode\":%d,\"name\":\"%s\",\"reboot\":true}",
             mode, usbd_get_mode_name((usb_output_mode_t)mode));
    send_json(response_buf);

    // Flush then switch mode (triggers reboot)
    tud_task();
    sleep_ms(50);
    tud_task();
    usbd_set_mode((usb_output_mode_t)mode);
}

static void cmd_mode_list(const char* json)
{
    (void)json;
    usb_output_mode_t current = usbd_get_mode();

    int pos = snprintf(response_buf, sizeof(response_buf),
                       "{\"current\":%d,\"modes\":[", (int)current);

    for (int i = 0; i < USB_OUTPUT_MODE_COUNT && pos < (int)sizeof(response_buf) - 50; i++) {
        if (i > 0) pos += snprintf(response_buf + pos, sizeof(response_buf) - pos, ",");
        pos += snprintf(response_buf + pos, sizeof(response_buf) - pos,
                        "{\"id\":%d,\"name\":\"%s\"}", i, usbd_get_mode_name(i));
    }
    snprintf(response_buf + pos, sizeof(response_buf) - pos, "]}");
    send_json(response_buf);
}

static void cmd_profile_get(const char* json)
{
    (void)json;
    uint8_t index = profile_get_active_index(OUTPUT_TARGET_USB_DEVICE);
    uint8_t count = profile_get_count(OUTPUT_TARGET_USB_DEVICE);
    const char* name = profile_get_name(OUTPUT_TARGET_USB_DEVICE, index);

    snprintf(response_buf, sizeof(response_buf),
             "{\"index\":%d,\"name\":\"%s\",\"count\":%d}",
             index, name ? name : "default", count);
    send_json(response_buf);
}

static void cmd_profile_set(const char* json)
{
    int index;
    if (!json_get_int(json, "index", &index)) {
        send_error("missing index");
        return;
    }

    uint8_t count = profile_get_count(OUTPUT_TARGET_USB_DEVICE);
    if (index < 0 || index >= count) {
        send_error("invalid index");
        return;
    }

    profile_set_active(OUTPUT_TARGET_USB_DEVICE, index);
    const char* name = profile_get_name(OUTPUT_TARGET_USB_DEVICE, index);

    snprintf(response_buf, sizeof(response_buf),
             "{\"index\":%d,\"name\":\"%s\"}",
             index, name ? name : "default");
    send_json(response_buf);
}

static void cmd_profile_list(const char* json)
{
    (void)json;
    uint8_t active = profile_get_active_index(OUTPUT_TARGET_USB_DEVICE);
    uint8_t count = profile_get_count(OUTPUT_TARGET_USB_DEVICE);

    int pos = snprintf(response_buf, sizeof(response_buf),
                       "{\"active\":%d,\"profiles\":[", active);

    for (int i = 0; i < count && pos < (int)sizeof(response_buf) - 50; i++) {
        const char* name = profile_get_name(OUTPUT_TARGET_USB_DEVICE, i);
        if (i > 0) pos += snprintf(response_buf + pos, sizeof(response_buf) - pos, ",");
        pos += snprintf(response_buf + pos, sizeof(response_buf) - pos,
                        "{\"id\":%d,\"name\":\"%s\"}", i, name ? name : "default");
    }
    snprintf(response_buf + pos, sizeof(response_buf) - pos, "]}");
    send_json(response_buf);
}

static void cmd_input_stream(const char* json)
{
    bool enable;
    if (!json_get_bool(json, "enable", &enable)) {
        send_error("missing enable");
        return;
    }

    protocol_ctx.input_streaming = enable;
    send_ok();
}

static void cmd_settings_get(const char* json)
{
    (void)json;
    flash_t flash_data;
    if (flash_load(&flash_data)) {
        snprintf(response_buf, sizeof(response_buf),
                 "{\"profile\":%d,\"mode\":%d}",
                 flash_data.active_profile_index,
                 flash_data.usb_output_mode);
    } else {
        snprintf(response_buf, sizeof(response_buf),
                 "{\"profile\":0,\"mode\":0,\"valid\":false}");
    }
    send_json(response_buf);
}

static void cmd_settings_reset(const char* json)
{
    (void)json;

    // Clear flash by writing defaults
    flash_t flash_data = {0};
    flash_save_now(&flash_data);

    snprintf(response_buf, sizeof(response_buf),
             "{\"ok\":true,\"reboot\":true}");
    send_json(response_buf);

    // Reboot
    tud_task();
    sleep_ms(50);
    tud_task();
    watchdog_enable(100, false);
    while(1);
}

#ifdef CONFIG_BLUETOOTH
static void cmd_bt_status(const char* json)
{
    (void)json;
    snprintf(response_buf, sizeof(response_buf),
             "{\"enabled\":%s,\"scanning\":%s,\"connections\":%d}",
             btstack_host_is_initialized() ? "true" : "false",
             btstack_host_is_scanning() ? "true" : "false",
             btstack_classic_get_connection_count());
    send_json(response_buf);
}

static void cmd_bt_bonds_clear(const char* json)
{
    (void)json;
    btstack_host_delete_all_bonds();
    send_ok();
}
#endif

// ============================================================================
// COMMAND DISPATCH
// ============================================================================

typedef void (*cmd_handler_t)(const char* json);

typedef struct {
    const char* name;
    cmd_handler_t handler;
} cmd_entry_t;

static const cmd_entry_t commands[] = {
    {"INFO", cmd_info},
    {"PING", cmd_ping},
    {"REBOOT", cmd_reboot},
    {"BOOTSEL", cmd_bootsel},
    {"MODE.GET", cmd_mode_get},
    {"MODE.SET", cmd_mode_set},
    {"MODE.LIST", cmd_mode_list},
    {"PROFILE.GET", cmd_profile_get},
    {"PROFILE.SET", cmd_profile_set},
    {"PROFILE.LIST", cmd_profile_list},
    {"INPUT.STREAM", cmd_input_stream},
    {"SETTINGS.GET", cmd_settings_get},
    {"SETTINGS.RESET", cmd_settings_reset},
#ifdef CONFIG_BLUETOOTH
    {"BT.STATUS", cmd_bt_status},
    {"BT.BONDS.CLEAR", cmd_bt_bonds_clear},
#endif
    {NULL, NULL}
};

// ============================================================================
// PACKET HANDLER
// ============================================================================

static void packet_handler(const cdc_packet_t* packet)
{
    if (packet->type != CDC_MSG_CMD) {
        // Only handle CMD packets here
        return;
    }

    // Null-terminate payload for string operations
    char json[CDC_MAX_PAYLOAD + 1];
    memcpy(json, packet->payload, packet->length);
    json[packet->length] = '\0';

    // Extract command name
    char cmd[32];
    if (!json_get_cmd(json, cmd, sizeof(cmd))) {
        send_error("invalid command format");
        return;
    }

    // Find and execute handler
    for (const cmd_entry_t* entry = commands; entry->name; entry++) {
        if (strcmp(cmd, entry->name) == 0) {
            entry->handler(json);
            return;
        }
    }

    send_error("unknown command");
}

// ============================================================================
// PUBLIC API
// ============================================================================

void cdc_commands_init(void)
{
    cdc_protocol_init(&protocol_ctx, packet_handler);

    // Debug: print build info at startup
    printf("[CDC] Build Info Debug:\n");
    printf("[CDC]   APP_NAME: %s\n", APP_NAME);
    printf("[CDC]   JOYPAD_VERSION: %s\n", JOYPAD_VERSION);
    printf("[CDC]   GIT_COMMIT: %s\n", GIT_COMMIT);
    printf("[CDC]   BUILD_TIME: %s\n", BUILD_TIME);
    printf("[CDC]   BOARD_NAME: %s\n", BOARD_NAME);
}

void cdc_commands_process(const cdc_packet_t* packet)
{
    packet_handler(packet);
}

cdc_protocol_t* cdc_commands_get_protocol(void)
{
    return &protocol_ctx;
}

void cdc_commands_send_input_event(uint32_t buttons, const uint8_t* axes)
{
    if (!protocol_ctx.input_streaming) return;

    snprintf(response_buf, sizeof(response_buf),
             "{\"type\":\"input\",\"buttons\":%lu,\"axes\":[%d,%d,%d,%d,%d,%d]}",
             (unsigned long)buttons,
             axes[0], axes[1], axes[2], axes[3], axes[4], axes[5]);
    cdc_protocol_send_event(&protocol_ctx, response_buf);
}

void cdc_commands_send_connect_event(uint8_t port, const char* name,
                                     uint16_t vid, uint16_t pid)
{
    snprintf(response_buf, sizeof(response_buf),
             "{\"type\":\"connect\",\"port\":%d,\"name\":\"%s\",\"vid\":%d,\"pid\":%d}",
             port, name, vid, pid);
    cdc_protocol_send_event(&protocol_ctx, response_buf);
}

void cdc_commands_send_disconnect_event(uint8_t port)
{
    snprintf(response_buf, sizeof(response_buf),
             "{\"type\":\"disconnect\",\"port\":%d}", port);
    cdc_protocol_send_event(&protocol_ctx, response_buf);
}
