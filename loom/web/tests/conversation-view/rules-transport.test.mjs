// The user's own rules and definitions through the transports (P12 2): the same calls over HTTP (/api/...) and over the native
// bridge (loom_invoke, port actions), with the request passed through untouched. The server-side behaviour is tested natively.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const { LoomHttpApi } = await loader.load('src/api/loom-http.ts');
const { LoomJniApi } = await loader.load('src/api/loom-jni.ts');
test.after(() => loader.close());

const rule = { scope: 'conversation', scope_id: 'c_1', effect: 'confirm', action: 'summarise', reason: 'Because I said so', dry_run: true };
const disable = { id: 'user:conversation:c_1:action:summarise', restore: false };
const list = { scope: 'conversation', scope_id: 'c_1', user_only: true, include_disabled: true };
const define = { id: 'rozpisz', instruction: 'Rozpisz tę wiadomość na kroki', requires: ['has_text'], phrases: { pl: ['rozpisz'] }, provenance: { origin: 'chat', conv_id: 'c_1' } };
const remove = { id: 'rozpisz', confirmed: true, ref: 'web' };
const group = { id: 'texts_and_images', traits: ['has_text', 'has_image'], label: { en: 'Texts and images' } };

const calls = [
  ['rulesSet', rule, '/api/rules/set', 'rules.set'],
  ['rulesDisable', disable, '/api/rules/disable', 'rules.disable'],
  ['rulesList', list, '/api/rules/list', 'rules.list'],
  ['actionsDefine', define, '/api/actions/define', 'actions.define'],
  ['actionsRemove', remove, '/api/actions/remove', 'actions.remove'],
  ['groupsDefine', group, '/api/groups/define', 'groups.define'],
  ['groupsRemove', { id: 'texts_and_images' }, '/api/groups/remove', 'groups.remove'],
];

test('over HTTP every call is a POST of its request to its route, and the answer is returned as it came', async () => {
  const original = globalThis.fetch;
  const seen = [];
  globalThis.fetch = async (url, options) => { seen.push({ url, options }); return new Response(JSON.stringify({ id: 'ar_1', status: 'completed', url })); };
  try {
    const api = new LoomHttpApi();
    for (const [name, request, path] of calls) {
      const answer = await api.actions[name](request);
      const call = seen.at(-1);
      assert.equal(call.url, path, name);
      assert.equal(call.options.method, 'POST', name);
      assert.deepEqual(JSON.parse(call.options.body), request, name);
      assert.equal(answer.url, path, name);
    }
    assert.equal(seen.length, calls.length);
    // a list without a request asks for the user's whole picture
    await api.actions.rulesList();
    assert.deepEqual(JSON.parse(seen.at(-1).options.body), {});
  } finally { globalThis.fetch = original; }
});

test('over the native bridge every call is loom_invoke on the actions port with the catalogue id as the action', async () => {
  const seen = [];
  globalThis.window = { LoomBridge: { call: (method, json) => { seen.push({ method, args: JSON.parse(json) }); return JSON.stringify({ id: 'ar_2', status: 'completed' }); } } };
  try {
    const api = new LoomJniApi();
    for (const [name, request, , action] of calls) {
      const answer = await api.actions[name](request);
      assert.deepEqual(seen.at(-1), { method: 'invoke', args: { port: 'actions', action, request } }, name);
      assert.equal(answer.status, 'completed', name);
    }
    await api.actions.rulesList();
    assert.deepEqual(seen.at(-1).args, { port: 'actions', action: 'rules.list', request: {} });
  } finally { delete globalThis.window; }
});

test('an error answer is an error of the call, on both transports', async () => {
  const original = globalThis.fetch;
  globalThis.fetch = async () => new Response(JSON.stringify({ error: { code: 'invalid_argument', message: 'a pinned rule' } }), { status: 400 });
  try { await assert.rejects(new LoomHttpApi().actions.rulesSet(rule), /a pinned rule/); }
  finally { globalThis.fetch = original; }
  globalThis.window = { LoomBridge: { call: () => JSON.stringify({ error: { code: 'invalid_argument', message: 'a pinned rule' } }) } };
  try { await assert.rejects(new LoomJniApi().actions.actionsRemove(remove), /a pinned rule/); }
  finally { delete globalThis.window; }
});
