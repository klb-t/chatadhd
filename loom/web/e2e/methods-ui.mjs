#!/usr/bin/env node
// Actual React + retained native synthetic method graph artifacts. --native
// repeats the UI through the real registry/packet store; never dispatches a model.
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { createServer } from "node:http";
import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { gunzipSync } from "node:zlib";
import { build } from "esbuild";
import { chromium } from "playwright";
import ts from "typescript";
import { closeHttpFixture, stopChild, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const web = fileURLToPath(new URL("../", import.meta.url)), loom = path.resolve(web, "..");
const nativeMode = process.argv.includes("--native"), groups = [], expected = nativeMode ? 14 : 11;
const completion = suiteCompletionGuard("methods-ui", expected, groups);
async function check(name, fn) { await fn(); groups.push(name); console.log(`[methods-ui] PASS ${name}`); }
const artifact = name => JSON.parse(gunzipSync(readFileSync(path.join(loom, `src/packet/tests/method-registry-variants/${name}.json.gz`))));
const golden = artifact("canonical-w3-export"), nested = artifact("nested-combination");
const helperText = ts.transpileModule(readFileSync(path.join(web, "src/methods/graph-methods.ts"), "utf8"), {
  compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022 },
}).outputText;
const helper = await import(`data:text/javascript;base64,${Buffer.from(helperText).toString("base64")}`);
const hashConfig = value => createHash("sha256").update(JSON.stringify(value)).digest("hex");
function profileOf(fixture) {
  return { vocabulary: fixture.contract.vocabulary, entities: fixture.packet.entities, claims: fixture.packet.claims, sources: fixture.packet.sources,
    selection: { members: [{ combination_version_id: fixture.contract.bindings.combination_version_id }], parameter_layers: ["method", "combination", "member", "selection", "user"] } };
}
const profile = profileOf(nested), initialDefinition = structuredClone(profile.entities.find(row => row.id === nested.contract.bindings.method_version_id));
const configSlots = ["method_registry", "graph_reply"].map(section => ({ id: `context_execution.${section}.profile`,
  config_path: ["context_execution", section, "profile"], receipt_path: ["context_execution", section, "receipt_ids"], selection_path: ["context_execution", section, "selection"], present: section === "method_registry" }));
const record = { id: configSlots[0].id, profile, receipt_ids: [], config_path: configSlots[0].config_path, receipt_path: configSlots[0].receipt_path, selection_path: configSlots[0].selection_path };
const snapshotOf = supplied => { const value = { schema: "loom.method_registry_snapshot/1", snapshot_sha256: "synthetic_UI_fixture_not_execution_evidence", profile: supplied,
  vocabulary: supplied.vocabulary, entities: Object.fromEntries(supplied.entities.map(row => [row.id, row])),
  claims: Object.fromEntries(supplied.claims.map(row => [row.id, row])), sources: Object.fromEntries(supplied.sources.map(row => [row.observation.id, row])), receipts: [] };
  return { ...value, snapshot_json: JSON.stringify(value), edit_attrs_json: Object.fromEntries(supplied.entities.map(row => [row.id, JSON.stringify(row.attrs, null, 2)])) }; };
