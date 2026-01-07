/**
 * Joypad Config Web App
 */

// Button names matching JP_BUTTON_* order (W3C Gamepad API)
// First 18 buttons are remappable in custom profiles
const BUTTON_NAMES = [
    'B1', 'B2', 'B3', 'B4',     // Face buttons (0-3)
    'L1', 'R1', 'L2', 'R2',     // Shoulders (4-7)
    'S1', 'S2',                 // Select/Start (8-9)
    'L3', 'R3',                 // Stick clicks (10-11)
    'DU', 'DD', 'DL', 'DR',     // D-pad (12-15)
    'A1', 'A2',                 // Auxiliary (16-17) - last remappable buttons
    'A3', 'A4',                 // Extended aux (18-19) - not remappable
    'L4', 'R4'                  // Paddles (20-21) - not remappable
];

// Number of buttons that support remapping in custom profiles
const REMAPPABLE_BUTTON_COUNT = 18;

// Friendly button names for UI
const BUTTON_LABELS = {
    'B1': 'A / Cross',
    'B2': 'B / Circle',
    'B3': 'X / Square',
    'B4': 'Y / Triangle',
    'L1': 'L1 / LB',
    'R1': 'R1 / RB',
    'L2': 'L2 / LT',
    'R2': 'R2 / RT',
    'S1': 'Select / Back',
    'S2': 'Start / Menu',
    'L3': 'L3 / LS',
    'R3': 'R3 / RS',
    'DU': 'D-Pad Up',
    'DD': 'D-Pad Down',
    'DL': 'D-Pad Left',
    'DR': 'D-Pad Right',
    'A1': 'Home / Guide',
    'A2': 'Capture / Touchpad',
    'A3': 'Mute',
    'A4': 'Aux 4',
    'L4': 'L4 / Paddle 1',
    'R4': 'R4 / Paddle 2'
};

// Profile flags
const PROFILE_FLAG_SWAP_STICKS = 1;
const PROFILE_FLAG_INVERT_LY = 2;
const PROFILE_FLAG_INVERT_RY = 4;

class JoypadConfigApp {
    constructor() {
        this.protocol = new CDCProtocol();
        this.streaming = false;
        this.customProfiles = [];
        this.activeProfileIndex = 0;
        this.editingProfileIndex = null;

        // UI Elements
        this.statusDot = document.getElementById('statusDot');
        this.statusText = document.getElementById('statusText');
        this.connectBtn = document.getElementById('connectBtn');
        this.connectBtn2 = document.getElementById('connectBtn2');
        this.connectPrompt = document.getElementById('connectPrompt');
        this.mainContent = document.getElementById('mainContent');
        this.modeSelect = document.getElementById('modeSelect');
        this.wiimoteOrientSelect = document.getElementById('wiimoteOrientSelect');
        this.streamBtn = document.getElementById('streamBtn');
        this.logEl = document.getElementById('log');

        // Device info
        this.deviceApp = document.getElementById('deviceApp');
        this.deviceVersion = document.getElementById('deviceVersion');
        this.deviceBoard = document.getElementById('deviceBoard');
        this.deviceSerial = document.getElementById('deviceSerial');
        this.deviceCommit = document.getElementById('deviceCommit');
        this.deviceBuild = document.getElementById('deviceBuild');

        // Input/Output display
        this.inputButtons = document.querySelectorAll('#inputButtons .btn');
        this.outputButtons = document.querySelectorAll('#outputButtons .btn');

        // Custom profile UI elements
        this.profileListEl = document.getElementById('profileList');
        this.profileEditorModal = document.getElementById('profileEditorModal');
        this.profileNameInput = document.getElementById('profileNameInput');
        this.buttonMapContainer = document.getElementById('buttonMapContainer');
        this.leftStickSens = document.getElementById('leftStickSens');
        this.rightStickSens = document.getElementById('rightStickSens');

        // Bind events
        this.connectBtn.addEventListener('click', () => this.toggleConnection());
        this.connectBtn2.addEventListener('click', () => this.connect());
        this.modeSelect.addEventListener('change', (e) => this.setMode(e.target.value));
        this.wiimoteOrientSelect.addEventListener('change', (e) => this.setWiimoteOrient(e.target.value));
        this.streamBtn.addEventListener('click', () => this.toggleStreaming());

        document.getElementById('clearBtBtn').addEventListener('click', () => this.clearBtBonds());
        document.getElementById('resetBtn').addEventListener('click', () => this.factoryReset());
        document.getElementById('rebootBtn').addEventListener('click', () => this.reboot());
        document.getElementById('bootselBtn').addEventListener('click', () => this.bootsel());

        // Custom profile events
        document.getElementById('newProfileBtn').addEventListener('click', () => this.openProfileEditor(null));
        document.getElementById('closeEditorBtn').addEventListener('click', () => this.closeProfileEditor());
        document.getElementById('cancelEditorBtn').addEventListener('click', () => this.closeProfileEditor());
        document.getElementById('saveProfileBtn').addEventListener('click', () => this.saveProfile());
        document.getElementById('deleteProfileBtn').addEventListener('click', () => this.deleteProfile());

        // Sensitivity slider events
        this.leftStickSens.addEventListener('input', (e) => {
            document.getElementById('leftStickSensValue').textContent = e.target.value + '%';
        });
        this.rightStickSens.addEventListener('input', (e) => {
            document.getElementById('rightStickSensValue').textContent = e.target.value + '%';
        });

        // Register event handler
        this.protocol.onEvent((event) => this.handleEvent(event));

        // Initialize button mapping UI
        this.initButtonMapUI();

        // Check Web Serial support
        if (!CDCProtocol.isSupported()) {
            this.log('Web Serial not supported in this browser', 'error');
            this.connectBtn.disabled = true;
            this.connectBtn2.disabled = true;
        }
    }

