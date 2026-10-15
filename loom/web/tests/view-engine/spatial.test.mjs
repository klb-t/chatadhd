// P28 step 3: the pure spatial projection (sector -> direction, depth -> distance, the blend by gaze angle, label policy). No browser, no three.js, no kernel.
import assert from 'node:assert/strict';
import { loadModule } from '../graph-perspectives/test-loader.mjs';
const S = await loadModule('src/view-engine/spatial.ts');
const { readSpatialConfig, directionOf, azimuthOf, declarationAngle, angleDiff, distanceOf, curveAt, sectorWeight, blendFor, labelVisible, projectSpatial } = S;
let count = 0;
async function check(name, fn) { await fn(); count++; console.log(`PASS ${name}`); }
const near = (a, b, eps = 1e-9) => assert.ok(Math.abs(a - b) <= eps, `${a} != ${b}`);
const cfg = readSpatialConfig(undefined);

const el = (id, coords, channels = {}, extra = {}) => ({ id, label: id, kind: 'version', measures: {}, coords, channels, sources: [], ...extra });
const model = (elements, systems, groups = [], edges = []) => ({
  schema: 'loom.view_model/1', declaration: {}, focus: 'f', sources: [], elements, edges, groups,
  measure_stats: {}, coordinate_systems: systems, encodings: [], unsupported: [], budget: { max_nodes: null, elements: elements.length, collapsed: 0 }, issues: [],
});
const sector = [{ id: 'plane', system: 'sector', axes: [], bind: {}, supported: true }];
const groups = [
  { id: 'past', kind: 'sector', center_deg: 0, width_deg: 90, members: [] },
  { id: 'callers', kind: 'sector', center_deg: -90, width_deg: 60, members: [] },
  { id: 'deps', kind: 'sector', center_deg: 90, width_deg: 60, members: [] },
  { id: 'future', kind: 'sector', center_deg: 180, width_deg: 90, members: [] },
];

await check('sector angles are real directions: 0 ahead (-z), 90 right (+x), 180 behind (+z), -90 left', async () => {
  const [a, r, b, l] = [0, 90, 180, -90].map(x => directionOf(x, cfg));
  near(a[0], 0); near(a[2], -1);
  near(r[0], 1); near(r[2], 0, 1e-12);
  near(b[0], 0, 1e-12); near(b[2], 1);
  near(l[0], -1);
  for (const d of [a, r, b, l]) near(d[1], 0);
  assert.equal(Math.round(azimuthOf(...[r[0], r[2]])), 90);
  assert.equal(Math.round(azimuthOf(l[0], l[2])), -90);
});

await check('the direction of the past is data: an offset turns it, the mirror flips it, and the inverse maps a gaze back to the declaration angle', async () => {
  const back = readSpatialConfig({ spatial: { direction_offset_deg: 180 } });
  const d = directionOf(0, back);
  near(d[2], 1); near(d[0], 0, 1e-12);
  const mir = readSpatialConfig({ spatial: { mirror: true } });
  near(directionOf(90, mir)[0], -1);
  for (const c of [back, mir, readSpatialConfig({ spatial: { mirror: true, direction_offset_deg: 30 } })])
    for (const a of [-120, -10, 0, 45, 170]) near(angleDiff(declarationAngle(azimuthOf(...[directionOf(a, c)[0], directionOf(a, c)[2]]), c), a), 0, 1e-9);
});

await check('angleDiff is the shortest signed difference', async () => {
  near(angleDiff(350, 10), -20); near(angleDiff(10, 350), 20); near(angleDiff(180, -180), 0); near(angleDiff(90, -90), 180);
});

await check('depth is distance: near + (far - near) * curve(r / max); the curve is data', async () => {
  near(distanceOf(0, 10, cfg), cfg.nearM); near(distanceOf(10, 10, cfg), cfg.farM); near(distanceOf(5, 10, cfg), (cfg.nearM + cfg.farM) / 2);
  const sq = readSpatialConfig({ spatial: { distance: { near_m: 2, far_m: 6, curve: [[0, 0], [0.5, 0.8], [1, 1]] } } });
  near(distanceOf(5, 10, sq), 2 + 4 * 0.8); near(distanceOf(99, 10, sq), 6); near(distanceOf(1, 0, sq), 2);
  near(curveAt([[0, 0], [1, 10]], 0.25), 2.5); near(curveAt([[1, 1], [0, 0]], 0.5), 0.5, 1e-12); near(curveAt([], 3), 3);
});

