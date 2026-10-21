// M1-W0 (2) P4: the map half of the graph navigator (src/components/navigator-map.ts) without a browser: the view model built from
// a graph.navigate answer (the same nodes as the list, the focus in the middle, the kernel's edges, colours by kind), the projection
// through the real view engine (src/view-engine/project.ts), the camera (± steps, pinch, pan), the exclusion rectangles for the
// Android shell, the layout choice (navigator_layout) and the words the map asks for in the catalogue presentation.graph_screen.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const map = await loader.load('src/components/navigator-map.ts');
const { project } = await loader.load('src/view-engine/project.ts');
const { colorFor } = await loader.load('src/components/kind-colors.ts');
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const { pl, en } = pack.entries.find(r => r.key === 'presentation.graph_screen').value.locales;
const fields = JSON.parse(await readFile(new URL('../../../data/runtime/settings_fields.pack', import.meta.url), 'utf8'));

// A graph.navigate answer the shape of the kernel's (loom/web/src/api/graph-projection.ts): a focus with four children, one kernel edge.
const row = (address, kind, extra = {}) => ({ address, label: 'L ' + address, kind, provenance: { layer: 'rom' }, evidence: { class: 'declared', confidence: null }, has_children: false, ...extra });
const result = {
  focus: { address: 'area:rom', label: 'ROM', kind: 'area', evidence: { class: 'declared', confidence: null }, provenance: { layer: 'rom' } },
  children: [row('rom:a', 'topic', { count: 3, has_children: true }), row('rom:b', 'person', { count: 0 }), row('rom:c', 'org', { count_unknown_reason: 'no_reader' }), row('rom:d', 'entity', { depth: 2 })],
  edges: [{ src: 'rom:a', dst: 'rom:b', kind: 'mentions', label: 'wspomina', meaning: null }],
  counts: { total: 4, shown: 4, collapsed: 0, by_source: {} },
};

test('the view model has the same nodes as the list: the focus in the middle and every child row around it, nothing of its own', () => {
  const model = map.viewModelFor(result, 'Graf');
  assert.equal(model.schema, 'loom.view_model/1');
  assert.deepEqual(model.elements.map(e => e.id), ['area:rom', 'rom:a', 'rom:b', 'rom:c', 'rom:d']);
  assert.equal(model.focus, 'area:rom');
  assert.equal(model.elements[0].focus, true);
  assert.deepEqual(model.elements[0].coords.sector, { radius: 0, angle_deg: 0 });
  // children evenly around the focus by their order; a deeper row (depth 2) sits further out
  assert.deepEqual(model.elements.slice(1).map(e => e.coords.sector.angle_deg), [0, 90, 180, 270]);
  assert.deepEqual(model.elements.slice(1).map(e => e.coords.sector.radius), [1, 1, 1, 2]);
  assert.equal(model.coordinate_systems[0].system, 'sector');
  assert.equal(model.budget.elements, 5);
});

test('edges: the kernel edge is kept, a focus→child edge is added only for a first-level child the kernel gave no edge for', () => {
  const model = map.viewModelFor(result, 'Graf');
  assert.deepEqual(model.edges.map(e => [e.src, e.dst, e.type]), [
    ['rom:a', 'rom:b', 'mentions'], ['area:rom', 'rom:a', 'child'], ['area:rom', 'rom:b', 'child'], ['area:rom', 'rom:c', 'child']]);
  const model2 = map.viewModelFor({ ...result, edges: [{ src: 'area:rom', dst: 'rom:a', kind: 'has', label: '', meaning: null }, { src: 'x', dst: 'rom:a', kind: 'has', label: '', meaning: null }] }, 'Graf');
  assert.deepEqual(model2.edges.map(e => [e.src, e.dst]), [['area:rom', 'rom:a'], ['area:rom', 'rom:b'], ['area:rom', 'rom:c']], 'an edge to a node not on the level is dropped');
});

