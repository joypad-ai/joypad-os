/** LEDs Output Page — NeoPixel strip configuration */
export class LedsCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
        this.visible = false;
        this.currentConfig = null;
    }

    render() {
        this.el.innerHTML = `
            <div class="card" id="ledsCard" style="display:none;">
                <h2>LEDs</h2>
                <div class="card-content">
                    <div class="pad-form-row">
                        <span class="label">LED Pin</span>
                        <input type="number" id="ledPin" min="-1" max="47" value="-1">
                    </div>
                    <div class="pad-form-row">
                        <span class="label">LED Count</span>
                        <input type="number" id="ledCount" min="0" max="16" value="0">
                    </div>
                    <p class="hint">WS2812/NeoPixel data pin and number of LEDs. Set pin to -1 to disable.</p>
                    <div class="buttons" style="margin-top: 12px;">
                        <button id="ledsSaveBtn">Save &amp; Reboot</button>
                    </div>
                </div>
            </div>`;

        this.el.querySelector('#ledsSaveBtn').addEventListener('click', () => this.save());
    }

    async load() {
        const card = this.el.querySelector('#ledsCard');
        try {
            const config = await this.protocol.getPadConfig();
            if (!config.ok) {
                card.style.display = 'none';
                this.visible = false;
                return;
            }
            card.style.display = '';
            this.visible = true;
            this.el.querySelector('#ledPin').value = config.led_pin !== undefined ? config.led_pin : -1;
            this.el.querySelector('#ledCount').value = config.led_count || 0;
            this.currentConfig = config;
        } catch (e) {
            card.style.display = 'none';
            this.visible = false;
        }
    }

    async save() {
        if (!confirm('Save LED configuration? The device will reboot.')) return;
        if (!this.currentConfig) return;

        const config = {
            name: this.currentConfig.name || 'Custom',
            active_high: this.currentConfig.active_high || false,
            i2c_sda: this.currentConfig.i2c_sda !== undefined ? this.currentConfig.i2c_sda : -1,
            i2c_scl: this.currentConfig.i2c_scl !== undefined ? this.currentConfig.i2c_scl : -1,
            deadzone: this.currentConfig.deadzone || 10,
            buttons: this.currentConfig.buttons || [],
            adc: this.currentConfig.adc || [-1, -1, -1, -1, -1, -1],
            invert_lx: this.currentConfig.invert_lx || false,
            invert_ly: this.currentConfig.invert_ly || false,
            invert_rx: this.currentConfig.invert_rx || false,
            invert_ry: this.currentConfig.invert_ry || false,
            led_pin: parseInt(this.el.querySelector('#ledPin').value),
            led_count: parseInt(this.el.querySelector('#ledCount').value),
            speaker_pin: this.currentConfig.speaker_pin !== undefined ? this.currentConfig.speaker_pin : -1,
            speaker_enable_pin: this.currentConfig.speaker_enable_pin !== undefined ? this.currentConfig.speaker_enable_pin : -1,
            usb_host_dp: this.currentConfig.usb_host_dp !== undefined ? this.currentConfig.usb_host_dp : -1,
            // Preserve toggles
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
            // Preserve joywing
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

        try {
            this.log('Saving LED config...');
            const result = await this.protocol.setPadConfig(config);
            this.log(result.reboot ? 'Config saved. Device rebooting...' : 'Config saved.', 'success');
        } catch (e) {
            this.log(`Failed to save LED config: ${e.message}`, 'error');
        }
    }

    isAvailable() { return this.visible; }
}
