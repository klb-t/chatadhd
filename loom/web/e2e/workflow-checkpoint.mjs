#!/usr/bin/env node
// Checked atomic checkpoints + the actual browser bundle over authored local
// HTTP fixtures. Remote/native exactly-once semantics are not asserted here.
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { existsSync, readFileSync, mkdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import ts from "typescript";
import { chromium } from "playwright";

const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const groups = [];
async function check(name, run) { await run(); groups.push(name); console.log(`[workflow-checkpoint] PASS ${name}`); }
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
const checkpointUrl = moduleUrl("../src/profiles/workflow-checkpoint.ts", [["./runtime", runtimeUrl], ["./workflow-snapshot", snapshotUrl]]);
const runtime = await import(runtimeUrl), checkpoints = await import(checkpointUrl);
const fixture = JSON.parse(readFileSync(path.join(web, "src/profiles/data/loom-default-r2.json"), "utf8"));
fixture.id = "workflow-checkpoint-fixture"; fixture.label = "Workflow checkpoint fixture";
fixture.actions.find(action => action.id === "create").label = "Create from input";
fixture.workflows = [{ id: "create-flow", initial: "ready", states: ["ready", "chat"], transitions: [
  { from: "ready", event: "create", action: "create", to: "chat", payload: { object: { title: { from: "inputs", pointer: "/title" } } },
    save: { conversation_id: { from: "result", pointer: "/id" } } },
] }];
const registry = runtime.createProfileRegistry({ operations: fixture.actions.map(action => action.operation), adapters: fixture.actions.map(action => ({
  operation: action.operation, capability: action.capability, execute: () => ({ id: "authored-reference" }),
})) });
const profile = runtime.registerProfile(registry, fixture), profileIdentity = JSON.stringify([profile.id, profile.profile_revision]);
const initialSession = runtime.createProfileSession(profile, registry);
const keyFor = viewId => `loom.application.workflow.v1:${JSON.stringify([viewId, profile.id, profile.profile_revision])}`;
const pureKey = keyFor("offline");
const records = new Map();
const previousStorage = globalThis.localStorage;
let rejectWrite = false, rejectKey = null;
globalThis.localStorage = { getItem: key => records.get(key) ?? null, setItem: (key, value) => {
  if (rejectWrite || key === rejectKey) throw new DOMException("Authored quota failure", "QuotaExceededError");
  records.set(key, String(value));
}, removeItem: key => records.delete(key) };
try {
  await check("quota failure preserves the complete prior checkpoint and its recovery state", async () => {
    const pending = { viewId: "offline", profile: profileIdentity, session: initialSession,
      recovery: { status: "unknown", pending: { action: "create", workflowId: "create-flow", event: "create" } } };
    checkpoints.writeWorkflowCheckpoint(pureKey, pending, profile, registry);
    const before = records.get(`${pureKey}:checkpoint`);
    const next = await runtime.executeProfileAction(initialSession, registry, "create", {
      workflowId: "create-flow", event: "create", bindings: { inputs: { title: "Synthetic pure call" } },
    });
    rejectWrite = true;
    assert.throws(() => checkpoints.writeWorkflowCheckpoint(pureKey, { ...pending, session: next.session, recovery: { status: "ready" } }, profile, registry), /quota failure/);
    rejectWrite = false;
    assert.equal(records.get(`${pureKey}:checkpoint`), before);
    const read = checkpoints.readWorkflowCheckpoint(pureKey, "offline", profile, registry);
    assert.equal(read.session.sequence, 0); assert.equal(read.recovery.status, "unknown");
    assert.equal(read.session.workflows["create-flow"], "ready");
    checkpoints.writeWorkflowCheckpoint(pureKey, { ...pending, session: next.session, recovery: { status: "ready" } }, profile, registry);
    assert.equal(checkpoints.readWorkflowCheckpoint(pureKey, "offline", profile, registry).session.sequence, 1);
  });
  await check("invalid checkpoints preserve original bytes instead of creating a ready session", () => {
    const bad = JSON.stringify({ schema: "loom.workflow_checkpoint/1", entry: { viewId: "offline", profile: profileIdentity,
      session: initialSession, recovery: { status: "ready", pending: { action: "create" } } } });
    records.set(`${pureKey}:checkpoint`, bad);
    assert.throws(() => checkpoints.readWorkflowCheckpoint(pureKey, "offline", profile, registry), /pending|ready/);
    assert.equal(records.get(`${pureKey}:checkpoint`), bad);
  });
  await check("failed multi-view restore rolls back prior writes including previously absent checkpoint keys", () => {
    for (const firstExisted of [true, false]) {
      const firstKey = keyFor(`batch-first-${firstExisted}`), secondKey = keyFor(`batch-second-${firstExisted}`);
      const firstEntry = { viewId: `batch-first-${firstExisted}`, profile: profileIdentity, session: initialSession, recovery: { status: "ready" } };
      const secondEntry = { viewId: `batch-second-${firstExisted}`, profile: profileIdentity, session: initialSession, recovery: { status: "ready" } };
      if (firstExisted) checkpoints.writeWorkflowCheckpoint(firstKey, firstEntry, profile, registry);
      const originalSecond = "Original second bytes retained even when not parseable.";
      records.set(`${secondKey}:checkpoint`, originalSecond);
      const beforeFirst = records.get(`${firstKey}:checkpoint`);
      rejectKey = `${secondKey}:checkpoint`;
      assert.throws(() => checkpoints.writeWorkflowCheckpoints([
        { key: firstKey, entry: { ...firstEntry, recovery: { status: "unknown", pending: { action: "create", workflowId: "create-flow", event: "create" } } }, profile },
        { key: secondKey, entry: secondEntry, profile },
      ], registry), /quota failure/);
      rejectKey = null;
      assert.equal(records.get(`${firstKey}:checkpoint`), beforeFirst);
      assert.equal(records.get(`${secondKey}:checkpoint`), originalSecond);
    }
  });
} finally { globalThis.localStorage = previousStorage; }

const dist = path.join(web, "dist");
assert.ok(existsSync(path.join(dist, "index.html")), "Run npm run build first.");
const timestamp = "2026-10-04T12:00:00Z";
const conversations = [], creates = [], requests = [], held = [];
let config = {}, browser, page, sidebarGate = null;
const server = createServer(async (req, res) => {
  try {
    const url = new URL(req.url, "http://fixture.local");
    const parts = []; for await (const chunk of req) parts.push(chunk);
    const body = parts.length ? JSON.parse(Buffer.concat(parts).toString()) : undefined;
    requests.push({ method: req.method, path: url.pathname });
    const json = value => res.writeHead(200, { "Content-Type": "application/json" }).end(JSON.stringify(value));
    if (url.pathname === "/api/config") { if (req.method === "PATCH") config = { ...config, ...body }; return json(config); }
    if (url.pathname === "/api/conversations" && req.method === "GET") return json(conversations);
    if (url.pathname === "/api/conversations" && req.method === "POST") {
      const record = { id: `created-${creates.length + 1}`, title: body.title || sidebarGate || "Untitled", created: timestamp, updated: timestamp };
      sidebarGate = null;
      creates.push(record);
      let released = false;
      const release = () => { if (released) return; released = true; conversations.push(record); json(record); };
      if (record.title.startsWith("Held")) held.push({ record, release, requestedTitle: body.title }); else release();
      return;
    }
    if (/^\/api\/conversations\/[^/]+\/messages$/.test(url.pathname)) return json([]);
    if (url.pathname === "/api/models") return json([]);
    if (url.pathname === "/api/semantic/status") return json({ pending: 0, processed: 0, errors: 0, mode: "off", rate: "off", paused: false });
    if (url.pathname.startsWith("/api/")) { res.writeHead(404).end(JSON.stringify({ error: { message: "Unsupported authored fixture endpoint" } })); return; }
    const file = path.resolve(dist, `.${url.pathname === "/" ? "/index.html" : url.pathname}`);
    if (!file.startsWith(`${dist}${path.sep}`) || !existsSync(file)) { res.writeHead(404).end(); return; }
    const mime = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css" }[path.extname(file)] || "application/octet-stream";
    res.writeHead(200, { "Content-Type": mime }).end(readFileSync(file));
  } catch (error) { res.writeHead(500).end(String(error)); }
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const base = `http://127.0.0.1:${server.address().port}`;
const pageErrors = [], external = [];
try {
  browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}) });
  page = await browser.newPage({ viewport: { width: 1800, height: 1100 } });
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.route("**/*", async route => {
    if (new URL(route.request().url()).origin === base) await route.continue();
    else { external.push(route.request().url()); await route.abort(); }
  });
  const view = index => page.getByTestId("application-profile-view").nth(index);
  const recovery = () => page.getByTestId("workflow-recovery-panel");
  async function importProfile(index, document, filename = "checkpoint-profile.json") {
    await view(index).getByLabel("Import application profile", { exact: true }).setInputFiles({ name: filename, mimeType: "application/json", buffer: Buffer.from(JSON.stringify(document)) });
  }
  async function workflow(index) {
    const details = view(index).locator(".profile-details");
    if (!(await details.evaluate(element => element.open))) await details.locator(":scope > summary").click();
    return view(index).getByTestId("profile-workflow");
  }
  async function stored(key) { return page.evaluate(key => localStorage.getItem(key), key); }
  async function startHeld(index, title) {
    const group = await workflow(index);
    await view(index).getByLabel("Workflow inputs JSON", { exact: true }).fill(JSON.stringify({ title }));
    const nativeRequest = page.waitForRequest(request => request.method() === "POST" && new URL(request.url()).pathname === "/api/conversations" && request.postDataJSON().title === title);
    await group.getByRole("button", { name: "Create from input", exact: true }).click();
    await nativeRequest;
    await page.waitForFunction(({ viewId, profileId, revision }) => {
      const key = `loom.application.workflow.v1:${JSON.stringify([viewId, profileId, revision])}:checkpoint`;
      return JSON.parse(localStorage.getItem(key) || "null")?.entry.recovery.status === "unknown";
    }, { viewId: await view(index).getAttribute("data-profile-view-id"), profileId: fixture.id, revision: fixture.profile_revision });
  }
  async function releaseHeld(title) {
    const item = held.find(entry => entry.record.title === title); assert.ok(item, "The native operation must be held before release.");
    const response = page.waitForResponse(response => new URL(response.url()).pathname === "/api/conversations" && response.request().method() === "POST" && response.request().postDataJSON()?.title === item.requestedTitle);
    item.release(); await (await response).finished();
  }
  await page.goto(base);
  await importProfile(0, fixture);
  await page.waitForFunction(id => document.querySelector('[data-testid="application-profile-view"]')?.getAttribute("data-profile-id") === id, fixture.id);
  await recovery().locator(":scope > summary").click();
  await recovery().getByTestId("save-workflow-snapshot").click();
  await recovery().getByRole("status").filter({ hasText: "saved and read back" }).waitFor();
  const primaryId = await view(0).getAttribute("data-profile-view-id");
  const primaryCheckpoint = `${keyFor(primaryId)}:checkpoint`;
  await check("missing bound input fails before native dispatch without creating an unknown-operation lock", async () => {
    await (await workflow(0)).getByRole("button", { name: "Create from input", exact: true }).click();
    await view(0).getByRole("alert").waitFor();
    assert.equal(creates.length, 0);
    assert.equal(await stored(primaryCheckpoint), null);
    assert.equal(await stored(`${keyFor(primaryId)}:recovery`), null);
    assert.match(await view(0).getByTestId("profile-workflow").textContent(), /create-flow: ready/);
  });
  await check("snapshot restore is blocked during delayed workflow dispatch and succeeds after completion", async () => {
    await startHeld(0, "Held workflow restore");
    const before = await stored(primaryCheckpoint);
    await recovery().getByTestId("restore-workflow-snapshot").click();
    await recovery().getByRole("alert").filter({ hasText: "Finish or cancel active operations" }).waitFor();
    assert.equal(await stored(primaryCheckpoint), before);
    await releaseHeld("Held workflow restore");
    await view(0).getByTestId("profile-workflow").filter({ hasText: "create-flow: chat" }).waitFor();
    assert.equal(JSON.parse(await stored(primaryCheckpoint)).entry.session.sequence, 1);
    await recovery().getByTestId("restore-workflow-snapshot").click();
    await recovery().getByRole("status").filter({ hasText: "restored without dispatching" }).waitFor();
    assert.equal(JSON.parse(await stored(primaryCheckpoint)).entry.session.sequence, 0);
    assert.equal(creates.length, 1);
    assert.equal(await view(0).getAttribute("data-conversation-id"), "");
  });
  await check("snapshot restore invalidates a delayed file import while retaining its source definition", async () => {
    await page.evaluate(() => {
      const original = Blob.prototype.arrayBuffer;
      Blob.prototype.arrayBuffer = async function () {
        if (this.name === "delayed-checkpoint-profile.json") {
          window.__checkpointImportStarted = true;
          await new Promise(resolve => { window.__releaseCheckpointImport = resolve; });
        }
        return original.call(this);
      };
    });
    const delayed = { ...fixture, id: "delayed-checkpoint-import", label: "Delayed checkpoint import" };
    await importProfile(0, delayed, "delayed-checkpoint-profile.json");
    await page.waitForFunction(() => window.__checkpointImportStarted === true);
    await recovery().getByTestId("restore-workflow-snapshot").click();
    await recovery().getByRole("status").filter({ hasText: "restored without dispatching" }).waitFor();
    await page.evaluate(() => window.__releaseCheckpointImport());
    await view(0).getByLabel("Application view profile", { exact: true }).locator("option").filter({ hasText: "Delayed checkpoint import" }).waitFor({ state: "attached" });
    assert.equal(await view(0).getAttribute("data-profile-id"), fixture.id);
    const saved = await page.evaluate(() => JSON.parse(localStorage.getItem("loom.application.views.v1")));
    assert.ok(saved.sources.some(source => source.profile === JSON.stringify([delayed.id, delayed.profile_revision]) && JSON.parse(source.text).id === delayed.id));
    assert.equal(creates.length, 1);
  });
  await check("closed-view completion preserves its pending checkpoint and cannot redirect restored selection", async () => {
    await page.getByTestId("add-profile-view").click();
    const closedId = await view(1).getAttribute("data-profile-view-id");
    const closedCheckpoint = `${keyFor(closedId)}:checkpoint`;
    await startHeld(1, "Held closed-view workflow");
    const before = await stored(closedCheckpoint);
    await view(1).getByRole("button", { name: "Close view", exact: true }).click();
    assert.equal(await page.getByTestId("application-profile-view").count(), 1);
    await recovery().getByTestId("restore-workflow-snapshot").click();
    await recovery().getByRole("alert").filter({ hasText: "Finish or cancel active operations" }).waitFor();
    await releaseHeld("Held closed-view workflow");
    await recovery().getByTestId("restore-workflow-snapshot").click();
    await recovery().getByRole("status").filter({ hasText: "restored without dispatching" }).waitFor();
    assert.equal(await stored(closedCheckpoint), before);
    assert.equal(await stored(keyFor(closedId)), null);
    assert.equal(await view(0).getAttribute("data-conversation-id"), "");
    assert.equal(creates.length, 2);
  });
  await check("global sidebar creation blocks restore until its native operation and selection complete", async () => {
    sidebarGate = "Held global sidebar";
    const requested = page.waitForRequest(request => request.method() === "POST" && new URL(request.url()).pathname === "/api/conversations");
    await page.getByTestId("sidebar").getByTestId("new-conversation").click(); await requested;
    await recovery().getByTestId("restore-workflow-snapshot").click();
    await recovery().getByRole("alert").filter({ hasText: "Finish or cancel active operations" }).waitFor();
    await releaseHeld("Held global sidebar");
    const record = creates.find(row => row.title === "Held global sidebar");
    await page.waitForFunction(id => document.querySelector('[data-testid="application-profile-view"]')?.getAttribute("data-conversation-id") === id, record.id);
    await recovery().getByTestId("restore-workflow-snapshot").click();
    await recovery().getByRole("status").filter({ hasText: "restored without dispatching" }).waitFor();
    assert.equal(await view(0).getAttribute("data-conversation-id"), "");
    assert.equal(creates.length, 3);
  });
  await check("closing a view with delayed sidebar creation preserves the restore lock and suppresses its old selection callback", async () => {
    await page.getByTestId("add-profile-view").click();
    await view(1).getByLabel("Conversation selection", { exact: true }).selectOption("independent");
    sidebarGate = "Held view sidebar";
    const requested = page.waitForRequest(request => request.method() === "POST" && new URL(request.url()).pathname === "/api/conversations");
    await view(1).getByTestId("profile-view-sidebar").getByTestId("new-conversation").click(); await requested;
    await view(1).getByRole("button", { name: "Close view", exact: true }).click();
    await recovery().getByTestId("restore-workflow-snapshot").click();
    await recovery().getByRole("alert").filter({ hasText: "Finish or cancel active operations" }).waitFor();
    await releaseHeld("Held view sidebar");
    await recovery().getByTestId("restore-workflow-snapshot").click();
    await recovery().getByRole("status").filter({ hasText: "restored without dispatching" }).waitFor();
    assert.equal(await page.getByTestId("application-profile-view").count(), 1);
    assert.equal(await view(0).getAttribute("data-conversation-id"), "");
    assert.equal(creates.length, 4);
  });
  assert.deepEqual(pageErrors, []); assert.deepEqual(external, []);
  if (process.env.WORKFLOW_CHECKPOINT_EVIDENCE_DIR) {
    const directory = process.env.WORKFLOW_CHECKPOINT_EVIDENCE_DIR; mkdirSync(directory, { recursive: true });
    const filename = path.join(directory, "workflow-checkpoint-results.json"); assert.ok(!existsSync(filename), "Use a fresh evidence directory.");
    writeFileSync(filename, JSON.stringify({ groups, passed: groups.length, fixture: "authored-local-http", paidCalls: 0, externalRequests: external }, null, 2));
  }
  console.log(`[workflow-checkpoint] ${groups.length}/${groups.length} passed; authored local HTTP fixtures; zero remote calls.`);
} catch (error) {
  console.error(JSON.stringify({ pageErrors, external, requests, body: page ? await page.locator("body").innerText().catch(() => "unavailable") : "no browser" }, null, 2));
  throw error;
} finally { held.forEach(entry => { try { entry.release(); } catch {} }); await browser?.close(); await new Promise(resolve => server.close(resolve)); }
