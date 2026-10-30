// R101 D5 (PULPIT_R101_2026-10-28 §6, §13 P3/P4, §14 row D5): the arrangement of the desktop icons (shell/windows/desktop-arrange.ts).
// Examples first (levels installation > person > preset, snapping on a rotation, a move that swaps, desktop.arrange by label/kind/count/
// recent/declaration, the journal), then generative passes (seeded; LOOM_DESKA_RUNS / LOOM_DESKA_SEED raise the presets): P4 - for any
// value (also garbage), any icons, any sequence of widths, columns and flows, every icon is placed exactly once, on its own cell inside the
// grid, and placing the placement again changes nothing (idempotent); P3 - any sequence of moves and arrangements, written as merge
// patches into a stored setting at both levels, is undone back to the first value and redone to the last one by the journal.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const A = await loader.load('src/shell/windows/desktop-arrange.ts');
const M = await loader.load('src/api/merge-patch.ts');
const RUNS = Number(process.env.LOOM_DESKA_RUNS ?? 400);
let seed = Number(process.env.LOOM_DESKA_SEED ?? 5005);
const rnd = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
const pick = a => a[Math.floor(rnd() * a.length)];
const int = (lo, hi) => lo + Math.floor(rnd() * (hi - lo + 1));
const GRID = { columns: 'auto', cellPx: 96, flow: 'row_major' };
const ADDR = ['party:self', 'area:context_projects', 'conversations:', 'kind:chat', 'area:knowledge', 'kind:settings', 'trash:'];
// the order of the members is not part of the value (a merge on the host may reorder them)
const canon = v => JSON.stringify(v, (k, x) => (x && typeof x === 'object' && !Array.isArray(x) ? Object.fromEntries(Object.entries(x).sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0))) : x));
const cellsOf = pl => Object.fromEntries(pl.placed.map(p => [p.address, [p.col, p.row]]));

test('levels: installation wins over person, person over the preset; another installation sees the person value', () => {
  const v = { phone: { positions: { 'trash:': { col: 0, row: 0 } } }, installations: { i1: { phone: { mode: 'free' } } } };
  const mine = A.readArrangement(v, 'phone', 'i1');
  assert.equal(mine.from, 'installation'); assert.equal(mine.mode, 'free'); assert.deepEqual(mine.positions, {});
  const other = A.readArrangement(v, 'phone', 'i2');
  assert.equal(other.from, 'person'); assert.equal(other.mode, 'grid'); assert.deepEqual(other.positions['trash:'], { col: 0, row: 0 });
  const none = A.readArrangement(undefined, 'desktop', 'i1');
  assert.equal(none.from, 'preset'); assert.equal(none.mode, 'grid'); assert.equal(none.snap, true); assert.deepEqual(none.advisories, []);
  assert.equal(A.readArrangement(JSON.stringify(v), 'phone', 'i1').from, 'installation', 'a JSON text is read too');
  assert.equal(A.readArrangement({ any: { mode: 'computed' } }, 'tablet').mode, 'computed', 'any = every class');
  assert.equal(A.readArrangement(42, 'phone').advisories[0].code, 'arrangement_unreadable');
  const rot = A.readArrangement({ phone: { positions: { 'trash:': { col: 3, row: 0 } } } }, 'desktop', 'i1');
  assert.equal(rot.inheritedFrom, 'phone', 'a class with no own value reads class_fallback (preset: the other class)');
  assert.deepEqual(rot.positions['trash:'], { col: 3, row: 0 });
  assert.equal(A.readArrangement({ phone: {} }, 'desktop', 'i1', { class_fallback: {} }).from, 'preset', 'class_fallback is data');
  assert.equal(A.readArrangement({ desktop: { mode: 'free' }, phone: {} }, 'desktop').inheritedFrom, null);
  const odd = A.readArrangement({ phone: { mode: 'spiral' } }, 'phone');
  assert.equal(odd.mode, 'grid'); assert.equal(odd.modeWritten, 'spiral'); assert.equal(odd.advisories[0].code, 'arrangement_mode_unknown');
});

test('the patch writes both levels of the preset, removes a dropped position with null, and merges back into what it wrote', () => {
  const v0 = { phone: { positions: { a: { col: 1, row: 0 }, b: { col: 2, row: 0 } } } };
  const p = A.arrangementPatch(v0, 'phone', { positions: { a: { col: 3, row: 1 } } }, 'i1');
  assert.deepEqual(p.phone, { positions: { b: null, a: { col: 3 , row: 1 } } });
  assert.deepEqual(p.installations, { i1: { phone: { positions: { a: { col: 3, row: 1 } } } } });
  const v1 = M.mergePatch(v0, p);
  assert.deepEqual(A.readArrangement(v1, 'phone', 'i1').positions, { a: { col: 3, row: 1 } });
  assert.deepEqual(A.readArrangement(v1, 'phone', 'i9').positions, { a: { col: 3, row: 1 } });
  assert.deepEqual(A.arrangementPatch(v1, 'phone', null, 'i1'), { installations: { i1: { phone: null } }, phone: null });
  assert.deepEqual(Object.keys(A.arrangementPatch(v1, 'phone', {}, 'i1', ['installation'])), ['installations'], 'levels are data');
  assert.deepEqual(Object.keys(A.arrangementPatch(v1, 'phone', {}, undefined, ['installation'])), ['phone'], 'no installation id: the person level');
});

