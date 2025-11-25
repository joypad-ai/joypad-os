// hid.c - HID protocol handler (TinyUSB HID host callbacks)
#include "tusb.h"
#include "core/globals.h"
#include "usb/usbh/hid/hid_utils.h"
#include "usb/usbh/hid/hid_registry.h"
#include "core/services/players/feedback.h"

// #define LANGUAGE_ID 0x0409
#define MAX_REPORTS 5

// Each HID instance can have multiple reports
typedef struct TU_ATTR_PACKED
{
  dev_type_t type;
  uint8_t report_count;
  tuh_hid_report_info_t report_info[MAX_REPORTS];
} instance_t;

// Cached device report properties on mount
typedef struct TU_ATTR_PACKED
{
  uint16_t vid, pid;
  instance_t instances[CFG_TUH_HID];
  // uint16_t serial[20];
} device_t;

static device_t devices[MAX_DEVICES] = { 0 };
int16_t spinner = 0;

static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

void hid_init()
{
  register_devices();
}

void hid_task(uint8_t rumble, uint8_t leds, uint8_t trigger_threshold, uint8_t test)
{
  uint32_t buttons;

  // iterate devices and instances that can receive responses
  for(uint8_t dev_addr=1; dev_addr<MAX_DEVICES; dev_addr++)
  {
    for(uint8_t instance=0; instance<CFG_TUH_HID; instance++)
    {
      int8_t player_index = find_player_index(dev_addr, instance);
      int8_t ctrl_type = devices[dev_addr].instances[instance].type;
      switch (ctrl_type)
      {
      case CONTROLLER_DUALSENSE: // send DS5 LED and rumble
      case CONTROLLER_DUALSHOCK3: // send DS3 Init, LED and rumble
      case CONTROLLER_DUALSHOCK4: // send DS4 LED and rumble
      case CONTROLLER_GAMECUBE: // send GameCube WiiU/Switch Adapter rumble
      case CONTROLLER_KEYBOARD: // send Keyboard LEDs
      case CONTROLLER_SWITCH: // send Switch Pro init, LED and rumble commands
        {
          // Override player_index during profile indication for all controllers
          int8_t display_player_index = feedback_get_display_player_index(player_index);

          // Build device output configuration
          device_output_config_t config = {
            .player_index = display_player_index,
            .rumble = rumble,
            .leds = leds,
            .trigger_threshold = trigger_threshold,
            .test = test
          };

          device_interfaces[ctrl_type]->task(dev_addr, instance, &config);
        }
        break;
      default:
        break;
      }
    }
  }
}

