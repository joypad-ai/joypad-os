/** Advanced / Danger Zone Card */
export class AdvancedCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
    }

    render() {
        this.el.innerHTML = `
            <div class="card">
                <h2>Advanced</h2>
                <div class="card-content">
                    <div class="danger-zone">
                        <h3>Danger Zone</h3>
                        <div class="buttons">
                            <button class="secondary" id="clearBtBtn">Clear BT Bonds</button>
                            <button class="secondary" id="resetBtn">Factory Reset</button>
                            <button class="secondary" id="rebootBtn">Reboot</button>
                            <button class="secondary" id="bootselBtn">Bootloader</button>
                        </div>
                    </div>
                </div>
            </div>`;

        this.el.querySelector('#clearBtBtn').addEventListener('click', () => this.clearBtBonds());
        this.el.querySelector('#resetBtn').addEventListener('click', () => this.factoryReset());
        this.el.querySelector('#rebootBtn').addEventListener('click', () => this.reboot());
        this.el.querySelector('#bootselBtn').addEventListener('click', () => this.bootsel());
    }

    async clearBtBonds() {
        if (!confirm('Clear all Bluetooth bonds? Devices will need to re-pair.')) return;
        try {
            await this.protocol.clearBtBonds();
            this.log('Bluetooth bonds cleared', 'success');
        } catch (e) {
            this.log(`Failed to clear bonds: ${e.message}`, 'error');
        }
    }

    async factoryReset() {
        if (!confirm('Factory reset? This will clear all settings.')) return;
        try {
            await this.protocol.resetSettings();
            this.log('Factory reset complete, device will reboot...', 'success');
        } catch (e) {
            this.log(`Failed to reset: ${e.message}`, 'error');
        }
    }

    async reboot() {
        try {
            await this.protocol.reboot();
            this.log('Rebooting device...', 'success');
        } catch (e) {
            this.log(`Failed to reboot: ${e.message}`, 'error');
        }
    }

    async bootsel() {
        try {
            await this.protocol.bootsel();
            this.log('Entering bootloader mode...', 'success');
        } catch (e) {
            this.log(`Failed to enter bootloader: ${e.message}`, 'error');
        }
    }
}
