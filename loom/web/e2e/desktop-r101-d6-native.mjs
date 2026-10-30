#!/usr/bin/env node
// R101 D6 (PULPIT_R101_2026-10-28 §13 interactions, §14 row D6; acceptance, the REAL loom-server, no model). Polish; a computer
// (1280x800) and a phone (390x844). The data decide the behaviour, and the two device classes read different presets of the same data:
// (1) a conversation icon dropped on the chat window (its minimized representation after „pokaż pulpit”, Ctrl+Shift+D) → the chat comes
// back showing that conversation (set_param conversation through the coupling), a receipt chip says so, „Cofnij” brings the previous one
// back; (2) an icon dropped on the trash → badge 1, receipt, the conversation is in the trash; back by the sentence „przywróć z kosza …”
// (computer) or by the receipt's „Cofnij” (phone); (3) keyboard: the arrows walk the icons (ArrowDown = one row of the grid), Enter opens;
// (4) drag start per class (gestures.json): the computer drags on a move, the phone only after a hold (a quick swipe moves nothing);
// (5) computer: a floating chat window dropped on the trash icon closes (window_to_trash). Data: a fresh data directory, the shipped pins,
// one conversation created through the API and pinned with window.pin (its title is the only synthetic text). M1_SHOTS_DIR=<dir> keeps shots.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-desktop-d6-shots-", { evidence: true });
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[desktop-d6] build loom-server and `npm run build` first"); process.exit(2); }
const GESTURES = JSON.parse(readFileSync(path.join(LOOM, "web/src/shell/data/gestures.json"), "utf8"));
const LONG_MS = GESTURES.long_press_ms ?? 500;

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }
const sleep = ms => new Promise(r => setTimeout(r, ms));

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", makeTmpDir("loom-desktop-d6-"), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[desktop-d6] PASS ${name}`); } catch (e) { failed = true; console.error(`[desktop-d6] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json(); };
const setKey = (key, value) => api("PUT", `/api/config/${key}`, { value });
const run = (action, args) => api("POST", "/api/actions/run", { action, element: { class: "application" }, args });
const listed = async (q = "") => (await api("GET", "/api/conversations?limit=500" + q)).map(c => c.id);
const until = async (what, fn, ms = 10000) => { let v; for (const end = Date.now() + ms; Date.now() < end; await sleep(150)) { v = await fn(); if (v) return v; } throw new Error(`never: ${what} (last ${JSON.stringify(v)})`); };

