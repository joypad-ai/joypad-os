// ds5_bt.c - Sony DualSense Bluetooth Driver
// Handles PS5 DualSense controllers over Bluetooth
//
// Reference: https://controllers.fandom.com/wiki/Sony_DualSense
// BT reports have similar structure to USB but with different report IDs
// BT output reports require CRC32

#include "ds5_bt.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "platform/platform.h"
#include <string.h>
#include <stdio.h>

#ifdef CONFIG_DS5_DROP_SCREAM
// Drop-scream content feature: IMU free-fall detection + speaker audio over BT
// via extended output report 0x36 (Opus frames baked into flash).
// See .dev/docs/DUALSENSE_DROP_SCREAM_PLAN.md — disabled by default.
#include "ds5_voice_assets.h"

// btstack_host.c (gated): direct L2CAP interrupt-channel send with per-packet
// can-send-now pacing — the hid_host_send_report path drops frames at 100Hz.
extern bool btstack_classic_send_interrupt_raw(uint8_t conn_index,
                                               const uint8_t* data, uint16_t len);
// Scan control: continuous Classic inquiry (6.4s GIAC/LIAC windows, restarted
// forever) + BLE scanning keep running while a controller is connected, and
// inquiry devastates ACL bandwidth — audible as stutter in the 0x36 stream.
// Silence the radio while a DS5 is connected; resume discovery on disconnect.
extern void btstack_host_stop_scan(void);
extern void btstack_host_start_scan(void);
extern void btstack_host_suppress_scan(bool suppress);
#endif

// Player LED colors (RGB values) - same as DS4
static const uint8_t PLAYER_COLORS[][3] = {
    {  0,   0,  64 },   // Player 1: Blue
    { 64,   0,   0 },   // Player 2: Red
    {  0,  64,   0 },   // Player 3: Green
    { 64,   0,  64 },   // Player 4: Pink/Fuchsia
    { 64,  64,   0 },   // Player 5: Yellow
    {  0,  64,  64 },   // Player 6: Cyan
    { 64,  32,   0 },   // Player 7: Orange
};

// Player LED patterns for DS5 (5 LEDs in a row)
// Pattern is a bitmask: bit 0=leftmost, bit 4=rightmost
static const uint8_t PLAYER_LED_PATTERNS[] = {
    0x04,   // Player 1: center LED (--*--)
    0x0A,   // Player 2: left+right of center (-*-*-)
    0x15,   // Player 3: outer + center (*-*-*)
    0x1B,   // Player 4: all but center (**-**)
    0x1F,   // Player 5: all LEDs (*****)
};

// ============================================================================
// DS5 CONSTANTS
// ============================================================================

// Report IDs
#define DS5_REPORT_BT_INPUT     0x31    // Full BT input report
#define DS5_REPORT_USB_INPUT    0x01    // USB input report (fallback)
#define DS5_REPORT_BT_OUTPUT    0x31    // BT output report

#ifdef CONFIG_DS5_DROP_SCREAM
#define DS5_REPORT_BT_AUDIO     0x36    // Extended output: state + haptic PCM + speaker Opus

// Voice sequencer states
enum {
    DS5_VOICE_IDLE = 0,
    DS5_VOICE_PREARM,       // white lightbar sent via known-good 0x31 (diagnostic)
    DS5_VOICE_ARMING,       // speaker-path 0x31 sent, waiting to start frames
    DS5_VOICE_PLAYING,
};

// Drop detector states
enum {
    DS5_DROP_IDLE = 0,
    DS5_DROP_PENDING,       // near-zero g seen, confirming sustained free-fall
    DS5_DROP_FALLING,       // confirmed free-fall, scream playing
    DS5_DROP_LANDING,       // free-fall ended, classifying impact vs catch
    DS5_DROP_COOLDOWN,
};

// DS5 accelerometer: 8192 LSB per g (Linux DS_ACC_RES_PER_G)
#define DS5_ACCEL_1G            8192
#define DS5_FF_THRESH           (DS5_ACCEL_1G * 3 / 10)   // 0.30 g — ballistic
#define DS5_NORMAL_THRESH       (DS5_ACCEL_1G * 3 / 4)    // 0.75 g — free-fall over
#define DS5_IMPACT_THRESH       (DS5_ACCEL_1G * 3)        // 3.0 g — hit something
#define DS5_FALL_CONFIRM_MS     90      // sustained 0g before scream starts
#define DS5_LANDING_WINDOW_MS   150     // peak-decel classification window
#define DS5_FALL_TIMEOUT_MS     2500    // give up if no landing (scream exhausted)
#define DS5_COOLDOWN_MS         3000
#endif

// ============================================================================
// DS5 REPORT STRUCTURE
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t x1, y1;         // Left stick
    uint8_t x2, y2;         // Right stick
    uint8_t l2_trigger;     // L2 analog
    uint8_t r2_trigger;     // R2 analog
    uint8_t counter;        // Report counter / sequence number

    struct {
        uint8_t dpad     : 4;   // Hat: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 8=released
        uint8_t square   : 1;
        uint8_t cross    : 1;
        uint8_t circle   : 1;
        uint8_t triangle : 1;
    };

    struct {
        uint8_t l1     : 1;
        uint8_t r1     : 1;
        uint8_t l2     : 1;
        uint8_t r2     : 1;
        uint8_t create : 1;     // Share/Create button
        uint8_t option : 1;
        uint8_t l3     : 1;
        uint8_t r3     : 1;
    };

    struct {
        uint8_t ps      : 1;    // PlayStation button
        uint8_t tpad    : 1;    // Touchpad click
        uint8_t mute    : 1;    // Mute button
        uint8_t pad     : 5;
    };

    uint8_t reserved1;          // 4th button byte

    // Extended data for motion (matches Linux kernel hid-playstation.c)
    uint8_t reserved2[4];       // Timestamp/padding bytes
    int16_t gyro[3];            // x, y, z (pitch, yaw, roll)
    int16_t accel[3];           // x, y, z
    uint32_t sensor_timestamp;  // Sensor timestamp (0.33µs units)
    uint8_t  reserved3;         // Temperature / reserved
    // Touchpad: 4 bytes per finger (matches dualsense_touch_point in hid-playstation.c)
    // Finger 1 at struct offset 32, Finger 2 at struct offset 36
    struct { uint8_t tpad_f1_count : 7; uint8_t tpad_f1_down : 1; };
    uint8_t  tpad_f1_pos[3];
    struct { uint8_t tpad_f2_count : 7; uint8_t tpad_f2_down : 1; };
    uint8_t  tpad_f2_pos[3];
} ds5_input_report_t;

