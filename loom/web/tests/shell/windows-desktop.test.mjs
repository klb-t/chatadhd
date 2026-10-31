// R101 P0 (PULPIT_R101_2026-10-28 §7, §10, §12): the desktop layer under the windows. Examples first, then a generative pass (seeded;
// LOOM_DESK_RUNS / LOOM_DESK_OPS / LOOM_DESK_SEED raise the presets) over random sequences of window steps and random desktop settings:
// P1 - in every reached state the desktop offers a way to put a window on the screen unless the person switched the emergency path off,
// and every offered way really does it (a window is shown afterwards).
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
const chatId = s => s.layout.windows.find(w => w.kind === 'chat')?.id;
const closeOwn = (s, id) => { const r = M.apply(s, { op: 'close', id }, S.WINDOW_CTX).state; return r.layout === s.layout ? s : { ...r, past: [...s.past, { op: 'close', layout: s.layout }], future: [] }; };

test('last_window_close is an open value: desktop (preset), close_app (alias exit), reopen:<kind>, layout:<id>, unknown = desktop', () => {
  assert.deepEqual(D.lastWindowClose(undefined), { do: 'desktop' });
  assert.deepEqual(D.lastWindowClose(''), { do: 'desktop' });
  assert.deepEqual(D.lastWindowClose('close_app'), { do: 'close_app' });
  assert.deepEqual(D.lastWindowClose('exit'), { do: 'close_app' });
  assert.deepEqual(D.lastWindowClose('reopen:navigator'), { do: 'reopen', kind: 'navigator' });
  assert.deepEqual(D.lastWindowClose('layout:siatka'), { do: 'layout', id: 'siatka' });
  assert.deepEqual(D.lastWindowClose('ask'), { do: 'desktop', unknown: 'ask' });
});

test('desktop settings: preset sections, a comma list or an array, unknown ids kept for the advisory; emergency on unless switched off', () => {
  assert.deepEqual(D.desktopSettings({}).sections, D.DESKTOP.sections);
  assert.equal(D.desktopSettings({}).emergency, true);
  assert.deepEqual(D.desktopSettings({ desktop_sections: 'open, wallpaper' }), { sections: ['open'], emergency: true, unknownSections: ['wallpaper'] });
  assert.deepEqual(D.desktopSettings({ desktop_sections: [], desktop_emergency_path: false }), { sections: [], emergency: false, unknownSections: [] });
  assert.equal(D.desktopSettings({ desktop_emergency_path: 'false' }).emergency, false);
});

test('every kind of the registry is on the desktop: planned ones with their reason, unopenable ones with the reason, never hidden', () => {
  const e = D.kindEntries(known, 'czat', 'pl');
  assert.equal(e.length, R.KINDS.length);
  assert.deepEqual(e.find(x => x.kind === 'chat'), { kind: 'chat', label: 'Czat', status: 'available', target: { chat: true } });
  assert.deepEqual(e.find(x => x.kind === 'navigator').target, { panel: 'navigator' });
  for (const x of e.filter(x => x.status !== 'available')) assert.ok(x.reason && x.reason.length > 5, x.kind);
  assert.ok(e.filter(x => x.status === 'planned').length >= 1);
  assert.equal(D.kindEntries(known, 'siatka', 'pl').find(x => x.kind === 'settings_fields').target.panel, 'settings');
});

test('cover: the maximized chat covers the desktop; minimized or closed it does not; a floating panel covers part', () => {
  let s = czat();
  assert.equal(D.desktopCover(s.layout), 'full');
  const min = M.apply(s, { op: 'minimize', id: chatId(s) }, S.WINDOW_CTX).state;
  assert.equal(D.desktopCover(min.layout), 'none');
  const closed = closeOwn(s, chatId(s));
  assert.equal(D.desktopCover(closed.layout), 'none');
  assert.equal(D.liveWindows(closed.layout).length, 0);
  assert.ok(D.closedLast(s.layout, closed), 'closing the chat is the close of the last window');
  assert.ok(!D.closedLast(s.layout, min), 'minimizing is not a close');
  const opened = D.openFromDesktop(min, { panel: 'settings' }, 'desktop');
  assert.equal(D.desktopCover(opened.layout), 'full');
  const fl = M.apply(opened, { op: 'float', id: S.overlays(opened.layout)[0].id }, S.WINDOW_CTX).state;
  assert.equal(D.desktopCover(fl.layout), 'partial');
});

test('from the desktop: a closed chat opens again under its preset id, a minimized one comes back, show-desktop is one undo', () => {
  const s = czat();
  const closed = closeOwn(s, chatId(s));
  const back = D.openFromDesktop(closed, { chat: true }, 'phone');
  assert.equal(chatId(back), 'w_chat');
  assert.equal(D.desktopCover(back.layout), 'full');
  assert.equal(back.past.length, closed.past.length + 1);
  const shown = D.showDesktop(s);
  assert.equal(D.desktopCover(shown.layout), 'none');
  assert.deepEqual(D.minimizedWindows(shown.layout).map(w => w.id), ['w_chat']);
  assert.equal(D.desktopCover(M.undo(shown).state.layout), 'full');
  const restored = D.restoreFromDesktop(shown, 'w_chat');
  assert.equal(D.desktopCover(restored.layout), 'full');
  assert.equal(restored.past.length, shown.past.length + 1);
});

