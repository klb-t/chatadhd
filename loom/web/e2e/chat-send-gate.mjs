#!/usr/bin/env node
// Real ChatView + LoomHttpApi against authored native-view responses (R43 send gates).
// No profile is selected, so the bootstrap catalogs generated from the canonical pack
// are used. No model transport: /api/chat is answered by this fixture.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createServer } from "node:http";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import { chromium } from "playwright";
import { closeHttpFixture, suiteCompletionGuard } from "./harness-lifecycle.mjs";
import { formatTemplate } from "../src/onboarding/presentation.mjs";

const root = fileURLToPath(new URL("../", import.meta.url));
const pack = JSON.parse(readFileSync(new URL("../src/onboarding/generated/conversation-view.json", import.meta.url), "utf8"));
const en = pack.entries.find(entry => entry.key === "presentation.chat_send").value.locales.en;
const REASON = "source_egress_not_bound", VIEW_FAILURE = "authored view failure";
const omitted = formatTemplate(en.reason, { reason: REASON, explanation: en[`reason.${REASON}`] });
const blockedText = formatTemplate(en.blocked_by_setting, { setting: en.setting_label });
const groups = [], chatRequests = [], viewRequests = [], unexpected = [], pageErrors = [];
const complete = suiteCompletionGuard("chat-send-gate", 5, groups);

const bundle = await build({ stdin: { contents: `
  import React from "react";
  import { createRoot } from "react-dom/client";
  import ChatView from "./src/components/ChatView";
  const root = createRoot(document.getElementById("root")); let epoch = 0;
  window.__chat = { mount: convId => root.render(<ChatView key={convId + ":" + (++epoch)} convId={convId} onConversationCreated={() => {}} />) };
`, resolveDir: root, loader: "tsx" }, bundle: true, write: false, format: "iife", platform: "browser",
  outfile: "/tmp/loom-chat-send-gate.js", jsx: "automatic", loader: { ".json": "json" } });
const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
const css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";

const stored = id => ({ id: `${id}-placeholder`, conv_id: id, role: "system", text: `Stored placeholder of ${id}`, status: "active",
  parent_id: null, version_group_id: null, version_num: 1, weight: 1, created: "2026-10-10T00:00:00Z", metadata: {},
  storage: "native", capabilities: { edit: true, set_status: true, restore: true, native_lookup: true } });
function view(id) {
  const linked = id === "linked";
  return { schema: "loom.conversation_view/1", conversation_id: id, view_id: `authored:${id}`, status: linked ? "partial" : "complete",
    messages: [stored(id)], omissions: linked ? [{ unit_id: "unit-1", scope: "reference_history", reason: "read_denied" }] : [],
    resources: linked ? [{ unit_id: "unit-1", source_id: "source-1", status: "read_denied", current: false, mapping_status: "not_attempted" }] : [],
    read_configuration: { values: { projection_storage: "transient" },
      value_schema: { properties: { projection_storage: { type: "string", enum: ["snapshot", "transient"] } } } },
    capabilities: { source_history_send: { available: false, reason: linked ? REASON : "not_applicable" } } };
}
const holds = new Map();
const hold = key => holds.set(key, []);
function release(key) { const waiting = holds.get(key) ?? []; holds.delete(key); for (const resume of waiting) resume(); }
async function waitFor(predicate, label) {
  const deadline = Date.now() + 10000;
  while (!predicate()) { if (Date.now() > deadline) throw new Error(`Timed out waiting for ${label}`); await new Promise(resolve => setTimeout(resolve, 20)); }
}
function json(response, value, status = 200) { response.writeHead(status, { "Content-Type": "application/json" }); response.end(JSON.stringify(value)); }

