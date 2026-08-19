/** Buttons & Pins Configuration — Tabbed Sub-Sections */
import { DirtyTracker } from './dirty-tracker.js';

// Fallbacks only. The board reports its own pin set, ADC channels and button
// names through PAD.CONFIG.PINS; these are what the UI falls back to when that
// command isn't answered. The first 22 entries must stay in PAD_BTN_* order —
// PAD.CONFIG.SET reads "buttons" as a positional array.
const PAD_BUTTON_NAMES_FALLBACK = [
    'D-Up', 'D-Down', 'D-Left', 'D-Right',
    'B1', 'B2', 'B3', 'B4',
    'L1', 'R1', 'L2', 'R2',
    'S1', 'S2', 'L3', 'R3',
    'A1', 'A2', 'A3', 'A4', 'L4', 'R4',
    'F1', 'F2'
];

// F1/F2 live in their own flash fields rather than the buttons array, so the
// firmware's button_names list stops at 22 and the UI appends these two.
const PAD_FUNCTION_NAMES = ['F1', 'F2'];

// Must match PAD_BTN_COUNT in pad_config_flash.h. PAD.CONFIG.SET reads
// "buttons" positionally, so the two sides have to agree on this length.
const PAD_BTN_COUNT = 22;

// GPIO ids offered when the board doesn't report its own: the RP2040 range,
// which covers every target shipping pad input today.
const PAD_GPIO_FALLBACK = Array.from({ length: 30 }, (_, i) => i);
const PAD_ADC_FALLBACK = [0, 1, 2, 3];
const PAD_ADC_GPIO_FALLBACK = [26, 27, 28, 29];

// Firmware sends wire names (dpad_up, b1, l4); these are the display labels.
// Anything unmapped falls through to the raw name uppercased, so a button
// added to the firmware table still renders instead of vanishing.
const PAD_BUTTON_LABELS = {
    dpad_up: 'D-Up', dpad_down: 'D-Down', dpad_left: 'D-Left', dpad_right: 'D-Right',
};

function padButtonLabel(name) {
    return PAD_BUTTON_LABELS[name] || String(name).toUpperCase();
}

function adcOptions(channels, adcGpio) {
    let html = '<option value="-1">Disabled</option>';
    for (const ch of channels) {
        const gpio = adcGpio ? adcGpio[ch] : undefined;
        // adc_gpio is -1 where analog inputs aren't GPIO-numbered (nRF SAADC,
        // ESP32 ADC1) — show the bare channel rather than an invented pin.
        const hint = (typeof gpio === 'number' && gpio >= 0) ? ` (GPIO ${gpio})` : '';
        html += `<option value="${ch}">ADC ${ch}${hint}</option>`;
    }
    return html;
}

function adcRow(label, id, channels, adcGpio) {
    return `<div class="pad-pin-row">
        <span>${label}</span>
        <select id="${id}" class="pad-adc-select">${adcOptions(channels, adcGpio)}</select>
        <label class="pad-invert"><input type="checkbox" id="${id}Invert"> Invert</label>
    </div>`;
}

