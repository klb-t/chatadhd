#!/usr/bin/env node
// Production React -> HTTP transport -> real static-kernel usage policy/SQLite.
// Authored local ledger fixtures only; this test never dispatches a model/import.
// Run after native build/rebase and npm run build: node e2e/usage-policy-native.mjs
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const loomRoot = path.resolve(webRoot, "..");
const serverBin = process.env.LOOM_SERVER_BIN || path.join(loomRoot, "build/dev/server/loom-server");
const dist = path.join(webRoot, "dist");
assert.ok(existsSync(serverBin), `Build the current native server first: ${serverBin}`);
assert.ok(existsSync(path.join(dist, "index.html")), "Build the current web bundle first");
const evidenceDir = process.env.USAGE_POLICY_EVIDENCE_DIR;
if (evidenceDir) {
  mkdirSync(evidenceDir, { recursive: true });
  for (const name of ["results.json", "server.log", "failure.png"]) assert.ok(!existsSync(path.join(evidenceDir, name)), "Use a fresh evidence directory; failed attempts must remain intact.");
}
const dataDir = mkdtempSync(path.join(tmpdir(), "loom-usage-policy-ui-"));
const configPath = path.join(dataDir, "config.json"), ledgerPath = path.join(dataDir, "usage-policy.sqlite");
writeFileSync(configPath, JSON.stringify({ semantic_analysis: false, base_url: "http://127.0.0.1:1", default_model: "offline/ledger-fixture" }));
writeFileSync(path.join(dataDir, "models.json"), "[]");
const probe = createServer();
await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
const port = probe.address().port;
await new Promise(resolve => probe.close(resolve));
const base = `http://127.0.0.1:${port}`;
const browserMutations = [], externalRequests = [], pageErrors = [], groups = [];
let server, browser, page, failure = null, serverLog = "", persistedInspection = null;
function startServer() {
  serverLog += "\n[start native usage-policy server]\n";
  server = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", dataDir, "--static-dir", dist], {
    cwd: loomRoot, env: process.env, stdio: ["ignore", "pipe", "pipe"],
  });
  for (const output of [server.stdout, server.stderr]) output.on("data", chunk => { serverLog += chunk.toString(); });
  server.on("error", error => { serverLog += String(error); });
}
async function stopServer() {
  if (!server || server.exitCode !== null) return;
  const current = server;
  current.kill("SIGTERM");
  await new Promise(resolve => { if (current.exitCode !== null) resolve(); else current.once("exit", resolve); });
}
async function ready() {
  const deadline = Date.now() + 20000;
  while (Date.now() < deadline) {
    try { if ((await fetch(`${base}/api/healthz`)).ok) return; } catch { /* startup */ }
    if (server.exitCode !== null) throw Error(`Native server exited: ${serverLog}`);
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  throw Error(`Native server startup timeout: ${serverLog}`);
}
async function native(method, endpoint, body) {
  const response = await fetch(`${base}${endpoint}`, { method, headers: { "Content-Type": "application/json" },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  const result = await response.json();
  assert.ok(response.ok && !(result.error && typeof result.error === "object"), `${endpoint}: ${JSON.stringify(result)}`);
  return result;
}
const policy = command => native("POST", "/api/usage-policy", command);
const receipt = async () => JSON.parse(await page.getByTestId("usage-receipt-json").textContent());
const commandLog = () => browserMutations.filter(item => item.path === "/api/usage-policy").map(item => item.body);
async function check(name, run) { await run(); groups.push(name); console.log(`[usage-policy-native] PASS ${name}`); }
async function openPage() {
  page = await browser.newPage({ viewport: { width: 1500, height: 1050 } });
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.route("**/*", async route => {
    const request = route.request(), url = new URL(request.url());
    if (url.origin !== base) { externalRequests.push(request.url()); await route.abort(); return; }
    if (request.method() !== "GET" && url.pathname.startsWith("/api/")) browserMutations.push({ method: request.method(), path: url.pathname, body: request.postDataJSON() });
    await route.continue();
  });
  await page.goto(base);
  await page.getByTestId("nav-settings").click();
  await page.getByTestId("usage-effective").waitFor();
}
async function requestInUi(estimate) {
  await page.getByTestId("usage-estimate-json").fill(JSON.stringify(estimate));
  await page.getByTestId("usage-request").click();
  await page.waitForFunction(id => {
    try { return JSON.parse(document.querySelector('[data-testid="usage-receipt-json"]')?.textContent || "null")?.operation_id === id; }
    catch { return false; }
  }, estimate.operation_id);
  return receipt();
}
async function waitReceiptStatus(status) {
  await page.waitForFunction(expected => {
    try { return JSON.parse(document.querySelector('[data-testid="usage-receipt-json"]')?.textContent || "null")?.status === expected; }
    catch { return false; }
  }, status);
  return receipt();
}
const cohort = "owner/custom Ω/units-v2", resource = "custom/quanta-v2", largeResource = "arbitrary-large-units", unknownResource = "unknown-owner-units";
let originalSettings, override, approvedReceipt, declinedReceipt, staleReceipt, refreshedReceipt;
try {
  startServer(); await ready();
  originalSettings = await policy({ action: "settings" });
  assert.equal(originalSettings.capabilities.resource_names, "open", "this suite requires the real policy capability; unavailable is not a pass");
  const cachedShell = "/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell";
  const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || (existsSync(cachedShell) ? cachedShell : undefined);
  browser = await chromium.launch({ headless: true, args: ["--no-sandbox", "--disable-dev-shm-usage"], ...(executablePath ? { executablePath } : {}) });
  await openPage();

  await check("embedded native settings and advisory override preview preserve config bytes and never open ledger", async () => {
    assert.equal(originalSettings.preset_source, "embedded_data");
    const presetBytes = readFileSync(path.resolve(loomRoot, "..", originalSettings.preset_document.path));
    assert.deepEqual(originalSettings.preset, JSON.parse(presetBytes));
    assert.equal(originalSettings.preset_document.source_sha256, createHash("sha256").update(presetBytes).digest("hex"));
    assert.equal(originalSettings.hashes.algorithm, "sha256"); assert.equal(originalSettings.hashes.representation, "loom.canonical_json");
    assert.match(originalSettings.hashes.effective, /^[a-f0-9]{64}$/); assert.equal(originalSettings.hashes.stored_override, null);
    const beforeBytes = readFileSync(configPath, "utf8"), beforeConfig = await native("GET", "/api/config");
    assert.equal(existsSync(ledgerPath), false);
    override = { ...originalSettings.effective, baseline_window: null,
      initial_baselines: { [cohort]: { [resource]: 2, [largeResource]: 1e290, [unknownResource]: null } },
      extensions: { authored_public_fixture: true, arbitrary_owner_field: { unit_version: 2 } } };
    await page.getByTestId("usage-policy-json").fill(JSON.stringify(override));
    await page.getByTestId("usage-policy-preview").click();
    await page.getByTestId("usage-policy-preview-json").waitFor();
    const response = JSON.parse(await page.getByTestId("usage-policy-preview-json").textContent());
    assert.deepEqual(response.hashes, originalSettings.hashes); assert.deepEqual(response.effective, originalSettings.effective);
    assert.deepEqual(response.preview.override, override); assert.deepEqual(response.preview.effective, override);
    assert.equal(response.preview.persisted, false); assert.equal(response.preview.effective_changed, true);
    assert.match(response.preview.hashes.override, /^[a-f0-9]{64}$/); assert.match(response.preview.hashes.effective, /^[a-f0-9]{64}$/);
    assert.equal(readFileSync(configPath, "utf8"), beforeBytes); assert.deepEqual(await native("GET", "/api/config"), beforeConfig);
    assert.equal(existsSync(ledgerPath), false);
    assert.deepEqual(commandLog().at(-1), { action: "preview_settings", override });
    assert.equal(commandLog().some(item => ["request", "confirm", "preview", "complete", "cancel", "inspect"].includes(item.action)), false);
    await page.getByTestId("usage-policy-json").fill('{"growth_factor":1}');
    await page.getByTestId("usage-policy-preview").click();
    await page.getByTestId("usage-policy-error").waitFor();
    assert.match(await page.getByTestId("usage-policy-error").innerText(), /greater than one/);
    assert.equal(await page.getByTestId("usage-policy-json").inputValue(), '{"growth_factor":1}');
    assert.equal(readFileSync(configPath, "utf8"), beforeBytes); assert.equal(existsSync(ledgerPath), false);
    await page.getByTestId("usage-policy-json").fill(JSON.stringify(override));
    await page.getByTestId("usage-policy-save").click();
    await page.waitForFunction(() => document.querySelector('[data-testid="usage-policy-notice"]')?.textContent.includes("Policy saved"));
    const configured = await policy({ action: "settings" });
    assert.deepEqual(configured.stored_override, override); assert.equal(configured.source, "configured");
    assert.equal(configured.hashes.stored_override, response.preview.hashes.override);
    assert.equal(configured.hashes.effective, response.preview.hashes.effective);
    assert.deepEqual(JSON.parse(readFileSync(configPath)).loom_usage_policy, override);
    assert.equal(existsSync(ledgerPath), false, "saving settings alone never opens the ledger");
  });

  const estimate = id => ({ operation_id: id, baseline_key: cohort,
    resources: { [resource]: 20, [largeResource]: 1e300, [unknownResource]: null },
    extensions: { authored_public_fixture: true } });
  await check("native ×10 receipt approval and rejection preserve arbitrary finite resource dimensions", async () => {
    const pending = await requestInUi(estimate("native-owner-approve"));
    assert.equal(pending.status, "requires_confirmation"); assert.equal(pending.authorized, false);
    assert.equal(pending.resources[resource].ratio, 10); assert.equal(pending.resources[resource].requires_confirmation, true);
    assert.equal(pending.resources[largeResource].estimate, 1e300); assert.equal(pending.resources[largeResource].requires_confirmation, true);
    assert.equal(pending.resources[unknownResource].baseline, null); assert.equal(pending.resources[unknownResource].projected, null);
    assert.equal(await page.getByTestId("usage-approve").isDisabled(), true);
    assert.equal(commandLog().filter(item => item.action === "confirm").length, 0);
    await page.getByTestId("usage-confirmation-ref").fill("public-owner-approval-v1");
    await page.getByTestId("usage-approve").click(); approvedReceipt = await waitReceiptStatus("allowed");
    assert.equal(approvedReceipt.authorized, true);
    assert.deepEqual(commandLog().at(-1), { action: "confirm", operation_id: pending.operation_id, receipt_id: pending.receipt_id, approved: true, confirmation_ref: "public-owner-approval-v1" });
    const declinePending = await requestInUi(estimate("native-owner-decline"));
    assert.equal(declinePending.status, "requires_confirmation");
    await page.getByTestId("usage-confirmation-ref").fill("public-owner-rejection-v1"); await page.getByTestId("usage-reject").click();
    declinedReceipt = await waitReceiptStatus("denied"); assert.equal(declinedReceipt.authorized, false);
    assert.deepEqual(commandLog().at(-1), { action: "confirm", operation_id: declinePending.operation_id, receipt_id: declinePending.receipt_id, approved: false, confirmation_ref: "public-owner-rejection-v1" });
  });

  await check("native reservation change refuses stale approval and requires explicit original-estimate refresh plus a fresh owner reference", async () => {
    const originalEstimate = estimate("native-owner-stale"); staleReceipt = await requestInUi(originalEstimate);
    assert.equal(staleReceipt.status, "requires_confirmation"); assert.equal(staleReceipt.resources[resource].reserved, 20);
    await policy({ action: "cancel", operation_id: approvedReceipt.operation_id, reason: "Public fixture cancels reserved operation without dispatch" });
    await page.getByTestId("usage-estimate-json").fill(JSON.stringify(estimate("changed-draft-not-requested")));
    await page.getByTestId("usage-confirmation-ref").fill("public-stale-approval-attempt");
    await page.getByTestId("usage-approve").click(); await page.getByTestId("usage-receipt-stale").waitFor();
    assert.match(await page.getByTestId("usage-policy-error").innerText(), /projection changed/);
    assert.equal(await page.getByTestId("usage-approve").isDisabled(), true); assert.equal(await page.getByTestId("usage-reject").isDisabled(), true);
    const requestsBeforeRefresh = commandLog().filter(item => item.action === "request").length;
    assert.equal(requestsBeforeRefresh, 3, "confirmation failure never silently re-requests or re-approves");
    await page.getByTestId("usage-receipt-refresh").click();
    await page.waitForFunction(previous => document.querySelector('[data-testid="usage-receipt-id"]')?.textContent !== previous, staleReceipt.receipt_id);
    refreshedReceipt = await receipt(); assert.equal(refreshedReceipt.resources[resource].reserved, 0);
    assert.equal(refreshedReceipt.status, "requires_confirmation"); assert.notEqual(refreshedReceipt.receipt_id, staleReceipt.receipt_id);
    assert.deepEqual(refreshedReceipt.estimate, originalEstimate); assert.deepEqual(commandLog().at(-1), { action: "request", estimate: originalEstimate });
    assert.equal(await page.getByTestId("usage-confirmation-ref").inputValue(), ""); assert.equal(await page.getByTestId("usage-approve").isDisabled(), true);
    await page.getByTestId("usage-confirmation-ref").fill("public-fresh-approval-v2"); await page.getByTestId("usage-approve").click();
    refreshedReceipt = await waitReceiptStatus("allowed"); assert.equal(refreshedReceipt.authorized, true);
    assert.equal(commandLog().at(-1).receipt_id, refreshedReceipt.receipt_id);
    const unresolved = await policy({ action: "complete", operation_id: refreshedReceipt.operation_id,
      actual: { resources: { [resource]: 20, [largeResource]: 1e300, [unknownResource]: null }, provenance: "instrument_measured" } });
    assert.equal(unresolved.status, "unresolved"); assert.deepEqual(unresolved.remaining, { [unknownResource]: null });
  });

  await check("native restart preserves owner override, decisions, measured baseline and unresolved reservation; UI reload inspects actual durable records", async () => {
    const settingsBeforeRestart = await policy({ action: "settings" });
    const configBytes = readFileSync(configPath, "utf8");
    assert.ok(existsSync(ledgerPath)); await page.close(); await stopServer(); startServer(); await ready();
    const settingsAfterRestart = await policy({ action: "settings" });
    assert.deepEqual(settingsAfterRestart.stored_override, override); assert.deepEqual(settingsAfterRestart.hashes, settingsBeforeRestart.hashes);
    assert.equal(settingsAfterRestart.source, "configured"); assert.equal(readFileSync(configPath, "utf8"), configBytes);
    persistedInspection = await policy({ action: "inspect", baseline_key: cohort });
    assert.equal(persistedInspection.operations.find(item => item.operation_id === approvedReceipt.operation_id).status, "cancelled");
    assert.equal(persistedInspection.operations.find(item => item.operation_id === declinedReceipt.operation_id).status, "denied");
    const retained = persistedInspection.operations.find(item => item.operation_id === refreshedReceipt.operation_id);
    assert.equal(retained.status, "unresolved"); assert.equal(retained.receipt_id, refreshedReceipt.receipt_id);
    assert.equal(retained.confirmation.ref, "public-fresh-approval-v2"); assert.deepEqual(retained.remaining, { [unknownResource]: null });
    assert.equal(persistedInspection.baseline[resource].value, 20); assert.equal(persistedInspection.baseline[resource].source, "measured");
    assert.equal(persistedInspection.reservations[unknownResource], null);
    await openPage();
    assert.deepEqual(JSON.parse(await page.getByTestId("usage-policy-json").inputValue()), settingsAfterRestart.effective);
    await page.getByTestId("usage-inspect-cohort").fill(cohort); await page.getByTestId("usage-inspect").click();
    await page.getByTestId("usage-inspection").waitFor();
    const rows = page.getByTestId("usage-inspected-operation");
    assert.equal(await rows.count(), 3); assert.match(await rows.filter({ hasText: "native-owner-stale" }).innerText(), /unresolved/);
    assert.match(await rows.filter({ hasText: "native-owner-stale" }).innerText(), /unknown-owner-units/);
  });
  assert.deepEqual(externalRequests, []); assert.deepEqual(pageErrors, []);
  assert.equal(browserMutations.some(item => /chat|import|knowledge\/run/.test(item.path)), false, "policy UI must never dispatch estimated work");
  console.log(`[usage-policy-native] ${groups.length}/4 groups passed; real native settings/receipts/SQLite restart; 0 model/import dispatches, 0 external browser requests`);
} catch (error) {
  failure = error instanceof Error ? error.stack : String(error);
  console.error(serverLog);
  if (evidenceDir && page && !page.isClosed()) await page.screenshot({ path: path.join(evidenceDir, "failure.png"), fullPage: true }).catch(() => {});
  throw error;
} finally {
  await browser?.close(); await stopServer();
  if (evidenceDir) {
    writeFileSync(path.join(evidenceDir, "server.log"), serverLog, { flag: "wx" });
    writeFileSync(path.join(evidenceDir, "results.json"), JSON.stringify({ status: failure ? "failed" : "passed", groups, failure,
      external_requests: externalRequests, page_errors: pageErrors, browser_mutations: browserMutations, durable_inspection: persistedInspection }, null, 2) + "\n", { flag: "wx" });
  }
  rmSync(dataDir, { recursive: true, force: true });
}
