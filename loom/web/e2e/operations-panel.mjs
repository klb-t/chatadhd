#!/usr/bin/env node
// Real React/adapters with synthetic local responses. Optional --native checks
// capability projections against the real public GraphPacketStore boundary.
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { webcrypto } from "node:crypto";
import { createServer } from "node:http";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import { chromium } from "playwright";
import ts from "typescript";

const root = fileURLToPath(new URL("../", import.meta.url));
const loomRoot = path.resolve(root, "..");
if (!globalThis.crypto?.subtle) globalThis.crypto = webcrypto;
function transpile(relative) {
  return ts.transpileModule(readFileSync(new URL(relative, import.meta.url), "utf8"), {
    compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022 },
  }).outputText;
}
const url = text => `data:text/javascript;base64,${Buffer.from(text).toString("base64")}`;
const runtimeUrl = url(transpile("../src/profiles/runtime.ts"));
const graphUrl = url(transpile("../src/profiles/graph.ts").replaceAll('from "./runtime"', `from "${runtimeUrl}"`));
const capabilityGraph = await import(url(transpile("../src/profiles/capability-graph.ts").replaceAll('from "./graph"', `from "${graphUrl}"`)));
const operationsModule = await import(url(transpile("../src/api/operations.ts")));
const groups = [];
async function check(name, fn) { await fn(); groups.push(name); console.log(`[operations-panel] PASS ${name}`); }

const profile = JSON.parse(readFileSync(path.join(root, "src/profiles/data/loom-default.json"), "utf8"));
const requests = [];
const transport = operationsModule.createOperationsApi(async (method, endpoint, body) => {
  requests.push({ method, endpoint, body });
  if (endpoint.startsWith("/api/artifacts/")) return { artifact: { id: "a/🧠" }, content: "synthetic" };
  if (endpoint === "/api/media/status") return { asr: { available: [], configured: false } };
  return [];
});
await check("transport filters, encoded identity, explicit content and unavailable upload", async () => {
  await transport.listArtifacts({ kind: "code & text", run_id: "run/Żółć", limit: 120000 });
  await transport.getArtifact("a/🧠"); await transport.getArtifact("a/🧠", true);
  const query = new URL(requests[0].endpoint, "http://localhost").searchParams;
  assert.equal(query.get("kind"), "code & text"); assert.equal(query.get("run_id"), "run/Żółć"); assert.equal(query.get("limit"), "120000");
  assert.equal(requests[1].endpoint, "/api/artifacts/a%2F%F0%9F%A7%A0");
  assert.equal(requests[2].endpoint, "/api/artifacts/a%2F%F0%9F%A7%A0?content=1");
  assert.equal(transport.transcribe, undefined);
  const statuses = operationsModule.operationEvidence(transport, await transport.mediaStatus());
  assert.equal(statuses.find(entry => entry.operation === "voice.transcribe").status, "unavailable");
  assert.equal(statuses.find(entry => entry.operation === "workflow.task.execute").status, "unavailable");
  const variants = operationsModule.operationEvidence(transport, null, ["future.operation"], [
    { operation: "future.operation", capability: "future.local", status: "equivalent", detail: "Synthetic local mapping", evidence: ["synthetic:variant/local"] },
    { operation: "future.operation", capability: "future.remote", status: "limited", detail: "Synthetic partial mapping", evidence: ["synthetic:variant/remote"] },
  ]).filter(entry => entry.operation === "future.operation");
  assert.equal(variants.length, 2); assert.deepEqual(variants.map(entry => entry.status), ["equivalent", "limited"]);
});

