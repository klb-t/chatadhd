// P28 step 3: XR input as data - matching events against the xr bindings, controller buttons as held modifiers, the thumbstick, picking by ray, large gestures,
// the XR hint, WebXR feature detection. Pure: no browser, no headset, no kernel.
import assert from 'node:assert/strict';
import { loadModule } from '../graph-perspectives/test-loader.mjs';
const X = await loadModule('src/view-engine/xr-input.ts');
const SUP = await loadModule('src/view-engine/xr-support.ts');
const HINT = await loadModule('src/view-engine/scene-hint.ts');
const I = await loadModule('src/view-engine/interaction.ts');
const { matchXr, heldFromButtons, isModifierButton, stickGesture, pickByRay, SwipeDetector, TwoHandRotateDetector, buildXrHint, readGestureSettings } = X;
let count = 0;
async function check(name, fn) { await fn(); count++; console.log(`PASS ${name}`); }
const b = (id, input, action, extra = {}) => ({ id, label: { en: id, pl: id }, device: 'xr', input, action, key: id, origin: 'rom', provenance: [], status: 'active', reasons: [], ...extra });
const none = I.noMods();
const bindings = [
  b('gaze', { gaze: 'look', gesture: 'pinch' }, { kind: 'change_focus', target: 'gazed' }),
  b('trigger', { button: 'right_trigger' }, { kind: 'change_focus', target: 'gazed' }),
  b('shift', { button: 'left_trigger' }, { kind: 'hold_modifier', modifier: 'shift' }),
  b('ctrl', { button: 'left_squeeze' }, { kind: 'hold_modifier', modifier: 'ctrl' }),
  b('plane.left', { gesture: 'stick_left', modifiers: [] }, { kind: 'move', pair: 'plane', axis: 'horizontal', direction: -1, step: 1 }),
  b('time.left', { gesture: 'stick_left', modifiers: ['shift'] }, { kind: 'move', pair: 'depth_time', axis: 'horizontal', direction: -1, step: 1 }),
  b('coarse.left', { gesture: 'large_swipe_left' }, { kind: 'move', pair: 'plane', axis: 'horizontal', direction: -1, step: 5, coarse: true }),
  b('rotate', { gesture: 'two_hand_rotate' }, { kind: 'rotate', target: 'perspective', direction: 1, step: 1 }),
  b('voice', { phrase: 'przybliż' }, { kind: 'zoom', factor: 1.25 }, { status: 'recorded' }),
  b('voice.active', { phrase: 'oddal' }, { kind: 'zoom', factor: 0.8 }),
  b('kbd', { key: 'ArrowLeft' }, { kind: 'move' }, { device: 'keyboard' }),
];

await check('pinch needs the gaze; a trigger button is its own binding; unknown events match nothing', async () => {
  assert.equal(matchXr(bindings, { gesture: 'pinch', gaze: true }, none).id, 'gaze');
  assert.equal(matchXr(bindings, { gesture: 'pinch' }, none), null, 'a pinch without the gaze ray is not the look-select');
  assert.equal(matchXr(bindings, { button: 'right_trigger' }, none).id, 'trigger');
  assert.equal(matchXr(bindings, { button: 'right_trigger' }, { ...none, shift: true }).id, 'trigger', 'a binding without modifiers works with any held');
  assert.equal(matchXr(bindings, { button: 'nothing' }, none), null);
  assert.equal(matchXr(bindings, {}, none), null);
});

await check('held controller buttons are modifiers: the same thumbstick direction means something else while a modifier is held', async () => {
  assert.equal(matchXr(bindings, { gesture: 'stick_left' }, none).id, 'plane.left');
  const held = heldFromButtons(bindings, ['left_trigger']);
  assert.deepEqual([held.shift, held.ctrl, held.alt, held.meta], [true, false, false, false]);
  assert.equal(matchXr(bindings, { gesture: 'stick_left' }, held).id, 'time.left');
  assert.equal(matchXr(bindings, { gesture: 'stick_left' }, heldFromButtons(bindings, ['left_squeeze'])), null, 'no binding for ctrl+stick here: nothing, not a guess');
  assert.equal(heldFromButtons(bindings, ['right_trigger']).shift, false);
  assert.ok(isModifierButton(bindings, 'left_trigger') && !isModifierButton(bindings, 'right_trigger'));
});

await check('large gestures are bound for coarse moves and match regardless of the held modifiers; the last binding wins, nothing is blocked', async () => {
  assert.equal(matchXr(bindings, { gesture: 'large_swipe_left' }, none).id, 'coarse.left');
  assert.equal(matchXr(bindings, { gesture: 'large_swipe_left' }, { ...none, alt: true }).id, 'coarse.left');
  assert.equal(matchXr(bindings, { gesture: 'two_hand_rotate' }, none).id, 'rotate');
  const dup = [...bindings, b('later', { button: 'right_trigger' }, { kind: 'change_focus', target: 'origin' })];
  assert.equal(matchXr(dup, { button: 'right_trigger' }, none).id, 'later');
});

