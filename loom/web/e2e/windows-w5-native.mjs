#!/usr/bin/env node
// W5 of OKNA_SYSTEM (acceptance, the REAL loom-server, no model). Phone 390x844, touch: the graph opened by a sentence and sent back to
// the switcher, then ⋮ › Układ ▾ › „Zapisz jako” saves the screen as „praca” (settings window_layouts + window_layout_active.phone);
// a reload opens „praca” (both windows); the graph closed from ⋮ › Okna, the sentence „wróć do układu «praca»” brings it back; closed
// again, „cofnij układ” undoes the close; after a reload the journal (window_layout_journal) still has the change, so „Cofnij” is
// enabled. Desktop 1280x800 (another device: its own browser storage): „praca” is listed but not active there; a layout saved on the
// desktop becomes the desktop's active one, the phone keeps „praca”. W5b (no journal loss, installation level D19): a second phone
// (another browser = another installation) keeps its own active layout while the first applies „praca”; two tabs of one phone make
// interleaved changes, both are reloaded, and „Cofnij” in each brings back that tab's own last step (the journal is one record per tab,
// merged on the server, not one value the last tab overwrites). With M1_SHOTS_DIR=<dir> the screenshots go there.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { stableShot, trackRequests } from "./stable-shot.mjs";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-w5-shots-", { evidence: true });
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[windows-w5] build loom-server and `npm run build` first"); process.exit(2); }

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", makeTmpDir("loom-w5-"), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[windows-w5] PASS ${name}`); } catch (e) { failed = true; console.error(`[windows-w5] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };
const config = async () => { const c = await api("GET", "/api/config"); return c.config ?? c; };
const named = c => { const v = c.window_layouts; return Array.isArray(v) ? v : v && typeof v === "object" ? Object.values(v).filter(Boolean) : []; };   // W5b: id → layout
const until = async (fn, what, ms = 10000) => { for (const end = Date.now() + ms; Date.now() < end;) { if (await fn()) return; await new Promise(r => setTimeout(r, 150)); } throw new Error("never: " + what); };

