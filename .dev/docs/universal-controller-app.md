# Universal Controller App

## Vision

Consolidate `controller` and `controller_btusb` into a single universal controller app where inputs and outputs are both modular and composable. A single board could accept GPIO buttons, USB controllers, and BT controllers as inputs, and output to USB HID, BLE, SNES, N64, GameCube, or any other supported console — all configured via compile definitions and runtime GPIO config via web UI.

## Current State

### Two Separate Apps
- **`apps/controller/`** — GPIO pad → USB device only. No BT input or output. Uses `pad_input`, profiles, speaker, display, UART link, I2C peer.
- **`apps/controller_btusb/`** — GPIO pad + JoyWing sensor → BLE peripheral + USB device. Uses CYW43/ESP32/nRF BT transport, BLE output (HOGP), Joy animation, OLED. No USB host input.

### What's Duplicated
- Button event handling (mode cycling, bond clearing)
- Router configuration (similar but not identical)
- LED color management based on active output mode
- Player management setup
- Display/OLED updates

### What's Different
- `controller` has: profiles system, codes detection, UART link, I2C peer, speaker rumble feedback, SPI display
- `controller_btusb` has: BLE output, BT transport init, Joy animation, OLED I2C display, JoyWing sensor

## Proposed Architecture

### Single App: `apps/controller/`

One `app.c` with compile-time feature flags controlling which inputs/outputs are active:

```
Inputs (any combination):
  CONFIG_INPUT_PAD          — GPIO buttons/sticks (pad_input)
  CONFIG_INPUT_USB_HOST     — USB controllers via TinyUSB host (PIO-USB or MAX3421E)
  CONFIG_INPUT_BT_HOST      — BT/BLE controllers via BTstack host
  CONFIG_INPUT_JOYWING      — Seesaw I2C sensor (JoyWing)

Outputs (any combination):
  CONFIG_OUTPUT_USB_DEVICE  — USB HID/XInput/PS3/Switch device
  CONFIG_OUTPUT_BLE         — BLE peripheral (HOGP gamepad)
  CONFIG_OUTPUT_SNES        — SNES controller emulation (PIO)
  CONFIG_OUTPUT_N64         — N64 controller emulation (joybus PIO)
  CONFIG_OUTPUT_GC          — GameCube controller emulation (joybus PIO)
  CONFIG_OUTPUT_PCE         — PCEngine controller emulation (PIO)
  CONFIG_OUTPUT_DC          — Dreamcast controller emulation (maple PIO)
  CONFIG_OUTPUT_NUON        — Nuon controller emulation (polyface PIO)
  CONFIG_OUTPUT_3DO         — 3DO controller emulation (PIO)
  CONFIG_OUTPUT_UART        — UART bridge output
```

### Build Targets (Examples)

```makefile
# Current equivalents
controller_fisherprice_v2:  INPUT_PAD + OUTPUT_USB_DEVICE
controller_btusb_abb:       INPUT_PAD + OUTPUT_USB_DEVICE + OUTPUT_BLE

# New combos
controller_universal:       INPUT_PAD + INPUT_USB_HOST + INPUT_BT_HOST + OUTPUT_USB_DEVICE + OUTPUT_BLE
controller_retro:           INPUT_PAD + OUTPUT_USB_DEVICE + OUTPUT_SNES + OUTPUT_N64 + OUTPUT_GC
controller_adapter:         INPUT_PAD + INPUT_USB_HOST + INPUT_BT_HOST + OUTPUT_SNES + OUTPUT_N64 + OUTPUT_GC + OUTPUT_USB_DEVICE
```

### App Structure

