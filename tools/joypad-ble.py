#!/usr/bin/env python3
"""
joypad-ble — self-contained BLE control for Joypad OS devices (macOS).

Talks to the device's Nordic UART Service over native CoreBluetooth, using
retrieveConnectedPeripherals — so it reaches the controller EVEN WHILE macOS
holds it as a HID gamepad (no advertising required, unlike bleak/Web Bluetooth).
Speaks the firmware's binary command protocol
(0xAA | len16 | type | seq | payload | crc16-CCITT), so no firmware change, no USB.

  joypad-ble.py info                    # {"cmd":"INFO"} -> JSON
  joypad-ble.py cmd '{"cmd":"IMU.MAP","x":-2,"y":-1,"z":-3}'
  joypad-ble.py ota                     # reboot into BLE OTA DFU (AdafruitDFU)

macOS-only (CoreBluetooth). Requires pyobjc (pyobjc-framework-CoreBluetooth).
"""
import argparse
import sys

import objc
from Foundation import NSObject, NSRunLoop, NSDate, NSData
from CoreBluetooth import (
    CBCentralManager, CBUUID, CBCharacteristicWriteWithoutResponse,
)

NUS_SVC = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_RX = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   # write  (host -> device)
NUS_TX = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   # notify (device -> host)
HID_SVC = "1812"                                   # so retrieve finds HID-held pads
CB_POWERED_ON = 5

SYNC = 0xAA
MSG_CMD, MSG_RSP, MSG_EVT = 0x01, 0x02, 0x03


def crc16(data: bytes) -> int:
    c = 0xFFFF
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def build_frame(payload: bytes, msg_type: int = MSG_CMD, seq: int = 0) -> bytes:
    body = bytes([msg_type, seq]) + payload
    c = crc16(body)
    return (bytes([SYNC, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF])
            + body + bytes([c & 0xFF, (c >> 8) & 0xFF]))


def parse_frames(buf: bytearray):
    while True:
        while buf and buf[0] != SYNC:
            del buf[0]
        if len(buf) < 5:
            return
        length = buf[1] | (buf[2] << 8)
        total = 3 + 2 + length + 2
        if len(buf) < total:
            return
        yield buf[3], buf[4], bytes(buf[5:5 + length])
        del buf[:total]


def log(*a):
    print(*a, file=sys.stderr)


