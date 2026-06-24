// hid_gamepad.c
#include "hid_gamepad.h"
#include "hid_parser.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include <string.h>

typedef struct
{
  uint8_t  byteIndex;
  uint16_t bitMask;
  uint16_t max;   // logical maximum (16-bit is plenty for gamepad axes)
  int16_t  min;   // logical minimum — <0 marks a signed/centered axis
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
  bool xbox_axes;  // Xbox HID convention: Rx/Ry=right stick, Z=triggers
  const uint32_t* button_map;  // per-device HID-index -> JP_BUTTON override (NULL = generic remap)
} dinput_instance_t;

// Direct HID-button-index -> JP_BUTTON map for devices whose button order the
// generic DInput reshuffle gets wrong. Index i = HID button (i+1).
// ELO Vagabond V1 (VID 0483/PID A4DB): A,B,R4,X,Y,L4,L1,R1,L2,R2,Select,Start.
static const uint32_t ELO_VAGABOND_BUTTON_MAP[MAX_BUTTONS] = {
  JP_BUTTON_B1,  //  1  A
  JP_BUTTON_B2,  //  2  B
  JP_BUTTON_R4,  //  3  R4 (back paddle)
  JP_BUTTON_B3,  //  4  X
  JP_BUTTON_B4,  //  5  Y
  JP_BUTTON_L4,  //  6  L4 (back paddle)
  JP_BUTTON_L1,  //  7  L1
  JP_BUTTON_R1,  //  8  R1
  JP_BUTTON_L2,  //  9  L2 (digital; analog also arrives via Sim-Controls Brake)
  JP_BUTTON_R2,  // 10  R2 (digital; analog also arrives via Sim-Controls Accelerator)
  JP_BUTTON_S1,  // 11  Select (three dots)
  JP_BUTTON_S2,  // 12  Start (three lines)
};

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
            hid_devices[dev_addr].instances[instance].xLoc.min = item->Attributes.Logical.Minimum;
            break;
          }
          case HID_USAGE_DESKTOP_Y: // Left Analog Y
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_Y ");
            hid_devices[dev_addr].instances[instance].yLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].yLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].yLoc.max = item->Attributes.Logical.Maximum;
            hid_devices[dev_addr].instances[instance].yLoc.min = item->Attributes.Logical.Minimum;
            break;
          }
          case HID_USAGE_DESKTOP_Z: // Right Analog X
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_Z ");
            hid_devices[dev_addr].instances[instance].zLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].zLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].zLoc.max = item->Attributes.Logical.Maximum;
            hid_devices[dev_addr].instances[instance].zLoc.min = item->Attributes.Logical.Minimum;
            break;
          }
          case HID_USAGE_DESKTOP_RZ: // Right Analog Y
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_RZ ");
            hid_devices[dev_addr].instances[instance].rzLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].rzLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].rzLoc.max = item->Attributes.Logical.Maximum;
            hid_devices[dev_addr].instances[instance].rzLoc.min = item->Attributes.Logical.Minimum;
            break;
          }
          case HID_USAGE_DESKTOP_RX: // Left Analog Trigger
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_RX ");
            hid_devices[dev_addr].instances[instance].rxLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].rxLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].rxLoc.max = item->Attributes.Logical.Maximum;
            hid_devices[dev_addr].instances[instance].rxLoc.min = item->Attributes.Logical.Minimum;
            break;
          }
          case HID_USAGE_DESKTOP_RY: // Right Analog Trigger
          {
            if (HID_DEBUG) TU_LOG1(" HID_USAGE_DESKTOP_RY ");
            hid_devices[dev_addr].instances[instance].ryLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].ryLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].ryLoc.max = item->Attributes.Logical.Maximum;
            hid_devices[dev_addr].instances[instance].ryLoc.min = item->Attributes.Logical.Minimum;
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
        case HID_USAGE_PAGE_SIMULATE:
        {
          // Xbox-style analog triggers live on Simulation Controls: Brake (0xC5)
          // and Accelerator (0xC4). Map them onto the trigger locs (rx=L2, ry=R2)
          // — same slots the Generic Desktop RX/RY path uses.
          switch (item->Attributes.Usage.Usage)
          {
          case 0xC5: // Brake -> Left trigger (L2)
            hid_devices[dev_addr].instances[instance].rxLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].rxLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].rxLoc.max = item->Attributes.Logical.Maximum;
            hid_devices[dev_addr].instances[instance].rxLoc.min = item->Attributes.Logical.Minimum;
            break;
          case 0xC4: // Accelerator -> Right trigger (R2)
            hid_devices[dev_addr].instances[instance].ryLoc.byteIndex = byteIndex;
            hid_devices[dev_addr].instances[instance].ryLoc.bitMask = bitMask;
            hid_devices[dev_addr].instances[instance].ryLoc.max = item->Attributes.Logical.Maximum;
            hid_devices[dev_addr].instances[instance].ryLoc.min = item->Attributes.Logical.Minimum;
            break;
          default: break;
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

  // Detect Xbox HID axis convention: Rx/Ry present (right stick) but no Rz
  // DInput: Z/Rz = right stick, Rx/Ry = triggers
  // Xbox HID: Rx/Ry = right stick, Z = triggers
  dinput_instance_t *inst = &hid_devices[dev_addr].instances[instance];
  if (inst->rxLoc.max && inst->ryLoc.max && !inst->rzLoc.max) {
    inst->xbox_axes = true;
    TU_LOG1("HID Gamepad: Xbox axis convention detected (Rx/Ry=sticks, Z=triggers)\n");
    // Remap so output mapping stays consistent: z/rz = right stick, rx/ry = triggers
    dinput_usage_t old_z = inst->zLoc;
    inst->zLoc = inst->rxLoc;     // Rx → right stick X
    inst->rzLoc = inst->ryLoc;    // Ry → right stick Y
    inst->rxLoc = old_z;          // Z → left trigger
    inst->ryLoc = (dinput_usage_t){0};  // No separate right trigger
  }
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

    // ELO Vagabond V1's button order doesn't match the generic DInput reshuffle,
    // so use a direct button map. VID 0483/PID A4DB is ELO-specific (0483 alone
    // is STMicro's shared VID, so match BOTH).
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    if (vid == 0x0483 && pid == 0xA4DB) {
      hid_devices[dev_addr].instances[instance].button_map = ELO_VAGABOND_BUTTON_MAP;
    }
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

