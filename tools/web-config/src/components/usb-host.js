import { DirtyTracker } from './dirty-tracker.js';

/** USB Host Configuration Page */
export class UsbHostCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
        this.visible = false;
    }

    render() {
        this.el.innerHTML = `
            <div class="card" id="usbHostCard" style="display:none;">
                <h2>USB Host</h2>
                <div class="card-content">
                    <div class="toggle-row" style="margin-bottom: 12px;">
                        <label class="toggle">
                            <input type="checkbox" id="usbHostEnabled">
                            <span class="toggle-slider"></span>
                        </label>
                        <span>Enable USB Host</span>
                    </div>
                    <div id="usbHostPins" style="display:none;">
                        <div class="pad-form-row">
                            <span class="label">D+ Pin</span>
                            <input type="number" id="usbHostDp" min="0" max="28" value="0">
                        </div>
                        <div class="pad-form-row">
                            <span class="label">D- Pin</span>
                            <input type="number" id="usbHostDm" disabled value="1">
                            <span class="hint">Always D+ next pin</span>
                        </div>
                    </div>
                    <div class="buttons" style="margin-top: 12px;">
                        <button id="usbHostSaveBtn">Save &amp; Reboot</button>
                    </div>
                    <p class="hint" style="margin-top: 8px;">Device will reboot to apply changes.</p>
                </div>
            </div>`;

        this.el.querySelector('#usbHostEnabled').addEventListener('change', () => this.togglePins());
        this.el.querySelector('#usbHostDp').addEventListener('input', () => this.updateDm());
        this.el.querySelector('#usbHostSaveBtn').addEventListener('click', () => this.save());
        this.dirty = new DirtyTracker(this.el, this.el.querySelector('#usbHostSaveBtn'));
    }

    togglePins() {
        const enabled = this.el.querySelector('#usbHostEnabled').checked;
        this.el.querySelector('#usbHostPins').style.display = enabled ? '' : 'none';
    }

    updateDm() {
        const dp = parseInt(this.el.querySelector('#usbHostDp').value);
        this.el.querySelector('#usbHostDm').value = dp + 1;
    }

    async load() {
        const card = this.el.querySelector('#usbHostCard');
        try {
            const config = await this.protocol.getPadConfig();
            if (!config.ok || config.name === 'none') {
                card.style.display = 'none';
                this.visible = false;
                return;
            }
            card.style.display = '';
            this.visible = true;
            // Tri-state semantic for usb_host_dp:
            //   > 0  → enabled, override pin
            //   == 0 → enabled, use compile-time default (sysDp)
            //   < 0  → explicitly disabled by user
            const dp = config.usb_host_dp !== undefined ? config.usb_host_dp : 0;
            const sysDp = config.sys_usb_host_dp !== undefined ? config.sys_usb_host_dp : -1;
            const enabled = dp >= 0 && (dp > 0 || sysDp >= 0);
            const shownPin = dp > 0 ? dp : (sysDp >= 0 ? sysDp : 0);
            this.el.querySelector('#usbHostEnabled').checked = enabled;
            this.el.querySelector('#usbHostDp').value = shownPin;
            this.togglePins();
            this.updateDm();
            this.currentConfig = config;
            this.dirty?.snapshot();
        } catch (e) {
            card.style.display = 'none';
            this.visible = false;
        }
    }

    async save() {
        if (!confirm('Save USB host configuration? The device will reboot.')) return;

        // Re-send the full pad config with updated usb_host_dp
        if (!this.currentConfig) return;

        const config = {
            name: this.currentConfig.name || 'Custom',
            active_high: this.currentConfig.active_high || false,
            dpad_toggle_invert: this.currentConfig.dpad_toggle_invert || false,
            invert_lx: this.currentConfig.invert_lx || false,
            invert_ly: this.currentConfig.invert_ly || false,
            invert_rx: this.currentConfig.invert_rx || false,
            invert_ry: this.currentConfig.invert_ry || false,
            i2c_sda: this.currentConfig.i2c_sda !== undefined ? this.currentConfig.i2c_sda : -1,
            i2c_scl: this.currentConfig.i2c_scl !== undefined ? this.currentConfig.i2c_scl : -1,
            deadzone: this.currentConfig.deadzone || 10,
            buttons: this.currentConfig.buttons || [],
            dpad_toggle: this.currentConfig.dpad_toggle !== undefined ? this.currentConfig.dpad_toggle : -1,
            adc: this.currentConfig.adc || [-1, -1, -1, -1],
            led_pin: this.currentConfig.led_pin !== undefined ? this.currentConfig.led_pin : -1,
            led_count: this.currentConfig.led_count || 0,
            speaker_pin: this.currentConfig.speaker_pin !== undefined ? this.currentConfig.speaker_pin : -1,
            speaker_enable_pin: this.currentConfig.speaker_enable_pin !== undefined ? this.currentConfig.speaker_enable_pin : -1,
            // Save mirrors firmware tri-state:
            //   unchecked → -1 (explicitly disabled)
            //   checked + pin == sysDp → 0 (use board default)
            //   checked + pin != sysDp → pin (override)
            usb_host_dp: (() => {
                if (!this.el.querySelector('#usbHostEnabled').checked) return -1;
                const p = parseInt(this.el.querySelector('#usbHostDp').value);
                const sys = this.currentConfig?.sys_usb_host_dp;
                return (sys != null && p === sys) ? 0 : p;
            })(),
        };

        try {
            this.log('Saving USB host config...');
            const result = await this.protocol.setPadConfig(config);
            this.log(result.reboot ? 'Config saved. Device rebooting...' : 'Config saved.', 'success');
        } catch (e) {
            this.log(`Failed to save USB host config: ${e.message}`, 'error');
        }
    }

    isAvailable() { return this.visible; }
}