    initButtonMapUI() {
        // Create button mapping dropdowns (only for remappable buttons)
        this.buttonMapContainer.innerHTML = '';
        const remappableButtons = BUTTON_NAMES.slice(0, REMAPPABLE_BUTTON_COUNT);
        for (let i = 0; i < REMAPPABLE_BUTTON_COUNT; i++) {
            const row = document.createElement('div');
            row.className = 'button-map-row';

            const label = document.createElement('span');
            label.className = 'input-label';
            label.textContent = BUTTON_NAMES[i];
            row.appendChild(label);

            const select = document.createElement('select');
            select.id = `buttonMap${i}`;
            select.innerHTML = `
                <option value="0">Passthrough</option>
                ${remappableButtons.map((name, idx) =>
                    `<option value="${idx + 1}">${name} (${BUTTON_LABELS[name]})</option>`
                ).join('')}
                <option value="255">Disabled</option>
            `;
            row.appendChild(select);

            this.buttonMapContainer.appendChild(row);
        }
    }

    log(message, type = '') {
        const entry = document.createElement('div');
        entry.className = 'log-entry' + (type ? ' ' + type : '');
        entry.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;
        this.logEl.appendChild(entry);
        this.logEl.scrollTop = this.logEl.scrollHeight;
    }

    updateConnectionUI(connected) {
        this.statusDot.className = 'status-dot' + (connected ? ' connected' : '');
        this.statusText.textContent = connected ? 'Connected' : 'Disconnected';
        this.connectBtn.textContent = connected ? 'Disconnect' : 'Connect';
        this.connectPrompt.classList.toggle('hidden', connected);
        this.mainContent.classList.toggle('hidden', !connected);
    }

    async toggleConnection() {
        if (this.protocol.connected) {
            await this.disconnect();
        } else {
            await this.connect();
        }
    }

    async connect() {
        try {
            this.log('Connecting...');
            await this.protocol.connect();
            this.log('Connected!', 'success');
            this.updateConnectionUI(true);

            // Load device info
            await this.loadDeviceInfo();
            await this.loadModes();
            await this.loadProfiles();
            await this.loadWiimoteOrient();

        } catch (e) {
            this.log(`Connection failed: ${e.message}`, 'error');
            this.updateConnectionUI(false);
        }
    }

