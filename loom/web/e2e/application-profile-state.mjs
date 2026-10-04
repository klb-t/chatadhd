// Offline contract regressions: real adapter availability, declarative workflow execution and restoration.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import ts from "typescript";
const source = readFileSync(new URL("../src/profiles/runtime.ts", import.meta.url), "utf8");
const js = ts.transpileModule(source, { compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.ES2020 } }).outputText;
const m = await import(`data:text/javascript;base64,${Buffer.from(js).toString("base64")}`);
const schema = JSON.parse(readFileSync(new URL("../../../docs/contracts/application_profile.schema.json", import.meta.url), "utf8"));
assert.equal(schema.properties.schema.const, m.PROFILE_SCHEMA);
let groups = 0;
async function check(name, fn) { await fn(); groups++; console.log(`PASS ${name}`); }
function profile(overrides = {}) {
  return { schema: m.PROFILE_SCHEMA, id: "fixture-view", profile_revision: 1, label: "Example app mapping",
    target: { application_id: "arbitrary/vendor.app", version: "old-release-2021", platform: "desktop-web" },
    evidence: { status: "partial", sources: [{ url: "https://example.org/source", revision: "abc123", note: "Reference only" }], gaps: ["No hidden provider instructions"] },
    presentation: { renderer: "chat", tokens: { background: "#101010", surface: "#161616", text: "#ffffff", muted: "#aaa", accent: "#008080", border: "#444" },
      sidebar: { side: "right", width: 320 }, content_width: 820, message_style: "plain" },
    composer: { submit: "mod-enter", placeholder: "Write" },
    actions: [{ id: "new", label: "New", operation: "chat.create", capability: "chat.create", required: true },
      { id: "send", label: "Send", operation: "chat.send", capability: "chat.send", required: true },
      { id: "memory", label: "Memory", operation: "memory.open", capability: "memory.open", required: false }],
    workflows: [{ id: "conversation", initial: "empty", states: ["empty", "ready", "answered"], transitions: [
      { from: "empty", event: "create", action: "new", to: "ready" },
      { from: "ready", event: "send", action: "send", to: "answered" },
      { from: "answered", event: "send", action: "send", to: "answered" },
    ] }], ...overrides };
}
function registry(adapters = [{ operation: "chat.create", capability: "chat.create", execute: async () => ({ id: "c1" }) },
  { operation: "chat.send", capability: "chat.send", execute: async payload => ({ echoed: payload }) }], options = {}) {
  return m.createProfileRegistry({ adapters, operations: ["memory.open", ...(options.operations ?? [])], ...options });
}
function code(expected) { return error => error instanceof m.ProfileError && error.code === expected; }