test('colours come from the kind (kind-colors.ts, the same as GraphView), size grows with the log of the count', () => {
  const model = map.viewModelFor(result, 'Graf');
  for (const e of model.elements) {
    const { hue, saturation, lightness } = map.hslOfHex(colorFor(e.kind));
    assert.deepEqual([e.channels.hue, e.channels.saturation, e.channels.lightness], [hue, saturation, lightness], e.id);
  }
  assert.deepEqual(map.hslOfHex('#ff0000'), { hue: 0, saturation: 1, lightness: 0.5 });
  assert.deepEqual(map.hslOfHex('#808080'), { hue: 0, saturation: 0, lightness: 0.502 });
  assert.equal(map.hslOfHex('nonsense').hue, 210, 'a colour that is not hex gets the preset of the engine');
  assert.equal(map.sizeOf({ count: 0 }), 14);
  assert.ok(map.sizeOf({ count: 9 }) > map.sizeOf({ count: 0 }) && map.sizeOf({ count: 999 }) > map.sizeOf({ count: 9 }));
  assert.ok(map.sizeOf({ count: 1e9 }) < 80, 'a billion still fits on the plane');
});

test('the real view engine places every element (focus at the centre, nothing unplaced) on a 342 x 300 phone map', () => {
  const model = map.viewModelFor(result, 'Graf');
  const projection = project(model, { width: 342, height: 300, margin: 26 });
  assert.equal(projection.system, 'sector');
  assert.equal(projection.unplaced.length, 0);
  assert.equal(projection.marks.length, 5);
  const focus = projection.marks.find(m => m.focus);
  assert.deepEqual([focus.x, focus.y], [171, 150]);
  for (const m of projection.marks) assert.ok(m.x >= 0 && m.x <= 342 && m.y >= 0 && m.y <= 300, `${m.id} inside the map`);
  assert.equal(projection.edges.length, 4);
  const root = map.viewModelFor({ ...result, focus: { ...result.focus, address: '', label: '' } }, 'Graf');
  assert.equal(root.elements[0].id, map.ROOT_ELEMENT_ID);
  assert.equal(root.elements[0].label, 'Graf', 'the root takes the catalogue word');
});

test('camera: + / − step around the centre and stay inside the presets; a pinch zooms by the finger distance and follows the midpoint; a pan shifts', () => {
  const centre = { x: 171, y: 150 };
  const inOnce = map.zoomBy(map.CAMERA_HOME, map.ZOOM_STEP_PRESET, centre);
  assert.deepEqual(inOnce, { zoom: 1.25, panX: 0, panY: 0 });
  const p = { x: 100, y: 100 };
  const s0 = map.screenOf(p, map.CAMERA_HOME, centre), s1 = map.screenOf(p, inOnce, centre);
  assert.deepEqual(s0, p);
  assert.ok(s1.x < p.x && s1.y < p.y, 'a point left/above the centre moves further out when zooming in');
  let cam = map.CAMERA_HOME;
  for (let i = 0; i < 40; i++) cam = map.zoomBy(cam, map.ZOOM_STEP_PRESET, centre);
  assert.equal(cam.zoom, map.ZOOM_MAX_PRESET);
  for (let i = 0; i < 80; i++) cam = map.zoomBy(cam, 1 / map.ZOOM_STEP_PRESET, centre);
  assert.equal(cam.zoom, map.ZOOM_MIN_PRESET);
  // zoom around a point keeps that point where it is
  const around = { x: 300, y: 50 };
  const z = map.zoomBy(map.CAMERA_HOME, 2, centre, around);
  const before = map.screenOf(around, map.CAMERA_HOME, centre), after = map.screenOf(around, z, centre);
  assert.ok(Math.abs(before.x - after.x) < 1e-9 && Math.abs(before.y - after.y) < 1e-9);
  // pinch: fingers 100 px apart to 200 px apart doubles; the midpoint moves 10 px right, the plane follows
  const pinched = map.pinch(map.CAMERA_HOME, { x: 100, y: 150 }, { x: 200, y: 150 }, { x: 60, y: 150 }, { x: 260, y: 150 }, centre);
  assert.equal(pinched.zoom, 2);
  const mid0 = map.screenOf({ x: 150, y: 150 }, map.CAMERA_HOME, centre), mid1 = map.screenOf({ x: 150, y: 150 }, pinched, centre);
  assert.deepEqual([mid0.x, mid1.x], [150, 160]);
  assert.deepEqual(map.panBy(map.CAMERA_HOME, 5, -7), { zoom: 1, panX: 5, panY: -7 });
  const same = map.pinch(map.CAMERA_HOME, { x: 1, y: 1 }, { x: 1, y: 1 }, { x: 1, y: 1 }, { x: 1, y: 1 }, centre);
  assert.equal(same.zoom, 1, 'two fingers at one point do not divide by zero');
});

