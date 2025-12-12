// hid_mouse.c
#include "hid_mouse.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/router/router.h"
#include "core/input_event.h"

static uint8_t local_x;
static uint8_t local_y;

// Button swap functionality
// -------------------------
#ifdef MID_BUTTON_SWAPPABLE
const bool buttons_swappable = true;
#else
const bool buttons_swappable = false;
#endif

static bool buttons_swapped = false;

void cursor_movement(int8_t x, int8_t y, int8_t wheel, uint8_t spinner)
{
  uint8_t x1, y1;

#if USE_ANSI_ESCAPE
  // Move X using ansi escape
  if ( x < 0)
  {
    TU_LOG1(ANSI_CURSOR_BACKWARD(%d), (-x)); // move left
  }else if ( x > 0)
  {
    TU_LOG1(ANSI_CURSOR_FORWARD(%d), x); // move right
  }

  // Move Y using ansi escape
  if ( y < 0)
  {
    TU_LOG1(ANSI_CURSOR_UP(%d), (-y)); // move up
  }else if ( y > 0)
  {
    TU_LOG1(ANSI_CURSOR_DOWN(%d), y); // move down
  }

  // Scroll using ansi escape
  if (wheel < 0)
  {
    TU_LOG1(ANSI_SCROLL_UP(%d), (-wheel)); // scroll up
  }else if (wheel > 0)
  {
    TU_LOG1(ANSI_SCROLL_DOWN(%d), wheel); // scroll down
  }

  TU_LOG1("\r\n");
#else
  TU_LOG1("(%d %d %d %d)\r\n", x, y, wheel, spinner);
#endif
}

// process usb hid input reports
void process_hid_mouse(uint8_t dev_addr, uint8_t instance, uint8_t const* mouse_report, uint16_t len) {
  uint32_t buttons;
  hid_mouse_report_t const* report = (hid_mouse_report_t const*)mouse_report;
  static hid_mouse_report_t prev_report = { 0 };

  static bool previous_middle_button = false;

  //------------- button state  -------------//
  uint8_t button_changed_mask = report->buttons ^ prev_report.buttons;
  if (button_changed_mask & report->buttons) {
    TU_LOG1(" %c%c%c%c%c ",
       report->buttons & MOUSE_BUTTON_BACKWARD  ? 'R' : '-',
       report->buttons & MOUSE_BUTTON_FORWARD   ? 'S' : '-',
       report->buttons & MOUSE_BUTTON_LEFT      ? '2' : '-',
       report->buttons & MOUSE_BUTTON_MIDDLE    ? 'M' : '-',
       report->buttons & MOUSE_BUTTON_RIGHT     ? '1' : '-');

    if (buttons_swappable && (report->buttons & MOUSE_BUTTON_MIDDLE) &&
        (previous_middle_button == false))
       buttons_swapped = (buttons_swapped ? false : true);

    previous_middle_button = (report->buttons & MOUSE_BUTTON_MIDDLE);
  }

  // Active-high: set bit when button is pressed
  if (buttons_swapped) {
     buttons = (((report->buttons & MOUSE_BUTTON_RIGHT)   ? JP_BUTTON_B1 : 0) |
                ((report->buttons & MOUSE_BUTTON_LEFT)    ? JP_BUTTON_B2 : 0) |
                ((report->buttons & MOUSE_BUTTON_BACKWARD)? JP_BUTTON_B3 : 0) |
                ((report->buttons & MOUSE_BUTTON_FORWARD) ? JP_BUTTON_S1 : 0) |
                ((report->buttons & MOUSE_BUTTON_MIDDLE)  ? JP_BUTTON_S2 : 0));
  } else {
     buttons = (((report->buttons & MOUSE_BUTTON_LEFT)    ? JP_BUTTON_B1 : 0) |
                ((report->buttons & MOUSE_BUTTON_RIGHT)   ? JP_BUTTON_B2 : 0) |
                ((report->buttons & MOUSE_BUTTON_BACKWARD)? JP_BUTTON_B3 : 0) |
                ((report->buttons & MOUSE_BUTTON_FORWARD) ? JP_BUTTON_S1 : 0) |
                ((report->buttons & MOUSE_BUTTON_MIDDLE)  ? JP_BUTTON_S2 : 0));
  }

  // Pass raw mouse deltas - console output layer handles any axis inversion
  local_x = report->x;
  local_y = report->y;

  // Pass raw mouse deltas (platform-agnostic)
  // Console-side decides how to interpret (e.g., Nuon converts to spinner)
  input_event_t event = {
    .dev_addr = dev_addr,
    .instance = instance,
    .type = INPUT_TYPE_MOUSE,
    .buttons = buttons,
    .analog = {128, 128, 128, 128, 128, 0, 0, 128},
    .delta_x = local_x,
    .delta_y = local_y,
    .delta_wheel = report->wheel,
    .keys = 0
  };
  router_submit_input(&event);

  //------------- cursor movement -------------//
  cursor_movement(report->x, report->y, report->wheel, 0);
}

DeviceInterface hid_mouse_interface = {
  .name = "HID Mouse",
  .is_device = NULL,
  .init = NULL,
  .task = NULL,
  .process = process_hid_mouse,
};
