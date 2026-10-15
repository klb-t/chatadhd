#!/usr/bin/env node
// Real production bundle -> native Loom server (no model, no network): P28 step 1, the visualization engine with its 2D renderer.
//   The fixture is the first spatial preset of the spec (docs/architecture/WIDOK_SILNIK_WIZUALIZACJI_2026-10-15.md, section 3): a version line with a branch
//   ahead, callers on one side, dependencies on the other, the goal behind.
//   1. the kernel evaluates the shipped preset into a view model (HTTP);
//   2. the panel draws it: history ahead (up), callers left, dependencies right, the future behind (down), sectors as wedges, badges, depth fog;
//   3. every encoding is promoted or allowed with its reason, and the panel shows what the kernel says;
//   4. unknown kinds pass through to "unsupported" with a visible reason while the rest is drawn (declaration written to the user's overlay);
//   5. the node budget (preset, overridable) collapses elements and says so; Polish labels from ui_language.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { DatabaseSync } from "node:sqlite";
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
const complete = suiteCompletionGuard("p28-view-engine-native", 5, groups);
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
async function check(name, run) { await run(); groups.push(name); console.log(`[p28-view-engine-native] PASS ${name}`); }

// The graph of the fixture goes straight into the data directory's database (the server reads it per request).
function seed(dataDir) {
  const db = new DatabaseSync(path.join(dataDir, "chatadhd.db"));
  db.exec("PRAGMA busy_timeout = 5000");
  const ids = {};
  const day = n => new Date(Date.UTC(2026, 0, 1 + n)).toISOString().replace(".000Z", "Z");
  const node = (key, kind, n) => {
    ids[key] = `n_p28_${key}`;
    db.prepare("INSERT INTO nodes (id, kind, label, content, tags, metadata, created) VALUES (?, ?, ?, '', '[]', '{}', ?)").run(ids[key], kind, key, day(n));
  };
  let linkNo = 0;
  const link = (a, b, type) => db.prepare("INSERT INTO links (id, src, dst, link_type, weight, metadata, created) VALUES (?, ?, ?, ?, 1.0, '{}', ?)").run(`l_p28_${++linkNo}`, ids[a], ids[b], type, day(0));
  [["v1", "version"], ["v2", "version"], ["v3", "version"], ["v4", "version"], ["v5", "version"], ["fb1", "version"]].forEach(([k, kind], i) => node(k, kind, i * 10));
  node("caller_a", "function", 60); node("caller_b", "function", 61); node("libx", "module", 62); node("ship", "goal", 63);
  link("v2", "v1", "previous_version"); link("v3", "v2", "previous_version"); link("v4", "v3", "previous_version"); link("v5", "v4", "previous_version");
  link("fb1", "v2", "forked_from");
  link("caller_a", "v3", "calls"); link("caller_b", "v3", "calls");
  link("v3", "libx", "imports");
  link("v3", "ship", "serves_goal");
  db.close();
  return ids;
}