test('rotation: a hand position is snapped into the narrower grid, never dropped; the wider grid gets it back', () => {
  const arr = A.readArrangement({ desktop: { positions: { 'trash:': { col: 10, row: 0 }, 'party:self': { col: 'last', row: 'last' } } } }, 'desktop');
  const wide = A.placeIcons(ADDR, arr, 1280, GRID);
  assert.equal(wide.columns, 13);
  assert.deepEqual(cellsOf(wide)['trash:'], [10, 0]); assert.deepEqual(cellsOf(wide)['party:self'], [12, 0]);
  const narrow = A.placeIcons(ADDR, arr, 390, GRID);
  assert.equal(narrow.columns, 4);
  assert.deepEqual(cellsOf(narrow)['trash:'], [3, 0], 'col 10 → the last column of 4');
  assert.deepEqual(cellsOf(narrow)['party:self'], [3, 1], '"last" column on a taken cell → the next free cell');
  assert.equal(narrow.placed.length, 7);
  assert.deepEqual(cellsOf(A.placeIcons(ADDR, arr, 1280, GRID)), cellsOf(wide), 'the stored value is not rewritten by a narrow width');
});

test('a move swaps with a pinned icon on the target cell; computed switches to grid; free stores a fraction', () => {
  const arr = A.readArrangement({ phone: { positions: { 'trash:': { col: 0, row: 0 } } } }, 'phone');
  const pl = A.placeIcons(ADDR, arr, 390, { ...GRID, columns: 4 });
  const from = cellsOf(pl)['kind:chat'];
  const next = A.moveIcon(arr, pl, 'kind:chat', { col: 0, row: 0 }, 't1');
  assert.deepEqual(next.positions['kind:chat'], { col: 0, row: 0, moved: 't1' });
  assert.deepEqual([next.positions['trash:'].col, next.positions['trash:'].row], from);
  const c = A.readArrangement({ phone: { mode: 'computed' } }, 'phone');
  assert.equal(A.moveIcon(c, A.placeIcons(ADDR, c, 390, GRID), 'trash:', { col: 1, row: 1 }).mode, 'grid');
  const f = A.readArrangement({ phone: { mode: 'free', snap: false } }, 'phone');
  const fm = A.moveIcon(f, A.placeIcons(ADDR, f, 390, GRID), 'trash:', { x: 0.5, y: 1.7 });
  assert.equal(fm.positions['trash:'].x, 0.5); assert.equal(fm.positions['trash:'].y, 1, 'clamped into the area');
  const fp = A.placeIcons(ADDR, A.readArrangement({ phone: fm }, 'phone'), 390, GRID);
  const t = fp.placed.find(p => p.address === 'trash:');
  assert.ok(t.x >= 0 && t.x <= 390 - fp.cellW && t.y >= 0 && t.y <= fp.height - fp.cellH, 'free: inside the area');
});

test('desktop.arrange: label, kind, count, recent, declaration; an unknown order changes nothing and says so', () => {
  const icons = [{ address: 'b', label: 'Żaba', kind: 'chat' }, { address: 'a', label: 'ćma', kind: 'settings', count: 3 }, { address: 'c', label: 'Atlas', kind: 'chat', count: 7 }];
  const arr = A.readArrangement({ phone: { positions: { b: { col: 0, row: 0, moved: '2026-10-30T10:00:00Z' }, a: { col: 1, row: 0, moved: '2026-10-30T11:00:00Z' } } } }, 'phone');
  const pl = A.placeIcons(icons.map(i => i.address), arr, 390, { ...GRID, columns: 4 });
  const order = by => { const r = A.arrangeIcons(arr, pl, icons, by, 'pl'); return Object.entries(r.value.positions).sort((x, y) => x[1].row - y[1].row || x[1].col - y[1].col).map(e => e[0]); };
  assert.deepEqual(order('label'), ['c', 'a', 'b'], 'Polish alphabet: Atlas, ćma, Żaba');
  assert.deepEqual(order('kind'), ['c', 'b', 'a']);
  assert.deepEqual(order('count'), ['c', 'a', 'b']);
  assert.deepEqual(order('recent'), ['a', 'b', 'c']);
  assert.deepEqual(A.arrangeIcons(arr, pl, icons, 'declaration').value.positions, {});
  const u = A.arrangeIcons(arr, pl, icons, 'colour');
  assert.equal(u.value, null); assert.equal(u.advisory.code, 'arrange_by_unknown');
});