    async disconnect() {
        try {
            if (this.streaming) {
                await this.protocol.enableInputStream(false);
                this.streaming = false;
                this.streamBtn.textContent = 'Start Streaming';
            }
            await this.protocol.disconnect();
            this.log('Disconnected');
        } catch (e) {
            this.log(`Disconnect error: ${e.message}`, 'error');
        }
        this.updateConnectionUI(false);
    }

    async loadDeviceInfo() {
        try {
            const info = await this.protocol.getInfo();
            this.deviceApp.textContent = info.app || '-';
            this.deviceVersion.textContent = info.version || '-';
            this.deviceBoard.textContent = info.board || '-';
            this.deviceSerial.textContent = info.serial || '-';
            this.deviceCommit.textContent = info.commit || '-';
            this.deviceBuild.textContent = info.build || '-';
            this.log(`Device: ${info.app} v${info.version} (${info.commit})`);
        } catch (e) {
            this.log(`Failed to get device info: ${e.message}`, 'error');
        }
    }

    async loadModes() {
        try {
            const result = await this.protocol.listModes();
            this.modeSelect.innerHTML = '';

            for (const mode of result.modes) {
                const option = document.createElement('option');
                option.value = mode.id;
                option.textContent = mode.name;
                option.selected = mode.id === result.current;
                this.modeSelect.appendChild(option);
            }

            this.log(`Loaded ${result.modes.length} modes, current: ${result.current}`);
        } catch (e) {
            this.log(`Failed to load modes: ${e.message}`, 'error');
        }
    }

    async loadProfiles() {
        try {
            const result = await this.protocol.listProfiles();
            this.customProfiles = result.profiles || [];
            this.activeProfileIndex = result.active || 0;
            this.renderProfileList();

            const builtinCount = this.customProfiles.filter(p => p.builtin).length;
            const customCount = this.customProfiles.filter(p => !p.builtin).length;
            this.log(`Loaded ${builtinCount} built-in + ${customCount} custom profiles, active: ${result.active}`);
        } catch (e) {
            this.log(`Failed to load profiles: ${e.message}`, 'error');
        }
    }

    async setMode(modeId) {
        try {
            this.log(`Setting mode to ${modeId}...`);
            const result = await this.protocol.setMode(parseInt(modeId));
            this.log(`Mode set to ${result.name}`, 'success');

            if (result.reboot) {
                this.log('Device will reboot...', 'warning');
                this.updateConnectionUI(false);
            }
        } catch (e) {
            this.log(`Failed to set mode: ${e.message}`, 'error');
        }
    }

    async selectProfile(index) {
        try {
            this.log(`Selecting profile ${index}...`);
            const result = await this.protocol.setProfile(parseInt(index));
            this.activeProfileIndex = index;
            this.renderProfileList();
            this.log(`Profile set to ${result.name}`, 'success');
        } catch (e) {
            this.log(`Failed to select profile: ${e.message}`, 'error');
        }
    }

    async loadWiimoteOrient() {
        try {
            const result = await this.protocol.getWiimoteOrient();
            this.wiimoteOrientSelect.value = result.mode;
            this.log(`Wiimote orientation: ${result.name}`);
        } catch (e) {
            this.log(`Failed to load Wiimote orientation: ${e.message}`, 'error');
        }
    }

    async setWiimoteOrient(mode) {
        try {
            this.log(`Setting Wiimote orientation to ${mode}...`);
            const result = await this.protocol.setWiimoteOrient(parseInt(mode));
            this.log(`Wiimote orientation set to ${result.name}`, 'success');
        } catch (e) {
            this.log(`Failed to set Wiimote orientation: ${e.message}`, 'error');
        }
    }

    async toggleStreaming() {
        try {
            this.streaming = !this.streaming;
            await this.protocol.enableInputStream(this.streaming);
            this.streamBtn.textContent = this.streaming ? 'Stop Streaming' : 'Start Streaming';
            this.log(this.streaming ? 'Input streaming enabled' : 'Input streaming disabled');
        } catch (e) {
            this.log(`Failed to toggle streaming: ${e.message}`, 'error');
            this.streaming = false;
        }
    }

