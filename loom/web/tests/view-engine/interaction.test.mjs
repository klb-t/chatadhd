// P28 step 2: the pure interpretation of interaction bindings (no browser, no kernel): matching with held modifiers and lock states, the lock model with
// its documented fallback, the hint, and the reducer that applies an action to the view state.
import assert from 'node:assert/strict';
import { loadModule } from '../graph-perspectives/test-loader.mjs';
const I = await loadModule('src/view-engine/interaction.ts');
let count = 0;
async function check(name, fn) { await fn(); count++; console.log(`PASS ${name}`); }

const b = (id, input, action, extra = {}) => ({ id, label: { pl: id, en: id }, device: 'keyboard', input, action, key: id, origin: 'rom', provenance: [], status: 'active', reasons: [], ...extra });
const arrows = (prefix, mods, make) => ['Left', 'Right', 'Up', 'Down'].map(d => b(`${prefix}.${d}`, { key: `Arrow${d}`, modifiers: mods }, make(d)));
const bindings = [
  ...arrows('plane', [], d => ({ kind: 'move', pair: 'plane', axis: d === 'Left' || d === 'Right' ? 'horizontal' : 'vertical', direction: d === 'Left' || d === 'Up' ? -1 : 1, step: 1 })),
  ...arrows('dt', ['shift'], d => ({ kind: 'move', pair: 'depth_time', axis: d === 'Left' || d === 'Right' ? 'horizontal' : 'vertical', direction: d === 'Left' || d === 'Up' ? -1 : 1, step: 1 })),
  b('zoom.in', { key: '+', modifiers: [] }, { kind: 'zoom', factor: 2 }),
  b('lock.caps', { lock: 'caps', state: 'on' }, { kind: 'switch_view', declaration: 'age_lanes' }),
  b('xr.gaze', { gaze: 'look' }, { kind: 'change_focus', target: 'gazed' }, { device: 'xr', status: 'recorded' }),
];
const actions = [{ id: 'move', label: { pl: 'Przesuń', en: 'Move' }, implemented: true, pairs: { plane: { label: { pl: 'płaszczyzna', en: 'plane' }, axes: {} }, depth_time: { label: { pl: 'głębia i czas', en: 'depth and time' }, axes: {} } } }];
const mods = (o = {}) => ({ ...I.noMods(), ...o });

await check('held modifiers choose the binding of an arrow; a printable key ignores the shift it carries', async () => {
  assert.equal(I.matchKey(bindings, 'ArrowLeft', mods(), I.noLocks()).id, 'plane.Left');
  assert.equal(I.matchKey(bindings, 'ArrowLeft', mods({ shift: true }), I.noLocks()).id, 'dt.Left');
  assert.equal(I.matchKey(bindings, 'ArrowLeft', mods({ ctrl: true }), I.noLocks()), null);
  assert.equal(I.matchKey(bindings, '+', mods({ shift: true }), I.noLocks()).id, 'zoom.in');
  assert.equal(I.matchKey(bindings, 'ArrowUp', mods(), I.noLocks(), ['touch']), null);
});

await check('the last of several matching bindings wins (a conflict is warned by the kernel, never blocked)', async () => {
  const both = [...bindings, b('mine.left', { key: 'ArrowLeft', modifiers: [] }, { kind: 'zoom', factor: 3 })];
  assert.equal(I.matchKey(both, 'ArrowLeft', mods(), I.noLocks()).id, 'mine.left');
});

await check('locks that are on switch the view or scale the zoom, as the (rebound) binding says', async () => {
  assert.deepEqual([I.lockEffects(bindings, I.noLocks()).view, I.lockEffects(bindings, I.noLocks()).zoom], [null, 1]);
  assert.equal(I.lockEffects(bindings, { ...I.noLocks(), caps: true }).view, 'age_lanes');
  const rebound = bindings.map(x => x.id === 'lock.caps' ? { ...x, action: { kind: 'zoom', factor: 1.5 } } : x);
  const e = I.lockEffects(rebound, { ...I.noLocks(), caps: true });
  assert.equal(e.view, null);
  assert.equal(e.zoom, 1.5);
});