try {
  await waitUp(`${base}/api/healthz`);
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const errors = [];
  const phone = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
  const page = await phone.newPage();
  trackRequests(page);
  page.on("pageerror", e => errors.push(String(e)));
  dump = () => page.evaluate(() => document.body.innerText.slice(-1500));
  const shot = (p, name) => stableShot(p, path.join(SHOTS, `${name}.png`));
  const back = p => p.evaluate(() => window.__loomBack ? window.__loomBack() : (history.back(), true));
  const menu = async (p, entry, sheet) => {
    await p.getByTestId("shell-menu-toggle").click();
    await p.getByTestId(`shell-menu-${entry}`).click();
    await p.getByTestId(sheet).waitFor();
  };
  const windowsCount = async p => { await menu(p, "windows", "windows-sheet"); const n = await p.getByTestId("windows-entry").count(); await back(p); await p.getByTestId("windows-sheet").waitFor({ state: "detached" }); return n; };
  const say = async (p, text, answer) => {
    await p.getByTestId("chat-input").fill(text);
    await p.getByTestId("send-chat").click();
    await p.waitForFunction(re => new RegExp(re).test(document.body.innerText), answer.source, { timeout: 30000 });
  };
  const closeGraph = async p => {
    await menu(p, "windows", "windows-sheet");
    await p.locator('[data-testid="windows-entry"]').nth(1).getByTestId("windows-entry-close").click();
    await until(async () => (await p.getByTestId("windows-entry").count()) === 1, "one window left");
    await back(p);
    await p.getByTestId("windows-sheet").waitFor({ state: "detached" });
  };

  await page.goto(base + "/");
  await page.getByTestId("chat-input").waitFor();

  await step("1 phone: graph by a sentence, back to the chat; ⋮ › Układ ▾ has no own layout, „Zapisz jako” praca", async () => {
    await say(page, "otwórz graf obok", /Otwarte:/);
    await page.getByTestId("panel-navigator").waitFor({ timeout: 30000 }).catch(() => {});
    if (await page.getByTestId("panel-navigator").count()) { await back(page); await page.getByTestId("panel-navigator").waitFor({ state: "detached" }); }
    assert.equal(await windowsCount(page), 2);
    await menu(page, "layouts", "layouts-sheet");
    assert.equal(await page.getByTestId("layouts-empty").count(), 1);
    assert.equal(await page.getByTestId("layouts-sheet").getAttribute("data-device-class"), "phone");
    await page.getByTestId("layouts-save-name").fill("praca");
    await page.getByTestId("layouts-save").click();
    await page.getByTestId("layouts-entry").waitFor();
    assert.equal(await page.getByTestId("layouts-entry").getAttribute("data-active"), "true");
    await shot(page, "w5-1-saved");
    await until(async () => { const c = await config(); return named(c).length === 1 && c.window_layout_active?.phone === named(c)[0].id; }, "window_layouts + window_layout_active.phone in the config");
    const c = await config();
    assert.equal(named(c)[0].label, "praca");
    assert.equal(named(c)[0].schema, "loom.window_layout/2");
    assert.equal(Array.isArray(c.window_layouts), false, "W5b: stored as id → layout");
    await back(page);
    await page.getByTestId("layouts-sheet").waitFor({ state: "detached" });
  });

  await step("2 reload → the active layout „praca” opens (2 windows); the graph closed → 1", async () => {
    await page.reload();
    await page.getByTestId("chat-input").waitFor();
    assert.equal(await windowsCount(page), 2);
    await closeGraph(page);
    assert.equal(await windowsCount(page), 1);
  });

  await step("3 „wróć do układu «praca»” by a sentence → the layout with the graph is back", async () => {
    await say(page, "wróć do układu «praca»", /Układ: praca/);
    assert.equal(await windowsCount(page), 2);
    await shot(page, "w5-3-applied");
  });

  await step("4 close the graph, „cofnij układ” brings it back; after a reload the journal still has the change (Cofnij enabled)", async () => {
    await closeGraph(page);
    await say(page, "cofnij układ", /Zmiana układu cofnięta/);
    assert.equal(await windowsCount(page), 2);
    await until(async () => { const w = Object.values((await config()).window_layout_journal?.writers ?? {}); return w.some(r => (r?.phone?.past?.length ?? 0) > 0 && (r?.phone?.future?.length ?? 0) > 0); }, "window_layout_journal.writers.<this tab>.phone saved with past and future");
    await page.reload();
    await page.getByTestId("chat-input").waitFor();
    await menu(page, "layouts", "layouts-sheet");
    assert.equal(await page.getByTestId("layouts-undo").isDisabled(), false, "undo is enabled after a reload: the journal persisted");
    assert.equal(await page.getByTestId("layouts-redo").isDisabled(), false, "redo too (the undone close)");
    await shot(page, "w5-4-journal-after-reload");
    await back(page);
    await page.getByTestId("layouts-sheet").waitFor({ state: "detached" });
  });

  await step("5 desktop 1280: „praca” listed but not active here; saving „biurko” makes it the desktop's own; the phone keeps „praca”", async () => {
    const desk = await browser.newContext({ viewport: { width: 1280, height: 800 } });
    const d = await desk.newPage();
    trackRequests(d);
    d.on("pageerror", e => errors.push(String(e)));
    await d.goto(base + "/");
    await d.getByTestId("chat-input").waitFor();
    assert.equal(await windowsCount(d), 1, "the desktop opens its own (shipped) layout, not the phone's");
    await menu(d, "layouts", "layouts-sheet");
    assert.equal(await d.getByTestId("layouts-sheet").getAttribute("data-device-class"), "desktop");
    assert.equal(await d.getByTestId("layouts-entry").getAttribute("data-active"), "false");
    await d.getByTestId("layouts-save-name").fill("biurko");
    await d.getByTestId("layouts-save").click();
    await until(async () => (await d.getByTestId("layouts-entry").count()) === 2, "two own layouts");
    await shot(d, "w5-5-desktop");
    await until(async () => { const a = (await config()).window_layout_active ?? {}; return a.desktop && a.phone && a.desktop !== a.phone; }, "active per class");
    await d.reload();
    await d.getByTestId("chat-input").waitFor();
    await menu(d, "layouts", "layouts-sheet");
    assert.equal(await d.locator('[data-testid="layouts-entry"][data-active="true"]').innerText().then(t => t.includes("biurko")), true);
    await desk.close();
    await page.reload();
    await page.getByTestId("chat-input").waitFor();
    await menu(page, "layouts", "layouts-sheet");
    assert.ok((await page.locator('[data-testid="layouts-entry"][data-active="true"]').innerText()).includes("praca"));
  });

  await step("6 rename and delete with consent", async () => {
    const entry = page.locator('[data-testid="layouts-entry"]', { hasText: "biurko" });
    await entry.getByTestId("layouts-rename").click();
    await page.getByTestId("layouts-rename-input").fill("praca");
    await page.getByTestId("layouts-rename-ok").click();
    await page.getByTestId("layouts-note").waitFor();   // „Inny układ już nazywa się …”: nothing renamed
    await page.getByTestId("layouts-rename-input").fill("stół");
    await page.getByTestId("layouts-rename-ok").click();
    await page.locator('[data-testid="layouts-entry"]', { hasText: "stół" }).waitFor();
    await page.locator('[data-testid="layouts-entry"]', { hasText: "stół" }).getByTestId("layouts-delete").click();
    await page.getByTestId("layouts-delete-ask").waitFor();
    assert.equal(await page.getByTestId("layouts-entry").count(), 2, "nothing deleted before the answer");
    await page.getByTestId("layouts-delete-yes").click();
    await until(async () => (await page.getByTestId("layouts-entry").count()) === 1, "one layout left");
    await until(async () => named(await config()).length === 1, "deleted in the config");
    await shot(page, "w5-6-deleted");
  });

  const readPraca = a => Object.values(a.installations ?? {}).map(x => x?.phone).find(id => typeof id === "string" && id.includes("praca"));
  const activeEntry = async p => { await menu(p, "layouts", "layouts-sheet"); const loc = p.locator('[data-testid="layouts-entry"][data-active="true"]'); const t = (await loc.count()) ? await loc.innerText() : ""; await back(p); await p.getByTestId("layouts-sheet").waitFor({ state: "detached" }); return t; };
  await step("7 two phones (two installations): B keeps its own active layout while A applies „praca” (D19)", async () => {
    const phoneB = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
    const b = await phoneB.newPage();
    trackRequests(b);
    b.on("pageerror", e => errors.push(String(e)));
    await b.goto(base + "/");
    await b.getByTestId("chat-input").waitFor();
    await menu(b, "layouts", "layouts-sheet");
    await b.getByTestId("layouts-save-name").fill("dom");
    await b.getByTestId("layouts-save").click();
    await until(async () => (await b.getByTestId("layouts-entry").count()) === 2, "two own layouts on B");
    await back(b);
    await b.getByTestId("layouts-sheet").waitFor({ state: "detached" });
    await until(async () => Object.keys((await config()).window_layout_active?.installations ?? {}).length >= 2, "B's own value at the installation level");
    await page.reload();
    await page.getByTestId("chat-input").waitFor();
    await say(page, "wróć do układu «praca»", /Układ: praca/);
    await until(async () => { const a = (await config()).window_layout_active ?? {}; const ids = Object.values(a.installations ?? {}).map(x => x?.phone).filter(Boolean); return new Set(ids).size === 2 && !!readPraca(a); }, "two installations, two active layouts");
    await b.reload();
    await b.getByTestId("chat-input").waitFor();
    assert.ok((await activeEntry(b)).includes("dom"), "phone B keeps „dom”");
    await page.reload();
    await page.getByTestId("chat-input").waitFor();
    assert.ok((await activeEntry(page)).includes("praca"), "phone A has „praca”");
    await shot(b, "w5b-7-phone-b");
    await phoneB.close();
  });

  await step("8 two tabs of one phone: interleaved changes, both reloaded, „Cofnij” in each brings back its own step", async () => {
    const undoInMenu = async p => { await menu(p, "layouts", "layouts-sheet"); await p.getByTestId("layouts-undo").click(); await back(p); await p.getByTestId("layouts-sheet").waitFor({ state: "detached" }); };
    const recs = async () => Object.values((await config()).window_layout_journal?.writers ?? {}).length;
    const before = await recs();
    const tab2 = await phone.newPage();
    trackRequests(tab2);
    tab2.on("pageerror", e => errors.push(String(e)));
    await tab2.goto(base + "/");
    await tab2.getByTestId("chat-input").waitFor();
    assert.equal(await windowsCount(tab2), 2, "the second tab opens „praca” too");
    assert.equal(await windowsCount(page), 2);
    await closeGraph(page);                                     // tab 1: close (its last step)
    await closeGraph(tab2);                                     // tab 2: close …
    await say(tab2, "wróć do układu «praca»", /Układ: praca/);  // … then apply „praca” (its last step)
    await until(async () => (await recs()) >= Math.max(before, 1) + 1, "a journal record per tab");
    await new Promise(r => setTimeout(r, 1500));                // both 600 ms writes are done
    const writers = (await config()).window_layout_journal.writers;
    const lens = Object.values(writers).map(r => r?.phone?.past?.length ?? 0);
    assert.ok(lens.filter(n => n > 0).length >= 2, `both tabs' journals are stored: ${JSON.stringify(lens)}`);
    await page.reload(); await tab2.reload();
    await page.getByTestId("chat-input").waitFor(); await tab2.getByTestId("chat-input").waitFor();
    await undoInMenu(page);
    assert.equal(await windowsCount(page), 2, "tab 1: undo brings back its closed graph");
    await undoInMenu(tab2);
    assert.equal(await windowsCount(tab2), 1, "tab 2: undo takes back its „praca” (the graph it closed before)");
    await shot(tab2, "w5b-8-tab2-undo");
    // both tabs save a layout from their own (stale) list: both are kept (a whole-list write would drop one)
    const saveAs = async (p, name) => { await menu(p, "layouts", "layouts-sheet"); await p.getByTestId("layouts-save-name").fill(name); await p.getByTestId("layouts-save").click(); await p.locator('[data-testid="layouts-entry"]', { hasText: name }).waitFor(); await back(p); await p.getByTestId("layouts-sheet").waitFor({ state: "detached" }); };
    const n0 = named(await config()).length;
    await saveAs(page, "karta1");
    await saveAs(tab2, "karta2");
    await until(async () => { const l = named(await config()).map(x => x.label); return l.includes("karta1") && l.includes("karta2"); }, "both tabs' layouts in window_layouts");
    assert.equal(named(await config()).length, n0 + 2);
    await tab2.close();
  });

  await step("9 no page errors", async () => { assert.deepEqual(errors, []); });
} catch (e) {
  failed = true;
  console.error(`[windows-w5] FAIL setup\n${e.stack}`);
} finally {
  await browser?.close();
  server.kill();
}
console.log(`[windows-w5] shots in ${SHOTS}`);
process.exit(failed ? 1 : 0);
