# USB4AMI User Guide

## What is USB4AMI?

USB4AMI is a small adapter that lets you use modern USB controllers and mice with classic computers including the Commodore Amiga, Commodore 64, and Atari computers. It plugs directly into your computer's joystick/mouse port and translates input from any compatible USB device into the signals your retro computer understands.

---

## Supported Platforms

USB4AMI supports three modes, selected via the BOOTSEL button. The table below shows which mode to use for each computer.

| Mode | LED Color | Supported Computers |
|------|-----------|-------------------|
| **Amiga** | Amber | Commodore Amiga (all models including CD32 console) |
| **C64** | Blue | Commodore 64 (including Ultimate64), Commodore 128, MEGA65 |
| **Atari** | Green | Atari ST (all), Atari Falcon, Atari 8-bit computers (all) |

---

## What's in the Box

- USB4AMI adapter

---

## Connecting USB4AMI

1. Plug USB4AMI directly into your retro computer's joystick/mouse port.
2. Plug your USB controller or mouse into the USB-A port on USB4AMI.
3. Power on your computer.

> **Note:** USB4AMI is powered by your computer. No separate power supply is needed.

---

## LED Colors

The LED on USB4AMI tells you what's going on at a glance.

| Color | Meaning |
|-------|---------|
| Breathing (any color) | Waiting for a USB device to connect |
| **Amber** | Amiga mode active |
| **Blue** | C64 mode active |
| **Green** | Atari mode active |
| **Purple** | DPI adjustment mode active |

---

## Choosing Your Platform

USB4AMI needs to know which computer it's connected to. You select your platform by tapping the BOOTSEL button within the first **8 seconds** after powering on.

**How to do it:**
1. Power on your computer with USB4AMI connected.
2. Within 8 seconds, tap the BOOTSEL button to cycle through modes:
   - First tap → C64 (blue)
   - Second tap → Atari (green)
   - Third tap → back to Amiga (amber)
3. Stop tapping when your platform's color appears.

Your choice is automatically saved and remembered next time you power on.

> **Tip:** If you don't tap anything within 8 seconds, USB4AMI stays on your last saved platform.

---

## Using a Gamepad or Joystick

USB4AMI works with most USB gamepads including Xbox, PlayStation, Nintendo Switch, and 8BitDo controllers, as well as most generic USB HID gamepads.

### Basic Controls
- **D-pad or left analog stick** → directional movement
- **B button (or equivalent)** → fire button

### Turbo Fire
Hold **Select** and press a face button to toggle turbo fire on that button. The LED will blink 2 times every 3 seconds to indicate turbo is enabled. Press the same combination again to turn turbo off.

### Up as Jump Profile
Some games do not have a dedicated button for jump instead they use the up direction. USB4AMI includes an alternate control profile that maps the second button to trigger the up direction, making it act as a jump button.

**To toggle the Up as Jump profile:**
- Hold **Select + L1 or R1** — the LED will blink once to confirm

The profile alternates between standard and Up as Jump each time you use the combo. The setting resets to standard on power cycle.

> **Note:** Turbo fire is not available on the jump button while Up as Jump is active.

### CD32 Controllers (Amiga only)
USB4AMI automatically detects and enables the full CD32 button layout when connected to an Amiga. No setup required. Just like an original CD32 controller, it will gracefully fall back to 1 or 2 button mode for games that don't support the CD32's extra buttons.

**CD32 button layout:**
- **B button (South)** → Red (Fire)
- **A button (East)** → Blue
- **Y button (North)** → Green
- **X button (West)** → Yellow
- **L1 / R1** → Left and Right shoulder buttons
- **Start** → Pause

> **Note:** When in CD32 mode, if you have a gamepad with dual analog sticks, the controls are duplicated to them. The dpad is duplicated to the left analog stick and the right analog stick is mapped to the 4 CD32 face buttons. This allows for games that use the 4 CD32 face buttons to play as dual stick shooters! Check out Rogue Declan and Cecconoid for examples of this. 

