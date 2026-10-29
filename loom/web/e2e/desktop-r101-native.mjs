#!/usr/bin/env node
// R101 P0 (PULPIT_R101_2026-10-28 §7, §10, §12; acceptance, the REAL loom-server, no model). Polish, a computer (1280x800) and a phone
// (390x844). The desktop is always under the windows: (1) close every window → the desktop stays, a window opens from it, a sentence
// written on it goes to the chat composer; (2) minimize all (Ctrl+Shift+D) → the desktop is seen → a minimized window comes back;
// (3) last_window_close=close_app → the end screen with its advisory → back to the desktop; (4) B3: with no section the emergency path
// is there; switched off, the desktop says so and brings it back on request. R101 D2 (§3.2, §4, §12, §14 row D2): the desktop is the preset
// pulpit_graf - the first pins as icons (written once into the graph) over the faint graph on a canvas; a tap on "Rozmowy" opens the
// conversation list window, Back brings the desktop; P0's sections are behind "Więcej…" and a long press; pulpit_ikony drops the graph.
// R101 D3 (§3.1, §8, §14 row D3): the other concepts are presets of the same mechanism, each reachable by a sentence and reversible -
// „pulpit jako ikony” (the graph goes), „pulpit jako start” (a tile for every kind of window_kinds.json, the planned ones greyed),
// „pulpit graf żywy” (the graph behind follows the focus of the navigator window), „pusty pulpit”, pulpit_karta, pulpit_polka, and
// desktop_minimized=icons (a minimized chat is an icon at the bottom; a tap restores it).
// R101 D4 (§5, §14 row D4): „Do kosza” on a conversation icon → off the list, badge 1 on the trash → the Trash window lists it →
// „przywróć z kosza …” by a sentence → back on the list; „Opróżnij kosz” asks first.
// With M1_SHOTS_DIR=<dir> the screenshots go there.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { stableShot } from "./stable-shot.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
const SHOTS = process.env.M1_SHOTS_DIR || mkdtempSync(path.join(tmpdir(), "loom-desktop-shots-"));
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[desktop-r101] build loom-server and `npm run build` first"); process.exit(2); }

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-desktop-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[desktop-r101] PASS ${name}`); } catch (e) { failed = true; console.error(`[desktop-r101] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };
const setKey = (key, value) => api("PUT", `/api/config/${key}`, { value });
const getKey = async key => { const cfg = await api("GET", "/api/config"); return (cfg.config ?? cfg)[key]; };
const KINDS = JSON.parse(readFileSync(path.join(LOOM, "web/src/shell/data/window_kinds.json"), "utf8")).kinds;

