# Bluetooth to USB Adapter (Pico W)

Bluetooth controllers to a USB gamepad via Raspberry Pi Pico W. No soldering required.

On a freshly flashed board this build enumerates as an **SInput controller**, the same default as
every other adapter (`USBD_DEFAULT_MODE=USB_OUTPUT_MODE_SINPUT`). The mode is switchable at runtime
and **persists in flash**, so a board whose mode was changed previously comes back up in the saved
mode, not in SInput. See [USB output modes](#usb-output-modes) below.

> ⚠️ Before 2.4.1 `bt2usb` was the one build that overrode this and came up in **PS4** mode. A board
> flashed with 2.4.0 or earlier that was never switched will move to SInput on its first 2.4.1 boot;
> a board with a saved mode keeps it.

## Parts Needed

- [Raspberry Pi Pico W](https://www.raspberrypi.com/products/raspberry-pi-pico/) (~$6)
- USB Micro-B cable (data-capable, not charge-only)

That is it. The Pico W has built-in Bluetooth (Classic + BLE) and a native USB port. No wiring needed.

## Build and Flash

```bash
# Build
make bt2usb_pico_w

# Flash: hold BOOTSEL while plugging in USB, then:
make flash-bt2usb_pico_w
```

Output file: `releases/joypad_os_<commit>_bt2usb_pico_w.uf2`

Alternatively, drag and drop the `.uf2` file onto the `RPI-RP2` drive that appears when the Pico W is in bootloader mode.

## Pairing a Controller

1. Plug the flashed Pico W into a PC or other USB host
2. Put your Bluetooth controller into pairing mode:
   - **PlayStation**: Hold Share + PS button until the light bar flashes
   - **Xbox (BLE)**: Hold the pairing button on top until the Xbox button flashes
   - **8BitDo**: Hold Start/Pair until the LED flashes
   - **Switch Pro**: Hold the sync button on top
3. With nothing connected, the Pico W scans automatically and will connect on its own
4. The board LED shows which state it is in — see the table below
5. The controller appears on the host PC (as an SInput pad by default)

### Board LED

| LED | Meaning |
|-----|---------|
| Fast blink (~0.2 s) | Actively scanning in a pairing window (BOOTSEL click, 60 s) |
| Slow blink (~0.8 s) | Nothing connected — idle, scanning, or connecting |
| Solid on | At least one controller connected |

Note that the slow blink is the *no-device* state, not a "scanning" state; the adapter blinks fast
only during an explicit pairing window.

### Adding a second controller

**Auto-scan only runs while zero controllers are connected.** Both automatic resume paths in
`btstack_host.c` are gated on `btstack_classic_get_connection_count() == 0`, so once one pad is
connected the adapter stops looking for more. To pair an additional controller, **click BOOTSEL**
to open a 60-second scan window (the LED will blink fast), then put the new pad into pairing mode.

## Testing

1. Open a gamepad tester (e.g., [gamepad-tester.com](https://gamepad-tester.com/) or Steam Input)
2. Press buttons and move sticks on the Bluetooth controller
3. Verify all inputs register correctly on the PC
4. Rumble feedback from the PC is forwarded back to the BT controller

## BOOTSEL button controls

The BOOTSEL button is the adapter's entire control surface once it is running. The firmware
prints these at startup over USB serial.

| Action | Effect |
|--------|--------|
| Click | Open a 60-second Bluetooth scan window (pair an additional controller) |
| Double-click | Cycle the USB output mode |
| Triple-click | Reset the output mode to SInput |
| Hold | Disconnect all controllers **and erase all bonds** |

## USB output modes

Double-clicking BOOTSEL cycles through:

```
SInput -> XInput -> PS3 -> PS4 -> Switch -> Keyboard/Mouse -> SInput
```

A freshly flashed board starts in **SInput**, so the first double-click moves it to XInput.
Triple-click jumps straight back to SInput. Less common modes (DInput, PS Classic, Xbox Original,
Xbox One, XAC) are not in the cycle — set those over the CDC config interface. **Each mode change
re-enumerates the board on the USB bus**, so the host will briefly see the device disconnect and
come back.

The selected mode is written to flash and reloaded by `usbd_init()` on every boot, so it survives
power cycles. If a board comes up in an unexpected mode, triple-click to force it back to SInput.

## Button profiles

Hold **SELECT + D-pad Up** (previous) or **SELECT + D-pad Down** (next) together for about
**0.7 s** to step through *button profiles*. The selection clamps at the ends; a quick tap passes
through to the game.

> ⚠️ This switches button **profiles**, not USB output modes — the two are separate controls and
> have been confused in earlier revisions of this doc. **Output modes are BOOTSEL double-click**
> (triple-click forces SInput); there is no D-pad gesture for them.
>
> `bt2usb` registers no combos of its own, so it inherits the router's built-in table
> (`router_install_default_combos()`): SELECT + Up/Down steps profiles and SELECT + Left/Right drives
> the **D-pad output-mode slider** (d-pad → left stick / right stick). Left/Right is therefore *not*
> inert here, which earlier revisions of this doc claimed.

## Notes

- Supports both Bluetooth Classic and BLE controllers
- Bond information persists across power cycles (stored in flash)
- Multiple controllers merge to a single USB output (MERGE_BLEND mode)
- See [bt2usb app docs](../../apps/bt2usb.md) for supported controllers and output modes
- For Pico 2 W builds, use `make bt2usb_pico2_w` instead