// DS5 BT output report for LED/rumble
typedef struct __attribute__((packed)) {
    uint8_t report_id;          // 0x31
    uint8_t seq_tag;            // Sequence tag (upper nibble)
    uint8_t tag;                // 0x10 for BT

    uint8_t valid_flag0;        // Feature flags
    uint8_t valid_flag1;
    uint8_t valid_flag2;

    uint8_t rumble_right;       // High frequency motor
    uint8_t rumble_left;        // Low frequency motor

    uint8_t headphone_volume;
    uint8_t speaker_volume;
    uint8_t mic_volume;

    uint8_t audio_flags;
    uint8_t mute_flags;

    uint8_t trigger_r[11];      // Right trigger haptics
    uint8_t trigger_l[11];      // Left trigger haptics

    uint8_t reserved1[6];

    uint8_t valid_flag3;

    uint8_t reserved2[2];

    uint8_t lightbar_setup;     // LED setup flag
    uint8_t led_brightness;
    uint8_t player_led;         // Player indicator LEDs

    uint8_t lightbar_r;
    uint8_t lightbar_g;
    uint8_t lightbar_b;
} ds5_bt_output_report_t;

// ============================================================================
// CRC32 for DS5 BT output reports
// ============================================================================

// Core CRC32 calculation (returns raw CRC, no inversion)
static uint32_t ds5_crc32_raw(uint32_t seed, const uint8_t* data, size_t len)
{
    uint32_t crc = seed;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc;
}

// DS5 BT output CRC - matches Linux kernel hid-playstation driver
// Two-step calculation: first hash seed (0xA2), then hash report data
static uint32_t ds5_bt_crc32(const uint8_t* report_data, size_t len)
{
    const uint8_t seed = 0xA2;  // PS_OUTPUT_CRC32_SEED

    // Step 1: Hash the seed byte
    uint32_t crc = ds5_crc32_raw(0xFFFFFFFF, &seed, 1);

    // Step 2: Continue hashing with report data (use intermediate CRC as seed)
    crc = ds5_crc32_raw(crc, report_data, len);

    // Final inversion
    return ~crc;
}

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;
    bool initialized;
    uint8_t activation_state;
    uint32_t activation_time;
    uint8_t output_seq;

    // Current feedback state (for change detection)
    uint8_t rumble_left;
    uint8_t rumble_right;
    uint8_t led_r, led_g, led_b;
    uint8_t player_led;

    // Touchpad swipe tracking
    uint16_t tpad_last_pos;
    bool tpad_dragging;

#ifdef CONFIG_DS5_DROP_SCREAM
    // Voice sequencer
    uint8_t voice_state;
    const ds5_voice_clip_t* voice_clip;
    uint16_t voice_frame;
    uint32_t voice_next_ms;         // next 10ms frame slot
    uint8_t audio_pkt_counter;      // free-running 0x36 packet counter
    bool voice_is_scream;
    uint8_t voice_r, voice_g, voice_b;
    bool voice_flash;               // flash lightbar while screaming
    bool led_refresh;               // force normal LED resend after playback
    bool drop_scream_enabled;       // armed via mute toggle (off by default)
    uint32_t toggle_cue_until;      // hold arm/disarm lightbar cue until this time
    bool mute_was_down;             // toggle edge detect
    uint8_t mute_streak;            // consecutive reports with mute down (glitch filter)
    uint32_t trigger_last_ms;       // toggle debounce
    bool scan_quiet_pending;        // defer scan-stop out of BT event context
    // Drop detector
    uint8_t drop_state;
    uint32_t drop_t0;               // current drop-state entry time
    int64_t drop_peak_m2;           // peak |accel|^2 during landing window
#endif
} ds5_bt_data_t;

static ds5_bt_data_t ds5_data[BTHID_MAX_DEVICES];

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void ds5_send_output(bthid_device_t* device, uint8_t rumble_left, uint8_t rumble_right,
                            uint8_t r, uint8_t g, uint8_t b, uint8_t player_led)
{
    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (!ds5) return;

    // DS5 BT output report - matches Linux kernel hid-playstation.c dualsense_output_report_common
    // Report structure: report_id(1) + seq_tag(1) + tag(1) + common(47) + reserved(24) + crc(4) = 78 bytes
    // Buffer: 0xA2 header + 78-byte report = 79 bytes total
    // Static: BTstack stores pointer to report data for deferred L2CAP send
    static uint8_t buf[79];
    memset(buf, 0, sizeof(buf));

    // BT HID header
    buf[0] = 0xA2;  // DATA | OUTPUT (BT HID transaction header)

    // Report header (report bytes 0-2, buf offset 1-3)
    buf[1] = 0x31;  // Report ID
    buf[2] = (ds5->output_seq++ << 4);  // Sequence tag (upper nibble)
    buf[3] = 0x10;  // Tag: 0x10 for BT

    // Common struct starts at report byte 3 (buf offset 4)
    // Linux kernel offsets within common:
    // 0: valid_flag0, 1: valid_flag1, 2: motor_right, 3: motor_left
    // 4-7: audio volumes, 8: mute_led, 9: power_save, 10-36: reserved2
    // 37: audio_control2, 38: valid_flag2, 39-40: reserved3
    // 41: lightbar_setup, 42: led_brightness, 43: player_leds
    // 44: red, 45: green, 46: blue

    // Valid flags
    buf[4] = 0x03;   // common[0] valid_flag0: COMPATIBLE_VIBRATION | HAPTICS_SELECT
    buf[5] = 0x14;   // common[1] valid_flag1: LIGHTBAR_CONTROL(0x04) | PLAYER_INDICATOR_CONTROL(0x10)

    // Rumble motors (common offsets 2-3)
    buf[6] = rumble_right;   // common[2] motor_right (high frequency)
    buf[7] = rumble_left;    // common[3] motor_left (low frequency)

    // common[4-9]: audio volumes, mute_led, power_save - leave as 0
    // common[10-36]: reserved2 - leave as 0
    // common[37]: audio_control2 - leave as 0

    // common[38] = buf[42]: valid_flag2
    buf[42] = 0x02;  // LIGHTBAR_SETUP_CONTROL

    // common[39-40] = buf[43-44]: reserved3 - leave as 0

    // common[41] = buf[45]: lightbar_setup
    buf[45] = 0x02;  // LIGHTBAR_SETUP_LIGHT_OUT

    // common[42] = buf[46]: led_brightness
    buf[46] = 0x01;  // Full brightness

    // common[43] = buf[47]: player_leds
    buf[47] = player_led;

    // common[44-46] = buf[48-50]: lightbar RGB
    buf[48] = r;
    buf[49] = g;
    buf[50] = b;

    // buf[51-74]: reserved[24] - leave as 0

    // CRC32 calculated over report data only (buf[1..74] = 74 bytes)
    // The 0xA2 seed is handled internally by ds5_bt_crc32
    uint32_t crc = ds5_bt_crc32(&buf[1], 74);

    // Append CRC (little-endian) at bytes 75-78
    buf[75] = (crc >> 0) & 0xFF;
    buf[76] = (crc >> 8) & 0xFF;
    buf[77] = (crc >> 16) & 0xFF;
    buf[78] = (crc >> 24) & 0xFF;

    // Send on interrupt channel (79 bytes: 0xA2 + 78-byte report including CRC)
    bt_send_interrupt(device->conn_index, buf, 79);
    printf("[DS5_BT] Output: rumble L=%d R=%d, LED=%02X, RGB=%d/%d/%d\n",
           rumble_left, rumble_right, player_led, r, g, b);

    // Update cached state
    ds5->rumble_left = rumble_left;
    ds5->rumble_right = rumble_right;
    ds5->led_r = r;
    ds5->led_g = g;
    ds5->led_b = b;
    ds5->player_led = player_led;
}

