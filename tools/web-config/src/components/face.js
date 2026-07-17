/** Face Page — drive the companion face (AMOLED eyes) over FACE.* commands.
 *
 * Works against any device that answers FACE.* — the face board itself over
 * USB CDC or BLE NUS, or a dongle that relays FACE.* to a paired face. The
 * page probes with a harmless FACE.LOOK on load and hides itself when the
 * firmware reports "unknown command".
 */
export class FaceCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
        this.available = false;
        this._lookTimer = null;
        this._babbleTimer = null;
    }

    static STYLES = ['classic', 'taby', 'astro'];
    static EMOTIONS = [
        ['neutral', '😐'], ['happy', '😊'], ['excited', '🤩'],
        ['love', '😍'], ['wink', '😉'], ['angry', '😠'],
        ['frustrated', '😖'], ['suspicious', '🤨'], ['surprised', '😮'],
        ['sad', '😢'], ['sleepy', '😴'],
    ];

    render() {
        this.el.innerHTML = `
            <div class="card" id="faceCard" style="display:none;">
                <h2>Face</h2>
                <div class="card-content">
                    <div>
                        <h3>Style</h3>
                        <div class="buttons" id="faceStyles" style="margin-top: 8px;">
                            ${FaceCard.STYLES.map(s =>
                                `<button class="secondary" data-style="${s}">${s}</button>`).join('')}
                        </div>
                    </div>

                    <div>
                        <h3>Emotion</h3>
                        <div class="buttons" id="faceEmotions" style="margin-top: 8px; flex-wrap: wrap;">
                            ${FaceCard.EMOTIONS.map(([e, icon]) =>
                                `<button class="secondary" data-emo="${e}">${icon} ${e}</button>`).join('')}
                        </div>
                    </div>

                    <div>
                        <h3>Gaze</h3>
                        <p class="hint" style="margin-top: 8px;">Drag to steer where the eyes look. Release to let them wander again.</p>
                        <div id="faceLookPad" style="margin-top: 8px; width: 220px; height: 120px;
                             border: 1px solid var(--border, #444); border-radius: 8px;
                             position: relative; touch-action: none; cursor: crosshair;">
                            <div id="faceLookDot" style="position: absolute; width: 14px; height: 14px;
                                 border-radius: 50%; background: var(--accent, #4da3ff);
                                 left: 103px; top: 53px; pointer-events: none;"></div>
                        </div>
                    </div>

                    <div>
                        <h3>Speech</h3>
                        <div class="pad-form-row" style="margin-top: 8px;">
                            <span class="label">Mouth</span>
                            <input type="range" id="faceSpeak" min="0" max="100" value="0">
                        </div>
                        <div class="buttons">
                            <button class="secondary" id="faceBabbleBtn">Babble test (3s)</button>
                        </div>
                    </div>

                    <div>
                        <h3>Panel</h3>
                        <div class="pad-form-row" style="margin-top: 8px;">
                            <span class="label">Brightness</span>
                            <input type="range" id="faceBright" min="10" max="255" value="200">
                        </div>
                    </div>
                </div>
            </div>`;

        this.el.querySelectorAll('#faceStyles button').forEach(b =>
            b.addEventListener('click', () => this.setStyle(b.dataset.style)));
        this.el.querySelectorAll('#faceEmotions button').forEach(b =>
            b.addEventListener('click', () => this.setEmotion(b.dataset.emo)));

        const pad = this.el.querySelector('#faceLookPad');
        const dot = this.el.querySelector('#faceLookDot');
        const steer = (ev) => {
            const r = pad.getBoundingClientRect();
            const cx = (ev.touches ? ev.touches[0].clientX : ev.clientX) - r.left;
            const cy = (ev.touches ? ev.touches[0].clientY : ev.clientY) - r.top;
            let x = Math.max(-1, Math.min(1, (cx / r.width) * 2 - 1));
            let y = Math.max(-1, Math.min(1, (cy / r.height) * 2 - 1));
            dot.style.left = `${(x + 1) / 2 * r.width - 7}px`;
            dot.style.top = `${(y + 1) / 2 * r.height - 7}px`;
            this.look(x, y);
        };
        let dragging = false;
        pad.addEventListener('pointerdown', (e) => { dragging = true; pad.setPointerCapture(e.pointerId); steer(e); });
        pad.addEventListener('pointermove', (e) => { if (dragging) steer(e); });
        pad.addEventListener('pointerup', () => { dragging = false; });

        this.el.querySelector('#faceSpeak').addEventListener('input', (e) =>
            this.speak(parseInt(e.target.value, 10)));
        this.el.querySelector('#faceBabbleBtn').addEventListener('click', () => this.babble());
        this.el.querySelector('#faceBright').addEventListener('change', (e) =>
            this.brightness(parseInt(e.target.value, 10)));
    }

    /** Probe FACE.* support; show/hide the card. Any response other than
     *  "unknown command" (including a relay's "no face connected") counts. */
    async load() {
        this.available = false;
        try {
            await this.protocol.sendCommand('FACE.LOOK', { x: 0, y: 0 });
            this.available = true;
        } catch (e) {
            this.available = !/unknown command/i.test(e.message || '');
        }
        const card = this.el.querySelector('#faceCard');
        if (card) card.style.display = this.available ? '' : 'none';
    }

    isAvailable() { return this.available; }

    async _cmd(cmd, args) {
        try {
            await this.protocol.sendCommand(cmd, args);
        } catch (e) {
            this.log(`${cmd}: ${e.message}`, 'error');
        }
    }

    setStyle(style)  { this._cmd('FACE.STYLE', { style }); }
    setEmotion(emo)  { this._cmd('FACE.EMO', { emo }); }
    speak(v)         { this._cmd('FACE.SPEAK', { v }); }
    brightness(v)    { this._cmd('FACE.BRIGHT', { v }); }

    look(x, y) {
        // throttle drags to ~20Hz — plenty for the pose spring
        if (this._lookTimer) return;
        this._lookTimer = setTimeout(() => { this._lookTimer = null; }, 50);
        // pad is viewer-space; the panel's canvas X runs the other way
        this._cmd('FACE.LOOK', { x: Math.round(-x * 100), y: Math.round(y * 100) });
    }

    /** Send a talky-looking envelope for ~3s to preview lip-sync. */
    babble() {
        if (this._babbleTimer) return;
        const t0 = Date.now();
        this._babbleTimer = setInterval(() => {
            const t = (Date.now() - t0) / 1000;
            if (t > 3.0) {
                clearInterval(this._babbleTimer);
                this._babbleTimer = null;
                this.speak(0);
                return;
            }
            const v = Math.max(0, Math.round(
                55 + 40 * Math.sin(t * 9.0) * Math.sin(t * 2.3 + 1)));
            this.speak(v);
        }, 66);
    }
}
