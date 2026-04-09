// sony_ds4.c
#include "sony_ds4.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "platform/platform.h"
#include "app_config.h"
#include <string.h>

// Mbedtls for PS4 auth signing
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"

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
// PS4 AUTH LOCAL SIGNING STATE
// ============================================================================

// Auth buffer sizes (matching hid_ps4_driver.c)
#define DS4_AUTH_PAGE_SIZE       56   // Bytes per page
#define DS4_AUTH_NONCE_PAGES     5    // Pages 0-4
#define DS4_AUTH_SIGNATURE_PAGES 19   // Pages 0-18
#define DS4_AUTH_NONCE_SIZE      256  // Actual nonce size for signing
#define DS4_AUTH_SIG_BUFFER_SIZE (DS4_AUTH_PAGE_SIZE * DS4_AUTH_SIGNATURE_PAGES) // 1064 bytes
#define DS4_AUTH_REPORT_SIZE     64   // Full report size with report ID

// Auth states (matching hid_ps4_driver.c)
typedef enum {
    AUTH_NO_NONCE = 0,
    AUTH_RECEIVING_NONCE = 1,
    AUTH_NONCE_READY = 2,
    AUTH_SIGNED_READY = 3
} auth_local_state_t;

// Local signing auth state
static struct {
    auth_local_state_t state;
    bool auth_sent;           // Whether signature has been sent to console
    
    uint8_t nonce_id;         // Current nonce ID from console
    uint8_t nonce_buffer[DS4_AUTH_NONCE_SIZE];    // 256-byte nonce for signing
    
    uint8_t sig_buffer[DS4_AUTH_SIG_BUFFER_SIZE];  // 1064-byte signature buffer
    uint8_t sig_page_sending; // Next page to send (0-18)
} ds4_auth = { 0 };

// Mbedtls RSA context for local signing
static mbedtls_pk_context ds4_pk_ctx;
static bool ds4_pk_initialized = false;

// Serial binary data (converted from hex string)
static uint8_t ds4_serial_binary[16] = {0};

