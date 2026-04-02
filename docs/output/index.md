# Output Interfaces

An output interface translates the common `input_event_t` format into a console-specific or USB protocol. Output drivers read from the [router](../core/index.md) and send data to the connected console or host device.

Console outputs use RP2040 PIO state machines for cycle-accurate protocol timing. They run on Core 1, isolated from USB and Bluetooth processing on Core 0. USB and UART outputs run on Core 0 since they are not timing-critical.

## All Output Interfaces

| Output | Protocol | Location | Max Players | PIO Programs | Notes |
|--------|----------|----------|-------------|--------------|-------|
| GameCube / Wii | Joybus | `src/native/device/gamecube/` | 4 | `joybus.pio` | Requires 130MHz overclock. Rumble feedback. Keyboard mode. |
| PCEngine / TurboGrafx-16 | Multiplexed | `src/native/device/pcengine/` | 5 | `plex.pio`, `clock.pio`, `select.pio` | Multitap emulation. 2/3/6-button and mouse modes. |
| Dreamcast | Maple bus | `src/native/device/dreamcast/` | 4 | `maple.pio` | Rumble feedback. Analog triggers. |
| Nuon | Polyface | `src/native/device/nuon/` | 8 | `polyface_read.pio`, `polyface_send.pio` | Tempest 3000 spinner support. In-game reset (IGR). |
| 3DO | Parallel bus (PBUS) | `src/native/device/3do/` | 8 | `sampling.pio`, `output.pio` | Mouse support. Extension passthrough. |
| N64 | Joybus | `src/native/device/n64/` | 1 | `joybus.pio` | Rumble feedback. Experimental. |
| Casio Loopy | Loopy protocol | `src/native/device/loopy/` | 4 | `loopy.pio` | Experimental. |
| Neo Geo / SuperGun | GPIO | `src/native/device/gpio/` | 1 | None | Active-low button output via GPIO pins. |
| NES | Shift register | `src/native/device/nes/` | 1 | PIO | NES controller emulation. |
| USB Device | USB 2.0 HID | `src/usb/usbd/` | 1 | None | 13 output modes (see below). Native USB controller. |
| BLE Peripheral | BLE HID | `src/usb/usbd/` | 1 | None | BLE gamepad emulation. Experimental. |
| UART | Serial TX | `src/native/device/uart/` | 1 | None | Serial bridge to external microcontrollers. |

## How Output Interfaces Work

Each output interface implements the `OutputInterface` struct:

```c
typedef struct {
    const char* name;
    output_target_t target;
    void (*init)(void);
    void (*task)(void);
    void (*core1_task)(void);
    uint8_t (*get_rumble)(void);
    uint8_t (*get_player_led)(void);
} OutputInterface;
```

- `init()` configures PIO programs, GPIO pins, or USB descriptors during startup.
- `core1_task()` runs on Core 1 in a tight loop -- the timing-critical PIO interaction for console protocols. Only one output can claim Core 1.
- `task()` runs on Core 0 each main loop iteration for non-timing-critical work (USB device polling, status updates).
- `get_rumble()` and `get_player_led()` return feedback state that the player manager routes back to input controllers.

## USB Device Output Modes

The USB device output supports 13 emulation modes, selectable at runtime via [web config](../core/web-config.md) or button combo:

| Mode | Emulates | Console Auth |
|------|----------|--------------|
| SInput | Joypad HID Gamepad | -- |
| XInput | Xbox 360 Controller | XSM3 |
| DirectInput | Generic HID Gamepad | -- |
| PS3 | DualShock 3 | -- |
| PS4 | DualShock 4 | Passthrough |
| PS Classic | PS Classic Controller | -- |
| Switch | Nintendo Switch Pro Controller | -- |
| Xbox Original | Controller S | -- |
| Xbox One | Xbox One Controller | -- |
| XAC | Xbox Adaptive Controller | -- |
| GC Adapter | Wii U GameCube Adapter | -- |
| Keyboard + Mouse | HID Keyboard and Mouse | -- |
| MIDI | MIDI Controller | -- |

Mode switching triggers USB re-enumeration. The selected mode persists to flash.

## Feedback

Console outputs can send feedback back to the input controllers:

- **Rumble**: GameCube, Dreamcast, and N64 outputs forward motor commands. The player manager routes these to the correct physical controller (USB SET_REPORT, BT HID output report, N64 rumble pak).
- **Player LEDs**: Some outputs report player numbers, which the player manager forwards to controllers that support player LEDs (Xbox, DualShock 4, DualSense).

## Next Steps

- [Architecture](../overview/architecture.md) -- Where outputs fit in the system
- [Data Flow](../overview/data-flow.md) -- How data reaches outputs from inputs
- [Input Interfaces](../input/index.md) -- The other side of the pipeline
- [Apps](../apps/index.md) -- How apps select which output to use
