// W0 / OKNA §10: generative tests of the window model, P1..P8, P11, P12 (P9, P10, P14 belong to later packages: couplings, back stack, profiles).
// Seeded PRNG, no dependency; the seed is printed on failure. Depth, operation count and areas are PRESETS (CONFIG), examples from the space,
// not its limits: raise them with LOOM_WIN_RUNS / LOOM_WIN_OPS / LOOM_WIN_DEPTH / LOOM_WIN_SEED for a longer run (a parameter, not a code change).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const M = await loader.load('src/shell/windows/model.ts');
const G = await loader.load('src/shell/windows/migrate.ts');
const R = await loader.load('src/shell/windows/registry.ts');
const file = JSON.parse(await readFile(new URL('../../src/shell/data/layouts.json', import.meta.url), 'utf8'));
const { layouts } = G.readLayoutsFile(file);

const env = (k, d) => Number.isFinite(Number(process.env[k])) && process.env[k] ? Number(process.env[k]) : d;
const CONFIG = { runs: env('LOOM_WIN_RUNS', 300), max_ops: env('LOOM_WIN_OPS', 40), max_depth: env('LOOM_WIN_DEPTH', 6), seed: env('LOOM_WIN_SEED', 20261026),
  areas: [[390, 844], [360, 640], [768, 1024], [1440, 900], [1, 1]] };

