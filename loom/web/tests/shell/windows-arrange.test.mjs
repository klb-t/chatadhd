// W4 (OKNA §2.2, §4.4, §6.3): docking and floating as actions of a person (arrange.ts), the drop zones and split ratios of dock.ts, and the
// advisories dock_narrow / float_offscreen / float_under_keyboard. Examples from the presets, then a generative pass (seeded; LOOM_ARR_RUNS
// raises the preset): random sequences of drags, drops, menu arrangements, split-edge drags, moves and closes on every screen size keep the
// model's invariants (check() empty), every action is one journal entry and undo restores the layout exactly; nothing throws.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const M = await loader.load('src/shell/windows/model.ts');
const D = await loader.load('src/shell/windows/dock.ts');
const A = await loader.load('src/shell/windows/arrange.ts');
const S = await loader.load('src/shell/windows/session.ts');
const sv = await loader.load('src/shell/start-view.ts');
const env = (k, d) => (Number.isFinite(Number(process.env[k])) && process.env[k] ? Number(process.env[k]) : d);
const CONFIG = { runs: env('LOOM_ARR_RUNS', 200), max_ops: env('LOOM_ARR_OPS', 30), seed: env('LOOM_ARR_SEED', 20261027) };
const phone = { x: 0, y: 0, w: 390, h: 844 }, desk = { x: 0, y: 0, w: 1280, h: 800 };

const czat = () => S.layoutState(sv.LAYOUTS.czat);
const graphBeside = () => {
  let s = S.openPanel(czat(), 'navigator');
  const g = s.layout.windows.find(w => w.params.panel === 'navigator');
  return { s: A.arrangeWindow(s, g.id, 'right'), g: g.id, before: s };
};

test('dock right of the chat: one journal entry, the dock tree is drawn (no maximized window), undo gives the drawer back', () => {
  const { s, g, before } = graphBeside();
  assert.deepEqual(M.check(s.layout), []);
  assert.equal(s.past.length, before.past.length + 1, 'one action = one entry');
  assert.equal(s.layout.windows.some(w => w.mode === 'maximized'), false);
  assert.equal(S.baseWindow(s.layout), null, 'a docked panel: the layout is drawn as its dock tree');
  const view = D.dockView(s.layout);
  assert.deepEqual(D.dockedWindows(view).map(w => w.kind), ['chat', s.layout.windows.find(w => w.id === g).kind]);
  assert.deepEqual(M.undo(s).state.layout, before.layout);
});

test('dock_narrow on a phone (195 px each < preset 240), not on a desktop; fingerprint = device class + kinds; 0 = never narrow', () => {
  const { s } = graphBeside();
  const a = A.windowAdvisories(s.layout, { area: phone, deviceClass: 'phone' });
  assert.equal(a.length, 1); assert.equal(a[0].code, 'dock_narrow');
  assert.equal(a[0].fingerprint, 'phone|chat+' + s.layout.windows.find(w => w.params.panel === 'navigator').kind);
  assert.deepEqual(a[0].evidence.leaves.map(l => l.width), [195, 195]);
  assert.match(a[0].question.pl, /Czy to celowe/);
  assert.equal(A.windowAdvisories(s.layout, { area: desk, deviceClass: 'desktop' }).length, 0);
  assert.equal(A.windowAdvisories(s.layout, { area: phone, deviceClass: 'phone' }, { ...A.ARRANGE, min_useful_width: 0 }).length, 0);
  assert.equal(A.windowAdvisories(czat().layout, { area: phone, deviceClass: 'phone' }).length, 0, 'a maximized chat: nothing narrow');
});

