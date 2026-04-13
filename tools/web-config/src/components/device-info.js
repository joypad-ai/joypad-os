/** Device Info — renders into the header bar */
export class DeviceInfoCard {
    constructor(container, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.el = container;  // the .header-info element
    }

    render() {
        // Header bar info is already in index.html — just target the spans
    }

    async load() {
        try {
            const info = await this.protocol.getInfo();
            const app = document.getElementById('headerApp');
            const board = document.getElementById('headerBoard');
            if (app) app.textContent = info.app || 'Joypad';
            if (board) board.textContent = `${info.board || '-'} \u2022 ${(info.commit || '-').substring(0, 7)}`;
            this.log(`Device: ${info.app} v${info.version} (${info.board}, ${info.commit})`);
        } catch (e) {
            this.log(`Failed to get device info: ${e.message}`, 'error');
        }
    }
}
