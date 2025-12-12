// hid_gamepad.c
#include "hid_gamepad.h"
#include "hid_parser.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/router/router.h"
#include "core/input_event.h"

typedef struct
{
  uint8_t byteIndex;
  uint16_t bitMask;
  uint32_t max;
} dinput_usage_t;

// Generic HID instance state
typedef struct TU_ATTR_PACKED
{
  dinput_usage_t xLoc;
  dinput_usage_t yLoc;
  dinput_usage_t zLoc;
  dinput_usage_t rzLoc;
  dinput_usage_t rxLoc;
  dinput_usage_t ryLoc;
  dinput_usage_t hatLoc;
  dinput_usage_t buttonLoc[MAX_BUTTONS]; // assuming a maximum of 12 buttons
  uint8_t buttonCnt;
  uint8_t type;
} dinput_instance_t;

// Cached device report properties on mount
typedef struct TU_ATTR_PACKED
{
  dinput_instance_t instances[CFG_TUH_HID];
} dinput_device_t;

static dinput_device_t hid_devices[MAX_DEVICES] = { 0 };

// hid_parser info
HID_ReportInfo_t *info;

//(hat format, 8 is released, 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW)
static const uint8_t HAT_SWITCH_TO_DIRECTION_BUTTONS[] = {0b0001, 0b0011, 0b0010, 0b0110, 0b0100, 0b1100, 0b1000, 0b1001, 0b0000};

// Gets HID descriptor report item for specific ReportID
static inline bool USB_GetHIDReportItemInfoWithReportId(const uint8_t *ReportData, HID_ReportItem_t *const ReportItem)
{
  if (HID_DEBUG) TU_LOG1("ReportID: %d ", ReportItem->ReportID);
  if (ReportItem->ReportID)
  {
    // if (ReportItem->ReportID != ReportData[0])
    //   return false;

    ReportData++;
  }
  return USB_GetHIDReportItemInfo(ReportItem->ReportID, ReportData, ReportItem);
}

