import { createServer } from 'vite';
import react from '@vitejs/plugin-react';
import { readFileSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { transformRenderer } from './renderer-transform.mjs';
import { invokeNativeLayers, ensureNativeLayers } from './native-layers-runner.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const web = resolve(here, '../..');
const repo = resolve(web, '../..');
const data = resolve(repo, 'loom/data/graph_perspectives');
const read = name => JSON.parse(readFileSync(resolve(data, name), 'utf8'));
const portIndex = process.argv.indexOf('--port');
const port = Number(portIndex >= 0 ? process.argv[portIndex + 1] : process.env.PORT ?? 5179);
await ensureNativeLayers();
const server = await createServer({
  root: web, configFile: false,
  plugins: [{ name: 'graph-perspectives-reviewable-renderer-hook', enforce: 'pre', transform(source, id) {
    if (id.split('?')[0] === resolve(web, 'src/components/KnowledgeWorkbench.tsx')) return { code: transformRenderer(source), map: null };
  }, configureServer(vite) {
    vite.middlewares.use(async (request, response, next) => {
      const send = (status, value) => { response.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' }); response.end(JSON.stringify(value)); };
      if (request.url === '/api/graph-perspectives/bootstrap' && request.method === 'GET') {
        send(200, { catalog: read('catalog.json'), packet: read('demo.packet.json'), pack: read('graph-perspectives.pack.json') }); return;
      }
      if (request.url === '/api/native-layers' && request.method === 'POST') {
        try {
          const parts = []; for await (const part of request) parts.push(part);
          const result = await invokeNativeLayers(JSON.parse(Buffer.concat(parts).toString('utf8')));
          send(result.error ? 422 : 200, result);
        } catch (failure) { send(500, { error: String(failure) }); }
        return;
      }
      next();
    });
  } }, react()],
  server: { host: '127.0.0.1', port, strictPort: true },
});
await server.listen();
process.stdout.write(`graph-perspectives harness http://127.0.0.1:${port}/src/graph-perspectives/harness.html\n`);
const stop = async () => { await server.close(); process.exit(0); };
process.on('SIGINT', stop); process.on('SIGTERM', stop);
