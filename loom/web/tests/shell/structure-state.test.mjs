// W11a-1: the window „Struktura projektu” without a browser (src/components/structure-state.ts): the window parameters and the request
// graph.structure gets, the appearance mapping as data (a preset, an own object, an unknown preset or sign, a mapping that hides
// everything -> a non-blocking advisory), the rows drawn from an answer (groups per expectation, ghosts with their expected property,
// children no template expects), and the advisories the person answered. The answers here are synthetic (shaped like graph.structure).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const st = await loader.load('src/components/structure-state.ts');
const mappings = JSON.parse(await readFile(new URL('../../src/shell/data/structure_mappings.json', import.meta.url), 'utf8'));
const kinds = JSON.parse(await readFile(new URL('../../src/shell/data/window_kinds.json', import.meta.url), 'utf8'));
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const SRC = await readFile(new URL('../../src/components/ProjectStructure.tsx', import.meta.url), 'utf8') + await readFile(new URL('../../src/components/structure-state.ts', import.meta.url), 'utf8');
const t = (id, vars = {}) => Object.entries(vars).reduce((s, [k, v]) => s.split(`{{${k}}}`).join(v), id + (Object.keys(vars).length ? ':' + Object.keys(vars).map(k => `{{${k}}}`).join(',') : ''));

const answer = {
  schema: 'loom.structure/1', found: true, root: { address: 'unit:project/alfa', label: 'Alfa' },
  expectations: [
    { template: 'unit_basic', applied_by: 'unit_basic', id: 'conversations', ref: 'unit_basic/conversations', label: 'Rozmowy', relation: 'part_of', card: { min: 1, max: null },
      child_match: { prefix: 'conv:' }, matched: ['conv:a', 'conv:b'], count: 2, state: 'present', state_label: 'jest', confidence: 0.9, source: 'template', source_ref: 'x' },
    { template: 'unit_basic', applied_by: 'unit_basic', id: 'documents', ref: 'unit_basic/documents', label: 'Dokumenty', relation: 'part_of', card: { min: 1, max: null },
      child_match: { kind: 'document' }, matched: [], count: 0, state: 'missing', state_label: 'brakuje', confidence: 0.6, source: 'template', source_ref: 'x', ghost: 'unit:project/alfa#expect/unit_basic/documents' },
  ],
  ghosts: [{ address: 'unit:project/alfa#expect/unit_basic/documents', label: 'Dokumenty', ghost: true, stored: false, state: 'missing', state_label: 'brakuje',
    expected: { relation: 'part_of', card: { min: 1, max: null }, child_match: { kind: 'document' } }, confidence: 0.6, source: 'template', template: 'unit_basic', expect: 'documents', parent: 'unit:project/alfa' }],
  children: [{ address: 'conv:a', label: 'Jeden', expected_by: ['unit_basic/conversations'] }, { address: 'conv:b', label: 'Dwa', expected_by: ['unit_basic/conversations'], evidence: { class: 'inferred' } },
    { address: 'x:1', label: 'Inne', expected_by: [] }],
  advisories: [],
};