try {
  const dataDir = mkdtempSync(path.join(tmpdir(), "loom-p28-")); dirs.push(dataDir);
  await start(dataDir);
  await api("PATCH", "/api/config", { semantic_analysis: false });
  const ids = seed(dataDir);
  const mark = (page, key) => page.locator(`[data-testid="ve-mark"][data-id="${ids[key]}"]`);

  await check("the kernel evaluates the shipped preset into a view model: sectors, measures, encodings with a verdict, nothing unsupported", async () => {
    const list = await api("GET", "/api/view-model");
    assert.ok(list.declarations.entries.some(e => e.id === "history_cone" && e.origin === "rom"));
    for (const kind of ["measure_kinds", "coordinate_systems", "channels", "composition_kinds", "combination_rules", "rules"]) assert.ok(list[kind].entries.length > 0, kind);
    const model = await api("POST", "/api/view-model", { declaration: "history_cone", focus: ids.v3 });
    assert.equal(model.schema, "loom.view_model/1");
    assert.equal(model.elements.length, 10);
    assert.deepEqual(model.unsupported, []);
    const sectorOf = key => model.elements.find(e => e.id === ids[key]).sector;
    assert.deepEqual(["v2", "v4", "fb1"].map(sectorOf), ["past", "past", "past"]);
    assert.equal(sectorOf("caller_a"), "callers");
    assert.equal(sectorOf("libx"), "deps");
    assert.equal(sectorOf("ship"), "future");
    assert.deepEqual(model.groups.map(g => g.id).sort(), ["callers", "deps", "future", "past"]);
    for (const e of model.encodings) {
      assert.ok(e.status === "promoted" || e.status === "allowed", e.id);
      if (e.status === "allowed") assert.ok(e.reasons.length > 0, `${e.id} is allowed with an explanation`);
    }
    assert.ok(model.encodings.some(e => e.id === "e_hue" && e.channel === "hue"));
    const bad = await fetch(`${base}/api/view-model`, { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ declaration: "history_cone" }) });
    assert.equal(bad.ok, false, "a view without a focus is an error, not a guess");
  });

  const cachedShell = "/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell";
  const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || (existsSync(cachedShell) ? cachedShell : undefined);
  browser = await chromium.launch({ headless: true, ...(executablePath ? { executablePath } : {}) });
  const page = await browser.newPage({ viewport: { width: 1500, height: 1200 } });
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.goto(base);
  await page.getByTestId("shell-menu-toggle").click();
  await page.getByTestId("shell-menu-workshop").click();

  const openPanel = async () => {
    await page.getByTestId("nav-view-engine").click();
    await page.getByTestId("view-engine-panel").waitFor();
  };
  const show = async ({ declaration = "history_cone", focus = ids.v3, budget = "" } = {}) => {
    await page.getByTestId("ve-declaration").selectOption(declaration);
    await page.getByTestId("ve-focus").selectOption(focus);
    await page.getByTestId("ve-budget").fill(budget);
    await page.getByTestId("ve-show").click();
    await page.locator('[data-testid="ve-result"], [data-testid="ve-error"]').first().waitFor();
    assert.equal(await page.getByTestId("ve-error").count(), 0, await page.getByTestId("ve-error").allInnerTexts().then(a => a.join()));
  };
  const at = async (key) => {
    const m = mark(page, key);
    return { x: Number(await m.getAttribute("data-x")), y: Number(await m.getAttribute("data-y")) };
  };

  await check("the 2D renderer: history ahead, callers left, dependencies right, the future behind; wedges for the sectors; branch angles; badges", async () => {
    await openPanel();
    await show();
    assert.equal(await page.getByTestId("ve-canvas").getAttribute("data-system"), "sector");
    assert.equal(await page.locator('[data-testid="ve-mark"]').count(), 10);
    for (const sector of ["past", "callers", "deps", "future"]) assert.equal(await page.getByTestId(`ve-wedge-${sector}`).count(), 1, sector);
    assert.match(await page.getByTestId("ve-counts").innerText(), /^10 elements, \d+ edges$/);
    const focus = await at("v3");
    for (const key of ["v2", "v4", "fb1", "v5"]) assert.ok((await at(key)).y < focus.y, `${key} is ahead (up)`);
    for (const key of ["caller_a", "caller_b"]) assert.ok((await at(key)).x < focus.x, `${key} is on the left`);
    assert.ok((await at("libx")).x > focus.x, "dependencies are on the right");
    assert.ok((await at("ship")).y > focus.y, "the future is behind (down)");
    // branches are at fixed angles: the fork at v2 sends v1 and fb1 in different directions, v4/v5 stay on one line
    const [v1, fb1, v2] = [await at("v1"), await at("fb1"), await at("v2")];
    assert.notEqual(Math.round(v1.x), Math.round(fb1.x), "the two branches of the fork leave at different angles");
    assert.ok(Math.abs((v1.x - v2.x) * (fb1.y - v2.y) - (v1.y - v2.y) * (fb1.x - v2.x)) > 1, "v1 and fb1 are not on one line from the fork");
    assert.equal(await mark(page, "v3").getAttribute("data-focus"), "true");
    assert.ok(await page.locator('[data-testid="ve-badge"]').count() >= 10, "every element carries the badge of its kind");
    // depth from distance in 2D: the farther, the smaller (fog and desaturation come with it)
    const r = key => mark(page, key).locator("circle").getAttribute("r").then(Number);
    assert.ok(await r("v3") > await r("v1"), "the focus is drawn larger than the far past");
  });

  await check("every encoding shows its verdict; allowed ones show their reason, in the kernel's words", async () => {
    const model = await api("POST", "/api/view-model", { declaration: "history_cone", focus: ids.v3 });
    for (const e of model.encodings.filter(x => !x.position)) {
      const row = page.getByTestId(`ve-encoding-${e.id}`);
      assert.equal(await row.getAttribute("data-status"), e.status, e.id);
    }
    const allowed = model.encodings.find(e => e.status === "allowed" && !e.position);
    assert.ok(allowed, "the preset has at least one allowed encoding (it is a preset, not a filter)");
    const warning = allowed.reasons.find(r => r.level === "warning");
    assert.ok(warning);
    const shown = await page.getByTestId(`ve-reason-${allowed.id}`).allInnerTexts();
    assert.ok(shown.some(t => t.trim() === warning.text.en), shown.join("|"));
  });

  await check("unknown kinds pass through to unsupported with a visible reason; the rest of the view is still drawn", async () => {
    await api("POST", "/api/views/write", { overlay: "p28", op: { view: "view_declarations", op: "add", id: "with_unknowns", entry: {
      label: { en: "With unknowns", pl: "Z nieznanymi" },
      elements: [
        { id: "history", kind: "source", perspective: "version_history", weight: 1, depth: 4, role: "main" },
        { id: "distance", kind: "measure", measure: "distance_from_focus" },
        { id: "plane", kind: "coordinates", system: "sector", bind: { radius: "distance" }, sectors: [{ id: "past", sources: ["history"], center_deg: 0, width_deg: 90 }] },
        { id: "mds", kind: "coordinates", system: "mds" },
        { id: "smell", kind: "encoding", channel: "smell", measure: "distance" },
        { id: "motion", kind: "motion", of: "time" },
        { id: "e_size", kind: "encoding", channel: "size", measure: "distance", scale: { invert: true } },
      ] } } });
    await page.getByTestId("close-panel").click();
    await page.getByTestId("nav-view-engine").click();
    await page.getByTestId("ve-declaration").locator('option[value="with_unknowns"]').waitFor({ state: "attached" });
    await show({ declaration: "with_unknowns" });
    for (const element of ["mds", "smell", "motion"]) await page.getByTestId(`ve-unsupported-${element}`).waitFor();
    assert.match(await page.getByTestId("ve-unsupported-mds").innerText(), /mds/);
    assert.match(await page.getByTestId("ve-unsupported-smell").innerText(), /smell/);
    assert.match(await page.getByTestId("ve-unsupported-motion").innerText(), /motion/);
    assert.ok(await page.locator('[data-testid="ve-mark"]').count() >= 5, "the supported part is drawn");
    await api("POST", "/api/views/write", { overlay: "p28", remove: true, view: "view_declarations", id: "with_unknowns" });
  });

  await check("the budget is a preset the user overrides: elements collapse into group nodes and the panel says so; Polish words from ui_language", async () => {
    await page.getByTestId("close-panel").click();
    await page.getByTestId("nav-view-engine").click();
    await show({ budget: "4" });
    assert.equal(await page.locator('[data-testid="ve-mark"]').count(), 4);
    assert.match(await page.getByTestId("ve-budget-note").innerText(), /6 elements were collapsed into group nodes to stay within the budget of 4 nodes\./);
    assert.equal(await mark(page, "v3").count(), 1, "the focus never collapses");
    await show({ budget: "0" });
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="ve-mark"]').length === 10);
    assert.equal(await page.getByTestId("ve-budget-note").count(), 0, "0 = no budget");
    await api("PUT", "/api/config/ui_language", { value: "pl" });
    await page.reload();
    await page.locator('[data-testid="shell-menu-toggle"], [data-testid="nav-view-engine"]').first().waitFor();
    if (await page.getByTestId("nav-view-engine").count() === 0) {
      await page.getByTestId("shell-menu-toggle").click();
      await page.getByTestId("shell-menu-workshop").click();
    }
    assert.equal((await page.getByTestId("nav-view-engine").innerText()).trim(), "Silnik widoków");
    await page.getByTestId("nav-view-engine").click();
    await show({ budget: "4" });
    assert.match(await page.getByTestId("ve-budget-note").innerText(), /6 elementów zwinięto w węzły grupowe/);
    assert.match(await page.getByTestId("ve-counts").innerText(), /^Elementów: 4, krawędzi: \d+$/);
  });

  assert.deepEqual(pageErrors, []);
  complete.complete();
  console.log(`[p28-view-engine-native] ${groups.length}/5 groups passed`);
} catch (error) {
  console.error(serverLog.slice(-2000));
  throw error;
} finally {
  await browser?.close();
  await stopChild(child).catch(() => {});
  for (const dir of dirs) rmSync(dir, { recursive: true, force: true });
}
