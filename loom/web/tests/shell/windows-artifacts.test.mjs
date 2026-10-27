// W8 (OKNA §7.1, UI08): blocks of an answer as artifacts (shell/artifacts/artifacts.ts, data shell/data/artifacts.json), the window
// `artifact` opened by ops.openArtifactWindow, and the W6 gaps closed here (coupling_max_iterations read by coupling.ts; the saved
// parameter values in the last-view record). Examples first, then a generative pass (seeded; LOOM_ART_RUNS raises the preset): random
// texts with random fences (``` and ~~~, lengths 3-6, closed or not, with and without words, nested other fences) always split into
// segments that give back every line of the text in order, never lose a block body, and give every block a format from the data.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const R = await loader.load('src/shell/artifacts/artifacts.ts');
const O = await loader.load('src/shell/windows/ops.ts');
const S = await loader.load('src/shell/windows/session.ts');
const C = await loader.load('src/shell/windows/coupling.ts');
const sv = await loader.load('src/shell/start-view.ts');
const env = (k, d) => (Number.isFinite(Number(process.env[k])) && process.env[k] ? Number(process.env[k]) : d);
const CONFIG = { runs: env('LOOM_ART_RUNS', 2000), seed: env('LOOM_ART_SEED', 20261027) };

test('examples: code, html (fence word and sniffed), markdown, svg, json, an unknown word falls to code', () => {
  const t = 'Tekst\n\n```python\nprint(1)\n```\nśrodek\n~~~\n<!DOCTYPE html><html><body>x</body></html>\n~~~\n```md\n# a\n```\n```svg\n<svg></svg>\n```\n```json\n{}\n```\n```brainfuck\n+++\n```';
  const segs = R.splitArtifacts(t, 'm_1');
  const arts = segs.filter(s => s.kind === 'artifact').map(s => s.artifact);
  assert.deepEqual(arts.map(a => a.format), ['code', 'html', 'markdown', 'svg', 'json', 'code']);
  assert.deepEqual(arts.map(a => a.index), [0, 1, 2, 3, 4, 5]);
  assert.equal(arts[0].content, 'print(1)'); assert.equal(arts[0].message, 'm_1');
  assert.deepEqual(segs.filter(s => s.kind === 'text').map(s => s.text.trim()), ['Tekst', 'środek']);
  assert.equal(R.fileNameOf(arts[0]), 'artefakt-1.py');
  assert.equal(R.fileNameOf(arts[1]), 'artefakt-2.html');
  assert.equal(R.fileNameOf(arts[5]), 'artefakt-6.txt');
});

test('no block: one text segment (the answer renders as before); inline ``` in a line is not a fence; an unclosed fence runs to the end', () => {
  assert.deepEqual(R.splitArtifacts('zwykła odpowiedź'), [{ kind: 'text', text: 'zwykła odpowiedź' }]);
  assert.equal(R.splitArtifacts('```a``` w linii').filter(s => s.kind === 'artifact').length, 0);
  const open = R.splitArtifacts('x\n```js\nlet a = 1\nlet b = 2');
  assert.equal(open[1].artifact.content, 'let a = 1\nlet b = 2');
  const nested = R.splitArtifacts('````md\n```js\nx\n```\n````');
  assert.equal(nested.length, 1); assert.equal(nested[0].artifact.content, '```js\nx\n```');
});

test('data drives it: min_lines, a new format, a broken sniff pattern sniffs nothing', () => {
  const data = { ...R.ARTIFACTS, min_lines: 3, formats: [{ id: 'mermaid', fence: ['mermaid'], ext: 'mmd', mime: 'text/plain' }, { id: 'bad', fence: [], sniff: '(', ext: 'x', mime: 'text/plain' }, ...R.ARTIFACTS.formats] };
  const segs = R.splitArtifacts('```mermaid\na\nb\nc\n```\n```\nkrótki\n```', undefined, data);
  assert.equal(segs[0].artifact.format, 'mermaid');
  assert.equal(segs.filter(s => s.kind === 'artifact').length, 1, 'a block under min_lines stays text');
  assert.equal(R.formatOf('', '((', data).id, 'code');
});

test('sandbox and the scripts setting: never same-origin; preset off; true/"true"/1 on', () => {
  assert.equal(R.sandboxOf(false), ''); assert.equal(R.sandboxOf(true), 'allow-scripts');
  assert.ok(!R.sandboxOf(true).includes('same-origin'));
  assert.equal(R.scriptsOn({}), false); assert.equal(R.scriptsOn({ artifact_html_scripts: true }), true);
  assert.equal(R.scriptsOn({ artifact_html_scripts: 'true' }), true); assert.equal(R.scriptsOn({ artifact_html_scripts: false }), false);
  assert.equal(R.artifactOf({ format: 'code' }), null); assert.equal(R.artifactOf(null), null);
});

