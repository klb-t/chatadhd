// Semantic queue controls: pure view/text logic plus the data texts (rendered path: e2e/semantic-status.mjs).
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const q = await loader.load('src/context/semantic-queue.ts');
const p = await loader.load('src/onboarding/presentation.mjs');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const generated = JSON.parse(await readFile(new URL('../../src/onboarding/generated/conversation-view.json', import.meta.url), 'utf8'));
const entry = pack.entries.find(row => row.key === 'presentation.semantic_queue');
const feature = p.resolvePresentationFeature(p.resolvePresentation(), 'semantic_queue');

test('generated web catalog carries the queue texts', () => {
  assert.deepEqual(generated.entries.find(row => row.key === 'presentation.semantic_queue').value, entry.value);
});
test('unknown counts stay unknown and are never shown as zero', () => {
  const view = q.semanticQueueView({ pending: 0, processed: 0, errors: 0, mode: 'x', rate: '', running: true, paused: false });
  assert.deepEqual(view, { stalled: null, quarantined: null, retryMode: null, llm: null });   // the llm field (audit fix d) is unknown too: null, not 0
  const texts = q.semanticQueueTexts(feature);
  assert.match(texts.stalled(view.stalled), new RegExp(entry.value.locales.en.unknown));
  assert.doesNotMatch(texts.stalled(view.stalled), /\b0\b/);
  assert.match(texts.stalled(2), /2/);
  assert.equal(q.semanticQueueView({ stalled: -1, quarantined: 1.5 }).stalled, null);
  assert.equal(q.semanticQueueView({ stalled: 0 }).stalled, 0);
});
test('retry presets follow worker data; a newer host mode stays visible without being offered as a default', async () => {
  const worker = JSON.parse(await readFile(new URL('../../../data/runtime/worker.pack', import.meta.url), 'utf8'));
  assert.deepEqual(q.RETRY_MODES, worker.value_schema.properties.retry_mode.enum);
  assert.equal(q.semanticQueueView({ retry_mode: 'automatic' }).retryMode, 'automatic');
  assert.equal(q.semanticQueueView({ retry_mode: 'other' }).retryMode, 'other');
  assert.deepEqual(q.retryModeChoices('other'), [...q.RETRY_MODES, 'other']);
  assert.deepEqual(q.retryModeChoices(null), q.RETRY_MODES);
  assert.deepEqual(q.retryModeChoices('automatic'), q.RETRY_MODES);
  assert.equal(q.semanticQueueView({ retry_mode: '' }).retryMode, null);
  const texts = q.semanticQueueTexts(feature);
  assert.equal(texts.mode('automatic'), entry.value.locales.en.mode_automatic);
  assert.equal(texts.requeue, entry.value.locales.en.requeue);
  const bare = q.semanticQueueTexts(null);
  assert.equal(bare.requeue, null);
  assert.equal(bare.mode('automatic'), 'automatic');
  assert.equal(texts.mode('other'), 'other');
  assert.ok(!q.RETRY_MODES.includes('other'), 'a reported unknown value never mutates the shipped choices');
});
test('requeue result text carries the returned counts only', () => {
  const texts = q.semanticQueueTexts(feature);
  const line = texts.requeued({ matched: 3, requeued: 3, failed_requeued: 2, stalled_requeued: 1, ids: [], skipped: [], dry_run: false, history_preserved: true });
  assert.match(line, /3.*2.*1/);
});

test('analysis-model warnings and re-enable labels exist in every offered language', () => {
  for (const [language, catalog] of Object.entries(entry.value.locales)) {
    assert.equal(typeof catalog.llm_disabled, 'string', `${language}: llm_disabled`);
    assert.equal(typeof catalog.llm_reenable, 'string', `${language}: llm_reenable`);
    assert.ok(catalog.llm_reenable.trim(), `${language}: nonempty re-enable label`);
    const texts = q.semanticQueueTexts({ ...feature, locale: language, catalog });
    const expected = catalog.llm_disabled.split('{{count}}').join('3').split('{{limit}}').join('5');
    assert.equal(texts.llmDisabled(3, 5), expected);
    assert.equal(texts.llmReenable, catalog.llm_reenable);
  }
});
