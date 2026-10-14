#!/usr/bin/env node
// Offline server-snapshot contracts. Executes the real runtime/serializer with
// an in-memory whole-value config transport; no providers or paid model calls.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { webcrypto } from "node:crypto";
import ts from "typescript";
if (!globalThis.crypto?.subtle) globalThis.crypto = webcrypto;
const clone = value => JSON.parse(JSON.stringify(value));
function moduleUrl(file, replacements = []) {
  let source = ts.transpileModule(readFileSync(new URL(file, import.meta.url), "utf8"), {
    compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022 },
  }).outputText;
  for (const [from, to] of replacements) source = source.replaceAll(`from "${from}"`, `from "${to}"`);
  return `data:text/javascript;base64,${Buffer.from(source).toString("base64")}`;
}
const runtimeUrl = moduleUrl("../src/profiles/runtime.ts");
const viewUrl = moduleUrl("../src/profiles/view-state.ts");
const graphUrl = moduleUrl("../src/profiles/graph.ts", [["./runtime", runtimeUrl]]);
const snapshotUrl = moduleUrl("../src/profiles/workflow-snapshot.ts", [["./runtime", runtimeUrl], ["./view-state", viewUrl], ["./graph", graphUrl]]);
const runtime = await import(runtimeUrl);
const view = await import(viewUrl);
const snapshots = await import(snapshotUrl);
let groups = 0, calls = 0;
async function check(name, fn) { await fn(); groups++; console.log(`[workflow-snapshot] PASS ${name}`); }
function registry(includeCreate = true) {
  return runtime.createProfileRegistry({ adapters: [
    ...(includeCreate ? [{ operation: "chat.create", capability: "chat.create", execute: () => { calls++; return { id: "native-conversation-reference" }; } }] : []),
    { operation: "chat.select", capability: "chat.select", execute: () => { calls++; return "selected"; } },
  ] });
}
function profile() {
  return { schema: runtime.PROFILE_SCHEMA, id: "snapshot-fixture", profile_revision: 1, label: "Snapshot fixture",
    target: { application_id: "example.app", version: "2021", platform: "web" },
    evidence: { status: "partial", sources: [{ url: "https://example.org/source" }], gaps: ["remote state not authoritative"] },
    presentation: { renderer: "chat", tokens: { background: "#111", surface: "#222", text: "#fff", muted: "#aaa", accent: "#008080", border: "#444" },
      sidebar: { side: "left", width: 320 }, content_width: 820, message_style: "plain" },
    composer: { submit: "enter", placeholder: "Write" },
    actions: [{ id: "new", label: "Create", operation: "chat.create", capability: "chat.create", required: true },
      { id: "return", label: "Return", operation: "chat.select", capability: "chat.select", required: true }],
    workflows: [{ id: "chat", initial: "empty", states: ["empty", "ready"], transitions: [
      { from: "empty", event: "create", action: "new", to: "ready", save: { conversation_id: { from: "result", pointer: "/id" } } },
      { from: "ready", event: "return", action: "return", to: "ready", payload: { from: "vars", pointer: "/conversation_id" } },
    ] }],
  };
}
const host = registry();
const p = runtime.registerProfile(host, profile());
const profileKey = JSON.stringify([p.id, p.profile_revision]);
const source = `\uFEFF\r\n${JSON.stringify(profile(), null, 2)}\r\n`;
function draft(session = runtime.createProfileSession(p, host), recovery = { status: "ready" }) {
  return { profiles: [p], sources: [{ profile: profileKey, text: source, sourceRef: "fixture:exact-utf8-source" }],
    views: [view.newApplicationView("primary", profileKey)], sessions: [{ viewId: "primary", profile: profileKey, session,
      references: { graph_receipt: "receipt-fixture", conversation_id: "native-conversation-reference" }, recovery }],
    sharedConversationId: "native-conversation-reference" };
}
function make(input = draft(), id = "saved-session") {
  return snapshots.makeWorkflowSnapshot(input, host, { id, label: "Offline fixture", savedAt: "2026-10-04T12:50:00.000Z" });
}
function transport(initial = {}) {
  let config = clone(initial);
  const patches = [];
  return { patches,
    getConfig: async () => clone(config),
    setConfig: async patch => { patches.push(clone(patch)); config = { ...config, ...clone(patch) }; return clone(config); },
    writeOther: patch => { config = { ...config, ...clone(patch) }; },
    config: () => clone(config),
  };
}

