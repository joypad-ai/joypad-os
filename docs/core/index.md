# Joypad Core

Joypad Core is the shared firmware layer that sits between inputs and outputs. It provides the router, player management, button remapping, storage, and other services that every app relies on. Core services are platform-independent -- they work the same on RP2040, ESP32-S3, and nRF52840.

All core services live in `src/core/`.

## Services Overview

| Service | Location | Purpose |
|---------|----------|---------|
| **Router** | `src/core/router/` | Routes input events to output slots. The central data plane. |
| **Players** | `src/core/services/players/` | Maps controllers to player slots. Routes feedback (rumble, LEDs) back to controllers. |
| **Profiles** | `src/core/services/profiles/` | Button remapping. Apps define profiles; users cycle them at runtime. |
| **Storage** | `src/core/services/storage/` | Flash persistence for settings, profiles, and Bluetooth bonds. |
| **LEDs** | `src/core/services/leds/` | NeoPixel RGB status indicators and profile color feedback. |
| **Hotkeys** | `src/core/services/hotkeys/` | Button combo detection (e.g., L1+R1+Start+Select for in-game reset). |
| **Codes** | `src/core/services/codes/` | Button sequence recognition (e.g., Konami code). |
| **Display** | `src/core/services/display/` | I2C OLED/LCD output (SSD1306). |
| **Button** | `src/core/services/button/` | Board button events: click, double-click, triple-click, hold. |
| **Speaker** | `src/core/services/speaker/` | Audio feedback via PWM. |

## Router

The router is the central data plane. It receives `input_event_t` events from input drivers, applies transforms and profiles, and stores the result for output drivers to read.

**Submit path** (`router_submit_input`): Transform, Profile, Merge, Store, Tap -- all inline, no queuing.

**Read path** (`router_get_output`): Returns a pointer to internal storage. Zero-copy, lock-free.

**Routing modes:**

- **SIMPLE** -- 1:1 mapping. Controller N goes to player slot N. Used by most console adapters.
- **MERGE** -- All controllers merge into slot 0. Used by bt2usb and copilot modes.
- **BROADCAST** -- Every input goes to every slot.
- **CONFIGURABLE** -- User-defined route table.

**Merge modes** (when multiple inputs target the same slot):

- **BLEND** -- OR all button states together.
- **PRIORITY** -- Highest-priority input wins.
- **ALL** -- Most recently active input wins.

See [Data Flow](../overview/data-flow.md) for a detailed walkthrough of the router pipeline.

## Players

The player manager tracks the mapping from physical devices to player slots and handles feedback routing.

**Slot modes:**

- **SHIFT** -- Players shift up when someone disconnects (PCEngine, 3DO). If Player 2 disconnects, Player 3 becomes Player 2.
- **FIXED** -- Players keep their assigned slots (GameCube 4-port). If Player 2 disconnects, slot 2 stays empty.

**Feedback state machine:** When an output reports rumble or LED state, `players_task()` routes that feedback back to the correct input controller by device address and instance. This enables cross-stack feedback -- a Bluetooth DualSense vibrates when a GameCube game triggers rumble.

## Profiles

Button remapping profiles are defined per-app in `profiles.h`. Each profile is a table that maps input buttons and analog axes to output buttons and values.

**Capabilities:**

- 1:1 button remapping
- Button combos (multiple inputs produce one output)
- Analog targets (button press produces specific analog value)
- Analog sensitivity scaling

**User interaction:**

- Hold SELECT + D-pad Up for 2 seconds to cycle to the next profile.
- Hold SELECT + D-pad Down for 2 seconds to cycle to the previous profile.
- The NeoPixel LED flashes to confirm the change.
- The selected profile persists to flash and survives power cycles.

## Storage

Abstracts flash persistence across all three platforms:

| Platform | Backend | Safety |
|----------|---------|--------|
| RP2040 | Last 4KB of flash | `flash_safe_execute` for dual-core coordination |
| ESP32-S3 | NVS (Non-Volatile Storage) | Mutex-protected |
| nRF52840 | Zephyr NVS | Mutex-protected |

**Write throttling:** Changes are debounced with a 5-second delay before writing to flash. This reduces flash wear (RP2040 flash supports roughly 100K write cycles).

**What gets stored:** Active profile selection, USB output mode, Bluetooth bonds, and app-specific settings.

## LEDs

NeoPixel RGB LED provides visual feedback:

- **Connection status** -- Color indicates whether controllers are connected.
- **Profile changes** -- Flashes a profile-specific color when the user cycles profiles.
- **Player assignment** -- Can show player number via color.

The LED service handles PIO-based WS2812 communication on RP2040.

## Hotkeys

Detects specific button combinations held simultaneously. Apps register hotkey callbacks for actions like:

- In-game reset (IGR) -- e.g., L1+R1+Start+Select on Nuon
- Mode switching
- Profile cycling (handled by the profile service)

## Codes

Detects button sequences (ordered presses, not simultaneous holds). Can be used for special feature activation.

## Display

Drives I2C OLED/LCD displays (SSD1306) for showing status information, controller names, profile names, and other runtime data.

## Button

Processes the board's physical button (BOOTSEL or dedicated GPIO button) into events: single click, double click, triple click, and long hold. Apps can register callbacks for each event type.

## Speaker

Generates audio feedback through PWM output. Used for confirmation tones on profile changes and other events.

## How Services Run

All services have a `*_task()` function called from the Core 0 main loop:

```
while (1) {
    leds_task();
    players_task();
    storage_task();
    // ... then input tasks, output tasks, app_task()
}
```

Services run every loop iteration alongside input and output polling. They do not use separate threads or interrupts (on RP2040). On ESP32-S3 and nRF52840, services run within the main RTOS task.

## Deep Dives

- [Router](router.md) -- Routing modes, merge modes, transform pipeline, step-by-step walkthrough
- [Buttons](buttons.md) -- JP_BUTTON_* reference table, analog axes, layout transforms
- [Profiles](profiles.md) -- Button remapping, trigger config, SOCD, profile cycling UX
- [Players](players.md) -- Slot modes, auto-assign, feedback routing
- [Storage](storage.md) -- Flash persistence, journaled writes, write throttling
- [LEDs](leds.md) -- NeoPixel patterns, board LED fallback, profile indication
- [Platform HAL](platform-hal.md) -- Time, identity, reboot across RP2040/ESP32/nRF

## Next Steps

- [Data Flow](../overview/data-flow.md) -- The full input-to-output pipeline
- [Architecture](../overview/architecture.md) -- Where core fits in the layer model
- [Apps](../apps/index.md) -- How apps configure core services
- [Glossary](../overview/glossary.md) -- Key terms defined