await check("retained native artifact provenance follows both real Claims and exact endpoints", () => {
  const snap = snapshotOf(profileOf(golden)), before = JSON.stringify(snap);
  const results = helper.methodResultLineage(snap);
  assert.deepEqual(results.map(row => row.entity.id).sort(), [...golden.result_entity_ids].sort());
  assert.ok(results.every(row => row.complete && row.methodVersions[0].id === golden.contract.bindings.method_version_id && row.runs[0].id === golden.contract.bindings.run_id));
  assert.equal(JSON.stringify(snap), before);
  const missing = structuredClone(snap);
  const predicate = missing.vocabulary.predicates.produced_in_run;
  for (const claim of Object.values(missing.claims)) if (claim.predicate === predicate) claim.assessment.status = "rejected";
  assert.ok(helper.methodResultLineage(missing).every(row => !row.complete && row.runs.length === 0));
  delete missing.vocabulary.predicates.produced_by_method_version;
  assert.ok(helper.methodResultLineage(missing).every(row => row.methodVersions.length === 0));
});
await check("exact native acceptance is mandatory; incomplete or extra CAS rows are rejected", () => {
  const accepted = { operation: "accept", target: "synthetic/UI", explicitly_accepted: true,
    packet: { schema: "loom.graph_packet/1" }, selection: { entities: ["e"], claims: [], sources: ["s"] },
    expected_rows: { entities: { e: null }, claims: {}, sources: { s: "a".repeat(64) } } };
  assert.strictEqual(helper.methodAcceptance({ accept_request: accepted }), accepted);
  const missing = structuredClone(accepted); delete missing.expected_rows.sources.s;
  assert.throws(() => helper.methodAcceptance({ accept_request: missing }), /CAS expectations/);
  const extra = structuredClone(accepted); extra.expected_rows.entities.foreign = null;
  assert.throws(() => helper.methodAcceptance({ accept_request: extra }), /CAS expectations/);
  const duplicated = structuredClone(accepted); duplicated.selection.entities.push("e");
  assert.throws(() => helper.methodAcceptance({ accept_request: duplicated }), /selection/);
  assert.throws(() => helper.parseMethodObject("[]", "Profile"), /object/);
  assert.throws(() => helper.parseReceiptIds('["r","r"]'), /repeat/);
});
await check("accepted selection persistence reads fresh siblings and verifies global config readback", async () => {
  let cfg = { context_execution: { unrelated: { future: [7, null] }, method_registry: { profile: { old: true }, receipt_ids: [], selection: { members: [{ method_version_id: "old" }] }, extra: "retain" }, graph_reply: { mode: "off", unknown: true } } };
  let reads = 0, writes = 0;
  const transport = { methods: async body => {
    if (body.operation === "chat_settings") { reads++; return { scope: "global_native_config", ...structuredClone(cfg), context_execution_sha256: hashConfig(cfg.context_execution) }; }
    assert.equal(body.operation, "save_profile_selection"); assert.equal(body.expected_context_execution_sha256, hashConfig(cfg.context_execution));
    writes++; cfg.context_execution.method_registry.profile = { ...JSON.parse(body.profile_json), selection: JSON.parse(body.selection_json) };
    cfg.context_execution.method_registry.receipt_ids = body.receipt_ids; cfg.context_execution.method_registry.selection = JSON.parse(body.selection_json);
    return { scope: "global_native_config", ...structuredClone(cfg) };
  } };
  const frozen = { selection: { members: [] }, unknown: { retained: [1, "Żółć 🧠"] } };
  await helper.saveAcceptedMethodProfile(transport, configSlots[0], frozen, ["gpr_synthetic"]);
  assert.equal(reads, 2); assert.equal(writes, 1); assert.deepEqual(cfg.context_execution.unrelated, { future: [7, null] });
  assert.equal(cfg.context_execution.method_registry.extra, "retain"); assert.deepEqual(cfg.context_execution.graph_reply, { mode: "off", unknown: true });
  assert.deepEqual(cfg.context_execution.method_registry.selection, frozen.selection);
  await assert.rejects(helper.saveAcceptedMethodProfile({ methods: async () => ({ scope: "global_native_config", ...cfg, context_execution_sha256: hashConfig(cfg.context_execution) }) }, configSlots[0], { changed: true, selection: { members: [] } }, []), /readback differs/);
  let compareConflicts = 0;
  await assert.rejects(helper.saveAcceptedMethodProfile({ methods: async body => {
    if (body.operation === "chat_settings") return { scope: "global_native_config", ...structuredClone(cfg), context_execution_sha256: hashConfig(cfg.context_execution) };
    cfg.context_execution.concurrent_update = { retained: true }; compareConflicts++;
    assert.notEqual(body.expected_context_execution_sha256, hashConfig(cfg.context_execution));
    return { error: { message: "Synthetic native CAS conflict" } };
  } }, configSlots[0], { overwrite: true, selection: { members: [] } }, []), /CAS conflict/);
  assert.equal(compareConflicts, 1); assert.deepEqual(cfg.context_execution.concurrent_update, { retained: true }); assert.deepEqual(cfg.context_execution.method_registry.profile, frozen);
  await assert.rejects(helper.saveAcceptedMethodProfile(transport, { config_path: ["context_execution", "__proto__", "profile"] }, frozen, []), /safe profile/);
});

const bundle = await build({ stdin: { contents: `
  import React from "react"; import {createRoot} from "react-dom/client"; import MethodsPanel from "./src/components/MethodsPanel";
  import {LoomHttpApi} from "./src/api/loom-http";
  const transport=new LoomHttpApi();
  createRoot(document.getElementById("root")).render(<MethodsPanel transport={location.search.includes("unavailable")?{}:transport}/>);
  `, resolveDir: web, loader: "tsx" }, write: false, bundle: true, format: "iife", platform: "browser", jsx: "automatic", outfile: "/tmp/methods-ui-fixture.js" });
const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text, css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";
const calls = []; let cfg = { context_execution: { method_registry: { profile, receipt_ids: [], future: "preserve" }, graph_reply: { mode: "off", future: { sibling: true } }, unknown: [1, null] } };
let lastPreview, stored, configFailure = false, nativeBase = null, nativeServer, nativeDir, nativeLog = "";
const nativeReceipts = [], nativeResponses = [];
const relevantSources = ["server/src/app.cpp", "server/src/native-ui-common.h", "server/src/method-ui-routes.h", "src/context/method_registry.cpp", "src/packet/METHOD_GRAPH.md",
  "web/src/api/loom-http.ts", "web/src/components/MethodsPanel.tsx", "web/src/components/methods-panel.css", "web/src/methods/graph-methods.ts", "web/e2e/methods-ui.mjs",
  "src/packet/tests/method-registry-variants/canonical-w3-export.json.gz", "src/packet/tests/method-registry-variants/nested-combination.json.gz"];
