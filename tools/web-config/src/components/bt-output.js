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
                    <div class="toggle-row" style="margin-bottom: 4px;">
                        <label class="toggle">
                            <input type="checkbox" id="bleOutputEnable">
                            <span class="toggle-slider"></span>
                        </label>
                        <span>Enable BLE device output</span>
                    </div>
                    <p class="hint" id="bleOutputHint">Advertise as a Bluetooth controller.
                        Turn off for a receiver-only setup so the adapter is not a BLE
                        peripheral and a central at the same time &mdash; with Bluetooth
                        Host also off, the BT stack is not started at all.</p>
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
        } catch (e) {
            card.style.display = 'none';
        }

        // Enable toggle: value from ROUTER.GET, availability from CAPS.GET. Older
        // firmware has neither, so default to on and hide the toggle rather than
        // showing it off while the radio is actually advertising.
        try {
            const router = await this.protocol.getRouter();
            const has = router && router.ble_output !== undefined;
            this.el.querySelector('#bleOutputEnable').checked = has ? !!router.ble_output : true;
            this.enableSupported = has;
            if (!has) this.setEnableVisible(false);
        } catch (e) {
            this.enableSupported = false;
            this.setEnableVisible(false);
        }
        try {
            const caps = await this.protocol.getCapabilities();
            const bd = caps && caps.bt_device;
            if (bd && bd.present && !bd.configurable) this.setEnableReadOnly(true);
        } catch (e) { /* older firmware: leave as-is */ }

        this.dirty?.snapshot();
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
        // The enable toggle lives in this card, so an unchanged mode is no longer a
        // reason to bail -- that early return used to make a toggle-only edit a
        // silent no-op.
        await this.saveEnable();

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

    // ROUTER.SET applies only the keys present in the payload, but it reboots every
    // time -- so skip the write entirely when the toggle has not moved, otherwise
    // saving a mode change would also bounce the device twice.
    async saveEnable() {
        if (!this.enableSupported || this.enableReadOnly) return;
        const want = this.el.querySelector('#bleOutputEnable').checked;
        try {
            const router = await this.protocol.getRouter();
            if (!!router.ble_output === want) return;
            await this.protocol.setRouter({ ble_output: want });
            this.log(`BLE device output ${want ? 'enabled' : 'disabled'}`, 'success');
            this.log('Device will reboot...', 'warning');
        } catch (e) {
            this.log(`Failed to set BLE device output: ${e.message}`, 'error');
        }
    }

    setEnableVisible(v) {
        const row = this.el.querySelector('#bleOutputEnable')?.closest('.toggle-row');
        if (row) row.style.display = v ? '' : 'none';
        const hint = this.el.querySelector('#bleOutputHint');
        if (hint) hint.style.display = v ? '' : 'none';
    }

    setEnableReadOnly(ro) {
        this.enableReadOnly = ro;
        const cb = this.el.querySelector('#bleOutputEnable');
        if (cb) { cb.checked = true; cb.disabled = ro; }
        const hint = this.el.querySelector('#bleOutputHint');
        if (ro && hint) hint.textContent = 'This build is a BLE device only, so the output is always on.';
    }

    isAvailable() { return this.hasBle; }
}