#ifdef CONFIG_DS5_DROP_SCREAM
// ============================================================================
// DROP SCREAM: speaker audio over BT (report 0x36) + IMU drop detection
// ============================================================================

// Send a 0x31 with speaker routing armed (enable=true) or released (false).
// Must be sent once before the first 0x36 audio frame to open the speaker path.
static void ds5_send_output31_audio(bthid_device_t* device, bool enable)
{
    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (!ds5) return;

    static uint8_t buf[79];
    memset(buf, 0, sizeof(buf));

    buf[0] = 0xA2;                       // DATA | OUTPUT
    buf[1] = 0x31;
    buf[2] = (uint8_t)(ds5->output_seq++ << 4);
    buf[3] = 0x10;                       // BT tag

    // Pure audio-control report, mirroring DS5_Bridge send_speaker_output_state():
    // no rumble/lightbar/LED flags — those are carried by 0x36 SetStateData.
    if (enable) {
        buf[4]  = 0x80 | 0x20;           // valid_flag0: audio_ctrl_en | speaker_vol_en
        buf[5]  = 0x80;                  // valid_flag1: audio_control2_en
        buf[9]  = 0x64;                  // speaker volume (max)
        buf[11] = 0x30;                  // audio_control: speaker output path
        buf[41] = 0x04;                  // audio_control2: speaker preamp gain (default 4)
    } else {
        buf[4]  = 0x80;                  // valid_flag0: audio_ctrl_en
        buf[11] = 0x00;                  // audio_control: headphones path (off)
    }

    uint32_t crc = ds5_bt_crc32(&buf[1], 74);
    buf[75] = (crc >> 0) & 0xFF;
    buf[76] = (crc >> 8) & 0xFF;
    buf[77] = (crc >> 16) & 0xFF;
    buf[78] = (crc >> 24) & 0xFF;

    bt_send_interrupt(device->conn_index, buf, 79);
    printf("[DS5_BT] Speaker %s\n", enable ? "armed" : "released");
}

