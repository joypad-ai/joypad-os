// xinput.c - X-input protocol handler (TinyUSB X-input host callbacks)
#include "tusb.h"
#include "host/usbh_pvt.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/router/router.h"
#include "xinput_host.h"
#include "core/input_event.h"

// BTD driver for Bluetooth dongles
#if CFG_TUH_BTD
#include "usb/usbh/btd/btd.h"
#endif

uint32_t buttons;
int last_player_count = 0; // used by xboxone

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
        memcpy(&drivers[idx++], &usbh_btd_driver, sizeof(usbh_class_driver_t));
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
      TU_LOG1("[%02x, %02x], Type: %s, Buttons %04x, LT: %02x RT: %02x, LX: %d, LY: %d, RX: %d, RY: %d\n",
        dev_addr, instance, type_str, p->wButtons, p->bLeftTrigger, p->bRightTrigger, p->sThumbLX, p->sThumbLY, p->sThumbRX, p->sThumbRY);

      // Scale Xbox analog values to [1-255] range (platform-agnostic)
      uint8_t analog_1x = byteScaleAnalog(p->sThumbLX);
      uint8_t analog_1y = byteScaleAnalog(p->sThumbLY);
      uint8_t analog_2x = byteScaleAnalog(p->sThumbRX);
      uint8_t analog_2y = byteScaleAnalog(p->sThumbRY);
      uint8_t analog_l = p->bLeftTrigger;
      uint8_t analog_r = p->bRightTrigger;

      buttons = (((p->wButtons & XINPUT_GAMEPAD_DPAD_UP)       ? USBR_BUTTON_DU : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_DPAD_DOWN)      ? USBR_BUTTON_DD : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_DPAD_LEFT)      ? USBR_BUTTON_DL : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_DPAD_RIGHT)     ? USBR_BUTTON_DR : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_A)              ? USBR_BUTTON_B1 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_B)              ? USBR_BUTTON_B2 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_X)              ? USBR_BUTTON_B3 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_Y)              ? USBR_BUTTON_B4 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER)  ? USBR_BUTTON_L1 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? USBR_BUTTON_R1 : 0) |
                ((analog_l > 100)                              ? USBR_BUTTON_L2 : 0) |
                ((analog_r > 100)                              ? USBR_BUTTON_R2 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_BACK)           ? USBR_BUTTON_S1 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_START)          ? USBR_BUTTON_S2 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_LEFT_THUMB)     ? USBR_BUTTON_L3 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_RIGHT_THUMB)    ? USBR_BUTTON_R3 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_GUIDE)          ? USBR_BUTTON_A1 : 0) |
                ((p->wButtons & XINPUT_GAMEPAD_SHARE)          ? USBR_BUTTON_A2 : 0));

      input_event_t event = {
        .dev_addr = dev_addr,
        .instance = instance,
        .type = INPUT_TYPE_GAMEPAD,
        .buttons = buttons,
        .button_count = 10,  // Xbox: A, B, X, Y, LB, RB, LT, RT, L3, R3
        .analog = {analog_1x, analog_1y, analog_2x, analog_2y, 128, analog_l, analog_r, 128},
        .keys = 0
      };
      router_submit_input(&event);
    }
  }
  tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_mount_cb(uint8_t dev_addr, uint8_t instance, const xinputh_interface_t *xinput_itf)
{
  printf("XINPUT MOUNTED %02x %d\n", dev_addr, instance);
  // If this is a Xbox 360 Wireless controller we need to wait for a connection packet
  // on the in pipe before setting LEDs etc. So just start getting data until a controller is connected.
  if (xinput_itf->type == XBOX360_WIRELESS && xinput_itf->connected == false)
  {
    tuh_xinput_receive_report(dev_addr, instance);
    return;
  }
  // tuh_xinput_init_chatpad(dev_addr, instance, true);
  tuh_xinput_set_led(dev_addr, instance, 0, true);
  // tuh_xinput_set_rumble(dev_addr, instance, 0, 0, true);
  tuh_xinput_receive_report(dev_addr, instance);
}

void tuh_xinput_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  printf("XINPUT UNMOUNTED %02x %d\n", dev_addr, instance);
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
  // Rumble only if controller connected
  if (!playersCount) return;

  // Update rumble/LED state for each xinput device
  for (int i = 0; i < playersCount; ++i)
  {
    if (players[i].dev_addr < 0) continue;  // Skip empty slots

    // Get per-player feedback state
    feedback_state_t* fb = feedback_get_state(i);
    uint8_t rumble = fb ? (fb->rumble.left > fb->rumble.right ? fb->rumble.left : fb->rumble.right) : 0;

    // TODO: throttle and only fire if device is xinput
    tuh_xinput_set_led(players[i].dev_addr, players[i].instance, i+1, true);
    tuh_xinput_set_rumble(players[i].dev_addr, players[i].instance, rumble, rumble, true);
  }
}

#endif
