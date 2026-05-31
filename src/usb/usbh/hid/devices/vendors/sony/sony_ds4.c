// sony_ds4.c
#include "sony_ds4.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "platform/platform.h"
#include "app_config.h"
#include <joypad/devices/sony/ds4.h>
#include <joypad/feedback.h>
#include <string.h>

static uint16_t tpadLastPos;
static bool tpadDragging;

// DualShock 4 instance state
typedef struct TU_ATTR_PACKED
{
  uint8_t rumble;
  uint8_t player;
  uint8_t led_r, led_g, led_b;
} ds4_instance_t;

// Cached device report properties on mount
typedef struct TU_ATTR_PACKED
{
  ds4_instance_t instances[CFG_TUH_HID];
} ds4_device_t;

static ds4_device_t ds4_devices[MAX_DEVICES] = { 0 };

// ============================================================================
// PS4 AUTH PASSTHROUGH STATE
// ============================================================================

// Auth buffer sizes (paged transfers)
#define DS4_AUTH_PAGE_SIZE       56   // Bytes per page (0x38)
#define DS4_AUTH_NONCE_PAGES     5    // Pages 0-4
#define DS4_AUTH_SIGNATURE_PAGES 19   // Pages 0-18
#define DS4_AUTH_NONCE_SIZE      (DS4_AUTH_PAGE_SIZE * DS4_AUTH_NONCE_PAGES)  // 280 bytes
#define DS4_AUTH_SIGNATURE_SIZE  (DS4_AUTH_PAGE_SIZE * DS4_AUTH_SIGNATURE_PAGES) // 1064 bytes
#define DS4_AUTH_STATUS_SIZE     16   // Status report size
#define DS4_AUTH_REPORT_SIZE     64   // Full report size with report ID

// Internal auth states (matching hid-remapper)
typedef enum {
    AUTH_IDLE = 0,
    AUTH_SENDING_RESET,      // Request 0xF3 from DS4 first
    AUTH_SENDING_NONCE,      // Send nonce pages to DS4
    AUTH_WAITING_FOR_SIG,    // Poll 0xF2 status
    AUTH_RECEIVING_SIG,      // Fetch 0xF1 signature pages
} auth_internal_state_t;

// Auth passthrough state
static struct {
    ds4_auth_state_t state;         // External state for API
    auth_internal_state_t internal; // Internal state machine
    uint8_t dev_addr;               // DS4 device address for auth
    uint8_t instance;               // DS4 instance for auth
    bool ds4_available;             // Is a DS4 connected for auth?
    bool busy;                      // Waiting for async operation

    // Nonce ID from console (sequence number)
    uint8_t nonce_id;

    // Nonce from console (280 bytes total - 5 pages of 56)
    uint8_t nonce_buffer[DS4_AUTH_NONCE_SIZE];
    uint8_t nonce_pages_received;   // Pages received from console (0-5)
    uint8_t nonce_page_sending;     // Page being sent to DS4 (0-4)

    // Signature from DS4 (1064 bytes total)
    uint8_t signature_buffer[DS4_AUTH_SIGNATURE_SIZE];
    uint8_t signature_pages_fetched;   // Pages fetched from DS4 (0-19)
    bool signature_ready;              // All 19 pages received

    // Page counter for returning signature to console
    uint8_t signature_page_returning;  // Next page to return (0-18)

    // Temp buffer for HID reports
    uint8_t report_buffer[DS4_AUTH_REPORT_SIZE];
} ds4_auth = { 0 };

// check if device is Sony PlayStation 4 controllers
bool is_sony_ds4(uint16_t vid, uint16_t pid) {
    return joypad_is_sony_ds4(vid, pid);
}