    async clearBtBonds() {
        if (!confirm('Clear all Bluetooth bonds? Devices will need to re-pair.')) {
            return;
        }

        try {
            await this.protocol.clearBtBonds();
            this.log('Bluetooth bonds cleared', 'success');
        } catch (e) {
            this.log(`Failed to clear bonds: ${e.message}`, 'error');
        }
    }

    async factoryReset() {
        if (!confirm('Factory reset? This will clear all settings.')) {
            return;
        }

        try {
            await this.protocol.resetSettings();
            this.log('Factory reset complete, device will reboot...', 'success');
            this.updateConnectionUI(false);
        } catch (e) {
            this.log(`Failed to reset: ${e.message}`, 'error');
        }
    }

    async reboot() {
        try {
            await this.protocol.reboot();
            this.log('Rebooting device...', 'success');
            this.updateConnectionUI(false);
        } catch (e) {
            this.log(`Failed to reboot: ${e.message}`, 'error');
        }
    }

    async bootsel() {
        try {
            await this.protocol.bootsel();
            this.log('Entering bootloader mode...', 'success');
            this.updateConnectionUI(false);
        } catch (e) {
            this.log(`Failed to enter bootloader: ${e.message}`, 'error');
        }
    }

    handleEvent(event) {
        if (event.type === 'input') {
            this.updateInputDisplay(event.buttons, event.axes);
        } else if (event.type === 'output') {
            this.updateOutputDisplay(event.buttons, event.axes);
        } else if (event.type === 'connect') {
            this.log(`Controller connected: ${event.name} (${event.vid}:${event.pid})`);
        } else if (event.type === 'disconnect') {
            this.log(`Controller disconnected: port ${event.port}`);
        }
    }

    updateInputDisplay(buttons, axes) {
        // Update buttons using data-bit attribute for correct bit position
        this.inputButtons.forEach((btn) => {
            const bit = parseInt(btn.dataset.bit);
            const pressed = (buttons & (1 << bit)) !== 0;
            btn.classList.toggle('pressed', pressed);
        });

        // Update axes
        if (axes && axes.length >= 6) {
            document.getElementById('axisLX').textContent = axes[0];
            document.getElementById('axisLY').textContent = axes[1];
            document.getElementById('axisRX').textContent = axes[2];
            document.getElementById('axisRY').textContent = axes[3];
            document.getElementById('axisL2').textContent = axes[4];
            document.getElementById('axisR2').textContent = axes[5];

            document.getElementById('axisLXBar').style.width = (axes[0] / 255 * 100) + '%';
            document.getElementById('axisLYBar').style.width = (axes[1] / 255 * 100) + '%';
            document.getElementById('axisRXBar').style.width = (axes[2] / 255 * 100) + '%';
            document.getElementById('axisRYBar').style.width = (axes[3] / 255 * 100) + '%';
            document.getElementById('axisL2Bar').style.width = (axes[4] / 255 * 100) + '%';
            document.getElementById('axisR2Bar').style.width = (axes[5] / 255 * 100) + '%';
        }
    }

    updateOutputDisplay(buttons, axes) {
        // Update buttons using data-bit attribute for correct bit position
        this.outputButtons.forEach((btn) => {
            const bit = parseInt(btn.dataset.bit);
            const pressed = (buttons & (1 << bit)) !== 0;
            btn.classList.toggle('pressed', pressed);
        });

        // Update axes
        if (axes && axes.length >= 6) {
            document.getElementById('outAxisLX').textContent = axes[0];
            document.getElementById('outAxisLY').textContent = axes[1];
            document.getElementById('outAxisRX').textContent = axes[2];
            document.getElementById('outAxisRY').textContent = axes[3];
            document.getElementById('outAxisL2').textContent = axes[4];
            document.getElementById('outAxisR2').textContent = axes[5];

            document.getElementById('outAxisLXBar').style.width = (axes[0] / 255 * 100) + '%';
            document.getElementById('outAxisLYBar').style.width = (axes[1] / 255 * 100) + '%';
            document.getElementById('outAxisRXBar').style.width = (axes[2] / 255 * 100) + '%';
            document.getElementById('outAxisRYBar').style.width = (axes[3] / 255 * 100) + '%';
            document.getElementById('outAxisL2Bar').style.width = (axes[4] / 255 * 100) + '%';
            document.getElementById('outAxisR2Bar').style.width = (axes[5] / 255 * 100) + '%';
        }
    }

