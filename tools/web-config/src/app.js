import { CDCProtocol, WebSerialTransport, WebBluetoothTransport } from './cdc-protocol.js';
import { DeviceInfoCard } from './components/device-info.js';
import { UsbOutputCard } from './components/usb-output.js';
import { BtOutputCard } from './components/bt-output.js';
import { NativeOutputCard } from './components/native-output.js';
import { PadConfigCard } from './components/pad-config.js';
import { ProfilesCard, BUTTON_NAMES, BUTTON_LABELS, REMAPPABLE_COUNT } from './components/profiles.js';
import { InputTestCard } from './components/input-test.js';
import { UsbHostCard } from './components/usb-host.js';
import { FeedbackCard } from './components/leds.js';
import { RouterCard } from './components/router.js';
import { HotkeysCard } from './components/hotkeys.js';
import { BtHostCard } from './components/bt-host.js';
import { AdvancedCard } from './components/advanced.js';

/**
 * Joypad Config — App Shell
 * Handles connection, navigation, and delegates features to components.
 */

// Page registry: maps page IDs to sidebar groups
const PAGE_GROUPS = {
    'device-info':   'core',
    'router':        'core',
    'profiles':      'core',
    'hotkeys':       'core',
    'usb':           'output',
    'bluetooth':     'output',
    'native-output': 'output',
    'leds':          'output',
    'feedback':      'output',
    'audio':         'output',
    'gpio':          'input',
    'usb-host':      'input',
    'bt-host':       'input',
    'input-test':    'debug',
    'log':           'debug',
    'advanced':      'system',
};

// First page in each group (for mobile tab navigation)
const GROUP_FIRST_PAGE = {
    'core':     'device-info',
    'output':   'usb',
    'input':    'gpio',
    'debug':    'input-test',
    'system':   'advanced',
};

class JoypadConfigApp {
    constructor() {
        this.protocol = new CDCProtocol();
        this.debugStreaming = false;
        this.currentPage = 'device-info';
        this.hasPadConfig = false;
        this.loaded = false;

        // Header / connection UI
        this.statusDot = document.getElementById('statusDot');
        this.statusText = document.getElementById('statusText');
        this.connectBtn = document.getElementById('connectBtn');
        this.connectPrompt = document.getElementById('connectPrompt');
        this.mainContent = document.getElementById('mainContent');
        this.logEl = document.getElementById('log');

        // Initialize components
        const log = (msg, type) => this.log(msg, type);
        this.deviceInfo = new DeviceInfoCard(document.getElementById('headerInfo'), document.getElementById('cardDeviceInfo'), this.protocol, log);
        this.usbOutput = new UsbOutputCard(document.getElementById('cardUsbOutput'), this.protocol, log);
        this.btOutput = new BtOutputCard(document.getElementById('cardBtOutput'), this.protocol, log);
        this.nativeOutput = new NativeOutputCard(document.getElementById('cardNativeOutput'), this.protocol, log);
        this.padConfig = new PadConfigCard(document.getElementById('cardPadConfig'), this.protocol, log);
        this.feedback = new FeedbackCard(document.getElementById('cardFeedback'), this.protocol, log);
        this.router = new RouterCard(document.getElementById('cardRouter'), this.protocol, log);
        this.hotkeys = new HotkeysCard(document.getElementById('cardHotkeys'), this.protocol, log);
        this.usbHost = new UsbHostCard(document.getElementById('cardUsbHost'), this.protocol, log);
        this.btHost = new BtHostCard(document.getElementById('cardBtHost'), this.protocol, log);
        this.profiles = new ProfilesCard(document.getElementById('cardProfiles'), this.protocol, log);
        this.inputTest = new InputTestCard(document.getElementById('cardInputTest'), this.protocol, log);
        this.advanced = new AdvancedCard(document.getElementById('cardAdvanced'), this.protocol, log);

        // Render component HTML
        this.deviceInfo.render();
        this.usbOutput.render();
        this.btOutput.render();
        this.nativeOutput.render();
        this.padConfig.render();
        this.feedback.render();
        this.router.render();
        this.hotkeys.render();
        this.usbHost.render();
        this.btHost.render();
        this.profiles.render();
        this.inputTest.render();
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
            const lastTransport = this.protocol.transportName || this._lastTransport;
            this.log('Device disconnected');
            this.debugStreaming = false;
            this.updateConnectionUI(false);
            // Auto-reconnect after reboot
            this._tryReconnect(0, lastTransport);
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

        // Try auto-reconnect to previously-granted serial port
        this.tryAutoConnect();
    }

