/** Test-only TS/JSON loading through the repository's existing Vite compiler. */
import { createServer } from 'vite';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

export const webRoot = fileURLToPath(new URL('../../', import.meta.url));
export const repoRoot = path.resolve(webRoot, '../..');
export const reportRoot = path.join(repoRoot, 'docs/reports/graph-perspectives-2026-10-09');

export async function createModuleLoader() {
  const server = await createServer({
    root: webRoot, configFile: false, appType: 'custom', logLevel: 'error',
    server: { middlewareMode: true },
  });
  return {
    load(relative) {
      const absolute = path.isAbsolute(relative) ? relative : path.resolve(webRoot, relative);
      return server.ssrLoadModule(absolute);
    },
    close: () => server.close(),
  };
}

export async function loadModule(relative) {
  const loader = await createModuleLoader();
  try { return await loader.load(relative); }
  finally { await loader.close(); }
}

// Backwards-friendly alias for suites expecting an esbuild-like helper name.
export const buildModule = loadModule;
