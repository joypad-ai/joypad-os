/** Hotkeys Page — Button combo remapping and actions */
import { BUTTON_NAMES, BUTTON_LABELS } from './profiles.js';

const COMBO_ACTION_SHIFT = 24;
const COMBO_BUTTON_MASK = 0x003FFFFF;

const ACTIONS = [
    { id: -1, name: 'Disabled' },
    { id: 0, name: 'Button Remap' },
    { id: 1, name: 'D-Pad → D-Pad' },
    { id: 2, name: 'D-Pad → Left Stick' },
    { id: 3, name: 'D-Pad → Right Stick' },
    { id: 4, name: 'Next Profile' },
];

export class HotkeysCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
        this.visible = false;
        this.currentConfig = null;
    }

    render() {
        const comboRows = [0, 1, 2, 3].map(i => `
            <div class="combo-row" style="margin-bottom: 16px; padding-bottom: 16px; border-bottom: 1px solid var(--border);">
                <div class="pad-form-row" style="margin-bottom: 8px;">
                    <span class="label">Combo ${i + 1}</span>
                    <select id="comboAction${i}" style="width: auto; min-width: 140px;">
                        ${ACTIONS.map(a => `<option value="${a.id}">${a.name}</option>`).join('')}
                    </select>
                </div>
                <div id="comboFields${i}" style="display:none;">
                    <div style="margin-bottom: 4px;">
                        <div class="hint" style="margin-bottom: 4px;">Input (hold together)</div>
                        <div class="combo-buttons" id="comboIn${i}">
                            ${this.buildCheckboxes(`ci${i}`)}
                        </div>
                    </div>
                    <div id="comboOutWrap${i}">
                        <div class="hint" style="margin-bottom: 4px; margin-top: 8px;">Output</div>
                        <div class="combo-buttons" id="comboOut${i}">
                            ${this.buildCheckboxes(`co${i}`)}
                        </div>
                    </div>
                </div>
            </div>
        `).join('');

        this.el.innerHTML = `
            <div class="card" id="hotkeysCard" style="display:none;">
                <h2>Hotkeys</h2>
                <div class="card-content">
                    <p class="hint">Map button combos to actions or other buttons.</p>
                    ${comboRows}
                    <div class="buttons" style="margin-top: 12px;">
                        <button id="hotkeysSaveBtn">Save &amp; Reboot</button>
                    </div>
                    <p class="hint" style="margin-top: 8px;">Device will reboot to apply changes.</p>
                </div>
            </div>`;

        // Wire up action select to show/hide fields
        for (let i = 0; i < 4; i++) {
            this.el.querySelector(`#comboAction${i}`).addEventListener('change', (e) => {
                const val = parseInt(e.target.value);
                this.el.querySelector(`#comboFields${i}`).style.display = val >= 0 ? '' : 'none';
                this.el.querySelector(`#comboOutWrap${i}`).style.display = val === 0 ? '' : 'none';
            });
        }
        this.el.querySelector('#hotkeysSaveBtn').addEventListener('click', () => this.save());
    }

    buildCheckboxes(prefix) {
        return BUTTON_NAMES.map((name, idx) => {
            const label = BUTTON_LABELS[name] || name;
            return `<label class="combo-btn" title="${label}">
                <input type="checkbox" data-bit="${idx}" id="${prefix}_${idx}">
                <span>${name}</span>
            </label>`;
        }).join('');
    }

    maskToChecks(prefix, mask) {
        for (let i = 0; i < 22; i++) {
            const cb = this.el.querySelector(`#${prefix}_${i}`);
            if (cb) cb.checked = (mask & (1 << i)) !== 0;
        }
    }

    checksToMask(prefix) {
        let mask = 0;
        for (let i = 0; i < 22; i++) {
            const cb = this.el.querySelector(`#${prefix}_${i}`);
            if (cb && cb.checked) mask |= (1 << i);
        }
        return mask;
    }

    async load() {
        const card = this.el.querySelector('#hotkeysCard');
        try {
            const config = await this.protocol.getPadConfig();
            if (!config.ok) {
                card.style.display = 'none';
                this.visible = false;
                return;
            }
            card.style.display = '';
            this.visible = true;
            this.currentConfig = config;

            const combos = config.combos || [];
            for (let i = 0; i < 4; i++) {
                const combo = combos[i] || [0, 0];
                const inputMask = combo[0];
                const outputRaw = combo[1];
                const action = inputMask === 0 ? -1 : ((outputRaw >>> COMBO_ACTION_SHIFT) & 0xFF);
                const outputButtons = outputRaw & COMBO_BUTTON_MASK;

                this.el.querySelector(`#comboAction${i}`).value = action;
                this.maskToChecks(`ci${i}`, inputMask);
                this.maskToChecks(`co${i}`, outputButtons);
                this.el.querySelector(`#comboFields${i}`).style.display = action >= 0 ? '' : 'none';
                this.el.querySelector(`#comboOutWrap${i}`).style.display = action === 0 ? '' : 'none';
            }
        } catch (e) {
            card.style.display = 'none';
            this.visible = false;
        }
    }

    async save() {
        if (!confirm('Save hotkey configuration? The device will reboot.')) return;
        if (!this.currentConfig) return;

        const config = {
            name: this.currentConfig.name || 'Custom',
            active_high: this.currentConfig.active_high || false,
            i2c_sda: this.currentConfig.i2c_sda !== undefined ? this.currentConfig.i2c_sda : -1,
            i2c_scl: this.currentConfig.i2c_scl !== undefined ? this.currentConfig.i2c_scl : -1,
            deadzone: this.currentConfig.deadzone || 10,
            dpad_mode: this.currentConfig.dpad_mode || 0,
            buttons: this.currentConfig.buttons || [],
            adc: this.currentConfig.adc || [-1, -1, -1, -1, -1, -1],
            invert_lx: this.currentConfig.invert_lx || false,
            invert_ly: this.currentConfig.invert_ly || false,
            invert_rx: this.currentConfig.invert_rx || false,
            invert_ry: this.currentConfig.invert_ry || false,
            sinput_rgb: this.currentConfig.sinput_rgb || false,
            led_pin: this.currentConfig.led_pin !== undefined ? this.currentConfig.led_pin : -1,
            led_count: this.currentConfig.led_count || 0,
            speaker_pin: this.currentConfig.speaker_pin !== undefined ? this.currentConfig.speaker_pin : -1,
            speaker_enable_pin: this.currentConfig.speaker_enable_pin !== undefined ? this.currentConfig.speaker_enable_pin : -1,
            usb_host_dp: this.currentConfig.usb_host_dp !== undefined ? this.currentConfig.usb_host_dp : -1,
            ...(() => {
                const tg = {};
                const toggles = this.currentConfig.toggles || [];
                for (let i = 0; i < 2; i++) {
                    const t = toggles[i] || [-1, 0, 0];
                    tg[`toggle${i}_pin`] = t[0];
                    tg[`toggle${i}_func`] = t[1];
                    tg[`toggle${i}_inv`] = t[2];
                }
                return tg;
            })(),
            ...(() => {
                const jw = {};
                const joywing = this.currentConfig.joywing || [];
                for (let i = 0; i < 2; i++) {
                    const slot = joywing[i] || [0, -1, -1, 0x49];
                    jw[`joywing${i}_bus`] = slot[0];
                    jw[`joywing${i}_sda`] = slot[1];
                    jw[`joywing${i}_scl`] = slot[2];
                    jw[`joywing${i}_addr`] = slot[3];
                }
                return jw;
            })(),
        };

        // Add combo hotkeys
        for (let i = 0; i < 4; i++) {
            const action = parseInt(this.el.querySelector(`#comboAction${i}`).value);
            if (action < 0) {
                config[`combo${i}_in`] = 0;
                config[`combo${i}_out`] = 0;
            } else {
                const outputButtons = this.checksToMask(`co${i}`);
                config[`combo${i}_in`] = this.checksToMask(`ci${i}`);
                config[`combo${i}_out`] = (action << COMBO_ACTION_SHIFT) | (outputButtons & COMBO_BUTTON_MASK);
            }
        }

        try {
            this.log('Saving hotkey config...');
            const result = await this.protocol.setPadConfig(config);
            this.log(result.reboot ? 'Config saved. Device rebooting...' : 'Config saved.', 'success');
        } catch (e) {
            this.log(`Failed to save config: ${e.message}`, 'error');
        }
    }

    isAvailable() { return this.visible; }
}