// Build and send one 0x36 audio frame: 0xA2 + 398-byte report.
// Sub-packet chain (format from DS5_Bridge/SAxense reverse engineering):
//   0x11 audio control | 0x10 SetStateData (63B) | 0x12 haptic PCM (64B) |
//   0x13 speaker Opus (200B) | pad | CRC32
static bool ds5_send_audio_frame(bthid_device_t* device)
{
    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (!ds5 || !ds5->voice_clip) return false;

    // Static: BTstack copies into its own buffer synchronously, but keep the
    // existing driver convention (and 400B off the task stack).
    static uint8_t buf[400];
    memset(buf, 0, sizeof(buf));

    buf[0] = 0xA2;                                   // DATA | OUTPUT
    buf[1] = DS5_REPORT_BT_AUDIO;                    // 0x36
    buf[2] = (uint8_t)((ds5->output_seq++ & 0x0F) << 4);

    // --- Sub-packet 0x11: audio control / enable (7 bytes) ---
    buf[3] = 0x11 | 0x80;                            // pid | sized
    buf[4] = 7;
    // Enable all audio sections EXCEPT mic streaming (bit 0): with mic on,
    // the controller streams mic-audio input reports that got parsed as
    // gamepad input — phantom mute presses and garbage accel data.
    buf[5] = 0xFE;
    buf[6] = buf[7] = buf[8] = buf[9] = buf[10] = 64; // haptic buffer length
    buf[11] = ds5->audio_pkt_counter++;

    // --- Sub-packet 0x10: SetStateData (63 bytes) ---
    // Carries the full 0x31-equivalent state so LEDs/rumble stay alive while
    // standalone 0x31 reports are suppressed during playback.
    buf[12] = 0x10 | 0x80;
    buf[13] = 63;
    // Byte-exact mirror of DS5_Bridge controller_output_state state_data after
    // controller_output_state_copy_audio_snapshot() in speaker mode.
    uint8_t* st = &buf[14];
    st[0] = 0xBD;                 // valid_flag0: vib+triggers+headphone+speaker+audio_ctrl (no mic)
    st[1] = 0xF6;                 // valid_flag1: pwr_save+lightbar+player+lowpass+motor_pwr+audio_ctrl2
    st[2] = ds5->rumble_right;
    st[3] = ds5->rumble_left;
    st[4] = 0x7F;                 // headphone volume
    st[5] = 0x64;                 // speaker volume
    st[6] = 0xFF;                 // mic volume (enable bit cleared — inert)
    st[7] = 0x30;                 // audio_control: speaker output path
    st[9] = 0x0F;                 // power_save_control: keep audio blocks awake
    st[37] = 0x04;                // audio_control2: speaker preamp gain (default 4)
    st[38] = 0x07;                // valid_flag2
    st[41] = 0x02;                // lightbar_setup: light out
    st[42] = 0x01;                // led brightness
    st[43] = ds5->player_led;
    // Lightbar acting: flash 100ms on/off while screaming, steady on quips
    bool blank = ds5->voice_flash && ((ds5->voice_frame / 10) & 1);
    st[44] = blank ? 0 : ds5->voice_r;
    st[45] = blank ? 0 : ds5->voice_g;
    st[46] = blank ? 0 : ds5->voice_b;

    // --- Sub-packet 0x12: haptic PCM, 64 bytes of silence ---
    buf[77] = 0x12 | 0x80;
    buf[78] = 64;
    // buf[79..142] already zero

    // --- Sub-packet 0x13: speaker Opus frame (200 bytes) ---
    buf[143] = 0x13 | 0x80;
    buf[144] = 200;
    memcpy(&buf[145], ds5->voice_clip->frames[ds5->voice_frame], 200);

    // buf[345..394]: zero pad. CRC32 over report bytes [0..393] = buf[1..394].
    uint32_t crc = ds5_bt_crc32(&buf[1], 394);
    buf[395] = (crc >> 0) & 0xFF;
    buf[396] = (crc >> 8) & 0xFF;
    buf[397] = (crc >> 16) & 0xFF;
    buf[398] = (crc >> 24) & 0xFF;

    static uint32_t frames_ok = 0, frames_fail = 0;
    bool ok = btstack_classic_send_interrupt_raw(device->conn_index, buf, 399);
    if (ok) frames_ok++; else frames_fail++;
    if (((frames_ok + frames_fail) % 100) == 1) {
        printf("[DS5_BT] audio frames ok=%lu busy=%lu\n",
               (unsigned long)frames_ok, (unsigned long)frames_fail);
    }
    return ok;
}

// Start a clip, or splice to a new clip if already streaming (frame-pointer
// swap at the next 10ms slot — the lead-in silence baked into each clip
// absorbs the Opus decoder-state discontinuity).
static void ds5_voice_play(bthid_device_t* device, ds5_bt_data_t* ds5,
                           const ds5_voice_clip_t* clip, bool is_scream,
                           uint8_t r, uint8_t g, uint8_t b, bool flash)
{
    ds5->voice_clip = clip;
    ds5->voice_frame = 0;
    ds5->voice_is_scream = is_scream;
    ds5->voice_r = r;
    ds5->voice_g = g;
    ds5->voice_b = b;
    ds5->voice_flash = flash;

    if (ds5->voice_state == DS5_VOICE_IDLE) {
        // Diagnostic stage 1: white lightbar via the KNOWN-GOOD 0x31 path.
        // Visible white = trigger + parse + BT send path all work.
        ds5_send_output(device, 0, 0, 255, 255, 255, ds5->player_led);
        ds5->voice_next_ms = platform_time_ms() + 20;
        ds5->voice_state = DS5_VOICE_PREARM;
    }
}

// Restore player-slot lightbar/LED via the normal 0x31 path (the cached-value
// feedback logic can otherwise re-send a stale cue/acting color forever)
static void ds5_restore_player_led(bthid_device_t* device, ds5_bt_data_t* ds5)
{
    int pidx = find_player_index(ds5->event.dev_addr, ds5->event.instance);
    int idx = (pidx >= 0 && pidx < 7) ? pidx : 0;
    int pat = (idx < 5) ? idx : idx % 5;
    ds5_send_output(device, 0, 0,
                    PLAYER_COLORS[idx][0], PLAYER_COLORS[idx][1], PLAYER_COLORS[idx][2],
                    PLAYER_LED_PATTERNS[pat]);
}

static void ds5_voice_stop(bthid_device_t* device, ds5_bt_data_t* ds5)
{
    if (ds5->voice_state == DS5_VOICE_IDLE) return;
    ds5->voice_state = DS5_VOICE_IDLE;
    ds5->voice_clip = NULL;
    ds5_send_output31_audio(device, false);
    ds5_restore_player_led(device, ds5);
}

static void ds5_voice_quip(bthid_device_t* device, ds5_bt_data_t* ds5, bool impact)
{
    uint32_t pick = platform_time_ms();
    const ds5_voice_clip_t* clip = impact
        ? ds5_clips_impact[pick % DS5_CLIPS_IMPACT_COUNT]
        : ds5_clips_catch[pick % DS5_CLIPS_CATCH_COUNT];
    printf("[DS5_BT] Quip: %s\n", impact ? "impact" : "catch");
    // Orange for hurt, green for relief
    ds5_voice_play(device, ds5, clip, false,
                   impact ? 255 : 0, impact ? 32 : 255, 0, false);
}

