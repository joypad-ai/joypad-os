import { CDCProtocol, WebSerialTransport, WebBluetoothTransport } from './cdc-protocol.js';
import { DeviceInfoCard } from './components/device-info.js';
import { ModeSelectCard } from './components/mode-select.js';
import { PadConfigCard } from './components/pad-config.js';
import { ProfilesCard, BUTTON_NAMES, BUTTON_LABELS, REMAPPABLE_COUNT } from './components/profiles.js';
import { InputTestCard } from './components/input-test.js';
import { WiimoteCard } from './components/wiimote.js';
import { AdvancedCard } from './components/advanced.js';

/**
 * Joypad Config — App Shell
 * Handles connection, navigation, and delegates features to components.
 */

// Page registry: maps page IDs to sidebar groups
const PAGE_GROUPS = {
    'modes':      'output',
    'profiles':   'output',
    'gpio':       'hardware',
    'wiimote':    'hardware',
    'input-test': 'debug',
    'log':        'debug',
    'advanced':   'system',
};

// First page in each group (for mobile tab navigation)
const GROUP_FIRST_PAGE = {
    'output':   'modes',
    'hardware': 'gpio',
    'debug':    'input-test',
    'system':   'advanced',
};

class JoypadConfigApp {
    constructor() {
        this.protocol = new CDCProtocol();
        this.debugStreaming = false;
        this.currentPage = 'modes';
        this.hasPadConfig = false;

        // Header / connection UI
        this.statusDot = document.getElementById('statusDot');
        this.statusText = document.getElementById('statusText');
        this.connectBtn = document.getElementById('connectBtn');
        this.connectPrompt = document.getElementById('connectPrompt');
        this.mainContent = document.getElementById('mainContent');
        this.logEl = document.getElementById('log');

        // Initialize components
        const log = (msg, type) => this.log(msg, type);
        this.deviceInfo = new DeviceInfoCard(document.getElementById('headerInfo'), this.protocol, log);
        this.modeSelect = new ModeSelectCard(document.getElementById('cardModeSelect'), this.protocol, log);
        this.padConfig = new PadConfigCard(document.getElementById('cardPadConfig'), this.protocol, log);
        this.profiles = new ProfilesCard(document.getElementById('cardProfiles'), this.protocol, log);
        this.inputTest = new InputTestCard(document.getElementById('cardInputTest'), this.protocol, log);
        this.wiimote = new WiimoteCard(document.getElementById('cardWiimote'), this.protocol, log);
        this.advanced = new AdvancedCard(document.getElementById('cardAdvanced'), this.protocol, log);

        // Render component HTML
        this.deviceInfo.render();
        this.modeSelect.render();
        this.padConfig.render();
        this.profiles.render();
        this.inputTest.render();
        this.wiimote.render();
        this.advanced.render();

        // Connection events
        this.connectBtn.addEventListener('click', () => this.toggleConnection());
        document.getElementById('connectSerialBtn').addEventListener('click', () => this.connectSerial());
        document.getElementById('connectBleBtn').addEventListener('click', () => this.connectBluetooth());

        // Sidebar navigation
        document.querySelectorAll('.nav-link').forEach(link => {
            link.addEventListener('click', (e) => {
                e.preventDefault();
                this.navigateTo(link.dataset.page);
            });
        });

        // Mobile bottom tabs
        document.querySelectorAll('.mobile-tabs .tab').forEach(tab => {
            tab.addEventListener('click', () => {
                const group = tab.dataset.group;
                // Navigate to first visible page in group
                const firstPage = this.getFirstVisiblePage(group);
                if (firstPage) this.navigateTo(firstPage);
            });
        });

        // Debug stream toggle
        document.getElementById('debugStreamBtn').addEventListener('click', () => this.toggleDebugStream());

        // Protocol events
        this.protocol.onEvent((event) => this.handleEvent(event));
        this.protocol.onDisconnect(() => {
            this.log('Device disconnected');
            this.debugStreaming = false;
            this.updateConnectionUI(false);
        });

        // Check transport support
        const serialBtn = document.getElementById('connectSerialBtn');
        const bleBtn = document.getElementById('connectBleBtn');
        if (!WebSerialTransport.isSupported()) {
            serialBtn.disabled = true;
            serialBtn.title = 'Web Serial not supported in this browser';
        }
        if (!WebBluetoothTransport.isSupported()) {
            bleBtn.disabled = true;
            bleBtn.title = 'Web Bluetooth not supported in this browser';
        }
        if (!CDCProtocol.isSupported()) {
            this.log('Neither Web Serial nor Web Bluetooth supported in this browser', 'error');
            this.connectBtn.disabled = true;
        }
    }

