# Button Constants

All input drivers normalize controller buttons to the `JP_BUTTON_*` constants defined in `src/core/buttons.h`. These follow the W3C Gamepad API standard ordering, where bit position equals the W3C button index.

## Button Reference

```
            ____________________________              __
           / [__L2__]          [__R2__] \               |
          / [__ L1 __]        [__ R1 __] \              | Triggers
       __/________________________________\__         __|
      /                                  _   \          |
     /      /\           __             (B4)  \         |
    /       ||      __  |A1|  __     _       _ \        | Main Pad
   |    <===DP===> |S1|      |S2|  (B3) -|- (B2)|       |
    \       ||      --        --       _       /        |
    /\      \/   /   \        /   \   (B1)   /\       __|
   /  \________ | LS  | ____ | RS  | _______/  \        |
  |         /  \ \___/ /    \ \___/ /  \         |      | Sticks
  |        /    \_____/      \_____/    \        |    __|
  |       /       L3            R3       \       |
   \_____/                                \_____/
```

## Full Button Table

| Constant | Bit | Value | XInput | Switch | PlayStation | DInput |
|----------|-----|-------|--------|--------|-------------|--------|
| `JP_BUTTON_B1` | 0 | 0x0001 | A | B | Cross | 2 |
| `JP_BUTTON_B2` | 1 | 0x0002 | B | A | Circle | 3 |
| `JP_BUTTON_B3` | 2 | 0x0004 | X | Y | Square | 1 |
| `JP_BUTTON_B4` | 3 | 0x0008 | Y | X | Triangle | 4 |
| `JP_BUTTON_L1` | 4 | 0x0010 | LB | L | L1 | 5 |
| `JP_BUTTON_R1` | 5 | 0x0020 | RB | R | R1 | 6 |
| `JP_BUTTON_L2` | 6 | 0x0040 | LT | ZL | L2 | 7 |
| `JP_BUTTON_R2` | 7 | 0x0080 | RT | ZR | R2 | 8 |
| `JP_BUTTON_S1` | 8 | 0x0100 | Back | - | Select | 9 |
| `JP_BUTTON_S2` | 9 | 0x0200 | Start | + | Start | 10 |
| `JP_BUTTON_L3` | 10 | 0x0400 | LS | LS | L3 | 11 |
| `JP_BUTTON_R3` | 11 | 0x0800 | RS | RS | R3 | 12 |
| `JP_BUTTON_DU` | 12 | 0x1000 | D-Up | D-Up | D-Up | Hat |
| `JP_BUTTON_DD` | 13 | 0x2000 | D-Down | D-Down | D-Down | Hat |
| `JP_BUTTON_DL` | 14 | 0x4000 | D-Left | D-Left | D-Left | Hat |
| `JP_BUTTON_DR` | 15 | 0x8000 | D-Right | D-Right | D-Right | Hat |
| `JP_BUTTON_A1` | 16 | 0x10000 | Guide | Home | PS | 13 |
| `JP_BUTTON_A2` | 17 | 0x20000 | -- | Capture | Touchpad | 14 |
| `JP_BUTTON_A3` | 18 | 0x40000 | -- | -- | Mute | -- |
| `JP_BUTTON_A4` | 19 | 0x80000 | -- | -- | -- | -- |
| `JP_BUTTON_L4` | 20 | 0x100000 | P1 | -- | -- | -- |
| `JP_BUTTON_R4` | 21 | 0x200000 | P2 | -- | -- | -- |

Buttons are active-high: bit = 1 means pressed, 0 means released.

## Analog Axis Indices

Defined in `src/core/input_event.h` as `analog_axis_index_t`:

| Index | Constant | Range | Default | Description |
|-------|----------|-------|---------|-------------|
| 0 | `ANALOG_LX` | 0-255 | 128 | Left stick X (0=left, 255=right) |
| 1 | `ANALOG_LY` | 0-255 | 128 | Left stick Y (0=up, 255=down) |
| 2 | `ANALOG_RX` | 0-255 | 128 | Right stick X (0=left, 255=right) |
| 3 | `ANALOG_RY` | 0-255 | 128 | Right stick Y (0=up, 255=down) |
| 4 | `ANALOG_L2` | 0-255 | 0 | Left trigger (0=released, 255=fully pressed) |
| 5 | `ANALOG_R2` | 0-255 | 0 | Right trigger (0=released, 255=fully pressed) |
| 6 | `ANALOG_RZ` | 0-255 | 0 | RZ axis / twist / spinner |

**Y-axis convention (HID standard):** 0 = stick pushed UP, 128 = centered, 255 = stick pushed DOWN. This matches USB HID and DirectInput. Input drivers for Nintendo controllers (which use inverted Y) must invert before submitting to the router.

## Controller Layout Classification

The `controller_layout_t` enum in `input_event.h` describes the physical button arrangement, mainly relevant for 6-button controllers:

| Layout | Physical Arrangement | Examples |
|--------|---------------------|----------|
| `LAYOUT_UNKNOWN` | Unknown / default | -- |
| `LAYOUT_MODERN_4FACE` | Standard 4-face (SNES/PS/Xbox style) | DualSense, Xbox, Pro Controller |
| `LAYOUT_NINTENDO_4FACE` | Nintendo BAYX face style | SNES controllers |
| `LAYOUT_NINTENDO_N64` | A/B + C-buttons + Z | N64 controllers |
| `LAYOUT_GAMECUBE` | AXBY face style | GameCube controllers |
| `LAYOUT_SEGA_6BUTTON` | Bottom [A][B][C], Top [X][Y][Z] | Genesis/Saturn 6-button pads |
| `LAYOUT_PCE_6BUTTON` | Bottom [III][II][I], Top [IV][V][VI] | PCEngine Avenue Pad 6 |
| `LAYOUT_ASTROCITY` | Bottom [D][E][F], Top [A][B][C] | Sega Astro City Mini arcade |
| `LAYOUT_3DO_3BUTTON` | Single row [A][B][C] | 3DO controllers |

### GP2040-CE Canonical Mapping

Internally, all 6-button layouts use the GP2040-CE positional mapping:

```
  Top row:    [B3][B4][R1]
  Bottom row: [B1][B2][R2]
```

Layout transform functions (e.g., `transform_to_pce_layout()`) convert between layouts when needed by output drivers.

## See Also

- [Router](router.md) -- How button data flows through the system
- [Profiles](profiles.md) -- Button remapping
- [Players](players.md) -- Player-to-slot assignment
