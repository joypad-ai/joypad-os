// usb2dc_config.h — build-time ARCHITECTURE config for the USB→Dreamcast app.
//
// This is the single source of truth for how usb2dc is built, for EVERY board.
// CMake force-includes this into every translation unit of the usb2dc targets
// (-include, see CMakeLists.txt) so the shared device code — dreamcast_device.c,
// vmu*.c, tusb_config.h — sees it too, not just app.c.
//
// Board targets in CMake carry ONLY GPIO / peripherals (Maple pins, NeoPixel,
// SD, button). They must NEVER set these flags — that per-board duplication is
// exactly what let the kb2040 build drift off CONFIG_DC_CORE1_TX and cause the
// Maple-drops-on-VMU-save bug. Architecture lives here; boards differ only in
// wiring.
#ifndef USB2DC_CONFIG_H
#define USB2DC_CONFIG_H

// Output: Dreamcast Maple bus, with the VMU persisted to a QSPI flash partition.
#define CONFIG_DC               1
#define CONFIG_USB_HOST         1
#define CONFIG_VMU              1
#define CONFIG_VMU_QSPI         1

// Core allocation: USB host runs on Core 0, so the Maple response runs RX+TX on
// the free Core 1 ("respond immediately", like GameCube). The whole Core-1 path
// is RAM-resident, so flash writes need no multicore lockout — a lockout would
// freeze Core 1 mid-poll and drop the controller.
#define CONFIG_DC_CORE1_TX      1
#define CONFIG_NO_FLASH_LOCKOUT 1

// Dreamcast = 4 controller ports, and the 128 KB VMU image is RAM-tight, so
// right-size the USB host arrays below the global hub/device caps.
#define CFG_TUH_HUB             1   // one hub covers 4 ports (no cascading)
#define CFG_TUH_DEVICE_MAX      5   // 4 ports + margin
#define MAX_DEVICES             7   // floor: CFG_TUH_DEVICE_MAX + CFG_TUH_HUB + 1
#define MAX_PLAYERS_PER_OUTPUT  4   // 4 ports; save RAM vs the global 8

#endif // USB2DC_CONFIG_H
