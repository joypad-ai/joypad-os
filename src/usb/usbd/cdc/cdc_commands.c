// cdc_commands.c - CDC command handlers
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "cdc_commands.h"
#include "cdc_protocol.h"
#include "../usbd.h"
#include "app.h"
#include "core/services/storage/flash.h"
#include "core/services/profiles/profile.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "hardware/watchdog.h"
#include "pico/unique_id.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "tusb.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Optional BT support
#ifdef ENABLE_BTSTACK
#include "bt/btstack/btstack_host.h"
#include "bt/bthid/devices/vendors/nintendo/wiimote_bt.h"
#endif

// ============================================================================
// STATE
// ============================================================================

static cdc_protocol_t protocol_ctx;
static char response_buf[CDC_MAX_PAYLOAD];

// ============================================================================
// LOG CAPTURE (ring buffer + stdio driver)
// ============================================================================

#define LOG_BUF_SIZE 1024
static char log_ring[LOG_BUF_SIZE];
static volatile uint16_t log_head = 0;  // Write position
static volatile uint16_t log_tail = 0;  // Read position

static void log_stdio_out_chars(const char *buf, int len)
{
    // Skip ring buffer writes when not streaming — zero overhead on normal path
    if (!protocol_ctx.log_streaming) return;

    for (int i = 0; i < len; i++) {
        uint16_t next = (log_head + 1) % LOG_BUF_SIZE;
        if (next == log_tail) {
            // Buffer full - drop oldest byte
            log_tail = (log_tail + 1) % LOG_BUF_SIZE;
        }
        log_ring[log_head] = buf[i];
        log_head = next;
    }
}

static stdio_driver_t log_stdio_driver = {
    .out_chars = log_stdio_out_chars,
    .out_flush = NULL,
    .in_chars = NULL,
    .set_chars_available_callback = NULL,
    .next = NULL,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF,
#endif
};

// Separate buffer for log events (not shared with response_buf)
static char log_event_buf[384];

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

    bool first = true;
    for (int i = 0; i < USB_OUTPUT_MODE_COUNT && pos < (int)sizeof(response_buf) - 50; i++) {
        // Skip DInput (HID) mode - replaced by SInput
        if (i == USB_OUTPUT_MODE_HID) continue;

        if (!first) pos += snprintf(response_buf + pos, sizeof(response_buf) - pos, ",");
        first = false;
        pos += snprintf(response_buf + pos, sizeof(response_buf) - pos,
                        "{\"id\":%d,\"name\":\"%s\"}", i, usbd_get_mode_name(i));
    }
    snprintf(response_buf + pos, sizeof(response_buf) - pos, "]}");
    send_json(response_buf);
}

// ============================================================================
// UNIFIED PROFILE COMMANDS
// ============================================================================
//
// Unified indexing:
// - If app has built-in profiles (builtin_count > 0):
//   Index 0 to builtin_count-1: Built-in profiles (builtin=true, editable=false)
//   Index builtin_count to builtin_count+custom_count-1: Custom profiles (editable=true)
//
// - If app has no built-in profiles (builtin_count == 0):
//   Index 0: Virtual "Default" passthrough (builtin=true, editable=false)
//   Index 1 to custom_count: Custom profiles (editable=true)

static uint8_t get_builtin_count(void)
{
    return profile_get_count(OUTPUT_TARGET_USB_DEVICE);
}

static uint8_t get_custom_count(void)
{
    flash_t* settings = flash_get_settings();
    return settings ? settings->custom_profile_count : 0;
}

// Get total profile count (for unified indexing)
static uint8_t get_total_count(void)
{
    uint8_t builtin = get_builtin_count();
    uint8_t custom = get_custom_count();
    // If no built-in profiles, we show a virtual "Default" at index 0
    return (builtin > 0 ? builtin : 1) + custom;
}

// Convert unified index to custom profile index, returns -1 if not a custom profile
static int unified_to_custom_index(int unified_idx)
{
    uint8_t builtin = get_builtin_count();
    int custom_start = (builtin > 0) ? builtin : 1;
    if (unified_idx >= custom_start) {
        return unified_idx - custom_start;
    }
    return -1;  // Not a custom profile
}

