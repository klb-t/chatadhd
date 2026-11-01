import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { createSourceTaskGate, materializationRequest, materializedSource, readSourceOccurrence, readSourceParts,
  saveSourceSelection, sourceDerivation, sourceOriginalHash, sourceSelector, textSources } from '../src/api/text-sources-state.mjs';

// Mapped state only. This is neither a reader rerun nor native/browser/context acceptance.
const extraction = JSON.parse(readFileSync(new URL('../../tests/fixtures/docx_parts/base.json', import.meta.url)));
const bytes = Buffer.from(extraction.text), originalHash = extraction.source.sha256;
const textHash = createHash('sha256').update(bytes).digest('hex');
const clone = v => structuredClone(v);
const derivation = (original = originalHash) => ({ schema: 'loom.docx_source_derivation/1',
  original_source_id: 'src_docx_' + original, original_sha256: original, original_size: 1823,
  text_sha256: textHash, extraction_blob: 'c'.repeat(64), transform_sha256: 'd'.repeat(64),
  transform: { schema: 'loom.docx_source_transform/1', extractor: 'loom.docx_basic/1', limits: { max_entries: 2048 } } });
const source = (id = 'derived_a', original = originalHash, key = 'upload:docx', previous = '') => ({
  id, title: 'żółw.docx', blob_hash: textHash, parser: 'loom.source_parts', format: 'docx',
  metadata: { source_key: key, previous_source_id: previous, coordinate: 'derived_utf8_bytes', derivation: derivation(original) } });
const upload = (original = originalHash, key = 'upload:docx', previous = '') => ({
  id: 'upload_' + original, title: 'żółw.docx', blob_hash: original, parser: 'loom.client_upload', format: 'docx',
  metadata: { source_key: key, previous_source_id: previous } });
const parts = (s = source()) => ({ schema: 'loom.source_text_parts/1', source_id: s.id, source_sha256: s.blob_hash,
  source_node: 'n_' + s.id, projection_node: 'projection_' + s.id, version_node: 'version_' + s.id,
  coordinate: 'derived_utf8_bytes', status: 'partial', reason: 'docx_basic_recognized_blocks_only',
  derivation: clone(s.metadata.derivation), method: { level: 'recognized_block', derivation: clone(s.metadata.derivation) },
  original_source_node: 'n_' + s.metadata.derivation.original_source_id, warnings: clone(extraction.warnings),
  occurrence_nodes: extraction.blocks.map((_, i) => s.id + '_occ_' + i),
  observations: extraction.blocks.map((b, i) => ({ ordinal: i, text: b.text,
    locator: { source: s.id, byte_start: extraction.projection.ranges[i].text_range[0], byte_len: Buffer.byteLength(b.text) },
    attrs: { recognized_kind: b.kind, original_locator: b.locator, evidence_class: 'derived', coordinate: 'derived_utf8_bytes' } })) });
const occurrence = (i, s = source()) => {
  const range = extraction.projection.ranges[i].byte_range;
  return { node_id: parts(s).occurrence_nodes[i], text: bytes.subarray(...range).toString(), source: clone(s), provenance: {
    schema: 'loom.source_text_occurrence/1', source_id: s.id, source_sha256: s.blob_hash, coordinate: 'derived_utf8_bytes',
    byte_range: range, index: i, derivation: clone(s.metadata.derivation) } };
};
const merge = (target, patch) => {
  const out = { ...target };
  for (const [key, value] of Object.entries(patch)) out[key] = value && typeof value === 'object' && !Array.isArray(value) ? merge(out[key], value) : value;
  return out;
};

