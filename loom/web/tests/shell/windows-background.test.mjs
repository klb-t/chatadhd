// R101 D1 (PULPIT_R101_2026-10-28 §7, §12, §14 row D1): the sixth window mode `background` - the desktop, exactly one per layout, outside
// windows[] (decision D5, card S1 / M1-R101-D1). Examples first, then a generative pass (seeded; LOOM_BG_RUNS / LOOM_BG_OPS / LOOM_BG_SEED
// raise the presets): after ANY sequence of operations - also ones aimed at the desktop and layouts that smuggle background windows into
// windows[] - (P2) there is exactly one background window, (close) every operation on the desktop is a noop with an advisory, and (P1 of
// P0) with only the desktop seen a way to a window exists unless the person switched the emergency path off.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const M = await loader.load('src/shell/windows/model.ts');
const S = await loader.load('src/shell/windows/session.ts');
const O = await loader.load('src/shell/windows/ops.ts');
const D = await loader.load('src/shell/windows/desktop.ts');
const R = await loader.load('src/shell/windows/registry.ts');
const sv = await loader.load('src/shell/start-view.ts');
const PANELS = ['graph', 'navigator', 'settings', 'memory', 'import', 'logs', 'context', 'methods', 'analysis', 'operations', 'perspectives',
  'onboarding', 'user-knowledge', 'graph-entry', 'view-engine', 'request'];
const known = id => PANELS.includes(id);
const czat = () => S.layoutState(sv.LAYOUTS.czat, sv.JOURNAL_LIMIT);
const bgCount = l => M.allWindows(l).filter(w => w.mode === 'background').length;

test('the desktop is a window of mode background seen through the adapter; a layout written before D1 stays byte-identical', () => {
  assert.ok(M.MODES.includes('background'));
  for (const id of Object.keys(sv.LAYOUTS)) {
    const s = S.layoutState(sv.LAYOUTS[id], sv.JOURNAL_LIMIT);
    assert.equal(s.layout.desktop, undefined, `${id}: nothing written`);
    const d = M.desktopOf(s.layout);
    assert.deepEqual([d.id, d.kind, d.node, d.mode], ['w_desktop', 'desktop', 'desktop:main', 'background']);
    assert.equal(bgCount(s.layout), 1);
    assert.equal(s.layout.windows.some(w => w.mode === 'background'), false);
    assert.deepEqual(M.check(s.layout), []);
  }
  const k = R.KINDS.find(x => x.id === 'desktop');
  assert.equal(k.mode, 'background');
  assert.equal(k.component, 'Desktop');
  assert.equal(R.KINDS.find(x => x.id === 'trash').status, 'planned');
});

test('close on the desktop is a noop with desktop_not_closable; dock/float/move/maximize/minimize = desktop_fixed; open = desktop_one', () => {
  const s = czat(); const id = M.desktopOf(s.layout).id;
  const cases = [
    [{ op: 'close', id }, 'desktop_not_closable'], [{ op: 'close', kind: 'desktop' }, 'desktop_not_closable'],
    [{ op: 'minimize', id }, 'desktop_fixed'], [{ op: 'maximize', id }, 'desktop_fixed'], [{ op: 'restore', id }, 'desktop_fixed'],
    [{ op: 'float', id }, 'desktop_fixed'], [{ op: 'move', id, dx: 0.1 }, 'desktop_fixed'], [{ op: 'dock', id, target: { leaf: 'L0' } }, 'desktop_fixed'],
    [{ op: 'open', kind: 'desktop' }, 'desktop_one'],
  ];
  for (const [op, code] of cases) {
    const r = M.apply(s, op, S.WINDOW_CTX);
    assert.equal(r.applied, false, JSON.stringify(op));
    assert.equal(r.state, s, 'the same state, no journal entry');
    assert.deepEqual(r.advisories.map(a => a.id), [code], JSON.stringify(op));
  }
  // close all_except never reaches the desktop: it is not in windows[]
  const r = M.apply(s, { op: 'close', all_except: [] }, S.WINDOW_CTX);
  assert.equal(r.state.layout.windows.length, 0);
  assert.equal(bgCount(r.state.layout), 1);
});