// Read a 16-bit or 8-bit axis value from HID report (little-endian)
static uint16_t read_axis_value(const uint8_t *report, const dinput_usage_t *loc)
{
  if (loc->bitMask > 0xFF) {
    // 16-bit: USB HID is little-endian (low byte first)
    uint16_t combined = (uint16_t)report[loc->byteIndex] | ((uint16_t)report[loc->byteIndex + 1] << 8);
    return (combined & loc->bitMask) >> __builtin_ctz(loc->bitMask);
  } else if (loc->bitMask) {
    return report[loc->byteIndex] & loc->bitMask;
  }
  return 0;
}

// Scale a parsed axis to 0-255. Most HID pads use unsigned axes (logical 0..max);
// some (e.g. ELO Vagabond) declare SIGNED axes centered at 0 (logicalMin < 0),
// where the unsigned path would read center as ~1. For signed axes, sign-extend
// the field and map [min,max] -> [1,255] so 0 lands at 128.
static uint8_t scale_axis(const dinput_usage_t *loc, uint16_t raw)
{
  if (loc->min < 0) {
    int32_t sval = (loc->bitMask > 0xFF) ? (int16_t)raw : (int8_t)raw;
    int32_t span = (int32_t)loc->max - (int32_t)loc->min;
    if (span <= 0) return 128;
    int32_t out = 1 + (int32_t)(sval - loc->min) * 254 / span;
    return out < 1 ? 1 : (out > 255 ? 255 : (uint8_t)out);
  }
  return scale_analog_hid_gamepad(raw, loc->max);
}

