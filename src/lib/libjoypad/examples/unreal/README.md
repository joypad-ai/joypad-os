# unreal example

Bare Unreal Engine 5 plugin that consumes libjoypad to add Sony DualSense
advanced-feature support to the engine's input pipeline.

This is a **reference integration**, not a marketplace-ready plugin. Pieces
are intentionally minimal so you can see how a real `joypad-unreal` would
wrap libjoypad in idiomatic UBT + Enhanced Input.

## What it does

- Detects Sony DualSense / Victrix Pro FS for PS5 via `joypad_is_sony_ds5`
- Opens the device via HIDAPI
- Polls in `Tick()`, parses HID reports via `joypad_parse_sony_ds5`
- Dispatches button events through `FGenericApplicationMessageHandler` so
  Enhanced Input pipes them into Blueprints / C++ as normal
- Emits gyro, accelerometer, and touchpad coordinates as custom analog axes
  (`Joypad_GyroX`, `Joypad_Touch0_X`, etc.) — bind them in your Input Action
  asset to surface in gameplay

Force-feedback dispatch (rumble + adaptive triggers + lightbar) is stubbed
out in `SetChannelValue` — wiring it up to `joypad_build_sony_ds5_feedback`
and `hid_write` is left as the next step.

## File layout

```
JoypadInput.uplugin                                    plugin manifest
Source/JoypadInput/JoypadInput.Build.cs                UBT build config
Source/JoypadInput/Public/JoypadInputModule.h          module interface
Source/JoypadInput/Private/JoypadInputModule.cpp       module impl
Source/JoypadInput/Private/FJoypadInputDevice.h        IInputDevice subclass
Source/JoypadInput/Private/FJoypadInputDevice.cpp      IInputDevice impl
Source/JoypadInput/Private/libjoypad_unity.c           pulls in libjoypad sources
```

`libjoypad_unity.c` keeps all of libjoypad's C sources in one TU so UBT
compiles them as part of the plugin without exotic include-path gymnastics.

## Install into a UE5 project

1. Copy this directory to `<YourProject>/Plugins/JoypadInput`
2. Install HIDAPI:
   - **macOS:** `brew install hidapi`
   - **Linux:** `apt install libhidapi-dev`
   - **Windows:** vcpkg or drop `hidapi.lib`/`hidapi.dll` under
     `ThirdParty/HIDAPI/Win64/` and uncomment that block in `Build.cs`
3. Regenerate project files (Tools → Refresh Visual Studio / Xcode project)
4. Build and launch the editor
5. Edit → Plugins → search "Joypad Input" → enable, restart
6. Connect a DualSense over USB before play-in-editor

## Verify

Open the Output Log; you should see:

```
LogJoypadInput: Joypad Input initialized (libjoypad).
LogJoypadInput: DualSense opened via libjoypad.
```

Standard gamepad inputs (`Gamepad_FaceButton_Bottom`, etc.) will route
through Enhanced Input automatically. To bind advanced features in
Blueprint:

1. Create an Input Action of type Axis 2D / Axis 3D
2. Bind it to a Key Mapping using a Custom Key with FName
   `Joypad_GyroX` / `Joypad_GyroY` / `Joypad_GyroZ`
3. Same for `Joypad_AccelX/Y/Z`, `Joypad_Touch0_X/Y`, etc.

## What this proves

The same C parser that runs in joypad-os firmware on RP2040 and in a Chrome
tab via WASM also runs unchanged inside Unreal Engine on Windows / macOS /
Linux desktops — with HIDAPI as the transport, `IInputDevice` as the
plugin contract, and Enhanced Input as the game-facing API.

For a UE5 game shipping to platforms where Microsoft GameInput doesn't
reach (PS5 — though there it's covered by Sony's SDK — Linux, macOS, web
export), this is the cross-platform alternative.

## Beyond this example

The polished `joypad-unreal` plugin (future separate repo) would add:
- Marketplace metadata + UE5/UE5.4/UE5.5 compatibility matrix
- Vendored HIDAPI per platform so users don't install it manually
- Blueprint nodes for direct `joypad_caps_t` and feedback control
- Per-player slot tracking + UE input device ID assignment
- All other libjoypad-supported controllers (Switch Pro, Xbox, 8BitDo, …)
  as they migrate into libjoypad
