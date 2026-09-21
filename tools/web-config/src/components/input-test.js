/** Input/Output Stream Test Card — Per-Player, Per-Source */

const STREAM_BUTTONS = [
    { bit: 0, label: 'B1' }, { bit: 1, label: 'B2' },
    { bit: 2, label: 'B3' }, { bit: 3, label: 'B4' },
    { bit: 4, label: 'L1' }, { bit: 5, label: 'R1' },
    { bit: 6, label: 'L2' }, { bit: 7, label: 'R2' },
    { bit: 8, label: 'S1' }, { bit: 9, label: 'S2' },
    { bit: 10, label: 'L3' }, { bit: 11, label: 'R3' },
    { bit: 20, label: 'L4' }, { bit: 21, label: 'R4' },
    { bit: 24, label: 'L5' }, { bit: 25, label: 'R5' },
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
            <div class="axis-bar" id="${prefix}Bar${i}" data-axis="${i}"><div class="axis-bar-fill" id="${prefix}Axis${name}Bar" style="width:${pct}"></div></div>
        </div>`;
    }).join('')}</div>`;
}

// Rest value per axis: sticks center at 128, triggers release to 0 (RZ centered).
function axisNeutral(idx) { return (idx === 4 || idx === 5) ? 0 : 128; }

// KeyboardEvent.code → HID usage (page 0x07) for KEY.INJECT.
const HID_KEYS = (() => {
    const m = {
        Enter: 40, Escape: 41, Backspace: 42, Tab: 43, Space: 44,
        Minus: 45, Equal: 46, BracketLeft: 47, BracketRight: 48, Backslash: 49,
        Semicolon: 51, Quote: 52, Backquote: 53, Comma: 54, Period: 55,
        Slash: 56, CapsLock: 57,
        PrintScreen: 70, ScrollLock: 71, Pause: 72, Insert: 73, Home: 74,
        PageUp: 75, Delete: 76, End: 77, PageDown: 78,
        ArrowRight: 79, ArrowLeft: 80, ArrowDown: 81, ArrowUp: 82,
    };
    for (let i = 0; i < 26; i++) m['Key' + String.fromCharCode(65 + i)] = 4 + i;
    for (let i = 1; i <= 9; i++) m['Digit' + i] = 29 + i;
    m.Digit0 = 39;
    for (let i = 1; i <= 12; i++) m['F' + i] = 57 + i;
    return m;
})();

// KeyboardEvent.code → HID modifier bit for KEY.INJECT "mod".
const HID_MODS = {
    ControlLeft: 0x01, ShiftLeft: 0x02, AltLeft: 0x04, MetaLeft: 0x08,
    ControlRight: 0x10, ShiftRight: 0x20, AltRight: 0x40, MetaRight: 0x80,
};

