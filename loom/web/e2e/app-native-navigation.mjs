#!/usr/bin/env node
// Whole production App -> real HTTP -> native SQLite/graph. Authored public
// inputs only. This suite prepares queries but never authorizes model calls.
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";
import { closeHttpFixture, stopChild, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const webRoot = fileURLToPath(new URL("../", import.meta.url));
const loomRoot = path.dirname(webRoot), repoRoot = path.dirname(loomRoot);
const serverBin = process.env.LOOM_SERVER_BIN || path.join(loomRoot, "build/dev/server/loom-server");
const dist = process.env.LOOM_WEB_DIST || path.join(webRoot, "dist");
assert.ok(existsSync(serverBin), `Current native server is required: ${serverBin}`);
assert.ok(existsSync(path.join(dist, "index.html")), `Current production bundle is required: ${dist}`);
const directory = process.env.APP_NATIVE_EVIDENCE_DIR || mkdtempSync(path.join(tmpdir(), "loom-app-native-evidence-"));
mkdirSync(directory, { recursive: true });
assert.ok(!existsSync(path.join(directory, "results.json")), "Use a fresh evidence directory; retain negative receipts.");
const dataDir = mkdtempSync(path.join(tmpdir(), "loom-app-native-data-"));
const hash = file => createHash("sha256").update(readFileSync(file)).digest("hex");
function treeFiles(root) {
  return readdirSync(root, { withFileTypes: true }).flatMap(entry => {
    const file = path.join(root, entry.name); return entry.isDirectory() ? treeFiles(file) : [file];
  });
}
const sourceFiles = [
  path.join(webRoot, "e2e/app-native-navigation.mjs"), path.join(webRoot, "e2e/harness-lifecycle.mjs"),
  ...["App.tsx", "api/loom-http.ts", "api/analysis.ts", "methods/graph-methods.ts", "api/onboarding-host.ts", "components/UserProfilePanel.tsx",
    "components/MethodsPanel.tsx", "components/AnalysisPanel.tsx", "onboarding/controller.ts",
    "onboarding/native-snapshot.ts", "onboarding/OnboardingPanel.tsx", "onboarding/WhatAppKnows.tsx"].map(file => path.join(webRoot, "src", file)),
  ...["src/app.cpp", "src/app.h", "src/native-ui-common.h", "src/onboarding-ui-routes.h",
    "src/method-ui-routes.h", "src/analysis-ui-routes.h"].map(file => path.join(loomRoot, "server", file)),
  ...["profiles/user.pack", "onboarding/scenario.pack"].map(file => path.join(loomRoot, "data", file)),
];
const manifest = () => ({
  binary: { path: serverBin, sha256: hash(serverBin) },
  dist: Object.fromEntries(treeFiles(dist).sort().map(file => [path.relative(dist, file), hash(file)])),
  sources: Object.fromEntries(sourceFiles.map(file => [path.relative(repoRoot, file), hash(file)])),
});
const inputsBefore = manifest();
if (process.env.EXPECTED_SERVER_SHA256) assert.equal(inputsBefore.binary.sha256, process.env.EXPECTED_SERVER_SHA256);
for (const file of sourceFiles) {
  const saved = path.join(directory, "source-inputs", path.relative(repoRoot, file));
  mkdirSync(path.dirname(saved), { recursive: true }); cpSync(file, saved);
}
const gitHead = spawnSync("git", ["rev-parse", "HEAD"], { cwd: repoRoot, encoding: "utf8" }).stdout.trim();
const gitStatus = spawnSync("git", ["status", "--short"], { cwd: repoRoot, encoding: "utf8" }).stdout;
const user = "synthetic/whole-app-navigation Ω";
const value = "Synthetic exact native form value Ω  retained double space";
const groups = [], traffic = [], pageErrors = [], externalRequests = [], providerRequests = [], pendingResponses = [], observations = {};
const completion = suiteCompletionGuard("app-native-navigation", 7, groups);
let server, browser, page, serverLog = "", failure = null;
const provider = createServer((request, response) => {
  providerRequests.push({ method: request.method, url: request.url }); response.writeHead(503); response.end("No model calls are authorized by this suite.");
});
await new Promise(resolve => provider.listen(0, "127.0.0.1", resolve));
const providerBase = `http://127.0.0.1:${provider.address().port}`;
writeFileSync(path.join(dataDir, "config.json"), JSON.stringify({ semantic_analysis: false, base_url: providerBase,
  default_model: "offline/app-native-navigation", semantic_model: "offline/app-native-navigation" }));
writeFileSync(path.join(dataDir, "models.json"), "[]");
const probe = createServer(); await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
const port = probe.address().port; await new Promise(resolve => probe.close(resolve));
const base = `http://127.0.0.1:${port}`;

async function native(endpoint, body, expectedStatus = 200) {
  const response = await fetch(`${base}${endpoint}`, { method: body === undefined ? "GET" : "POST",
    headers: { "Content-Type": "application/json" }, ...(body === undefined ? {} : { body: JSON.stringify(body) }),
    signal: AbortSignal.timeout(10000) });
  const raw = await response.text(); let result; try { result = JSON.parse(raw); } catch { result = raw; }
  traffic.push({ source: "test", endpoint, command: body ?? null, status: response.status, raw, result });
  assert.equal(response.status, expectedStatus, `${endpoint}: ${raw}`); return result;
}
const read = () => native("/api/onboarding", { operation: "read", user_id: user });
const defaults = (snapshot, key) => snapshot.snapshot.effectiveDefaults.find(item => item.key === key);
const lastBrowser = (endpoint, operation) => [...traffic].reverse().find(row => row.source === "browser" && row.endpoint === endpoint && row.command?.operation === operation);
async function check(name, work) { await work(); groups.push(name); console.log(`[app-native-navigation] PASS ${name}`); }
async function until(work, label, timeout = 10000) {
  const deadline = Date.now() + timeout; let last;
  while (Date.now() < deadline) {
    try { const result = await work(); if (result) return result; } catch (error) { last = error; }
    await new Promise(resolve => setTimeout(resolve, 40));
  }
  throw Error(`${label}: timed out${last ? `: ${last.message}` : ""}`);
}
const revision = async () => (await page.getByTestId("user-profile-revision").textContent()).trim();
async function changed(before) { return until(async () => { const now = await revision(); return now !== before && /^\d+$/.test(now) ? now : null; }, "Native revision did not advance"); }
async function openPanel(id) {
  if (await page.getByTestId("close-panel").count()) await page.getByTestId("close-panel").click();
  await page.getByTestId(`nav-${id}`).click(); await page.getByTestId(`panel-${id}`).waitFor();
}
async function screenshot(name) { await page.screenshot({ path: path.join(directory, `${name}.png`), fullPage: true }); }

try {
  server = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", dataDir,
    "--static-dir", dist, "--token", ""], { cwd: loomRoot, stdio: ["ignore", "pipe", "pipe"] });
  server.stdout.on("data", chunk => { serverLog += chunk.toString(); });
  server.stderr.on("data", chunk => { serverLog += chunk.toString(); });
  server.on("error", error => { serverLog += String(error); });
  await until(async () => {
    if (server.exitCode !== null) throw Error(`Native server exited: ${serverLog}`);
    try { return (await fetch(`${base}/api/healthz`, { signal: AbortSignal.timeout(500) })).ok; } catch { return false; }
  }, "Native server startup", 20000);
  browser = await chromium.launch({ headless: true });
  const context = await browser.newContext({ viewport: { width: 1800, height: 1100 } });
  await context.route("**/*", route => {
    const url = new URL(route.request().url());
    if (url.origin !== base) { externalRequests.push(url.href); return route.abort(); }
    return route.continue();
  });
  page = await context.newPage(); page.setDefaultTimeout(10000);
  page.on("pageerror", error => pageErrors.push(error.message));
  page.on("response", response => {
    const url = new URL(response.url()); if (!url.pathname.startsWith("/api/")) return;
    const request = response.request();
    let command = null; try { command = request.postDataJSON(); } catch { /* GET/non-JSON */ }
    pendingResponses.push((async () => {
      const raw = await response.text(); let result; try { result = JSON.parse(raw); } catch { result = raw; }
      traffic.push({ source: "browser", endpoint: url.pathname, query: url.search, method: request.method(), command, status: response.status(), raw, result });
    })().catch(error => traffic.push({ source: "browser", endpoint: url.pathname, command, status: response.status(), capture_error: error.message })));
  });
  await page.goto(base, { waitUntil: "networkidle" });
  let snapshot, field, section;

  await check("whole App opens an explicit native identity and leaves unavailable model completion disabled", async () => {
    await openPanel("onboarding"); await page.getByTestId("user-profile-host").waitFor();
    assert.equal(await page.getByTestId("user-profile-selected").count(), 0, "No invented profile identity");
    await page.getByTestId("user-profile-identity").fill(user); await page.getByTestId("user-profile-open").click();
    await page.getByTestId("onboarding-panel").waitFor();
    assert.equal(await page.getByTestId("user-profile-selected").textContent(), user);
    snapshot = await read(); assert.equal(await revision(), snapshot.revision); assert.equal(snapshot.revision, "0");
    assert.equal(snapshot.snapshot.user_id, user); assert.ok(Object.keys(snapshot.snapshot.graph.nodes).length);
    for (const record of Object.values(snapshot.snapshot.profile.fields)) { assert.equal(record.status, "unknown"); assert.equal(record.value, null); }
    const modelButton = page.getByRole("button", { name: "Ask model to summarise and continue", exact: true });
    assert.equal(await modelButton.isDisabled(), true);
    observations.runtime_profile = snapshot.snapshot.runtime_profile;
    observations.completion = "No completion API is connected; native form workflow remains available.";
  });

  await check("real form candidate requires separate confirmation and preserves native provenance and user layer", async () => {
    field = snapshot.snapshot.scenario_definition.fields.find(item => item.default_key && (item.input === undefined || ["text", "multiline"].includes(item.input)));
    assert.ok(field, "A declared preference field is required, not an invented schema");
    section = snapshot.snapshot.scenario_definition.sections.find(item => item.field_ids?.includes(field.id) || item.fields?.includes(field.id));
    // Native descriptors currently include the field's section ID explicitly.
    section ||= snapshot.snapshot.scenario_definition.sections.find(item => item.id === field.section);
    assert.ok(section, `Declared section for ${field.id}`);
    let before = await revision();
    await page.getByRole("navigation", { name: "Onboarding sections" }).getByRole("button").filter({ hasText: section.title }).click();
    await changed(before);
    await page.getByTestId("onboarding-panel").getByRole("button", { name: "Form", exact: true }).click();
    const input = page.locator(`[id=${JSON.stringify(`onboarding-${field.id}`)}]`);
    await input.fill(value); before = await revision();
    await page.locator("fieldset").filter({ has: input }).getByRole("button", { name: "Submit for confirmation", exact: true }).click();
    await changed(before); snapshot = await read();
    const candidate = Object.values(snapshot.snapshot.profile.candidates).find(item => item.field === field.id && item.review === "pending");
    assert.ok(candidate); assert.equal(candidate.value, value); assert.equal(snapshot.snapshot.profile.fields[field.id].status, "unknown");
    before = await revision(); await page.getByRole("button", { name: "Confirm", exact: true }).click(); await changed(before);
    snapshot = await read(); const recorded = snapshot.snapshot.profile.fields[field.id];
    assert.equal(recorded.status, "known"); assert.equal(snapshot.snapshot.profile.candidates[candidate.id].review, "confirmed");
    assert.equal(recorded.value, value); assert.equal(recorded.provenance, "form");
    assert.equal(defaults(snapshot, field.default_key).layer, "user"); assert.equal(defaults(snapshot, field.default_key).value, value);
    observations.form = { field, section, candidate, recorded, revision: snapshot.revision };
    await screenshot("onboarding-native-form");
  });

  await check("knowledge navigation shares the selected native host and reload persists only identity configuration", async () => {
    await Promise.all(pendingResponses); const opensBefore = traffic.filter(row => row.source === "browser" && row.endpoint === "/api/onboarding" && row.command?.operation === "open").length;
    await openPanel("user-knowledge"); await page.getByTestId("what-app-knows").waitFor();
    assert.equal(await page.getByTestId("user-profile-selected").textContent(), user); assert.equal(await revision(), snapshot.revision);
    assert.ok((await page.getByTestId("what-app-knows").textContent()).includes(value));
    await Promise.all(pendingResponses);
    assert.equal(traffic.filter(row => row.source === "browser" && row.endpoint === "/api/onboarding" && row.command?.operation === "open").length, opensBefore,
      "Switching W12 projections reads the same host; it does not reopen/reseed the identity");
    const storage = await page.evaluate(() => Object.fromEntries(Object.keys(localStorage).map(key => [key, localStorage.getItem(key)])));
    assert.deepEqual(JSON.parse(storage["loom.user-profile.identity.v1"]), { schema: "loom.user_profile_identity/1", user_id: user });
    assert.ok(!JSON.stringify(storage).includes(value), "Profile content is native-only, not persisted in browser storage");
    observations.browser_storage = storage;
    await page.reload({ waitUntil: "networkidle" }); await openPanel("user-knowledge"); await page.getByTestId("what-app-knows").waitFor();
    assert.equal(await page.getByTestId("user-profile-selected").textContent(), user); assert.equal(await revision(), snapshot.revision);
    assert.equal((await read()).snapshot.profile.fields[field.id].value, value);
  });

  await check("permanent exclusion and explicit removal use native stable-key layers without erasing the confirmed fact", async () => {
    const resolution = defaults(snapshot, field.default_key), label = resolution.entity?.label ?? field.default_key;
    const row = () => page.getByTestId("what-app-knows").locator("fieldset").filter({ has: page.locator("legend").filter({ hasText: label }) });
    assert.equal(await row().count(), 1, "Unique default label from the actual native snapshot");
    let before = await revision(); await row().getByRole("button", { name: "Exclude permanently", exact: true }).click(); await changed(before);
    snapshot = await read(); assert.equal(defaults(snapshot, field.default_key).status, "excluded");
    assert.equal(snapshot.snapshot.profile.fields[field.id].value, value);
    before = await revision(); await row().getByRole("button", { name: "Remove permanent exclusion", exact: true }).click(); await changed(before);
    snapshot = await read(); assert.equal(defaults(snapshot, field.default_key).status, "effective");
    assert.equal(defaults(snapshot, field.default_key).layer, "builtin");
    assert.equal(defaults(snapshot, field.default_key).value, snapshot.snapshot.pack.entries.find(entry => entry.key === field.default_key).value);
    assert.equal(snapshot.snapshot.profile.fields[field.id].value, value, "Native permanent exclusion clears the override, not the recorded profile fact");
    assert.ok(snapshot.snapshot.layers.history.length >= 2); observations.layer_history = snapshot.snapshot.layers.history;
    await screenshot("knowledge-native-layers");
  });

  await check("native privacy form changes storage policy and explicit restoration recovers the prior rule", async () => {
    await openPanel("onboarding"); await page.getByTestId("onboarding-panel").waitFor();
    const policyRequest = { op: "store", category: field.category, field: field.id, provenance: "user_stated" };
    const original = snapshot.snapshot.profile.privacy.rules.find(rule => rule.category === field.category)
      ?? snapshot.snapshot.profile.privacy.rules.find(rule => rule.category === "*");
    assert.ok(original); assert.equal(original.store, true);
    assert.equal((await native("/api/onboarding", { operation: "policy_decision", user_id: user, request: policyRequest })).allowed, true);
    const row = page.locator("fieldset").filter({ has: page.locator(`[id=${JSON.stringify(`providers-${original.category}`)}]`) });
    let before = await revision(); await row.getByRole("checkbox", { name: "Allow storage", exact: true }).uncheck();
    await row.getByRole("button", { name: "Save privacy rule", exact: true }).click(); await changed(before);
    const blocked = await native("/api/onboarding", { operation: "policy_decision", user_id: user, request: policyRequest }); assert.equal(blocked.allowed, false);
    before = await revision(); await row.getByRole("checkbox", { name: "Allow storage", exact: true }).check();
    await row.getByRole("button", { name: "Save privacy rule", exact: true }).click(); await changed(before);
    snapshot = await read(); assert.deepEqual(snapshot.snapshot.profile.privacy.rules.find(rule => rule.category === original.category), original);
    assert.equal((await native("/api/onboarding", { operation: "policy_decision", user_id: user, request: policyRequest })).allowed, true);
    observations.privacy = { original, blocked, restored_revision: snapshot.revision };
  });

  await check("Methods route loads the actual native onboarding method graph and records a real library acceptance receipt", async () => {
    const prepared = await native("/api/onboarding", { operation: "model_request", user_id: user, provider: "offline/app-native-navigation" });
    assert.equal(prepared.calls_authorized, false); assert.ok(prepared.method_profile); observations.onboarding_model_preparation = prepared;
    await openPanel("methods"); await page.getByTestId("methods-panel").waitFor();
    await until(async () => { await Promise.all(pendingResponses); return lastBrowser("/api/methods", "catalog"); }, "Native methods catalog");
    observations.methods_capabilities = lastBrowser("/api/methods", "capabilities")?.result;
    observations.methods_catalog = lastBrowser("/api/methods", "catalog")?.result;
    await page.getByTestId("methods-profile-json").fill(JSON.stringify(prepared.method_profile, null, 2));
    await page.getByRole("button", { name: "Load graph", exact: true }).click(); await page.getByTestId("methods-snapshot").waitFor();
    assert.ok(await page.getByTestId("methods-entity").count()); assert.equal(await page.getByTestId("methods-no-results").count(), 1);
    await page.getByTestId("methods-target").fill("synthetic/app-native-navigation/method-library");
    await page.getByTestId("methods-actor").fill("synthetic/app-native-navigation/user");
    await page.getByTestId("methods-known-at").fill("2000-01-01T00:00:00Z");
    await page.getByRole("button", { name: "Validate profile for library", exact: true }).click(); await page.getByTestId("methods-version-preview").waitFor();
    await page.getByTestId("methods-accept-button").click(); await page.getByTestId("methods-accepted").waitFor();
    await Promise.all(pendingResponses); const acceptance = lastBrowser("/api/methods", "accept") || [...traffic].reverse().find(row => row.source === "browser" && row.endpoint === "/api/methods" && row.result?.receipt?.id);
    assert.ok(acceptance?.result.receipt.id, "Native GraphPacketStore receipt must exist"); assert.equal(acceptance.result.row_drift.matches, true);
    observations.library_acceptance = acceptance.result;
    await page.getByRole("button", { name: "Read native result lineage", exact: true }).click(); await page.getByTestId("methods-native-lineage").waitFor();
    await screenshot("methods-native-library-receipt");
  });

  await check("expert Analysis route prepares and inspects the full native query without a provider call or invented method binding", async () => {
    await openPanel("analysis"); await page.getByTestId("analysis-panel").waitFor();
    await until(async () => (await page.getByTestId("analysis-contract").inputValue()).length > 2, "Native prompt contract");
    await page.getByTestId("analysis-model").fill("offline/app-native-navigation"); await page.getByTestId("analysis-provider").fill(providerBase);
    await page.getByTestId("analysis-bindings").fill(JSON.stringify({ text: "Authored public synthetic expert analysis input Ω",
      input_json: { synthetic: true, text: "Authored public synthetic expert analysis input Ω" } }));
    await page.getByTestId("analysis-prepare").click(); await page.getByTestId("analysis-prepared").waitFor();
    const prepared = JSON.parse(await page.getByTestId("analysis-prepared").textContent());
    assert.equal(prepared.schema, "loom.analysis_prepared/1"); assert.ok(prepared.prepared_id); assert.match(prepared.request_identity_hash, /^[a-f0-9]{64}$/);
    assert.equal(prepared.attempted, false); assert.equal(prepared.graph_binding_available, false); assert.equal(await page.getByTestId("analysis-send").isDisabled(), true);
    assert.equal(prepared.request.body_bytes, await page.getByTestId("analysis-body").inputValue());
    assert.ok(prepared.request.body_bytes.includes("Authored public synthetic expert analysis input Ω"));
    observations.analysis_preparation = prepared;
    await page.getByTestId("analysis-inspect").click(); await until(async () => (await page.getByTestId("analysis-notice").textContent()).includes("Saved attempt inspected"), "Native prepared query inspection");
    const inspected = JSON.parse(await page.getByTestId("analysis-prepared").textContent()); assert.equal(inspected.prepared_id, prepared.prepared_id); assert.equal(inspected.request_identity_hash, prepared.request_identity_hash);
    observations.analysis_inspection = inspected; await screenshot("analysis-native-expert-preview");
    await Promise.all(pendingResponses); assert.deepEqual(pageErrors, []); assert.deepEqual(externalRequests, []); assert.deepEqual(providerRequests, []);
    assert.equal(traffic.some(row => row.endpoint === "/api/analysis" && row.command?.operation === "execute"), false);
  });
  assert.deepEqual(manifest(), inputsBefore, "Source, production assets and native binary must remain unchanged during this receipt");
} catch (error) {
  failure = error; if (page) {
    try { await screenshot("failure"); writeFileSync(path.join(directory, "failure-dom.html"), await page.content()); } catch { /* Retain original failure */ }
  }
} finally {
  try { await browser?.close(); await Promise.all(pendingResponses); await stopChild(server); await closeHttpFixture(provider); }
  catch (error) { failure ||= error; }
  if (failure) cpSync(dataDir, path.join(directory, "failed-native-data"), { recursive: true });
  const inputsAfter = manifest();
  writeFileSync(path.join(directory, "server.log"), serverLog);
  writeFileSync(path.join(directory, "results.json"), JSON.stringify({ schema: "loom.app_native_navigation_receipt/1", status: failure ? "failed" : "passed",
    timestamp: new Date().toISOString(), gitHead, gitStatus, serverBin, dist, base, providerBase,
    groups, declared_groups: 7, failure: failure ? { message: failure.message, stack: failure.stack } : null,
    inputsBefore, inputsAfter, inputs_stable: JSON.stringify(inputsBefore) === JSON.stringify(inputsAfter),
    traffic, observations, pageErrors, externalRequests, providerRequests,
    limitations: ["No onboarding model completion transport is connected; the interview button remains disabled.",
      "RuntimeProfile availability is recorded from the actual native diagnostic, without claiming a UI projection.",
      "The fresh instance has no selected native analysis method version; preview is available and sending is disabled.",
      "Method definitions/library acceptance establish no model execution or generated content truth.",
      "Source and artifact hashes bind this receipt to actual inputs; this suite does not claim to rebuild those artifacts."] }, null, 2));
  rmSync(dataDir, { recursive: true, force: true });
}
if (failure) throw failure;
completion.complete();
console.log(`[app-native-navigation] 7/7 groups passed; real whole App/native forms/layers/privacy/library receipt/query preview; 0 provider calls; evidence ${directory}`);
