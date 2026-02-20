/**
 * CDC Protocol - Binary framed communication with Joypad devices
 *
 * Packet format:
 * [SYNC:0xAA][LENGTH:2][TYPE:1][SEQ:1][PAYLOAD:N][CRC16:2]
 */

const CDC_SYNC = 0xAA;
const MSG_CMD = 0x01;
const MSG_RSP = 0x02;
const MSG_EVT = 0x03;
const MSG_ACK = 0x04;
const MSG_NAK = 0x05;
const MSG_DAT = 0x10;

const TIMEOUT_MS = 2000;

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

/**
 * CDC Protocol handler for Web Serial
 */
class CDCProtocol {
    constructor() {
        this.port = null;
        this.reader = null;
        this.writer = null;
        this.seq = 0;
        this.rxBuffer = new Uint8Array(0);
        this.pendingCommands = new Map();
        this.eventCallbacks = [];
        this.connected = false;
        this.readLoopRunning = false;
    }

    /**
     * Check if Web Serial is supported
     */
    static isSupported() {
        return 'serial' in navigator;
    }

    /**
     * Connect to a device
     */
    async connect() {
        if (!CDCProtocol.isSupported()) {
            throw new Error('Web Serial not supported in this browser');
        }

        // Request port from user
        this.port = await navigator.serial.requestPort({
            filters: [
                // Add VID/PID filters if needed
            ]
        });

        await this.port.open({ baudRate: 115200 });

        this.writer = this.port.writable.getWriter();
        this.reader = this.port.readable.getReader();
        this.connected = true;

        // Start read loop
        this._startReadLoop();

        return true;
    }

    /**
     * Disconnect from device
     */
    async disconnect() {
        this.connected = false;
        this.readLoopRunning = false;

        if (this.reader) {
            try {
                await this.reader.cancel();
                this.reader.releaseLock();
            } catch (e) {}
            this.reader = null;
        }

        if (this.writer) {
            try {
                this.writer.releaseLock();
            } catch (e) {}
            this.writer = null;
        }

        if (this.port) {
            try {
                await this.port.close();
            } catch (e) {}
            this.port = null;
        }

        // Reject pending commands
        for (const [seq, pending] of this.pendingCommands) {
            pending.reject(new Error('Disconnected'));
        }
        this.pendingCommands.clear();
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
        await this.writer.write(packet);

        return responsePromise;
    }

    /**
     * Background read loop
     */
    async _startReadLoop() {
        this.readLoopRunning = true;

        while (this.readLoopRunning && this.reader) {
            try {
                const { value, done } = await this.reader.read();
                if (done) break;

                // Append to buffer
                const newBuffer = new Uint8Array(this.rxBuffer.length + value.length);
                newBuffer.set(this.rxBuffer);
                newBuffer.set(value, this.rxBuffer.length);
                this.rxBuffer = newBuffer;

                // Process packets
                this._processBuffer();
            } catch (e) {
                if (this.readLoopRunning) {
                    console.error('Read error:', e);
                }
                break;
            }
        }
    }

    /**
     * Process received data buffer
     */
    _processBuffer() {
        console.log('[CDC] Processing buffer, length:', this.rxBuffer.length, 'data:', Array.from(this.rxBuffer.slice(0, 20)).map(b => b.toString(16).padStart(2, '0')).join(' '));

        while (this.rxBuffer.length >= 7) {
            // Find sync byte
            const syncIdx = this.rxBuffer.indexOf(CDC_SYNC);
            console.log('[CDC] Sync byte index:', syncIdx);
            if (syncIdx === -1) {
                console.log('[CDC] No sync byte found, clearing buffer');
                this.rxBuffer = new Uint8Array(0);
                break;
            }
            if (syncIdx > 0) {
                console.log('[CDC] Skipping', syncIdx, 'bytes to sync');
                this.rxBuffer = this.rxBuffer.slice(syncIdx);
            }

            // Check if we have enough data
            if (this.rxBuffer.length < 3) break;

            const length = this.rxBuffer[1] | (this.rxBuffer[2] << 8);
            const packetLen = 5 + length + 2;
            console.log('[CDC] Packet length field:', length, 'total packet:', packetLen, 'buffer has:', this.rxBuffer.length);

            if (this.rxBuffer.length < packetLen) {
                console.log('[CDC] Not enough data yet, waiting...');
                break;
            }

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

            console.log('[CDC] Packet type:', type, 'seq:', seq, 'CRC recv:', crcReceived.toString(16), 'calc:', crcCalc.toString(16));

            // Consume packet from buffer
            this.rxBuffer = this.rxBuffer.slice(packetLen);

            if (crcReceived !== crcCalc) {
                console.warn('[CDC] CRC mismatch! Dropping packet');
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
        console.log('[CDC] Received packet type:', type, 'seq:', seq, 'payload:', payloadStr);
        try {
            data = JSON.parse(payloadStr);
            console.log('[CDC] Parsed JSON:', data);
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

    async enableInputStream(enable = true) {
        return this.sendCommand('INPUT.STREAM', { enable });
    }

    async getBtStatus() {
        return this.sendCommand('BT.STATUS');
    }

    async clearBtBonds() {
        return this.sendCommand('BT.BONDS.CLEAR');
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

}

export { CDCProtocol, crc16, buildPacket };
