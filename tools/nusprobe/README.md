# nusprobe — BLE NUS command tool for OS-paired JoypadOS devices

macOS hides OS-paired BLE HID peripherals from processes without app
identity: `retrieveConnectedPeripherals*` returns an empty list for plain
python/bleak scripts, no error. It's input-device protection (same spirit as
Input Monitoring) and there is no settings override — the override is having
an identity. An **ad-hoc signed app bundle** (`codesign -s -`, no Apple
account) is the smallest identity that passes: it can retrieve the paired
device, connect, discover services, and write to NUS.

This tool is that identity, kept deliberately dumb: `NUSProbe.app` is a byte
pipe to the NUS RX characteristic; the python wrapper is the brains (builds
the standard JoypadOS CDC frame — see `tools/cdc_cmd.py` for the USB-serial
sibling).

```sh
./build.sh                                      # one-time (and after edits)
./nus_cmd.py '{"cmd":"FACE.STYLE","style":"astro"}'
./nus_cmd.py '{"cmd":"FACE.EMO","emo":"happy"}'
./nus_cmd.py                                    # PING
```

Run with no frame argument to just probe (retrieve + connect + list GATT
services). First launch pops one Bluetooth permission dialog for "NUSProbe".

Notes:
- The device must be BLE-connected to macOS (paired and awake).
- macOS hides the HID service (0x1812) itself from GATT discovery even for
  signed apps; Battery/Device Info/NUS are visible.
- Responses are not read back (RX is write-without-response, fire and
  forget). For two-way work, extend the app to subscribe to NUS TX.
