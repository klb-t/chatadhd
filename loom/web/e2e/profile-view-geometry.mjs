#!/usr/bin/env node
// Actual web bundle + authored local HTTP fixtures. This checks client projection
// behavior, not native-store semantics (covered by application-profiles.mjs).
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { existsSync, readFileSync, mkdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import ts from "typescript";
import { chromium } from "playwright";

const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const dist = path.join(web, "dist");
const source = readFileSync(path.join(web, "src/profiles/view-state.ts"), "utf8");
const tsjs = (src) => ts.transpileModule(src, { compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.ES2020 } }).outputText;
// W6: view-state reads its conversation through shell/windows/binding.ts (no imports of its own): mapped to a data: module
const bindingUrl = `data:text/javascript;base64,${Buffer.from(tsjs(readFileSync(path.join(web, "src/shell/windows/binding.ts"), "utf8"))).toString("base64")}`;
const js = tsjs(source).replaceAll('from "../shell/windows/binding"', `from "${bindingUrl}"`);
const state = await import(`data:text/javascript;base64,${Buffer.from(js).toString("base64")}`);
const groups = [];
async function check(name, fn) { await fn(); groups.push(name); console.log(`[profile-view-geometry] PASS ${name}`); }
const profileKey = JSON.stringify(["loom-default", 2]);

await check("legacy view identities migrate to the unchanged shared selection default", () => {
  const legacy = { id: "kept-workflow-id", profile: profileKey };
  const migrated = state.readApplicationView(legacy);
  assert.deepEqual(migrated, { ...legacy, conversation: { coupling: "coupled", selectedId: null, sidebarOpen: false } });
  assert.equal(state.viewConversationId(migrated, "shared"), "shared");
  assert.deepEqual(legacy, { id: "kept-workflow-id", profile: profileKey });
});
await check("detaching preserves the visible selection and stored independent state round trips", () => {
  const coupled = state.newApplicationView("second", profileKey);
  const detached = state.setConversationCoupling(coupled, "independent", "conversation-a");
  assert.equal(state.viewConversationId(detached, "conversation-b"), "conversation-a");
  assert.equal(detached.conversation.sidebarOpen, true);
  const restored = state.readApplicationView(JSON.parse(JSON.stringify(detached)));
  assert.deepEqual(restored, detached);
  assert.equal(state.viewConversationId(state.setConversationCoupling(restored, "coupled", "conversation-b"), "conversation-b"), "conversation-b");
  assert.equal(coupled.conversation.coupling, "coupled");
});
await check("invalid persisted conversation settings are rejected without mutating source", () => {
  for (const conversation of [null, [], {}, { coupling: "unknown", selectedId: null, sidebarOpen: false },
    { coupling: "independent", selectedId: 1, sidebarOpen: true },
    { coupling: "coupled", selectedId: null, sidebarOpen: "false" }]) {
    const saved = { id: "primary", profile: profileKey, conversation };
    const bytes = JSON.stringify(saved);
    assert.throws(() => state.readApplicationView(saved), /conversation settings/);
    assert.equal(JSON.stringify(saved), bytes);
  }
});

assert.ok(existsSync(path.join(dist, "index.html")), "Run npm run build first.");
const timestamp = "2026-10-04T12:00:00Z";
const conversations = ["a", "b"].map(id => ({ id, title: `Conversation ${id.toUpperCase()}`, created: timestamp, updated: timestamp }));
const messages = new Map(conversations.map(conv => [conv.id, [{ id: `${conv.id}-message`, conv_id: conv.id, role: "user",
  text: `Recorded conversation ${conv.id.toUpperCase()}`, status: "active", created: timestamp }]]));
