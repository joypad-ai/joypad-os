import { DirtyTracker } from './dirty-tracker.js';

/** Bluetooth Host (Input) — scan, status, bonds, Wiimote orientation */
export class BtHostCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
        this.visible = false;
    }

    render() {
        this.el.innerHTML = `
            <div class="card" id="btHostCard" style="display:none;">
                <h2>Bluetooth Host</h2>
                <div class="card-content">
                    <div class="toggle-row" style="margin-bottom: 4px;">
                        <label class="toggle">
                            <input type="checkbox" id="btInputEnable">
                            <span class="toggle-slider"></span>
                        </label>
                        <span>Enable Bluetooth Host</span>
                    </div>
                    <p class="hint">Use onboard BT radio to scan for controllers. USB BT dongles work independently when plugged in.</p>
                    <div class="buttons" style="margin-top: 12px;">
                        <button id="btHostSaveBtn">Save &amp; Reboot</button>
                    </div>
                    <p class="hint" style="margin-top: 8px;">Device will reboot to apply changes.</p>
                </div>
            </div>

            <div class="card" id="btScanCard" style="display:none;">
                <h2 style="display: flex; align-items: center; justify-content: space-between;">
                    <span>Status</span>
                    <span style="display: flex; align-items: center; gap: 8px;">
                        <span id="btStatusText">—</span>
                        <span class="bt-status-dot" id="btStatusDot"></span>
                    </span>
                </h2>
                <div class="card-content">
                    <div class="row" id="btTransportRow">
                        <span class="label">Transport</span>
                        <span class="value" id="btTransportText">—</span>
                    </div>
                    <div id="btDevicesList" style="display:none;"></div>
                </div>
            </div>

            <div class="card" id="btWiimoteCard" style="display:none;">
                <h2>Wiimote</h2>
                <div class="card-content">
                    <div class="row">
                        <span class="label">Orientation</span>
                        <select id="wiimoteOrientSelect">
                            <option value="0">Auto</option>
                            <option value="1">Horizontal</option>
                            <option value="2">Vertical</option>
                        </select>
                    </div>
                    <p class="hint">Controls D-pad mapping when using Wiimote alone (no extension).</p>
                </div>
            </div>

            <div class="card" id="btBondsCard" style="display:none;">
                <h2>Bluetooth Bonds</h2>
                <div class="card-content">
                    <p class="hint">Clear all saved Bluetooth pairings. Devices will need to re-pair.</p>
                    <div>
                        <button class="secondary" id="clearBtBtn">Clear All Bonds</button>
                    </div>
                </div>
            </div>`;

        this.el.querySelector('#btHostSaveBtn').addEventListener('click', () => this.saveBtHost());
        this.dirty = new DirtyTracker(this.el.querySelector('#btHostCard'), this.el.querySelector('#btHostSaveBtn'));
        this.el.querySelector('#wiimoteOrientSelect').addEventListener('change', (e) => this.setWiimoteOrient(e.target.value));
        this.el.querySelector('#clearBtBtn').addEventListener('click', () => this.clearBtBonds());
    }

    async load() {
        await this.loadWiimoteOrient();
        await this.loadBtStatus();
    }

    async loadWiimoteOrient() {
        const card = this.el.querySelector('#btWiimoteCard');
        try {
            const result = await this.protocol.getWiimoteOrient();
            this.el.querySelector('#wiimoteOrientSelect').value = result.mode;
            card.style.display = '';
        } catch (e) {
            card.style.display = 'none';
        }
    }

    async loadBtStatus() {
        const hostCard = this.el.querySelector('#btHostCard');
        const scanCard = this.el.querySelector('#btScanCard');
        const bondsCard = this.el.querySelector('#btBondsCard');
        try {
            const status = await this.protocol.getBtStatus();
            hostCard.style.display = '';
            bondsCard.style.display = '';
            this.visible = true;

            // Load BT input toggle from router settings (only once on first load)
            if (!this._toggleLoaded) {
                try {
                    const router = await this.protocol.getRouter();
                    this.el.querySelector('#btInputEnable').checked = router.bt_input || false;
                } catch (e) {}
                this._toggleLoaded = true;
                this.dirty?.snapshot();
            }

            // Scanning card only shown when BT is active
            if (!status.enabled) {
                scanCard.style.display = 'none';
                return;
            }
            scanCard.style.display = '';

            const statusText = this.el.querySelector('#btStatusText');
            const statusDot = this.el.querySelector('#btStatusDot');
            const devicesList = this.el.querySelector('#btDevicesList');

            statusDot.className = 'bt-status-dot';
            if (status.connections > 0) {
                statusDot.classList.add('connected');
                statusText.textContent = `Connected (${status.connections})`;
            } else if (status.scanning) {
                statusDot.classList.add('scanning');
                statusText.textContent = 'Scanning';
            } else {
                statusText.textContent = 'Idle';
            }

            // Show transport
            this.el.querySelector('#btTransportText').textContent = status.transport || '—';

            const devices = status.devices || [];
            if (devices.length > 0) {
                devicesList.style.display = '';
                devicesList.innerHTML = devices.map(d => {
                    const meta = d.connected
                        ? `${d.addr} · ${d.ble ? 'BLE' : 'Classic'}${d.vid ? ' · VID:' + d.vid + ' PID:' + d.pid : ''}`
                        : `${d.addr} · ${d.ble ? 'BLE' : 'Classic'} · bonded`;
                    return `
                        <div class="bt-device-row">
                            <span class="bt-status-dot ${d.connected ? 'connected' : 'idle'}"></span>
                            <div style="flex: 1;">
                                <div class="bt-device-name">${d.name || 'Bonded device'}</div>
                                <div class="bt-device-meta">${meta}</div>
                            </div>
                            <button class="secondary bt-forget-btn" data-addr="${d.addr}" style="padding: 4px 10px; font-size: 12px;">Forget</button>
                        </div>
                    `;
                }).join('');
                devicesList.querySelectorAll('.bt-forget-btn').forEach(btn => {
                    btn.addEventListener('click', () => this.forgetDevice(btn.dataset.addr));
                });
            } else {
                devicesList.style.display = 'none';
            }

            if (!this._statusInterval) {
                this._statusInterval = setInterval(() => this.loadBtStatus(), 2000);
            }
        } catch (e) {
            hostCard.style.display = 'none';
            bondsCard.style.display = 'none';
        }
    }

    async saveBtHost() {
        if (!confirm('Save Bluetooth Host configuration? The device will reboot.')) return;
        try {
            const router = await this.protocol.getRouter();
            await this.protocol.setRouter({
                routing_mode: router.routing_mode || 0,
                merge_mode: router.merge_mode || 0,
                dpad_mode: router.dpad_mode || 0,
                bt_input: this.el.querySelector('#btInputEnable').checked,
            });
        } catch (e) {
            this.log(`Failed to save: ${e.message}`, 'error');
        }
    }

    async setWiimoteOrient(mode) {
        try {
            const result = await this.protocol.setWiimoteOrient(parseInt(mode));
            this.log(`Wiimote orientation: ${result.name}`, 'success');
        } catch (e) {
            this.log(`Failed to set orientation: ${e.message}`, 'error');
        }
    }

    async clearBtBonds() {
        if (!confirm('Clear all Bluetooth bonds? Devices will need to re-pair.')) return;
        try {
            await this.protocol.clearBtBonds();
            this.log('Bluetooth bonds cleared', 'success');
            await this.loadBtStatus();
        } catch (e) {
            this.log(`Failed to clear bonds: ${e.message}`, 'error');
        }
    }

    async forgetDevice(addr) {
        if (!confirm(`Forget device ${addr}? It will be disconnected and bond removed.`)) return;
        try {
            await this.protocol.forgetBtDevice(addr);
            this.log(`Forgot device ${addr}`, 'success');
            await this.loadBtStatus();
        } catch (e) {
            this.log(`Failed to forget device: ${e.message}`, 'error');
        }
    }

    isAvailable() { return this.visible; }
}
