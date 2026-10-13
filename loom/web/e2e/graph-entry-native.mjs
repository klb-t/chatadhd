#!/usr/bin/env node
// Real production bundle -> native Loom server (no model, no network): the graph entry panel (R45/R52/R53).
// Counters of interfaces, methods and perspectives come from real registries and must be numbers, not "unknown";
// the panel hides/shows; an overlay profile hides an area (everything is user-configurable).
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
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
const groups = [], pageErrors = [];
const complete = suiteCompletionGuard("graph-entry-native", 3, groups);
const dirs = [];
let browser, child, serverLog = "";

async function freePort() {
  const probe = createServer();
  await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
  const { port } = probe.address();
  await new Promise(resolve => probe.close(resolve));
  return port;
}
async function start(dataDir) {
  const port = await freePort(), base = `http://127.0.0.1:${port}`;
  child = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", dataDir, "--static-dir", dist], {
    cwd: loomRoot, stdio: ["ignore", "pipe", "pipe"] });
  for (const output of [child.stdout, child.stderr]) output.on("data", chunk => { serverLog += chunk.toString(); });
  const deadline = Date.now() + 20000;
  for (;;) {
    try { if ((await fetch(`${base}/api/healthz`)).ok) return base; } catch { /* startup */ }
    if (child.exitCode !== null) throw Error(`Native server exited: ${serverLog}`);
    if (Date.now() > deadline) throw Error(`Native server startup timeout: ${serverLog}`);
    await new Promise(resolve => setTimeout(resolve, 100));
  }
}
async function check(name, run) { await run(); groups.push(name); console.log(`[graph-entry-native] PASS ${name}`); }
const area = (page, id) => page.getByTestId(`graph-entry-area-${id}`);
const countText = async (page, areaId, name) => (await area(page, areaId).getByTestId(`graph-entry-count-${name}`).innerText()).trim();
async function openPanel(page, base) {
  await page.goto(base);
  await page.getByTestId("nav-graph-entry").click();
  await page.getByTestId("graph-entry-panel").waitFor();
}

try {
  const cachedShell = "/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell";
  const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || (existsSync(cachedShell) ? cachedShell : undefined);
  browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  const dataDir = mkdtempSync(path.join(tmpdir(), "loom-graph-entry-")); dirs.push(dataDir);
  const base = await start(dataDir);
  const page = await browser.newPage();
  page.on("pageerror", error => pageErrors.push(error.message));
  const view = await (await fetch(`${base}/api/graph/entry`)).json();

  await check("panel renders the areas of the native view with the seed", async () => {
    await openPanel(page, base);
    assert.ok(view.areas.length >= 14);
    for (const a of view.areas) await area(page, a.id).waitFor();
    assert.equal(await page.locator('[data-testid^="graph-entry-area-"]').count(), view.areas.length);
    assert.ok(await page.locator('[data-testid^="graph-entry-seed:"]').count() >= 5, "zero seed elements are rendered");
    assert.equal(await page.getByTestId("graph-entry-error").count(), 0);
  });

  await check("interfaces, methods and perspectives show real numbers that equal their registries", async () => {
    const shown = { interfaces: ["interfaces", "interfaces"], methods: ["methods", "methods.registry"], perspectives: ["perspectives", "perspectives.store"] };
    for (const [id, [areaId, name]] of Object.entries(shown)) {
      const native = view.areas.find(a => a.id === areaId).counts[name];
      assert.equal(native.known, true, `${id} is a registry count, not unknown`);
      assert.ok(Number.isInteger(native.value) && native.value > 0, `${id} > 0`);
      const text = await countText(page, areaId, name);
      assert.match(text, /^\d+/, `${id} renders a number: ${text}`);
      assert.equal(Number(/^\d+/.exec(text)[0]), native.value);
      assert.doesNotMatch(text, /unknown/i);
    }
    // Registry sizes equal the sources: C ABI functions and CLI commands behind the interface breakdown.
    const by = view.areas.find(a => a.id === "interfaces").counts.interfaces.by;
    assert.deepEqual(Object.keys(by).sort(), ["c_abi", "cli", "http"]);
    assert.ok(by.c_abi > 100 && by.http > 50 && by.cli > 10);
    const header = await import("node:fs").then(fs => fs.readFileSync(path.join(loomRoot, "include/loom/loom.h"), "utf8"));
    assert.equal(by.c_abi, new Set([...header.matchAll(/LOOM_API\s+[^;(]*?\b(loom_\w+)\s*\(/g)].map(m => m[1])).size);
    const interfacesText = await area(page, "interfaces").innerText();
    assert.match(interfacesText, /c_abi: \d+/);
  });

  await check("panel hides and shows; an overlay hides an area (user configuration)", async () => {
    await page.getByTestId("nav-graph-entry").click();
    await page.getByTestId("graph-entry-panel").waitFor({ state: "detached" });
    await page.getByTestId("nav-graph-entry").click();
    await page.getByTestId("graph-entry-panel").waitFor();
    await area(page, "perspectives").waitFor();
    await stopChild(child);
    const overlayDir = mkdtempSync(path.join(tmpdir(), "loom-graph-entry-overlay-")); dirs.push(overlayDir);
    mkdirSync(path.join(overlayDir, "profiles"));
    writeFileSync(path.join(overlayDir, "profiles/graph_entry.pack"), JSON.stringify({
      schema: "loom.runtime_profile_overlay/1", domain: "graph_entry", overrides: { areas: { perspectives: { hidden: true } } } }));
    const base2 = await start(overlayDir);
    await openPanel(page, base2);
    await area(page, "methods").waitFor();
    assert.equal(await area(page, "perspectives").count(), 0, "hidden area is not shown");
    assert.equal(await area(page, "interfaces").count(), 1);
  });
  assert.deepEqual(pageErrors, []);
  complete.complete();
  console.log(`[graph-entry-native] ${groups.length}/3 groups passed`);
} catch (error) {
  console.error(serverLog.slice(-2000));
  throw error;
} finally {
  await browser?.close();
  await stopChild(child).catch(() => {});
  for (const dir of dirs) rmSync(dir, { recursive: true, force: true });
}
