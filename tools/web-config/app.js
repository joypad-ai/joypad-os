/**
 * Joypad Config Web App
 */

// Button names matching JP_BUTTON_* order
const BUTTON_NAMES = [
    'B1', 'B2', 'B3', 'B4',     // Face buttons
    'L1', 'R1', 'L2', 'R2',     // Shoulders
    'S1', 'S2',                 // Select/Start
    'L3', 'R3',                 // Stick clicks
    'DU', 'DD', 'DL', 'DR',     // D-pad
    'A1'                        // Home/Guide
];

class JoypadConfigApp {
    constructor() {
        this.protocol = new CDCProtocol();
        this.streaming = false;

        // UI Elements
        this.statusDot = document.getElementById('statusDot');
        this.statusText = document.getElementById('statusText');
        this.connectBtn = document.getElementById('connectBtn');
        this.connectBtn2 = document.getElementById('connectBtn2');
        this.connectPrompt = document.getElementById('connectPrompt');
        this.mainContent = document.getElementById('mainContent');
        this.modeSelect = document.getElementById('modeSelect');
        this.profileSelect = document.getElementById('profileSelect');
        this.streamBtn = document.getElementById('streamBtn');
        this.logEl = document.getElementById('log');

        // Device info
        this.deviceApp = document.getElementById('deviceApp');
        this.deviceVersion = document.getElementById('deviceVersion');
        this.deviceBoard = document.getElementById('deviceBoard');
        this.deviceSerial = document.getElementById('deviceSerial');
        this.deviceCommit = document.getElementById('deviceCommit');
        this.deviceBuild = document.getElementById('deviceBuild');

        // Input display
        this.inputButtons = document.querySelectorAll('#inputButtons .btn');

        // Bind events
        this.connectBtn.addEventListener('click', () => this.toggleConnection());
        this.connectBtn2.addEventListener('click', () => this.connect());
        this.modeSelect.addEventListener('change', (e) => this.setMode(e.target.value));
        this.profileSelect.addEventListener('change', (e) => this.setProfile(e.target.value));
        this.streamBtn.addEventListener('click', () => this.toggleStreaming());

        document.getElementById('clearBtBtn').addEventListener('click', () => this.clearBtBonds());
        document.getElementById('resetBtn').addEventListener('click', () => this.factoryReset());
        document.getElementById('rebootBtn').addEventListener('click', () => this.reboot());

        // Register event handler
        this.protocol.onEvent((event) => this.handleEvent(event));

        // Check Web Serial support
        if (!CDCProtocol.isSupported()) {
            this.log('Web Serial not supported in this browser', 'error');
            this.connectBtn.disabled = true;
            this.connectBtn2.disabled = true;
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
            this.profileSelect.innerHTML = '';

            if (!result.profiles || result.profiles.length === 0) {
                const option = document.createElement('option');
                option.value = '0';
                option.textContent = 'Default';
                this.profileSelect.appendChild(option);
                return;
            }

            for (const profile of result.profiles) {
                const option = document.createElement('option');
                option.value = profile.id;
                option.textContent = profile.name;
                option.selected = profile.id === result.active;
                this.profileSelect.appendChild(option);
            }

            this.log(`Loaded ${result.profiles.length} profiles, active: ${result.active}`);
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

    async setProfile(index) {
        try {
            this.log(`Setting profile to ${index}...`);
            const result = await this.protocol.setProfile(parseInt(index));
            this.log(`Profile set to ${result.name}`, 'success');
        } catch (e) {
            this.log(`Failed to set profile: ${e.message}`, 'error');
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

    handleEvent(event) {
        if (event.type === 'input') {
            this.updateInputDisplay(event.buttons, event.axes);
        } else if (event.type === 'connect') {
            this.log(`Controller connected: ${event.name} (${event.vid}:${event.pid})`);
        } else if (event.type === 'disconnect') {
            this.log(`Controller disconnected: port ${event.port}`);
        }
    }

    updateInputDisplay(buttons, axes) {
        // Update buttons
        this.inputButtons.forEach((btn, i) => {
            const pressed = (buttons & (1 << i)) !== 0;
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
}

// Initialize app when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.app = new JoypadConfigApp();
});