// Parses HID descriptor into byteIndex/buttonMasks
void parse_descriptor(uint8_t dev_addr, uint8_t instance)
{
  HID_ReportItem_t *item = info->FirstReportItem;
  //iterate filtered reports info to match report from data
  uint8_t btns_count = 0;
  uint8_t idOffset = 0;

  // check if reportID exists within input report
  if (item->ReportID)
  {
    TU_LOG1("ReportID in report = %04x\r\n", item->ReportID);
    idOffset = 8;
  }

  while (item)
  {
    uint8_t midValue = (item->Attributes.Logical.Maximum - item->Attributes.Logical.Minimum) / 2;
    uint8_t bitSize = item->Attributes.BitSize ? item->Attributes.BitSize : 0; // bits per usage
    uint8_t bitOffset = (item->BitOffset ? item->BitOffset : 0) + idOffset; // bits offset from start
    uint16_t bitMask = ((0xFFFF >> (16 - bitSize)) << bitOffset % 8); // usage bits byte mask
    uint8_t byteIndex = (int)(bitOffset / 8); // usage start byte

    if (HID_DEBUG) {
      TU_LOG1("minimum: %d ", item->Attributes.Logical.Minimum);
      TU_LOG1("mid: %d ", midValue);
      TU_LOG1("maximum: %d ", item->Attributes.Logical.Maximum);
      TU_LOG1("bitSize: %d ", bitSize);
      TU_LOG1("bitOffset: %d ", bitOffset);
      TU_LOG1("bitMask: 0x%x ", bitMask);
      TU_LOG1("byteIndex: %d ", byteIndex);
    }
    // TODO: this is limiting to repordId 0..
    // Need to parse reportId and match later with received reports.
    // Also helpful if multiple reportId maps can be saved per instance and report as individual
    // players for single instance HID reports that contain multiple reportIds.
    //
    uint8_t report[1] = {0}; // reportId = 0; original ex maps report to descriptor data structure
    if (USB_GetHIDReportItemInfoWithReportId(report, item))
    {
      if (HID_DEBUG) TU_LOG1("PAGE: %d ", item->Attributes.Usage.Page);
      hid_devices[dev_addr].instances[instance].type = HID_GAMEPAD;

      switch (item->Attributes.Usage.Page)
      {
        case HID_USAGE_PAGE_DESKTOP:
        {
          switch (item->Attributes.Usage.Usage)
          {
          case HID_USAGE_DESKTOP_WHEEL:
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_WHEEL ");
            hid_devices[dev_addr].instances[instance].type = HID_MOUSE;
            break;
          }
          case HID_USAGE_DESKTOP_MOUSE:
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_MOUSE ");
            hid_devices[dev_addr].instances[instance].type = HID_MOUSE;
            break;
          }
          case HID_USAGE_DESKTOP_KEYBOARD:
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_KEYBOARD ");
            hid_devices[dev_addr].instances[instance].type = HID_KEYBOARD;
            break;
          }
          case HID_USAGE_DESKTOP_X: // Left Analog X
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_X ");
            hid_devices[dev_addr].instances[instance].xLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].xLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].xLoc.max = item->Attributes.Logical.Maximum;
            break;
          }
          case HID_USAGE_DESKTOP_Y: // Left Analog Y
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_Y ");
            hid_devices[dev_addr].instances[instance].yLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].yLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].yLoc.max = item->Attributes.Logical.Maximum;
            break;
          }
          case HID_USAGE_DESKTOP_Z: // Right Analog X
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_Z ");
            hid_devices[dev_addr].instances[instance].zLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].zLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].zLoc.max = item->Attributes.Logical.Maximum;
            break;
          }
          case HID_USAGE_DESKTOP_RZ: // Right Analog Y
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_RZ ");
            hid_devices[dev_addr].instances[instance].rzLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].rzLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].rzLoc.max = item->Attributes.Logical.Maximum;
            break;
          }
          case HID_USAGE_DESKTOP_RX: // Left Analog Trigger
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_RX ");
            hid_devices[dev_addr].instances[instance].rxLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].rxLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].rxLoc.max = item->Attributes.Logical.Maximum;
            break;
          }
          case HID_USAGE_DESKTOP_RY: // Right Analog Trigger
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_RY ");
            hid_devices[dev_addr].instances[instance].ryLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].ryLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].ryLoc.max = item->Attributes.Logical.Maximum;
            break;
          }
          case HID_USAGE_DESKTOP_HAT_SWITCH:
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_HAT_SWITCH ");
            hid_devices[dev_addr].instances[instance].hatLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].hatLoc.bitMask = bitMask;
            break;
          }
          default:
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_NOT_HANDLED 0x%x", item->Attributes.Usage.Usage);
            break;
          // case HID_USAGE_DESKTOP_SLIDER:
          // case HID_USAGE_DESKTOP_DIAL:
          //   break;
          // case HID_USAGE_DESKTOP_DPAD_UP:
          //   current.up |= 1;
          //   break;
          // case HID_USAGE_DESKTOP_DPAD_RIGHT:
          //   current.right |= 1;
          //   break;
          // case HID_USAGE_DESKTOP_DPAD_DOWN:
          //   current.down |= 1;
          //   break;
          // case HID_USAGE_DESKTOP_DPAD_LEFT:
          //   current.left |= 1;
          //   break;
          }
          break;
        }
        case HID_USAGE_PAGE_BUTTON:
        {
          if (HID_DEBUG) TU_LOG1(" HID_USAGE_PAGE_BUTTON ");
          uint8_t usage = item->Attributes.Usage.Usage;

          if (usage >= 1 && usage <= MAX_BUTTONS) {
            hid_devices[dev_addr].instances[instance].buttonLoc[usage - 1].byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].buttonLoc[usage - 1].bitMask = bitMask;
          }
          btns_count++;
          break;
        }
        default:
          if (HID_DEBUG) TU_LOG1(" HID_USAGE_PAGE_NOT_HANDLED 0x%x", item->Attributes.Usage.Page);
          break;
      }
    }
    item = item->Next;
    if (HID_DEBUG) TU_LOG1("\n\n");
  }

  hid_devices[dev_addr].instances[instance].buttonCnt = btns_count;
  // hid_devices[dev_addr].instances[instance].reportID = item->ReportID;
}

