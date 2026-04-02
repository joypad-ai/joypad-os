# Bluetooth to USB Adapter (Pico W)

Bluetooth controllers to USB HID gamepad via Raspberry Pi Pico W. No soldering required.

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

Output file: `releases/joypad_<commit>_bt2usb_rpi_pico_w.uf2`

Alternatively, drag and drop the `.uf2` file onto the `RPI-RP2` drive that appears when the Pico W is in bootloader mode.

## Pairing a Controller

1. Plug the flashed Pico W into a PC or other USB host
2. Put your Bluetooth controller into pairing mode:
   - **PlayStation**: Hold Share + PS button until the light bar flashes
   - **Xbox (BLE)**: Hold the pairing button on top until the Xbox button flashes
   - **8BitDo**: Hold Start/Pair until the LED flashes
   - **Switch Pro**: Hold the sync button on top
3. The Pico W scans continuously and will connect automatically
4. The board LED blinks slowly while scanning, then turns solid when connected
5. The controller appears as a USB HID gamepad on the host PC

## Testing

1. Open a gamepad tester (e.g., [gamepad-tester.com](https://gamepad-tester.com/) or Steam Input)
2. Press buttons and move sticks on the Bluetooth controller
3. Verify all inputs register correctly on the PC
4. Rumble feedback from the PC is forwarded back to the BT controller

## Notes

- Supports both Bluetooth Classic and BLE controllers
- Bond information persists across power cycles (stored in flash)
- Multiple controllers merge to a single USB output (MERGE_BLEND mode)
- Profile cycling: hold SELECT + D-pad Up/Down for 2 seconds to switch USB output modes
- See [bt2usb app docs](../../apps/bt2usb.md) for supported controllers and output modes
- For Pico 2 W builds, use `make bt2usb_pico2_w` instead
