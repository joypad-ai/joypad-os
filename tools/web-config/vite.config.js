import { defineConfig } from 'vite';
import { viteSingleFile } from 'vite-plugin-singlefile';
import { execSync } from 'node:child_process';

// Build stamp shown in the sidebar footer so a deployed/opened copy of the
// web config identifies itself (commit + build time; "+" = dirty tree).
function buildStamp() {
    let commit = 'dev';
    let dirty = '';
    try {
        // Last commit touching the web config's SOURCE (dist excluded), so the
        // stamp stays valid when dist is committed separately afterwards.
        commit = execSync("git log -1 --format=%h -- src vite.config.js package.json")
            .toString().trim() || 'dev';
        dirty = execSync('git status --porcelain -- src vite.config.js package.json')
            .toString().trim() ? '+' : '';
    } catch { /* not a git checkout */ }
    const date = new Date().toLocaleDateString('sv-SE'); // YYYY-MM-DD, local time
    return `${commit}${dirty} · ${date}`;
}

export default defineConfig({
  root: 'src',
  plugins: [viteSingleFile()],
  define: {
    __WC_BUILD__: JSON.stringify(buildStamp()),
  },
  build: {
    outDir: '../dist',
    emptyOutDir: true,
    target: 'es2020',
  },
});
