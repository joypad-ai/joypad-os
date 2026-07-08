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
// ---------------------------------------------------------------------------
// VMU (simulated Dreamcast memory card) — DISABLED for release.
// Internal-flash persistence (QSPI) still introduces Maple timing drops we
// haven't fully resolved; ship without the memory card for now. RUMBLE is
// unaffected — with CONFIG_VMU off, the DC still advertises controller +
// PuruPuru (SUBPERIPHERAL1); only the VMU sub-peripheral (SUBPERIPHERAL0) is
// dropped. All VMU logic stays behind #ifdef CONFIG_VMU for dev to continue.
// Re-enable both lines to resume VMU development.
// #define CONFIG_VMU              1
// #define CONFIG_VMU_QSPI         1

// Core allocation: Core-0 TX (the proven workaround), mirroring rock-solid
// n642dc. KB2040 has a known, never-root-caused *enumeration-dropoff bug* with
// Core-1 TX (see commit 87ad2c4a) — RP2040-Zero validated Core-1 TX fine, only
// KB2040 drops. Core-1 TX was re-enabled during the VMU work purely to keep the
// flash flush off the TX core; with the VMU disabled there's no flush, so we
// return to Core-0 TX and the dropoff goes away.
// When VMU dev resumes, re-enable Core-1 TX *per board* (RP2040-Zero only) —
// NOT on KB2040 until the dropoff is root-caused.
// #define CONFIG_DC_CORE1_TX      1
// #define CONFIG_NO_FLASH_LOCKOUT 1   // only relevant with the QSPI flush

// Dreamcast = 4 controller ports, and the 128 KB VMU image is RAM-tight, so
// right-size the USB host arrays below the global hub/device caps.
#define CFG_TUH_HUB             1   // one hub covers 4 ports (no cascading)
#define CFG_TUH_DEVICE_MAX      5   // 4 ports + margin
#define MAX_DEVICES             7   // floor: CFG_TUH_DEVICE_MAX + CFG_TUH_HUB + 1
#define MAX_PLAYERS_PER_OUTPUT  4   // 4 ports; save RAM vs the global 8

#endif // USB2DC_CONFIG_H