// Convert custom profile index to unified index
static int custom_to_unified_index(int custom_idx)
{
    uint8_t builtin = get_builtin_count();
    int custom_start = (builtin > 0) ? builtin : 1;
    return custom_start + custom_idx;
}

// Check if unified index is a built-in profile
static bool is_builtin_profile(int unified_idx)
{
    uint8_t builtin = get_builtin_count();
    if (builtin > 0) {
        return unified_idx < builtin;
    }
    return unified_idx == 0;  // Virtual default
}

// PROFILE.LIST - Unified list of all profiles
static void cmd_profile_list(const char* json)
{
    (void)json;
    uint8_t builtin_count = get_builtin_count();
    flash_t* settings = flash_get_settings();
    uint8_t custom_count = settings ? settings->custom_profile_count : 0;

    // Determine active profile in unified indexing
    int active;
    if (builtin_count > 0) {
        // Use built-in profile active index, or offset for custom
        active = profile_get_active_index(OUTPUT_TARGET_USB_DEVICE);
        // TODO: Handle custom profile selection for apps with built-in profiles
    } else {
        // No built-in profiles - use flash active index (already 0-based with virtual default)
        active = settings ? settings->active_profile_index : 0;
    }

    int pos = snprintf(response_buf, sizeof(response_buf),
                       "{\"ok\":true,\"active\":%d,\"profiles\":[", active);

    int idx = 0;

    // Add built-in profiles (or virtual Default)
    if (builtin_count > 0) {
        for (int i = 0; i < builtin_count && pos < (int)sizeof(response_buf) - 80; i++) {
            const char* name = profile_get_name(OUTPUT_TARGET_USB_DEVICE, i);
            if (idx > 0) pos += snprintf(response_buf + pos, sizeof(response_buf) - pos, ",");
            pos += snprintf(response_buf + pos, sizeof(response_buf) - pos,
                            "{\"index\":%d,\"name\":\"%s\",\"builtin\":true,\"editable\":false}",
                            idx, name ? name : "Default");
            idx++;
        }
    } else {
        // Virtual Default for apps without built-in profiles
        pos += snprintf(response_buf + pos, sizeof(response_buf) - pos,
                        "{\"index\":0,\"name\":\"Default\",\"builtin\":true,\"editable\":false}");
        idx = 1;
    }

    // Add custom profiles
    for (int i = 0; i < custom_count && i < CUSTOM_PROFILE_MAX_COUNT && pos < (int)sizeof(response_buf) - 80; i++) {
        const custom_profile_t* p = &settings->profiles[i];
        pos += snprintf(response_buf + pos, sizeof(response_buf) - pos,
                        ",{\"index\":%d,\"name\":\"%.11s\",\"builtin\":false,\"editable\":true}",
                        idx, p->name);
        idx++;
    }

    snprintf(response_buf + pos, sizeof(response_buf) - pos, "]}");
    send_json(response_buf);
}

// PROFILE.GET - Get profile details
static void cmd_profile_get(const char* json)
{
    int index;
    if (!json_get_int(json, "index", &index)) {
        // No index - return active profile info
        uint8_t builtin_count = get_builtin_count();
        int active;
        if (builtin_count > 0) {
            active = profile_get_active_index(OUTPUT_TARGET_USB_DEVICE);
        } else {
            flash_t* settings = flash_get_settings();
            active = settings ? settings->active_profile_index : 0;
        }
        index = active;
    }

    uint8_t builtin_count = get_builtin_count();
    flash_t* settings = flash_get_settings();
    uint8_t custom_count = settings ? settings->custom_profile_count : 0;
    uint8_t total = get_total_count();

    if (index < 0 || index >= total) {
        send_error("invalid index");
        return;
    }

    bool builtin = is_builtin_profile(index);
    int custom_idx = unified_to_custom_index(index);

    if (builtin) {
        // Built-in profile (or virtual Default)
        const char* name;
        if (builtin_count > 0) {
            name = profile_get_name(OUTPUT_TARGET_USB_DEVICE, index);
        } else {
            name = "Default";
        }
        // Built-in profiles don't expose button_map (it's compiled in)
        snprintf(response_buf, sizeof(response_buf),
                 "{\"ok\":true,\"index\":%d,\"name\":\"%s\",\"builtin\":true,\"editable\":false}",
                 index, name ? name : "Default");
    } else {
        // Custom profile
        if (custom_idx < 0 || custom_idx >= custom_count) {
            send_error("invalid index");
            return;
        }
        const custom_profile_t* p = &settings->profiles[custom_idx];

        // Build button map array string
        char map_str[100];
        int mpos = 0;
        for (int i = 0; i < CUSTOM_PROFILE_BUTTON_COUNT; i++) {
            if (i > 0) mpos += snprintf(map_str + mpos, sizeof(map_str) - mpos, ",");
            mpos += snprintf(map_str + mpos, sizeof(map_str) - mpos, "%d", p->button_map[i]);
        }

        snprintf(response_buf, sizeof(response_buf),
                 "{\"ok\":true,\"index\":%d,\"name\":\"%.11s\",\"builtin\":false,\"editable\":true,"
                 "\"button_map\":[%s],"
                 "\"left_stick_sens\":%d,\"right_stick_sens\":%d,\"flags\":%d,\"socd_mode\":%d}",
                 index, p->name, map_str,
                 p->left_stick_sens, p->right_stick_sens, p->flags, p->socd_mode);
    }
    send_json(response_buf);
}