await check('projection: history ahead, callers left, dependencies right, the future behind; farther = farther; focus at the near ring', async () => {
  const m = model([
    el('f', { plane: { angle_deg: 0, radius: 0 } }, {}, { focus: true }),
    el('a1', { plane: { angle_deg: 0, radius: 1, sector: 'past' } }, {}, { sector: 'past' }),
    el('a3', { plane: { angle_deg: 0, radius: 3, sector: 'past' } }, {}, { sector: 'past' }),
    el('l', { plane: { angle_deg: -90, radius: 2 } }, {}, { sector: 'callers' }),
    el('r', { plane: { angle_deg: 90, radius: 2 } }, {}, { sector: 'deps' }),
    el('b', { plane: { angle_deg: 180, radius: 2 } }, {}, { sector: 'future' }),
  ], sector, groups, [{ src: 'f', dst: 'a1', type: 't', source: 's' }, { src: 'f', dst: 'zzz', type: 't', source: 's' }]);
  const p = projectSpatial(m, cfg);
  const at = id => p.marks.find(x => x.id === id);
  assert.equal(p.system, 'sector');
  assert.ok(at('a1').z < 0 && Math.abs(at('a1').x) < 1e-9, 'history is ahead (-z)');
  assert.ok(at('l').x < 0 && at('r').x > 0 && at('b').z > 0);
  assert.ok(at('a3').distance > at('a1').distance && at('a1').distance > at('f').distance);
  near(at('f').distance, cfg.nearM); near(at('a3').distance, cfg.farM);
  assert.ok(at('a3').y > at('a1').y, 'a rising line keeps nodes on one ray from hiding each other');
  assert.equal(p.edges.length, 1, 'edges to missing elements are dropped');
  assert.equal(p.wedges.length, 4);
  near(p.wedges.find(w => w.id === 'deps').azimuth, 90); near(p.wedges.find(w => w.id === 'callers').azimuth, -90);
  near(Math.abs(p.wedges.find(w => w.id === 'future').azimuth), 180);
});

await check('the future sector reversed by the renderer data puts history behind and the future ahead - nothing hardcoded', async () => {
  const m = model([el('f', { plane: { angle_deg: 0, radius: 0 } }, {}, { focus: true }), el('a', { plane: { angle_deg: 0, radius: 2 } }, {}, { sector: 'past' }), el('b', { plane: { angle_deg: 180, radius: 2 } }, {}, { sector: 'future' })], sector, groups);
  const p = projectSpatial(m, readSpatialConfig({ spatial: { direction_offset_deg: 180 } }));
  assert.ok(p.marks.find(x => x.id === 'a').z > 0 && p.marks.find(x => x.id === 'b').z < 0);
});

await check('a tree (depth in hops, branch angle) and a cartesian system are placed too; unplaced elements are listed, never dropped', async () => {
  const tree = model([el('f', { t: { depth: 0, angle_deg: 0 } }, {}, { focus: true }), el('c', { t: { depth: 3, angle_deg: 30 } }), el('x', {})], [{ id: 't', system: 'tree_branch_angles', axes: [], bind: {}, supported: true }]);
  const pt = projectSpatial(tree, cfg);
  assert.equal(pt.marks.length, 2); assert.equal(pt.unplaced.length, 1);
  assert.ok(pt.marks.find(x => x.id === 'c').x > 0, '30 degrees is to the right of ahead');
  const cart = model([el('p', { c: { x: 0, y: 0 } }), el('q', { c: { x: 10, y: 4 } })], [{ id: 'c', system: 'cartesian', axes: [], bind: {}, supported: true }]);
  const pc = projectSpatial(cart, cfg);
  assert.ok(pc.marks.every(x => Math.abs(x.z + cfg.flatM) < 1e-9) && pc.marks[1].x > pc.marks[0].x && pc.marks[1].y > pc.marks[0].y);
  const none = projectSpatial(model([el('a', {}), el('b', {})], []), cfg);
  assert.equal(none.system, null); assert.ok(none.notes.includes('no_spatial_coordinate_system')); assert.equal(none.marks.length, 2);
});

await check('channels: hue, saturation, lightness, alpha, size, blur and badge reach the mark; depth fog and desaturation come from the depth element settings', async () => {
  const systems = [...sector, { id: 'depth', system: 'depth_from_distance', axes: [], bind: {}, supported: true, settings: { fog: 0.5, desaturate: 1 } }];
  const m = model([
    el('f', { plane: { angle_deg: 0, radius: 0 }, depth: { depth: 0 } }, { hue: 10, saturation: 0.8, lightness: 0.5, alpha: 1, size: 20, blur: 2, badge: 'v' }, { focus: true }),
    el('d', { plane: { angle_deg: 0, radius: 4 }, depth: { depth: 4 } }, { hue: 10, saturation: 0.8, lightness: 0.5, alpha: 1, size: 20 }, { sector: 'past' }),
  ], systems, groups);
  const p = projectSpatial(m, cfg);
  const f = p.marks.find(x => x.id === 'f'), d = p.marks.find(x => x.id === 'd');
  assert.equal(f.hue, 10); near(f.saturation, 0.8); near(f.lightness, 0.5); near(f.alpha, 1); assert.equal(f.blur, 2); assert.equal(f.badge, 'v');
  near(f.radiusM, 10 * cfg.mPerPx);
  near(d.alpha, 0.5); near(d.saturation, 0); assert.equal(d.depth, 1);
  const off = projectSpatial(m, readSpatialConfig({ spatial: { fog: { use_depth_system: false } } }));
  near(off.marks.find(x => x.id === 'd').alpha, 1);
  const big = projectSpatial(model([el('z', { plane: { angle_deg: 0, radius: 1 } }, { size: 9999 })], sector), cfg);
  near(big.marks[0].radiusM, cfg.maxRadiusM);
});

