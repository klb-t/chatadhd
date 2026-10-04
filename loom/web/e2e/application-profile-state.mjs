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
  for (const submit of ["enter", "mod-enter"]) {
    assert.equal(m.shouldSubmit(submit, { key: "Enter", ctrlKey: true, shiftKey: true }), false);
    assert.equal(m.shouldSubmit(submit, { key: "Enter", ctrlKey: true, isComposing: true }), false);
    assert.equal(m.shouldSubmit(submit, { key: "a", ctrlKey: true }), false);
  }
});
console.log(`[application-profile-state] ${groups}/${groups} groups passed; offline, zero model calls`);
