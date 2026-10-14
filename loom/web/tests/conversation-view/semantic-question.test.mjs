// P17 3: the x10 question of the background analysis is asked in the conversation. Pure view logic and the data texts; the kernel side is
// tests/test_semantic_question.cpp (rendered path: the block of ChatView, the usage block it reuses is covered by usage-pending.test.mjs).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const q = await loader.load('src/context/semantic-question.ts');
const u = await loader.load('src/context/usage-pending.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const generated = JSON.parse(await readFile(new URL('../../src/onboarding/generated/conversation-view.json', import.meta.url), 'utf8'));
const entry = pack.entries.find(row => row.key === 'presentation.semantic_question');
const feature = p.resolvePresentationFeature(p.resolvePresentation(), 'semantic_question');

const receipt = { schema: 'loom.semantic_usage_pending/1', status: 'requires_confirmation', operation_id: 'model_usage_1', receipt_id: 'usage_abc',
  baseline_key: 'semantic.batch|https://x|m', messages: 61, projected: { messages: { projected: 61, baseline: 1, ratio: 61, requires_confirmation: true } } };
const asking = { semantic_question: { status: 'pending', lang: 'pl', kind: 'batch', receipt_id: 'usage_abc', operation_id: 'model_usage_1', ratio: 61, receipt } };

test('the generated web catalog carries the texts, in both languages, with the same keys', () => {
  assert.deepEqual(generated.entries.find(row => row.key === 'presentation.semantic_question').value, entry.value);
  assert.deepEqual(Object.keys(entry.value.locales.pl).sort(), Object.keys(entry.value.locales.en).sort());
  for (const id of ['title', 'explanation', 'confirm', 'reject', 'status_approved', 'status_declined', 'status_stale']) assert.ok(entry.value.locales.en[id], id);
});

test('a message asks when it carries the receipt of the guard; without one it is not a question', () => {
  const question = q.semanticQuestion(asking);
  assert.equal(question.status, 'pending');
  assert.equal(question.lang, 'pl');
  assert.equal(question.kind, 'batch');
  assert.equal(question.receiptId, 'usage_abc');
  assert.equal(q.semanticQuestionOpen(question), true);
  assert.equal(q.semanticQuestion(undefined), null);
  assert.equal(q.semanticQuestion({}), null);
  assert.equal(q.semanticQuestion({ semantic_question: { status: 'pending' } }), null);
  assert.equal(q.semanticQuestion({ semantic_question: null }), null);
});

test('the receipt is the one the usage block shows: both identifiers, the growth lines, nothing invented', () => {
  const view = u.usagePendingView(q.semanticQuestion(asking).receipt);
  assert.equal(view.answerable, true);
  assert.deepEqual(view.lines.map(line => line.name), ['messages']);
  assert.equal(view.lines[0].ratio, '61');
  const lost = q.semanticQuestion({ semantic_question: { status: 'pending', receipt_id: 'usage_abc', receipt: { receipt_id: null } } });
  assert.equal(lost.receipt.receipt_id, 'usage_abc');
  assert.equal(u.usagePendingView(lost.receipt).answerable, false, 'without the operation id the question cannot be answered here');
});

test('a question that was answered or is out of date stops asking and says how it ended, in the words of the catalog', () => {
  for (const [status, id] of [['approved', 'status_approved'], ['declined', 'status_declined'], ['stale', 'status_stale']]) {
    const question = q.semanticQuestion({ semantic_question: { ...asking.semantic_question, status } });
    assert.equal(q.semanticQuestionOpen(question), false, status);
    assert.equal(q.semanticAnsweredText(feature, question), entry.value.locales.en[id]);
    assert.equal(q.semanticAnsweredText(null, question), null);
  }
  assert.equal(q.semanticAnsweredText(feature, q.semanticQuestion(asking)), null, 'a pending question has no ending to name');
  assert.deepEqual(q.SEMANTIC_ANSWERED, ['approved', 'declined', 'stale']);
});

test('the lines the page adds are in the language of the question', () => {
  const question = q.semanticQuestion(asking);
  const pl = p.featureInLanguage(feature, question.lang);
  const text = q.semanticAnsweredText(pl, q.semanticQuestion({ semantic_question: { ...asking.semantic_question, status: 'declined' } }));
  assert.equal(text, entry.value.locales.pl.status_declined);
  assert.match(q.semanticFailedText(pl, 'HTTP 409'), /HTTP 409/);
  assert.equal(q.semanticFailedText(null, 'HTTP 409'), 'HTTP 409');
});

test('the kernel event names ids only; other events and payloads without ids are not questions', () => {
  assert.equal(q.SEMANTIC_QUESTION_EVENT, 'semantic:question');
  const envelope = (payload, event = 'semantic:question') => ({ event, payload });
  const waiting = { conv_id: 'c_1', message_id: 'm_9', receipt_id: 'usage_abc', state: 'waiting' };
  assert.deepEqual(q.semanticQuestionEvent(envelope(waiting)), { convId: 'c_1', messageId: 'm_9', receiptId: 'usage_abc', state: 'waiting' });
  assert.deepEqual(q.semanticQuestionEvent(envelope({ ...waiting, state: 'closed', status: 'approved' })), { convId: 'c_1', messageId: 'm_9', receiptId: 'usage_abc', state: 'closed' });
  assert.equal(q.semanticQuestionEvent(envelope(waiting, 'action:client_step')), null);
  assert.equal(q.semanticQuestionEvent(envelope({ conv_id: 'c_1' })), null);
  assert.equal(q.semanticQuestionEvent(envelope(null)), null);
  assert.equal(q.semanticQuestionEvent(null), null);
});

test('one feed, the question event only; a transport without a feed is not watched', () => {
  assert.equal(q.watchSemanticQuestions({}, () => assert.fail('no feed')), null);
  const opened = [];
  const source = { subscribeEvents: (event, handlers) => { const row = { event, handlers, closed: false }; opened.push(row); return () => { row.closed = true; }; } };
  const seen = [];
  const stop = q.watchSemanticQuestions(source, (event) => seen.push(event.state));
  assert.equal(opened.length, 1);
  assert.equal(opened[0].event, 'semantic:question');
  opened[0].handlers.onChunk({ event: 'semantic:question', payload: { conv_id: 'c_1', message_id: 'm_1', receipt_id: 'r', state: 'waiting' } });
  opened[0].handlers.onChunk({ event: 'action:client_step', payload: { conv_id: 'c_1', message_id: 'm_1', run_id: 'ar_1', state: 'waiting' } });
  opened[0].handlers.onChunk({ event: 'semantic:question', payload: { conv_id: 'c_1', message_id: 'm_1', receipt_id: 'r', state: 'closed' } });
  assert.deepEqual(seen, ['waiting', 'closed']);
  stop();
  assert.equal(opened[0].closed, true);
});
