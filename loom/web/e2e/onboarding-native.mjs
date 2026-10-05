#!/usr/bin/env node
// Real HTTP -> OnboardingStore -> native SQLite/knowledge graph. All inputs
// below are authored public fixtures. No provider completion is dispatched.
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { stopChild, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const loomRoot = fileURLToPath(new URL("../../", import.meta.url));
const serverBin = process.env.LOOM_SERVER_BIN || path.join(loomRoot, "build/dev/server/loom-server");
assert.ok(existsSync(serverBin), `Build the current native server first: ${serverBin}`);
const runtimeFoundationInstalled = existsSync(path.join(loomRoot, "include/loom/runtime_profile.h"));
const evidenceDir = process.env.ONBOARDING_NATIVE_EVIDENCE_DIR;
if (evidenceDir) {
  mkdirSync(evidenceDir, { recursive: true });
  for (const name of ["results.json", "server.log"]) assert.ok(!existsSync(path.join(evidenceDir, name)), "Use a fresh evidence directory; preserve negative attempts.");
}
const dataDir = mkdtempSync(path.join(tmpdir(), "loom-onboarding-native-"));
writeFileSync(path.join(dataDir, "config.json"), JSON.stringify({ semantic_analysis: false, base_url: "http://127.0.0.1:1", default_model: "offline/onboarding-native" }));
writeFileSync(path.join(dataDir, "models.json"), "[]");
const probe = createServer();
await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
const port = probe.address().port;
await new Promise(resolve => probe.close(resolve));
const base = `http://127.0.0.1:${port}`;
const user = "synthetic/onboarding-http-user Ω";
const groups = [], commands = [];
const completion = suiteCompletionGuard("onboarding-native", 8, groups);
let server, serverLog = "", failure = null, snapshot;

function startServer() {
  serverLog += "\n[start native onboarding server]\n";
  server = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", dataDir, "--token", ""], {
    cwd: loomRoot, env: process.env, stdio: ["ignore", "pipe", "pipe"],
  });
  server.stdout.on("data", chunk => { serverLog += chunk.toString(); });
  server.stderr.on("data", chunk => { serverLog += chunk.toString(); });
  server.on("error", error => { serverLog += String(error); });
}
async function stopServer() {
  await stopChild(server);
}
async function ready() {
  const deadline = Date.now() + 20000;
  while (Date.now() < deadline) {
    try { if ((await fetch(`${base}/api/healthz`, { signal: AbortSignal.timeout(1000) })).ok) return; } catch { /* startup */ }
    if (server.exitCode !== null) throw Error(`Native server exited: ${serverLog}`);
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  throw Error(`Native server startup timeout: ${serverLog}`);
}
async function request(body, status = 200, { raw = false, endpoint = "/api/onboarding", method = "POST" } = {}) {
  commands.push({ endpoint, method, body });
  const response = await fetch(`${base}${endpoint}`, { method, headers: { "Content-Type": "application/json" },
    ...(body === undefined ? {} : { body: raw ? body : JSON.stringify(body) }), signal: AbortSignal.timeout(10000) });
  const result = await response.json();
  assert.equal(response.status, status, `${endpoint}: ${JSON.stringify(result)}`);
  if (status >= 400) assert.ok(result.error?.code, "Errors retain the native error envelope");
  else assert.equal(result.error, undefined, `Unexpected native error: ${JSON.stringify(result)}`);
  return result;
}
const command = (operation, fields = {}, status = 200) => request({ operation, user_id: user, ...fields }, status);
function state(value) {
  assert.deepEqual(Object.keys(value).sort(), ["revision", "snapshot"]);
  assert.match(value.revision, /^(0|[1-9][0-9]*)$/);
  assert.equal(value.snapshot.schema, "loom.onboarding_store/1");
  assert.equal(value.snapshot.user_id, user);
  return value;
}
const apply = async action => { snapshot = state(await command("apply", { expected_revision: snapshot.revision, action })); return snapshot; };
const event = (op, id, rest = {}) => ({ op, id, time: "2000-01-01T00:00:00Z", source_refs: [`synthetic/${id}`], ...rest });
const resolution = key => snapshot.snapshot.effectiveDefaults.find(item => item.key === key);
async function check(name, run) { await run(); groups.push(name); console.log(`[onboarding-native] PASS ${name}`); }

// Deliberately seed a high outer revision in this suite's own stopped synthetic
// database. This makes precision loss observable, rather than merely checking
// that a large stale decimal token gets a generic conflict.
function seedOuterRevision(token) {
  const python = process.env.CODEX_PRIMARY_RUNTIME_PYTHON || process.env.PYTHON || "python3";
  const script = `import json, sqlite3, sys
db, user, token = sys.argv[1:]
connection = sqlite3.connect(db)
row = connection.execute("SELECT body FROM loom_onboarding_profiles WHERE user_id=?", (user,)).fetchone()
assert row is not None
body = json.loads(row[0]); body["revision"] = int(token)
with connection:
    connection.execute("UPDATE loom_onboarding_profiles SET revision=?,body=? WHERE user_id=?", (int(token),json.dumps(body,ensure_ascii=False,separators=(",",":")),user))
connection.close()
`;
  const seeded = spawnSync(python, ["-c", script, path.join(dataDir, "chatadhd.db"), user, token], { encoding: "utf8" });
  assert.equal(seeded.status, 0, `High-revision fixture seed failed: ${seeded.stderr || seeded.error}`);
}

try {
  startServer(); await ready();
  await check("strict transport validation rejects malformed and unexpected operation fields before mutation", async () => {
    for (const body of ["{", "null", "[]"]) await request(body, 400, { raw: true });
    for (const body of [{}, { operation: "open", user_id: "" }, { operation: "open", user_id: 2 },
      { operation: "unknown", user_id: user }, { operation: "open", user_id: user, legacy: [] },
      { operation: "read", user_id: user, action: {} }, { operation: "open", user_id: user, unexpected: true }]) {
      await request(body, 400);
    }
    assert.equal((await command("read", {}, 404)).error.code, "not_found");
  });

  let originalConversation;
  await check("native open and read preserve legacy bytes and seed real graph definitions with unknown personal fields", async () => {
    originalConversation = await request({ title: "Synthetic untouched onboarding conversation" }, 201, { endpoint: "/api/conversations" });
    snapshot = state(await command("open", { legacy: { public_fixture_extension: { bytes: "  keep\n Ω  ", null_value: null } } }));
    assert.equal(snapshot.revision, "0");
    assert.ok(snapshot.snapshot.graph_run_id);
    if (runtimeFoundationInstalled) {
      assert.ok(snapshot.snapshot.runtime_profile.available !== false, "An installed RuntimeProfile foundation must be operative");
    } else {
      assert.equal(snapshot.snapshot.runtime_profile.available, false);
      assert.match(snapshot.snapshot.runtime_profile.reason, /RuntimeProfile from thread 11 is not integrated/,
        "Native inspection must explicitly disclose the absent optional foundation");
    }
    for (const field of Object.values(snapshot.snapshot.profile.fields)) { assert.equal(field.status, "unknown"); assert.equal(field.value, null); }
    assert.ok(Object.keys(snapshot.snapshot.graph.nodes).length > 0);
    const entities = await request({ what: "entities", run: snapshot.snapshot.graph_run_id, limit: 10000 }, 200, { endpoint: "/api/knowledge/query" });
    assert.ok(entities.items.length > 0, "native onboarding seeds the existing knowledge store");
    assert.deepEqual(snapshot.snapshot.profile.public_fixture_extension, { bytes: "  keep\n Ω  ", null_value: null });
    assert.deepEqual(state(await command("read")), snapshot);
    assert.deepEqual(state(await command("open", { legacy: { public_fixture_extension: "must not overwrite" } })), snapshot);
    const other = await request({ operation: "open", user_id: "synthetic/separate-onboarding-user" });
    assert.equal(other.snapshot.user_id, "synthetic/separate-onboarding-user");
    assert.equal(other.snapshot.profile.public_fixture_extension, undefined);
  });

  await check("CAS accepts only canonical int64 strings and a concurrent stale writer cannot overwrite", async () => {
    const before = structuredClone(snapshot);
    for (const token of [0, null, "", "00", "01", "-1", "+1", "1.0", "1e0", " 0", "0 ", "9223372036854775808"]) {
      const rejected = await command("apply", { expected_revision: token, action: { target: "layers", op: "disable", key: "preference.style" } }, 400);
      assert.equal(rejected.error.code, "invalid_argument");
    }
    for (const token of ["9007199254740993", "9223372036854775807"]) {
      assert.equal((await command("apply", { expected_revision: token, action: {} }, 409)).error.code, "conflict");
    }
    assert.deepEqual(state(await command("read")), before);
    const writes = [{ target: "layers", op: "override", key: "preference.style", value: "synthetic concurrent first" },
      { target: "layers", op: "override", key: "preference.style", value: "synthetic concurrent second" }];
    const results = await Promise.all(writes.map(async action => {
      const body = { operation: "apply", user_id: user, expected_revision: before.revision, action };
      commands.push({ endpoint: "/api/onboarding", method: "POST", body });
      const response = await fetch(`${base}/api/onboarding`, { method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body), signal: AbortSignal.timeout(10000) });
      return { status: response.status, value: await response.json(), action };
    }));
    assert.deepEqual(results.map(result => result.status).sort(), [200, 409]);
    assert.equal(results.find(result => result.status === 409).value.error.code, "conflict");
    snapshot = state(await command("read"));
    assert.equal(snapshot.revision, (BigInt(before.revision) + 1n).toString());
    assert.equal(resolution("preference.style").value, results.find(result => result.status === 200).action.value);
  });

  await check("form answer is a candidate until a separate native review confirms its provenance and preference", async () => {
    const field = "communication.style", candidate = "http-form-style", value = "Synthetic exact form value Ω\n  second line";
    await apply(event("answer", candidate, { target: "profile", field, value, provenance: "form" }));
    assert.equal(snapshot.snapshot.profile.fields[field].status, "unknown");
    assert.equal(snapshot.snapshot.profile.candidates[candidate].review, "pending");
    await apply(event("review", "http-review-style", { target: "profile", candidate, decision: "confirmed" }));
    assert.equal(snapshot.snapshot.profile.fields[field].value, value);
    assert.equal(snapshot.snapshot.profile.fields[field].provenance, "form");
    assert.equal(resolution("preference.style").layer, "user");
    assert.equal(resolution("preference.style").value, value);
  });

  await check("native privacy layers deny operational use when disabled and recover only after explicit reenable", async () => {
    const request = { op: "store", category: "communication", field: "communication.style", provenance: "user_stated" };
    assert.equal((await command("policy_decision", { request })).allowed, true);
    await apply({ target: "layers", op: "disable", key: "onboarding.privacy" });
    const blocked = await command("policy_decision", { request });
    assert.equal(blocked.allowed, false); assert.equal(blocked.resolution.status, "disabled");
    assert.equal((await command("model_request", { provider: "offline/onboarding-native-provider" }, 503)).error.code, "unavailable");
    assert.equal(snapshot.snapshot.profile.privacy.rules.length > 0, true, "retained raw policy is not authority");
    await apply({ target: "layers", op: "reenable", key: "onboarding.privacy" });
    assert.equal((await command("policy_decision", { request })).allowed, true);
    assert.equal(resolution("onboarding.privacy").status, "effective");
  });

  await check("native model preparation remains unauthorized and a saved reply is checked against original CAS", async () => {
    const prepared = await command("model_request", { provider: "offline/onboarding-native-provider" });
    assert.equal(prepared.calls_authorized, false); assert.equal(prepared.provider, "offline/onboarding-native-provider");
    assert.ok(prepared.request_token); assert.equal(String(prepared.snapshot_revision), snapshot.revision);
    assert.ok(prepared.method_profile); assert.ok(prepared.method_ref.version_id); assert.equal(prepared.graph_run_id, snapshot.snapshot.graph_run_id);
    const before = structuredClone(snapshot), reply = { provider: prepared.provider, request_token: prepared.request_token,
      section: prepared.section, summary: "Synthetic saved offline summary; please confirm.", questions: [], candidates: [] };
    await apply({ target: "model_reply", reply, time: "2000-01-01T00:00:00Z" });
    assert.ok(snapshot.snapshot.profile.method_executions.length > 0);
    const savedExecution = snapshot.snapshot.profile.method_executions.at(-1);
    assert.equal(savedExecution.request_token, prepared.request_token);
    assert.equal(savedExecution.measurement_status, "unavailable");
    const after = structuredClone(snapshot);
    assert.equal((await command("apply", { expected_revision: before.revision, action: { target: "model_reply", reply } }, 409)).error.code, "conflict");
    assert.deepEqual(state(await command("read")), after);
    await request({ operation: "complete_model", user_id: user }, 400);
  });

  await check("expert pack update uses native forward validation and survives process restart without clearing conversations", async () => {
    const pack = structuredClone(snapshot.snapshot.pack), scenario = structuredClone(snapshot.snapshot.scenario_definition);
    pack.revision += 1; scenario.prompt = "Synthetic expert-updated interview prompt; only permitted questions.";
    const before = structuredClone(snapshot);
    snapshot = state(await command("update_pack", { expected_revision: before.revision, pack, scenario }));
    assert.equal(snapshot.revision, (BigInt(before.revision) + 1n).toString());
    assert.equal(snapshot.snapshot.scenario_definition.prompt, scenario.prompt);
    assert.equal((await command("model_request", { provider: "offline/onboarding-native-provider" })).prompt, scenario.prompt);
    assert.equal(snapshot.snapshot.profile.fields["communication.style"].value, before.snapshot.profile.fields["communication.style"].value);
    const saved = structuredClone(snapshot);
    assert.equal((await command("update_pack", { expected_revision: saved.revision, pack: before.snapshot.pack, scenario }, 409)).error.code, "conflict");
    assert.deepEqual(state(await command("read")), saved);
    await stopServer(); startServer(); await ready();
    assert.deepEqual(state(await command("read")), saved);
    const conversations = await request(undefined, 200, { endpoint: "/api/conversations", method: "GET" });
    const list = Array.isArray(conversations) ? conversations : conversations.conversations;
    assert.ok(list.some(item => item.id === originalConversation.id));
  });

  await check("exact outer revisions above JavaScript safe integer remain usable through restart and int64 maximum never overflows", async () => {
    await stopServer(); seedOuterRevision("9007199254740993"); startServer(); await ready();
    snapshot = state(await command("read")); assert.equal(snapshot.revision, "9007199254740993");
    await apply({ target: "layers", op: "override", key: "preference.style", value: "Synthetic high-revision CAS value" });
    assert.equal(snapshot.revision, "9007199254740994");
    const before = structuredClone(snapshot);
    assert.equal((await command("apply", { expected_revision: "9007199254740992", action: { target: "layers", op: "disable", key: "preference.style" } }, 409)).error.code, "conflict");
    assert.deepEqual(state(await command("read")), before);
    await stopServer(); seedOuterRevision("9223372036854775807"); startServer(); await ready();
    snapshot = state(await command("read")); assert.equal(snapshot.revision, "9223372036854775807");
    assert.equal((await command("apply", { expected_revision: snapshot.revision, action: {} }, 409)).error.code, "conflict");
    assert.deepEqual(state(await command("read")), snapshot);
  });
  completion.complete();
  console.log("[onboarding-native] 8/8 groups passed; actual HTTP/native CAS/forms/layers/privacy/pack/restart; 0 provider completions");
} catch (error) {
  failure = error instanceof Error ? error.stack : String(error);
  console.error(serverLog);
  throw error;
} finally {
  await stopServer();
  if (evidenceDir) {
    writeFileSync(path.join(evidenceDir, "server.log"), serverLog, { flag: "wx" });
    writeFileSync(path.join(evidenceDir, "results.json"), JSON.stringify({ status: failure ? "failed" : "passed", groups, failure,
      server_binary: serverBin, server_sha256: createHash("sha256").update(readFileSync(serverBin)).digest("hex"),
      provider_completions: 0, runtime_foundation_installed: runtimeFoundationInstalled,
      runtime_foundation_inspection: snapshot?.snapshot.runtime_profile,
      commands, final_revision: snapshot?.revision }, null, 2) + "\n", { flag: "wx" });
  }
  rmSync(dataDir, { recursive: true, force: true });
}