    // ========================================================================
    // PROFILE MANAGEMENT (unified built-in + custom)
    // ========================================================================

    renderProfileList() {
        this.profileListEl.innerHTML = '';

        // customProfiles contains all profiles (built-in + custom) from unified PROFILE.LIST
        for (const profile of this.customProfiles) {
            const item = this.createProfileItem(profile, profile.index === this.activeProfileIndex);
            this.profileListEl.appendChild(item);
        }

        // Update "New Profile" button state (max 4 custom profiles, Default doesn't count)
        const newBtn = document.getElementById('newProfileBtn');
        const customCount = this.customProfiles.filter(p => !p.builtin).length;
        if (customCount >= 4) {
            newBtn.disabled = true;
            newBtn.textContent = 'Max Profiles (4)';
        } else {
            newBtn.disabled = false;
            newBtn.textContent = '+ New Profile';
        }
    }

    createProfileItem(profile, isActive) {
        const item = document.createElement('div');
        item.className = 'profile-item' + (isActive ? ' active' : '');

        const info = document.createElement('div');
        info.className = 'profile-item-info';

        const name = document.createElement('div');
        name.className = 'profile-item-name';
        name.textContent = profile.name;
        info.appendChild(name);

        const details = document.createElement('div');
        details.className = 'profile-item-details';
        details.textContent = profile.builtin ? 'Built-in' : 'Custom';
        info.appendChild(details);

        item.appendChild(info);

        const actions = document.createElement('div');
        actions.className = 'profile-item-actions';

        if (!isActive) {
            const selectBtn = document.createElement('button');
            selectBtn.className = 'secondary';
            selectBtn.textContent = 'Select';
            selectBtn.addEventListener('click', () => this.selectProfile(profile.index));
            actions.appendChild(selectBtn);
        }

        // Clone button for built-in profiles
        if (profile.builtin) {
            const cloneBtn = document.createElement('button');
            cloneBtn.className = 'secondary';
            cloneBtn.textContent = 'Clone';
            cloneBtn.addEventListener('click', () => this.cloneProfile(profile.index, profile.name));
            actions.appendChild(cloneBtn);
        }

        // Edit button for editable (custom) profiles
        if (profile.editable) {
            const editBtn = document.createElement('button');
            editBtn.className = 'secondary';
            editBtn.textContent = 'Edit';
            editBtn.addEventListener('click', () => this.openProfileEditor(profile.index));
            actions.appendChild(editBtn);
        }

        item.appendChild(actions);
        return item;
    }

    async cloneProfile(index, originalName) {
        // Generate clone name
        const cloneName = (originalName + ' Copy').substring(0, 11);

        try {
            this.log(`Cloning profile "${originalName}"...`);
            const result = await this.protocol.cloneProfile(index, cloneName);
            this.log(`Profile cloned as "${result.name}"`, 'success');
            await this.loadProfiles();
        } catch (e) {
            this.log(`Failed to clone profile: ${e.message}`, 'error');
        }
    }

