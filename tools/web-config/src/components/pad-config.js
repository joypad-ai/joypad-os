/** Buttons & Pins Configuration — Tabbed Sub-Sections */

const PAD_BUTTON_NAMES = [
    'D-Up', 'D-Down', 'D-Left', 'D-Right',
    'B1', 'B2', 'B3', 'B4',
    'L1', 'R1', 'L2', 'R2',
    'S1', 'S2', 'L3', 'R3',
    'A1', 'A2', 'A3', 'A4', 'L4', 'R4'
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
                <h2>Custom</h2>
                <div class="sub-tabs">
                    <button class="sub-tab active" data-tab="buttons">Buttons</button>
                    <button class="sub-tab" data-tab="analog">Analog</button>
                    <button class="sub-tab" data-tab="toggles">Toggles</button>
                    <button class="sub-tab" data-tab="hardware">Misc</button>
                </div>

                <!-- Buttons Tab -->
                <div class="sub-tab-content active" id="tabButtons" data-tab="buttons">
                    <div class="pad-form-row">
                        <span class="label">Active Level</span>
                        <select id="padActiveHigh">
                            <option value="false">Active Low (GND = pressed)</option>
                            <option value="true">Active High (VCC = pressed)</option>
                        </select>
                    </div>

                    <h3 style="margin-top: 12px; margin-bottom: 8px;">Pin Assignments</h3>
                    <div id="padButtonPins" class="pad-pin-grid two-col"></div>

                </div>

                <!-- Toggles Tab -->
                <div class="sub-tab-content" id="tabToggles" data-tab="toggles">
                    ${[0,1].map(i => `
                    <div style="margin-bottom: 10px;">
                        <div class="toggle-row" style="margin-bottom: 8px;">
                            <label class="toggle">
                                <input type="checkbox" id="padToggle${i}Enabled">
                                <span class="toggle-slider"></span>
                            </label>
                            <span>Toggle ${i + 1}</span>
                        </div>
                        <div id="padToggle${i}Pins" style="display:none;">
                            <div class="pad-form-row">
                                <span class="label">Pin</span>
                                <input type="number" id="padToggle${i}Pin" min="0" max="48" value="0">
                            </div>
                            <div class="pad-form-row">
                                <span class="label">Active Level</span>
                                <select id="padToggle${i}Inv">
                                    <option value="0">Active High (VCC = active)</option>
                                    <option value="1">Active Low (GND = active)</option>
                                </select>
                            </div>
                            <div class="pad-form-row">
                                <span class="label">Function</span>
                                <select id="padToggle${i}Func">
                                    <option value="1">D-pad to Left Stick</option>
                                    <option value="2">D-pad to Right Stick</option>
                                </select>
                            </div>
                        </div>
                    </div>`).join('')}
                    <p class="hint">Toggle switches act like held buttons, mapped to config changes instead of button presses.</p>
                </div>

                <!-- Analog Tab -->
                <div class="sub-tab-content" id="tabAnalog" data-tab="analog">
                    <h3 style="margin-bottom: 8px;">Stick Assignments (ADC)</h3>
                    <div class="pad-pin-grid">
                        ${adcRow('Left X', 'padAdcLX')}
                        ${adcRow('Left Y', 'padAdcLY')}
                        ${adcRow('Right X', 'padAdcRX')}
                        ${adcRow('Right Y', 'padAdcRY')}
                        ${adcRow('Left Trigger', 'padAdcLT')}
                        ${adcRow('Right Trigger', 'padAdcRT')}
                    </div>
                    <div class="pad-form-row" style="margin-top: 12px;">
                        <span class="label">Deadzone</span>
                        <div style="display: flex; align-items: center; gap: 8px;">
                            <input type="range" id="padDeadzone" min="0" max="127" value="10" style="width: 120px;">
                            <span id="padDeadzoneValue">10</span>
                        </div>
                    </div>
                </div>

                <!-- Hardware Tab -->
                <div class="sub-tab-content" id="tabHardware" data-tab="hardware">
                    <h3 style="margin-bottom: 8px;">JoyWing (Seesaw I2C)</h3>
                    ${[0,1].map(i => `
                    <div style="margin-bottom: 12px;">
                        <div class="toggle-row" style="margin-bottom: 8px;">
                            <label class="toggle">
                                <input type="checkbox" id="padJoywing${i}Enabled" data-jw="${i}">
                                <span class="toggle-slider"></span>
                            </label>
                            <span>JoyWing ${i + 1}</span>
                        </div>
                        <div id="padJoywing${i}Pins" style="display:none;">
                            <div class="pad-form-row">
                                <span class="label">I2C Bus</span>
                                <select id="padJoywing${i}Bus">
                                    <option value="0">I2C0</option>
                                    <option value="1">I2C1</option>
                                </select>
                            </div>
                            <div class="pad-form-row">
                                <span class="label">SDA Pin</span>
                                <input type="number" id="padJoywing${i}Sda" min="0" max="48" value="3">
                            </div>
                            <div class="pad-form-row">
                                <span class="label">SCL Pin</span>
                                <input type="number" id="padJoywing${i}Scl" min="0" max="48" value="4">
                            </div>
                            <div class="pad-form-row">
                                <span class="label">Address</span>
                                <select id="padJoywing${i}Addr">
                                    <option value="73">0x49 (Default)</option>
                                    <option value="74">0x4A</option>
                                    <option value="75">0x4B</option>
                                    <option value="76">0x4C</option>
                                </select>
                            </div>
                        </div>
                    </div>`).join('')}

                    <h3 style="margin-top: 12px; margin-bottom: 8px;">I2C Expander</h3>
                    <div class="pad-form-row">
                        <span class="label">SDA Pin</span>
                        <input type="number" id="padI2cSda" min="-1" max="47" value="-1">
                    </div>
                    <div class="pad-form-row">
                        <span class="label">SCL Pin</span>
                        <input type="number" id="padI2cScl" min="-1" max="47" value="-1">
                    </div>

                    <h3 style="margin-top: 12px; margin-bottom: 8px;">Speaker</h3>
                    <div class="pad-form-row">
                        <span class="label">Speaker Pin</span>
                        <input type="number" id="padSpeakerPin" min="-1" max="47" value="-1">
                    </div>
                    <div class="pad-form-row">
                        <span class="label">Enable Pin</span>
                        <input type="number" id="padSpeakerEnablePin" min="-1" max="47" value="-1">
                    </div>
                </div>

                <div id="padPinConflicts" class="pad-conflicts" style="display:none;"></div>

                <div class="buttons" style="margin-top: 16px;">
                    <button id="padSaveBtn">Save &amp; Reboot</button>
                    <button id="padResetBtn" class="secondary">Reset to Default</button>
                </div>
                <p class="hint" style="margin-top: 8px;">Saving will reboot the device to apply changes.</p>
            </div>`;

        // Sub-tab switching
        this.el.querySelectorAll('.sub-tab').forEach(tab => {
            tab.addEventListener('click', () => {
                this.el.querySelectorAll('.sub-tab').forEach(t => t.classList.remove('active'));
                this.el.querySelectorAll('.sub-tab-content').forEach(c => c.classList.remove('active'));
                tab.classList.add('active');
                this.el.querySelector(`.sub-tab-content[data-tab="${tab.dataset.tab}"]`).classList.add('active');
            });
        });

        for (let i = 0; i < 2; i++) {
            this.el.querySelector(`#padToggle${i}Enabled`).addEventListener('change', () => {
                this.el.querySelector(`#padToggle${i}Pins`).style.display =
                    this.el.querySelector(`#padToggle${i}Enabled`).checked ? '' : 'none';
            });
            this.el.querySelector(`#padJoywing${i}Enabled`).addEventListener('change', () => {
                this.el.querySelector(`#padJoywing${i}Pins`).style.display =
                    this.el.querySelector(`#padJoywing${i}Enabled`).checked ? '' : 'none';
            });
        }
        this.el.querySelector('#padSaveBtn').addEventListener('click', () => this.save());
        this.el.querySelector('#padResetBtn').addEventListener('click', () => this.reset());
        this.el.querySelector('#padDeadzone').addEventListener('input', (e) => {
            this.el.querySelector('#padDeadzoneValue').textContent = e.target.value;
        });
    }

    hasI2C() {
        const sda = parseInt(this.el.querySelector('#padI2cSda')?.value ?? -1);
        const scl = parseInt(this.el.querySelector('#padI2cScl')?.value ?? -1);
        return sda >= 0 && scl >= 0;
    }

    buildPinSelect(id, value, includeI2C) {
        let html = `<select id="${id}" class="pad-pin-select">`;
        html += `<option value="-1"${value < 0 ? ' selected' : ''}>Disabled</option>`;
        for (let i = 0; i <= 47; i++) html += `<option value="${i}"${value === i ? ' selected' : ''}>GPIO ${i}</option>`;
        if (includeI2C) {
            for (let i = 100; i <= 115; i++) html += `<option value="${i}"${value === i ? ' selected' : ''}>I2C0 P${i - 100}</option>`;
            for (let i = 200; i <= 215; i++) html += `<option value="${i}"${value === i ? ' selected' : ''}>I2C1 P${i - 200}</option>`;
        }
        html += '</select>';
        return html;
    }

    rebuildPinSelects() {
        const includeI2C = this.hasI2C();
        const container = this.el.querySelector('#padButtonPins');
        if (!container) return;
        const values = [];
        for (let i = 0; i < PAD_BUTTON_NAMES.length; i++) {
            const sel = this.el.querySelector('#padBtn' + i);
            values.push(sel ? parseInt(sel.value) : -1);
        }
        container.innerHTML = PAD_BUTTON_NAMES.map((name, i) =>
            `<div class="pad-pin-row"><span>${name}</span>${this.buildPinSelect('padBtn' + i, values[i], includeI2C)}</div>`
        ).join('');
        container.addEventListener('change', () => this.checkConflicts());
        this.checkConflicts();
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

            this.el.querySelector('#padActiveHigh').value = String(config.active_high || false);

            // I2C (set before button pins so buildPinSelect can check hasI2C)
            this.el.querySelector('#padI2cSda').value = config.i2c_sda !== undefined ? config.i2c_sda : -1;
            this.el.querySelector('#padI2cScl').value = config.i2c_scl !== undefined ? config.i2c_scl : -1;

            this.el.querySelector('#padI2cSda').addEventListener('change', () => this.rebuildPinSelects());
            this.el.querySelector('#padI2cScl').addEventListener('change', () => this.rebuildPinSelects());

            // Button pins
            const container = this.el.querySelector('#padButtonPins');
            const buttons = config.buttons || [];
            const includeI2C = this.hasI2C();
            container.innerHTML = PAD_BUTTON_NAMES.map((name, i) =>
                `<div class="pad-pin-row"><span>${name}</span>${this.buildPinSelect('padBtn' + i, buttons[i] !== undefined ? buttons[i] : -1, includeI2C)}</div>`
            ).join('');

            // Toggle switches: array of [pin, function, flags]
            const toggles = config.toggles || [];
            for (let i = 0; i < 2; i++) {
                const t = toggles[i] || [-1, 0, 0];
                const enabled = t[0] >= 0 && t[1] > 0;
                this.el.querySelector(`#padToggle${i}Enabled`).checked = enabled;
                this.el.querySelector(`#padToggle${i}Pins`).style.display = enabled ? '' : 'none';
                if (enabled) {
                    this.el.querySelector(`#padToggle${i}Pin`).value = t[0];
                    this.el.querySelector(`#padToggle${i}Func`).value = t[1];
                    this.el.querySelector(`#padToggle${i}Inv`).value = (t[2] & 1) ? '1' : '0';
                }
            }

            // ADC
            const adc = config.adc || [-1, -1, -1, -1, -1, -1];
            this.el.querySelector('#padAdcLX').value = adc[0];
            this.el.querySelector('#padAdcLY').value = adc[1];
            this.el.querySelector('#padAdcRX').value = adc[2];
            this.el.querySelector('#padAdcRY').value = adc[3];
            this.el.querySelector('#padAdcLT').value = adc[4] !== undefined ? adc[4] : -1;
            this.el.querySelector('#padAdcRT').value = adc[5] !== undefined ? adc[5] : -1;
            this.el.querySelector('#padAdcLXInvert').checked = config.invert_lx || false;
            this.el.querySelector('#padAdcLYInvert').checked = config.invert_ly || false;
            this.el.querySelector('#padAdcRXInvert').checked = config.invert_rx || false;
            this.el.querySelector('#padAdcRYInvert').checked = config.invert_ry || false;

            // Deadzone
            this.el.querySelector('#padDeadzone').value = config.deadzone || 10;
            this.el.querySelector('#padDeadzoneValue').textContent = config.deadzone || 10;

            // Speaker
            this.el.querySelector('#padSpeakerPin').value = config.speaker_pin !== undefined ? config.speaker_pin : -1;
            this.el.querySelector('#padSpeakerEnablePin').value = config.speaker_enable_pin !== undefined ? config.speaker_enable_pin : -1;

            // JoyWing (array of [bus, sda, scl])
            // JoyWing: array of [bus, sda, scl, addr]
            const jw = config.joywing || [];
            for (let i = 0; i < 2; i++) {
                const slot = jw[i] || [0, -1, -1, 0x49];
                const enabled = slot[1] >= 0;  // sda is index 1
                this.el.querySelector(`#padJoywing${i}Enabled`).checked = enabled;
                this.el.querySelector(`#padJoywing${i}Pins`).style.display = enabled ? '' : 'none';
                if (enabled) {
                    this.el.querySelector(`#padJoywing${i}Bus`).value = slot[0];
                    this.el.querySelector(`#padJoywing${i}Sda`).value = slot[1];
                    this.el.querySelector(`#padJoywing${i}Scl`).value = slot[2];
                    this.el.querySelector(`#padJoywing${i}Addr`).value = slot[3] || 73;
                }
            }

            // Conflict detection
            container.addEventListener('change', () => this.checkConflicts());
            this.checkConflicts();

            this.currentConfig = config;
            this.log(`Pad config loaded: ${config.name} (${config.source})`);
        } catch (e) {
            card.style.display = 'none';
            this.log(`Pad config not available: ${e.message}`);
        }
    }

    checkConflicts() {
        const pinCounts = {};
        const conflicts = [];

        for (let i = 0; i < PAD_BUTTON_NAMES.length; i++) {
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
                for (let i = 0; i < PAD_BUTTON_NAMES.length; i++) {
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
        if (!confirm('Save configuration? The device will reboot.')) return;

        const buttons = [];
        for (let i = 0; i < 22; i++) {
            const sel = this.el.querySelector('#padBtn' + i);
            buttons.push(sel ? parseInt(sel.value) : -1);
        }

        const config = {
            name: 'Custom',
            active_high: this.el.querySelector('#padActiveHigh').value === 'true',
            invert_lx: this.el.querySelector('#padAdcLXInvert').checked,
            invert_ly: this.el.querySelector('#padAdcLYInvert').checked,
            invert_rx: this.el.querySelector('#padAdcRXInvert').checked,
            invert_ry: this.el.querySelector('#padAdcRYInvert').checked,
            i2c_sda: parseInt(this.el.querySelector('#padI2cSda').value),
            i2c_scl: parseInt(this.el.querySelector('#padI2cScl').value),
            deadzone: parseInt(this.el.querySelector('#padDeadzone').value),
            buttons,
            ...(() => {
                const tg = {};
                for (let i = 0; i < 2; i++) {
                    const enabled = this.el.querySelector(`#padToggle${i}Enabled`).checked;
                    tg[`toggle${i}_pin`] = enabled ? parseInt(this.el.querySelector(`#padToggle${i}Pin`).value) : -1;
                    tg[`toggle${i}_func`] = enabled ? parseInt(this.el.querySelector(`#padToggle${i}Func`).value) : 0;
                    tg[`toggle${i}_inv`] = enabled ? parseInt(this.el.querySelector(`#padToggle${i}Inv`).value) : 0;
                }
                return tg;
            })(),
            adc: [
                parseInt(this.el.querySelector('#padAdcLX').value),
                parseInt(this.el.querySelector('#padAdcLY').value),
                parseInt(this.el.querySelector('#padAdcRX').value),
                parseInt(this.el.querySelector('#padAdcRY').value),
                parseInt(this.el.querySelector('#padAdcLT').value),
                parseInt(this.el.querySelector('#padAdcRT').value),
            ],
            led_pin: this.currentConfig?.led_pin !== undefined ? this.currentConfig.led_pin : -1,
            led_count: this.currentConfig?.led_count || 0,
            speaker_pin: parseInt(this.el.querySelector('#padSpeakerPin').value),
            speaker_enable_pin: parseInt(this.el.querySelector('#padSpeakerEnablePin').value),
            ...(() => {
                const jw = {};
                for (let i = 0; i < 2; i++) {
                    const enabled = this.el.querySelector(`#padJoywing${i}Enabled`).checked;
                    jw[`joywing${i}_bus`] = enabled ? parseInt(this.el.querySelector(`#padJoywing${i}Bus`).value) : 0;
                    jw[`joywing${i}_sda`] = enabled ? parseInt(this.el.querySelector(`#padJoywing${i}Sda`).value) : -1;
                    jw[`joywing${i}_scl`] = enabled ? parseInt(this.el.querySelector(`#padJoywing${i}Scl`).value) : -1;
                    jw[`joywing${i}_addr`] = enabled ? parseInt(this.el.querySelector(`#padJoywing${i}Addr`).value) : 0x49;
                }
                return jw;
            })(),
        };

        try {
            this.log('Saving pad config...');
            const result = await this.protocol.setPadConfig(config);
            this.log(result.reboot ? 'Config saved. Device rebooting...' : 'Config saved.', 'success');
        } catch (e) {
            this.log(`Failed to save pad config: ${e.message}`, 'error');
        }
    }

    async reset() {
        if (!confirm('Reset to compile-time default? The device will reboot.')) return;
        try {
            this.log('Resetting pad config...');
            await this.protocol.resetPadConfig();
            this.log('Config reset. Device rebooting...', 'success');
        } catch (e) {
            this.log(`Failed to reset pad config: ${e.message}`, 'error');
        }
    }
}
