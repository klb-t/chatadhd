// Entry view of the graph (R45): contract of the reader, count texts and expand rules.
// The rendered panel reuses these functions; its strings are data (presentation.graph_entry).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const g = await loader.load('src/api/graph-entry.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const entry = pack.entries.find(row => row.key === 'presentation.graph_entry');
const textsFor = locale => (id, parameters) => p.featureMessage({ feature: 'graph_entry', locale, catalog: entry.value.locales[locale] }, id, parameters);
const envelope = areas => ({ schema: 'loom.graph_entry/1', lang: 'en', profile_hash: 'h', axes: {}, areas, seed: [], issues: [] });
const area = (id, counts, expand = null) => ({ id, label: id, origin: 'area', axes: {}, order: 1, counts, sources: [], expand });

test('both locales carry the same keys', () => {
  const { en, pl } = entry.value.locales;
  assert.deepEqual(Object.keys(en).sort(), Object.keys(pl).sort());
});

test('an unknown count is shown as unknown, with its reason, and never as zero', () => {
  const texts = textsFor('en');
  assert.equal(g.countText({ known: false }, texts), 'unknown');
  assert.equal(g.countText({ known: false, reason: 'database_unavailable' }, texts), 'unknown (database_unavailable)');
  assert.equal(g.countText({ known: true, value: 0 }, texts), '0');
  assert.equal(g.countText({ known: false }, textsFor('pl')), 'nieznane');
});

test('breakdown lists the groups of a known count only', () => {
  const texts = textsFor('en');
  assert.deepEqual(g.breakdown({ known: true, value: 3, by: { active: 2, excluded: 1 } }, texts), ['active: 2', 'excluded: 1']);
  assert.deepEqual(g.breakdown({ known: false, reason: 'x' }, texts), []);
});

test('the reader rejects a value on an unknown count and a missing value on a known one', () => {
  assert.doesNotThrow(() => g.readGraphEntry(envelope([area('a', { c: { known: false, reason: 'r' } })])));
  assert.throws(() => g.readGraphEntry(envelope([area('a', { c: { known: false, value: 0 } })])), /graph_entry_count_invalid/);
  assert.throws(() => g.readGraphEntry(envelope([area('a', { c: { known: true } })])), /graph_entry_count_invalid/);
  assert.throws(() => g.readGraphEntry({ schema: 'other' }), /graph_entry_envelope_invalid/);
});

test('expand links exist only where an existing view can open the descriptor', () => {
  const openers = { 'graph.nodes': () => {} };
  assert.equal(g.canExpand(area('a', {}, { view: 'graph.nodes', params: {} }), openers), true);
  assert.equal(g.canExpand(area('a', {}, { view: 'kb.query', params: {} }), openers), false);
  assert.equal(g.canExpand(area('a', {}, null), openers), false);
});
