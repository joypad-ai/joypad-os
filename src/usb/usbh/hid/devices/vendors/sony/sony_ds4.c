// sony_ds4.c
#include "sony_ds4.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "pico/time.h"
#include "app_config.h"
#include <string.h>

static uint16_t tpadLastPos;
static bool tpadDragging;

// DualSense instance state
typedef struct TU_ATTR_PACKED
{
  uint8_t rumble;
  uint8_t player;
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
  return ( 
    // Sony
    (vid == 0x054c && (pid == 0x09cc || pid == 0x05c4)) // Sony DualShock4 
    || (vid == 0x054c && pid == 0x0ba0) // Sony PS4 Wireless Adapter PC
    // HORI
    || (vid == 0x0f0d && pid == 0x005e) // Hori Fighting Commander 4 (FC4)
    || (vid == 0x0f0d && pid == 0x0066) // HORIPAD FPS+ (PS4)
    || (vid == 0x0f0d && pid == 0x008a) // HORI Real Arcade Pro (RAP) V HAYABUSA (Modo PS4)
    || (vid == 0x0f0d && pid == 0x00ee) // Hori Wired Controller Light (PS4 Mini / PS4-099U)
    // RAZER
    || (vid == 0x1532 && pid == 0x0401) // Razer Panthera PS4 Controller (GP2040-CE PS4 Mode)
    || (vid == 0x1532 && pid == 0x1004) // Razer Raiju Ultimate (High-end Pro controller)
    || (vid == 0x1532 && pid == 0x1008) // Razer Panthera EVO (Late model Arcade Stick)
    // Brook
    || (vid == 0x0c12 && pid == 0x0c30) // Brook Universal Fighting Board (Multi-console PCB)
    || (vid == 0x0c12 && pid == 0x0ef7) // Brook Fighting Board PS3/PS4 (PS4 Mode)
    // Mad Catz
    || (vid == 0x0738 && pid == 0x8180) // Mad Catz Fight Stick Alpha (Compact Stick)
    || (vid == 0x0738 && pid == 0x8384) // Mad Catz SFV Arcade FightStick TES+ (PS4 Mode)
    || (vid == 0x0738 && pid == 0x8481) // Mad Catz SFV Arcade FightStick TE2+ (PS4 Mode)
    // Quanba
    || (vid == 0x2c22 && pid == 0x2000) // Qanba Drone (Entry-level Arcade Stick)
    || (vid == 0x2c22 && pid == 0x2200) // Qanba Crystal (Arcade Stick with LEDs)
    || (vid == 0x2c22 && pid == 0x2300) // Qanba Obsidian (Professional Arcade Stick)
    // Other
    || (vid == 0x0c12 && pid == 0x1e1b) // Feir Wired FR-225C (Budget PS4 controller)
    || (vid == 0x146b && pid == 0x0d09) // Nacon Daija Arcade Stick (PS4 Mode)
    || (vid == 0x20d6 && pid == 0x792a) // PowerA FUSION Wired FightPad (6-button layout)
    || (vid == 0x1f4f && pid == 0x1002) // ASW Guilty Gear xrd Controller (Collector's Edition)
    || (vid == 0x04d8 && pid == 0x1529) // Universal PCB Project (UPCB Open Source)
  );
}

// check if 2 reports are different enough
bool diff_report_ds4(sony_ds4_report_t const* rpt1, sony_ds4_report_t const* rpt2)
{
  bool result;

  // x, y, z, rz must different than 2 to be counted
  result = diff_than_n(rpt1->x, rpt2->x, 2) || diff_than_n(rpt1->y, rpt2->y, 2) ||
           diff_than_n(rpt1->z, rpt2->z, 2) || diff_than_n(rpt1->rz, rpt2->rz, 2) ||
           diff_than_n(rpt1->l2_trigger, rpt2->l2_trigger, 2) ||
           diff_than_n(rpt1->r2_trigger, rpt2->r2_trigger, 2);

  // check the reset with mem compare
  result |= memcmp(&rpt1->rz + 1, &rpt2->rz + 1, 2);
  result |= (rpt1->ps != rpt2->ps);
  result |= (rpt1->tpad != rpt2->tpad);
  result |= memcmp(&rpt1->tpad_f1_pos, &rpt2->tpad_f1_pos, 3);

  return result;
}

