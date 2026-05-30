# unreal example

Bare Unreal Engine 5 plugin. UBT builds libjoypad as a static lib; the plugin
opens controllers via HIDAPI (Windows) / IOKit (macOS) / hidraw (Linux),
registers as an `IInputDevice`, and dispatches events into Enhanced Input.

Reference integration showing how Unreal devs would consume libjoypad. The
polished, marketplace-ready version lives in a future `joypad-unreal` repo.

Status: not yet implemented (Task #10).
