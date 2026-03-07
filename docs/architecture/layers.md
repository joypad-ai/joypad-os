# Architecture Layers

Developer reference for Joypad OS internals — layer boundaries, key interfaces, data flow, and runtime behavior.

## Layer Diagram

```
┌─────────────────────────────────────────────────────────────┐
│  APP                                                         │
│  src/apps/*/app.c — declarative wiring                      │
│  Selects inputs, outputs, routing mode, profiles            │
├─────────────────────────────────────────────────────────────┤
│  MAIN LOOP                                                   │
│  src/main.c — dual-core orchestration                       │
│  Core 0: services → inputs → outputs → app_task()           │
│  Core 1: timing-critical output (PIO console protocols)     │
├─────────────────────────────────────────────────────────────┤
│  CORE SERVICES                                               │
│  src/core/services/ — platform-independent middleware       │
│  Players, Profiles, Storage, LEDs, Hotkeys, Codes,          │
│  Display, Button, Speaker                                    │
├─────────────────────────────────────────────────────────────┤
│  ROUTER                                                      │
│  src/core/router/ — zero-latency N:M event routing          │
│  submit → transform → profile → merge → store → tap         │
│  Modes: SIMPLE (1:1), MERGE (N:1), BROADCAST (1:N)         │
├────────────────────────┬────────────────────────────────────┤
│  INPUT                 │  OUTPUT                             │
│  → router_submit_input │  ← router_get_output               │
│                        │                                     │
│  USB Host (usbh/)      │  Console Devices (native/device/)  │
│  Bluetooth (bt/)       │  USB Device (usbd/)                │
│  Native Host           │  UART / GPIO                       │
│  WiFi (wifi/)          │                                     │
├────────────────────────┴────────────────────────────────────┤
│  PLATFORM HAL                                                │
│  src/platform/platform.h                                     │
│  RP2040: pico-sdk  |  ESP32: ESP-IDF  |  nRF52840: Zephyr  │
├─────────────────────────────────────────────────────────────┤
│  LIBRARIES                                                   │
│  src/lib/ — TinyUSB, BTstack, joybus-pio, tusb_xinput       │
└─────────────────────────────────────────────────────────────┘
```

Dependencies flow downward. Each layer only calls into the layer below it, with two exceptions: input drivers call up into the router via `router_submit_input()`, and outputs call up via `router_get_output()`.

---

## App Layer

**Location:** `src/apps/*/app.c`, `src/apps/*/app.h`, `src/apps/*/profiles.h`

Apps are declarative — they wire together inputs, outputs, and services without implementing control flow. The main loop handles orchestration.

```c
// app.h — compile-time configuration
#define APP_VERSION          "1.0.0"
#define ROUTING_MODE         ROUTING_SIMPLE
#define MERGE_MODE           MERGE_BLEND
#define MAX_PLAYER_SLOTS     4
#define TRANSFORM_FLAGS      TRANSFORM_MOUSE_TO_ANALOG

// app.c — runtime wiring
const InputInterface** app_get_input_interfaces(uint8_t* count);
const OutputInterface** app_get_output_interfaces(uint8_t* count);
void app_init(void);       // Configure router, register profiles
void app_task(void);       // Per-loop app logic (optional)
```

**What apps declare:**

- Input interface array (e.g., `{&usb_host_interface, &bt_interface}`)
- Output interface array (e.g., `{&gamecube_output_interface}`)
- Router config: routing mode, merge mode, max players per output
- Transform flags: mouse-to-analog, spinner accumulation, instance merging
- Profiles: console-specific button remapping tables (in `profiles.h`)
- Button/hotkey callbacks: profile cycling, IGR, mode switching

**Examples:**

| App | Inputs | Outputs | Routing | Players |
|-----|--------|---------|---------|---------|
| `usb2gc` | USB host, BT | GameCube joybus | SIMPLE | 4 |
| `bt2usb` | BT only | USB device | MERGE | 1 |
| `usb23do` | USB host, BT | 3DO PBUS | SIMPLE | 8 |
| `n642usb` | N64 native | USB device | SIMPLE | 1 |
| `wifi2usb` | WiFi (JOCP) | USB device | MERGE | 1 |

---

## Main Loop

**Location:** `src/main.c`

Orchestrates dual-core execution on RP2040. ESP32 and nRF52840 have platform-specific entry points (`esp/main/main.c`, `nrf/src/main.c`) that follow the same pattern with RTOS threads instead of bare-metal cores.

