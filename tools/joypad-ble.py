#!/usr/bin/env python3
"""
joypad-ble — self-contained BLE control + OTA for Joypad OS devices (macOS).

Reaches the device's Nordic UART Service over native CoreBluetooth using
retrieveConnectedPeripherals — so it works EVEN WHILE macOS holds the controller
as a HID gamepad (no advertising, no browser, no USB). Then flashes new firmware
entirely over BLE via the Adafruit/Nordic legacy DFU.

  joypad-ble.py info                      # {"cmd":"INFO"} -> JSON
  joypad-ble.py cmd '{"cmd":"IMU.MAP",...}'
  joypad-ble.py ota                       # reboot into BLE OTA DFU
  joypad-ble.py dfu firmware.zip          # DFU transfer to an AdafruitDFU target
  joypad-ble.py flash firmware.zip        # ota + wait + dfu  (the whole thing)

macOS: NUS access uses CoreBluetooth (pyobjc). The DFU target advertises, so the
transfer uses bleak. Both ship with the miniconda env here.
"""
import argparse
import asyncio
import json
import struct
import sys
import zipfile

import objc
from Foundation import NSObject, NSRunLoop, NSDate, NSData
from CoreBluetooth import CBCentralManager, CBUUID, CBCharacteristicWriteWithoutResponse
from bleak import BleakScanner, BleakClient

NUS_SVC = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_RX = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
NUS_TX = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
HID_SVC = "1812"
CB_POWERED_ON = 5

# Adafruit/Nordic legacy DFU (SDK-11 style)
DFU_SVC = "00001530-1212-efde-1523-785feabcd123"
DFU_CTRL = "00001531-1212-efde-1523-785feabcd123"   # control point (write + notify)
DFU_PKT = "00001532-1212-efde-1523-785feabcd123"    # packet (write without response)

SYNC = 0xAA
MSG_CMD, MSG_RSP, MSG_EVT = 0x01, 0x02, 0x03


def log(*a):
    print(*a, file=sys.stderr, flush=True)


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


# ============================================================================
# NUS command channel — CoreBluetooth (reaches the HID-held app device)
# ============================================================================
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
        central.stopScan()
        self.peripheral = peripheral
        peripheral.setDelegate_(self)
        central.connectPeripheral_options_(peripheral, None)

    def centralManager_didConnectPeripheral_(self, central, peripheral):
        peripheral.discoverServices_([CBUUID.UUIDWithString_(NUS_SVC)])

    def centralManager_didFailToConnectPeripheral_error_(self, central, peripheral, error):
        self._fail("connect failed: %s" % error)

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
            self._fail("NUS RX not found")
            return
        if self.want_reply and self.tx_char:
            peripheral.setNotifyValue_forCharacteristic_(True, self.tx_char)
        else:
            self._write()

    def peripheral_didUpdateNotificationStateForCharacteristic_error_(self, peripheral, ch, error):
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


def nus_run(payload: bytes, want_reply: bool, timeout: float = 15.0):
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
        log("ERROR: NUS timed out")
        sys.exit(3)
    return runner.result


# ============================================================================
# BLE DFU — Adafruit/Nordic legacy, over bleak (the DFU target advertises)
# ============================================================================
async def find_dfu(timeout=40):
    log(f"waiting up to {timeout}s for an AdafruitDFU target…")

    def match(d, ad):
        name = (ad.local_name or d.name or "")
        svcs = [s.lower() for s in (ad.service_uuids or [])]
        return name.lower().startswith("adafruit") or "dfu" in name.lower() \
            or DFU_SVC.lower() in svcs
    return await BleakScanner.find_device_by_filter(match, timeout=timeout)


