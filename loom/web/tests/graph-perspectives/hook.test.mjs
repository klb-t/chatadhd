import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import ts from 'typescript';
import { repoRoot, reportRoot } from './test-loader.mjs';
import { transformRenderer } from './renderer-transform.mjs';
const relative = 'loom/web/src/components/KnowledgeWorkbench.tsx';
const source = readFileSync(path.join(repoRoot, relative), 'utf8');
const original = spawnSync('git', ['show', `9e20f99ab27e7cd45e1892f83bf60fbe60db3de9:${relative}`], { cwd: repoRoot, encoding: 'utf8' });
assert.equal(original.status, 0); assert.equal(source, original.stdout, 'B-owned component must remain identical to pinned base');
const transformed = transformRenderer(source);
assert.match(transformed, /export function KnowledgeGraph/);
assert.throws(() => transformRenderer(source.replace('function KnowledgeGraph', 'function DriftedGraph')), /anchor/);
const folder = mkdtempSync(path.join(tmpdir(), 'graph-perspectives-hook-'));
try {
  mkdirSync(path.dirname(path.join(folder, relative)), { recursive: true });
  writeFileSync(path.join(folder, relative), source);
  const patch = path.join(reportRoot, 'integration.patch');
  for (const args of [['apply', '--check', patch], ['apply', patch]]) {
    const result = spawnSync('git', args, { cwd: folder, encoding: 'utf8' });
    assert.equal(result.status, 0, result.stderr);
  }
  assert.equal(readFileSync(path.join(folder, relative), 'utf8'), transformed, 'Harness and proposed B hook must execute identical renderer source');
  assert.equal(readFileSync(path.join(repoRoot, relative), 'utf8'), source);
} finally { rmSync(folder, { recursive: true, force: true }); }
console.log('PASS exact pinned B hook, drift rejection, patch applies, harness equality, original unchanged');

const configPath = path.join(repoRoot, 'loom/web/tsconfig.json');
const config = ts.readConfigFile(configPath, ts.sys.readFile);
assert.equal(config.error, undefined);
const parsed = ts.parseJsonConfigFileContent(config.config, ts.sys, path.dirname(configPath));
const host = ts.createCompilerHost(parsed.options);
const ordinaryRead = host.readFile.bind(host);
host.readFile = file => path.resolve(file) === path.join(repoRoot, relative) ? transformed : ordinaryRead(file);
const program = ts.createProgram(parsed.fileNames, { ...parsed.options, incremental: false, noEmit: true }, host);
const diagnostics = ts.getPreEmitDiagnostics(program);
assert.equal(diagnostics.length, 0, ts.formatDiagnosticsWithColorAndContext(diagnostics, {
  getCurrentDirectory: () => repoRoot, getCanonicalFileName: name => name, getNewLine: () => '\n',
}));
console.log('PASS exact patched renderer TypeScript with current application and headless contracts');
