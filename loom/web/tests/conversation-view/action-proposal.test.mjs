// Command proposed in the conversation (R54): pure view/answer logic, data texts, and the rendered block.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const a = await loader.load('src/context/action-proposal.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
const block = await loader.load('src/components/ActionProposalBlock.tsx');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const generated = JSON.parse(await readFile(new URL('../../src/onboarding/generated/conversation-view.json', import.meta.url), 'utf8'));
const entry = pack.entries.find(row => row.key === 'presentation.action_proposal');
const en = entry.value.locales.en;
const pl = entry.value.locales.pl;
const feature = p.resolvePresentationFeature(p.resolvePresentation(), 'action_proposal');

const proposal = { schema: 'loom.action_proposal/1', status: 'pending', run_id: 'ar_1', receipt_id: 'arr_1', title: 'Summarise', lang: 'en',
  items: [{ action: 'transcribe', title: 'Transcribe', cost: 1 }, { action: 'summarise', title: 'Summarise', cost: 1 }],
  cost: { declared: 2, model_calls: 1, input_bytes: 1234 },
  needs_confirmation: [{ action: 'share', title: 'Share', reason: 'ask', initiator: 'owner' }], runnable: true };

test('generated web catalog carries the proposal texts, in both languages', () => {
  assert.deepEqual(generated.entries.find(row => row.key === 'presentation.action_proposal').value, entry.value);
  assert.ok(en.confirm && pl.confirm && en.reject && pl.reject);
  assert.notEqual(en.title, pl.title);
});

test('a message asks for an answer in exactly three situations, and the answer carries the receipt', () => {
  const asked = a.pendingAnswer({ action_proposal: proposal });
  assert.equal(asked.kind, 'proposal');
  assert.equal(asked.runId, 'ar_1');
  assert.equal(asked.receiptId, 'arr_1');
  assert.deepEqual(a.answerRequest(asked, true, 'web'), { run_id: 'ar_1', approved: true, receipt_id: 'arr_1', ref: 'web' });
  assert.equal(a.answerRequest(asked, false, 'web').approved, false);
  const rules = a.pendingAnswer({ action_run: { run_id: 'ar_2', status: 'awaiting_confirmation', pending: { kind: 'confirmation', receipt_id: 'arr_2' } } });
  assert.equal(rules.kind, 'confirmation');
  assert.equal(rules.receiptId, 'arr_2');
  const usage = a.pendingAnswer({ action_run: { run_id: 'ar_3', status: 'awaiting_usage',
    pending: { kind: 'usage', receipt_id: 'usage_9', operation_id: 'op', usage_pending: { receipt_id: 'usage_9', projected: { input_bytes: { projected: 60000 } } } } } });
  assert.equal(usage.kind, 'usage');
  assert.equal(usage.receiptId, 'usage_9');
  assert.equal(usage.usage.projected.input_bytes.projected, 60000);
});

test('answered, denied, stale or unknown proposals ask nothing; their state is a badge', () => {
  for (const status of ['executed', 'rejected', 'denied', 'unavailable', 'stale', 'waiting']) {
    assert.equal(a.pendingAnswer({ action_proposal: { ...proposal, status } }), null);
    assert.equal(a.proposalState({ action_proposal: { ...proposal, status } }), status);
  }
  assert.equal(a.pendingAnswer({ action_proposal: { status: 'pending' } }), null, 'a proposal without a run or receipt cannot be answered');
  assert.equal(a.pendingAnswer(undefined), null);
  assert.equal(a.pendingAnswer({}), null);
  assert.equal(a.pendingAnswer({ action_run: { run_id: 'ar_4', status: 'completed', pending: null } }), null);
  assert.equal(a.proposalState({ action_proposal: proposal }), null);
});

test('view shows the plan, the estimate and what the rules ask; unknown numbers are unknown, never zero', () => {
  const view = a.proposalView(a.pendingAnswer({ action_proposal: proposal }));
  assert.equal(view.answerable, true);
  assert.equal(view.plan, 'Transcribe -> Summarise');
  assert.deepEqual(view.cost, { cost: '2', calls: '1', bytes: '1234' });
  assert.deepEqual(view.confirmations, ['Share']);
  const unknown = a.proposalView(a.pendingAnswer({ action_proposal: { ...proposal, cost: { declared: null, model_calls: 1, input_bytes: null } } }));
  assert.equal(unknown.cost.cost, null);
  const texts = a.proposalTexts(feature, a.pendingAnswer({ action_proposal: { ...proposal, cost: { declared: null, model_calls: 1, input_bytes: null } } }), unknown);
  assert.match(texts.cost, new RegExp(en.unknown));
  assert.doesNotMatch(texts.cost, /\b0\b/);
});

