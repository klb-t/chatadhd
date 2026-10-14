#!/usr/bin/env node
// P16: no silent outcome. Every way a /api/chat call can end without the provider's answer is visible in the conversation, and the field
// is cleared or kept on purpose. The real ChatView and LoomHttpApi in Chromium against a fixture that answers /api/chat with one body per
// kind (a start and an error in one write, a refusal with a status, a 200 that carries an error envelope, a usage hold, a proposal, a route
// issue, an answer with no words, a stream that is cut, an event the page does not know, a last event with no blank line).
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { createServer } from "node:http";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import { chromium } from "playwright";
import { closeHttpFixture, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const root = fileURLToPath(new URL("../", import.meta.url));
const pack = JSON.parse(readFileSync(new URL("../src/onboarding/generated/conversation-view.json", import.meta.url), "utf8"));
const sendCatalog = pack.entries.find(entry => entry.key === "presentation.chat_send").value.locales;
const en = sendCatalog.en, pl = sendCatalog.pl;
const BLOCKED = "this server does not connect to private or local network addresses ('127.0.0.1')";

const groups = [], chatRequests = [], viewRequests = [], pageErrors = [];
const KINDS = 12;
const complete = suiteCompletionGuard("chat-outcomes", KINDS + 1, groups);

const bundle = await build({ stdin: { contents: `
  import React from "react";
  import { createRoot } from "react-dom/client";
  import ChatView from "./src/components/ChatView";
  const root = createRoot(document.getElementById("root"));
  // The page as the App holds it: the conversation is a prop, and a conversation made by a send is selected through the callback.
  function Host() {
    const [conv, setConv] = React.useState(null);
    window.__host = { select: id => setConv(id) };
    return <ChatView convId={conv} onConversationCreated={setConv} />;
  }
  root.render(<Host />);
`, resolveDir: root, loader: "tsx" }, bundle: true, write: false, format: "iife", platform: "browser",
  outfile: "/tmp/loom-chat-outcomes.js", jsx: "automatic", loader: { ".json": "json" } });
const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
const css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";

const stored = (id, role, text, metadata = {}) => ({ id: `${id}-${role}`, conv_id: id, role, text, status: "active", parent_id: null, version_group_id: null,
  version_num: 1, weight: 1, created: "2026-10-14T00:00:00Z", metadata, storage: "native", capabilities: { edit: true, set_status: true, restore: true, native_lookup: true } });
const proposal = { schema: "loom.action_proposal/1", status: "pending", run_id: "ar_1", receipt_id: "arr_1", title: "Summarise", lang: "en",
  items: [{ action: "summarise", title: "Summarise", cost: 1 }], cost: { declared: 1, model_calls: 1, input_bytes: 10 }, needs_confirmation: [], runnable: true };
function view(id) {
  const messages = [stored(id, "user", `Stored words of ${id}`)];
  if (id === "c_proposal") messages.push(stored(id, "assistant", "I read this as a command", { action_proposal: proposal }));
  return { schema: "loom.conversation_view/1", conversation_id: id, view_id: `authored:${id}`, status: "complete", messages, omissions: [], resources: [],
    read_configuration: { values: { projection_storage: "transient" }, value_schema: { properties: { projection_storage: { type: "string", enum: ["snapshot", "transient"] } } } },
    capabilities: { source_history_send: { available: false, reason: "not_applicable" } } };
}
const data = (value) => `data: ${JSON.stringify(value)}\n\n`;
const start = (conv) => data({ type: "start", request_id: "req_1", conv_id: conv, user_message_id: "m_user" });
const done = (conv, text = "") => data({ type: "done", request_id: "req_1", message_id: "m_done", conv_id: conv, text });
// text of the person -> what /api/chat answers
const kinds = {
  OUT_BLOCKED: r => r.write(start("c_blocked") + data({ type: "error", request_id: "req_1", code: "invalid_argument", message: BLOCKED })),
  OUT_HTTP_REFUSED: r => r.fail(403, { error: { code: "forbidden", message: "refused: origin not allowed" } }),
  OUT_JSON_200: r => r.json200({ error: { code: "invalid_argument", message: "the provider says no" } }),
  OUT_USAGE_HOLD: r => r.write(start("c_hold") + data({ type: "usage_pending", request_id: "req_1", conv_id: "c_hold", user_message_id: "m_user",
    usage_pending: { status: "awaiting_confirmation", operation_id: "op_1", receipt_id: "usage_1", stage: "send", projected: {} } })),
  OUT_PROPOSAL: r => r.write(start("c_proposal") + data({ type: "action_proposal", request_id: "req_1", conv_id: "c_proposal", user_message_id: "m_user", message_id: "m_prop",
    text: "I read this as a command", proposal })),
  // as the kernel orders them: the route issue is said before the answer, before the start
  OUT_ROUTE_ISSUE: r => r.write(data({ type: "route_issue", request_id: "req_1", issues: [{ code: "model_route_failed", message: "no model is configured for routing", fallback: "heuristic" }] })
    + start("c_issue") + data({ type: "delta", text: "The answer." }) + done("c_issue", "The answer.")),
  OUT_NO_DELTA: r => r.write(start("c_empty") + done("c_empty", "")),
  OUT_CUT: r => r.write(start("c_cut")),
  OUT_UNKNOWN: r => r.write(start("c_unknown") + data({ type: "from_the_future", detail: 1 }) + data({ type: "delta", text: "Fine." }) + done("c_unknown", "Fine.")),
  OUT_EMPTY: r => r.write(""),
  OUT_ONLY_UNKNOWN: r => r.write(start("c_only") + data({ type: "from_the_future", detail: 1 })),
  OUT_UNTERMINATED: r => r.write(start("c_open") + data({ type: "delta", text: "Whole answer." }) + `data: ${JSON.stringify({ type: "done", request_id: "req_1", message_id: "m_done", conv_id: "c_open", text: "Whole answer." })}`),
  OUT_LATE_ERROR: r => r.write(start("c_late") + data({ type: "delta", text: "Part of an answer. " }) + data({ type: "error", request_id: "req_1", code: "network", message: "http transport error (Could not establish connection)" })),
};

function json(response, value, status = 200) { response.writeHead(status, { "Content-Type": "application/json" }); response.end(JSON.stringify(value)); }
const server = createServer(async (request, response) => {
  const url = new URL(request.url, "http://fixture.local");
  if (url.pathname === "/component.js") { response.writeHead(200, { "Content-Type": "text/javascript" }); response.end(js); return; }
  if (url.pathname === "/component.css") { response.writeHead(200, { "Content-Type": "text/css" }); response.end(css); return; }
  if (url.pathname === "/") { response.writeHead(200, { "Content-Type": "text/html" });
    response.end('<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="stylesheet" href="/component.css"></head><body><div id="root"></div><script src="/component.js"></script></body></html>'); return; }
  const chunks = []; for await (const chunk of request) chunks.push(chunk);
  const body = chunks.length ? JSON.parse(Buffer.concat(chunks).toString()) : undefined;
  const viewMatch = /^\/api\/conversations\/([^/]+)\/view$/.exec(url.pathname);
  if (viewMatch) { viewRequests.push(decodeURIComponent(viewMatch[1])); return json(response, view(decodeURIComponent(viewMatch[1]))); }
  if (url.pathname === "/api/models") return json(response, []);
  if (url.pathname === "/api/chat" && request.method === "POST") {
    chatRequests.push(body);
    const answer = Object.entries(kinds).find(([key]) => body.message.startsWith(key))?.[1];
    if (!answer) return json(response, { error: { code: "fixture", message: "unknown kind" } }, 500);
    const stream = { write: text => { response.writeHead(200, { "Content-Type": "text/event-stream" }); response.end(text); },
      fail: (status, value) => json(response, value, status), json200: value => json(response, value, 200) };
    answer(stream);
    return;
  }
  json(response, { error: { code: "fixture_unsupported", message: "Unsupported fixture endpoint" } }, 404);
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const base = `http://127.0.0.1:${server.address().port}`;
let browser, page;
async function check(name, run) { await run(); groups.push(name); console.log(`[chat-outcomes] PASS ${name}`); }
const send = () => page.getByTestId("send-chat");
async function fresh() {
  await page.goto((base).replace(/\/?$/, "/#warsztat"));
  await page.getByTestId("chat-input").waitFor();
}
// Types the words, sends, and waits until the page is at rest: no stream on the screen and no Stop button.
async function sendKind(kind, rest = "") {
  const before = chatRequests.length;
  await page.getByTestId("chat-input").fill(kind + rest);
  await send().click();
  for (const end = Date.now() + 10000; chatRequests.length === before && Date.now() < end;) await new Promise(resolve => setTimeout(resolve, 20));
  await page.getByTestId("cancel-chat").waitFor({ state: "detached", timeout: 10000 });
  assert.equal(chatRequests.length, before + 1, `${kind}: one request`);
  assert.deepEqual(chatRequests.at(-1), { message: kind + rest }, `${kind}: the request of a blank page names no conversation and the words as typed`);
}
const errorText = () => page.getByTestId("chat-error").innerText();
const field = () => page.getByTestId("chat-input").inputValue();
try {
  browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}) });
  page = await browser.newPage();
  page.on("pageerror", error => pageErrors.push(error.message));

  await check("a start and an error in one write (a refused provider address): the error is said in the conversation just made, nothing is left running", async () => {
    await fresh();
    await sendKind("OUT_BLOCKED");
    await page.getByTestId("chat-error").waitFor();
    assert.equal(await errorText(), BLOCKED);
    assert.equal(await field(), "", "the kernel stored the message: the field is cleared on purpose");
    await page.getByTestId("message").filter({ hasText: "Stored words of c_blocked" }).waitFor();  // the page went to the conversation the send made
    assert.equal(await send().isEnabled(), false, "an empty field sends nothing");
    assert.equal(await page.getByTestId("streaming-message").count(), 0);
  });

  await check("a refusal with a status: the server's words, and the text goes back into the field", async () => {
    await fresh();
    await sendKind("OUT_HTTP_REFUSED");
    await page.getByTestId("chat-error").waitFor();
    assert.equal(await errorText(), "refused: origin not allowed");
    assert.equal(await field(), "OUT_HTTP_REFUSED", "nothing was stored: the text stays to be sent again");
    assert.equal(await send().isEnabled(), true);
  });

  await check("a 200 that carries an error envelope: the refusal is said, the text goes back into the field", async () => {
    await fresh();
    await sendKind("OUT_JSON_200");
    await page.getByTestId("chat-error").waitFor();
    assert.equal(await errorText(), "the provider says no");
    assert.equal(await field(), "OUT_JSON_200");
  });

  await check("a usage hold: the question is shown and the text stays in the field", async () => {
    await fresh();
    await sendKind("OUT_USAGE_HOLD");
    await page.getByTestId("chat-usage-pending").waitFor();
    assert.equal(await field(), "OUT_USAGE_HOLD");
    assert.equal(await page.getByTestId("chat-error").count(), 0, "a hold is a state, not an error");
  });

  await check("a routing proposal: no error, the field is cleared, the proposal is in the conversation with its answers", async () => {
    await fresh();
    await sendKind("OUT_PROPOSAL");
    await page.getByTestId("chat-action-proposal").waitFor();
    assert.equal(await page.getByTestId("chat-action-proposal-confirm").isVisible(), true);
    assert.equal(await field(), "");
    assert.equal(await page.getByTestId("chat-error").count(), 0);
    assert.ok(viewRequests.includes("c_proposal"), "the page read the conversation the proposal is in");
  });

  await check("a route issue: a quiet note, and the answer follows", async () => {
    await fresh();
    await sendKind("OUT_ROUTE_ISSUE");
    await page.getByTestId("chat-route-note").waitFor();
    assert.equal(await page.getByTestId("chat-error").count(), 0);
  });

  await check("an answer with no words: said, not an empty bubble", async () => {
    await fresh();
    await sendKind("OUT_NO_DELTA");
    await page.getByTestId("chat-error").waitFor();
    assert.equal(await errorText(), en.empty_answer);
  });

  await check("a stream that is cut after its start: said, nothing keeps running, the conversation is shown", async () => {
    await fresh();
    await sendKind("OUT_CUT");
    await page.getByTestId("chat-error").waitFor();
    assert.equal(await errorText(), en.stream_ended);
    await page.getByTestId("message").filter({ hasText: "Stored words of c_cut" }).waitFor();
  });

  await check("an event the page does not know is skipped; a stream with nothing else is said to have ended", async () => {
    await fresh();
    await sendKind("OUT_UNKNOWN");
    await page.getByTestId("message").filter({ hasText: "Stored words of c_unknown" }).waitFor();
    assert.equal(await page.getByTestId("chat-error").count(), 0, "the unknown event changed nothing");
    await fresh();
    await sendKind("OUT_ONLY_UNKNOWN");
    await page.getByTestId("chat-error").waitFor();
    assert.equal(await errorText(), en.stream_ended);
  });

  await check("a 200 with an empty body: said to have ended with no result, and the text goes back into the field", async () => {
    await fresh();
    await sendKind("OUT_EMPTY");
    await page.getByTestId("chat-error").waitFor();
    assert.equal(await errorText(), en.stream_ended);
    assert.equal(await field(), "OUT_EMPTY", "nothing was stored: the text stays to be sent again");
  });

  await check("a last event without its blank line is still an event", async () => {
    await fresh();
    await sendKind("OUT_UNTERMINATED");
    await page.getByTestId("message").filter({ hasText: "Stored words of c_open" }).waitFor();
    assert.equal(await page.getByTestId("chat-error").count(), 0, "the done at the end of the body was taken");
  });

  await check("an error after part of an answer: said, and the conversation is shown", async () => {
    await fresh();
    await sendKind("OUT_LATE_ERROR");
    await page.getByTestId("chat-error").waitFor();
    assert.match(await errorText(), /Could not establish connection/);
    await page.getByTestId("message").filter({ hasText: "Stored words of c_late" }).waitFor();
  });

  await check("the words of the notes exist in both languages", async () => {
    for (const key of ["stream_ended", "empty_answer", "queued_waiting", "queued_failed"]) { assert.ok(en[key] && pl[key] && en[key] !== pl[key], key); }
  });

  assert.deepEqual(pageErrors, []);
  complete.complete();
  console.log(`[chat-outcomes] ${groups.length}/${KINDS + 1} groups passed; ${chatRequests.length} fixture chat requests`);
} finally {
  await browser?.close();
  await closeHttpFixture(server);
}
