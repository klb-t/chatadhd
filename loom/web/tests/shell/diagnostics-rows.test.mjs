// R67 (Diagnostyka): the sheet lists only messages that carry a recorded context or a route trace, with the checksum as its own line.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const m = await loader.load('src/components/DiagnosticsSheet.tsx');
const msgs = [
  { id: 'm_a', role: 'user', text: 'x', status: 'active', created: null, metadata: {} },
  { id: 'm_b', role: 'assistant', model: 'p/model', text: 'y', status: 'active', created: null, metadata: { context_trace: { kind: 'compiled_messages', version: 1, messages: [], messages_sha256: 'sha1' } } },
  { id: 'm_c', role: 'assistant', text: 'z', status: 'active', created: null, metadata: { route_trace: { stage: 'rule' } } },
  { id: 'm_d', role: 'assistant', text: 'w', status: 'active', created: null, metadata: { context_trace: { kind: 'other' } } },
];
test('only messages with a record are listed, in the order of the conversation', () => {
  const rows = m.diagnosticRows(msgs);
  assert.deepEqual(rows.map(r => r.id), ['m_b', 'm_c']);
  assert.equal(rows[0].sha, 'sha1');
  assert.equal(rows[0].model, 'p/model');
  assert.equal(rows[1].sha, null);
  assert.deepEqual(rows[1].route, { stage: 'rule' });
});
