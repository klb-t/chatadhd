import assert from 'node:assert/strict';
import test from 'node:test';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve, dirname } from 'node:path';
import { invokeNativeLayers, ensureNativeLayers } from './native-layers-runner.mjs';
const repo = resolve(dirname(fileURLToPath(import.meta.url)), '../../../..');
const pack = JSON.parse(readFileSync(resolve(repo, 'loom/data/graph_perspectives/graph-perspectives.pack.json'), 'utf8'));
const key = 'graph.perspective.budget';
const selected = (response, name = key) => response.effectiveDefaults.find(row => row.key === name);
const resolveNative = (input = {}) => invokeNativeLayers({ pack, ...input });

test('actual native DefaultLayers resolves all pack entries with native provenance', async () => {
  const result = await resolveNative();
  assert.equal(result.resolver, 'loom::onboarding::DefaultLayers');
  assert.equal(result.effectiveDefaults.length, pack.entries.length);
  assert.equal(selected(result).layer, 'builtin');
  assert.deepEqual(selected(result).value, { query: 600, render: 80, page: 64 });
  assert.equal(selected(result).source.entry_id, 'gp.budget');
  assert.match(selected(result).explanation, /default|pack|built/i);
});
test('override preserves exact caller source metadata and native history', async () => {
  const action = { op: 'override', key, value: { query: 90, render: 24, page: 16 }, source_refs: ['source#/override'], time: '2026-10-09T07:00:00Z' };
  const result = await resolveNative({ actions: [action] });
  assert.equal(selected(result).layer, 'user');
  assert.deepEqual(selected(result).source.source_refs, action.source_refs);
  assert.deepEqual(result.layers.history, [action]);
});
test('disable suppresses value even after a later override and roundtrip', async () => {
  const result = await resolveNative({ actions: [{ op: 'disable', key }, { op: 'override', key, value: 900 }] });
  assert.equal(selected(result).status, 'disabled'); assert.equal('value' in selected(result), false);
  const roundtrip = await resolveNative({ state: JSON.parse(JSON.stringify(result.layers)) });
  assert.deepEqual(roundtrip, result);
});
test('exclusion cannot be bypassed by override; explicit reenable required', async () => {
  const excluded = await resolveNative({ actions: [{ op: 'exclude', key }] });
  assert.equal(selected(excluded).status, 'excluded'); assert.equal('value' in selected(excluded), false);
  const rejected = await resolveNative({ state: excluded.layers, actions: [{ op: 'override', key, value: 99 }] });
  assert.equal(rejected.error.code, 'conflict');
  const enabled = await resolveNative({ state: excluded.layers, actions: [{ op: 'reenable', key }, { op: 'override', key, value: 99 }] });
  assert.equal(selected(enabled).value, 99);
});
test('permanent exclusion survives pack replacement and unknown state fields', async () => {
  const excluded = await resolveNative({ state: { vendor_unknown: { keep: [3, 5] } }, actions: [{ op: 'exclude', key }] });
  const nextPack = structuredClone(pack); nextPack.revision = 2;
  nextPack.entries = nextPack.entries.filter(entry => entry.key !== key);
  const removed = await resolveNative({ pack: nextPack, state: excluded.layers, keys: [key] });
  assert.equal(selected(removed).status, 'excluded');
  const restoredPack = structuredClone(pack); restoredPack.revision = 3;
  const returned = await resolveNative({ pack: restoredPack, state: removed.layers });
  assert.equal(selected(returned).status, 'excluded');
  assert.deepEqual(returned.layers.vendor_unknown, { keep: [3, 5] });
});
test('new capabilities in excluded area become proposals, never silently effective', async () => {
  const prior = await resolveNative({ actions: [{ op: 'exclude', key }] });
  const nextPack = structuredClone(pack); nextPack.revision = 2;
  nextPack.entries.push({ id: 'future.component', key: 'future.capability', area: 'graph.perspectives', revision: 1, value: { opaque: true } });
  const migrated = await resolveNative({ pack: nextPack, state: prior.layers });
  assert.equal(selected(migrated, 'future.capability').status, 'proposal');
  assert.equal('value' in selected(migrated, 'future.capability'), false);
  const accepted = await resolveNative({ pack: nextPack, state: migrated.layers, actions: [{ op: 'accept_proposal', key: 'future.capability' }] });
  assert.deepEqual(selected(accepted, 'future.capability').value, { opaque: true });
  assert.equal(selected(accepted).status, 'excluded');
});
test('unknown component can remain as user data and missing lookup is explicit', async () => {
  const result = await resolveNative({ actions: [{ op: 'override', key: 'vendor.extensible', value: { future: [1, 2] } }], keys: ['vendor.extensible', 'unknown.absent'] });
  assert.deepEqual(selected(result, 'vendor.extensible').value, { future: [1, 2] });
  assert.equal(selected(result, 'unknown.absent').status, 'missing');
});
test('null is an ordinary overridden value, not a fallback trigger', async () => {
  const result = await resolveNative({ actions: [{ op: 'override', key, value: null }] });
  assert.equal(selected(result).status, 'effective'); assert.equal(selected(result).value, null);
});
test('source pack and original caller state stay immutable', async () => {
  const state = { retained: true }; const beforePack = JSON.stringify(pack);
  await resolveNative({ state, actions: [{ op: 'disable', key }] });
  assert.deepEqual(state, { retained: true }); assert.equal(JSON.stringify(pack), beforePack);
});
test('native build is pinned to current source hashes and actual binary hash', async () => {
  const info = await ensureNativeLayers();
  assert.match(info.binary_sha256, /^[a-f0-9]{64}$/);
  assert.match(info.sources['loom/src/onboarding/layers.cpp'], /^[a-f0-9]{64}$/);
  assert.equal(info.runtime_source, 'current_checkout');
});

test('headless facade consumes existing OnboardingAdapter without a second resolver', async () => {
  const { loadModule } = await import('./test-loader.mjs');
  const { NativeLayerClient, nativeResolutionFromResponse } = await loadModule('src/graph-perspectives/native-layers.ts');
  let dispatched;
  const rows = [{ id: 'gp.budget', key, area: 'graph.perspectives', enabled: false, excluded: true,
    status: 'excluded', layer: 'user_exclusion', reason: 'explicit exclusion', resolution: { source: { entry_id: 'gp.budget' } } }];
  const client = new NativeLayerClient({ getSnapshot: async () => ({ defaults: rows }),
    dispatchLayer: async action => { dispatched = action; return { defaults: rows }; } });
  const result = await client.resolve();
  assert.equal(result.components[0].status, 'excluded'); assert.equal('value' in result.components[0], false);
  assert.deepEqual(result.components[0].origin, { entry_id: 'gp.budget' });
  const action = { op: 'disable', key }; await client.dispatch(action); assert.strictEqual(dispatched, action);
  const raw = await resolveNative({ actions: [action] });
  const headless = nativeResolutionFromResponse(raw);
  assert.equal(headless.components.find(row => row.id === key).status, 'disabled');
  assert.equal('value' in headless.components.find(row => row.id === key), false);
  await assert.rejects(new NativeLayerClient({ getSnapshot: async () => ({}) }).resolve(), /native_defaults_unavailable/);
  await assert.rejects(new NativeLayerClient({ getSnapshot: async () => ({ defaults: rows }) }).dispatch(action), /native_dispatch_unavailable/);
  assert.throws(() => nativeResolutionFromResponse({ error: { code: 'conflict' } }), /native_error/);
});