//
bool is_hid_gamepad(uint16_t vid, uint16_t pid)
{
  return false;
}

// hid_parser
bool parse_hid_gamepad(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  uint8_t ret = USB_ProcessHIDReport(dev_addr, instance, desc_report, desc_len, &(info));
  if(ret == HID_PARSE_Successful)
  {
    parse_descriptor(dev_addr, instance);
  }
  else
  {
    TU_LOG1("Error: USB_ProcessHIDReport failed: %d\r\n", ret);
  }

  // free up memory for next report to be parsed
  USB_FreeReportInfo(info);
  info = NULL;

  // assume it is d-input device if buttons exist on report
  if (hid_devices[dev_addr].instances[instance].buttonCnt > 0 &&
     hid_devices[dev_addr].instances[instance].type == HID_GAMEPAD
  ) {
    return true;  
  }

  return false;
}

// check if 2 reports are different enough
// bool diff_report_dinput(dinput_report_t const* rpt1, dinput_report_t const* rpt2)
// {
// }

// called from parser for filtering report items
bool CALLBACK_HIDParser_FilterHIDReportItem(uint8_t dev_addr, uint8_t instance, HID_ReportItem_t *const CurrentItem)
{
  if (CurrentItem->ItemType != HID_REPORT_ITEM_In)
    return false;

  // if (hid_devices[dev_addr].instances[instance].reportID == INVALID_REPORT_ID)
  // {
  //   hid_devices[dev_addr].instances[instance].reportID = CurrentItem->ReportID;
  // }

  TU_LOG1("ITEM_PAGE: 0x%x", CurrentItem->Attributes.Usage.Page);
  TU_LOG1(" USAGE: 0x%x\n", CurrentItem->Attributes.Usage.Usage);
  switch (CurrentItem->Attributes.Usage.Page)
  {
    case HID_USAGE_PAGE_DESKTOP:
      switch (CurrentItem->Attributes.Usage.Usage)
      {
        case HID_USAGE_DESKTOP_X:
        case HID_USAGE_DESKTOP_Y:
        case HID_USAGE_DESKTOP_Z:
        case HID_USAGE_DESKTOP_RZ:
        case HID_USAGE_DESKTOP_RX:
        case HID_USAGE_DESKTOP_RY:
        case HID_USAGE_DESKTOP_HAT_SWITCH:
        case HID_USAGE_DESKTOP_DPAD_UP:
        case HID_USAGE_DESKTOP_DPAD_DOWN:
        case HID_USAGE_DESKTOP_DPAD_LEFT:
        case HID_USAGE_DESKTOP_DPAD_RIGHT:
        case HID_USAGE_DESKTOP_WHEEL:
        case HID_USAGE_DESKTOP_MOUSE:
        case HID_USAGE_DESKTOP_KEYBOARD:
          return true;
      }
      return false;
    case HID_USAGE_PAGE_BUTTON:
      return true;
  }
  return false;
}

// scales down switch analog value to a single byte
uint8_t scale_analog_hid_gamepad(uint16_t value, uint32_t max_value)
{
  int mid_point = max_value / 2;
  int scaled_value;

  if (value <= mid_point) {
    // Scale between [0, mid_point] to [1, 128]
    scaled_value = 1 + (value * 127) / mid_point;
  } else {
    // Scale between [mid_point, max_value] to [128, 255]
    scaled_value = 128 + ((value - mid_point) * 127) / (max_value - mid_point);
  }

  return scaled_value;
}

