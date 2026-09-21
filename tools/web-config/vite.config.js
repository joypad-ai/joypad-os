import { defineConfig } from 'vite';
import { viteSingleFile } from 'vite-plugin-singlefile';
import { execSync } from 'node:child_process';

// Build stamp shown in the sidebar footer so a deployed/opened copy of the
// web config identifies itself (commit + build time; "+" = dirty tree).
function buildStamp() {
    let commit = 'dev';
    let dirty = '';
    try {
        commit = execSync('git rev-parse --short HEAD').toString().trim();
        dirty = execSync('git status --porcelain -- .').toString().trim() ? '+' : '';
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