test('exclusion rectangles: the visible part of the open map in whole CSS px; nothing when folded, off screen or without size', () => {
  const viewport = { width: 390, height: 844 };
  assert.deepEqual(map.exclusionRects({ left: 24.4, top: 300.6, width: 341.2, height: 300 }, true, viewport), [{ x: 24, y: 301, w: 341, h: 300 }]);
  assert.deepEqual(map.exclusionRects({ left: 24, top: 700, width: 342, height: 300 }, true, viewport), [{ x: 24, y: 700, w: 342, h: 144 }], 'clipped to the screen');
  assert.deepEqual(map.exclusionRects({ left: 24, top: -100, width: 342, height: 300 }, true, viewport), [{ x: 24, y: 0, w: 342, h: 200 }]);
  assert.deepEqual(map.exclusionRects({ left: 24, top: 900, width: 342, height: 300 }, true, viewport), [], 'scrolled below the screen');
  assert.deepEqual(map.exclusionRects({ left: 24, top: 300, width: 342, height: 300 }, false, viewport), [], 'folded');
  assert.deepEqual(map.exclusionRects(null, true, viewport), []);
  assert.deepEqual(map.exclusionRects({ left: 24, top: 300, width: 0, height: 0 }, true, viewport), []);
  assert.ok(map.sameRects([{ x: 1, y: 2, w: 3, h: 4 }], [{ x: 1, y: 2, w: 3, h: 4 }]) && !map.sameRects([], [{ x: 1, y: 2, w: 3, h: 4 }]));
});

test('navigator_layout: the three values of the field decide which disclosure starts open; anything else is the preset both', () => {
  const field = (function find(o) { if (o && typeof o === 'object') { if (o.key === 'navigator_layout' && o.values) return o; for (const v of Object.values(o)) { const r = find(v); if (r) return r; } } return null; })(fields);
  assert.deepEqual(field.values.map(v => v.value), [...map.LAYOUTS], 'the layouts here are the values of the field');
  assert.equal(field.status.state !== 'planned', true, 'the field says it is read now (this screen reads it)');
  assert.deepEqual(map.openByLayout('both'), { list: true, map: true });
  assert.deepEqual(map.openByLayout('list'), { list: true, map: false });
  assert.deepEqual(map.openByLayout('map'), { list: false, map: true });
  for (const v of [undefined, null, 7, 'grid', '']) assert.equal(map.layoutOf(v), 'both');
  assert.equal(map.layoutOf('map'), 'map');
});

