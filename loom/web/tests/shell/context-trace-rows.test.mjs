// R67 (Warsztat context block): the recorded context is shown as rows of words; the raw JSON is not a row.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const m = await loader.load('src/components/context-trace-rows.ts');
const words = { ctx_row_messages: 'Złożone wiadomości: {{count}}', ctx_row_roles: 'Według roli: {{list}}', ctx_row_history: 'Z historii rozmowy: {{count}}', ctx_row_knowledge: 'Wybór wiedzy: pozycji {{count}}' };
const t = (id, p = {}) => (words[id] ?? id).replace(/\{\{(\w+)\}\}/g, (_, k) => p[k] ?? '');
const fixture = { kind: 'compiled_messages', version: 1, messages: [{ role: 'system', content: 'a' }, { role: 'user', content: 'b' }, { role: 'user', content: 'c' }],
  messages_sha256: 'abc123', history_message_ids: ['m_1', 'm_2'], knowledge_context: { items: [{}, {}, {}, {}] } };

test('rows come from the receipt in words and carry no JSON, ids or checksum', () => {
  const rows = m.contextTraceRows(fixture, t);
  assert.deepEqual(rows.map(r => r.id), ['messages', 'roles', 'history', 'knowledge']);
  assert.equal(rows[0].text, 'Złożone wiadomości: 3');
  assert.equal(rows[1].text, 'Według roli: system 1 · user 2');
  assert.equal(rows[2].text, 'Z historii rozmowy: 2');
  assert.equal(rows[3].text, 'Wybór wiedzy: pozycji 4');
  for (const r of rows) { assert.ok(!/[{}\[\]]/.test(r.text), r.text); assert.ok(!r.text.includes('abc123')); assert.ok(!r.text.includes('m_1')); }
});
test('a row appears only for a fact the record carries', () => {
  const rows = m.contextTraceRows({ kind: 'compiled_messages', version: 1, messages: [] }, t);
  assert.deepEqual(rows.map(r => r.id), ['messages']);
  assert.equal(rows[0].text, 'Złożone wiadomości: 0');
});
