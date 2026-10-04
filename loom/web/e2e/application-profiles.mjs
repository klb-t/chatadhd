#!/usr/bin/env node
// Real native server + Chromium, with a local fake provider only. Run after
// building loom-server and the web bundle; no paid/remote model credentials.
import assert from "node:assert/strict";
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
assert.ok(existsSync(serverBin), `Build loom-server first: ${serverBin}`);
assert.ok(existsSync(path.join(dist, "index.html")), "Run npm run build first");
const evidenceDir = process.env.APPLICATION_PROFILE_EVIDENCE_DIR;
if (evidenceDir) {
  mkdirSync(evidenceDir, { recursive: true });
  assert.ok(!existsSync(path.join(evidenceDir, "application-profiles-results.json")), "Use a fresh evidence directory; earlier results are retained.");
}
const dataDir = mkdtempSync(path.join(tmpdir(), "loom-application-profiles-"));
writeFileSync(path.join(dataDir, "models.json"), JSON.stringify([
  { id: "mock/view-one", name: "Local view one", context_length: 8192 },
  { id: "mock/view-two", name: "Local view two", context_length: 8192 },
]));

const captures = [], chatRequests = [], nativeMutations = [], groups = [];
const pageErrors = [], externalRequests = [];
const heldReplies = new Map(), heldWaiters = new Map();
const heldMessages = new Set(["PROFILE_DELAYED_A_1873", "PROFILE_STOP_2391", "PROFILE_NEW_AFTER_STOP_8723"]);
let cancelGate, profileReceiptGate;
let serverLog = "", browser, page, baselineConfig, finalConfig, savedViews, savedWorkflow;
let server;
const provider = createServer(async (req, res) => {
  if (req.method !== "POST") { res.writeHead(404).end(); return; }
  try {
    const parts = []; for await (const part of req) parts.push(part);
    const body = JSON.parse(Buffer.concat(parts).toString()); captures.push(body);
    const content = `Local profile reply ${captures.length}: native streaming completed.`;
    const reply = () => {
      if (res.destroyed || res.writableEnded) return;
      if (body.stream) {
        res.writeHead(200, { "Content-Type": "text/event-stream" });
        res.write(`data: ${JSON.stringify({ choices: [{ delta: { reasoning: "Synthetic profile test." } }] })}\n\n`);
        res.write(`data: ${JSON.stringify({ choices: [{ delta: { content } }] })}\n\n`);
        res.end("data: [DONE]\n\n");
      } else {
        res.writeHead(200, { "Content-Type": "application/json" });
        res.end(JSON.stringify({ choices: [{ message: { role: "assistant", content } }], usage: { prompt_tokens: 1, completion_tokens: 1 } }));
      }
    };
    const message = body.messages.findLast(entry => entry.role === "user")?.content;
    if (heldMessages.has(message)) {
      heldReplies.set(message, reply); heldWaiters.get(message)?.(); heldWaiters.delete(message);
    } else reply();
  } catch (error) { res.writeHead(500).end(String(error)); }
});
await new Promise(resolve => provider.listen(0, "127.0.0.1", resolve));
const providerBase = `http://127.0.0.1:${provider.address().port}`;
const portProbe = createServer();
await new Promise(resolve => portProbe.listen(0, "127.0.0.1", resolve));
const serverPort = portProbe.address().port;
await new Promise(resolve => portProbe.close(resolve));
const base = `http://127.0.0.1:${serverPort}`;
server = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(serverPort), "--data-dir", dataDir, "--static-dir", dist], {
  cwd: loomRoot, stdio: ["ignore", "pipe", "pipe"],
});
for (const output of [server.stdout, server.stderr]) output.on("data", chunk => { serverLog += chunk.toString(); });
server.on("error", error => { serverLog += error.stack; });