await check("arbitrary application and original versions are data, distinct from profile revisions", () => {
  const r = registry(); const raw = profile(); const before = JSON.stringify(raw); const p = m.registerProfile(r, raw);
  assert.equal(p.target.version, "old-release-2021"); assert.equal(p.profile_revision, 1);
  assert.equal(p.target.application_id, "arbitrary/vendor.app"); assert.equal(JSON.stringify(raw), before);
  assert.notEqual(p, raw); assert.ok(Object.isFrozen(p.presentation.tokens));
  raw.presentation.tokens.background = "#123"; assert.equal(p.presentation.tokens.background, "#101010");
  const unknown = m.registerProfile(r, profile({ id: "another", target: { application_id: "invented-app", version: null, platform: "android" } }));
  assert.equal(unknown.target.version, null);
});
await check("missing optional capabilities are explicit and required capabilities block activation", async () => {
  const r = registry(); const p = m.registerProfile(r, profile()); const status = m.profileAvailability(p, r);
  assert.equal(status.supported, true); assert.deepEqual(status.capabilities, ["chat.create", "chat.send"]);
  assert.deepEqual(status.optionalGaps.map(g => g.action_id), ["memory"]);
  const s = m.createProfileSession(p, r); await assert.rejects(m.executeProfileAction(s, r, "memory"), code("capability"));
  assert.equal(s.sequence, 0);
  const unavailable = m.registerProfile(r, profile({ id: "blocked", actions: profile().actions.map(a => ({ ...a, required: true })) }));
  assert.equal(m.profileAvailability(unavailable, r).supported, false);
  assert.throws(() => m.createProfileSession(unavailable, r), code("capability"));
});
await check("capability labels cannot stand in for a matching operation adapter", () => {
  const r = registry([{ operation: "chat.create", capability: "chat.send", execute: () => true }]);
  const p = m.registerProfile(r, profile());
  assert.deepEqual(m.profileAvailability(p, r).requiredGaps.map(g => g.action_id), ["new", "send"]);
  assert.throws(() => m.createProfileSession(p, r), code("capability"));
});
await check("unknown schema, operation, renderer and executable fields fail visibly", () => {
  for (const [change, expected] of [
    [p => p.schema = "future-profile", "schema"], [p => p.actions[0].operation = "chat.fabricated", "operation"],
    [p => p.presentation.renderer = "unregistered", "renderer"], [p => p.script = "globalThis.pwned=true", "contract"],
    [p => p.model = "provider-model", "contract"], [p => p.request_adapter = { system: "invisible" }, "contract"],
    [p => p.presentation.tokens.background = "url(https://example.org/tracker)", "contract"],
    [p => p.evidence.sources[0].url = "javascript:alert(1)", "contract"],
    [p => p.evidence.sources[0].url = "https://secret:password@example.org/", "contract"],
    [p => p.evidence.sources[0].url = "https://example.org/\n", "contract"],
    [p => p.presentation.tokens.accent = "#123\n", "contract"],
    [p => p.presentation.content_width = Infinity, "contract"], [p => p.presentation.sidebar.width = 0, "contract"],
    [p => delete p.actions[0], "contract"],
  ]) { const raw = profile(); change(raw); assert.throws(() => m.registerProfile(registry(), raw), code(expected)); }
  const raw = profile(); Object.defineProperty(raw, "label", { get() { throw Error("Accessor executed"); }, enumerable: true });
  assert.throws(() => m.registerProfile(registry(), raw), code("contract"));
  const sparseReplacement = new Array(1); sparseReplacement["00"] = "replaces-missing-index";
  const hiddenLiteral = {}; Object.defineProperty(hiddenLiteral, "chosen", { value: "silently-lost", enumerable: false });
  for (const literal of [sparseReplacement, hiddenLiteral]) {
    const bad = profile(); bad.workflows[0].transitions[0].payload = { literal };
    assert.throws(() => m.registerProfile(registry(), bad), code("contract"));
  }
});
await check("extensions are installed by code while new targets and versions remain declarative", async () => {
  let observed;
  const r = registry([{ operation: "canvas.open", capability: "canvas", execute: input => { observed = input; return "opened"; } }],
    { operations: ["canvas.open"], renderers: ["canvas"] });
  const p = m.registerProfile(r, profile({ id: "canvas-for-unseen-app", target: { application_id: "new-app", version: "3.17", platform: "tablet" },
    presentation: { ...profile().presentation, renderer: "canvas" },
    actions: [{ id: "open", label: "Open", operation: "canvas.open", capability: "canvas", required: true }], workflows: [] }));
  const output = await m.executeProfileAction(m.createProfileSession(p, r), r, "open", { payload: { document: "d1" } });
  assert.equal(output.result, "opened"); assert.deepEqual(observed, { document: "d1" }); assert.equal(output.session.sequence, 1);
  assert.throws(() => m.createProfileRegistry({ adapters: [{ operation: "ghost", capability: "ghost", execute: () => true }] }), code("operation"));
});
await check("workflow order is enforced and state advances only after adapter success", async () => {
  let calls = 0; let release;
  const r = registry([{ operation: "chat.create", capability: "chat.create", execute: () => { calls++; return new Promise(resolve => { release = resolve; }); } },
    { operation: "chat.send", capability: "chat.send", execute: () => { calls++; return "sent"; } }]);
  const p = m.registerProfile(r, profile()); const start = m.createProfileSession(p, r);
  await assert.rejects(m.executeProfileAction(start, r, "send", { workflowId: "conversation", event: "send" }), code("workflow"));
  await assert.rejects(m.executeProfileAction(start, r, "send", { workflowId: "conversation", event: "create" }), code("workflow"));
  assert.equal(calls, 0);
  const running = m.executeProfileAction(start, r, "new", { workflowId: "conversation", event: "create" });
  assert.equal(start.workflows.conversation, "empty"); assert.equal(start.history.length, 0);
  await assert.rejects(m.executeProfileAction(start, r, "new"), code("busy")); assert.equal(calls, 1);
  release({ id: "created" }); const created = await running;
  assert.equal(start.workflows.conversation, "empty"); assert.equal(created.session.workflows.conversation, "ready");
  assert.deepEqual(created.result, { id: "created" });
  await assert.rejects(m.executeProfileAction(start, r, "new"), code("stale"));
  const sent = await m.executeProfileAction(created.session, r, "send", { workflowId: "conversation", event: "send" });
  assert.equal(sent.session.workflows.conversation, "answered"); assert.equal(sent.session.sequence, 2); assert.equal(calls, 2);
});
await check("rejected adapters leave workflow and history intact and can be retried", async () => {
  let attempts = 0;
  const r = registry([{ operation: "chat.create", capability: "chat.create", execute: async () => { attempts++; if (attempts === 1) throw Error("Native create failed"); return "ok"; } },
    { operation: "chat.send", capability: "chat.send", execute: () => true }]);
  const p = m.registerProfile(r, profile()); const start = m.createProfileSession(p, r); const before = m.serializeProfileSession(start);
  await assert.rejects(m.executeProfileAction(start, r, "new", { workflowId: "conversation", event: "create" }), /Native create failed/);
  assert.equal(m.serializeProfileSession(start), before);
  const retried = await m.executeProfileAction(start, r, "new", { workflowId: "conversation", event: "create" });
  assert.equal(retried.session.workflows.conversation, "ready"); assert.equal(attempts, 2);
});
await check("capability guards run before native side effects", async () => {
  let calls = 0;
  const r = registry([{ operation: "chat.create", capability: "chat.create", execute: () => { calls++; return true; } },
    { operation: "chat.send", capability: "chat.send", execute: () => true }]);
  const raw = profile(); raw.workflows[0].transitions[0].requires = ["workspace.project.create"];
  const p = m.registerProfile(r, raw); const s = m.createProfileSession(p, r);
  await assert.rejects(m.executeProfileAction(s, r, "new", { workflowId: "conversation", event: "create" }), code("capability"));
  assert.equal(calls, 0); assert.equal(s.sequence, 0);
});
await check("invalid finite-state declarations are rejected at registration", () => {
  for (const mutate of [p => p.actions.push(p.actions[0]), p => p.workflows.push(p.workflows[0]),
    p => p.workflows[0].initial = "missing", p => p.workflows[0].transitions[0].to = "missing",
    p => p.workflows[0].transitions[0].action = "missing", p => p.workflows[0].states.push("empty"),
    p => p.workflows[0].transitions.push({ ...p.workflows[0].transitions[0], to: "answered" })]) {
    const raw = profile(); mutate(raw); assert.throws(() => m.registerProfile(registry(), raw), m.ProfileError);
  }
});
await check("profile revision cannot silently mutate target identity or same-revision data", () => {
  const r = registry(); const p = m.registerProfile(r, profile());
  assert.equal(m.registerProfile(r, Object.fromEntries(Object.entries(profile()).reverse())), p);
  assert.throws(() => m.registerProfile(r, profile({ label: "Modified without revision" })), code("revision"));
  const next = m.registerProfile(r, profile({ label: "New revision", profile_revision: 2 })); assert.equal(next.profile_revision, 2);
  for (const field of ["application_id", "version", "platform"]) {
    const raw = profile({ profile_revision: 3 }); raw.target[field] = "different";
    assert.throws(() => m.registerProfile(r, raw), code("identity"));
  }
  assert.throws(() => m.profileAvailability(profile(), r), code("revision"));
});
await check("restoration replays workflow order and rejects revision, target, history or state mismatch", async () => {
  const r = registry(); const p = m.registerProfile(r, profile());
  const first = await m.executeProfileAction(m.createProfileSession(p, r), r, "new", { workflowId: "conversation", event: "create" });
  const second = await m.executeProfileAction(first.session, r, "send", { workflowId: "conversation", event: "send" });
  const stored = m.serializeProfileSession(second.session);
  assert.deepEqual(m.parseProfileSession(stored, p, r), second.session);
  assert.deepEqual(m.createProfileSession(p, r, stored), second.session);
  for (const mutate of [s => s.schema = "future", s => s.profile.id = "another", s => s.profile.profile_revision = 2,
    s => s.profile.target.version = null, s => s.profile.definition = "changed", s => s.workflows.conversation = "empty", s => s.workflows.extra = "ready",
    s => s.history.reverse(), s => s.history[0].workflow.to = "answered", s => s.history[1].operation = "message.edit",
    s => s.sequence = 5, s => s.history = [], s => s.default_model = "other"]) {
    const state = JSON.parse(stored); mutate(state); assert.throws(() => m.parseProfileSession(state, p, r), m.ProfileError);
  }
  assert.throws(() => m.parseProfileSession("{broken", p, r), code("restore"));
  const forged = JSON.parse(stored); forged.workflows.conversation = "empty";
  await assert.rejects(m.executeProfileAction(forged, r, "new", { workflowId: "conversation", event: "create" }), code("restore"));
  const rebuilt = registry(); const altered = m.registerProfile(rebuilt, profile({ label: "Same revision, changed after reload" }));
  assert.throws(() => m.parseProfileSession(stored, altered, rebuilt), code("revision"));
  const rebuiltIdentical = registry(); const identical = m.registerProfile(rebuiltIdentical, Object.fromEntries(Object.entries(profile()).reverse()));
  assert.deepEqual(m.parseProfileSession(stored, identical, rebuiltIdentical), second.session);
});
await check("null version and uncertain evidence cannot claim verified original parity", () => {
  const r = registry();
  for (const change of [p => p.target.version = null, p => p.evidence.sources = [], p => p.evidence.gaps = ["Missing workflow"]]) {
    const raw = profile(); raw.evidence.status = "verified"; raw.evidence.gaps = []; change(raw);
    assert.throws(() => m.registerProfile(r, raw), code("evidence"));
  }
  const raw = profile(); raw.evidence.status = "verified"; raw.evidence.gaps = []; assert.equal(m.registerProfile(r, raw).evidence.status, "verified");
});
await check("changing profile views preserves canonical data, context and model configuration", () => {
  const canonical = { messages: [{ id: "m1", raw: { provider_field: [1, 2, 3] } }], knowledge: { claims: ["c1"] },
    settings: { model: "my-chosen-model", reasoning: true, budget: 1000000000 }, context: { scope: "owner-selected" } };
  const before = structuredClone(canonical); const raw = profile(); const originalBytes = JSON.stringify(raw); const r = registry();
  const p = m.registerProfile(r, raw); const s = m.createProfileSession(p, r);
  const other = m.registerProfile(r, profile({ id: "another-view", composer: { submit: "enter", placeholder: "Other" } }));
  m.createProfileSession(other, r); m.createProfileSession(p, r, m.serializeProfileSession(s));
  assert.deepEqual(canonical, before); assert.equal(JSON.stringify(raw), originalBytes);
  assert.equal(s.default_model, undefined); assert.equal(s.context, undefined);
});
await check("submit shortcuts preserve multiline input and IME composition", () => {
  assert.equal(m.shouldSubmit("enter", { key: "Enter" }), true);
  assert.equal(m.shouldSubmit("mod-enter", { key: "Enter" }), false);
  assert.equal(m.shouldSubmit("mod-enter", { key: "Enter", ctrlKey: true }), true);
  assert.equal(m.shouldSubmit("mod-enter", { key: "Enter", metaKey: true }), true);
  assert.equal(m.shouldSubmit("unmodified-enter", { key: "Enter" }), true);
  for (const modifier of ["ctrlKey", "metaKey", "altKey", "shiftKey"]) assert.equal(m.shouldSubmit("unmodified-enter", { key: "Enter", [modifier]: true }), false);
  for (const submit of ["enter", "mod-enter", "unmodified-enter"]) {
    assert.equal(m.shouldSubmit(submit, { key: "Enter", ctrlKey: true, shiftKey: true }), false);
    assert.equal(m.shouldSubmit(submit, { key: "Enter", ctrlKey: true, isComposing: true }), false);
    assert.equal(m.shouldSubmit(submit, { key: "a", ctrlKey: true }), false);
  }
  const r = registry(); const raw = profile(); raw.presentation.message_style = "user-bubble"; raw.composer.submit = "unmodified-enter";
  assert.equal(m.registerProfile(r, raw).presentation.message_style, "user-bubble");
});

