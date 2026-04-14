/** Input/Output Stream Test Card — Per-Player, Per-Source */

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
    { bit: 18, label: 'A3' }, { bit: 19, label: 'A4' },
];

const AXES = ['LX', 'LY', 'RX', 'RY', 'L2', 'R2'];

function renderButtons(id) {
    return `<div class="buttons" id="${id}">${
        STREAM_BUTTONS.map(b => `<span class="btn" data-bit="${b.bit}">${b.label}</span>`).join('')
    }</div>`;
}

function renderAxes(prefix) {
    return `<div class="axes">${AXES.map((name, i) => {
        const val = i < 4 ? 128 : 0;
        const pct = i < 4 ? '50%' : '0%';
        return `<div class="axis">
            <div>${name}: <span id="${prefix}Axis${name}">${val}</span></div>
            <div class="axis-bar"><div class="axis-bar-fill" id="${prefix}Axis${name}Bar" style="width:${pct}"></div></div>
        </div>`;
    }).join('')}</div>`;
}

export class InputTestCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
        this.streaming = false;
        this.players = {};  // keyed by player index
        this.pendingUpdates = {};  // buffered display updates keyed by prefix
        this.rafScheduled = false;
    }

    render() {
        this.el.innerHTML = `
            <div class="card">
                <div class="card-header">
                    <h2>Input Test</h2>
                    <div style="display: flex; gap: 8px; align-items: center;">
                        <button id="rumbleBtn" class="btn-sm secondary" title="Test rumble">Rumble</button>
                        <button id="streamBtn" class="btn-sm secondary">Start Stream</button>
                    </div>
                </div>
                <div id="playerGroups" class="player-groups">
                    <p class="hint" id="streamHint">Start streaming to see connected controllers.</p>
                </div>
            </div>`;

        this.el.querySelector('#streamBtn').addEventListener('click', () => this.toggleStreaming());
        this.el.querySelector('#rumbleBtn').addEventListener('click', () => this.testRumble());
    }

    handleEvent(event) {
        if (event.type === 'input') {
            const player = event.player !== undefined ? event.player : 0;
            const addr = event.addr !== undefined ? event.addr : 0;
            this.ensurePlayerGroup(player);
            this.ensureInputSource(player, addr, event.name || 'Unknown', event.src || '');
            this.scheduleUpdate(`p${player}a${addr}`, event.buttons, event.axes);
        } else if (event.type === 'output') {
            const player = event.player !== undefined ? event.player : 0;
            this.ensurePlayerGroup(player);
            this.scheduleUpdate(`p${player}out`, event.buttons, event.axes);
        } else if (event.type === 'connect') {
            this.log(`Controller connected: ${event.name} (${event.vid}:${event.pid})`);
        } else if (event.type === 'disconnect') {
            this.log(`Controller disconnected: port ${event.port}`);
            this.removePlayerGroup(event.port);
        }
    }

    scheduleUpdate(prefix, buttons, axes) {
        this.pendingUpdates[prefix] = { buttons, axes };
        if (!this.rafScheduled) {
            this.rafScheduled = true;
            requestAnimationFrame(() => {
                for (const [pfx, data] of Object.entries(this.pendingUpdates)) {
                    this.updateDisplay(pfx, data.buttons, data.axes);
                }
                this.pendingUpdates = {};
                this.rafScheduled = false;
            });
        }
    }

    ensurePlayerGroup(player) {
        if (this.players[player]) return;

        // Hide hint
        const hint = this.el.querySelector('#streamHint');
        if (hint) hint.style.display = 'none';

        const container = this.el.querySelector('#playerGroups');
        const group = document.createElement('div');
        group.className = 'player-group';
        group.id = `playerGroup${player}`;
        group.innerHTML = `
            <div class="player-header">Player ${player + 1}</div>
            <div class="player-inputs" id="playerInputs${player}"></div>
            <div class="player-output">
                <div class="source-label">Output (Merged)</div>
                <div class="input-display compact">
                    ${renderButtons(`p${player}outBtns`)}
                    ${renderAxes(`p${player}out`)}
                </div>
            </div>`;
        container.appendChild(group);

        this.players[player] = { sources: {} };
    }

    ensureInputSource(player, addr, name, source) {
        const key = `${player}:${addr}`;
        if (this.players[player].sources[addr]) {
            // Update name if changed
            const label = this.el.querySelector(`#srcLabel${player}a${addr}`);
            if (label) {
                const text = source ? `${name} (${source})` : name;
                if (label.textContent !== text) label.textContent = text;
            }
            return;
        }

        const inputsContainer = this.el.querySelector(`#playerInputs${player}`);
        const row = document.createElement('div');
        row.className = 'input-source';
        row.id = `inputSrc${player}a${addr}`;
        row.innerHTML = `
            <div class="source-label" id="srcLabel${player}a${addr}">${source ? `${name} (${source})` : name}</div>
            <div class="input-display compact">
                ${renderButtons(`p${player}a${addr}Btns`)}
                ${renderAxes(`p${player}a${addr}`)}
            </div>`;
        inputsContainer.appendChild(row);

        this.players[player].sources[addr] = true;
    }

    removePlayerGroup(player) {
        const group = this.el.querySelector(`#playerGroup${player}`);
        if (group) group.remove();
        delete this.players[player];

        // Show hint if no players left
        if (Object.keys(this.players).length === 0) {
            const hint = this.el.querySelector('#streamHint');
            if (hint) hint.style.display = '';
        }
    }

    updateDisplay(prefix, buttons, axes) {
        const btns = this.el.querySelectorAll(`#${prefix}Btns .btn`);
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
                if (bar) bar.style.width = (axes[i] / 255 * 100) + '%';
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
                // Pre-populate player groups from connected players
                await this.refreshPlayers();
            } else {
                // Clear all player groups
                this.players = {};
                const container = this.el.querySelector('#playerGroups');
                container.innerHTML = '<p class="hint" id="streamHint">Start streaming to see connected controllers.</p>';
            }
        } catch (e) {
            this.log(`Failed to toggle streaming: ${e.message}`, 'error');
            this.streaming = false;
        }
    }

    async refreshPlayers() {
        try {
            const result = await this.protocol.getPlayers();
            if (result.players && result.players.length > 0) {
                for (const p of result.players) {
                    const player = p.slot !== undefined ? p.slot : 0;
                    this.ensurePlayerGroup(player);
                    const src = p.transport || '';
                    this.ensureInputSource(player, p.slot + 1, p.name || 'Unknown', src);
                }
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
