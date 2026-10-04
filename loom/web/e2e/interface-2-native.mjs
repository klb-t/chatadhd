#!/usr/bin/env node
// Real production bundle -> native Loom server -> local fake provider only.
// Public authored fixtures; no external models, accounts or retained private data.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from "node:fs";
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
const evidenceDir = process.env.INTERFACE_2_EVIDENCE_DIR;
if (evidenceDir) {
  mkdirSync(evidenceDir, { recursive: true });
  for (const name of ["results.json", "server.log", "failure.png"]) assert.ok(!existsSync(path.join(evidenceDir, name)), "Use a fresh evidence directory; failed attempts must remain intact.");
}
const dataDir = mkdtempSync(path.join(tmpdir(), "loom-interface-2-"));
const mediaTemp = path.join(dataDir, "media-temp");
mkdirSync(mediaTemp);
writeFileSync(path.join(dataDir, "models.json"), JSON.stringify([
  { id: "mock/interface-one", name: "Local interface one", context_length: 8192 },
  { id: "mock/interface-two", name: "Local interface two", context_length: 8192 },
]));
const captures = [], chatRequests = [], nativeMutations = [], externalRequests = [], pageErrors = [], groups = [];
let browser, page, serverLog = "", server;
let failure = null;
const provider = createServer(async (req, res) => {
  if (req.method !== "POST" || !req.url.endsWith("/chat/completions")) { res.writeHead(404).end(); return; }
  try {
    const chunks = []; for await (const chunk of req) chunks.push(chunk);
    const body = JSON.parse(Buffer.concat(chunks).toString()); captures.push(body);
    const content = `Native interface reply ${captures.length}`;
    if (body.stream) {
      res.writeHead(200, { "Content-Type": "text/event-stream" });
      res.write(`data: ${JSON.stringify({ choices: [{ delta: { content } }] })}\n\n`);
      res.end("data: [DONE]\n\n");
    } else {
      res.writeHead(200, { "Content-Type": "application/json" });
      res.end(JSON.stringify({ choices: [{ message: { role: "assistant", content } }], usage: { prompt_tokens: 1, completion_tokens: 1 } }));
    }
  } catch (error) { res.writeHead(500).end(String(error)); }
});
await new Promise(resolve => provider.listen(0, "127.0.0.1", resolve));
const providerBase = `http://127.0.0.1:${provider.address().port}`;
const probe = createServer();
await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
const port = probe.address().port;
await new Promise(resolve => probe.close(resolve));
const base = `http://127.0.0.1:${port}`;
server = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", dataDir, "--static-dir", dist], {
  cwd: loomRoot, env: { ...process.env, TMPDIR: mediaTemp }, stdio: ["ignore", "pipe", "pipe"],
});
for (const output of [server.stdout, server.stderr]) output.on("data", chunk => { serverLog += chunk.toString(); });
server.on("error", error => { serverLog += String(error); });
async function api(method, endpoint, body) {
  const response = await fetch(`${base}${endpoint}`, { method, headers: { "Content-Type": "application/json" },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  const result = await response.json();
  assert.ok(response.ok && !(result.error && typeof result.error === "object"), `${endpoint}: ${JSON.stringify(result)}`);
  return result;
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
async function check(name, run) { await run(); groups.push(name); console.log(`[interface-2-native] PASS ${name}`); }
const view = () => page.getByTestId("application-profile-view").first();
async function openDetails(locator) { if (await locator.getAttribute("open") === null) await locator.locator(":scope > summary").click(); }
async function chooseConversation(id) {
  const conversations = await api("GET", "/api/conversations?limit=100");
  const target = conversations.find(item => item.id === id); assert.ok(target);
  await page.getByTestId("sidebar").getByTestId("conv-item").filter({ has: page.locator(".title", { hasText: target.title }) }).first().click();
  await page.waitForFunction(value => document.querySelector('[data-testid="application-profile-view"]')?.getAttribute("data-conversation-id") === value, id);
}
async function send(text, count) {
  await view().getByTestId("chat-input").fill(text);
  await view().getByTestId("send-chat").click();
  await view().locator('[data-testid="message"][data-role="assistant"]').nth(count - 1).waitFor({ timeout: 20000 });
  assert.equal(await view().getByTestId("chat-error").count(), 0);
}
try {
  await ready();
  await api("PATCH", "/api/config", { base_url: providerBase, default_model: "mock/interface-one", semantic_analysis: false, stream: false });
  await api("POST", "/api/secrets/api_key", { value: "local-fake-provider-only" });
  const run = await api("POST", "/api/knowledge/run", { sources: [path.join(loomRoot, "tests/fixtures/eval/synthetic_dev/chatgpt_export.zip")], llm: "off", priors: false });
  assert.equal(run.status, "done");
  const entities = await api("POST", "/api/knowledge/query", { what: "entities", run: run.run, limit: 1000 });
  const target = entities.items.find(item => item.kind === "project") || entities.items[0]; assert.ok(target?.id);
  const form = new FormData();
  form.append("file", new Blob([readFileSync(path.join(loomRoot, "tests/fixtures/exports/openai_2026_sharded.zip"))]), "public-openai.zip");
  const imported = await fetch(`${base}/api/import`, { method: "POST", body: form });
  assert.ok(imported.ok);
  const importEvents = (await imported.text()).split(/\r?\n/).filter(line => line.startsWith("data: ")).map(line => JSON.parse(line.slice(6)));
  assert.ok(importEvents.some(event => event.type === "done")); assert.ok(!importEvents.some(event => event.type === "error"));
  assert.equal(captures.length, 0);
  const cachedShell = "/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell";
  const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || (existsSync(cachedShell) ? cachedShell : undefined);
  browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  page = await browser.newPage({ viewport: { width: 1550, height: 1100 } });
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.route("**/*", async route => {
    const request = route.request(), url = new URL(request.url());
    if (url.origin !== base) { externalRequests.push(request.url()); await route.abort(); return; }
    if (request.method() !== "GET" && url.pathname.startsWith("/api/")) nativeMutations.push({ method: request.method(), path: url.pathname, body: request.postData() });
    if (request.method() === "POST" && url.pathname === "/api/chat") chatRequests.push(request.postDataJSON());
    await route.continue();
  });
  await page.goto(base);
  await page.getByTestId("new-conversation").click();
  await page.waitForFunction(() => document.querySelector('[data-testid="application-profile-view"]')?.getAttribute("data-conversation-id"));
  const convId = await view().getAttribute("data-conversation-id");
  await openDetails(view().getByTestId("chat-context-controls"));
  await view().getByTestId("use-knowledge-context").check();
  await view().getByTestId("context-run").fill(run.run);
  await view().getByTestId("context-project").fill(target.id);
  await view().getByTestId("context-targets").fill(target.id);
  await view().getByTestId("context-budget").fill("1600");
  await view().getByTestId("context-counter-evidence").check();
  await view().getByTestId("context-lexical-shadow").check();
  await view().getByTestId("context-scan-limit").fill("2000");
  const channels = [{ id: "tfidf", limit: 25, min_score: 0 }, { id: "lexical", limit: 9, min_score: 0 }, { id: "not-installed-public-fixture", limit: 3 }];
  await check("channel validation precedes dispatch and native traces retain channels, counters and lexical shadow", async () => {
    await view().getByTestId("context-channels").fill('[{"id":"tfidf","limit":0}]');
    await view().getByTestId("chat-input").fill("NATIVE_CHANNELS_7214");
    await view().getByTestId("send-chat").click();
    assert.match(await view().getByTestId("chat-error").innerText(), /positive native integer/);
    assert.equal(chatRequests.length, 0); assert.equal(captures.length, 0);
    assert.equal(await view().getByTestId("chat-input").inputValue(), "NATIVE_CHANNELS_7214");
    await view().getByTestId("context-channels").fill(JSON.stringify(channels));
    await send("NATIVE_CHANNELS_7214", 1);
    const request = chatRequests[0].knowledge_context;
    assert.deepEqual(request.candidate_channels, channels);
    assert.equal(request.include_counter_evidence, true); assert.equal(request.lexical_shadow, true); assert.equal(request.candidate_scan_limit, 2000);
    const messages = await api("GET", `/api/conversations/${convId}/messages`);
    const trace = messages.find(message => message.text === "NATIVE_CHANNELS_7214").metadata.context_trace;
    assert.deepEqual(trace.knowledge_context_request.candidate_channels, channels.map(channel => ({ min_score: 0, ...channel })));
    assert.equal(trace.knowledge_context_request.include_counter_evidence, true); assert.equal(trace.knowledge_context_request.lexical_shadow, true);
    const retrieval = trace.knowledge_context.context_set.goal.params.candidate_retrieval;
    assert.notEqual(retrieval.channels.find(channel => channel.id === "tfidf").status, "unavailable");
    assert.equal(retrieval.channels.find(channel => channel.id === "lexical").method, "lexical_token_overlap");
    assert.equal(retrieval.channels.find(channel => channel.id === "not-installed-public-fixture").status, "unavailable");
    assert.equal(retrieval.lexical_shadow.selection_effect, "none");
    assert.ok(Object.hasOwn(trace.knowledge_context.context_set.goal.params, "counter_evidence"));
    assert.deepEqual(trace.messages, captures[0].messages);
  });
  await check("exact one-call override dispatches with an empty composer and invalid normal drafts, then normal controls rebuild once", async () => {
    await view().getByTestId("chat-input").fill("DRAFT_REPLACED_IN_ONE_CALL_3812");
    await openDetails(view().getByTestId("expert-chat-request"));
    const mutations = nativeMutations.length;
    await view().getByTestId("preview-chat-request").click();
    const prepared = JSON.parse(await view().getByTestId("one-call-request").inputValue());
    assert.equal(prepared.message, "DRAFT_REPLACED_IN_ONE_CALL_3812"); assert.deepEqual(prepared.knowledge_context.candidate_channels, channels);
    assert.equal(nativeMutations.length, mutations); assert.equal(captures.length, 1);
    const override = { message: "EXACT_OVERRIDE_5291", conv_id: convId, model: "mock/interface-two", temperature: 0.13,
      include_memory: false, include_graph_memory: false, include_history: false, trace_context: true };
    await view().getByTestId("one-call-request").fill(JSON.stringify(override, null, 2));
    await view().getByTestId("chat-input").fill("");
    await view().getByTestId("context-channels").fill("invalid normal channel JSON");
    await view().getByTestId("use-context-plan").check();
    await view().getByTestId("context-plan-thesis").first().getByTestId("thesis-text").fill("");
    await view().getByTestId("context-budget").fill("0");
    assert.equal(await view().getByTestId("send-chat").isEnabled(), true);
    await view().getByTestId("send-chat").click();
    await view().locator('[data-testid="message"][data-role="assistant"]').nth(1).waitFor({ timeout: 20000 });
    assert.deepEqual(chatRequests[1], override); assert.equal(captures[1].model, "mock/interface-two");
    assert.equal(await view().getByTestId("one-call-request").count(), 0);
    assert.equal(await view().getByTestId("context-channels").inputValue(), "invalid normal channel JSON");
    assert.equal(await view().getByTestId("use-context-plan").isChecked(), true);
    assert.equal(await view().getByTestId("send-chat").isDisabled(), true, "consumed override does not make an empty normal composer dispatchable");
    await view().getByTestId("chat-input").fill("NEXT_NORMAL_1425");
    await view().getByTestId("send-chat").click();
    assert.match(await view().getByTestId("chat-error").innerText(), /positive whole number/);
    assert.equal(chatRequests.length, 2);
    assert.equal(captures.length, 2);
    await view().getByTestId("context-budget").fill("1600");
    await view().getByTestId("context-channels").fill(JSON.stringify(channels));
    await view().getByTestId("use-context-plan").uncheck();
    await send("NEXT_NORMAL_1425", 3);
    assert.equal(chatRequests.length, 3); assert.equal(captures.length, 3);
    const next = chatRequests[2];
    assert.equal(next.message, "NEXT_NORMAL_1425"); assert.equal(next.temperature, undefined);
    assert.deepEqual(next.knowledge_context.candidate_channels, channels);
    assert.equal(captures[2].model, "mock/interface-one");
  });
  await check("independent view context settings persist through reload without execution", async () => {
    await page.getByTestId("add-profile-view").click();
    const sibling = () => page.getByTestId("application-profile-view").nth(1);
    await openDetails(sibling().getByTestId("chat-context-controls"));
    assert.equal(await sibling().getByTestId("use-knowledge-context").isChecked(), false);
    await sibling().getByTestId("use-knowledge-context").check();
    await sibling().getByTestId("context-budget").fill("2750");
    await sibling().getByTestId("context-channels").fill('[{"id":"lexical","limit":17}]');
    await sibling().getByTestId("context-counter-evidence").uncheck();
    await sibling().getByTestId("context-lexical-shadow").uncheck();
    await sibling().getByTestId("model-picker").selectOption("mock/interface-two");
    const siblingId = await sibling().getAttribute("data-profile-view-id");
    await page.reload();
    await page.getByTestId("application-profile-view").nth(1).waitFor();
    await openDetails(view().getByTestId("chat-context-controls"));
    await openDetails(sibling().getByTestId("chat-context-controls"));
    assert.equal(await view().getByTestId("context-budget").inputValue(), "1600");
    assert.equal(await view().getByTestId("context-channels").inputValue(), JSON.stringify(channels));
    assert.equal(await view().getByTestId("context-counter-evidence").isChecked(), true);
    assert.equal(await view().getByTestId("context-lexical-shadow").isChecked(), true);
    assert.equal(await sibling().getAttribute("data-profile-view-id"), siblingId);
    assert.equal(await sibling().getByTestId("context-budget").inputValue(), "2750");
    assert.equal(await sibling().getByTestId("model-picker").inputValue(), "mock/interface-two");
    assert.equal(await sibling().getByTestId("context-counter-evidence").isChecked(), false);
    assert.equal(captures.length, 3); assert.equal(chatRequests.length, 3);
  });
  await check("native imported descendant paths navigate read-only with unchanged message statuses", async () => {
    const conversations = await api("GET", "/api/conversations?limit=100");
    let selected;
    for (const conversation of conversations.filter(item => item.source?.startsWith("import:"))) {
      const rows = await api("GET", `/api/conversations/${conversation.id}/messages?all=1`);
      const byId = new Map(rows.map(row => [row.id, row]));
      for (const row of rows.filter(item => item.status !== "active")) {
        const ancestors = []; const seen = new Set(); let current = row;
        while (current && !seen.has(current.id)) { seen.add(current.id); ancestors.unshift(current); current = byId.get(current.parent_id); }
        if (ancestors.length >= 3 && (!selected || ancestors.length > selected.path.length)) selected = { conversation, rows, leaf: row, path: ancestors };
      }
    }
    assert.ok(selected, "public native fixture contains a retained descendant path");
    await chooseConversation(selected.conversation.id);
    const mutations = nativeMutations.length;
    await view().getByTestId("show-conversation-branches").check();
    await openDetails(view().getByTestId("conversation-branches").locator("details"));
    const node = view().getByTestId("conversation-branches").locator(`[data-message-id="${selected.leaf.id}"] button`);
    await node.waitFor(); await node.click();
    assert.equal(await view().getByTestId("message").count(), selected.path.length);
    assert.deepEqual(await view().getByTestId("message").evaluateAll(elements => elements.map(element => element.getAttribute("data-status"))), selected.path.map(row => row.status));
    await view().getByTestId("chat-input").fill("READ_ONLY_BRANCH_DRAFT_1093");
    assert.equal(await view().getByTestId("send-chat").isDisabled(), true);
    assert.equal(await view().getByTestId("preview-chat-request").isDisabled(), true);
    assert.equal(nativeMutations.length, mutations); assert.equal(captures.length, 3);
    assert.deepEqual(await api("GET", `/api/conversations/${selected.conversation.id}/messages?all=1`), selected.rows);
    await view().getByTestId("return-current-branch").click();
    assert.equal(await view().getByTestId("send-chat").isEnabled(), true);
  });
  await check("native workflow snapshot restores original source and validated trace after browser storage reset", async () => {
    const definition = JSON.parse(readFileSync(path.join(webRoot, "src/profiles/data/loom-default-r2.json"), "utf8"));
    definition.id = "public-native-snapshot"; definition.profile_revision = 1; definition.label = "Public snapshot fixture";
    const source = `\uFEFF \n${JSON.stringify(definition, null, 2)}\n`;
    await view().getByLabel("Import application profile", { exact: true }).setInputFiles({ name: "public-native-profile.json", mimeType: "application/json", buffer: Buffer.from(source) });
    await page.waitForFunction(() => document.querySelector('[data-testid="application-profile-view"]')?.getAttribute("data-profile-id") === "public-native-snapshot");
    await openDetails(view().locator("details.profile-details"));
    await view().getByTestId("profile-workflow").getByRole("button", { name: "New conversation", exact: true }).click();
    await view().getByTestId("profile-workflow").getByText("conversation-review: chat", { exact: true }).waitFor();
    const recovery = page.getByTestId("workflow-recovery-panel"); await openDetails(recovery);
    await recovery.getByLabel("Workflow snapshot label", { exact: true }).fill("Public native recovery fixture");
    await recovery.getByTestId("save-workflow-snapshot").click();
    await recovery.getByRole("status").filter({ hasText: "saved and read back" }).waitFor();
    const config = await api("GET", "/api/config");
    const store = config.application_workflow_snapshots_v1;
    assert.equal(store.schema, "loom.application_workflow_snapshots/1"); assert.equal(store.snapshots.length, 1);
    const snapshot = store.snapshots[0];
    const identity = JSON.stringify([definition.id, definition.profile_revision]);
    assert.deepEqual(snapshot.profiles.find(profile => profile.id === definition.id), definition);
    assert.equal(snapshot.sources.find(item => item.profile === identity).text, source);
    const session = snapshot.sessions.find(entry => entry.profile === identity);
    assert.equal(session.session.sequence, 1); assert.equal(session.session.history.length, 1);
    assert.equal(session.session.variables["conversation-review"].conversation_id, snapshot.sharedConversationId);
    const mutations = nativeMutations.length;
    await page.evaluate(() => localStorage.clear()); await page.reload();
    assert.equal(await view().getAttribute("data-profile-id"), "loom-default");
    await openDetails(page.getByTestId("workflow-recovery-panel"));
    await page.getByTestId("load-workflow-snapshots").click();
    await page.getByTestId("restore-workflow-snapshot").waitFor();
    await page.getByTestId("restore-workflow-snapshot").click();
    await page.waitForFunction(() => document.querySelector('[data-testid="application-profile-view"]')?.getAttribute("data-profile-id") === "public-native-snapshot");
    const restored = await page.evaluate(() => ({ views: JSON.parse(localStorage.getItem("loom.application.views.v1")), sessions: Object.fromEntries(Object.keys(localStorage).filter(key => key.startsWith("loom.application.workflow.v1:")).map(key => [key, JSON.parse(localStorage.getItem(key))])) }));
    assert.equal(restored.views.sources.find(item => item.profile === identity).text, source);
    const sessionKey = `loom.application.workflow.v1:${JSON.stringify([session.viewId, definition.id, definition.profile_revision])}`;
    const checkpoint = restored.sessions[`${sessionKey}:checkpoint`];
    assert.equal(checkpoint.schema, "loom.workflow_checkpoint/1");
    assert.deepEqual(checkpoint.entry.session, session.session);
    assert.deepEqual(checkpoint.entry.recovery, session.recovery);
    assert.equal(checkpoint.entry.profile, session.profile);
    assert.equal(checkpoint.entry.viewId, session.viewId);
    assert.equal(await page.getByTestId("application-profile-view").count(), snapshot.views.length);
    assert.equal(nativeMutations.length, mutations, "restore must not dispatch an adapter or native write");
    assert.deepEqual((await api("GET", "/api/config")).application_workflow_snapshots_v1, store);
    assert.equal(captures.length, 3);
  });
  await check("media capability read and failed multipart transcription clean uploaded temporary files", async () => {
    const status = await api("GET", "/api/media/status");
    assert.ok(Array.isArray(status.asr.available)); assert.equal(status.asr.configured, false);
    const before = readdirSync(mediaTemp).sort();
    const upload = new FormData(); upload.append("file", new Blob(["public invalid audio bytes"]), "../../untrusted.wav");
    upload.append("options", JSON.stringify({ provider: "nonexistent-public-provider", language: "en" }));
    const response = await fetch(`${base}/api/media/transcribe`, { method: "POST", body: upload });
    const result = await response.json(); assert.ok(["unavailable", "not_found"].includes(result.error?.code), JSON.stringify(result));
    assert.deepEqual(readdirSync(mediaTemp).sort(), before, "temporary upload is removed after native provider failure");
    const invalid = new FormData(); invalid.append("file", new Blob(["x"]), "file.wav"); invalid.append("options", "[]");
    const invalidResult = await (await fetch(`${base}/api/media/transcribe`, { method: "POST", body: invalid })).json();
    assert.equal(invalidResult.error?.code, "invalid_argument"); assert.deepEqual(readdirSync(mediaTemp).sort(), before);
    assert.equal(captures.length, 3);
  });
  await check("native config rejects malformed/nonobject PATCH and malformed/nonobject/missing-value PUT without changing stored values", async () => {
    const before = await api("GET", "/api/config");
    const invalid = [
      { method: "PATCH", endpoint: "/api/config", body: '{"temperature":0.33,"truncated":', code: "parse" },
      { method: "PATCH", endpoint: "/api/config", body: "[]", code: "invalid_argument" },
      { method: "PATCH", endpoint: "/api/config", body: "null", code: "invalid_argument" },
      { method: "PUT", endpoint: "/api/config/temperature", body: "{invalid", code: "parse" },
      { method: "PUT", endpoint: "/api/config/temperature", body: "[]", code: "invalid_argument" },
      { method: "PUT", endpoint: "/api/config/temperature", body: "{}", code: "invalid_argument" },
    ];
    for (const { method, endpoint, body, code } of invalid) {
      const response = await fetch(`${base}${endpoint}`, { method, headers: { "Content-Type": "application/json" }, body });
      const result = await response.json();
      assert.ok(!response.ok, `${method} ${endpoint} invalid envelope must fail`);
      assert.equal(result.error?.code, code);
      assert.deepEqual(await api("GET", "/api/config"), before, `${method} ${endpoint} must preserve all original config`);
    }
    assert.equal(captures.length, 3); assert.equal(chatRequests.length, 3);
  });
  await check("usage-policy malformed envelopes fail before dispatch and native capability is explicit", async () => {
    const before = await api("GET", "/api/config");
    for (const [body, code] of [["{invalid", "parse"], ["[]", "invalid_argument"]]) {
      const response = await fetch(`${base}/api/usage-policy`, { method: "POST", headers: { "Content-Type": "application/json" }, body });
      const result = await response.json(); assert.equal(result.error?.code, code); assert.ok(!response.ok);
    }
    const response = await fetch(`${base}/api/usage-policy`, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ action: "settings" }) });
    const settings = await response.json();
    if (settings.error) assert.equal(settings.error.code, "unavailable");
    else { assert.ok(settings.effective && typeof settings.effective === "object"); assert.ok(settings.capabilities && typeof settings.capabilities === "object"); }
    await page.getByTestId("nav-settings").click();
    await page.getByTestId("usage-policy-panel").waitFor();
    if (settings.error) { await page.getByTestId("usage-policy-error").waitFor(); assert.match(await page.getByTestId("usage-policy-error").innerText(), /not installed|unavailable/i); }
    else await page.getByTestId("usage-effective").waitFor();
    assert.deepEqual(await api("GET", "/api/config"), before);
    assert.equal(captures.length, 3); assert.equal(chatRequests.length, 3);
  });
  assert.deepEqual(externalRequests, []); assert.deepEqual(pageErrors, []);
  console.log(`[interface-2-native] ${groups.length}/${groups.length} groups passed; ${captures.length} local fake-provider calls; 0 external requests`);
} catch (error) {
  failure = error instanceof Error ? error.stack : String(error);
  console.error(serverLog);
  if (evidenceDir && page) await page.screenshot({ path: path.join(evidenceDir, "failure.png"), fullPage: true }).catch(() => {});
  throw error;
} finally {
  await browser?.close();
  server.kill("SIGTERM");
  await new Promise(resolve => { if (server.exitCode !== null) resolve(); else server.once("exit", resolve); });
  await new Promise(resolve => provider.close(resolve));
  if (evidenceDir) {
    writeFileSync(path.join(evidenceDir, "server.log"), serverLog, { flag: "wx" });
    writeFileSync(path.join(evidenceDir, "results.json"), JSON.stringify({ status: failure ? "failed" : "passed", groups, failure,
      external_requests: externalRequests, page_errors: pageErrors, browser_chat_requests: chatRequests, provider_requests: captures,
      native_mutations: nativeMutations }, null, 2) + "\n", { flag: "wx" });
  }
  rmSync(dataDir, { recursive: true, force: true });
}