test('journal: undo gives the value before, redo the value after; a new operation clears redo; a limit is data', () => {
  let j = A.emptyJournal();
  j = A.record(j, { op: 'move', before: null, after: { positions: { a: { col: 1, row: 0 } } } });
  j = A.record(j, { op: 'arrange', before: { positions: { a: { col: 1, row: 0 } } }, after: { positions: {} } });
  const u = A.undo(j); assert.deepEqual(u.value, { positions: { a: { col: 1, row: 0 } } });
  const u2 = A.undo(u.journal); assert.equal(u2.value, null); assert.equal(A.undo(u2.journal), null);
  const r = A.redo(u2.journal); assert.deepEqual(r.value, { positions: { a: { col: 1, row: 0 } } });
  assert.deepEqual(A.record(r.journal, { op: 'x', before: null, after: null }).future, []);
  assert.equal(A.record(A.record(A.emptyJournal(), { op: 'a', before: null, after: null }, 1), { op: 'b', before: null, after: null }, 1).past.length, 1);
  assert.deepEqual(A.readJournal('nonsense'), A.emptyJournal());
  assert.equal(A.readJournal(JSON.stringify(j)).past.length, 2);
});

const garbagePos = () => pick([
  () => ({ col: int(-5, 40), row: int(-5, 40) }), () => ({ col: 'last', row: int(0, 9) }), () => ({ col: int(0, 9), row: 'last' }),
  () => ({ col: rnd() * 20 - 3, row: rnd() * 20 }), () => ({ col: 'x', row: null }), () => ({ x: rnd() * 1.6 - 0.3, y: rnd() * 1.6 - 0.3 }),
  () => ({ x: int(-50, 2000), y: int(-50, 2000), unit: 'px' }), () => ({}), () => ({ col: Infinity, row: -Infinity }), () => ({ col: String(int(0, 9)), row: String(int(0, 9)) }),
])();
const randomValue = addrs => {
  const positions = {};
  for (const a of addrs) if (rnd() < 0.6) positions[a] = garbagePos();
  if (rnd() < 0.15) positions['not-on-desktop:' + int(0, 9)] = garbagePos();
  return { mode: pick(['grid', 'free', 'computed', 'spiral', undefined, 7]), snap: pick([true, false, undefined]), columns: pick([undefined, 'auto', int(1, 12), 0, -3, 'abc']),
    flow: pick([undefined, 'row_major', 'column_major', 'spiral']), positions };
};
const randomAddrs = () => { const n = int(0, 30); return Array.from({ length: n }, (_, i) => pick([`x:${i}`, `x:${i}`, `x:${int(0, 40)}`])); };

test(`P4 (${RUNS} runs): every icon placed once, on its own cell inside the grid, for any value and any sequence of sizes; idempotent`, () => {
  for (let k = 0; k < RUNS; k++) {
    const addrs = randomAddrs(); const uniq = [...new Set(addrs)];
    const arr = A.readArrangement({ c: randomValue(uniq) }, 'c');
    const grid = { columns: pick(['auto', int(1, 8)]), cellPx: pick([96, 56, 0, -4, 200]), flow: pick(['row_major', 'column_major', 'odd']) };
    const widths = Array.from({ length: int(1, 6) }, () => pick([390, 844, 1280, 320, 0, -1, NaN, int(1, 3000)]));
    let first;
    for (const [wi, w] of widths.entries()) {
      const pl = A.placeIcons(addrs, arr, w, grid);
      const got = pl.placed.map(p => p.address);
      assert.deepEqual(got, uniq, `every icon exactly once (run ${k}, width ${w})`);
      for (const p of pl.placed) {
        assert.ok(Number.isInteger(p.col) && p.col >= 0 && p.col < pl.columns && Number.isInteger(p.row) && p.row >= 0, `inside the grid (run ${k})`);
        assert.ok(Number.isFinite(p.x) && Number.isFinite(p.y) && p.x >= 0 && p.y >= 0, `finite (run ${k})`);
      }
      assert.equal(new Set(pl.placed.map(p => `${p.col},${p.row}`)).size, uniq.length, `no two icons on one cell (run ${k}, mode ${pl.mode})`);
      if (pl.mode === 'free') for (const p of pl.placed) assert.ok(p.x <= Math.max(0, pl.width - pl.cellW) + 1e-6 && p.y <= pl.height - pl.cellH + 1e-6, `free: inside the area (run ${k})`);
      // idempotent: the placement written back as the positions places to the same cells
      const again = A.readArrangement({ c: { ...(arr.value ?? {}), mode: arr.mode, positions: Object.fromEntries(pl.placed.map(p => [p.address, pl.mode === 'free' ? { x: p.x, y: p.y, unit: 'px' } : { col: p.col, row: p.row }])) } }, 'c');
      const pl2 = A.placeIcons(addrs, again, w, grid);
      if (pl.mode !== 'computed') assert.deepEqual(pl2.placed.map(p => [p.address, p.col, p.row, Math.round(p.x), Math.round(p.y)]), pl.placed.map(p => [p.address, p.col, p.row, Math.round(p.x), Math.round(p.y)]), `idempotent (run ${k})`);
      if (wi === 0) first = JSON.stringify(cellsOf(pl));
    }
    assert.equal(JSON.stringify(cellsOf(A.placeIcons(addrs, arr, widths[0], grid))), first, `back at the first size, the first placement (run ${k})`);
  }
});

