// Offline contract checks against the actual profile runtime and Loom adapter.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import ts from "typescript";

function transpile(file) {
  return ts.transpileModule(readFileSync(new URL(file, import.meta.url), "utf8"), {
    compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022 },
  }).outputText;
}
const moduleUrl = source => `data:text/javascript;base64,${Buffer.from(source).toString("base64")}`;
const runtimeUrl = moduleUrl(transpile("../src/profiles/runtime.ts"));
const runtime = await import(runtimeUrl);
const adapter = await import(moduleUrl(transpile("../src/profiles/loom-adapter.ts").replace('from "./runtime"', `from "${runtimeUrl}"`)));
let cases = 0;
async function check(name, fn) { await fn(); cases++; console.log(`PASS ${name}`); }

function fakeApi() {
  const calls = [];
  const streams = [];
  const api = {
    knowledge: {},
    async createConversation(title) { calls.push(["create", title]); return { id: "c_new", title: title ?? "", created: "", updated: "" }; },
    async updateConversation(id, patch) { calls.push(["rename", id, patch]); return { id, ...patch }; },
    async editMessage(id, text) { calls.push(["edit", id, text]); return { id, text }; },
    async restoreVersion(id) { calls.push(["restore", id]); },
    async setMessageStatus(id, status) { calls.push(["status", id, status]); },
    async cancelChat(id) { calls.push(["cancel", id]); },
    chat(request, handlers) {
      const stream = { request, handlers, stopped: 0 };
      streams.push(stream);
      return () => { stream.stopped++; };
    },
  };
  return { api, calls, streams };
}
function profile(registry, operation, capability = operation, required = true) {
  return runtime.registerProfile(registry, {
    schema: runtime.PROFILE_SCHEMA, id: `test.${operation}.${capability}`, profile_revision: 1, label: "Adapter test",
    target: { application_id: "test", version: null, platform: "web" },
    evidence: { status: "inspired", sources: [], gaps: ["No original-app parity claim"] },
    presentation: { renderer: "chat", tokens: { background: "#111", surface: "#222", text: "#fff", muted: "#999", accent: "#aaa", border: "#333" },
      sidebar: { side: "left", width: 250 }, content_width: 800, message_style: "plain" },
    composer: { submit: "enter", placeholder: "Type" },
    actions: [{ id: "action", label: "Run", operation, capability, required }],
    workflows: [{ id: "flow", initial: "ready", states: ["ready", "complete"], transitions: [{ from: "ready", event: "run", action: "action", to: "complete" }] }],
  });
}
function start(registry, operation, payload) {
  const definition = profile(registry, operation);
  const session = runtime.createProfileSession(definition, registry);
  return { session, promise: runtime.executeProfileAction(session, registry, "action", { workflowId: "flow", event: "run", payload }) };
}
async function run(registry, operation, payload) { return start(registry, operation, payload).promise; }
function done(overrides = {}) { return { type: "done", message_id: "m_out", conv_id: "c", text: "Result", ...overrides }; }
// Catch immediately: cancelling a send settles it during the concurrent cancel action.
function capture(promise) { return promise.then(value => ({ value }), error => ({ error })); }
async function settles(promise) {
  let timeout;
  try {
    return await Promise.race([promise, new Promise((_, reject) => { timeout = setTimeout(() => reject(new Error("Adapter left a pending promise.")), 500); })]);
  } finally { clearTimeout(timeout); }
}

await check("native conversation/message operations preserve exact API arguments", async () => {
  const { api, calls } = fakeApi(); const events = [];
  const registry = adapter.createLoomProfileRegistry(api, { conversationCreated: id => events.push(["created", id]), selectConversation: id => events.push(["select", id]) });
  await run(registry, "chat.create", { title: "New" });
  await run(registry, "chat.select", { id: "c_selected" });
  await run(registry, "chat.rename", { id: "c", title: "Renamed" });
  await run(registry, "message.edit", { id: "m", text: "Edited\ntext" });
  await run(registry, "message.restore", { id: "m_version" });
  await run(registry, "message.exclude", { id: "m", status: "excluded" });
  await run(registry, "message.exclude", { id: "m", status: "active" });
  assert.deepEqual(calls, [["create", "New"], ["rename", "c", { title: "Renamed" }], ["edit", "m", "Edited\ntext"], ["restore", "m_version"], ["status", "m", "excluded"], ["status", "m", "active"]]);
  assert.deepEqual(events, [["created", "c_new"], ["select", "c_selected"]]);
});

