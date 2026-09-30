#!/usr/bin/env node
// Real Chromium -> real Loom HTTP/C++ chat path -> local fake provider.
// Uses only the authored synthetic_dev export. No remote model or paid call.
// Build web + loom-server first; optionally set LOOM_SERVER_BIN and
// PLAYWRIGHT_CHROMIUM_EXECUTABLE. Run: node e2e/chat-context.mjs
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const loomRoot = path.resolve(webRoot, "..");
const serverBin = process.env.LOOM_SERVER_BIN || path.join(loomRoot, "build/dev/server/loom-server");
assert.ok(existsSync(serverBin), `Build loom-server first: ${serverBin}`);
assert.ok(existsSync(path.join(webRoot, "dist/index.html")), "Run npm run build first");
const dataDir = mkdtempSync(path.join(tmpdir(), "loom-chat-context-"));
const captures = [];
const provider = createServer(async (req, res) => {
  if (req.method !== "POST") { res.writeHead(404).end(); return; }
  const chunks = [];
  for await (const chunk of req) chunks.push(chunk);
  const body = JSON.parse(Buffer.concat(chunks).toString());
  captures.push(body);
  if (body.messages.some((message) => message.role === "user" && message.content === "FAILED_TURN_9046")) {
    res.writeHead(503, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: { message: "Deliberate local provider failure" } }));
    return;
  }
  const content = `Local context test reply ${captures.length}`;
  if (body.stream) {
    res.writeHead(200, { "Content-Type": "text/event-stream" });
    res.write(`data: ${JSON.stringify({ choices: [{ delta: { content } }] })}\n\n`);
    res.end("data: [DONE]\n\n");
  } else {
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ choices: [{ message: { role: "assistant", content } }], usage: { prompt_tokens: 1, completion_tokens: 1 } }));
  }
});
await new Promise((resolve) => provider.listen(0, "127.0.0.1", resolve));
const providerBase = `http://127.0.0.1:${provider.address().port}`;
const portProbe = createServer();
await new Promise((resolve) => portProbe.listen(0, "127.0.0.1", resolve));
const serverPort = portProbe.address().port;
await new Promise((resolve) => portProbe.close(resolve));
const base = `http://127.0.0.1:${serverPort}`;
let serverLog = "";
const server = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(serverPort), "--data-dir", dataDir, "--static-dir", path.join(webRoot, "dist")], { cwd: loomRoot, stdio: ["ignore", "pipe", "pipe"] });
for (const stream of [server.stdout, server.stderr]) stream.on("data", (chunk) => { serverLog += chunk.toString(); });
let browser;