await check("declarative payloads bind inputs/context and selected result variables across workflow steps", async () => {
  const calls = [];
  const r = registry([{ operation: "chat.create", capability: "chat.create", execute: payload => { calls.push(payload); return { id: "generated-conversation", unrelated_native_request: { never: "AUTOMATICALLY_CAPTURED" }, handler: () => {} }; } },
    { operation: "chat.send", capability: "chat.send", execute: payload => { calls.push(payload); return { answer: "not-persisted" }; } }]);
  const raw = profile();
  raw.workflows[0].transitions[0].payload = { object: { title: { from: "inputs", pointer: "/title" }, meta: { from: "context", pointer: "/meta" },
    parts: { array: [{ literal: "first" }, { from: "inputs", pointer: "/nested/a~1b/~0key/1" }] } } };
  raw.workflows[0].transitions[0].save = { conversation_id: { from: "result", pointer: "/id" },
    receipt: { object: { source_title: { from: "inputs", pointer: "/title" }, origin: { literal: "explicit-profile-selection" } } } };
  raw.workflows[0].transitions[1].payload = { object: { id: { from: "vars", pointer: "/conversation_id" }, request: { from: "inputs", pointer: "/request" } } };
  const p = m.registerProfile(r, raw); const start = m.createProfileSession(p, r);
  const inputs = { title: "Chosen title", nested: { "a/b": { "~key": [11, 22] } }, unselected: "PRIVATE_INPUT_NOT_CAPTURED" }; const before = structuredClone(inputs);
  const created = await m.executeProfileAction(start, r, "new", { workflowId: "conversation", event: "create", bindings: { inputs, context: { meta: "chosen-context" } } });
  assert.deepEqual(calls[0], { title: "Chosen title", meta: "chosen-context", parts: ["first", 22] }); assert.deepEqual(inputs, before);
  assert.deepEqual(created.session.variables.conversation, { conversation_id: "generated-conversation", receipt: { source_title: "Chosen title", origin: "explicit-profile-selection" } });
  const request = { message: "PRIVATE_NATIVE_REQUEST_NOT_CAPTURED", model: "chosen-by-owner" };
  const sent = await m.executeProfileAction(created.session, r, "send", { workflowId: "conversation", event: "send", bindings: { inputs: { request } } });
  assert.deepEqual(calls[1], { id: "generated-conversation", request }); assert.notEqual(calls[1].request, request);
  const stored = m.serializeProfileSession(sent.session);
  for (const text of ["PRIVATE_NATIVE_REQUEST_NOT_CAPTURED", "PRIVATE_INPUT_NOT_CAPTURED", "AUTOMATICALLY_CAPTURED", "not-persisted"]) assert.ok(!stored.includes(text));
  assert.deepEqual(m.parseProfileSession(stored, p, r), sent.session);
});