test('the window kind is present with its parameters, and its words exist in both languages', () => {
  const k = (Array.isArray(kinds) ? kinds : kinds.kinds).find(x => x.id === 'project_structure');
  assert.equal(k.status, 'present');
  assert.equal(k.component, 'ProjectStructure');
  assert.deepEqual(k.params, ['root', 'templates', 'show_expected', 'mapping', 'depth', 'budget']);
  const blocks = [];
  const walk = o => { if (o && typeof o === 'object') { if ('ps_title' in o) blocks.push(o); Object.values(o).forEach(walk); } };
  walk(pack);
  // Every language the catalog offers carries the window's words (not a fixed count: a new language is data only).
  const offered = [];
  const walkL = o => { if (o && typeof o === 'object') { if (o.locales && Object.values(o.locales).some(l => l && 'ps_title' in l)) offered.push(...Object.keys(o.locales)); Object.values(o).forEach(walkL); } };
  walkL(pack);
  assert.ok(offered.includes('pl') && offered.includes('en'), `languages offered: ${offered}`);
  assert.equal(blocks.length, new Set(offered).size);
  const src = new Set((SRC).match(/t\("ps_[a-z_]+"/g).map(x => x.slice(3, -1)));
  for (const b of blocks) for (const id of src) assert.ok(b[id], `${id} missing in a language`);
});

test('parameters: presets when absent, values when given; the request carries them', () => {
  const none = st.paramsOf(() => undefined);
  assert.deepEqual(none, { root: '', templates: [], show_expected: true, mapping: null, depth: 1, budget: null });
  const p = st.paramsOf(k => ({ root: 'unit:project/alfa', templates: ['unit_basic', 3], show_expected: false, depth: 2, budget: 50 })[k]);
  assert.deepEqual(st.requestOf(p, 'pl', 'c_1'), { root: 'unit:project/alfa', show_expected: false, depth: 2, lang: 'pl', templates: ['unit_basic'], budget: 50, conv_id: 'c_1' });
});

test('mapping: the preset reads me_inferred_opacity; every preset of the data resolves; an own object; unknown preset and sign are said', () => {
  const m = st.resolveMapping(null, 'pl', { me_inferred_opacity: 0.4 });
  assert.equal(m.id, mappings.default);
  assert.equal(m.states.missing.opacity, 0.4);
  for (const id of Object.keys(mappings.presets)) assert.equal(st.resolveMapping(id, 'en').id, id);
  const own = st.resolveMapping({ states: { missing: { color: 'red', sparkle: 1 } } }, 'pl');
  assert.equal(own.id, 'own');
  assert.ok(own.advisories.some(a => a.code === 'structure_mapping_unknown_sign'));
  const bad = st.resolveMapping('no-such', 'pl');
  assert.equal(bad.id, mappings.default);
  assert.ok(bad.advisories.some(a => a.code === 'structure_mapping_unknown_preset'));
});

test('a mapping under which nothing missing is visible raises a non-blocking advisory (hidden, or opacity 0)', () => {
  assert.ok(st.resolveMapping('hide_missing', 'pl').advisories.some(a => a.code === 'structure_mapping_nothing_visible'));
  assert.ok(st.resolveMapping({ states: { missing: { opacity: 0 } } }, 'pl').advisories.some(a => a.code === 'structure_mapping_nothing_visible'));
  assert.ok(!st.resolveMapping('dashed', 'pl').advisories.length);
});

test('rows: a group per expectation, its children, its ghost with the expected property; other children apart; hiding counts', () => {
  const m = st.resolveMapping(null, 'pl', { me_inferred_opacity: 0.5 });
  const d = st.rowsOf(answer, m, t);
  assert.equal(d.groups.length, 2);
  assert.deepEqual(d.groups[0].rows.map(r => r.address), ['conv:a', 'conv:b']);
  assert.equal(d.groups[0].rows[1].style.opacity, 0.5);   // an inferred child takes the look of its evidence class
  assert.equal(d.groups[0].rows[0].style.opacity, undefined);
  const g = d.groups[1].rows[0];
  assert.equal(g.ghost, true);
  assert.equal(g.style.opacity, 0.5);
  assert.equal(g.style.borderStyle, 'dashed');
  assert.match(g.detail, /ps_card/);
  assert.deepEqual(d.other.map(r => r.address), ['x:1']);
  const hidden = st.rowsOf(answer, st.resolveMapping('hide_missing', 'pl'), t);
  assert.equal(hidden.groups[1].rows.length, 0);
  assert.equal(hidden.hidden, 1);
  // order: a sign with a lower order brings its group first
  const loud = st.rowsOf(answer, st.resolveMapping('loud', 'pl'), t);
  assert.equal(loud.groups[0].expectation.id, 'documents');
});

test('property: any mapping over any states gives rows, never an exception; opacity stays within 0..1', () => {
  const signs = [{}, { opacity: -1 }, { opacity: 2 }, { hidden: true }, { order: 3 }, { border: 'dotted' }, { size: 1.5 }, { weight: 'bold' }, { icon: '*' }, { unknown: 1 }, { opacity: { setting: 'me_inferred_opacity' } }];
  const states = ['present', 'missing', 'inferred', 'excess', 'declined', 'empty', 'weird'];
  let seed = 7; const rnd = n => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed % n; };
  for (let i = 0; i < 300; i++) {
    const map = { states: {} };
    for (const s of states) if (rnd(2)) map.states[s] = signs[rnd(signs.length)];
    const m = st.resolveMapping(map, rnd(2) ? 'pl' : 'en', { me_inferred_opacity: rnd(3) / 2 });
    const a = structuredClone(answer);
    for (const e of a.expectations) e.state = states[rnd(states.length)];
    const d = st.rowsOf(a, m, t);
    for (const g of d.groups) for (const r of g.rows) if ('opacity' in r.style) assert.ok(r.style.opacity >= 0 && r.style.opacity <= 1);
  }
});

test('advisories: kernel and page together, without the answered ones', () => {
  const kernel = [{ code: 'empty_expects', fingerprint: 'f1', template: 't', text: 'x', words: { pl: 'x', en: 'x' }, blocking: false }];
  const page = [{ code: 'structure_mapping_nothing_visible', fingerprint: 'nothing_visible|hide_missing', text: 'y' }];
  assert.equal(st.visibleAdvisories(kernel, page, new Set(), new Set()).length, 2);
  assert.equal(st.visibleAdvisories(kernel, page, new Set(['structure_empty_expects|f1']), new Set()).length, 1);
  assert.equal(st.visibleAdvisories(kernel, page, new Set(), new Set(['structure_mapping_nothing_visible|nothing_visible|hide_missing'])).length, 1);
});
