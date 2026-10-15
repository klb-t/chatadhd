// P28 step 1: the pure 2D projection of a view model (no browser, no kernel).
import assert from 'node:assert/strict';
import { loadModule } from '../graph-perspectives/test-loader.mjs';
const { project, hsla } = await loadModule('src/view-engine/project.ts');
let count = 0;
async function check(name, fn) { await fn(); count++; console.log(`PASS ${name}`); }

const el = (id, coords, channels = {}, extra = {}) => ({ id, label: id, kind: 'version', measures: {}, coords, channels, sources: [], ...extra });
const model = (elements, systems, groups = []) => ({
  schema: 'loom.view_model/1', declaration: {}, focus: 'f', sources: [], elements, edges: [{ src: 'f', dst: 'a', type: 't', source: 's' }], groups,
  measure_stats: {}, coordinate_systems: systems, encodings: [], unsupported: [], budget: { max_nodes: null, elements: elements.length, collapsed: 0 }, issues: [],
});
const W = 400, H = 400;

await check('sector: 0 degrees is ahead (up), -90 is left, 90 is right, 180 is behind', async () => {
  const sys = [{ id: 'plane', system: 'sector', axes: [], bind: {}, supported: true }];
  const m = model([
    el('f', { plane: { angle_deg: 0, radius: 0 } }, {}, { focus: true }),
    el('a', { plane: { angle_deg: 0, radius: 2 } }), el('l', { plane: { angle_deg: -90, radius: 2 } }),
    el('r', { plane: { angle_deg: 90, radius: 2 } }), el('b', { plane: { angle_deg: 180, radius: 2 } }),
  ], sys, [{ id: 'past', kind: 'sector', center_deg: 0, width_deg: 90, members: ['a'] }]);
  const p = project(m, { width: W, height: H });
  const at = id => p.marks.find(x => x.id === id);
  assert.equal(p.system, 'sector');
  assert.ok(at('a').y < at('f').y && Math.abs(at('a').x - at('f').x) < 1e-6);
  assert.ok(at('l').x < at('f').x && Math.abs(at('l').y - at('f').y) < 1e-6);
  assert.ok(at('r').x > at('f').x);
  assert.ok(at('b').y > at('f').y);
  assert.equal(p.wedges.length, 1);
  assert.equal(p.edges.length, 1);
});

await check('depth from distance in 2D: shrink, fog and desaturation by the settings of the coordinates element; no depth system = untouched', async () => {
  const systems = [{ id: 'plane', system: 'sector', axes: [], bind: {}, supported: true }, { id: 'depth', system: 'depth_from_distance', axes: [], bind: {}, supported: true, settings: { shrink: 1, fog: 0.5, desaturate: 1 } }];
  const ch = { hue: 100, saturation: 0.8, lightness: 0.5, alpha: 1, size: 20 };
  const m = model([el('f', { plane: { angle_deg: 0, radius: 0 }, depth: { depth: 0 } }, ch), el('a', { plane: { angle_deg: 0, radius: 4 }, depth: { depth: 4 } }, ch)], systems);
  const p = project(m, { width: W, height: H });
  const [near, far] = [p.marks.find(x => x.id === 'f'), p.marks.find(x => x.id === 'a')];
  assert.ok(far.r < near.r);
  assert.equal(near.fill, hsla(100, 0.8, 0.5, 1));
  assert.equal(far.fill, hsla(100, 0, 0.5, 0.5));
  const flat = project(model([el('f', { plane: { angle_deg: 0, radius: 0 } }, ch), el('a', { plane: { angle_deg: 0, radius: 4 } }, ch)], [systems[0]]), { width: W, height: H });
  assert.equal(flat.marks[1].r, flat.marks[0].r);
});

await check('cartesian places by min/max, an element without a position is listed as unplaced, never dropped; no system falls back with a note', async () => {
  const sys = [{ id: 'xy', system: 'cartesian', axes: [], bind: {}, supported: true }];
  const m = model([el('f', { xy: { x: 0, y: 0 } }), el('a', { xy: { x: 10, y: 5 } }), el('n', {})], sys);
  const p = project(m, { width: W, height: H, margin: 20 });
  assert.equal(p.marks.length, 2);
  assert.deepEqual(p.unplaced.map(e => e.id), ['n']);
  assert.equal(p.marks.find(x => x.id === 'a').x, W - 20);
  const none = project(model([el('f', {}), el('a', {})], []), { width: W, height: H });
  assert.equal(none.system, null);
  assert.equal(none.marks.length, 2);
  assert.deepEqual(none.notes, ['no_spatial_coordinate_system']);
});

console.log(`${count} view-engine projection checks passed`);