// PROFILE.SELECT - Select active profile (unified index)
static void cmd_profile_set(const char* json)
{
    int index;
    if (!json_get_int(json, "index", &index)) {
        send_error("missing index");
        return;
    }

    uint8_t total = get_total_count();
    if (index < 0 || index >= total) {
        send_error("invalid index");
        return;
    }

    uint8_t builtin_count = get_builtin_count();

    if (builtin_count > 0 && index < builtin_count) {
        // Select built-in profile
        profile_set_active(OUTPUT_TARGET_USB_DEVICE, index);
        const char* name = profile_get_name(OUTPUT_TARGET_USB_DEVICE, index);
        snprintf(response_buf, sizeof(response_buf),
                 "{\"ok\":true,\"index\":%d,\"name\":\"%s\"}",
                 index, name ? name : "Default");
    } else {
        // Select custom profile (or default for apps without built-in)
        int custom_idx = unified_to_custom_index(index);
        // For flash, index 0 = default, 1+ = custom profiles
        int flash_idx = (custom_idx < 0) ? 0 : custom_idx + 1;
        flash_set_active_profile_index((uint8_t)flash_idx);

        flash_t* settings = flash_get_settings();
        const char* name = "Default";
        if (custom_idx >= 0 && settings && custom_idx < settings->custom_profile_count) {
            name = settings->profiles[custom_idx].name;
        }
        snprintf(response_buf, sizeof(response_buf),
                 "{\"ok\":true,\"index\":%d,\"name\":\"%.11s\"}",
                 index, name);
    }
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

// Parse JSON array of integers: [1,2,3,...]
// Returns number of values parsed
static int json_get_int_array(const char* json, const char* key,
                               uint8_t* out, int max_count)
{
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":[", key);

    const char* start = strstr(json, search);
    if (!start) return 0;

    start += strlen(search);
    int count = 0;

    while (*start && count < max_count) {
        // Skip whitespace
        while (*start == ' ' || *start == '\t') start++;

        if (*start == ']') break;

        // Parse number
        if (*start == '-' || (*start >= '0' && *start <= '9')) {
            out[count++] = (uint8_t)atoi(start);
            // Skip past number
            while (*start == '-' || (*start >= '0' && *start <= '9')) start++;
        }

        // Skip comma
        while (*start == ' ' || *start == '\t') start++;
        if (*start == ',') start++;
    }

    return count;
}