// process generic usb hid input reports (from parsed HID descriptor byteIndexes & bitMasks)
void process_hid_gamepad(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  uint32_t buttons = 0;
  static dinput_gamepad_t previous[5][5];
  dinput_gamepad_t current = {0};
  current.value = 0;

  uint16_t xValue, yValue, zValue, rzValue, rxValue, ryValue;

  if (hid_devices[dev_addr].instances[instance].xLoc.bitMask > 0xFF) { // if bitmask is larger than 8 bits
    // Combine the current byte and the next byte into a 16-bit value
    uint16_t combinedBytes = ((uint16_t)report[hid_devices[dev_addr].instances[instance].xLoc.byteIndex] << 8) | report[hid_devices[dev_addr].instances[instance].xLoc.byteIndex + 1];
    // Apply the bitmask to the combined 16-bit value
    xValue = (combinedBytes & hid_devices[dev_addr].instances[instance].xLoc.bitMask) >> (__builtin_ctz(hid_devices[dev_addr].instances[instance].xLoc.bitMask));
  } else if (hid_devices[dev_addr].instances[instance].xLoc.bitMask) {
    // Apply the bitmask to the single byte value (your existing implementation)
    xValue = report[hid_devices[dev_addr].instances[instance].xLoc.byteIndex] & hid_devices[dev_addr].instances[instance].xLoc.bitMask;
  }

  if (hid_devices[dev_addr].instances[instance].yLoc.bitMask > 0xFF) { // if bitmask is larger than 8 bits
    // Combine the current byte and the next byte into a 16-bit value
    uint16_t combinedBytes = ((uint16_t)report[hid_devices[dev_addr].instances[instance].yLoc.byteIndex] << 8) | report[hid_devices[dev_addr].instances[instance].yLoc.byteIndex + 1];
    // Apply the bitmask to the combined 16-bit value
    yValue = (combinedBytes & hid_devices[dev_addr].instances[instance].yLoc.bitMask) >> (__builtin_ctz(hid_devices[dev_addr].instances[instance].yLoc.bitMask));
  } else if (hid_devices[dev_addr].instances[instance].yLoc.bitMask) {
    // Apply the bitmask to the single byte value (your existing implementation)
    yValue = report[hid_devices[dev_addr].instances[instance].yLoc.byteIndex] & hid_devices[dev_addr].instances[instance].yLoc.bitMask;
  }

  if (hid_devices[dev_addr].instances[instance].zLoc.bitMask > 0xFF) { // if bitmask is larger than 8 bits
    // Combine the current byte and the next byte into a 16-bit value
    uint16_t combinedBytes = ((uint16_t)report[hid_devices[dev_addr].instances[instance].zLoc.byteIndex] << 8) | report[hid_devices[dev_addr].instances[instance].zLoc.byteIndex + 1];
    // Apply the bitmask to the combined 16-bit value
    zValue = (combinedBytes & hid_devices[dev_addr].instances[instance].zLoc.bitMask) >> (__builtin_ctz(hid_devices[dev_addr].instances[instance].zLoc.bitMask));
  } else if (hid_devices[dev_addr].instances[instance].zLoc.bitMask) {
    // Apply the bitmask to the single byte value (your existing implementation)
    zValue = report[hid_devices[dev_addr].instances[instance].zLoc.byteIndex] & hid_devices[dev_addr].instances[instance].zLoc.bitMask;
  }

  if (hid_devices[dev_addr].instances[instance].rzLoc.bitMask > 0xFF) { // if bitmask is larger than 8 bits
    // Combine the current byte and the next byte into a 16-bit value
    uint16_t combinedBytes = ((uint16_t)report[hid_devices[dev_addr].instances[instance].rzLoc.byteIndex] << 8) | report[hid_devices[dev_addr].instances[instance].rzLoc.byteIndex + 1];
    // Apply the bitmask to the combined 16-bit value
    rzValue = (combinedBytes & hid_devices[dev_addr].instances[instance].rzLoc.bitMask) >> (__builtin_ctz(hid_devices[dev_addr].instances[instance].rzLoc.bitMask));
  } else if (hid_devices[dev_addr].instances[instance].rzLoc.bitMask) {
    // Apply the bitmask to the single byte value (your existing implementation)
    rzValue = report[hid_devices[dev_addr].instances[instance].rzLoc.byteIndex] & hid_devices[dev_addr].instances[instance].rzLoc.bitMask;
  }

  if (hid_devices[dev_addr].instances[instance].rxLoc.bitMask > 0xFF) { // if bitmask is larger than 8 bits
    // Combine the current byte and the next byte into a 16-bit value
    uint16_t combinedBytes = ((uint16_t)report[hid_devices[dev_addr].instances[instance].rxLoc.byteIndex] << 8) | report[hid_devices[dev_addr].instances[instance].rxLoc.byteIndex + 1];
    // Apply the bitmask to the combined 16-bit value
    rxValue = (combinedBytes & hid_devices[dev_addr].instances[instance].rxLoc.bitMask) >> (__builtin_ctz(hid_devices[dev_addr].instances[instance].rxLoc.bitMask));
  } else if (hid_devices[dev_addr].instances[instance].rxLoc.bitMask) {
    // Apply the bitmask to the single byte value (your existing implementation)
    rxValue = report[hid_devices[dev_addr].instances[instance].rxLoc.byteIndex] & hid_devices[dev_addr].instances[instance].rxLoc.bitMask;
  }

  if (hid_devices[dev_addr].instances[instance].ryLoc.bitMask > 0xFF) { // if bitmask is larger than 8 bits
    // Combine the current byte and the next byte into a 16-bit value
    uint16_t combinedBytes = ((uint16_t)report[hid_devices[dev_addr].instances[instance].ryLoc.byteIndex] << 8) | report[hid_devices[dev_addr].instances[instance].ryLoc.byteIndex + 1];
    // Apply the bitmask to the combined 16-bit value
    ryValue = (combinedBytes & hid_devices[dev_addr].instances[instance].ryLoc.bitMask) >> (__builtin_ctz(hid_devices[dev_addr].instances[instance].ryLoc.bitMask));
  } else if (hid_devices[dev_addr].instances[instance].ryLoc.bitMask) {
    // Apply the bitmask to the single byte value (your existing implementation)
    ryValue = report[hid_devices[dev_addr].instances[instance].ryLoc.byteIndex] & hid_devices[dev_addr].instances[instance].ryLoc.bitMask;
  }

  uint8_t hatValue = report[hid_devices[dev_addr].instances[instance].hatLoc.byteIndex] & hid_devices[dev_addr].instances[instance].hatLoc.bitMask;

  // parse hat from report
  if (hid_devices[dev_addr].instances[instance].hatLoc.bitMask) {
    uint8_t direction = hatValue <= 8 ? hatValue : 8; // fix for hats with pressed state greater than 8
    current.all_direction |= HAT_SWITCH_TO_DIRECTION_BUTTONS[direction];
  } else {
    hatValue = 8;
  }

  // parse buttons from report
  current.all_buttons = 0;
  for (int i = 0; i < 12; i++) {
    if (report[hid_devices[dev_addr].instances[instance].buttonLoc[i].byteIndex] & hid_devices[dev_addr].instances[instance].buttonLoc[i].bitMask) {
      current.all_buttons |= (0x01 << i);
    }
  }

  // parse analog from report
  if (hid_devices[dev_addr].instances[instance].xLoc.max) {
    current.x = scale_analog_hid_gamepad(xValue, hid_devices[dev_addr].instances[instance].xLoc.max);
  } else {
    current.x = 128;
  }
  if (hid_devices[dev_addr].instances[instance].yLoc.max) {
    current.y = scale_analog_hid_gamepad(yValue, hid_devices[dev_addr].instances[instance].yLoc.max);
  } else {
    current.y = 128;
  }
  if (hid_devices[dev_addr].instances[instance].zLoc.max) {
    current.z = scale_analog_hid_gamepad(zValue, hid_devices[dev_addr].instances[instance].zLoc.max);
  } else {
    current.z = 128;
  }
  if (hid_devices[dev_addr].instances[instance].rzLoc.max) {
    current.rz = scale_analog_hid_gamepad(rzValue, hid_devices[dev_addr].instances[instance].rzLoc.max);
  } else {
    current.rz = 128;
  }
  if (hid_devices[dev_addr].instances[instance].rxLoc.max) {
    current.rx = scale_analog_hid_gamepad(rxValue, hid_devices[dev_addr].instances[instance].rxLoc.max);
  } else {
    current.rx = 0;
  }
  if (hid_devices[dev_addr].instances[instance].ryLoc.max) {
    current.ry = scale_analog_hid_gamepad(ryValue, hid_devices[dev_addr].instances[instance].ryLoc.max);
  } else {
    current.ry = 0;
  }

  // TODO: based on diff report rather than current's datastructure in order to get subtle analog changes
  if (previous[dev_addr-1][instance].value != current.value)
  {
    previous[dev_addr-1][instance] = current;

    if (HID_DEBUG) {
      TU_LOG1("Super HID Report: ");
      TU_LOG1("Button Count: %d\n", hid_devices[dev_addr].instances[instance].buttonCnt);
      TU_LOG1(" x:%d, y:%d, z:%d, rz:%d dPad:%d \n", current.x, current.y, current.z, current.rz, hatValue);
      for (int i = 0; i < 12; i++) {
        TU_LOG1(" B%d:%d", i + 1, (report[hid_devices[dev_addr].instances[instance].buttonLoc[i].byteIndex] & hid_devices[dev_addr].instances[instance].buttonLoc[i].bitMask) ? 1 : 0 );
      }
      TU_LOG1("\n");
    }

    uint8_t buttonCount = hid_devices[dev_addr].instances[instance].buttonCnt;
    if (buttonCount > 12) buttonCount = 12;
    bool buttonSelect = current.all_buttons & (0x01 << (buttonCount-2));
    bool buttonStart = current.all_buttons & (0x01 << (buttonCount-1));
    bool buttonI = current.button1;
    bool buttonIII = current.button3;
    bool buttonIV = current.button4;
    bool buttonV = buttonCount >=7 ? current.button5 : 0;
    bool buttonVI = buttonCount >=8 ? current.button6 : 0;
    bool buttonVII = buttonCount >=9 ? current.button7 : 0;
    bool buttonVIII = buttonCount >=10 ? current.button8 : 0;
    bool buttonIX = buttonCount >=11 ? current.button9 : 0;
    bool buttonX = buttonCount >=12 ? current.button10 : 0;

    // assume DirectInput mapping
    if (buttonCount >= 10) {
      buttonSelect = current.button9;
      buttonStart = current.button10;
      buttonI = current.button3;
      buttonIII = current.button4;
      buttonIV = current.button1;
    }

    // Active-high: set bit when button is pressed
    buttons = ((current.up)       ? USBR_BUTTON_DU : 0) |
              ((current.down)     ? USBR_BUTTON_DD : 0) |
              ((current.left)     ? USBR_BUTTON_DL : 0) |
              ((current.right)    ? USBR_BUTTON_DR : 0) |
              ((current.button2)  ? USBR_BUTTON_B1 : 0) |
              ((buttonI)          ? USBR_BUTTON_B2 : 0) |
              ((buttonIV)         ? USBR_BUTTON_B3 : 0) |
              ((buttonIII)        ? USBR_BUTTON_B4 : 0) |
              ((buttonV)          ? USBR_BUTTON_L1 : 0) |
              ((buttonVI)         ? USBR_BUTTON_R1 : 0) |
              ((buttonVII)        ? USBR_BUTTON_L2 : 0) |
              ((buttonVIII)       ? USBR_BUTTON_R2 : 0) |
              ((buttonSelect)     ? USBR_BUTTON_S1 : 0) |
              ((buttonStart)      ? USBR_BUTTON_S2 : 0) |
              ((current.button11) ? USBR_BUTTON_L3 : 0) |
              ((current.button12) ? USBR_BUTTON_R3 : 0);

    // HID convention: 0=up, 255=down (no inversion needed)
    uint8_t axis_x = current.x;
    uint8_t axis_y = current.y;
    uint8_t axis_z = current.z;
    uint8_t axis_rz = current.rz;

    // keep analog within range [1-255]
    ensureAllNonZero(&axis_x, &axis_y, &axis_z, &axis_rz);

    input_event_t event = {
      .dev_addr = dev_addr,
      .instance = instance,
      .type = INPUT_TYPE_GAMEPAD,
      .buttons = buttons,
      .button_count = buttonCount,
      .analog = {axis_x, axis_y, axis_z, axis_rz, 128, current.rx, current.ry, 128},
      .keys = 0,
    };
    router_submit_input(&event);
  }
}