await check("missing and unsafe bindings fail before adapter effects and payload overrides are explicit errors", async () => {
  let calls = 0;
  const r = registry([{ operation: "chat.create", capability: "chat.create", execute: () => { calls++; return { id: "c1" }; } },
    { operation: "chat.send", capability: "chat.send", execute: () => true }]);
  for (const [i, reference, bindings] of [
    [0, { from: "inputs", pointer: "/missing" }, { inputs: {} }], [1, { from: "context", pointer: "/id" }, { context: null }],
    [2, { from: "vars", pointer: "/not_created" }, {}], [3, { from: "inputs", pointer: "/request" }, { inputs: { request: { handler: () => {} } } }],
    [4, { from: "inputs", pointer: "/toString" }, { inputs: {} }], [5, { from: "inputs", pointer: "/items/01" }, { inputs: { items: [1, 2] } }],
  ]) {
    const raw = profile({ id: `missing-binding-${i}` }); raw.workflows[0].transitions[0].payload = reference;
    const p = m.registerProfile(r, raw); const s = m.createProfileSession(p, r);
    await assert.rejects(m.executeProfileAction(s, r, "new", { workflowId: "conversation", event: "create", bindings }), code("binding"));
    assert.equal(s.sequence, 0);
  }
  const raw = profile({ id: "declarative-override" }); raw.workflows[0].transitions[0].payload = { literal: {} };
  raw.workflows[0].transitions[0].save = { selection: { from: "context", pointer: "/missing" } };
  const p = m.registerProfile(r, raw); const s = m.createProfileSession(p, r);
  await assert.rejects(m.executeProfileAction(s, r, "new", { workflowId: "conversation", event: "create", payload: {} }), code("binding"));
  await assert.rejects(m.executeProfileAction(s, r, "new", { workflowId: "conversation", event: "create", bindings: { context: {} } }), code("binding"));
  assert.equal(calls, 0);
  for (const payload of [{ from: "result", pointer: "/id" }, { from: "settings", pointer: "/model" }, { from: "inputs", pointer: "#fragment" },
    { from: "inputs", pointer: "/bad~2escape" }, { literal: 1, object: {} }, { script: "return globalThis" }]) {
    const invalid = profile({ id: "invalid-expression" }); invalid.workflows[0].transitions[0].payload = payload;
    assert.throws(() => m.registerProfile(r, invalid), m.ProfileError);
  }
});

