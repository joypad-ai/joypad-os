// xinput.c - X-input protocol handler (TinyUSB X-input host callbacks)
#include "tusb.h"
#include "host/usbh_pvt.h"
#include "pico/time.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/router/router.h"
#include "xinput_host.h"
#include "chatpad.h"
#include "core/input_event.h"

// Xbox One auth passthrough - weak stubs for non-USB-device builds
// These are overridden by xbone_auth.c when linked (usb2usb build)
__attribute__((weak)) void xbone_auth_init(void) {}
__attribute__((weak)) void xbone_auth_task(void) {}
__attribute__((weak)) void xbone_auth_register(uint8_t dev_addr, uint8_t instance) { (void)dev_addr; (void)instance; }
__attribute__((weak)) void xbone_auth_unregister(uint8_t dev_addr) { (void)dev_addr; }
__attribute__((weak)) void xbone_auth_report_received(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) { (void)dev_addr; (void)instance; (void)report; (void)len; }

// BTstack driver for Bluetooth dongles
#if CFG_TUH_BTD
#include "usb/usbh/btd/hci_transport_h2_tinyusb.h"
#endif

uint32_t buttons;
int last_player_count = 0; // used by xboxone

// Chatpad keepalive tracking (per device/instance)
// Size +1 because device addresses are 1-indexed (1 to CFG_TUH_DEVICE_MAX inclusive)
static uint32_t chatpad_last_keepalive[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_XINPUT];

uint8_t byteScaleAnalog(int16_t xbox_val);

//--------------------------------------------------------------------+
// Custom USB Host Drivers
//--------------------------------------------------------------------+

#if CFG_TUH_XINPUT || CFG_TUH_BTD
#include <string.h>

// Count how many drivers we have
enum {
    CUSTOM_DRIVER_COUNT = 0
#if CFG_TUH_XINPUT
    + 1
#endif
#if CFG_TUH_BTD
    + 1
#endif
};

// Static storage for driver array (not const so we can memcpy into it)
static uint8_t custom_driver_storage[CUSTOM_DRIVER_COUNT * sizeof(usbh_class_driver_t)];
static bool drivers_initialized = false;

usbh_class_driver_t const* usbh_app_driver_get_cb(uint8_t* driver_count) {
    if (!drivers_initialized) {
        usbh_class_driver_t* drivers = (usbh_class_driver_t*)custom_driver_storage;
        int idx = 0;
#if CFG_TUH_XINPUT
        memcpy(&drivers[idx++], &usbh_xinput_driver, sizeof(usbh_class_driver_t));
#endif
#if CFG_TUH_BTD
        memcpy(&drivers[idx++], &usbh_btstack_driver, sizeof(usbh_class_driver_t));
#endif
        drivers_initialized = true;
    }
    *driver_count = CUSTOM_DRIVER_COUNT;
    return (usbh_class_driver_t const*)custom_driver_storage;
}
#endif

//--------------------------------------------------------------------+
// USB X-input
//--------------------------------------------------------------------+
#if CFG_TUH_XINPUT