test('texts come from the catalog; without one the machine names are shown', () => {
  const answer = a.pendingAnswer({ action_proposal: proposal });
  const view = a.proposalView(answer);
  const texts = a.proposalTexts(feature, answer, view);
  assert.equal(texts.title, en.title);
  assert.equal(texts.confirm, en.confirm);
  assert.equal(texts.reject, en.reject);
  assert.match(texts.plan, /Transcribe -> Summarise/);
  assert.match(texts.cost, /2.*1.*1234/);
  assert.match(texts.confirmNeeded, /Share/);
  assert.equal(texts.status('executed'), en.status_executed);
  assert.equal(texts.failed('offline'), en.answer_failed.replace('{{reason}}', 'offline'));
  const rules = a.proposalTexts(feature, a.pendingAnswer({ action_run: { run_id: 'ar_2', status: 'awaiting_confirmation', pending: { kind: 'confirmation', receipt_id: 'arr_2' } } }), view);
  assert.equal(rules.title, en.title_confirmation);
  const bare = a.proposalTexts(null, answer, view);
  assert.equal(bare.title, null);
  assert.equal(bare.confirm, null);
  assert.equal(bare.failed('offline'), 'offline');
});

test('a chat request is re-sent with the router\'s own usage answer, and nothing else changes', () => {
  const request = { message: 'hello', conv_id: 'c' };
  const yes = a.routeConfirmedRequest(request, { receipt_id: 'usage_1', operation_id: 'op1' }, true, 'web');
  assert.deepEqual(yes, { ...request, route_usage_confirmation: { approved: true, receipt_id: 'usage_1', operation_id: 'op1', ref: 'web' } });
  assert.equal('usage_confirmation' in yes, false, 'the chat itself is not confirmed by the router\'s answer');
  assert.deepEqual(request, { message: 'hello', conv_id: 'c' });
  assert.equal(a.ACTION_PROPOSAL_CHUNK, 'action_proposal');
  assert.equal(a.ACTION_RESULT_CHUNK, 'action_result');
});

test('the rendered block shows the question, the estimate and both answers', () => {
  const answer = a.pendingAnswer({ action_proposal: proposal });
  const html = renderToStaticMarkup(createElement(block.default, { feature, answer, testId: 'x', onConfirm: () => undefined, onReject: () => undefined }));
  assert.match(html, /data-testid="x"/);
  assert.match(html, /data-run-id="ar_1"/);
  assert.match(html, /data-receipt-id="arr_1"/);
  assert.match(html, /data-kind="proposal"/);
  assert.ok(html.includes(en.title));
  assert.ok(html.includes('Transcribe -&gt; Summarise'));
  assert.match(html, /data-testid="x-cost"/);
  assert.match(html, /data-testid="x-confirm-needed"/);
  assert.match(html, new RegExp('data-testid="x-confirm"[^>]*>' + en.confirm));
  assert.match(html, new RegExp('data-testid="x-reject"[^>]*>' + en.reject));
  assert.doesNotMatch(html, /disabled/);
  assert.doesNotMatch(html, /x-failure/);
  // busy: both answers are disabled; a failed answer is shown
  const busy = renderToStaticMarkup(createElement(block.default, { feature, answer, busy: true, failure: 'offline', testId: 'x', onConfirm: () => undefined, onReject: () => undefined }));
  assert.equal((busy.match(/disabled/g) || []).length, 2);
  assert.ok(busy.includes('offline'));
  assert.match(busy, /role="alert"/);
  // without a catalog the machine names show instead of words
  const bare = renderToStaticMarkup(createElement(block.default, { feature: null, answer, testId: 'x', onConfirm: () => undefined, onReject: () => undefined }));
  assert.ok(bare.includes('arr_1'));
  assert.ok(bare.includes('ar_1'));
});

test('a block that cannot be answered offers disabled buttons', () => {
  const answer = { kind: 'proposal', runId: '', receiptId: '', proposal: null, usage: null };
  const html = renderToStaticMarkup(createElement(block.default, { feature, answer, testId: 'y', onConfirm: () => undefined, onReject: () => undefined }));
  assert.equal((html.match(/disabled/g) || []).length, 2);
});
