/** Profile Management Card + Editor Modal */

const BUTTON_NAMES = [
    'B1', 'B2', 'B3', 'B4', 'L1', 'R1', 'L2', 'R2',
    'S1', 'S2', 'L3', 'R3', 'DU', 'DD', 'DL', 'DR',
    'A1', 'A2', 'A3', 'A4', 'L4', 'R4'
];

const BUTTON_LABELS = {
    'B1': 'A / Cross', 'B2': 'B / Circle', 'B3': 'X / Square', 'B4': 'Y / Triangle',
    'L1': 'L1 / LB', 'R1': 'R1 / RB', 'L2': 'L2 / LT', 'R2': 'R2 / RT',
    'S1': 'Select / Back', 'S2': 'Start / Menu', 'L3': 'L3 / LS', 'R3': 'R3 / RS',
    'DU': 'D-Pad Up', 'DD': 'D-Pad Down', 'DL': 'D-Pad Left', 'DR': 'D-Pad Right',
    'A1': 'Home / Guide', 'A2': 'Capture / Touchpad',
};

const REMAPPABLE_COUNT = 18;
const FLAG_SWAP_STICKS = 1;
const FLAG_INVERT_LY = 2;
const FLAG_INVERT_RY = 4;

export class ProfilesCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;
        this.profiles = [];
        this.activeIndex = 0;
        this.editingIndex = null;
    }

    render() {
        const remappable = BUTTON_NAMES.slice(0, REMAPPABLE_COUNT);
        const mapOptions = `<option value="0">Passthrough</option>` +
            remappable.map((name, idx) =>
                `<option value="${idx + 1}">${name} (${BUTTON_LABELS[name]})</option>`
            ).join('') +
            `<option value="255">Disabled</option>`;

        const mapRows = remappable.map((name, i) =>
            `<div class="button-map-row">
                <span class="input-label">${name}</span>
                <select id="buttonMap${i}">${mapOptions}</select>
            </div>`
        ).join('');

        this.el.innerHTML = `
            <div class="card" id="profilesCard">
                <h2>Profiles</h2>
                <div class="card-content">
                    <div class="row">
                        <span class="label">Manage Profiles</span>
                        <button id="newProfileBtn" class="secondary">+ New Profile</button>
                    </div>
                    <div id="profileList" class="profile-list"></div>
                </div>
            </div>

            <div id="profileEditorModal" class="modal hidden">
                <div class="modal-content">
                    <div class="modal-header">
                        <h3 id="profileEditorTitle">Edit Profile</h3>
                        <button class="modal-close" id="closeEditorBtn">&times;</button>
                    </div>
                    <div class="modal-body">
                        <div class="form-group">
                            <label>Profile Name</label>
                            <input type="text" id="profileNameInput" maxlength="11" placeholder="Profile name">
                        </div>
                        <div class="form-group">
                            <label>Button Mapping</label>
                            <p class="hint" style="margin-bottom: 10px;">For each input button, select what output button it should produce.</p>
                            <div id="buttonMapContainer" class="button-map-grid">${mapRows}</div>
                        </div>
                        <div class="form-group">
                            <label>Stick Sensitivity</label>
                            <div class="sensitivity-row">
                                <span>Left Stick</span>
                                <input type="range" id="leftStickSens" min="0" max="200" value="100">
                                <span id="leftStickSensValue">100%</span>
                            </div>
                            <div class="sensitivity-row">
                                <span>Right Stick</span>
                                <input type="range" id="rightStickSens" min="0" max="200" value="100">
                                <span id="rightStickSensValue">100%</span>
                            </div>
                        </div>
                        <div class="form-group">
                            <label>SOCD Cleaning</label>
                            <p class="hint" style="margin-bottom: 10px;">How to handle simultaneous opposite cardinal directions.</p>
                            <select id="socdModeSelect" style="width: 100%;">
                                <option value="0">Passthrough (no cleaning)</option>
                                <option value="1">Neutral (cancel both directions)</option>
                                <option value="2">Up Priority (U+D=U, L+R=neutral)</option>
                                <option value="3">Last Win (last input takes priority)</option>
                            </select>
                        </div>
                        <div class="form-group">
                            <label>Options</label>
                            <div class="checkbox-row"><input type="checkbox" id="flagSwapSticks"><label for="flagSwapSticks">Swap Left/Right Sticks</label></div>
                            <div class="checkbox-row"><input type="checkbox" id="flagInvertLY"><label for="flagInvertLY">Invert Left Y Axis</label></div>
                            <div class="checkbox-row"><input type="checkbox" id="flagInvertRY"><label for="flagInvertRY">Invert Right Y Axis</label></div>
                        </div>
                    </div>
                    <div class="modal-footer">
                        <button id="deleteProfileBtn" class="secondary danger">Delete</button>
                        <div style="flex: 1;"></div>
                        <button id="cancelEditorBtn" class="secondary">Cancel</button>
                        <button id="saveProfileBtn">Save Profile</button>
                    </div>
                </div>
            </div>`;

        // Events
        this.el.querySelector('#newProfileBtn').addEventListener('click', () => this.openEditor(null));
        this.el.querySelector('#closeEditorBtn').addEventListener('click', () => this.closeEditor());
        this.el.querySelector('#cancelEditorBtn').addEventListener('click', () => this.closeEditor());
        this.el.querySelector('#saveProfileBtn').addEventListener('click', () => this.save());
        this.el.querySelector('#deleteProfileBtn').addEventListener('click', () => this.delete());

        this.el.querySelector('#leftStickSens').addEventListener('input', (e) => {
            this.el.querySelector('#leftStickSensValue').textContent = e.target.value + '%';
        });
        this.el.querySelector('#rightStickSens').addEventListener('input', (e) => {
            this.el.querySelector('#rightStickSensValue').textContent = e.target.value + '%';
        });
    }

    async load() {
        try {
            const result = await this.protocol.listProfiles();
            this.profiles = result.profiles || [];
            this.activeIndex = result.active || 0;
            this.renderList();

            const builtinCount = this.profiles.filter(p => p.builtin).length;
            const customCount = this.profiles.filter(p => !p.builtin).length;
            this.log(`Loaded ${builtinCount} built-in + ${customCount} custom profiles, active: ${result.active}`);
        } catch (e) {
            this.log(`Failed to load profiles: ${e.message}`, 'error');
        }
    }

    renderList() {
        const list = this.el.querySelector('#profileList');
        list.innerHTML = '';

        for (const profile of this.profiles) {
            const isActive = profile.index === this.activeIndex;
            const item = document.createElement('div');
            item.className = 'profile-item' + (isActive ? ' active' : '');

            const info = document.createElement('div');
            info.className = 'profile-item-info';
            info.innerHTML = `<div class="profile-item-name">${profile.name}</div>
                              <div class="profile-item-details">${profile.builtin ? 'Built-in' : 'Custom'}</div>`;
            item.appendChild(info);

            const actions = document.createElement('div');
            actions.className = 'profile-item-actions';

            if (!isActive) {
                const selectBtn = document.createElement('button');
                selectBtn.className = 'secondary';
                selectBtn.textContent = 'Select';
                selectBtn.addEventListener('click', () => this.select(profile.index));
                actions.appendChild(selectBtn);
            }

            if (profile.builtin) {
                const cloneBtn = document.createElement('button');
                cloneBtn.className = 'secondary';
                cloneBtn.textContent = 'Clone';
                cloneBtn.addEventListener('click', () => this.clone(profile.index, profile.name));
                actions.appendChild(cloneBtn);
            }

            if (profile.editable) {
                const editBtn = document.createElement('button');
                editBtn.className = 'secondary';
                editBtn.textContent = 'Edit';
                editBtn.addEventListener('click', () => this.openEditor(profile.index));
                actions.appendChild(editBtn);
            }

            item.appendChild(actions);
            list.appendChild(item);
        }

        // Update new profile button
        const newBtn = this.el.querySelector('#newProfileBtn');
        const customCount = this.profiles.filter(p => !p.builtin).length;
        newBtn.disabled = customCount >= 4;
        newBtn.textContent = customCount >= 4 ? 'Max Profiles (4)' : '+ New Profile';
    }

    async select(index) {
        try {
            this.log(`Selecting profile ${index}...`);
            const result = await this.protocol.setProfile(parseInt(index));
            this.activeIndex = index;
            this.renderList();
            this.log(`Profile set to ${result.name}`, 'success');
        } catch (e) {
            this.log(`Failed to select profile: ${e.message}`, 'error');
        }
    }

    async clone(index, originalName) {
        const cloneName = (originalName + ' Copy').substring(0, 11);
        try {
            this.log(`Cloning profile "${originalName}"...`);
            const result = await this.protocol.cloneProfile(index, cloneName);
            this.log(`Profile cloned as "${result.name}"`, 'success');
            await this.load();
        } catch (e) {
            this.log(`Failed to clone profile: ${e.message}`, 'error');
        }
    }

    async openEditor(index) {
        this.editingIndex = index;
        const isNew = index === null;
        const modal = this.el.querySelector('#profileEditorModal');

        this.el.querySelector('#profileEditorTitle').textContent = isNew ? 'New Profile' : 'Edit Profile';
        this.el.querySelector('#deleteProfileBtn').classList.toggle('hidden', isNew);

        if (isNew) {
            this.el.querySelector('#profileNameInput').value = '';
            for (let i = 0; i < REMAPPABLE_COUNT; i++) {
                this.el.querySelector(`#buttonMap${i}`).value = '0';
            }
            this.el.querySelector('#leftStickSens').value = 100;
            this.el.querySelector('#rightStickSens').value = 100;
            this.el.querySelector('#leftStickSensValue').textContent = '100%';
            this.el.querySelector('#rightStickSensValue').textContent = '100%';
            this.el.querySelector('#socdModeSelect').value = '0';
            this.el.querySelector('#flagSwapSticks').checked = false;
            this.el.querySelector('#flagInvertLY').checked = false;
            this.el.querySelector('#flagInvertRY').checked = false;
        } else {
            try {
                const profile = await this.protocol.getProfile(index);
                this.el.querySelector('#profileNameInput').value = profile.name || '';
                const map = profile.button_map || [];
                for (let i = 0; i < REMAPPABLE_COUNT; i++) {
                    this.el.querySelector(`#buttonMap${i}`).value = map[i] !== undefined ? map[i] : 0;
                }
                const ls = this.el.querySelector('#leftStickSens');
                const rs = this.el.querySelector('#rightStickSens');
                ls.value = profile.left_stick_sens || 100;
                rs.value = profile.right_stick_sens || 100;
                this.el.querySelector('#leftStickSensValue').textContent = ls.value + '%';
                this.el.querySelector('#rightStickSensValue').textContent = rs.value + '%';
                this.el.querySelector('#socdModeSelect').value = (profile.socd_mode || 0).toString();
                const flags = profile.flags || 0;
                this.el.querySelector('#flagSwapSticks').checked = (flags & FLAG_SWAP_STICKS) !== 0;
                this.el.querySelector('#flagInvertLY').checked = (flags & FLAG_INVERT_LY) !== 0;
                this.el.querySelector('#flagInvertRY').checked = (flags & FLAG_INVERT_RY) !== 0;
            } catch (e) {
                this.log(`Failed to load profile: ${e.message}`, 'error');
                return;
            }
        }

        modal.classList.remove('hidden');
    }

    closeEditor() {
        this.el.querySelector('#profileEditorModal').classList.add('hidden');
        this.editingIndex = null;
    }

    async save() {
        const name = this.el.querySelector('#profileNameInput').value.trim();
        if (!name) { alert('Please enter a profile name'); return; }

        const buttonMap = [];
        for (let i = 0; i < REMAPPABLE_COUNT; i++) {
            buttonMap.push(parseInt(this.el.querySelector(`#buttonMap${i}`).value));
        }

        let flags = 0;
        if (this.el.querySelector('#flagSwapSticks').checked) flags |= FLAG_SWAP_STICKS;
        if (this.el.querySelector('#flagInvertLY').checked) flags |= FLAG_INVERT_LY;
        if (this.el.querySelector('#flagInvertRY').checked) flags |= FLAG_INVERT_RY;

        const data = {
            name,
            button_map: buttonMap,
            left_stick_sens: parseInt(this.el.querySelector('#leftStickSens').value),
            right_stick_sens: parseInt(this.el.querySelector('#rightStickSens').value),
            flags,
            socd_mode: parseInt(this.el.querySelector('#socdModeSelect').value),
        };

        const index = this.editingIndex === null ? 255 : this.editingIndex;

        try {
            this.log('Saving profile...');
            const result = await this.protocol.saveProfile(index, data);
            this.log(`Profile "${result.name}" saved`, 'success');
            this.closeEditor();
            await this.load();
        } catch (e) {
            this.log(`Failed to save profile: ${e.message}`, 'error');
        }
    }

    async delete() {
        if (this.editingIndex === null) return;
        const profile = this.profiles.find(p => p.index === this.editingIndex);
        if (profile && profile.builtin) { alert('Cannot delete built-in profiles'); return; }
        if (!confirm('Delete this profile?')) return;

        try {
            this.log(`Deleting profile ${this.editingIndex}...`);
            await this.protocol.deleteProfile(this.editingIndex);
            this.log('Profile deleted', 'success');
            this.closeEditor();
            await this.load();
        } catch (e) {
            this.log(`Failed to delete profile: ${e.message}`, 'error');
        }
    }
}

export { BUTTON_NAMES, BUTTON_LABELS, REMAPPABLE_COUNT };