test('a background window inside windows[] becomes the desktop; a second one is minimized with desktop_duplicate (nothing dropped)', () => {
  const l = structuredClone(sv.LAYOUTS.czat);
  const extra = (id, kind) => ({ id, kind, node: 'desktop:' + id, mode: 'background', z: 3, params: { declaration: 'pulpit_graf' }, profile: null, title_override: null, collapsed: false, pinned: false });
  l.windows.push(extra('w_d1', 'desktop'), extra('w_d2', 'navigator'));
  const out = [];
  const n = M.normalize(structuredClone(l), {}, out);
  assert.equal(n.desktop.id, 'w_d1');
  assert.equal(n.desktop.mode, 'background');
  assert.deepEqual(n.desktop.params, { declaration: 'pulpit_graf' }, 'params travel with the desktop');
  assert.equal(n.windows.find(w => w.id === 'w_d2').mode, 'minimized');
  assert.equal(n.windows.length, l.windows.length - 1);
  assert.deepEqual(out.map(a => a.id).filter(x => x.startsWith('desktop_')), ['desktop_lifted', 'desktop_duplicate']);
  assert.deepEqual(M.check(n), []);
  assert.equal(bgCount(n), 1);
  // a stored desktop survives every operation and its id stays out of windows[]
  const s = M.createState(n, 10, S.WINDOW_CTX);
  const t = M.apply(s, { op: 'close', id: 'w_d1' }, S.WINDOW_CTX);
  assert.equal(t.applied, false);
  assert.equal(M.desktopOf(t.state.layout).id, 'w_d1');
});

test('the desktop kind is listed on the desktop (never hidden) with the reason that it is always there', () => {
  const e = D.kindEntries(known, 'czat', 'pl');
  assert.equal(e.length, R.KINDS.length);
  const d = e.find(x => x.kind === 'desktop');
  assert.equal(d.status, 'cannot_open');
  assert.ok(d.reason.length > 20);
  assert.equal(e.find(x => x.kind === 'trash').status, 'planned');
});

// ---------- generative ----------
let seed = Number(process.env.LOOM_BG_SEED ?? 1012);
const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
const pick = a => a[Math.floor(rnd() * a.length)];
const KIND_IDS = [...R.KINDS.map(k => k.id), 'desktop', 'unknown_kind_x'];
function smuggle(s) {   // a layout from outside (import, hand edit) with background windows inside windows[]
  const l = structuredClone(s.layout);
  const n = 1 + Math.floor(rnd() * 3);
  for (let i = 0; i < n; i++) l.windows.push({ id: `w_bg_${seed % 9973}_${i}`, kind: pick(KIND_IDS), node: '', mode: 'background', z: 5, params: {}, profile: null, title_override: null, collapsed: rnd() < 0.2, pinned: false, ...(rnd() < 0.5 ? { dock: { leaf: 'L0' } } : {}) });
  return S.switchLayout(s, l, sv.JOURNAL_LIMIT);
}
function step(s, stats) {
  const all = M.allWindows(s.layout);
  const w = pick(all); const desk = M.desktopOf(s.layout);
  const k = Math.floor(rnd() * 12);
  if (k <= 5) {
    const ops = [{ op: 'close', id: w.id }, { op: 'minimize', id: w.id }, { op: 'maximize', id: w.id }, { op: 'float', id: w.id },
      { op: 'move', id: w.id, dx: 0.05 }, { op: 'dock', id: w.id, target: { side: pick(['left', 'right']) } }, { op: 'open', kind: pick(KIND_IDS), placement: pick(['maximize', 'float', 'beside', 'shelf']) },
      { op: 'close', kind: pick(['desktop', 'chat']) }, { op: 'restore', id: w.id }];
    const op = pick(ops);
    const r = M.apply(s, op, S.WINDOW_CTX);
    const aimed = op.id === desk.id || (op.op === 'close' && op.kind === 'desktop') || (op.op === 'open' && op.kind === 'desktop');
    if (aimed) {
      stats.aimed++;
      assert.equal(r.applied, false, `an op on the desktop changed the state: ${JSON.stringify(op)}`);
      assert.equal(r.state, s);
      assert.ok(r.advisories.some(a => a.id.startsWith('desktop_')), `no desktop advisory for ${JSON.stringify(op)}`);
    }
    return r.state;
  }
  switch (k) {
    case 6: return D.showDesktop(s);
    case 7: return M.undo(s).state;
    case 8: return smuggle(s);
    case 9: return O.openWindow(s, { panel: pick(PANELS) }, pick(['', 'float', 'beside']), pick(['phone', 'desktop'])).state;
    case 10: return S.switchLayout(s, sv.LAYOUTS[pick(Object.keys(sv.LAYOUTS))], sv.JOURNAL_LIMIT);
    default: return D.openFromDesktop(s, pick([{ chat: true }, { panel: pick(PANELS) }]), 'phone');
  }
}
function follow(s, p) {
  if (p.window) return D.restoreFromDesktop(s, p.window);
  if (p.layout) return S.switchLayout(s, sv.LAYOUTS[p.layout], sv.JOURNAL_LIMIT);
  const e = D.kindEntries(known, s.layout.id, 'pl').find(x => x.kind === p.kind);
  return D.openFromDesktop(s, e.target, 'phone');
}