await check("selected unsafe outputs reject state advancement after native success; unrelated unsafe outputs stay unpersisted", async () => {
  const cycle = {}; cycle.self = cycle;
  const getter = {}; Object.defineProperty(getter, "field", { enumerable: true, get() { throw Error("Getter must not execute"); } });
  const hidden = {}; Object.defineProperty(hidden, "field", { enumerable: false, value: "do-not-drop" });
  const sparseReplacement = new Array(1); sparseReplacement["00"] = "do-not-replace-with-null";
  const unsafe = [undefined, () => {}, Infinity, NaN, cycle, new Date(), getter, hidden, sparseReplacement, { nested: undefined }, [1, , 3], 1n];
  for (let i = 0; i < unsafe.length; i++) {
    let calls = 0;
    const r = registry([{ operation: "chat.create", capability: "chat.create", execute: () => { calls++; return { chosen: unsafe[i] }; } },
      { operation: "chat.send", capability: "chat.send", execute: () => true }]);
    const raw = profile(); raw.workflows[0].transitions[0].save = { chosen: { from: "result", pointer: "/chosen" } };
    const p = m.registerProfile(r, raw); const start = m.createProfileSession(p, r); const before = m.serializeProfileSession(start);
    await assert.rejects(m.executeProfileAction(start, r, "new", { workflowId: "conversation", event: "create" }), m.ProfileError);
    assert.equal(calls, 1); assert.equal(m.serializeProfileSession(start), before);
  }
  const r = registry(); const raw = profile(); raw.workflows[0].transitions[0].save = { missing: { from: "result", pointer: "/missing" } };
  const p = m.registerProfile(r, raw); const s = m.createProfileSession(p, r);
  await assert.rejects(m.executeProfileAction(s, r, "new", { workflowId: "conversation", event: "create" }), code("binding")); assert.equal(s.sequence, 0);
});