const server = createServer(async (request, response) => {
  const url = new URL(request.url, "http://fixture.local");
  if (url.pathname === "/component.js") { response.writeHead(200, { "Content-Type": "text/javascript" }); response.end(js); return; }
  if (url.pathname === "/component.css") { response.writeHead(200, { "Content-Type": "text/css" }); response.end(css); return; }
  if (url.pathname === "/") { response.writeHead(200, { "Content-Type": "text/html" });
    response.end('<!doctype html><html><head><link rel="stylesheet" href="/component.css"></head><body><div id="root"></div><script src="/component.js"></script></body></html>'); return; }
  const chunks = []; for await (const chunk of request) chunks.push(chunk);
  const body = chunks.length ? JSON.parse(Buffer.concat(chunks).toString()) : undefined;
  const viewMatch = /^\/api\/conversations\/([^/]+)\/view$/.exec(url.pathname);
  if (viewMatch) {
    const id = decodeURIComponent(viewMatch[1]), key = `${request.method} ${id}`;
    viewRequests.push({ key, body });
    if (holds.has(key)) await new Promise(resume => holds.get(key).push(resume));
    if (id === "failing") return json(response, { error: { code: "fixture_unavailable", message: VIEW_FAILURE } }, 503);
    return json(response, view(id));
  }
  if (url.pathname === "/api/models") return json(response, []);
  if (url.pathname === "/api/methods/chat-settings") return json(response, { error: { code: "fixture_unavailable", message: "authored" } }, 503);
  if (url.pathname === "/api/chat" && request.method === "POST") {
    chatRequests.push(body);
    const n = chatRequests.length;
    response.writeHead(200, { "Content-Type": "text/event-stream" });
    for (const chunk of [{ type: "start", request_id: `request-${n}`, conv_id: body.conv_id, user_message_id: `user-${n}` },
      { type: "done", conv_id: body.conv_id, assistant_message_id: `assistant-${n}` }]) response.write(`data: ${JSON.stringify(chunk)}\n\n`);
    response.end(); return;
  }
  unexpected.push(`${request.method} ${url.pathname}`);
  json(response, { error: { code: "fixture_unsupported", message: "Unsupported fixture endpoint" } }, 404);
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const base = `http://127.0.0.1:${server.address().port}`;
let browser, page;
async function check(name, run) { await run(); groups.push(name); console.log(`[chat-send-gate] PASS ${name}`); }
const send = () => page.getByTestId("send-chat");
async function mount(id) {
  const before = viewRequests.length;
  await page.evaluate(conv => window.__chat.mount(conv), id);
  await waitFor(() => viewRequests.slice(before).some(row => row.key === `GET ${id}`), `view request of ${id}`);
  // Wait until the authored view is applied, unless the test holds or fails it.
  if (id !== "failing" && !holds.has(`GET ${id}`))
    await page.getByTestId("message").filter({ hasText: `Stored placeholder of ${id}` }).waitFor();
}
async function sendAndSettle(text, conv) {
  const before = chatRequests.length;
  await page.getByTestId("chat-input").fill(text);
  assert.equal(await send().isEnabled(), true);
  await send().click();
  await waitFor(() => chatRequests.length === before + 1, "dispatched chat request");
  assert.deepEqual(chatRequests.at(-1), { message: text, conv_id: conv }, "client sends no history; native assembles stored messages only");
  await send().waitFor();
}
try {
  browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}) });
  page = await browser.newPage();
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.goto(base);
  await page.waitForFunction(() => Boolean(window.__chat));

  await check("missing source-history capability: Send dispatches and a data notice names the native reason", async () => {
    await mount("linked");
    const notice = page.getByTestId("send-notice-source-history");
    await notice.waitFor();
    assert.equal(await notice.getAttribute("data-reason"), REASON);
    assert.equal(await notice.getAttribute("data-initiator"), null);
    assert.equal(await notice.locator("p").first().innerText(), en.source_history_omitted);
    assert.equal(await page.getByTestId("send-notice-reason").innerText(), omitted);
    await page.getByTestId("chat-input").fill("draft");
    assert.equal(await send().getAttribute("title"), null);
    assert.equal(await page.getByTestId("preview-chat-request").isEnabled(), true);
    await sendAndSettle("GATE_DEFAULT_SEND_4417", "linked");
  });

  await check("user-initiated block: the setting disables Send, says who set it and how to undo it, and persists", async () => {
    await page.getByTestId("chat-context-controls").locator("summary").click();
    const setting = page.getByTestId("block-send-without-source-history");
    assert.equal((await page.locator("label", { has: setting }).innerText()).trim(), en.setting_label);
    assert.equal(await setting.isChecked(), false, "preset informs and sends");
    await setting.check();
    await page.getByTestId("chat-input").fill("GATE_BLOCKED_DRAFT_2291");
    assert.equal(await send().isDisabled(), true);
    assert.equal(await page.getByTestId("preview-chat-request").isDisabled(), true);
    assert.equal(await send().getAttribute("data-gate-initiator"), "user");
    assert.equal(await send().getAttribute("data-gate-reason"), REASON);
    assert.equal(await send().getAttribute("title"), blockedText);
    const described = await send().getAttribute("aria-describedby");
    assert.equal(await page.locator(`[id="${described}"]`).textContent(), blockedText);
    const notice = page.getByTestId("send-notice-source-history");
    assert.equal(await notice.getAttribute("data-initiator"), "user");
    assert.equal(await notice.locator("p").first().innerText(), blockedText);
    assert.equal(await page.getByTestId("send-gate-unblock").innerText(), en.unblock);
    const before = chatRequests.length;
    await page.getByTestId("chat-input").press("Enter");
    assert.equal(await page.getByTestId("chat-error").innerText(), blockedText);
    assert.equal(chatRequests.length, before);
    const persisted = () => page.evaluate(() => JSON.parse(localStorage.getItem("loom.chat-settings.default")).value.blockSendWithoutSourceHistory);
    assert.equal(await persisted(), true);
    await page.reload(); await page.waitForFunction(() => Boolean(window.__chat));
    await mount("linked"); await page.getByTestId("send-notice-source-history").waitFor();
    await page.getByTestId("chat-input").fill("GATE_BLOCKED_AFTER_RELOAD");
    assert.equal(await send().isDisabled(), true, "the user's choice survives reload");
    await mount("plain"); await page.getByTestId("chat-input").fill("GATE_PLAIN_WITH_SETTING");
    assert.equal(await page.getByTestId("send-notice-source-history").count(), 0);
    await sendAndSettle("GATE_PLAIN_WITH_SETTING", "plain");
    await mount("linked"); await page.getByTestId("send-gate-unblock").click();
    assert.equal(await persisted(), false);
    await sendAndSettle("GATE_UNBLOCKED_SEND", "linked");
  });

  await check("failed view request: Send stays available and a data notice gives the failure", async () => {
    await mount("failing");
    const notice = page.getByTestId("send-notice-view-unavailable");
    await notice.waitFor();
    assert.equal(await notice.innerText(), formatTemplate(en.view_unavailable, { reason: VIEW_FAILURE }));
    assert.equal(await notice.getAttribute("data-reason"), VIEW_FAILURE);
    assert.equal(await page.getByTestId("send-notice-source-history").count(), 0);
    await sendAndSettle("GATE_AFTER_VIEW_FAILURE_7730", "failing");
    await notice.waitFor();
  });

  await check("view request in flight: Send waits with a data explanation, then opens", async () => {
    hold("GET slow");
    await mount("slow");
    await page.getByTestId("chat-input").fill("GATE_WAITS_FOR_VIEW");
    assert.equal(await send().isDisabled(), true);
    assert.equal(await send().getAttribute("data-gate-initiator"), "invariant:integrity");
    assert.equal(await send().getAttribute("data-gate-reason"), "conversation_view_loading");
    assert.equal(await send().getAttribute("title"), en.view_loading);
    release("GET slow");
    await page.waitForFunction(() => !document.querySelector('[data-testid="send-chat"]').disabled);
    assert.equal(await send().getAttribute("title"), null);
    await sendAndSettle("GATE_AFTER_VIEW_LOADED", "slow");
  });

  await check("local source read in flight: transient integrity gate with explanation, reopened after the read", async () => {
    await mount("linked");
    const read = page.getByTestId("read-conversation-sources");
    await read.waitFor();
    hold("POST linked");
    const before = viewRequests.length;
    await read.click();
    await waitFor(() => viewRequests.slice(before).some(row => row.key === "POST linked"), "local read request");
    await page.getByTestId("chat-input").fill("GATE_WAITS_FOR_SOURCE_READ");
    assert.equal(await send().isDisabled(), true);
    assert.equal(await send().getAttribute("data-gate-reason"), "source_read_in_flight");
    assert.equal(await send().getAttribute("title"), en.source_read_in_flight);
    release("POST linked");
    await page.waitForFunction(() => !document.querySelector('[data-testid="send-chat"]').disabled);
    await sendAndSettle("GATE_AFTER_SOURCE_READ", "linked");
  });
  assert.deepEqual(pageErrors, []);
  assert.deepEqual(unexpected, []);
} finally {
  await browser?.close();
  await closeHttpFixture(server);
}
complete.complete();
console.log(`[chat-send-gate] ${groups.length}/5 groups passed; ${chatRequests.length} fixture chat requests; zero model calls`);