// process usb hid input reports
void input_sony_ds4(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  uint32_t buttons;
  // previous report used to compare for changes
  static sony_ds4_report_t prev_report[5] = { 0 };

  uint8_t const report_id = report[0];
  report++;
  len--;

  // all buttons state is stored in ID 1
  if (report_id == 1)
  {
    sony_ds4_report_t ds4_report;
    memcpy(&ds4_report, report, sizeof(ds4_report));

    // counter is +1, assign to make it easier to compare 2 report
    prev_report[dev_addr-1].counter = ds4_report.counter;

    // only print if changes since it is polled ~ 5ms
    // Since count+1 after each report and  x, y, z, rz fluctuate within 1 or 2
    // We need more than memcmp to check if report is different enough
    if ( diff_report_ds4(&prev_report[dev_addr-1], &ds4_report) )
    {
      TU_LOG1("(x, y, z, rz, l, r) = (%u, %u, %u, %u, %u, %u)\r\n", ds4_report.x, ds4_report.y, ds4_report.z, ds4_report.rz, ds4_report.r2_trigger, ds4_report.l2_trigger);
      TU_LOG1("DPad = %s ", ds4_report.dpad);

      if (ds4_report.square   ) TU_LOG1("Square ");
      if (ds4_report.cross    ) TU_LOG1("Cross ");
      if (ds4_report.circle   ) TU_LOG1("Circle ");
      if (ds4_report.triangle ) TU_LOG1("Triangle ");

      if (ds4_report.l1       ) TU_LOG1("L1 ");
      if (ds4_report.r1       ) TU_LOG1("R1 ");
      if (ds4_report.l2       ) TU_LOG1("L2 ");
      if (ds4_report.r2       ) TU_LOG1("R2 ");

      if (ds4_report.share    ) TU_LOG1("Share ");
      if (ds4_report.option   ) TU_LOG1("Option ");
      if (ds4_report.l3       ) TU_LOG1("L3 ");
      if (ds4_report.r3       ) TU_LOG1("R3 ");

      if (ds4_report.ps       ) TU_LOG1("PS ");
      if (ds4_report.tpad     ) TU_LOG1("TPad ");

      if (!ds4_report.tpad_f1_down) TU_LOG1("F1 ");

      uint16_t tx = (((ds4_report.tpad_f1_pos[1] & 0x0f) << 8)) | ((ds4_report.tpad_f1_pos[0] & 0xff) << 0);
      uint16_t ty = (((ds4_report.tpad_f1_pos[1] & 0xf0) >> 4)) | ((ds4_report.tpad_f1_pos[2] & 0xff) << 4);
      // TU_LOG1(" (tx, ty) = (%u, %u)\r\n", tx, ty);
      // TU_LOG1("\r\n");

      bool dpad_up    = (ds4_report.dpad == 0 || ds4_report.dpad == 1 || ds4_report.dpad == 7);
      bool dpad_right = ((ds4_report.dpad >= 1 && ds4_report.dpad <= 3));
      bool dpad_down  = ((ds4_report.dpad >= 3 && ds4_report.dpad <= 5));
      bool dpad_left  = ((ds4_report.dpad >= 5 && ds4_report.dpad <= 7));

      // Touchpad left/right click detection (touchpad is ~1920 wide, center at 960)
      bool tpad_left = ds4_report.tpad && !ds4_report.tpad_f1_down && tx < 960;
      bool tpad_right = ds4_report.tpad && !ds4_report.tpad_f1_down && tx >= 960;

      buttons = (((dpad_up)             ? JP_BUTTON_DU : 0) |
                 ((dpad_down)           ? JP_BUTTON_DD : 0) |
                 ((dpad_left)           ? JP_BUTTON_DL : 0) |
                 ((dpad_right)          ? JP_BUTTON_DR : 0) |
                 ((ds4_report.cross)    ? JP_BUTTON_B1 : 0) |
                 ((ds4_report.circle)   ? JP_BUTTON_B2 : 0) |
                 ((ds4_report.square)   ? JP_BUTTON_B3 : 0) |
                 ((ds4_report.triangle) ? JP_BUTTON_B4 : 0) |
                 ((ds4_report.l1)       ? JP_BUTTON_L1 : 0) |
                 ((ds4_report.r1)       ? JP_BUTTON_R1 : 0) |
                 ((ds4_report.l2)       ? JP_BUTTON_L2 : 0) |
                 ((ds4_report.r2)       ? JP_BUTTON_R2 : 0) |
                 ((ds4_report.share)    ? JP_BUTTON_S1 : 0) |
                 ((ds4_report.option)   ? JP_BUTTON_S2 : 0) |
                 ((ds4_report.l3)       ? JP_BUTTON_L3 : 0) |
                 ((ds4_report.r3)       ? JP_BUTTON_R3 : 0) |
                 ((ds4_report.ps)       ? JP_BUTTON_A1 : 0) |
                 ((ds4_report.tpad)     ? JP_BUTTON_A2 : 0) |
                 ((tpad_left)           ? JP_BUTTON_L4 : 0) |
                 ((tpad_right)          ? JP_BUTTON_R4 : 0));

      uint8_t analog_1x = ds4_report.x;
      uint8_t analog_1y = ds4_report.y;   // HID convention: 0=up, 255=down
      uint8_t analog_2x = ds4_report.z;
      uint8_t analog_2y = ds4_report.rz;  // HID convention: 0=up, 255=down
      uint8_t analog_l = ds4_report.l2_trigger;
      uint8_t analog_r = ds4_report.r2_trigger;

      // Touch Pad - provides mouse-like delta for horizontal swipes
      // Can be used for spinners, camera control, etc. (platform-agnostic)
      int8_t touchpad_delta_x = 0;
      if (!ds4_report.tpad_f1_down) {
        // Calculate horizontal swipe delta while finger is down
        if (tpadDragging) {
          int16_t delta = 0;
          if (tx >= tpadLastPos) delta = tx - tpadLastPos;
          else delta = (-1) * (tpadLastPos - tx);

          // Clamp delta to reasonable range
          if (delta > 12) delta = 12;
          if (delta < -12) delta = -12;

          touchpad_delta_x = (int8_t)delta;
        }

        tpadLastPos = tx;
        tpadDragging = true;
      } else {
        tpadDragging = false;
      }

      // keep analog within range [1-255]
      ensureAllNonZero(&analog_1x, &analog_1y, &analog_2x, &analog_2y);

      // adds deadzone
      uint8_t deadzone = 40;
      if (analog_1x > (128-(deadzone/2)) && analog_1x < (128+(deadzone/2))) analog_1x = 128;
      if (analog_1y > (128-(deadzone/2)) && analog_1y < (128+(deadzone/2))) analog_1y = 128;
      if (analog_2x > (128-(deadzone/2)) && analog_2x < (128+(deadzone/2))) analog_2x = 128;
      if (analog_2y > (128-(deadzone/2)) && analog_2y < (128+(deadzone/2))) analog_2y = 128;

      // add to accumulator and post to the state machine
      // if a scan from the host machine is ongoing, wait
      input_event_t event = {
        .dev_addr = dev_addr,
        .instance = instance,
        .type = INPUT_TYPE_GAMEPAD,
        .transport = INPUT_TRANSPORT_USB,
        .transport = INPUT_TRANSPORT_USB,
        .transport = INPUT_TRANSPORT_USB,
        .transport = INPUT_TRANSPORT_USB,
        .buttons = buttons,
        .button_count = 10,  // PS4: Cross, Circle, Square, Triangle, L1, R1, L2, R2, L3, R3
        .analog = {analog_1x, analog_1y, analog_2x, analog_2y, analog_l, analog_r},
        .delta_x = touchpad_delta_x,  // Touchpad horizontal swipe as mouse-like delta
        .keys = 0,
        // Motion data (DS4 has full 3-axis gyro and accel)
        .has_motion = true,
        .accel = {ds4_report.accel[0], ds4_report.accel[1], ds4_report.accel[2]},
        .gyro = {ds4_report.gyro[0], ds4_report.gyro[1], ds4_report.gyro[2]},
      };
      router_submit_input(&event);

      prev_report[dev_addr-1] = ds4_report;
    }
  }
}

