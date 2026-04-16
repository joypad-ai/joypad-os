/**
 * CDC Protocol - Binary framed communication with Joypad devices
 *
 * Packet format:
 * [SYNC:0xAA][LENGTH:2][TYPE:1][SEQ:1][PAYLOAD:N][CRC16:2]
 *
 * Transport-agnostic: works over Web Serial (USB CDC) or Web Bluetooth (BLE NUS).
 */

const CDC_SYNC = 0xAA;
const MSG_CMD = 0x01;
const MSG_RSP = 0x02;
const MSG_EVT = 0x03;
const MSG_ACK = 0x04;
const MSG_NAK = 0x05;
const MSG_DAT = 0x10;

const TIMEOUT_MS = 2000;

// NUS Service UUIDs
const NUS_SERVICE_UUID = '6e400001-b5a3-f393-e0a9-e50e24dcca9e';
const NUS_RX_CHAR_UUID = '6e400002-b5a3-f393-e0a9-e50e24dcca9e';  // Write to device
const NUS_TX_CHAR_UUID = '6e400003-b5a3-f393-e0a9-e50e24dcca9e';  // Notifications from device

/**
 * CRC-16-CCITT (poly 0x1021, init 0xFFFF)
 */
function crc16(data) {
    let crc = 0xFFFF;
    for (const byte of data) {
        crc ^= byte << 8;
        for (let i = 0; i < 8; i++) {
            if (crc & 0x8000) {
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF;
            } else {
                crc = (crc << 1) & 0xFFFF;
            }
        }
    }
    return crc;
}

/**
 * Build a framed packet
 */
function buildPacket(type, seq, payload) {
    const payloadBytes = typeof payload === 'string'
        ? new TextEncoder().encode(payload)
        : payload;

    const packet = new Uint8Array(5 + payloadBytes.length + 2);

    // Header
    packet[0] = CDC_SYNC;
    packet[1] = payloadBytes.length & 0xFF;
    packet[2] = (payloadBytes.length >> 8) & 0xFF;
    packet[3] = type;
    packet[4] = seq;

    // Payload
    packet.set(payloadBytes, 5);

    // CRC over type + seq + payload
    const crcData = new Uint8Array(2 + payloadBytes.length);
    crcData[0] = type;
    crcData[1] = seq;
    crcData.set(payloadBytes, 2);
    const crcValue = crc16(crcData);

    packet[5 + payloadBytes.length] = crcValue & 0xFF;
    packet[5 + payloadBytes.length + 1] = (crcValue >> 8) & 0xFF;

    return packet;
}

// ============================================================================
// TRANSPORT: Web Serial (USB CDC)
// ============================================================================

class WebSerialTransport {
    constructor() {
        this.port = null;
        this.reader = null;
        this.writer = null;
        this.readLoopRunning = false;
        this.onData = null;  // callback(Uint8Array)
    }

    static isSupported() {
        return 'serial' in navigator;
    }

    get name() { return 'USB'; }

    async connect() {
        this.port = await navigator.serial.requestPort({ filters: [] });
        await this.port.open({ baudRate: 115200 });
        this.writer = this.port.writable.getWriter();
        this.reader = this.port.readable.getReader();
        this._startReadLoop();
    }

    /** Try reconnecting to a previously-granted port without user prompt */
    async tryAutoConnect() {
        const ports = await navigator.serial.getPorts();
        if (ports.length === 0) return false;
        this.port = ports[0];
        try {
            await this.port.open({ baudRate: 115200 });
        } catch (e) {
            // Port already open or unavailable
            this.port = null;
            return false;
        }
        this.writer = this.port.writable.getWriter();
        this.reader = this.port.readable.getReader();
        this._startReadLoop();
        return true;
    }

    async disconnect() {
        this.readLoopRunning = false;
        if (this.reader) {
            try { await this.reader.cancel(); this.reader.releaseLock(); } catch (e) {}
            this.reader = null;
        }
        if (this.writer) {
            try { this.writer.releaseLock(); } catch (e) {}
            this.writer = null;
        }
        if (this.port) {
            try { await this.port.close(); } catch (e) {}
            this.port = null;
        }
    }

