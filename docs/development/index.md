# Development Guide

This section is for contributors who want to extend Joypad OS -- adding new input interfaces, output interfaces, apps, or device drivers.

## Prerequisites

Before diving in, make sure you can build and flash existing firmware:

```bash
# One-time setup (macOS)
brew install --cask gcc-arm-embedded cmake git

# Clone and initialize
git clone https://github.com/joypad-ai/joypad-os.git
cd joypad-os
make init

# Build any app to verify your setup
make usb2gc_kb2040
```

See the [build guide](../getting-started/building.md) for full setup instructions including Linux and Windows.

## Repository Layout

```
src/
  apps/           App configurations (one directory per app)
  core/           Shared firmware: router, buttons, input_event, output_interface
    router/       Input-to-output routing
    services/     Players, profiles, storage, LEDs, hotkeys, codes, display, button, speaker
  usb/
    usbh/         USB host (input): HID parsing, vendor drivers, XInput
    usbd/         USB device (output): HID gamepad, XInput, PS3/4, Switch modes
  bt/             Bluetooth: transport, BTstack host, BT HID device drivers
  wifi/           WiFi: JOCP protocol (Pico W)
  native/
    host/         Native controller reading (SNES, N64, GC, NES, LodgeNet, etc.)
    device/       Console output protocols (GameCube, PCEngine, Dreamcast, etc.)
  platform/       Platform HAL (RP2040, ESP32, nRF52840)
  lib/            External libraries (TinyUSB, BTstack, pico-sdk, joybus-pio)
esp/              ESP32-S3 build directory (ESP-IDF)
nrf/              nRF52840 build directory (Zephyr/nRF Connect SDK)
```

## Adding a New App

1. Create `src/apps/<appname>/` with three files:

   - **`app.h`** -- Compile-time config: version, routing mode, max players, transform flags.
   - **`app.c`** -- Runtime wiring: return input/output interface arrays, call `router_init()` with your config, register profiles.
   - **`profiles.h`** -- (Optional) Button remapping tables.

2. Add build targets to `CMakeLists.txt` and `Makefile`.

3. Build: `make <appname>_<board>`

Use an existing app as a template. `usb2gc` is a good example of a console adapter; `bt2usb` is a good example of a USB output app.

Key decisions for your app:
- **Routing mode**: SIMPLE (1:1), MERGE (all-to-one), or BROADCAST (one-to-all)
- **Player management**: SHIFT (slots shift on disconnect) or FIXED (slots stay assigned)
- **Profiles**: Define in `profiles.h` or omit for passthrough

## Adding a New USB Device Driver

When a new USB controller needs special handling beyond generic HID:

1. Create `src/usb/usbh/hid/devices/vendors/<vendor>/<device>.c` and `.h`

2. Implement four functions:
   ```c
   bool <device>_is_device(uint16_t vid, uint16_t pid);
   void <device>_init(uint8_t dev_addr, uint8_t instance);
   void <device>_process(uint8_t dev_addr, uint8_t instance,
                         uint8_t const* report, uint16_t len);
   void <device>_disconnect(uint8_t dev_addr, uint8_t instance);
   ```

3. Register in `hid_registry.c`.

The `_is_device` function matches VID/PID. The `_process` function parses the raw HID report and calls `router_submit_input()` with a normalized `input_event_t`.

## Adding a New Bluetooth Device Driver

Same pattern as USB, but in `src/bt/bthid/devices/vendors/<vendor>/`:

1. Create the driver `.c` and `.h` files.
2. Implement the same four-function interface.
3. Register in the BT device registry.

BT drivers receive HID reports from BTstack instead of TinyUSB, but the normalization and router submission are identical.

## Adding a New Input Interface

For a new input source (new protocol, new bus type):

1. Create `src/native/host/<protocol>/` with `<protocol>_host.c` and `.h`.

2. Implement `InputInterface`:
   ```c
   const InputInterface <protocol>_input_interface = {
       .name = "<protocol>",
       .source = INPUT_SOURCE_NATIVE_<PROTOCOL>,
       .init = <protocol>_host_init,
       .task = <protocol>_host_task,
       .is_connected = <protocol>_host_is_connected,
       .get_device_count = <protocol>_host_get_device_count,
   };
   ```

3. Add `INPUT_SOURCE_NATIVE_<PROTOCOL>` to `router.h`.

4. In `task()`, poll the controller and call `router_submit_input()` with a normalized `input_event_t`.

5. Use device addresses in the 0xD0+ range for native controllers.

6. If the protocol uses non-HID Y-axis convention (like Nintendo controllers), invert Y during normalization.

## Adding a New Output Interface

For a new console or output device:

1. Create `src/native/device/<console>/` with the device driver and any PIO programs.

2. Implement `OutputInterface`:
   ```c
   const OutputInterface <console>_output_interface = {
       .name = "<console>",
       .target = OUTPUT_TARGET_<CONSOLE>,
       .init = <console>_init,
       .task = <console>_task,
       .core1_task = <console>_core1_task,   // if timing-critical
       .get_rumble = <console>_get_rumble,
       .get_player_led = <console>_get_player_led,
   };
   ```

3. In `core1_task()`, read from the router with `router_get_output(target, slot)` and send via PIO.

4. PIO programs have a 32-instruction limit. GameCube requires 130MHz overclock via `set_sys_clock_khz(130000, true)`.

5. Use `__not_in_flash_func` for timing-critical code to keep it in SRAM.

## Adding Platform Support

To port Joypad OS to a new microcontroller:

1. Implement the platform HAL functions in `src/platform/<platform>/`:
   ```c
   uint32_t platform_time_ms(void);
   uint32_t platform_time_us(void);
   void platform_sleep_ms(uint32_t ms);
   void platform_get_serial(char* buf, size_t len);
   void platform_reboot(void);
   void platform_reboot_bootloader(void);
   ```

2. Create the platform-specific build directory (like `esp/` or `nrf/`).

3. Implement flash storage backend for the storage service.

4. Implement LED driver if the board has NeoPixel or similar.

See `esp/` and `nrf/` for complete examples of platform ports.

## Common Pitfalls

- **GameCube requires 130MHz** -- `set_sys_clock_khz(130000, true)` must be called before PIO init.
- **PIO has 32 instruction limit** -- Optimize or split programs across state machines.
- **Use `__not_in_flash_func`** -- For all timing-critical code called from Core 1.
- **Y-axis convention** -- HID standard: 0=up, 128=center, 255=down. Nintendo is inverted.
- **ESP32 `tud_task()` blocks forever** -- Always use `tud_task_ext(1, false)` on FreeRTOS.
- **BTstack threading** -- All BTstack API calls must happen in the BTstack task/thread, not the main task.

## CI/CD

GitHub Actions (`.github/workflows/build.yml`) builds all apps on push to `main`. Docker-based for consistency. Artifacts go to `releases/`.

## Next Steps

- [Architecture](../overview/architecture.md) -- Understand the layer model
- [Data Flow](../overview/data-flow.md) -- How data moves through the system
- [Glossary](../overview/glossary.md) -- Key terms defined
- [Apps](../apps/index.md) -- See how existing apps are structured