// process usb hid output reports
void output_sony_ds4(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
  sony_ds4_output_report_t output_report = {0};
  output_report.set_led = 1;

  // Console-specific LED colors from led_config.h
  switch (config->player_index+1)
  {
  case 1:
    output_report.lightbar_red = LED_P1_R;
    output_report.lightbar_green = LED_P1_G;
    output_report.lightbar_blue = LED_P1_B;
    break;

  case 2:
    output_report.lightbar_red = LED_P2_R;
    output_report.lightbar_green = LED_P2_G;
    output_report.lightbar_blue = LED_P2_B;
    break;

  case 3:
    output_report.lightbar_red = LED_P3_R;
    output_report.lightbar_green = LED_P3_G;
    output_report.lightbar_blue = LED_P3_B;
    break;

  case 4:
    output_report.lightbar_red = LED_P4_R;
    output_report.lightbar_green = LED_P4_G;
    output_report.lightbar_blue = LED_P4_B;
    break;

  case 5:
    output_report.lightbar_red = LED_P5_R;
    output_report.lightbar_green = LED_P5_G;
    output_report.lightbar_blue = LED_P5_B;
    break;

  default:
    output_report.lightbar_red = LED_DEFAULT_R;
    output_report.lightbar_green = LED_DEFAULT_G;
    output_report.lightbar_blue = LED_DEFAULT_B;
    break;
  }

  // fun
  if (config->player_index+1 && config->test) {
    output_report.lightbar_red = config->test;
    output_report.lightbar_green = (config->test%2 == 0) ? config->test+64 : 0;
    output_report.lightbar_blue = (config->test%2 == 0) ? 0 : config->test+128;
  }

  output_report.set_rumble = 1;
  if (config->rumble) {
    output_report.motor_left = 192;
    output_report.motor_right = 192;
  } else {
    output_report.motor_left = 0;
    output_report.motor_right = 0;
  }

  if (ds4_devices[dev_addr].instances[instance].rumble != config->rumble ||
      ds4_devices[dev_addr].instances[instance].player != config->player_index+1 ||
      config->test)
  {
    ds4_devices[dev_addr].instances[instance].rumble = config->rumble;
    ds4_devices[dev_addr].instances[instance].player = config->test ? config->test : config->player_index+1;
    tuh_hid_send_report(dev_addr, instance, 5, &output_report, sizeof(output_report));
  }
}

// process usb hid output reports
void task_sony_ds4(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
  const uint32_t interval_ms = 20;
  static uint32_t start_ms = 0;

  uint32_t current_time_ms = to_ms_since_boot(get_absolute_time());
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
