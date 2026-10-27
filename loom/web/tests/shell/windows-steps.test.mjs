// W7: the arrangement steps of a sentence (steps.ts: window.maximize / restore / minimize / dock / float / tab / move / resize), the keys of
// keymap.json, the chain cycle of gestures.json, the focus order of §6.4 and the parameter words of a binding sentence. Examples first, then a
// generative pass (seeded; LOOM_WIN_RUNS / LOOM_WIN_OPS / LOOM_WIN_SEED raise the presets): every step keeps the model's check empty and is
// at most ONE journal entry, so one undo takes it back.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const M = await loader.load('src/shell/windows/model.ts');
const S = await loader.load('src/shell/windows/session.ts');
const O = await loader.load('src/shell/windows/ops.ts');
const T = await loader.load('src/shell/windows/steps.ts');
const C = await loader.load('src/shell/windows/coupling.ts');
const sv = await loader.load('src/shell/start-view.ts');
const PANELS = ['graph', 'navigator', 'settings', 'memory', 'import', 'logs', 'context'];
const known = id => PANELS.includes(id);
const czat = () => S.layoutState(sv.LAYOUTS.czat, sv.JOURNAL_LIMIT);
const area = { w: 1280, h: 800 };
const run = (s, action, args) => T.runArrangeStep(s, action, args, known, 'desktop', area);
const win = (s, id) => s.layout.windows.find(w => w.id === id);

test('„zmaksymalizuj ustawienia”: settings not open yet are opened and maximized - one journal entry', () => {
  const s = czat();
  const r = run(s, 'window.maximize', { target: 'ustawienia' });
  assert.equal(r.receipt.state, 'ok');
  assert.equal(win(r.state, r.receipt.windows[0]).params.panel, 'settings');
  assert.equal(win(r.state, r.receipt.windows[0]).mode, 'maximized');
  assert.equal(r.state.past.length, s.past.length + 1);
  assert.deepEqual(M.check(r.state.layout), []);
  assert.equal(M.undo(r.state).state.layout, s.layout, 'one undo takes it back');
});

test('„zadokuj graf po lewej”: the graph docked on the left of the chat; an unknown side is answered, not guessed', () => {
  const r = run(czat(), 'window.dock', { target: 'graf', side: 'lewej' });
  assert.equal(r.receipt.state, 'ok');
  assert.equal(r.receipt.how, 'left');
  const g = win(r.state, r.receipt.windows[0]);
  assert.equal(g.mode, 'docked');
  assert.ok(g.dock?.leaf);
  assert.deepEqual(M.check(r.state.layout), []);
  const bad = run(czat(), 'window.dock', { target: 'graf', side: 'ukosem' });
  assert.equal(bad.receipt.state, 'none');
  assert.equal(bad.receipt.word, 'ukosem');
  const nowin = run(czat(), 'window.dock', { target: 'kalafior', side: 'lewej' });
  assert.equal(nowin.receipt.state, 'none');
});

test('float, move by a step or a number, resize to px or by a word; minimize and restore', () => {
  let s = run(czat(), 'window.float', { target: 'graf' }).state;
  const id = s.layout.windows.find(w => w.params?.panel === 'navigator').id;
  assert.equal(win(s, id).mode, 'floating');
  const x0 = win(s, id).rect.x;
  s = run(s, 'window.move', { target: 'graf', direction: 'prawo', amount: '128' }).state;
  assert.ok(Math.abs(win(s, id).rect.x - (x0 + 128 / area.w)) < 1e-9);
  s = run(s, 'window.move', { target: 'graf', direction: 'lewo' }).state;   // preset step
  assert.ok(Math.abs(win(s, id).rect.x - (x0 + (128 - 80) / area.w)) < 1e-9);
  s = run(s, 'window.resize', { target: 'graf', w: '640', h: '400' }).state;
  assert.ok(Math.abs(win(s, id).rect.w - 0.5) < 1e-9 && Math.abs(win(s, id).rect.h - 0.5) < 1e-9);
  s = run(s, 'window.resize', { target: 'graf', how: 'powiększ' }).state;
  assert.ok(Math.abs(win(s, id).rect.w - 0.625) < 1e-9);
  assert.equal(run(s, 'window.resize', { target: 'graf', how: 'rozdmuchaj' }).receipt.state, 'none');
  s = run(s, 'window.minimize', { target: 'graf' }).state;
  assert.equal(win(s, id).mode, 'minimized');
  s = run(s, 'window.restore', { target: 'graf' }).state;
  assert.notEqual(win(s, id).mode, 'minimized');
  assert.deepEqual(M.check(s.layout), []);
});

