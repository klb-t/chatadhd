import { spawn, execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, writeFileSync, renameSync, rmSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';

const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../../../..');
const nativeSources = [
  'loom/src/onboarding/layers.cpp', 'loom/src/onboarding/presentation.cpp', 'loom/src/onboarding/store.cpp',
  'loom/src/util/json.cpp', 'loom/src/util/utf8.cpp', 'loom/src/util/sha256.cpp', 'loom/src/util/result.cpp',
];
const bridge = 'loom/web/tests/graph-perspectives/native-layers-bridge.cpp';
const compiler = process.env.CXX ?? 'c++';
const buildFlags = ['-std=c++20', '-O0', '-ffunction-sections', '-fdata-sections', '-Iloom/include', '-Iloom/third_party/nlohmann', '-Iloom/third_party/sqlite'];
const linkFlags = ['-Wl,--gc-sections'];
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
let pendingBuild;
export function nativeBuildInfo() {
  const dependencies = execFileSync(compiler, [...buildFlags, '-MM', bridge, ...nativeSources], { cwd: repo, encoding: 'utf8', maxBuffer: 4 * 1024 * 1024 });
  const paths = [...new Set(dependencies.replace(/\\\n/g, ' ').split(/\s+/).filter(path => path && !path.endsWith(':')))].sort();
  const sources = Object.fromEntries(paths.map(path => [path, hash(readFileSync(resolve(repo, path)))]));
  const compilerVersion = execFileSync(compiler, ['--version'], { encoding: 'utf8' }).trim();
  const build = { compiler, compiler_version: compilerVersion, compile_flags: buildFlags, link_flags: linkFlags };
  return { schema: 'loom.graph_perspective_native_build/1', source_commit: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: repo, encoding: 'utf8' }).trim(),
    ...build, sources, fingerprint: hash(JSON.stringify({ sources, ...build })), resolver: 'loom::onboarding::DefaultLayers', runtime_source: 'current_checkout',
    source_scope: 'compiler-generated user-header dependency closure of linked translation units' };
}
export async function ensureNativeLayers() {
  if (pendingBuild) return pendingBuild;
  pendingBuild = (async () => {
    const info = nativeBuildInfo();
    const out = resolve(process.env.LOOM_GRAPH_PERSPECTIVES_TMP ?? resolve(tmpdir(), 'loom-graph-perspectives-native'), info.fingerprint);
    mkdirSync(out, { recursive: true });
    const binary = resolve(out, 'native-layers');
    if (!existsSync(binary)) {
      const pendingBinary = `${binary}.${process.pid}.tmp`;
      const args = [...buildFlags, bridge, ...nativeSources, ...linkFlags, '-o', pendingBinary];
      await new Promise((accept, reject) => {
        const child = spawn(compiler, args, { cwd: repo, stdio: ['ignore', 'pipe', 'pipe'] });
        let diagnostics = '';
        child.stdout.on('data', data => { diagnostics += data; }); child.stderr.on('data', data => { diagnostics += data; });
        child.on('error', error => { rmSync(pendingBinary, { force: true }); reject(error); });
        child.on('close', code => {
          if (code === 0) accept();
          else { rmSync(pendingBinary, { force: true }); reject(new Error(`native bridge build failed (${code}): ${diagnostics}`)); }
        });
      });
      renameSync(pendingBinary, binary);
    }
    const manifest = { ...info, binary, binary_sha256: hash(readFileSync(binary)) };
    writeFileSync(resolve(out, 'manifest.json'), JSON.stringify(manifest, null, 2));
    return manifest;
  })().catch(error => { pendingBuild = undefined; throw error; });
  return pendingBuild;
}
export async function invokeNativeLayers(request, options = {}) {
  const { binary } = await ensureNativeLayers();
  return new Promise((accept, reject) => {
    const child = spawn(binary, [], { cwd: repo, stdio: ['pipe', 'pipe', 'pipe'], signal: options.signal });
    let output = ''; let diagnostics = '';
    child.stdout.on('data', data => { output += data; }); child.stderr.on('data', data => { diagnostics += data; });
    child.on('error', reject); child.on('close', code => {
      if (code !== 0) return reject(new Error(`native bridge failed (${code}): ${diagnostics}`));
      try { accept(JSON.parse(output)); } catch (error) { reject(error); }
    });
    child.stdin.end(`${JSON.stringify(request)}\n`);
  });
}