async function api(method, endpoint, body) {
  const response = await fetch(`${base}${endpoint}`, { method, headers: { "Content-Type": "application/json" },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  const result = await response.json();
  assert.ok(response.ok && !(result.error && typeof result.error === "object"), `${endpoint}: ${JSON.stringify(result)}`);
  return result;
}
async function waitForServer() {
  const deadline = Date.now() + 20000;
  while (Date.now() < deadline) {
    try { if ((await fetch(`${base}/api/healthz`)).ok) return; } catch { /* Native server is still starting. */ }
    if (server.exitCode !== null) throw new Error(`Loom exited: ${serverLog}`);
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  throw new Error(`Loom startup timeout: ${serverLog}`);
}
async function check(name, fn) {
  await fn(); groups.push(name); console.log(`[application-profiles] PASS ${name}`);
}
const view = index => page.getByTestId("application-profile-view").nth(index);
const userMessage = (index, text) => view(index).locator('[data-testid="message"][data-role="user"]')
  .filter({ has: page.locator(".body").filter({ hasText: text }) });
const profileKey = id => JSON.stringify([id, ["loom-default", "chatgpt-inspired", "claude-inspired", "gemini-inspired"].includes(id) ? 2 : 1]);
async function choose(index, id) { await view(index).getByLabel("Application view profile", { exact: true }).selectOption(profileKey(id)); }
async function openContext(index) {
  const details = view(index).getByTestId("chat-context-controls");
  if (await details.getAttribute("open") === null) await details.locator(":scope > summary").click();
}
async function send(index, text, expectedCalls, shortcut = false) {
  await view(index).getByTestId("chat-input").fill(text);
  if (shortcut) await view(index).getByTestId("chat-input").press("Control+Enter");
  else await view(index).getByTestId("send-chat").click();
  await view(index).locator('[data-testid="message"][data-role="assistant"]').filter({ hasText: `Local profile reply ${expectedCalls}:` }).waitFor({ timeout: 20000 });
  assert.equal(captures.length, expectedCalls);
  assert.equal(await view(index).getByTestId("chat-error").count(), 0);
  await view(index).getByTestId("streaming-message").waitFor({ state: "detached" });
}
async function importProfile(index, document, text = JSON.stringify(document)) {
  await view(index).getByLabel("Import application profile", { exact: true }).setInputFiles({
    name: `${document.id}.json`, mimeType: "application/json", buffer: Buffer.from(text),
  });
}
async function unchangedConfig() { assert.deepEqual(await api("GET", "/api/config"), baselineConfig); }
async function waitForHeld(message) {
  if (heldReplies.has(message)) return;
  let timer;
  try {
    await Promise.race([new Promise(resolve => heldWaiters.set(message, resolve)),
      new Promise((_, reject) => { timer = setTimeout(() => reject(new Error(`Provider did not receive held message ${message}.`)), 10000); })]);
  } finally { clearTimeout(timer); heldWaiters.delete(message); }
}
function releaseHeld(message) { assert.ok(heldReplies.has(message)); heldReplies.get(message)(); heldReplies.delete(message); }
async function within(promise, label) {
  let timer;
  try {
    return await Promise.race([promise, new Promise((_, reject) => {
      timer = setTimeout(() => reject(new Error(`${label} did not settle.`)), 10000);
    })]);
  } finally { clearTimeout(timer); }
}

try {
  await waitForServer();
  await api("PATCH", "/api/config", { base_url: providerBase, default_model: "mock/default", semantic_analysis: false, stream: true });
  await api("POST", "/api/secrets/api_key", { value: "local-fake-provider-only" });
  const run = await api("POST", "/api/knowledge/run", {
    sources: [path.join(loomRoot, "tests/fixtures/eval/synthetic_dev/chatgpt_export.zip")], llm: "off", priors: false,
  });
  assert.equal(run.status, "done", "synthetic offline knowledge preparation completed");
  const entities = await api("POST", "/api/knowledge/query", { what: "entities", run: run.run, limit: 1000 });
  const target = entities.items.find(entity => entity.kind === "project") || entities.items[0];
  assert.ok(target?.id, "native context has a real synthetic entity anchor");
  baselineConfig = await api("GET", "/api/config");
  const providersBefore = await api("GET", "/api/providers");
  assert.equal(captures.length, 0);
  const cachedShell = "/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell";
  const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || (existsSync(cachedShell) ? cachedShell : undefined);
  browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  page = await browser.newPage({ viewport: { width: 1600, height: 1100 } });
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.route("**/*", async route => {
    const request = route.request(); const url = new URL(request.url());
    if (url.origin !== base) { externalRequests.push(request.url()); await route.abort(); return; }
    if (request.method() === "POST" && url.pathname === "/api/chat") chatRequests.push(request.postDataJSON());
    if (!["GET", "HEAD"].includes(request.method())) nativeMutations.push({ method: request.method(), path: url.pathname, body: request.postDataJSON() });
    if (url.pathname === "/api/graph/packets/store" && profileReceiptGate && request.postDataJSON()?.operation === "read") {
      const gate = profileReceiptGate; profileReceiptGate = undefined;
      const response = await route.fetch(); gate.observed(); await gate.releasePromise;
      await route.fulfill({ response }); gate.finished(); return;
    }
    if (url.pathname === "/api/chat/cancel" && cancelGate) {
      const gate = cancelGate; cancelGate = undefined;
      // Exercise the real native cancellation first. Delay a synthetic error
      // response to prove that an old Stop cannot affect a subsequent Send.
      await route.fetch(); gate.observed(); await gate.releasePromise;
      await route.fulfill({ status: 503, contentType: "application/json", body: JSON.stringify({ error: { code: "test", message: "Delayed synthetic cancel failure" } }) });
      gate.finished(); return;
    }
    if (url.pathname === "/api/chat" && request.postDataJSON()?.message === "PROFILE_TRUNCATED_EOF_4819") {
      const req = request.postDataJSON();
      await route.fulfill({ status: 200, contentType: "text/event-stream", body:
        `data: ${JSON.stringify({ type: "start", request_id: "synthetic-eof", conv_id: req.conv_id, user_message_id: "synthetic-unsaved-user" })}\n\n` +
        `data: ${JSON.stringify({ type: "delta", text: "Synthetic partial response." })}\n\n` });
      return;
    }
    await route.continue();
  });
  await page.goto(base);
  await view(0).waitFor();
  await page.getByTestId("new-conversation").click();
  await page.locator('[data-testid="conv-item"].active').waitFor();
  const conversation = (await api("GET", "/api/conversations?limit=1"))[0];

  await check("profile changes preserve explicit model and native context selection", async () => {
    await view(0).getByTestId("model-picker").selectOption("mock/view-one");
    await openContext(0);
    for (const id of ["include-memory", "include-graph-memory", "include-history"]) await view(0).getByTestId(id).uncheck();
    await view(0).getByTestId("use-knowledge-context").check();
    await view(0).getByTestId("context-query").fill("SYNTHETIC_PROFILE_CONTEXT");
    await view(0).getByTestId("context-targets").fill(target.id);
    await view(0).getByTestId("context-run").fill(run.run);
    await view(0).getByTestId("context-budget").fill("1337");
    await view(0).getByTestId("context-hops").fill("2");
    await view(0).getByTestId("context-detail").selectOption("full");
    await choose(0, "chatgpt-inspired"); await choose(0, "claude-inspired"); await choose(0, "chatgpt-inspired");
    assert.equal(await view(0).getByTestId("model-picker").inputValue(), "mock/view-one");
    assert.equal(await view(0).getByTestId("context-query").inputValue(), "SYNTHETIC_PROFILE_CONTEXT");
    assert.equal(await view(0).getByTestId("context-budget").inputValue(), "1337");
    assert.equal(await view(0).getByTestId("context-hops").inputValue(), "2");
    assert.equal(await view(0).getByTestId("context-detail").inputValue(), "full");
    for (const id of ["include-memory", "include-graph-memory", "include-history"]) assert.equal(await view(0).getByTestId(id).isChecked(), false);
    assert.equal(await view(0).getByTestId("use-knowledge-context").isChecked(), true);
    assert.equal(captures.length, 0); await unchangedConfig();
  });

  await check("two simultaneous application views keep independent profile/model/context settings", async () => {
    await page.getByTestId("add-profile-view").click(); assert.equal(await page.getByTestId("application-profile-view").count(), 2);
    await choose(1, "gemini-inspired"); await view(1).getByTestId("model-picker").selectOption("mock/view-two");
    await openContext(1);
    assert.equal(await view(0).getAttribute("data-profile-id"), "chatgpt-inspired");
    assert.equal(await view(1).getAttribute("data-profile-id"), "gemini-inspired");
    assert.equal(await view(0).getByTestId("model-picker").inputValue(), "mock/view-one");
    assert.equal(await view(1).getByTestId("model-picker").inputValue(), "mock/view-two");
    assert.equal(await view(1).getByTestId("use-knowledge-context").isChecked(), false);
    assert.equal(await view(1).getByTestId("include-history").isChecked(), true);
    assert.equal(captures.length, 0); await unchangedConfig();
  });

  await check("pinned clone versions expose source-mapped layout and composer behavior", async () => {
    await choose(1, "librechat-0.8.8");
    assert.match(await view(1).getAttribute("class"), /profile-messages-user-bubble/);
    assert.equal(await view(1).getByTestId("model-picker").inputValue(), "mock/view-two");
    await choose(1, "nextchat-2.16.1");
    await view(1).getByTestId("chat-input").fill("PINNED_COMPOSER_DRAFT");
    for (const chord of ["Control+Enter", "Alt+Enter", "Shift+Enter"]) {
      await view(1).getByTestId("chat-input").press(chord);
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
      assert.equal(chatRequests.length, 0);
    }
    assert.match(await view(1).getByTestId("chat-input").inputValue(), /PINNED_COMPOSER_DRAFT/);
    await view(1).getByTestId("chat-input").fill(""); await choose(1, "gemini-inspired");
    await unchangedConfig();
  });

  await check("profile Send reaches native completion and both views show the same saved conversation", async () => {
    await send(0, "PROFILE_SHARED_ORIGINAL_4517", 1);
    await view(1).locator('[data-testid="message"][data-role="assistant"]').filter({ hasText: "Local profile reply 1:" }).waitFor();
    await userMessage(1, "PROFILE_SHARED_ORIGINAL_4517").waitFor();
    assert.equal(chatRequests[0].model, "mock/view-one"); assert.equal(captures[0].model, "mock/view-one");
    assert.equal(chatRequests[0].conv_id, conversation.id);
    for (const key of ["include_memory", "include_graph_memory", "include_history"]) assert.equal(chatRequests[0][key], false);
    assert.deepEqual(chatRequests[0].knowledge_context, { text: "SYNTHETIC_PROFILE_CONTEXT", targets: [target.id], budget_tokens: 1337, run: run.run, relation_hops: 2, detail_resolution: "full" });
    const messages = await api("GET", `/api/conversations/${conversation.id}/messages`);
    const user = messages.find(message => message.text === "PROFILE_SHARED_ORIGINAL_4517");
    assert.equal(user.metadata.context_trace.kind, "compiled_messages");
    assert.deepEqual(user.metadata.context_trace.messages, captures[0].messages, "actual provider messages match native trace");
    await unchangedConfig();
  });

  await check("profile edit and restore use native message versions and refresh both views", async () => {
    let user = userMessage(0, "PROFILE_SHARED_ORIGINAL_4517");
    await user.getByTestId("edit-message").click();
    // Editing removes the rendered body, so keep this row locator independent
    // of body text until its new version has been saved.
    user = view(0).locator('[data-testid="message"][data-role="user"]').first();
    await user.locator("textarea").fill("PROFILE_SHARED_EDITED_8321"); await user.getByTestId("save-edit").click();
    user = userMessage(0, "PROFILE_SHARED_EDITED_8321"); await user.waitFor();
    await userMessage(1, "PROFILE_SHARED_EDITED_8321").waitFor();
    await user.getByTestId("load-versions").click(); await user.getByTestId("switch-version").first().waitFor();
    assert.ok(await user.getByTestId("switch-version").count() >= 2); await user.getByTestId("switch-version").first().click();
    await userMessage(0, "PROFILE_SHARED_ORIGINAL_4517").waitFor();
    await userMessage(1, "PROFILE_SHARED_ORIGINAL_4517").waitFor();
    assert.ok(nativeMutations.some(mutation => /\/api\/messages\/[^/]+\/edit$/.test(mutation.path) && mutation.body.text === "PROFILE_SHARED_EDITED_8321"));
    assert.ok(nativeMutations.some(mutation => /\/api\/messages\/[^/]+\/restore$/.test(mutation.path)));
    const messages = await api("GET", `/api/conversations/${conversation.id}/messages?all=1`);
    assert.ok(messages.some(message => message.text === "PROFILE_SHARED_EDITED_8321" && message.status === "version"));
    assert.ok(messages.some(message => message.text === "PROFILE_SHARED_ORIGINAL_4517" && message.status === "active"));
    assert.equal(captures.length, 1);
  });

  const custom = JSON.parse(readFileSync(path.join(webRoot, "src/profiles/data/loom-default-r2.json"), "utf8"));
  custom.profile_revision = 1;
  custom.id = "synthetic-arbitrary-app-3.7"; custom.label = "Synthetic arbitrary app 3.7";
  custom.target = { application_id: "synthetic-any-application", version: "ui-release-3.7", platform: "web" };
  custom.evidence = { status: "partial", sources: [], gaps: ["Synthetic editable profile; no external application fidelity claim."] };
  custom.presentation.sidebar = { side: "right", width: 240 }; custom.presentation.content_width = 610;
  custom.composer = { submit: "mod-enter", placeholder: "Custom application composer" };
  // Multiple actions may map one operation; direct controls choose an actually
  // supported mapping rather than the unavailable source-service entry first.
  custom.actions.unshift({ id: "original-send-gap", label: "Original service Send unavailable", operation: "chat.send", capability: "original.chat.send", required: false });
  const customSource = "\ufeff" + JSON.stringify(custom, null, 2) + "\n";
  await check("arbitrary versioned profile imports exact UTF-8 bytes with right sidebar and explicit shortcut", async () => {
    await importProfile(0, custom, customSource);
    await page.waitForFunction(id => document.querySelector('[data-testid="application-profile-view"]')?.getAttribute("data-profile-id") === id, custom.id);
    assert.equal(await view(0).getAttribute("data-profile-revision"), "1");
    assert.equal(await view(0).getByTestId("model-picker").inputValue(), "mock/view-one");
    assert.equal(await view(0).getByTestId("context-run").inputValue(), run.run);
    assert.equal(await page.locator(".app-root").getAttribute("data-sidebar-side"), "right");
    assert.equal(await page.getByTestId("sidebar").evaluate(element => getComputedStyle(element).order), "2");
    assert.equal(await page.getByTestId("sidebar").evaluate(element => getComputedStyle(element).width), "240px");
    assert.equal(await view(0).getByTestId("chat-input").getAttribute("placeholder"), "Custom application composer");
    await view(0).getByTestId("chat-input").fill("PROFILE_CUSTOM_SHORTCUT_7198");
    await view(0).getByTestId("chat-input").press("Enter");
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    assert.equal(chatRequests.length, 1, "plain Enter keeps the draft for a mod-enter profile");
    assert.equal(captures.length, 1); assert.match(await view(0).getByTestId("chat-input").inputValue(), /PROFILE_CUSTOM_SHORTCUT_7198\n/);
    await send(0, "PROFILE_CUSTOM_SHORTCUT_7198", 2, true);
    assert.equal(chatRequests[1].model, "mock/view-one"); assert.equal(captures[1].model, "mock/view-one");
    assert.deepEqual(chatRequests[1].knowledge_context, chatRequests[0].knowledge_context);
    await send(1, "PROFILE_INDEPENDENT_VIEW_6413", 3);
    assert.equal(chatRequests[2].model, "mock/view-two"); assert.equal(captures[2].model, "mock/view-two");
    assert.equal(chatRequests[2].knowledge_context, undefined);
    assert.equal(chatRequests[2].include_history, undefined);
    await userMessage(0, "PROFILE_INDEPENDENT_VIEW_6413").waitFor();
    await unchangedConfig();
  });

  await check("invalid UTF-8 profile bytes cannot be silently repaired into accepted source", async () => {
    await view(0).getByLabel("Import application profile", { exact: true }).setInputFiles({
      name: "invalid-utf8.json", mimeType: "application/json", buffer: Buffer.concat([Buffer.from('{"label":"'), Buffer.from([255]), Buffer.from('"}')]),
    });
    await page.getByRole("alert").filter({ hasText: /encoded data|encoding/i }).waitFor();
    assert.equal(await view(0).getAttribute("data-profile-id"), custom.id);
    assert.equal(captures.length, 3);
  });

  await check("a declared required capability without a real adapter cannot activate", async () => {
    const unsupported = structuredClone(custom); unsupported.id = "synthetic-unsupported-required"; unsupported.label = "Unsupported required workflow";
    unsupported.actions.find(action => action.operation === "chat.send" && action.required).capability = "vendor-unimplemented-send";
    await importProfile(0, unsupported);
    await page.getByRole("alert").filter({ hasText: "Profile retained but cannot run" }).waitFor();
    assert.equal(await view(0).getAttribute("data-profile-id"), custom.id);
    const option = view(0).getByLabel("Application view profile", { exact: true }).locator("option").filter({ hasText: "Unsupported required workflow" });
    assert.equal(await option.getAttribute("disabled"), "");
    assert.equal(captures.length, 3); await unchangedConfig();
  });

  await check("declarative new conversation → knowledge → return reaches actual native/UI operations", async () => {
    await view(0).getByText("Profile and workflows", { exact: true }).click();
    let flow = view(0).getByTestId("profile-workflow");
    assert.match(await flow.innerText(), /conversation-review: ready/);
    await flow.getByRole("button", { name: "New conversation", exact: true }).click();
    await page.waitForFunction(() => document.querySelector('[data-testid="profile-workflow"]')?.textContent.includes("conversation-review: chat"));
    const fresh = (await api("GET", "/api/conversations?limit=1"))[0];
    assert.notEqual(fresh.id, conversation.id); assert.equal(fresh.title, "Profile conversation");
    assert.equal(await view(0).getByTestId("message").count(), 0); assert.equal(await view(1).getByTestId("message").count(), 0);
    await flow.getByRole("button", { name: "Inspect knowledge", exact: true }).click();
    await page.getByTestId("knowledge-workbench").waitFor();
    assert.match(await flow.innerText(), /conversation-review: review/);
    await page.getByTestId("new-conversation").click();
    await page.waitForFunction(() => document.querySelector('[data-testid="conv-item"].active .title')?.textContent === "New Chat");
    await flow.getByRole("button", { name: "Return to conversation", exact: true }).click();
    await page.waitForFunction(() => document.querySelector('[data-testid="profile-workflow"]')?.textContent.includes("conversation-review: chat"));
    const active = await page.locator('[data-testid="conv-item"].active').innerText(); assert.match(active, /Profile conversation/);
    await page.getByRole("button", { name: "Close knowledge workbench", exact: true }).click();
    savedWorkflow = await page.evaluate(() => {
      const key = `loom.application.workflow.v1:${JSON.stringify(["primary", "synthetic-arbitrary-app-3.7", 1])}`;
      return JSON.parse(localStorage.getItem(key));
    });
    assert.equal(savedWorkflow.workflows["conversation-review"], "chat");
    assert.equal(savedWorkflow.variables["conversation-review"].conversation_id, fresh.id);
    assert.equal(savedWorkflow.history[0].variables.conversation_id, fresh.id);
    assert.deepEqual(savedWorkflow.history.map(receipt => receipt.operation), ["chat.create", "knowledge.open", "chat.select"]);
    assert.equal(captures.length, 3);
  });

  await check("exact profile source reaches canonical native graph and reloads from an immutable receipt", async () => {
    await view(0).getByTestId("save-profile-graph").click();
    await view(0).getByTestId("profile-graph-receipt").waitFor();
    const receiptId = await view(0).getByTestId("profile-graph-receipt").locator("code").first().innerText();
    const stored = await api("POST", "/api/graph/packets/store", { operation: "read", receipt_id: receiptId });
    assert.equal(stored.row_drift.matches, true);
    assert.equal(stored.receipt.acceptance_establishes_content_truth, false);
    const entity = stored.receipt.packet.entities[0];
    assert.equal(entity.kind, "application_profile");
    assert.equal(entity.attrs.raw_source, customSource);
    assert.equal(stored.receipt.packet.sources[0].observation.text, customSource);
    assert.deepEqual(JSON.parse(entity.attrs.raw_source.replace(/^\ufeff/, "")), custom);
    assert.deepEqual(stored.receipt.packet.history, []);
    const materialized = await api("POST", "/api/knowledge/query", { what: "entities", run: stored.receipt.run_id, limit: 1000 });
    assert.ok(materialized.items.some(row => row.id === entity.id && row.attrs.raw_source === customSource));
    await view(0).getByTestId("save-profile-graph").click();
    await page.waitForFunction(() => !document.querySelector('[data-testid="save-profile-graph"]').disabled);
    await choose(0, "loom-default");
    await page.getByText("Saved profiles", { exact: true }).click();
    await page.getByTestId("find-native-profiles").click();
    await page.getByLabel("Recent native profile receipts", { exact: true }).waitFor();
    await page.getByLabel("Recent native profile receipts", { exact: true }).selectOption(receiptId);
    let observed, release, finished;
    const observedPromise = new Promise(resolve => { observed = resolve; });
    const releasePromise = new Promise(resolve => { release = resolve; });
    const finishedPromise = new Promise(resolve => { finished = resolve; });
    profileReceiptGate = { observed, releasePromise, finished };
    await page.getByTestId("load-profile-receipt").click();
    await within(observedPromise, "Delayed native profile read");
    await page.getByTestId("add-profile-view").click();
    await choose(0, "chatgpt-inspired");
    release(); await within(finishedPromise, "Delayed profile read response");
    await page.getByTestId("load-profile-receipt").waitFor({ state: "visible" });
    await page.waitForFunction(() => !document.querySelector('[data-testid="load-profile-receipt"]').disabled);
    assert.equal(await page.getByTestId("application-profile-view").count(), 3);
    assert.equal(await view(0).getAttribute("data-profile-id"), "chatgpt-inspired", "late receipt cannot overwrite newer profile choice");
    await view(2).getByRole("button", { name: "Close view", exact: true }).click();
    await page.getByTestId("load-profile-receipt").click();
    await page.waitForFunction(id => document.querySelector('[data-testid="application-profile-view"]')?.getAttribute("data-profile-id") === id, custom.id);
    assert.equal(await view(0).getByTestId("model-picker").inputValue(), "mock/view-one");
    assert.equal(await view(0).getByTestId("context-run").inputValue(), run.run);
    const saved = await page.evaluate(() => JSON.parse(localStorage.getItem("loom.application.views.v1")));
    assert.ok(saved.sources.some(source => source.profile === JSON.stringify([custom.id, 1]) && source.text === customSource));
    assert.equal(captures.length, 3); await unchangedConfig();
  });

  await check("two exact profile revisions and workflow state reload without provider/config mutation", async () => {
    savedViews = await page.evaluate(() => JSON.parse(localStorage.getItem("loom.application.views.v1")));
    assert.equal(savedViews.views.length, 2); assert.equal(savedViews.views[0].profile, profileKey(custom.id));
    assert.equal(savedViews.views[1].profile, profileKey("gemini-inspired"));
    await page.reload(); await view(1).waitFor();
    assert.equal(await view(0).getAttribute("data-profile-id"), custom.id);
    assert.equal(await view(1).getAttribute("data-profile-id"), "gemini-inspired");
    assert.equal(await page.locator(".app-root").getAttribute("data-sidebar-side"), "right");
    await view(0).getByText("Profile and workflows", { exact: true }).click();
    assert.match(await view(0).getByTestId("profile-workflow").innerText(), /conversation-review: chat/);
    assert.deepEqual(await page.evaluate(() => JSON.parse(localStorage.getItem("loom.application.views.v1"))), savedViews);
    finalConfig = await api("GET", "/api/config"); assert.deepEqual(finalConfig, baselineConfig);
    assert.deepEqual(await api("GET", "/api/providers"), providersBefore);
    assert.equal(nativeMutations.filter(mutation => mutation.path === "/api/config").length, 0);
    assert.equal(captures.length, 3); assert.equal(chatRequests.length, 3);
    assert.deepEqual(pageErrors, []); assert.deepEqual(externalRequests, []);
  });
  await check("completion for conversation A cannot replace selected conversation B", async () => {
    const a = await api("POST", "/api/conversations", { title: "Synthetic pending A" });
    const b = await api("POST", "/api/conversations", { title: "Synthetic selected B" });
    await page.reload(); await view(1).waitFor();
    await page.getByTestId("conv-item").filter({ hasText: "Synthetic pending A" }).click();
    await view(0).getByTestId("chat-input").fill("PROFILE_DELAYED_A_1873"); await view(0).getByTestId("send-chat").click();
    await waitForHeld("PROFILE_DELAYED_A_1873"); await view(0).getByTestId("cancel-chat").waitFor();
    await page.getByTestId("conv-item").filter({ hasText: "Synthetic selected B" }).click();
    assert.equal(await view(0).getByTestId("streaming-message").count(), 0);
    assert.equal(await userMessage(0, "PROFILE_DELAYED_A_1873").count(), 0);
    releaseHeld("PROFILE_DELAYED_A_1873");
    await view(0).getByTestId("cancel-chat").waitFor({ state: "detached" });
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    assert.equal(await view(0).getByTestId("message").count(), 0); assert.equal(await view(1).getByTestId("message").count(), 0);
    assert.match(await page.locator('[data-testid="conv-item"].active').innerText(), /Synthetic selected B/);
    const aMessages = await api("GET", `/api/conversations/${a.id}/messages`);
    assert.ok(aMessages.some(message => message.role === "assistant" && message.text.includes("Local profile reply 4:")));
    assert.deepEqual(await api("GET", `/api/conversations/${b.id}/messages`), []);
    assert.equal(captures.length, 4);
  });

  await check("delayed old Stop failure cannot clear or report an error on a new Send", async () => {
    let observed, release, finished;
    const observedPromise = new Promise(resolve => { observed = resolve; });
    const releasePromise = new Promise(resolve => { release = resolve; });
    const finishedPromise = new Promise(resolve => { finished = resolve; });
    cancelGate = { observed, releasePromise, finished };
    await view(0).getByTestId("chat-input").fill("PROFILE_STOP_2391"); await view(0).getByTestId("send-chat").click();
    await waitForHeld("PROFILE_STOP_2391"); await view(0).getByTestId("cancel-chat").click();
    await within(observedPromise, "Native cancel request"); await view(0).getByTestId("send-chat").waitFor();
    await view(0).getByTestId("chat-input").fill("PROFILE_NEW_AFTER_STOP_8723"); await view(0).getByTestId("send-chat").click();
    await waitForHeld("PROFILE_NEW_AFTER_STOP_8723"); await view(0).getByTestId("streaming-message").waitFor();
    release(); await within(finishedPromise, "Delayed cancel response");
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
    await view(0).getByTestId("streaming-message").waitFor(); assert.equal(await view(0).getByTestId("chat-error").count(), 0);
    releaseHeld("PROFILE_STOP_2391"); releaseHeld("PROFILE_NEW_AFTER_STOP_8723");
    await view(0).locator('[data-testid="message"][data-role="assistant"]').filter({ hasText: "Local profile reply 6:" }).waitFor();
    await view(0).getByTestId("streaming-message").waitFor({ state: "detached" });
    assert.equal(await view(0).getByTestId("chat-error").count(), 0); assert.equal(captures.length, 6);
  });

  await check("truncated transport EOF clears pending content and exposes failure", async () => {
    await view(0).getByTestId("chat-input").fill("PROFILE_TRUNCATED_EOF_4819"); await view(0).getByTestId("send-chat").click();
    await view(0).getByTestId("chat-error").waitFor();
    assert.match(await view(0).getByTestId("chat-error").innerText(), /ended before a done result/);
    await view(0).getByTestId("streaming-message").waitFor({ state: "detached" });
    assert.equal(await view(0).locator(".msg.user .body").filter({ hasText: "PROFILE_TRUNCATED_EOF_4819" }).count(), 0);
    assert.equal(await view(0).getByTestId("send-chat").count(), 1); assert.equal(captures.length, 6);
  });

  await check("optional missing or absent Send stays disabled and preserves the draft", async () => {
    const requestsBefore = chatRequests.length;
    for (const mode of ["missing", "absent"]) {
      const noSend = structuredClone(custom); noSend.id = `synthetic-no-send-${mode}`; noSend.label = `Synthetic no Send ${mode}`;
      if (mode === "missing") {
        const action = noSend.actions.find(action => action.operation === "chat.send" && action.required); action.required = false; action.capability = "missing-send-adapter";
      } else noSend.actions = noSend.actions.filter(action => action.operation !== "chat.send");
      await importProfile(0, noSend);
      await page.waitForFunction(id => document.querySelector('[data-testid="application-profile-view"]')?.getAttribute("data-profile-id") === id, noSend.id);
      await view(0).getByTestId("chat-input").fill(`PROFILE_PRESERVED_DRAFT_${mode}`);
      assert.equal(await view(0).getByTestId("send-chat").isDisabled(), true);
      await view(0).getByTestId("chat-input").press("Control+Enter");
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
      assert.equal(await view(0).getByTestId("chat-input").inputValue(), `PROFILE_PRESERVED_DRAFT_${mode}`);
      assert.equal(chatRequests.length, requestsBefore);
    }
    await choose(0, custom.id); await unchangedConfig();
    finalConfig = await api("GET", "/api/config");
    assert.deepEqual(pageErrors, []); assert.deepEqual(externalRequests, []); assert.equal(captures.length, 6);
  });
  console.log(`[application-profiles] ${groups.length}/${groups.length} groups passed; ${captures.length} local fake-provider calls; 0 remote calls`);
} catch (error) {
  console.error(serverLog);
  if (evidenceDir && page) await page.screenshot({ path: path.join(evidenceDir, "application-profiles-failure.png"), fullPage: true }).catch(() => {});
  throw error;
} finally {
  if (browser) await browser.close();
  for (const reply of heldReplies.values()) reply();
  server.kill("SIGTERM");
  await new Promise(resolve => { if (server.exitCode !== null) resolve(); else server.once("exit", resolve); });
  await new Promise(resolve => provider.close(resolve));
  rmSync(dataDir, { recursive: true, force: true });
  if (evidenceDir) writeFileSync(path.join(evidenceDir, "application-profiles-results.json"), JSON.stringify({
    groups, counts: { passed_groups: groups.length, local_provider_calls: captures.length, remote_calls: externalRequests.length },
    synthetic_fixture: "synthetic_dev/chatgpt_export.zip", provider: "local fake only", browser_chat_requests: chatRequests,
    captured_provider_requests: captures, native_mutations: nativeMutations, saved_views: savedViews,
    saved_workflow: savedWorkflow, config_unchanged: baselineConfig && finalConfig && JSON.stringify(baselineConfig) === JSON.stringify(finalConfig),
    page_errors: pageErrors, external_requests: externalRequests,
  }, null, 2) + "\n", { flag: "wx" });
}