const evidence = operationsModule.operationEvidence(transport, { asr: { available: [], configured: false } }, ["fictional.operation"]);
let projection;
await check("capabilities individually queryable; source/profile serializer preserved", async () => {
  const raw = JSON.stringify(profile, null, 2) + " \r\n";
  projection = await capabilityGraph.makeCapabilityGraphAcceptance(profile, evidence,
    { text: raw, sourceRef: "synthetic:operations/profile.json", actor: "synthetic-operator" });
  const records = projection.packet.entities;
  const rootRecord = records.find(row => row.kind === "application_profile_capabilities");
  assert.equal(records.filter(row => row.kind === "application_profile").length, 0);
  assert.equal(records.filter(row => row.kind === "application_capability").length, evidence.length);
  assert.ok(records.filter(row => row.kind === "application_capability").every(row => row.parent === rootRecord.id && row.attrs.profile_id === profile.id));
  assert.equal(projection.packet.claims.length, 0);
  assert.equal(JSON.parse(projection.packet.sources[0].observation.text).profile_source, raw);
  assert.ok(records.every(row => row.attrs.original_service_parity === "not_asserted"));
  const validator = spawnSync(process.env.PYTHON || "python3", ["-c", "import json,sys; sys.path.insert(0,sys.argv[1]); from agentic_graph_v1.packet import validate_packet; validate_packet(json.load(sys.stdin)); print('valid')", path.join(loomRoot, "tools/structure")],
    { input: JSON.stringify(projection.packet), encoding: "utf8", timeout: 15000 });
  assert.ifError(validator.error); assert.equal(validator.status, 0, validator.stderr); assert.equal(validator.stdout.trim(), "valid");
});
await check("duplicate evidence rejected before persistence", async () => {
  await assert.rejects(capabilityGraph.makeCapabilityGraphAcceptance(profile, [evidence[0], evidence[0]],
    { text: JSON.stringify(profile), sourceRef: "synthetic:duplicate", actor: "synthetic" }), /Duplicate capability/);
});