try {
  await waitUp(`${base}/api/healthz`);
  await setKey("ui_language", "pl");
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const errors = [];
  // the first pins of the declaration are written on the first visit (D2); a pin made before it would count as "pins exist" - so one visit first
  { const c0 = await browser.newContext({ viewport: { width: 1280, height: 800 } }); const p0 = await c0.newPage(); await p0.goto(base + "/"); await p0.getByTestId("chat-input").waitFor();
    await p0.locator("body").click({ position: { x: 5, y: 400 } }); await p0.keyboard.press("Control+Shift+D");
    await p0.waitForFunction(() => document.querySelector('[data-testid="desktop-icons"]')?.getAttribute("data-state") === "ok", null, { timeout: 15000 });
    await p0.locator('[data-testid="desktop-icon"][data-address="trash:"]').waitFor(); await c0.close(); }
  const TITLE = "Rozmowa próbna D6";
  const conv = await api("POST", "/api/conversations", { title: TITLE });
  const CONV = `conv:${conv.id}`;
  await run("window.pin", { target: CONV });

  for (const [device, viewport] of [["computer", { width: 1280, height: 800 }], ["phone", { width: 390, height: 844 }]]) {
    const ctx = await browser.newContext({ viewport });
    const p = await ctx.newPage();
    p.on("pageerror", e => errors.push(`${device}: ${e}`));
    dump = () => p.evaluate(() => document.body.innerText.slice(-900));
    const desk = () => p.getByTestId("desktop");
    const icon = a => p.locator(`[data-testid="desktop-icon"][data-address="${a}"]`);
    const seen = async () => { await desk().waitFor({ state: "visible" }); await p.waitForFunction(() => document.querySelector('[data-testid="desktop-icons"]')?.getAttribute("data-state") === "ok", null, { timeout: 15000 }); await icon(CONV).waitFor(); };
    const covered = async () => { await desk().waitFor({ state: "hidden" }); };
    const center = async loc => { const b = await loc.boundingBox(); assert.ok(b, "visible"); return [b.x + b.width / 2, b.y + b.height / 2]; };
    const dragIcon = async (a, to, { hold = 0 } = {}) => {
      const [x0, y0] = await center(icon(a)); const [x1, y1] = await center(to);
      await p.mouse.move(x0, y0); await p.mouse.down(); if (hold) await sleep(hold);
      await p.mouse.move(x0 + 10, y0 + 10, { steps: 3 }); await p.mouse.move(x1, y1, { steps: 12 }); await p.mouse.up();
    };
    const receipt = () => p.getByTestId("desktop-receipt");
    // the chat host carries data-conversation while the chat window is shown (a minimized window is parked without it → null)
    const activeConv = async () => { const l = p.locator('[data-testid="chat-conversation"]'); return (await l.count()) ? l.first().getAttribute("data-conversation") : null; };
    let iconLabel = "", initialConv = null;
    const shot = name => p.screenshot({ path: path.join(SHOTS, `d6-${device}-${name}.png`) });
    const showDesktop = async () => { if ((await desk().getAttribute("data-cover")) === "none") return; await p.locator("body").click({ position: { x: 5, y: Math.floor(viewport.height / 2) } }).catch(() => {}); await p.keyboard.press("Control+Shift+D"); await seen(); };

    await p.goto(base + "/");
    await p.getByTestId("chat-input").waitFor();

    await step(`${device}: Ctrl+Shift+D shows the desktop; the drag start of this class comes from gestures.json (${device === "phone" ? "long_press" : "move"})`, async () => {
      initialConv = await activeConv();   // the chat may already hold a conversation of its own at the first visit
      await showDesktop();
      assert.equal(await p.getByTestId("desktop-icons").getAttribute("data-drag-start"), device === "phone" ? "long_press" : "move");
      iconLabel = (await icon(CONV).locator(".desktop-icon-label").innerText()).trim();
    });
    const minimizedChat = () => p.locator(`[data-testid="desktop-minimized-icon"][data-window="w_chat"], [data-testid="desktop-restore-item"][data-window="w_chat"]`).first();
    await step(`${device}: drag the conversation icon onto the (minimized) chat window → the chat comes back showing it, a receipt says so`, async () => {
      await minimizedChat().waitFor();
      await dragIcon(CONV, minimizedChat(), { hold: device === "phone" ? LONG_MS + 150 : 0 });
      await p.getByTestId("chat-input").waitFor();
      await until("the chat shows the dropped conversation", async () => (await activeConv()) === conv.id);
      await receipt().waitFor();
      assert.equal(await receipt().getAttribute("data-code"), "drop_receipt_set_param");
      assert.ok((await receipt().innerText()).includes(iconLabel), `the receipt names the icon (${iconLabel})`);
      await shot("1-dropped-on-chat");
    });
    await step(`${device}: „Cofnij” on the receipt brings the previous value back (P3)`, async () => {
      await p.getByTestId("desktop-receipt-undo").click();
      await until(`the previous conversation (${initialConv})`, async () => (await activeConv()) === initialConv);
      assert.equal(await receipt().getAttribute("data-code"), "drop_receipt_set_param_undo");
      await p.getByTestId("desktop-receipt-close").click();
      await receipt().waitFor({ state: "detached" });
    });
    await step(`${device}: keyboard from keymap.json - ArrowRight walks the icons, ArrowDown one row, Enter opens`, async () => {
      await showDesktop();
      await icon("party:self").focus();
      await p.keyboard.press("ArrowRight");
      assert.equal(await p.evaluate(() => document.activeElement?.getAttribute("data-address")), "area:context_projects");
      const cols = Number(await p.getByTestId("desktop-icons").getAttribute("data-columns"));
      await p.keyboard.press("ArrowDown");
      const addrs = await p.locator('[data-testid="desktop-icon"]:not([disabled])').evaluateAll(els => els.map(e => e.getAttribute("data-address")));
      // one row down: the icon under it; a shorter last row: its last icon; a single row: the focus stays (moveIndex of desktop-interact.ts)
      const n = addrs.length, want = 1 + cols < n ? 1 + cols : Math.floor((n - 1) / cols) > 0 ? n - 1 : 1;
      assert.equal(await p.evaluate(() => document.activeElement?.getAttribute("data-address")), addrs[want], `one row down in a grid of ${cols} with ${n} icons`);
      await icon("conversations:").focus();
      await p.keyboard.press("Enter");
      await until("a window opened", async () => (await desk().getAttribute("data-cover")) !== "none" || (await p.getByTestId("sidebar").count()) > 0);
    });
    if (device === "phone") await step(`${device}: a quick swipe on an icon moves nothing (long_press start: the swipe is the scroll)`, async () => {
      await showDesktop();
      const before = await icon(CONV).getAttribute("data-col");
      await dragIcon(CONV, icon("trash:"), { hold: 0 });
      await sleep(400);
      assert.equal(await receipt().count(), 0, "no receipt: nothing happened");
      assert.equal(await icon(CONV).getAttribute("data-col"), before);
      assert.equal(await p.getByTestId("desktop-trash-badge").count(), 0);
    });
    await step(`${device}: drag the conversation icon onto the trash → badge 1, receipt, the conversation is in the trash`, async () => {
      await showDesktop();
      assert.equal(await p.getByTestId("desktop-trash-badge").count(), 0, "an empty trash has no badge");
      await dragIcon(CONV, icon("trash:"), { hold: device === "phone" ? LONG_MS + 150 : 0 });
      await p.getByTestId("desktop-trash-badge").waitFor();
      assert.equal((await p.getByTestId("desktop-trash-badge").innerText()).trim(), "1");
      await receipt().waitFor(); assert.equal(await receipt().getAttribute("data-code"), "drop_receipt_trash");
      assert.ok((await listed("&trashed=only")).includes(conv.id), "it is in the trash");
      assert.ok(!(await listed()).includes(conv.id));
      await shot("2-dropped-on-trash");
    });
    if (device === "computer") await step(`${device}: „przywróć z kosza …” by a sentence → back on the list, the icon is back`, async () => {
      await p.getByTestId("desktop-sentence-input").fill(`przywróć z kosza ${TITLE}`);
      await p.getByTestId("desktop-sentence-send").click();
      await until("the sentence brought the conversation back", async () => (await listed()).includes(conv.id), 15000);
      assert.ok(!(await listed("&trashed=only")).includes(conv.id));
      await showDesktop();
      await until("the badge is gone", async () => (await p.getByTestId("desktop-trash-badge").count()) === 0);
    });
    else await step(`${device}: „Cofnij” on the receipt restores it from the trash`, async () => {
      await p.getByTestId("desktop-receipt-undo").click();
      await until("back on the list", async () => (await listed()).includes(conv.id));
      assert.equal(await receipt().getAttribute("data-code"), "drop_receipt_trash_undo");
      await until("the badge is gone", async () => (await p.getByTestId("desktop-trash-badge").count()) === 0);
      await p.getByTestId("desktop-receipt-close").click();
    });
    if (device === "computer") await step(`${device}: a floating chat window dropped on the trash icon closes (window_to_trash)`, async () => {
      await showDesktop();
      await minimizedChat().click();
      await p.getByTestId("chat-input").waitFor();
      await p.locator("body").click({ position: { x: viewport.width / 2, y: 40 } });
      await p.keyboard.press("Control+Shift+F");   // keymap.json: float the focused window
      const bar = p.locator('[data-window="w_chat"] [data-testid="window-titlebar-chat"]').first();
      await bar.waitFor();
      await desk().waitFor({ state: "visible" });
      await icon("trash:").waitFor();
      const [x0, y0] = await center(bar); const [x1, y1] = await center(icon("trash:"));
      await p.mouse.move(x0, y0); await p.mouse.down(); await p.mouse.move(x0 + 12, y0 + 12, { steps: 3 }); await p.mouse.move(x1, y1, { steps: 15 }); await p.mouse.up();
      await receipt().waitFor(); assert.equal(await receipt().getAttribute("data-code"), "drop_receipt_close");
      await until("the chat window is closed", async () => (await p.getByTestId("chat-input").count()) === 0);
      await shot("3-window-on-trash");
    });
    await ctx.close();
  }
  assert.deepEqual(errors, [], "no page errors");
  if (!failed) console.log(`[desktop-d6] all steps passed (${SHOTS})`);
} catch (e) { failed = true; console.error("[desktop-d6] FAIL", e); }
finally { await browser?.close(); server.kill(); process.exit(failed ? 1 : 0); }