export class PadConfigCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
        // Replaced by the board's own values in load() via PAD.CONFIG.PINS.
        this.buttonNames = PAD_BUTTON_NAMES_FALLBACK;
        this.gpioList = PAD_GPIO_FALLBACK;
        this.adcChannels = PAD_ADC_FALLBACK;
        this.adcGpio = PAD_ADC_GPIO_FALLBACK;
    }

    // Ask the board what pins it has. Falls back to the RP2040 defaults if the
    // command isn't supported, so older firmware keeps working.
    async loadPinInfo() {
        try {
            const pins = await this.protocol.getPadPins();
            if (!pins || !pins.ok) return;

            if (Array.isArray(pins.gpio) && pins.gpio.length) {
                this.gpioList = pins.gpio;
            }
            if (Array.isArray(pins.button_names)) {
                // "buttons" is a POSITIONAL array — slot i is PAD_BTN_*(i). A
                // list of any other length means firmware and tool disagree
                // about that mapping, and adopting it would write every button
                // to a shifted slot with no error. Keep the known-good labels
                // and say so instead.
                if (pins.button_names.length === PAD_BTN_COUNT) {
                    // F1/F2 are separate flash fields, not part of the array,
                    // so they always occupy the last two UI slots.
                    this.buttonNames = pins.button_names.map(padButtonLabel).concat(PAD_FUNCTION_NAMES);
                } else {
                    this.log(`Firmware reports ${pins.button_names.length} buttons, expected ${PAD_BTN_COUNT} — using built-in labels. Update the config tool.`, 'error');
                }
            }
            if (Array.isArray(pins.adc) && pins.adc.length) {
                this.adcChannels = pins.adc;
                this.adcGpio = Array.isArray(pins.adc_gpio) ? pins.adc_gpio : null;
            }
        } catch (e) {
            this.log(`Pin capabilities unavailable, using defaults: ${e.message}`);
        }
    }

    // ADC selects are built from the board's channel list, so they have to be
    // regenerated once loadPinInfo() has run — before load() assigns values.
    rebuildAdcSelects() {
        const sticks = this.el.querySelector('#padAdcSticks');
        const triggers = this.el.querySelector('#padAdcTriggers');
        if (!sticks || !triggers) return;
        const ch = this.adcChannels, gp = this.adcGpio;
        sticks.innerHTML =
            adcRow('Left X', 'padAdcLX', ch, gp) +
            adcRow('Left Y', 'padAdcLY', ch, gp) +
            adcRow('Right X', 'padAdcRX', ch, gp) +
            adcRow('Right Y', 'padAdcRY', ch, gp);
        triggers.innerHTML =
            adcRow('Left Trigger', 'padAdcLT', ch, gp) +
            adcRow('Right Trigger', 'padAdcRT', ch, gp);
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
                    <h3 style="margin-bottom: 8px;">Sticks (ADC)</h3>
                    <div class="pad-pin-grid" id="padAdcSticks">
                        ${adcRow('Left X', 'padAdcLX', this.adcChannels, this.adcGpio)}
                        ${adcRow('Left Y', 'padAdcLY', this.adcChannels, this.adcGpio)}
                        ${adcRow('Right X', 'padAdcRX', this.adcChannels, this.adcGpio)}
                        ${adcRow('Right Y', 'padAdcRY', this.adcChannels, this.adcGpio)}
                    </div>
                    <div class="pad-form-row" style="margin-top: 12px;">
                        <span class="label">Deadzone</span>
                        <div style="display: flex; align-items: center; gap: 8px;">
                            <input type="range" id="padDeadzone" min="0" max="127" value="10" style="width: 120px;">
                            <span id="padDeadzoneValue">10</span>
                        </div>
                    </div>
                    <h3 style="margin-top: 16px; margin-bottom: 8px;">Triggers (ADC)</h3>
                    <div class="pad-pin-grid" id="padAdcTriggers">
                        ${adcRow('Left Trigger', 'padAdcLT', this.adcChannels, this.adcGpio)}
                        ${adcRow('Right Trigger', 'padAdcRT', this.adcChannels, this.adcGpio)}
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
                                <select id="padJoywing${i}Bus" title="RP2040: bus 1 for GPIO 2/3, 6/7, 10/11, 14/15, 18/19, 22/23, 26/27. ESP32: any bus.">
                                    <option value="0">I2C0</option>
                                    <option value="1" selected>I2C1</option>
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

                    <h3 style="margin-top: 12px; margin-bottom: 8px;">GPIO Expander (I2C)</h3>
                    <p class="hint">PCA9555 / TCA9555 at 0x20, 0x21</p>
                    <div class="toggle-row" style="margin-bottom: 8px;">
                        <label class="toggle">
                            <input type="checkbox" id="padI2cEnable">
                            <span class="toggle-slider"></span>
                        </label>
                        <span>Enable</span>
                    </div>
                    <div id="padI2cSettings" style="display:none;">
                        <div class="pad-form-row">
                            <span class="label">SDA Pin</span>
                            <input type="number" id="padI2cSda" min="0" max="47" value="0">
                        </div>
                        <div class="pad-form-row">
                            <span class="label">SCL Pin</span>
                            <input type="number" id="padI2cScl" min="0" max="47" value="0">
                        </div>
                    </div>

                </div>

                <div id="padPinConflicts" class="pad-conflicts" style="display:none;"></div>

                <div class="buttons" style="margin-top: 12px;">
                    <button id="padSaveBtn">Save &amp; Reboot</button>
                    <button id="padResetBtn" class="secondary">Reset to Default</button>
                </div>
                <p class="hint" style="margin-top: 8px;">Device will reboot to apply changes.</p>
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
        this.el.querySelector('#padI2cEnable').addEventListener('change', (e) => {
            this.el.querySelector('#padI2cSettings').style.display = e.target.checked ? '' : 'none';
            this.rebuildPinSelects();
        });
        this.el.querySelector('#padSaveBtn').addEventListener('click', () => this.save());
        this.dirty = new DirtyTracker(this.el, this.el.querySelector('#padSaveBtn'));
        this.el.querySelector('#padResetBtn').addEventListener('click', () => this.reset());
        this.el.querySelector('#padDeadzone').addEventListener('input', (e) => {
            this.el.querySelector('#padDeadzoneValue').textContent = e.target.value;
        });
    }

    hasI2C() {
        return this.el.querySelector('#padI2cEnable')?.checked || false;
    }

    buildPinSelect(id, value, includeI2C) {
        let html = `<select id="${id}" class="pad-pin-select">`;
        html += `<option value="-1"${value < 0 ? ' selected' : ''}>Disabled</option>`;
        // Only the pins the board reports. This used to be a flat 0-47, which
        // offered 18 nonexistent pins on every RP2040 target and the nRF's two
        // LFXO crystal pins — all of them accepted, saved and silently dead.
        for (const i of this.gpioList) {
            html += `<option value="${i}"${value === i ? ' selected' : ''}>GPIO ${i}</option>`;
        }
        // A pin saved by other means (or by older firmware) may not be in the
        // reported set — keep it selectable so opening the tool doesn't
        // silently rewrite it to Disabled on the next save.
        if (value >= 0 && !this.gpioList.includes(value) && value < 100) {
            html += `<option value="${value}" selected>GPIO ${value} (not on this board)</option>`;
        }
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
        for (let i = 0; i < this.buttonNames.length; i++) {
            const sel = this.el.querySelector('#padBtn' + i);
            values.push(sel ? parseInt(sel.value) : -1);
        }
        container.innerHTML = this.buttonNames.map((name, i) =>
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

            // Ask the board for its pin set before building any select, so the
            // menus below are populated from firmware rather than assumptions.
            await this.loadPinInfo();
            this.rebuildAdcSelects();

            this.el.querySelector('#padActiveHigh').value = String(config.active_high || false);

            // I2C Expander (set before button pins so buildPinSelect can check hasI2C)
            const i2cSda = config.i2c_sda !== undefined ? config.i2c_sda : -1;
            const i2cScl = config.i2c_scl !== undefined ? config.i2c_scl : -1;
            const i2cEnabled = i2cSda >= 0 && i2cScl >= 0;
            this.el.querySelector('#padI2cEnable').checked = i2cEnabled;
            this.el.querySelector('#padI2cSettings').style.display = i2cEnabled ? '' : 'none';
            if (i2cEnabled) {
                this.el.querySelector('#padI2cSda').value = i2cSda;
                this.el.querySelector('#padI2cScl').value = i2cScl;
            }

            // Button pins (22 from buttons array + F1/F2 from separate fields)
            const container = this.el.querySelector('#padButtonPins');
            const buttons = [...(config.buttons || [])];
            // Append F1/F2 from separate flash fields
            buttons[22] = config.f1_pin !== undefined ? config.f1_pin : -1;
            buttons[23] = config.f2_pin !== undefined ? config.f2_pin : -1;
            const includeI2C = this.hasI2C();
            container.innerHTML = this.buttonNames.map((name, i) =>
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
            this.dirty?.snapshot();
        } catch (e) {
            card.style.display = 'none';
            this.log(`Pad config not available: ${e.message}`);
        }
    }

    checkConflicts() {
        const pinCounts = {};
        const conflicts = [];

        for (let i = 0; i < this.buttonNames.length; i++) {
            const sel = this.el.querySelector('#padBtn' + i);
            if (!sel) continue;
            const pin = parseInt(sel.value);
            if (pin < 0) continue;
            if (!pinCounts[pin]) pinCounts[pin] = [];
            pinCounts[pin].push(this.buttonNames[i]);
            sel.classList.remove('conflict');
        }

        for (const [pin, names] of Object.entries(pinCounts)) {
            if (names.length > 1) {
                conflicts.push(`Pin ${pin} used by: ${names.join(', ')}`);
                for (let i = 0; i < this.buttonNames.length; i++) {
                    const sel = this.el.querySelector('#padBtn' + i);
                    if (sel && parseInt(sel.value) === parseInt(pin)) sel.classList.add('conflict');
                }
            }
        }

        const adcIds = ['padAdcLX', 'padAdcLY', 'padAdcRX', 'padAdcRY'];
        const adcLabels = ['Left X', 'Left Y', 'Right X', 'Right Y'];
        for (let a = 0; a < 4; a++) {
            const el = this.el.querySelector('#' + adcIds[a]);
            if (!el) continue;
            const ch = parseInt(el.value);
            if (ch < 0) continue;
            // Only meaningful where the analog input is a GPIO that could also
            // be claimed as a digital button. On nRF/ESP32 adc_gpio is -1 and
            // there is no overlap to warn about — the old `26 + ch` invented
            // one, flagging a phantom conflict on GPIO 26-29.
            const gpio = this.adcGpio ? this.adcGpio[ch] : undefined;
            if (typeof gpio !== 'number' || gpio < 0) continue;
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
        // F1/F2 stored as separate fields, not in buttons array
        const f1Sel = this.el.querySelector('#padBtn22');
        const f2Sel = this.el.querySelector('#padBtn23');

        const config = {
            name: 'Custom',
            active_high: this.el.querySelector('#padActiveHigh').value === 'true',
            invert_lx: this.el.querySelector('#padAdcLXInvert').checked,
            invert_ly: this.el.querySelector('#padAdcLYInvert').checked,
            invert_rx: this.el.querySelector('#padAdcRXInvert').checked,
            invert_ry: this.el.querySelector('#padAdcRYInvert').checked,
            i2c_sda: this.el.querySelector('#padI2cEnable').checked ? parseInt(this.el.querySelector('#padI2cSda').value) : -1,
            i2c_scl: this.el.querySelector('#padI2cEnable').checked ? parseInt(this.el.querySelector('#padI2cScl').value) : -1,
            deadzone: parseInt(this.el.querySelector('#padDeadzone').value),
            buttons,
            f1_pin: f1Sel ? parseInt(f1Sel.value) : -1,
            f2_pin: f2Sel ? parseInt(f2Sel.value) : -1,
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
            speaker_pin: this.currentConfig?.speaker_pin !== undefined ? this.currentConfig.speaker_pin : -1,
            speaker_enable_pin: this.currentConfig?.speaker_enable_pin !== undefined ? this.currentConfig.speaker_enable_pin : -1,
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