    async write(data) {
        await this.writer.write(data);
    }

    async _startReadLoop() {
        this.readLoopRunning = true;
        while (this.readLoopRunning && this.reader) {
            try {
                const { value, done } = await this.reader.read();
                if (done) break;
                if (this.onData) this.onData(value);
            } catch (e) {
                if (this.readLoopRunning) console.error('Serial read error:', e);
                break;
            }
        }
        // Device disconnected (reboot, unplug, etc.)
        if (this.readLoopRunning && this.onDisconnect) {
            this.readLoopRunning = false;
            try { this.reader?.releaseLock(); } catch (e) {}
            try { this.writer?.releaseLock(); } catch (e) {}
            try { await this.port?.close(); } catch (e) {}
            this.reader = null;
            this.writer = null;
            this.port = null;
            this.onDisconnect();
        }
    }
}

// ============================================================================
// TRANSPORT: Web Bluetooth (BLE NUS)
// ============================================================================

class WebBluetoothTransport {
    constructor() {
        this.device = null;
        this.server = null;
        this.rxChar = null;  // Write to device
        this.txChar = null;  // Notifications from device
        this.onData = null;  // callback(Uint8Array)
        this._onDisconnected = null;
    }

    static isSupported() {
        return 'bluetooth' in navigator;
    }

    get name() { return 'BLE'; }

    async connect() {
        // Try previously authorized devices first (already connected, no scan needed)
        if (navigator.bluetooth.getDevices) {
            try {
                const devices = await navigator.bluetooth.getDevices();
                for (const device of devices) {
                    if (device.gatt) {
                        try {
                            console.log('[BLE] Trying cached device:', device.name);
                            const server = await device.gatt.connect();
                            this.device = device;
                            this.server = server;
                            this._setupDisconnectHandler();
                            await this._setupNUS();
                            return;
                        } catch (e) {
                            console.log('[BLE] Reconnect failed for', device.name, ':', e.message);
                        }
                    }
                }
            } catch (e) {
                console.log('[BLE] getDevices() failed:', e.message);
            }
        }

        // Fall back to scanning — filter by name prefix, NUS as optional service
        console.log('[BLE] Falling through to requestDevice()...');
        this.device = await navigator.bluetooth.requestDevice({
            filters: [{ namePrefix: 'Joypad' }],
            optionalServices: [NUS_SERVICE_UUID],
        });

        this._setupDisconnectHandler();
        this.server = await this.device.gatt.connect();
        await this._setupNUS();
    }

    _setupDisconnectHandler() {
        this._onDisconnected = () => {
            console.log('[BLE] Device disconnected');
            if (this.onDisconnect) this.onDisconnect();
        };
        this.device.addEventListener('gattserverdisconnected', this._onDisconnected);
    }

    async _setupNUS() {
        const service = await this.server.getPrimaryService(NUS_SERVICE_UUID);

        this.rxChar = await service.getCharacteristic(NUS_RX_CHAR_UUID);
        this.txChar = await service.getCharacteristic(NUS_TX_CHAR_UUID);

        // Subscribe to notifications (responses from device)
        await this.txChar.startNotifications();
        this.txChar.addEventListener('characteristicvaluechanged', (event) => {
            const value = new Uint8Array(event.target.value.buffer);
            const hex = Array.from(value.slice(0, 32)).map(b => b.toString(16).padStart(2, '0')).join(' ');
            console.log(`[BLE RX] ${value.length} bytes: ${hex}${value.length > 32 ? '...' : ''}`);
            if (this.onData) this.onData(value);
        });
    }

    async disconnect() {
        if (this.txChar) {
            try { await this.txChar.stopNotifications(); } catch (e) {}
            this.txChar = null;
        }
        this.rxChar = null;
        if (this.device) {
            this.device.removeEventListener('gattserverdisconnected', this._onDisconnected);
            if (this.server) {
                try { this.server.disconnect(); } catch (e) {}
                this.server = null;
            }
            this.device = null;
        }
    }