function rng(seed) { let a = seed >>> 0; return () => { a = (a + 0x6D2B79F5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
const pick = (r, a) => a[Math.floor(r() * a.length)];
const KINDS = R.KINDS.map(k => k.id);
const RATIOS = [0, 1, 0.5, 0.28, 0.9, 0.01];

function randTree(r, depth, ids) {
  if (depth <= 0 || r() < 0.3) { const id = 'L' + ids.length; ids.push(id); return { leaf: id }; }
  return { split: r() < 0.5 ? 'h' : 'v', ratio: r() < 0.4 ? pick(r, RATIOS) : r(), a: randTree(r, depth - 1, ids), b: randTree(r, depth - 1, ids) };
}
function randLayout(r, tag) {
  const base = r();
  if (base < 0.25) return structuredClone(layouts.czat);
  if (base < 0.5) return structuredClone(layouts.siatka);
  const ids = []; const tree = randTree(r, Math.floor(r() * (CONFIG.max_depth + 1)), ids);
  const leaves = Object.fromEntries(ids.map(i => [i, { windows: [], active: null }]));
  const windows = [];
  const n = Math.floor(r() * 6);
  for (let i = 0; i < n; i++) {
    const mode = pick(r, M.MODES); const w = { id: `w_${tag}_${i}`, kind: pick(r, KINDS.filter(k => k)), node: '', mode, z: Math.floor(r() * 5), params: {}, profile: null, title_override: null, collapsed: r() < 0.3, pinned: r() < 0.1 };
    if (mode === 'floating') w.rect = { x: r() * 2 - 0.5, y: r() * 2 - 0.5, w: pick(r, [0, 0.3, 1.5, -1]), h: pick(r, [0, 0.4, 2]), unit: 'fraction' };
    if (mode === 'docked' || mode === 'tabbed' || (mode === 'maximized' && r() < 0.5)) w.dock = { leaf: pick(r, ids) };
    windows.push(w);
  }
  return { id: 'rand', tree, leaves, windows, focus: windows.length ? windows[0].id : null, channels: {}, shelf: { handles: windows.filter(() => r() < 0.3).map(w => w.id) } };
}
function randOp(r, s) {
  const l = s.layout; const wid = () => r() < 0.12 ? 'ghost' : l.windows.length ? pick(r, l.windows).id : 'ghost';
  const leaf = () => r() < 0.1 ? 'Lghost' : pick(r, M.leafIds(l.tree));
  const rect = () => r() < 0.5 ? undefined : { x: pick(r, [r(), -2, 5, NaN, Infinity]), y: r(), w: pick(r, [0, r(), 3, -1]), h: r() };
  switch (Math.floor(r() * 15)) {
    case 14: return { op: 'ratio', path: pick(r, ['', 'a', 'b', 'ab', 'ba', 'aab', 'x', 7]), ratio: pick(r, [0, 0.3, 0.5, 1, -1, 2, NaN, '0.5']) };
    case 0: return { op: 'open', kind: r() < 0.1 ? 'unknown_kind_x' : pick(r, KINDS), node: 'n' + Math.floor(r() * 3), placement: pick(r, ['maximize', 'beside', 'below', 'float', 'tab', 'shelf', 'weird']) };
    case 1: return { op: 'close', id: wid() };
    case 2: return { op: 'close', all_except: l.windows.filter(() => r() < 0.4).map(w => w.id) };
    case 3: return { op: 'close', kind: pick(r, KINDS) };
    case 4: return { op: 'focus', id: wid() };
    case 5: return { op: pick(r, ['next', 'prev']) };
    case 6: return { op: pick(r, ['maximize', 'restore']), id: r() < 0.3 ? undefined : wid() };
    case 7: return { op: 'minimize', id: wid() };
    case 8: return { op: 'dock', id: wid(), target: r() < 0.5 ? { leaf: leaf(), index: pick(r, [undefined, 0, 1, 9, -3]) } : { side: pick(r, ['left', 'right', 'top', 'bottom', 'up']), ratio: pick(r, [undefined, ...RATIOS, 7, -1, NaN]), of: r() < 0.5 ? leaf() : undefined } };
    case 9: return { op: 'float', id: wid(), rect: rect() };
    case 10: return { op: 'split', leaf: leaf(), dir: pick(r, ['h', 'v', 'x']), ratio: pick(r, [undefined, ...RATIOS, 9]), new_first: r() < 0.5 };
    case 11: return { op: 'move', id: wid(), rect: rect() };
    case 12: return { op: 'move', id: wid(), leaf: leaf(), index: pick(r, [undefined, 0, 2]) };
    default: return pick(r, [{ op: 'move', id: wid(), dx: r() - 0.5, dy: pick(r, [0.1, NaN]) }, null, 42, { op: 'zzz' }, { op: 'dock', id: wid() }, { op: 'close' }, { op: 'open' }]);
  }
}
let counter = 0;
const mkCtx = (extra = {}) => ({ kinds: R.kindsCtx(), newId: () => 'w_g' + (counter++).toString(16).padStart(10, '0'), ...extra });
function runs(name, body) {
  test(`${name} (seed ${CONFIG.seed}, ${CONFIG.runs} runs x <=${CONFIG.max_ops} ops)`, () => {
    for (let run = 0; run < CONFIG.runs; run++) {
      const r = rng(CONFIG.seed + run * 7919);
      try { body(r, run); } catch (e) { e.message = `[seed ${CONFIG.seed + run * 7919}, run ${run}] ` + e.message; throw e; }
    }
  });
}
function walk(r, body, ctxExtra) {
  const ctx = mkCtx(ctxExtra); let s = M.createState(randLayout(r, 'a'), 50, ctx);
  const n = Math.floor(r() * (CONFIG.max_ops + 1));
  for (let i = 0; i < n; i++) { const op = randOp(r, s); const res = M.apply(s, op, ctx); body(s, op, res, ctx); s = res.state; }
  return s;
}

runs('P1 totality: every operation on every layout returns a valid layout, never throws; unknown ids give a noop with a reason', (r) => {
  walk(r, (s, op, res) => {
    assert.deepEqual(M.check(res.state.layout), [], JSON.stringify(op));
    if (!res.applied) { assert.strictEqual(res.state, s); }
    if (op && typeof op === 'object' && ['close', 'focus', 'minimize', 'float', 'dock'].includes(op.op) && op.id === 'ghost') { assert.equal(res.applied, false); assert.ok(res.advisories.length >= 1); }
  });
});
runs('P2 nothing disappears silently: every window is visible, on a handle or in the switcher; handles name existing windows', (r) => {
  walk(r, (s, op, res) => { const reach = M.reachability(res.state.layout); for (const w of res.state.layout.windows) assert.ok(['visible', 'handle', 'switcher'].includes(reach[w.id])); });
});
runs('P3 round-trip: read(write(L)) equals L', (r) => {
  walk(r, (s, op, res) => {
    const l = res.state.layout; const doc = JSON.parse(JSON.stringify({ schema: 'loom.shell_layouts/2', layouts: { [l.id]: l } }));
    const back = G.readLayoutsFile(doc).layouts[l.id];
    assert.deepEqual(back, { ...l, schema: 'loom.window_layout/2' });
  });
});
runs('P4 reversibility: undo(op(L)) equals L; redo(undo(op(L))) equals op(L)', (r) => {
  walk(r, (s, op, res) => {
    if (!res.applied) return;
    const u = M.undo(res.state); assert.deepEqual(u.state.layout, s.layout);
    const rd = M.redo(u.state); assert.deepEqual(rd.state.layout, res.state.layout);
  });
});
runs('P5 tree: every leaf holds a window or was explicit; ratios in 0..1; every dock.leaf exists (checked by check())', (r) => {
  walk(r, (s, op, res) => { assert.deepEqual(M.check(res.state.layout), []); });
  walk(r, (s, op, res) => { assert.deepEqual(M.check(res.state.layout, { allowEmptyLeaves: true }), []); }, { collapseEmptyLeaves: 'none' });
});
runs('P6 z-order: floating z is a permutation 0..k-1 after focus', (r) => {
  walk(r, (s, op, res) => { const z = res.state.layout.windows.filter(w => w.mode === 'floating').map(w => w.z).sort((a, b) => a - b); assert.deepEqual(z, z.map((_, i) => i)); });
});
runs('P7 focus: focus is a window id, or null exactly when the layout is empty', (r) => {
  walk(r, (s, op, res) => { const l = res.state.layout; assert.equal(l.focus === null, l.windows.length === 0); if (l.focus) assert.ok(l.windows.some(w => w.id === l.focus)); });
});
runs('P8 plain chat app: czat satisfies the predicate; maximize(chat) then close(all_except chat) satisfies it on any layout', (r) => {
  assert.ok(M.isPlainChatApp(layouts.czat));
  const ctx = mkCtx(); let s = walk(r, () => {}); 
  let res = M.apply(s, { op: 'open', kind: 'chat', node: 'conv:current', placement: 'maximize' }, ctx); s = res.state;
  const chat = s.layout.windows.find(w => w.id === s.layout.focus); assert.equal(chat.kind, 'chat'); assert.equal(chat.mode, 'maximized');
  res = M.apply(s, { op: 'close', all_except: [chat.id] }, ctx); s = res.state;
  const rest = s.layout.windows.filter(w => w.id !== chat.id);
  assert.ok(rest.every(w => w.pinned), 'only pinned windows survive');
  const pinnedOpenList = rest.some(w => w.kind === 'conversation_list' && !w.collapsed);
  assert.equal(M.isPlainChatApp(s.layout), !pinnedOpenList);
  assert.deepEqual(M.check(s.layout), []);
});
runs('P11 area change: every window stays reachable, rects are clamped into the area, dock_narrow counts the narrow occupied leaves', (r) => {
  const l = walk(r, () => {}).layout; const [W, H] = pick(r, CONFIG.areas);
  const f = M.fitToArea(l, { w: W, h: H }, { minUsefulWidth: 160 });
  assert.equal(f.layout.windows.length, l.windows.length);
  for (const w of f.layout.windows) if (w.rect && w.rect.unit === 'fraction') { assert.ok(w.rect.x >= 0 && w.rect.x + w.rect.w <= 1 + 1e-9, 'x'); assert.ok(w.rect.y >= 0 && w.rect.y + w.rect.h <= 1 + 1e-9, 'y'); }
  const sizes = M.leafSizes(l.tree, { w: W, h: H });
  const narrow = M.leafIds(l.tree).filter(k => l.leaves[k].windows.length && sizes[k].w < 160).length;
  assert.equal(f.advisories.filter(a => a.id === 'dock_narrow').length, narrow);
  const reach = M.reachability(f.layout); for (const w of f.layout.windows) assert.ok(reach[w.id]);
});
runs('P12 migration: every (saved v1, layout_mode, hash) gives a known layout id; the conversation of the hash is kept; junk never throws', (r) => {
  const saved = pick(r, ['chat', 'workshop', 'warsztat', null, 7, '', 'praca']);
  const mode = pick(r, ['full_screen_chat', 'views_grid', 'windows', 'layout:praca', 'junk', undefined, 3]);
  const conv = pick(r, [null, 'c_abc123', 'c_x']);
  const hash = pick(r, ['#czat', '#warsztat', '#uklad/praca', '#???', '']) + (conv && r() < 0.8 ? '/' + conv : '');
  const a = G.layoutIdOfSaved(saved); assert.ok(typeof a === 'string' && a.length);
  if (saved === 'chat') assert.equal(a, 'czat'); if (saved === 'workshop') assert.equal(a, 'siatka');
  const m = G.layoutIdOfMode(mode); assert.ok(typeof m.id === 'string' && m.id.length);
  if (mode === 'full_screen_chat' || mode === 'junk') assert.equal(m.id, mode === 'junk' ? 'czat' : 'czat');
  if (mode === 'junk') assert.ok(m.advisory);
  const h = G.parseLayoutHash(hash);
  if (h) { assert.ok(h.layout.length); const c = hash.split('/').slice(hash.startsWith('#uklad') ? 2 : 1)[0]; assert.equal(h.conversationId, c ?? null); }
});