await check('lock model: a reading of on is trusted; a press flips an unreadable lock; without getModifierState everything is counted', async () => {
  let m = I.initialLockModel();
  const key = (k, type = 'keydown', read = () => false) => ({ key: k, type, repeat: false, read });
  m = I.updateLocks(m, key('ScrollLock'));
  assert.equal(m.state.scroll, true, 'unreadable: a press flips');
  assert.equal(m.read.scroll, false);
  m = I.updateLocks(m, key('ScrollLock'));
  assert.equal(m.state.scroll, false);
  m = I.updateLocks(m, key('a', 'keydown', n => n === 'CapsLock'));
  assert.equal(m.state.caps, true, 'a reading of on is trusted');
  assert.equal(m.read.caps, true);
  m = I.updateLocks(m, key('a'));
  assert.equal(m.state.caps, false, 'a readable lock follows its reading');
  m = I.updateLocks(m, key('NumLock', 'keydown', null));
  assert.equal(m.api, false);
  assert.equal(m.state.num, true);
  assert.deepEqual(I.unreadLocks(I.initialLockModel()), ['caps', 'num', 'scroll']);
});

await check('the hint says what the arrows do now, what each modifier would change and what the locks do', async () => {
  const plain = I.buildHint(bindings, actions, mods(), I.noLocks(), 'pl');
  assert.equal(plain.now, 'płaszczyzna');
  assert.equal(plain.arrows.length, 4);
  assert.deepEqual(plain.held, [{ mod: 'shift', what: 'głębia i czas' }]);
  assert.equal(plain.locks[0].view, 'age_lanes');
  assert.equal(I.buildHint(bindings, actions, mods({ shift: true }), I.noLocks(), 'pl').now, 'głębia i czas');
  assert.equal(I.buildHint(bindings, actions, mods(), I.noLocks(), 'en').now, 'plane');
  assert.equal(I.buildHint([], actions, mods(), I.noLocks(), 'pl').now, null);
});

await check('applyAction: pan, depth, zoom, rotate, labels, and the requests it leaves to the panel', async () => {
  let s = I.initialViewState();
  s = I.applyAction(s, { kind: 'move', pair: 'plane', axis: 'horizontal', direction: -1, step: 1 }).state;
  assert.equal(s.panX, -I.PAN_STEP);
  s = I.applyAction(s, { kind: 'move', pair: 'depth_time', axis: 'vertical', direction: 1, step: 1 }).state;
  assert.equal(s.depth, 1);
  assert.equal(I.applyAction(s, { kind: 'zoom', factor: 2 }).state.zoom, 2);
  assert.equal(I.applyAction(s, { kind: 'rotate', target: 'direction', direction: 1, step: 15 }).state.angle, 15);
  assert.equal(I.applyAction(s, { kind: 'toggle_element', element: 'labels' }).state.labels, true);
  assert.deepEqual(I.applyAction(s, { kind: 'move', pair: 'depth_time', axis: 'horizontal', direction: -1, step: 1 }).request, { type: 'focus', target: 'time', direction: -1 });
  assert.deepEqual(I.applyAction(s, { kind: 'switch_view', declaration: 'age_lanes' }).request, { type: 'switch_view', declaration: 'age_lanes' });
  assert.equal(I.applyAction(s, { kind: 'teleport' }).request.type, 'unsupported');
  assert.equal(I.neighbour([{ id: 'a', value: 1 }, { id: 'b', value: 2 }, { id: 'c', value: null }], 'a', 1), 'b');
  assert.equal(I.neighbour([{ id: 'a', value: 1 }], 'a', 1), null);
});

console.log(`${count} checks`);