    async write(data) {
        if (!this.rxChar) throw new Error('Not connected');

        // BLE has MTU limits — split into chunks if needed
        // Default ATT MTU is 23 (20 byte payload), but Chrome negotiates higher.
        // writeValueWithoutResponse is preferred for NUS RX characteristic.
        const maxChunk = 240;  // Safe BLE chunk size (below typical negotiated MTU)
        for (let offset = 0; offset < data.length; offset += maxChunk) {
            const chunk = data.slice(offset, offset + maxChunk);
            await this.rxChar.writeValueWithoutResponse(chunk);
        }
    }
}

// ============================================================================
// CDC PROTOCOL (transport-agnostic)
// ============================================================================

class CDCProtocol {
    constructor() {
        this.transport = null;
        this.seq = 0;
        this.rxBuffer = new Uint8Array(0);
        this.pendingCommands = new Map();
        this.eventCallbacks = [];
        this.disconnectCallbacks = [];
        this.connected = false;
    }

    static isSupported() {
        return WebSerialTransport.isSupported() || WebBluetoothTransport.isSupported();
    }

    static isSerialSupported() {
        return WebSerialTransport.isSupported();
    }

    static isBluetoothSupported() {
        return WebBluetoothTransport.isSupported();
    }

    /**
     * Connect via Web Serial (USB CDC)
     */
    async connectSerial() {
        const transport = new WebSerialTransport();
        await this._connectWithTransport(transport);
    }

    /**
     * Try auto-reconnect to a previously-granted serial port (no user prompt)
     * Returns true if reconnected successfully
     */
    async tryAutoConnect() {
        if (!WebSerialTransport.isSupported()) return false;
        const transport = new WebSerialTransport();
        transport.onData = (data) => {
            const newBuffer = new Uint8Array(this.rxBuffer.length + data.length);
            newBuffer.set(this.rxBuffer);
            newBuffer.set(data, this.rxBuffer.length);
            this.rxBuffer = newBuffer;
            this._processBuffer();
        };
        transport.onDisconnect = () => {
            this.connected = false;
            this.transport = null;
            for (const [seq, pending] of this.pendingCommands) {
                pending.reject(new Error('Disconnected'));
            }
            this.pendingCommands.clear();
            this.rxBuffer = new Uint8Array(0);
            for (const cb of this.disconnectCallbacks) {
                try { cb(); } catch (e) {}
            }
        };
        const ok = await transport.tryAutoConnect();
        if (ok) {
            this.transport = transport;
            this.connected = true;
        }
        return ok;
    }

    /**
     * Connect via Web Bluetooth (BLE NUS)
     */
    async connectBluetooth() {
        const transport = new WebBluetoothTransport();
        await this._connectWithTransport(transport);
    }

    /**
     * Legacy connect method — defaults to serial
     */
    async connect() {
        return this.connectSerial();
    }

    async _connectWithTransport(transport) {
        transport.onData = (data) => {
            // Append to buffer
            const newBuffer = new Uint8Array(this.rxBuffer.length + data.length);
            newBuffer.set(this.rxBuffer);
            newBuffer.set(data, this.rxBuffer.length);
            this.rxBuffer = newBuffer;
            this._processBuffer();
        };

        transport.onDisconnect = () => {
            this.connected = false;
            this.transport = null;
            // Reject pending commands
            for (const [seq, pending] of this.pendingCommands) {
                pending.reject(new Error('Disconnected'));
            }
            this.pendingCommands.clear();
            this.rxBuffer = new Uint8Array(0);
            // Notify disconnect listeners
            for (const cb of this.disconnectCallbacks) {
                try { cb(); } catch (e) {}
            }
        };

        await transport.connect();
        this.transport = transport;
        this.connected = true;
    }

    /**
     * Get transport name (USB or BLE)
     */
    get transportName() {
        return this.transport ? this.transport.name : '';
    }

    /**
     * Disconnect from device
     */
    async disconnect() {
        this.connected = false;

        if (this.transport) {
            await this.transport.disconnect();
            this.transport = null;
        }

        // Reject pending commands
        for (const [seq, pending] of this.pendingCommands) {
            pending.reject(new Error('Disconnected'));
        }
        this.pendingCommands.clear();
        this.rxBuffer = new Uint8Array(0);
    }