await check('voice stays recorded: a phrase binding is never matched, active or not; recorded and keyboard bindings are ignored', async () => {
  assert.equal(matchXr(bindings, { gesture: 'przybliż' }, none), null);
  assert.equal(matchXr(bindings, { button: 'przybliż' }, none), null);
  assert.equal(matchXr([bindings.find(x => x.id === 'voice.active')], { gesture: 'oddal', gaze: true }, none), null);
  assert.equal(matchXr(bindings.filter(x => x.id === 'voice' || x.id === 'kbd'), { gesture: 'stick_left' }, none), null);
  const recorded = bindings.map(x => (x.id === 'trigger' ? { ...x, status: 'recorded' } : x));
  assert.equal(matchXr(recorded, { button: 'right_trigger' }, none), null);
});

await check('the thumbstick is an arrow gesture past the threshold, on the dominant axis (up is negative)', async () => {
  assert.equal(stickGesture(0.1, 0.2, 0.7), null);
  assert.equal(stickGesture(-0.9, 0.1, 0.7), 'stick_left'); assert.equal(stickGesture(0.9, 0.1, 0.7), 'stick_right');
  assert.equal(stickGesture(0.1, -0.9, 0.7), 'stick_up'); assert.equal(stickGesture(0.1, 0.9, 0.7), 'stick_down');
  assert.equal(stickGesture(0.8, -0.75, 0.7), 'stick_right');
});

await check('picking by ray: inside a sphere or within the angular tolerance; the smallest angle wins, then the nearest; behind the user never', async () => {
  const t = [{ id: 'a', x: 0, y: 0, z: -3, radius: 0.2 }, { id: 'b', x: 0.3, y: 0, z: -3, radius: 0.1 }, { id: 'back', x: 0, y: 0, z: 3, radius: 1 }, { id: 'near', x: 0, y: 0, z: -1, radius: 0.1 }];
  assert.equal(pickByRay([0, 0, 0], [0, 0, -1], t, 4), 'near', 'two on one ray: the nearest');
  assert.equal(pickByRay([0, 0, 0], [0, 0, -1], t.filter(x => x.id !== 'near'), 4), 'a');
  assert.equal(pickByRay([0, 0, 0], [0.1, 0, -1], t.filter(x => x.id === 'b'), 1), 'b', 'inside the sphere radius even with a tight tolerance');
  assert.equal(pickByRay([0, 0, 0], [1, 0, 0], t, 4), null);
  assert.equal(pickByRay([0, 0, 0], [0, 0, -1], t.filter(x => x.id === 'back'), 4), null);
  assert.equal(pickByRay([0, 0, 0], [0.06, 0, -1], t.filter(x => x.id === 'a'), 4), 'a', 'tolerance ~4 degrees');
  assert.equal(pickByRay([0, 0, 0], [0.2, 0, -1], t.filter(x => x.id === 'a'), 1), null);
});

await check('a large swipe is far, quick and straight; small moves, slow moves and diagonals are not; a cooldown prevents repeats', async () => {
  const d = new SwipeDetector({ minDistanceM: 0.4, maxMs: 700, dominance: 1.6, cooldownMs: 800 });
  assert.equal(d.push(0, [0, 0, 0]), null);
  assert.equal(d.push(100, [-0.1, 0, 0]), null, 'small');
  assert.equal(d.push(300, [-0.45, 0.02, 0]), 'large_swipe_left');
  assert.equal(d.push(400, [-0.9, 0, 0]), null, 'cooldown');
  const s = new SwipeDetector({ minDistanceM: 0.4, maxMs: 700, dominance: 1.6, cooldownMs: 100 });
  for (let t = 0, x = 0; t <= 3000; t += 100, x += 0.02) assert.equal(s.push(t, [x, 0, 0]), null, 'slow drift is not a gesture');
  const u = new SwipeDetector({ minDistanceM: 0.4, maxMs: 700, dominance: 1.6, cooldownMs: 100 });
  u.push(0, [0, 0, 0]); assert.equal(u.push(200, [0.1, 0.5, 0]), 'large_swipe_up');
  const dg = new SwipeDetector({ minDistanceM: 0.4, maxMs: 700, dominance: 1.6, cooldownMs: 100 });
  dg.push(0, [0, 0, 0]); assert.equal(dg.push(200, [0.5, 0.45, 0]), null, 'a diagonal is not a left/right/up/down swipe');
  const dn = new SwipeDetector(); dn.push(0, [0, 0, 0]); assert.equal(dn.push(200, [0, -0.5, 0]), 'large_swipe_down');
});

