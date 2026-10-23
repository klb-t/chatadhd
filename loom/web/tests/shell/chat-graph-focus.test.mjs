// M1-G1b: the target of a completed graph.navigate run (the record is the real kernel's /api/actions/status answer for "pokaż import w grafie", shortened).
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const m = await loader.load('src/context/chat-graph-focus.ts');
const run = (address, extra = {}, args = { focus: 'import' }) => ({ id: 'ar_1', status: 'completed', request: { action: 'graph.navigate', args }, steps: [{ action: 'graph.navigate', variants: [{ state: 'ok', data: { schema: 'loom.graph_navigate/1', focus: { address, label: 'x' }, children: [] } }] }], ...extra });

test('the area the kernel resolved is the target, not the name the person said', () => {
  assert.deepEqual(m.graphTargetOfRun(run('area:data_corpora')), { focus: 'area:data_corpora', perspectives: [] });
});
test('the root is the empty address', () => {
  assert.deepEqual(m.graphTargetOfRun(run('', {}, {})), { focus: '', perspectives: [] });
});
test('perspectives asked for travel with the target', () => {
  assert.deepEqual(m.graphTargetOfRun(run('conv:ab', {}, { focus: 'conv:ab', perspectives: ['sent_to_model', 3] })), { focus: 'conv:ab', perspectives: ['sent_to_model'] });
});
test('any other run, an unfinished one or a malformed record opens nothing', () => {
  assert.equal(m.graphTargetOfRun({ ...run('area:rom'), request: { action: 'settings.resolved' } }), null);
  assert.equal(m.graphTargetOfRun(run('area:rom', { status: 'needs_confirmation' })), null);
  assert.equal(m.graphTargetOfRun(run('area:rom', { steps: [] })), null);
  assert.equal(m.graphTargetOfRun({ request: { action: 'graph.navigate' }, steps: [{ action: 'graph.navigate', variants: [{ data: { schema: 'other/1', focus: { address: 'x' } } }] }] }), null);
  for (const junk of [null, undefined, 5, 'x', [], {}, { request: 5 }]) assert.equal(m.graphTargetOfRun(junk), null, JSON.stringify(junk));
});
