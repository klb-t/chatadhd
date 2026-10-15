#!/usr/bin/env node
// Real ChatView + LoomHttpApi against an authored backend: a chat input that is a command is answered in the
// conversation as a proposal (R54); the user sees the block, answers it, and the conversation shows what became of it.
// No profile is selected, so the bootstrap catalog generated from the canonical pack provides the texts.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createServer } from "node:http";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import { chromium } from "playwright";
import { closeHttpFixture, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const root = fileURLToPath(new URL("../", import.meta.url));
const pack = JSON.parse(readFileSync(new URL("../src/onboarding/generated/conversation-view.json", import.meta.url), "utf8"));
const en = pack.entries.find(entry => entry.key === "presentation.action_proposal").value.locales.en;
const groups = [], chatRequests = [], answers = [], pageErrors = [];
const complete = suiteCompletionGuard("action-proposal-ui", 3, groups);
const messages = [];   // the conversation as the backend holds it
let runSeq = 0;

const bundle = await build({ stdin: { contents: `
  import React from "react";
  import { createRoot } from "react-dom/client";
  import ChatView from "./src/components/ChatView";
  createRoot(document.getElementById("root")).render(<ChatView convId="conv-1" onConversationCreated={() => {}} />);
`, resolveDir: root, loader: "tsx" }, bundle: true, write: false, format: "iife", platform: "browser",
  outfile: "/tmp/loom-action-proposal-ui.js", jsx: "automatic", loader: { ".json": "json" } });
const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
const css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";
const json = (response, value, status = 200) => { response.writeHead(status, { "Content-Type": "application/json" }); response.end(JSON.stringify(value)); };
const view = id => ({ schema: "loom.conversation_view/1", conversation_id: id, view_id: "authored:" + id + ":" + messages.length, status: "complete",
  messages: messages.map(row => ({ ...row })), omissions: [], resources: [], read_configuration: { values: {}, value_schema: { properties: {} } },
  capabilities: { source_history_send: { available: true, reason: "not_applicable" } } });
const message = (id, role, text, metadata = {}) => ({ id, conv_id: "conv-1", role, text, status: "active", metadata, created: "2026-10-13T10:00:00Z", model: null,
  storage: "native", capabilities: { edit: true, set_status: true, restore: true, native_lookup: true } });
const proposal = n => ({ schema: "loom.action_proposal/1", status: "pending", run_id: "ar_" + n, receipt_id: "arr_" + n, title: "Summarise", lang: "en",
  items: [{ action: "summarise", title: "Summarise", cost: 1, implementation: "implemented" }],
  cost: { declared: 1, model_calls: 1, input_bytes: 321, money: null }, needs_confirmation: [{ action: "use_model", title: "Use a model", initiator: "owner" }], runnable: true });
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
  if (url.pathname === "/api/chat" && request.method === "POST") {
    chatRequests.push(body);
    const n = ++runSeq;
    messages.push(message("user-" + n, "user", body.message, { routed: "command" }));
    messages.push(message("assistant-" + n, "assistant", "I read this as a command: Summarise.", { action_proposal: proposal(n), routed: "proposal" }));
    response.writeHead(200, { "Content-Type": "text/event-stream" });
    for (const chunk of [{ type: "start", request_id: "request-" + n, conv_id: "conv-1", user_message_id: "user-" + n },
      { type: "action_proposal", request_id: "request-" + n, conv_id: "conv-1", user_message_id: "user-" + n, message_id: "assistant-" + n, text: "I read this as a command: Summarise.", proposal: proposal(n) }])
      response.write("data: " + JSON.stringify(chunk) + "\n\n");
    response.end(); return;
  }
  if (url.pathname === "/api/actions/confirm" && request.method === "POST") {
    answers.push(body);
    const n = Number(body.run_id.slice(3));
    const proposed = messages.find(m => m.id === "assistant-" + n);
    proposed.metadata = { ...proposed.metadata, action_proposal: { ...proposed.metadata.action_proposal, status: body.approved ? "executed" : "rejected" } };
    messages.push(message("result-" + n, "assistant", body.approved ? "Done: Summarise." : "Dropped.", { action_run: { run_id: body.run_id, status: body.approved ? "completed" : "rejected" } }));
    return json(response, { id: body.run_id, status: body.approved ? "completed" : "rejected" });
  }
  json(response, { error: { code: "fixture_unsupported", message: "Unsupported fixture endpoint" } }, 404);
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const base = "http://127.0.0.1:" + server.address().port;
let browser, page;
async function check(name, run) { await run(); groups.push(name); console.log("[action-proposal-ui] PASS " + name); }
async function waitFor(predicate, label) {
  const deadline = Date.now() + 10000;
  while (!predicate()) { if (Date.now() > deadline) throw new Error("Timed out waiting for " + label); await new Promise(resolve => setTimeout(resolve, 20)); }
}
const block = () => page.getByTestId("chat-action-proposal");
async function sendCommand(text) {
  const before = chatRequests.length;
  await page.getByTestId("chat-input").fill(text);
  await page.getByTestId("send-chat").click();
  await block().waitFor();
  assert.equal(chatRequests.length, before + 1);
}
try {
  const cachedShell = "/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell";
  const { existsSync } = await import("node:fs");
  const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || (existsSync(cachedShell) ? cachedShell : undefined);
  browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  page = await browser.newPage();
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.goto((base).replace(/\/?$/, "/#warsztat"));
  await page.getByTestId("chat-input").waitFor();

  await check("a command is answered in the conversation: the proposal text, the block with plan, estimate and what the rules ask, no error", async () => {
    await sendCommand("Summarise this conversation");
    assert.equal(await block().getAttribute("data-run-id"), "ar_1");
    assert.equal(await block().getAttribute("data-receipt-id"), "arr_1");
    assert.equal(await block().getAttribute("data-kind"), "proposal");
    assert.match(await block().innerText(), new RegExp(en.title));
    assert.match(await block().getByTestId("chat-action-proposal-plan").innerText(), /Summarise/);
    assert.match(await block().getByTestId("chat-action-proposal-cost").innerText(), /1.*1.*321/);
    assert.match(await block().getByTestId("chat-action-proposal-confirm-needed").innerText(), /Use a model/);
    assert.equal(await block().getByTestId("chat-action-proposal-confirm").innerText(), en.confirm);
    assert.equal(await block().getByTestId("chat-action-proposal-reject").innerText(), en.reject);
    assert.ok(await page.locator(".msg.assistant .body", { hasText: "I read this as a command: Summarise." }).count() >= 1, "the proposal is a message of the conversation");
    assert.equal(await page.getByTestId("chat-error").count(), 0, "a proposal is a state, not an error");
    assert.equal(await page.getByTestId("chat-usage-pending").count(), 0);
    assert.equal(await page.getByTestId("chat-input").inputValue(), "", "the input was consumed by the command");
  });

  await check("confirm answers with the run id and the pending receipt; the block gives way to the state and the result", async () => {
    await block().getByTestId("chat-action-proposal-confirm").click();
    await waitFor(() => answers.length === 1, "the confirmation");
    assert.deepEqual(answers[0], { run_id: "ar_1", approved: true, receipt_id: "arr_1", ref: "web" });
    await block().waitFor({ state: "detached" });
    assert.equal(await page.getByTestId("chat-action-state").first().innerText(), en.status_executed);
    assert.ok(await page.locator(".msg.assistant .body", { hasText: "Done: Summarise." }).count() >= 1);
    assert.equal(await page.getByTestId("chat-error").count(), 0);
  });

  await check("reject answers approved:false; nothing else is sent; the state says it was dropped", async () => {
    await sendCommand("Summarise the last message");
    const chats = chatRequests.length;
    await block().getByTestId("chat-action-proposal-reject").click();
    await waitFor(() => answers.length === 2, "the rejection");
    assert.deepEqual(answers[1], { run_id: "ar_2", approved: false, receipt_id: "arr_2", ref: "web" });
    await block().waitFor({ state: "detached" });
    assert.equal(chatRequests.length, chats, "answering is not another chat request");
    assert.equal(await page.getByTestId("chat-action-state").last().innerText(), en.status_rejected);
    assert.equal(await page.getByTestId("chat-error").count(), 0);
  });
  assert.deepEqual(pageErrors, []);
  complete.complete();
  console.log("[action-proposal-ui] " + groups.length + "/3 groups passed");
} finally {
  await browser?.close();
  await closeHttpFixture(server);
}
