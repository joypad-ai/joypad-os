/** Device Info — header bar summary + full detail page + update check */
export class DeviceInfoCard {
    constructor(headerEl, cardEl, protocol, log) {
        this.protocol = protocol;
        this.log = log;
        this.headerEl = headerEl;   // .header-info element
        this.cardEl = cardEl;       // #cardDeviceInfo container
    }

    render() {
        this.cardEl.innerHTML = `
            <div class="card">
                <h2>Device Information</h2>
                <div class="card-content">
                    <div class="device-info">
                        <div class="row"><span class="label">App</span><span class="value" id="deviceApp">-</span></div>
                        <div class="row"><span class="label">Version</span><span class="value"><span id="deviceVersion">-</span> <span id="updateStatus" style="font-weight: normal;"></span></span></div>
                        <div class="row"><span class="label">Board</span><span class="value" id="deviceBoard">-</span></div>
                        <div class="row"><span class="label">Serial</span><span class="value" id="deviceSerial">-</span></div>
                        <div class="row"><span class="label">Commit</span><span class="value" id="deviceCommit">-</span></div>
                        <div class="row"><span class="label">Build</span><span class="value" id="deviceBuild">-</span></div>
                    </div>
                </div>
            </div>`;
    }

    async load() {
        try {
            const info = await this.protocol.getInfo();

            // Header bar (compact)
            const headerApp = document.getElementById('headerApp');
            const headerBoard = document.getElementById('headerBoard');
            if (headerApp) headerApp.textContent = `${info.app || 'Joypad'} v${info.version || '?'}`;
            if (headerBoard) headerBoard.textContent = info.board || '-';
            const headerCommit = document.getElementById('headerCommit');
            if (headerCommit) {
                const hash = (info.commit || '-').substring(0, 7);
                headerCommit.textContent = hash;
                headerCommit.href = `https://github.com/joypad-ai/joypad-os/commit/${info.commit}`;
            }

            // Full detail card
            this.setText('deviceApp', info.app);
            this.setText('deviceVersion', info.version);
            this.setText('deviceBoard', info.board);
            this.setText('deviceSerial', info.serial);
            this.setText('deviceCommit', info.commit);
            this.setText('deviceBuild', info.build);

            this.log(`Device: ${info.app} v${info.version} (${info.board}, ${info.commit})`);

            // Check for updates (non-blocking)
            this.checkUpdate(info.version, info.app, info.board);
        } catch (e) {
            this.log(`Failed to get device info: ${e.message}`, 'error');
        }
    }

    async checkUpdate(currentVersion, app, board) {
        const status = document.getElementById('updateStatus');
        if (!status || !currentVersion) return;

        status.textContent = '(checking...)';

        try {
            const res = await fetch('https://api.github.com/repos/joypad-ai/joypad-os/releases/latest');
            if (!res.ok) throw new Error(`HTTP ${res.status}`);
            const release = await res.json();

            const latest = (release.tag_name || '').replace(/^v/, '');
            if (!latest) throw new Error('No version tag');

            if (this.isNewer(latest, currentVersion)) {
                const appLower = (app || '').toLowerCase();
                const boardLower = (board || '').toLowerCase();
                const assets = release.assets || [];
                const match = assets.find(a =>
                    a.name.endsWith('.uf2') &&
                    a.name.includes(`_${appLower}_`) &&
                    a.name.includes(`_${boardLower}.uf2`)
                );

                if (match) {
                    const link = document.createElement('a');
                    link.href = '#';
                    link.style.color = 'var(--accent)';
                    link.textContent = `v${latest} — update`;
                    link.addEventListener('click', (e) => {
                        e.preventDefault();
                        this.doUpdate(match.browser_download_url, match.name);
                    });
                    status.textContent = '— ';
                    status.appendChild(link);
                } else {
                    status.innerHTML = `— <a href="${release.html_url}" target="_blank" style="color: var(--accent);">v${latest} available</a>`;
                }
            } else {
                status.innerHTML = `— <a href="${release.html_url}" target="_blank" style="color: var(--text-muted);">latest</a>`;
            }
        } catch (e) {
            status.textContent = '';
        }
    }

    async doUpdate(downloadUrl, filename) {
        const status = document.getElementById('updateStatus');

        try {
            // Step 1: Download UF2 into memory
            status.textContent = '— downloading...';
            this.log('Downloading firmware...');
            const res = await fetch(downloadUrl);
            if (!res.ok) throw new Error(`Download failed: HTTP ${res.status}`);
            const blob = await res.blob();
            this.log(`Downloaded ${filename} (${(blob.size / 1024).toFixed(0)} KB)`);

            // Step 2: Reboot into bootloader
            status.textContent = '— rebooting to bootloader...';
            this.log('Rebooting device into bootloader...');
            try {
                await this.protocol.bootsel();
            } catch (e) {
                // Expected — device disconnects immediately
            }

            // Step 3: Wait for bootloader drive to mount
            await new Promise(r => setTimeout(r, 2000));

            // Step 4: Let user pick the bootloader drive
            if (window.showDirectoryPicker) {
                status.textContent = '— select bootloader drive...';
                this.log('Select the bootloader drive (e.g., RPI-RP2, FTHR840BOOT)');
                try {
                    const dirHandle = await window.showDirectoryPicker({ mode: 'readwrite' });
                    const fileHandle = await dirHandle.getFileHandle(filename, { create: true });
                    const writable = await fileHandle.createWritable();
                    await writable.write(blob);
                    await writable.close();
                    status.textContent = '— update complete!';
                    this.log('Firmware written! Device will reboot automatically.', 'success');
                    return;
                } catch (e) {
                    if (e.name === 'AbortError') {
                        status.textContent = '— update cancelled';
                        this.log('Update cancelled');
                        return;
                    }
                    // Fall through to browser download
                    this.log(`Direct write failed: ${e.message}. Downloading instead...`);
                }
            }

            // Fallback: trigger browser download
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = filename;
            a.click();
            URL.revokeObjectURL(url);
            status.textContent = '— drag UF2 to bootloader drive';
            this.log('UF2 downloaded. Drag it to the bootloader drive to complete the update.');
        } catch (e) {
            status.textContent = '— update failed';
            this.log(`Update failed: ${e.message}`, 'error');
        }
    }

    /** Returns true if a is newer than b (semver comparison) */
    isNewer(a, b) {
        const pa = a.split('.').map(Number);
        const pb = b.split('.').map(Number);
        for (let i = 0; i < 3; i++) {
            const va = pa[i] || 0;
            const vb = pb[i] || 0;
            if (va > vb) return true;
            if (va < vb) return false;
        }
        return false;
    }

    setText(id, value) {
        const el = document.getElementById(id);
        if (el) el.textContent = value || '-';
    }
}