### Core 0 Loop

```c
while (1) {
    // 1. Services
    leds_task();
    players_task();
    storage_task();

    // 2. Poll all inputs (feeds router)
    for (each input_interface)
        input->task();

    // 3. Poll all outputs (reads from router)
    for (each output_interface)
        output->task();

    // 4. App-specific logic
    app_task();
}
```

Input polling runs before output polling — outputs always read the freshest data from the current iteration.

### Core 1

Core 1 runs a single output's `core1_task()` — the timing-critical PIO loop for console protocols (GameCube joybus, Dreamcast maple bus, etc.). Only one output can claim Core 1.

### Initialization Sequence

```
1. stdio_init_all()                    — UART debug output
2. multicore_launch_core1()            — Launch Core 1 early
3. Core 1: flash_safe_execute_core_init() — Flash safety barrier
4. Core 1: wait for task assignment     — Spins on __wfe()
5. leds_init(), storage_init(), players_init() — Services
6. app_init()                           — Router config, profiles
7. input_interface[].init()             — USB host, BT, native, WiFi
8. output_interface[].init()            — PIO setup, USB descriptors
9. Assign core1_task, signal __sev()    — Core 1 starts running
10. Enter core0_main() loop             — Never returns
```

Core 1 launches early because `flash_safe_execute` requires both cores to coordinate before any flash writes (pico-sdk requirement). Core 1 then idles until step 9 gives it work.

---

## Core Services

**Location:** `src/core/services/`

Platform-independent middleware. Services don't depend on specific inputs or outputs — they operate on the common `input_event_t` and abstract interfaces.

| Service | Location | Purpose |
|---------|----------|---------|
| **Players** | `services/players/` | Device-to-slot mapping, feedback state machine |
| **Profiles** | `services/profiles/` | Button remapping, analog mapping, profile cycling |
| **Storage** | `services/storage/` | Flash/NVS persistence with write throttling |
| **LEDs** | `services/leds/` | NeoPixel status, profile color feedback |
| **Hotkeys** | `services/hotkeys/` | Button combo detection (e.g., IGR) |
| **Codes** | `services/codes/` | Button sequence recognition |
| **Display** | `services/display/` | I2C OLED/LCD (SSD1306) |
| **Button** | `services/button/` | Board button events (click, double, triple, hold) |
| **Speaker** | `services/speaker/` | Audio feedback via PWM |

### Players

Manages the mapping from physical devices to player slots.

```c
// Slot modes
PLAYER_MODE_SHIFT  — Players shift up on disconnect (3DO, PCEngine)
PLAYER_MODE_FIXED  — Players keep assigned slots (GameCube 4-port)
```

The feedback state machine in `players_task()` handles rumble/LED forwarding — when an output reports rumble, the player manager routes it back to the correct input controller.

### Profiles

Button remapping profiles are defined per-app in `profiles.h`. Each profile maps input buttons to output buttons, with support for:

- 1:1 button remapping
- Button combos (multiple inputs → single output)
- Analog targets (button → analog axis value)
- Analog sensitivity scaling

Profile cycling: SELECT + D-pad Up/Down (held 2 seconds). Selection persists to flash via storage.

### Storage

Abstracts flash persistence across platforms:

- **RP2040**: Last 4KB of flash, with `flash_safe_execute` for dual-core safety
- **ESP32**: NVS (Non-Volatile Storage)
- **nRF52840**: Zephyr NVS

Write throttling: 5-second debounce after changes to reduce flash wear (~100K write cycles available).

---

## Router

**Location:** `src/core/router/router.h`, `src/core/router/router.c`

The router is the central data plane. It's zero-latency and lock-free on the read path.

### Submit Pipeline

When any input driver calls `router_submit_input(&event)`:

```
1. TRANSFORM     — Mouse→analog, instance merge, spinner accumulation
2. PROFILE       — Apply active button remapping profile
3. MERGE         — Blend with other inputs targeting the same output+slot
4. STORE         — Write to router_outputs[target][slot] (atomic)
5. TAP           — Call push callbacks (for outputs that need immediate notification)
```

### Read Path

Output devices call `router_get_output(target, slot)` to read the latest state. This returns a pointer to internal storage — zero-copy, no mutex.

### Routing Modes

