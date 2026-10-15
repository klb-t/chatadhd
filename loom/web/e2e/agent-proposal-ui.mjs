#!/usr/bin/env node
// Real ChatView + LoomHttpApi against an authored backend: an agent run stopped on proposals (proposals[], run_id) is shown in the
// conversation; accepting / rejecting each resumes the run with the transcript and the answers (agent.run `resume`).
// The Polish words come from the same data catalog (presentation.ui_shell), checked against the pack.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createServer } from "node:http";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import { chromium } from "playwright";
import { closeHttpFixture, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const root = fileURLToPath(new URL("../", import.meta.url));
const pack = JSON.parse(readFileSync(new URL("../src/onboarding/generated/conversation-view.json", import.meta.url), "utf8"));
const shell = pack.entries.find(entry => entry.key === "presentation.ui_shell").value.locales;
const groups = [], resumes = [], patches = [], pageErrors = [];
const complete = suiteCompletionGuard("agent-proposal-ui", 3, groups);
let failNext = false;
const transcript = [{ role: "user", content: "tidy" }, { role: "assistant", content: "", tool_calls: [{ id: "t1" }, { id: "t2" }] },
  { role: "tool", tool_call_id: "t1", content: "{\"status\":\"proposed\",\"run_id\":\"ar_a\"}" },
  { role: "tool", tool_call_id: "t2", content: "{\"status\":\"proposed\",\"run_id\":\"ar_b\"}" }];
const messages = [{ id: "assistant-1", conv_id: "conv-1", role: "assistant", text: "The agent wants to run 2 actions.", status: "active", created: "2026-10-15T10:00:00Z", model: null,
  storage: "native", capabilities: { edit: true, set_status: true, restore: true, native_lookup: true },
  metadata: { action_run: { run_id: "ar_loop", status: "completed", lang: "en" }, agent_loop: { status: "needs_confirmation", loop_id: "al_1", messages: transcript,
    proposals: [{ action: "memory.add", arguments: { text: "x" }, run_id: "ar_a", status: "proposed", tool_call_id: "t1" },
      { action: "storage.sync", arguments: {}, run_id: "ar_b", status: "awaiting_confirmation", tool_call_id: "t2" }] } } }];

const bundle = await build({ stdin: { contents: `
  import React from "react";
  import { createRoot } from "react-dom/client";
  import ChatView from "./src/components/ChatView";
  import { ShellText, shellTranslator } from "./src/shell/text";
  import pack from "./src/onboarding/generated/conversation-view.json";
  const locales = pack.entries.find(entry => entry.key === "presentation.ui_shell").value.locales;
  const feature = { locale: "en", catalog: locales.en, locales };
  createRoot(document.getElementById("root")).render(<ShellText.Provider value={shellTranslator(feature, "en")}><ChatView convId="conv-1" onConversationCreated={() => {}} /></ShellText.Provider>);
`, resolveDir: root, loader: "tsx" }, bundle: true, write: false, format: "iife", platform: "browser",
  outfile: "/tmp/loom-agent-proposal-ui.js", jsx: "automatic", loader: { ".json": "json" } });
const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
const css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";
const json = (response, value, status = 200) => { response.writeHead(status, { "Content-Type": "application/json" }); response.end(JSON.stringify(value)); };
const view = id => ({ schema: "loom.conversation_view/1", conversation_id: id, view_id: "authored:" + id + ":" + JSON.stringify(messages).length, status: "complete",
  messages: messages.map(row => ({ ...row })), omissions: [], resources: [], read_configuration: { values: {}, value_schema: { properties: {} } },
  capabilities: { source_history_send: { available: true, reason: "not_applicable" } } });
const server = createServer(async (request, response) => {
  const url = new URL(request.url, "http://fixture.local");
  if (url.pathname === "/component.js") { response.writeHead(200, { "Content-Type": "text/javascript" }); response.end(js); return; }
  if (url.pathname === "/component.css") { response.writeHead(200, { "Content-Type": "text/css" }); response.end(css); return; }
  if (url.pathname === "/") { response.writeHead(200, { "Content-Type": "text/html" });
    response.end('<!doctype html><html><head><link rel="stylesheet" href="/component.css"></head><body><div id="root"></div><script src="/component.js"></script></body></html>'); return; }
  const chunks = []; for await (const chunk of request) chunks.push(chunk);
  const body = chunks.length ? JSON.parse(Buffer.concat(chunks).toString()) : undefined;
  if (/^\/api\/conversations\/[^/]+\/view$/.test(url.pathname)) return json(response, view("conv-1"));
  if (/^\/api\/conversations\/[^/]+\/messages/.test(url.pathname)) return json(response, messages);
  if (url.pathname === "/api/models") return json(response, []);
  if (url.pathname === "/api/agent/run" && request.method === "POST") {
    resumes.push(body);
    if (failNext) { failNext = false; return json(response, { error: { code: "conflict", message: "run is gone" } }, 409); }
    return json(response, { schema: "loom.agent_loop/1", status: "finished", text: "Done.", proposals: [], messages: [] });
  }
  if (/^\/api\/messages\/[^/]+$/.test(url.pathname) && request.method === "PATCH") {
    patches.push(body);
    Object.assign(messages[0], { metadata: body.metadata });
    return json(response, messages[0]);
  }
  json(response, { error: { code: "fixture_unsupported", message: "Unsupported fixture endpoint" } }, 404);
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const base = "http://127.0.0.1:" + server.address().port;
let browser, page;
async function check(name, run) { await run(); groups.push(name); console.log("[agent-proposal-ui] PASS " + name); }
async function waitFor(predicate, label) {
  const deadline = Date.now() + 10000;
  while (!predicate()) { if (Date.now() > deadline) throw new Error("Timed out waiting for " + label); await new Promise(resolve => setTimeout(resolve, 20)); }
}
const block = () => page.getByTestId("chat-agent-proposal");
try {
  const cachedShell = "/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell";
  const { existsSync } = await import("node:fs");
  const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || (existsSync(cachedShell) ? cachedShell : undefined);
  browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  page = await browser.newPage();
  page.on("pageerror", error => pageErrors.push(error.message));
  page.on("console", m => { if (process.env.DEBUG_E2E) console.log("[console]", m.text()); });
  page.on("pageerror", error => { if (process.env.DEBUG_E2E) console.log("[pageerror]", error.message); });
  await page.goto(base.replace(/\/?$/, "/#warsztat"));
  await block().waitFor();

  await check("the stopped agent shows its proposals with run ids and the two answers; Polish words exist in the same catalog", async () => {
    assert.equal(await block().getAttribute("data-loop-id"), "al_1");
    assert.equal(await block().getByTestId("chat-agent-proposal-item").count(), 2);
    assert.equal(await block().getByTestId("chat-agent-proposal-action").first().innerText(), "memory.add");
    assert.match(await block().getByTestId("chat-agent-proposal-run").first().innerText(), /ar_a/);
    assert.equal(await block().getByTestId("chat-agent-proposal-accept").first().innerText(), shell.en.agent_accept);
    assert.equal(await block().getByTestId("chat-agent-proposal-reject").first().innerText(), shell.en.agent_reject);
    for (const key of ["agent_title", "agent_accept", "agent_reject", "agent_failed", "agent_resuming"])
      assert.ok(shell.pl[key] && shell.pl[key] !== shell.en[key], "Polish text for " + key);
    assert.equal(resumes.length, 0, "nothing resumes before every proposal has an answer");
  });

  await check("a failed resume leaves the proposals open and says why", async () => {
    failNext = true;
    await block().getByTestId("chat-agent-proposal-accept").first().click();
    assert.equal(resumes.length, 0);
    await block().getByTestId("chat-agent-proposal-reject").last().click();
    await waitFor(() => resumes.length === 1, "the failed resume");
    await block().getByTestId("chat-agent-proposal-failure").waitFor();
    assert.match(await block().getByTestId("chat-agent-proposal-failure").innerText(), /run is gone/);
    assert.equal(await block().getByTestId("chat-agent-proposal-accept").count(), 2);
  });

  await check("accept + reject resume the run with the transcript and both answers; the block gives way", async () => {
    await block().getByTestId("chat-agent-proposal-accept").first().click();
    await block().getByTestId("chat-agent-proposal-reject").last().click();
    await waitFor(() => resumes.length === 2, "the resume");
    assert.deepEqual(resumes[1], { messages: transcript, resume: [{ run_id: "ar_a", approved: true }, { run_id: "ar_b", approved: false }], conversation_id: "conv-1" });
    await waitFor(() => patches.length === 1, "the record of the answers");
    assert.deepEqual(patches[0].metadata.agent_loop.answered, { ar_a: true, ar_b: false });
    await block().waitFor({ state: "detached" });
  });
  assert.deepEqual(pageErrors, []);
  complete.complete();
  console.log("[agent-proposal-ui] " + groups.length + "/3 groups passed");
} finally {
  await browser?.close();
  await closeHttpFixture(server);
}