test('DOCX upload retains stable key and original hash; materialization is a separate request with format docx', () => {
  const raw = upload();
  assert.deepEqual(textSources([raw]), [raw]);
  assert.equal(sourceDerivation(raw), null);
  assert.deepEqual(materializationRequest(raw, 'pl'), { hash: originalHash, source_key: 'upload:docx', name: 'żółw.docx', format: 'docx', language: 'pl' });
});
test('upload collapses only with its key + ORIGINAL hash, independently of descriptor order', () => {
  const made = source(), raw = upload();
  for (const rows of [[raw, made], [made, raw]]) assert.deepEqual(textSources(rows), [made]);
  assert.equal(sourceOriginalHash(made), originalHash);
  assert.notEqual(sourceOriginalHash(made), made.blob_hash);
  assert.equal(textSources([raw, source('other', originalHash, 'upload:other')]).length, 2);
});
test('equal text from different ZIP versions keeps derived IDs, occurrences and originals distinct', () => {
  const a = source(), b = source('derived_b', 'b'.repeat(64), 'upload:docx', a.id);
  assert.equal(a.blob_hash, b.blob_hash);
  assert.deepEqual(textSources([upload(), a, upload('b'.repeat(64), 'upload:docx', a.id), b]), [a, b]);
  assert.notEqual(parts(a).occurrence_nodes[0], parts(b).occurrence_nodes[0]);
  assert.equal(materializationRequest(b, 'pl').previous_source_id, a.id);
});
test('another extraction receipt for one original remains visible; raw DOCX Source is never a revision choice', () => {
  const a = source(), b = source('new_reader_receipt'); b.metadata.derivation.extraction_blob = 'e'.repeat(64);
  assert.equal(textSources([a, b, { ...upload(), id: 'src_docx_' + originalHash, parser: 'docx_basic' }]).length, 2);
});
test('derived rematerialization sends original ZIP hash, same key and previous DERIVED source', () => {
  const made = source('derived_b', 'b'.repeat(64), 'upload:docx', 'derived_a');
  const request = materializationRequest(made, 'pl');
  assert.equal(request.hash, 'b'.repeat(64)); assert.equal(request.format, 'docx');
  assert.equal(request.previous_source_id, 'derived_a'); assert.equal(request.source_key, 'upload:docx');
  assert.deepEqual(materializationRequest(made, 'pl'), request);
});
test('invalid derivation cannot masquerade as raw UTF-8 or an available DOCX version', () => {
  for (const change of [s => delete s.metadata.derivation, s => s.metadata.coordinate = 'source_utf8_bytes',
    s => s.metadata.derivation.text_sha256 = originalHash, s => s.metadata.derivation.original_source_id = s.id,
    s => s.metadata.derivation.original_sha256 = 'not-a-hash', s => s.metadata.derivation.original_size = -1]) {
    const s = source(); change(s); assert.throws(() => sourceOriginalHash(s), /derivation_invalid/); assert.deepEqual(textSources([s]), []);
  }
});
test('projection accepts derived coordinates, mapped block types, empty blocks and warnings', () => {
  const p = readSourceParts(parts(), source());
  assert.equal(p.status, 'partial'); assert.equal(p.occurrence_nodes.length, 11);
  assert.deepEqual([...new Set(p.observations.map(b => b.attrs.recognized_kind))], ['paragraph', 'heading', 'list_item', 'table']);
  assert.equal(p.observations.filter(b => b.text === '').length, 4);
  assert.deepEqual(p.warnings, extraction.warnings);
});
test('projection refuses original hash/coordinates, cross-derivation and malformed mapped observations', () => {
  for (const change of [p => p.source_sha256 = originalHash, p => p.coordinate = 'source_utf8_bytes',
    p => p.derivation.original_sha256 = 'f'.repeat(64), p => p.method.derivation = {}, p => p.warnings = [3],
    p => p.observations[0].attrs.coordinate = 'zip_bytes', p => p.observations[1].ordinal = 0,
    p => p.observations[0].locator.byte_len = 1, p => p.occurrence_nodes.push(p.occurrence_nodes[0])]) {
    const p = parts(); change(p); assert.throws(() => readSourceParts(p, source()), /projection_invalid/);
  }
});
test('equal paragraphs and empty final block retain separate occurrence IDs and exact derived UTF-8 ranges', () => {
  const a = occurrence(0), b = occurrence(1), empty = occurrence(10);
  assert.equal(a.text, b.text); assert.notEqual(a.node_id, b.node_id); assert.notDeepEqual(a.provenance.byte_range, b.provenance.byte_range);
  for (const r of [a, b, empty]) assert.deepEqual(readSourceOccurrence(r, source(), r.node_id), r);
  assert.equal(empty.text, ''); assert.deepEqual(empty.provenance.byte_range, [112, 112]);
});
test('preview refuses ZIP coordinates, wrong range length, wrong occurrence/key and mismatched original', () => {
  for (const change of [r => r.provenance.coordinate = 'source_utf8_bytes', r => r.provenance.source_sha256 = originalHash,
    r => r.provenance.byte_range = [0, 1], r => r.node_id = 'another', r => r.source.metadata.source_key = 'foreign',
    r => r.provenance.derivation.original_sha256 = 'f'.repeat(64)]) {
    const r = occurrence(0); change(r); assert.throws(() => readSourceOccurrence(r, source(), 'derived_a_occ_0'), /occurrence_invalid/);
  }
});
test('materialization resolves returned Source ID rather than first same-text version, validates predecessor and original', () => {
  const a = source(), b = source('derived_b', 'b'.repeat(64), 'upload:docx', a.id), raw = upload('b'.repeat(64), 'upload:docx', a.id);
  assert.equal(materializedSource([a, b, raw], parts(b), raw).id, b.id);
  assert.throws(() => materializedSource([a], parts(a), raw), /descriptor_missing/);
  const wrongParent = clone(b); wrongParent.metadata.previous_source_id = 'src_docx_' + originalHash;
  assert.throws(() => materializedSource([wrongParent], parts(wrongParent), raw), /descriptor_missing/);
  assert.throws(() => materializedSource([], parts(b), raw), /descriptor_missing/);
});
test('DOCX selection preserves other seed IDs, all budgets, hops and execution methods; repeated add is idempotent', async () => {
  let cfg = { context_execution: { unified: { enabled: false, legacy_seed_ids: ['existing'], budget_tokens: 9100, include_knowledge: false },
    graph_reply: { mode: 'off' }, goal_typing: { calls_authorized: false }, custom: ['kept'] }, model: 'unchanged' };
  const before = clone(cfg), id = occurrence(1).node_id;
  const host = { getConfig: async () => clone(cfg), mergeConfigKey: async (key, patch) => { cfg[key] = merge(cfg[key], patch); return clone(cfg); } };
  await saveSourceSelection(host, id, true, 0); const saved = await saveSourceSelection(host, id, true, 0);
  const expected = clone(before); Object.assign(expected.context_execution.unified, { enabled: true, legacy_seed_ids: ['existing', id], legacy_relation_hops: 0 });
  assert.deepEqual(saved, expected); assert.deepEqual(sourceSelector(saved).ids, ['existing', id]);
});
test('refusal/readback failure never mutates the previous local selection or claims a confirmed save', async () => {
  const before = { context_execution: { unified: { enabled: true, legacy_seed_ids: ['old'], legacy_relation_hops: 2 } } };
  const host = { getConfig: async () => clone(before), mergeConfigKey: async () => { throw Error('synthetic_native_refusal'); } };
  await assert.rejects(saveSourceSelection(host, occurrence(0).node_id, true, 0), /native_refusal/);
  await assert.rejects(saveSourceSelection({ ...host, mergeConfigKey: async () => ({}) }, occurrence(0).node_id, true, 0), /readback_mismatch/);
  assert.deepEqual(sourceSelector(before).ids, ['old']);
});
test('task gate blocks repeated clicks, rejects stale writes after reopen and keeps newer task locked', async () => {
  const gate = createSourceTaskGate(), old = gate.begin(); assert.ok(old); assert.equal(gate.begin(), null);
  let finishRead, writes = 0;
  const pending = saveSourceSelection({ getConfig: () => new Promise(r => { finishRead = r; }), mergeConfigKey: async () => { writes++; } },
    occurrence(0).node_id, true, 0, old.isCurrent);
  gate.invalidate(); const reopened = gate.begin(); old.finish();
  assert.equal(old.isCurrent(), false); assert.equal(reopened.isCurrent(), true); assert.equal(gate.begin(), null);
  finishRead({}); await assert.rejects(pending, /selection_stale/); assert.equal(writes, 0);
  reopened.finish(); assert.ok(gate.begin());
});
test('Polish document explanation, known warning translations and DOCX preset are data; PDF/PNG remain opaque', () => {
  const words = JSON.parse(readFileSync(new URL('../src/content/text-sources.words.json', import.meta.url)));
  assert.deepEqual(words.formats.docx, ['docx']);
  for (const ext of ['pdf', 'png']) assert.ok(!Object.values(words.formats).flat().includes(ext));
  for (const code of extraction.warnings) assert.ok(words.warnings[code].pl);
  assert.match(words.scope.pl, /wszystkich rozmowach/); assert.match(words.scope.pl, /równoczesnych/);
  assert.match(words.derivedAbout.pl, /UTF-8/); assert.match(words.derivedRange.pl, /odczytanego tekstu/);
});
