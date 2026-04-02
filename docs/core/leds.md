# LED Indicators

The LED subsystem provides visual feedback for connection status and profile changes. It supports both WS2812 NeoPixel RGB LEDs and plain GPIO board LEDs as a fallback.

Source: `src/core/services/leds/leds.h`, `leds.c`, and `neopixel/ws2812.c`

## Architecture

```
leds_task()
   |
   +-- board_led_task()     (plain GPIO, when BOARD_LED_PIN defined)
   +-- neopixel_task()      (WS2812 RGB via PIO)
```

Both LED types run in parallel when available. The LED task is called every main loop iteration on Core 0.

## NeoPixel Status Patterns

The NeoPixel shows animated color patterns based on the number of connected controllers. Patterns are defined per-app via `led_config.h` using a `NEOPIXEL_PATTERN_*` macro table:

| Count | Pattern | Description |
|-------|---------|-------------|
| 0 | App-specific idle | Pulsing or animated, indicating no controllers |
| 1 | Solid color | One controller connected |
| 2 | Alternating colors | Two controllers |
| 3 | Three-color cycle | Three controllers |
| 4 | Four-color cycle | Four controllers |
| 5 | Five-color cycle | Five controllers |

Common color patterns used across apps include purple (idle), blue (1 player), red (2 players), green (3 players), and combinations thereof. Each app can override these via its `led_config.h`.

## Board LED Fallback

When `BOARD_LED_PIN` is defined (e.g., GPIO 25 on Raspberry Pi Pico), a plain GPIO LED provides basic status:

| State | Pattern |
|-------|---------|
| No controllers | Slow blink (500ms toggle) |
| Controller(s) connected | Solid on |
| Profile change | Fast blinks (count = profile_index + 1) |

This is the fallback for boards without a WS2812 LED (Pico, Pico W).

## Profile Indication

When a user cycles profiles, the LED provides visual confirmation:

### NeoPixel

1. LED turns **OFF** for 200ms (this is what gets counted)
2. LED turns **ON** briefly (100ms) between OFF blinks, showing the normal connection color
3. Number of OFF blinks = profile index + 1 (Profile 0 = 1 blink, Profile 1 = 2 blinks)
4. Returns to normal idle/connected pattern

### Board LED

Same counting pattern: fast blinks (150ms on + 150ms off) followed by a 600ms pause, then return to normal.

The indication state machine uses `neopixel_state_t` with states: `NEOPIXEL_IDLE`, `NEOPIXEL_BLINK_ON`, `NEOPIXEL_BLINK_OFF`.

## Override Color

Apps can set a specific LED color for mode indication:

```c
leds_set_color(0, 0, 255);  // Blue for a specific USB output mode
```

When an override color is set:
- **Idle (0 controllers)**: The override color pulses (triangle wave brightness)
- **Connected (1+ controllers)**: The override color shows solid at full brightness

## Auto-Disable

The NeoPixel automatically does nothing when:
- `CONFIG_NO_NEOPIXEL` is defined (for targets where NeoPixel conflicts with PIO-USB)
- `WS2812_PIN` is not defined for the board
- PIO initialization fails

## PIO Resource Note

NeoPixel uses PIO0 by default (`pio_claim_unused_sm(pio0, true)`). On boards where PIO-USB also needs PIO0 (e.g., bt2usb on Pico W where CYW43 claims PIO1), NeoPixel must be disabled via `CONFIG_NO_NEOPIXEL=1` to avoid resource conflicts. See the [PIO resource notes](../../CLAUDE.md) for details.

## API

```c
leds_init();                         // Initialize LED subsystem
leds_task();                         // Call from main loop
leds_set_connected_devices(count);   // Override device count (before player assignment)
leds_set_color(r, g, b);            // Set override color for mode indication
leds_indicate_profile(index);        // Trigger profile blink pattern
leds_is_indicating();                // Check if profile indicator is active
```

## See Also

- [Profiles](profiles.md) -- Profile cycling triggers LED indication
- [Players](players.md) -- Player count drives LED patterns