async def dfu_flash(zip_path):
    with zipfile.ZipFile(zip_path) as z:
        manifest = json.loads(z.read("manifest.json"))
        app = manifest["manifest"]["application"]
        firmware = z.read(app["bin_file"])
        init = z.read(app["dat_file"])
    log(f"package: {len(firmware)} B firmware + {len(init)} B init packet")

    dev = await find_dfu()
    if not dev:
        log("ERROR: no AdafruitDFU target found (did OTA reboot it?)")
        sys.exit(4)
    log("DFU target:", dev.name or dev.address)

    notifs: asyncio.Queue = asyncio.Queue()

    async with BleakClient(dev, timeout=20) as client:
        def on_ctrl(_c, data):
            notifs.put_nowait(bytes(data))
        await client.start_notify(DFU_CTRL, on_ctrl)

        async def cp(data, response=True):
            await client.write_gatt_char(DFU_CTRL, bytes(data), response=response)

        async def pkt(data):
            await client.write_gatt_char(DFU_PKT, bytes(data), response=False)

        async def expect(op, timeout=30):
            while True:
                data = await asyncio.wait_for(notifs.get(), timeout)
                if data and data[0] == 0x11:
                    continue  # stray packet-receipt notification
                if not (len(data) >= 3 and data[0] == 0x10 and data[1] == op and data[2] == 0x01):
                    raise RuntimeError(f"DFU op 0x{op:02x} failed: {data.hex()}")
                return

        # 1) Start DFU, application image (bit 2)
        log("start dfu…")
        await cp([0x01, 0x04])
        await pkt(struct.pack("<III", 0, 0, len(firmware)))  # SD=0, BL=0, app size
        await expect(0x01)

        # 2) Init packet
        log("init packet…")
        await cp([0x02, 0x00])
        await pkt(init)
        await cp([0x02, 0x01])
        await expect(0x02)

        # 3) Receive firmware with packet-receipt-notification (PRN) flow
        #    control. Without it, write-without-response overruns the link on
        #    macOS, packets drop, and the image fails its CRC (0x03 -> 0x06).
        prn = 8
        await cp([0x08, prn & 0xFF, (prn >> 8) & 0xFF])
        await cp([0x03])

        chunk = 20  # legacy DFU packet characteristic
        log(f"sending {len(firmware)} bytes in {chunk}-byte chunks (prn={prn})…")
        since = 0
        for i in range(0, len(firmware), chunk):
            await pkt(firmware[i:i + chunk])
            since += 1
            if since >= prn:
                since = 0
                rec = await asyncio.wait_for(notifs.get(), 20)  # await receipt
                if rec and rec[0] == 0x10 and not (rec[1] == 0x03 and rec[2] == 0x01):
                    raise RuntimeError(f"DFU firmware error: {rec.hex()}")
            if i % 48000 < chunk:
                log(f"  {min(i + chunk, len(firmware))}/{len(firmware)} bytes")
        await expect(0x03, timeout=120)
        log("firmware received; validating…")

        # 4) Validate, then activate + reset
        await cp([0x04])
        await expect(0x04)
        log("validated; activating + resetting…")
        try:
            await cp([0x05])
        except Exception:
            pass  # device resets; the write may not ACK
    log("DFU COMPLETE — device rebooting into new firmware.")


async def do_flash(zip_path):
    log("triggering OTA over NUS…")
    nus_run(b'{"cmd":"OTA"}', want_reply=False)
    log("OTA sent; waiting for the device to re-advertise as DFU…")
    await asyncio.sleep(3)
    await dfu_flash(zip_path)


def main():
    p = argparse.ArgumentParser(prog="joypad-ble")
    sub = p.add_subparsers(dest="c", required=True)
    sub.add_parser("info")
    c = sub.add_parser("cmd"); c.add_argument("json"); c.add_argument("--no-wait", action="store_true")
    sub.add_parser("ota")
    d = sub.add_parser("dfu"); d.add_argument("zip")
    f = sub.add_parser("flash"); f.add_argument("zip")
    a = p.parse_args()

    if a.c == "info":
        print(nus_run(b'{"cmd":"INFO"}', want_reply=True) or "(no response)")
    elif a.c == "cmd":
        r = nus_run(a.json.encode(), want_reply=not a.no_wait)
        if r is not None:
            print(r)
    elif a.c == "ota":
        nus_run(b'{"cmd":"OTA"}', want_reply=False)
        print("OTA sent — device should reboot to AdafruitDFU.")
    elif a.c == "dfu":
        asyncio.run(dfu_flash(a.zip))
    elif a.c == "flash":
        asyncio.run(do_flash(a.zip))


if __name__ == "__main__":
    main()
