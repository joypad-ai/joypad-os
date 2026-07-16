// NUSProbe — minimal CoreBluetooth probe + NUS byte pipe.
// No args: retrieve/connect/list services. With NUSPROBE_HEX env: write those
// bytes to the NUS RX characteristic (the signed-proxy pattern in miniature).
import Foundation
import CoreBluetooth

let NUS = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
let NUS_RX = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
let BATT = CBUUID(string: "180F")
let HID = CBUUID(string: "1812")
let DEVINFO = CBUUID(string: "180A")

func hexToData(_ s: String) -> Data {
    var d = Data(); var i = s.startIndex
    while i < s.endIndex, let j = s.index(i, offsetBy: 2, limitedBy: s.endIndex) {
        d.append(UInt8(s[i..<j], radix: 16) ?? 0); i = j
    }
    return d
}

final class Probe: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var central: CBCentralManager!
    var target: CBPeripheral?
    let payload: Data?

    override init() {
        let arg = CommandLine.arguments.dropFirst().first
        if let hex = arg ?? ProcessInfo.processInfo.environment["NUSPROBE_HEX"], !hex.isEmpty {
            payload = hexToData(hex)
        } else { payload = nil }
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        print("[state] \(c.state.rawValue) (5=poweredOn)")
        guard c.state == .poweredOn else { return }
        let byNUS = c.retrieveConnectedPeripherals(withServices: [NUS])
        let byStd = c.retrieveConnectedPeripherals(withServices: [BATT, HID, DEVINFO])
        print("[retrieve NUS]  \(byNUS.count): \(byNUS.map { "\($0.name ?? "?") \($0.identifier)" })")
        print("[retrieve std]  \(byStd.count): \(byStd.map { "\($0.name ?? "?") \($0.identifier)" })")
        if let p = byNUS.first ?? byStd.first {
            target = p
            p.delegate = self
            print("[connect] attaching to \(p.name ?? "?")...")
            c.connect(p, options: nil)
        } else {
            print("[result] nothing retrievable — same wall as the script")
            exit(2)
        }
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        print("[connected] \(p.name ?? "?") — discovering services...")
        p.discoverServices(nil)
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        print("[connect FAILED] \(error?.localizedDescription ?? "unknown")")
        exit(3)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        if let e = error { print("[services ERROR] \(e)"); exit(4) }
        let uuids = (p.services ?? []).map { $0.uuid.uuidString }
        print("[services] \(uuids)")
        guard let nus = p.services?.first(where: { $0.uuid == NUS }) else {
            print("[result] connected, no NUS service"); exit(1)
        }
        if payload == nil {
            print("[result] NUS REACHABLE from ad-hoc signed app ✅"); exit(0)
        }
        p.discoverCharacteristics([NUS_RX], for: nus)
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        if let e = error { print("[chars ERROR] \(e)"); exit(4) }
        guard let rx = s.characteristics?.first(where: { $0.uuid == NUS_RX }) else {
            print("[result] no RX characteristic"); exit(1)
        }
        let wwr = rx.properties.contains(.writeWithoutResponse)
        print("[rx] props=\(rx.properties.rawValue) writeWithoutResponse=\(wwr)")
        let data = payload!
        let mtu = p.maximumWriteValueLength(for: wwr ? .withoutResponse : .withResponse)
        var off = 0
        while off < data.count {
            let end = min(off + mtu, data.count)
            p.writeValue(data[off..<end], for: rx, type: wwr ? .withoutResponse : .withResponse)
            off = end
        }
        print("[sent] \(data.count) bytes to NUS RX ✅")
        if wwr {  // no ack coming; give the stack a beat to flush
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { exit(0) }
        }
    }

    func peripheral(_ p: CBPeripheral, didWriteValueFor c: CBCharacteristic, error: Error?) {
        if let e = error { print("[write ERROR] \(e)"); exit(4) }
        print("[write acked] ✅"); exit(0)
    }
}

let probe = Probe()
DispatchQueue.main.asyncAfter(deadline: .now() + 25) {
    print("[timeout] no conclusive result in 25s")
    exit(5)
}
RunLoop.main.run()