```c
ROUTING_SIMPLE      — Device N → slot N (most console adapters)
ROUTING_MERGE       — All devices → slot 0 (bt2usb, copilot mode)
ROUTING_BROADCAST   — Device 0 → all slots
ROUTING_CONFIGURABLE — User-defined route table (N:M)
```

### Merge Modes

When multiple inputs target the same output slot:

```c
MERGE_PRIORITY  — Highest-priority input wins
MERGE_BLEND     — OR all button states together (copilot)
MERGE_ALL       — Latest active input wins
```

### Router Configuration

```c
typedef struct {
    routing_mode_t mode;
    merge_mode_t merge_mode;
    uint8_t max_players_per_output[];  // GC=4, 3DO=8, PCE=5
    uint8_t transform_flags;           // Bitfield of transforms
} router_config_t;
```

---

## Input Layer

**Location:** `src/usb/usbh/`, `src/bt/`, `src/native/host/`, `src/wifi/`

All input sources implement `InputInterface` and submit events via `router_submit_input()`.

### InputInterface

```c
typedef struct {
    const char* name;
    input_source_t source;     // INPUT_SOURCE_USB, INPUT_SOURCE_BT, etc.
    void (*init)(void);
    void (*task)(void);        // Called each Core 0 loop iteration
    bool (*is_connected)(void);
    uint8_t (*get_device_count)(void);
} InputInterface;
```

### input_event_t

The common input format all drivers normalize to:

```c
typedef struct {
    // Device identity
    uint8_t dev_addr;
    int8_t instance;
    input_device_type_t type;     // GAMEPAD, MOUSE, KEYBOARD
    input_transport_t transport;  // USB, BT_CLASSIC, BLE, WIFI, NATIVE

    // Digital
    uint32_t buttons;             // JP_BUTTON_* bitmap (W3C order)

    // Analog (all normalized: 0-255, 128=center)
    uint8_t analog[7];            // LX, LY, RX, RY, L2, R2, RZ

    // Mouse
    int8_t delta_x, delta_y;

    // Extended (motion, touch, battery, etc.)
    // ...
} input_event_t;
```

### USB Host (`src/usb/usbh/`)

- TinyUSB host stack handles enumeration and polling
- Generic HID parser for standard gamepads
- Vendor-specific drivers in `usbh/hid/devices/vendors/`: Microsoft (Xbox), Sony (PS3/4/5), Nintendo (Switch), 8BitDo, HORI, Logitech, Sega, Raphnet
- XInput protocol in `usbh/xinput/`
- Feedback path: `usbh_task()` checks for pending rumble/LED from the output side and sends to controllers

### Bluetooth (`src/bt/`)

Three sub-layers:

```
bt/transport/     — Platform-specific HCI transport
                    bt_transport_cyw43.c (Pico W)
                    bt_transport_esp32.c (ESP32-S3)
                    (nRF52840 uses Zephyr raw HCI)

bt/btstack/       — BTstack host integration
                    btstack_host.c — scanning, pairing, connection mgmt

bt/bthid/         — BT HID device drivers
                    Per-vendor drivers (same structure as USB)
                    router_submit_input() on report
```

### Native Host (`src/native/host/`)

Direct controller reading via PIO or GPIO:

| Protocol | Method | Polling Rate | Notes |
|----------|--------|-------------|-------|
| SNES | GPIO polling (SNESpad lib) | Per-loop | Also NES, SNES mouse, Xband keyboard |
| N64 | Joybus PIO | 60Hz | Rumble pak support, Y-axis inverted |
| GameCube | Joybus PIO | 125Hz | Rumble support, Y-axis inverted |
| Neo Geo | GPIO polling (active-low) | Per-loop | Internal pull-ups |

### WiFi (`src/wifi/`)

JOCP (Joypad Open Controller Protocol) on Pico W:

- Adapter runs as WiFi AP
- DHCP server assigns IPs
- UDP port 30100: input packets (low-latency)
- TCP port 30101: control channel (capability negotiation, time sync)

---

## Output Layer

**Location:** `src/native/device/`, `src/usb/usbd/`

All output targets implement `OutputInterface` and read from the router via `router_get_output()`.

### OutputInterface

```c
typedef struct {
    const char* name;
    output_target_t target;
    void (*init)(void);
    void (*task)(void);            // Core 0 periodic task
    void (*core1_task)(void);      // Core 1 timing-critical loop (optional)
    uint8_t (*get_rumble)(void);   // Feedback: rumble state
    uint8_t (*get_player_led)(void); // Feedback: player LED assignment
} OutputInterface;
```