const requests = [], chatRequests = [], declares = [], failures = [], eventFeeds = [];
let created = 0;
const server = createServer(async (req, res) => {
  const url = new URL(req.url, "http://fixture.local");
  const parts = []; for await (const chunk of req) parts.push(chunk);
  const body = parts.length ? JSON.parse(Buffer.concat(parts).toString()) : undefined;
  requests.push({ method: req.method, path: url.pathname, body });
  function json(value) { res.writeHead(200, { "Content-Type": "application/json" }).end(JSON.stringify(value)); }
  if (url.pathname === "/api/conversations" && req.method === "GET") return json(conversations);
  if (url.pathname === "/api/conversations" && req.method === "POST") {
    const row = { id: `created-${++created}`, title: body.title || "Untitled", created: timestamp, updated: timestamp };
    conversations.push(row); messages.set(row.id, []); return json(row);
  }
  // Authored native-only additive view; does not emulate external source I/O.
  const viewMatch = /^\/api\/conversations\/([^/]+)\/view$/.exec(url.pathname);
  if (viewMatch && req.method === "GET") {
    const id = decodeURIComponent(viewMatch[1]);
    return json({ schema: "loom.conversation_view/1", conversation_id: id, view_id: `authored:${id}`,
      status: "complete", messages: (messages.get(id) ?? []).map(message => ({ ...message, storage: "native",
        capabilities: { edit: true, set_status: true, restore: true, native_lookup: true } })),
      resources: [], omissions: [], capabilities: { source_history_send: { available: false, reason: "not_applicable" } } });
  }
  const messageMatch = /^\/api\/conversations\/([^/]+)\/messages$/.exec(url.pathname);
  if (messageMatch) return json(messages.get(decodeURIComponent(messageMatch[1])) ?? []);
  if (url.pathname === "/api/models") return json([{ id: "fixture/one", name: "Fixture one" }, { id: "fixture/two", name: "Fixture two" }]);
  // The page reads its page sizes (loom_web_message_window) from the settings; this fixture sets none, so the presets apply.
  if (url.pathname === "/api/config") return json({});
  if (url.pathname === "/api/semantic/status") return json({ pending: 0, processed: 0, errors: 0, mode: "off", rate: "off", paused: false });
  // P13: the page declares its devices right before its first send (never while only viewing); this fixture takes the declaration.
  if (url.pathname === "/api/client/declare") {
    declares.push(body); return json({ client_id: body.client_id, capabilities: {}, runnable_actions: [], refused: {} });
  }
  if (url.pathname === "/api/chat") {
    chatRequests.push(body);
    const conv = body.conv_id;
    const rows = messages.get(conv);
    assert.ok(rows, "Fixture chat must address an existing selected conversation.");
    const id = `sent-${chatRequests.length}`;
    rows.push({ id, conv_id: conv, role: "user", text: body.message, status: "active", created: timestamp });
    rows.push({ id: `${id}-reply`, conv_id: conv, role: "assistant", text: `Fixture reply to ${conv}`, status: "active", created: timestamp });
    res.writeHead(200, { "Content-Type": "text/event-stream" });
    for (const chunk of [{ type: "start", request_id: id, conv_id: conv, user_message_id: id },
      { type: "done", conv_id: conv, assistant_message_id: `${id}-reply` }]) res.write(`data: ${JSON.stringify(chunk)}\n\n`);
    res.end(); return;
  }
  // P1: the app asks whether the server runs accounts; this fixture is a single-user server.
  if (url.pathname === "/api/auth/me") return json({ accounts: false });
  // P14 4: every chat view opens the kernel's event feed; this fixture has none (404), which the page does not ask for again.
  if (url.pathname === "/api/events/stream") { eventFeeds.push(url.search); res.writeHead(404).end(JSON.stringify({ error: { message: "no event feed in this fixture" } })); return; }
  if (url.pathname.startsWith("/api/")) {
    failures.push(`${req.method} ${url.pathname}`); res.writeHead(404).end(JSON.stringify({ error: { message: "Unsupported fixture endpoint" } })); return;
  }
  const filename = path.resolve(dist, `.${url.pathname === "/" ? "/index.html" : url.pathname}`);
  if (!filename.startsWith(`${dist}${path.sep}`) || !existsSync(filename)) { res.writeHead(404).end(); return; }
  const mime = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".svg": "image/svg+xml" }[path.extname(filename)] || "application/octet-stream";
  res.writeHead(200, { "Content-Type": mime }).end(readFileSync(filename));
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const base = `http://127.0.0.1:${server.address().port}`;
let browser, page;
const errors = [], external = [];
const storageKey = "loom.application.views.v1";
try {
  browser = await chromium.launch({ headless: true,
    ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}) });
  page = await browser.newPage({ viewport: { width: 1800, height: 1000 } });
  page.on("pageerror", error => errors.push(error.message));
  await page.route("**/*", async route => {
    if (new URL(route.request().url()).origin !== base) { external.push(route.request().url()); await route.abort(); }
    else await route.continue();
  });
  const view = index => page.getByTestId("application-profile-view").nth(index);
  // After a reload the model picker is filled from the saved layout a moment after the selection; wait for that value
  // (a value lost for good still fails: the wait times out and the equality below reports what is there).
  async function pickerValue(index) {
    await page.waitForFunction(index => document.querySelectorAll('[data-testid="application-profile-view"]')[index]?.querySelector('[data-testid="model-picker"]')?.value, index).catch(() => {});
    return view(index).getByTestId("model-picker").inputValue();
  }
  async function waitSelection(index, id) { await page.waitForFunction(({ index, id }) => document.querySelectorAll('[data-testid="application-profile-view"]')[index]?.getAttribute("data-conversation-id") === id, { index, id }); }
  async function chooseInHost(id) { await page.getByTestId("sidebar").getByTestId("conv-item").filter({ hasText: `Conversation ${id.toUpperCase()}` }).click(); }
  async function chooseInView(index, id) { await view(index).getByTestId("profile-view-sidebar").getByTestId("conv-item").filter({ hasText: `Conversation ${id.toUpperCase()}` }).click(); }
  async function openContext(index) { const control = view(index).getByTestId("chat-context-controls"); if (!(await control.evaluate(element => element.open))) await control.locator(":scope > summary").click(); }
  await page.goto((base).replace(/\/?$/, "/#warsztat"));
  await check("new views retain the legacy shared sidebar and coupled conversation selection", async () => {
    await chooseInHost("a");
    await page.getByTestId("add-profile-view").click();
    await waitSelection(0, "a"); await waitSelection(1, "a");
    assert.equal(await page.getByTestId("profile-view-sidebar").count(), 0);
    await chooseInHost("b");
    await waitSelection(0, "b"); await waitSelection(1, "b");
  });
  await check("independent selection and local creation do not redirect sibling or host selection", async () => {
    await view(1).getByLabel("Conversation selection", { exact: true }).selectOption("independent");
    await waitSelection(1, "b");
    await chooseInView(1, "a");
    await waitSelection(1, "a"); await waitSelection(0, "b");
    await view(1).getByTestId("profile-view-sidebar").getByTestId("new-conversation").click();
    await waitSelection(1, "created-1"); await waitSelection(0, "b");
    assert.equal(await page.getByTestId("sidebar").locator(".conv-item.active .title").textContent(), "Conversation B");
    await chooseInView(1, "a");
  });
  await check("profile-specific sidebar geometry and workflow selection stay local to the invoking view", async () => {
    const profile = JSON.parse(readFileSync(path.join(web, "src/profiles/data/loom-default-r2.json"), "utf8"));
    profile.id = "fixture-right-sidebar"; profile.label = "Fixture right sidebar"; profile.presentation.sidebar = { side: "right", width: 315 };
    profile.workflows = [{ id: "local-selection", initial: "ready", states: ["ready", "created"], transitions: [
      { from: "ready", event: "create", action: "create", to: "created", payload: { object: {} }, save: { conversation_id: { from: "result", pointer: "/id" } } },
      { from: "created", event: "return", action: "select", to: "created", payload: { object: { id: { from: "vars", pointer: "/conversation_id" } } } },
    ] }];
    await view(1).getByLabel("Import application profile", { exact: true }).setInputFiles({ name: "fixture-profile.json", mimeType: "application/json", buffer: Buffer.from(JSON.stringify(profile)) });
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="application-profile-view"]')[1]?.getAttribute("data-profile-id") === "fixture-right-sidebar");
    const sidebar = view(1).getByTestId("profile-view-sidebar");
    assert.equal(await sidebar.evaluate(element => getComputedStyle(element).width), "315px");
    assert.equal(await sidebar.evaluate(element => getComputedStyle(element).order), "2");
    await view(1).locator(".profile-details > summary").click();
    await view(1).getByTestId("profile-workflow").getByRole("button", { name: "New conversation", exact: true }).click();
    await waitSelection(1, "created-2"); await waitSelection(0, "b");
    await chooseInView(1, "a");
    await view(1).getByTestId("profile-workflow").getByRole("button", { name: "Return to conversation", exact: true }).click();
    await waitSelection(1, "created-2"); await waitSelection(0, "b");
    await view(1).locator(".profile-details > summary").click();
  });
  await check("model and context controls survive coupling changes and sending uses the view's conversation", async () => {
    await view(0).getByTestId("model-picker").selectOption("fixture/one");
    await view(1).getByTestId("model-picker").selectOption("fixture/two");
    await openContext(1); await view(1).getByTestId("include-memory").uncheck();
    await view(1).getByLabel("Conversation selection", { exact: true }).selectOption("coupled");
    await waitSelection(1, "b");
    await chooseInView(1, "a"); await waitSelection(0, "a"); await waitSelection(1, "a");
    await view(1).getByLabel("Conversation selection", { exact: true }).selectOption("independent");
    await chooseInHost("b"); await waitSelection(0, "b"); await waitSelection(1, "a");
    assert.equal(await view(1).getByTestId("model-picker").inputValue(), "fixture/two");
    assert.equal(await view(1).getByTestId("include-memory").isChecked(), false);
    assert.equal(await view(0).getByTestId("model-picker").inputValue(), "fixture/one");
    assert.deepEqual(declares, [], "viewing, layout, selection and settings declare nothing");
    await view(1).getByTestId("chat-input").fill("Geometry fixture send");
    await view(1).getByTestId("send-chat").click();
    await view(1).getByTestId("message").filter({ hasText: "Fixture reply to a" }).waitFor();
    assert.equal(chatRequests.length, 1); assert.equal(chatRequests[0].conv_id, "a");
    assert.equal(declares.length, 1, "one declaration, right before the first send"); assert.match(declares[0].client_id, /^web-[0-9a-f]{12}$/);
    assert.equal(chatRequests[0].client_id, declares[0].client_id, "the send carries the id of the client that declared");
    assert.equal(chatRequests[0].model, "fixture/two"); assert.equal(chatRequests[0].include_memory, false);
    await waitSelection(0, "b");
  });
  await check("independent layout and successful local workflow survive reload and closing does not delete conversation data", async () => {
    const saved = await page.evaluate(key => JSON.parse(localStorage.getItem(key)), storageKey);
    assert.equal(saved.views[1].conversation.selectedId, "a");
    const id = saved.views[1].id;
    await page.reload(); await waitSelection(1, "a");
    assert.equal(await view(1).getAttribute("data-profile-view-id"), id);
    assert.equal(await pickerValue(1), "fixture/two");
    assert.equal(await pickerValue(0), "fixture/one");
    await openContext(1); assert.equal(await view(1).getByTestId("include-memory").isChecked(), false);
    assert.equal(await view(1).getByTestId("profile-view-sidebar").evaluate(element => getComputedStyle(element).width), "315px");
    await view(1).locator(".profile-details > summary").click();
    assert.match(await view(1).getByTestId("profile-workflow").textContent(), /local-selection: created/);
    await view(1).getByRole("button", { name: "Close view", exact: true }).click();
    assert.equal(await page.getByTestId("application-profile-view").count(), 1);
    assert.equal(requests.filter(request => request.method === "DELETE").length, 0);
    assert.equal(conversations.length, 4);
  });
  await check("legacy layout restoration preserves workflow identity and invalid new settings preserve saved bytes", async () => {
    const legacy = { schema: "loom.application.views/1", views: [{ id: "legacy", profile: profileKey }], custom_profiles: [] };
    await page.evaluate(({ key, saved }) => localStorage.setItem(key, JSON.stringify(saved)), { key: storageKey, saved: legacy });
    await page.reload();
    await view(0).waitFor(); assert.equal(await view(0).getAttribute("data-profile-view-id"), "legacy");
    assert.equal(await view(0).getAttribute("data-conversation-coupling"), "coupled");
    assert.equal(await page.getByTestId("profile-view-sidebar").count(), 0);
    legacy.views[0].conversation = { coupling: "invalid", selectedId: null, sidebarOpen: false };
    const bytes = JSON.stringify(legacy);
    await page.evaluate(({ key, bytes }) => localStorage.setItem(key, bytes), { key: storageKey, bytes });
    await page.reload(); await page.getByRole("alert").filter({ hasText: "Saved bytes were preserved" }).waitFor();
    assert.equal(await page.evaluate(key => localStorage.getItem(key), storageKey), bytes);
  });
  assert.deepEqual(errors, []); assert.deepEqual(external, []); assert.deepEqual(failures, []);
  assert.ok(eventFeeds.length > 0 && eventFeeds.every(search => search === "?event=action%3Aclient_step"), "the only feed a page opens is the kernel's client-step feed");
  if (process.env.PROFILE_GEOMETRY_EVIDENCE_DIR) {
    const directory = process.env.PROFILE_GEOMETRY_EVIDENCE_DIR; mkdirSync(directory, { recursive: true });
    const filename = path.join(directory, "profile-view-geometry-results.json");
    assert.ok(!existsSync(filename), "Use a fresh evidence directory.");
    writeFileSync(filename, JSON.stringify({ groups, passed: groups.length, fixture: "authored-local-http", paidCalls: 0, externalRequests: external }, null, 2));
  }
  console.log(`[profile-view-geometry] ${groups.length}/${groups.length} passed; authored local HTTP fixtures; zero remote model calls.`);
} catch (error) {
  console.error(JSON.stringify({ errors, failures, external, requests: requests.map(({ method, path }) => ({ method, path })),
    body: page ? await page.locator("body").innerText().catch(() => "unavailable") : "browser unavailable" }, null, 2));
  throw error;
} finally {
  await browser?.close(); await new Promise(resolve => server.close(resolve));
}
