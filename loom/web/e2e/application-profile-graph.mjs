#!/usr/bin/env node
// Actual profile runtime/serializer, Python GraphPacket codec and Loom HTTP
// transport. Native mode starts an isolated server; no chat/model/provider calls.
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { createHash, webcrypto } from "node:crypto";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:net";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import ts from "typescript";

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const loomRoot = path.resolve(webRoot, "..");
const offlineOnly = process.argv.includes("--offline");
if (!globalThis.crypto?.subtle) globalThis.crypto = webcrypto;
function transpile(file) {
  return ts.transpileModule(readFileSync(new URL(file, import.meta.url), "utf8"), {
    compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022 },
  }).outputText + `\n//# sourceURL=${file}\n`;
}
const moduleUrl = source => `data:text/javascript;base64,${Buffer.from(source).toString("base64")}`;
const runtimeUrl = moduleUrl(transpile("../src/profiles/runtime.ts"));
const typesUrl = moduleUrl(transpile("../src/api/types.ts"));
const operationsUrl = moduleUrl(transpile("../src/api/operations.ts"));
const runtime = await import(runtimeUrl);
const graph = await import(moduleUrl(transpile("../src/profiles/graph.ts").replaceAll('from "./runtime"', `from "${runtimeUrl}"`)));
const { LoomHttpApi } = await import(moduleUrl(transpile("../src/api/loom-http.ts")
  .replaceAll('from "./types"', `from "${typesUrl}"`)
  .replaceAll('from "./operations"', `from "${operationsUrl}"`)));
const { LoomJniApi } = await import(moduleUrl(transpile("../src/api/loom-jni.ts").replaceAll('from "./types"', `from "${typesUrl}"`)));

const groups = [], nativeRequests = [], persisted = [];
const originalFetch = globalThis.fetch;
const originalSessionStorage = globalThis.sessionStorage;
const storage = new Map();
let server, serverLog = "", nativeDir, configBefore, configAfter, failure, spawnFailure;
const evidenceDir = process.env.APPLICATION_PROFILE_GRAPH_EVIDENCE_DIR;
if (evidenceDir) {
  mkdirSync(evidenceDir, { recursive: true });
  assert.ok(!existsSync(path.join(evidenceDir, "application-profile-graph-results.json")), "Use a fresh evidence directory to preserve prior results.");
}
async function stopServer() {
  if (!server || server.exitCode !== null || server.signalCode !== null || server.pid === undefined) return;
  const child = server;
  await new Promise(resolve => {
    let fallback;
    const finish = () => {
      clearTimeout(timeout); clearTimeout(fallback);
      child.removeListener("close", finish); child.removeListener("error", finish); resolve();
    };
    const timeout = setTimeout(() => { child.kill("SIGKILL"); fallback = setTimeout(finish, 1000); }, 5000);
    child.once("close", finish); child.once("error", finish);
    child.kill("SIGTERM");
  });
}
async function freePort() {
  const probe = createServer();
  await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
  const port = probe.address().port;
  await new Promise(resolve => probe.close(resolve));
  return port;
}
async function check(name, fn) { await fn(); groups.push(name); console.log(`[application-profile-graph] PASS ${name}`); }
const clone = value => JSON.parse(JSON.stringify(value));
function canonical(value) {
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  if (value && typeof value === "object") return `{${Object.keys(value).sort().map(key => `${JSON.stringify(key)}:${canonical(value[key])}`).join(",")}}`;
  return JSON.stringify(value);
}
const sha256 = text => createHash("sha256").update(text, "utf8").digest("hex");
function definition() {
  return {
    schema: runtime.PROFILE_SCHEMA, id: "synthetic.graph.profile", profile_revision: 1,
    label: "Fikcyjny profil Żółć / 日本語 / 🧠", target: { application_id: "fictional", version: "2099.β", platform: "web" },
    evidence: { status: "inspired", sources: [], gaps: ["Synthetic fixture; original-application parity has not been established."] },
    presentation: { renderer: "chat", tokens: { background: "#111", surface: "#222", text: "#fff", muted: "#999", accent: "#aaa", border: "#333" },
      sidebar: { side: "left", width: 1e-6 }, content_width: 0.333, message_style: "plain" },
    composer: { submit: "mod-enter", placeholder: "Wiadomość 🧠\nSecond line" },
    actions: [{ id: "send", label: "Send", operation: "chat.send", capability: "chat.send", required: true }],
    workflows: [{ id: "flow", initial: "ready", states: ["ready", "complete"],
      transitions: [{ from: "ready", event: "submit", action: "send", to: "complete",
        payload: { object: { request: { from: "inputs", pointer: "/request" } } },
        save: { messageId: { from: "result", pointer: "/message_id" } } }] }],
  };
}
const originalText = JSON.stringify(definition(), null, 2).replace('"width": 0.000001', '"width": 1e-6') + " \r\n";
const source = { text: originalText, sourceRef: "synthetic:Żółć/profile-🧠.json", actor: "synthetic-test-user" };
const profile = runtime.registerProfile(runtime.createProfileRegistry(), JSON.parse(originalText));
let acceptance;
function validateWithPython(packet) {
  const script = [
    "import json, sys", "sys.path.insert(0, sys.argv[1])",
    "from agentic_graph_v1.packet import validate_packet", "packet=json.load(sys.stdin)",
    "validate_packet(packet)", "print(packet['packet_id'])",
  ].join("\n");
  const result = spawnSync(process.env.PYTHON || "python3", ["-c", script, path.join(loomRoot, "tools/structure")], {
    input: JSON.stringify(packet), encoding: "utf8", timeout: 15000,
  });
  assert.ifError(result.error);
  assert.equal(result.status, 0, `Actual Python GraphPacket codec rejected serializer output:\n${result.stderr}`);
  assert.equal(result.stdout.trim(), packet.packet_id);
}
function syntheticReadResult(request) {
  const payload = { target: request.target, packet: request.packet, selection: request.selection,
    expected_rows: request.expected_rows, explicitly_accepted: true };
  const hash = sha256(canonical(payload));
  const receipt = { ...payload, id: `gpr_${hash}`, request_sha256: hash, run_id: "kr_synthetic_contract_only",
    acceptance_establishes_content_truth: false, receipt_sha256: "a".repeat(64) };
  return { receipt, row_drift: { matches: true, rows: [], current_row_snapshots: {} }, replayed: false };
}