void tuh_xinput_report_received_cb(uint8_t dev_addr, uint8_t instance, xinputh_interface_t const* xid_itf, uint16_t len)
{
  uint32_t buttons;
  const xinput_gamepad_t *p = &xid_itf->pad;
  const char* type_str;

  if (xid_itf->last_xfer_result == XFER_RESULT_SUCCESS)
  {
    switch (xid_itf->type)
    {
      case 1: type_str = "Xbox One";          break;
      case 2: type_str = "Xbox 360 Wireless"; break;
      case 3: type_str = "Xbox 360 Wired";    break;
      case 4: type_str = "Xbox OG";           break;
      default: type_str = "Unknown";
    }

    if (xid_itf->connected && xid_itf->new_pad_data)
    {
      // TU_LOG1("[%02x, %02x], Type: %s, Buttons %04x, LT: %02x RT: %02x, LX: %d, LY: %d, RX: %d, RY: %d\n",
      //   dev_addr, instance, type_str, p->wButtons, p->bLeftTrigger, p->bRightTrigger, p->sThumbLX, p->sThumbLY, p->sThumbRX, p->sThumbRY);

      // Scale Xbox analog values to [1-255] range (platform-agnostic)
      // XInput uses positive Y = UP, but internal format uses Y: 0=UP, 255=DOWN
      // So we invert Y-axis values to match HID convention
      uint8_t analog_1x = byteScaleAnalog(p->sThumbLX);
      uint8_t analog_1y = 256 - byteScaleAnalog(p->sThumbLY);  // Invert Y
      uint8_t analog_2x = byteScaleAnalog(p->sThumbRX);
      uint8_t analog_2y = 256 - byteScaleAnalog(p->sThumbRY);  // Invert Y
      uint8_t analog_l = p->bLeftTrigger;
      uint8_t analog_r = p->bRightTrigger;

      buttons = (((p->wButtons & XINPUT_GAMEPAD_DPAD_UP)       ? JP_BUTTON_DU : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)      ? JP_BUTTON_DD : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)      ? JP_BUTTON_DL : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)     ? JP_BUTTON_DR : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_A)              ? JP_BUTTON_B1 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_B)              ? JP_BUTTON_B2 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_X)              ? JP_BUTTON_B3 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_Y)              ? JP_BUTTON_B4 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER)  ? JP_BUTTON_L1 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? JP_BUTTON_R1 : 0) |
                // Note: No threshold-based L2/R2 - let output profiles handle analog-to-digital
                // Xbox triggers are purely analog; digital behavior is profile/output dependent
                ((p->wButtons & XINPUT_GAMEPAD_BACK)           ? JP_BUTTON_S1 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_START)          ? JP_BUTTON_S2 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_LEFT_THUMB)     ? JP_BUTTON_L3 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_RIGHT_THUMB)    ? JP_BUTTON_R3 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_GUIDE)          ? JP_BUTTON_A1 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_SHARE)          ? JP_BUTTON_A2 : 0));

      input_event_t event = {
        .dev_addr = dev_addr,
        .instance = instance,
        .type = INPUT_TYPE_GAMEPAD,
        .transport = INPUT_TRANSPORT_USB,
        .buttons = buttons,
        .button_count = 10,  // Xbox: A, B, X, Y, LB, RB, LT, RT, L3, R3
        .analog = {analog_1x, analog_1y, analog_2x, analog_2y, analog_l, analog_r},
        .keys = 0,
        .chatpad = {xid_itf->chatpad_data[0], xid_itf->chatpad_data[1], xid_itf->chatpad_data[2]},
        .has_chatpad = xid_itf->chatpad_enabled && xid_itf->chatpad_inited
      };
      router_submit_input(&event);
    }
  }
  tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, const xinputh_interface_t *xinput_itf)
{
  printf("XINPUT MOUNTED %02x %d type=%d\n", dev_addr, instance, xinput_itf->type);

  // Register Xbox One controllers for auth passthrough
  if (xinput_itf->type == XBOXONE)
  {
    printf("[xinput] Xbox One controller detected - registering for auth passthrough\n");
    xbone_auth_register(dev_addr, instance);
  }

  // If this is a Xbox 360 Wireless controller we need to wait for a connection packet
  // on the in pipe before setting LEDs etc. So just start getting data until a controller is connected.
  if (xinput_itf->type == XBOX360_WIRELESS && xinput_itf->connected == false)
  {
    tuh_xinput_receive_report(dev_addr, instance);
    return;
  }

  // Enable chatpad for Xbox 360 Wireless controllers
  if (xinput_itf->type == XBOX360_WIRELESS)
  {
    tuh_xinput_init_chatpad(dev_addr, instance, true);
  }

  // Reset keepalive timer for any controller with chatpad support
  // (wired chatpad is initialized separately in set_config)
  if (dev_addr < CFG_TUH_DEVICE_MAX && instance < CFG_TUH_XINPUT)
  {
    chatpad_last_keepalive[dev_addr][instance] = 0;
  }

  tuh_xinput_set_led(dev_addr, instance, 0, true);
  // tuh_xinput_set_rumble(dev_addr, instance, 0, 0, true);
  tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  printf("XINPUT UNMOUNTED %02x %d\n", dev_addr, instance);

  // Unregister from auth passthrough
  xbone_auth_unregister(dev_addr);
}

