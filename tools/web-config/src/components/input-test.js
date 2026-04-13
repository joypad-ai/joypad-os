/** Input/Output Stream Test Card */

// Button display config (bit position → label)
const STREAM_BUTTONS = [
    { bit: 0, label: 'B1' }, { bit: 1, label: 'B2' },
    { bit: 2, label: 'B3' }, { bit: 3, label: 'B4' },
    { bit: 4, label: 'L1' }, { bit: 5, label: 'R1' },
    { bit: 6, label: 'L2' }, { bit: 7, label: 'R2' },
    { bit: 8, label: 'S1' }, { bit: 9, label: 'S2' },
    { bit: 10, label: 'L3' }, { bit: 11, label: 'R3' },
    { bit: 20, label: 'L4' }, { bit: 21, label: 'R4' },
    { bit: 12, label: 'DU' }, { bit: 13, label: 'DD' },
    { bit: 14, label: 'DL' }, { bit: 15, label: 'DR' },
    { bit: 16, label: 'A1' }, { bit: 17, label: 'A2' },
    { bit: 18, label: 'A3' },
];

const AXES = ['LX', 'LY', 'RX', 'RY', 'L2', 'R2'];

function renderButtonRow(prefix) {
    return STREAM_BUTTONS.map(b =>
        `<span class="btn" data-bit="${b.bit}">${b.label}</span>`
    ).join('');
}

function renderAxes(prefix) {
    return AXES.map((name, i) => {
        const center = i < 4 ? 128 : 0;
        const pct = i < 4 ? '50%' : '0%';
        return `<div class="axis">
            <div>${name}: <span id="${prefix}Axis${name}">${center}</span></div>
            <div class="axis-bar"><div class="axis-bar-fill" id="${prefix}Axis${name}Bar" style="width: ${pct}"></div></div>
        </div>`;
    }).join('');
}

export class InputTestCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
        this.streaming = false;
        this.debugStreaming = false;
    }

    render() {
        this.el.innerHTML = `
            <div class="card">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px;">
                    <h2 style="margin-bottom: 0;">Input Test</h2>
                    <button id="streamBtn" class="secondary" style="padding: 6px 12px; font-size: 12px;">Start Stream</button>
                </div>
                <div class="card-content">
                    <div class="stream-container">
                        <div class="stream-section">
                            <h4>Input (Raw) <span id="inputDeviceName" style="font-weight: normal; color: var(--text-muted);"></span>
                                <button id="rumbleBtn" class="secondary" style="padding: 4px 8px; font-size: 11px; margin-left: 10px;" title="Test rumble">Rumble</button>
                            </h4>
                            <div class="input-display" id="inputDisplay">
                                <div class="buttons" id="inputButtons">${renderButtonRow('in')}</div>
                                <div class="axes">${renderAxes('in')}</div>
                            </div>
                        </div>
                        <div class="stream-section">
                            <h4>Output (Mapped)</h4>
                            <div class="input-display" id="outputDisplay">
                                <div class="buttons" id="outputButtons">${renderButtonRow('out')}</div>
                                <div class="axes">${renderAxes('out')}</div>
                            </div>
                        </div>
                    </div>
                </div>
            </div>`;

        this.inputBtns = this.el.querySelectorAll('#inputButtons .btn');
        this.outputBtns = this.el.querySelectorAll('#outputButtons .btn');

        this.el.querySelector('#streamBtn').addEventListener('click', () => this.toggleStreaming());
        this.el.querySelector('#rumbleBtn').addEventListener('click', () => this.testRumble());
    }

    handleEvent(event) {
        if (event.type === 'input') {
            this.updateDisplay(this.inputBtns, 'in', event.buttons, event.axes);
        } else if (event.type === 'output') {
            this.updateDisplay(this.outputBtns, 'out', event.buttons, event.axes);
        } else if (event.type === 'connect') {
            this.log(`Controller connected: ${event.name} (${event.vid}:${event.pid})`);
            this.el.querySelector('#inputDeviceName').textContent = `- ${event.name}`;
        } else if (event.type === 'disconnect') {
            this.log(`Controller disconnected: port ${event.port}`);
            this.el.querySelector('#inputDeviceName').textContent = '';
        }
    }

    updateDisplay(btns, prefix, buttons, axes) {
        btns.forEach(btn => {
            const bit = parseInt(btn.dataset.bit);
            btn.classList.toggle('pressed', (buttons & (1 << bit)) !== 0);
        });

        if (axes && axes.length >= 6) {
            for (let i = 0; i < AXES.length; i++) {
                const name = AXES[i];
                const el = document.getElementById(`${prefix}Axis${name}`);
                const bar = document.getElementById(`${prefix}Axis${name}Bar`);
                if (el) el.textContent = axes[i];
                if (bar) bar.style.width = (i < 4 ? axes[i] / 255 * 100 : axes[i] / 255 * 100) + '%';
            }
        }
    }

    async toggleStreaming() {
        const btn = this.el.querySelector('#streamBtn');
        try {
            this.streaming = !this.streaming;
            await this.protocol.enableInputStream(this.streaming);
            btn.textContent = this.streaming ? 'Stop Stream' : 'Start Stream';
            btn.style.background = this.streaming ? 'var(--success)' : '';
            this.log(this.streaming ? 'Input streaming enabled' : 'Input streaming disabled');

            if (this.streaming) {
                await this.refreshPlayers();
            } else {
                this.el.querySelector('#inputDeviceName').textContent = '';
            }
        } catch (e) {
            this.log(`Failed to toggle streaming: ${e.message}`, 'error');
            this.streaming = false;
        }
    }

    async refreshPlayers() {
        try {
            const result = await this.protocol.getPlayers();
            if (result.count > 0 && result.players && result.players.length > 0) {
                const player = result.players[0];
                this.el.querySelector('#inputDeviceName').textContent = `- ${player.name}`;
                this.log(`Connected: ${player.name} (${player.transport})`);
            } else {
                this.el.querySelector('#inputDeviceName').textContent = '- No controller';
            }
        } catch (e) {
            console.log('Failed to get players:', e.message);
        }
    }

    async testRumble() {
        try {
            this.log('Testing rumble...');
            await this.protocol.testRumble(0, 200, 200, 500);
            this.log('Rumble test sent', 'success');
        } catch (e) {
            this.log(`Rumble test failed: ${e.message}`, 'error');
        }
    }

    async stop() {
        if (this.streaming) {
            try { await this.protocol.enableInputStream(false); } catch (e) { /* ignore */ }
            this.streaming = false;
        }
    }
}