test('float over the chat: the chat stays maximized under it; offscreen and keyboard advisories; their fixes', () => {
  let s = S.openPanel(czat(), 'settings');
  const id = s.layout.windows.find(w => w.params.panel === 'settings').id;
  s = A.arrangeWindow(s, id, 'float');
  assert.equal(s.layout.windows.find(w => w.kind === 'chat').mode, 'maximized');
  assert.equal(s.layout.windows.find(w => w.id === id).mode, 'floating');
  assert.equal(A.windowAdvisories(s.layout, { area: phone, deviceClass: 'phone' }).length, 0);
  const off = A.moveFloating(s, id, { x: 0.9 });
  const adv = A.windowAdvisories(off.layout, { area: phone, deviceClass: 'phone' });
  assert.deepEqual(adv.map(a => a.code), ['float_offscreen']);
  const snapped = A.applyFix(off, adv[0], { area: phone, deviceClass: 'phone' });
  assert.equal(A.windowAdvisories(snapped.layout, { area: phone, deviceClass: 'phone' }).length, 0);
  const kb = { x: 0, y: 500, w: 390, h: 344 };
  const under = A.windowAdvisories(s.layout, { area: phone, deviceClass: 'phone', keyboard: kb });
  assert.deepEqual(under.map(a => a.code), ['float_under_keyboard']);
  const moved = A.applyFix(s, under[0], { area: phone, deviceClass: 'phone', keyboard: kb });
  const r = A.rectBox(moved.layout.windows.find(w => w.id === id).rect, phone);
  assert.ok(Math.abs(r.y + r.h - 500) < 1e-6 || r.y === 0, 'moved above the keyboard (or to the top when taller than the space)');
  assert.equal(A.windowAdvisories(s.layout, { area: phone, deviceClass: 'phone', keyboard: { ...kb, y: 800, h: 44 } }).length, 0, 'under keyboard_min_px: no keyboard');
});

test('chips: remembered dont_ask (kernel list) and kept-now hide; transient keep in the list does not; ask=false hides all', () => {
  const a = [{ code: 'dock_narrow', fingerprint: 'phone|chat+x' }, { code: 'float_offscreen', fingerprint: 'phone|settings' }];
  const rem = A.rememberedKeys({ acks: [{ code: 'dock_narrow', fingerprint: 'phone|chat+x', choice: 'dont_ask' }, { code: 'float_offscreen', fingerprint: 'phone|settings', choice: 'keep' }] });
  assert.deepEqual(A.visibleChips(a, rem, new Set()).map(c => c.code), ['float_offscreen']);
  assert.deepEqual(A.visibleChips(a, rem, new Set(['float_offscreen|phone|settings'])), []);
  assert.deepEqual(A.visibleChips(a, new Set(['dock_narrow|']), new Set()).map(c => c.code), ['float_offscreen'], 'a code answered without fingerprint: every configuration');
  assert.deepEqual(A.visibleChips(a, new Set(), new Set(), false), []);
});

test('drop zones and ratios: edges dock to their side, the middle is a tab, outside/0-px boxes take nothing; ratio clamped', () => {
  const b = { x: 100, y: 0, w: 200, h: 400 };
  assert.equal(D.dropZone(b, 110, 200), 'left'); assert.equal(D.dropZone(b, 295, 200), 'right');
  assert.equal(D.dropZone(b, 200, 10), 'top'); assert.equal(D.dropZone(b, 200, 395), 'bottom');
  assert.equal(D.dropZone(b, 200, 200), 'center'); assert.equal(D.dropZone(b, 50, 200), null);
  assert.equal(D.dropZone({ ...b, w: 0 }, 100, 200), null);
  assert.equal(D.dropZone(b, 200, 200, 0.5), 'left', 'edge 0.5: no middle');
  assert.equal(D.ratioAt(b, 'h', 150, 0), 0.25); assert.equal(D.ratioAt(b, 'h', 999, 0), 1); assert.equal(D.ratioAt({ ...b, w: 0 }, 'h', 1, 0), 0.5, 'a 0-px split: the preset middle');
  const { s } = graphBeside();
  const r = A.setRatio(s, 'b', 0.3);
  assert.equal(M.splitAt(r.layout.tree, 'b').ratio, 0.3);
  assert.equal(D.dockView(r.layout).path, '', 'paths of splits for the grips'); assert.equal(D.dockView(r.layout).b.path, 'b');
  assert.deepEqual(M.undo(r).state.layout, s.layout);
  assert.equal(M.apply(s, { op: 'ratio', path: 'zz', ratio: 0.3 }).applied, false);
  assert.equal(M.apply(s, { op: 'ratio', path: 'aa', ratio: 0.3 }).advisories[0].id, 'unknown_split');
  assert.equal(M.apply(s, { op: 'ratio', path: 'b', ratio: 7 }).state.layout.tree.b.ratio, 1, 'clamped, with zero_size advised');
});