    async openProfileEditor(index) {
        this.editingProfileIndex = index;
        const isNew = index === null;

        document.getElementById('profileEditorTitle').textContent = isNew ? 'New Profile' : 'Edit Profile';
        document.getElementById('deleteProfileBtn').classList.toggle('hidden', isNew);

        if (isNew) {
            // Reset to defaults
            this.profileNameInput.value = '';
            for (let i = 0; i < REMAPPABLE_BUTTON_COUNT; i++) {
                document.getElementById(`buttonMap${i}`).value = '0';
            }
            this.leftStickSens.value = 100;
            this.rightStickSens.value = 100;
            document.getElementById('leftStickSensValue').textContent = '100%';
            document.getElementById('rightStickSensValue').textContent = '100%';
            document.getElementById('flagSwapSticks').checked = false;
            document.getElementById('flagInvertLY').checked = false;
            document.getElementById('flagInvertRY').checked = false;
        } else {
            // Load existing profile using unified API
            try {
                const profile = await this.protocol.getProfile(index);
                this.profileNameInput.value = profile.name || '';

                // Set button map
                const buttonMap = profile.button_map || [];
                for (let i = 0; i < REMAPPABLE_BUTTON_COUNT; i++) {
                    const value = buttonMap[i] !== undefined ? buttonMap[i] : 0;
                    document.getElementById(`buttonMap${i}`).value = value;
                }

                // Set sensitivities
                this.leftStickSens.value = profile.left_stick_sens || 100;
                this.rightStickSens.value = profile.right_stick_sens || 100;
                document.getElementById('leftStickSensValue').textContent = this.leftStickSens.value + '%';
                document.getElementById('rightStickSensValue').textContent = this.rightStickSens.value + '%';

                // Set flags
                const flags = profile.flags || 0;
                document.getElementById('flagSwapSticks').checked = (flags & PROFILE_FLAG_SWAP_STICKS) !== 0;
                document.getElementById('flagInvertLY').checked = (flags & PROFILE_FLAG_INVERT_LY) !== 0;
                document.getElementById('flagInvertRY').checked = (flags & PROFILE_FLAG_INVERT_RY) !== 0;
            } catch (e) {
                this.log(`Failed to load profile: ${e.message}`, 'error');
                return;
            }
        }

        this.profileEditorModal.classList.remove('hidden');
    }

    closeProfileEditor() {
        this.profileEditorModal.classList.add('hidden');
        this.editingProfileIndex = null;
    }

    async saveProfile() {
        const name = this.profileNameInput.value.trim();
        if (!name) {
            alert('Please enter a profile name');
            return;
        }

        // Collect button map (only remappable buttons)
        const buttonMap = [];
        for (let i = 0; i < REMAPPABLE_BUTTON_COUNT; i++) {
            buttonMap.push(parseInt(document.getElementById(`buttonMap${i}`).value));
        }

        // Collect flags
        let flags = 0;
        if (document.getElementById('flagSwapSticks').checked) flags |= PROFILE_FLAG_SWAP_STICKS;
        if (document.getElementById('flagInvertLY').checked) flags |= PROFILE_FLAG_INVERT_LY;
        if (document.getElementById('flagInvertRY').checked) flags |= PROFILE_FLAG_INVERT_RY;

        const data = {
            name,
            button_map: buttonMap,
            left_stick_sens: parseInt(this.leftStickSens.value),
            right_stick_sens: parseInt(this.rightStickSens.value),
            flags
        };

        // Use unified PROFILE.SAVE API
        // index 255 means create new, otherwise update existing
        const index = this.editingProfileIndex === null ? 255 : this.editingProfileIndex;

        try {
            this.log(`Saving profile...`);
            const result = await this.protocol.saveProfile(index, data);
            this.log(`Profile "${result.name}" saved`, 'success');
            this.closeProfileEditor();
            await this.loadProfiles();
        } catch (e) {
            this.log(`Failed to save profile: ${e.message}`, 'error');
        }
    }

    async deleteProfile() {
        if (this.editingProfileIndex === null) {
            return;
        }

        // Check if this is a built-in profile (can't delete)
        const profile = this.customProfiles.find(p => p.index === this.editingProfileIndex);
        if (profile && profile.builtin) {
            alert('Cannot delete built-in profiles');
            return;
        }

        if (!confirm('Delete this profile?')) {
            return;
        }

        try {
            this.log(`Deleting profile ${this.editingProfileIndex}...`);
            await this.protocol.deleteProfile(this.editingProfileIndex);
            this.log('Profile deleted', 'success');
            this.closeProfileEditor();
            await this.loadProfiles();
        } catch (e) {
            this.log(`Failed to delete profile: ${e.message}`, 'error');
        }
    }
}

// Initialize app when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.app = new JoypadConfigApp();
});
