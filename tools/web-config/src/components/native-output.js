/**
 * Native Output Page — generic renderer for any console-output type
 * (joybus / maple / pce / polyface / 3do / loopy / uart / ...).
 *
 * Reads OUTPUT.NATIVE.GET, builds form dynamically from the response schema
 * (modes + pins). Each device firmware returns its own type/modes/pins, so
 * adding a new console output requires no change to this component.
 *
 * Response shape:
 *   {
 *     ok: true, available: true,
 *     type: "joybus",
 *     modes: ["gamecube"],
 *     current_mode: "gamecube",
 *     pins: { data: { label: "Data", value: 7, min: 0, max: 28, default: 7 } }
 *   }
 */
import { DirtyTracker } from './dirty-tracker.js';

// Display name + nav label per type (extend as new outputs land)
const TYPE_INFO = {
    joybus:   { title: 'Joybus Output',     nav: 'Joybus'   },
    maple:    { title: 'Maple Bus Output',  nav: 'Maple'    },
    pce:      { title: 'PCEngine Output',   nav: 'PCEngine' },
    polyface: { title: 'Polyface Output',   nav: 'Polyface' },
    '3do':    { title: '3DO Output',        nav: '3DO'      },
    loopy:    { title: 'Loopy Output',      nav: 'Loopy'    },
    uart:     { title: 'UART Output',       nav: 'UART'     },
};

export class NativeOutputCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
        this.config = null;
        this.visible = false;
    }

    render() {
        // Initial empty shell — load() rebuilds based on schema
        this.el.innerHTML = `
            <div class="card" id="nativeOutputCard" style="display:none;">
                <h2 id="nativeOutputTitle">Native Output</h2>
                <div class="card-content" id="nativeOutputBody">
                    <p class="hint">Loading…</p>
                </div>
            </div>`;
    }

    async load() {
        const card = this.el.querySelector('#nativeOutputCard');
        try {
            const result = await this.protocol.getNativeOutput();
            if (!result.ok || !result.available) {
                card.style.display = 'none';
                this.visible = false;
                return;
            }
            this.config = result;
            this.visible = true;
            card.style.display = '';
            this.rebuild();
        } catch (e) {
            card.style.display = 'none';
            this.visible = false;
        }
    }

    rebuild() {
        const card  = this.el.querySelector('#nativeOutputCard');
        const title = this.el.querySelector('#nativeOutputTitle');
        const body  = this.el.querySelector('#nativeOutputBody');

        const info = TYPE_INFO[this.config.type] || { title: 'Native Output', nav: this.config.type || 'Native' };
        title.textContent = info.title;

        // Update nav label to match type
        const navLabel = document.getElementById('navNativeOutputLabel');
        if (navLabel) navLabel.textContent = info.nav;

        // Build mode dropdown
        const modes = this.config.modes || [];
        let html = '';
        if (modes.length > 0) {
            html += '<div class="row">';
            html += '<span class="label">Mode</span>';
            html += '<select id="nativeModeSelect">';
            for (const m of modes) {
                const sel = (m === this.config.current_mode) ? ' selected' : '';
                html += `<option value="${m}"${sel}>${this.formatLabel(m)}</option>`;
            }
            html += '</select>';
            html += '</div>';
        }

        // Build pin inputs from schema
        const pins = this.config.pins || {};
        for (const [name, spec] of Object.entries(pins)) {
            const min = spec.min ?? 0;
            const max = spec.max ?? 28;
            const val = spec.value ?? 0;
            const dflt = (spec.default !== undefined) ? `<span class="hint">default: GP${spec.default}</span>` : '';
            html += `<div class="pad-form-row">
                        <span class="label">${spec.label || this.formatLabel(name)}</span>
                        <input type="number" data-pin="${name}" min="${min}" max="${max}" value="${val}">
                        ${dflt}
                     </div>`;
        }

        html += `<div class="buttons" style="margin-top: 12px;">
                    <button id="nativeSaveBtn">Save &amp; Reboot</button>
                 </div>
                 <p class="hint" style="margin-top: 8px;">Device will reboot to apply changes.</p>`;

        body.innerHTML = html;

        body.querySelector('#nativeSaveBtn').addEventListener('click', () => this.save());
        this.dirty = new DirtyTracker(card, body.querySelector('#nativeSaveBtn'));
        this.dirty.snapshot();
    }

    formatLabel(s) {
        return String(s).replace(/_/g, ' ').replace(/\b\w/g, c => c.toUpperCase());
    }

    async save() {
        if (!this.config) return;
        if (!confirm('Save and reboot device to apply native output changes?')) return;

        const payload = {};
        const modeSel = this.el.querySelector('#nativeModeSelect');
        if (modeSel) payload.mode = modeSel.value;

        // Flatten pins into top-level keys (firmware gc_set_native_config looks for "data":N)
        for (const input of this.el.querySelectorAll('input[data-pin]')) {
            payload[input.dataset.pin] = parseInt(input.value, 10);
        }

        try {
            this.log('Saving native output config…');
            const result = await this.protocol.setNativeOutput(payload);
            if (result.ok) {
                this.log(result.reboot ? 'Saved. Device rebooting…' : 'Saved.', 'success');
            } else {
                this.log(`Save failed: ${result.err || 'unknown'}`, 'error');
            }
        } catch (e) {
            this.log(`Failed to save native output: ${e.message}`, 'error');
        }
    }

    isAvailable() { return this.visible; }
}
