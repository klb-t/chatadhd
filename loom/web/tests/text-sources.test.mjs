import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { materializationRequest, readSourceOccurrence, readSourceParts, saveSourceSelection, sourceSelectionPatch, sourceSelector, textSources } from '../src/api/text-sources-state.mjs';

const hash = 'a'.repeat(64), nextHash = 'b'.repeat(64);
const source = (id = 'src_text_a', key = 'upload:one', digest = hash, parser = 'loom.source_parts') =>
  ({ id, title: 'Zażółć.md', blob_hash: digest, parser, format: 'markdown', metadata: { source_key: key, previous_source_id: '' } });
const projection = (s = source()) => ({ schema: 'loom.source_text_parts/1', source_id: s.id, source_node: 'n_' + s.id,
  source_sha256: s.blob_hash, projection_node: 'n_projection', version_node: 'n_version', coordinate: 'source_utf8_bytes',
  occurrence_nodes: ['n_occ_1', 'n_occ_2'], method: { level: 'line' }, status: 'partial', reason: 'markdown_observations_partial_no_table_parser' });
const occurrence = (id = 'n_occ_1', begin = 0) => ({ node_id: id, text: 'Żółw\n', source: source(), provenance: {
  schema: 'loom.source_text_occurrence/1', source_id: source().id, source_sha256: hash,
  coordinate: 'source_utf8_bytes', byte_range: [begin, begin + Buffer.byteLength('Żółw\n')], index: begin ? 1 : 0 } });
// Fixture of native RFC 7386 config merging. No server, browser, runtime dependency or provider.
const merge = (target, patch) => {
  const out = { ...target };
  for (const [key, value] of Object.entries(patch)) out[key] = value && typeof value === 'object' && !Array.isArray(value) ? merge(out[key], value) : value;
  return out;
};