await check("chat request identity, all context fields and streaming handlers survive the adapter", async () => {
  const { api, streams } = fakeApi(); const registry = adapter.createLoomProfileRegistry(api);
  const request = { message: "Hello", request_id: "r_client", conv_id: "c", model: "explicit/model", temperature: 0.37, max_tokens: 456,
    system_prompt: "Caller policy", web_search: false, deep_research: true, reasoning_effort: "medium", attachments: ["a"], context_depth: 3,
    include_memory: false, include_graph_memory: false, include_history: false, trace_context: true, stream: true,
    knowledge_context: { text: "Q", targets: ["entity"], run: "run", budget_tokens: 789, relation_hops: 2, detail_resolution: "raw", plan: { id: "p", theses: [{ id: "t", text: "Thesis" }] } } };
  const chunks = []; let eof = 0; let subscription;
  const action = start(registry, "chat.send", { request, handlers: { onChunk: chunk => chunks.push(chunk), onDone: () => eof++ }, onSubscription: value => { subscription = value; } });
  let completed = false; action.promise.then(() => { completed = true; });
  assert.strictEqual(streams[0].request, request);
  assert.deepEqual(streams[0].request, request); assert.equal(typeof subscription, "function");
  const begin = { type: "start", request_id: "r_server", conv_id: "c", user_message_id: "m_user" };
  const delta = { type: "delta", text: "Re" }; const reasoning = { type: "reasoning", text: "Reason" }; const result = done();
  streams[0].handlers.onChunk(begin); streams[0].handlers.onChunk(delta); streams[0].handlers.onChunk(reasoning);
  await Promise.resolve(); assert.equal(completed, false); assert.equal(action.session.workflows.flow, "ready");
  streams[0].handlers.onChunk(result); streams[0].handlers.onDone();
  const output = await action.promise;
  assert.strictEqual(output.result, result); assert.deepEqual(chunks, [begin, delta, reasoning, result]); assert.equal(eof, 1);
  assert.equal(output.session.workflows.flow, "complete"); assert.equal(streams[0].stopped, 0);
  subscription(); assert.equal(streams[0].stopped, 1); // A completed result remains successful after transport cleanup.
});

await check("native error, network failure and truncated stream never advance a workflow", async () => {
  for (const failure of ["chunk", "network", "eof"]) {
    const { api, streams } = fakeApi(); const registry = adapter.createLoomProfileRegistry(api);
    const events = [];
    const action = start(registry, "chat.send", { request: { message: "Hello" }, handlers: { onChunk: c => events.push(c.type), onError: m => events.push(m), onDone: () => events.push("eof") } });
    const result = capture(action.promise);
    if (failure === "chunk") streams[0].handlers.onChunk({ type: "error", code: "provider", message: "Provider rejected" });
    if (failure === "network") streams[0].handlers.onError("Network failed");
    if (failure === "eof") streams[0].handlers.onDone();
    const output = await settles(result); assert.ok(output.error instanceof Error); assert.equal(action.session.workflows.flow, "ready");
    assert.equal(streams[0].stopped, 1);
    if (failure === "chunk") assert.deepEqual(events, ["error"]);
    if (failure === "network") assert.deepEqual(events, ["Network failed"]);
    if (failure === "eof") { assert.deepEqual(events, ["eof"]); assert.match(output.error.message, /ended before a done result/); }
    streams[0].handlers.onChunk(done()); // Late results cannot convert an error to successful workflow completion.
  }
});

await check("request-ID cancellation settles the active send and stops the transport", async () => {
  const { api, streams, calls } = fakeApi(); const registry = adapter.createLoomProfileRegistry(api);
  const action = start(registry, "chat.send", { request: { message: "Hello", conv_id: "not-a-request" } });
  const result = capture(action.promise);
  streams[0].handlers.onChunk({ type: "start", request_id: "r_actual", conv_id: "c", user_message_id: "m" });
  await run(registry, "chat.cancel", { requestId: "r_actual" });
  const output = await settles(result); assert.equal(output.error.name, "AbortError");
  assert.deepEqual(calls, [["cancel", "r_actual"]]); assert.equal(streams[0].stopped, 1); assert.equal(action.session.workflows.flow, "ready");
});

await check("pre-start cancellation uses the returned subscription without inventing an ID", async () => {
  const { api, streams, calls } = fakeApi(); const registry = adapter.createLoomProfileRegistry(api); let unsubscribe;
  const action = start(registry, "chat.send", { request: { message: "Hello", conv_id: "c" }, onSubscription: value => { unsubscribe = value; } });
  const result = capture(action.promise);
  await run(registry, "chat.cancel", { unsubscribe });
  assert.equal((await settles(result)).error.name, "AbortError"); assert.deepEqual(calls, []); assert.equal(streams[0].stopped, 1);
  unsubscribe(); assert.equal(streams[0].stopped, 1);
});

