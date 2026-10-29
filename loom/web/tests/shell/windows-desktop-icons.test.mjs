// R101 D2 (PULPIT_R101_2026-10-28 §3.2, §4, §6, §12, §13): the content of the desktop as pure functions - the declaration chosen by
// desktop_declaration (preset pulpit_graf), the icons of the pins, icon_grid, the window an address opens, the faint graph behind.
// Examples first, then generative passes (seeded; LOOM_DESKI_RUNS / LOOM_DESKI_SEED raise the presets): any declaration value resolves
// without an exception into a well-formed one; the grid never loses or stacks an icon whatever the width, columns and flow; the graph
// behind keeps only real edges; P1 - with icons, a desktop with no other section still offers a way to a window.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const I = await loader.load('src/shell/windows/desktop-icons.ts');
const D = await loader.load('src/shell/windows/desktop.ts');
const RUNS = Number(process.env.LOOM_DESKI_RUNS ?? 400);
let seed = Number(process.env.LOOM_DESKI_SEED ?? 2002);
const rnd = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
const pick = a => a[Math.floor(rnd() * a.length)];
const PANELS = ['graph', 'navigator', 'settings', 'memory', 'import', 'logs', 'context', 'methods', 'analysis', 'operations', 'perspectives',
  'onboarding', 'user-knowledge', 'graph-entry', 'view-engine', 'request', 'conversations', 'trash'];
const entries = D.kindEntries(id => PANELS.includes(id), 'czat', 'pl');

test('declaration: unset = pulpit_graf (pins with the first pins, the graph behind with a budget per device class); pulpit_ikony has no graph', () => {
  const g = I.resolveDeclaration(undefined, 'phone');
  assert.equal(g.id, 'pulpit_graf'); assert.equal(g.origin, 'preset');
  assert.equal(g.pins.focus, 'desktop:main');
  assert.deepEqual(g.pins.seed, ['party:self', 'area:context_projects', 'conversations:', 'kind:chat', 'area:knowledge', 'kind:settings', 'trash:']);
  assert.equal(g.around.budget, 150); assert.equal(I.resolveDeclaration(undefined, 'desktop').around.budget, 400);
  assert.ok(g.alpha > 0 && g.alpha < 0.5 && g.edgeAlpha > 0 && g.edgeAlpha < 0.5, 'faint');
  assert.equal(g.grid.columns, 4); assert.equal(I.resolveDeclaration(undefined, 'desktop').grid.columns, 'auto');
  assert.deepEqual(g.unsupported, []); assert.deepEqual(g.advisories, []);
  const k = I.resolveDeclaration('pulpit_ikony', 'desktop');
  assert.equal(k.origin, 'setting'); assert.equal(k.around, null); assert.ok(k.pins);
});

test('declaration: an unknown id = the preset with an advisory; inline object or JSON text; unknown element kinds are listed, not refused', () => {
  const u = I.resolveDeclaration('pulpit_nieznany', 'desktop');
  assert.equal(u.id, 'pulpit_graf'); assert.deepEqual(u.advisories.map(a => a.code), ['desktop_declaration_unknown']);
  const inline = { id: 'moj', elements: [{ id: 'p', kind: 'source', perspective: 'pinned_on', focus: 'desktop:praca' }, { id: 'x', kind: 'wallpaper' }, { id: 'c', kind: 'layers' }] };
  for (const v of [inline, JSON.stringify(inline)]) {
    const r = I.resolveDeclaration(v, 'phone');
    assert.equal(r.origin, 'inline'); assert.equal(r.id, 'moj'); assert.equal(r.pins.focus, 'desktop:praca'); assert.equal(r.around, null);
    assert.deepEqual(r.unsupported.map(e => e.id), ['x', 'c']);
  }
  const dark = I.resolveDeclaration({ elements: [{ kind: 'source', perspective: 'navigate' }, { kind: 'encoding', channel: 'alpha', value: 0 }, { kind: 'encoding', channel: 'edge_alpha', value: 0 }] }, 'desktop');
  assert.deepEqual(dark.advisories.map(a => a.code), ['nothing_visible'], 'opacity 0 is legal; the desktop asks whether it is intended');
  assert.equal(I.resolveDeclaration('{broken', 'desktop').advisories[0].code, 'desktop_declaration_unknown');
});

