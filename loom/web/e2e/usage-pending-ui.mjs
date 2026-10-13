#!/usr/bin/env node
// Real ChatView + LoomHttpApi against an authored /api/chat: the x10 usage guard pauses a send
// (usage_pending, nothing sent), the user sees the confirmation block and answers it (T6).
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
const en = pack.entries.find(entry => entry.key === "presentation.chat_usage").value.locales.en;
const RECEIPT = { status: "requires_confirmation", operation_id: "op-x10", receipt_id: "usage_r1", baseline_key: "chat.send|fixture|model",
  projected: { input_bytes: { projected: 60000, baseline: 250, ratio: 240 }, cost_usd: { projected: null, baseline: null, ratio: null } } };
const groups = [], chatRequests = [], pageErrors = [];
const complete = suiteCompletionGuard("usage-pending-ui", 3, groups);
let mode = "pause";  // pause: answer with usage_pending unless the request carries a confirmation

const bundle = await build({ stdin: { contents: `
  import React from "react";
  import { createRoot } from "react-dom/client";
  import ChatView from "./src/components/ChatView";
  createRoot(document.getElementById("root")).render(<ChatView convId="conv-1" onConversationCreated={() => {}} />);
`, resolveDir: root, loader: "tsx" }, bundle: true, write: false, format: "iife", platform: "browser",
  outfile: "/tmp/loom-usage-pending-ui.js", jsx: "automatic", loader: { ".json": "json" } });
const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
const css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";
const json = (response, value, status = 200) => { response.writeHead(status, { "Content-Type": "application/json" }); response.end(JSON.stringify(value)); };
const view = id => ({ schema: "loom.conversation_view/1", conversation_id: id, view_id: `authored:${id}`, status: "complete",
  messages: [], omissions: [], resources: [], read_configuration: { values: {}, value_schema: { properties: {} } },
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
  if (/^\/api\/conversations\/[^/]+\/messages/.test(url.pathname)) return json(response, []);
  if (url.pathname === "/api/models") return json(response, []);
  if (url.pathname === "/api/chat" && request.method === "POST") {
    chatRequests.push(body);
    const n = chatRequests.length;
    response.writeHead(200, { "Content-Type": "text/event-stream" });
    const chunksOut = [{ type: "start", request_id: `request-${n}`, conv_id: "conv-1", user_message_id: `user-${n}` }];
    if (mode === "pause" && !body.usage_confirmation)
      chunksOut.push({ type: "usage_pending", conv_id: "conv-1", usage_pending: RECEIPT });
    else if (body.usage_confirmation?.approved === false) chunksOut.push({ type: "done", conv_id: "conv-1" });
    else chunksOut.push({ type: "done", conv_id: "conv-1", assistant_message_id: `assistant-${n}` });
    for (const chunk of chunksOut) response.write(`data: ${JSON.stringify(chunk)}\n\n`);
    response.end(); return;
  }
  json(response, { error: { code: "fixture_unsupported", message: "Unsupported fixture endpoint" } }, 404);
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const base = `http://127.0.0.1:${server.address().port}`;
let browser, page;
async function check(name, run) { await run(); groups.push(name); console.log(`[usage-pending-ui] PASS ${name}`); }
async function waitFor(predicate, label) {
  const deadline = Date.now() + 10000;
  while (!predicate()) { if (Date.now() > deadline) throw new Error(`Timed out waiting for ${label}`); await new Promise(resolve => setTimeout(resolve, 20)); }
}
const block = () => page.getByTestId("chat-usage-pending");
async function pauseASend(text) {
  const before = chatRequests.length;
  await page.getByTestId("chat-input").fill(text);
  await page.getByTestId("send-chat").click();
  await block().waitFor();
  assert.equal(chatRequests.length, before + 1);
  assert.equal(chatRequests.at(-1).usage_confirmation, undefined, "first request carries no confirmation");
}
try {
  const cachedShell = "/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell";
  const { existsSync } = await import("node:fs");
  const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || (existsSync(cachedShell) ? cachedShell : undefined);
  browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  page = await browser.newPage();
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.goto(base);
  await page.getByTestId("chat-input").waitFor();

  await check("a paused send shows the confirmation block: texts from data, unknown shown as unknown, text kept, no error", async () => {
    await pauseASend("USAGE_X10_TEXT_1");
    assert.equal(await block().getAttribute("data-receipt-id"), RECEIPT.receipt_id);
    assert.equal(await block().getAttribute("data-operation-id"), RECEIPT.operation_id);
    assert.match(await block().innerText(), new RegExp(en.title));
    assert.match(await block().innerText(), /chat\.send\|fixture\|model/);
    const lines = await block().getByTestId("chat-usage-pending-line").allInnerTexts();
    assert.equal(lines.length, 2);
    assert.match(lines.find(line => line.includes("input_bytes")), /60000.*250.*240/);
    assert.match(lines.find(line => line.includes("cost_usd")), new RegExp(en.unknown));
    assert.equal(await block().getByTestId("chat-usage-pending-confirm").isEnabled(), true);
    assert.equal(await block().getByTestId("chat-usage-pending-reject").isEnabled(), true);
    assert.equal(await page.getByTestId("chat-error").count(), 0, "a pause is a state, not an error");
    assert.equal(await page.getByTestId("chat-input").inputValue(), "USAGE_X10_TEXT_1", "the text returns to the composer");
  });

  await check("confirm re-sends the identical request with the approved confirmation; the block disappears", async () => {
    const first = chatRequests.at(-1);
    await block().getByTestId("chat-usage-pending-confirm").click();
    await waitFor(() => chatRequests.length === 2, "confirmed re-send");
    const { usage_confirmation: confirmation, ...rest } = chatRequests.at(-1);
    assert.deepEqual(rest, first, "identical request apart from the confirmation");
    assert.deepEqual(confirmation, { approved: true, receipt_id: RECEIPT.receipt_id, operation_id: RECEIPT.operation_id, ref: "web" });
    await block().waitFor({ state: "detached" });
    assert.equal(await page.getByTestId("chat-error").count(), 0);
  });

  await check("reject records the denial (approved:false), sends nothing, shows the notice and keeps the text", async () => {
    await pauseASend("USAGE_X10_TEXT_2");
    const before = chatRequests.length;
    await block().getByTestId("chat-usage-pending-reject").click();
    await waitFor(() => chatRequests.length === before + 1, "denial request");
    assert.deepEqual(chatRequests.at(-1).usage_confirmation, { approved: false, receipt_id: RECEIPT.receipt_id, operation_id: RECEIPT.operation_id, ref: "web" });
    await block().waitFor({ state: "detached" });
    await page.getByTestId("chat-usage-notice").waitFor();
    assert.equal(await page.getByTestId("chat-usage-notice").innerText(), en.rejected);
    assert.equal(await page.getByTestId("chat-input").inputValue(), "USAGE_X10_TEXT_2");
    assert.equal(await page.getByTestId("chat-error").count(), 0);
  });
  assert.deepEqual(pageErrors, []);
  complete.complete();
  console.log(`[usage-pending-ui] ${groups.length}/3 groups passed`);
} finally {
  await browser?.close();
  await closeHttpFixture(server);
}