test('canonical source versions merge upload receipts only by key AND original hash', () => {
  const upload = source('src_upload_a', 'upload:one', hash, 'loom.client_upload'), made = source();
  for (const rows of [[upload, made], [made, upload]]) assert.deepEqual(textSources(rows), [made]);
  const independent = source('src_text_other', 'upload:two');
  const revision = source('src_text_revision', 'upload:one', nextHash);
  assert.equal(textSources([upload, made, independent, revision]).length, 3);
  assert.equal(textSources([null, 5, [], {}, { ...made, metadata: {} }, { ...made, parser: 'unrelated' }]).length, 0);
});
test('a revision preserves explicit predecessor, key, hash and language without changing the source', () => {
  const s = source('src_text_revision', 'upload:one', nextHash); s.metadata.previous_source_id = 'src_text_a';
  const before = structuredClone(s);
  assert.deepEqual(materializationRequest(s, 'pl'), { hash: nextHash, source_key: 'upload:one', name: 'Zażółć.md', format: 'markdown', language: 'pl', previous_source_id: 'src_text_a' });
  assert.deepEqual(s, before);
  assert.ok(!('previous_source_id' in materializationRequest(source(), 'pl')));
});
test('projection retains identical-text occurrences as distinct identities and positions', () => {
  const p = readSourceParts(projection(), source());
  assert.deepEqual(p.occurrence_nodes, ['n_occ_1', 'n_occ_2']);
  const a = readSourceOccurrence(occurrence(), source(), p.occurrence_nodes[0]);
  const b = readSourceOccurrence(occurrence('n_occ_2', 8), source(), p.occurrence_nodes[1]);
  assert.equal(a.text, b.text); assert.notEqual(a.node_id, b.node_id);
  assert.notDeepEqual(a.provenance.byte_range, b.provenance.byte_range);
});
test('cross-source/version projections and fake duplicate occurrence IDs are refused', () => {
  for (const patch of [{ source_id: 'other' }, { source_sha256: nextHash }, { coordinate: 'pdf_pages' },
    { occurrence_nodes: ['n_occ_1', 'n_occ_1'] }, { occurrence_nodes: [null] }, { method: null }]) {
    assert.throws(() => readSourceParts({ ...projection(), ...patch }, source()), /projection_invalid/);
  }
});
test('read checks raw UTF-8 bytes, not character count, and refuses foreign coordinates', () => {
  const good = occurrence(); assert.equal(Buffer.byteLength(good.text), 8); assert.equal(good.text.length, 5);
  for (const patch of [{ coordinate: 'ocr_pixels' }, { source_sha256: nextHash }, { source_id: 'other' },
    { byte_range: [0, 5] }, { byte_range: [-1, 6] }, { byte_range: [0.5, 7.5] }, { index: -1 }]) {
    assert.throws(() => readSourceOccurrence({ ...good, provenance: { ...good.provenance, ...patch } }, source(), good.node_id), /occurrence_invalid/);
  }
  assert.throws(() => readSourceOccurrence({ ...good, source: source('other') }, source(), good.node_id));
});
test('missing selector reach stays unknown; malformed saved selection cannot become an empty overwrite', () => {
  assert.deepEqual(sourceSelector({}), { enabled: false, ids: [], hops: null });
  for (const execution of [null, [], 5, { unified: null }, { unified: { enabled: 'yes' } }, { unified: { legacy_seed_ids: [4] } }, { unified: { legacy_relation_hops: -1 } }, { unified: { legacy_relation_hops: null } }]) {
    assert.throws(() => sourceSelectionPatch({ context_execution: execution }, 'n_occ_2', true, 0), /selector_invalid/);
  }
});
test('selection appends exact ID, is idempotent, preserves unrelated seeds and does not mutate config', () => {
  const cfg = { context_execution: { unified: { legacy_seed_ids: ['existing', 'n_occ_1'], budget_tokens: 42 }, graph_reply: { mode: 'off' } } };
  const before = structuredClone(cfg), patch = sourceSelectionPatch(cfg, 'n_occ_2', true, 0);
  assert.deepEqual(patch, { unified: { enabled: true, legacy_relation_hops: 0, legacy_seed_ids: ['existing', 'n_occ_1', 'n_occ_2'] } });
  const applied = { context_execution: merge(cfg.context_execution, patch) };
  assert.deepEqual(sourceSelectionPatch(applied, 'n_occ_2', true, 0), patch);
  assert.equal(applied.context_execution.unified.budget_tokens, 42);
  assert.deepEqual(cfg, before);
});
test('removing one ID leaves the enabled state, reach, budget and other selections alone', () => {
  const cfg = { context_execution: { unified: { enabled: false, legacy_seed_ids: ['a', 'b'] } } };
  assert.deepEqual(sourceSelectionPatch(cfg, 'a', false, 0), { unified: { legacy_seed_ids: ['b'] } });
  assert.deepEqual(sourceSelectionPatch(cfg, 'a', false, NaN), { unified: { legacy_seed_ids: ['b'] } });
  assert.throws(() => sourceSelectionPatch(cfg, 'a', true, 0.1));
});
test('explicit save reads fresh native state, merges one setting and confirms by a second read', async () => {
  let cfg = { context_execution: { graph_reply: { mode: 'off', profile: 'unchanged' }, goal_typing: { calls_authorized: false },
    unified: { legacy_seed_ids: ['existing'], include_knowledge: false, budget_tokens: 8000 } } };
  const calls = [];
  const host = { getConfig: async () => { calls.push('read'); return structuredClone(cfg); },
    mergeConfigKey: async (key, patch) => { calls.push(['merge', key, patch]); cfg[key] = merge(cfg[key], patch); return structuredClone(cfg); } };
  const saved = await saveSourceSelection(host, 'n_occ_2', true, 0);
  assert.equal(calls[0], 'read'); assert.equal(calls[1][0], 'merge'); assert.equal(calls[1][1], 'context_execution'); assert.equal(calls[2], 'read');
  assert.deepEqual(sourceSelector(saved).ids, ['existing', 'n_occ_2']);
  assert.equal(saved.context_execution.unified.budget_tokens, 8000);
  assert.equal(saved.context_execution.unified.include_knowledge, false);
  assert.deepEqual(saved.context_execution.graph_reply, { mode: 'off', profile: 'unchanged' });
  assert.equal(saved.context_execution.goal_typing.calls_authorized, false);
});
test('failed merge/readback is a failure, never a confirmed selection', async () => {
  await assert.rejects(saveSourceSelection({ getConfig: async () => ({}) }, 'n_occ_2', true, 0), /merge_unavailable/);
  await assert.rejects(saveSourceSelection({ getConfig: async () => ({}), mergeConfigKey: async () => ({}) }, 'n_occ_2', true, 0), /readback_mismatch/);
  await assert.rejects(saveSourceSelection({ getConfig: async () => ({}), mergeConfigKey: async () => { throw Error('synthetic refusal'); } }, 'n_occ_2', true, 0), /synthetic refusal/);
});
test('PL/EN labels and explicit format presets exist; PNG/PDF get no fake text adapter; DOCX has an explicit derived adapter', () => {
  const data = JSON.parse(readFileSync(new URL('../src/content/text-sources.words.json', import.meta.url)));
  for (const [key, value] of Object.entries(data)) if (!['formats', 'defaults', 'blockKinds', 'warnings'].includes(key)) { assert.ok(value.pl, key); assert.ok(value.en, key); }
  const extensions = Object.values(data.formats).flat();
  for (const ext of ['txt', 'md', 'py', 'docx']) assert.ok(extensions.includes(ext));
  for (const ext of ['png', 'pdf']) assert.ok(!extensions.includes(ext));
});