try {
  await waitUp(`${base}/api/healthz`);
  await setKey("ui_language", "pl");
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const errors = [];
  for (const [device, viewport] of [["computer", { width: 1280, height: 800 }], ["phone", { width: 390, height: 844 }]]) {
    const fresh = async () => {
      const ctx = await browser.newContext({ viewport });
      const p = await ctx.newPage();
      p.on("pageerror", e => errors.push(`${device}: ${e}`));
      dump = () => p.evaluate(() => document.body.innerText.slice(-900));
      await p.goto(base + "/");
      await p.getByTestId("chat-input").waitFor();
      return p;
    };
    const desk = p => p.getByTestId("desktop");
    const seen = async p => { await desk(p).waitFor({ state: "visible" }); assert.notEqual(await desk(p).getAttribute("data-cover"), "full"); };
    const covered = async p => { await desk(p).waitFor({ state: "hidden" }); assert.equal(await desk(p).getAttribute("data-cover"), "full"); };
    const closeChat = async p => {
      await p.getByTestId("shell-menu-toggle").click();
      await p.getByTestId("shell-menu-windows").click();
      await p.locator('[data-testid="windows-entry"][data-window="w_chat"]').getByTestId("windows-entry-close").click();
    };

    let p = await fresh();
    await step(`${device}: the desktop is mounted under the maximized chat, hidden and inert`, async () => {
      assert.equal(await desk(p).count(), 1);
      await covered(p);
    });
    await step(`${device}: close every window → the desktop stays with every kind of window (planned ones greyed with a reason)`, async () => {
      await closeChat(p);
      await seen(p);
      assert.equal(await p.getByTestId("chat-input").count(), 0);
      // R101 D2: pulpit_graf - the first pins of the declaration as icons, the faint graph behind them on one canvas
      const icons = p.getByTestId("desktop-icons");
      await p.waitForFunction(() => document.querySelector('[data-testid="desktop-icons"]')?.getAttribute("data-state") === "ok", null, { timeout: 15000 });
      assert.equal(await icons.getAttribute("data-declaration"), "pulpit_graf");
      const labels = await p.getByTestId("desktop-icon").allInnerTexts();
      assert.deepEqual(labels.map(t => t.split("\n").pop().trim()), ["Ty", "Moje projekty", "Rozmowy", "Nowy czat", "Mój graf", "Ustawienia", "Kosz"]);
      const trash = p.locator('[data-testid="desktop-icon"][data-address="trash:"]');
      // R101 D4: the trash is a window now - no longer greyed
      assert.equal(await trash.getAttribute("data-status"), "available"); assert.ok(!(await trash.isDisabled()), "the trash opens (D4)");
      const graph = p.getByTestId("desktop-graph");
      await p.waitForFunction(() => Number(document.querySelector('[data-testid="desktop-graph"]')?.getAttribute("data-nodes")) > 3, null, { timeout: 15000 });
      assert.ok(Number(await graph.getAttribute("data-edges")) > 0, "real edges behind");
      assert.ok(Number(await graph.getAttribute("data-alpha")) < 0.5, "faint");
      assert.equal(await p.getByTestId("desktop-emergency").count(), 0, "icons are a path: no emergency button");
      await stableShot(p, path.join(SHOTS, `${device}-desktop-graf.png`));
      // P0's sections stay reachable behind "Więcej…"
      assert.equal(await p.getByTestId("desktop-open-kind").count(), 0);
      await p.getByTestId("desktop-more").click();
      const kinds = p.getByTestId("desktop-open-kind");
      assert.ok(await kinds.count() >= 25, `kinds on the desktop: ${await kinds.count()}`);
      const planned = p.locator('[data-testid="desktop-open-kind"][data-status="planned"]');
      assert.ok(await planned.count() >= 1);
      assert.ok(await planned.first().isDisabled());
      assert.ok((await planned.first().getAttribute("title") ?? "").length > 5, "a planned kind carries its reason");
      await stableShot(p, path.join(SHOTS, `${device}-desktop-closed.png`));
    });
    await step(`${device}: D2 - tap "Rozmowy" → the conversation list window → Back → the desktop with its icons`, async () => {
      await p.locator('[data-testid="desktop-icon"][data-address="conversations:"]').click();
      await covered(p);
      await p.getByTestId("panel-conversations").waitFor();
      assert.ok(await p.getByTestId("panel-conversations").getByRole("button").count() >= 1, "the conversation list is in the window");
      await stableShot(p, path.join(SHOTS, `${device}-desktop-rozmowy.png`));
      await p.getByTestId("close-panel").click();
      await seen(p);
      await p.locator('[data-testid="desktop-icon"][data-address="conversations:"]').waitFor();
    });
    await step(`${device}: D2 - a long press on the empty desktop opens "Więcej…" (every kind of window)`, async () => {
      if (await p.getByTestId("desktop-more").getAttribute("aria-expanded") === "true") await p.getByTestId("desktop-more").click();
      await p.getByTestId("desktop-more-panel").waitFor({ state: "detached" });
      await p.mouse.move(viewport.width - 6, viewport.height - 6);
      await p.mouse.down(); await p.waitForTimeout(900); await p.mouse.up();
      await p.getByTestId("desktop-more-panel").waitFor();
      assert.ok(await p.getByTestId("desktop-open-kind").count() >= 25);
    });
    await step(`${device}: a window opens from the desktop (Graf), closing it brings the desktop back`, async () => {
      if (await p.getByTestId("desktop-more").getAttribute("aria-expanded") !== "true") await p.getByTestId("desktop-more").click();
      await p.locator('[data-testid="desktop-open-kind"][data-kind="navigator"]').click();
      await covered(p);
      await p.getByTestId("close-panel").click();
      await seen(p);
    });
    await step(`${device}: a sentence on the desktop goes to the chat composer of a chat window opened for it`, async () => {
      await p.getByTestId("desktop-sentence-input").fill("pulpit mówi dzień dobry");
      await p.getByTestId("desktop-sentence-send").click();
      await covered(p);
      await p.getByTestId("chat-input").waitFor();
      await p.waitForFunction(() => document.body.innerText.includes("pulpit mówi dzień dobry") || document.querySelector('[data-testid="chat-input"]')?.value === "pulpit mówi dzień dobry", null, { timeout: 15000 });
    });
    await p.context().close();

    p = await fresh();
    await step(`${device}: minimize all (Ctrl+Shift+D) → the desktop is seen → the minimized chat comes back from it`, async () => {
      await p.locator("body").click({ position: { x: 5, y: Math.floor(viewport.height / 2) } });
      await p.keyboard.press("Control+Shift+D");
      await seen(p);
      // R101 D3: desktop_minimized preset - a list (phone) or an icon on the strip at the bottom (computer)
      const item = p.locator('[data-testid="desktop-restore-item"][data-window="w_chat"], [data-testid="desktop-minimized-icon"][data-window="w_chat"]');
      await item.waitFor();
      await stableShot(p, path.join(SHOTS, `${device}-desktop-minimized.png`));
      await item.click();
      await covered(p);
      await p.getByTestId("chat-input").waitFor();
    });
    await p.context().close();

    await setKey("last_window_close", "close_app");
    p = await fresh();
    await step(`${device}: last_window_close=close_app → the end screen with the advisory that the browser may refuse`, async () => {
      await closeChat(p);
      await p.getByTestId("app-ended").waitFor();
      assert.match(await p.getByTestId("app-ended-advisory").innerText(), /przeglądarka może odmówić/i);
      await stableShot(p, path.join(SHOTS, `${device}-ended.png`));
      await p.getByTestId("app-ended-back").click();
      await p.getByTestId("app-ended").waitFor({ state: "detached" });
      await seen(p);
    });
    await p.context().close();
    await setKey("last_window_close", null);

    await setKey("desktop_sections", []);
    p = await fresh();
    await step(`${device}: B3 - no section on the desktop: the emergency path is there and opens a chat window`, async () => {
      await closeChat(p);
      await seen(p);
      assert.equal(await p.getByTestId("desktop-open-kind").count(), 0);
      assert.equal(await p.getByTestId("desktop-icon").count(), 0, "P1: the icons are a section too; with none, the emergency path stays");
      await p.getByTestId("desktop-emergency").click();
      await covered(p);
      await p.getByTestId("chat-input").waitFor();
    });
    await p.context().close();
    await setKey("desktop_emergency_path", false);
    p = await fresh();
    await step(`${device}: B3 - emergency path switched off: the desktop says there is no way and brings it back on request`, async () => {
      await closeChat(p);
      await seen(p);
      assert.equal(await p.getByTestId("desktop-emergency").count(), 0);
      const adv = p.locator('[data-testid="desktop-advisory"][data-code="desktop_no_path"]');
      await adv.waitFor();
      await p.getByTestId("desktop-advisory-fix").click();
      await p.getByTestId("desktop-emergency").waitFor();
      let stored;
      for (const end = Date.now() + 5000; Date.now() < end && stored !== true; await new Promise(r => setTimeout(r, 100))) { const cfg = await api("GET", "/api/config"); stored = (cfg.config ?? cfg).desktop_emergency_path; }
      assert.equal(stored, true, "the fix writes desktop_emergency_path=true");
      assert.match(await p.getByTestId("window-announce").innerText(), /Pulpit/);
    });
    await p.context().close();
    await setKey("desktop_sections", null);
    await setKey("desktop_emergency_path", null);
    await setKey("desktop_declaration", "pulpit_ikony");
    p = await fresh();
    await step(`${device}: D2 - desktop_declaration=pulpit_ikony: the same icons, no graph behind`, async () => {
      await closeChat(p);
      await seen(p);
      await p.waitForFunction(() => document.querySelector('[data-testid="desktop-icons"]')?.getAttribute("data-state") === "ok", null, { timeout: 15000 });
      assert.equal(await p.getByTestId("desktop-icons").getAttribute("data-declaration"), "pulpit_ikony");
      assert.equal(await p.getByTestId("desktop-icon").count(), 7);
      assert.equal(await p.getByTestId("desktop-graph").count(), 0);
    });
    await p.context().close();
    await setKey("desktop_declaration", null);

    // ---------- R101 D3 ----------
    const decl = p => desk(p).getAttribute("data-declaration");
    const say = async (p, text, want) => {
      await p.getByTestId("desktop-sentence-input").fill(text);
      await p.getByTestId("desktop-sentence-send").click();
      await covered(p);
      let v; for (const end = Date.now() + 15000; Date.now() < end && v !== want; await new Promise(r => setTimeout(r, 150))) v = await getKey("desktop_declaration");
      assert.equal(v, want, `„${text}” writes desktop_declaration`);
      await closeChat(p);
      await seen(p);
      await p.waitForFunction(w => document.querySelector('[data-testid="desktop"]')?.getAttribute("data-declaration") === w, want, { timeout: 10000 });
    };
    p = await fresh();
    await step(`${device}: D3 - „pulpit jako ikony” → the graph behind disappears, the icons stay`, async () => {
      await closeChat(p); await seen(p);
      await p.getByTestId("desktop-graph").waitFor();
      await say(p, "pulpit jako ikony", "pulpit_ikony");
      await p.waitForFunction(() => document.querySelector('[data-testid="desktop-icons"]')?.getAttribute("data-state") === "ok", null, { timeout: 15000 });
      assert.equal(await p.getByTestId("desktop-graph").count(), 0);
      assert.equal(await p.getByTestId("desktop-icon").count(), 7);
    });
    await step(`${device}: D3 - „pulpit jako start” → a tile for every kind of window, the planned ones greyed with a reason`, async () => {
      await say(p, "pulpit jako start", "pulpit_start");
      const tiles = p.locator('[data-testid="desktop-icon"][data-source="kinds"]');
      await tiles.first().waitFor();
      assert.equal(await tiles.count(), KINDS.length, "one tile per kind of window_kinds.json");
      const planned = p.locator('[data-testid="desktop-icon"][data-source="kinds"][data-status="planned"]');
      assert.equal(await planned.count(), KINDS.filter(k => k.status !== "present").length);
      assert.ok(await planned.first().isDisabled()); assert.ok(((await planned.first().getAttribute("title")) ?? "").length > 5, "greyed with its reason");
      assert.ok(await p.locator('[data-testid="desktop-icon"][data-source="layouts"]').count() >= 1, "the layouts are tiles too");
      assert.equal(await p.getByTestId("desktop-graph").count(), 0);
      await stableShot(p, path.join(SHOTS, `${device}-desktop-start.png`));
      await p.locator('[data-testid="desktop-icon"][data-source="kinds"][data-kind="navigator"]').click();
      await covered(p);
      await p.getByTestId("close-panel").click();
      await seen(p);
    });
    await step(`${device}: D3 - „pulpit graf żywy” → the graph behind follows the focus of the navigator window`, async () => {
      await say(p, "pulpit graf żywy", "pulpit_graf_zywy");
      const g = p.getByTestId("desktop-graph");
      await g.waitFor();
      assert.equal(await g.getAttribute("data-focus"), "party:self", "no navigator window: the fallback");
      // the navigator writes its focus once graph.navigate has answered (wait for the answer, not for a fixed time)
      const answered = p.waitForResponse(r => r.url().includes("/api/graph/navigate") && r.request().postData()?.includes("area:knowledge"), { timeout: 15000 });
      await p.locator('[data-testid="desktop-icon"][data-address="area:knowledge"]').click();
      await covered(p);
      await answered; await p.waitForTimeout(300);
      await p.evaluate(() => (document.activeElement)?.blur?.());
      await p.keyboard.press("Control+Shift+D");
      await seen(p);
      const focusNow = async () => [await p.getByTestId("desktop-graph").getAttribute("data-focus"), await desk(p).getAttribute("data-focus-from")];
      await p.waitForFunction(() => document.querySelector('[data-testid="desktop-graph"]')?.getAttribute("data-focus") === "area:knowledge", null, { timeout: 10000 })
        .catch(async e => { throw new Error(`the graph behind did not follow the navigator: ${await focusNow()} (${e.message})`); });
      assert.equal(await desk(p).getAttribute("data-focus-from"), "window");
      await stableShot(p, path.join(SHOTS, `${device}-desktop-graf-zywy.png`));
      // the navigator moves to another focus (the icon "Ty" brings the same window back on party:self) → the graph behind moves too
      const again = p.waitForResponse(r => r.url().includes("/api/graph/navigate") && r.request().postData()?.includes("party:self"), { timeout: 15000 });
      await p.locator('[data-testid="desktop-icon"][data-address="party:self"]').click();
      await again; await p.waitForTimeout(300);
      await p.evaluate(() => (document.activeElement)?.blur?.());
      await p.keyboard.press("Control+Shift+D");
      await seen(p);
      await p.waitForFunction(() => document.querySelector('[data-testid="desktop-graph"]')?.getAttribute("data-focus") === "party:self", null, { timeout: 10000 })
        .catch(async e => { throw new Error(`the graph behind did not move with the navigator: ${await focusNow()} (${e.message})`); });
      assert.equal(await desk(p).getAttribute("data-focus-from"), "window");
    });
    await step(`${device}: D3 - „pusty pulpit” → no icons, no graph, the sentence field and "Więcej…" stay (P1); „pulpit jako graf” brings the default back`, async () => {
      await say(p, "pusty pulpit", "pulpit_pusty");
      assert.equal(await p.getByTestId("desktop-icon").count(), 0);
      assert.equal(await p.getByTestId("desktop-graph").count(), 0);
      assert.equal(await p.getByTestId("desktop-emergency").count(), 0, "the sentence is a way: no emergency button");
      await p.getByTestId("desktop-more").click();
      assert.ok(await p.getByTestId("desktop-open-kind").count() >= 25);
      await stableShot(p, path.join(SHOTS, `${device}-desktop-pusty.png`));
      await say(p, "pulpit jako graf", "pulpit_graf");
      await p.getByTestId("desktop-graph").waitFor();
      assert.equal(await p.getByTestId("desktop-icon").count(), 7);
    });
    await p.context().close();
    await setKey("desktop_declaration", "pulpit_karta");
    p = await fresh();
    await step(`${device}: D3 - pulpit_karta → the card of party:self with the pins under it`, async () => {
      await closeChat(p); await seen(p);
      await p.waitForFunction(() => document.querySelector('[data-testid="desktop-card"]')?.getAttribute("data-state") === "ok", null, { timeout: 15000 });
      assert.ok((await p.getByTestId("desktop-card").locator("h3").innerText()).trim().length > 0);
      await p.waitForFunction(() => document.querySelector('[data-testid="desktop-icons"]')?.getAttribute("data-state") === "ok", null, { timeout: 15000 });
      assert.equal(await p.getByTestId("desktop-icon").count(), 7);
      await stableShot(p, path.join(SHOTS, `${device}-desktop-karta.png`));
    });
    await p.context().close();
    await setKey("desktop_declaration", "pulpit_polka");
    p = await fresh();
    await step(`${device}: D3 - pulpit_polka → the minimized chat is the icon of the desktop; a tap restores it`, async () => {
      await p.locator("body").click({ position: { x: 5, y: Math.floor(viewport.height / 2) } });
      await p.keyboard.press("Control+Shift+D");
      await seen(p);
      const shelf = p.locator('[data-testid="desktop-icon"][data-source="shelf"]');
      await shelf.first().waitFor();
      assert.equal(await shelf.count(), 1); assert.equal(await shelf.getAttribute("data-address"), "window:w_chat");
      await shelf.click();
      await covered(p);
      await p.getByTestId("chat-input").waitFor();
    });
    await p.context().close();
    await setKey("desktop_declaration", null);
    await setKey("desktop_minimized", "icons");
    p = await fresh();
    await step(`${device}: D3 - desktop_minimized=icons → the minimized chat is an icon at the bottom → a tap restores it`, async () => {
      await p.locator("body").click({ position: { x: 5, y: Math.floor(viewport.height / 2) } });
      await p.keyboard.press("Control+Shift+D");
      await seen(p);
      assert.equal(await desk(p).getAttribute("data-minimized"), "icons");
      const icon = p.locator('[data-testid="desktop-minimized-icon"][data-window="w_chat"]');
      await icon.waitFor();
      assert.equal(await p.getByTestId("desktop-restore-item").count(), 0, "icons, not the list");
      const box = await p.getByTestId("desktop-minimized").boundingBox();
      assert.ok(box && box.y + box.height > viewport.height * 0.6, `the strip is at the bottom (${JSON.stringify(box)})`);
      await stableShot(p, path.join(SHOTS, `${device}-desktop-minimized-icons.png`));
      await icon.click();
      await covered(p);
      await p.getByTestId("chat-input").waitFor();
    });
    await p.context().close();
    await setKey("desktop_minimized", null);

    // ---------- R101 D4: the trash ----------
    const run = (action, args) => api("POST", "/api/actions/run", { action, element: { class: "application" }, args });
    const listed = async (q = "") => (await api("GET", "/api/conversations?limit=500" + q)).map(c => c.id);
    const title = `Kosz próba ${device}`;
    const conv = await api("POST", "/api/conversations", { title });
    await run("window.pin", { target: `conv:${conv.id}` });
    p = await fresh();
    const trashIcon = p => p.locator('[data-testid="desktop-icon"][data-address="trash:"]');
    await step(`${device}: D4 - „Do kosza” on a conversation icon → off the list, the trash icon shows 1`, async () => {
      await closeChat(p); await seen(p);
      await p.waitForFunction(() => document.querySelector('[data-testid="desktop-icons"]')?.getAttribute("data-state") === "ok", null, { timeout: 15000 });
      const icon = p.locator(`[data-testid="desktop-icon"][data-address="conv:${conv.id}"]`);
      await icon.waitFor();
      assert.equal(await p.getByTestId("desktop-trash-badge").count(), 0, "an empty trash has no badge");
      await icon.click({ button: "right" });
      await p.getByTestId("desktop-icon-trash").click();
      await p.getByTestId("desktop-trash-badge").waitFor();
      assert.equal((await p.getByTestId("desktop-trash-badge").innerText()).trim(), "1");
      assert.equal(await trashIcon(p).getAttribute("data-badge"), "1");
      assert.ok(!(await listed()).includes(conv.id), "the list hides a trashed conversation");
      assert.ok((await listed("&trashed=only")).includes(conv.id), "it is in the trash");
      await stableShot(p, path.join(SHOTS, `${device}-desktop-trash-badge.png`));
    });
    await step(`${device}: D4 - tap the trash → the Trash window lists it by kind with the date and where from`, async () => {
      await trashIcon(p).click();
      await covered(p);
      await p.getByTestId("panel-trash").waitFor();
      const item = p.locator(`[data-testid="trash-item"][data-id="${conv.id}"]`);
      await item.waitFor();
      assert.equal(await item.getAttribute("data-kind"), "conversation");
      assert.ok((await item.innerText()).includes(title));
      assert.ok((await item.innerText()).includes("desktop"), "where from");
      await stableShot(p, path.join(SHOTS, `${device}-trash-window.png`));
      await p.getByTestId("close-panel").click();
      await seen(p);
    });
    await step(`${device}: D4 - „przywróć z kosza …” by a sentence → back on the list`, async () => {
      await p.getByTestId("desktop-sentence-input").fill(`przywróć z kosza ${title}`);
      await p.getByTestId("desktop-sentence-send").click();
      await covered(p);
      let back = false; for (const end = Date.now() + 15000; Date.now() < end && !back; await new Promise(r => setTimeout(r, 200))) back = (await listed()).includes(conv.id);
      assert.ok(back, "the sentence brought the conversation back");
      assert.ok(!(await listed("&trashed=only")).includes(conv.id));
    });
    await step(`${device}: D4 - „Opróżnij kosz” asks first; „Nie” keeps everything; „Przywróć” brings it back`, async () => {
      await run("conversation.trash", { query: conv.id, from: "list" });
      await closeChat(p); await seen(p);
      await p.getByTestId("desktop-trash-badge").waitFor();
      await trashIcon(p).click();
      await p.getByTestId("panel-trash").waitFor();
      await p.locator(`[data-testid="trash-item"][data-id="${conv.id}"]`).waitFor();
      await p.getByTestId("trash-empty").click();
      await p.getByTestId("trash-consent").waitFor();
      assert.ok((await p.getByTestId("trash-consent").innerText()).includes("nie da się cofnąć"));
      await p.getByTestId("trash-consent-no").click();
      await p.getByTestId("trash-consent").waitFor({ state: "detached" });
      assert.ok((await listed("&trashed=only")).includes(conv.id), "a no deletes nothing");
      await p.locator(`[data-testid="trash-item"][data-id="${conv.id}"]`).getByTestId("trash-restore").click();
      await p.locator(`[data-testid="trash-item"][data-id="${conv.id}"]`).waitFor({ state: "detached" });
      assert.ok((await listed()).includes(conv.id));
      await p.getByTestId("trash-empty-now").waitFor();
    });
    await p.context().close();
  }
  await step("no page errors", async () => assert.deepEqual(errors, []));
} catch (e) { failed = true; console.error(e); }
finally { await browser?.close().catch(() => {}); server.kill(); }
console.log(failed ? "[desktop-r101] FAILED" : `[desktop-r101] OK (screenshots in ${SHOTS})`);
process.exit(failed ? 1 : 0);
