// usbh.c - USB Host Layer
//
// Provides unified USB host handling across HID and X-input protocols.
// Combines feedback delivery (rumble, LEDs, triggers) for all USB input devices.

#include "usbh.h"
#include "tusb.h"
#include "core/output_interface.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/codes/codes.h"

// HID protocol handlers
extern void hid_init(void);
extern void hid_task(uint8_t rumble, uint8_t leds, uint8_t trigger_threshold, uint8_t test);

// X-input protocol handlers
extern void xinput_task(uint8_t rumble);

// App provides output interface
extern const OutputInterface* app_get_output_interface(void);

void usbh_init(void)
{
    hid_init();
}

void usbh_task(void)
{
    // Get output interface for console-specific feedback
    const OutputInterface* output = app_get_output_interface();

    // Combine console rumble with profile indicator rumble
    uint8_t console_rumble = (output->get_rumble) ? output->get_rumble() : 0;
    uint8_t combined_rumble = console_rumble | feedback_get_rumble();

    // Get player LED value (combines console LED with profile indicator)
    uint8_t console_led = (output->get_player_led) ? output->get_player_led() : 0;
    uint8_t player_led = feedback_get_player_led(playersCount) | console_led;

    // Get adaptive trigger threshold from output interface (DualSense L2/R2)
    uint8_t trigger_threshold = (output->get_trigger_threshold) ? output->get_trigger_threshold() : 0;

    // Get test mode counter (for LED test patterns)
    uint8_t test_counter = codes_get_test_counter();

#if CFG_TUH_XINPUT
    // X-input rumble task (Xbox 360/One controllers)
    xinput_task(combined_rumble);
#endif

#if CFG_TUH_HID
    // HID device rumble/LED/trigger task (DualSense, DualShock, Switch Pro, etc.)
    hid_task(combined_rumble, player_led, trigger_threshold, test_counter);
#endif
}