const calls = [], uploads = [], stored = [];
const bundle = await build({
  stdin: { contents: `
    import React from "react";
    import { createRoot } from "react-dom/client";
    import OperationsPanel from "./src/components/OperationsPanel";
    import { createOperationsApi } from "./src/api/operations";
    import profile from "./src/profiles/data/loom-default.json";
    async function request(method, path, body) {
      const response = await fetch(path, {method, headers: body === undefined ? {} : {"Content-Type":"application/json"}, ...(body === undefined ? {} : {body:JSON.stringify(body)})});
      const result = await response.json(); if (!response.ok || result.error) throw new Error(result.error?.message ?? "HTTP failed"); return result;
    }
    async function upload(path, body) { const response = await fetch(path,{method:"POST",body}); const result = await response.json(); if(!response.ok || result.error) throw new Error(result.error?.message ?? "Upload failed"); return result; }
    const operations = createOperationsApi(request, upload);
    createRoot(document.getElementById("root")).render(<OperationsPanel operations={operations} profiles={[profile]}
      graphPacketStore={body => request("POST","/api/graph/packets/store",body)} onTranscript={text => {window.__usedTranscript = text;}} />);
  `, resolveDir: root, loader: "tsx" }, bundle: true, write: false, format: "iife", platform: "browser", outfile: "/tmp/loom-operations-test.js", jsx: "automatic",
});
const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
const css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";
const artifact = { id: "a_synthetic", kind: "document", title: "Synthetic artifact", mime: "text/html", task_id: "t_fixture", blob_hash: "a".repeat(64), created: "2026-01-01T00:00:00Z" };
const nativeContent = '<script>window.__artifactRan=true</script><img src="https://example.invalid/artifact.png"> Żółć 🧠';
const fixtureServer = createServer(async (req, res) => {
  const parts = []; for await (const chunk of req) parts.push(chunk);
  const raw = Buffer.concat(parts).toString(); calls.push({ method: req.method, path: req.url, raw });
  if (req.url === "/component.js") { res.setHeader("Content-Type", "text/javascript"); res.end(js); return; }
  if (req.url === "/component.css") { res.setHeader("Content-Type", "text/css"); res.end(css); return; }
  res.setHeader("Content-Type", "application/json");
  if (req.url === "/api/media/status") { res.end(JSON.stringify({ asr: { available: ["synthetic-asr"], configured: true } })); return; }
  if (req.url?.startsWith("/api/artifacts?")) { res.end(JSON.stringify([artifact])); return; }
  if (req.url === "/api/artifacts/a_synthetic?content=1") { res.end(JSON.stringify({ artifact, content: nativeContent })); return; }
  if (req.url === "/api/media/transcribe") {
    uploads.push({ raw, contentType: req.headers["content-type"] });
    if (raw.includes('"provider":"failure-fixture"')) { res.statusCode = 503; res.end(JSON.stringify({ error: { code: "unavailable", message: "Synthetic provider unavailable" } })); }
    else res.end(JSON.stringify({ text: "Synthetic spoken Żółć 🧠", language: "pl", confidence: 0.75 }));
    return;
  }
  if (req.url === "/api/graph/packets/store") {
    const body = JSON.parse(raw);
    if (body.operation === "accept") stored.push(body);
    const result = { receipt: { id: "gpr_fixture_only", run_id: "kr_fixture_only", packet: stored.at(-1)?.packet }, row_drift: { matches: true, rows: [], current_row_snapshots: {} }, replayed: body.operation === "read" };
    res.end(JSON.stringify(result)); return;
  }
  res.setHeader("Content-Type", "text/html");
  res.end('<!doctype html><html><head><link rel="stylesheet" href="/component.css"></head><body><div id="root"></div><script src="/component.js"></script></body></html>');
});
await new Promise(resolve => fixtureServer.listen(0, "127.0.0.1", resolve));
let browser;
try {
  browser = await chromium.launch({ headless: true, args: ["--no-sandbox"] });
  const page = await browser.newPage();
  const pageErrors = [], remote = [];
  page.on("pageerror", error => pageErrors.push(error.message));
  page.on("request", request => { if (!request.url().startsWith(`http://127.0.0.1:${fixtureServer.address().port}`)) remote.push(request.url()); });
  await page.goto(`http://127.0.0.1:${fixtureServer.address().port}/`);
  await page.locator('[data-testid="asr-status"]').waitFor();
  await check("artifact metadata listing never reads content; explicit read stays inert", async () => {
    assert.equal(calls.filter(call => call.path?.includes("/api/artifacts")).length, 0);
    await page.getByRole("button", { name: "List artifacts", exact: true }).click();
    await page.locator('[data-testid="artifact-row"]').waitFor();
    assert.equal(calls.filter(call => call.path?.includes("content=1")).length, 0);
    await page.getByRole("button", { name: "Read content", exact: true }).click();
    await page.locator('[data-testid="artifact-content"]').waitFor();
    assert.equal(await page.locator('[data-testid="artifact-content"] > pre').innerText(), nativeContent);
    assert.equal(await page.locator("img,iframe,object,embed").count(), 0);
    assert.equal(await page.evaluate(() => window.__artifactRan), undefined);
  });
  await check("audio upload is explicit; chosen provider/language and exact file survive", async () => {
    await page.getByLabel("Transcription audio file", { exact: true }).setInputFiles({ name: "synthetic.wav", mimeType: "audio/wav", buffer: Buffer.from("SYNTHETIC_AUDIO_BYTES") });
    assert.equal(uploads.length, 0);
    await page.getByLabel("Transcription provider", { exact: true }).fill("synthetic-asr");
    await page.getByLabel("Transcription language", { exact: true }).fill("pl");
    await page.getByRole("button", { name: "Transcribe file", exact: true }).click();
    await page.locator('[data-testid="transcription-result"]').waitFor();
    assert.equal(uploads.length, 1); assert.match(uploads[0].contentType, /multipart\/form-data; boundary=/);
    assert.match(uploads[0].raw, /SYNTHETIC_AUDIO_BYTES/); assert.match(uploads[0].raw, /filename="synthetic.wav"/);
    assert.match(uploads[0].raw, /"provider":"synthetic-asr","language":"pl"/);
    assert.equal(await page.evaluate(() => window.__usedTranscript), undefined);
    await page.getByRole("button", { name: "Use transcript", exact: true }).click();
    assert.equal(await page.evaluate(() => window.__usedTranscript), "Synthetic spoken Żółć 🧠");
  });
  await check("capability status and additive persistence surface native read receipt", async () => {
    assert.equal(await page.locator('[data-operation="artifact.read"]').getAttribute("data-status"), "native");
    assert.equal(await page.locator('[data-operation="voice.transcribe"]').getAttribute("data-status"), "native");
    assert.equal(await page.locator('[data-operation="browser.execute"]').getAttribute("data-status"), "unavailable");
    assert.equal(await page.locator('[data-operation="workflow.task.execute"]').getAttribute("data-status"), "unavailable");
    await page.getByRole("button", { name: "Save capability snapshot to graph", exact: true }).click();
    await page.locator('[data-testid="capability-graph-receipt"]').waitFor();
    assert.equal(stored.length, 1); assert.equal(stored[0].operation, "accept");
    assert.equal(stored[0].packet.claims.length, 0);
    assert.ok(stored[0].packet.entities.some(entity => entity.kind === "application_capability" && entity.attrs.operation === "voice.transcribe"));
    assert.ok(calls.some(call => call.path === "/api/graph/packets/store" && JSON.parse(call.raw).operation === "read"));
  });
  await check("provider failures are visible, without automatic retry", async () => {
    await page.getByLabel("Transcription provider", { exact: true }).fill("failure-fixture");
    await page.getByRole("button", { name: "Transcribe file", exact: true }).click();
    await page.getByRole("alert").filter({ hasText: "Synthetic provider unavailable" }).waitFor();
    assert.equal(uploads.length, 2); assert.equal(await page.locator('[data-testid="transcription-result"]').count(), 0);
    assert.deepEqual(pageErrors, []); assert.deepEqual(remote, []);
  });
} finally { await browser?.close(); await new Promise(resolve => fixtureServer.close(resolve)); }