// 10ms frame pacer, called every ds5_task tick.
// Returns true while playback owns the output channel (suppresses 0x31 path).
static bool ds5_voice_task(bthid_device_t* device, ds5_bt_data_t* ds5)
{
    if (ds5->voice_state == DS5_VOICE_IDLE) return false;

    uint32_t now = platform_time_ms();
    if ((int32_t)(now - ds5->voice_next_ms) < 0) return true;

    if (ds5->voice_state == DS5_VOICE_PREARM) {
        // Diagnostic stage 2: open the speaker routing path
        ds5_send_output31_audio(device, true);
        ds5->voice_next_ms = now + 30;   // let routing settle
        ds5->voice_state = DS5_VOICE_ARMING;
        return true;
    }

    // Diagnostic stage 3: the 0x36 stream itself.
    // Retry-don't-drop: if L2CAP can't take the packet this tick, keep the
    // frame and retry next task pass — a dropped frame is an audible gap.
    ds5->voice_state = DS5_VOICE_PLAYING;
    if (!ds5_send_audio_frame(device)) {
        if ((int32_t)(now - ds5->voice_next_ms) > 60) {
            ds5->voice_next_ms = now;  // hopelessly behind: resync clock
        }
        return true;
    }
    ds5->voice_frame++;
    // Controller consumes one 0x36 per 10.667ms (64-byte haptic buffer at
    // 3kHz stereo = 32ms per 3 packets). Sending at a plain 10ms overflows
    // its intake queue and it drops frames (audible stutter). +11,+11,+10.
    ds5->voice_next_ms += ((ds5->voice_frame % 3) == 0) ? 10 : 11;
    // Resync instead of bursting if the task loop stalled past a few slots
    if ((int32_t)(now - ds5->voice_next_ms) > 30) {
        ds5->voice_next_ms = now + 11;
    }

    if (ds5->voice_frame >= ds5->voice_clip->frame_count) {
        ds5_voice_stop(device, ds5);
        return false;
    }
    return true;
}

// Free-fall / impact / catch detector, fed raw accel from every input report.
// A toss is ballistic (0g) just like a drop — physically indistinguishable,
// and screaming when tossed is part of the bit.
static void ds5_drop_update(bthid_device_t* device, ds5_bt_data_t* ds5,
                            const int16_t accel[3])
{
    const int64_t ff2     = (int64_t)DS5_FF_THRESH * DS5_FF_THRESH;
    const int64_t normal2 = (int64_t)DS5_NORMAL_THRESH * DS5_NORMAL_THRESH;
    const int64_t impact2 = (int64_t)DS5_IMPACT_THRESH * DS5_IMPACT_THRESH;

    int64_t m2 = (int64_t)accel[0] * accel[0] +
                 (int64_t)accel[1] * accel[1] +
                 (int64_t)accel[2] * accel[2];
    uint32_t now = platform_time_ms();

    switch (ds5->drop_state) {
        case DS5_DROP_IDLE:
            if (m2 < ff2) {
                ds5->drop_state = DS5_DROP_PENDING;
                ds5->drop_t0 = now;
                // ~1g should read m2≈64 here (8192²>>20); near-zero = free-fall.
                // If this line spams at rest, the accel parse is misaligned.
                printf("[DS5_BT] Drop: near-zero g (m2>>20=%ld)\n", (long)(m2 >> 20));
            }
            break;

        case DS5_DROP_PENDING:
            if (m2 >= ff2) {
                ds5->drop_state = DS5_DROP_IDLE;
            } else if (now - ds5->drop_t0 >= DS5_FALL_CONFIRM_MS) {
                printf("[DS5_BT] Free-fall — screaming\n");
                ds5->drop_state = DS5_DROP_FALLING;
                ds5->drop_t0 = now;
                ds5_voice_play(device, ds5, &DS5_CLIP_SCREAM, true,
                               255, 0, 0, true);  // flashing red
            }
            break;

        case DS5_DROP_FALLING:
            if (m2 > impact2) {
                ds5_voice_quip(device, ds5, true);
                ds5->drop_state = DS5_DROP_COOLDOWN;
                ds5->drop_t0 = now;
            } else if (m2 > normal2) {
                // Free-fall ended without a hard spike yet — watch the window
                ds5->drop_state = DS5_DROP_LANDING;
                ds5->drop_t0 = now;
                ds5->drop_peak_m2 = m2;
            } else if (now - ds5->drop_t0 > DS5_FALL_TIMEOUT_MS) {
                ds5_voice_stop(device, ds5);
                ds5->drop_state = DS5_DROP_COOLDOWN;
                ds5->drop_t0 = now;
            }
            break;

        case DS5_DROP_LANDING:
            if (m2 > ds5->drop_peak_m2) ds5->drop_peak_m2 = m2;
            if (ds5->drop_peak_m2 > impact2) {
                ds5_voice_quip(device, ds5, true);
                ds5->drop_state = DS5_DROP_COOLDOWN;
                ds5->drop_t0 = now;
            } else if (now - ds5->drop_t0 >= DS5_LANDING_WINDOW_MS) {
                ds5_voice_quip(device, ds5, false);  // soft decel = caught
                ds5->drop_state = DS5_DROP_COOLDOWN;
                ds5->drop_t0 = now;
            }
            break;

        case DS5_DROP_COOLDOWN:
            if (now - ds5->drop_t0 >= DS5_COOLDOWN_MS) {
                ds5->drop_state = DS5_DROP_IDLE;
            }
            break;
    }
}
#endif  // CONFIG_DS5_DROP_SCREAM

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool ds5_match(const char* device_name, const uint8_t* class_of_device,
                      uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)class_of_device;
    (void)is_ble;

    // VID/PID match (highest priority) - Sony vendor ID = 0x054C
    // DualSense = 0x0CE6, DualSense Edge = 0x0DF2
    if (vendor_id == 0x054C && (product_id == 0x0CE6 || product_id == 0x0DF2)) {
        return true;
    }

    // Name-based match (fallback if SDP query didn't return VID/PID)
    if (device_name) {
        if (strstr(device_name, "DualSense") != NULL) {
            return true;
        }
        if (strstr(device_name, "PS5 Controller") != NULL) {
            return true;
        }
    }

    return false;
}