test('paths: the emergency path appears only when the sections leave none, and only while it is on', () => {
  const s = closeOwn(czat(), 'w_chat');
  const e = D.kindEntries(known, 'czat', 'pl');
  assert.ok(D.desktopPaths(s.layout, D.desktopSettings({}), e, ['czat', 'siatka']).every(p => !p.emergency));
  assert.deepEqual(D.desktopPaths(s.layout, D.desktopSettings({ desktop_sections: [] }), e, []), [{ section: 'emergency', kind: 'chat', emergency: true }]);
  assert.deepEqual(D.desktopPaths(s.layout, D.desktopSettings({ desktop_sections: [], desktop_emergency_path: false }), e, []), []);
});

// ---------- generative: P1 on reachable states ----------
let seed = Number(process.env.LOOM_DESK_SEED ?? 101);
const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
const pick = a => a[Math.floor(rnd() * a.length)];
const SECTIONS = [...D.DESKTOP.sections, 'nonsense'];
function randomSettings() {
  const cfg = {};
  if (rnd() < 0.7) cfg.desktop_sections = SECTIONS.filter(() => rnd() < 0.4);
  if (rnd() < 0.4) cfg.desktop_emergency_path = rnd() < 0.5;
  return cfg;
}
function randomStep(s) {
  const ws = s.layout.windows.filter(w => !w.collapsed);
  const w = ws.length ? pick(ws) : null;
  switch (Math.floor(rnd() * 9)) {
    case 0: return O.openWindow(s, { panel: pick(PANELS) }, pick(['', 'float', 'beside']), pick(['phone', 'desktop'])).state;
    case 1: return w ? closeOwn(s, w.id) : s;
    case 2: return w ? M.apply(s, { op: 'minimize', id: w.id }, S.WINDOW_CTX).state : s;
    case 3: return w ? M.apply(s, { op: 'float', id: w.id }, S.WINDOW_CTX).state : s;
    case 4: return D.showDesktop(s);
    case 5: return w ? M.apply(s, { op: 'restore', id: w.id }, S.WINDOW_CTX).state : s;
    case 6: return M.undo(s).state;
    case 7: return S.switchLayout(s, sv.LAYOUTS[pick(Object.keys(sv.LAYOUTS))], sv.JOURNAL_LIMIT);
    default: return D.openFromDesktop(s, pick([{ chat: true }, { panel: pick(PANELS) }]), 'phone');
  }
}
function follow(s, p) {
  if (p.window) return D.restoreFromDesktop(s, p.window);
  if (p.layout) return S.switchLayout(s, sv.LAYOUTS[p.layout], sv.JOURNAL_LIMIT);
  const e = D.kindEntries(known, s.layout.id, 'pl').find(x => x.kind === p.kind);
  return D.openFromDesktop(s, e.target, 'phone');
}

test('P1 generative: with only the desktop seen, a way to a window exists unless switched off, and every way shows a window', () => {
  const runs = Number(process.env.LOOM_DESK_RUNS ?? 60), ops = Number(process.env.LOOM_DESK_OPS ?? 25);
  let checked = 0, waived = 0, followed = 0;
  for (let r = 0; r < runs; r++) {
    let s = rnd() < 0.5 ? czat() : S.layoutState(sv.LAYOUTS.siatka, sv.JOURNAL_LIMIT);
    for (let i = 0; i < ops; i++) {
      s = randomStep(s);
      if (D.desktopCover(s.layout) !== 'none') continue;
      const settings = D.desktopSettings(randomSettings());
      const paths = D.desktopPaths(s.layout, settings, D.kindEntries(known, s.layout.id, 'pl'), Object.keys(sv.LAYOUTS));
      checked++;
      if (!paths.length) { assert.equal(settings.emergency, false, `no path while the emergency path is on (run ${r} op ${i})`); waived++; continue; }
      for (const p of paths.filter(() => rnd() < 0.3).concat([paths[0]])) {
        const after = follow(s, p);
        assert.notEqual(D.desktopCover(after.layout), 'none', `the path ${JSON.stringify(p)} showed no window (run ${r} op ${i})`);
        followed++;
      }
    }
  }
  assert.ok(checked > 20, `too few desktop-only states reached: ${checked}`);
  console.log(`[windows-desktop] P1: ${checked} desktop-only states, ${followed} paths followed, ${waived} waived (emergency off, no section path)`);
});

test('desktop words honor arbitrary language catalogs and substitute parameters after selecting the language', () => {
  const words = { open: { en: 'Open {name}', nl: 'Openen {name}', xx: 'X {name}' } };
  assert.equal(D.say(words, 'open', 'nl', { name: 'A' }), 'Openen A');
  assert.equal(D.say(words, 'open', 'xx', { name: 'B' }), 'X B');
  assert.equal(D.say(words, 'open', 'missing', { name: 'C' }), 'Open C');
});
