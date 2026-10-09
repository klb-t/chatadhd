/** Reproducible Task E gates, without editing the application's package scripts. */
import { spawnSync } from 'node:child_process';
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';
import { webRoot, repoRoot, reportRoot } from './test-loader.mjs';

const flags = new Set(process.argv.slice(2));
const gates = [
  ['native', ['node', 'tests/graph-perspectives/native-layers.test.mjs']],
  ['headless', ['node', 'tests/graph-perspectives/headless.test.mjs']],
  ['adapters', ['node', 'tests/graph-perspectives/adapters.test.mjs']],
  ['workspace', ['node', 'tests/graph-perspectives/workspace.test.mjs']],
  ['compatibility', ['node', 'tests/graph-perspectives/compatibility.test.mjs']],
  ['hook', ['node', 'tests/graph-perspectives/hook.test.mjs']],
  ['existing-workspace', ['npm', 'run', 'test:workspace-state']],
  ['existing-profile', ['npm', 'run', 'test:application-profiles']],
  ['web-build', ['npm', 'run', 'build']],
];
if (!flags.has('--no-browser')) gates.push(['browser', ['node', 'tests/graph-perspectives/browser.test.mjs']]);
if (!flags.has('--no-benchmark')) gates.push(['benchmark', ['node', '--expose-gc', 'tests/graph-perspectives/benchmark.mjs']]);
mkdirSync(reportRoot, { recursive: true });
const outcomes = [];
for (const [id, [program, ...args]] of gates) {
  const started = performance.now();
  const result = spawnSync(program, args, { cwd: webRoot, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 });
  const elapsedMs = performance.now() - started;
  const log = `${result.stdout ?? ''}${result.stderr ?? ''}${result.error ? String(result.error) : ''}`;
  writeFileSync(path.join(reportRoot, `${id}.txt`), log);
  outcomes.push({ id, command: [program, ...args], exitCode: result.status, signal: result.signal, elapsedMs });
  console.log(`${result.status === 0 ? 'PASS' : 'FAIL'} ${id} (${Math.round(elapsedMs)} ms)`);
  if (result.status !== 0) console.log(log.slice(-4000));
}
const git = args => spawnSync('git', args, { cwd: repoRoot, encoding: 'utf8' }).stdout.trim();
const sourcePaths = [...new Set(git(['ls-files', '--cached', '--others', '--exclude-standard', '--', 'loom/web/src/graph-perspectives', 'loom/web/tests/graph-perspectives', 'loom/data/graph_perspectives']).split('\n').filter(Boolean))].sort();
const sourceHashes = Object.fromEntries(sourcePaths.map(file => [file, createHash('sha256').update(readFileSync(path.join(repoRoot, file))).digest('hex')]));
writeFileSync(path.join(reportRoot, 'gates.json'), JSON.stringify({
  sourceHashes,
  measuredAt: new Date().toISOString(), base: '9e20f99ab27e7cd45e1892f83bf60fbe60db3de9',
  head: git(['rev-parse', 'HEAD']), workingTreeStatus: git(['status', '--short', '--', 'loom/web/src/graph-perspectives', 'loom/web/tests/graph-perspectives', 'loom/data/graph_perspectives']),
  outcomes,
}, null, 2) + '\n');
process.exitCode = outcomes.every(row => row.exitCode === 0) ? 0 : 1;