uint8_t byteScaleAnalog(int16_t xbox_val)
{
  // Scale the xbox value from [-32768, 32767] to [1, 255]
  // Offset by 32768 to get in range [0, 65536], then divide by 256 to get in range [1, 255]
  uint8_t scale_val = (xbox_val + 32768) / 256;
  if (scale_val == 0) return 1;
  return scale_val;
}

void xinput_task(void)
{
  // Process Xbox One auth passthrough
  xbone_auth_task();

  uint32_t now = to_ms_since_boot(get_absolute_time());

  // Chatpad keepalive for all xinput devices (runs even without player assignment)
  // TODO: chatpad keepalive disabled until chatpad support is working
  // for (uint8_t dev_addr = 1; dev_addr <= CFG_TUH_DEVICE_MAX; dev_addr++)
  // {
  //   for (uint8_t instance = 0; instance < CFG_TUH_XINPUT; instance++)
  //   {
  //     if (now - chatpad_last_keepalive[dev_addr][instance] >= XINPUT_CHATPAD_KEEPALIVE_MS)
  //     {
  //       if (tuh_xinput_chatpad_keepalive(dev_addr, instance))
  //       {
  //         chatpad_last_keepalive[dev_addr][instance] = now;
  //       }
  //     }
  //   }
  // }

  // Rumble/LED only if controller connected to a player
  if (!playersCount) return;

  // Throttle feedback to every 20ms (matches HID device task intervals)
  static uint32_t last_feedback_ms = 0;
  if (now - last_feedback_ms < 20) return;
  last_feedback_ms = now;

  // Track last-sent values per player to avoid redundant USB transfers
  static uint8_t last_led[4] = {0};
  static uint8_t last_rumble[4] = {0};

  // Update rumble/LED state for each xinput device
  for (int i = 0; i < playersCount; ++i)
  {
    if (players[i].dev_addr < 0) continue;  // Skip empty slots
    if (players[i].transport != INPUT_TRANSPORT_USB) continue;  // USB only

    uint8_t dev_addr = players[i].dev_addr;
    uint8_t instance = players[i].instance;

    // Get per-player feedback state
    feedback_state_t* fb = feedback_get_state(i);
    uint8_t rumble = fb ? (fb->rumble.left > fb->rumble.right ? fb->rumble.left : fb->rumble.right) : 0;

    // Derive LED quadrant from feedback pattern (set by host console)
    // Pattern bitmask: 0x01=P1, 0x02=P2, 0x04=P3, 0x08=P4
    // XInput quadrant: 1=P1, 2=P2, 3=P3, 4=P4, 0=off
    uint8_t led = i + 1;  // Default to player slot
    if (fb && fb->led.pattern) {
      if (fb->led.pattern & 0x01) led = 1;
      else if (fb->led.pattern & 0x02) led = 2;
      else if (fb->led.pattern & 0x04) led = 3;
      else if (fb->led.pattern & 0x08) led = 4;
    }

    // Only send if values changed (non-blocking)
    if (led != last_led[i]) {
      tuh_xinput_set_led(dev_addr, instance, led, false);
      last_led[i] = led;
    }
    if (rumble != last_rumble[i]) {
      tuh_xinput_set_rumble(dev_addr, instance, rumble, rumble, false);
      last_rumble[i] = rumble;
    }
  }
}

#endif