// process usb hid input reports
void input_sony_ds4(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
    input_event_t event;
    if (!joypad_parse_sony_ds4(report, len, &event)) return;

    event.dev_addr = dev_addr;
    event.instance = instance;
    event.timestamp_us = (uint64_t)platform_time_ms() * 1000ULL;

    // Touchpad horizontal-swipe delta — firmware-side spinner mapping built
    // on the absolute touch[].x that the libjoypad parser already filled.
    if (event.has_touch && event.touch[0].active) {
        uint16_t tx = event.touch[0].x;
        if (tpadDragging) {
            int16_t delta = (int16_t)tx - (int16_t)tpadLastPos;
            if (delta >  12) delta =  12;
            if (delta < -12) delta = -12;
            event.delta_x = (int8_t)delta;
        }
        tpadLastPos = tx;
        tpadDragging = true;
    } else {
        tpadDragging = false;
    }

    // Console-output safety: avoid emitting raw 0 on analog axes (some console
    // emulation paths interpret 0 as "disconnected").
    ensureAllNonZero(&event.analog[ANALOG_LX], &event.analog[ANALOG_LY],
                     &event.analog[ANALOG_RX], &event.analog[ANALOG_RY]);

    // Radial dead zone (firmware-specific; small circle, ~5% of half-range).
    // DS4 already filters residual stick noise so this is just to silence
    // the last 1-2 ticks of drift.
    const int dz_radius = 6;
    const int dz_sq = dz_radius * dz_radius;
    int dx1 = (int)event.analog[ANALOG_LX] - 128;
    int dy1 = (int)event.analog[ANALOG_LY] - 128;
    if (dx1*dx1 + dy1*dy1 < dz_sq) { event.analog[ANALOG_LX] = 128; event.analog[ANALOG_LY] = 128; }
    int dx2 = (int)event.analog[ANALOG_RX] - 128;
    int dy2 = (int)event.analog[ANALOG_RY] - 128;
    if (dx2*dx2 + dy2*dy2 < dz_sq) { event.analog[ANALOG_RX] = 128; event.analog[ANALOG_RY] = 128; }

    router_submit_input(&event);
}

// process usb hid output reports
void output_sony_ds4(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
    joypad_feedback_t fb;
    joypad_feedback_init(&fb);

    // --- Lightbar RGB: player slot LED color or app default ---
    uint8_t r_val, g_val, b_val;
    int8_t player_idx = find_player_index(dev_addr, instance);
    feedback_state_t* feedback = (player_idx >= 0) ? feedback_get_state(player_idx) : NULL;

    if (feedback && (feedback->led.r || feedback->led.g || feedback->led.b)) {
        r_val = feedback->led.r;
        g_val = feedback->led.g;
        b_val = feedback->led.b;
    } else {
        switch (config->player_index + 1) {
            case 1: r_val = LED_P1_R; g_val = LED_P1_G; b_val = LED_P1_B; break;
            case 2: r_val = LED_P2_R; g_val = LED_P2_G; b_val = LED_P2_B; break;
            case 3: r_val = LED_P3_R; g_val = LED_P3_G; b_val = LED_P3_B; break;
            case 4: r_val = LED_P4_R; g_val = LED_P4_G; b_val = LED_P4_B; break;
            case 5: r_val = LED_P5_R; g_val = LED_P5_G; b_val = LED_P5_B; break;
            case 6: r_val = LED_P6_R; g_val = LED_P6_G; b_val = LED_P6_B; break;
            case 7: r_val = LED_P7_R; g_val = LED_P7_G; b_val = LED_P7_B; break;
            default: r_val = LED_DEFAULT_R; g_val = LED_DEFAULT_G; b_val = LED_DEFAULT_B; break;
        }
    }

    // Test pattern override.
    if (config->player_index + 1 && config->test) {
        r_val = config->test;
        g_val = (config->test % 2 == 0) ? (uint8_t)(config->test + 64) : 0;
        b_val = (config->test % 2 == 0) ? 0 : (uint8_t)(config->test + 128);
    }

    fb.lightbar_dirty = true;
    fb.lightbar.r = r_val;
    fb.lightbar.g = g_val;
    fb.lightbar.b = b_val;

    fb.rumble_dirty = true;
    fb.rumble_low  = config->rumble ? 192 : 0;
    fb.rumble_high = config->rumble ? 192 : 0;

    // Only send when something changed (or test mode forces it).
    if (ds4_devices[dev_addr].instances[instance].rumble != config->rumble ||
        ds4_devices[dev_addr].instances[instance].led_r  != r_val ||
        ds4_devices[dev_addr].instances[instance].led_g  != g_val ||
        ds4_devices[dev_addr].instances[instance].led_b  != b_val ||
        config->test)
    {
        ds4_devices[dev_addr].instances[instance].rumble = config->rumble;
        ds4_devices[dev_addr].instances[instance].led_r  = r_val;
        ds4_devices[dev_addr].instances[instance].led_g  = g_val;
        ds4_devices[dev_addr].instances[instance].led_b  = b_val;

        uint8_t buf[JOYPAD_SONY_DS4_FEEDBACK_PAYLOAD_LEN];
        uint16_t n = joypad_build_sony_ds4_feedback(&fb, buf, sizeof(buf));
        if (n > 0) {
            tuh_hid_send_report(dev_addr, instance, JOYPAD_SONY_DS4_FEEDBACK_REPORT_ID, buf, n);
        }
    }
}

