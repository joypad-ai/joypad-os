/**
 * PS4 Auth — install DualShock 4 local RSA authentication key material.
 *
 * PS4 consoles disconnect an unauthenticated controller after ~8 minutes. With
 * RSA-2048 key material extracted from a genuine DS4, the device can answer the
 * console's auth challenge itself (no real controller on the host port needed).
 *
 * The three inputs are the standard extraction artifacts:
 *   key.prm    — RSA-2048 private key (PKCS#1 or PKCS#8 PEM)
 *   serial.txt — up to 32 hex chars (device serial, right-aligned to 16 bytes)
 *   sig.bin    — 256-byte Sony device signature
 *
 * key.prm is parsed entirely client-side via WebCrypto; only N/E/P/Q (never the
 * PEM) are sent, base64-encoded, in a single PS4AUTH.SET command. The firmware
 * stores them in a dedicated flash sector and activates local auth on the fly.
 */
export class Ps4AuthCard {
    constructor(container, protocol, log) {
        this.el = container;
        this.protocol = protocol;
        this.log = log;
        this.available = false;         // firmware exposes PS4AUTH.* commands
        // Parsed, validated key parts (Uint8Array) — null until each file is OK.
        this.parts = { N: null, E: null, P: null, Q: null, serial: null, sig: null };
    }

    render() {
        this.el.innerHTML = `
            <div class="card">
                <h2>PS4 Authentication</h2>
                <div class="card-content">
                    <p class="hint">
                        Install RSA key material from a genuine DualShock 4 so this device is
                        recognized as an official controller (removes the 8-minute timeout).
                        Keys are parsed in your browser — only the RSA components are sent.
                    </p>

                    <div class="device-info">
                        <div class="row"><span class="label">Status</span><span class="value" id="ps4AuthStatus">—</span></div>
                        <div class="row" id="ps4AuthSerialRow" style="display:none;"><span class="label">Serial</span><span class="value" id="ps4AuthSerial">—</span></div>
                    </div>

                    <div class="ps4-auth-files">
                        <div class="row">
                            <span class="label">key.prm</span>
                            <input type="file" id="ps4KeyFile" accept=".prm,.pem,.key,.txt">
                            <span class="value" id="ps4KeyBadge">—</span>
                        </div>
                        <div class="row">
                            <span class="label">serial.txt</span>
                            <input type="file" id="ps4SerialFile" accept=".txt">
                            <span class="value" id="ps4SerialBadge">—</span>
                        </div>
                        <div class="row">
                            <span class="label">sig.bin</span>
                            <input type="file" id="ps4SigFile" accept=".bin">
                            <span class="value" id="ps4SigBadge">—</span>
                        </div>
                    </div>

                    <div class="buttons">
                        <button id="ps4InstallBtn" disabled>Install Keys</button>
                        <button id="ps4ClearBtn" class="secondary">Clear</button>
                    </div>
                </div>
            </div>`;

        this.el.querySelector('#ps4KeyFile').addEventListener('change', (e) => this.onFile('key', e.target.files[0]));
        this.el.querySelector('#ps4SerialFile').addEventListener('change', (e) => this.onFile('serial', e.target.files[0]));
        this.el.querySelector('#ps4SigFile').addEventListener('change', (e) => this.onFile('sig', e.target.files[0]));
        this.el.querySelector('#ps4InstallBtn').addEventListener('click', () => this.install());
        this.el.querySelector('#ps4ClearBtn').addEventListener('click', () => this.clearKeys());
    }

    isAvailable() {
        return this.available;
    }

    async load() {
        // PS4AUTH.STATUS exists on any USB-output firmware; failure => unsupported
        // build (older firmware / ESP / nRF), so hide the page.
        try {
            const status = await this.protocol.sendCommand('PS4AUTH.STATUS');
            this.available = true;
            this.renderStatus(status);
        } catch (e) {
            this.available = false;
            return;
        }
    }