test('keymap.json: combinations, the legacy keymap of layouts.json still read, text fields keep their keys by preset', () => {
  assert.equal(T.comboOf({ ctrlKey: true, shiftKey: true, key: 'M' }), 'ctrl+shift+m');
  assert.equal(T.keyOperation('ctrl+shift+m'), 'maximize');
  assert.equal(T.keyOperation('ctrl+shift+arrowleft'), 'left');
  assert.equal(T.keyOperation('ctrl+alt+arrowright'), 'next');
  assert.equal(T.keyOperation('ctrl+q', { keys: {} }, { 'ctrl+q': 'float' }), 'float');
  assert.equal(T.keyOperation('ctrl+shift+q'), null);
  assert.equal(T.KEYMAP.text_fields_keep_keys, true);
});

test('gestures.json: the chain cycle walks shared → independent → linked, skipping linked without a source; any order works', () => {
  assert.equal(T.nextChainMode('independent', true), 'linked');
  assert.equal(T.nextChainMode('independent', false), 'shared');
  assert.equal(T.nextChainMode('linked', true), 'shared');
  assert.equal(T.nextChainMode('shared', true, ['shared', 'linked']), 'linked');
  assert.equal(T.nextChainMode('x', true, []), 'x');
});

test('§6.4 focus order: dock tree in reading order, then floating by z; another order from data', () => {
  let s = run(czat(), 'window.dock', { target: 'graf', side: 'prawej' }).state;
  s = run(s, 'window.float', { target: 'ustawienia' }).state;
  const order = T.focusOrder(s.layout);
  const chat = s.layout.windows.find(w => w.kind === 'chat').id;
  const graph = s.layout.windows.find(w => w.params?.panel === 'navigator').id;
  const settings = s.layout.windows.find(w => w.params?.panel === 'settings').id;
  assert.ok(order.indexOf(chat) < order.indexOf(graph) && order.indexOf(graph) < order.indexOf(settings), order.join(','));
  assert.equal(T.focusOrder(s.layout, ['floating', 'dock'])[0], settings);
});

test('words of a binding sentence: „przybliżenie”, „zoom”, „przesunięcia” name parameters; other words none', () => {
  assert.equal(C.paramOfWord('przybliżenie'), 'zoom');
  assert.equal(C.paramOfWord('zoom'), 'zoom');
  assert.equal(C.paramOfWord('przesunięcia'), 'pan');
  assert.equal(C.paramOfWord('the zoom'.replace('the ', '')), 'zoom');
  assert.equal(C.paramOfWord('kalafior'), null);
  assert.equal(C.paramOfWord(''), null);
});

test('generative: random arrangement steps keep the check empty and add at most one journal entry each', () => {
  const runs = Number(process.env.LOOM_WIN_RUNS ?? 40), ops = Number(process.env.LOOM_WIN_OPS ?? 25);
  let seed = Number(process.env.LOOM_WIN_SEED ?? 7);
  const rnd = n => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed % n; };
  const actions = ['maximize', 'restore', 'minimize', 'dock', 'float', 'tab', 'move', 'resize'];
  const words = ['graf', 'ustawienia', 'pamięć', 'czat', '', 'kalafior'];
  const sides = ['lewej', 'prawej', 'górze', 'dole', 'ukosem'];
  let steps = 0;
  for (let r = 0; r < runs; r++) {
    let s = czat();
    for (let i = 0; i < ops; i++) {
      const a = actions[rnd(actions.length)];
      const args = { target: words[rnd(words.length)], side: sides[rnd(sides.length)], direction: ['lewo', 'prawo', 'gore', 'dol'][rnd(4)], amount: String(rnd(300)), w: String(rnd(900)), h: String(rnd(700)) };
      const before = s.past.length;
      const out = run(s, `window.${a}`, args);
      assert.deepEqual(M.check(out.state.layout), [], `${a} ${JSON.stringify(args)}`);
      assert.ok(out.state.past.length <= Math.min(before + 1, s.limit), `${a}: one entry at most`);
      if (out.receipt.state === 'none') assert.equal(out.state, s, `${a}: a step that names nothing changes nothing`);
      s = out.state; steps++;
    }
  }
  assert.ok(steps >= 1000);
});
