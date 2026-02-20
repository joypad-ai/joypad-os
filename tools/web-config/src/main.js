import './style.css';
import { JoypadConfigApp } from './app.js';

// Initialize app when DOM is ready
document.addEventListener('DOMContentLoaded', () => {
    window.app = new JoypadConfigApp();
});
