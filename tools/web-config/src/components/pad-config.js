/** GPIO Pin Configuration Card */

const PAD_BUTTON_NAMES = [
    'D-Up', 'D-Down', 'D-Left', 'D-Right',
    'B1', 'B2', 'B3', 'B4',
    'L1', 'R1', 'L2', 'R2',
    'S1', 'S2', 'L3', 'R3',
    'A1', 'A2', 'L4', 'R4'
];

const ADC_OPTIONS = `
    <option value="-1">Disabled</option>
    <option value="0">ADC 0 (GPIO 26)</option>
    <option value="1">ADC 1 (GPIO 27)</option>
    <option value="2">ADC 2 (GPIO 28)</option>
    <option value="3">ADC 3 (GPIO 29)</option>`;

function adcRow(label, id) {
    return `<div class="pad-pin-row">
        <span>${label}</span>
        <select id="${id}" class="pad-adc-select">${ADC_OPTIONS}</select>
        <label class="pad-invert"><input type="checkbox" id="${id}Invert"> Invert</label>
    </div>`;
}

export class PadConfigCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
    }

    render() {
        this.el.innerHTML = `
            <div class="card" id="padConfigCard" style="display:none;">
                <h2>GPIO Pin Configuration</h2>
                <div class="card-content">
                    <div class="row">
                        <span class="label">Config Name</span>
                        <input type="text" id="padConfigName" maxlength="15" placeholder="Custom" style="width: 150px;">
                    </div>
                    <div class="row">
                        <span class="label">Source</span>
                        <span class="value" id="padConfigSource">-</span>
                    </div>
                    <div class="row">
                        <span class="label">Active Level</span>
                        <select id="padActiveHigh">
                            <option value="false">Active Low (pull-up, GND = pressed)</option>
                            <option value="true">Active High (pull-down, VCC = pressed)</option>
                        </select>
                    </div>

                    <h3 style="margin-top: 15px; margin-bottom: 10px;">Button Pin Assignments</h3>
                    <div id="padButtonPins" class="pad-pin-grid"></div>

                    <h3 style="margin-top: 15px; margin-bottom: 10px;">D-pad Toggle Switch</h3>
                    <div class="row">
                        <span class="label">Toggle Pin</span>
                        <input type="number" id="padDpadToggle" min="-1" max="29" value="-1" style="width: 80px;">
                    </div>
                    <div class="checkbox-row">
                        <input type="checkbox" id="padDpadToggleInvert">
                        <label for="padDpadToggleInvert">Invert toggle (HIGH = D-pad mode)</label>
                    </div>

                    <h3 style="margin-top: 15px; margin-bottom: 10px;">Analog Sticks (ADC)</h3>
                    <div class="pad-pin-grid">
                        ${adcRow('Left X', 'padAdcLX')}
                        ${adcRow('Left Y', 'padAdcLY')}
                        ${adcRow('Right X', 'padAdcRX')}
                        ${adcRow('Right Y', 'padAdcRY')}
                    </div>
                    <div class="row" style="margin-top: 10px;">
                        <span class="label">Deadzone</span>
                        <input type="range" id="padDeadzone" min="0" max="127" value="10" style="width: 120px;">
                        <span id="padDeadzoneValue">10</span>
                    </div>

                    <h3 style="margin-top: 15px; margin-bottom: 10px;">I2C Expander</h3>
                    <div class="row">
                        <span class="label">SDA Pin</span>
                        <input type="number" id="padI2cSda" min="-1" max="29" value="-1" style="width: 80px;">
                    </div>
                    <div class="row">
                        <span class="label">SCL Pin</span>
                        <input type="number" id="padI2cScl" min="-1" max="29" value="-1" style="width: 80px;">
                    </div>

                    <h3 style="margin-top: 15px; margin-bottom: 10px;">NeoPixel LEDs</h3>
                    <div class="row">
                        <span class="label">LED Pin</span>
                        <input type="number" id="padLedPin" min="-1" max="29" value="-1" style="width: 80px;">
                    </div>
                    <div class="row">
                        <span class="label">LED Count</span>
                        <input type="number" id="padLedCount" min="0" max="16" value="0" style="width: 80px;">
                    </div>

                    <h3 style="margin-top: 15px; margin-bottom: 10px;">Speaker</h3>
                    <div class="row">
                        <span class="label">Speaker Pin</span>
                        <input type="number" id="padSpeakerPin" min="-1" max="29" value="-1" style="width: 80px;">
                    </div>
                    <div class="row">
                        <span class="label">Enable Pin</span>
                        <input type="number" id="padSpeakerEnablePin" min="-1" max="29" value="-1" style="width: 80px;">
                    </div>

                    <div id="padPinConflicts" class="pad-conflicts" style="display:none;"></div>

                    <div class="buttons" style="margin-top: 15px;">
                        <button id="padSaveBtn">Save &amp; Reboot</button>
                        <button id="padResetBtn" class="secondary">Reset to Default</button>
                    </div>
                    <p class="hint" style="margin-top: 8px;">Saving will reboot the device to apply the new pin configuration.</p>
                </div>
            </div>`;

        this.el.querySelector('#padSaveBtn').addEventListener('click', () => this.save());
        this.el.querySelector('#padResetBtn').addEventListener('click', () => this.reset());
        this.el.querySelector('#padDeadzone').addEventListener('input', (e) => {
            this.el.querySelector('#padDeadzoneValue').textContent = e.target.value;
        });
    }

    buildPinSelect(id, value) {
        let html = `<select id="${id}" class="pad-pin-select">`;
        html += '<option value="-1">Disabled</option>';
        for (let i = 0; i <= 29; i++) html += `<option value="${i}"${value === i ? ' selected' : ''}>GPIO ${i}</option>`;
        for (let i = 100; i <= 115; i++) html += `<option value="${i}"${value === i ? ' selected' : ''}>I2C0 P${i - 100}</option>`;
        for (let i = 200; i <= 215; i++) html += `<option value="${i}"${value === i ? ' selected' : ''}>I2C1 P${i - 200}</option>`;
        html += '</select>';
        return html;
    }

    async load() {
        const card = this.el.querySelector('#padConfigCard');
        try {
            const config = await this.protocol.getPadConfig();

            if (!config.ok || config.name === 'none') {
                card.style.display = 'none';
                return;
            }

            card.style.display = '';

            this.el.querySelector('#padConfigName').value = config.name || '';
            this.el.querySelector('#padConfigSource').textContent =
                config.source === 'flash' ? 'Custom (Flash)' : `Default: ${config.name}`;
            this.el.querySelector('#padActiveHigh').value = String(config.active_high || false);

            // Button pins
            const container = this.el.querySelector('#padButtonPins');
            const buttons = config.buttons || [];
            container.innerHTML = PAD_BUTTON_NAMES.map((name, i) =>
                `<div class="pad-pin-row"><span>${name}</span>${this.buildPinSelect('padBtn' + i, buttons[i] !== undefined ? buttons[i] : -1)}</div>`
            ).join('');

            // D-pad toggle
            this.el.querySelector('#padDpadToggle').value = config.dpad_toggle !== undefined ? config.dpad_toggle : -1;
            this.el.querySelector('#padDpadToggleInvert').checked = config.dpad_toggle_invert || false;

            // ADC
            const adc = config.adc || [-1, -1, -1, -1];
            this.el.querySelector('#padAdcLX').value = adc[0];
            this.el.querySelector('#padAdcLY').value = adc[1];
            this.el.querySelector('#padAdcRX').value = adc[2];
            this.el.querySelector('#padAdcRY').value = adc[3];
            this.el.querySelector('#padAdcLXInvert').checked = config.invert_lx || false;
            this.el.querySelector('#padAdcLYInvert').checked = config.invert_ly || false;
            this.el.querySelector('#padAdcRXInvert').checked = config.invert_rx || false;
            this.el.querySelector('#padAdcRYInvert').checked = config.invert_ry || false;

            // Deadzone
            this.el.querySelector('#padDeadzone').value = config.deadzone || 10;
            this.el.querySelector('#padDeadzoneValue').textContent = config.deadzone || 10;

            // I2C
            this.el.querySelector('#padI2cSda').value = config.i2c_sda !== undefined ? config.i2c_sda : -1;
            this.el.querySelector('#padI2cScl').value = config.i2c_scl !== undefined ? config.i2c_scl : -1;

            // LED
            this.el.querySelector('#padLedPin').value = config.led_pin !== undefined ? config.led_pin : -1;
            this.el.querySelector('#padLedCount').value = config.led_count || 0;

            // Speaker
            this.el.querySelector('#padSpeakerPin').value = config.speaker_pin !== undefined ? config.speaker_pin : -1;
            this.el.querySelector('#padSpeakerEnablePin').value = config.speaker_enable_pin !== undefined ? config.speaker_enable_pin : -1;

            // Conflict detection
            container.addEventListener('change', () => this.checkConflicts());
            this.checkConflicts();

            this.log(`Pad config loaded: ${config.name} (${config.source})`);
        } catch (e) {
            card.style.display = 'none';
            this.log(`Pad config not available: ${e.message}`);
        }
    }

    checkConflicts() {
        const pinCounts = {};
        const conflicts = [];

        for (let i = 0; i < 20; i++) {
            const sel = this.el.querySelector('#padBtn' + i);
            if (!sel) continue;
            const pin = parseInt(sel.value);
            if (pin < 0) continue;
            if (!pinCounts[pin]) pinCounts[pin] = [];
            pinCounts[pin].push(PAD_BUTTON_NAMES[i]);
            sel.classList.remove('conflict');
        }

        for (const [pin, names] of Object.entries(pinCounts)) {
            if (names.length > 1) {
                conflicts.push(`Pin ${pin} used by: ${names.join(', ')}`);
                for (let i = 0; i < 20; i++) {
                    const sel = this.el.querySelector('#padBtn' + i);
                    if (sel && parseInt(sel.value) === parseInt(pin)) sel.classList.add('conflict');
                }
            }
        }

        const adcIds = ['padAdcLX', 'padAdcLY', 'padAdcRX', 'padAdcRY'];
        const adcLabels = ['Left X', 'Left Y', 'Right X', 'Right Y'];
        for (let a = 0; a < 4; a++) {
            const ch = parseInt(this.el.querySelector('#' + adcIds[a]).value);
            if (ch < 0) continue;
            const gpio = 26 + ch;
            if (pinCounts[gpio]) {
                conflicts.push(`GPIO ${gpio} used as both ADC (${adcLabels[a]}) and digital (${pinCounts[gpio].join(', ')})`);
            }
        }

        const el = this.el.querySelector('#padPinConflicts');
        if (conflicts.length > 0) {
            el.innerHTML = conflicts.map(c => `<p>Warning: ${c}</p>`).join('');
            el.style.display = '';
        } else {
            el.style.display = 'none';
        }
    }

    async save() {
        this.checkConflicts();
        const conflictEl = this.el.querySelector('#padPinConflicts');
        if (conflictEl.style.display !== 'none') {
            if (!confirm('There are pin conflicts. Save anyway?')) return;
        }
        if (!confirm('Save GPIO configuration? The device will reboot.')) return;

        const buttons = [];
        for (let i = 0; i < 22; i++) {
            const sel = this.el.querySelector('#padBtn' + i);
            buttons.push(sel ? parseInt(sel.value) : -1);
        }

        const config = {
            name: this.el.querySelector('#padConfigName').value || 'Custom',
            active_high: this.el.querySelector('#padActiveHigh').value === 'true',
            dpad_toggle_invert: this.el.querySelector('#padDpadToggleInvert').checked,
            invert_lx: this.el.querySelector('#padAdcLXInvert').checked,
            invert_ly: this.el.querySelector('#padAdcLYInvert').checked,
            invert_rx: this.el.querySelector('#padAdcRXInvert').checked,
            invert_ry: this.el.querySelector('#padAdcRYInvert').checked,
            i2c_sda: parseInt(this.el.querySelector('#padI2cSda').value),
            i2c_scl: parseInt(this.el.querySelector('#padI2cScl').value),
            deadzone: parseInt(this.el.querySelector('#padDeadzone').value),
            buttons,
            dpad_toggle: parseInt(this.el.querySelector('#padDpadToggle').value),
            adc: [
                parseInt(this.el.querySelector('#padAdcLX').value),
                parseInt(this.el.querySelector('#padAdcLY').value),
                parseInt(this.el.querySelector('#padAdcRX').value),
                parseInt(this.el.querySelector('#padAdcRY').value),
            ],
            led_pin: parseInt(this.el.querySelector('#padLedPin').value),
            led_count: parseInt(this.el.querySelector('#padLedCount').value),
            speaker_pin: parseInt(this.el.querySelector('#padSpeakerPin').value),
            speaker_enable_pin: parseInt(this.el.querySelector('#padSpeakerEnablePin').value),
        };

        try {
            this.log('Saving pad GPIO config...');
            const result = await this.protocol.setPadConfig(config);
            this.log(result.reboot ? 'Config saved. Device rebooting...' : 'Config saved.', 'success');
        } catch (e) {
            this.log(`Failed to save pad config: ${e.message}`, 'error');
        }
    }

    async reset() {
        if (!confirm('Reset GPIO configuration to compile-time default? The device will reboot.')) return;
        try {
            this.log('Resetting pad config...');
            await this.protocol.resetPadConfig();
            this.log('Config reset. Device rebooting...', 'success');
        } catch (e) {
            this.log(`Failed to reset pad config: ${e.message}`, 'error');
        }
    }
}
