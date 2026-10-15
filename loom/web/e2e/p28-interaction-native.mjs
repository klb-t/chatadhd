#!/usr/bin/env node
// Real production bundle -> native Loom server (no model, no network): P28 step 2, interaction as data.
//   1. the kernel resolves the effective bindings of a device (presets), xr entries are recorded as data only;
//   2. the keyboard in the panel: held modifiers choose the axis pair of the arrows (plain = plane, Shift = depth/time, Ctrl = between branches, Alt = rotate);
//   3. a lock switches an alternative view (and the hint says where the lock state is only counted from presses);
//   4. a user overlay remaps a lock to zoom, a duplicate binding is a visible warning and nothing is blocked, taking the overlay back restores the preset;
//   5. the always-visible hint is in Polish with ui_language.
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
const complete = suiteCompletionGuard("p28-interaction-native", 5, groups);
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
async function check(name, run) { await run(); groups.push(name); console.log(`[p28-interaction-native] PASS ${name}`); }

try {
  const dataDir = mkdtempSync(path.join(tmpdir(), "loom-p28i-")); dirs.push(dataDir);
  await start(dataDir);
  await api("PATCH", "/api/config", { semantic_analysis: false });
  const ids = seed(dataDir);
  const overlay = (op) => api("POST", "/api/views/write", { overlay: "p28i", op });
  const byId = (r, id) => r.bindings.find(b => b.id === id);

  await check("the kernel resolves the effective bindings of a device: presets with provenance, xr and voice recorded as data only, no conflicts", async () => {
    const kbd = await api("GET", "/api/view-interaction?device=keyboard");
    assert.equal(kbd.schema, "loom.view_interaction/1");
    assert.ok(kbd.bindings.length >= 20 && kbd.bindings.every(b => b.device === "keyboard"));
    assert.equal(byId(kbd, "kbd.plane.left").action.pair, "plane");
    assert.deepEqual(byId(kbd, "kbd.depth_time.up").input.modifiers, ["shift"]);
    assert.deepEqual(byId(kbd, "kbd.branches.up").input.modifiers, ["ctrl"]);
    assert.deepEqual(byId(kbd, "kbd.rotate.up").input.modifiers, ["alt"]);
    assert.equal(byId(kbd, "kbd.lock.caps").action.kind, "switch_view");
    assert.equal(byId(kbd, "kbd.lock.caps").origin, "rom");
    assert.deepEqual(kbd.conflicts, []);
    const xr = await api("POST", "/api/view-interaction", { device: "xr" });
    assert.ok(xr.bindings.length > 0 && xr.bindings.every(b => b.status === "recorded"));
    const touch = await api("POST", "/api/view-interaction", { device: "touch" });
    assert.ok(byId(touch, "touch.hold.shift"));
    const all = await api("GET", "/api/view-interaction");
    assert.ok(["keyboard", "touch", "voice", "xr"].every(d => all.bindings.some(b => b.device === d)));
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
    await page.getByTestId("ve-hint-arrows").waitFor();
  };
  const show = async ({ declaration = "history_cone", focus = ids.v3 } = {}) => {
    await page.getByTestId("ve-declaration").selectOption(declaration);
    await page.getByTestId("ve-focus").selectOption(focus);
    await page.getByTestId("ve-show").click();
    await page.getByTestId("ve-result").waitFor();
  };
  const state = async () => {
    const el = page.getByTestId("ve-view-state");
    const get = async name => el.getAttribute(`data-${name}`);
    return { panX: Number(await get("pan-x")), panY: Number(await get("pan-y")), zoom: Number(await get("zoom")), angle: Number(await get("angle")), depth: Number(await get("depth")), declaration: await get("declaration"), focus: await get("focus") };
  };
  const last = () => page.getByTestId("ve-last-action").getAttribute("data-binding");
  const press = async key => { await page.keyboard.press(key); };

  await check("the keyboard: held modifiers choose the axis pair of the arrows (plain = plane, Shift = depth/time, Ctrl = between branches, Alt = rotate)", async () => {
    await openPanel();
    assert.match(await page.getByTestId("ve-hint-arrows").innerText(), /^Arrows now: plane/);
    await show();
    await press("ArrowLeft");
    assert.equal(await last(), "kbd.plane.left");
    assert.equal((await state()).panX, -40);
    await press("ArrowDown");
    assert.equal(await last(), "kbd.plane.down");
    assert.equal((await state()).panY, 40);
    // Shift: depth (vertical) re-evaluates the view with a deeper main source, time (horizontal) moves the focus to an older element
    await page.keyboard.down("Shift");
    assert.match(await page.getByTestId("ve-hint-arrows").innerText(), /^Arrows now: Shift: depth and time/);
    await press("ArrowDown");
    assert.equal(await last(), "kbd.depth_time.down");
    await page.waitForFunction(() => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-depth") === "1");
    await press("ArrowLeft");
    assert.equal(await last(), "kbd.depth_time.left");
    await page.waitForFunction(id => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-focus") !== id, ids.v3);
    assert.equal((await state()).panX, -40, "Shift did not move the plane");
    await page.keyboard.up("Shift");
    // Ctrl: between branches
    const before = (await state()).focus;
    await page.keyboard.down("Control");
    await press("ArrowRight");
    assert.equal(await last(), "kbd.branches.right");
    await page.keyboard.up("Control");
    await page.waitForFunction(id => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-focus") !== id, before);
    // Alt: rotate - left/right the direction of the view, up/down the perspective (the next view)
    await page.keyboard.down("Alt");
    await press("ArrowRight");
    assert.equal(await last(), "kbd.rotate.right");
    assert.equal((await state()).angle, 15);
    const decl = (await state()).declaration;
    await press("ArrowDown");
    assert.equal(await last(), "kbd.rotate.down");
    await page.keyboard.up("Alt");
    await page.waitForFunction(d => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-declaration") !== d, decl);
    // zoom, labels
    await press("+");
    assert.equal((await state()).zoom, 1.25);
    await press("l");
    assert.ok(await page.getByTestId("ve-label").count() > 0);
    // back to the starting view for the next groups
    await page.getByTestId("ve-declaration").selectOption("history_cone");
    await page.getByTestId("ve-focus").selectOption(ids.v3);
    await page.getByTestId("ve-show").click();
    await page.waitForFunction(() => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-declaration") === "history_cone");
  });

  await check("a lock switches an alternative view and back; where the browser cannot read the lock the hint says it is counted from presses", async () => {
    assert.equal(await page.getByTestId("ve-lock-caps").getAttribute("data-on"), "false");
    await press("CapsLock");
    assert.equal(await page.getByTestId("ve-lock-caps").getAttribute("data-on"), "true");
    await page.waitForFunction(() => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-declaration") === "age_lanes");
    assert.match(await page.getByTestId("ve-lock-caps").innerText(), /CapsLock on .*view age_lanes/);
    await page.getByTestId("ve-result").waitFor();
    assert.match(await page.getByTestId("ve-hint-inferred").innerText(), /^The state of CapsLock, NumLock, ScrollLock is counted from key presses/);
    await press("CapsLock");
    await page.waitForFunction(() => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-declaration") === "history_cone");
    assert.equal(await page.getByTestId("ve-lock-caps").getAttribute("data-on"), "false");
    await press("ScrollLock");
    assert.equal(await page.getByTestId("ve-lock-scroll").getAttribute("data-on"), "true");
    await page.waitForFunction(() => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-declaration") === "age_lanes");
    await press("ScrollLock");
    await page.waitForFunction(() => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-declaration") === "history_cone");
  });

  await check("everything is remappable by an overlay: a lock becomes zoom; a duplicate binding is a visible warning, nothing is blocked; taking the overlay back restores the preset", async () => {
    await overlay({ view: "interaction_bindings", op: "modify", id: "kbd.lock.caps", patch: { action: { kind: "zoom", factor: 1.5, declaration: null } } });
    const r = await api("GET", "/api/view-interaction?device=keyboard");
    assert.equal(byId(r, "kbd.lock.caps").action.kind, "zoom");
    assert.equal(byId(r, "kbd.lock.caps").provenance.length, 2);
    await page.getByTestId("ve-bindings-reload").click();
    await page.waitForFunction(() => /zoom x1\.5/.test(document.querySelector('[data-testid="ve-lock-caps"]')?.textContent ?? ""));
    const zoom0 = (await state()).zoom;
    await press("CapsLock");
    await page.waitForFunction(z => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-zoom") === String(z * 1.5), zoom0);
    assert.equal((await state()).declaration, "history_cone", "the lock no longer switches the view");
    await press("CapsLock");
    await page.waitForFunction(z => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-zoom") === String(z), zoom0);
    // a second binding for the same input: a warning in the hint, both listed, the last wins
    assert.equal(await page.getByTestId("ve-hint-conflict").count(), 0);
    await overlay({ view: "interaction_bindings", op: "add", id: "mine.caps", entry: { label: { pl: "Moje", en: "Mine" }, device: "keyboard", input: { lock: "caps", state: "on" }, action: { kind: "zoom", factor: 2 } } });
    const c = await api("GET", "/api/view-interaction?device=keyboard");
    assert.equal(c.conflicts.length, 1);
    assert.equal(c.conflicts[0].winner, "mine.caps");
    assert.ok(byId(c, "kbd.lock.caps") && byId(c, "mine.caps"), "nothing is blocked");
    await page.getByTestId("ve-bindings-reload").click();
    await page.getByTestId("ve-hint-conflict").waitFor();
    assert.match(await page.getByTestId("ve-hint-conflict").innerText(), /Conflicting bindings: kbd\.lock\.caps, mine\.caps\. The last one wins; none is blocked\./);
    await press("CapsLock");
    await page.waitForFunction(z => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-zoom") === String(z * 2), zoom0);
    await press("CapsLock");
    // an unknown thing is unsupported with its reason, the rest keeps working
    await overlay({ view: "interaction_bindings", op: "add", id: "mine.pedal", entry: { device: "keyboard", input: { foot_pedal: "left" }, action: { kind: "zoom" } } });
    await page.getByTestId("ve-bindings-reload").click();
    await page.getByTestId("ve-hint-unsupported").waitFor();
    assert.match(await page.getByTestId("ve-hint-unsupported").innerText(), /mine\.pedal/);
    // taking it back
    for (const id of ["kbd.lock.caps", "mine.caps", "mine.pedal"]) await api("POST", "/api/views/write", { overlay: "p28i", remove: true, view: "interaction_bindings", id });
    const back = await api("GET", "/api/view-interaction?device=keyboard");
    assert.equal(byId(back, "kbd.lock.caps").action.kind, "switch_view");
    assert.deepEqual(back.conflicts, []);
    await page.getByTestId("ve-bindings-reload").click();
    await page.waitForFunction(() => /view age_lanes/.test(document.querySelector('[data-testid="ve-lock-caps"]')?.textContent ?? ""));
    assert.equal(await page.getByTestId("ve-hint-conflict").count(), 0);
  });

  await check("the touch preset: on-screen held-modifier buttons act like held keys, and the hint is in Polish with ui_language", async () => {
    await page.getByTestId("ve-touch-shift").waitFor();
    const box = await page.getByTestId("ve-touch-shift").boundingBox();
    await page.mouse.move(box.x + 5, box.y + 5);
    await page.mouse.down();
    assert.equal(await page.getByTestId("ve-touch-shift").getAttribute("aria-pressed"), "true");
    assert.match(await page.getByTestId("ve-hint-arrows").innerText(), /Shift: depth and time/);
    await page.mouse.up();
    assert.equal(await page.getByTestId("ve-touch-shift").getAttribute("aria-pressed"), "false");
    await api("PUT", "/api/config/ui_language", { value: "pl" });
    await page.reload();
    await page.locator('[data-testid="shell-menu-toggle"], [data-testid="nav-view-engine"]').first().waitFor();
    if (await page.getByTestId("nav-view-engine").count() === 0) {
      await page.getByTestId("shell-menu-toggle").click();
      await page.getByTestId("shell-menu-workshop").click();
    }
    await openPanel();
    const arrows = await page.getByTestId("ve-hint-arrows").innerText();
    assert.match(arrows, /^Strzałki teraz: płaszczyzna/);
    assert.match(arrows, /Płaszczyzna: w lewo/);
    assert.match(await page.getByTestId("ve-hint-held").innerText(), /^Przytrzymaj Shift = głębia i czas · Ctrl = między gałęziami · Alt = obrót/);
    assert.match(await page.getByTestId("ve-hint-locks").innerText(), /^Blokady: CapsLock wyłączona → widok age_lanes/);
    assert.match(await page.getByTestId("ve-hint-inferred").innerText(), /^Stan blokad CapsLock, NumLock, ScrollLock jest liczony z naciśnięć klawiszy/);
    await page.keyboard.down("Shift");
    assert.match(await page.getByTestId("ve-hint-arrows").innerText(), /^Strzałki teraz: Shift: głębia i czas/);
    await page.keyboard.up("Shift");
    assert.match(await page.getByTestId("ve-last-action").innerText(), /^$/);
  });

  assert.deepEqual(pageErrors, []);
  complete.complete();
  console.log(`[p28-interaction-native] ${groups.length}/5 groups passed`);
} catch (error) {
  console.error(serverLog.slice(-2000));
  throw error;
} finally {
  await browser?.close();
  await stopChild(child).catch(() => {});
  for (const dir of dirs) rmSync(dir, { recursive: true, force: true });
}
