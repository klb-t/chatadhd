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
  assert.deepEqual(view, { stalled: null, quarantined: null, retryMode: null });
  const texts = q.semanticQueueTexts(feature);
  assert.match(texts.stalled(view.stalled), new RegExp(entry.value.locales.en.unknown));
  assert.doesNotMatch(texts.stalled(view.stalled), /\b0\b/);
  assert.match(texts.stalled(2), /2/);
  assert.equal(q.semanticQueueView({ stalled: -1, quarantined: 1.5 }).stalled, null);
  assert.equal(q.semanticQueueView({ stalled: 0 }).stalled, 0);
});
test('retry modes are exactly the worker values; texts come from the catalog, machine names without one', () => {
  assert.deepEqual(q.RETRY_MODES, ['explicit_requeue', 'automatic']);
  assert.equal(q.semanticQueueView({ retry_mode: 'automatic' }).retryMode, 'automatic');
  assert.equal(q.semanticQueueView({ retry_mode: 'other' }).retryMode, null);
  const texts = q.semanticQueueTexts(feature);
  assert.equal(texts.mode('automatic'), entry.value.locales.en.mode_automatic);
  assert.equal(texts.requeue, entry.value.locales.en.requeue);
  const bare = q.semanticQueueTexts(null);
  assert.equal(bare.requeue, null);
  assert.equal(bare.mode('automatic'), 'automatic');
});
test('requeue result text carries the returned counts only', () => {
  const texts = q.semanticQueueTexts(feature);
  const line = texts.requeued({ matched: 3, requeued: 3, failed_requeued: 2, stalled_requeued: 1, ids: [], skipped: [], dry_run: false, history_preserved: true });
  assert.match(line, /3.*2.*1/);
});