    renderStatus(status) {
        const statusEl = this.el.querySelector('#ps4AuthStatus');
        const serialRow = this.el.querySelector('#ps4AuthSerialRow');
        const serialEl = this.el.querySelector('#ps4AuthSerial');
        if (status.installed) {
            statusEl.textContent = status.active ? 'Installed — local auth active' : 'Installed — signing inactive (passthrough only)';
            if (status.serial) {
                serialEl.textContent = status.serial;
                serialRow.style.display = '';
            }
        } else {
            statusEl.textContent = 'Not installed';
            serialRow.style.display = 'none';
        }
    }

    // ── File handling ───────────────────────────────────────────────────────

    async onFile(which, file) {
        if (!file) return;
        const badge = this.el.querySelector(
            which === 'key' ? '#ps4KeyBadge' : which === 'serial' ? '#ps4SerialBadge' : '#ps4SigBadge');
        try {
            if (which === 'key') await this.validateKey(file);
            else if (which === 'serial') await this.validateSerial(file);
            else await this.validateSig(file);
            badge.textContent = 'OK ✓';
            this.log(`${file.name}: validated`, 'success');
        } catch (e) {
            if (which === 'key') { this.parts.N = this.parts.E = this.parts.P = this.parts.Q = null; }
            else if (which === 'serial') this.parts.serial = null;
            else this.parts.sig = null;
            badge.textContent = 'Error';
            this.log(`${file.name}: ${e.message}`, 'error');
        }
        this.updateInstallEnabled();
    }

    updateInstallEnabled() {
        const p = this.parts;
        const ready = p.N && p.E && p.P && p.Q && p.serial && p.sig;
        this.el.querySelector('#ps4InstallBtn').disabled = !ready;
    }

    async validateKey(file) {
        const text = await file.text();
        const isPkcs1 = text.includes('-----BEGIN RSA PRIVATE KEY-----');
        const isPkcs8 = text.includes('-----BEGIN PRIVATE KEY-----');
        if (!isPkcs1 && !isPkcs8) {
            throw new Error('not an RSA private key PEM (need BEGIN RSA PRIVATE KEY or BEGIN PRIVATE KEY)');
        }

        const b64 = text.replace(/-----[^-]+-----/g, '').replace(/\s/g, '');
        const der = Uint8Array.from(atob(b64), c => c.charCodeAt(0));
        const pkcs8Der = isPkcs1 ? pkcs1ToPkcs8(der) : der;

        let cryptoKey;
        try {
            cryptoKey = await crypto.subtle.importKey(
                'pkcs8', pkcs8Der, { name: 'RSA-PSS', hash: 'SHA-256' }, true, ['sign']);
        } catch (e) {
            throw new Error('WebCrypto could not import key: ' + e.message + ' (must be RSA-2048)');
        }

        const jwk = await crypto.subtle.exportKey('jwk', cryptoKey);
        if (!jwk.n || !jwk.p || !jwk.q) throw new Error('key missing n/p/q — is it a full private key?');

        const N = b64urlToBytes(jwk.n, 256);
        const E = b64urlToBytes(jwk.e, 4);
        const P = b64urlToBytes(jwk.p, 128);
        const Q = b64urlToBytes(jwk.q, 128);

        // Confirm RSA-2048 and that P×Q=N (so mbedTLS will accept it).
        const big = arr => BigInt('0x' + Array.from(arr).map(b => b.toString(16).padStart(2, '0')).join(''));
        const nBig = big(N);
        if (nBig.toString(2).length < 2048) throw new Error(`key must be RSA-2048 (got ${nBig.toString(2).length} bits)`);
        if (big(P) * big(Q) !== nBig) throw new Error('key verification failed: P×Q ≠ N');

        this.parts.N = N; this.parts.E = E; this.parts.P = P; this.parts.Q = Q;
    }

