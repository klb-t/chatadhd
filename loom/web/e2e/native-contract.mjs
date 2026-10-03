// Fast adapter contract gate. This exercises the real TypeScript transport;
// the native stub implements the documented Kotlin/JNI message boundary.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import ts from "typescript";

function moduleUrl(source) {
  return `data:text/javascript;base64,${Buffer.from(ts.transpileModule(source, { compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.ES2020 } }).outputText).toString("base64")}`;
}
const typesUrl = moduleUrl(await readFile(new URL("../src/api/types.ts", import.meta.url), "utf8"));
const source = (await readFile(new URL("../src/api/loom-jni.ts", import.meta.url), "utf8")).replace('from "./types";', `from "${typesUrl}";`);
// The first './types' import is type-only and disappears. Replace the value
// import as well, without relying on its order.
const sourceWithTypes = source.replaceAll('from "./types";', `from "${typesUrl}";`);
const { LoomJniApi } = await import(moduleUrl(sourceWithTypes));
const { isLoomError } = await import(typesUrl);

const calls = [];
let nativeError = false;
let streamError = false;
globalThis.window = {
  LoomBridge: {
    call(method, args) {
      calls.push({ method, args: JSON.parse(args) });
      if (nativeError) return JSON.stringify({ error: { code: "io", message: "write failed" } });
      if (method === "has_secret") return JSON.stringify({ value: true });
      if (method === "kb_query") return JSON.stringify({ run: "kr_test", items: [] });
      return JSON.stringify({ ok: true });
    },
    startStream(method, args, callbackId) {
      calls.push({ method, args: JSON.parse(args) });
      queueMicrotask(() => {
        const callback = window.__loomCallbacks[callbackId];
        if (streamError) callback(JSON.stringify({ error: { code: "io", message: "analysis failed" } }), 1);
        else {
          callback(JSON.stringify({ current: 1, total: 2, message: "extract" }), 0);
          callback(JSON.stringify({ status: "done", error: "", run: "kr_test" }), 1);
        }
      });
    },
    cancelStream() {},
  },
};

assert.equal(isLoomError({ status: "done", error: "" }), false, "RunResult.error is not an ABI error envelope");
assert.equal(isLoomError({ error: { code: "io", message: "failed" } }), true);
const api = new LoomJniApi();
assert.equal(await api.hasSecret("api_key"), true, "tri-state native result is unwrapped");
assert.deepEqual(calls.at(-1), { method: "has_secret", args: { key: "api_key" } });
nativeError = true;
await assert.rejects(() => api.deleteConversation("c_test"), /write failed/, "native write errors reach UI");
nativeError = false;
await api.knowledge.query("claims", { run: "kr_test", limit: 20 });
assert.deepEqual(calls.at(-1), { method: "kb_query", args: { query: { run: "kr_test", limit: 20, what: "claims" } } });
assert.deepEqual(await api.knowledge.run({ sources: ["archive.zip"] }), { status: "done", error: "", run: "kr_test" });
assert.deepEqual(calls.at(-1), { method: "knowledge_run", args: { config: { sources: ["archive.zip"] } } });
assert.equal(Object.keys(window.__loomCallbacks).length, 0, "completed stream callback released");
streamError = true;
await assert.rejects(() => api.knowledge.catalogScan({ sources: ["missing.zip"] }), /analysis failed/);
assert.equal(Object.keys(window.__loomCallbacks).length, 0, "failed stream callback released");
console.log("[native-contract] error envelopes, tri-state results, named arguments and background completion passed");