test('openArtifactWindow: a floating window of the kind artifact, one journal entry; the same block again comes forward without a copy', () => {
  const s0 = S.layoutState(sv.LAYOUTS.czat);
  const a = { format: 'code', lang: 'py', content: 'x', message: 'm_1', index: 0 };
  const r = O.openArtifactWindow(s0, a, 'float', 'desktop', S.WINDOW_CTX, { ...O.WINDOW_DATA, window_placements: {} });
  assert.equal(r.receipt.state, 'ok');
  const w = r.state.layout.windows.find(x => x.kind === 'artifact');
  assert.equal(w.mode, 'floating'); assert.deepEqual(w.params.artifact, a);
  assert.equal(r.state.past.length, s0.past.length + 1);
  const again = O.openArtifactWindow(r.state, a, 'float', 'desktop', S.WINDOW_CTX, { ...O.WINDOW_DATA, window_placements: {} });
  assert.equal(again.state.layout.windows.filter(x => x.kind === 'artifact').length, 1);
  assert.equal(again.state.past.length, r.state.past.length, 'coming forward is not a journal entry');
  const other = O.openArtifactWindow(r.state, { ...a, index: 1 }, 'float', 'desktop', S.WINDOW_CTX, { ...O.WINDOW_DATA, window_placements: {} });
  assert.equal(other.state.layout.windows.filter(x => x.kind === 'artifact').length, 2);
});

test('W6 gap B: coupling_max_iterations overrides the preset (numbers and numeric text ≥ 0), anything else keeps it', () => {
  assert.equal(C.couplingDataOf({}).max_iterations, C.COUPLING.max_iterations);
  assert.equal(C.couplingDataOf({ coupling_max_iterations: 3 }).max_iterations, 3);
  assert.equal(C.couplingDataOf({ coupling_max_iterations: '0' }).max_iterations, 0);
  assert.equal(C.couplingDataOf({ coupling_max_iterations: 2.7 }).max_iterations, 2);
  for (const bad of [-1, 'x', null, '', NaN, Infinity]) assert.equal(C.couplingDataOf({ coupling_max_iterations: bad }).max_iterations, C.COUPLING.max_iterations, String(bad));
});

test('W6 gap A: the parameter values are saved with the last windows and read back; a record without windows keeps none', () => {
  const store = new Map(); const st = { getItem: k => store.get(k) ?? null, setItem: (k, v) => store.set(k, v) };
  const windows = sv.LAYOUTS.czat;
  const values = { windows: { w1: { zoom: 2 } }, channels: { zoom: 3 } };
  sv.writeSaved(st, { layout: windows.id, conversationId: null, windows, values });
  assert.deepEqual(sv.readSaved(st).values, values);
  sv.writeSaved(st, { layout: windows.id, conversationId: null, windows: null, values });
  assert.equal(sv.readSaved(st).values, undefined);
});

function rng(seed) { let x = seed >>> 0; return () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x / 4294967296; }; }
test(`generative: ${CONFIG.runs} random texts split losslessly, every block has a data format (seed ${CONFIG.seed})`, () => {
  const r = rng(CONFIG.seed), pick = a => a[Math.floor(r() * a.length)];
  const ids = new Set(R.ARTIFACTS.formats.map(f => f.id));
  let blocks = 0;
  for (let run = 0; run < CONFIG.runs; run++) {
    const lines = [];
    for (let i = 0, n = 1 + Math.floor(r() * 14); i < n; i++) {
      const k = r();
      if (k < 0.25) lines.push(pick(['`', '~']).repeat(3 + Math.floor(r() * 4)) + pick(['', 'html', 'py', 'md', 'svg', 'json', 'zzz', ' js extra']));
      else lines.push(pick(['tekst', '', '<!doctype html>', '<svg>', '  ```', 'a ``` b', '# nagłówek', '    kod']));
    }
    const text = lines.join('\n');
    const segs = R.splitArtifacts(text);
    for (const s of segs) if (s.kind === 'artifact') { blocks++; assert.ok(ids.has(s.artifact.format), s.artifact.format); assert.ok(text.includes(s.artifact.content)); }
    // lossless: every non-empty line of the text is in a segment (text, or a block's body, or a fence line), in order
    const rebuilt = segs.map(s => s.kind === 'text' ? s.text : s.artifact.content).join('\n');
    const body = text.split('\n').filter(l => l.trim() && !/^ {0,3}(`{3,}|~{3,})/.test(l));
    let at = 0; for (const l of body) { const j = rebuilt.indexOf(l, at); assert.ok(j >= 0, `line lost: ${JSON.stringify(l)} in ${JSON.stringify(text)}`); at = j + l.length; }
  }
  assert.ok(blocks > CONFIG.runs / 4, `blocks seen: ${blocks}`);
});
