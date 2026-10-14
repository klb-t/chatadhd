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

test('money is a range where the price is listed and unknown, with its reason, where it is not: never zero', () => {
  const known = { ...proposal, cost: { declared: 1, model_calls: 1, input_bytes: 4000, money: { known: true, low: 0.00244, high: 0.008053, currency: 'USD' } } };
  const answer = a.pendingAnswer({ action_proposal: known });
  const view = a.proposalView(answer);
  assert.deepEqual(view.money, { known: true, low: '0.0024', high: '0.0081', currency: 'USD', reason: '' });
  const texts = a.proposalTexts(feature, answer, view);
  assert.equal(texts.money, en.cost_money.replace('{{low}}', '0.0024').replace('{{high}}', '0.0081').replace('{{currency}}', 'USD'));
  const unknownMoney = { ...proposal, cost: { declared: 1, model_calls: 1, input_bytes: 4000, money: { known: false, currency: 'USD', reason: 'the registry lists no price for the model' } } };
  const unknownAnswer = a.pendingAnswer({ action_proposal: unknownMoney });
  const unknownView = a.proposalView(unknownAnswer);
  assert.equal(unknownView.money.known, false);
  assert.equal(unknownView.money.low, null);
  const unknownTexts = a.proposalTexts(feature, unknownAnswer, unknownView);
  assert.match(unknownTexts.money, /unknown/);
  assert.match(unknownTexts.money, /no price/);
  assert.doesNotMatch(unknownTexts.money, /\b0\b/);
  // known:true without numbers is not a known price
  const broken = a.proposalView(a.pendingAnswer({ action_proposal: { ...proposal, cost: { model_calls: 1, money: { known: true } } } }));
  assert.equal(broken.money.known, false);
  // a plan that calls no model says nothing about money; a proposal without the field neither
  assert.equal(a.proposalTexts(feature, answer, a.proposalView(a.pendingAnswer({ action_proposal: { ...known, cost: { ...known.cost, model_calls: 0 } } }))).money, null);
  assert.equal(a.proposalView(a.pendingAnswer({ action_proposal: proposal })).money, null);
  assert.equal(a.proposalTexts(feature, a.pendingAnswer({ action_proposal: proposal }), a.proposalView(a.pendingAnswer({ action_proposal: proposal }))).money, null);
  const html = renderToStaticMarkup(createElement(block.default, { feature, answer, testId: 'm', onConfirm: () => undefined, onReject: () => undefined }));
  assert.match(html, /data-testid="m-money"/);
  assert.ok(html.includes('0.0024'));
  const plain = renderToStaticMarkup(createElement(block.default, { feature, answer: a.pendingAnswer({ action_proposal: proposal }), testId: 'm', onConfirm: () => undefined, onReject: () => undefined }));
  assert.doesNotMatch(plain, /m-money/);
  assert.ok(pl.cost_money && pl.cost_money_unknown && pl.produced, 'the Polish strings exist');
});

test('a result message points at the graph nodes it presents', () => {
  const report = { run_id: 'ar_7', status: 'completed', produced: { target: 'graph', forms: ['n_form1'], content: 'n_content', run_node: 'n_run' } };
  const view = a.producedView({ action_run: report });
  assert.deepEqual(view, { target: 'graph', forms: ['n_form1'], content: 'n_content', runNode: 'n_run' });
  assert.equal(a.producedText(feature, view), en.produced.replace('{{target}}', 'graph').replace('{{nodes}}', 'n_form1'));
  assert.equal(a.producedText(null, view), null);
  // a result posted as a message of its own is itself the form node
  const posted = a.producedView({ action_result: { run_id: 'ar_8', content: 'n_c', run_node: 'n_r' } }, 'm_posted');
  assert.deepEqual(posted, { target: 'conversation_message', forms: ['m_posted'], content: 'n_c', runNode: 'n_r' });
  assert.equal(a.producedView({ action_result: { run_id: 'ar_8' } }), null, 'without the message id there is nothing to point at');
  // a run that produced nothing, a run in progress and an ordinary message point at nothing
  assert.equal(a.producedView({ action_run: { run_id: 'ar_9', status: 'completed' } }), null);
  assert.equal(a.producedView({ action_run: { ...report, produced: { target: 'run_only', forms: [] } } }), null);
  assert.equal(a.producedView({}), null);
  assert.equal(a.producedView(undefined), null);
  // the view of a pending question is unaffected
  assert.equal(a.pendingAnswer({ action_run: report }), null);
});

