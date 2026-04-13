/** Wiimote Orientation Card */
export class WiimoteCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
    }

    render() {
        this.el.innerHTML = `
            <div class="card">
                <h2>Wiimote</h2>
                <div class="card-content">
                    <div class="row">
                        <span class="label">Orientation</span>
                        <select id="wiimoteOrientSelect">
                            <option value="0">Auto</option>
                            <option value="1">Horizontal</option>
                            <option value="2">Vertical</option>
                        </select>
                    </div>
                    <p class="hint">Controls D-pad mapping when using Wiimote alone (no extension).</p>
                </div>
            </div>`;

        this.el.querySelector('#wiimoteOrientSelect').addEventListener('change',
            (e) => this.setOrient(e.target.value));
    }

    async load() {
        try {
            const result = await this.protocol.getWiimoteOrient();
            this.el.querySelector('#wiimoteOrientSelect').value = result.mode;
            this.log(`Wiimote orientation: ${result.name}`);
        } catch (e) {
            this.log(`Failed to load Wiimote orientation: ${e.message}`, 'error');
        }
    }

    async setOrient(mode) {
        try {
            this.log(`Setting Wiimote orientation to ${mode}...`);
            const result = await this.protocol.setWiimoteOrient(parseInt(mode));
            this.log(`Wiimote orientation set to ${result.name}`, 'success');
        } catch (e) {
            this.log(`Failed to set Wiimote orientation: ${e.message}`, 'error');
        }
    }
}