try {
await check("deterministic complete raw-source projection passes the actual Python GraphPacket codec", async () => {
  acceptance = await graph.makeApplicationProfileGraphAcceptance(profile, source);
  assert.deepEqual(await graph.makeApplicationProfileGraphAcceptance(profile, source), acceptance);
  validateWithPython(acceptance.packet);
  const entity = acceptance.packet.entities[0], record = acceptance.packet.sources[0];
  assert.equal(entity.attrs.raw_source, originalText);
  assert.equal(record.observation.text, originalText);
  assert.ok(originalText.includes('"width": 1e-6'));
  assert.ok(originalText.endsWith(" \r\n"));
  assert.equal(entity.attrs.raw_sha256, sha256(originalText));
  assert.equal(record.text_sha256, sha256(originalText));
  assert.equal(record.observation.locator.byte_len, Buffer.byteLength(originalText, "utf8"));
  assert.ok(record.observation.locator.byte_len > originalText.length, "UTF-8 byte count differs from JS string length");
  assert.equal(JSON.parse(entity.attrs.raw_source).presentation.sidebar.width, 1e-6);
  assert.equal(JSON.parse(entity.attrs.raw_source).presentation.content_width, 0.333);
  assert.deepEqual(acceptance.selection, { entities: [entity.id], claims: [], sources: [record.observation.id] });
  assert.deepEqual(acceptance.expected_rows, { entities: { [entity.id]: null }, claims: {}, sources: { [record.observation.id]: null } });
});

await check("raw whitespace or recorded source changes use distinct immutable observation identities", async () => {
  const compact = await graph.makeApplicationProfileGraphAcceptance(profile, { ...source, text: JSON.stringify(profile) });
  const decimal = await graph.makeApplicationProfileGraphAcceptance(profile, { ...source, text: originalText.replace('"width": 1e-6', '"width": 0.000001') });
  const relocated = await graph.makeApplicationProfileGraphAcceptance(profile, { ...source, sourceRef: "synthetic:other-location.json" });
  const anotherActor = await graph.makeApplicationProfileGraphAcceptance(profile, { ...source, actor: "another-recorded-user" });
  for (const variant of [compact, decimal, relocated, anotherActor]) {
    assert.equal(variant.packet.entities[0].id, acceptance.packet.entities[0].id);
    assert.equal(variant.target, acceptance.target);
    assert.notEqual(variant.packet.sources[0].observation.id, acceptance.packet.sources[0].observation.id);
    validateWithPython(variant.packet);
  }
  assert.notEqual(compact.packet.entities[0].attrs.raw_sha256, acceptance.packet.entities[0].attrs.raw_sha256);
  assert.notEqual(decimal.packet.entities[0].attrs.raw_sha256, acceptance.packet.entities[0].attrs.raw_sha256);
  const changedDefinition = definition(); changedDefinition.presentation.content_width = 0.334;
  const changed = runtime.registerProfile(runtime.createProfileRegistry(), changedDefinition);
  const changedAcceptance = await graph.makeApplicationProfileGraphAcceptance(changed, { ...source, text: JSON.stringify(changedDefinition) });
  assert.notEqual(changedAcceptance.packet.entities[0].id, acceptance.packet.entities[0].id);
  assert.notEqual(changedAcceptance.target, acceptance.target);
  validateWithPython(changedAcceptance.packet);
});

await check("leading UTF-8 BOM remains exact source evidence while profile parsing preserves its semantic definition", async () => {
  const bomText = "\ufeff" + originalText;
  assert.deepEqual(graph.parseApplicationProfileSource(bomText), profile);
  const bom = await graph.makeApplicationProfileGraphAcceptance(profile, { ...source, text: bomText });
  assert.equal(bom.packet.entities[0].id, acceptance.packet.entities[0].id);
  assert.notEqual(bom.packet.sources[0].observation.id, acceptance.packet.sources[0].observation.id);
  assert.equal(bom.packet.sources[0].observation.text, bomText);
  assert.equal(bom.packet.entities[0].attrs.raw_source, bomText);
  assert.equal(bom.packet.entities[0].attrs.raw_sha256, sha256(bomText));
  assert.equal(bom.packet.sources[0].observation.locator.byte_len, Buffer.byteLength(bomText, "utf8"));
  assert.equal(Buffer.byteLength(bomText, "utf8"), Buffer.byteLength(originalText, "utf8") + 3);
  validateWithPython(bom.packet);
});

await check("missing dates and transformation history stay unknown; explicit dates and CAS expectations survive", async () => {
  const packet = acceptance.packet;
  assert.deepEqual(packet.history, []); assert.deepEqual(packet.claims, []); assert.deepEqual(packet.definitions, []);
  assert.equal(packet.task.transformation_history, "not_asserted");
  assert.equal(packet.sources[0].known_at, null); assert.equal(packet.sources[0].observation.date, "");
  assert.equal(packet.entities[0].first_seen, ""); assert.equal(packet.entities[0].last_seen, "");
  for (const collection of ["entities", "sources"]) for (const entry of Object.values(packet.provenance[collection])) {
    assert.equal(entry.known_at, null);
    assert.deepEqual(entry.origin, { kind: "recorded", actor: source.actor, model: null, recipe_sha256: null, response_sha256: null });
  }
  const knownAt = "2026-10-04T12:11:31+02:00";
  const dated = await graph.makeApplicationProfileGraphAcceptance(profile, { ...source, knownAt });
  assert.equal(dated.packet.sources[0].known_at, knownAt); assert.equal(dated.packet.sources[0].observation.date, knownAt);
  assert.notEqual(dated.packet.sources[0].observation.id, packet.sources[0].observation.id);
  validateWithPython(dated.packet);
  const expectations = clone(acceptance.expected_rows);
  expectations.entities[packet.entities[0].id] = "b".repeat(64);
  const cas = await graph.makeApplicationProfileGraphAcceptance(profile, source, expectations);
  assert.deepEqual(cas.packet, packet); assert.deepEqual(cas.expected_rows, expectations);
});

await check("source mismatch, malformed JSON, invalid timestamps and malformed Unicode are rejected", async () => {
  await assert.rejects(graph.makeApplicationProfileGraphAcceptance(profile, { ...source, text: "{broken" }), /valid JSON/);
  await assert.rejects(graph.makeApplicationProfileGraphAcceptance(profile, { ...source, text: JSON.stringify({ ...profile, label: "Different source" }) }), /differs/);
  for (const field of ["text", "sourceRef", "actor"]) await assert.rejects(graph.makeApplicationProfileGraphAcceptance(profile, { ...source, [field]: " \n" }), /nonempty/);
  for (const knownAt of ["invalid", "2026-10-04T12:11:31", "2026-99-99T12:11:31Z", "2026-02-30T12:11:31Z", 123]) {
    await assert.rejects(graph.makeApplicationProfileGraphAcceptance(profile, { ...source, knownAt }), /timestamp/);
  }
  for (const field of ["sourceRef", "actor"]) await assert.rejects(graph.makeApplicationProfileGraphAcceptance(profile, { ...source, [field]: "bad-\ud800" }), /Unicode/);
  const malformedDefinition = definition(); malformedDefinition.label = "unpaired-\udc00";
  const malformed = runtime.registerProfile(runtime.createProfileRegistry(), malformedDefinition);
  await assert.rejects(graph.makeApplicationProfileGraphAcceptance(malformed, { ...source, text: JSON.stringify(malformedDefinition) }), /Unicode/);
  for (const field of ["api_key", "settings"]) {
    const polluted = clone(profile); polluted[field] = field === "api_key" ? "synthetic-secret-must-not-persist" : { model: "injected-model" };
    await assert.rejects(graph.makeApplicationProfileGraphAcceptance(polluted, { ...source, text: JSON.stringify(polluted) }), /unknown field/);
  }
});

await check("receipt restoration validates projection, acceptance identity and drift before registration", async () => {
  const result = syntheticReadResult(acceptance);
  const restored = await graph.readProfileFromReceipt(result, runtime.createProfileRegistry());
  assert.deepEqual(restored.profile, profile); assert.deepEqual(restored.source, { ...source, knownAt: null });
  assert.equal(restored.rawSha256, sha256(originalText));
  const changes = [
    value => { value.row_drift.matches = false; },
    value => { value.receipt.explicitly_accepted = false; },
    value => { value.receipt.acceptance_establishes_content_truth = true; },
    value => { value.receipt.packet.entities[0].attrs.raw_source += "\n"; },
    value => { value.receipt.packet.sources[0].observation.text += "\n"; },
    value => { value.receipt.packet.entities[0].id = "e_wrong"; },
    value => { value.receipt.target = "wrong-target"; },
    value => { value.receipt.selection.sources = []; },
    value => { value.receipt.expected_rows.entities[acceptance.selection.entities[0]] = "0".repeat(64); },
    value => { value.receipt.id = "gpr_wrong"; },
    value => { value.receipt.request_sha256 = "0".repeat(64); },
    value => { value.receipt.receipt_sha256 = "not-a-hash"; },
  ];
  for (const change of changes) {
    const corrupted = clone(result); change(corrupted);
    const registry = runtime.createProfileRegistry();
    await assert.rejects(graph.readProfileFromReceipt(corrupted, registry));
    const replacement = clone(profile); replacement.label = "Probe: rejected receipt did not reserve this revision";
    assert.equal(runtime.registerProfile(registry, replacement).label, replacement.label,
      "Rejected receipt cannot install a profile or reserve its immutable revision");
  }
});

globalThis.sessionStorage = { getItem: key => storage.get(key) ?? null, setItem: (key, value) => storage.set(key, value), removeItem: key => storage.delete(key) };
try {
  await check("actual HTTP adapter preserves named endpoint, request, bearer authentication and error semantics", async () => {
    const calls = []; let response = { status: 200, body: syntheticReadResult(acceptance) };
    globalThis.fetch = async (url, init) => { calls.push({ url, init }); return new Response(JSON.stringify(response.body), { status: response.status }); };
    storage.set("loom.auth_token", "synthetic-in-memory-token");
    const api = new LoomHttpApi();
    assert.deepEqual(await api.graphPacketStore(acceptance), response.body);
    assert.equal(calls[0].url, "/api/graph/packets/store");
    assert.equal(calls[0].init.method, "POST");
    assert.deepEqual(calls[0].init.headers, { "Content-Type": "application/json", Authorization: "Bearer synthetic-in-memory-token" });
    assert.deepEqual(JSON.parse(calls[0].init.body), acceptance);
    const read = { operation: "read", receipt_id: response.body.receipt.id };
    await api.graphPacketStore(read); assert.deepEqual(JSON.parse(calls.at(-1).init.body), read);
    api.setAuthToken("replacement-token"); await api.graphPacketStore(read);
    assert.equal(calls.at(-1).init.headers.Authorization, "Bearer replacement-token");
    api.setAuthToken(null); await api.graphPacketStore(read);
    assert.equal(calls.at(-1).init.headers.Authorization, undefined); assert.equal(storage.has("loom.auth_token"), false);
    response = { status: 409, body: { error: { code: "conflict", message: "CAS conflict" } } };
    await assert.rejects(api.graphPacketStore(read), error => error.status === 409 && error.message === "CAS conflict");
    response = { status: 200, body: { error: { code: "invalid_argument", message: "Native rejected" } } };
    await assert.rejects(api.graphPacketStore(read), error => error.status === 200 && error.message === "Native rejected");
    response = { status: 503, body: {} };
    await assert.rejects(api.graphPacketStore(read), error => error.status === 503 && error.message === "HTTP 503");
    const jni = new LoomJniApi();
    assert.equal(jni.graphPacketStore, undefined, "Unimplemented Android persistence stays an absent capability");
  });
} finally {
  globalThis.fetch = originalFetch;
}

  if (!offlineOnly) {
    const serverBin = process.env.LOOM_SERVER_BIN || path.join(loomRoot, "build/dev/server/loom-server");
    assert.ok(existsSync(serverBin), `Build loom-server first: ${serverBin}`);
    nativeDir = mkdtempSync(path.join(tmpdir(), "loom-profile-graph-"));
    writeFileSync(path.join(nativeDir, "models.json"), JSON.stringify([{ id: "synthetic/unused", name: "Unused cached fixture", context_length: 8192 }]) + "\n");
    writeFileSync(path.join(nativeDir, "config.json"), JSON.stringify({ semantic_analysis: false }));
    const port = await freePort(), base = `http://127.0.0.1:${port}`, token = "synthetic-loopback-profile-test";
    function startServer() {
      spawnFailure = undefined;
      server = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", nativeDir, "--token", token], {
        cwd: loomRoot, stdio: ["ignore", "pipe", "pipe"],
      });
      for (const output of [server.stdout, server.stderr]) output.on("data", chunk => { serverLog += chunk.toString(); });
      server.on("error", error => { spawnFailure = error; serverLog += error.stack; });
    }
    async function ready() {
      const deadline = Date.now() + 20000;
      while (Date.now() < deadline) {
        if (spawnFailure || server.exitCode !== null || server.signalCode !== null) throw new Error(`Native server failed to start: ${serverLog}`);
        try { if ((await originalFetch(`${base}/api/healthz`, { headers: { Authorization: `Bearer ${token}` }, signal: AbortSignal.timeout(1000) })).ok) return; } catch { /* Starting. */ }
        await new Promise(resolve => setTimeout(resolve, 100));
      }
      throw new Error(`Native server startup timed out: ${serverLog}`);
    }
    globalThis.fetch = async (url, init) => {
      const endpoint = String(url);
      assert.ok(["/api/graph/packets/store", "/api/config", "/api/knowledge/query"].includes(endpoint), `Unexpected API call: ${endpoint}`);
      const request = { endpoint, method: init.method, body: init.body === undefined ? undefined : JSON.parse(init.body) };
      nativeRequests.push(request);
      const response = await originalFetch(`${base}${endpoint}`, { ...init, signal: AbortSignal.timeout(15000) });
      request.status = response.status;
      if (!response.ok) request.response = await response.clone().text();
      return response;
    };
    startServer(); await ready();
    const api = new LoomHttpApi(); api.setAuthToken(token);
    configBefore = await api.getConfig();
    let receiptId;
    await check("actual HTTP/native acceptance persists complete source bytes and restores a profile from fresh readback", async () => {
      api.setAuthToken(null);
      await assert.rejects(api.graphPacketStore(acceptance), error => error.status === 401);
      api.setAuthToken(token);
      const rejected = clone(acceptance); rejected.explicitly_accepted = false;
      await assert.rejects(api.graphPacketStore(rejected), error => error.status === 400 && /explicit acceptance/.test(error.message));
      const accepted = await api.graphPacketStore(acceptance);
      assert.equal(accepted.replayed, false); assert.equal(accepted.row_drift.matches, true);
      assert.deepEqual(accepted.receipt.packet, acceptance.packet);
      receiptId = accepted.receipt.id;
      const read = await api.graphPacketStore({ operation: "read", receipt_id: receiptId });
      assert.deepEqual(read.receipt, accepted.receipt); assert.equal(read.replayed, false);
      const sourceSnapshot = read.row_drift.current_row_snapshots.sources[acceptance.selection.sources[0]];
      assert.equal(sourceSnapshot.row.length, 1, "Fresh native snapshot contains the selected observation row");
      assert.deepEqual(JSON.parse(sourceSnapshot.row[0].body), acceptance.packet.sources[0].observation);
      assert.equal(JSON.parse(sourceSnapshot.row[0].body).text, originalText,
        "The native observation row retains exact raw bytes, including exponent spelling and trailing whitespace");
      const restored = await graph.readProfileFromReceipt(read, runtime.createProfileRegistry());
      assert.deepEqual(restored.profile, profile); assert.deepEqual(restored.source, { ...source, knownAt: null });
      assert.equal(restored.entityId, acceptance.selection.entities[0]);
      assert.equal(restored.receiptId, receiptId); assert.equal(restored.runId, accepted.receipt.run_id);
      const rows = await api.knowledge.query("entities", { run: restored.runId, kind: "application_profile" });
      const row = rows.items.find(item => item.id === restored.entityId);
      assert.ok(row, "Profile is visible through the existing native knowledge store");
      assert.equal(row.attrs.raw_source, originalText); assert.equal(row.attrs.raw_sha256, sha256(originalText));
      assert.deepEqual(JSON.parse(row.attrs.raw_source), profile);
      persisted.push({ receipt: accepted.receipt, restored });
    });
    await check("actual native replay and identical acceptance retries retain the immutable original receipt", async () => {
      const replay = await api.graphPacketStore({ operation: "replay", receipt_id: receiptId });
      const retry = await api.graphPacketStore(acceptance);
      assert.equal(replay.replayed, true); assert.equal(retry.replayed, true);
      assert.deepEqual(replay.receipt, persisted[0].receipt); assert.deepEqual(retry.receipt, persisted[0].receipt);
      assert.deepEqual((await graph.readProfileFromReceipt(replay, runtime.createProfileRegistry())).profile, profile);
    });
    await check("native BOM source persistence and receipt restoration preserve all original UTF-8 bytes", async () => {
      const bomText = "\ufeff" + originalText.replace('"profile_revision": 1', '"profile_revision": 2');
      const bomProfile = runtime.registerProfile(runtime.createProfileRegistry(), graph.parseApplicationProfileSource(bomText));
      const bomSource = { ...source, text: bomText, sourceRef: "synthetic:BOM-Żółć/profile-🧠.json" };
      const request = await graph.makeApplicationProfileGraphAcceptance(bomProfile, bomSource);
      const accepted = await api.graphPacketStore(request);
      const read = await api.graphPacketStore({ operation: "read", receipt_id: accepted.receipt.id });
      const restored = await graph.readProfileFromReceipt(read, runtime.createProfileRegistry());
      assert.deepEqual(restored.profile, bomProfile); assert.equal(restored.source.text, bomText);
      assert.equal(restored.rawSha256, sha256(bomText));
      assert.equal(JSON.parse(read.row_drift.current_row_snapshots.sources[request.selection.sources[0]].row[0].body).text, bomText);
      persisted.push({ receipt: accepted.receipt, restored });
    });
    await check("native profile receipt and exact source restore after server restart without changing model configuration", async () => {
      await stopServer(); startServer(); await ready();
      for (const stored of persisted) {
        const read = await api.graphPacketStore({ operation: "read", receipt_id: stored.receipt.id });
        assert.equal(read.row_drift.matches, true); assert.deepEqual(read.receipt, stored.receipt);
        const restored = await graph.readProfileFromReceipt(read, runtime.createProfileRegistry());
        assert.equal(restored.source.text, stored.restored.source.text); assert.deepEqual(restored.profile, stored.restored.profile);
      }
      configAfter = await api.getConfig(); assert.deepEqual(configAfter, configBefore);
    });
  }
} catch (error) {
  failure = { message: error.message, stack: error.stack };
  if (serverLog) console.error(serverLog);
  throw error;
} finally {
  await stopServer();
  if (nativeDir) rmSync(nativeDir, { recursive: true, force: true });
  globalThis.fetch = originalFetch;
  if (originalSessionStorage === undefined) delete globalThis.sessionStorage;
  else globalThis.sessionStorage = originalSessionStorage;
  if (evidenceDir) writeFileSync(path.join(evidenceDir, "application-profile-graph-results.json"), JSON.stringify({
    groups, counts: { passed_groups: groups.length, model_provider_calls: 0 }, offline_only: offlineOnly,
    native_requests: nativeRequests, persisted, failure,
    config_unchanged: configBefore !== undefined && canonical(configBefore) === canonical(configAfter),
  }, null, 2) + "\n", { flag: "wx" });
}
console.log(`[application-profile-graph] ${groups.length}/${groups.length} groups passed (${offlineOnly ? "offline serializer/codec/transport contract" : "actual isolated native HTTP persistence"}; 0 model/provider calls)`);