test('generative: any declaration value resolves without an exception into a well-formed declaration', () => {
  const junk = () => pick([undefined, null, 0, -3, 2.5, 1e9, NaN, '', 'auto', '7', 'x', true, [], {}, { phone: pick([3, 'auto', -1, 'q']) }]);
  const KINDS = ['source', 'encoding', 'coordinates', 'icon', 'rules', 'card', 'layers', '', undefined, 42];
  let unsupported = 0, total = 0;
  for (let r = 0; r < RUNS; r++) {
    const elements = Array.from({ length: Math.floor(rnd() * 8) }, (_, i) => rnd() < 0.08 ? pick([null, 3, 'x', []]) : ({
      id: rnd() < 0.8 ? `e${i}` : undefined, kind: pick(KINDS), perspective: pick(['pinned_on', 'navigate', 'scope_chain', 'other', undefined]),
      focus: junk(), seed: pick([undefined, [], ['kind:chat', 3, ''], 'x']), depth: junk(), budget: junk(), channel: pick(['alpha', 'edge_alpha', 'hue', undefined]),
      value: junk(), system: pick(['icon_grid', 'cartesian', undefined]), columns: junk(), cell_px: junk(), flow: junk(), label_lines: junk() }));
    const value = pick([() => ({ elements }), () => JSON.stringify({ id: 'j', elements }), () => pick(['pulpit_graf', 'pulpit_ikony', 'zz', '', '  ']), () => junk()])();
    const res = I.resolveDeclaration(value, pick(['phone', 'tablet', 'desktop', 'tv']));
    total++; unsupported += res.unsupported.length;
    assert.ok(res.alpha >= 0 && res.alpha <= 1 && res.edgeAlpha >= 0 && res.edgeAlpha <= 1);
    assert.ok(res.grid.columns === 'auto' || (Number.isInteger(res.grid.columns) && res.grid.columns >= 1));
    assert.ok(res.grid.cellPx > 0 && Number.isFinite(res.grid.cellPx));
    if (res.around) assert.ok(Number.isInteger(res.around.budget) && res.around.budget >= 0 && Number.isInteger(res.around.depth) && res.around.depth >= 0);
    if (res.pins) assert.ok(res.pins.seed.every(s => typeof s === 'string' && s.trim()));
    if (res.pins) assert.ok(res.icon, 'pins are always drawn as icons');
  }
  console.log(`[desktop-icons] declarations: ${total} resolved, ${unsupported} elements listed as unsupported, 0 exceptions`);
});

test('icon_grid: examples (phone 4 columns, auto from the width, column_major)', () => {
  const g = I.iconGrid(7, 358, { columns: 4, cellPx: 84, flow: 'row_major' });
  assert.equal(g.columns, 4); assert.equal(g.rows, 2);
  assert.deepEqual(g.cells.map(c => [c.col, c.row]), [[0, 0], [1, 0], [2, 0], [3, 0], [0, 1], [1, 1], [2, 1]]);
  assert.equal(I.iconGrid(7, 1000, { columns: 'auto', cellPx: 96, flow: 'row_major' }).columns, 10);
  assert.deepEqual(I.iconGrid(5, 300, { columns: 2, cellPx: 96, flow: 'column_major' }).cells.map(c => [c.col, c.row]), [[0, 0], [0, 1], [0, 2], [1, 0], [1, 1]]);
  assert.deepEqual(I.iconGrid(12, 390, { columns: 12, cellPx: 84, flow: 'row_major' }).advisories.map(a => a.code), ['icons_tiny'], '12 columns on a phone: allowed, said');
});

test('generative: icon_grid never loses, stacks or overflows an icon, whatever the width, columns and flow; deterministic', () => {
  let cells = 0;
  for (let r = 0; r < RUNS; r++) {
    const n = Math.floor(rnd() * 120); const width = pick([0, -5, NaN, 1, 44, 200 + rnd() * 2000, 390]);
    const grid = { columns: pick(['auto', 1, 2, 4, 9, 13.7, 200]), cellPx: pick([1, 44, 84, 96, 300]), flow: pick(['row_major', 'column_major', 'spiral', ''] ) };
    const g = I.iconGrid(n, width, grid);
    assert.equal(g.cells.length, n); cells += n;
    const seen = new Set(g.cells.map(c => `${c.col},${c.row}`));
    assert.equal(seen.size, n, 'no two icons share a cell');
    for (const c of g.cells) assert.ok(c.col >= 0 && c.col < g.columns && c.row >= 0 && c.row < Math.max(1, g.rows) && Number.isFinite(c.x) && Number.isFinite(c.y));
    assert.deepEqual(I.iconGrid(n, width, grid), g);
    assert.equal(g.advisories.length > 0, n > 0 && g.cellW < 44);
  }
  console.log(`[desktop-icons] grids: ${RUNS}, ${cells} icons placed, none lost or stacked`);
});

