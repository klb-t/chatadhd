// R67 (topbar_model_line): the top bar shows the model the next request will use and the real state of the last request - not a constant.
import test from 'node:test';
import assert from 'node:assert/strict';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const m = await loader.load('src/shell/model-chip.ts');

const words = { tb_model_default: "{{model}} (domyślny)", cm_model_default: "model domyślny", tb_conn_none: "none", tb_conn_ok: "ok {{time}}", tb_conn_failed: "failed {{time}}" };
const t = (id, p = {}) => (words[id] ?? id).replace(/\{\{(\w+)\}\}/g, (_, k) => p[k] ?? "");

test('the "no pick" option names the model the next request will use', () => {
  assert.equal(m.defaultOptionLabel("anthropic/claude-sonnet-4", t), "claude-sonnet-4 (domyślny)");
  assert.equal(m.defaultOptionLabel("gpt-x", t), "gpt-x (domyślny)");
});
test("until the chain has answered, the old words stay (no model name that was never resolved)", () => {
  assert.equal(m.defaultOptionLabel(null, t), "model domyślny");
  assert.equal(m.defaultOptionLabel("", t), "model domyślny");
});
test("the connection state is derived from the last request, three states, not a constant", () => {
  assert.deepEqual(m.connectionView(null, t), { state: "none", text: "none" });
  const at = Date.UTC(2026, 9, 23, 12, 30);
  const ok = m.connectionView({ ok: true, at }, t, "en-GB");
  const failed = m.connectionView({ ok: false, at }, t, "en-GB");
  assert.equal(ok.state, "ok");
  assert.match(ok.text, /^ok \d\d:\d\d$/);
  assert.equal(failed.state, "failed");
  assert.match(failed.text, /^failed \d\d:\d\d$/);
});