// PROFILE.SAVE - Create or update custom profile (unified index)
// index=255 creates a new profile
static void cmd_profile_save(const char* json)
{
    int index;
    if (!json_get_int(json, "index", &index)) {
        send_error("missing index");
        return;
    }

    // Can't modify built-in profiles
    if (index != 255 && is_builtin_profile(index)) {
        send_error("cannot modify built-in profile");
        return;
    }

    // Use runtime settings to keep in sync with active profile
    flash_t* settings = flash_get_settings();
    if (!settings) {
        send_error("flash not initialized");
        return;
    }

    // Index 255 = create new
    int custom_idx;
    bool is_new = false;
    if (index == 255) {
        if (settings->custom_profile_count >= CUSTOM_PROFILE_MAX_COUNT) {
            send_error("max profiles reached");
            return;
        }
        custom_idx = settings->custom_profile_count;
        settings->custom_profile_count++;
        index = custom_to_unified_index(custom_idx);
        is_new = true;
    } else {
        custom_idx = unified_to_custom_index(index);
        if (custom_idx < 0 || custom_idx >= settings->custom_profile_count) {
            send_error("invalid index");
            return;
        }
    }

    custom_profile_t* p = &settings->profiles[custom_idx];

    // Get name
    int name_len;
    const char* name = json_get_string(json, "name", &name_len);
    if (name && name_len > 0) {
        int copy_len = name_len < CUSTOM_PROFILE_NAME_LEN - 1 ? name_len : CUSTOM_PROFILE_NAME_LEN - 1;
        memcpy(p->name, name, copy_len);
        p->name[copy_len] = '\0';
    } else if (is_new) {
        // New profile without name
        snprintf(p->name, CUSTOM_PROFILE_NAME_LEN, "Custom %d", custom_idx + 1);
    }

    // Get button map
    uint8_t button_map[CUSTOM_PROFILE_BUTTON_COUNT];
    int map_count = json_get_int_array(json, "button_map", button_map, CUSTOM_PROFILE_BUTTON_COUNT);
    if (map_count == CUSTOM_PROFILE_BUTTON_COUNT) {
        memcpy(p->button_map, button_map, CUSTOM_PROFILE_BUTTON_COUNT);
    } else if (map_count == 0 && is_new) {
        // New profile - initialize to passthrough
        memset(p->button_map, BUTTON_MAP_PASSTHROUGH, CUSTOM_PROFILE_BUTTON_COUNT);
    }

    // Get stick sensitivities
    int sens;
    if (json_get_int(json, "left_stick_sens", &sens)) {
        p->left_stick_sens = (uint8_t)(sens > 200 ? 200 : (sens < 0 ? 0 : sens));
    } else if (is_new) {
        p->left_stick_sens = 100;
    }

    if (json_get_int(json, "right_stick_sens", &sens)) {
        p->right_stick_sens = (uint8_t)(sens > 200 ? 200 : (sens < 0 ? 0 : sens));
    } else if (is_new) {
        p->right_stick_sens = 100;
    }

    // Get flags
    int flags;
    if (json_get_int(json, "flags", &flags)) {
        p->flags = (uint8_t)flags;
    }

    // Get SOCD mode
    int socd;
    if (json_get_int(json, "socd_mode", &socd)) {
        p->socd_mode = (uint8_t)(socd > 3 ? 0 : (socd < 0 ? 0 : socd));
    } else if (is_new) {
        p->socd_mode = 0;  // Default to passthrough
    }

    // Save to flash (runtime settings are already updated)
    flash_save(settings);

    snprintf(response_buf, sizeof(response_buf),
             "{\"ok\":true,\"index\":%d,\"name\":\"%.11s\"}", index, p->name);
    send_json(response_buf);
}

// PROFILE.DELETE - Delete custom profile (unified index)
static void cmd_profile_delete(const char* json)
{
    int index;
    if (!json_get_int(json, "index", &index)) {
        send_error("missing index");
        return;
    }

    // Can't delete built-in profiles
    if (is_builtin_profile(index)) {
        send_error("cannot delete built-in profile");
        return;
    }

    // Use runtime settings to keep in sync
    flash_t* settings = flash_get_settings();
    if (!settings) {
        send_error("flash not initialized");
        return;
    }

    int custom_idx = unified_to_custom_index(index);
    if (custom_idx < 0 || custom_idx >= settings->custom_profile_count) {
        send_error("invalid index");
        return;
    }

    // Shift remaining profiles down
    for (int i = custom_idx; i < settings->custom_profile_count - 1; i++) {
        memcpy(&settings->profiles[i], &settings->profiles[i + 1], sizeof(custom_profile_t));
    }
    settings->custom_profile_count--;

    // Clear the last slot
    memset(&settings->profiles[settings->custom_profile_count], 0, sizeof(custom_profile_t));

    // Adjust active profile if needed (using flash index: 0=default, 1+=custom)
    uint8_t flash_idx = custom_idx + 1;  // Convert custom_idx to flash index
    if (settings->active_profile_index > flash_idx) {
        settings->active_profile_index--;
    } else if (settings->active_profile_index == flash_idx) {
        settings->active_profile_index = 0;  // Switch to default
    }

    // Save to flash (runtime settings are already updated)
    flash_save(settings);
    send_ok();
}