await check('a far node never looks smaller than the minimum angle (data), so the far past stays visible; 0 turns it off', async () => {
  const m = model([el('f', { plane: { angle_deg: 0, radius: 0 } }, { size: 4 }, { focus: true }), el('far', { plane: { angle_deg: 0, radius: 9 } }, { size: 4 })], sector);
  const far = projectSpatial(m, cfg).marks.find(x => x.id === 'far');
  near(far.radiusM, far.distance * Math.tan((cfg.minAngleDeg * Math.PI) / 360));
  assert.ok(far.radiusM > cfg.minRadiusM);
  const off = projectSpatial(m, readSpatialConfig({ spatial: { node: { min_angle_deg: 0 } } })).marks.find(x => x.id === 'far');
  near(off.radiusM, cfg.minRadiusM);
});

await check('the sector blend by gaze angle: full weight on the sector centre, the curve (data) falls off over half-widths, the focus and sectorless elements keep full weight', async () => {
  const w = a => sectorWeight(a, groups[0], cfg);
  near(w(0), 1); assert.ok(w(22.5) > w(45) && w(45) > w(67.5) && w(67.5) > w(180));
  near(w(45), curveAt(cfg.blendCurve, 1));
  near(sectorWeight(90, groups[2], cfg), 1);
  near(sectorWeight(0, groups[2], cfg), curveAt(cfg.blendCurve, 90 / 30), 1e-9);
  const flat = readSpatialConfig({ blend: { curve: [[0, 1], [10, 1]] } });
  near(sectorWeight(180, groups[0], flat), 1);
  const rev = readSpatialConfig({ spatial: { direction_offset_deg: 180 } });
  near(sectorWeight(180, groups[0], rev), 1, 1e-9); // looking at azimuth 180 faces the past when the past is reversed
  const marks = [{ id: 'f', focus: true, sector: undefined }, { id: 'a', focus: false, sector: 'past' }, { id: 'b', focus: false, sector: 'future' }, { id: 't', focus: false, sector: undefined }];
  const blend = blendFor(marks, groups, 0, cfg);
  near(blend.get('a').alpha, 1); near(blend.get('b').alpha, cfg.blendFloorAlpha + (1 - cfg.blendFloorAlpha) * blend.get('b').weight);
  assert.ok(blend.get('b').alpha < blend.get('a').alpha && blend.get('b').scale < blend.get('a').scale);
  near(blend.get('f').alpha, 1); near(blend.get('t').scale, 1);
  const turned = blendFor(marks, groups, 180, cfg);
  assert.ok(turned.get('b').alpha > turned.get('a').alpha, 'turning around swaps the emphasis: the world is still, the user turns');
});

await check('labels follow the policy (data): focus, gazed sectors and the near ones; all / none; the toggle forces all', async () => {
  const m = { focus: false, distance: 5 };
  assert.equal(labelVisible(m, 0.1, cfg, false), false);
  assert.equal(labelVisible(m, 0.9, cfg, false), true);
  assert.equal(labelVisible({ focus: true, distance: 9 }, 0, cfg, false), true);
  assert.equal(labelVisible({ focus: false, distance: 1 }, 0, cfg, false), true);
  assert.equal(labelVisible(m, 0.1, cfg, true), true);
  assert.equal(labelVisible(m, 1, readSpatialConfig({ labels: { policy: 'none' } }), false), false);
  assert.equal(labelVisible(m, 0, readSpatialConfig({ labels: { policy: 'all' } }), false), true);
});

await check('renderer settings that are not usable fall back to the presets and are REPORTED, not hidden', async () => {
  const c = readSpatialConfig({ spatial: { distance: { near_m: 'x', curve: [1, 2] } }, blend: 7 });
  assert.equal(c.nearM, 1);
  assert.ok(c.notes.some(n => n.includes('near_m')) && c.notes.some(n => n.includes('curve')) && c.notes.some(n => n.startsWith('blend')), c.notes.join('|'));
  assert.deepEqual(readSpatialConfig(undefined).notes, []);
});
console.log(`${count} checks passed`);