await check("restart restores exact source bytes, current variables, trace and references without dispatch", async () => {
  const success = await runtime.executeProfileAction(runtime.createProfileSession(p, host), host, "new", { workflowId: "chat", event: "create" });
  const snapshot = make(draft(success.session));
  const api = transport({ unrelated_owner_setting: 987 });
  const before = calls;
  await snapshots.saveWorkflowSnapshot(api, snapshot, host);
  const restartRegistry = registry();
  const [restored] = await snapshots.loadWorkflowSnapshots(api, restartRegistry);
  assert.equal(calls, before);
  assert.equal(restored.sources[0].text, source);
  assert.equal(Buffer.from(restored.sources[0].text, "utf8").compare(Buffer.from(source, "utf8")), 0);
  assert.equal(restored.sessions[0].session.sequence, 1);
  assert.equal(restored.sessions[0].session.workflows.chat, "ready");
  assert.equal(restored.sessions[0].session.variables.chat.conversation_id, "native-conversation-reference");
  assert.deepEqual(restored.sessions[0].session.history, success.session.history);
  assert.deepEqual(restored.sessions[0].references, snapshot.sessions[0].references);
  assert.equal(api.config().unrelated_owner_setting, 987);
  assert.deepEqual(Object.keys(api.patches[0]), [snapshots.WORKFLOW_SNAPSHOT_CONFIG_KEY]);
});
await check("named snapshots coexist and replacing one uses whole-value config semantics", async () => {
  const api = transport();
  await snapshots.saveWorkflowSnapshot(api, make(draft(), "first"), host);
  await snapshots.saveWorkflowSnapshot(api, make(draft(), "second"), host);
  const replacement = make(draft(), "first"); replacement.label = "Explicit revision of saved session";
  await snapshots.saveWorkflowSnapshot(api, replacement, host);
  const list = await snapshots.loadWorkflowSnapshots(api, registry());
  assert.deepEqual(list.map(row => row.id).sort(), ["first", "second"]);
  assert.equal(list.find(row => row.id === "first").label, replacement.label);
  assert.equal(api.patches.length, 3);
});
await check("canonical definition/source drift and host same-revision drift are rejected without dispatch", () => {
  const snapshot = make(), original = JSON.stringify(snapshot), before = calls;
  const changedSource = clone(snapshot); changedSource.sources[0].text = JSON.stringify({ ...profile(), label: "drift" });
  assert.throws(() => snapshots.parseWorkflowSnapshot(changedSource, registry()), /revision|definition/);
  const changedHost = registry(); runtime.registerProfile(changedHost, { ...profile(), label: "different same-revision host definition" });
  assert.throws(() => snapshots.parseWorkflowSnapshot(snapshot, changedHost), /revision/);
  const missingSource = clone(snapshot); missingSource.sources = [];
  assert.throws(() => snapshots.parseWorkflowSnapshot(missingSource, registry()), /retained source/);
  assert.equal(JSON.stringify(snapshot), original); assert.equal(calls, before);
});
await check("changed snapshot state or saved variables cannot bypass trace replay", async () => {
  const success = await runtime.executeProfileAction(runtime.createProfileSession(p, host), host, "new", { workflowId: "chat", event: "create" });
  const snapshot = make(draft(success.session)); const before = calls;
  const wrongState = clone(snapshot); wrongState.sessions[0].session.workflows.chat = "empty";
  assert.throws(() => snapshots.parseWorkflowSnapshot(wrongState, registry()), /replayed history/);
  const wrongVariables = clone(snapshot); wrongVariables.sessions[0].session.variables.chat.conversation_id = "unrelated-conversation";
  assert.throws(() => snapshots.parseWorkflowSnapshot(wrongVariables, registry()), /selected-output history/);
  const badTrace = clone(snapshot); badTrace.sessions[0].session.history[0].operation = "chat.send";
  assert.throws(() => snapshots.parseWorkflowSnapshot(badTrace, registry()), /operation vocabulary/);
  assert.equal(calls, before);
});
await check("pending remote work survives restart as unknown and cannot continue automatically", async () => {
  const pending = make(draft(undefined, { status: "unknown", pending: { action: "new", workflowId: "chat", event: "create", reference: "remote-reconcile-reference" } }));
  const api = transport(); const before = calls;
  await snapshots.saveWorkflowSnapshot(api, pending, host);
  const [restored] = await snapshots.loadWorkflowSnapshots(api, registry());
  assert.equal(restored.sessions[0].recovery.status, "unknown");
  assert.equal(snapshots.workflowSnapshotCanContinue(restored.sessions[0]), false);
  assert.equal(restored.sessions[0].session.sequence, 0); assert.equal(calls, before);
  const invalid = clone(pending); invalid.sessions[0].recovery.status = "ready";
  assert.throws(() => snapshots.parseWorkflowSnapshot(invalid, registry()), /cannot hide a pending/);
});
await check("explicit reconciliation never fabricates success, and abandonment keeps workflow blocked", () => {
  const pending = make(draft(undefined, { status: "unknown", pending: { action: "new", workflowId: "chat", event: "create" } }));
  const before = calls;
  const resolved = snapshots.reconcileWorkflowSnapshot(pending, "primary", profileKey, "verified_not_applied", host);
  assert.equal(snapshots.workflowSnapshotCanContinue(resolved.sessions[0]), true);
  assert.deepEqual(resolved.sessions[0].session, pending.sessions[0].session);
  assert.equal(resolved.sessions[0].recovery.decision.choice, "verified_not_applied");
  const abandoned = snapshots.reconcileWorkflowSnapshot(pending, "primary", profileKey, "abandon", host);
  assert.equal(snapshots.workflowSnapshotCanContinue(abandoned.sessions[0]), false);
  assert.deepEqual(abandoned.sessions[0].session, pending.sessions[0].session);
  assert.equal(pending.sessions[0].recovery.status, "unknown"); assert.equal(calls, before);
  assert.throws(() => snapshots.reconcileWorkflowSnapshot(resolved, "primary", profileKey, "abandon", host), /No unresolved/);
});
await check("readback detects a lost write and retains the candidate including source bytes", async () => {
  const original = make(); const api = transport();
  const interruptedApi = { getConfig: api.getConfig, setConfig: async patch => {
    const result = await api.setConfig(patch);
    api.writeOther({ [snapshots.WORKFLOW_SNAPSHOT_CONFIG_KEY]: { schema: snapshots.WORKFLOW_SNAPSHOT_STORE_SCHEMA, snapshots: [] } });
    return result;
  } };
  await assert.rejects(snapshots.saveWorkflowSnapshot(interruptedApi, original, host), error => {
    assert.ok(error instanceof snapshots.WorkflowSnapshotWriteConflict);
    assert.equal(error.localSnapshot.sources[0].text, source);
    assert.deepEqual(error.localSnapshot.sessions, original.sessions);
    return true;
  });
  assert.equal(original.sources[0].text, source);
});
await check("malformed remote store is preserved instead of overwritten or silently repaired", async () => {
  const broken = { schema: "future/schema", snapshots: [] };
  const api = transport({ [snapshots.WORKFLOW_SNAPSHOT_CONFIG_KEY]: broken });
  await assert.rejects(snapshots.loadWorkflowSnapshots(api, host), /Unsupported/);
  await assert.rejects(snapshots.saveWorkflowSnapshot(api, make(), host), /Unsupported/);
  assert.equal(api.patches.length, 0);
  assert.deepEqual(api.config()[snapshots.WORKFLOW_SNAPSHOT_CONFIG_KEY], broken);
});
await check("current capability availability and malformed pending state fail before dispatch", () => {
  const before = calls;
  assert.throws(() => snapshots.parseWorkflowSnapshot(make(), registry(false)), /Required capabilities/);
  const pending = make(draft(undefined, { status: "unknown", pending: { action: "new", workflowId: "chat", event: "create" } }));
  const wrongAction = clone(pending); wrongAction.sessions[0].recovery.pending.action = "return";
  assert.throws(() => snapshots.parseWorkflowSnapshot(wrongAction, registry()), /declared workflow transition/);
  const readyState = clone(pending); readyState.sessions[0].recovery.pending = { action: "return", workflowId: "chat", event: "return" };
  assert.throws(() => snapshots.parseWorkflowSnapshot(readyState, registry()), /saved workflow state/);
  assert.equal(calls, before);
});
await check("JSON boundaries reject hidden values, nonfinite references and invalid Unicode without source mutation", () => {
  const undefinedReference = draft(); undefinedReference.sessions[0].references.extra = undefined;
  assert.throws(() => make(undefinedReference), /finite, acyclic JSON/);
  const nonfinite = draft(); nonfinite.sessions[0].references.tokens = Infinity;
  assert.throws(() => make(nonfinite), /finite, acyclic JSON/);
  const invalidUnicode = draft(); invalidUnicode.sources[0].text += "\ud800";
  assert.throws(() => make(invalidUnicode), /invalid Unicode/);
  const accessor = draft(); Object.defineProperty(accessor.sessions[0].references, "hidden", { get() { throw Error("accessor executed"); }, enumerable: true });
  assert.throws(() => make(accessor), /non-data field/);
});
await check("every view has exactly one selected-profile session; missing, duplicate, mismatched and extra sessions are rejected", () => {
  const snapshot = make(), before = calls;
  const missing = clone(snapshot); missing.sessions = [];
  assert.throws(() => snapshots.parseWorkflowSnapshot(missing, registry()), /Each snapshot view must have exactly one session/);
  const missingSecond = clone(snapshot); missingSecond.views.push(view.newApplicationView("secondary", profileKey));
  assert.throws(() => snapshots.parseWorkflowSnapshot(missingSecond, registry()), /missing session/);
  const duplicate = clone(snapshot); duplicate.sessions.push(clone(duplicate.sessions[0]));
  assert.throws(() => snapshots.parseWorkflowSnapshot(duplicate, registry()), /repeats a workflow session identity/);
  const alternate = { ...profile(), id: "alternate-profile" };
  const alternateKey = JSON.stringify([alternate.id, alternate.profile_revision]);
  const mismatched = clone(snapshot);
  mismatched.profiles.push(alternate);
  mismatched.sources.push({ profile: alternateKey, text: JSON.stringify(alternate), sourceRef: "fixture:alternate-profile" });
  mismatched.sessions[0].profile = alternateKey;
  assert.throws(() => snapshots.parseWorkflowSnapshot(mismatched, registry()), /profile must match the profile currently selected/);
  const extraHistorical = clone(mismatched); extraHistorical.sessions.unshift(clone(snapshot.sessions[0]));
  assert.throws(() => snapshots.parseWorkflowSnapshot(extraHistorical, registry()), /historical or extra profile sessions/);
  const complete = clone(snapshot);
  complete.views.push(view.newApplicationView("secondary", profileKey));
  complete.sessions.push({ ...clone(snapshot.sessions[0]), viewId: "secondary" });
  assert.equal(snapshots.parseWorkflowSnapshot(complete, registry()).sessions.length, 2);
  assert.equal(calls, before); assert.equal(snapshot.sources[0].text, source);
});
if (process.argv.includes("--browser")) {
  const { createServer } = await import("vite");
  const { default: react } = await import("@vitejs/plugin-react");
  const { chromium } = await import("playwright");
  const { fileURLToPath } = await import("node:url");
  const path = await import("node:path");
  const pending = make(draft(undefined, { status: "unknown", pending: { action: "new", workflowId: "chat", event: "create", reference: "inspect-remote-reference" } }));
  let serverConfig = { [snapshots.WORKFLOW_SNAPSHOT_CONFIG_KEY]: { schema: snapshots.WORKFLOW_SNAPSHOT_STORE_SCHEMA, snapshots: [pending] } };
  const configWrites = [];
  let serverGets = 0;
  const html = `<!doctype html><html><body><div id="root"></div><script type="module">
    import React from 'react';
    import {createRoot} from 'react-dom/client';
    import Panel from '/src/components/WorkflowRecoveryPanel.tsx';
    import {createProfileRegistry,registerProfile,createProfileSession} from '/src/profiles/runtime.ts';
    import {newApplicationView} from '/src/profiles/view-state.ts';
    import {workflowSnapshotCanContinue} from '/src/profiles/workflow-snapshot.ts';
    window.fixture={dispatches:0,restores:[],requests:[]};
    const registry=createProfileRegistry({adapters:[
      {operation:'chat.create',capability:'chat.create',execute:()=>{window.fixture.dispatches++;return {id:'never-dispatched'};}},
      {operation:'chat.select',capability:'chat.select',execute:()=>{window.fixture.dispatches++;return 'never-dispatched';}}
    ]});
    const profile=registerProfile(registry,${JSON.stringify(profile())});
    const profileKey=JSON.stringify([profile.id,profile.profile_revision]);
    const value={profiles:[profile],sources:[{profile:profileKey,text:${JSON.stringify(source)},sourceRef:'fixture:exact-utf8-source'}],
      views:[newApplicationView('primary',profileKey)],sessions:[{viewId:'primary',profile:profileKey,
        session:createProfileSession(profile,registry),recovery:{status:'ready'}}],sharedConversationId:null};
    const api={getConfig:async()=>{window.fixture.requests.push('get');return await (await fetch('/__fixture_config')).json();},
      setConfig:async patch=>{window.fixture.requests.push('set');return await (await fetch('/__fixture_config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(patch)})).json();}};
    createRoot(document.getElementById('root')).render(React.createElement(Panel,{api,registry,getValue:()=>value,
      onRestore:snapshot=>{window.fixture.restores.push({snapshot,canContinue:workflowSnapshotCanContinue(snapshot.sessions[0])});}}));
  </script></body></html>`;
  const vite = await createServer({ root: path.resolve(path.dirname(fileURLToPath(import.meta.url)), ".."), configFile: false,
    plugins: [react(), { name: "workflow-snapshot-browser-fixture", configureServer(server) {
      server.middlewares.use(async (request, response, next) => {
        if (request.url === "/__fixture_config") {
          if (request.method === "POST") {
            const parts = []; for await (const chunk of request) parts.push(chunk);
            const patch = JSON.parse(Buffer.concat(parts).toString()); configWrites.push(clone(patch)); serverConfig = { ...serverConfig, ...patch };
          } else serverGets++;
          response.setHeader("Content-Type", "application/json"); response.end(JSON.stringify(serverConfig)); return;
        }
        if (request.url === "/__workflow_fixture.html") {
          response.setHeader("Content-Type", "text/html"); response.end(await server.transformIndexHtml(request.url, html)); return;
        }
        next();
      });
    } }], server: { host: "127.0.0.1", port: 0 } });
  let browser;
  const pageErrors = [];
  try {
    await vite.listen();
    const address = vite.httpServer.address();
    const url = `http://127.0.0.1:${address.port}/__workflow_fixture.html`;
    browser = await chromium.launch({ headless: true, args: ["--no-sandbox"] });
    const page = await browser.newPage();
    page.on("pageerror", error => pageErrors.push(error.message));
    await page.goto(url);
    try { await page.getByTestId("workflow-recovery-panel").locator("summary").first().click(); }
    catch (error) { throw Error(`${error.message}\nBrowser errors: ${JSON.stringify(pageErrors)}`); }
    await check("browser panel performs no implicit reads/dispatch and read-only unknown recovery is explicit", async () => {
      assert.equal(await page.evaluate(() => window.fixture.dispatches), 0);
      assert.equal(serverGets, 0);
      await page.getByTestId("load-workflow-snapshots").click();
      await page.getByText("remote outcome unknown — workflow blocked", { exact: false }).waitFor();
      await page.getByTestId("restore-workflow-snapshot").click();
      await page.getByRole("status").filter({ hasText: "Navigation restored" }).waitFor();
      const result = await page.evaluate(() => window.fixture.restores[0]);
      assert.equal(result.canContinue, false); assert.equal(result.snapshot.sessions[0].recovery.status, "unknown");
      assert.equal(result.snapshot.sources[0].text, source); assert.equal(result.snapshot.sessions[0].session.sequence, 0);
      assert.equal(await page.evaluate(() => window.fixture.dispatches), 0); assert.equal(configWrites.length, 0);
    });
    await check("browser reconciliation is staged, saved explicitly and survives browser restart without execution", async () => {
      await page.getByTestId("reconcile-workflow-not-applied").click();
      await page.getByRole("status").filter({ hasText: "Recovery choice staged" }).waitFor();
      assert.equal(configWrites.length, 0);
      await page.getByRole("button", { name: "Save inspected recovery decision" }).click();
      await page.getByRole("status").filter({ hasText: "saved and read back" }).waitFor();
      assert.equal(configWrites.length, 1);
      await page.reload();
      await page.getByTestId("workflow-recovery-panel").locator("summary").first().click();
      await page.getByTestId("load-workflow-snapshots").click();
      await page.getByLabel("Saved workflow snapshot", { exact: true }).waitFor();
      await page.getByTestId("restore-workflow-snapshot").click();
      await page.getByRole("status").filter({ hasText: "Session restored without" }).waitFor();
      const result = await page.evaluate(() => window.fixture.restores[0]);
      assert.equal(result.canContinue, true); assert.equal(result.snapshot.sessions[0].recovery.decision.choice, "verified_not_applied");
      assert.equal(result.snapshot.sessions[0].session.sequence, 0); assert.equal(await page.evaluate(() => window.fixture.dispatches), 0);
    });
    assert.deepEqual(pageErrors, []);
  } finally { if (browser) await browser.close(); await vite.close(); }
}
console.log(`[workflow-snapshot] ${groups} groups passed; persistence/restore never dispatched an adapter.`);
