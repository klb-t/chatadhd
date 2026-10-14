// Paused-send state: pure view/confirmation logic plus data texts (rendered path: browser e2e).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const u = await loader.load('src/context/usage-pending.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const generated = JSON.parse(await readFile(new URL('../../src/onboarding/generated/conversation-view.json', import.meta.url), 'utf8'));
const en = pack.entries.find(row => row.key === 'presentation.chat_usage').value.locales.en;
const receipt = { status: 'requires_confirmation', operation_id: 'op1', receipt_id: 'usage_abc', baseline_key: 'chat.send|https://x|m',
  projected: { input_bytes: { projected: 60000, baseline: 250, ratio: 240 }, cost_usd: { projected: null, baseline: null, ratio: null },
    requests: { projected: 1, baseline: 1, ratio: 1 } } };
const feature = p.resolvePresentationFeature(p.resolvePresentation(), 'chat_usage');

test('generated web catalog carries the usage texts', () => {
  assert.deepEqual(generated.entries.find(row => row.key === 'presentation.chat_usage').value, pack.entries.find(row => row.key === 'presentation.chat_usage').value);
});
test('view shows numbers from the receipt and unknown as unknown, never zero', () => {
  const view = u.usagePendingView(receipt);
  assert.equal(view.baselineKey, receipt.baseline_key);
  assert.equal(view.answerable, true);
  const cost = view.lines.find(line => line.name === 'cost_usd');
  assert.deepEqual(cost, { name: 'cost_usd', projected: null, baseline: null, ratio: null });
  assert.equal(view.lines.find(line => line.name === 'input_bytes').projected, '60000');
  const texts = u.usagePendingTexts(feature, view);
  assert.equal(texts.title, en.title);
  assert.match(texts.line(cost), new RegExp(en.unknown));
  assert.doesNotMatch(texts.line(cost), /\b0\b/);
  assert.match(texts.line(view.lines.find(line => line.name === 'input_bytes')), /60000.*250.*240/);
});
test('a receipt without identifiers cannot be answered; absent catalog falls back to machine names', () => {
  assert.equal(u.usagePendingView({ status: 'requires_confirmation' }).answerable, false);
  const view = u.usagePendingView(receipt);
  const texts = u.usagePendingTexts(null, view);
  assert.equal(texts.title, null);
  assert.equal(texts.line(view.lines[0]), view.lines[0].name);
});
test('confirmation re-sends the identical request plus the receipt answer', () => {
  const request = { message: 'hello', conv_id: 'c', model: 'm' };
  const yes = u.confirmedRequest(request, receipt, true, 'web');
  assert.deepEqual(yes, { ...request, usage_confirmation: { approved: true, receipt_id: 'usage_abc', operation_id: 'op1', ref: 'web' } });
  assert.equal(u.confirmedRequest(request, receipt, false, 'web').usage_confirmation.approved, false);
  assert.deepEqual(request, { message: 'hello', conv_id: 'c', model: 'm' });
});

// P18: a spending limit that holds (confirm) or refuses (deny) is its own reason with its own words; the reasons are the kernel's data per language.
const reason = { en: 'The monthly spending limit of 20 PLN is already used up: 25.00 PLN spent so far.', pl: 'Limit wydatków miesięczny 20 PLN jest już wykorzystany: do tej pory wydano 25.00 PLN.' };
const hold = { status: 'requires_confirmation', operation_id: 'op2', receipt_id: 'usage_lim', reason_codes: ['spend_limit'],
  limits: [{ period: 'month', on_exceed: 'confirm', code: 'spend_limit_already_over', reason }] };
const refused = { ...hold, status: 'denied', limits: [{ ...hold.limits[0], on_exceed: 'deny' }] };
const pl = pack.entries.find(row => row.key === 'presentation.chat_usage').value.locales.pl;
test('a limit that holds: the reasons replace the x10 lines, in the language of the page, and it can be answered', () => {
  const view = u.usagePendingView(hold, 'pl');
  assert.deepEqual(view.limit, { refused: false, reasons: [reason.pl] });
  assert.equal(view.answerable, true);
  const texts = u.usagePendingTexts(feature, view);
  assert.equal(texts.limitTitle, en.limit_title);
  assert.equal(texts.limitExplanation, en.limit_explanation);
  assert.equal(texts.limitConfirm, en.limit_confirm);
  assert.equal(u.usagePendingView(hold, 'en').limit.reasons[0], reason.en);
});
test('a limit that refuses cannot be answered: nothing to confirm, only to dismiss', () => {
  const view = u.usagePendingView(refused, 'en');
  assert.deepEqual(view.limit, { refused: true, reasons: [reason.en] });
  assert.equal(view.answerable, false);
  assert.equal(u.usagePendingTexts(feature, view).limitExplanation, en.limit_refused_explanation);
  // a refusal is also a refusal when only the status says it
  assert.equal(u.usagePendingView({ ...hold, status: 'denied' }, 'en').limit.refused, true);
});
test('the reason falls back to English, then to any language there is; a receipt without limits has no limit view', () => {
  const only = { ...hold, limits: [{ ...hold.limits[0], reason: { de: 'Ausgabenlimit' } }] };
  assert.deepEqual(u.usagePendingView(only, 'pl').limit.reasons, ['Ausgabenlimit']);
  assert.deepEqual(u.usagePendingView({ ...hold, limits: [{ ...hold.limits[0], reason: { en: 'x' } }] }, 'pl').limit.reasons, ['x']);
  assert.equal(u.usagePendingView(receipt, 'en').limit, null);
  assert.equal(u.usagePendingView({ ...hold, limits: [] }, 'en').limit, null);
});
test('both languages have every word of the limit block', () => {
  for (const key of ['limit_title', 'limit_explanation', 'limit_refused_explanation', 'limit_confirm', 'limit_dismiss', 'limit_dismissed']) {
    assert.equal(typeof en[key], 'string', key);
    assert.equal(typeof pl[key], 'string', key);
    assert.notEqual(pl[key], '');
  }
  assert.notEqual(pl.limit_title, en.limit_title);
});
test('notices of a limit that only tells: one quiet line each, in the language of the page', () => {
  const notices = [{ code: 'spend_limit_notice', period: 'month', text: { en: 'Heads up: x', pl: 'Uwaga: x' } }, { code: 'other' }, null];
  assert.deepEqual(u.usageNoticeLines(notices, 'pl'), ['Uwaga: x']);
  assert.deepEqual(u.usageNoticeLines(notices, 'de'), ['Heads up: x']);
  assert.deepEqual(u.usageNoticeLines(undefined, 'en'), []);
  assert.equal(u.USAGE_NOTICE_CHUNK, 'usage_notice');
});