class Runner(NSObject):
    def initWithPayload_wantReply_(self, payload, want_reply):
        self = objc.super(Runner, self).init()
        if self is None:
            return None
        self.payload = payload
        self.want_reply = want_reply
        self.rx = bytearray()
        self.result = None
        self.done = False
        self.error = None
        self.peripheral = None
        self.rx_char = None
        self.tx_char = None
        self.central = CBCentralManager.alloc().initWithDelegate_queue_(self, None)
        return self

    # --- central state ---
    def centralManagerDidUpdateState_(self, central):
        if central.state() != CB_POWERED_ON:
            self._fail("Bluetooth not powered on (state=%s)" % central.state())
            return
        uuids = [CBUUID.UUIDWithString_(NUS_SVC), CBUUID.UUIDWithString_(HID_SVC)]
        peris = central.retrieveConnectedPeripheralsWithServices_(uuids)
        if peris and len(peris):
            log("found connected peripheral:", peris[0].name())
            self.peripheral = peris[0]
            self.peripheral.setDelegate_(self)
            central.connectPeripheral_options_(self.peripheral, None)
        else:
            log("no connected peripheral — scanning…")
            central.scanForPeripheralsWithServices_options_(
                [CBUUID.UUIDWithString_(NUS_SVC)], None)

    def centralManager_didDiscoverPeripheral_advertisementData_RSSI_(
            self, central, peripheral, adv, rssi):
        log("discovered:", peripheral.name())
        central.stopScan()
        self.peripheral = peripheral
        peripheral.setDelegate_(self)
        central.connectPeripheral_options_(peripheral, None)

    def centralManager_didConnectPeripheral_(self, central, peripheral):
        log("connected; discovering NUS…")
        peripheral.discoverServices_([CBUUID.UUIDWithString_(NUS_SVC)])

    def centralManager_didFailToConnectPeripheral_error_(self, central, peripheral, error):
        self._fail("connect failed: %s" % error)

    # --- peripheral ---
    def peripheral_didDiscoverServices_(self, peripheral, error):
        if error:
            self._fail("service discovery: %s" % error)
            return
        for s in peripheral.services():
            if s.UUID().UUIDString().upper() == NUS_SVC:
                peripheral.discoverCharacteristics_forService_(
                    [CBUUID.UUIDWithString_(NUS_RX), CBUUID.UUIDWithString_(NUS_TX)], s)
                return
        self._fail("NUS service not found")

    def peripheral_didDiscoverCharacteristicsForService_error_(self, peripheral, service, error):
        if error:
            self._fail("char discovery: %s" % error)
            return
        for ch in service.characteristics():
            u = ch.UUID().UUIDString().upper()
            if u == NUS_RX:
                self.rx_char = ch
            elif u == NUS_TX:
                self.tx_char = ch
        if not self.rx_char:
            self._fail("NUS RX characteristic not found")
            return
        if self.want_reply and self.tx_char:
            peripheral.setNotifyValue_forCharacteristic_(True, self.tx_char)
        else:
            self._write()

    def peripheral_didUpdateNotificationStateForCharacteristic_error_(self, peripheral, ch, error):
        # Notifications are live now — safe to write and expect a reply.
        self._write()

    def peripheral_didUpdateValueForCharacteristic_error_(self, peripheral, ch, error):
        val = ch.value()
        if not val:
            return
        self.rx.extend(bytes(val))
        for t, _s, p in parse_frames(bytearray(self.rx)):
            if t in (MSG_RSP, MSG_EVT):
                self._finish(p.decode("utf-8", "replace"))
                return

    @objc.python_method
    def _write(self):
        frame = build_frame(self.payload)
        data = NSData.dataWithBytes_length_(frame, len(frame))
        self.peripheral.writeValue_forCharacteristic_type_(
            data, self.rx_char, CBCharacteristicWriteWithoutResponse)
        log("sent %d-byte frame" % len(frame))
        if not self.want_reply:
            self._finish(None)

    @objc.python_method
    def _finish(self, result):
        self.result = result
        self.done = True

    @objc.python_method
    def _fail(self, msg):
        self.error = msg
        self.done = True


def run(payload: bytes, want_reply: bool, timeout: float = 15.0):
    runner = Runner.alloc().initWithPayload_wantReply_(payload, want_reply)
    loop = NSRunLoop.currentRunLoop()
    deadline = NSDate.dateWithTimeIntervalSinceNow_(timeout)
    while not runner.done and NSDate.date().compare_(deadline) < 0:
        loop.runMode_beforeDate_("NSDefaultRunLoopMode",
                                 NSDate.dateWithTimeIntervalSinceNow_(0.1))
    if runner.error:
        log("ERROR:", runner.error)
        sys.exit(2)
    if not runner.done:
        log("ERROR: timed out")
        sys.exit(3)
    return runner.result


def main():
    p = argparse.ArgumentParser(prog="joypad-ble")
    sub = p.add_subparsers(dest="c", required=True)
    sub.add_parser("info")
    c = sub.add_parser("cmd")
    c.add_argument("json")
    c.add_argument("--no-wait", action="store_true")
    sub.add_parser("ota")
    a = p.parse_args()

    if a.c == "info":
        print(run(b'{"cmd":"INFO"}', want_reply=True) or "(no response)")
    elif a.c == "cmd":
        r = run(a.json.encode(), want_reply=not a.no_wait)
        if r is not None:
            print(r)
    elif a.c == "ota":
        run(b'{"cmd":"OTA"}', want_reply=False)
        print("OTA sent — device should reboot to AdafruitDFU.")


if __name__ == "__main__":
    main()