export class InputTestCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
        this.streaming = false;
        this.players = {};  // keyed by player index
        this.pendingUpdates = {};  // buffered display updates keyed by prefix
        this.rafScheduled = false;
        // Manual inject (INPUT.INJECT): held button bitmask driven by mouse clicks
        // on the inject pad. Sends are coalesced/serialized so rapid presses only
        // ever transmit the latest mask and frames never interleave.
        this.injectMask = 0;
        this.injectSending = false;
        this.injectDirty = false;
        // Injected analog axes [LX,LY,RX,RY,L2,R2,RZ]; only sent while
        // injectAnalogActive (a drag is engaged), else axes go back to the real
        // controller (INPUT.INJECT "analog":false).
        this.injectAnalog = [128, 128, 128, 128, 0, 0, 128];
        this.injectAnalogActive = false;
    }

    render() {
        this.el.innerHTML = `
            <div class="card">
                <div class="card-header">
                    <h2>Input Test</h2>
                    <div style="display: flex; gap: 8px; align-items: center;">
                        <button id="injectClearBtn" class="btn-sm secondary" title="Release all injected buttons">Release All</button>
                        <button id="rumbleBtn" class="btn-sm secondary" title="Test rumble">Rumble</button>
                        <button id="streamBtn" class="btn-sm secondary">Start Stream</button>
                    </div>
                </div>
                <div id="playerGroups" class="player-groups">
                    <p class="hint" id="streamHint">Start streaming to see connected controllers. Click the Output buttons to drive input over serial (INPUT.INJECT).</p>
                </div>
            </div>
            <div class="card" style="margin-top: 16px;">
                <div class="card-header">
                    <h2>Mouse &amp; Keyboard Test</h2>
                </div>
                <div class="mousekbd-grid">
                    <div>
                        <div class="source-label">Virtual Mouse</div>
                        <div id="mousePad" class="mouse-pad">
                            <span class="mouse-pad-hint">drag to move &middot; scroll for wheel</span>
                        </div>
                        <div class="mouse-btn-row">
                            <span class="btn" id="mBtnL">Left</span>
                            <span class="btn" id="mBtnM">Middle</span>
                            <span class="btn" id="mBtnR">Right</span>
                        </div>
                    </div>
                    <div>
                        <div class="source-label">Virtual Keyboard</div>
                        <div id="kbdCapture" class="kbd-capture" tabindex="0">
                            <span class="hint" id="kbdHint">click here, then type &mdash; keys pass through (Esc to stop)</span>
                            <div id="kbdHeld" class="kbd-held"></div>
                        </div>
                    </div>
                </div>
                <p class="hint" style="margin-top: 8px;">Drives MOUSE.INJECT / KEY.INJECT — the injected pointer and keys come out of the device's USB and BLE mouse/keyboard interfaces like real hardware.</p>
            </div>`;

        this.el.querySelector('#streamBtn').addEventListener('click', () => this.toggleStreaming());
        this.el.querySelector('#rumbleBtn').addEventListener('click', () => this.testRumble());
        this.el.querySelector('#injectClearBtn').addEventListener('click', () => this.injectClear());
        this.wireMouse();
        this.wireKeyboard();
    }

    // Virtual mouse: drag inside the pad to send relative deltas over serial
    // (MOUSE.INJECT), scroll for the wheel, and hold the L/M/R buttons to
    // click. Deltas accumulate and flush at ~30Hz so pointermove spam never
    // outruns the serial link.
    wireMouse() {
        const pad = this.el.querySelector('#mousePad');
        if (!pad) return;
        pad.style.touchAction = 'none';
        let accX = 0, accY = 0, accW = 0, lastX = 0, lastY = 0, timer = null, mbtn = 0;
        let mouseLogged = false;
        const flush = async () => {
            timer = null;
            const dx = Math.round(accX), dy = Math.round(accY), w = Math.round(accW);
            accX -= dx; accY -= dy; accW -= w;
            if (!dx && !dy && !w) return;
            try {
                await this.protocol.sendCommand('MOUSE.INJECT', { dx, dy, wheel: w, buttons: mbtn });
                if (!mouseLogged) { mouseLogged = true; this.log('Virtual mouse streaming (MOUSE.INJECT)', 'success'); }
            } catch (e) {
                if (!mouseLogged) { mouseLogged = true; this.log(`MOUSE.INJECT failed: ${e.message}`, 'error'); }
            }
        };
        const queue = () => { if (!timer) timer = setTimeout(flush, 33); };
        let dragging = false;
        pad.addEventListener('pointerdown', (e) => {
            e.preventDefault();
            // Capture keeps the drag alive outside the pad, but never gate on
            // it — setPointerCapture can throw (and does for synthetic
            // pointers), which silently killed the whole mouse path.
            try { pad.setPointerCapture(e.pointerId); } catch (err) { /* ok */ }
            dragging = true;
            lastX = e.clientX; lastY = e.clientY;
        });
        pad.addEventListener('pointermove', (e) => {
            if (!dragging) return;
            accX += (e.clientX - lastX) * 2;
            accY += (e.clientY - lastY) * 2;
            lastX = e.clientX; lastY = e.clientY;
            queue();
        });
        const endDrag = () => { dragging = false; };
        pad.addEventListener('pointerup', endDrag);
        pad.addEventListener('pointercancel', endDrag);
        pad.addEventListener('pointerleave', (e) => { if (!pad.hasPointerCapture?.(e.pointerId)) endDrag(); });
        pad.addEventListener('wheel', (e) => { e.preventDefault(); accW += -e.deltaY / 30; queue(); }, { passive: false });

        const sendButtons = async () => {
            try { await this.protocol.sendCommand('MOUSE.INJECT', { buttons: mbtn }); }
            catch (e) { this.log(`MOUSE.INJECT failed: ${e.message}`, 'error'); }
        };
        // Firmware button bits: 1=left, 2=right, 4=middle (HID order).
        [['mBtnL', 1], ['mBtnM', 4], ['mBtnR', 2]].forEach(([id, bit]) => {
            const b = this.el.querySelector('#' + id);
            b.style.cursor = 'pointer';
            b.style.userSelect = 'none';
            const down = (e) => { e.preventDefault(); mbtn |= bit; b.classList.add('pressed'); sendButtons(); };
            const up = () => {
                if (!(mbtn & bit)) return;
                mbtn &= ~bit; b.classList.remove('pressed'); sendButtons();
            };
            b.addEventListener('pointerdown', down);
            b.addEventListener('pointerup', up);
            b.addEventListener('pointerleave', up);
            b.addEventListener('pointercancel', up);
        });
    }

    // Virtual keyboard: focus the capture box and type — keydown/keyup map to
    // HID usages and stream as held state (KEY.INJECT). Esc or clicking away
    // releases everything, so nothing can stay stuck.
    wireKeyboard() {
        const box = this.el.querySelector('#kbdCapture');
        const heldEl = this.el.querySelector('#kbdHeld');
        if (!box) return;
        let mod = 0;
        const keys = new Map();  // event.code -> HID usage
        const sendState = async () => {
            const names = [...keys.keys()].map(c => c.replace(/^Key|^Digit/, ''));
            const mods = Object.entries(HID_MODS).filter(([, b]) => mod & b).map(([c]) => c.replace(/Left|Right/, m => m === 'Left' ? 'L' : 'R'));
            heldEl.textContent = [...mods, ...names].join(' + ');
            try { await this.protocol.sendCommand('KEY.INJECT', { mod, keys: [...keys.values()].slice(0, 6) }); }
            catch (e) { this.log(`KEY.INJECT failed: ${e.message}`, 'error'); }
        };
        box.addEventListener('keydown', (e) => {
            if (e.code === 'Escape') { box.blur(); return; }
            e.preventDefault();
            if (e.repeat) return;
            if (HID_MODS[e.code]) { mod |= HID_MODS[e.code]; sendState(); return; }
            const usage = HID_KEYS[e.code];
            if (usage && !keys.has(e.code) && keys.size < 6) { keys.set(e.code, usage); sendState(); }
        });
        box.addEventListener('keyup', (e) => {
            e.preventDefault();
            if (HID_MODS[e.code]) { mod &= ~HID_MODS[e.code]; sendState(); return; }
            if (keys.delete(e.code)) sendState();
        });
        box.addEventListener('focus', () => box.classList.add('capturing'));
        box.addEventListener('blur', () => {
            box.classList.remove('capturing');
            if (mod || keys.size) { mod = 0; keys.clear(); sendState(); }
        });
    }

    // Make an Output (Merged) button row clickable to inject: pointer-down presses a
    // bit, pointer-up/leave/cancel releases it. Pointer (not click) gives real
    // press-and-hold so combos work. The pressed visual is optimistic; when streaming
    // the round-tripped output confirms it via updateDisplay.
    wireOutputInject(prefix) {
        const row = this.el.querySelector(`#${prefix}Btns`);
        if (!row || row.dataset.injectWired) return;
        row.dataset.injectWired = '1';
        row.style.cursor = 'pointer';
        row.style.touchAction = 'none';
        row.style.userSelect = 'none';
        row.querySelectorAll('.btn').forEach(btn => {
            const bit = parseInt(btn.dataset.bit);
            const press = (e) => { e.preventDefault(); btn.classList.add('pressed'); this.injectSet(this.injectMask | (1 << bit)); };
            const release = () => {
                if (!(this.injectMask & (1 << bit))) return;  // not injecting this bit
                btn.classList.remove('pressed');  // optimistic (stream confirms when live)
                this.injectSet(this.injectMask & ~(1 << bit));
            };
            btn.addEventListener('pointerdown', press);
            btn.addEventListener('pointerup', release);
            btn.addEventListener('pointerleave', release);
            btn.addEventListener('pointercancel', release);
        });
        this.wireAxisDrag(prefix);
    }

    // Make the Output axis bars click-and-draggable: drag left/right to set the
    // axis (0..255), release to spring back to rest (stick=center, trigger=0).
    // Drives INPUT.INJECT "analog"; when every axis is at rest the axes are handed
    // back to the real controller.
    wireAxisDrag(prefix) {
        for (let i = 0; i < 6; i++) {
            const bar = this.el.querySelector(`#${prefix}Bar${i}`);
            if (!bar || bar.dataset.injectWired) continue;
            bar.dataset.injectWired = '1';
            bar.style.cursor = 'ew-resize';
            bar.style.touchAction = 'none';
            const valueAt = (e) => {
                const r = bar.getBoundingClientRect();
                let v = Math.round((e.clientX - r.left) / r.width * 255);
                return Math.max(0, Math.min(255, v));
            };
            const setAxis = (v) => {
                if (this.injectAnalog[i] === v && this.injectAnalogActive) return;
                this.injectAnalog[i] = v;
                this.injectAnalogActive = true;
                this.updateAxisVisual(prefix, i, v);
                this.injectDirty = true;
                if (!this.injectSending) this.flushInject();
            };
            bar.addEventListener('pointerdown', (e) => { e.preventDefault(); bar.setPointerCapture(e.pointerId); setAxis(valueAt(e)); });
            bar.addEventListener('pointermove', (e) => { if (bar.hasPointerCapture(e.pointerId)) setAxis(valueAt(e)); });
            const end = (e) => {
                if (!bar.hasPointerCapture?.(e.pointerId)) return;
                bar.releasePointerCapture(e.pointerId);
                this.injectAnalog[i] = axisNeutral(i);
                this.updateAxisVisual(prefix, i, this.injectAnalog[i]);
                // If every axis is back at rest, release analog to the real controller.
                const allRest = this.injectAnalog.every((v, idx) => v === axisNeutral(idx));
                if (allRest) this.injectAnalogActive = false;
                this.injectDirty = true;
                if (!this.injectSending) this.flushInject();
            };
            bar.addEventListener('pointerup', end);
            bar.addEventListener('pointercancel', end);
        }
    }

    updateAxisVisual(prefix, idx, v) {
        const name = AXES[idx];
        const el = document.getElementById(`${prefix}Axis${name}`);
        const bar = document.getElementById(`${prefix}Axis${name}Bar`);
        if (el) el.textContent = v;
        if (bar) bar.style.width = (v / 255 * 100) + '%';
    }

    injectClear() {
        // Clear optimistic pressed state on the clickable Output rows (stream, if
        // live, will immediately repaint real controller state).
        this.el.querySelectorAll('.buttons[data-inject-wired] .btn.pressed')
            .forEach(b => b.classList.remove('pressed'));
        this.injectAnalog = [128, 128, 128, 128, 0, 0, 128];
        this.injectAnalogActive = false;
        this.injectSet(0);
        this.injectDirty = true;
        if (!this.injectSending) this.flushInject();
    }

    injectSet(mask) {
        mask = mask >>> 0;  // keep unsigned (L5/R5 live at bits 24/25)
        if (mask === this.injectMask) return;
        this.injectMask = mask;
        this.injectDirty = true;
        if (!this.injectSending) this.flushInject();
    }

    async flushInject() {
        this.injectSending = true;
        try {
            while (this.injectDirty) {
                this.injectDirty = false;
                // analog: array holds the axes while dragging; false hands them back
                // to the real controller when no drag is engaged.
                const args = {
                    buttons: this.injectMask,
                    analog: this.injectAnalogActive ? this.injectAnalog.slice() : false,
                };
                try {
                    await this.protocol.sendCommand('INPUT.INJECT', args);
                } catch (e) {
                    this.log(`Inject failed: ${e.message}`, 'error');
                    break;
                }
            }
        } finally {
            this.injectSending = false;
        }
    }

    handleEvent(event) {
        if (event.type === 'input') {
            const player = event.player !== undefined ? event.player : 0;
            const addr = event.addr !== undefined ? event.addr : 0;
            this.ensurePlayerGroup(player);
            this.ensureInputSource(player, addr, event.name || '', event.src || '');
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

        // The Output (Merged) row doubles as the manual-inject pad — click its
        // buttons to drive input over serial (INPUT.INJECT).
        this.wireOutputInject(`p${player}out`);
    }

    ensureInputSource(player, addr, name, source) {
        // The CDC-injected virtual mouse (0xD9) and keyboard (0xDA) are not
        // gamepads — never list them as input sources (newer firmware already
        // excludes them from the stream; this guards against older builds).
        if (addr === 0xD9 || addr === 0xDA) return;
        const key = `${player}:${addr}`;
        if (this.players[player].sources[addr]) {
            // Update name if we have one and it changed
            if (name) {
                const label = this.el.querySelector(`#srcLabel${player}a${addr}`);
                if (label) {
                    const text = source ? `${name} (${source})` : name;
                    if (label.textContent !== text) label.textContent = text;
                }
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
                // Always show Player 1 so its Output row is clickable to inject even
                // with no controller connected, then fill in any real players.
                this.ensurePlayerGroup(0);
                await this.refreshPlayers();
            } else {
                // Release any held inject and clear all player groups.
                this.injectClear();
                this.players = {};
                const container = this.el.querySelector('#playerGroups');
                container.innerHTML = '<p class="hint" id="streamHint">Start streaming to see connected controllers. Click the Output buttons to drive input over serial (INPUT.INJECT).</p>';
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
                    // Don't pre-create input source rows — streaming events
                    // will create them with the correct dev_addr and name
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
        // Release any held inject first so we never leave the board with a stuck
        // synthetic mask (the router holds INPUT.INJECT state until cleared/reboot).
        if (this.injectMask || this.injectAnalogActive) {
            this.injectMask = 0;
            this.injectAnalogActive = false;
            this.injectAnalog = [128, 128, 128, 128, 0, 0, 128];
            try { await this.protocol.sendCommand('INPUT.INJECT', { buttons: 0, analog: false }); } catch (e) { /* ignore */ }
        }
        if (this.streaming) {
            try { await this.protocol.enableInputStream(false); } catch (e) { /* ignore */ }
            this.streaming = false;
        }
    }
}
