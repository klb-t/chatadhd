// W3 (OKNA §2.2, §3.1): the former Workshop is the layout `siatka` drawn from its dock tree (dock.ts). Examples from the preset, then a
// generative pass (seeded; LOOM_DOCK_RUNS raises the preset): any tree of splits and leaves, any ratio (also 0, 1, NaN) and any choice of
// shown windows gives a drawable tree whose windows are exactly the shown docked ones, in reading order, and never throws.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const D = await loader.load('src/shell/windows/dock.ts');
const sv = await loader.load('src/shell/start-view.ts');
const layouts = await loader.load('src/shell/data/layouts.json');

test('siatka: list (own width) | chat over knowledge; knowledge drops out while closed, the chat then takes the whole column', () => {
  const l = sv.LAYOUTS.siatka;
  const all = D.dockView(l);
  assert.equal(all.type, 'split'); assert.equal(all.dir, 'h');
  assert.equal(all.a.type, 'leaf'); assert.equal(all.a.active.kind, 'conversation_list'); assert.equal(all.a.sizing, 'content');
  assert.equal(all.b.dir, 'v'); assert.equal(all.b.a.active.kind, 'chat'); assert.equal(all.b.b.active.kind, 'knowledge');
  assert.equal(D.sideFlex(all.a, 'a', all.ratio, all.b), '0 0 auto', 'the list keeps its CSS width as in the former Workshop');
  assert.equal(D.sideFlex(all.b.a, 'a', all.b.ratio, all.b.b), '0 0 32.0000%');
  const closed = D.dockView(l, w => w.kind !== 'knowledge');
  assert.equal(closed.b.type, 'leaf'); assert.equal(closed.b.active.kind, 'chat');
  assert.deepEqual(D.dockedWindows(closed).map(w => w.kind), ['conversation_list', 'chat']);
  assert.equal(D.dockView(l, () => false), null, 'nothing shown: nothing to draw (the frame stays)');
  const raw = (layouts.default ?? layouts).layouts.siatka;
  assert.equal(raw.switcher.style, 'bar', 'the former header is the switcher bar (data)');
  assert.ok(raw.aliases.includes('warsztat'), '#warsztat opens the grid until W4 (§11 pt 3)');
});

test('a leaf with several windows is a tab group: the active one is drawn, the others are tabs', () => {
  const l = structuredClone(sv.LAYOUTS.siatka);
  l.windows.push({ ...l.windows.find(w => w.kind === 'knowledge'), id: 'w_graph', kind: 'graph', dock: { leaf: 'L2' } });
  l.leaves.L2 = { windows: ['w_knowledge', 'w_graph'], active: 'w_graph' };
  const v = D.dockView(l);
  assert.deepEqual(v.b.b.windows.map(w => w.id), ['w_knowledge', 'w_graph']);
  assert.equal(v.b.b.active.id, 'w_graph');
  l.leaves.L2.active = 'gone';
  assert.equal(D.dockView(l).b.b.active.id, 'w_knowledge', 'an unknown active window falls back to the first tab');
});

test('generative: any tree, ratio and shown set is drawable and loses no shown window', () => {
  let seed = Number(process.env.LOOM_DOCK_SEED || 7);
  const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
  const runs = Number(process.env.LOOM_DOCK_RUNS || 300);
  for (let r = 0; r < runs; r++) {
    let n = 0; const leaves = {}; const windows = [];
    const make = depth => {
      if (depth > 4 || rnd() < 0.35) {
        const id = 'L' + (n++); const ids = [];
        const k = Math.floor(rnd() * 3);   // 0..2 windows, an empty leaf too
        for (let i = 0; i < k; i++) { const w = 'w' + windows.length; ids.push(w); windows.push({ id: w, kind: 'k' + (windows.length % 5), node: '', mode: rnd() < 0.1 ? 'minimized' : 'docked', z: 0, params: {}, profile: null, title_override: null, collapsed: rnd() < 0.15, pinned: false, dock: { leaf: id } }); }
        leaves[id] = { windows: ids, active: ids.length ? ids[Math.floor(rnd() * ids.length)] : null };
        return { leaf: id };
      }
      const ratios = [0, 1, NaN, 0.5, rnd()];
      return { split: rnd() < 0.5 ? 'h' : 'v', ratio: ratios[Math.floor(rnd() * ratios.length)], a: make(depth + 1), b: make(depth + 1) };
    };
    const l = { id: 'g', tree: make(0), leaves, windows, focus: null, channels: {}, shelf: { handles: [] } };
    const hide = new Set(windows.filter(() => rnd() < 0.2).map(w => w.id));
    const shown = w => !w.collapsed && !hide.has(w.id) && w.mode !== 'minimized';
    const v = D.dockView(l, shown);
    const expected = [];
    const order = t => t.leaf !== undefined ? expected.push(...leaves[t.leaf].windows.filter(id => shown(windows.find(w => w.id === id)))) : (order(t.a), order(t.b));
    order(l.tree);
    assert.deepEqual(D.dockedWindows(v).map(w => w.id), expected, `run ${r}`);
    const walk = x => { if (!x) return; if (x.type === 'split') { assert.ok(x.ratio >= 0 && x.ratio <= 1); D.sideFlex(x.a, 'a', x.ratio, x.b); walk(x.a); walk(x.b); } else assert.ok(x.windows.includes(x.active)); };
    walk(v);
  }
});

test('aliases (§11 pt 3): #warsztat and #siatka open the grid until W4; layout_mode views_grid and windows reach it from data', () => {
  assert.equal(sv.parseShellHash('#warsztat').layout, 'siatka');
  assert.equal(sv.parseShellHash('#siatka').layout, 'siatka');
  assert.equal(sv.parseShellHash('#siatka/c_1').conversationId, 'c_1');
  assert.equal(sv.parseShellHash('#czat').layout, 'czat');
});