// PROFILE.CLONE - Clone any profile (built-in or custom) to new custom profile
static void cmd_profile_clone(const char* json)
{
    int source_index;
    if (!json_get_int(json, "index", &source_index)) {
        send_error("missing index");
        return;
    }

    uint8_t total = get_total_count();
    if (source_index < 0 || source_index >= total) {
        send_error("invalid source index");
        return;
    }

    flash_t* settings = flash_get_settings();
    if (!settings) {
        send_error("flash not initialized");
        return;
    }

    if (settings->custom_profile_count >= CUSTOM_PROFILE_MAX_COUNT) {
        send_error("max profiles reached");
        return;
    }

    // Create new custom profile
    int new_custom_idx = settings->custom_profile_count;
    settings->custom_profile_count++;
    custom_profile_t* new_profile = &settings->profiles[new_custom_idx];

    // Generate name for the new profile
    char new_name[CUSTOM_PROFILE_NAME_LEN];
    int name_len;
    const char* json_name = json_get_string(json, "name", &name_len);
    if (json_name && name_len > 0) {
        int copy_len = name_len < CUSTOM_PROFILE_NAME_LEN - 1 ? name_len : CUSTOM_PROFILE_NAME_LEN - 1;
        memcpy(new_name, json_name, copy_len);
        new_name[copy_len] = '\0';
    } else {
        // Generate name based on source
        if (is_builtin_profile(source_index)) {
            const char* src_name = (get_builtin_count() > 0) ?
                profile_get_name(OUTPUT_TARGET_USB_DEVICE, source_index) : "Default";
            snprintf(new_name, CUSTOM_PROFILE_NAME_LEN, "%.7s Copy", src_name ? src_name : "Default");
        } else {
            int src_custom_idx = unified_to_custom_index(source_index);
            if (src_custom_idx >= 0 && src_custom_idx < settings->custom_profile_count - 1) {
                snprintf(new_name, CUSTOM_PROFILE_NAME_LEN, "%.7s Copy",
                         settings->profiles[src_custom_idx].name);
            } else {
                snprintf(new_name, CUSTOM_PROFILE_NAME_LEN, "Custom %d", new_custom_idx + 1);
            }
        }
    }

    // Initialize the new profile with the generated name
    custom_profile_init(new_profile, new_name);

    // Copy settings from source if it's a custom profile
    if (!is_builtin_profile(source_index)) {
        int src_custom_idx = unified_to_custom_index(source_index);
        if (src_custom_idx >= 0 && src_custom_idx < settings->custom_profile_count - 1) {
            const custom_profile_t* src = &settings->profiles[src_custom_idx];
            memcpy(new_profile->button_map, src->button_map, CUSTOM_PROFILE_BUTTON_COUNT);
            new_profile->left_stick_sens = src->left_stick_sens;
            new_profile->right_stick_sens = src->right_stick_sens;
            new_profile->flags = src->flags;
        }
    }
    // For built-in profiles, keep passthrough settings (already initialized)

    // Save to flash
    flash_save(settings);

    int new_unified_idx = custom_to_unified_index(new_custom_idx);
    snprintf(response_buf, sizeof(response_buf),
             "{\"ok\":true,\"index\":%d,\"name\":\"%.11s\"}", new_unified_idx, new_profile->name);
    send_json(response_buf);
}

// Legacy alias for CPROFILE.SELECT (deprecated, use PROFILE.SET)
static void cmd_cprofile_select(const char* json)
{
    // Redirect to unified PROFILE.SET
    cmd_profile_set(json);
}

// Legacy alias for CPROFILE.LIST (deprecated, use PROFILE.LIST)
static void cmd_cprofile_list(const char* json)
{
    // Redirect to unified PROFILE.LIST
    cmd_profile_list(json);
}