    // ================================================================
    // NAVIGATION
    // ================================================================

    navigateTo(pageId) {
        // Auto-stop streaming when leaving input-test
        if (this.currentPage === 'input-test' && pageId !== 'input-test') {
            this.inputTest.stop();
        }

        // Switch active page
        document.querySelectorAll('.page').forEach(p => p.classList.remove('page--active'));
        const target = document.querySelector(`.page[data-page="${pageId}"]`);
        if (target) target.classList.add('page--active');

        // Update sidebar active link
        document.querySelectorAll('.nav-link').forEach(l => l.classList.remove('active'));
        const link = document.querySelector(`.nav-link[data-page="${pageId}"]`);
        if (link) link.classList.add('active');

        // Update mobile tabs
        const group = PAGE_GROUPS[pageId];
        document.querySelectorAll('.mobile-tabs .tab').forEach(t => {
            t.classList.toggle('active', t.dataset.group === group);
        });

        this.currentPage = pageId;
    }

    getFirstVisiblePage(group) {
        // Find first visible nav link in this group
        for (const [page, g] of Object.entries(PAGE_GROUPS)) {
            if (g !== group) continue;
            const link = document.querySelector(`.nav-link[data-page="${page}"]`);
            if (link && link.style.display !== 'none') return page;
        }
        return GROUP_FIRST_PAGE[group];
    }

    updateNavVisibility() {
        // Hide GPIO nav link if device doesn't support pad config
        const gpioLink = document.getElementById('navGpio');
        if (gpioLink) {
            gpioLink.style.display = this.hasPadConfig ? '' : 'none';
        }

        // If current page is hidden, navigate to first available
        if (this.currentPage === 'gpio' && !this.hasPadConfig) {
            this.navigateTo('wiimote');
        }
    }

    // ================================================================
    // LOGGING
    // ================================================================

    log(message, type = '') {
        const entry = document.createElement('div');
        entry.className = 'log-entry' + (type ? ' ' + type : '');
        entry.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;
        this.logEl.appendChild(entry);
        this.logEl.scrollTop = this.logEl.scrollHeight;
    }

    // ================================================================
    // CONNECTION
    // ================================================================

    updateConnectionUI(connected) {
        const transport = this.protocol.transportName;
        this.statusDot.className = 'status-dot' + (connected ? ' connected' : '');
        this.statusText.textContent = connected ? transport : 'Disconnected';
        this.connectBtn.textContent = connected ? 'Disconnect' : 'Connect';
        this.connectPrompt.classList.toggle('hidden', connected);
        this.mainContent.classList.toggle('hidden', !connected);
    }

    async toggleConnection() {
        if (this.protocol.connected) {
            await this.disconnect();
        } else if (WebSerialTransport.isSupported()) {
            await this.connectSerial();
        } else if (WebBluetoothTransport.isSupported()) {
            await this.connectBluetooth();
        }
    }

    async connectSerial() {
        try {
            this.log('Connecting via USB...');
            await this.protocol.connectSerial();
            this.log('Connected via USB!', 'success');
            this.updateConnectionUI(true);
            await this.loadAll();
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
            await this.loadAll();
        } catch (e) {
            this.log(`BLE connection failed: ${e.message}`, 'error');
            this.updateConnectionUI(false);
        }
    }

    async disconnect() {
        try {
            await this.inputTest.stop();
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

    async loadAll() {
        await this.deviceInfo.load();
        await this.modeSelect.load();
        await this.padConfig.load();
        // Check if pad config card is visible to determine nav visibility
        const padCard = document.querySelector('#cardPadConfig .card, #cardPadConfig #padConfigCard');
        this.hasPadConfig = padCard && padCard.style.display !== 'none';
        this.updateNavVisibility();
        await this.profiles.load();
        await this.wiimote.load();
        // Navigate to default page
        this.navigateTo('modes');
    }

    // ================================================================
    // EVENTS
    // ================================================================

    handleEvent(event) {
        if (event.type === 'input' || event.type === 'output' ||
            event.type === 'connect' || event.type === 'disconnect') {
            this.inputTest.handleEvent(event);
        } else if (event.type === 'log') {
            if (event.msg) {
                const lines = event.msg.split('\n').filter(l => l.length > 0);
                for (const line of lines) this.log(line, 'debug');
            }
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
}

export { JoypadConfigApp, BUTTON_NAMES, BUTTON_LABELS, REMAPPABLE_COUNT };
