/** Router Page — Input routing and D-Pad mode configuration */
export class RouterCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
    }

    render() {
        this.el.innerHTML = `
            <div class="card">
                <h2>Router</h2>
                <div class="card-content">
                    <div class="pad-form-row">
                        <span class="label">Routing Mode</span>
                        <select id="routingMode">
                            <option value="0">Simple (1:1)</option>
                            <option value="1">Merge (N:1)</option>
                            <option value="2" disabled>Broadcast (1:N)</option>
                        </select>
                    </div>
                    <div class="pad-form-row" id="mergeModeRow">
                        <span class="label">Merge Mode</span>
                        <select id="mergeMode">
                            <option value="0">Priority</option>
                            <option value="1">Blend</option>
                            <option value="2">Latest</option>
                        </select>
                    </div>
                    <div class="pad-form-row">
                        <span class="label">D-Pad Mode</span>
                        <select id="dpadMode">
                            <option value="0">D-Pad</option>
                            <option value="1">Left Stick</option>
                            <option value="2">Right Stick</option>
                        </select>
                    </div>
                    <div class="pad-form-row">
                        <span class="label">Bluetooth Input</span>
                        <label class="toggle"><input type="checkbox" id="btInput"><span class="toggle-slider"></span></label>
                        <span class="hint">Scan for BT/BLE controllers</span>
                    </div>
                    <div class="buttons" style="margin-top: 12px;">
                        <button id="routerSaveBtn">Save &amp; Reboot</button>
                    </div>
                    <p class="hint" style="margin-top: 8px;">Device will reboot to apply changes.</p>
                </div>
            </div>`;

        this.el.querySelector('#routingMode').addEventListener('change', (e) => {
            this.el.querySelector('#mergeModeRow').style.display = e.target.value === '1' ? '' : 'none';
        });
        this.el.querySelector('#routerSaveBtn').addEventListener('click', () => this.save());
    }

    async load() {
        try {
            const config = await this.protocol.getRouter();
            this.el.querySelector('#routingMode').value = config.routing_mode || 0;
            this.el.querySelector('#mergeMode').value = config.merge_mode || 0;
            this.el.querySelector('#dpadMode').value = config.dpad_mode || 0;
            this.el.querySelector('#btInput').checked = config.bt_input || false;
            this.el.querySelector('#mergeModeRow').style.display =
                (config.routing_mode || 0) === 1 ? '' : 'none';
        } catch (e) {
            this.log(`Failed to load router config: ${e.message}`, 'error');
        }
    }

    async save() {
        if (!confirm('Save router configuration? The device will reboot.')) return;

        const config = {
            routing_mode: parseInt(this.el.querySelector('#routingMode').value),
            merge_mode: parseInt(this.el.querySelector('#mergeMode').value),
            dpad_mode: parseInt(this.el.querySelector('#dpadMode').value),
            bt_input: this.el.querySelector('#btInput').checked,
        };

        try {
            this.log('Saving router config...');
            const result = await this.protocol.setRouter(config);
            this.log(result.reboot ? 'Config saved. Device rebooting...' : 'Config saved.', 'success');
        } catch (e) {
            this.log(`Failed to save router config: ${e.message}`, 'error');
        }
    }
}