async function api(method, endpoint, body) {
  const response = await fetch(`${base}${endpoint}`, { method, headers: { "Content-Type": "application/json" }, ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  const result = await response.json();
  assert.ok(response.ok && !(result.error && typeof result.error === "object"), `${endpoint}: ${JSON.stringify(result)}`);
  return result;
}
async function waitForServer() {
  const deadline = Date.now() + 20000;
  while (Date.now() < deadline) {
    try { if ((await fetch(`${base}/api/healthz`)).ok) return; } catch { /* still starting */ }
    if (server.exitCode !== null) throw new Error(`Loom exited: ${serverLog}`);
    await new Promise((resolve) => setTimeout(resolve, 100));
  }
  throw new Error(`Loom startup timeout: ${serverLog}`);
}

try {
  await waitForServer();
  await api("PATCH", "/api/config", { base_url: providerBase, default_model: "mock/context", semantic_analysis: false, stream: false });
  await api("POST", "/api/secrets/api_key", { value: "local-fake-provider-only" });
  await api("POST", "/api/memory", { content: "MEMORY_SENTINEL_4729", active: true });
  const run = await api("POST", "/api/knowledge/run", { sources: [path.join(loomRoot, "tests/fixtures/eval/synthetic_dev/chatgpt_export.zip")], llm: "off", priors: false });
  assert.equal(run.status, "done", "synthetic knowledge run completed without models");
  const entities = await api("POST", "/api/knowledge/query", { what: "entities", run: run.run, limit: 1000 });
  const target = entities.items.find((item) => item.kind === "project") || entities.items[0];
  assert.ok(target?.id, "synthetic run contains a selectable entity");
  const claims = await api("POST", "/api/knowledge/query", { what: "claims", run: run.run, limit: 1000 });
  assert.ok(claims.items[0]?.id, "synthetic run contains an explicit claim anchor");
  const claimId = claims.items[0].id;
  const configBefore = await api("GET", "/api/config");
  assert.equal(captures.length, 0, "knowledge preparation performs no model requests");

  browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}) });
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 } });
  const errors = [], unexpected = [], chatRequests = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.route("**/*", async (route) => {
    const req = route.request();
    if (new URL(req.url()).origin !== base) { unexpected.push(req.url()); await route.abort(); return; }
    if (req.method() === "POST" && new URL(req.url()).pathname === "/api/chat") chatRequests.push(req.postDataJSON());
    await route.continue();
  });
  await page.goto(base);
  await page.getByTestId("new-conversation").click();
  await page.locator('[data-testid="conv-item"].active').waitFor();
  async function send(text, count) {
    await page.getByTestId("chat-input").fill(text);
    await page.getByTestId("send-chat").click();
    await page.locator('[data-testid="message"][data-role="assistant"]').nth(count - 1).waitFor({ timeout: 20000 });
    assert.equal(await page.getByTestId("chat-error").count(), 0);
  }

  await send("HISTORY_SENTINEL_9831", 1);
  assert.equal(captures.length, 1);
  assert.ok(captures[0].messages.some((m) => String(m.content).includes("MEMORY_SENTINEL_4729")), "default includes active memory");
  assert.equal(chatRequests[0].knowledge_context, undefined, "default request leaves knowledge opt-in absent");
  assert.equal(chatRequests[0].include_history, undefined);
  assert.equal(await page.getByTestId("context-trace").count(), 0, "default does not create an invented trace");

  await page.getByTestId("chat-context-controls").locator(":scope > summary").click();
  for (const id of ["include-memory", "include-graph-memory", "include-history"]) await page.getByTestId(id).uncheck();
  await page.getByTestId("use-knowledge-context").check();
  await page.getByTestId("context-project").fill(target.id);
  await page.getByTestId("context-targets").fill(`${target.id}, ${target.id}`);
  await page.getByTestId("context-run").fill(run.run);
  await page.getByTestId("context-language").fill("en");
  await page.getByTestId("context-hops").fill("2");
  await page.getByTestId("context-detail").selectOption("full");
  await page.getByTestId("context-budget").fill("0");
  await page.getByTestId("chat-input").fill("CURRENT_TURN_5128");
  await page.getByTestId("send-chat").click();
  assert.match(await page.getByTestId("chat-error").innerText(), /positive whole number/);
  assert.equal(captures.length, 1, "invalid budget is rejected before sending");
  assert.equal(await page.getByTestId("chat-input").inputValue(), "CURRENT_TURN_5128", "invalid options preserve draft");
  await page.getByTestId("context-budget").fill("1200");
  await page.getByTestId("use-context-plan").check();
  await page.getByTestId("context-plan-id").fill("authored-browser-plan");
  await page.getByTestId("context-plan-source").fill('{"note":"synthetic browser outline","verified":false}');
  const thesis = (index) => page.getByTestId("context-plan-thesis").nth(index);
  await thesis(0).getByTestId("thesis-text").fill("Implement the project preserving its stated constraints");
  await thesis(0).locator("summary").click();
  await thesis(0).getByTestId("thesis-claims-mode").selectOption("custom");
  await thesis(0).getByTestId("thesis-claims").fill(claimId);
  await thesis(0).getByTestId("thesis-weight").fill("2");
  await page.getByTestId("add-context-thesis").click();
  await thesis(1).getByTestId("thesis-text").fill("  Inspect recorded alternatives, not inferred contradictions.  ");
  await thesis(1).locator("summary").click();
  await thesis(1).getByTestId("thesis-targets-mode").selectOption("custom");
  await thesis(1).getByTestId("thesis-hops").fill("0");
  await thesis(1).getByTestId("thesis-detail").selectOption("summary");
  await thesis(1).getByTestId("thesis-counter").uncheck();
  await thesis(1).getByTestId("thesis-id").fill("thesis-1");
  await page.getByTestId("send-chat").click();
  assert.match(await page.getByTestId("chat-error").innerText(), /duplicate thesis ID/);
  assert.equal(chatRequests.length, 1, "invalid authored plan is rejected before native request");
  assert.equal(await page.getByTestId("chat-input").inputValue(), "CURRENT_TURN_5128");
  await thesis(1).getByTestId("thesis-id").fill("thesis-2");
  const plan = { id: "authored-browser-plan", source_ref: { note: "synthetic browser outline", verified: false }, theses: [
    { id: "thesis-1", text: "Implement the project preserving its stated constraints", claims: [claimId], require_counter_evidence: true, budget_weight: 2 },
    { id: "thesis-2", text: "  Inspect recorded alternatives, not inferred contradictions.  ", targets: [], relation_hops: 0, detail_resolution: "summary", require_counter_evidence: false, budget_weight: 1 },
  ] };

  // Keep W5 visible with the chat. A saved independent reference and profile
  // remain presentation state; they must never become chat request options.
  await page.getByTestId("nav-knowledge").click();
  const graph = () => page.getByTestId("kb-pane-graph");
  await graph().first().locator(".kb-node").first().waitFor();
  await graph().first().getByLabel("Graph focus depth").fill("3");
  await graph().first().getByRole("button", { name: "Duplicate Knowledge graph view", exact: true }).click();
  await graph().last().locator(".kb-node").first().waitFor();
  const referenceId = await graph().last().getAttribute("data-view-id");
  await page.getByLabel("View profile", { exact: true }).selectOption("compact");
  await page.getByRole("button", { name: "Save perspective", exact: true }).click();
  const workspace = () => page.evaluate(() => JSON.parse(localStorage.getItem("loom.knowledge.workspace.v2")));
  const savedWorkspace = await workspace();
  assert.equal(savedWorkspace.panes.find((p) => p.id === referenceId).followRun, false);
  await graph().first().getByLabel("Graph focus depth").fill("1");
  await page.getByRole("button", { name: "Restore perspective", exact: true }).click();
  await graph().first().locator(".kb-node").first().waitFor();
  assert.deepEqual(await workspace(), savedWorkspace);
  assert.ok(await page.getByTestId("chat-input").isVisible(), "chat coexists with workbench");
  assert.equal(await thesis(1).getByTestId("thesis-detail").inputValue(), "summary", "perspective restore does not alter plan draft");
  assert.deepEqual(await api("GET", "/api/config"), configBefore);
  assert.equal(captures.length, 1, "editing plan and perspective makes no provider requests");
  await send("CURRENT_TURN_5128", 2);
  assert.equal(captures.length, 2, "one local provider call per submitted turn");
  assert.deepEqual(chatRequests[1].knowledge_context, { project: target.id, targets: [target.id, target.id], budget_tokens: 1200, run: run.run, lang: "en", relation_hops: 2, detail_resolution: "full", plan });
  for (const key of ["include_memory", "include_graph_memory", "include_history"]) assert.equal(chatRequests[1][key], false);
  const activeConv = await api("GET", "/api/conversations?limit=1");
  const convId = activeConv[0].id;
  const messages = await api("GET", `/api/conversations/${convId}/messages`);
  const user = messages.find((m) => m.text === "CURRENT_TURN_5128");
  const trace = user.metadata.context_trace;
  assert.equal(trace.kind, "compiled_messages");
  assert.equal(trace.knowledge_context_request.text, "CURRENT_TURN_5128", "empty selection query resolves to current message");
  assert.equal(trace.knowledge_context_request.run, run.run);
  assert.equal(trace.knowledge_context_request.relation_hops, 2);
  assert.equal(trace.knowledge_context_request.detail_resolution, "full");
  assert.deepEqual(trace.knowledge_context_request.plan, plan, "caller-authored plan retained without hidden task inference");
  const planTrace = trace.knowledge_context.context_set.goal.params.plan_trace;
  assert.equal(planTrace.schema, "loom.context_plan_trace/1");
  assert.equal(planTrace.run, run.run);
  assert.deepEqual(planTrace.plan, plan);
  assert.equal(planTrace.theses.length, 2);
  assert.equal(planTrace.theses[0].relation_hops, 2, "omitted per-thesis reach inherits request");
  assert.equal(planTrace.theses[0].detail_resolution, "full");
  assert.deepEqual(planTrace.theses[0].claims, [claimId]);
  assert.deepEqual(planTrace.theses[1].targets, [], "explicit empty clears inherited entity anchors");
  assert.equal(planTrace.theses[1].relation_hops, 0);
  assert.equal(planTrace.theses[1].detail_resolution, "summary");
  assert.equal(planTrace.theses[1].require_counter_evidence, false);
  assert.deepEqual(await workspace(), savedWorkspace, "sending does not alter saved workspace");
  assert.deepEqual(trace.messages, captures[1].messages, "persisted trace equals actual provider messages");
  assert.equal(trace.selection.include_memory, false);
  assert.equal(trace.selection.include_graph_memory, false);
  assert.equal(trace.selection.include_history, false);
  assert.ok(trace.knowledge_context.context_set);
  assert.ok(trace.messages.some((m) => m.content === trace.knowledge_context.prompt), "real context prompt enters request");
  assert.ok(!JSON.stringify(trace.messages).includes("MEMORY_SENTINEL_4729"));
  assert.ok(!JSON.stringify(trace.messages).includes("HISTORY_SENTINEL_9831"));
  assert.equal(trace.messages.filter((m) => m.role === "user" && m.content === "CURRENT_TURN_5128").length, 1);
  await page.getByTestId("context-trace").locator("summary").first().click();
  assert.deepEqual(JSON.parse(await page.getByTestId("context-trace-json").first().innerText()), trace, "inspector displays actual recorded trace");

  // Independent controls restore legacy sources without retaining the enabled
  // knowledge query; explicit recording still works for that legacy request.
  await page.getByTestId("use-knowledge-context").uncheck();
  for (const id of ["include-memory", "include-graph-memory", "include-history"]) await page.getByTestId(id).check();
  await page.getByTestId("record-context").selectOption("on");
  await send("THIRD_TURN_8163", 3);
  assert.equal(chatRequests[2].knowledge_context, undefined);
  assert.equal(chatRequests[2].trace_context, true);
  assert.ok(JSON.stringify(captures[2].messages).includes("MEMORY_SENTINEL_4729"));
  assert.ok(JSON.stringify(captures[2].messages).includes("HISTORY_SENTINEL_9831"));
  await page.getByTestId("use-knowledge-context").check();
  await page.getByTestId("record-context").selectOption("off");
  await send("UNRECORDED_TURN_3042", 4);
  assert.equal(chatRequests[3].trace_context, false, "explicit off overrides automatic recording for custom context");
  assert.ok(chatRequests[3].knowledge_context);
  const updatedMessages = await api("GET", `/api/conversations/${convId}/messages`);
  assert.equal(updatedMessages.find((m) => m.text === "UNRECORDED_TURN_3042").metadata?.context_trace, undefined);
  await page.getByTestId("record-context").selectOption("on");
  await page.getByTestId("chat-input").fill("FAILED_TURN_9046");
  await page.getByTestId("send-chat").click();
  await page.getByTestId("chat-error").waitFor();
  const failedTurn = page.locator('[data-testid="message"][data-role="user"]').filter({ hasText: "FAILED_TURN_9046" });
  await failedTurn.getByTestId("context-trace").waitFor();
  assert.equal(captures.length, 5, "failed provider request is not silently retried");
  await page.reload();
  await page.getByTestId("conv-item").first().click();
  await page.getByTestId("context-trace").nth(2).waitFor();
  assert.equal(await page.getByTestId("context-trace").count(), 3, "successful and failed request traces survive reload through native metadata");
  await page.getByTestId("nav-knowledge").click();
  await graph().first().locator(".kb-node").first().waitFor();
  assert.deepEqual(await workspace(), savedWorkspace, "native turns and reload preserve perspective identities and settings");
  assert.equal(await graph().last().getAttribute("data-view-id"), referenceId);
  await page.setViewportSize({ width: 390, height: 844 });
  if (!(await page.getByTestId("sidebar").getAttribute("class")).includes("collapsed")) await page.getByTestId("toggle-sidebar").click();
  await page.getByTestId("chat-context-controls").locator(":scope > summary").click();
  await page.getByTestId("use-knowledge-context").check();
  assert.equal(await page.getByTestId("use-context-plan").isChecked(), false, "workspace restore does not silently enable a plan after reload");
  await page.getByTestId("use-context-plan").check();
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), true, "mobile controls do not overflow page");
  assert.equal(captures.length, 5, "inspection/options/reload make no provider calls");
  assert.deepEqual(errors, []);
  assert.deepEqual(unexpected, []);
  console.log("[chat-context] PASS: default path, native authored multi-thesis plan, independent sources, validation, exact provider trace, W5 perspective coexistence, recording opt-out, failure inspection, persisted inspection, and mobile layout; 5 local fake-provider calls, 0 remote calls");
} catch (error) {
  console.error(serverLog);
  throw error;
} finally {
  if (browser) await browser.close();
  server.kill("SIGTERM");
  await new Promise((resolve) => { if (server.exitCode !== null) resolve(); else server.once("exit", resolve); });
  await new Promise((resolve) => provider.close(resolve));
  rmSync(dataDir, { recursive: true, force: true });
}
