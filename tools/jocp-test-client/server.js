/**
 * JOCP Test Client Server
 *
 * This Node.js server bridges between a browser frontend (WebSocket) and
 * the Joypad dongle (UDP). Browsers can't send UDP directly, so this
 * server acts as a relay.
 *
 * Usage:
 *   1. Connect laptop WiFi to JOYPAD-XXXX network (password: joypad1234)
 *   2. npm install && npm start
 *   3. Open http://localhost:3000 in browser
 *   4. Use gamepad or on-screen buttons to send input
 */

const dgram = require('dgram');
const http = require('http');
const fs = require('fs');
const path = require('path');
const WebSocket = require('ws');

// Configuration
const CONFIG = {
    // Dongle IP (default for Joypad AP)
    dongleIp: process.env.DONGLE_IP || '192.168.4.1',
    donglePort: parseInt(process.env.DONGLE_PORT || '30100'),

    // Local server
    httpPort: parseInt(process.env.HTTP_PORT || '3001'),

    // Packet rate (Hz)
    pollRate: parseInt(process.env.POLL_RATE || '125'),
};

// JOCP Protocol Constants
const JOCP_MAGIC = 0x4A50;  // "JP" little-endian
const JOCP_VERSION = 0x01;
const JOCP_MSG_INPUT = 0x01;
const JOCP_FLAG_KEYFRAME = 0x04;

// Current controller state
let controllerState = {
    buttons: 0,
    lx: 0, ly: 0,  // Left stick (-32768 to 32767)
    rx: 0, ry: 0,  // Right stick
    lt: 0, rt: 0,  // Triggers (0 to 65535)
};

let sequenceNumber = 0;

// Create UDP socket and bind to receive rumble feedback
const udpSocket = dgram.createSocket('udp4');
udpSocket.bind(CONFIG.donglePort, () => {
    console.log(`UDP socket bound to port ${CONFIG.donglePort} for rumble feedback`);
});

// Build JOCP input packet (76 bytes)
function buildJocpPacket() {
    const buffer = Buffer.alloc(76);
    let offset = 0;

    // Header (12 bytes)
    buffer.writeUInt16LE(JOCP_MAGIC, offset); offset += 2;
    buffer.writeUInt8(JOCP_VERSION, offset); offset += 1;
    buffer.writeUInt8(JOCP_MSG_INPUT, offset); offset += 1;
    buffer.writeUInt16LE(sequenceNumber++ & 0xFFFF, offset); offset += 2;
    buffer.writeUInt16LE(JOCP_FLAG_KEYFRAME, offset); offset += 2;
    // Timestamp in microseconds, wrapped to 32-bit
    const timestamp = Number(BigInt(Date.now()) * 1000n % 0x100000000n);
    buffer.writeUInt32LE(timestamp, offset); offset += 4;

    // Payload (64 bytes)
    // Buttons (4 bytes)
    buffer.writeUInt32LE(controllerState.buttons, offset); offset += 4;

    // Sticks (8 bytes) - signed 16-bit
    buffer.writeInt16LE(controllerState.lx, offset); offset += 2;
    buffer.writeInt16LE(controllerState.ly, offset); offset += 2;
    buffer.writeInt16LE(controllerState.rx, offset); offset += 2;
    buffer.writeInt16LE(controllerState.ry, offset); offset += 2;

    // Triggers (4 bytes) - unsigned 16-bit
    buffer.writeUInt16LE(controllerState.lt, offset); offset += 2;
    buffer.writeUInt16LE(controllerState.rt, offset); offset += 2;

    // IMU (12 bytes) - zeros for now
    offset += 12;

    // IMU timestamp (4 bytes)
    buffer.writeUInt32LE(timestamp, offset); offset += 4;

    // Touch (12 bytes) - zeros
    offset += 12;

    // Battery/status (2 bytes)
    buffer.writeUInt8(100, offset); offset += 1;  // Battery 100%
    buffer.writeUInt8(0, offset); offset += 1;    // Not charging/wired

    // Controller ID (1 byte)
    buffer.writeUInt8(0, offset); offset += 1;

    // Reserved (17 bytes) - zeros
    // Already zeroed by Buffer.alloc

    return buffer;
}

// Send packet to dongle
function sendPacket() {
    const packet = buildJocpPacket();
    udpSocket.send(packet, CONFIG.donglePort, CONFIG.dongleIp, (err) => {
        if (err) {
            console.error('UDP send error:', err.message);
        }
    });
}

// Start periodic sending
let sendInterval = null;

function startSending() {
    if (sendInterval) return;
    const intervalMs = Math.floor(1000 / CONFIG.pollRate);
    sendInterval = setInterval(sendPacket, intervalMs);
    console.log(`Started sending at ${CONFIG.pollRate}Hz to ${CONFIG.dongleIp}:${CONFIG.donglePort}`);
}

function stopSending() {
    if (sendInterval) {
        clearInterval(sendInterval);
        sendInterval = null;
        console.log('Stopped sending');
    }
}