// resets default values in case devices are hotswapped
void unmount_hid_gamepad(uint8_t dev_addr, uint8_t instance)
{
  TU_LOG1("DINPUT[%d|%d]: Unmount Reset\r\n", dev_addr, instance);
  hid_devices[dev_addr].instances[instance].xLoc.byteIndex = 0;
  hid_devices[dev_addr].instances[instance].xLoc.bitMask = 0;
  hid_devices[dev_addr].instances[instance].xLoc.max = 0;
  hid_devices[dev_addr].instances[instance].yLoc.byteIndex = 0;
  hid_devices[dev_addr].instances[instance].yLoc.bitMask = 0;
  hid_devices[dev_addr].instances[instance].yLoc.max = 0;
  hid_devices[dev_addr].instances[instance].zLoc.byteIndex = 0;
  hid_devices[dev_addr].instances[instance].zLoc.bitMask = 0;
  hid_devices[dev_addr].instances[instance].zLoc.max = 0;
  hid_devices[dev_addr].instances[instance].rzLoc.byteIndex = 0;
  hid_devices[dev_addr].instances[instance].rzLoc.bitMask = 0;
  hid_devices[dev_addr].instances[instance].rzLoc.max = 0;
  hid_devices[dev_addr].instances[instance].rxLoc.byteIndex = 0;
  hid_devices[dev_addr].instances[instance].rxLoc.bitMask = 0;
  hid_devices[dev_addr].instances[instance].rxLoc.max = 0;
  hid_devices[dev_addr].instances[instance].ryLoc.byteIndex = 0;
  hid_devices[dev_addr].instances[instance].ryLoc.bitMask = 0;
  hid_devices[dev_addr].instances[instance].ryLoc.max = 0;
  hid_devices[dev_addr].instances[instance].hatLoc.byteIndex = 0;
  hid_devices[dev_addr].instances[instance].hatLoc.bitMask = 0;
  hid_devices[dev_addr].instances[instance].buttonCnt = 0;
  for (int i = 0; i < MAX_BUTTONS; i++) {
    hid_devices[dev_addr].instances[instance].buttonLoc[i].byteIndex = 0;
    hid_devices[dev_addr].instances[instance].buttonLoc[i].bitMask = 0;
  }
}

DeviceInterface hid_gamepad_interface = {
  .name = "DirectInput",
  .is_device = is_hid_gamepad,
  .check_descriptor = parse_hid_gamepad,
  .process = process_hid_gamepad,
  .unmount = unmount_hid_gamepad,
  .init = NULL,
};