await check("selected variable receipts replay exactly; forged extras or cross-workflow values are rejected", async () => {
  const r = registry(); const raw = profile(); raw.workflows[0].transitions[0].save = { id: { from: "result", pointer: "/id" } };
  const p = m.registerProfile(r, raw); const result = await m.executeProfileAction(m.createProfileSession(p, r), r, "new", { workflowId: "conversation", event: "create" });
  const stored = m.serializeProfileSession(result.session);
  for (const mutate of [s => delete s.variables, s => delete s.history[0].variables, s => s.variables.conversation.id = "changed",
    s => s.history[0].variables.extra = "unselected", s => s.variables.extra_workflow = { id: "fake" },
    s => s.history[0].variables.id = () => {}, s => s.history[0].workflow = undefined]) {
    const snapshot = JSON.parse(stored); mutate(snapshot); assert.throws(() => m.parseProfileSession(snapshot, p, r), m.ProfileError);
  }
  assert.deepEqual(m.parseProfileSession(stored, p, r), result.session);
  const oldR = registry(); const oldP = m.registerProfile(oldR, profile()); const old = m.createProfileSession(oldP, oldR);
  assert.equal(old.variables, undefined); assert.deepEqual(m.parseProfileSession(m.serializeProfileSession(old), oldP, oldR), old);
  const protoRaw = profile({ id: "proto-safe-workflow" }); protoRaw.workflows[0].id = "__proto__";
  protoRaw.workflows[0].transitions[0].save = JSON.parse('{"__proto__":{"from":"result","pointer":"/id"}}');
  const protoP = m.registerProfile(r, protoRaw); const protoStep = await m.executeProfileAction(m.createProfileSession(protoP, r), r, "new", { workflowId: "__proto__", event: "create" });
  assert.equal(protoStep.session.variables.__proto__.__proto__, "c1");
  assert.deepEqual(m.parseProfileSession(m.serializeProfileSession(protoStep.session), protoP, r), protoStep.session);
  assert.equal({}.id, undefined);
  for (const workflowId of ["constructor", "toString", "__proto__"]) {
    const reservedRaw = profile({ id: `workflow-${workflowId}` }); reservedRaw.workflows[0].id = workflowId;
    reservedRaw.workflows[0].transitions[0].payload = { from: "vars", pointer: "" };
    const reservedP = m.registerProfile(r, reservedRaw);
    const explicitEmpty = JSON.parse(m.serializeProfileSession(m.createProfileSession(reservedP, r))); explicitEmpty.variables = {};
    const restored = m.parseProfileSession(explicitEmpty, reservedP, r);
    const selected = await m.executeProfileAction(restored, r, "new", { workflowId, event: "create" });
    assert.equal(selected.session.sequence, 1);
  }
});