// process usb hid output reports
void task_sony_ds4(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
  const uint32_t interval_ms = 20;
  static uint32_t start_ms = 0;

  uint32_t current_time_ms = platform_time_ms();
  if (current_time_ms - start_ms >= interval_ms) {
    start_ms = current_time_ms;
    output_sony_ds4(dev_addr, instance, config);
  }
}

// resets default values in case devices are hotswapped
void unmount_sony_ds4(uint8_t dev_addr, uint8_t instance)
{
  ds4_devices[dev_addr].instances[instance].rumble = 0;
  ds4_devices[dev_addr].instances[instance].player = 0xff;
}

DeviceInterface sony_ds4_interface = {
  .name = "Sony DualShock 4",
  .is_device = is_sony_ds4,
  .process = input_sony_ds4,
  .task = task_sony_ds4,
  .unmount = unmount_sony_ds4,
};

// ============================================================================
// PS4 AUTH PASSTHROUGH IMPLEMENTATION
// ============================================================================

// Called when a DS4 is mounted - register it for auth
void ds4_auth_register(uint8_t dev_addr, uint8_t instance) {
    if (!ds4_auth.ds4_available) {
        ds4_auth.dev_addr = dev_addr;
        ds4_auth.instance = instance;
        ds4_auth.ds4_available = true;
        ds4_auth.state = DS4_AUTH_STATE_IDLE;
        printf("[DS4 Auth] Registered DS4 at %d:%d for auth passthrough\n", dev_addr, instance);
    }
}

// Called when a DS4 is unmounted - unregister it from auth
void ds4_auth_unregister(uint8_t dev_addr, uint8_t instance) {
    if (ds4_auth.ds4_available &&
        ds4_auth.dev_addr == dev_addr &&
        ds4_auth.instance == instance) {
        ds4_auth.ds4_available = false;
        ds4_auth.state = DS4_AUTH_STATE_IDLE;
        ds4_auth.internal = AUTH_IDLE;
        ds4_auth.busy = false;  // Reset busy so new DS4 can start fresh
        ds4_auth.signature_ready = false;
        TU_LOG1("[DS4 Auth] Unregistered DS4 from auth passthrough\r\n");
    }
}

// Check if a DS4 is available for auth passthrough
bool ds4_auth_is_available(void) {
    return ds4_auth.ds4_available;
}

// Get the current auth state
ds4_auth_state_t ds4_auth_get_state(void) {
    return ds4_auth.state;
}