test('every word the map and the view menu ask for exists in both languages (a missing one would show its id on screen)', () => {
  const ids = ['list.show', 'list.hide', 'map.show', 'map.hide', 'map.aria', 'map.zoom_in', 'map.zoom_out', 'map.home', 'map.hint', 'map.enter', 'map.unplaced',
    'view.title', 'view.saved', 'view.save_error', ...map.LAYOUTS.map(l => `view.${l}`)];
  for (const id of ids) { assert.equal(typeof pl[id], 'string', `pl ${id}`); assert.equal(typeof en[id], 'string', `en ${id}`); }
  assert.equal(pl['bar.planned'], undefined, 'no bar button says planned any more (r1:b2: Perspektywa ▾ and Działania work)');
  for (const l of [pl, en]) assert.ok(!/navigator_layout|\(/.test(l['view.saved']), 'the saved note carries no setting key (ids belong in INDEX, not on the screen)');
  assert.equal(map.shortLabel('x'.repeat(40)).length, map.LABEL_CHARS_PRESET);
  assert.equal(map.shortLabel('krótka'), 'krótka');
});

test('labels on the plane are drawn only where they can be read: inside the map, never on top of another label; the focus and the tapped mark first; a mark near an edge flips its side; the rest keep the line under the map', () => {
  // A crowded level like the root (24 rows with long Polish labels) on the 342 x 300 phone map.
  const labels = ['Rozumowanie modelu', 'Model rozmowy', 'Temperatura odpowiedzi', 'Polecenia w czacie', 'Cięcie wyników', 'Odpowiedź na głos', 'Rozpoznawanie mowy',
    'Nowe działania', 'Gdzie trafiają pliki', 'Własne granice', 'Pamięć rozmowy', 'Analiza w tle', 'Koszty i limity', 'Eksport', 'Import rozmów', 'Połączenie',
    'Widoki', 'Role modeli', 'Prywatność', 'Silnik', 'Interfejs', 'Kontekst', 'Profil', 'Ustawienia'];
  const crowded = { ...result, children: labels.map((label, i) => row(`n:${i}`, 'setting', { label, count: i })) };
  const projection = project(map.viewModelFor(crowded, 'Graf'), { width: 342, height: 300, margin: map.MAP_MARGIN_PRESET });
  const cam = map.CAMERA_HOME, centre = { x: 171, y: 150 };
  const marks = projection.marks.map(m => ({ id: m.id, label: m.label, focus: m.focus, screen: map.screenOf({ x: m.x, y: m.y }, cam, centre), rScreen: Math.max(3, m.r) }));
  const placed = map.placeLabels(marks, { width: 342, height: 300 }, null);
  assert.ok(placed.length >= 1 && placed.length < marks.length, `some labels wait for a tap (${placed.length} of ${marks.length} drawn)`);
  assert.ok(placed.some(p => p.id === marks.find(m => m.focus).id), 'the focus always has its label');
  for (const p of placed) assert.ok(p.box.left >= 0 && p.box.top >= 0 && p.box.left + p.box.width <= 342 && p.box.top + p.box.height <= 300, `${p.id} inside the map`);
  for (const a of placed) for (const b of placed) if (a !== b) {
    const apart = a.box.left + a.box.width <= b.box.left || b.box.left + b.box.width <= a.box.left || a.box.top + a.box.height <= b.box.top || b.box.top + b.box.height <= a.box.top;
    assert.ok(apart, `${a.id} and ${b.id} do not cover each other`);
  }
  for (const p of placed) assert.equal(p.text, map.shortLabel(marks.find(m => m.id === p.id).label));
  // The tapped mark goes before the others: its label is drawn even when a neighbour's would have taken the room.
  const last = marks[marks.length - 1];
  const withSelected = map.placeLabels(marks, { width: 342, height: 300 }, last.id);
  assert.ok(withSelected.some(p => p.id === last.id), 'the tapped mark has its label');
  // A mark at the left edge cannot hold a label on its outer side: the side flips; one that fits nowhere is left to the line under the map.
  const edge = [{ id: 'e', label: 'Krótka', screen: { x: 4, y: 150 }, rScreen: 4 }];
  assert.equal(map.placeLabels(edge, { width: 342, height: 300 }, null)[0].anchor, 'start');
  assert.deepEqual(map.placeLabels([{ id: 'c', label: 'Za długa etykieta na mapę', screen: { x: 171, y: 150 }, rScreen: 170 }], { width: 342, height: 300 }, null), []);
});

test('a tap goes to the nearest mark, not to the hit button on top (r2:b2: dense levels, 44 px targets overlapping)', () => {
  // three marks 20 px apart (the root level at 390 px: 25 children about 21 px apart); the 44 px buttons of neighbours overlap
  const marks = [{ id: 'a', screen: { x: 100, y: 150 } }, { id: 'b', screen: { x: 120, y: 150 } }, { id: 'c', screen: { x: 140, y: 150 } }];
  assert.equal(map.nearestMark(marks, { x: 100, y: 150 }).id, 'a', 'the centre of a: a (DOM order would give c, the last button over that point)');
  assert.equal(map.nearestMark(marks, { x: 121, y: 149 }).id, 'b');
  assert.equal(map.nearestMark(marks, { x: 139, y: 160 }).id, 'c');
  assert.equal(map.nearestMark(marks, { x: 110, y: 150 }).id, 'a', 'a tie keeps the first mark in order');
  assert.equal(map.nearestMark([], { x: 0, y: 0 }), null);
});