test('icons: the first pins open their windows (Rozmowy = the conversation list); the trash opens its window (D4); unknown prefix = fallback', () => {
  const rows = I.resolveDeclaration(undefined, 'phone').pins.seed.map((address, order) => ({ node: `n_${order}`, address, nodeLabel: address, nodeKind: 'x', order }));
  const icons = rows.map(r => I.iconOf(r, 'pl', entries));
  const by = Object.fromEntries(icons.map(i => [i.address, i]));
  assert.deepEqual(icons.map(i => i.label), ['Ty', 'Moje projekty', 'Rozmowy', 'Nowy czat', 'Mój graf', 'Ustawienia', 'Kosz']);
  assert.equal(by['conversations:'].kind, 'conversation_list'); assert.equal(by['conversations:'].status, 'available');
  assert.deepEqual(by['conversations:'].open.target, { panel: 'conversations' });
  assert.deepEqual(by['kind:chat'].open.target, { chat: true });
  assert.deepEqual(by['party:self'].open, { target: { panel: 'navigator' }, focus: 'party:self' });
  assert.equal(by['area:knowledge'].open.focus, 'area:knowledge');
  assert.equal(by['kind:settings'].status, 'available');
  assert.equal(by['trash:'].status, 'available'); assert.deepEqual(by['trash:'].open.target, { panel: 'trash' });   // R101 D4
  assert.equal(I.iconOf({ node: 'n', address: 'kind:request_inspector', nodeLabel: 'kind:request_inspector', nodeKind: 'window_kind', order: 0 }, 'pl', entries).status, 'available', 'P0 gap: request opens now');
  const odd = I.iconOf({ node: 'n', address: 'weird:x', nodeLabel: 'weird:x', nodeKind: 'address', order: 0, labelOverride: 'Moje' }, 'en', entries);
  assert.equal(odd.label, 'Moje'); assert.equal(odd.open.focus, 'weird:x');
  assert.equal(I.iconOf({ node: 'n', address: 'conversations:', nodeLabel: 'x', nodeKind: 'area', order: 0 }, 'en', entries).label, 'Conversations');
  assert.equal(I.openTargetOf('conv:c_1').conversation, 'c_1');
});

test('pins: the perspective rows joined with the link metadata, in the pinned order; the desktop node itself is not an icon', () => {
  const rows = I.pinsFrom('n_d', [{ id: 'n_d', kind: 'desktop', label: 'desktop:main' }, { id: 'n_b', kind: 'area', label: 'conversations:' }, { id: 'n_a', kind: 'party', label: 'party:self' }],
    [{ src: 'n_b', dst: 'n_d', metadata: { order: 1, address: 'conversations:', label_override: 'Gadki' } }, { src: 'n_a', dst: 'n_d', metadata: { order: 0, address: 'party:self' } }, { src: 'n_a', dst: 'n_zz', metadata: { order: 9 } }]);
  assert.deepEqual(rows.map(r => [r.address, r.order, r.labelOverride]), [['party:self', 0, null], ['conversations:', 1, 'Gadki']]);
});

test('P1 with icons: an available icon is a path (the emergency button is not needed); only greyed icons and no section = emergency', () => {
  const l = { id: 'czat', windows: [] };
  const none = D.desktopSettings({ desktop_sections: ['icons'] });
  assert.deepEqual(D.desktopPaths(l, none, entries, [], D.DESKTOP, [{ address: 'conversations:', status: 'available' }]), [{ section: 'icons', icon: 'conversations:' }]);
  assert.equal(D.desktopPaths(l, none, entries, [], D.DESKTOP, [{ address: 'trash:', status: 'planned' }])[0].emergency, true);
  assert.deepEqual(D.desktopPaths(l, D.desktopSettings({ desktop_sections: ['icons'], desktop_emergency_path: false }), entries, [], D.DESKTOP, []), []);
  assert.deepEqual(D.DESKTOP.sections.slice(0, 1), ['icons']);
});

test('generative: the graph behind keeps only real edges between drawn nodes, positions inside the area, deterministic', () => {
  let kept = 0, dropped = 0;
  for (let r = 0; r < Math.max(20, RUNS / 10); r++) {
    const n = Math.floor(rnd() * 60); const nodes = Array.from({ length: n }, (_, i) => ({ address: `a${i % Math.max(1, n - 2)}` }));
    const edges = Array.from({ length: Math.floor(rnd() * 120) }, () => ({ src: `a${Math.floor(rnd() * (n + 5))}`, dst: `a${Math.floor(rnd() * (n + 5))}` }));
    const b = I.backdropLayout(nodes, edges, 30);
    const ids = new Set(nodes.map(x => x.address));
    for (const e of b.edges) { assert.ok(ids.has(e.src) && ids.has(e.dst) && e.src !== e.dst); assert.ok(edges.includes(e), 'an edge of the input, never a made-up one'); }
    for (const p of b.pos.values()) assert.ok(p.x >= 0 && p.x <= 1 && p.y >= 0 && p.y <= 1);
    assert.equal(b.pos.size, ids.size);
    assert.deepEqual([...I.backdropLayout(nodes, edges, 30).pos], [...b.pos]);
    kept += b.edges.length; dropped += edges.length - b.edges.length;
  }
  console.log(`[desktop-icons] graph behind: ${kept} real edges kept, ${dropped} with an end outside the budget or a loop not drawn`);
});