function fileManifest() {
  const sources = Object.fromEntries(relevantSources.map(relative => [relative, createHash("sha256").update(readFileSync(path.join(loom, relative))).digest("hex")]));
  const bin = process.env.LOOM_SERVER_BIN || path.join(loom, "build/dev/server/loom-server");
  return { node: process.version, command: process.argv, sources, ...(nativeMode ? { server_binary: bin, server_binary_sha256: createHash("sha256").update(readFileSync(bin)).digest("hex") } : {}) };
}
const manifestBefore = fileManifest();
function preview(command) {
  const old = command.snapshot.entities[command.entity_id], attrs = structuredClone(command.attrs);
  attrs.definition_sha256 = createHash("sha256").update(JSON.stringify(attrs.definition)).digest("hex");
  const fresh = { ...old, id: "synthetic_ui_new_version", attrs }, next = { ...command.snapshot.profile, entities: [...Object.values(command.snapshot.entities), fresh] };
  const packet = { ...nested.packet, entities: next.entities }, selection = {}, expected_rows = {};
  for (const key of ["entities", "claims", "sources"]) { selection[key] = packet[key].map(row => key === "sources" ? row.observation.id : row.id); expected_rows[key] = Object.fromEntries(selection[key].map(id => [id, null])); }
  const request = { operation: "accept", target: command.target, packet, selection, expected_rows, explicitly_accepted: true };
  return { new_version_id: fresh.id, previous_version_id: old.id, profile: next, profile_json: JSON.stringify(next), accept_request: request, accept_request_json: JSON.stringify(request) };
}
const server = createServer(async (req, res) => {
  try {
    const chunks = []; for await (const chunk of req) chunks.push(chunk);
    const raw = Buffer.concat(chunks).toString(); let body = raw ? JSON.parse(raw) : undefined;
    if (req.url === "/app.js") { res.setHeader("Content-Type", "text/javascript"); res.end(js); return; }
    if (req.url === "/app.css") { res.setHeader("Content-Type", "text/css"); res.end(css); return; }
    res.setHeader("Content-Type", "application/json");
    if (req.url?.startsWith("/api/")) {
      calls.push({ path: req.url, method: req.method, body });
      if (nativeBase) { const result = await fetch(nativeBase + req.url, { method: req.method, headers: { "Content-Type": "application/json" }, ...(raw ? { body: raw } : {}) });
        const responseRaw = await result.text(); nativeResponses.push({ source: "actual_LoomHttpApi_browser", method: req.method, path: req.url, command: body?.operation ?? body?.action ?? null, status: result.status,
          request_raw: raw, request_sha256: createHash("sha256").update(raw).digest("hex"), raw: responseRaw, sha256: createHash("sha256").update(responseRaw).digest("hex") });
        res.statusCode = result.status; res.end(responseRaw); return; }
      if (req.url === "/api/methods/chat-settings" && req.method === "GET") body = { operation: "chat_settings" };
      if (body) {
        body = { ...body };
        for (const key of ["profile", "snapshot", "selection", "attrs"]) if (typeof body[key + "_json"] === "string") body[key] = JSON.parse(body[key + "_json"]);
        if (typeof body.request_json === "string") body = JSON.parse(body.request_json);
      }
      if (req.url === "/api/config") { res.end(JSON.stringify(cfg)); return; }
      if (req.url === "/api/config/context_execution") {
        if (configFailure) { res.statusCode = 503; res.end(JSON.stringify({ error: { message: "Synthetic config save failed; graph already committed" } })); return; }
        cfg.context_execution = body.value; res.end(JSON.stringify(cfg)); return;
      }
      const action = body.operation ?? body.action;
      if (action === "chat_settings") { res.end(JSON.stringify({ scope: "global_native_config", context_execution: cfg.context_execution, context_execution_sha256: hashConfig(cfg.context_execution) })); return; }
      if (action === "save_profile_selection") {
        assert.equal(body.expected_context_execution_sha256, hashConfig(cfg.context_execution));
        if (configFailure) { res.statusCode = 503; res.end(JSON.stringify({ error: { message: "Synthetic config save failed; graph already committed" } })); return; }
        cfg.context_execution.method_registry.profile = { ...JSON.parse(body.profile_json), selection: JSON.parse(body.selection_json) };
        cfg.context_execution.method_registry.receipt_ids = body.receipt_ids; cfg.context_execution.method_registry.selection = JSON.parse(body.selection_json);
        res.end(JSON.stringify({ scope: "global_native_config", context_execution: cfg.context_execution, context_execution_sha256: hashConfig(cfg.context_execution) })); return;
      }
      if (action === "capabilities") { res.end(JSON.stringify({ execution: { synthetic_ui_no_execution: { available: false } }, fusion: {} })); return; }
      if (action === "catalog") { res.end(JSON.stringify({ profiles: [{ ...record, profile: cfg.context_execution.method_registry.profile, profile_json: JSON.stringify(cfg.context_execution.method_registry.profile), receipt_ids: cfg.context_execution.method_registry.receipt_ids, selection_overlay: cfg.context_execution.method_registry.selection ?? {}, selection_overlay_json: JSON.stringify(cfg.context_execution.method_registry.selection ?? {}) }], config_slots: configSlots, receipts: stored ? [{ id: stored.receipt.id }] : [] })); return; }
      if (action === "load") { const snapshot = snapshotOf(body.profile); snapshot.receipts = (body.receipt_ids ?? []).map(receipt_id => ({ receipt_id })); res.end(JSON.stringify(snapshot)); return; }
      if (action === "resolve") { res.end(JSON.stringify({ resolution_sha256: "synthetic_resolution_not_execution", selection: { ...body.snapshot.profile.selection, ...body.selection }, leaves: [0, 1, 2, 3].map(i => ({ method_version_id: initialDefinition.id, available: false, weight: -6, effective_parameters: { caller: null }, path: [{ id: "synthetic_variant_outer_nested-combination", member_index: Math.floor(i / 2) }, { id: "synthetic_variant_inner_nested-combination", member_index: i % 2 }] })) })); return; }
      if (action === "version_edit") { lastPreview = preview(body); res.end(JSON.stringify(lastPreview)); return; }
      if (action === "profile_preview") {
        const packet = { ...nested.packet, entities: body.profile.entities, claims: body.profile.claims, sources: body.profile.sources }, selection = {}, expected_rows = {};
        for (const key of ["entities", "claims", "sources"]) { selection[key] = packet[key].map(row => key === "sources" ? row.observation.id : row.id); expected_rows[key] = Object.fromEntries(selection[key].map(id => [id, null])); }
        const request = { operation: "accept", target: body.target, packet, selection, expected_rows, explicitly_accepted: true };
        lastPreview = { profile: body.profile, profile_json: body.profile_json, accept_request: request, accept_request_json: JSON.stringify(request) };
        res.end(JSON.stringify(lastPreview)); return;
      }
      if (action === "accept") { stored = { receipt: { id: "gpr_synthetic_UI_only", packet: body.packet, selection: body.selection }, row_drift: { matches: true } }; res.end(JSON.stringify(stored)); return; }
      if (action === "read") { res.end(JSON.stringify(stored)); return; }
      if (action === "result_lineage") { res.end(JSON.stringify({ snapshot_sha256: body.snapshot.snapshot_sha256, rows: helper.methodResultLineage(body.snapshot), producer_execution_verified: false })); return; }
      res.statusCode = 400; res.end(JSON.stringify({ error: { message: "Unexpected fixture operation" } })); return;
    }
    res.setHeader("Content-Type", "text/html"); res.end('<!doctype html><html><head><link rel="stylesheet" href="/app.css"></head><body><div id="root"></div><script src="/app.js"></script></body></html>');
  } catch (error) { res.statusCode = 500; res.end(JSON.stringify({ error: { message: error.message } })); }
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const base = `http://127.0.0.1:${server.address().port}`;
let browser; const external = [], errors = [];
async function native(method, endpoint, body, originalBody) {
  const requestRaw = originalBody ?? (body === undefined ? undefined : JSON.stringify(body));
  const response = await fetch(nativeBase + endpoint, { method, headers: { "Content-Type": "application/json" }, ...(requestRaw === undefined ? {} : { body: requestRaw }) });
  const raw = await response.text(), data = JSON.parse(raw);
  nativeResponses.push({ source: "native_verification", method, path: endpoint, command: body?.operation ?? body?.action ?? null, status: response.status,
    ...(requestRaw === undefined ? {} : { request_raw: requestRaw, request_sha256: createHash("sha256").update(requestRaw).digest("hex") }), raw, sha256: createHash("sha256").update(raw).digest("hex") });
  assert.ok(response.ok, JSON.stringify(data)); return data;
}
async function startNative() {
  const bin = process.env.LOOM_SERVER_BIN || path.join(loom, "build/dev/server/loom-server"); assert.ok(existsSync(bin), `Build native server first: ${bin}`);
  const probe = createServer(); await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve)); const port = probe.address().port; await closeHttpFixture(probe);
  nativeBase = `http://127.0.0.1:${port}`;
  nativeServer = spawn(bin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", nativeDir], { stdio: ["ignore", "pipe", "pipe"] });
  for (const stream of [nativeServer.stdout, nativeServer.stderr]) stream.on("data", chunk => { nativeLog += chunk.toString(); });
  const deadline = Date.now() + 20000;
  while (true) { try { if ((await fetch(nativeBase + "/api/healthz")).ok) return; } catch { /* Starting. */ } if (Date.now() > deadline) throw new Error(nativeLog); await new Promise(resolve => setTimeout(resolve, 100)); }
}
try {
  browser = await chromium.launch({ headless: true, args: ["--no-sandbox"] });
  const page = await browser.newPage(); page.on("pageerror", error => errors.push(error.message));
  await page.route("**/*", async route => { if (!route.request().url().startsWith(base + "/")) { external.push(route.request().url()); await route.abort(); } else await route.continue(); });
  await page.goto(base); await page.getByLabel("Method profile", { exact: true }).selectOption(record.id);
  await page.getByRole("button", { name: "Load graph", exact: true }).click(); await page.locator('[data-testid="methods-snapshot"]').waitFor();
  await check("actual graph entities and three observed result relations render without fabricated defaults", async () => {
    assert.equal(await page.locator('[data-testid="methods-entity"]').count(), nested.packet.entities.length);
    assert.equal(await page.locator('[data-testid="methods-result"][data-complete="true"]').count(), 3);
    assert.equal(calls.filter(row => ["prepare", "bind", "accept"].includes(row.body?.operation)).length, 0);
    await page.getByRole("button", { name: "Read native result lineage", exact: true }).click(); await page.locator('[data-testid="methods-native-lineage"]').waitFor();
    await page.locator('[data-testid="methods-native-lineage"] summary').click();
    const line = await page.locator('[data-testid="methods-native-lineage"] pre').innerText(); assert.match(line, /produced_by_method_version/); assert.match(line, /producer_execution_verified.*false/);
  });
  await check("nested combination occurrences retain repeated child identities and signed weights", async () => {
    await page.locator('[data-entity-id="synthetic_variant_outer_nested-combination"] button').click();
    assert.equal(await page.locator('[data-testid="methods-members"] > li').count(), 2);
    await page.getByRole("button", { name: "Inspect member 2", exact: true }).click();
    assert.equal(await page.locator('[data-testid="methods-members"] > li').count(), 2);
    assert.match(await page.locator('[data-testid="methods-members"]').innerText(), /"weight": -3/);
    await page.getByRole("button", { name: "Resolve selection without execution", exact: true }).click();
    await page.locator('[data-testid="methods-resolution"]').waitFor(); assert.equal(await page.locator('[data-testid="methods-resolved-leaf"]').count(), 4);
    assert.match(await page.locator('[data-testid="methods-resolution"]').innerText(), /available: false/);
  });
  await check("invalid JSON stays local; version draft preserves unknown attrs and preview is read-only", async () => {
    await page.locator(`[data-entity-id="${initialDefinition.id}"] button`).click();
    await page.getByLabel("Target library", { exact: true }).fill("synthetic/UI/library"); await page.getByLabel("Actor", { exact: true }).fill("synthetic-owner");
    await page.getByLabel("Known at (timestamp with timezone)", { exact: true }).fill("2026-10-05T10:00:00Z");
    const before = calls.length; await page.locator('[data-testid="methods-version-json"]').fill("[");
    await page.getByRole("button", { name: "Validate new version", exact: true }).click(); await page.getByRole("alert").waitFor(); assert.equal(calls.length, before);
    const attrs = structuredClone(initialDefinition.attrs); attrs.definition.ui_test = { unknown: [null, "Żółć 🧠", -4] }; attrs.future_unknown = { retain: true };
    await page.locator('[data-testid="methods-version-json"]').fill(JSON.stringify(attrs));
    await page.getByRole("button", { name: "Validate new version", exact: true }).click(); await page.locator('[data-testid="methods-version-preview"]').waitFor();
    const command = calls.filter(row => row.body?.operation === "version_edit").at(-1).body; assert.deepEqual(JSON.parse(command.attrs_json), attrs); assert.equal(command.entity_id, initialDefinition.id);
    assert.equal(calls.filter(row => row.body?.operation === "accept").length, 0);
    await page.getByLabel("Actor", { exact: true }).fill("another synthetic actor"); assert.equal(await page.locator('[data-testid="methods-version-preview"]').count(), 0);
    await page.getByRole("button", { name: "Validate new version", exact: true }).click(); await page.locator('[data-testid="methods-version-preview"]').waitFor();
  });
  await check("acceptance forwards exact native scoped CAS once and leaves old execution version untouched", async () => {
    await page.getByRole("button", { name: "Accept new version into library", exact: true }).click(); await page.locator('[data-testid="methods-accepted"]').waitFor();
    const accepts = calls.filter(row => row.body?.operation === "accept"); assert.equal(accepts.length, 1); assert.equal(accepts[0].body.request_json, lastPreview.accept_request_json);
    assert.deepEqual(stored.receipt.packet.entities.find(row => row.id === initialDefinition.id), initialDefinition);
    assert.ok(stored.receipt.packet.entities.find(row => row.id === "synthetic_ui_new_version"));
    assert.ok(stored.receipt.packet.claims.filter(row => row.predicate === profile.vocabulary.predicates.produced_by_method_version).every(row => row.object === nested.contract.bindings.method_version_id));
    assert.equal(await page.locator('[data-testid="methods-accept-button"]').isDisabled(), true);
  });
  await check("config failure retains committed receipt; explicit retry saves selected accepted rows without reaccepting", async () => {
    const selection = { ...profile.selection, members: [{ method_version_id: "synthetic_ui_new_version", weight: -42, parameters: { unknown: [null, 7] } }] };
    await page.locator('[data-testid="methods-selection-json"]').fill(JSON.stringify(selection)); configFailure = true;
    await page.getByRole("button", { name: "Save accepted profile selection", exact: true }).click(); await page.getByRole("alert").filter({ hasText: "Synthetic config save failed" }).waitFor();
    assert.match(await page.locator('[data-testid="methods-accepted"]').innerText(), /gpr_synthetic_UI_only/);
    assert.match(await page.locator('[data-testid="methods-config-selection"]').innerText(), /has not been saved/);
    const acceptedCount = calls.filter(row => row.body?.operation === "accept").length; configFailure = false;
    await page.getByRole("button", { name: "Save accepted profile selection", exact: true }).click(); await page.getByRole("status").filter({ hasText: "verified by readback" }).waitFor();
    assert.equal(calls.filter(row => row.body?.operation === "accept").length, acceptedCount);
    assert.deepEqual(cfg.context_execution.method_registry.profile.selection, selection); assert.deepEqual(cfg.context_execution.method_registry.receipt_ids, ["gpr_synthetic_UI_only"]);
    assert.deepEqual(cfg.context_execution.method_registry.selection, selection);
    assert.equal(cfg.context_execution.method_registry.future, "preserve"); assert.deepEqual(cfg.context_execution.graph_reply, { mode: "off", future: { sibling: true } }); assert.deepEqual(cfg.context_execution.unknown, [1, null]);
  });
  await check("explicit receipt read and supplied profile retain complete unknown rows without automatic writes", async () => {
    await page.getByLabel("Read native receipt ID", { exact: true }).fill("gpr_synthetic_UI_only");
    await page.getByRole("button", { name: "Read receipt", exact: true }).click(); await page.locator('[data-testid="methods-receipt-read"]').waitFor();
    await page.locator('[data-testid="methods-receipt-read"] summary').click();
    assert.match(await page.locator('[data-testid="methods-receipt-read"] pre').innerText(), /future_unknown/);
    await page.getByLabel("Method profile", { exact: true }).selectOption("");
    const supplied = { ...profile, unknown_profile_data: { future: ["source remains", null] } };
    await page.locator('[data-testid="methods-profile-json"]').fill(JSON.stringify(supplied));
    await page.locator('[data-testid="methods-receipts-json"]').fill('["gpr_synthetic_UI_only"]');
    const writeCount = calls.filter(row => row.body?.operation === "accept" || row.path === "/api/config/context_execution").length;
    await page.getByRole("button", { name: "Load graph", exact: true }).click(); await page.locator('[data-testid="methods-snapshot"]').waitFor();
    assert.equal(calls.filter(row => row.body?.operation === "accept" || row.path === "/api/config/context_execution").length, writeCount);
    const loaded = calls.filter(row => row.body?.operation === "load").at(-1).body;
    assert.deepEqual(JSON.parse(loaded.profile_json).unknown_profile_data, supplied.unknown_profile_data); assert.deepEqual(loaded.receipt_ids, ["gpr_synthetic_UI_only"]);
    const beforeAccept = calls.filter(row => row.body?.operation === "accept").length;
    await page.getByRole("button", { name: "Validate profile for library", exact: true }).click(); await page.locator('[data-testid="methods-version-preview"]').waitFor();
    assert.equal(calls.filter(row => row.body?.operation === "accept").length, beforeAccept);
    await page.getByRole("button", { name: "Accept profile into library", exact: true }).click();
    await page.waitForFunction(() => document.querySelector('[data-testid="methods-accept-button"]')?.disabled === true);
    assert.equal(calls.filter(row => row.body?.operation === "accept").length, beforeAccept + 1);
    assert.equal(calls.filter(row => row.body?.operation === "accept").at(-1).body.request_json, lastPreview.accept_request_json);
  });
  await check("producer version filter follows exact produced_by Claims and never assigns default results", async () => {
    await page.locator(`[data-entity-id="${initialDefinition.id}"] button`).click();
    await page.getByRole("button", { name: "Results from this version", exact: true }).click(); assert.equal(await page.locator('[data-testid="methods-result"]').count(), 3);
    const unused = nested.packet.entities.find(row => row.kind === profile.vocabulary.kinds.method_version && row.id !== initialDefinition.id);
    await page.locator(`[data-entity-id="${unused.id}"] button`).click();
    await page.getByRole("button", { name: "Results from this version", exact: true }).click(); assert.equal(await page.locator('[data-testid="methods-result"]').count(), 0);
    assert.match(await page.locator('[data-testid="methods-no-results"]').innerText(), /exact version/);
    await page.getByRole("button", { name: "Show all results", exact: true }).click(); assert.equal(await page.locator('[data-testid="methods-result"]').count(), 3);
  });
  await check("native unavailable host is explicit and navigation performs no execution", async () => {
    await page.goto(base + "/?unavailable"); await page.locator('[data-testid="methods-unavailable"]').waitFor();
    assert.equal(await page.getByRole("button", { name: "Load graph", exact: true }).isDisabled(), true);
    assert.deepEqual(external, []); assert.deepEqual(errors, []);
  });

  if (nativeMode) {
    nativeDir = mkdtempSync(path.join(tmpdir(), "loom-method-ui-native-")); await startNative();
    const seed = spawnSync("python3", ["-c", `import json,gzip,sys
f=json.load(gzip.open(sys.argv[1],'rt'));p={k:f['packet'][k] for k in ['entities','claims','sources']};p['vocabulary']=f['contract']['vocabulary'];p['selection']={'members':[{'combination_version_id':f['contract']['bindings']['combination_version_id']}],'parameter_layers':['method','combination','member','selection','user']}
print(json.dumps({'method_registry':{'profile':p,'receipt_ids':[],'selection':{'parameters':{'retained_owner_overlay':314}},'preserve':{'sibling':'Żółć','native_float':1.0}},'graph_reply':{'mode':'off','untouched':True}},ensure_ascii=False))`, path.join(loom, "src/packet/tests/method-registry-variants/nested-combination.json.gz")], { encoding: "utf8" });
    assert.equal(seed.status, 0, seed.stderr);
    // Seed through the native opaque/CAS boundary: the generic config JSON
    // endpoint parses into unordered objects and cannot preserve DTO key order.
    const settingsBeforeSeed = await native("GET", "/api/methods/chat-settings");
    await native("POST", "/api/methods", { operation: "set_chat_settings", context_execution_json: seed.stdout,
      expected_context_execution_sha256: settingsBeforeSeed.context_execution_sha256 });
    await page.goto(base); await page.getByLabel("Method profile", { exact: true }).selectOption(record.id);
    await page.getByRole("button", { name: "Load graph", exact: true }).click(); await page.locator('[data-testid="methods-snapshot"]').waitFor();
    await check("actual native catalog/load/resolve returns repeated DAG paths and exact result Claims", async () => {
      assert.equal(await page.locator('[data-testid="methods-result"][data-complete="true"]').count(), 3);
      const callsBeforeFilter = calls.length;
      await page.locator(`[data-entity-id="${initialDefinition.id}"] button`).click();
      await page.getByRole("button", { name: "Results from this version", exact: true }).click();
      assert.equal(await page.locator('[data-testid="methods-result"]').count(), 3);
      const unused = nested.packet.entities.find(row => row.kind === profile.vocabulary.kinds.method_version && row.id !== initialDefinition.id);
      await page.locator(`[data-entity-id="${unused.id}"] button`).click();
      await page.getByRole("button", { name: "Results from this version", exact: true }).click();
      assert.equal(await page.locator('[data-testid="methods-result"]').count(), 0);
      await page.getByRole("button", { name: "Show all results", exact: true }).click();
      assert.equal(await page.locator('[data-testid="methods-result"]').count(), 3);
      assert.equal(calls.length, callsBeforeFilter, "Exact producer filtering is a read-only graph projection");
      assert.equal(JSON.parse(await page.locator('[data-testid="methods-selection-json"]').inputValue()).parameters.retained_owner_overlay, 314);
      await page.getByRole("button", { name: "Resolve selection without execution", exact: true }).click(); await page.locator('[data-testid="methods-resolution"]').waitFor();
      assert.equal(await page.locator('[data-testid="methods-resolved-leaf"]').count(), 4);
      await page.getByRole("button", { name: "Read native result lineage", exact: true }).click(); await page.locator('[data-testid="methods-native-lineage"]').waitFor();
      await page.locator('[data-testid="methods-native-lineage"] summary').click();
      const rows = JSON.parse(await page.locator('[data-testid="methods-native-lineage"] pre').innerText()); assert.equal(rows.provider_calls, 0);
      assert.equal(rows.producer_execution_verified, false);
      const baselineCatalog = await native("POST", "/api/methods", { operation: "catalog" });
      const baselineProfileJson = baselineCatalog.profiles.find(row => row.id === record.id).profile_json;
      const baselineSnapshot = await native("POST", "/api/methods", { operation: "load", profile_json: baselineProfileJson, receipt_ids: [] });
      // Generic config writes also have to retain exact ordered native DTOs.
      // Both paths receive original Python fixture bytes, without JS reencoding.
      for (const [verb, endpoint, rawBody] of [["PUT", "/api/config/context_execution", `{"value":${seed.stdout}}`],
        ["PATCH", "/api/config", `{"context_execution":${seed.stdout}}`]]) {
        await native(verb, endpoint, undefined, rawBody);
        const catalog = await native("POST", "/api/methods", { operation: "catalog" });
        const retainedJson = catalog.profiles.find(row => row.id === record.id).profile_json;
        assert.equal(retainedJson, baselineProfileJson, `${verb} must preserve DTO key order, float tokens and retained graph/source rows`);
        const retained = await native("POST", "/api/methods", { operation: "load", profile_json: retainedJson, receipt_ids: [] });
        assert.equal(retained.snapshot_sha256, baselineSnapshot.snapshot_sha256, `${verb} must preserve exact loadable native snapshot identity`);
      }
    });
    let nativeReceiptId, nativeVersionId, nativeReceiptJson;
    await check("actual native version fork/closed CAS accept saves explicit new selection while old run edges remain", async () => {
      await page.locator(`[data-entity-id="${initialDefinition.id}"] button`).click();
      await page.getByLabel("Target library", { exact: true }).fill("synthetic/UI/native-methods"); await page.getByLabel("Actor", { exact: true }).fill("synthetic-owner");
      await page.getByLabel("Known at (timestamp with timezone)", { exact: true }).fill("2026-10-05T11:00:00Z");
      const attrs = structuredClone(initialDefinition.attrs); attrs.definition.ui_native_fixture = { value: "Aą🙂B", unknown: [null, -17] }; attrs.retained_unknown = { yes: true };
      const originalAttrsJson = JSON.parse(nativeResponses.findLast(row => row.command === "load").raw).edit_attrs_json[initialDefinition.id];
      assert.equal(await page.locator('[data-testid="methods-version-json"]').inputValue(), originalAttrsJson);
      assert.match(originalAttrsJson, new RegExp(`"weight"\\s*:\\s*${initialDefinition.attrs.definition.selection.weight}\\.0`));
      await page.locator('[data-testid="methods-version-json"]').fill(JSON.stringify(attrs)); await page.getByRole("button", { name: "Validate new version", exact: true }).click();
      await page.locator('[data-testid="methods-version-preview"]').waitFor(); nativeVersionId = await page.locator('[data-testid="methods-version-preview"] > p code').innerText();
      const nativePreview = JSON.parse(await page.locator('[data-testid="methods-version-preview"] pre').textContent());
      assert.notEqual(nativeVersionId, initialDefinition.id);
      await page.getByRole("button", { name: "Accept new version into library", exact: true }).click(); await page.locator('[data-testid="methods-accepted"]').waitFor();
      nativeReceiptId = await page.locator('[data-testid="methods-accepted"] > p code').innerText();
      const read = await native("POST", "/api/methods", { operation: "read", receipt_id: nativeReceiptId }); assert.equal(read.row_drift.matches, true);
      nativeReceiptJson = read.receipt_json;
      assert.equal(typeof nativeReceiptJson, "string", "Native immutable receipt must expose its exact JSON bytes");
      nativeReceipts.push({ stage: "accepted_before_negative_overwrite", result: read });
      assert.deepEqual(read.receipt.packet.entities.find(row => row.id === initialDefinition.id), initialDefinition);
      assert.deepEqual(read.receipt.packet.entities.find(row => row.id === nativeVersionId).attrs.retained_unknown, { yes: true });
      const produced = read.receipt.packet.claims.filter(row => row.predicate === profile.vocabulary.predicates.produced_by_method_version);
      assert.equal(produced.length, 3); assert.ok(produced.every(row => row.object === initialDefinition.id));
      const catalogBefore = await native("POST", "/api/methods", { operation: "catalog" });
      const tampered = spawnSync("python3", ["-c", `import json,sys
d=json.load(sys.stdin);p=json.loads(d['profile_json']);edited=json.loads(d['edited_profile_json']);old=next(e for e in p['entities'] if e['id']==d['old_id']);new=next(e for e in edited['entities'] if e['id']==d['new_id']);old['attrs']=new['attrs'];print(json.dumps(p,ensure_ascii=False))`], {
        input: JSON.stringify({ profile_json: catalogBefore.profiles.find(row => row.id === record.id).profile_json, edited_profile_json: nativePreview.profile_json, old_id: initialDefinition.id, new_id: nativeVersionId }), encoding: "utf8" });
      assert.equal(tampered.status, 0, tampered.stderr);
      const rejectedRequest = JSON.stringify({ operation: "profile_preview", profile_json: tampered.stdout, receipt_ids: [], target: "synthetic/UI/native-methods", actor: "synthetic-owner", known_at: "2026-10-05T11:01:00Z" });
      const rejected = await fetch(nativeBase + "/api/methods", { method: "POST", headers: { "Content-Type": "application/json" }, body: rejectedRequest });
      const rejectedRaw = await rejected.text();
      nativeResponses.push({ source: "native_negative_verification", method: "POST", path: "/api/methods", command: "profile_preview", status: rejected.status,
        request_raw: rejectedRequest, request_sha256: createHash("sha256").update(rejectedRequest).digest("hex"), raw: rejectedRaw, sha256: createHash("sha256").update(rejectedRaw).digest("hex") });
      assert.equal(rejected.ok, false, "Existing immutable definition under original ID must be rejected at preview");
      assert.match(JSON.parse(rejectedRaw).error.message, /immutable|new version/i);
      const originalAgain = await native("POST", "/api/methods", { operation: "read", receipt_id: nativeReceiptId }); assert.equal(originalAgain.row_drift.matches, true);
      assert.equal(originalAgain.receipt_json, nativeReceiptJson, "Rejected overwrite must leave the entire native receipt unchanged");
      nativeReceipts.push({ stage: "after_rejected_immutable_overwrite", result: originalAgain });
      assert.deepEqual(originalAgain.receipt.packet.entities.find(row => row.id === initialDefinition.id), initialDefinition);
      const selection = { ...profile.selection, members: [{ method_version_id: nativeVersionId, weight: -42 }] };
      await page.locator('[data-testid="methods-selection-json"]').fill(JSON.stringify(selection));
      await page.getByRole("button", { name: "Save accepted profile selection", exact: true }).click(); await page.getByRole("status").filter({ hasText: "verified by readback" }).waitFor();
      const config = await native("GET", "/api/config"); assert.deepEqual(config.context_execution.method_registry.profile.selection, selection);
      assert.deepEqual(config.context_execution.method_registry.selection, selection);
      assert.deepEqual(config.context_execution.method_registry.receipt_ids, [nativeReceiptId]); assert.deepEqual(config.context_execution.graph_reply, { mode: "off", untouched: true });
      assert.deepEqual(config.context_execution.method_registry.preserve, { sibling: "Żółć", native_float: 1 });
      const settings = await native("POST", "/api/methods", { operation: "chat_settings" }); assert.match(settings.context_execution_json, /"native_float"\s*:\s*1\.0/);
      await page.getByRole("button", { name: "Load graph", exact: true }).click();
      await page.locator(`[data-entity-id="${nativeVersionId}"]`).waitFor();
      await page.locator(`[data-entity-id="${nativeVersionId}"] button`).click();
      await page.getByRole("button", { name: "Results from this version", exact: true }).click();
      assert.equal(await page.locator('[data-testid="methods-result"]').count(), 0, "Publishing/selecting a new version must not reassign older executed results");
      await page.locator(`[data-entity-id="${initialDefinition.id}"] button`).click();
      await page.getByRole("button", { name: "Results from this version", exact: true }).click();
      assert.equal(await page.locator('[data-testid="methods-result"]').count(), 3);
    });
    await check("actual native restart reads receipt and selected frozen profile without rewriting executed results", async () => {
      await stopChild(nativeServer); nativeServer = undefined; await startNative();
      const read = await native("POST", "/api/methods", { operation: "read", receipt_id: nativeReceiptId }); assert.equal(read.row_drift.matches, true);
      assert.equal(read.receipt_json, nativeReceiptJson, "Server restart must retain the entire immutable receipt byte-for-byte");
      nativeReceipts.push({ stage: "after_server_restart", result: read });
      const config = await native("GET", "/api/config"), registry = config.context_execution.method_registry;
      assert.equal(registry.profile.selection.members[0].method_version_id, nativeVersionId);
      const catalog = await native("POST", "/api/methods", { operation: "catalog" }), row = catalog.profiles.find(row => row.id === record.id);
      const snap = await native("POST", "/api/methods", { operation: "load", profile_json: row.profile_json, receipt_ids: registry.receipt_ids });
      assert.equal(snap.entities[nativeVersionId].attrs.definition.ui_native_fixture.value, "Aą🙂B");
      const lineage = await native("POST", "/api/methods", { operation: "result_lineage", snapshot_json: snap.snapshot_json }); assert.equal(lineage.provider_calls, 0); assert.equal(lineage.producer_execution_verified, false);
      assert.deepEqual(external, []); assert.deepEqual(errors, []);
    });
  }
} finally {
  await browser?.close(); await closeHttpFixture(server); await stopChild(nativeServer);
  if (process.env.METHODS_UI_EVIDENCE_DIR) {
    const evidence = process.env.METHODS_UI_EVIDENCE_DIR; assert.ok(existsSync(evidence), "Create a fresh evidence directory first");
    for (const name of ["results.json", "server.log"]) assert.equal(existsSync(path.join(evidence, name)), false, "Do not overwrite prior evidence");
    writeFileSync(path.join(evidence, "server.log"), nativeLog);
    writeFileSync(path.join(evidence, "results.json"), JSON.stringify({ groups, expected, nativeMode, externalRequests: external, pageErrors: errors,
      modelDispatchCommands: calls.filter(row => ["prepare", "bind"].includes(row.body?.operation)), calls, nativeLog,
      manifestBefore, manifestAfter: fileManifest(), nativeReceipts, nativeResponses }, null, 2));
  }
  if (nativeDir) rmSync(nativeDir, { recursive: true, force: true });
}
completion.complete(); console.log(`[methods-ui] ${groups.length}/${expected} groups passed; retained public synthetic fixtures${nativeMode ? " and actual native methods/store/config/restart" : ""}, zero model dispatch and zero external requests`);