static bool ds5_init(bthid_device_t* device)
{
    printf("[DS5_BT] Init for device: %s\n", device->name);

    // Find free data slot
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!ds5_data[i].initialized) {
            init_input_event(&ds5_data[i].event);
            ds5_data[i].initialized = true;
            ds5_data[i].activation_state = 0;
            ds5_data[i].activation_time = 0;
            ds5_data[i].output_seq = 0;
            ds5_data[i].rumble_left = 0;
            ds5_data[i].rumble_right = 0;
            ds5_data[i].led_r = 0;
            ds5_data[i].led_g = 0;
            ds5_data[i].led_b = 64;  // Default blue
            ds5_data[i].player_led = PLAYER_LED_PATTERNS[0];
            ds5_data[i].tpad_last_pos = 0;
            ds5_data[i].tpad_dragging = false;
#ifdef CONFIG_DS5_DROP_SCREAM
            ds5_data[i].voice_state = DS5_VOICE_IDLE;
            ds5_data[i].voice_clip = NULL;
            ds5_data[i].voice_frame = 0;
            ds5_data[i].audio_pkt_counter = 0;
            ds5_data[i].led_refresh = false;
            ds5_data[i].drop_scream_enabled = false;  // armed via mute toggle
            ds5_data[i].toggle_cue_until = 0;
            ds5_data[i].mute_was_down = false;
            ds5_data[i].drop_state = DS5_DROP_IDLE;
            // Radio silence: ongoing inquiry/scan windows starve the ACL link
            // and stutter the audio stream. Deferred to ds5_task (main-loop
            // context) — stopping GAP from inside the BT event handler that
            // delivered this connection crash-reboots the stack.
            ds5_data[i].scan_quiet_pending = true;
#endif

            ds5_data[i].event.type = INPUT_TYPE_GAMEPAD;
            ds5_data[i].event.transport = INPUT_TRANSPORT_BT_CLASSIC;
            ds5_data[i].event.dev_addr = device->conn_index;
            ds5_data[i].event.instance = 0;
            ds5_data[i].event.button_count = 14;
            ds5_data[i].event.has_motion = true;

            device->driver_data = &ds5_data[i];

            // Activation happens in task (state machine with delays)
            return true;
        }
    }

    return false;
}

static bool ds5_process_debug_done = false;

static void ds5_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;

    if (!ds5 || len < 1) return;

    // Debug: print first report received
    if (!ds5_process_debug_done) {
        printf("[DS5_BT] Process report: len=%d, data[0]=0x%02X\n", len, data[0]);
        ds5_process_debug_done = true;
    }

    uint8_t report_id = data[0];
    const uint8_t* report_data = NULL;
    uint16_t report_len = 0;

    if (report_id == DS5_REPORT_BT_INPUT && len >= 12) {
#ifdef CONFIG_DS5_DROP_SCREAM
        // Strict shape gate: the gamepad input report is exactly 78 bytes.
        // With audio armed the controller emits other (e.g. mic) reports that
        // must not be parsed as input — that was the phantom-trigger source.
        if (len != 78) {
            return;
        }
#endif
        // Full BT report: report_id (1) + header (1) = skip 2 bytes
        report_data = data + 2;
        report_len = len - 2;
    } else if (report_id == DS5_REPORT_USB_INPUT && len >= 10) {
        // USB-style report: skip just report_id
        report_data = data + 1;
        report_len = len - 1;
    } else {
        // Unknown report format
        printf("[DS5_BT] Unknown report: len=%d, data[0]=0x%02X\n", len, data[0]);
        return;
    }

    if (report_len < sizeof(ds5_input_report_t)) {
        return;
    }

    const ds5_input_report_t* rpt = (const ds5_input_report_t*)report_data;

#ifdef CONFIG_DS5_DROP_SCREAM
    // Layout probe: trigger/drop instrumentation showed garbage at the
    // button/accel offsets — dump raw bytes to find the true BT layout.
    static uint8_t probe_count = 0;
    if (probe_count < 3) {
        probe_count++;
        printf("[DS5_BT] Probe id=0x%02X len=%u raw:", data[0], (unsigned)len);
        for (int pi = 0; pi < 28 && pi < (int)len; pi++) printf(" %02X", data[pi]);
        printf("\n");
    }