test(`P3 (${Math.ceil(RUNS / 4)} runs): any sequence of moves and arrangements written as merge patches is undone to the first value and redone to the last`, () => {
  for (let k = 0; k < Math.ceil(RUNS / 4); k++) {
    const addrs = [...new Set(randomAddrs())]; if (!addrs.length) addrs.push('x:0');
    const cls = pick(['phone', 'desktop', 'tablet']); const inst = pick(['i1', 'i2', undefined]);
    // stored as the host stores it (JSON: Infinity is null)
    let stored = rnd() < 0.5 ? undefined : JSON.parse(JSON.stringify({ [cls]: randomValue(addrs), ...(rnd() < 0.5 ? { installations: { i1: { [cls]: randomValue(addrs) } } } : {}) }));
    const view = () => { const a = A.readArrangement(stored, cls, inst); return canon({ mode: a.mode, positions: a.positions, columns: a.columns, flow: a.flow, snap: a.snap }); };
    const width = pick([390, 844, 1280]);
    const first = view(); const views = [first];
    let j = A.emptyJournal();
    const write = value => { stored = M.mergePatch(stored, A.arrangementPatch(stored, cls, value, inst)); };
    for (let s = int(1, 8); s > 0; s--) {
      const arr = A.readArrangement(stored, cls, inst);
      const pl = A.placeIcons(addrs, arr, width, GRID);
      let after;
      if (rnd() < 0.6) {
        const to = arr.mode === 'free' ? { x: rnd(), y: rnd() } : { col: int(0, pl.columns - 1), row: int(0, pl.rows + 1) };
        after = A.moveIcon(arr, pl, pick(addrs), to, `t${s}`);
      } else {
        const r = A.arrangeIcons(arr, pl, addrs.map(a => ({ address: a, label: a, kind: a.slice(0, 2), count: int(0, 3) })), pick(['label', 'kind', 'recent', 'count', 'declaration', 'nope']));
        if (!r.value) continue;
        after = r.value;
      }
      j = A.record(j, { op: 'x', before: arr.value, after });
      write(after); views.push(view());
    }
    for (let i = views.length - 2; i >= 0; i--) { const u = A.undo(j); j = u.journal; write(u.value); assert.equal(view(), views[i], `undo step back to view ${i} (run ${k})`); }
    assert.equal(view(), first, `undone to the first value (run ${k})`);
    for (let i = 1; i < views.length; i++) { const r = A.redo(j); j = r.journal; write(r.value); assert.equal(view(), views[i], `redo to view ${i} (run ${k})`); }
  }
});

test('free mode: a flowing icon never lands under a hand-placed one (review 1030 finding 4); two hand icons on one cell are separated', () => {
  for (const snap of [true, false]) {
    const arr = A.readArrangement({ c: { mode: 'free', snap, positions: { 'b:1': { x: 0, y: 0 } } } }, 'c');
    const pl = A.placeIcons(['a:0', 'b:1', 'c:2'], arr, 390, GRID);
    const at = Object.fromEntries(pl.placed.map(p => [p.address, [p.col, p.row]]));
    assert.deepEqual(at['b:1'], [0, 0], 'the hand-placed icon keeps its cell');
    assert.deepEqual(at['a:0'], [1, 0], 'the first flowing icon skips the pinned cell');
    assert.deepEqual(at['c:2'], [2, 0]);
  }
  const two = A.readArrangement({ c: { mode: 'free', snap: true, positions: { 'a:0': { x: 0, y: 0 }, 'b:1': { x: 0, y: 0 } } } }, 'c');
  const p2 = A.placeIcons(['a:0', 'b:1'], two, 390, GRID);
  assert.deepEqual(p2.placed.map(p => [p.col, p.row]), [[0, 0], [1, 0]]);
  assert.deepEqual(p2.advisories, [{ code: 'arrangement_cell_taken', vars: { address: 'b:1' } }]);
});