// Legacy alias for CPROFILE.GET (deprecated, use PROFILE.GET)
static void cmd_cprofile_get(const char* json)
{
    // Redirect to unified PROFILE.GET
    cmd_profile_get(json);
}

// Legacy alias for CPROFILE.SET (deprecated, use PROFILE.SAVE)
static void cmd_cprofile_set(const char* json)
{
    // Redirect to unified PROFILE.SAVE
    cmd_profile_save(json);
}

// Legacy alias for CPROFILE.DELETE (deprecated, use PROFILE.DELETE)
static void cmd_cprofile_delete(const char* json)
{
    // Redirect to unified PROFILE.DELETE
    cmd_profile_delete(json);
}

// ============================================================================
// DEBUG LOG STREAM COMMAND
// ============================================================================

static void cmd_debug_stream(const char* json)
{
    bool enable;
    if (!json_get_bool(json, "enable", &enable)) {
        send_error("missing enable");
        return;
    }

    protocol_ctx.log_streaming = enable;

    if (!enable) {
        // Drain any stale data when disabling
        log_tail = log_head;
    } else {
        // Flush stale data so only fresh logs are streamed
        log_tail = log_head;
    }

    snprintf(response_buf, sizeof(response_buf),
             "{\"ok\":true,\"streaming\":%s}",
             enable ? "true" : "false");
    send_json(response_buf);

    // Print after response so next drain picks it up
    if (enable) {
        printf("[LOG] Debug log streaming started\n");
    }
}

// ============================================================================
// SETTINGS COMMANDS
// ============================================================================

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

#ifdef ENABLE_BTSTACK
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

static void cmd_wiimote_orient_get(const char* json)
{
    (void)json;
    uint8_t mode = wiimote_get_orient_mode();
    snprintf(response_buf, sizeof(response_buf),
             "{\"mode\":%d,\"name\":\"%s\"}",
             mode, wiimote_get_orient_mode_name(mode));
    send_json(response_buf);
}

static void cmd_wiimote_orient_set(const char* json)
{
    int mode;
    if (!json_get_int(json, "mode", &mode)) {
        send_error("missing mode");
        return;
    }
    if (mode < 0 || mode > 2) {
        send_error("invalid mode (0=auto, 1=horizontal, 2=vertical)");
        return;
    }
    wiimote_set_orient_mode((uint8_t)mode);

    // Save to flash
    flash_t flash_data;
    if (flash_load(&flash_data)) {
        flash_data.wiimote_orient_mode = (uint8_t)mode;
        flash_save(&flash_data);
    }

    snprintf(response_buf, sizeof(response_buf),
             "{\"mode\":%d,\"name\":\"%s\"}",
             mode, wiimote_get_orient_mode_name(mode));
    send_json(response_buf);
}
#endif

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================

// PLAYERS.LIST - Get list of connected players/controllers
static void cmd_players_list(const char* json)
{
    (void) json;

    // Build JSON array of players
    int len = snprintf(response_buf, sizeof(response_buf), "{\"count\":%d,\"players\":[", playersCount);

    for (int i = 0; i < playersCount && i < MAX_PLAYERS; i++) {
        if (players[i].dev_addr == -1) continue;  // Skip empty slots

        const char* name = get_player_name(i);
        const char* transport;
        switch (players[i].transport) {
            case INPUT_TRANSPORT_USB: transport = "usb"; break;
            case INPUT_TRANSPORT_BT_CLASSIC: transport = "bt_classic"; break;
            case INPUT_TRANSPORT_BT_BLE: transport = "bt_ble"; break;
            case INPUT_TRANSPORT_NATIVE: transport = "native"; break;
            default: transport = "unknown"; break;
        }

        // Most USB/BT controllers support rumble
        bool supports_rumble = (players[i].transport == INPUT_TRANSPORT_USB ||
                               players[i].transport == INPUT_TRANSPORT_BT_CLASSIC ||
                               players[i].transport == INPUT_TRANSPORT_BT_BLE);

        len += snprintf(response_buf + len, sizeof(response_buf) - len,
                        "%s{\"slot\":%d,\"name\":\"%s\",\"transport\":\"%s\",\"rumble\":%s}",
                        i > 0 ? "," : "",
                        i,
                        name ? name : "Unknown",
                        transport,
                        supports_rumble ? "true" : "false");
    }

    snprintf(response_buf + len, sizeof(response_buf) - len, "]}");
    send_json(response_buf);
}