#endif

    // Parse D-pad (hat format)
    bool dpad_up    = (rpt->dpad == 0 || rpt->dpad == 1 || rpt->dpad == 7);
    bool dpad_right = (rpt->dpad >= 1 && rpt->dpad <= 3);
    bool dpad_down  = (rpt->dpad >= 3 && rpt->dpad <= 5);
    bool dpad_left  = (rpt->dpad >= 5 && rpt->dpad <= 7);

    // Build button state (inverted: 0 = pressed in USBR convention)
    uint32_t buttons = 0x00000000;

    if (dpad_up)       buttons |= JP_BUTTON_DU;
    if (dpad_down)     buttons |= JP_BUTTON_DD;
    if (dpad_left)     buttons |= JP_BUTTON_DL;
    if (dpad_right)    buttons |= JP_BUTTON_DR;
    if (rpt->cross)    buttons |= JP_BUTTON_B1;
    if (rpt->circle)   buttons |= JP_BUTTON_B2;
    if (rpt->square)   buttons |= JP_BUTTON_B3;
    if (rpt->triangle) buttons |= JP_BUTTON_B4;
    if (rpt->l1)       buttons |= JP_BUTTON_L1;
    if (rpt->r1)       buttons |= JP_BUTTON_R1;
    if (rpt->l2)       buttons |= JP_BUTTON_L2;
    if (rpt->r2)       buttons |= JP_BUTTON_R2;
    if (rpt->create)   buttons |= JP_BUTTON_S1;
    if (rpt->option)   buttons |= JP_BUTTON_S2;
    if (rpt->l3)       buttons |= JP_BUTTON_L3;
    if (rpt->r3)       buttons |= JP_BUTTON_R3;
    if (rpt->ps)       buttons |= JP_BUTTON_A1;
    if (rpt->tpad)     buttons |= JP_BUTTON_A2;
    if (rpt->mute)     buttons |= JP_BUTTON_A3;

    // Update event
    ds5->event.buttons = buttons;

    // Analog sticks (HID convention: 0=up, 255=down)
    ds5->event.analog[ANALOG_LX] = rpt->x1;
    ds5->event.analog[ANALOG_LY] = rpt->y1;
    ds5->event.analog[ANALOG_RX] = rpt->x2;
    ds5->event.analog[ANALOG_RY] = rpt->y2;

    // Triggers
    ds5->event.analog[ANALOG_L2] = rpt->l2_trigger;
    ds5->event.analog[ANALOG_R2] = rpt->r2_trigger;

    // Motion data (DS5 has full 3-axis gyro and accel)
    // Check if we have enough data for motion
    if (report_len >= sizeof(ds5_input_report_t)) {
        ds5->event.has_motion = true;
        ds5->event.accel[0] = rpt->accel[0];
        ds5->event.accel[1] = rpt->accel[1];
        ds5->event.accel[2] = rpt->accel[2];
        ds5->event.gyro[0] = rpt->gyro[0];
        ds5->event.gyro[1] = rpt->gyro[1];
        ds5->event.gyro[2] = rpt->gyro[2];
    } else {
        ds5->event.has_motion = false;
    }

    // Battery: status byte at report_data[52] — bits 0-3 = level (0-10), bits 4-7 = status
    // Per Linux kernel hid-playstation.c: 0=discharging, 1=charging, 2=full, 0xa/0xb/0xf=error
    if (report_len > 52) {
        uint8_t raw = report_data[52];
        uint8_t level = raw & 0x0F;
        uint8_t status = (raw >> 4) & 0x0F;

        switch (status) {
            case 0x0:  // Discharging
                ds5->event.battery_level = (level > 10) ? 100 : level * 10 + 5;
                ds5->event.battery_charging = false;
                break;
            case 0x1:  // Charging
                ds5->event.battery_level = (level > 10) ? 100 : level * 10 + 5;
                ds5->event.battery_charging = true;
                break;
            case 0x2:  // Full
                ds5->event.battery_level = 100;
                ds5->event.battery_charging = false;
                break;
            default:   // 0xa=voltage/temp, 0xb=temp, 0xf=charge error
                ds5->event.battery_level = 0;
                ds5->event.battery_charging = false;
                break;
        }
    }

    // Touchpad (only in full 0x31 reports that include touch fields)
    if (report_len >= sizeof(ds5_input_report_t)) {
        uint16_t tx = ((rpt->tpad_f1_pos[1] & 0x0f) << 8) | (rpt->tpad_f1_pos[0] & 0xff);
        uint16_t ty = ((rpt->tpad_f1_pos[1] & 0xf0) >> 4) | ((rpt->tpad_f1_pos[2] & 0xff) << 4);
        uint16_t tx2 = ((rpt->tpad_f2_pos[1] & 0x0f) << 8) | (rpt->tpad_f2_pos[0] & 0xff);
        uint16_t ty2 = ((rpt->tpad_f2_pos[1] & 0xf0) >> 4) | ((rpt->tpad_f2_pos[2] & 0xff) << 4);

        // Touchpad left/right click detection (touchpad is ~1920 wide, center at 960)
        if (rpt->tpad && !rpt->tpad_f1_down && tx < 960)
            ds5->event.buttons |= JP_BUTTON_L4;
        if (rpt->tpad && !rpt->tpad_f1_down && tx >= 960)
            ds5->event.buttons |= JP_BUTTON_R4;

        // Touchpad swipe delta (horizontal)
        int8_t touchpad_delta_x = 0;
        if (!rpt->tpad_f1_down) {
            if (ds5->tpad_dragging) {
                int16_t delta = (int16_t)tx - (int16_t)ds5->tpad_last_pos;
                if (delta > 12) delta = 12;
                if (delta < -12) delta = -12;
                touchpad_delta_x = (int8_t)delta;
            }
            ds5->tpad_last_pos = tx;
            ds5->tpad_dragging = true;
        } else {
            ds5->tpad_dragging = false;
        }
        ds5->event.delta_x = touchpad_delta_x;

        // Touch coordinates for SInput pass-through
        ds5->event.touch[0].x = tx;
        ds5->event.touch[0].y = ty;
        ds5->event.touch[0].active = !rpt->tpad_f1_down;
        ds5->event.touch[1].x = tx2;
        ds5->event.touch[1].y = ty2;
        ds5->event.touch[1].active = !rpt->tpad_f2_down;
        ds5->event.has_touch = true;
    }

#ifdef CONFIG_DS5_DROP_SCREAM
    // Mute button = ARM/DISARM toggle for the drop-scream (off by default).
    // It never plays audio directly — sound only happens on an actual fall.
    // Glitch-filtered (3 consecutive reports down) + 700ms debounce.
    if (rpt->mute) {
        if (ds5->mute_streak < 255) ds5->mute_streak++;
    } else {
        ds5->mute_streak = 0;
        ds5->mute_was_down = false;
    }
    uint32_t trig_now = platform_time_ms();
    if (ds5->mute_streak == 3 && !ds5->mute_was_down &&
        (trig_now - ds5->trigger_last_ms) >= 700) {
        ds5->mute_was_down = true;
        ds5->trigger_last_ms = trig_now;
        ds5->drop_scream_enabled = !ds5->drop_scream_enabled;
        printf("[DS5_BT] Drop-scream %s\n",
               ds5->drop_scream_enabled ? "ARMED" : "disarmed");
        if (!ds5->drop_scream_enabled) {
            ds5_voice_stop(device, ds5);   // no-op if idle
            ds5->drop_state = DS5_DROP_IDLE;
        }
        // Lightbar cue via the normal 0x31 path: green = armed, red = off.
        // Held briefly by ds5_task, then player color is restored.
        ds5_send_output(device, 0, 0,
                        ds5->drop_scream_enabled ? 0 : 64,
                        ds5->drop_scream_enabled ? 64 : 0,
                        0, ds5->player_led);
        ds5->toggle_cue_until = trig_now + 800;
    }

    if (ds5->drop_scream_enabled) {
        ds5_drop_update(device, ds5, rpt->accel);
    }
#endif

    // Submit to router
    router_submit_input(&ds5->event);
}

