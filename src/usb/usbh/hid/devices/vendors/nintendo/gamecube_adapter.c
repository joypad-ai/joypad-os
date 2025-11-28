// gamecube_adapter.c
#include "gamecube_adapter.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "pico/time.h"

// check if device is GameCube Adapter for WiiU/Switch
static inline bool is_gamecube_adapter(uint16_t vid, uint16_t pid) {
  return (vid == 0x057e && pid == 0x0337); // GameCube Adapter
}

// check if 2 reports are different enough
bool diff_report_gamecube_adapter(gamecube_adapter_report_t const* rpt1, gamecube_adapter_report_t const* rpt2, uint8_t player) {
  bool result;

  // x, y must different than 2 to be counted
  result = diff_than_n(rpt1->port[player].x1, rpt2->port[player].x1, 2) || diff_than_n(rpt1->port[player].y1, rpt2->port[player].y1, 2) ||
           diff_than_n(rpt1->port[player].x2, rpt2->port[player].x2, 2) || diff_than_n(rpt1->port[player].y2, rpt2->port[player].y2, 2) ||
           diff_than_n(rpt1->port[player].zl, rpt2->port[player].zl, 2) || diff_than_n(rpt1->port[player].zr, rpt2->port[player].zr, 2);

  // check the all with mem compare (after report_id players are spaced 9 bytes apart)
  result |= memcmp(&rpt1->report_id + 1 + (player*9), &rpt2->report_id + 1 + (player*9), 3);

  return result;
}

// process usb hid input reports
void input_gamecube_adapter(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static gamecube_adapter_report_t prev_report[5][4];

  gamecube_adapter_report_t gamecube_report;
  memcpy(&gamecube_report, report, sizeof(gamecube_report));

  if (gamecube_report.report_id == 0x21) {
    uint32_t buttons; // GameCube Controller Report
    for(int i = 0; i < 4; i++) {
      if (gamecube_report.port[i].connected) {
        if (diff_report_gamecube_adapter(&prev_report[dev_addr-1][instance + i], &gamecube_report, i)) {
          TU_LOG1("GAMECUBE[%d|%d]: Report ID = 0x%x\r\n", dev_addr, (instance + i), gamecube_report.report_id);
          TU_LOG1("(x, y, cx, cy, zl, zr) = (%u, %u, %u, %u, %u, %u)\r\n",
            gamecube_report.port[i].x1,
            gamecube_report.port[i].y1,
            gamecube_report.port[i].x2,
            gamecube_report.port[i].y2,
            gamecube_report.port[i].zl,
            gamecube_report.port[i].zr);
          TU_LOG1("DPad = ");

          if (gamecube_report.port[i].down) TU_LOG1("Down ");
          if (gamecube_report.port[i].up) TU_LOG1("Up ");
          if (gamecube_report.port[i].right) TU_LOG1("Right ");
          if (gamecube_report.port[i].left) TU_LOG1("Left ");
          if (gamecube_report.port[i].a) TU_LOG1("A ");
          if (gamecube_report.port[i].b) TU_LOG1("B ");
          if (gamecube_report.port[i].x) TU_LOG1("X ");
          if (gamecube_report.port[i].y) TU_LOG1("Y ");
          if (gamecube_report.port[i].z) TU_LOG1("Z ");
          if (gamecube_report.port[i].l) TU_LOG1("L ");
          if (gamecube_report.port[i].r) TU_LOG1("R ");
          if (gamecube_report.port[i].start) TU_LOG1("Start ");
          TU_LOG1("\n");

          bool dpad_left  = gamecube_report.port[i].left;
          bool dpad_right = gamecube_report.port[i].right;
          bool dpad_up    = gamecube_report.port[i].up;
          bool dpad_down  = gamecube_report.port[i].down;

          buttons = (((dpad_up)                       ? USBR_BUTTON_DU : 0) |
                     ((dpad_down)                     ? USBR_BUTTON_DD : 0) |
                     ((dpad_left)                     ? USBR_BUTTON_DL : 0) |
                     ((dpad_right)                    ? USBR_BUTTON_DR : 0) |
                     ((gamecube_report.port[i].b)     ? USBR_BUTTON_B1 : 0) |
                     ((gamecube_report.port[i].a)     ? USBR_BUTTON_B2 : 0) |
                     ((gamecube_report.port[i].y)     ? USBR_BUTTON_B3 : 0) |
                     ((gamecube_report.port[i].x)     ? USBR_BUTTON_B4 : 0) |
                     ((0)                             ? USBR_BUTTON_L1 : 0) |
                     ((gamecube_report.port[i].z)     ? USBR_BUTTON_R1 : 0) |
                     ((gamecube_report.port[i].l)     ? USBR_BUTTON_L2 : 0) |
                     ((gamecube_report.port[i].r)     ? USBR_BUTTON_R2 : 0) |
                     ((0)                             ? USBR_BUTTON_S1 : 0) |
                     ((gamecube_report.port[i].start) ? USBR_BUTTON_S2 : 0) |
                     ((0)                             ? USBR_BUTTON_L3 : 0) |
                     ((0)                             ? USBR_BUTTON_R3 : 0) |
                     ((0)                             ? USBR_BUTTON_A1 : 0));

          uint8_t zl_axis = gamecube_report.port[i].zl;
          zl_axis = zl_axis > 38 ? zl_axis - 38 : 0;
          uint8_t zr_axis = gamecube_report.port[i].zr;
          zr_axis = zr_axis > 38 ? zr_axis - 38 : 0;

          input_event_t event = {
            .dev_addr = dev_addr,
            .instance = i,
            .type = INPUT_TYPE_GAMEPAD,
            .buttons = buttons,
            .button_count = 6,  // GC: B, A, Y, X, L, R (Z is mapped to S1)
            .analog = {
              gamecube_report.port[i].x1,
              gamecube_report.port[i].y1,
              gamecube_report.port[i].x2,
              gamecube_report.port[i].y2,
              128,
              zl_axis,
              zr_axis,
              128
            },
            .keys = 0,
          };
          router_submit_input(&event);

          prev_report[dev_addr-1][instance + i] = gamecube_report;
        }
      } else if (prev_report[dev_addr-1][instance + i].port[i].connected) { // disconnected
        remove_players_by_address(dev_addr, instance + i);
        prev_report[dev_addr-1][instance + i] = gamecube_report;
      }
    }
  }
}

// process usb hid output reports
void output_gamecube_adapter(uint8_t dev_addr, uint8_t instance, device_output_config_t* config)
{
  static uint8_t last_rumble = 0;
  if (config->rumble != last_rumble)
  {
    last_rumble = config->rumble;

    uint8_t buf4[5] = { 0x11, /* GC_CMD_RUMBLE */ };
    for(int i = 0; i < 4; i++)
    {
      buf4[i+1] = config->rumble ? 1 : 0;
    }
    tuh_hid_send_report(dev_addr, instance, buf4[0], &(buf4[0])+1, sizeof(buf4) - 1);
  }
}

// process usb hid output reports
void task_gamecube_adapter(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
  const uint32_t interval_ms = 20;
  static uint32_t start_ms = 0;

  uint32_t current_time_ms = to_ms_since_boot(get_absolute_time());
  if (current_time_ms - start_ms >= interval_ms) {
    start_ms = current_time_ms;
    output_gamecube_adapter(dev_addr, instance, config);
  }
}

DeviceInterface gamecube_adapter_interface = {
  .name = "GameCube Adapter for WiiU/Switch",
  .is_device = is_gamecube_adapter,
  .process = input_gamecube_adapter,
  .task = task_gamecube_adapter,
  .init = NULL
};
