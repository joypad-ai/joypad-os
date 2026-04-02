# Player Management

The player manager tracks the mapping from physical controllers to player slots and handles feedback routing (rumble, LEDs) from output back to input devices.

Source: `src/core/services/players/manager.h`, `manager.c`, and `feedback.h`

## Slot Modes

Apps configure one of two slot modes at init:

### SHIFT Mode

Players shift up when someone disconnects. If Player 2 disconnects, Player 3 becomes Player 2 and Player 4 becomes Player 3. All remaining players are renumbered.

Used by consoles where player numbers are positional: **PCEngine** (5-player multitap), **3DO** (daisy-chain), and other adapters where the host expects contiguous player slots.

### FIXED Mode

Players keep their assigned slots permanently. If Player 2 disconnects, slot 2 stays empty (dev_addr = -1) and Player 3 remains in slot 3.

Used by consoles with fixed port hardware: **GameCube** (4-port), and apps where controllers should always be in a specific position.

## Player Data Structure

Each player slot is a `Player_t`:

```c
typedef struct {
    int dev_addr;               // Device address (-1 = empty slot)
    int instance;               // Device instance/connection index
    int player_number;          // 1-based player number (0 = unassigned)
    input_transport_t transport; // USB, BT Classic, BLE, native
    char name[32];              // Device name (e.g., "DualSense", "Xbox Controller")
} Player_t;
```

The `players[]` array holds up to `MAX_PLAYERS` slots (default 5, configurable per-app). The `playersCount` global tracks the highest occupied slot + 1.

## Auto-Assign on Press

When `auto_assign_on_press` is enabled (the default), controllers are not assigned a slot until the user presses a button or moves an analog stick beyond threshold (~40% deflection). This prevents phantom players from devices that send idle reports.

The router calls `add_player()` when it first sees meaningful input from an unassigned device. The device name is looked up from the USB HID registry, Bluetooth device table, or I2C peer name.

## Player Lifecycle

1. **Connect** -- Input driver detects a new device
2. **First input** -- Router calls `add_player()` when buttons/sticks are active
3. **Active** -- Input flows through router to the assigned output slot
4. **Disconnect** -- Input driver calls `remove_players_by_address()`
   - SHIFT mode: Remaining players shift up, all renumbered
   - FIXED mode: Slot marked empty, other players unchanged
5. **Router cleanup** -- `router_device_disconnected()` clears output state and blend tracking

## Configuration

Apps set player config at init:

```c
player_config_t player_cfg = {
    .slot_mode = PLAYER_SLOT_FIXED,
    .max_slots = 4,
    .auto_assign_on_press = true,
};
players_init_with_config(&player_cfg);
```

Or use defaults (SHIFT mode, 5 slots) with `players_init()`.

## Feedback Routing

The feedback system (`feedback.h`) routes console-generated feedback back to the correct physical controller. This enables cross-stack feedback -- a Bluetooth DualSense vibrates when a GameCube game triggers rumble.

### Feedback State

Each player slot has a `feedback_state_t`:

```c
typedef struct {
    feedback_rumble_t rumble;       // Left/right/trigger motors (0-255)
    feedback_led_t led;             // Player LED pattern + RGB color
    feedback_trigger_t left_trigger;  // Adaptive trigger effects
    feedback_trigger_t right_trigger;
    bool rumble_dirty;              // Changed since last applied
    bool led_dirty;
    bool triggers_dirty;
} feedback_state_t;
```

### Feedback Flow

1. Output driver reads rumble/LED state from the console (e.g., GameCube rumble motor command)
2. Output driver calls `feedback_set_rumble(player_index, left, right)`
3. `players_task()` (called from main loop) routes the dirty feedback state to the correct input driver based on `dev_addr` and `instance`
4. Input driver applies the feedback to the physical controller (USB HID output report, BT rumble command, etc.)

### Device Capabilities

Input drivers report their feedback capabilities via `FEEDBACK_CAP_*` flags:

| Flag | Description |
|------|-------------|
| `FEEDBACK_CAP_RUMBLE_BASIC` | Standard 2-motor rumble |
| `FEEDBACK_CAP_RUMBLE_TRIGGER` | Trigger motors (Xbox, DualSense) |
| `FEEDBACK_CAP_RUMBLE_HD` | HD rumble (Switch) |
| `FEEDBACK_CAP_LED_PLAYER` | Player indicator LEDs (1-4) |
| `FEEDBACK_CAP_LED_RGB` | RGB lightbar (DualShock 4, DualSense) |
| `FEEDBACK_CAP_TRIGGER_ADAPT` | Adaptive triggers (DualSense) |

### Profile Indicator Priority

During profile change indication, external rumble and LED calls are temporarily blocked. The profile indicator uses internal setter functions (`feedback_set_rumble_internal`, etc.) that bypass this check, ensuring the profile change feedback is always visible.

## Player LED Patterns

The `PLAYER_LEDS[]` array provides bitmask patterns for controllers with player indicator LEDs (PS3, Switch):

| Player | LED Pattern |
|--------|-------------|
| 1 | LED 1 on |
| 2 | LED 2 on |
| 3 | LED 3 on |
| 4 | LED 4 on |
| 5 | LED 1+4 on |
| 6 | LED 2+4 on |
| 7+ | LED 3+4 on |

## See Also

- [Router](router.md) -- How input flows to player slots
- [LEDs](leds.md) -- Board NeoPixel feedback (separate from controller LEDs)
- [Profiles](profiles.md) -- Per-player profile support