// process generic usb hid input reports (from parsed HID descriptor byteIndexes & bitMasks)
void process_hid_gamepad(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  uint32_t buttons = 0;
  static dinput_gamepad_t previous[MAX_DEVICES][5];
  dinput_gamepad_t current = {0};
  current.value = 0;

  dinput_instance_t *inst = &hid_devices[dev_addr].instances[instance];

  uint16_t xValue = read_axis_value(report, &inst->xLoc);
  uint16_t yValue = read_axis_value(report, &inst->yLoc);
  uint16_t zValue = read_axis_value(report, &inst->zLoc);
  uint16_t rzValue = read_axis_value(report, &inst->rzLoc);
  uint16_t rxValue = read_axis_value(report, &inst->rxLoc);
  uint16_t ryValue = read_axis_value(report, &inst->ryLoc);

  uint8_t hatValue = report[inst->hatLoc.byteIndex] & inst->hatLoc.bitMask;

  // parse hat from report
  if (inst->hatLoc.bitMask) {
    uint8_t direction = hatValue <= 8 ? hatValue : 8; // fix for hats with pressed state greater than 8
    current.all_direction |= HAT_SWITCH_TO_DIRECTION_BUTTONS[direction];
  } else {
    hatValue = 8;
  }

  // parse buttons from report
  current.all_buttons = 0;
  for (int i = 0; i < MAX_BUTTONS; i++) {
    if (inst->buttonLoc[i].bitMask &&
        (report[inst->buttonLoc[i].byteIndex] & inst->buttonLoc[i].bitMask)) {
      current.all_buttons |= (0x01 << i);
    }
  }

  // parse analog from report (sign-aware; sticks default centered, triggers to 0)
  current.x  = inst->xLoc.bitMask  ? scale_axis(&inst->xLoc, xValue)   : 128;
  current.y  = inst->yLoc.bitMask  ? scale_axis(&inst->yLoc, yValue)   : 128;
  current.z  = inst->zLoc.bitMask  ? scale_axis(&inst->zLoc, zValue)   : 128;
  current.rz = inst->rzLoc.bitMask ? scale_axis(&inst->rzLoc, rzValue) : 128;
  current.rx = inst->rxLoc.bitMask ? scale_axis(&inst->rxLoc, rxValue) : 0;
  current.ry = inst->ryLoc.bitMask ? scale_axis(&inst->ryLoc, ryValue) : 0;

  // TODO: based on diff report rather than current's datastructure in order to get subtle analog changes
  if (previous[dev_addr-1][instance].value != current.value)
  {
    previous[dev_addr-1][instance] = current;

    uint8_t buttonCount = inst->buttonCnt;
    if (buttonCount > MAX_BUTTONS) buttonCount = MAX_BUTTONS;

    if (HID_DEBUG) {
      TU_LOG1("HID Report [%s]: ", inst->xbox_axes ? "Xbox" : "DInput");
      TU_LOG1("Buttons: %d", buttonCount);
      TU_LOG1(" x:%d y:%d z:%d rz:%d rx:%d ry:%d dPad:%d\n",
        current.x, current.y, current.z, current.rz, current.rx, current.ry, hatValue);
      for (int i = 0; i < buttonCount && i < MAX_BUTTONS; i++) {
        TU_LOG1(" B%d:%d", i + 1, (current.all_buttons & (0x01 << i)) ? 1 : 0);
      }
      TU_LOG1("\n");
    }

    if (inst->button_map) {
      // Direct per-device map (HID button index -> JP_BUTTON), bypassing the
      // generic reshuffle for pads whose button order it gets wrong.
      buttons = ((current.up)    ? JP_BUTTON_DU : 0) |
                ((current.down)  ? JP_BUTTON_DD : 0) |
                ((current.left)  ? JP_BUTTON_DL : 0) |
                ((current.right) ? JP_BUTTON_DR : 0);
      for (int i = 0; i < MAX_BUTTONS; i++) {
        if (current.all_buttons & (0x01u << i)) buttons |= inst->button_map[i];
      }
    } else if (inst->xbox_axes) {
      // Xbox HID: buttons in W3C order (A,B,X,Y,LB,RB,Back,Start,LS,RS,Guide)
      buttons = ((current.up)       ? JP_BUTTON_DU : 0) |
                ((current.down)     ? JP_BUTTON_DD : 0) |
                ((current.left)     ? JP_BUTTON_DL : 0) |
                ((current.right)    ? JP_BUTTON_DR : 0) |
                ((current.button1)  ? JP_BUTTON_B1 : 0) |
                ((current.button2)  ? JP_BUTTON_B2 : 0) |
                ((current.button3)  ? JP_BUTTON_B3 : 0) |
                ((current.button4)  ? JP_BUTTON_B4 : 0) |
                ((current.button5)  ? JP_BUTTON_L1 : 0) |
                ((current.button6)  ? JP_BUTTON_R1 : 0) |
                ((current.button7)  ? JP_BUTTON_S1 : 0) |
                ((current.button8)  ? JP_BUTTON_S2 : 0) |
                ((current.button9)  ? JP_BUTTON_L3 : 0) |
                ((current.button10) ? JP_BUTTON_R3 : 0) |
                ((current.button11) ? JP_BUTTON_A1 : 0);
    } else {
      // DInput: remap face buttons and Select/Start
      bool buttonSelect, buttonStart;
      bool buttonI = current.button1;
      bool buttonIII = current.button3;
      bool buttonIV = current.button4;
      bool buttonV = buttonCount >= 7 ? current.button5 : 0;
      bool buttonVI = buttonCount >= 8 ? current.button6 : 0;
      bool buttonVII = buttonCount >= 9 ? current.button7 : 0;
      bool buttonVIII = buttonCount >= 10 ? current.button8 : 0;

      if (buttonCount >= 10) {
        buttonSelect = current.button9;
        buttonStart = current.button10;
        buttonI = current.button3;
        buttonIII = current.button4;
        buttonIV = current.button1;
      } else {
        buttonSelect = current.all_buttons & (0x01 << (buttonCount-2));
        buttonStart = current.all_buttons & (0x01 << (buttonCount-1));
      }

      buttons = ((current.up)       ? JP_BUTTON_DU : 0) |
                ((current.down)     ? JP_BUTTON_DD : 0) |
                ((current.left)     ? JP_BUTTON_DL : 0) |
                ((current.right)    ? JP_BUTTON_DR : 0) |
                ((current.button2)  ? JP_BUTTON_B1 : 0) |
                ((buttonI)          ? JP_BUTTON_B2 : 0) |
                ((buttonIV)         ? JP_BUTTON_B3 : 0) |
                ((buttonIII)        ? JP_BUTTON_B4 : 0) |
                ((buttonV)          ? JP_BUTTON_L1 : 0) |
                ((buttonVI)         ? JP_BUTTON_R1 : 0) |
                ((buttonVII)        ? JP_BUTTON_L2 : 0) |
                ((buttonVIII)       ? JP_BUTTON_R2 : 0) |
                ((buttonSelect)     ? JP_BUTTON_S1 : 0) |
                ((buttonStart)      ? JP_BUTTON_S2 : 0) |
                ((current.button11) ? JP_BUTTON_L3 : 0) |
                ((current.button12) ? JP_BUTTON_R3 : 0);
    }

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
      .transport = INPUT_TRANSPORT_USB,
      .buttons = buttons,
      .button_count = buttonCount,
      .analog = {axis_x, axis_y, axis_z, axis_rz, current.rx, current.ry},
      .keys = 0,
    };
    router_submit_input(&event);
  }
}

// resets default values in case devices are hotswapped
void unmount_hid_gamepad(uint8_t dev_addr, uint8_t instance)
{
  TU_LOG1("HID Gamepad[%d|%d]: Unmount Reset\r\n", dev_addr, instance);
  memset(&hid_devices[dev_addr].instances[instance], 0, sizeof(dinput_instance_t));
}

DeviceInterface hid_gamepad_interface = {
  .name = "HID Gamepad",
  .is_device = is_hid_gamepad,
  .check_descriptor = parse_hid_gamepad,
  .process = process_hid_gamepad,
  .unmount = unmount_hid_gamepad,
  .init = NULL,
};