    async tryAutoConnect() {
        try {
            const ok = await this.protocol.tryAutoConnect();
            if (ok) {
                this._lastTransport = 'USB';
                this.log('Auto-reconnected via USB', 'success');
                this.updateConnectionUI(true);
                await this.loadAll();
            }
        } catch (e) {
            // Silently fail — user can connect manually
        }
    }

    async _tryReconnect(attempt = 0, transport = 'USB') {
        if (attempt >= 10) {
            this.log('Auto-reconnect failed. Please reconnect manually.', 'error');
            return;
        }
        if (attempt === 0) {
            this.log('Reconnecting...');
        }
        const delay = attempt < 3 ? 1500 : 3000;
        await new Promise(r => setTimeout(r, delay));
        if (this.protocol.connected) return;
        try {
            let ok = false;
            if (transport === 'BLE') {
                await this.protocol.connectBluetooth();
                ok = true;
            } else {
                ok = await this.protocol.tryAutoConnect();
            }
            if (ok) {
                this._lastTransport = transport;
                this.log('Reconnected', 'success');
                this.updateConnectionUI(true);
                await this.loadAll();
                return;
            }
        } catch (e) {}
        this._tryReconnect(attempt + 1, transport);
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

        // Auto-start streaming when visiting input-test
        if (pageId === 'input-test' && !this.inputTest.streaming && this.protocol.connected && this.loaded) {
            this.inputTest.toggleStreaming();
        }

        // Persist in URL hash for reload
        history.replaceState(null, '', '#' + pageId);
    }

    getInitialPage() {
        const hash = location.hash.replace('#', '');
        return (hash && PAGE_GROUPS[hash]) ? hash : 'device-info';
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
        // Hide Controller nav link if device doesn't support pad config
        const gpioLink = document.getElementById('navGpio');
        if (gpioLink) {
            gpioLink.style.display = this.hasPadConfig ? '' : 'none';
        }

        // Hide USB Host nav link if device doesn't support it
        const usbHostLink = document.getElementById('navUsbHost');
        if (usbHostLink) {
            usbHostLink.style.display = this.usbHost.isAvailable() ? '' : 'none';
        }

        // Hide Feedback/Hotkeys nav links if device doesn't support pad config
        const feedbackLink = document.getElementById('navFeedback');
        if (feedbackLink) {
            feedbackLink.style.display = this.feedback.isAvailable() ? '' : 'none';
        }
        const hotkeysLink = document.getElementById('navHotkeys');
        if (hotkeysLink) {
            hotkeysLink.style.display = this.hotkeys.isAvailable() ? '' : 'none';
        }

        // Hide Bluetooth output nav link if device has no BLE output
        const btLink = document.getElementById('navBluetooth');
        if (btLink) {
            btLink.style.display = this.btOutput.isAvailable() ? '' : 'none';
        }

        // Hide Native Output nav link if device has no native console output
        const nativeLink = document.getElementById('navNativeOutput');
        if (nativeLink) {
            nativeLink.style.display = this.nativeOutput.isAvailable() ? '' : 'none';
        }

        // Hide Bluetooth host nav link if device has no BT host features
        const btHostLink = document.getElementById('navBtHost');
        if (btHostLink) {
            btHostLink.style.display = this.btHost.isAvailable() ? '' : 'none';
        }

        // If current page is hidden, navigate to first available
        if (this.currentPage === 'gpio' && !this.hasPadConfig) {
            this.navigateTo('usb');
        }
        if (this.currentPage === 'usb-host' && !this.usbHost.isAvailable()) {
            this.navigateTo('usb');
        }
        if (this.currentPage === 'bluetooth' && !this.btOutput.isAvailable()) {
            this.navigateTo('usb');
        }
        if (this.currentPage === 'native-output' && !this.nativeOutput.isAvailable()) {
            this.navigateTo('usb');
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
        document.getElementById('headerInfo').style.display = connected ? '' : 'none';
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
            this._lastTransport = 'USB';
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
            this._lastTransport = 'BLE';
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
        await this.usbOutput.load();
        await this.btOutput.load();
        await this.nativeOutput.load();
        await this.padConfig.load();
        await this.feedback.load();
        await this.router.load();
        await this.hotkeys.load();
        await this.usbHost.load();
        await this.btHost.load();
        // Check if pad config card is visible to determine nav visibility
        const padCard = document.querySelector('#cardPadConfig .card, #cardPadConfig #padConfigCard');
        this.hasPadConfig = padCard && padCard.style.display !== 'none';
        this.updateNavVisibility();
        await this.profiles.load();
        this.loaded = true;
        // Navigate to default page (may auto-start streaming if input-test)
        this.navigateTo(this.getInitialPage());
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