// ---------- generative ----------
function rng(seed) { let a = seed >>> 0; return () => { a = (a + 0x6D2B79F5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
const pick = (r, xs) => xs[Math.floor(r() * xs.length)];
const PANELS = ['navigator', 'settings', 'memory', 'logs', 'import'];
const HOWS = ['left', 'right', 'top', 'bottom', 'tab', 'float', 'maximize', 'nonsense'];
function splitPaths(t, p = '') { return 'leaf' in t ? [] : [p, ...splitPaths(t.a, p + 'a'), ...splitPaths(t.b, p + 'b')]; }
function action(r, s) {
  const ids = s.layout.windows.map(w => w.id); const leaves = M.leafIds(s.layout.tree);
  switch (Math.floor(r() * 7)) {
    case 0: return ['open', S.openPanel(s, pick(r, PANELS))];
    case 1: return ['arrange', A.arrangeWindow(s, pick(r, [...ids, 'ghost']), pick(r, HOWS), r() < 0.5 ? { of: pick(r, [...leaves, 'L99']) } : {})];
    case 2: { const z = pick(r, ['left', 'right', 'top', 'bottom', 'center']); return ['drop', A.arrangeWindow(s, pick(r, ids), z === 'center' ? 'tab' : z, { of: pick(r, leaves) })]; }
    case 3: { const p = splitPaths(s.layout.tree); return ['ratio', p.length ? A.setRatio(s, pick(r, p), pick(r, [0, 0.1, 0.5, 0.9, 1, r()])) : s]; }
    case 4: { const fl = s.layout.windows.filter(w => w.mode === 'floating'); return ['move', fl.length ? A.moveFloating(s, pick(r, fl).id, { x: r() * 2 - 0.5, y: r() * 2 - 0.5, w: pick(r, [0, 0.3, 1.2]) }) : s]; }
    case 5: return ['close', S.closePanels(s)];
    default: return ['float', A.arrangeWindow(s, pick(r, ids), 'float', { rect: { x: r(), y: r(), w: r(), h: r(), unit: pick(r, ['fraction', 'px']) } })];
  }
}
test(`generative: drag/drop/menu/split/move sequences keep the invariants; one action = at most one journal entry; undo restores (seed ${CONFIG.seed}, ${CONFIG.runs} runs x <=${CONFIG.max_ops})`, () => {
  for (let run = 0; run < CONFIG.runs; run++) {
    const seed = CONFIG.seed + run * 7919; const r = rng(seed);
    let s = S.layoutState(sv.LAYOUTS[pick(r, ['czat', 'siatka'])]);
    const area = pick(r, [phone, desk, { x: 0, y: 0, w: 0, h: 0 }, { x: 0, y: 0, w: 4000, h: 300 }]);
    try {
      const n = Math.floor(r() * (CONFIG.max_ops + 1));
      for (let i = 0; i < n; i++) {
        const [name, next] = action(r, s);
        assert.deepEqual(M.check(next.layout), [], name);
        if (next !== s && next.layout !== s.layout) {
          assert.equal(next.past.length, Math.min(s.limit, s.past.length + 1), `${name}: one entry`);
          if (s.past.length < s.limit) assert.deepEqual(M.undo(next).state.layout, s.layout, `${name}: undo restores`);
        }
        const adv = A.windowAdvisories(next.layout, { area, deviceClass: 'x', keyboard: r() < 0.3 ? { x: 0, y: area.h / 2, w: area.w, h: area.h / 2 } : null });
        for (const a of adv) { assert.ok(['dock_narrow', 'float_offscreen', 'float_under_keyboard'].includes(a.code)); assert.equal(typeof a.fingerprint, 'string'); const f = A.applyFix(next, a, { area, deviceClass: 'x', keyboard: { x: 0, y: area.h / 2, w: area.w, h: area.h / 2 } }); assert.deepEqual(M.check(f.layout), [], 'fix ' + a.code); }
        s = next;
      }
    } catch (e) { e.message = `[seed ${seed}, run ${run}] ` + e.message; throw e; }
  }
});