```c
// app.c — universal controller app

// Input interfaces (conditional)
static const InputInterface* input_interfaces[] = {
#ifdef CONFIG_INPUT_PAD
    &pad_input_interface,
#endif
#ifdef CONFIG_INPUT_USB_HOST
    &usbh_input_interface,
#endif
#ifdef CONFIG_INPUT_BT_HOST
    &bthid_input_interface,
#endif
#ifdef CONFIG_INPUT_JOYWING
    &joywing_input_interface,
#endif
};

// Output interfaces (conditional)
static const OutputInterface* output_interfaces[] = {
#ifdef CONFIG_OUTPUT_USB_DEVICE
    &usbd_output_interface,
#endif
#ifdef CONFIG_OUTPUT_BLE
    &ble_output_interface,
#endif
#ifdef CONFIG_OUTPUT_N64
    &n64_device_interface,
#endif
    // ... etc
};

void app_init(void) {
    // GPIO pad config (runtime from flash, fallback to compile-time)
#ifdef CONFIG_INPUT_PAD
    pad_config_flash_init();
    const pad_device_config_t* pad_cfg = pad_config_load_runtime();
    if (!pad_cfg) pad_cfg = &PAD_CONFIG;  // compile-time default
    pad_input_add_device(pad_cfg);
#endif

    // BT transport (needed for BLE output OR BT host input)
#if defined(CONFIG_OUTPUT_BLE) || defined(CONFIG_INPUT_BT_HOST)
    bt_init(&bt_transport);
#endif

    // Router: connect all inputs to all outputs
    router_init(&router_cfg);
    // ... add routes based on active input/output combos
}
```

### Button Event Behavior

Mode cycling adapts to available outputs:
- **Single output**: Double-click cycles modes within that output (USB modes, BLE modes, etc.)
- **Multiple outputs**: Double-click cycles the "active" output, triple-click switches which output is active
- **BLE output**: Long-press clears bonds (same as current controller_btusb)

### Router Configuration

With multiple outputs, routing modes become more interesting:
- **SIMPLE**: Each input → one output (round-robin assignment)
- **BROADCAST**: All inputs → all outputs (most common for controller app)
- **MERGE**: All inputs merged, sent to all outputs

Default for universal controller: BROADCAST with merge_all_inputs=true.

### Web Config Extensions

The GPIO Pin Configuration card already works for all controller targets. Future additions:
- Output selection card (enable/disable outputs at runtime)
- Per-output pin assignment (which GPIO for SNES data/clock, N64 joybus, etc.)
- Input source enable/disable

## Migration Plan

### Phase 1: Consolidate (Minimal Disruption)
1. Merge `controller_btusb` features into `controller/app.c` behind `#ifdef` guards
2. Keep all existing build targets working with same behavior
3. Delete `apps/controller_btusb/` once unified
4. Existing CMake targets just change their source from `apps/controller_btusb/app.c` to `apps/controller/app.c`

### Phase 2: Add Input Combos
1. Add `CONFIG_INPUT_USB_HOST` and `CONFIG_INPUT_BT_HOST` support to the controller app
2. Create new build targets that combine GPIO pad input with USB/BT host input
3. The "controller adapter" — accept any input, output to USB/BLE

### Phase 3: Add Native Console Outputs
1. Wire existing console output interfaces (`n64_device`, `gc_device`, `snes_device`, etc.) as selectable outputs
2. Create multi-output build targets
3. Runtime output selection via web config

### Phase 4: Runtime Output Configuration
1. Store active output set in flash (alongside GPIO pin config)
2. Web config card for enabling/disabling outputs
3. Console output pin assignments configurable at runtime

## Hardware Considerations

- **PIO budget**: RP2040 has 2 PIO blocks x 4 SMs = 8 SMs total. Each console protocol uses 1-3 SMs. USB host (PIO-USB) uses 3 SMs. NeoPixel uses 1 SM. Not all outputs can be active simultaneously.
- **Pin budget**: RP2040 has 30 GPIO pins. With USB host + BLE (CYW43) + multiple console outputs, pin allocation gets tight. Runtime pin config helps here.
- **Flash budget**: Each output's pin config needs its own flash storage or the pad_config_flash_t struct needs to grow.
- **RAM budget**: Each active output interface consumes RAM for buffers and state. May need to trim unused features.

## Key Principle

The existing `usb2*` apps (usb2pce, usb2gc, etc.) remain separate — they're optimized for single-purpose adapter use with USB/BT input → one console output. The universal controller app is specifically for custom-built controllers where the user controls the hardware and wants maximum flexibility.
