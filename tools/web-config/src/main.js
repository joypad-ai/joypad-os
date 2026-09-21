import { JoypadConfigApp } from './app.js';

// Initialize app when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.app = new JoypadConfigApp();

    // Web-config build stamp (injected by vite.config.js at build time)
    const el = document.getElementById('wcVersion');
    if (el) {
        const stamp = typeof __WC_BUILD__ !== 'undefined' ? __WC_BUILD__ : 'dev';
        el.textContent = stamp;
        el.title = `Web config build ${stamp}`;
    }
});