static void ds5_task(bthid_device_t* device)
{
    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (!ds5) return;

    uint32_t now = platform_time_ms();

#ifdef CONFIG_DS5_DROP_SCREAM
    // Deferred from ds5_init: silence discovery from main-loop context.
    // Inquiry/scan windows starve the ACL link and stutter the audio stream.
    if (ds5->scan_quiet_pending) {
        ds5->scan_quiet_pending = false;
        btstack_host_suppress_scan(true);
        btstack_host_stop_scan();
        printf("[DS5_BT] Scan suppressed for audio streaming\n");
    }
#endif

    // State machine for activation with delays
    switch (ds5->activation_state) {
        case 0:  // Wait a moment then send initial LED
            ds5->activation_state = 1;
            ds5->activation_time = now;
            break;

        case 1:  // Wait 100ms then send initial LED
            if (now - ds5->activation_time >= 100) {
                // Set initial LED based on player index
                int player_idx = find_player_index(ds5->event.dev_addr, ds5->event.instance);
                int idx = (player_idx >= 0 && player_idx < 7) ? player_idx : 0;
                int pat_idx = (idx < 5) ? idx : idx % 5;
                ds5_send_output(device, 0, 0,
                    PLAYER_COLORS[idx][0], PLAYER_COLORS[idx][1], PLAYER_COLORS[idx][2],
                    PLAYER_LED_PATTERNS[pat_idx]);
                ds5->activation_state = 2;
            }
            break;

        case 2:  // Activated - monitor feedback system for rumble/LED updates
            {
#ifdef CONFIG_DS5_DROP_SCREAM
                // While voice playback owns the channel, 0x36 frames carry the
                // control state — suppress the standalone 0x31 path entirely.
                if (ds5_voice_task(device, ds5)) break;

                // Hold the arm/disarm lightbar cue briefly, then restore
                if (ds5->toggle_cue_until != 0) {
                    if ((int32_t)(now - ds5->toggle_cue_until) < 0) break;
                    ds5->toggle_cue_until = 0;
                    ds5_restore_player_led(device, ds5);
                }
#endif
                int player_idx = find_player_index(ds5->event.dev_addr, ds5->event.instance);
                if (player_idx >= 0) {
                    feedback_state_t* fb = feedback_get_state(player_idx);
                    if (!fb) break;

                    bool need_update = false;
                    uint8_t r = ds5->led_r;
                    uint8_t g = ds5->led_g;
                    uint8_t b = ds5->led_b;
                    uint8_t player_led = ds5->player_led;
                    uint8_t rumble_left = ds5->rumble_left;
                    uint8_t rumble_right = ds5->rumble_right;

                    // Calculate player LED from pattern (like DS3)
                    // DS5 has separate player LED bar and RGB lightbar
                    uint8_t calc_player_led;
                    if (fb->led.pattern != 0) {
                        // Map feedback pattern to player number via PLAYER_LEDS[] lookup
                        int player_num = 0;
                        for (int p = 1; p <= 7; p++) {
                            if (fb->led.pattern == PLAYER_LEDS[p]) {
                                player_num = p - 1;
                                break;
                            }
                        }
                        int pat_idx = (player_num < 5) ? player_num : player_num % 5;
                        calc_player_led = PLAYER_LED_PATTERNS[pat_idx];
                    } else {
                        // Default to player index
                        int idx = (player_idx < 5) ? player_idx : player_idx % 5;
                        calc_player_led = PLAYER_LED_PATTERNS[idx];
                    }

                    // Check if player LED changed
                    if (calc_player_led != ds5->player_led) {
                        player_led = calc_player_led;
                        need_update = true;
                    }

                    // Check RGB lightbar from feedback
                    if (fb->led_dirty) {
                        if (fb->led.r != 0 || fb->led.g != 0 || fb->led.b != 0) {
                            // Host specified RGB color directly
                            r = fb->led.r;
                            g = fb->led.g;
                            b = fb->led.b;
                        } else if (fb->led.pattern != 0) {
                            // Use player color based on pattern via PLAYER_LEDS[] lookup
                            int player_num = 0;
                            for (int p = 1; p <= 7; p++) {
                                if (fb->led.pattern == PLAYER_LEDS[p]) {
                                    player_num = p - 1;
                                    break;
                                }
                            }
                            int color_idx = (player_num < 7) ? player_num : player_num % 7;
                            r = PLAYER_COLORS[color_idx][0];
                            g = PLAYER_COLORS[color_idx][1];
                            b = PLAYER_COLORS[color_idx][2];
                        } else {
                            // Default to player index color
                            int idx = (player_idx < 7) ? player_idx : player_idx % 7;
                            r = PLAYER_COLORS[idx][0];
                            g = PLAYER_COLORS[idx][1];
                            b = PLAYER_COLORS[idx][2];
                        }
                        player_led = calc_player_led;
                        need_update = true;
                    }

                    // Check rumble
                    if (fb->rumble_dirty) {
                        rumble_left = fb->rumble.left;
                        rumble_right = fb->rumble.right;
                        need_update = true;
                    }

                    // Also check if values changed (even without dirty flag)
                    if (rumble_left != ds5->rumble_left || rumble_right != ds5->rumble_right ||
                        r != ds5->led_r || g != ds5->led_g || b != ds5->led_b ||
                        player_led != ds5->player_led) {
                        need_update = true;
                    }

#ifdef CONFIG_DS5_DROP_SCREAM
                    // Voice playback just ended: resend LEDs the 0x36 acting overrode
                    if (ds5->led_refresh) {
                        ds5->led_refresh = false;
                        need_update = true;
                    }
#endif

                    if (need_update) {
                        ds5_send_output(device, rumble_left, rumble_right, r, g, b, player_led);
                        feedback_clear_dirty(player_idx);
                    }
                }
            }
            break;
    }
}

static void ds5_disconnect(bthid_device_t* device)
{
    printf("[DS5_BT] Disconnect: %s\n", device->name);

#ifdef CONFIG_DS5_DROP_SCREAM
    // ONLY clear the suppression flag (plain bool write, safe here). The
    // existing "No devices connected, resuming scan" path performs the actual
    // restart right after this callback — starting scan ourselves as well
    // double-starts GAP inquiry, which desyncs its state so the NEXT
    // connection's scan-stop doesn't fully stop it and the LMP encryption
    // exchange starves (reconnect hangs after auth, no player LED).
    btstack_host_suppress_scan(false);
#endif

    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (ds5) {
        // Clear router state first (sends zeroed input report)
        router_device_disconnected(ds5->event.dev_addr, ds5->event.instance);
        // Remove player assignment
        remove_players_by_address(ds5->event.dev_addr, ds5->event.instance);

        init_input_event(&ds5->event);
        ds5->initialized = false;
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t ds5_bt_driver = {
    .name = "Sony DualSense",
    .match = ds5_match,
    .init = ds5_init,
    .process_report = ds5_process_report,
    .task = ds5_task,
    .disconnect = ds5_disconnect,
};

void ds5_bt_register(void)
{
    bthid_register_driver(&ds5_bt_driver);
}