### Console Outputs (Core 1)

Each console protocol runs as a `core1_task()` — a tight PIO loop that must not be interrupted:

| Console | PIO Programs | Clock | Players |
|---------|-------------|-------|---------|
| GameCube | `joybus.pio` | 130MHz (overclocked) | 4 |
| PCEngine | `plex.pio`, `clock.pio`, `select.pio` | Default | 5 |
| Dreamcast | `maple.pio` | Default | 4 |
| Nuon | `polyface_read.pio`, `polyface_send.pio` | Default | 8 |
| 3DO | `sampling.pio`, `output.pio` | Default | 8 |
| Loopy | `loopy.pio` | Default | 4 |

### USB Device Output (`src/usb/usbd/`)

Runs on Core 0 (not timing-critical). TinyUSB device stack with 13 output modes:

| Mode | Emulates | Auth |
|------|----------|------|
| SInput | Joypad HID Gamepad | — |
| XInput | Xbox 360 Controller | XSM3 |
| PS3 | DualShock 3 | — |
| PS4 | DualShock 4 | Passthrough |
| Switch | Pro Controller | — |
| KB/Mouse | HID Keyboard + Mouse | — |
| Xbox Original | Controller S | — |
| Xbox One | Xbox One Controller | — |
| XAC | Xbox Adaptive Controller | — |
| PS Classic | PS Classic Controller | — |
| GC Adapter | Wii U GC Adapter | — |

Mode switching triggers USB re-enumeration. Selection persists to flash.

---

## Feedback Loop

Feedback (rumble, LEDs, RGB) flows backward through the system:

```
Console/USB host sends rumble command
  → OutputInterface.get_rumble() returns motor values
  → players_task() reads feedback per player slot
  → Routes to correct InputInterface by dev_addr/instance
  → Input driver sends rumble to physical controller
    (USB: SET_REPORT, BT: HID output report, N64: rumble pak)
```

This is why a DualSense connected via Bluetooth vibrates when a GameCube game triggers rumble — the feedback loop crosses the entire stack.

---

## Platform HAL

**Location:** `src/platform/platform.h`

Thin abstraction over platform-specific APIs:

```c
uint32_t platform_time_ms(void);
uint32_t platform_time_us(void);
void platform_sleep_ms(uint32_t ms);
void platform_get_serial(char* buf, size_t len);
void platform_reboot(void);
void platform_reboot_bootloader(void);
```

| Platform | Implementation | RTOS | Main Loop |
|----------|---------------|------|-----------|
| RP2040 | `platform/rp2040/platform_rp2040.c` | Bare metal | `while(1)` on Core 0 |
| ESP32-S3 | `esp/main/main.c` | FreeRTOS | Task with `tud_task_ext(1, false)` |
| nRF52840 | `nrf/src/main.c` | Zephyr | Thread with `k_msleep(1)` |

On RP2040, timing-critical code is placed in RAM with `__not_in_flash_func` to avoid XIP cache misses during PIO operations.

---

## Libraries

**Location:** `src/lib/`

| Library | Version | Used By |
|---------|---------|---------|
| **TinyUSB** | 0.19.0 | USB host (input), USB device (output) |
| **BTstack** | — | Bluetooth host (Classic BT + BLE) |
| **joybus-pio** | — | GameCube/N64 joybus protocol |
| **Pico-PIO-USB** | — | PIO-based USB host (RP2040 boards without native USB-A) |
| **tusb_xinput** | — | Xbox XInput protocol descriptors |
| **pico-sdk** | 2.2.0 | RP2040 hardware abstraction |

---

## Latency Design

The architecture is optimized for minimum input-to-output latency:

1. **Input before output** — Core 0 polls inputs, then outputs. Outputs always read data from the current loop iteration, not the previous one.
2. **Zero-copy router** — `router_get_output()` returns a pointer to internal state. No memcpy on the read path.
3. **No mutexes on critical path** — Router writes are atomic (single-writer from Core 0). Core 1 reads are lock-free.
4. **No queuing** — `router_submit_input()` processes the event inline (transform, profile, merge, store). No event queue between input and router.
5. **RAM placement** — `__not_in_flash_func` keeps PIO interaction code in SRAM, avoiding XIP flash cache misses.
6. **Core isolation** — Console protocol PIO loops run on Core 1 with no interruption from USB/BT processing.