await check('two hands turning around each other, both moving, past the angle is two_hand_rotate with a direction; one hand moving past a resting one, small turns and slow drift are not', async () => {
  const cfg = { minAngleDeg: 25, cooldownMs: 500, minHandMoveM: 0.1, maxMs: 1000 };
  const at = deg => { const a = (deg * Math.PI) / 180; return [[-Math.cos(a) * 0.3, 0, -Math.sin(a) * 0.3], [Math.cos(a) * 0.3, 0, Math.sin(a) * 0.3]]; };
  const r = new TwoHandRotateDetector(cfg);
  assert.equal(r.push(0, ...at(0)), null);
  assert.equal(r.push(100, ...at(10)), null);
  const hit = r.push(200, ...at(40));
  assert.deepEqual(hit, { gesture: 'two_hand_rotate', direction: 1 });
  assert.equal(r.push(300, ...at(90)), null, 'cooldown');
  const back = new TwoHandRotateDetector({ ...cfg, cooldownMs: 0 });
  back.push(0, ...at(0)); assert.equal(back.push(100, ...at(-40)).direction, -1);
  // one hand sweeping past a resting hand turns the vector between them, but it is a swipe, not a two-hand turn
  const one = new TwoHandRotateDetector(cfg);
  one.push(0, [0, 0, 0], [0.3, 0, 0]);
  assert.equal(one.push(150, [0, 0, 0], [-0.3, 0, 0.05]), null, 'the left hand did not move');
  // a slow turn spread over longer than the window re-bases and never fires
  const slow = new TwoHandRotateDetector(cfg);
  for (let t = 0, d = 0; t <= 6000; t += 100, d += 1) assert.equal(slow.push(t, ...at(d)), null, `slow ${d}`);
});

await check('gesture settings come from the renderer data with the presets as fallback', async () => {
  const g = readGestureSettings({ gestures: { large_swipe: { min_distance_m: 0.9 }, two_hand_rotate: { min_angle_deg: 60 } } });
  assert.equal(g.swipe.minDistanceM, 0.9); assert.equal(g.swipe.maxMs, 700); assert.equal(g.rotate.minAngleDeg, 60);
  assert.equal(readGestureSettings(undefined).swipe.minDistanceM, 0.45);
});

await check('the XR hint says what select, the held buttons, the thumbstick and the large gestures do now, and how many inputs are only recorded', async () => {
  const actions = [{ id: 'move', label: { en: 'Move', pl: 'Przesuń' }, implemented: true, pairs: { plane: { label: { en: 'plane', pl: 'płaszczyzna' }, axes: {} }, depth_time: { label: { en: 'depth and time', pl: 'głębia i czas' }, axes: {} } } }];
  const h = buildXrHint(bindings, actions, none, 'pl');
  assert.deepEqual(h.select.sort(), ['gaze', 'trigger'].sort());
  assert.deepEqual(h.held.map(x => `${x.button}=${x.mod}`).sort(), ['left_squeeze=ctrl', 'left_trigger=shift']);
  assert.equal(h.stick[0].gesture, 'stick_left');
  assert.deepEqual(h.modifiers, [{ mod: 'shift', what: 'głębia i czas' }]);
  assert.deepEqual(h.coarse.sort(), ['coarse.left', 'rotate']);
  assert.equal(h.recorded, 1);
  const held = buildXrHint(bindings, actions, { ...none, shift: true }, 'pl');
  assert.equal(held.stick[0].label, 'time.left');
  const lines = HINT.sceneHintLines({ xr: true, flat: null, immersive: h, heldNames: ['Shift'], gazedSector: 'Historia', mode: 'immersive-vr', conflicts: 1, unsupported: 2 }, (k, p) => `${k}${p ? ':' + Object.values(p).join('|') : ''}`);
  assert.ok(lines[0].startsWith('scene_hint_title'));
  for (const key of ['scene_hint_select', 'scene_hint_held', 'scene_hint_stick', 'scene_hint_coarse', 'scene_hint_recorded', 'scene_hint_gaze', 'scene_hint_conflicts', 'scene_hint_unsupported']) assert.ok(lines.some(l => l.startsWith(key)), key);
  assert.ok(lines.find(l => l.startsWith('scene_hint_held')).includes('L-trigger = Shift'));
});

await check('feature detection: no navigator.xr, unsupported sessions and a failing check are all reported - never a silent fallback', async () => {
  const none = await SUP.detectXr(['immersive-vr', 'immersive-ar'], {});
  assert.equal(none.available, false); assert.deepEqual(none.sessions, { 'immersive-vr': false, 'immersive-ar': false });
  const part = await SUP.detectXr(['immersive-vr', 'immersive-ar'], { xr: { isSessionSupported: async m => m === 'immersive-vr' } });
  assert.equal(part.available, true); assert.equal(part.sessions['immersive-vr'], true); assert.equal(part.sessions['immersive-ar'], false); assert.equal(part.error, undefined);
  const bad = await SUP.detectXr(['immersive-vr'], { xr: { isSessionSupported: async () => { throw new Error('SecurityError'); } } });
  assert.equal(bad.available, true); assert.equal(bad.sessions['immersive-vr'], false); assert.match(bad.error, /SecurityError/);
});
console.log(`${count} checks passed`);
