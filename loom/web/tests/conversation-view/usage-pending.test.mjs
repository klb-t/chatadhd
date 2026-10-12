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
