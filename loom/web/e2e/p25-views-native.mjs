#!/usr/bin/env node
// Real production bundle -> native Loom server (no model, no network): P25.
//   1. the page sizes of the web views are settings (loom_web_message_window, loom_web_graph_page): presets 500 / 2000, 0 = no cut, and the
//      chat view and the graph view page by exactly what the settings say ("Load older" buttons, labels by ui_language pl);
//   2. the "Perspektywy" panel: choose several perspectives, render them side by side;
//   3. the scope-explain view of the conversation settings: the value of a key and which level supplied it.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";
import { stopChild, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const loomRoot = path.resolve(webRoot, "..");
const serverBin = process.env.LOOM_SERVER_BIN || path.join(loomRoot, "build/dev/server/loom-server");
const dist = path.join(webRoot, "dist");
assert.ok(existsSync(serverBin), `Build loom-server first: ${serverBin}`);
assert.ok(existsSync(path.join(dist, "index.html")), "Run npm run build first");
const groups = [], pageErrors = [], dirs = [];
const complete = suiteCompletionGuard("p25-views-native", 4, groups);
const MESSAGES = 12, MESSAGE_WINDOW = 5, GRAPH_PAGE = 4;
let browser, child, serverLog = "", base = "";

async function freePort() {
  const probe = createServer();
  await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
  const { port } = probe.address();
  await new Promise(resolve => probe.close(resolve));
  return port;
}
async function start(dataDir) {
  const port = await freePort();
  base = `http://127.0.0.1:${port}`;
  child = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", dataDir, "--static-dir", dist], { cwd: loomRoot, stdio: ["ignore", "pipe", "pipe"] });
  for (const output of [child.stdout, child.stderr]) output.on("data", chunk => { serverLog += chunk.toString(); });
  const deadline = Date.now() + 20000;
  for (;;) {
    try { if ((await fetch(`${base}/api/healthz`)).ok) return; } catch { /* startup */ }
    if (child.exitCode !== null) throw Error(`Native server exited: ${serverLog}`);
    if (Date.now() > deadline) throw Error(`Native server startup timeout: ${serverLog}`);
    await new Promise(resolve => setTimeout(resolve, 100));
  }
}
async function api(method, url, body) {
  const response = await fetch(`${base}${url}`, { method, ...(body === undefined ? {} : { headers: { "content-type": "application/json" }, body: JSON.stringify(body) }) });
  assert.ok(response.ok, `${method} ${url}: ${response.status} ${await response.clone().text()}`);
  return response.json();
}
async function check(name, run) { await run(); groups.push(name); console.log(`[p25-views-native] PASS ${name}`); }

try {
  const dataDir = mkdtempSync(path.join(tmpdir(), "loom-p25-")); dirs.push(dataDir);
  await start(dataDir);
  await api("PATCH", "/api/config", { semantic_analysis: false });

  await check("page sizes are settings: presets 500 / 2000 in the config and the field registry", async () => {
    const config = await api("GET", "/api/config");
    assert.equal(config.loom_web_message_window, 500);
    assert.equal(config.loom_web_graph_page, 2000);
    const fields = (await api("GET", "/api/settings/fields")).fields;
    for (const key of ["loom_web_message_window", "loom_web_graph_page"]) {
      const field = fields.find(f => f.key === key);
      assert.ok(field, `${key} is in the settings registry`);
      assert.equal(field.type, "integer");
    }
    await api("PATCH", "/api/config", { loom_web_message_window: MESSAGE_WINDOW, loom_web_graph_page: GRAPH_PAGE, ui_language: "pl", temperature: 0.3 });
    const form = new FormData();
    const rows = Array.from({ length: MESSAGES }, (_, i) => ({ role: i % 2 ? "assistant" : "user", content: `p25 message number ${i + 1}` }));
    form.append("file", new Blob([JSON.stringify(rows)]), "p25-paging.json");
    const imported = await fetch(`${base}/api/import`, { method: "POST", body: form });
    assert.ok(imported.ok);
    const events = (await imported.text()).split(/\r?\n/).filter(line => line.startsWith("data: ")).map(line => JSON.parse(line.slice(6)));
    assert.ok(events.some(event => event.type === "done") && !events.some(event => event.type === "error"), JSON.stringify(events));
  });

  const cachedShell = "/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell";
  const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || (existsSync(cachedShell) ? cachedShell : undefined);
  browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  const page = await browser.newPage({ viewport: { width: 1500, height: 1100 } });
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.goto(base.replace(/\/?$/, "/#warsztat"));
  await page.getByTestId("conv-item").first().click();
  const messages = () => page.locator('[data-testid="message"]');

  await check("the chat view pages by loom_web_message_window; Load older reaches every message (Polish label)", async () => {
    await messages().first().waitFor();
    await page.getByTestId("chat-load-older").waitFor();
    assert.equal(await messages().count(), MESSAGE_WINDOW);
    assert.match(await page.getByTestId("chat-load-older").innerText(), /Wczytaj starsze wiadomości/);
    await page.getByTestId("chat-load-older").click();
    await page.waitForFunction(n => document.querySelectorAll('[data-testid="message"]').length === n, MESSAGE_WINDOW * 2);
    await page.getByTestId("chat-load-older").click();
    await page.waitForFunction(n => document.querySelectorAll('[data-testid="message"]').length === n, MESSAGES);
    assert.equal(await page.getByTestId("chat-load-older").count(), 0, "nothing older is left");
  });

  await check("the graph view pages by loom_web_graph_page; Load older adds the rest", async () => {
    await page.getByTestId("nav-graph").click();
    const counter = page.getByTestId("graph-page-count");
    await counter.waitFor();
    assert.equal((await counter.innerText()).trim(), `${GRAPH_PAGE} of ${MESSAGES} messages`);
    await page.getByTestId("graph-load-more").click();
    await page.waitForFunction(text => document.querySelector('[data-testid="graph-page-count"]')?.textContent?.trim() === text, `${GRAPH_PAGE * 2} of ${MESSAGES} messages`);
    await page.getByTestId("graph-load-more").click();
    await page.waitForFunction(text => document.querySelector('[data-testid="graph-page-count"]')?.textContent?.trim() === text, `${MESSAGES} of ${MESSAGES} messages`);
    assert.equal(await page.getByTestId("graph-load-more").count(), 0);
    await page.getByTestId("close-panel").click();
  });

  await check("Perspektywy: several perspectives side by side; scope explain shows the level that supplied the value", async () => {
    const list = (await api("GET", "/api/perspectives")).perspectives;
    assert.ok(list.length >= 2, "the kernel ships at least two perspectives");
    const navigation = page.getByTestId("nav-perspectives");
    assert.equal((await navigation.innerText()).trim(), "Perspektywy");
    await navigation.click();
    await page.getByTestId("perspectives-panel").waitFor();
    const chosen = list.slice(0, 2).map(p => p.id);
    for (const id of chosen) await page.getByTestId(`perspective-choose-${id}`).check();
    await page.getByTestId("perspectives-show").click();
    await page.getByTestId("perspectives-side-by-side").waitFor();
    for (const id of chosen) await page.getByTestId(`perspective-col-${id}`).waitFor();
    assert.equal(await page.locator('[data-testid^="perspective-col-"]').count(), chosen.length);
    assert.match(await page.getByTestId(`perspective-counts-${chosen[0]}`).innerText(), /^Węzłów: \d+, krawędzi: \d+$/);
    assert.equal(await page.getByTestId("perspectives-error").count(), 0);
    await page.getByTestId("close-panel").click();

    const native = await api("POST", "/api/scopes/resolve", { key: "temperature" });
    assert.equal(native.defined, true);
    await page.getByTestId("scope-explain").locator("summary").click();
    await page.getByTestId("scope-explain-key").fill("temperature");
    await page.getByTestId("scope-explain-run").click();
    await page.getByTestId("scope-explain-result").waitFor();
    assert.match(await page.getByTestId("scope-explain-value").innerText(), /^Wartość: 0\.3 \(z poziomu: /);
    const supplied = page.locator('[data-testid^="scope-level-"][data-role="supplied"]');
    assert.equal(await supplied.count(), 1, "exactly one level supplied the value");
    assert.match(await supplied.first().innerText(), /dał wartość/);
    assert.equal(await page.locator('[data-testid^="scope-level-"]').count(), native.chain.length);
    assert.equal(await page.getByTestId("scope-explain-error").count(), 0);
  });

  assert.deepEqual(pageErrors, []);
  complete.complete();
  console.log(`[p25-views-native] ${groups.length}/4 groups passed`);
} catch (error) {
  console.error(serverLog.slice(-2000));
  throw error;
} finally {
  await browser?.close();
  await stopChild(child).catch(() => {});
  for (const dir of dirs) rmSync(dir, { recursive: true, force: true });
}
