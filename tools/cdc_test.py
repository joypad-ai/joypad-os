#!/usr/bin/env python3
"""
CDC Protocol Test Tool

Tests the binary framed CDC protocol for Joypad configuration.

Usage:
    python3 cdc_test.py /dev/tty.usbmodem*
    python3 cdc_test.py /dev/ttyACM0

Commands:
    info, ping, reboot
    mode, mode.set <n>, modes
    profile, profile.set <n>, profiles
    settings, reset
    stream - enable input streaming
    quit - exit
"""

import serial
import struct
import sys
import json
import time
import threading

# Protocol constants
CDC_SYNC = 0xAA
MSG_CMD = 0x01
MSG_RSP = 0x02
MSG_EVT = 0x03
MSG_ACK = 0x04
MSG_NAK = 0x05
MSG_DAT = 0x10


def crc16_ccitt(data: bytes) -> int:
    """CRC-16-CCITT (poly 0x1021, init 0xFFFF)"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc = crc << 1
            crc &= 0xFFFF
    return crc


def build_packet(msg_type: int, seq: int, payload: bytes) -> bytes:
    """Build a framed packet"""
    # Header: sync(1) + len(2) + type(1) + seq(1)
    header = struct.pack('<BHBB', CDC_SYNC, len(payload), msg_type, seq)
    # CRC over type + seq + payload
    crc_data = struct.pack('BB', msg_type, seq) + payload
    crc = crc16_ccitt(crc_data)
    return header + payload + struct.pack('<H', crc)


def parse_packet(data: bytes) -> dict:
    """Parse a received packet, returns None if incomplete/invalid"""
    if len(data) < 7:  # Minimum: sync(1) + len(2) + type(1) + seq(1) + crc(2)
        return None

    if data[0] != CDC_SYNC:
        return None

    length = struct.unpack('<H', data[1:3])[0]
    if len(data) < 5 + length + 2:
        return None

    msg_type = data[3]
    seq = data[4]
    payload = data[5:5+length]
    crc_received = struct.unpack('<H', data[5+length:7+length])[0]

    # Verify CRC
    crc_data = struct.pack('BB', msg_type, seq) + payload
    crc_calc = crc16_ccitt(crc_data)

    if crc_received != crc_calc:
        print(f"CRC mismatch: got {crc_received:04x}, expected {crc_calc:04x}")
        return None

    return {
        'type': msg_type,
        'seq': seq,
        'payload': payload,
        'raw_len': 5 + length + 2
    }


class CDCProtocol:
    def __init__(self, port: str, baudrate: int = 115200):
        self.ser = serial.Serial(port, baudrate, timeout=0.5)
        self.seq = 0
        self.rx_buffer = bytes()
        self.running = True
        self.event_callback = None

        # Start reader thread
        self.reader_thread = threading.Thread(target=self._reader, daemon=True)
        self.reader_thread.start()

    def _reader(self):
        """Background thread to read and parse packets"""
        while self.running:
            try:
                data = self.ser.read(64)
                if data:
                    self.rx_buffer += data
                    self._process_buffer()
            except Exception as e:
                if self.running:
                    print(f"Read error: {e}")
                break

    def _process_buffer(self):
        """Process received data, extract packets"""
        while len(self.rx_buffer) >= 7:
            # Find sync byte
            sync_pos = self.rx_buffer.find(bytes([CDC_SYNC]))
            if sync_pos == -1:
                self.rx_buffer = bytes()
                break
            if sync_pos > 0:
                self.rx_buffer = self.rx_buffer[sync_pos:]

            packet = parse_packet(self.rx_buffer)
            if packet is None:
                break

            self.rx_buffer = self.rx_buffer[packet['raw_len']:]
            self._handle_packet(packet)

    def _handle_packet(self, packet: dict):
        """Handle a received packet"""
        type_names = {MSG_RSP: 'RSP', MSG_EVT: 'EVT', MSG_ACK: 'ACK', MSG_NAK: 'NAK'}
        type_name = type_names.get(packet['type'], f"0x{packet['type']:02x}")

        try:
            payload_str = packet['payload'].decode('utf-8')
            payload_json = json.loads(payload_str)
            print(f"<< [{type_name}] seq={packet['seq']}: {json.dumps(payload_json, indent=2)}")
        except:
            print(f"<< [{type_name}] seq={packet['seq']}: {packet['payload'].hex()}")

    def send_cmd(self, cmd: str, args: dict = None):
        """Send a command and wait for response"""
        payload = {'cmd': cmd}
        if args:
            payload['args'] = args
        payload_bytes = json.dumps(payload).encode('utf-8')

        packet = build_packet(MSG_CMD, self.seq, payload_bytes)
        self.seq = (self.seq + 1) & 0xFF

        print(f">> [CMD] {cmd} {args or ''}")
        self.ser.write(packet)
        time.sleep(0.1)  # Give time for response

    def close(self):
        self.running = False
        self.ser.close()


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 cdc_test.py <serial_port>")
        print("Example: python3 cdc_test.py /dev/tty.usbmodem11301")
        sys.exit(1)

    port = sys.argv[1]
    print(f"Connecting to {port}...")

    try:
        proto = CDCProtocol(port)
    except Exception as e:
        print(f"Failed to open {port}: {e}")
        sys.exit(1)

    print("Connected. Type 'help' for commands, 'quit' to exit.")
    print()

    try:
        while True:
            try:
                line = input("> ").strip()
            except EOFError:
                break

            if not line:
                continue

            parts = line.split()
            cmd = parts[0].lower()

            if cmd == 'quit' or cmd == 'exit':
                break
            elif cmd == 'help':
                print("Commands:")
                print("  info, ping, reboot")
                print("  mode, mode.set <n>, modes")
                print("  profile, profile.set <n>, profiles")
                print("  settings, reset")
                print("  stream - enable input streaming")
                print("  raw <hex> - send raw bytes")
                print("  quit - exit")
            elif cmd == 'info':
                proto.send_cmd('INFO')
            elif cmd == 'ping':
                proto.send_cmd('PING')
            elif cmd == 'reboot':
                proto.send_cmd('REBOOT')
            elif cmd == 'mode':
                proto.send_cmd('MODE.GET')
            elif cmd == 'mode.set' and len(parts) > 1:
                proto.send_cmd('MODE.SET', {'mode': int(parts[1])})
            elif cmd == 'modes':
                proto.send_cmd('MODE.LIST')
            elif cmd == 'profile':
                proto.send_cmd('PROFILE.GET')
            elif cmd == 'profile.set' and len(parts) > 1:
                proto.send_cmd('PROFILE.SET', {'index': int(parts[1])})
            elif cmd == 'profiles':
                proto.send_cmd('PROFILE.LIST')
            elif cmd == 'settings':
                proto.send_cmd('SETTINGS.GET')
            elif cmd == 'reset':
                proto.send_cmd('SETTINGS.RESET')
            elif cmd == 'stream':
                proto.send_cmd('INPUT.STREAM', {'enable': True})
            elif cmd == 'raw' and len(parts) > 1:
                proto.ser.write(bytes.fromhex(parts[1]))
            else:
                # Try as raw command
                proto.send_cmd(cmd.upper())

            time.sleep(0.2)  # Wait for response

    except KeyboardInterrupt:
        print()

    proto.close()
    print("Disconnected.")


if __name__ == '__main__':
    main()
