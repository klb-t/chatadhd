#!/usr/bin/env node
// Native public-fixture import -> real ChatView -> inert typed source inspector.
// No model credentials or inference calls: all import/edit operations stay local.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync, mkdtempSync, readFileSync, rmSync } from "node:fs";
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
const dataDir = mkdtempSync(path.join(tmpdir(), "loom-imported-source-"));
const probe = createServer();
await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
const port = probe.address().port;
await new Promise(resolve => probe.close(resolve));
const base = `http://127.0.0.1:${port}`;
const server = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", dataDir, "--static-dir", dist], {
  cwd: loomRoot, stdio: ["ignore", "pipe", "pipe"],
});
let log = "", browser, page;
const groups = [], pageErrors = [], remoteRequests = [], inferenceRequests = [];
for (const output of [server.stdout, server.stderr]) output.on("data", chunk => { log += chunk.toString(); });
server.on("error", error => { log += String(error); });
async function api(method, endpoint, body) {
  const response = await fetch(`${base}${endpoint}`, { method, headers: { "Content-Type": "application/json" },
    ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  const result = await response.json();
  assert.ok(response.ok && !result.error, `${endpoint}: ${JSON.stringify(result)}`);
  return result;
}
async function ready() {
  const deadline = Date.now() + 20000;
  while (Date.now() < deadline) {
    try { if ((await fetch(`${base}/api/healthz`)).ok) return; } catch { /* Startup. */ }
    if (server.exitCode !== null) throw new Error(`Native server exited: ${log}`);
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  throw new Error(`Native server startup timeout: ${log}`);
}
async function importFixture(filename) {
  const form = new FormData();
  form.append("file", new Blob([readFileSync(path.join(loomRoot, "tests/fixtures/exports", filename))]), filename);
  const response = await fetch(`${base}/api/import`, { method: "POST", body: form });
  assert.ok(response.ok, `Native fixture import ${filename}: ${response.status}`);
  const events = (await response.text()).split(/\r?\n/).filter(line => line.startsWith("data: ")).map(line => JSON.parse(line.slice(6)));
  assert.ok(events.some(event => event.type === "done"), `Import completed: ${JSON.stringify(events)}`);
  assert.ok(!events.some(event => event.type === "error"), `Import failed: ${JSON.stringify(events)}`);
}
async function check(name, action) { await action(); groups.push(name); console.log(`[imported-message-content-native] PASS ${name}`); }
const view = () => page.getByTestId("application-profile-view").first();
async function chooseConversation(title) {
  await page.getByTestId("conv-item").filter({ has: page.locator(".title", { hasText: title }) }).click();
  await view().getByTestId("message").first().waitFor();
}

try {
  await ready();
  await api("PATCH", "/api/config", { semantic_analysis: false });
  await importFixture("anthropic_2026_full.zip");
  await importFixture("openai_2026_sharded.zip");
  const conversations = await api("GET", "/api/conversations?limit=100");
  const claudeConversation = conversations.find(conversation => conversation.title === "Widok listy");
  assert.ok(claudeConversation, "Native Claude conversation retained its public-fixture title");
  const claudeMessages = await api("GET", `/api/conversations/${claudeConversation.id}/messages?all=1`);
  const sourceAssistant = claudeMessages.find(message => message.metadata?.export?.raw?.uuid === "m-a1");
  const sourceUser = claudeMessages.find(message => message.metadata?.export?.raw?.uuid === "m-u1");
  assert.ok(sourceAssistant && sourceUser, "Native importer retained source identities");
  const originalAssistantRaw = structuredClone(sourceAssistant.metadata.export.raw);
  const originalUserRaw = structuredClone(sourceUser.metadata.export.raw);
  const cachedShell = "/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell";
  const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || (existsSync(cachedShell) ? cachedShell : undefined);
  browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  page = await browser.newPage({ viewport: { width: 1500, height: 1100 } });
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.route("**/*", async route => {
    const request = route.request(); const url = new URL(request.url());
    if (url.origin !== base) { remoteRequests.push(request.url()); await route.abort(); return; }
    if (url.pathname === "/api/chat") { inferenceRequests.push(request.url()); await route.abort(); return; }
    await route.continue();
  });
  await page.goto(base);
  await view().waitFor();

  await check("native Claude import renders all ten typed blocks through ChatView without running tools or media", async () => {
    await chooseConversation("Widok listy");
    const assistant = view().locator('[data-testid="message"][data-role="assistant"]').filter({ has: page.locator(".body", { hasText: sourceAssistant.text.split("\n")[0] }) }).first();
    await assistant.getByTestId("imported-message-content").waitFor();
    await assistant.locator('[data-testid="imported-source-details"] > summary').click();
    const blocks = assistant.getByTestId("imported-source-block");
    assert.equal(await blocks.count(), 10);
    assert.deepEqual(await blocks.evaluateAll(elements => elements.map(element => element.dataset.kind)),
      ["reasoning", "text", "tool_call", "tool_result", "tool_call", "tool_result", "transcript", "media", "document", "unknown"]);
    assert.match(await blocks.nth(0).innerText(), /Użytkownik chce widok listy|Plan widoku/);
    assert.match(await blocks.nth(4).innerText(), /artifacts/);
    assert.match(await blocks.nth(6).innerText(), /Transkrypcja|voice_note/);
    assert.match(await blocks.nth(8).innerText(), /document/);
    await blocks.nth(9).getByText("Raw source block", { exact: true }).click();
    assert.match(await blocks.nth(9).locator("pre").innerText(), /"payload"/);
    await assistant.getByText("Complete preserved source message", { exact: true }).click();
    assert.deepEqual(JSON.parse(await assistant.getByTestId("imported-source-raw").innerText()), originalAssistantRaw);
    assert.equal(await assistant.locator("img,svg,audio,video,iframe,object,embed,script").count(), 0);
  });

  await check("editing imported native message preserves source and displays the current version without fetching Markdown media", async () => {
    const user = view().locator('[data-testid="message"][data-role="user"]').first();
    await user.getByTestId("edit-message").click();
    const editedText = 'CURRENT_NATIVE_EDIT_6482 **owner revision**\n![remote](https://example.invalid/native-edit.png)\n' +
      '<span style="background-image:url(https://example.invalid/native-css.png)">inert style</span>' +
      '<svg><image href="https://example.invalid/native-svg.png" /></svg>' +
      '<img src="https://example.invalid/native-html.png" onerror="window.__nativeSourceScriptRan=true">' +
      '<script>window.__nativeSourceScriptRan=true</script>';
    await user.locator("textarea").fill(editedText);
    await user.getByTestId("save-edit").click();
    const edited = view().locator('[data-testid="message"][data-role="user"]').filter({ has: page.locator(".body", { hasText: "CURRENT_NATIVE_EDIT_6482" }) });
    await edited.waitFor();
    assert.equal(await edited.locator(".body strong").innerText(), "owner revision");
    assert.equal(await edited.locator("img,svg,audio,video,iframe,object,embed,script").count(), 0);
    await edited.locator('[data-testid="imported-source-details"] > summary').click();
    assert.match((await edited.getByTestId("imported-source-reference").allInnerTexts()).join("\n"), /File bytes unresolved/);
    await edited.getByText("Complete preserved source message", { exact: true }).click();
    assert.deepEqual(JSON.parse(await edited.getByTestId("imported-source-raw").innerText()), originalUserRaw);
    const saved = await api("GET", `/api/conversations/${claudeConversation.id}/messages?all=1`);
    const active = saved.find(message => message.text === editedText && message.status === "active");
    assert.ok(active, "Current owner revision exists in native store");
    assert.deepEqual(active.metadata.export.raw, originalUserRaw);
    assert.ok(saved.some(message => message.id === sourceUser.id && message.status === "version"));
    assert.equal(await page.evaluate(() => window.__nativeSourceScriptRan), undefined);
  });

  await check("excluded native OpenAI thoughts, tool output and code are inspectable through the per-view display control", async () => {
    await chooseConversation("Migracja bazy danych");
    assert.equal(await view().locator('[data-testid="message"][data-status="excluded"]').count(), 0);
    await view().getByTestId("show-all-messages").check();
    await view().locator('[data-testid="message"][data-status="excluded"]').first().waitFor();
    const details = view().locator('[data-testid="message"][data-status="excluded"] [data-testid="imported-source-details"]');
    for (let index = 0; index < await details.count(); index++) await details.nth(index).locator(":scope > summary").click();
    assert.ok(await view().locator('[data-testid="imported-source-block"][data-kind="reasoning"]').count() >= 2);
    assert.ok(await view().locator('[data-testid="imported-source-block"][data-kind="code"]').count() >= 1);
    assert.ok(await view().locator('[data-testid="imported-source-block"][data-kind="tool_result"]').count() >= 1);
    assert.ok(await view().locator('[data-testid="message"][data-role="tool"]').count() >= 1);
    assert.match((await view().locator('[data-testid="imported-source-block"][data-kind="reasoning"]').allInnerTexts()).join("\n"), /thoughts|reasoning_recap/);
    await view().getByTestId("show-all-messages").uncheck();
    await view().locator('[data-testid="message"][data-status="excluded"]').first().waitFor({ state: "detached" });
    assert.equal(await view().getByTestId("include-history").isChecked(), true, "Display visibility does not alter request context policy");
  });

  await check("native OpenAI image references remain inspectable without automatic downloads", async () => {
    const openai = conversations.find(conversation => conversation.title === "Obrazy i dźwięk");
    assert.ok(openai, "Public OpenAI multimodal conversation is present");
    await chooseConversation(openai.title);
    const sourceDetails = view().getByTestId("imported-source-details");
    for (let index = 0; index < await sourceDetails.count(); index++) await sourceDetails.nth(index).locator(":scope > summary").click();
    assert.ok(await view().locator('[data-testid="imported-source-block"][data-kind="media"]').count() >= 1);
    assert.ok(await view().locator('[data-testid="imported-source-reference"][data-kind="media-pointer"]').count() >= 1);
    assert.equal(await view().locator("img,svg,audio,video,iframe,object,embed").count(), 0);
    assert.deepEqual(remoteRequests, []);
    assert.deepEqual(inferenceRequests, []);
    assert.deepEqual(pageErrors, []);
  });
  console.log(`[imported-message-content-native] ${groups.length}/${groups.length} groups passed; zero remote media or inference requests`);
} catch (error) {
  if (page) console.error("Current ChatView:", await view().innerText().catch(() => "unavailable"));
  console.error(`[imported-message-content-native] FAIL after ${groups.length} groups: ${error.stack}\nNative log:\n${log}`);
  throw error;
} finally {
  await browser?.close();
  if (server.exitCode === null) {
    const ended = new Promise(resolve => server.once("exit", resolve)); server.kill("SIGTERM");
    let timeout;
    await Promise.race([ended, new Promise(resolve => { timeout = setTimeout(() => { server.kill("SIGKILL"); resolve(); }, 3000); })]);
    clearTimeout(timeout);
  }
  rmSync(dataDir, { recursive: true, force: true });
}
