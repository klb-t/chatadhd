#!/usr/bin/env node
// W9 of OKNA_SYSTEM (acceptance + UI17; the REAL loom-server, a local fake provider that records every request body it receives - no model,
// no network). Desktop 1280x800, layout `czat`. A question is sent and the provider records the exact bytes the kernel sent; the kernel's
// context receipt of that question and the settings (minus the layout records) are kept. Then: ⋮ › Układ ▾ › Styl układu = ChatGPT-inspired
// (the chat window takes its colours: CSS variables on the window root, source "layout"); ⋮ › Okna › the chat window's style = Claude-inspired
// (source "window", it wins over the layout); a settings panel opened and docked beside gets its own style from its menu ⋯ (and inherits the
// layout one when set back to "dziedzicz"). After a reload the styles come back with the layout. The same question in a new conversation →
// the provider receives a body BYTE-IDENTICAL to the first one (model, temperature, privacy-related fields, the context selection), the
// receipt is the same up to ids and times, and no setting other than the layout records changed (UI17: "changing a UI profile must not
// implicitly change model, permissions, privacy or context selection"). Undo (Układ ▾ › Cofnij) takes back exactly one style choice.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
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
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-w9-shots-", { evidence: true });
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[windows-w9] build loom-server and `npm run build` first"); process.exit(2); }

const QUESTION = "UI17: czy zmiana stylu zmienia żądanie?";
const bodies = [];   // the raw bytes of every chat request the provider received (a chat request carries `messages`)
const provider = createServer(async (req, res) => {
  if (req.method !== "POST" || !req.url.endsWith("/chat/completions")) { res.writeHead(404).end(); return; }
  const chunks = []; for await (const c of req) chunks.push(c);
  const raw = Buffer.concat(chunks).toString();
  const body = JSON.parse(raw);
  if (Array.isArray(body.messages) && body.messages.some(m => typeof m.content === "string" && m.content.includes("UI17"))) bodies.push(raw);
  const content = "Odpowiedź bez zmian.";
  if (body.stream) { res.writeHead(200, { "Content-Type": "text/event-stream" }); res.write(`data: ${JSON.stringify({ choices: [{ delta: { content } }] })}\n\n`); res.end("data: [DONE]\n\n"); }
  else { res.writeHead(200, { "Content-Type": "application/json" }); res.end(JSON.stringify({ choices: [{ message: { role: "assistant", content } }], usage: { prompt_tokens: 1, completion_tokens: 1 } })); }
});
await new Promise(r => provider.listen(0, "127.0.0.1", r));
const providerBase = `http://127.0.0.1:${provider.address().port}`;

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", makeTmpDir("loom-w9-"), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[windows-w9] PASS ${name}`); } catch (e) { failed = true; console.error(`[windows-w9] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "?")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };
// what may differ between two identical questions in two conversations: ids and times (the receipt names its messages)
const anon = v => JSON.stringify(v).replace(/\b[a-z]{1,3}_[0-9a-f]{12}\b/g, "<id>").replace(/\d{4}-\d{2}-\d{2}T[\d:.]+Z?/g, "<t>");
// the settings minus the records of the window system (layouts, journal, last layout) - those are where a style is stored
const settingsOf = async () => { const c = await api("GET", "/api/config"); const cfg = c.config ?? c; return Object.fromEntries(Object.entries(cfg).filter(([k]) => !/^window_layout/.test(k)).sort()); };