    /**
     * Register event callback
     */
    onEvent(callback) {
        this.eventCallbacks.push(callback);
        return () => {
            const idx = this.eventCallbacks.indexOf(callback);
            if (idx >= 0) this.eventCallbacks.splice(idx, 1);
        };
    }

    /**
     * Register disconnect callback
     */
    onDisconnect(callback) {
        this.disconnectCallbacks.push(callback);
        return () => {
            const idx = this.disconnectCallbacks.indexOf(callback);
            if (idx >= 0) this.disconnectCallbacks.splice(idx, 1);
        };
    }

    /**
     * Send a command and wait for response
     */
    async sendCommand(cmd, args = null) {
        if (!this.connected) {
            throw new Error('Not connected');
        }

        const seq = this.seq;
        this.seq = (this.seq + 1) & 0xFF;

        const payload = JSON.stringify({ cmd, ...(args && { args }) });
        const packet = buildPacket(MSG_CMD, seq, payload);

        // Create promise for response
        const responsePromise = new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                this.pendingCommands.delete(seq);
                reject(new Error(`Timeout waiting for response to ${cmd}`));
            }, TIMEOUT_MS);

            this.pendingCommands.set(seq, { resolve, reject, timeout, cmd });
        });

        // Send packet
        await this.transport.write(packet);

        return responsePromise;
    }

    /**
     * Process received data buffer
     */
    _processBuffer() {
        while (this.rxBuffer.length >= 7) {
            // Find sync byte
            const syncIdx = this.rxBuffer.indexOf(CDC_SYNC);
            if (syncIdx === -1) {
                this.rxBuffer = new Uint8Array(0);
                break;
            }
            if (syncIdx > 0) {
                this.rxBuffer = this.rxBuffer.slice(syncIdx);
            }

            // Check if we have enough data
            if (this.rxBuffer.length < 3) break;

            const length = this.rxBuffer[1] | (this.rxBuffer[2] << 8);
            const packetLen = 5 + length + 2;

            if (this.rxBuffer.length < packetLen) break;

            // Extract packet
            const type = this.rxBuffer[3];
            const seq = this.rxBuffer[4];
            const payload = this.rxBuffer.slice(5, 5 + length);
            const crcReceived = this.rxBuffer[5 + length] | (this.rxBuffer[6 + length] << 8);

            // Verify CRC
            const crcData = new Uint8Array(2 + length);
            crcData[0] = type;
            crcData[1] = seq;
            crcData.set(payload, 2);
            const crcCalc = crc16(crcData);

            // Consume packet from buffer
            this.rxBuffer = this.rxBuffer.slice(packetLen);

            if (crcReceived !== crcCalc) {
                const hex = Array.from(this.rxBuffer.slice(0, Math.min(packetLen + 10, 64)))
                    .map(b => b.toString(16).padStart(2, '0')).join(' ');
                console.warn(`[CDC] CRC mismatch! type=0x${type.toString(16)} seq=${seq} len=${length} recv=0x${crcReceived.toString(16)} calc=0x${crcCalc.toString(16)}`);
                console.warn(`[CDC] Raw packet: ${Array.from(payload).map(b => b.toString(16).padStart(2, '0')).join(' ')}`);
                console.warn(`[CDC] Payload as text: ${new TextDecoder().decode(payload)}`);
                continue;
            }

            // Handle packet
            this._handlePacket(type, seq, payload);
        }
    }

    /**
     * Handle a received packet
     */
    _handlePacket(type, seq, payload) {
        let data;
        const payloadStr = new TextDecoder().decode(payload);
        try {
            data = JSON.parse(payloadStr);
        } catch (e) {
            console.error('[CDC] JSON parse error:', e);
            data = { raw: Array.from(payload) };
        }

        if (type === MSG_RSP) {
            // Response to command
            const pending = this.pendingCommands.get(seq);
            if (pending) {
                clearTimeout(pending.timeout);
                this.pendingCommands.delete(seq);

                if (data.ok === false) {
                    pending.reject(new Error(data.error || 'Unknown error'));
                } else {
                    pending.resolve(data);
                }
            }
        } else if (type === MSG_EVT) {
            // Async event
            for (const cb of this.eventCallbacks) {
                try {
                    cb(data);
                } catch (e) {
                    console.error('Event callback error:', e);
                }
            }
        } else if (type === MSG_NAK) {
            // NAK - command failed
            const pending = this.pendingCommands.get(seq);
            if (pending) {
                clearTimeout(pending.timeout);
                this.pendingCommands.delete(seq);
                pending.reject(new Error('NAK received'));
            }
        }
    }

    // Convenience methods

    async getInfo() {
        return this.sendCommand('INFO');
    }

    async ping() {
        return this.sendCommand('PING');
    }

    async reboot() {
        return this.sendCommand('REBOOT');
    }

    async bootsel() {
        return this.sendCommand('BOOTSEL');
    }

    async getMode() {
        return this.sendCommand('MODE.GET');
    }

    async setMode(mode) {
        return this.sendCommand('MODE.SET', { mode });
    }

    async listModes() {
        return this.sendCommand('MODE.LIST');
    }

    // BLE Output Mode methods
    async getBleMode() {
        return this.sendCommand('BLE.MODE.GET');
    }

    async setBleMode(mode) {
        return this.sendCommand('BLE.MODE.SET', { mode });
    }

    async listBleModes() {
        return this.sendCommand('BLE.MODE.LIST');
    }

    // Unified Profile methods (supports both built-in and custom profiles)
    async listProfiles() {
        return this.sendCommand('PROFILE.LIST');
    }

    async getProfile(index) {
        return this.sendCommand('PROFILE.GET', { index });
    }

    async setProfile(index) {
        return this.sendCommand('PROFILE.SET', { index });
    }

    async saveProfile(index, data) {
        return this.sendCommand('PROFILE.SAVE', { index, ...data });
    }

    async deleteProfile(index) {
        return this.sendCommand('PROFILE.DELETE', { index });
    }

    async cloneProfile(index, name) {
        return this.sendCommand('PROFILE.CLONE', { index, name });
    }

    async getSettings() {
        return this.sendCommand('SETTINGS.GET');
    }

    async resetSettings() {
        return this.sendCommand('SETTINGS.RESET');
    }

    async getNativeOutput() {
        return this.sendCommand('OUTPUT.NATIVE.GET');
    }

    async setNativeOutput(payload) {
        return this.sendCommand('OUTPUT.NATIVE.SET', payload);
    }

    async enableInputStream(enable = true) {
        return this.sendCommand('INPUT.STREAM', { enable });
    }

    async getBtStatus() {
        return this.sendCommand('BT.STATUS');
    }

    async clearBtBonds() {
        return this.sendCommand('BT.BONDS.CLEAR');
    }

    async forgetBtDevice(addr) {
        return this.sendCommand('BT.FORGET', { addr });
    }

    async getWiimoteOrient() {
        return this.sendCommand('WIIMOTE.ORIENT.GET');
    }

    async setWiimoteOrient(mode) {
        return this.sendCommand('WIIMOTE.ORIENT.SET', { mode });
    }

    async getPlayers() {
        return this.sendCommand('PLAYERS.LIST');
    }

    async testRumble(player, left, right, duration = 500) {
        return this.sendCommand('RUMBLE.TEST', { player, left, right, duration });
    }

    async stopRumble(player = -1) {
        return this.sendCommand('RUMBLE.STOP', { player });
    }

    async enableDebugStream(enable) {
        return this.sendCommand('DEBUG.STREAM', { enable });
    }

    // Router Config
    async getRouter() {
        return this.sendCommand('ROUTER.GET');
    }

    async setRouter(config) {
        return this.sendCommand('ROUTER.SET', config);
    }

    async setDpadMode(mode) {
        return this.sendCommand('ROUTER.DPAD.SET', { mode });
    }

    // Pad GPIO Config
    async getPadConfig() {
        return this.sendCommand('PAD.CONFIG.GET');
    }

    async setPadConfig(config) {
        return this.sendCommand('PAD.CONFIG.SET', config);
    }

    async resetPadConfig() {
        return this.sendCommand('PAD.CONFIG.RESET');
    }

    async getPadPins() {
        return this.sendCommand('PAD.CONFIG.PINS');
    }

}

export { CDCProtocol, WebSerialTransport, WebBluetoothTransport, crc16, buildPacket };