await check("failed backend cancellation preserves a live send unless explicitly unsubscribed", async () => {
  for (const abortLocally of [false, true]) {
    const { api, streams } = fakeApi(); api.cancelChat = async () => { throw new Error("Cancel failed"); };
    const registry = adapter.createLoomProfileRegistry(api); let unsubscribe;
    const action = start(registry, "chat.send", { request: { message: "Hello", request_id: "r" }, onSubscription: value => { unsubscribe = value; } });
    const result = capture(action.promise);
    await assert.rejects(run(registry, "chat.cancel", { requestId: "r", ...(abortLocally ? { unsubscribe } : {}) }), /Cancel failed/);
    if (abortLocally) { assert.equal((await settles(result)).error.name, "AbortError"); assert.equal(streams[0].stopped, 1); }
    else {
      assert.equal(streams[0].stopped, 0); streams[0].handlers.onChunk(done());
      assert.equal((await settles(result)).value.session.workflows.flow, "complete");
    }
  }
});

await check("synchronous transport/callback failures and immediate unsubscribe all settle", async () => {
  {
    const { api } = fakeApi(); api.chat = () => { throw new Error("Transport startup failed"); };
    await assert.rejects(run(adapter.createLoomProfileRegistry(api), "chat.send", { request: { message: "Hello" } }), /Transport startup failed/);
  }
  {
    const { api, streams } = fakeApi(); const registry = adapter.createLoomProfileRegistry(api);
    const action = start(registry, "chat.send", { request: { message: "Hello" }, handlers: { onChunk: () => { throw new Error("Observer failed"); } } });
    const result = capture(action.promise); streams[0].handlers.onChunk({ type: "delta", text: "x" });
    assert.match((await settles(result)).error.message, /Observer failed/); assert.equal(streams[0].stopped, 1);
  }
  {
    const { api, streams } = fakeApi(); const registry = adapter.createLoomProfileRegistry(api);
    const result = capture(run(registry, "chat.send", { request: { message: "Hello" }, onSubscription: unsubscribe => unsubscribe() }));
    assert.equal((await settles(result)).error.name, "AbortError"); assert.equal(streams[0].stopped, 1);
  }
});

await check("UI capabilities come from installed host adapters and knowledge backend", async () => {
  const { api } = fakeApi(); const panels = [];
  const registry = adapter.createLoomProfileRegistry(api, { openPanel: panel => panels.push(panel), openKnowledge: () => panels.push("knowledge") });
  for (const panel of ["memory", "import", "context", "graph", "settings", "logs"]) await run(registry, `${panel}.open`);
  await run(registry, "knowledge.open"); assert.deepEqual(panels, ["memory", "import", "context", "graph", "settings", "logs", "knowledge"]);
  const withoutUi = adapter.createLoomProfileRegistry(api);
  for (const operation of ["chat.select", "knowledge.open", "settings.open", "memory.open"]) {
    const definition = profile(withoutUi, operation, operation, false);
    const available = runtime.profileAvailability(definition, withoutUi);
    assert.equal(available.optionalGaps.length, 1); assert.ok(!available.capabilities.includes(operation));
  }
  const oldApi = { ...api, knowledge: undefined };
  const oldRegistry = adapter.createLoomProfileRegistry(oldApi, { openKnowledge: () => {} });
  assert.equal(runtime.profileAvailability(profile(oldRegistry, "knowledge.open", "knowledge.open", false), oldRegistry).optionalGaps.length, 1);
  const unsupported = profile(registry, "message.restore", "branch.navigate", false);
  assert.equal(runtime.profileAvailability(unsupported, registry).optionalGaps.length, 1);
});

await check("invalid operation payloads cannot call native APIs or invent cancelled requests", async () => {
  const { api, calls, streams } = fakeApi(); const registry = adapter.createLoomProfileRegistry(api);
  for (const [operation, payload] of [["chat.send", { request: { message: "" } }], ["chat.cancel", { conv_id: "c" }],
    ["chat.cancel", { requestId: "" }], ["message.edit", { id: "m", text: 4 }], ["message.restore", { id: "" }],
    ["message.exclude", { id: "m", status: "deleted" }], ["chat.rename", { id: "c", title: false }]]) {
    await assert.rejects(run(registry, operation, payload), TypeError);
  }
  assert.deepEqual(calls, []); assert.deepEqual(streams, []);
});
console.log(`[application-profile-adapter] ${cases}/${cases} groups passed (offline; no model/provider calls)`);