test('the arguments the words gave and the ones that keep their defaults are shown (P11)', () => {
  const read = { ...proposal, args: { target_language: 'German' }, params: [
    { name: 'target_language', required: false, given: true, value: 'German', from: 'phrase', default: 'the language of the command' },
    { name: 'style', required: false, given: false, default: 'light satire' },
    { name: 'model', required: true, given: false },
  ] };
  const answer = a.pendingAnswer({ action_proposal: read });
  const view = a.proposalView(answer);
  assert.deepEqual(view.args, ['target_language=German']);
  assert.deepEqual(view.open, ['style (light satire)']);  // a required parameter that is missing is the router's business, not a default
  const texts = a.proposalTexts(feature, answer, view);
  assert.equal(texts.args, en.args.replace('{{args}}', 'target_language=German'));
  assert.equal(texts.argsOpen, en.args_open.replace('{{params}}', 'style (light satire)'));
  const html = renderToStaticMarkup(createElement(block.default, { feature, answer, testId: 'a', onConfirm: () => undefined, onReject: () => undefined }));
  assert.match(html, /data-testid="a-args"/);
  assert.match(html, /data-testid="a-args-open"/);
  assert.ok(html.includes('target_language=German'));
  // a proposal without parameters says nothing about them
  const plain = a.proposalTexts(feature, a.pendingAnswer({ action_proposal: proposal }), a.proposalView(a.pendingAnswer({ action_proposal: proposal })));
  assert.equal(plain.args, null);
  assert.equal(plain.argsOpen, null);
  assert.ok(pl.args && pl.args_open && pl.rule_replaces);
});

test('a proposal that grows the catalogue says what it writes and where, in both languages (P11)', () => {
  for (const kind of ['define_action', 'define_sequence', 'define_group']) {
    assert.ok(en['definition_' + kind] && pl['definition_' + kind], kind);
    const grown = { ...proposal, kind, definition: { kind, id: 'rozpisz', overlay: 'profiles/actions.pack', node: 'n_1' } };
    const answer = a.pendingAnswer({ action_proposal: grown });
    const view = a.proposalView(answer);
    assert.deepEqual(view.definition, { kind, id: 'rozpisz', overlay: 'profiles/actions.pack' });
    const texts = a.proposalTexts(feature, answer, view);
    assert.equal(texts.title, en.title_definition);
    assert.equal(texts.definition, en['definition_' + kind].replace('{{id}}', 'rozpisz').replace('{{overlay}}', 'profiles/actions.pack'));
    const html = renderToStaticMarkup(createElement(block.default, { feature, answer, testId: 'g', onConfirm: () => undefined, onReject: () => undefined }));
    assert.match(html, /data-testid="g-definition"/);
    assert.ok(html.includes('profiles/actions.pack'));
    assert.match(html, /data-testid="g-confirm"/);  // still a yes/no: the definition is offered, never made
  }
  // an ordinary command has no definition line, and a replacement of an earlier rule is said
  assert.equal(a.proposalView(a.pendingAnswer({ action_proposal: proposal })).definition, null);
  const replaces = a.proposalTexts(feature, a.pendingAnswer({ action_proposal: { ...proposal, replaces: true } }), a.proposalView(a.pendingAnswer({ action_proposal: { ...proposal, replaces: true } })));
  assert.equal(replaces.replaces, en.rule_replaces);
});