// ============================================================================
// RUMBLE TEST
// ============================================================================

// State for auto-stopping rumble after duration
static struct {
    bool active;
    uint32_t start_ms;
    uint32_t duration_ms;
    int player;  // -1 for all players
} rumble_test_state = {0};

// RUMBLE.TEST - Test rumble on a player's controller
// {"cmd":"RUMBLE.TEST","player":0,"left":255,"right":255,"duration":500}
// player: 0-based index, or -1 for all players
// left/right: motor intensity 0-255
// duration: optional, ms (default 500, max 5000)
static void cmd_rumble_test(const char* json)
{
    int player = 0;
    int left = 128;
    int right = 128;
    int duration = 500;

    json_get_int(json, "player", &player);
    json_get_int(json, "left", &left);
    json_get_int(json, "right", &right);
    json_get_int(json, "duration", &duration);

    // Clamp values
    if (left < 0) left = 0;
    if (left > 255) left = 255;
    if (right < 0) right = 0;
    if (right > 255) right = 255;
    if (duration < 0) duration = 0;
    if (duration > 5000) duration = 5000;

    printf("[CDC] RUMBLE.TEST: player=%d left=%d right=%d duration=%d\n",
           player, left, right, duration);

    // Apply rumble via feedback system
    if (player == -1) {
        // All players
        for (int i = 0; i < playersCount && i < MAX_PLAYERS; i++) {
            if (players[i].dev_addr != -1) {
                feedback_set_rumble_internal(i, (uint8_t)left, (uint8_t)right);
            }
        }
    } else if (player >= 0 && player < playersCount && players[player].dev_addr != -1) {
        feedback_set_rumble_internal(player, (uint8_t)left, (uint8_t)right);
    } else {
        send_error("invalid player");
        return;
    }

    // Store state for auto-stop
    if (duration > 0 && (left > 0 || right > 0)) {
        rumble_test_state.active = true;
        rumble_test_state.start_ms = to_ms_since_boot(get_absolute_time());
        rumble_test_state.duration_ms = duration;
        rumble_test_state.player = player;
    }

    snprintf(response_buf, sizeof(response_buf),
             "{\"ok\":true,\"player\":%d,\"left\":%d,\"right\":%d,\"duration\":%d}",
             player, left, right, duration);
    send_json(response_buf);
}

// RUMBLE.STOP - Stop rumble on a player's controller
static void cmd_rumble_stop(const char* json)
{
    int player = -1;
    json_get_int(json, "player", &player);

    if (player == -1) {
        // All players
        for (int i = 0; i < playersCount && i < MAX_PLAYERS; i++) {
            if (players[i].dev_addr != -1) {
                feedback_set_rumble_internal(i, 0, 0);
            }
        }
    } else if (player >= 0 && player < playersCount) {
        feedback_set_rumble_internal(player, 0, 0);
    }

    rumble_test_state.active = false;
    send_ok();
}