// Forward nonce page from PS4 console to connected DS4
// Console sends: [nonce_id][page][0][data(56)]...
// (CRC32 is handled at USB layer, not in our data)
bool ds4_auth_send_nonce(const uint8_t* data, uint16_t len) {
    printf("[DS4 Auth] send_nonce called, len=%d, ds4_available=%d\n", len, ds4_auth.ds4_available);

    if (!ds4_auth.ds4_available) {
        printf("[DS4 Auth] No DS4 available for auth\n");
        return false;
    }

    if (len < 59) {  // nonce_id + page + padding + 56 bytes data
        printf("[DS4 Auth] Nonce data too short (%d bytes)\n", len);
        return false;
    }

    uint8_t nonce_id = data[0];
    uint8_t page = data[1];
    // data[2] is padding, data[3:59] is nonce data

    if (page >= DS4_AUTH_NONCE_PAGES) {
        printf("[DS4 Auth] Invalid nonce page %d\n", page);
        return false;
    }

    // Copy nonce data to buffer (56 bytes per page)
    memcpy(&ds4_auth.nonce_buffer[page * DS4_AUTH_PAGE_SIZE], &data[3], DS4_AUTH_PAGE_SIZE);

    printf("[DS4 Auth] Nonce page %d received (id=%d)\n", page, nonce_id);

    // Track nonce_id
    if (page == 0) {
        ds4_auth.nonce_id = nonce_id;
    }

    // When page 4 is received, all nonce data is ready - start auth sequence
    if (page == 4) {
        ds4_auth.nonce_pages_received = 5;
        ds4_auth.signature_ready = false;
        ds4_auth.signature_pages_fetched = 0;
        ds4_auth.signature_page_returning = 0;
        ds4_auth.nonce_page_sending = 0;
        ds4_auth.internal = AUTH_SENDING_RESET;  // First get 0xF3 from DS4
        ds4_auth.state = DS4_AUTH_STATE_NONCE_PENDING;
        printf("[DS4 Auth] All 5 nonce pages received, starting auth with DS4\n");
    }

    return true;
}

// Get cached signature response (0xF1) for a specific page
// Format: [nonce_id][page][0][signature_data(56)][padding(4)]
uint16_t ds4_auth_get_signature(uint8_t* buffer, uint16_t max_len, uint8_t page) {
    // Zero entire buffer first to avoid uninitialized bytes
    memset(buffer, 0, max_len);

    if (page >= DS4_AUTH_SIGNATURE_PAGES) {
        TU_LOG1("[DS4 Auth] Invalid signature page request %d\r\n", page);
        return max_len;
    }

    // Build response: [nonce_id][page][0][signature_data(56)]
    buffer[0] = ds4_auth.nonce_id;
    buffer[1] = page;
    buffer[2] = 0;

    if (!ds4_auth.signature_ready) {
        // Signature not ready - already zeroed above
        TU_LOG1("[DS4 Auth] Signature page %d requested but not ready (have %d pages)\r\n",
                page, ds4_auth.signature_pages_fetched);
    } else {
        // Copy signature data for this page
        memcpy(&buffer[3], &ds4_auth.signature_buffer[page * DS4_AUTH_PAGE_SIZE], 56);
    }

    TU_LOG1("[DS4 Auth] Returning signature page %d (id=%d, ready=%d)\r\n",
            page, ds4_auth.nonce_id, ds4_auth.signature_ready);
    return max_len;
}

// Get next signature page (auto-incrementing)
// Console calls GET_REPORT(0xF1) sequentially, this returns pages 0-18 in order
uint16_t ds4_auth_get_next_signature(uint8_t* buffer, uint16_t max_len) {
    uint8_t page = ds4_auth.signature_page_returning;

    // Get the current page
    uint16_t len = ds4_auth_get_signature(buffer, max_len, page);

    // Advance to next page (wrap around at 19)
    if (ds4_auth.signature_page_returning < DS4_AUTH_SIGNATURE_PAGES - 1) {
        ds4_auth.signature_page_returning++;
    }
    // Stay at last page if we're at the end (console might retry)

    return len;
}

// Get auth status (0xF2)
// Format: [nonce_id][status][zeros(13)]
// status: 0 = ready, 16 = signing
uint16_t ds4_auth_get_status(uint8_t* buffer, uint16_t max_len) {
    // Zero entire buffer first to avoid uninitialized bytes
    memset(buffer, 0, max_len);

    buffer[0] = ds4_auth.nonce_id;
    buffer[1] = ds4_auth.signature_ready ? 0 : 16;

    TU_LOG1("[DS4 Auth] Status: %s (id=%d, ready=%d)\r\n",
            ds4_auth.signature_ready ? "ready" : "signing",
            ds4_auth.nonce_id, ds4_auth.signature_ready);
    return max_len;
}

// Reset auth state (0xF3)
void ds4_auth_reset(void) {
    ds4_auth.state = DS4_AUTH_STATE_IDLE;
    ds4_auth.internal = AUTH_IDLE;
    ds4_auth.busy = false;
    ds4_auth.signature_ready = false;
    ds4_auth.signature_page_returning = 0;
    TU_LOG1("[DS4 Auth] Auth state reset\r\n");
}

