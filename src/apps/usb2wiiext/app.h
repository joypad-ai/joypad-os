// app.h - USB2WIIEXT App Manifest
// USB HID controllers → Wii extension-port emulation.
//
// A real Wiimote plugs into the microcontroller's I2C bus (extension
// socket wiring) and sees a Wii Classic Controller Pro responding to its
// polls. Input comes from any USB HID controller attached to the board's
// native USB host port.
//
// This is the wired sibling of bt2wiiext: identical output stage, USB host
// instead of the CYW43 Bluetooth radio as the input stage.
//
// NOTE ON FLAGS: only the defines below that the build or app.c actually
// reads are declared here. joypad-ai/joypad-os#198 catalogues 55 app.h
// manifest flags that are defined across the tree and read nowhere — several
// of which state the opposite of what the firmware does. Rather than copy
// bt2wiiext's manifest wholesale and inherit that debt, this file carries
// only live values.

#ifndef APP_USB2WIIEXT_H
#define APP_USB2WIIEXT_H

#define APP_NAME "USB2WIIEXT"
#define APP_DESCRIPTION "USB controllers to Wii extension port"
#define APP_AUTHOR "RobertDaleSmith"

// Output: Wii extension I2C slave at 0x52. One Wiimote, one player.
#define WII_OUTPUT_PORTS 1

// Routing — blend every attached USB controller into the single ext port,
// matching bt2wiiext (and usb2gc's single-port MERGE_BLEND).
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE   MERGE_BLEND
#define TRANSFORM_FLAGS 0

#define PLAYER_SLOT_MODE     PLAYER_SLOT_SHIFT
#define MAX_PLAYER_SLOTS     1
#define AUTO_ASSIGN_ON_PRESS 1

void app_init(void);
void app_task(void);

#endif // APP_USB2WIIEXT_H
