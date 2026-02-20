# NEOGEO to USB Adapter

neogeo2usb app turns a KB2040 or RP2040 Zero into a USB controller adapter. Attach DB15/NEOGEO stick and get [USB Output Interface](./USB2USB.md)

## Features

### HotKeys

* **D-Pad Mode Switching (Hold for 2 seconds):**
    * `S1 (Coin) + S2 (Start) + Down`: **Digital D-Pad Mode** (Default). Directions act as standard digital inputs on the D-Pad.
    * `S1 (Coin) + S2 (Start) + Left`: **Left Analog Stick Mode**. Directions are remapped to the Left Analog Stick axes.
    * `S1 (Coin) + S2 (Start) + Right`: **Right Analog Stick Mode**. Directions are remapped to the Right Analog Stick axes.

* **System Actions:**
    * `S1 (Coin) + S2 (Start)`: **Home / Guide Button**.


## Hardware Requirements

- **Board**: Adafruit KB2040 (default), RP2040-Zero
- **Protocol**: Parallel **Active-Low** logic direct from GPIO
- **Internal Pull-ups**: Firmware enables internal RP2040 pull-up resistors; no external resistors are required.
- **Connector**: DB15 male connector.

### Wiring

| KB2040 | RP2040 Zero | DB15 Port | NEOGEO | Joypad |
| :--- | :--- | :--- | :--- | :--- |
| GND | GND | Pin 1 | Ground | - |
| GPIO 9  | GPIO 29 | Pin 2 | Button 6 / K3 | R2 |
| GPIO 7  | GPIO 28 | Pin 3 | Coin | S1 |
| GPIO 5  | GPIO 27 | Pin 4 | Button 4 / K1 | B1 |
| GPIO 3  | GPIO 4  | Pin 5 | Button 2 / P2 | B4 |
| GPIO 18 | GPIO 3  | Pin 6 | Right | D-Pad Right |
| GPIO 19 | GPIO 2  | Pin 7 | Down | D-Pad Down |
| N/C | N/C | Pin 8 | - | - |
| N/C | N/C | Pin 9 | - | - |
| GPIO 8  | GPIO 13 | Pin 10 | Button 5 / K2 | B2 |
| GPIO 6  | GPIO 12 | Pin 11 | Start | S2 |
| GPIO 4  | GPIO 11 | Pin 12 | Button 3 / P3 | R1 |
| GPIO 2  | GPIO 10 | Pin 13 | Button 1 / P1 | B3 |
| GPIO 20 | GPIO 9  | Pin 14 | Left | D-Pad Left |
| GPIO 10 | GPIO 8  | Pin 15 | Up | D-Pad Up |

### RP2040 Zero Wiring Reference

It is possible to connect the RP2040 Zero directly to a DB15 connector by taking advantage of the board design.

**Installation Steps:**

 * Solder pins 10 through 15 of the DB15 directly to GPIO pins 14 through 9 on the bottom of the RP2040 Zero. To minimize risk, solder to the base of the DB15 pins; it is possible to solder in the center, but it requires special care.
 
 * Connect the remaining GPIOs and GND line, using wires as shown in the front and back reference images.

| Front | Back |
| :---: | :---: |
| ![NEOGEO-2-USB RP2040 Zero Front](../images/neogeo2usb_rp2040_zero_front.png) | ![USB-2-NEOGEO RP2040 Zero Back](../images/usb2neogeo_rp2040_zero_back.png) |


## Troubleshooting

**Controller not detected:**

- Check DB15 connections
- Ensure the DB15 connectos is GND on Pin 1.
- Check data pin assignment in firmware


## Product Links

- [GitHub Releases](https://github.com/joypad-ai/joypad-os/releases) - Latest firmware