// Shared buffer for DS3 BT address verification (filled by tuh_hid_get_report)
static uint8_t ds3_verify_buf[8] = {0};

// Get pointer to DS3 verify buffer (called from sony_ds3.c)
uint8_t* ds3_get_verify_buffer(void) {
    return ds3_verify_buf;
}

// TinyUSB callback for get_report completion
void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t idx,
                                    uint8_t report_id, uint8_t report_type,
                                    uint16_t len) {
    // Handle DS3 BT address verification (report 0xF5)
    if (report_id == 0xF5) {
        // Notify DS3 driver that GET_REPORT completed
        extern void ds3_on_get_report_complete(uint8_t dev_addr, uint8_t instance);
        ds3_on_get_report_complete(dev_addr, idx);

        if (len == 0) {
            printf("[DS3] GET_REPORT 0xF5 FAILED\n");
        } else {
            printf("[DS3] Current host address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                   ds3_verify_buf[2], ds3_verify_buf[3], ds3_verify_buf[4],
                   ds3_verify_buf[5], ds3_verify_buf[6], ds3_verify_buf[7]);
        }
        return;
    }

    // Check if this is for our auth DS4
    if (!ds4_auth.ds4_available ||
        dev_addr != ds4_auth.dev_addr ||
        idx != ds4_auth.instance) {
        return;
    }

    // Always clear busy for our device, even on failure
    ds4_auth.busy = false;

    // Check for transfer failure (len == 0 means transfer failed)
    if (len == 0) {
        printf("[DS4 Auth] CB: GET_REPORT transfer FAILED (report_id=0x%02X)\n", report_id);
        return;
    }

    if (report_type != HID_REPORT_TYPE_FEATURE) {
        printf("[DS4 Auth] CB: Unexpected report type %d\n", report_type);
        return;
    }

    switch (report_id) {
        case DS4_AUTH_REPORT_RESET:  // 0xF3
            // Reset response received - start sending nonce
            printf("[DS4 Auth] CB: Reset response from DS4, sending nonce\n");
            ds4_auth.internal = AUTH_SENDING_NONCE;
            break;

        case DS4_AUTH_REPORT_STATUS:  // 0xF2
            // Status: report_buffer[0]=nonce_id, [1]=status (0=ready, 16=signing)
            if (ds4_auth.report_buffer[1] == 0) {
                printf("[DS4 Auth] CB: DS4 signing complete, fetching signature\n");
                ds4_auth.signature_pages_fetched = 0;
                ds4_auth.internal = AUTH_RECEIVING_SIG;
            } else {
                printf("[DS4 Auth] CB: DS4 still signing (status=%d)\n", ds4_auth.report_buffer[1]);
            }
            break;

        case DS4_AUTH_REPORT_SIGNATURE:  // 0xF1
            // Signature: [nonce_id][page][0][data(56)]
            // Copy signature data (offset 3) to buffer
            memcpy(&ds4_auth.signature_buffer[ds4_auth.signature_pages_fetched * DS4_AUTH_PAGE_SIZE],
                   &ds4_auth.report_buffer[3], DS4_AUTH_PAGE_SIZE);
            ds4_auth.signature_pages_fetched++;
            printf("[DS4 Auth] CB: Signature page %d received from DS4\n",
                    ds4_auth.signature_pages_fetched - 1);

            if (ds4_auth.signature_pages_fetched >= DS4_AUTH_SIGNATURE_PAGES) {
                // All 19 pages received
                ds4_auth.internal = AUTH_IDLE;
                ds4_auth.signature_ready = true;
                ds4_auth.state = DS4_AUTH_STATE_READY;
                printf("[DS4 Auth] CB: All 19 signature pages received, auth ready!\n");
            }
            break;
    }
}