dev_type_t get_dev_type(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  printf("VID = %04x, PID = %04x\r\n", vid, pid);

  for (int i = 0; i < CONTROLLER_TYPE_COUNT-2; i++) {
    if (device_interfaces[i] &&
        device_interfaces[i]->is_device(vid, pid)) {
      printf("DEVICE:[%s]\n", device_interfaces[i]->name);
      return (dev_type_t)i;
    }
  }

  // Interface protocol (hid_interface_protocol_enum_t)
  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
  printf("HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);

  switch (itf_protocol)
  {
  case HID_ITF_PROTOCOL_KEYBOARD:
    printf("DEVICE:[KEYBOARD]\n");
    return CONTROLLER_KEYBOARD;
    break;
  case HID_ITF_PROTOCOL_MOUSE:
    printf("DEVICE:[MOUSE]\n");
    return CONTROLLER_MOUSE;
    break;
  default:
    break;
  }

  if (device_interfaces[CONTROLLER_DINPUT]->check_descriptor(dev_addr, instance, desc_report, desc_len))
  {
    printf("DEVICE:[%s]\n", device_interfaces[CONTROLLER_DINPUT]->name);
    return CONTROLLER_DINPUT;
  }

  printf("DEVICE:[UKNOWN]\n");
  return CONTROLLER_UNKNOWN;
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  printf("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);

  dev_type_t dev_type = get_dev_type(dev_addr, instance, desc_report, desc_len);
  devices[dev_addr].instances[instance].type = dev_type;

  // Set device type and defaults
  switch (dev_type)
  {
  case CONTROLLER_DUALSHOCK3:
  case CONTROLLER_SWITCH:
    device_interfaces[dev_type]->init(dev_addr, instance);
    break;
  default:
    break;
  }

  if (dev_type == CONTROLLER_UNKNOWN)
  {
    // Safe on ARM Cortex-M0+: suppress alignment warning for packed struct access
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Waddress-of-packed-member"
    tuh_hid_report_info_t* report_info_ptr = devices[dev_addr].instances[instance].report_info;
    devices[dev_addr].instances[instance].report_count =
      tuh_hid_parse_report_descriptor(report_info_ptr, MAX_REPORTS, desc_report, desc_len);
    #pragma GCC diagnostic pop
    printf("HID has %u reports \r\n", devices[dev_addr].instances[instance].report_count);
  }

  // gets serial for discovering some devices
  // uint16_t temp_buf[128];
  // if (0 == tuh_descriptor_get_serial_string_sync(dev_addr, LANGUAGE_ID, temp_buf, sizeof(temp_buf)))
  // {
  //   for(int i=0; i<20; i++){
  //     devices[dev_addr].serial[i] = temp_buf[i];
  //   }
  // }

  // request to receive report
  // tuh_hid_report_received_cb() will be invoked when report is available
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    printf("Error: cannot request to receive report\r\n");
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);

  // Reset device states
  dev_type_t dev_type = devices[dev_addr].instances[instance].type;
  switch (dev_type)
  {
  case CONTROLLER_DINPUT:
  case CONTROLLER_DUALSENSE:
  case CONTROLLER_DUALSHOCK3:
  case CONTROLLER_DUALSHOCK4:
  case CONTROLLER_KEYBOARD:
  case CONTROLLER_SWITCH:
    device_interfaces[dev_type]->unmount(dev_addr, instance);
  default:
    break;
  }

  devices[dev_addr].instances[instance].type = CONTROLLER_UNKNOWN;
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  dev_type_t dev_type = devices[dev_addr].instances[instance].type;
  if (dev_type == CONTROLLER_UNKNOWN)
  {
    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    switch (itf_protocol)
    {
      case HID_ITF_PROTOCOL_KEYBOARD:
        TU_LOG1("HID receive boot keyboard report\r\n");
        device_interfaces[CONTROLLER_KEYBOARD]->process(dev_addr, instance, report, len);
      break;

      case HID_ITF_PROTOCOL_MOUSE:
        TU_LOG1("HID receive boot mouse report\r\n");
        device_interfaces[CONTROLLER_MOUSE]->process(dev_addr, instance, report, len);
      break;

      default:
        TU_LOG1("HID receive generic report\r\n");
        process_generic_report(dev_addr, instance, report, len);
      break;
    }
  }
  else
  {
    // process known device interface reports
    device_interfaces[dev_type]->process(dev_addr, instance, report, len);
  }

  // continue to request to receive report
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    printf("Error: cannot request to receive report\r\n");
  }
}

//--------------------------------------------------------------------+
// Generic Report
//--------------------------------------------------------------------+

static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) dev_addr;

  uint8_t const rpt_count = devices[dev_addr].instances[instance].report_count;
  // Safe on ARM Cortex-M0+: suppress alignment warning for packed struct access
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Waddress-of-packed-member"
  tuh_hid_report_info_t* rpt_info_arr = devices[dev_addr].instances[instance].report_info;
  #pragma GCC diagnostic pop
  tuh_hid_report_info_t* rpt_info = NULL;

  if ( rpt_count == 1 && rpt_info_arr[0].report_id == 0)
  {
    // Simple report without report ID as 1st byte
    rpt_info = &rpt_info_arr[0];
  }
  else
  {
    // Composite report, 1st byte is report ID, data starts from 2nd byte
    uint8_t const rpt_id = report[0];

    // Find report id in the arrray
    for(uint8_t i=0; i<rpt_count; i++)
    {
      if (rpt_id == rpt_info_arr[i].report_id )
      {
        rpt_info = &rpt_info_arr[i];
        break;
      }
    }

    report++;
    len--;
  }

  if (!rpt_info)
  {
    printf("Couldn't find the report info for this report !\r\n");
    return;
  }

  // For complete list of Usage Page & Usage checkout src/class/hid/hid.h. For examples:
  // - Keyboard                     : Desktop, Keyboard
  // - Mouse                        : Desktop, Mouse
  // - Gamepad                      : Desktop, Gamepad
  // - Consumer Control (Media Key) : Consumer, Consumer Control
  // - System Control (Power key)   : Desktop, System Control
  // - Generic (vendor)             : 0xFFxx, xx
  if ( rpt_info->usage_page == HID_USAGE_PAGE_DESKTOP )
  {
    switch (rpt_info->usage)
    {
      case HID_USAGE_DESKTOP_KEYBOARD:
        TU_LOG1("HID receive keyboard report\r\n");
        // Assume keyboard follow boot report layout
        device_interfaces[CONTROLLER_KEYBOARD]->process(dev_addr, instance, report, len);
      break;

      case HID_USAGE_DESKTOP_MOUSE:
        TU_LOG1("HID receive mouse report\r\n");
        // Assume mouse follow boot report layout
        device_interfaces[CONTROLLER_MOUSE]->process(dev_addr, instance, report, len);
      break;

      default: break;
    }
  }
}
