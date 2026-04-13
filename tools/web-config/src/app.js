import { CDCProtocol, WebSerialTransport, WebBluetoothTransport } from './cdc-protocol.js';

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
        this.debugStreaming = false;
        this.customProfiles = [];
        this.activeProfileIndex = 0;
        this.editingProfileIndex = null;

        // UI Elements
        this.statusDot = document.getElementById('statusDot');
        this.statusText = document.getElementById('statusText');
        this.connectBtn = document.getElementById('connectBtn');
        this.connectSerialBtn = document.getElementById('connectSerialBtn');
        this.connectBleBtn = document.getElementById('connectBleBtn');
        this.connectPrompt = document.getElementById('connectPrompt');
        this.mainContent = document.getElementById('mainContent');
        this.modeSelect = document.getElementById('modeSelect');
        this.bleModeSelect = document.getElementById('bleModeSelect');
        this.bleModeCard = document.getElementById('bleModeCard');
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
        this.connectSerialBtn.addEventListener('click', () => this.connectSerial());
        this.connectBleBtn.addEventListener('click', () => this.connectBluetooth());
        this.modeSelect.addEventListener('change', (e) => this.setMode(e.target.value));
        this.bleModeSelect.addEventListener('change', (e) => this.setBleMode(e.target.value));
        this.wiimoteOrientSelect.addEventListener('change', (e) => this.setWiimoteOrient(e.target.value));
        this.streamBtn.addEventListener('click', () => this.toggleStreaming());

        document.getElementById('clearBtBtn').addEventListener('click', () => this.clearBtBonds());
        document.getElementById('resetBtn').addEventListener('click', () => this.factoryReset());
        document.getElementById('rebootBtn').addEventListener('click', () => this.reboot());
        document.getElementById('bootselBtn').addEventListener('click', () => this.bootsel());
        document.getElementById('rumbleBtn').addEventListener('click', () => this.testRumble());
        document.getElementById('debugStreamBtn').addEventListener('click', () => this.toggleDebugStream());

        // Custom profile events
        document.getElementById('newProfileBtn').addEventListener('click', () => this.openProfileEditor(null));
        document.getElementById('closeEditorBtn').addEventListener('click', () => this.closeProfileEditor());
        document.getElementById('cancelEditorBtn').addEventListener('click', () => this.closeProfileEditor());
        document.getElementById('saveProfileBtn').addEventListener('click', () => this.saveProfile());
        document.getElementById('deleteProfileBtn').addEventListener('click', () => this.deleteProfile());

        // Pad config events
        const padSaveBtn = document.getElementById('padSaveBtn');
        const padResetBtn = document.getElementById('padResetBtn');
        if (padSaveBtn) padSaveBtn.addEventListener('click', () => this.savePadConfig());
        if (padResetBtn) padResetBtn.addEventListener('click', () => this.resetPadConfig());
        const padDeadzone = document.getElementById('padDeadzone');
        if (padDeadzone) padDeadzone.addEventListener('input', (e) => {
            document.getElementById('padDeadzoneValue').textContent = e.target.value;
        });

        // Sensitivity slider events
        this.leftStickSens.addEventListener('input', (e) => {
            document.getElementById('leftStickSensValue').textContent = e.target.value + '%';
        });
        this.rightStickSens.addEventListener('input', (e) => {
            document.getElementById('rightStickSensValue').textContent = e.target.value + '%';
        });

        // Register event and disconnect handlers
        this.protocol.onEvent((event) => this.handleEvent(event));
        this.protocol.onDisconnect(() => {
            this.log('Device disconnected');
            this.streaming = false;
            this.debugStreaming = false;
            this.updateConnectionUI(false);
        });

        // Initialize button mapping UI
        this.initButtonMapUI();

        // Check transport support
        if (!WebSerialTransport.isSupported()) {
            this.connectSerialBtn.disabled = true;
            this.connectSerialBtn.title = 'Web Serial not supported in this browser';
        }
        if (!WebBluetoothTransport.isSupported()) {
            this.connectBleBtn.disabled = true;
            this.connectBleBtn.title = 'Web Bluetooth not supported in this browser';
        }
        if (!CDCProtocol.isSupported()) {
            this.log('Neither Web Serial nor Web Bluetooth supported in this browser', 'error');
            this.connectBtn.disabled = true;
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
        const transport = this.protocol.transportName;
        this.statusDot.className = 'status-dot' + (connected ? ' connected' : '');
        this.statusText.textContent = connected ? `Connected (${transport})` : 'Disconnected';
        this.connectBtn.textContent = connected ? 'Disconnect' : 'Connect';
        this.connectPrompt.classList.toggle('hidden', connected);
        this.mainContent.classList.toggle('hidden', !connected);
    }

    async toggleConnection() {
        if (this.protocol.connected) {
            await this.disconnect();
        } else {
            // Default: try serial first, fall back to bluetooth
            if (WebSerialTransport.isSupported()) {
                await this.connectSerial();
            } else if (WebBluetoothTransport.isSupported()) {
                await this.connectBluetooth();
            }
        }
    }

    async connectSerial() {
        try {
            this.log('Connecting via USB...');
            await this.protocol.connectSerial();
            this.log('Connected via USB!', 'success');
            this.updateConnectionUI(true);
            await this._onConnected();
        } catch (e) {
            this.log(`USB connection failed: ${e.message}`, 'error');
            this.updateConnectionUI(false);
        }
    }

    async connectBluetooth() {
        try {
            this.log('Connecting via BLE...');
            await this.protocol.connectBluetooth();
            this.log('Connected via BLE!', 'success');
            this.updateConnectionUI(true);
            await this._onConnected();
        } catch (e) {
            this.log(`BLE connection failed: ${e.message}`, 'error');
            this.updateConnectionUI(false);
        }
    }

    async _onConnected() {
        // Load device info
        await this.loadDeviceInfo();
        await this.loadModes();
        await this.loadBleModes();
        await this.loadPadConfig();
        await this.loadProfiles();
        await this.loadWiimoteOrient();
    }

    async disconnect() {
        try {
            if (this.streaming) {
                await this.protocol.enableInputStream(false);
                this.streaming = false;
                this.streamBtn.textContent = 'Start Stream';
                this.streamBtn.style.background = '';
            }
            if (this.debugStreaming) {
                this.debugStreaming = false;
                const btn = document.getElementById('debugStreamBtn');
                btn.textContent = 'Debug Stream';
                btn.style.background = '';
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

    async loadBleModes() {
        try {
            const result = await this.protocol.listBleModes();
            this.bleModeSelect.innerHTML = '';

            for (const mode of result.modes) {
                const option = document.createElement('option');
                option.value = mode.id;
                option.textContent = mode.name;
                option.selected = mode.id === result.current;
                this.bleModeSelect.appendChild(option);
            }

            // Show the BLE mode card (device supports BLE output)
            this.bleModeCard.style.display = '';
            this.log(`Loaded ${result.modes.length} BLE modes, current: ${result.current}`);
        } catch (e) {
            // Device doesn't support BLE output modes — hide the card
            this.bleModeCard.style.display = 'none';
        }
    }

    async setBleMode(modeId) {
        try {
            this.log(`Setting BLE mode to ${modeId}...`);
            const result = await this.protocol.setBleMode(parseInt(modeId));
            this.log(`BLE mode set to ${result.name}`, 'success');

            if (result.reboot) {
                this.log('Device will reboot...', 'warning');
                this.updateConnectionUI(false);
            }
        } catch (e) {
            this.log(`Failed to set BLE mode: ${e.message}`, 'error');
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

    async toggleDebugStream() {
        const btn = document.getElementById('debugStreamBtn');
        const enable = !this.debugStreaming;
        btn.textContent = enable ? 'Starting...' : 'Stopping...';
        try {
            await this.protocol.enableDebugStream(enable);
            this.debugStreaming = enable;
            btn.textContent = enable ? 'Stop Debug' : 'Debug Stream';
            btn.style.background = enable ? 'var(--success)' : '';
            this.log(enable ? 'Debug log streaming enabled' : 'Debug log streaming disabled');
        } catch (e) {
            this.debugStreaming = false;
            btn.textContent = 'Debug Stream';
            btn.style.background = '';
            this.log(`Failed to toggle debug stream: ${e.message}`, 'error');
        }
    }

    async toggleStreaming() {
        try {
            this.streaming = !this.streaming;
            await this.protocol.enableInputStream(this.streaming);
            this.streamBtn.textContent = this.streaming ? 'Stop Stream' : 'Start Stream';
            this.streamBtn.style.background = this.streaming ? 'var(--success)' : '';
            this.log(this.streaming ? 'Input streaming enabled' : 'Input streaming disabled');

            if (this.streaming) {
                // Query connected players when streaming starts
                await this.refreshPlayers();
            } else {
                // Clear player info when streaming stops
                document.getElementById('inputDeviceName').textContent = '';
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
                // Display first player's controller name
                const player = result.players[0];
                document.getElementById('inputDeviceName').textContent = `- ${player.name}`;
                this.log(`Connected: ${player.name} (${player.transport})`);
            } else {
                document.getElementById('inputDeviceName').textContent = '- No controller';
            }
        } catch (e) {
            console.log('Failed to get players:', e.message);
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

    async testRumble() {
        try {
            // Test rumble on player 0 with medium intensity for 500ms
            this.log('Testing rumble...');
            await this.protocol.testRumble(0, 200, 200, 500);
            this.log('Rumble test sent', 'success');
        } catch (e) {
            this.log(`Rumble test failed: ${e.message}`, 'error');
        }
    }

    handleEvent(event) {
        if (event.type === 'input') {
            this.updateInputDisplay(event.buttons, event.axes);
        } else if (event.type === 'output') {
            this.updateOutputDisplay(event.buttons, event.axes);
        } else if (event.type === 'connect') {
            this.log(`Controller connected: ${event.name} (${event.vid}:${event.pid})`);
            document.getElementById('inputDeviceName').textContent = `- ${event.name}`;
        } else if (event.type === 'disconnect') {
            this.log(`Controller disconnected: port ${event.port}`);
            document.getElementById('inputDeviceName').textContent = '';
        } else if (event.type === 'log') {
            // Weave firmware debug logs into the main log pane
            if (event.msg) {
                const lines = event.msg.split('\n').filter(l => l.length > 0);
                for (const line of lines) {
                    this.log(line, 'debug');
                }
            }
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
            document.getElementById('socdModeSelect').value = '0';
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

                // Set SOCD mode
                document.getElementById('socdModeSelect').value = (profile.socd_mode || 0).toString();

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

        // Collect SOCD mode
        const socdMode = parseInt(document.getElementById('socdModeSelect').value);

        const data = {
            name,
            button_map: buttonMap,
            left_stick_sens: parseInt(this.leftStickSens.value),
            right_stick_sens: parseInt(this.rightStickSens.value),
            flags,
            socd_mode: socdMode
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
    // ================================================================
    // PAD GPIO CONFIG
    // ================================================================

    // Button names in pad config buttons[] array order
    static PAD_BUTTON_NAMES = [
        'D-Up', 'D-Down', 'D-Left', 'D-Right',
        'B1', 'B2', 'B3', 'B4',
        'L1', 'R1', 'L2', 'R2',
        'S1', 'S2', 'L3', 'R3',
        'A1', 'A2', 'L4', 'R4'
    ];

    buildPadPinSelect(id, value) {
        // Build a GPIO pin select dropdown
        let html = `<select id="${id}" class="pad-pin-select">`;
        html += '<option value="-1">Disabled</option>';
        // Direct GPIO 0-29
        for (let i = 0; i <= 29; i++) {
            html += `<option value="${i}"${value === i ? ' selected' : ''}>GPIO ${i}</option>`;
        }
        // I2C expander 0: 100-115
        for (let i = 100; i <= 115; i++) {
            html += `<option value="${i}"${value === i ? ' selected' : ''}>I2C0 P${i - 100}</option>`;
        }
        // I2C expander 1: 200-215
        for (let i = 200; i <= 215; i++) {
            html += `<option value="${i}"${value === i ? ' selected' : ''}>I2C1 P${i - 200}</option>`;
        }
        html += '</select>';
        return html;
    }

    async loadPadConfig() {
        const card = document.getElementById('padConfigCard');
        try {
            const config = await this.protocol.getPadConfig();

            if (!config.ok || config.name === 'none') {
                // Not a controller app — hide the card
                card.style.display = 'none';
                return;
            }

            // Show the card
            card.style.display = '';

            // Populate fields
            document.getElementById('padConfigName').value = config.name || '';
            document.getElementById('padConfigSource').textContent =
                config.source === 'flash' ? 'Custom (Flash)' : `Default: ${config.name}`;
            document.getElementById('padActiveHigh').value = String(config.active_high || false);

            // Build button pin assignment grid
            const container = document.getElementById('padButtonPins');
            const buttons = config.buttons || [];
            let gridHTML = '';
            for (let i = 0; i < 20; i++) {
                const name = JoypadConfigApp.PAD_BUTTON_NAMES[i];
                const pin = buttons[i] !== undefined ? buttons[i] : -1;
                gridHTML += `<div class="pad-pin-row">
                    <span>${name}</span>
                    ${this.buildPadPinSelect('padBtn' + i, pin)}
                </div>`;
            }
            container.innerHTML = gridHTML;

            // D-pad toggle
            document.getElementById('padDpadToggle').value = config.dpad_toggle !== undefined ? config.dpad_toggle : -1;
            document.getElementById('padDpadToggleInvert').checked = config.dpad_toggle_invert || false;

            // ADC
            const adc = config.adc || [-1, -1, -1, -1];
            document.getElementById('padAdcLX').value = adc[0];
            document.getElementById('padAdcLY').value = adc[1];
            document.getElementById('padAdcRX').value = adc[2];
            document.getElementById('padAdcRY').value = adc[3];
            document.getElementById('padInvertLX').checked = config.invert_lx || false;
            document.getElementById('padInvertLY').checked = config.invert_ly || false;
            document.getElementById('padInvertRX').checked = config.invert_rx || false;
            document.getElementById('padInvertRY').checked = config.invert_ry || false;

            // Deadzone
            document.getElementById('padDeadzone').value = config.deadzone || 10;
            document.getElementById('padDeadzoneValue').textContent = config.deadzone || 10;

            // I2C
            document.getElementById('padI2cSda').value = config.i2c_sda !== undefined ? config.i2c_sda : -1;
            document.getElementById('padI2cScl').value = config.i2c_scl !== undefined ? config.i2c_scl : -1;

            // LED
            document.getElementById('padLedPin').value = config.led_pin !== undefined ? config.led_pin : -1;
            document.getElementById('padLedCount').value = config.led_count || 0;

            // Speaker
            document.getElementById('padSpeakerPin').value = config.speaker_pin !== undefined ? config.speaker_pin : -1;
            document.getElementById('padSpeakerEnablePin').value = config.speaker_enable_pin !== undefined ? config.speaker_enable_pin : -1;

            // Add change listeners for conflict detection
            container.addEventListener('change', () => this.checkPadPinConflicts());
            this.checkPadPinConflicts();

            this.log(`Pad config loaded: ${config.name} (${config.source})`);
        } catch (e) {
            // Command not supported = not a controller app
            card.style.display = 'none';
            this.log(`Pad config not available: ${e.message}`);
        }
    }

    checkPadPinConflicts() {
        const pinCounts = {};
        const conflicts = [];

        // Collect all assigned pins
        for (let i = 0; i < 20; i++) {
            const sel = document.getElementById('padBtn' + i);
            if (!sel) continue;
            const pin = parseInt(sel.value);
            if (pin < 0) continue;
            if (!pinCounts[pin]) pinCounts[pin] = [];
            pinCounts[pin].push(JoypadConfigApp.PAD_BUTTON_NAMES[i]);
            sel.classList.remove('conflict');
        }

        // Check for duplicates
        for (const [pin, names] of Object.entries(pinCounts)) {
            if (names.length > 1) {
                conflicts.push(`Pin ${pin} used by: ${names.join(', ')}`);
                // Mark conflicting selects
                for (let i = 0; i < 20; i++) {
                    const sel = document.getElementById('padBtn' + i);
                    if (sel && parseInt(sel.value) === parseInt(pin)) {
                        sel.classList.add('conflict');
                    }
                }
            }
        }

        // Check ADC/digital overlap (ADC uses GPIO 26-29)
        const adcIds = ['padAdcLX', 'padAdcLY', 'padAdcRX', 'padAdcRY'];
        const adcLabels = ['Left X', 'Left Y', 'Right X', 'Right Y'];
        for (let a = 0; a < 4; a++) {
            const ch = parseInt(document.getElementById(adcIds[a]).value);
            if (ch < 0) continue;
            const adcGpio = 26 + ch;
            if (pinCounts[adcGpio]) {
                conflicts.push(`GPIO ${adcGpio} used as both ADC (${adcLabels[a]}) and digital (${pinCounts[adcGpio].join(', ')})`);
            }
        }

        // Display conflicts
        const el = document.getElementById('padPinConflicts');
        if (conflicts.length > 0) {
            el.innerHTML = conflicts.map(c => `<p>⚠ ${c}</p>`).join('');
            el.style.display = '';
        } else {
            el.style.display = 'none';
        }
    }

    async savePadConfig() {
        // Check for conflicts first
        this.checkPadPinConflicts();
        const conflictEl = document.getElementById('padPinConflicts');
        if (conflictEl.style.display !== 'none') {
            if (!confirm('There are pin conflicts. Save anyway?')) return;
        }

        if (!confirm('Save GPIO configuration? The device will reboot.')) return;

        // Collect button pins
        const buttons = [];
        for (let i = 0; i < 22; i++) {
            const sel = document.getElementById('padBtn' + i);
            buttons.push(sel ? parseInt(sel.value) : -1);
        }

        const config = {
            name: document.getElementById('padConfigName').value || 'Custom',
            active_high: document.getElementById('padActiveHigh').value === 'true',
            dpad_toggle_invert: document.getElementById('padDpadToggleInvert').checked,
            invert_lx: document.getElementById('padInvertLX').checked,
            invert_ly: document.getElementById('padInvertLY').checked,
            invert_rx: document.getElementById('padInvertRX').checked,
            invert_ry: document.getElementById('padInvertRY').checked,
            i2c_sda: parseInt(document.getElementById('padI2cSda').value),
            i2c_scl: parseInt(document.getElementById('padI2cScl').value),
            deadzone: parseInt(document.getElementById('padDeadzone').value),
            buttons: buttons,
            dpad_toggle: parseInt(document.getElementById('padDpadToggle').value),
            adc: [
                parseInt(document.getElementById('padAdcLX').value),
                parseInt(document.getElementById('padAdcLY').value),
                parseInt(document.getElementById('padAdcRX').value),
                parseInt(document.getElementById('padAdcRY').value)
            ],
            led_pin: parseInt(document.getElementById('padLedPin').value),
            led_count: parseInt(document.getElementById('padLedCount').value),
            speaker_pin: parseInt(document.getElementById('padSpeakerPin').value),
            speaker_enable_pin: parseInt(document.getElementById('padSpeakerEnablePin').value),
        };

        try {
            this.log('Saving pad GPIO config...');
            const result = await this.protocol.setPadConfig(config);
            if (result.reboot) {
                this.log('Config saved. Device rebooting...', 'success');
            } else {
                this.log('Config saved.', 'success');
            }
        } catch (e) {
            this.log(`Failed to save pad config: ${e.message}`, 'error');
        }
    }

    async resetPadConfig() {
        if (!confirm('Reset GPIO configuration to compile-time default? The device will reboot.')) return;

        try {
            this.log('Resetting pad config...');
            await this.protocol.resetPadConfig();
            this.log('Config reset. Device rebooting...', 'success');
        } catch (e) {
            this.log(`Failed to reset pad config: ${e.message}`, 'error');
        }
    }
}

export { JoypadConfigApp, BUTTON_NAMES, BUTTON_LABELS, REMAPPABLE_BUTTON_COUNT };
