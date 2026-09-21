import { DirtyTracker } from './dirty-tracker.js';

/** Bluetooth Device Output Page — BLE mode only */
export class BtOutputCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
        this.hasBle = false;
    }

    render() {
        this.el.innerHTML = `
            <div class="card" id="bleModeCard" style="display:none;">
                <h2>BLE Device Output Mode</h2>
                <div class="card-content">
                    <div class="row">
                        <span class="label">Current Mode</span>
                        <select id="bleModeSelect"><option value="">Loading...</option></select>
                    </div>
                    <div class="buttons" style="margin-top: 12px;">
                        <button id="bleModeSaveBtn">Save &amp; Reboot</button>
                    </div>
                    <p class="hint" style="margin-top: 8px;">Device will reboot to apply changes.</p>
                </div>
            </div>
            <div class="card" id="wirelessPolicyCard" style="display:none;">
                <h2>USB / BLE Priority</h2>
                <div class="card-content">
                    <div class="row">
                        <span class="label">Active Outputs</span>
                        <select id="wirelessPolicySelect">
                            <option value="0">Both (USB + BLE)</option>
                            <option value="1">USB dominant</option>
                            <option value="2">BLE dominant</option>
                        </select>
                    </div>
                    <p class="hint" style="margin-top: 8px;">
                        Both: input goes to USB and BLE together.
                        USB dominant: input goes only to USB while a USB host is
                        connected (Bluetooth stays paired, just idle).
                        BLE dominant: input goes only to BLE while a BLE host is
                        connected (USB stays enumerated, config keeps working).
                        Applies immediately, no reboot.
                    </p>
                </div>
            </div>`;

        this.el.querySelector('#bleModeSaveBtn').addEventListener('click', () => this.save());
        this.el.querySelector('#wirelessPolicySelect').addEventListener('change', () => this.savePolicy());
        this.dirty = new DirtyTracker(this.el.querySelector('#bleModeCard'), this.el.querySelector('#bleModeSaveBtn'));
    }

    async load() {
        const card = this.el.querySelector('#bleModeCard');
        try {
            const result = await this.protocol.listBleModes();
            const select = this.el.querySelector('#bleModeSelect');
            select.innerHTML = '';
            for (const mode of result.modes) {
                const opt = document.createElement('option');
                opt.value = mode.id;
                opt.textContent = mode.name;
                opt.selected = mode.id === result.current;
                select.appendChild(opt);
            }
            card.style.display = '';
            this.hasBle = true;
            this.currentModeId = result.current;
            this.log(`Loaded ${result.modes.length} BLE modes, current: ${result.current}`);
            this.dirty?.snapshot();
        } catch (e) {
            card.style.display = 'none';
        }
        await this.loadPolicy();
    }

    async loadPolicy() {
        const card = this.el.querySelector('#wirelessPolicyCard');
        try {
            const result = await this.protocol.getWirelessPolicy();
            this.el.querySelector('#wirelessPolicySelect').value = String(result.policy);
            card.style.display = '';
        } catch (e) {
            // Firmware without WIRELESS.POLICY support — hide the card.
            card.style.display = 'none';
        }
    }

    async savePolicy() {
        const policy = parseInt(this.el.querySelector('#wirelessPolicySelect').value);
        try {
            const result = await this.protocol.setWirelessPolicy(policy);
            this.log(`Wireless policy set to ${result.name}`, 'success');
        } catch (e) {
            this.log(`Failed to set wireless policy: ${e.message}`, 'error');
        }
    }

    async save() {
        const id = parseInt(this.el.querySelector('#bleModeSelect').value);
        if (id === this.currentModeId) {
            this.log('BLE mode unchanged', 'success');
            return;
        }
        try {
            this.log(`Setting BLE mode to ${id}...`);
            const result = await this.protocol.setBleMode(id);
            this.log(`BLE mode set to ${result.name}`, 'success');
            if (result.reboot) this.log('Device will reboot...', 'warning');
        } catch (e) {
            this.log(`Failed to set BLE mode: ${e.message}`, 'error');
        }
    }

    isAvailable() { return this.hasBle; }
}
