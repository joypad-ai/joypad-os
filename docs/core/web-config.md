# Web Config

Web Config is a browser-based configuration tool that connects directly to your Joypad adapter over USB. No software to install — just plug in, open the page, and configure.

**Link:** [config.joypad.ai](https://config.joypad.ai)

---

## Requirements

- **Browser:** Google Chrome or Microsoft Edge (Web Serial API required — Safari and Firefox are **not** supported)
- **Cable:** USB data cable between your adapter and your computer (not charge-only)
- **Mode:** Your adapter must be in **SInput mode** (the default) for the serial port to be visible

---

## Which Adapters Support Web Config?

Web Config works on adapters that output over **USB**. These expose a virtual serial port (CDC) alongside the controller output.

### ✅ Supported — USB Output Adapters

| Adapter | Description |
|---------|-------------|
| **USB2USB** | USB controller → USB output (PC, Xbox 360, PS3, Switch) |
| **BT2USB** | Bluetooth controller → USB output |
| **GC2USB** | GameCube controller → USB output |
| **NES2USB** | NES controller → USB output |
| **SNES2USB** | SNES controller → USB output |
| **N642USB** | N64 controller → USB output |
| **Neo Geo2USB** | Neo Geo controller → USB output |
| **Nuon2USB** | Nuon controller → USB output |
| **Controller** | Custom controller with USB output |

### ❌ Not Yet Supported — Native Console Output Adapters

These adapters output directly to the console over GPIO pins — there is no USB device connection to your computer, so Web Config cannot communicate with them.

| Adapter | Why No Web Config |
|---------|-------------------|
| **USB2GC** | Output is GameCube Joybus protocol over GPIO |
| **USB2N64** | Output is N64 Joybus protocol over GPIO |
| **USB2DC** | Output is Dreamcast Maple bus over GPIO |
| **USB2NES** | Output is NES shift register over GPIO |
| **USB2SNES** | Output is SNES shift register over GPIO |
| **USB23DO** | Output is 3DO PBus protocol over GPIO |
| **USB2PCE** | Output is PC Engine protocol over GPIO |
| **USB2Neo Geo** | Output is Neo Geo direct wiring over GPIO |
| **USB2Nuon** | Output is Nuon Polyface serial over GPIO |
| **BT2N64** | Output is N64 Joybus protocol over GPIO |
| **BT2GC** | Output is GameCube Joybus protocol over GPIO |

!!! info "Coming Soon"
    Web Config support for native console output adapters is planned for a future firmware update.

---

## How to Connect

### Step 1: Put Your Adapter in SInput Mode

Your adapter must be in the **default SInput mode** for Web Config to work. If you've switched to another output mode (XInput, PS4, Switch, etc.), the virtual serial port won't be visible to your browser.

**To return to SInput mode:** Triple-click the user button on your adapter.

!!! tip
    SInput is the default mode when you first flash the firmware. If you haven't changed modes, you're already there.

### Step 2: Open Web Config

Go to [config.joypad.ai](https://config.joypad.ai) in **Chrome** or **Edge**.

### Step 3: Click Connect

Click the **Connect** button. A browser dialog will appear listing available serial ports. Select your Joypad device and click **Connect**.

### Step 4: Configure

Once connected, the status indicator turns green and you'll see your device info and all available settings.

---

## What You Can Configure

### Output Mode

Switch between output modes without reflashing firmware:

| Mode | Description |
|------|-------------|
| **SInput** | Default composite mode — works on most platforms |
| **XInput** | Xbox 360 compatible |
| **DInput** | Generic DirectInput |
| **PS3** | PlayStation 3 |
| **PS4** | PlayStation 4 (may require auth passthrough) |
| **Switch** | Nintendo Switch Pro Controller |
| **Xbox Original** | Original Xbox (XID) |
| **Xbox One** | Xbox One (WIP) |
| **PS Classic** | PlayStation Classic |
| **KB/Mouse** | Keyboard and mouse emulation |
| **GC Adapter** | GameCube adapter mode |
| **XAC** | Xbox Adaptive Controller compatibility |
| **PCE Mini** | PC Engine Mini |

### Profiles

Create up to **4 custom profiles** in addition to the built-in profiles.

Each profile can configure:

- **Button remapping** — remap any button to any other button, or disable individual buttons
- **Stick sensitivity** — adjust left and right analog stick sensitivity (0–200%)
- **SOCD modes** — choose how simultaneous opposite cardinal directions (SOCD) are handled:
    - **Passthrough** — both directions sent as-is
    - **Neutral** — opposite directions cancel out
    - **Up Priority** — up always wins over down
    - **Last Win** — last direction pressed wins
- **Swap sticks** — swap left and right analog stick axes
- **Invert Y axes** — invert left and/or right stick Y axis

!!! tip
    You can **clone** any built-in profile to create a custom version with your own remapping.

### Input Stream

Enable live input visualization to see exactly what buttons and axes your controller is sending in real-time. Useful for testing controllers and debugging input issues.

### Bluetooth Management

- **Clear Bluetooth bonds** — removes all paired devices, forcing re-pairing on next connection

### Wiimote Orientation

Set the Wii Remote orientation mode (horizontal vs. vertical) for correct button and motion mapping.

### Device Management

| Action | Description |
|--------|-------------|
| **Reboot** | Restart the adapter |
| **Bootloader (BOOTSEL)** | Enter USB mass storage mode for firmware flashing |
| **Factory Reset** | Clear all settings, profiles, and bonds — return to defaults |

---

## Troubleshooting

### No device shows up when I click Connect

1. **Wrong mode** — Make sure your adapter is in SInput mode. Triple-click the user button to cycle back.
2. **Wrong browser** — Web Serial only works in **Chrome** and **Edge**. Safari and Firefox are not supported.
3. **Bad cable** — Use a USB data cable, not a charge-only cable.
4. **Driver conflict** (Windows) — Some controller drivers claim the serial port. Try unplugging other USB devices.

### I'm using USB2GC / BT2N64 / USB2DC and it doesn't connect

Native console output adapters don't support Web Config yet. These adapters communicate with the console over GPIO pins, not USB — so there's no serial port for the browser to connect to. See the [compatibility table](#which-adapters-support-web-config) above.

### I changed the output mode and now I can't reconnect

Some output modes don't expose the CDC serial port. Triple-click the user button to return to SInput mode, then reconnect.

### Device was working but stopped responding

Disconnect in the browser, unplug the USB cable, plug it back in, and reconnect.

### Debug Stream

Enable the **Debug Stream** button in Web Config to see firmware log output in real-time. This is helpful for diagnosing controller detection issues or reporting bugs.