test('a rejected command offers its sentence as a message: one open offer, one request, the states that follow (P12)', () => {
  const rejected = { ...proposal, status: 'rejected', offer: { action: 'send_as_message', status: 'open', message_id: 'm_sentence', mode: 'offer' } };
  assert.equal(a.pendingAnswer({ action_proposal: rejected }), null, 'a dropped command asks nothing');
  const open = a.openOffer({ action_proposal: rejected }, 'm_proposal');
  assert.deepEqual(open, { proposalId: 'm_proposal', sentenceId: 'm_sentence', auto: false });
  assert.deepEqual(a.openOffer({ action_proposal: { ...rejected, offer: { ...rejected.offer, mode: 'send_as_message' } } }, 'm_proposal'),
    { proposalId: 'm_proposal', sentenceId: 'm_sentence', auto: true });
  // sent, declined, none, or a proposal that is not rejected: nothing to take
  for (const offer of [{ ...rejected.offer, status: 'sent' }, { ...rejected.offer, status: 'declined' }, null, undefined, { status: 'open' }]) {
    assert.equal(a.openOffer({ action_proposal: { ...rejected, offer } }, 'm_proposal'), null);
  }
  assert.equal(a.openOffer({ action_proposal: { ...rejected, status: 'executed' } }, 'm_proposal'), null);
  assert.equal(a.openOffer({ action_proposal: rejected }, ''), null);
  assert.equal(a.openOffer(undefined, 'm'), null);
  assert.equal(a.offerState({ action_proposal: { ...rejected, offer: { ...rejected.offer, status: 'sent' } } }), 'sent');
  assert.equal(a.offerState({ action_proposal: { ...rejected, offer: { ...rejected.offer, status: 'declined' } } }), 'declined');
  assert.equal(a.offerState({ action_proposal: rejected }), null, 'an open offer is not an ended one');
  assert.equal(a.offerState({}), null);
  // the request takes the offer by the proposal's message id; the engine reads the sentence from the conversation
  assert.deepEqual(a.offerRequest(open, 'Streść tę rozmowę', 'c_1'), { message: 'Streść tę rozmowę', conv_id: 'c_1', send_as_message: 'm_proposal' });
});

test('the offer has words in both languages, and a machine-name fallback without a catalog', () => {
  assert.ok(en.offer && pl.offer && en.offer_hint && pl.offer_hint && en.offer_sent && pl.offer_sent && en.offer_declined && pl.offer_declined && en.offer_failed && pl.offer_failed);
  const texts = a.offerTexts(feature);
  assert.equal(texts.send, en.offer);
  assert.equal(texts.hint, en.offer_hint);
  assert.equal(texts.state('sent'), en.offer_sent);
  assert.equal(texts.state('declined'), en.offer_declined);
  assert.equal(texts.state('open'), null);
  assert.equal(texts.failed('offline'), en.offer_failed.replace('{{reason}}', 'offline'));
  const bare = a.offerTexts(null);
  assert.equal(bare.send, null);
  assert.equal(bare.failed('offline'), 'offline');
});

test('the offer block is a button in the proposal message; busy disables it; a failure is shown', () => {
  const offer = { proposalId: 'm_proposal', sentenceId: 'm_sentence', auto: false };
  const html = renderToStaticMarkup(createElement(block.ProposalOffer, { feature, offer, testId: 'o', onSend: () => undefined }));
  assert.match(html, /data-testid="o"/);
  assert.match(html, /data-proposal-id="m_proposal"/);
  assert.match(html, /data-sentence-id="m_sentence"/);
  assert.match(html, new RegExp('data-testid="o-send"[^>]*>' + en.offer));
  assert.ok(html.includes(en.offer_hint));
  assert.doesNotMatch(html, /disabled/);
  const busy = renderToStaticMarkup(createElement(block.ProposalOffer, { feature, offer, busy: true, failure: 'offline', testId: 'o', onSend: () => undefined }));
  assert.equal((busy.match(/disabled/g) || []).length, 1);
  assert.match(busy, /role="alert"/);
  assert.ok(busy.includes('offline'));
  const bare = renderToStaticMarkup(createElement(block.ProposalOffer, { feature: null, offer, testId: 'o', onSend: () => undefined }));
  assert.ok(bare.includes('m_proposal'));
});