test('generative: exactly one background window after any op; ops on the desktop are noops; P1 of P0 holds on the reached states', () => {
  const runs = Number(process.env.LOOM_BG_RUNS ?? 60), ops = Number(process.env.LOOM_BG_OPS ?? 30);
  const stats = { states: 0, aimed: 0, desktopOnly: 0, followed: 0, waived: 0, smuggledKept: 0 };
  for (let r = 0; r < runs; r++) {
    let s = rnd() < 0.5 ? czat() : S.layoutState(sv.LAYOUTS.siatka, sv.JOURNAL_LIMIT);
    for (let i = 0; i < ops; i++) {
      s = step(s, stats); stats.states++;
      assert.equal(bgCount(s.layout), 1, `background windows != 1 (run ${r} op ${i})`);
      assert.equal(s.layout.windows.some(w => w.mode === 'background'), false, `a background window left in windows[] (run ${r} op ${i})`);
      assert.deepEqual(M.check(s.layout, { allowEmptyLeaves: true }), [], `run ${r} op ${i}`);
      if (s.layout.desktop) stats.smuggledKept++;
      if (D.desktopCover(s.layout) !== 'none') continue;
      const cfg = {};
      if (rnd() < 0.6) cfg.desktop_sections = [...D.DESKTOP.sections, 'nonsense'].filter(() => rnd() < 0.4);
      if (rnd() < 0.4) cfg.desktop_emergency_path = rnd() < 0.5;
      const settings = D.desktopSettings(cfg);
      const paths = D.desktopPaths(s.layout, settings, D.kindEntries(known, s.layout.id, 'pl'), Object.keys(sv.LAYOUTS));
      stats.desktopOnly++;
      if (!paths.length) { assert.equal(settings.emergency, false, `no path while the emergency path is on (run ${r} op ${i})`); stats.waived++; continue; }
      const p = pick(paths); const after = follow(s, p);
      assert.notEqual(D.desktopCover(after.layout), 'none', `the path ${JSON.stringify(p)} showed no window (run ${r} op ${i})`);
      assert.equal(bgCount(after.layout), 1);
      stats.followed++;
    }
  }
  assert.ok(stats.aimed > 50, `too few ops aimed at the desktop: ${stats.aimed}`);
  assert.ok(stats.desktopOnly > 20, `too few desktop-only states: ${stats.desktopOnly}`);
  assert.ok(stats.smuggledKept > 0, 'no smuggled background window was ever lifted');
  console.log(`[windows-background] ${JSON.stringify(stats)}`);
});
