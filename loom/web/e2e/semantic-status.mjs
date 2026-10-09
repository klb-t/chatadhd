#!/usr/bin/env node
// Production React + HTTP client + UserProfileHost. Authored native-wire
// responses exercise the host boundary; this does not replace native worker,
// database or R40 tests. No model transport or external service is contacted.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import { chromium } from "playwright";
import { closeHttpFixture, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const root = fileURLToPath(new URL("../", import.meta.url));
const directory = process.env.SEMANTIC_STATUS_EVIDENCE_DIR || mkdtempSync(path.join(tmpdir(), "loom-semantic-status-"));
mkdirSync(directory, { recursive: true });
assert.equal(existsSync(path.join(directory, "receipt-before.json")), false, "Use a fresh evidence directory; retain earlier receipts.");
const sources = ["e2e/semantic-status.mjs", "e2e/harness-lifecycle.mjs", "src/App.tsx", "src/components/SemanticStatus.tsx",
  "src/api/types.ts", "src/api/onboarding-host.ts", "src/api/loom-http.ts", "src/onboarding/native-snapshot.ts",
  "src/onboarding/presentation.mjs", "src/onboarding/presentation-context.tsx", "src/onboarding/generated/ui.json",
  "../data/profiles/user.pack", "../data/onboarding/scenario.pack"];
const hash = value => createHash("sha256").update(value).digest("hex");
const receipt = () => Object.fromEntries(sources.map(file => [file, hash(readFileSync(path.join(root, file)))]));
const before = receipt();
const pack = JSON.parse(readFileSync(new URL("../../data/profiles/user.pack", import.meta.url)));
const scenario = JSON.parse(readFileSync(new URL("../../data/onboarding/scenario.pack", import.meta.url)));
const catalog = JSON.parse(readFileSync(new URL("../src/onboarding/generated/ui.json", import.meta.url)));
const clone = value => structuredClone(value);
const initialWorker = { pending: 0, processed: 4, failed: 0, executing: 0, counts_known: true,
  errors: 0, running: true, paused: false, mode: "regex", rate: "normal" };
let worker = clone(initialWorker), statusFailure = false, actionFailure = false, profileFailure = false, rejectWrite = false;
let profileGate = null, releaseProfile = null, nextPresentation = { available: true, status: "effective", value: clone(catalog) };
const calls = [], records = new Map(), pageErrors = [], outside = [], groups = [];
const completion = suiteCompletionGuard("semantic-status", 12, groups);
const bundle = await build({ stdin: { contents: `
  import React from "react";
  import { createRoot } from "react-dom/client";
  import SemanticStatus from "./src/components/SemanticStatus";
  import { createUserProfileHost } from "./src/api/onboarding-host";
  import { api } from "./src/api";
  const host = createUserProfileHost(api, { storage: null });
  const root = createRoot(document.getElementById("root")); let epoch = 0;
  const mount = () => root.render(<SemanticStatus key={++epoch} profileHost={host} />);
  window.__semantic = { mount, select: id => host.selectUser(id), state: () => host.getState(),
    read: () => host.getAdapter().getSnapshot(), write: () => host.getAdapter().dispatchLayer({
      op: "override", key: "presentation.onboarding", value: {} }) };
  mount();
`, resolveDir: root, loader: "tsx" }, bundle: true, write: false, format: "iife", platform: "browser",
  outfile: "/tmp/loom-semantic-status.js", jsx: "automatic" });
const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
const browserLauncher = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || chromium.executablePath();
writeFileSync(path.join(directory, "receipt-before.json"), JSON.stringify({ sources: before, bundle_sha256: hash(js),
  node: { version: process.version, path: process.execPath, sha256: hash(readFileSync(process.execPath)) },
  browser_launcher: { path: browserLauncher, sha256: hash(readFileSync(browserLauncher)) },
  browser_options: { headless: true, hasTouch: true }, transport: "authored local HTTP responses", model_calls: 0 }, null, 2));

function snapshot(user, revision = "0") {
  return { schema: "loom.onboarding_store/1", user_id: user, revision: Number(revision), pack: clone(pack),
    scenario_definition: clone(scenario), presentation: clone(nextPresentation), effectiveDefaults: [],
    profile: { fields: {}, candidates: {}, history: [], privacy: { rules: [] }, settings: { preference_mode: "ask" },
      session: { status: "active", section: scenario.sections[0].id, sections: Object.fromEntries(
        scenario.sections.map(section => [section.id, { status: "pending" }])) } }, layers: { areas: {}, history: [] } };
}
function json(response, value, status = 200) {
  response.writeHead(status, { "Content-Type": "application/json" }); response.end(JSON.stringify(value));
}
const server = createServer(async (request, response) => {
  if (request.url === "/component.js") { response.writeHead(200, { "Content-Type": "text/javascript" }); response.end(js); return; }
  if (request.url === "/") { response.writeHead(200, { "Content-Type": "text/html" });
    response.end('<!doctype html><html><body><div id="root"></div><script src="/component.js"></script></body></html>'); return; }
  const chunks = []; for await (const chunk of request) chunks.push(chunk);
  const bytes = Buffer.concat(chunks).toString();
  const command = bytes ? JSON.parse(bytes) : null;
  calls.push({ path: request.url, method: request.method, command });
  if (request.url === "/api/semantic/status") {
    json(response, statusFailure ? { error: { code: "fixture_unavailable", message: "authored status unavailable" } } : worker, statusFailure ? 503 : 200); return;
  }
  if (["/api/semantic/pause", "/api/semantic/resume"].includes(request.url)) {
    if (actionFailure) { json(response, { error: { code: "fixture_unavailable", message: "authored pause unavailable" } }, 503); return; }
    worker.paused = request.url.endsWith("/pause"); json(response, worker); return;
  }
  if (request.url === "/api/onboarding") {
    if (profileGate) { profileGate.entered(); await profileGate.promise; }
    if (profileFailure || (rejectWrite && command.operation === "apply")) {
      json(response, { error: { code: "fixture_refusal", message: "authored native profile refusal" } }, profileFailure ? 503 : 409); return;
    }
    const user = command.user_id;
    if (!records.has(user)) records.set(user, { revision: "0", snapshot: snapshot(user) });
    const record = records.get(user);
    if (command.operation === "apply") {
      assert.equal(command.expected_revision, record.revision);
      record.revision = String(BigInt(record.revision) + 1n);
      record.snapshot.revision = Number(record.revision);
    } else if (JSON.stringify(record.snapshot.presentation) !== JSON.stringify(nextPresentation)) {
      // A fixture-controlled edit by another actor advances native CAS, too.
      record.revision = String(BigInt(record.revision) + 1n);
      record.snapshot.revision = Number(record.revision);
    }
    record.snapshot.presentation = clone(nextPresentation);
    json(response, record); return;
  }
  json(response, { error: { code: "unexpected_fixture_route", message: request.url } }, 404);
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
let browser, failure = null;
const nativeCalls = () => calls.filter(call => call.path === "/api/onboarding");
async function check(name, work) { await work(); groups.push(name); console.log(`[semantic-status] PASS ${name}`); }
try {
  browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? {
    executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}) });
  const context = await browser.newContext({ hasTouch: true });
  const page = await context.newPage(); page.setDefaultTimeout(10000);
  const origin = `http://127.0.0.1:${server.address().port}`;
  await page.route("**/*", route => {
    if (new URL(route.request().url()).origin !== origin) { outside.push(route.request().url()); return route.abort(); }
    return route.continue();
  });
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.goto(origin);
  const badge = page.getByTestId("semantic-status"), details = page.getByTestId("semantic-status-details");
  const summary = details.locator("summary"), toggle = page.getByTestId("semantic-toggle-pause");
  async function remount() {
    const response = page.waitForResponse(response => response.url().endsWith("/api/semantic/status"));
    await page.evaluate(() => window.__semantic.mount());
    await response;
    await page.waitForFunction(() => !!document.querySelector('[data-testid="semantic-status"]'));
  }
  async function shownCount(key, value) {
    await page.waitForFunction(({ key, value }) => {
      const pre = document.querySelector('[data-testid="semantic-status-details"] pre');
      return pre && JSON.parse(pre.textContent)[key] === value;
    }, { key, value });
  }

  await check("complete durable counts preserve the idle success state and dispatch no profile read", async () => {
    await shownCount("processed", 4);
    assert.match(await badge.getAttribute("class"), /\bok\b/);
    assert.equal(await toggle.textContent(), catalog.locales.en["session.pause"]);
    assert.equal(nativeCalls().length, 0);
    assert.deepEqual(JSON.parse(await summary.getAttribute("title")), initialWorker);
  });
  await check("failed is visible and non-green; diagnostics work with hover, keyboard and tap", async () => {
    worker = { ...initialWorker, failed: 3 };
    await remount(); await shownCount("failed", 3);
    assert.match(await badge.getAttribute("class"), /\bwarn\b/);
    assert.match(await summary.textContent(), /The operation failed/);
    await summary.hover(); assert.equal(JSON.parse(await summary.getAttribute("title")).failed, 3);
    await summary.focus(); await page.keyboard.press("Enter"); assert.equal(await details.locator("pre").isVisible(), true);
    await summary.tap(); assert.equal(await details.locator("pre").isVisible(), false);
  });
  await check("executing remains neutral when no messages are pending", async () => {
    worker = { ...initialWorker, executing: 2 };
    await remount(); await shownCount("executing", 2);
    assert.doesNotMatch(await badge.getAttribute("class"), /\b(ok|warn)\b/);
    assert.match(await summary.textContent(), /Diagnostic details/);
  });
  await check("older and unreadable hosts show unknown counts instead of fabricated zeros", async () => {
    worker = { ...initialWorker }; delete worker.executing; delete worker.failed; delete worker.counts_known;
    await remount(); await shownCount("executing", null);
    assert.match(await summary.textContent(), /unknown/); assert.doesNotMatch(await badge.getAttribute("class"), /\bok\b/);
    assert.equal(JSON.parse(await details.locator("pre").textContent()).failed, null);
    worker = { ...initialWorker, counts_known: false, executing: null, failed: null };
    await remount(); await shownCount("counts_known", false);
    assert.match(await summary.textContent(), /unknown/); assert.doesNotMatch(await badge.getAttribute("class"), /\bok\b/);
  });
  await check("failed status and pause requests invalidate stale green data and recover on a real read", async () => {
    statusFailure = true; await remount(); await shownCount("error", "authored status unavailable");
    assert.doesNotMatch(await badge.getAttribute("class"), /\bok\b/); assert.equal(await toggle.count(), 0);
    statusFailure = false; worker = { ...initialWorker }; await remount(); await shownCount("processed", 4);
    actionFailure = true; await toggle.click(); await shownCount("error", "authored pause unavailable");
    assert.doesNotMatch(await badge.getAttribute("class"), /\bok\b/); assert.equal(await toggle.count(), 0);
    actionFailure = false; await remount(); await shownCount("processed", 4);
    assert.match(await badge.getAttribute("class"), /\bok\b/);
  });
  await check("keyboard pause and touch resume only unpause and never retry failed messages", async () => {
    worker = { ...initialWorker, failed: 2 };
    await remount(); await shownCount("failed", 2);
    const start = calls.length;
    await toggle.focus(); await page.keyboard.press("Enter"); await shownCount("paused", true);
    assert.equal(await toggle.textContent(), catalog.locales.en["session.resume"]);
    await toggle.tap(); await shownCount("paused", false);
    assert.equal(worker.failed, 2); assert.doesNotMatch(await badge.getAttribute("class"), /\bok\b/);
    assert.deepEqual(calls.slice(start).filter(call => call.method === "POST").map(call => call.path),
      ["/api/semantic/pause", "/api/semantic/resume"]);
  });
  await check("one initial native profile read survives remount and supplies the confirmed Polish catalog", async () => {
    nextPresentation = { available: true, status: "effective", value: clone(catalog) };
    nextPresentation.value.default_locale = "pl";
    let entered; const requested = new Promise(resolve => { entered = resolve; });
    profileGate = { promise: new Promise(resolve => { releaseProfile = resolve; }), entered };
    const beforeReads = nativeCalls().length;
    await page.evaluate(() => window.__semantic.select("synthetic-status-pl"));
    await requested;
    await page.waitForFunction(() => window.__semantic.state().session?.active === 1);
    await badge.getByRole("alert").waitFor(); assert.equal(await toggle.count(), 0);
    await page.evaluate(() => window.__semantic.mount());
    assert.equal(nativeCalls().length, beforeReads + 1);
    profileGate = null; releaseProfile(); releaseProfile = null;
    await toggle.getByText(catalog.locales.pl["session.pause"], { exact: true }).waitFor();
    assert.equal(nativeCalls().length, beforeReads + 1);
    assert.equal((await page.evaluate(() => window.__semantic.state())).session.reloadRequired, false);
  });
  await check("acknowledged native profile edits change the same consumer without a second read", async () => {
    nextPresentation.value.locales.pl["session.pause"] = "Autorska pauza B";
    const start = nativeCalls().length;
    await page.evaluate(() => window.__semantic.write());
    await toggle.getByText("Autorska pauza B", { exact: true }).waitFor();
    assert.equal(nativeCalls().length, start + 1); assert.equal(nativeCalls().at(-1).command.operation, "apply");
  });
  await check("disabled and excluded catalogs suppress the consumer without a bootstrap fallback", async () => {
    for (const status of ["disabled", "excluded"]) {
      nextPresentation = { available: false, status };
      await page.evaluate(() => window.__semantic.read());
      await badge.getByRole("alert").waitFor(); assert.equal(await badge.innerText(), "error.presentation");
      assert.equal(await toggle.count(), 0); assert.equal(await details.count(), 0);
    }
  });
  await check("invalid custom catalogs remain explicit errors and are not replaced by defaults", async () => {
    nextPresentation = { available: true, status: "effective", value: clone(catalog) };
    delete nextPresentation.value.locales.en["session.pause"];
    await page.evaluate(() => window.__semantic.read());
    await badge.getByRole("alert").waitFor(); assert.match(await badge.innerText(), /Invalid onboarding presentation/);
    assert.equal(await toggle.count(), 0); assert.doesNotMatch(await badge.getAttribute("class"), /\bok\b/);
  });
  await check("uncertain native outcomes block reuse and do not auto-replay; explicit read restores the snapshot", async () => {
    nextPresentation = { available: true, status: "effective", value: clone(catalog) };
    await page.evaluate(() => window.__semantic.read()); await toggle.waitFor();
    rejectWrite = true;
    await page.evaluate(() => window.__semantic.write().catch(() => undefined));
    const start = nativeCalls().length;
    await page.evaluate(() => window.__semantic.mount()); await badge.getByRole("alert").waitFor();
    assert.equal(await badge.innerText(), "error.presentation"); assert.equal(nativeCalls().length, start);
    assert.equal((await page.evaluate(() => window.__semantic.state())).session.reloadRequired, true);
    rejectWrite = false; await page.evaluate(() => window.__semantic.read()); await toggle.waitFor();
    assert.equal(nativeCalls().length, start + 1);
  });
  await check("a failed new identity read cannot expose the previous profile or dispatch a retry or egress", async () => {
    profileFailure = true;
    await page.evaluate(() => window.__semantic.select("synthetic-status-unavailable"));
    await page.waitForFunction(() => window.__semantic.state().session?.reloadRequired === true);
    const start = nativeCalls().length;
    await page.evaluate(() => window.__semantic.mount()); await badge.getByRole("alert").waitFor();
    assert.equal(await badge.innerText(), "error.presentation"); assert.equal(await toggle.count(), 0);
    assert.equal(nativeCalls().length, start);
    assert.deepEqual(outside, []); assert.deepEqual(pageErrors, []);
    assert.equal(calls.some(call => /chat|model|retry|update_msg/.test(call.path)), false);
    assert.match(readFileSync(path.join(root, "src/App.tsx"), "utf8"), /<SemanticStatus profileHost=\{userProfileHost\} \/>/);
    assert.deepEqual(receipt(), before, "Measured source inputs must remain unchanged");
  });
} catch (error) { failure = error; }
finally {
  if (releaseProfile) releaseProfile();
  if (browser) await browser.close();
  await closeHttpFixture(server);
  writeFileSync(path.join(directory, "results.json"), JSON.stringify({ passed: groups.length, expected: 12, groups,
    failure: failure ? String(failure.stack || failure) : null, calls, pageErrors, outside, sources: receipt() }, null, 2));
}
if (failure) throw failure;
completion.complete();
console.log(`[semantic-status] ${groups.length}/12 groups passed; real component/host/HTTP, authored native-wire responses, zero model calls`);