if (process.argv.includes("--native")) {
  const serverBin = process.env.LOOM_SERVER_BIN || path.join(loomRoot, "build/dev/server/loom-server");
  assert.ok(existsSync(serverBin), `Build native server first: ${serverBin}`);
  const nativeDir = mkdtempSync(path.join(tmpdir(), "loom-capability-native-"));
  const probe = createServer(); await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
  const port = probe.address().port; await new Promise(resolve => probe.close(resolve));
  const base = `http://127.0.0.1:${port}`;
  const server = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", nativeDir], { stdio: ["ignore", "pipe", "pipe"] });
  let log = ""; for (const stream of [server.stdout, server.stderr]) stream.on("data", chunk => { log += chunk.toString(); });
  try {
    const deadline = Date.now() + 15000;
    while (true) { try { if ((await fetch(`${base}/api/healthz`)).ok) break; } catch { /* Starting. */ } if (Date.now() > deadline) throw new Error(log); await new Promise(resolve => setTimeout(resolve, 100)); }
    async function native(method, endpoint, body) {
      const response = await fetch(base + endpoint, { method, headers: { "Content-Type": "application/json" }, ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
      const result = await response.json(); assert.ok(response.ok, JSON.stringify(result)); return result;
    }
    await check("real native projection acceptance, individual query and idempotent replay", async () => {
      const result = await native("POST", "/api/graph/packets/store", projection);
      assert.equal(result.row_drift.matches, true);
      const query = await native("POST", "/api/knowledge/query", { run: result.receipt.run_id, what: "entities", kind: "application_capability" });
      assert.equal(query.items.length, evidence.length);
      assert.ok(query.items.every(row => row.attrs.profile_id === profile.id && row.attrs.availability));
      const retry = await native("POST", "/api/graph/packets/store", projection);
      assert.equal(retry.receipt.id, result.receipt.id); assert.equal(retry.replayed, true);
      const read = await native("POST", "/api/graph/packets/store", { operation: "read", receipt_id: result.receipt.id });
      assert.equal(read.row_drift.matches, true);
    });
  } finally {
    if (server.exitCode === null) { const stopped = new Promise(resolve => server.once("close", resolve)); server.kill("SIGTERM"); await stopped; }
    rmSync(nativeDir, { recursive: true, force: true });
  }
}
console.log(`[operations-panel] ${groups.length}/${groups.length} groups passed; synthetic offline fixtures, zero paid calls`);