// Call from main loop to auto-stop rumble after duration and drain log buffer
void cdc_commands_task(void)
{
    if (rumble_test_state.active) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - rumble_test_state.start_ms >= rumble_test_state.duration_ms) {
            // Stop rumble
            if (rumble_test_state.player == -1) {
                for (int i = 0; i < MAX_PLAYERS; i++) {
                    feedback_set_rumble_internal(i, 0, 0);
                }
            } else {
                feedback_set_rumble_internal(rumble_test_state.player, 0, 0);
            }
            rumble_test_state.active = false;
            printf("[CDC] RUMBLE.TEST: auto-stopped after %lu ms\n",
                   (unsigned long)rumble_test_state.duration_ms);
        }
    }

    // Drain log ring buffer and send as events
    if (protocol_ctx.log_streaming && log_head != log_tail) {
        // Collect up to 256 raw bytes from ring buffer
        char raw[256];
        int raw_len = 0;
        while (log_tail != log_head && raw_len < (int)sizeof(raw)) {
            raw[raw_len++] = log_ring[log_tail];
            log_tail = (log_tail + 1) % LOG_BUF_SIZE;
        }

        if (raw_len > 0) {
            // Build JSON event with escaped message
            // Prefix: {"type":"log","msg":"  = 22 chars
            // Suffix: "}                     = 2 chars
            // Max overhead per char: 2 (for \n, \", \\)
            int pos = 0;
            pos += snprintf(log_event_buf + pos, sizeof(log_event_buf) - pos,
                            "{\"type\":\"log\",\"msg\":\"");
            for (int i = 0; i < raw_len && pos < (int)sizeof(log_event_buf) - 10; i++) {
                char c = raw[i];
                if (c == '\\') {
                    log_event_buf[pos++] = '\\';
                    log_event_buf[pos++] = '\\';
                } else if (c == '"') {
                    log_event_buf[pos++] = '\\';
                    log_event_buf[pos++] = '"';
                } else if (c == '\n') {
                    log_event_buf[pos++] = '\\';
                    log_event_buf[pos++] = 'n';
                } else if (c == '\r') {
                    log_event_buf[pos++] = '\\';
                    log_event_buf[pos++] = 'r';
                } else if (c == '\t') {
                    log_event_buf[pos++] = '\\';
                    log_event_buf[pos++] = 't';
                } else if (c >= 0x20) {
                    log_event_buf[pos++] = c;
                }
                // Drop other control characters
            }
            log_event_buf[pos++] = '"';
            log_event_buf[pos++] = '}';
            log_event_buf[pos] = '\0';

            cdc_protocol_send_event(&protocol_ctx, log_event_buf);
        }
    }
}

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
    // Unified profile commands
    {"PROFILE.LIST", cmd_profile_list},
    {"PROFILE.GET", cmd_profile_get},
    {"PROFILE.SET", cmd_profile_set},
    {"PROFILE.SAVE", cmd_profile_save},
    {"PROFILE.DELETE", cmd_profile_delete},
    {"PROFILE.CLONE", cmd_profile_clone},
    // Legacy CPROFILE.* aliases (deprecated - redirect to unified commands)
    {"CPROFILE.LIST", cmd_cprofile_list},
    {"CPROFILE.GET", cmd_cprofile_get},
    {"CPROFILE.SET", cmd_cprofile_set},
    {"CPROFILE.DELETE", cmd_cprofile_delete},
    {"CPROFILE.SELECT", cmd_cprofile_select},
    {"INPUT.STREAM", cmd_input_stream},
    {"DEBUG.STREAM", cmd_debug_stream},
    {"SETTINGS.GET", cmd_settings_get},
    {"SETTINGS.RESET", cmd_settings_reset},
    // Player management
    {"PLAYERS.LIST", cmd_players_list},
    // Rumble testing
    {"RUMBLE.TEST", cmd_rumble_test},
    {"RUMBLE.STOP", cmd_rumble_stop},
#ifdef ENABLE_BTSTACK
    {"BT.STATUS", cmd_bt_status},
    {"BT.BONDS.CLEAR", cmd_bt_bonds_clear},
    {"WIIMOTE.ORIENT.GET", cmd_wiimote_orient_get},
    {"WIIMOTE.ORIENT.SET", cmd_wiimote_orient_set},
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

    // Register stdio driver to capture printf output into ring buffer
    stdio_set_driver_enabled(&log_stdio_driver, true);

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

    // Input axes from input_event_t (now contiguous):
    // [0]=LX, [1]=LY, [2]=RX, [3]=RY, [4]=L2, [5]=R2, [6]=RZ
    snprintf(response_buf, sizeof(response_buf),
             "{\"type\":\"input\",\"buttons\":%lu,\"axes\":[%d,%d,%d,%d,%d,%d,%d]}",
             (unsigned long)buttons,
             axes[0], axes[1], axes[2], axes[3], axes[4], axes[5], axes[6]);
    cdc_protocol_send_event(&protocol_ctx, response_buf);
}

void cdc_commands_send_output_event(uint32_t buttons, const uint8_t* axes)
{
    if (!protocol_ctx.input_streaming) return;

    snprintf(response_buf, sizeof(response_buf),
             "{\"type\":\"output\",\"buttons\":%lu,\"axes\":[%d,%d,%d,%d,%d,%d,%d]}",
             (unsigned long)buttons,
             axes[0], axes[1], axes[2], axes[3], axes[4], axes[5], axes[6]);
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
