#!/usr/bin/env node
// Actual W10 host adapter and W12 projection, with authored native-wire fixtures.
// These tests do not claim execution of C++ privacy/retention or a provider.
import assert from "node:assert/strict";
import { readFileSync, mkdirSync, existsSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { createServer } from "node:http";
import { createHash } from "node:crypto";
import { build } from "esbuild";
import { chromium } from "playwright";
import ts from "typescript";
import { closeHttpFixture, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const sourceFiles = ["onboarding-host.mjs", "harness-lifecycle.mjs", "../src/api/onboarding-host.ts", "../src/api/loom-http.ts", "../src/api/loom-api.ts",
  "../src/api/operations.ts", "../src/api/types.ts", "../src/components/UserProfilePanel.tsx", "../src/components/user-profile-panel.css",
  ...["index.ts", "types.ts", "native-snapshot.ts", "controller.ts", "OnboardingPanel.tsx", "WhatAppKnows.tsx", "onboarding.css"].map(file => `../src/onboarding/${file}`),
  "../../data/profiles/user.pack", "../../data/onboarding/scenario.pack"];
const sourceSnapshot = () => Object.fromEntries(sourceFiles.map(file => [file,
  createHash("sha256").update(readFileSync(new URL(file, import.meta.url))).digest("hex")]));
const sourcesBefore = sourceSnapshot();

function moduleUrl(file, replacements = []) {
  let source = ts.transpileModule(readFileSync(new URL(file, import.meta.url), "utf8"), {
    compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022 },
  }).outputText;
  for (const [from, to] of replacements) source = source.replaceAll(`from "${from}"`, `from "${to}"`);
  return `data:text/javascript;base64,${Buffer.from(source).toString("base64")}`;
}
const nativeUrl = moduleUrl("../src/onboarding/native-snapshot.ts");
const { createUserProfileHost, NativeProfileSession, USER_PROFILE_IDENTITY_KEY } = await import(moduleUrl(
  "../src/api/onboarding-host.ts", [["../onboarding/native-snapshot", nativeUrl]],
));
const pack = JSON.parse(readFileSync(new URL("../../data/profiles/user.pack", import.meta.url), "utf8"));
const scenario = JSON.parse(readFileSync(new URL("../../data/onboarding/scenario.pack", import.meta.url), "utf8"));
const clone = value => structuredClone(value);
function nativeSnapshot(userId, exactRevision = "0") {
  const firstSection = scenario.sections[0].id;
  return {
    schema: "loom.onboarding_store/1", user_id: userId, revision: Number(exactRevision),
    graph_run_id: `authored-native-run:${userId}`, pack: clone(pack), scenario_definition: clone(scenario),
    profile: {
      fields: Object.fromEntries(scenario.fields.map(field => [field.id, { status: "unknown", category: field.category }])),
      privacy: clone(pack.entries.find(entry => entry.key === "onboarding.privacy").value),
      settings: { preference_mode: "ask" }, candidates: {}, history: [],
      session: { status: "active", section: firstSection,
        sections: Object.fromEntries(scenario.sections.map(section => [section.id, { status: "pending", summary: null }])) },
    }, layers: { areas: {}, history: [] }, effectiveDefaults: [],
  };
}
function fixture(startRevision = "0") {
  const records = new Map(), calls = [];
  let handler = null;
  const response = userId => {
    const record = records.get(userId);
    return { snapshot: clone(record.snapshot), revision: record.revision };
  };
  const api = { onboarding: async (command, options) => {
    calls.push({ command: clone(command), options });
    const user = command.user_id;
    if (!records.has(user)) records.set(user, { revision: startRevision, snapshot: nativeSnapshot(user, startRevision) });
    if (handler) return handler(command, options, response);
    if (command.operation === "open" || command.operation === "read") return response(user);
    const record = records.get(user);
    if (command.expected_revision !== record.revision) throw Object.assign(new Error("Authored native CAS conflict"), { status: 409 });
    record.revision = String(BigInt(record.revision) + 1n);
    record.snapshot.revision = Number(record.revision);
    if (command.operation === "update_pack") {
      record.snapshot.pack = clone(command.pack); record.snapshot.scenario_definition = clone(command.scenario);
    } else {
      const action = command.action;
      if (action.target === "layers") record.snapshot.layers.history.push(clone(action));
      else {
        record.snapshot.profile.history.push(clone(action));
        if (action.op === "answer") record.snapshot.profile.candidates[action.id] = { ...clone(action), review: "pending" };
      }
    }
    return response(user);
  } };
  return { api, records, calls, response, setHandler(value) { handler = value; } };
}
function storage() {
  const records = new Map();
  return { records, getItem: key => records.get(key) ?? null, setItem: (key, value) => records.set(key, value), removeItem: key => records.delete(key) };
}
function deferred() { let resolve; const promise = new Promise(done => { resolve = done; }); return { promise, resolve }; }
const nextTurn = () => new Promise(done => setImmediate(done));
const groups = [];
const pureOnly = process.argv.includes("--pure");
const completion = suiteCompletionGuard("onboarding-host", pureOnly ? 15 : 21, groups);
const directory = process.env.ONBOARDING_HOST_EVIDENCE_DIR ?? path.join(fileURLToPath(new URL("../../../../", import.meta.url)), "onboarding-host-verification-2026-10-05");
async function check(name, run) { await run(); groups.push(name); console.log(`[onboarding-host] PASS ${name}`); }
const field = scenario.fields[0].id;
const answer = (id, value = "authored synthetic value") => ({ op: "answer", field, value, provenance: "form", id, time: "2026-10-05T00:00:00Z", source_refs: [] });

await check("explicit identity is the only persisted value; construction performs no native or model call", async () => {
  const f = fixture(), saved = storage(), host = createUserProfileHost(f.api, { storage: saved });
  assert.equal(host.getState().userId, null); assert.equal(host.getAdapter(), null); assert.equal(f.calls.length, 0);
  host.selectUser("authored-user"); assert.equal(f.calls.length, 0);
  const adapter = host.getAdapter(); await adapter.getSnapshot();
  assert.equal(adapter.modelRequest, undefined); assert.equal(adapter.completeModelRequest, undefined); assert.equal(adapter.ingestModelReply, undefined);
  assert.deepEqual(JSON.parse(saved.records.get(USER_PROFILE_IDENTITY_KEY)), { schema: "loom.user_profile_identity/1", user_id: "authored-user" });
  assert.deepEqual([...saved.records.keys()], [USER_PROFILE_IDENTITY_KEY]);
  assert.deepEqual(f.calls.map(call => call.command.operation), ["open"]);
  host.reload(); assert.equal(host.getAdapter(), adapter); await adapter.getSnapshot();
  assert.equal(f.calls.at(-1).command.operation, "read");
});

await check("empty, malformed and extra-field identity settings preserve their exact bytes", () => {
  for (const bytes of ["", "{broken", "null", JSON.stringify({ schema: "future/2", user_id: "x" }),
    JSON.stringify({ schema: "loom.user_profile_identity/1", user_id: " " }),
    JSON.stringify({ schema: "loom.user_profile_identity/1", user_id: "x", profile: { private: true } })]) {
    const saved = storage(); saved.records.set(USER_PROFILE_IDENTITY_KEY, bytes);
    const host = createUserProfileHost(fixture().api, { storage: saved });
    assert.equal(host.getState().userId, null); assert.match(host.getState().storageError, /preserved/);
    assert.equal(saved.records.get(USER_PROFILE_IDENTITY_KEY), bytes);
  }
  const saved = storage(); saved.records.set(USER_PROFILE_IDENTITY_KEY, JSON.stringify({ schema: "loom.user_profile_identity/1", user_id: "persisted-user" }));
  const f = fixture(), host = createUserProfileHost(f.api, { storage: saved });
  assert.equal(host.getState().userId, "persisted-user"); assert.equal(f.calls.length, 0);
});

await check("actual native pack snapshot projects into W12 and exact int64 CAS exceeds JavaScript's integer range", async () => {
  const f = fixture("9007199254740993"), session = new NativeProfileSession("authored-user", f.api);
  const projected = await session.adapter.getSnapshot();
  assert.equal(projected.scenario.id, scenario.id); assert.equal(projected.fields[field].status, "unknown");
  assert.deepEqual(projected.scenario.source, scenario);
  await session.adapter.dispatch(answer("large-cas"));
  assert.equal(f.calls.at(-1).command.expected_revision, "9007199254740993");
  assert.equal(session.state().revision, "9007199254740994");
});

await check("candidate review uses a distinct native event ID while answer identity and acquisition stay unchanged", async () => {
  const f = fixture(), session = new NativeProfileSession("authored-user", f.api); await session.adapter.getSnapshot();
  await session.adapter.dispatch(answer("candidate-user-answer"));
  assert.equal(f.calls.at(-1).command.action.id, "candidate-user-answer");
  assert.equal(f.calls.at(-1).command.action.provenance, "form");
  await session.adapter.dispatch({ op: "review", id: "candidate-user-answer", decision: "confirmed" });
  const action = f.calls.at(-1).command.action;
  assert.equal(action.candidate, "candidate-user-answer"); assert.notEqual(action.id, action.candidate); assert.equal(action.target, "profile");
});

await check("layer exclusion and area policy use stable keys without flattening distinct operations", async () => {
  const f = fixture(), session = new NativeProfileSession("authored-user", f.api); await session.adapter.getSnapshot();
  for (const op of ["disable", "exclude", "reenable", "clear_override", "accept_proposal"]) {
    await session.adapter.dispatchLayer({ op, key: "authored.default.stable-key" });
    assert.equal(f.calls.at(-1).command.action.key, "authored.default.stable-key");
    assert.equal(f.calls.at(-1).command.action.op, op); assert.equal(f.calls.at(-1).command.action.target, "layers");
  }
  await session.adapter.dispatchLayer({ op: "set_area_mode", area: "authored.area", mode: "proposal" });
  assert.equal(f.calls.at(-1).command.action.target, "layers");
});

await check("scenario editing preserves the full native pack and captured input bytes", async () => {
  const f = fixture(); const initial = nativeSnapshot("authored-user"); initial.pack.authored_extension = { retained: "exact source" };
  f.records.set("authored-user", { revision: "0", snapshot: initial });
  const session = new NativeProfileSession("authored-user", f.api); await session.adapter.getSnapshot();
  const edited = clone(scenario); edited.prompt += "\nAuthored offline variation";
  const before = clone(edited), pending = session.adapter.saveScenario(edited);
  edited.prompt = "Changed after invocation"; await pending;
  const command = f.calls.at(-1).command;
  assert.deepEqual(command.scenario, before); assert.deepEqual(command.pack, initial.pack);
  assert.equal(command.operation, "update_pack");
});

await check("queued edits retain invocation CAS and do not silently overwrite after an earlier mutation", async () => {
  const f = fixture(), session = new NativeProfileSession("authored-user", f.api); await session.adapter.getSnapshot();
  const first = session.adapter.dispatch(answer("first"));
  const second = assert.rejects(session.adapter.dispatch(answer("second")), error => error.status === 409);
  await first; await second;
  assert.equal(f.calls.filter(call => call.command.operation === "apply").length, 1);
  assert.equal(session.state().reloadRequired, true);
  await session.adapter.getSnapshot(); await session.adapter.dispatch(answer("after-reload"));
  assert.equal(f.calls.at(-1).command.expected_revision, "1");
});

await check("HTTP conflict is not retried and the queue recovers only through explicit native reload", async () => {
  const f = fixture(), session = new NativeProfileSession("authored-user", f.api); await session.adapter.getSnapshot();
  f.records.get("authored-user").revision = "1"; f.records.get("authored-user").snapshot.revision = 1;
  await assert.rejects(session.adapter.dispatch(answer("conflict")), error => error.status === 409);
  assert.equal(session.state().outcomeUnknown, true);
  assert.equal(f.calls.filter(call => call.command.operation === "apply").length, 1);
  await assert.rejects(session.adapter.dispatch(answer("blocked")), /Reload/);
  assert.equal(f.calls.filter(call => call.command.operation === "apply").length, 1);
  await session.adapter.getSnapshot(); await session.adapter.dispatch(answer("fresh"));
  assert.equal(f.calls.at(-1).command.expected_revision, "1");
});

await check("lost write response leaves an unknown outcome and never creates an automatic replay", async () => {
  const f = fixture(), session = new NativeProfileSession("authored-user", f.api); await session.adapter.getSnapshot();
  f.setHandler(async command => {
    assert.equal(command.operation, "apply");
    const record = f.records.get(command.user_id); record.revision = "1"; record.snapshot.revision = 1;
    throw new TypeError("Authored connection loss after native commit");
  });
  await assert.rejects(session.adapter.dispatch(answer("lost")), /outcome is unknown/);
  assert.equal(session.state().outcomeUnknown, true); assert.equal(session.state().revision, null);
  const before = f.calls.length; await assert.rejects(session.adapter.dispatch(answer("blocked")), /Reload/); assert.equal(f.calls.length, before);
  f.setHandler(null); await session.adapter.getSnapshot(); assert.equal(session.state().revision, "1");
  assert.equal(session.state().outcomeUnknown, true); assert.equal(f.calls.at(-1).command.operation, "read");
});

await check("aborting a sent mutation does not discard its native revision or forward an undo signal", async () => {
  const f = fixture(), session = new NativeProfileSession("authored-user", f.api); await session.adapter.getSnapshot();
  const gate = deferred(); f.setHandler(async (command, options, response) => {
    assert.equal(options?.signal, undefined); await gate.promise;
    const record = f.records.get(command.user_id); record.revision = "1"; record.snapshot.revision = 1; return response(command.user_id);
  });
  const controller = new AbortController(), pending = session.adapter.dispatch(answer("delayed"), { signal: controller.signal });
  await nextTurn(); controller.abort(); gate.resolve(); await pending;
  assert.equal(session.state().revision, "1"); assert.equal(session.state().outcomeUnknown, false);
  f.setHandler(null); const before = f.calls.length;
  await assert.rejects(session.adapter.dispatch(answer("not-sent"), { signal: controller.signal }), error => error.name === "AbortError");
  assert.equal(f.calls.length, before); assert.equal(session.state().revision, "1");
});

await check("a user switch suppresses late old-view callbacks and balances native activity", async () => {
  const f = fixture(), saved = storage(), activity = [], host = createUserProfileHost(f.api, { storage: saved, onActivity: delta => activity.push(delta) });
  host.selectUser("old-user"); const old = host.getAdapter(); await old.getSnapshot();
  const gate = deferred(); f.setHandler(async (command, _options, response) => {
    if (command.operation === "open") return response(command.user_id);
    await gate.promise; const record = f.records.get(command.user_id); record.revision = "1"; record.snapshot.revision = 1; return response(command.user_id);
  });
  const pending = assert.rejects(old.dispatch(answer("old-write")), error => error.name === "AbortError");
  await nextTurn(); host.selectUser("new-user"); await host.getAdapter().getSnapshot();
  const current = host.getState(); gate.resolve(); await pending;
  assert.equal(host.getState(), current); assert.equal(host.getState().userId, "new-user");
  assert.equal(host.getState().session.revision, "0"); assert.equal(f.records.get("old-user").revision, "1");
  assert.equal(activity.reduce((sum, delta) => sum + delta, 0), 0);
});

await check("privacy actions preserve extensions and are copied before entering the queue", async () => {
  const f = fixture(), session = new NativeProfileSession("authored-user", f.api); const snap = await session.adapter.getSnapshot();
  const rule = { ...clone(snap.privacy[0]), authored_extension: { preserved: true } };
  const before = clone(rule), pending = session.adapter.dispatch({ op: "privacy", rule });
  rule.authored_extension.preserved = false; await pending;
  assert.deepEqual(f.calls.at(-1).command.action.rule, before);
});

await check("invalid envelopes cannot establish a revision and malformed success invalidates a sent write", async () => {
  for (const corrupt of [response => { response.revision = 0; }, response => { response.revision = "-1"; },
    response => { response.revision = "1.5"; }, response => { response.revision = "01"; },
    response => { response.revision = "9223372036854775808"; }, response => { response.snapshot.user_id = "wrong-user"; },
    response => { response.snapshot.schema = "future/2"; }, response => { response.snapshot.revision = 0.5; },
    response => { delete response.snapshot.revision; }, response => { response.snapshot.profile.session.sections = []; },
    response => { response.snapshot.profile.fields[field] = null; },
    response => { response.snapshot.profile.privacy.rules[0].providers = null; },
    response => { response.snapshot.profile.session.latest_reply = { section: "x", summary: "authored", questions: null, candidates: [] }; }]) {
    const f = fixture(), session = new NativeProfileSession("authored-user", f.api);
    f.setHandler(async (command, _options, response) => { const result = response(command.user_id); corrupt(result); return result; });
    await assert.rejects(session.adapter.getSnapshot()); assert.equal(session.state().revision, null);
  }
  const f = fixture(), session = new NativeProfileSession("authored-user", f.api); await session.adapter.getSnapshot();
  f.setHandler(async (command, _options, response) => response(command.user_id)); // Invalid 200: CAS did not advance.
  await assert.rejects(session.adapter.dispatch(answer("malformed-success")), /outcome is unknown/);
  assert.equal(session.state().outcomeUnknown, true); assert.equal(session.state().reloadRequired, true);
});

await check("lost initialization response is reported and idempotent open reloads the same explicit identity", async () => {
  const f = fixture(), session = new NativeProfileSession("authored-user", f.api);
  f.setHandler(async () => { throw new TypeError("Authored response loss after profile initialization"); });
  await assert.rejects(session.adapter.getSnapshot()); assert.equal(session.state().outcomeUnknown, true);
  f.setHandler(null); await session.adapter.getSnapshot();
  assert.deepEqual(f.calls.map(call => call.command), [
    { operation: "open", user_id: "authored-user" }, { operation: "open", user_id: "authored-user" },
  ]);
  assert.equal(f.records.size, 1); assert.equal(session.state().revision, "0");
});

await check("failed identity persistence retains prior settings and never falls back to browser profile storage", async () => {
  const saved = storage(); saved.records.set(USER_PROFILE_IDENTITY_KEY, JSON.stringify({ schema: "loom.user_profile_identity/1", user_id: "prior-user" }));
  const bytes = saved.records.get(USER_PROFILE_IDENTITY_KEY); saved.setItem = () => { throw new DOMException("Authored quota failure", "QuotaExceededError"); };
  const f = fixture(), host = createUserProfileHost(f.api, { storage: saved }); host.selectUser("current-user"); await host.getAdapter().getSnapshot();
  assert.equal(saved.records.get(USER_PROFILE_IDENTITY_KEY), bytes); assert.equal(host.getState().userId, "current-user");
  assert.match(host.getState().storageError, /could not be saved/); assert.equal(saved.records.size, 1);
  const adapter = host.getAdapter(); saved.setItem = (key, value) => saved.records.set(key, value);
  host.selectUser("current-user"); assert.equal(host.getAdapter(), adapter); assert.equal(host.getState().storageError, null);
  assert.deepEqual(JSON.parse(saved.records.get(USER_PROFILE_IDENTITY_KEY)), { schema: "loom.user_profile_identity/1", user_id: "current-user" });
  const absent = createUserProfileHost({}, { storage: storage() }); absent.selectUser("unavailable-user");
  assert.equal(absent.available, false); assert.equal(absent.getAdapter(), null);
});

const browserErrors = [], externalRequests = [], browserCommands = [];
if (!pureOnly) {
  const web = fileURLToPath(new URL("../", import.meta.url));
  const bundle = await build({ stdin: { contents: `
    import React, {useState} from "react";
    import {createRoot} from "react-dom/client";
    import UserProfilePanel from "./src/components/UserProfilePanel";
    import {createUserProfileHost} from "./src/api/onboarding-host";
    import {LoomHttpApi} from "./src/api/loom-http";
    const host = createUserProfileHost(new LoomHttpApi(), {onActivity: delta => {window.__nativeActivity = (window.__nativeActivity ?? 0) + delta;}});
    function Shell() {
      const [view, setView] = useState("onboarding");
      return <><nav><button data-testid="fixture-onboarding" onClick={() => setView("onboarding")}>Onboarding</button><button data-testid="fixture-knowledge" onClick={() => setView("knowledge")}>Knowledge</button></nav><UserProfilePanel host={host} view={view}/></>;
    }
    createRoot(document.getElementById("root")).render(<Shell/>);
  `, resolveDir: web, loader: "tsx" }, bundle: true, write: false, format: "iife", platform: "browser",
    outfile: "/tmp/loom-onboarding-host-component.js", jsx: "automatic" });
  const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
  const css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";
  const wire = fixture();
  const defaultRow = { id: "authored-versioned-default-id", key: "preference.style", area: "communication",
    layer: "builtin", explanation: "Authored default resolution", status: "effective", entity: { label: "Authored layered default" }, value: "brief" };
  let loseNextWrite = false, holdNextWrite = false, heldResponse = null;
  const held = deferred();
  const server = createServer(async (req, res) => {
    if (req.url === "/component.js") { res.setHeader("Content-Type", "text/javascript"); res.end(js); return; }
    if (req.url === "/component.css") { res.setHeader("Content-Type", "text/css"); res.end(css); return; }
    if (req.url === "/api/onboarding") {
      const chunks = []; for await (const chunk of req) chunks.push(chunk);
      const command = JSON.parse(Buffer.concat(chunks).toString()); browserCommands.push(clone(command));
      try {
        if (!wire.records.has(command.user_id)) {
          const snapshot = nativeSnapshot(command.user_id);
          snapshot.effectiveDefaults = [clone(defaultRow)];
          snapshot.layers.areas.communication = { new_defaults_mode: "proposal" };
          wire.records.set(command.user_id, { revision: "0", snapshot });
        }
        const result = await wire.api.onboarding(command);
        const record = wire.records.get(command.user_id);
        if (command.operation === "apply") {
          const action = command.action;
          if (action.op === "review") {
            const candidate = record.snapshot.profile.candidates[action.candidate];
            candidate.review = action.decision;
            if (action.decision === "confirmed") record.snapshot.profile.fields[candidate.field] = {
              ...record.snapshot.profile.fields[candidate.field], status: "known", value: candidate.value,
              provenance: candidate.provenance, review: "confirmed",
            };
          }
          if (action.target === "layers") {
            const row = record.snapshot.effectiveDefaults.find(entry => entry.key === action.key);
            if (row) row.status = action.op === "exclude" ? "excluded" : action.op === "disable" ? "disabled" : "effective";
          }
        }
        const current = command.operation === "apply" ? wire.response(command.user_id) : result;
        const reply = () => { if (!res.destroyed) { res.setHeader("Content-Type", "application/json"); res.end(JSON.stringify(current)); } };
        if (command.operation === "apply" && loseNextWrite) { loseNextWrite = false; res.destroy(); return; }
        if (command.operation === "apply" && holdNextWrite) { holdNextWrite = false; heldResponse = reply; held.resolve(); return; }
        reply();
      } catch (error) {
        res.writeHead(error.status ?? 500, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ error: { code: "authored_failure", message: error.message } }));
      }
      return;
    }
    if (req.url?.startsWith("/api/")) { res.writeHead(404).end(); return; }
    res.setHeader("Content-Type", "text/html");
    res.end('<!doctype html><html><head><link rel="stylesheet" href="/component.css"></head><body><main style="max-width:1000px;margin:auto"><div id="root"></div></main><script src="/component.js"></script></body></html>');
  });
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  let browser, page;
  try {
    browser = await chromium.launch({ headless: true,
      ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}) });
    page = await browser.newPage({ viewport: { width: 1300, height: 1000 } });
    page.on("pageerror", error => browserErrors.push(error.message));
    await page.route("**/*", async route => {
      if (new URL(route.request().url()).origin !== base) { externalRequests.push(route.request().url()); await route.abort(); }
      else await route.continue();
    });
    const select = async user => {
      await page.getByTestId("user-profile-identity").fill(user); await page.getByTestId("user-profile-open").click();
      await page.getByTestId("user-profile-selected").filter({ hasText: user }).waitFor();
      await page.getByTestId("user-profile-revision").filter({ hasText: "0" }).waitFor();
    };
    const firstInput = () => page.locator(`input[id="onboarding-${field}"]`);
    const submit = async value => {
      await firstInput().fill(value);
      await page.locator("fieldset").filter({ has: firstInput() }).getByRole("button", { name: "Submit for confirmation", exact: true }).click();
    };
    const waitRevision = async value => {
      await page.waitForFunction(value => document.querySelector('[data-testid="user-profile-revision"]')?.textContent === value, value);
    };
    await page.goto(base);
    await check("browser exposes explicit identity without mount-time model calls and uses actual HTTP transport", async () => {
      assert.equal(browserCommands.length, 0); await select("browser-user");
      await page.getByTestId("onboarding-panel").waitFor();
      assert.equal(await page.getByRole("button", { name: "Ask model to summarise and continue" }).isDisabled(), true);
      assert.deepEqual(browserCommands.map(command => command.operation), ["open"]);
    });
    await check("browser form stages and reviews native-bound candidates without rewriting acquisition", async () => {
      await page.getByTestId("onboarding-panel").getByRole("button", { name: "Form", exact: true }).click();
      await submit("Authored browser profile value"); await waitRevision("1");
      await page.getByTestId("onboarding-panel").getByRole("button", { name: "Confirm", exact: true }).click(); await waitRevision("2");
      const [answerCommand, reviewCommand] = browserCommands.filter(command => command.operation === "apply");
      assert.equal(answerCommand.action.provenance, "form"); assert.equal(reviewCommand.action.candidate, answerCommand.action.id);
      assert.notEqual(reviewCommand.action.id, reviewCommand.action.candidate);
    });
    await check("browser view switches and restart read the same identity while browser storage holds no profile facts", async () => {
      await page.getByTestId("fixture-knowledge").click(); await page.getByTestId("what-app-knows").waitFor();
      await waitRevision("2"); assert.equal(browserCommands.at(-1).operation, "read");
      assert.match(await page.getByTestId("what-app-knows").textContent(), /Authored browser profile value/);
      const stored = await page.evaluate(() => ({ ...localStorage }));
      assert.deepEqual(Object.keys(stored), [USER_PROFILE_IDENTITY_KEY]); assert.ok(!JSON.stringify(stored).includes("Authored browser profile value"));
      await page.reload(); await page.getByTestId("onboarding-panel").waitFor(); await waitRevision("2");
      assert.equal(browserCommands.at(-1).user_id, "browser-user");
    });
    await check("browser permanent exclusion sends the stable layer key and requires explicit reenabling", async () => {
      await page.getByTestId("fixture-knowledge").click(); await page.getByTestId("what-app-knows").waitFor();
      const row = page.locator("fieldset").filter({ has: page.locator("legend").filter({ hasText: "Authored layered default" }) });
      await row.getByRole("button", { name: "Exclude permanently", exact: true }).click(); await waitRevision("3");
      assert.equal(browserCommands.at(-1).action.key, "preference.style"); assert.equal(browserCommands.at(-1).action.op, "exclude");
      assert.match(await row.textContent(), /Permanently excluded/);
      await row.getByRole("button", { name: "Remove permanent exclusion", exact: true }).click(); await waitRevision("4");
      assert.equal(browserCommands.at(-1).action.op, "reenable");
    });
    await check("browser lost-response status blocks another edit until native reload without replay", async () => {
      await page.getByTestId("fixture-onboarding").click(); await page.getByTestId("onboarding-panel").waitFor();
      loseNextWrite = true; await submit("Authored response-loss value");
      await page.getByTestId("user-profile-unknown-outcome").waitFor({ timeout: 3000 });
      const before = browserCommands.filter(command => command.operation === "apply").length;
      await submit("Authored blocked replacement");
      await page.getByTestId("onboarding-panel").getByRole("alert").filter({ hasText: "Reload" }).waitFor();
      assert.equal(browserCommands.filter(command => command.operation === "apply").length, before);
      await page.getByTestId("user-profile-reload").click(); await waitRevision("5");
      assert.equal(browserCommands.at(-1).operation, "read");
      assert.equal(browserCommands.filter(command => command.operation === "apply").length, before);
    });
    await check("browser switching identity during a delayed write ignores the closed view and leaves no activity lock", async () => {
      await select("race-old"); holdNextWrite = true; await submit("Authored delayed old-user value"); await held.promise;
      await select("race-new"); heldResponse();
      await page.waitForFunction(() => window.__nativeActivity === 0);
      assert.equal(await page.getByTestId("user-profile-selected").textContent(), "race-new"); await waitRevision("0");
      assert.equal(wire.records.get("race-old").revision, "1"); assert.equal(wire.records.get("race-new").revision, "0");
      assert.equal(await page.getByTestId("user-profile-unknown-outcome").count(), 0);
      assert.deepEqual(browserErrors, []); assert.deepEqual(externalRequests, []);
    });
  } catch (error) {
    mkdirSync(directory, { recursive: true });
    const failurePath = path.join(directory, "onboarding-host-failure.json");
    if (!existsSync(failurePath)) writeFileSync(failurePath, JSON.stringify({ groups, browserCommands, browserErrors, externalRequests,
      error: String(error), dom: await page?.content().catch(() => null) }, null, 2));
    throw error;
  } finally {
    if (heldResponse) heldResponse();
    try { await browser?.close(); } finally { await closeHttpFixture(server); }
  }
}

mkdirSync(directory, { recursive: true });
const resultPath = path.join(directory, "onboarding-host-results.json");
if (existsSync(resultPath)) throw new Error(`Refusing to overwrite prior evidence: ${resultPath}`);
const sourcesAfter = sourceSnapshot();
assert.deepEqual(sourcesAfter, sourcesBefore, "Executed source inputs changed during the suite; rerun on stable sources.");
writeFileSync(resultPath, JSON.stringify({ schema: "loom.onboarding_host_test_receipt/1", groups, passed: groups.length,
  execution: pureOnly ? "actual TypeScript adapter/projection; authored native HTTP envelopes; no C++/provider execution"
    : "actual TypeScript adapter/projection + React/W12 views/LoomHttpApi in Chromium; authored native HTTP envelopes; no C++/provider execution",
  browserErrors, externalRequests, browserCommands, sourcesBefore, sourcesAfter }, null, 2));
completion.complete();
console.log(`[onboarding-host] ${groups.length}/${groups.length} groups passed; ${resultPath}`);
