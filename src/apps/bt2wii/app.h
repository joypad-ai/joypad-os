// app.h - BT2WII App Manifest
// Bluetooth controllers → Wii extension-port emulation on Pico W.
//
// A real Wiimote plugs into the microcontroller's I2C bus (extension
// socket wiring) and sees a Wii Classic Controller responding to its
// polls. Input comes from any Bluetooth HID controller (Xbox, Sony,
// Nintendo, 8BitDo, etc.) paired to the Pico W's built-in CYW43 radio.

#ifndef APP_BT2WII_H
#define APP_BT2WII_H

#define APP_NAME "BT2WIIEXT"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "Bluetooth to Wii extension (Pico W)"
#define APP_AUTHOR "RobertDaleSmith"

// Input: Pico W built-in Bluetooth
#define REQUIRE_BT_CYW43 1
#define REQUIRE_USB_HOST 0
#define MAX_USB_DEVICES 0

// Output: Wii extension I2C slave at 0x52 (and no USB device).
#define REQUIRE_USB_DEVICE 0
#define REQUIRE_NATIVE_WII_OUTPUT 1
#define WII_OUTPUT_PORTS 1

// Services
#define REQUIRE_FLASH_SETTINGS 0
#define REQUIRE_PROFILE_SYSTEM 0
#define REQUIRE_PLAYER_MANAGEMENT 1

// Routing — single Wiimote, single player.
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE   MERGE_BLEND
#define APP_MAX_ROUTES 4
#define TRANSFORM_FLAGS 0

#define PLAYER_SLOT_MODE     PLAYER_SLOT_SHIFT
#define MAX_PLAYER_SLOTS     1
#define AUTO_ASSIGN_ON_PRESS 1

// Hardware
#define BOARD "pico_w"
#define CPU_OVERCLOCK_KHZ 0
#define UART_DEBUG 1

#define BT_MAX_CONNECTIONS 4
#define BT_SCAN_ON_STARTUP 1

void app_init(void);
void app_task(void);

#endif // APP_BT2WII_H