    async validateSerial(file) {
        let hex = (await file.text()).trim().replace(/\s/g, '');
        if (!/^[0-9a-fA-F]{1,32}$/.test(hex)) throw new Error('serial.txt must be up to 32 hex chars');
        if (hex.length % 2 !== 0) throw new Error('serial.txt must have an even number of hex chars');
        hex = hex.padStart(32, '0');
        const serial = new Uint8Array(16);
        for (let i = 0; i < 16; i++) serial[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
        this.parts.serial = serial;
    }

    async validateSig(file) {
        const buf = await file.arrayBuffer();
        if (buf.byteLength !== 256) throw new Error(`sig.bin must be exactly 256 bytes (got ${buf.byteLength})`);
        this.parts.sig = new Uint8Array(buf);
    }

    // ── Commands ────────────────────────────────────────────────────────────

    async install() {
        const p = this.parts;
        if (!(p.N && p.E && p.P && p.Q && p.serial && p.sig)) {
            this.log('Select and validate all three files first', 'error');
            return;
        }
        try {
            this.log('Installing PS4 key material...');
            const status = await this.protocol.sendCommand('PS4AUTH.SET', {
                N: bytesToBase64(p.N),
                E: bytesToBase64(p.E),
                P: bytesToBase64(p.P),
                Q: bytesToBase64(p.Q),
                serial: bytesToBase64(p.serial),
                signature: bytesToBase64(p.sig),
            });
            this.log('PS4 keys installed', 'success');
            // Firmware returns {status:"ready"|"failed"}; refresh the full status.
            if (status && status.status === 'failed') {
                this.log('Firmware reported key load failed — check the key is a valid RSA-2048 DS4 key', 'error');
            }
            await this.refreshStatus();
        } catch (e) {
            this.log(`PS4AUTH.SET failed: ${e.message}`, 'error');
        }
    }

    async clearKeys() {
        if (!confirm('Erase installed PS4 key material from the device?')) return;
        try {
            await this.protocol.sendCommand('PS4AUTH.CLEAR');
            this.log('PS4 keys cleared', 'success');
            await this.refreshStatus();
        } catch (e) {
            this.log(`PS4AUTH.CLEAR failed: ${e.message}`, 'error');
        }
    }

    async refreshStatus() {
        try {
            this.renderStatus(await this.protocol.sendCommand('PS4AUTH.STATUS'));
        } catch (e) { /* ignore */ }
    }
}

// ── Standalone key helpers (ported from tools/ps4-auth-upload) ──────────────

// Wrap a PKCS#1 DER private key in a PKCS#8 envelope so WebCrypto can import it.
function pkcs1ToPkcs8(pkcs1Der) {
    const algId = new Uint8Array([
        0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01, 0x05, 0x00,
    ]);
    const version = new Uint8Array([0x02, 0x01, 0x00]);
    const encLen = n =>
        n < 0x80 ? new Uint8Array([n]) :
        n < 0x100 ? new Uint8Array([0x81, n]) :
        new Uint8Array([0x82, (n >> 8) & 0xFF, n & 0xFF]);

    const osTagLen = encLen(pkcs1Der.length);
    const os = new Uint8Array(1 + osTagLen.length + pkcs1Der.length);
    os[0] = 0x04;
    os.set(osTagLen, 1);
    os.set(pkcs1Der, 1 + osTagLen.length);

    const innerLen = version.length + algId.length + os.length;
    const outerTagLen = encLen(innerLen);
    const out = new Uint8Array(1 + outerTagLen.length + innerLen);
    out[0] = 0x30;
    out.set(outerTagLen, 1);
    let off = 1 + outerTagLen.length;
    out.set(version, off); off += version.length;
    out.set(algId, off); off += algId.length;
    out.set(os, off);
    return out;
}

// Decode a base64url string to a Uint8Array, right-aligned to `length` bytes.
function b64urlToBytes(b64url, length) {
    const b64 = b64url.replace(/-/g, '+').replace(/_/g, '/');
    const padded = b64 + '=='.slice((b64.length % 4) || 4);
    const raw = atob(padded);
    const bytes = new Uint8Array(length);
    const off = length - raw.length;
    for (let i = 0; i < raw.length; i++) bytes[off + i] = raw.charCodeAt(i);
    return bytes;
}

function bytesToBase64(bytes) {
    let bin = '';
    for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
    return btoa(bin);
}
