// One synthetic picker/transport fixture. No native reader, build or browser.
// Before integration: node this-file --panel /tmp/csv-ast-picker-SourcePartsPanel.tsx
// After integration: node this-file (reads the actual integrated panel).
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import { materializationRequest, materializedSource, textSources } from '../src/api/text-sources-state.mjs';

const words = JSON.parse(readFileSync(new URL('../src/content/text-sources.words.json', import.meta.url)));
const panelFlag = process.argv.indexOf('--panel');
const panel = readFileSync(panelFlag < 0 ? new URL('../src/components/SourcePartsPanel.tsx', import.meta.url) : process.argv[panelFlag + 1], 'utf8');
const uploadHandler = panel.match(/data-testid="source-upload-file"[^]*?onChange=\{e => \{([^]*?)\}\} \/>/)?.[1];
const materializeHandler = panel.match(/const materialize = async \(isCurrent: \(\) => boolean\) => \{([^]*?)\n  \};/)?.[1];
assert.ok(uploadHandler, 'actual picker file handler');
assert.ok(materializeHandler, 'actual materialization handler');

const source = format => ({ id: 'upload:' + format, title: 'żółw.' + (format === 'csv' ? 'csv' : 'py'),
  blob_hash: 'a'.repeat(64), format, parser: 'loom.client_upload', metadata: { source_key: 'fixture:' + format } });
const receipt = s => ({ schema: 'loom.source_text_parts/1', source_id: 'made:' + s.format,
  source_sha256: s.blob_hash, source_node: 'n_source', projection_node: 'n_projection', version_node: 'n_version',
  coordinate: 'source_utf8_bytes', occurrence_nodes: ['n_occurrence'], method: { format: s.format },
  status: 'partial', reason: 'synthetic_fixture_only' });

async function materialize(s, action) {
  const writes = [], requests = [];
  const made = { ...s, id: 'made:' + s.format, parser: 'loom.source_parts' };
  const api = { actions: action ? { clientBlobText: async request => { requests.push(request); return action(request); } } : {},
    listSources: async () => { writes.push('reload'); return [made]; } };
  // Execute the panel's actual handler with a synthetic transport and state sinks.
  const run = new Function('api', 'source', 'language', 'limit', 'w', 'materializationRequest', 'materializedSource',
    'textSources', 'setSources', 'setRecordCount', 'setSelected',
    'return async isCurrent => {' + materializeHandler + '}')(
    api, s, 'pl', '100', key => words[key].pl, materializationRequest, materializedSource, textSources,
    rows => writes.push(rows), count => writes.push(count), id => writes.push(id));
  try { await run(() => true); } catch (error) { return { error, writes, requests }; }
  return { writes, requests };
}

test('picker detects CSV/Python AST from data; code and original-only choices remain', () => {
  let chosen, file;
  const choose = new Function('e', 'words', 'setFile', 'uploadKey', 'crypto', 'setFormat', uploadHandler);
  for (const [name, expected] of [['ŻÓŁW.CSV', 'csv'], ['żółw.PY', 'python_ast'], ['main.cpp', 'code'], ['obraz.png', '']]) {
    choose({ target: { files: [{ name }] } }, words, value => { file = value; }, { current: '' },
      { randomUUID: () => 'synthetic' }, value => { chosen = value; });
    assert.equal(chosen, expected); assert.equal(file.name, name);
  }
  assert.ok(Object.hasOwn(words.formats, 'code'), 'plain code stays in the same dropdown');
  assert.match(panel, /Object\.keys\(words\.formats\)[^\n]*<option key=\{f\} value=\{f\}/);
  assert.match(panel, /<option value="">\{w\("opaque"\)\}/);
  assert.match(panel, /disabled=\{!Object\.keys\(words\.formats\)\.includes\(source\.format\) \|\| !api\.actions\?\.clientBlobText\}/);
});

test('existing materialization request preserves source identity and original UTF-8 coordinates', async () => {
  for (const format of ['csv', 'python_ast', 'code']) {
    const s = source(format), before = structuredClone(s), result = receipt(s);
    const { writes, requests, error } = await materialize(s, async () => result);
    assert.equal(error, undefined);
    assert.deepEqual(requests, [{ hash: s.blob_hash, source_key: s.metadata.source_key, name: s.title, format,
      ...(format === 'code' ? { language: 'pl' } : {}) }]);
    assert.equal(writes.at(-1), result.source_id);
    assert.deepEqual(s, before);
  }
});

test('unavailable helper/transport and SyntaxError stay errors without fallback or changed identity', async () => {
  for (const [format, code] of [['csv', 'docx_reader_unavailable'], ['python_ast', 'host_reader_format_unavailable'],
    ['python_ast', 'python_ast_syntax_error']]) {
    const s = source(format), before = structuredClone(s);
    const { error, writes, requests } = await materialize(s, async () => { throw Error(code); });
    assert.equal(error.message, code); assert.deepEqual(writes, []);
    assert.equal(requests.length, 1); assert.equal(requests[0].format, format);
    assert.deepEqual(s, before);
  }
  const missing = await materialize(source('python_ast'));
  assert.equal(missing.error.message, words.unsupported.pl);
  assert.deepEqual(missing.requests, []); assert.deepEqual(missing.writes, []);
});

test('hook connects data descriptions to picker and materialization without claiming helper availability', () => {
  assert.match(panel, /words\[\(value \+ "About"\) as Word\]/);
  for (const value of ['format', 'source.format']) assert.ok(panel.includes('{formatHelp(' + value + ') && <p'));
  assert.ok(panel.includes('"structuredCapability" : "unsupported"'));
  for (const format of ['csv', 'python_ast']) for (const language of ['pl', 'en']) {
    assert.ok(words[format][language]); assert.ok(words[format + 'About'][language]);
  }
  assert.match(words.python_astAbout.pl, /SyntaxError \(python_ast_syntax_error\)/);
  assert.match(words.structuredCapability.pl, /nie potwierdza dostępności helpera/);
  // Existing run() shows the transport error unchanged; no successful-result path on rejection.
  assert.match(panel, /setError\(e instanceof Error \? e\.message : String\(e\)\)/);
});