try {
  await waitUp(`${base}/api/healthz`);
  await api("PATCH", "/api/config", { base_url: providerBase, semantic_analysis: false, stream: false, ui_language: "pl", default_model: "mock/ui17-model", temperature: 0.3, window_restore_front: "saved" });
  await api("POST", "/api/secrets/api_key", { value: "local-fake-provider-only" });
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const errors = [];
  const desk = await browser.newContext({ viewport: { width: 1280, height: 800 } });
  const page = await desk.newPage();
  trackRequests(page);
  page.on("pageerror", e => errors.push(String(e)));
  dump = () => page.evaluate(() => document.body.innerText.slice(-1500));
  const shot = name => stableShot(page, path.join(SHOTS, `${name}.png`));
  const answers = page.locator('[data-testid="message"][data-role="assistant"]');
  const back = () => page.evaluate(() => window.__loomBack ? window.__loomBack() : (history.back(), true));
  const menu = async (entry, sheet) => { await page.getByTestId("shell-menu-toggle").click(); await page.getByTestId(`shell-menu-${entry}`).click(); await page.getByTestId(sheet).waitFor(); };
  const closeSheet = async sheet => { await back(); await page.getByTestId(sheet).waitFor({ state: "detached" }); };
  const ask = async n => {
    await page.getByTestId("chat-input").fill(QUESTION);
    await page.getByTestId("send-chat").click();
    await answers.nth(n - 1).waitFor({ timeout: 20000 });
    await page.waitForFunction(k => document.querySelectorAll('[data-testid="message"][data-role="assistant"]').length >= k, n);
  };
  const receiptOfLast = async () => {
    const convs = await api("GET", "/api/conversations");
    const list = (convs.conversations ?? convs).slice().sort((a, b) => String(b.updated ?? b.created).localeCompare(String(a.updated ?? a.created)));
    const msgs = await api("GET", `/api/conversations/${list[0].id}/messages`);
    const user = (msgs.messages ?? msgs).filter(m => m.role === "user").at(-1);
    const run = await api("POST", "/api/context/receipt", { msg_id: user.id, conversation_id: list[0].id });
    const data = run.result?.data ?? run.data ?? run;
    assert.ok(data.receipt, `the kernel keeps a receipt of the question: ${JSON.stringify(run).slice(0, 300)}`);
    return data.receipt;
  };
  const chatStyle = () => page.locator('[data-testid="window-style-root"]').filter({ has: page.getByTestId("chat-input") });
  const cssVar = (loc, name) => loc.locator("> *").first().evaluate((el, n) => getComputedStyle(el).getPropertyValue(n).trim(), name);

  await page.goto(base + "/");
  await page.getByTestId("chat-input").waitFor();
  let body0, receipt0, settings0;

  await step("1 the question before any style: the provider records the exact request, the kernel its receipt", async () => {
    await ask(1);
    assert.equal(bodies.length, 1, "one chat request reached the provider");
    body0 = bodies[0]; receipt0 = await receiptOfLast(); settings0 = await settingsOf();
    const b = JSON.parse(body0);
    assert.equal(b.model, "mock/ui17-model");
    assert.equal(await page.locator('[data-testid="window-style-root"]').count(), 0, "no style named: nothing wraps a window (as before W9)");
  });

  await step("2 Układ ▾ › Styl układu = ChatGPT-inspired: the chat window takes it (source layout)", async () => {
    await menu("layouts", "layouts-sheet");
    const sel = page.getByTestId("layout-style-select");
    assert.equal(await sel.inputValue(), "", "the layout inherits at first");
    assert.ok((await sel.locator("option").first().textContent()).startsWith("dziedzicz"), "the inherit entry says what it inherits");
    await sel.selectOption("chatgpt-inspired@2");
    await closeSheet("layouts-sheet");
    const root = chatStyle();
    await root.waitFor();
    assert.equal(await root.getAttribute("data-style-source"), "layout");
    assert.equal(await root.getAttribute("data-style-profile"), "chatgpt-inspired");
    assert.equal(await cssVar(root, "--bg"), "#171717", "the colours of the layout's style (tokens.background)");
    await shot("w9-layout-style");
  });

  await step("3 ⋮ › Okna › the chat window's style = Claude-inspired: the window wins over the layout (source window)", async () => {
    await menu("windows", "windows-sheet");
    const entry = page.locator('[data-testid="windows-entry"]').filter({ has: page.getByTestId("windows-entry-focus").filter({ hasText: /czat|rozmow/i }) }).first();
    await entry.getByTestId("windows-entry-style").selectOption("claude-inspired@2");
    await closeSheet("windows-sheet");
    const root = chatStyle();
    await page.locator('[data-testid="window-style-root"][data-style-source="window"]').waitFor();
    assert.equal(await root.getAttribute("data-style-profile"), "claude-inspired");
    assert.equal(await cssVar(root, "--bg"), "#252321", "the colours of the window's own style");
    await shot("w9-window-style");
  });

  await step("4 a docked panel gets its own style from its menu ⋯ and inherits the layout's on „dziedzicz”", async () => {
    await page.getByTestId("shell-menu-toggle").click();
    await page.getByTestId("shell-menu-settings").click();
    await page.getByTestId("panel-settings").or(page.getByTestId("window-settings")).first().waitFor();
    // dock it beside the chat (the panel's own ⋯ › right, as in W4), then its frame has the menu ⋯
    await page.getByTestId("panel-arrange-menu").click();
    await page.getByTestId("panel-arrange-right").click();
    await page.getByTestId("window-settings").waitFor();
    await page.getByTestId("window-menu-settings").click();
    await page.getByTestId("window-style-select").selectOption("nextchat-2.16.1@1");
    const panelRoot = page.locator('[data-testid="window-style-root"]').filter({ has: page.locator('[data-testid="window-settings"]') }).or(page.locator('[data-testid="window-settings"] [data-testid="window-style-root"]')).first();
    await page.locator('[data-testid="window-style-root"][data-style-profile="nextchat-2.16.1"]').waitFor();
    await page.getByTestId("window-menu-settings").click();
    assert.equal(await page.getByTestId("window-style-select").inputValue(), "nextchat-2.16.1@1");
    await page.getByTestId("window-style-select").selectOption("");
    await page.locator('[data-testid="window-style-root"][data-style-profile="nextchat-2.16.1"]').waitFor({ state: "detached" });
    void panelRoot;
    const sources = await page.locator('[data-testid="window-style-root"]').evaluateAll(els => els.map(e => `${e.dataset.window}:${e.dataset.styleSource}:${e.dataset.styleProfile}`));
    assert.ok(sources.some(s => s.endsWith(":layout:chatgpt-inspired")), `the panel inherits the layout's style again: ${sources}`);
    await page.getByTestId("window-close-settings").click();
    await page.getByTestId("window-settings").waitFor({ state: "detached" });
  });

  await step("5 after a reload the styles come back with the layout", async () => {
    await page.waitForTimeout(800);   // the last layout / journal are written a moment after the last change
    await page.reload();
    await page.getByTestId("chat-input").waitFor();
    const root = chatStyle();
    await page.locator('[data-testid="window-style-root"][data-style-source="window"]').waitFor();
    assert.equal(await root.getAttribute("data-style-profile"), "claude-inspired");
  });

  await step("6 UI17: the same question in a new conversation - a byte-identical request, the same receipt, no other setting changed", async () => {
    await page.getByTestId("shell-new-chat").or(page.getByTestId("shell-drawer-toggle")).first().click();
    if (await page.getByTestId("shell-drawer").isVisible().catch(() => false)) await page.getByTestId("shell-new-chat").click();
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="message"]').length === 0);
    await ask(1);
    assert.equal(bodies.length, 2, "a second chat request reached the provider");
    assert.equal(bodies[1], body0, "the request body is byte-identical after the style switches (model, settings, context)");
    // the ×10 guard compares the request with the spending ledger, which learnt from the first answer (baseline_*): history, not the
    // style - its own measure of THIS request (bytes) must be equal, and the rest of the receipt (classes, items, sha256) identical
    const receipt1 = await receiptOfLast();
    assert.equal(receipt1.guard?.bytes, receipt0.guard?.bytes, "the guard measured the same request size");
    assert.equal(receipt1.sha256, receipt0.sha256, "the compiled context has the same hash");
    const noGuard = r => anon({ ...r, guard: null });
    assert.equal(noGuard(receipt1), noGuard(receipt0), "the context receipt is the same up to ids, times and the guard's ledger baseline");
    assert.deepEqual(await settingsOf(), settings0, "no setting but the layout records changed");
  });

  await step("7 one style choice = one undo step: Okna › Gemini-inspired, then Układ ▾ › Cofnij → Claude-inspired again", async () => {
    await menu("windows", "windows-sheet");
    const entry = page.locator('[data-testid="windows-entry"]').filter({ has: page.getByTestId("windows-entry-focus").filter({ hasText: /czat|rozmow/i }) }).first();
    await entry.getByTestId("windows-entry-style").selectOption("gemini-inspired@2");
    await closeSheet("windows-sheet");
    await page.locator('[data-testid="window-style-root"][data-style-profile="gemini-inspired"]').waitFor();
    await menu("layouts", "layouts-sheet");
    await page.getByTestId("layouts-undo").click();
    await page.locator('[data-testid="window-style-root"][data-style-profile="claude-inspired"][data-style-source="window"]').waitFor();
    if (await page.getByTestId("layouts-sheet").count()) await closeSheet("layouts-sheet");
    // and the layout's style chosen back to „dziedzicz” → the window keeps its own, nothing else wraps
    await menu("layouts", "layouts-sheet");
    await page.getByTestId("layout-style-select").selectOption("");
    await closeSheet("layouts-sheet");
    assert.deepEqual(await page.locator('[data-testid="window-style-root"]').evaluateAll(els => els.map(e => `${e.dataset.styleSource}:${e.dataset.styleProfile}`)), ["window:claude-inspired"]);
    assert.equal(bodies.length, 2, "no style choice sent anything to the provider");
    assert.deepEqual(errors, [], "no page errors");
  });
} finally {
  await browser?.close().catch(() => {});
  server.kill("SIGTERM");
  provider.close();
}
if (failed) process.exit(1);
console.log(`[windows-w9] all steps passed; screenshots in ${SHOTS}`);