// HTTP server for static files
const httpServer = http.createServer((req, res) => {
    let filePath = req.url === '/' ? '/index.html' : req.url;
    filePath = path.join(__dirname, 'public', filePath);

    const ext = path.extname(filePath);
    const contentTypes = {
        '.html': 'text/html',
        '.js': 'application/javascript',
        '.css': 'text/css',
    };

    fs.readFile(filePath, (err, data) => {
        if (err) {
            res.writeHead(404);
            res.end('Not found');
            return;
        }
        res.writeHead(200, { 'Content-Type': contentTypes[ext] || 'text/plain' });
        res.end(data);
    });
});

// WebSocket server for browser communication
const wss = new WebSocket.Server({ server: httpServer });

wss.on('connection', (ws) => {
    console.log('Browser connected');
    startSending();

    ws.on('message', (data) => {
        try {
            const msg = JSON.parse(data);

            if (msg.type === 'state') {
                // Full state update
                controllerState = {
                    buttons: msg.buttons || 0,
                    lx: msg.lx || 0,
                    ly: msg.ly || 0,
                    rx: msg.rx || 0,
                    ry: msg.ry || 0,
                    lt: msg.lt || 0,
                    rt: msg.rt || 0,
                };
            } else if (msg.type === 'config') {
                // Configuration update
                if (msg.dongleIp) CONFIG.dongleIp = msg.dongleIp;
                if (msg.donglePort) CONFIG.donglePort = msg.donglePort;
                if (msg.pollRate) {
                    CONFIG.pollRate = msg.pollRate;
                    if (sendInterval) {
                        stopSending();
                        startSending();
                    }
                }
                console.log('Config updated:', CONFIG);
            }
        } catch (e) {
            console.error('Invalid message:', e.message);
        }
    });

    ws.on('close', () => {
        console.log('Browser disconnected');
        // Keep sending for a bit in case of reconnect
        setTimeout(() => {
            if (wss.clients.size === 0) {
                stopSending();
            }
        }, 5000);
    });
});

// Start server
httpServer.listen(CONFIG.httpPort, () => {
    console.log(`
JOCP Test Client
================
HTTP server: http://localhost:${CONFIG.httpPort}
Target dongle: ${CONFIG.dongleIp}:${CONFIG.donglePort}
Poll rate: ${CONFIG.pollRate} Hz

Instructions:
1. Connect laptop WiFi to JOYPAD-XXXX (password: joypad1234)
2. Open http://localhost:${CONFIG.httpPort} in your browser
3. Use a gamepad or the on-screen buttons

Environment variables:
  DONGLE_IP   - Dongle IP address (default: 192.168.4.1)
  DONGLE_PORT - UDP port (default: 30100)
  HTTP_PORT   - HTTP server port (default: 3000)
  POLL_RATE   - Packets per second (default: 125)
`);
});

// Handle incoming UDP packets (feedback from dongle)
udpSocket.on('message', (msg, rinfo) => {
    if (msg.length < 12) return;  // Too short for header

    const magic = msg.readUInt16LE(0);
    const version = msg.readUInt8(2);
    const msgType = msg.readUInt8(3);

    if (magic !== JOCP_MAGIC || version !== JOCP_VERSION) return;

    // OUTPUT_CMD = 0x04
    if (msgType === 0x04 && msg.length >= 13) {
        const cmdType = msg.readUInt8(12);

        let feedback = null;

        // RUMBLE = 0x01
        if (cmdType === 0x01 && msg.length >= 19) {
            const leftAmp = msg.readUInt8(13);
            const rightAmp = msg.readUInt8(15);
            console.log(`Rumble: L=${leftAmp} R=${rightAmp}`);
            feedback = { type: 'rumble', left: leftAmp, right: rightAmp };
        }
        // PLAYER_LED = 0x02
        else if (cmdType === 0x02 && msg.length >= 14) {
            const index = msg.readUInt8(13);
            console.log(`Player LED: ${index}`);
            feedback = { type: 'player_led', index };
        }
        // RGB_LED = 0x03
        else if (cmdType === 0x03 && msg.length >= 16) {
            const r = msg.readUInt8(13);
            const g = msg.readUInt8(14);
            const b = msg.readUInt8(15);
            console.log(`RGB LED: ${r}, ${g}, ${b}`);
            feedback = { type: 'rgb_led', r, g, b };
        }

        // Forward to browser
        if (feedback) {
            wss.clients.forEach(ws => {
                if (ws.readyState === WebSocket.OPEN) {
                    ws.send(JSON.stringify(feedback));
                }
            });
        }
    }
});

// Handle UDP errors
udpSocket.on('error', (err) => {
    console.error('UDP error:', err.message);
});

// Cleanup on exit
process.on('SIGINT', () => {
    console.log('\nShutting down...');
    stopSending();
    udpSocket.close();
    httpServer.close();
    process.exit(0);
});