await check("payload and non-result saved inputs are captured before asynchronous effects", async () => {
  let release; let received;
  const r = registry([{ operation: "chat.create", capability: "chat.create", execute: payload => { received = payload; return new Promise(resolve => { release = resolve; }); } },
    { operation: "chat.send", capability: "chat.send", execute: () => true }]);
  const raw = profile(); raw.workflows[0].transitions[0].payload = { from: "inputs", pointer: "/title" };
  raw.workflows[0].transitions[0].save = { title: { from: "inputs", pointer: "/title" }, id: { from: "result", pointer: "/id" } };
  const p = m.registerProfile(r, raw); const inputs = { title: { value: "before" } };
  const mutableSession = JSON.parse(m.serializeProfileSession(m.createProfileSession(p, r)));
  const running = m.executeProfileAction(mutableSession, r, "new", { workflowId: "conversation", event: "create", bindings: { inputs } });
  inputs.title.value = "after"; received.value = "adapter-mutated-copy";
  mutableSession.sequence = 500; mutableSession.history.push({ fake: true }); mutableSession.workflows.conversation = "answered";
  release({ id: "c1" }); const result = await running;
  assert.deepEqual(result.session.variables.conversation, { title: { value: "before" }, id: "c1" });
  assert.equal(result.session.sequence, 1); assert.equal(result.session.history.length, 1); assert.equal(result.session.workflows.conversation, "ready");
  assert.deepEqual(inputs, { title: { value: "after" } });
});
console.log(`[application-profile-state] ${groups}/${groups} groups passed; offline, zero model calls`);