---

## Using a Mouse

### Amiga
Connect any USB mouse and it will work as a standard quadrature mouse. Left and right buttons work as expected.

### Atari
Connect any USB mouse and it will work as a standard Atari quadrature mouse. Left and right buttons work as expected.

### Commodore 64 (C1351 Mode)
On C64, USB4AMI emulates a Commodore 1351 proportional mouse. This gives smooth, analog-style cursor movement in software that supports it, such as GEOS.

- **Left mouse button** → left button
- **Right mouse button** → right button

---

## Adjusting Mouse Speed (DPI)

If your mouse feels too fast or too slow, you can adjust the sensitivity without any tools or software. USB4AMI uses a DPI divisor to scale mouse movement — a lower divisor means faster movement, a higher divisor means slower movement. The divisor range is **1–8**, with a default of **2**.

**How to adjust:**
1. Connect your USB mouse (LED shows your platform color).
2. **Hold the middle mouse button for 2 seconds.** The LED turns **purple** to confirm you're in DPI adjustment mode.
3. While the LED is purple:
   - **Left mouse button** → faster (decreases the divisor)
   - **Right mouse button** → slower (increases the divisor)
4. **Tap the middle mouse button** to exit and save your setting.

The LED returns to your platform color and your new setting is saved automatically.

> **Tip:** DPI is saved separately for each platform, so you can have different speeds set for Amiga, C64, and Atari.

---

## Updating Firmware

1. Download the latest `.uf2` for USB4AMI from [Releases](https://github.com/thgill/joypad-os/releases)
2. **Disconnect USB4AMI from your retro computer first**
3. Hold the BOOTSEL button and connect the USB-A cable to your computer
4. Drag the `.uf2` file onto the `RP2350` drive that appears
5. Done — the drive ejects and your adapter is running the new firmware

> **Note:** USB4AMI automatically resets its settings to safe defaults after a firmware update. Your platform selection and DPI settings will need to be reconfigured.

---

## Troubleshooting

**The adapter doesn't seem to be doing anything.**
Check the LED color to confirm which platform is active. If needed, power cycle and tap BOOTSEL within 8 seconds to cycle to your desired platform.

**My mouse or gamepad isn't being recognized.**
Most modern gamepads and mice work out of the box. Some very old or unusual USB devices may not be supported — try a different USB device.

**The computer behaves strangely after a firmware update.**
USB4AMI automatically resets its settings to safe defaults the first time it boots after a firmware update. If you experience issues, tap BOOTSEL within the first 8 seconds to cycle to your desired platform, which will rewrite your settings cleanly.

**The cursor moves in the wrong direction.**
Make sure you're in the correct platform mode. Amiga and Atari use different quadrature encodings — confirm the LED color matches your computer.

**I accidentally changed the DPI and want to reset it.**
Enter DPI adjustment mode (hold MMB 2 seconds), adjust until the speed feels right, then tap MMB to save. The default divisor is 2.

**A CD32 game isn't recognizing all buttons after switching control profiles.**
Exit the game and relaunch it. CD32 games detect the controller type at startup — relaunching allows USB4AMI to respond correctly to the CD32 detection handshake.

---

## Coming Soon

We are actively working on the following features for a future firmware update:

- **Scroll wheel support** — Amiga and C64 scroll wheel support is under investigation. This requires coordination with Amiga and C64 driver software and is a non-trivial implementation. We will announce when this is available.
- **Atari 7800 support** — The Atari 7800 uses a unique controller port protocol. We are investigating compatibility.

---

## Technical Notes

- USB4AMI remembers your platform and DPI settings across power cycles.
- Settings are automatically reset to defaults the first time USB4AMI boots after a firmware update.
- The BOOTSEL button is only active for platform selection during the first 8 seconds after power-on.
- The Up as Jump profile is not saved across power cycles and resets to standard on each boot.
