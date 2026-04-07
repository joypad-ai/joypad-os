# Neo Geo / SuperGun Output Interface

Outputs controller data to Neo Geo consoles (AES/MVS) and SuperGun arcade boards via active-low GPIO signals on a DB15 connector. Uses open-drain logic to safely interface 3.3V GPIO with 5V Neo Geo hardware.

## Protocol

- **Bus**: Active-low GPIO (directly driving DB15 pins)
- **Voltage**: Open-drain with pull-ups to 5V (Neo Geo side)
- **Core**: Core 0 (no timing-critical PIO needed)

## Player Support

- **Max players**: 1 per adapter

## Button Mapping

Neo Geo uses a 1-lever, 4-6 button layout. Standard arcade notation:

| DB15 Pin | Neo Geo Function | Arcade Label |
|----------|------------------|--------------|
| Pin 2 | Button 6 | K3 |
| Pin 3 | Coin (S1) | Coin |
| Pin 4 | Button 4 / D | K1 |
| Pin 5 | Button 2 / B | P2 |
| Pin 6 | Right | - |
| Pin 7 | Down | - |
| Pin 10 | Button 5 / Select | K2 |
| Pin 11 | Start (S2) | Start |
| Pin 12 | Button 3 / C | P3 |
| Pin 13 | Button 1 / A | P1 |
| Pin 14 | Left | - |
| Pin 15 | Up | - |

## Profiles

7 profiles for different arcade stick and pad layouts. See [usb2neogeo app](../apps/usb2neogeo.md) for full profile details.

## Feedback

No rumble or LED feedback -- Neo Geo controllers are passive.

## Apps Using This Output

| App | Description |
|-----|-------------|
| `usb2neogeo` | USB/BT controllers to Neo Geo/SuperGun |