// TinyUSB callback for set_report completion
void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t idx,
                                    uint8_t report_id, uint8_t report_type,
                                    uint16_t len) {
    // DS3 BT address programming complete
    if (report_id == 0xF5) {
        if (len == 8) {
            printf("[DS3] BT host address programmed successfully\n");
        }
        return;
    }

    // Check if this is for our auth DS4
    if (!ds4_auth.ds4_available ||
        dev_addr != ds4_auth.dev_addr ||
        idx != ds4_auth.instance) {
        return;
    }

    // Always clear busy for our device, even on failure
    ds4_auth.busy = false;

    // Check for transfer failure (len == 0 means transfer failed)
    if (len == 0) {
        printf("[DS4 Auth] CB: SET_REPORT transfer FAILED (report_id=0x%02X)\n", report_id);
        return;
    }

    if (report_type != HID_REPORT_TYPE_FEATURE) {
        printf("[DS4 Auth] CB: Unexpected report type %d\n", report_type);
        return;
    }

    if (report_id == DS4_AUTH_REPORT_NONCE) {
        printf("[DS4 Auth] CB: Nonce page %d sent to DS4\n", ds4_auth.nonce_page_sending);
        ds4_auth.nonce_page_sending++;

        if (ds4_auth.nonce_page_sending >= DS4_AUTH_NONCE_PAGES) {
            // All 5 nonce pages sent - wait for signing
            printf("[DS4 Auth] CB: All 5 nonce pages sent, waiting for signing\n");
            ds4_auth.internal = AUTH_WAITING_FOR_SIG;
            ds4_auth.state = DS4_AUTH_STATE_SIGNING;
        }
    }
}

// Auth task - state machine matching hid-remapper approach
void ds4_auth_task(void) {
    if (!ds4_auth.ds4_available || ds4_auth.busy) return;

    switch (ds4_auth.internal) {
        case AUTH_IDLE:
            // Nothing to do
            break;

        case AUTH_SENDING_RESET:
            // Request 0xF3 from DS4 to reset its auth state
            printf("[DS4 Auth] Task: Requesting reset (0xF3) from DS4\n");
            tuh_hid_get_report(ds4_auth.dev_addr, ds4_auth.instance,
                               DS4_AUTH_REPORT_RESET, HID_REPORT_TYPE_FEATURE,
                               ds4_auth.report_buffer, 8);
            ds4_auth.busy = true;
            break;

        case AUTH_SENDING_NONCE: {
            // Send nonce pages to DS4: [nonce_id][page][0][data(56)][padding(4)]
            uint8_t page = ds4_auth.nonce_page_sending;
            // Clear entire buffer first to avoid uninitialized bytes
            memset(ds4_auth.report_buffer, 0, 63);
            ds4_auth.report_buffer[0] = ds4_auth.nonce_id;
            ds4_auth.report_buffer[1] = page;
            ds4_auth.report_buffer[2] = 0;
            memcpy(&ds4_auth.report_buffer[3],
                   &ds4_auth.nonce_buffer[page * DS4_AUTH_PAGE_SIZE],
                   DS4_AUTH_PAGE_SIZE);

            printf("[DS4 Auth] Task: Sending nonce page %d to DS4\n", page);
            tuh_hid_set_report(ds4_auth.dev_addr, ds4_auth.instance,
                               DS4_AUTH_REPORT_NONCE, HID_REPORT_TYPE_FEATURE,
                               ds4_auth.report_buffer, 63);
            ds4_auth.busy = true;
            break;
        }

        case AUTH_WAITING_FOR_SIG:
            // Poll 0xF2 status from DS4
            printf("[DS4 Auth] Task: Polling status (0xF2) from DS4\n");
            tuh_hid_get_report(ds4_auth.dev_addr, ds4_auth.instance,
                               DS4_AUTH_REPORT_STATUS, HID_REPORT_TYPE_FEATURE,
                               ds4_auth.report_buffer, 16);
            ds4_auth.busy = true;
            break;

        case AUTH_RECEIVING_SIG:
            // Fetch 0xF1 signature pages from DS4
            printf("[DS4 Auth] Task: Fetching signature page %d from DS4\n",
                    ds4_auth.signature_pages_fetched);
            tuh_hid_get_report(ds4_auth.dev_addr, ds4_auth.instance,
                               DS4_AUTH_REPORT_SIGNATURE, HID_REPORT_TYPE_FEATURE,
                               ds4_auth.report_buffer, 64);
            ds4_auth.busy = true;
            break;
    }
}