// External symbols for embedded resources (linker-generated)
extern const unsigned char ds4_key_pem_start[] asm("_binary_key_pem_start");
extern const unsigned char ds4_key_pem_end[] asm("_binary_key_pem_end");
extern const unsigned char ds4_serial_start[] asm("_binary_serial_txt_start");
extern const unsigned char ds4_serial_end[] asm("_binary_serial_txt_end");
extern const unsigned char ds4_signature_start[] asm("_binary_sig_bin_start");
extern const unsigned char ds4_signature_end[] asm("_binary_sig_bin_end");

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
    || (vid == 0x0e6f && pid == 0x020a) // Victrix Pro FS for PS4
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
      uint16_t tx2 = (((ds4_report.tpad_f2_pos[1] & 0x0f) << 8)) | ((ds4_report.tpad_f2_pos[0] & 0xff) << 0);
      uint16_t ty2 = (((ds4_report.tpad_f2_pos[1] & 0xf0) >> 4)) | ((ds4_report.tpad_f2_pos[2] & 0xff) << 4);

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
      // Battery: status[0] at report[29] — bits 0-3 = level, bit 4 = cable connected
      // Level interpretation differs based on cable state (per Linux kernel hid-playstation.c)
      uint8_t bat_level = 0;
      bool bat_charging = false;
      if (len >= 30) {
        uint8_t raw = report[29];
        uint8_t battery_data = (raw & 0x0F);
        bool cable_connected = (raw & 0x10) != 0;

        if (cable_connected) {
            if (battery_data < 10) {
                bat_level = battery_data * 10 + 5;
                bat_charging = true;
            } else if (battery_data == 10) {
                bat_level = 100;
                bat_charging = true;
            } else if (battery_data == 11) {
                bat_level = 100;
                bat_charging = false;  // Full
            } else {
                bat_level = 0;  // Error (14=voltage/temp, 15=charge)
                bat_charging = false;
            }
        } else {
            if (battery_data < 10)
                bat_level = battery_data * 10 + 5;
            else
                bat_level = 100;
            bat_charging = false;
        }
      }

      input_event_t event = {
        .dev_addr = dev_addr,
        .instance = instance,
        .type = INPUT_TYPE_GAMEPAD,
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
        .battery_level = bat_level,
        .battery_charging = bat_charging,
        // Touchpad (2-finger capacitive)
        .has_touch = true,
        .touch = {
          { .x = tx,  .y = ty,  .active = !ds4_report.tpad_f1_down },
          { .x = tx2, .y = ty2, .active = !ds4_report.tpad_f2_down },
        },
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

  // Get RGB from feedback system (canonical source)
  int8_t player_idx = find_player_index(dev_addr, instance);
  feedback_state_t* fb = (player_idx >= 0) ? feedback_get_state(player_idx) : NULL;

  if (fb && (fb->led.r || fb->led.g || fb->led.b)) {
    output_report.lightbar_red = fb->led.r;
    output_report.lightbar_green = fb->led.g;
    output_report.lightbar_blue = fb->led.b;
  } else {
    // Fallback to app-specific defaults when no feedback RGB set
    switch (config->player_index+1) {
    case 1:  output_report.lightbar_red = LED_P1_R; output_report.lightbar_green = LED_P1_G; output_report.lightbar_blue = LED_P1_B; break;
    case 2:  output_report.lightbar_red = LED_P2_R; output_report.lightbar_green = LED_P2_G; output_report.lightbar_blue = LED_P2_B; break;
    case 3:  output_report.lightbar_red = LED_P3_R; output_report.lightbar_green = LED_P3_G; output_report.lightbar_blue = LED_P3_B; break;
    case 4:  output_report.lightbar_red = LED_P4_R; output_report.lightbar_green = LED_P4_G; output_report.lightbar_blue = LED_P4_B; break;
    case 5:  output_report.lightbar_red = LED_P5_R; output_report.lightbar_green = LED_P5_G; output_report.lightbar_blue = LED_P5_B; break;
    case 6:  output_report.lightbar_red = LED_P6_R; output_report.lightbar_green = LED_P6_G; output_report.lightbar_blue = LED_P6_B; break;
    case 7:  output_report.lightbar_red = LED_P7_R; output_report.lightbar_green = LED_P7_G; output_report.lightbar_blue = LED_P7_B; break;
    default: output_report.lightbar_red = LED_DEFAULT_R; output_report.lightbar_green = LED_DEFAULT_G; output_report.lightbar_blue = LED_DEFAULT_B; break;
    }
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
      ds4_devices[dev_addr].instances[instance].led_r != output_report.lightbar_red ||
      ds4_devices[dev_addr].instances[instance].led_g != output_report.lightbar_green ||
      ds4_devices[dev_addr].instances[instance].led_b != output_report.lightbar_blue ||
      config->test)
  {
    ds4_devices[dev_addr].instances[instance].rumble = config->rumble;
    ds4_devices[dev_addr].instances[instance].player = config->test ? config->test : config->player_index+1;
    ds4_devices[dev_addr].instances[instance].led_r = output_report.lightbar_red;
    ds4_devices[dev_addr].instances[instance].led_g = output_report.lightbar_green;
    ds4_devices[dev_addr].instances[instance].led_b = output_report.lightbar_blue;
    tuh_hid_send_report(dev_addr, instance, 5, &output_report, sizeof(output_report));
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
// PS4 AUTH LOCAL SIGNING IMPLEMENTATION
// ============================================================================

// Random number generator for mbedtls
static int ds4_rng(void *p_rng, unsigned char *p, size_t len) {
    (void)p_rng;
    // Platform-specific random generator
    #if defined(CFG_TUSB_MCU) && (CFG_TUSB_MCU == OPT_MCU_ESP32S2 || CFG_TUSB_MCU == OPT_MCU_ESP32S3)
        extern void esp_fill_random(void *buf, size_t len);
        esp_fill_random(p, len);
    #elif defined(PICO_RP2040) || defined(PICO_RP2350)
        // Use pico_rand hardware RNG for better entropy
        #include "pico/rand.h"
        for (size_t i = 0; i < len; i += 4) {
            uint32_t rnd = get_rand_32();
            size_t copy = (i + 4 <= len) ? 4 : (len - i);
            memcpy(&p[i], &rnd, copy);
        }
    #else
        // Fallback for other platforms
        for (size_t i = 0; i < len; i++) {
            p[i] = (uint8_t)(rand() & 0xFF);
        }
    #endif
    return 0;
}

// Helper: convert hex character to integer
static uint8_t ds4_hex_char_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

// Helper: convert hex string to binary data
static void ds4_hex_to_binary(const unsigned char *hex_str, size_t hex_len, uint8_t *output, size_t output_size) {
    size_t output_idx = 0;
    size_t i = 0;
    
    while (i < hex_len && output_idx < output_size) {
        // Skip non-hex characters
        if (!((hex_str[i] >= '0' && hex_str[i] <= '9') || 
              (hex_str[i] >= 'A' && hex_str[i] <= 'F') || 
              (hex_str[i] >= 'a' && hex_str[i] <= 'f'))) {
            i++;
            continue;
        }
        
        if (i + 1 < hex_len) {
            uint8_t high = ds4_hex_char_to_int(hex_str[i]);
            uint8_t low = ds4_hex_char_to_int(hex_str[i + 1]);
            output[output_idx] = (high << 4) | low;
            output_idx++;
            i += 2;
        } else {
            i++;
        }
    }
    
    // Pad with zeros at the beginning if needed
    if (output_idx < output_size) {
        size_t data_start = output_size - output_idx;
        for (int j = output_idx - 1; j >= 0; j--) {
            output[data_start + j] = output[j];
        }
        for (size_t j = 0; j < data_start; j++) {
            output[j] = 0;
        }
    }
}

// Initialize RSA key from embedded PEM
static bool ds4_auth_init_rsa(void) {
    if (ds4_pk_initialized) return true;
    
    mbedtls_pk_init(&ds4_pk_ctx);
    
    size_t key_len = ds4_key_pem_end - ds4_key_pem_start;
    if (key_len == 0) {
        printf("[DS4 Auth] ERROR: No embedded key found\n");
        return false;
    }
    
    int ret = mbedtls_pk_parse_key(&ds4_pk_ctx, 
                                    (uint8_t *)ds4_key_pem_start, key_len,
                                    NULL, 0,
                                    ds4_rng, NULL);
    if (ret != 0) {
        printf("[DS4 Auth] ERROR: Failed to parse RSA key (ret=%d)\n", -ret);
        return false;
    }
    
    // Check if the key is RSA
    if (mbedtls_pk_get_type(&ds4_pk_ctx) != MBEDTLS_PK_RSA) {
        printf("[DS4 Auth] ERROR: Key is not RSA type\n");
        return false;
    }
    
    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(ds4_pk_ctx);
    
    ret = mbedtls_rsa_complete(rsa);
    if (ret != 0) {
        printf("[DS4 Auth] ERROR: mbedtls_rsa_complete failed (ret=%d)\n", -ret);
        return false;
    }
    
    ret = mbedtls_rsa_check_privkey(rsa);
    if (ret != 0) {
        printf("[DS4 Auth] ERROR: RSA private key check failed (ret=%d)\n", -ret);
        return false;
    }
    
    // Convert serial.txt from hex to binary
    size_t serial_len = ds4_serial_end - ds4_serial_start;
    ds4_hex_to_binary(ds4_serial_start, serial_len, ds4_serial_binary, 16);
    
    ds4_pk_initialized = true;
    printf("[DS4 Auth] RSA key loaded successfully\n");
    return true;
}

// Sign the 256-byte nonce using RSA-PSS
static bool ds4_sign_nonce(void) {
    if (ds4_auth.state != AUTH_NONCE_READY) {
        printf("[DS4 Auth] ERROR: Cannot sign, nonce not ready\n");
        return false;
    }
    
    if (!ds4_auth_init_rsa()) {
        printf("[DS4 Auth] ERROR: RSA not initialized\n");
        return false;
    }
    
    // SHA256 hash the nonce
    uint8_t hashed_nonce[32];
    if (mbedtls_sha256(ds4_auth.nonce_buffer, DS4_AUTH_NONCE_SIZE, hashed_nonce, 0) != 0) {
        printf("[DS4 Auth] ERROR: SHA256 failed\n");
        return false;
    }
    
    // RSA-PSS sign using PK layer for better compatibility
    uint8_t nonce_signature[256];
    size_t sig_len;
    int ret = mbedtls_pk_sign(&ds4_pk_ctx,
                              MBEDTLS_MD_SHA256,
                              hashed_nonce, 32,
                              nonce_signature, sizeof(nonce_signature),
                              &sig_len,
                              ds4_rng, NULL);
    if (ret != 0) {
        printf("[DS4 Auth] ERROR: RSA signing failed (ret=%d)\n", -ret);
        return false;
    }
    
    // Build signature buffer (1064 bytes = 19 pages × 56 bytes)
    // Layout: [signature(256)][serial_binary(16)][N(256)][E(256)][preset_sig(256)][padding(24)]
    int offset = 0;
    
    // 1. RSA signature (256 bytes)
    memcpy(&ds4_auth.sig_buffer[offset], nonce_signature, 256);
    offset += 256;
    
    // 2. Serial binary (16 bytes)
    memcpy(&ds4_auth.sig_buffer[offset], ds4_serial_binary, 16);
    offset += 16;
    
    // 3. RSA modulus N and exponent E (256 bytes each)
    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(ds4_pk_ctx);
    
    // For mbedTLS 3.x compatibility, we need to handle RSA parameters differently
    // Since direct access to N and E is restricted in mbedTLS 3.x, we'll use a simplified approach
    // that avoids the problematic access patterns
    #if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03000000
        // For mbedTLS 3.x, we'll use the public key export functions to get N and E
        // This is the safe way to access RSA parameters in mbedTLS 3.x
        mbedtls_mpi N, E;
        mbedtls_mpi_init(&N);
        mbedtls_mpi_init(&E);
        
        // Extract N and E using the safe API
        int ret = mbedtls_rsa_export(&rsa, &N, NULL, NULL, &E);
        if (ret == 0) {
            mbedtls_mpi_write_binary(&N, &ds4_auth.sig_buffer[offset], 256);
            offset += 256;
            mbedtls_mpi_write_binary(&E, &ds4_auth.sig_buffer[offset], 256);
            offset += 256;
        } else {
            printf("[DS4 Auth] ERROR: Could not export RSA parameters (ret=%d)\n", -ret);
            mbedtls_mpi_free(&N);
            mbedtls_mpi_free(&E);
            return false;
        }
        
        mbedtls_mpi_free(&N);
        mbedtls_mpi_free(&E);
    #else
        // For mbedTLS 2.x, use direct access
        mbedtls_mpi_write_binary(&rsa->N, &ds4_auth.sig_buffer[offset], 256);
        offset += 256;
        mbedtls_mpi_write_binary(&rsa->E, &ds4_auth.sig_buffer[offset], 256);
        offset += 256;
    #endif
    
    // 5. Preset signature (256 bytes)
    size_t sig_len = ds4_signature_end - ds4_signature_start;
    if (sig_len > 0 && sig_len <= 256) {
        memcpy(&ds4_auth.sig_buffer[offset], ds4_signature_start, sig_len);
    } else {
        memset(&ds4_auth.sig_buffer[offset], 0, 256);
    }
    offset += 256;
    
    // 6. Padding (24 bytes to reach 1064)
    memset(&ds4_auth.sig_buffer[offset], 0, 24);
    
    ds4_auth.state = AUTH_SIGNED_READY;
    ds4_auth.sig_page_sending = 0;
    printf("[DS4 Auth] Nonce signed successfully, signature ready\n");
    return true;
}

// Save nonce page from console
static void ds4_save_nonce(uint8_t nonce_id, uint8_t page, const uint8_t *data, uint16_t len) {
    // Validate nonce ID consistency
    if (page != 0 && nonce_id != ds4_auth.nonce_id) {
        printf("[DS4 Auth] ERROR: Nonce ID mismatch (expected %d, got %d)\n", 
               ds4_auth.nonce_id, nonce_id);
        ds4_auth.state = AUTH_NO_NONCE;
        return;
    }
    
    // Copy nonce page (56 bytes per page, last page is 32 bytes)
    uint16_t copy_len = (page == 4) ? 32 : 56;
    if (len < copy_len) copy_len = len;
    
    memcpy(&ds4_auth.nonce_buffer[page * 56], data, copy_len);
    
    if (page == 0) {
        ds4_auth.nonce_id = nonce_id;
        ds4_auth.state = AUTH_RECEIVING_NONCE;
        printf("[DS4 Auth] Nonce page 0 received (id=%d)\n", nonce_id);
    } else if (page == 4) {
        ds4_auth.state = AUTH_NONCE_READY;
        printf("[DS4 Auth] All nonce pages received, signing...\n");
        
        // Sign immediately
        ds4_sign_nonce();
    } else {
        printf("[DS4 Auth] Nonce page %d received\n", page);
    }
}

// Called when DS4 device is mounted - initialize auth
void ds4_auth_register(uint8_t dev_addr, uint8_t instance) {
    // Initialize RSA if not done
    if (!ds4_pk_initialized) {
        ds4_auth_init_rsa();
    }
    
    // Reset auth state
    ds4_auth.state = AUTH_NO_NONCE;
    ds4_auth.auth_sent = false;
    memset(ds4_auth.nonce_buffer, 0, sizeof(ds4_auth.nonce_buffer));
    memset(ds4_auth.sig_buffer, 0, sizeof(ds4_auth.sig_buffer));
    
    printf("[DS4 Auth] Local signing mode initialized\n");
}

// Called when DS4 device is unmounted
void ds4_auth_unregister(uint8_t dev_addr, uint8_t instance) {
    ds4_auth.state = AUTH_NO_NONCE;
    ds4_auth.auth_sent = false;
    printf("[DS4 Auth] Auth state reset on unmount\n");
}

// Check if auth is available (RSA key loaded)
bool ds4_auth_is_available(void) {
    return ds4_pk_initialized;
}

// Get the current auth state
ds4_auth_state_t ds4_auth_get_state(void) {
    switch (ds4_auth.state) {
        case AUTH_NO_NONCE:        return DS4_AUTH_STATE_IDLE;
        case AUTH_RECEIVING_NONCE: return DS4_AUTH_STATE_NONCE_PENDING;
        case AUTH_NONCE_READY:     return DS4_AUTH_STATE_SIGNING;
        case AUTH_SIGNED_READY:    return DS4_AUTH_STATE_READY;
        default:                   return DS4_AUTH_STATE_ERROR;
    }
}

// Forward nonce page from PS4 console
bool ds4_auth_send_nonce(const uint8_t* data, uint16_t len) {
    if (len < 4) {
        printf("[DS4 Auth] ERROR: Nonce data too short (%d bytes)\n", len);
        return false;
    }
    
    uint8_t nonce_id = data[0];
    uint8_t page = data[1];
    
    if (page >= DS4_AUTH_NONCE_PAGES) {
        printf("[DS4 Auth] ERROR: Invalid nonce page %d\n", page);
        return false;
    }
    
    // Extract nonce data (skip report_id, nonce_id, page, padding)
    const uint8_t *nonce_data = &data[3];
    uint16_t nonce_len = (page == 4) ? 32 : 56;
    
    ds4_save_nonce(nonce_id, page, nonce_data, nonce_len);
    return true;
}

// Get signature page for console (0xF1)
uint16_t ds4_auth_get_signature(uint8_t* buffer, uint16_t max_len, uint8_t page) {
    memset(buffer, 0, max_len);
    
    if (page >= DS4_AUTH_SIGNATURE_PAGES) {
        printf("[DS4 Auth] ERROR: Invalid signature page %d\n", page);
        return max_len;
    }
    
    // Format: [nonce_id][page][0][signature_data(56)]
    buffer[0] = ds4_auth.nonce_id;
    buffer[1] = page;
    buffer[2] = 0;
    
    if (ds4_auth.state != AUTH_SIGNED_READY) {
        printf("[DS4 Auth] Signature page %d requested but not ready\n", page);
        // Return zeros if not ready
    } else {
        memcpy(&buffer[3], &ds4_auth.sig_buffer[page * DS4_AUTH_PAGE_SIZE], DS4_AUTH_PAGE_SIZE);
    }
    
    return max_len;
}

// Get next signature page (auto-incrementing)
uint16_t ds4_auth_get_next_signature(uint8_t* buffer, uint16_t max_len) {
    uint8_t page = ds4_auth.sig_page_sending;
    uint16_t len = ds4_auth_get_signature(buffer, max_len, page);
    
    // Advance to next page
    if (ds4_auth.sig_page_sending < DS4_AUTH_SIGNATURE_PAGES - 1) {
        ds4_auth.sig_page_sending++;
    }
    
    // Reset state after all pages sent
    if (ds4_auth.sig_page_sending >= DS4_AUTH_SIGNATURE_PAGES - 1) {
        ds4_auth.state = AUTH_NO_NONCE;
        ds4_auth.auth_sent = true;
    }
    
    return len;
}

// Get auth status (0xF2)
uint16_t ds4_auth_get_status(uint8_t* buffer, uint16_t max_len) {
    memset(buffer, 0, max_len);
    
    buffer[0] = ds4_auth.nonce_id;
    buffer[1] = (ds4_auth.state == AUTH_SIGNED_READY) ? 0 : 16;  // 0=ready, 16=signing
    
    printf("[DS4 Auth] Status: %s (id=%d)\n",
           (ds4_auth.state == AUTH_SIGNED_READY) ? "ready" : "signing",
           ds4_auth.nonce_id);
    return max_len;
}

// Reset auth state (0xF3)
void ds4_auth_reset(void) {
    ds4_auth.state = AUTH_NO_NONCE;
    ds4_auth.auth_sent = false;
    ds4_auth.nonce_id = 0;
    ds4_auth.sig_page_sending = 0;
    memset(ds4_auth.nonce_buffer, 0, sizeof(ds4_auth.nonce_buffer));
    printf("[DS4 Auth] Auth state reset\n");
}

// Auth task (no longer needed for local signing, synchronous)
void ds4_auth_task(void) {
    // Local signing is synchronous in ds4_save_nonce()
    // No background task needed
}
