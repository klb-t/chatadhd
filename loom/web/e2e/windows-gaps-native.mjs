#!/usr/bin/env node
// W-gaps of OKNA_SYSTEM (the REAL loom-server, a local fake provider that answers with a code block - no model, no network). Desktop 1280x800.
// 1 „skopiuj ostatni blok” / „otwórz ostatni blok kodu w oknie” / „zapisz ostatni blok”: sentences routed by the kernel to artifact.copy /
//   open / save, carried out by this page only (the clipboard holds the block, a window of the kind artifact opens, a file is downloaded).
// 2 „ustaw styl tego okna na Claude”, „przywróć styl domyślny”, „układ w stylu ChatGPT”, „przywróć domyślny styl układu” (window.style /
//   layout.style): the window / the layout takes the style and gives it back; the receipt names it.
// 3 „pokaż zapytanie” and the chat menu „Pokaż zapytanie”: the window request_inspector shows the last request in words, the raw JSON under
//   „Szczegóły techniczne”.
// 4 profile_moves_dock in a browser: a profile the person imported (TEST FIXTURE seeded into the Workshop store, never a product default)
//   wants the list on the right; layout `siatka`, Styl układu = that profile → the list takes the profile's width (sidebar.width preset,
//   W10 gap 2) → chip → „Przestaw” moves the list right → Cofnij takes the move back.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync, readFileSync } from "node:fs";
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
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-wgaps-shots-", { evidence: true });
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[windows-gaps] build loom-server and `npm run build` first"); process.exit(2); }

const received = [];
const SECRET = "ghp_" + "RI2wgapsSecretValue0123456789abcdefXY";  // a token typed into settings: goes to the provider, never shown
const CODE = "print('w-gaps')\nx = 1\ny = 2";
const provider = createServer(async (req, res) => {
  if (req.method !== "POST" || !req.url.endsWith("/chat/completions")) { res.writeHead(404).end(); return; }
  const chunks = []; for await (const c of req) chunks.push(c);
  const raw = Buffer.concat(chunks).toString();
  received.push(raw);  // RI-2: what the provider got, byte for byte (the kernel's record is compared with it)
  const body = JSON.parse(raw);
  const content = "Oto kod:\n\n```python\n" + CODE + "\n```\n\nGotowe.";
  if (body.stream) { res.writeHead(200, { "Content-Type": "text/event-stream" }); res.write(`data: ${JSON.stringify({ choices: [{ delta: { content } }] })}\n\n`); res.end("data: [DONE]\n\n"); }
  else { res.writeHead(200, { "Content-Type": "application/json" }); res.end(JSON.stringify({ choices: [{ message: { role: "assistant", content } }], usage: { prompt_tokens: 1, completion_tokens: 1 } })); }
});
await new Promise(r => provider.listen(0, "127.0.0.1", r));
const providerBase = `http://127.0.0.1:${provider.address().port}`;

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

// the test fixture: a copy of a shipped profile under another id that wants the list on the right with a wider list (NOT shipped)
const RIGHTY = { ...JSON.parse(readFileSync(path.join(LOOM, "web/src/profiles/data/chatgpt-inspired-r2.json"), "utf8")), id: "righty-test", profile_revision: 1, label: "Righty (test)" };
RIGHTY.presentation = { ...RIGHTY.presentation, sidebar: { ...(RIGHTY.presentation?.sidebar ?? {}), side: "right", width: 400 } };

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", makeTmpDir("loom-wgaps-"), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[windows-gaps] PASS ${name}`); } catch (e) { failed = true; console.error(`[windows-gaps] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "?")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };

try {
  await waitUp(`${base}/api/healthz`);
  await api("PATCH", "/api/config", { base_url: providerBase, semantic_analysis: false, stream: false, ui_language: "pl", default_model: "mock/wgaps-model" });
  await api("POST", "/api/secrets/api_key", { value: "local-fake-provider-only" });
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const errors = [];
  const desk = await browser.newContext({ viewport: { width: 1280, height: 800 }, acceptDownloads: true, permissions: ["clipboard-read", "clipboard-write"] });
  await desk.addInitScript(doc => { if (!localStorage.getItem("loom.application.views.v1")) localStorage.setItem("loom.application.views.v1", JSON.stringify({ schema: "loom.application.views/1", custom_profiles: [doc], views: [{ id: "primary" }], sources: [] })); }, RIGHTY);
  const page = await desk.newPage();
  trackRequests(page);
  page.on("pageerror", e => errors.push(String(e)));
  dump = async () => "[errors] " + errors.join(" | ") + "\n[client steps] " + (await page.evaluate(() => [...document.querySelectorAll('[data-kind="client"]')].map(e => e.dataset.action + ": " + e.innerText.replace(/\s+/g, " ")).join(" || "))) + "\n" + (await page.evaluate(() => document.body.innerText.slice(-600)));
  const shot = name => stableShot(page, path.join(SHOTS, `${name}.png`));
  const back = () => page.evaluate(() => window.__loomBack ? window.__loomBack() : (history.back(), true));
  const say = async (text, answer) => {
    await page.getByTestId("chat-input").fill(text);
    await page.getByTestId("send-chat").click();
    if (answer) await page.waitForFunction(re => new RegExp(re).test(document.body.innerText), answer.source, { timeout: 30000 });
  };
  const menu = async (entry, sheet) => { await page.getByTestId("shell-menu-toggle").click(); await page.getByTestId(`shell-menu-${entry}`).click(); if (sheet) await page.getByTestId(sheet).waitFor(); };
  const closeSheet = async sheet => { await back(); await page.getByTestId(sheet).waitFor({ state: "detached" }); };

  await page.goto(base + "/");
  await page.getByTestId("chat-input").waitFor();

  await step("1a an answer with a code block; „skopiuj ostatni blok” puts the block on the clipboard of this page", async () => {
    await say("daj mi kod", /Gotowe\./);
    await say("skopiuj ostatni blok", /Skopiowane do schowka: blok python/);
    for (let i = 0; i < 30 && (await page.evaluate(() => navigator.clipboard.readText())) !== CODE; i++) await page.waitForTimeout(100);
    assert.equal(await page.evaluate(() => navigator.clipboard.readText()), CODE);
  });

  await step("1b „otwórz ostatni blok kodu w oknie” opens a window of the kind artifact", async () => {
    await say("otwórz ostatni blok kodu w oknie", /Otwarte w oknie: blok python/);
    await page.locator('[data-testid="window-artifact"]').first().waitFor({ timeout: 15000 });
    await shot("wgaps-1-artifact-window");
  });

  await step("1c „zapisz ostatni blok” downloads the block as a file", async () => {
    const download = page.waitForEvent("download", { timeout: 20000 });
    await say("zapisz ostatni blok");
    const d = await download;
    assert.match(d.suggestedFilename(), /\.py$/);
    await page.waitForFunction(() => /Zapisane jako plik: .*\.py/.test(document.body.innerText), null, { timeout: 10000 });
  });

  await step("2a „ustaw styl tego okna na Claude” styles the focused window; „przywróć styl domyślny” gives it back", async () => {
    await say("ustaw styl tego okna na Claude", /Styl okna .*: Claude-inspired/);
    const root = page.locator('[data-testid="window-style-root"][data-style-profile="claude-inspired"]');
    await root.first().waitFor({ timeout: 10000 });
    assert.equal(await root.first().getAttribute("data-style-source"), "window");
    await say("przywróć styl domyślny", /znowu dziedziczy styl/);
    await root.first().waitFor({ state: "detached", timeout: 10000 });
  });

  await step("2b „układ w stylu ChatGPT” styles the layout; „przywróć domyślny styl układu” gives it back", async () => {
    await say("układ w stylu ChatGPT", /Styl układu: ChatGPT-inspired/);
    const root = page.locator('[data-testid="window-style-root"][data-style-source="layout"][data-style-profile="chatgpt-inspired"]');
    await root.first().waitFor({ timeout: 10000 });
    await say("przywróć domyślny styl układu", /Układ znowu dziedziczy styl/);
    await root.first().waitFor({ state: "detached", timeout: 10000 });
  });

  await step("3 „pokaż zapytanie” and ⋮ › Pokaż zapytanie: the last request in words, the JSON under „Szczegóły techniczne”", async () => {
    await say("pokaż zapytanie", /Otwarte|Okno|Zapytanie do modelu/);
    const ri = page.getByTestId("request-inspector");
    await ri.first().waitFor({ timeout: 15000 });
    assert.equal(await ri.first().getAttribute("data-state"), "sent");
    assert.match(await ri.first().innerText(), /Twoja wiadomość: znaków \d+/);
    assert.equal(await page.getByTestId("request-inspector-json").first().isVisible(), false, "the JSON is folded");
    await page.getByTestId("request-inspector-details").first().locator("summary").click();
    assert.match(await page.getByTestId("request-inspector-json").first().innerText(), /"message"/);
    await shot("wgaps-3-request-inspector");
    await page.keyboard.press("Escape");
    await back();
    await page.waitForTimeout(300);
    await menu("request");
    await ri.first().waitFor({ timeout: 10000 });
  });

  await step("3b RI-2 „Co jądro wysłało dostawcy”: the system prompt and the message in words, the JSON equals the provider's body minus masks, a secret never shown", async () => {
    await api("POST", "/api/secrets/github_token", { value: SECRET });
    await api("PATCH", "/api/config", { system_prompt: "WGAPS-SYS token " + SECRET });
    await page.goto(base + "/");
    await page.getByTestId("chat-input").waitFor();
    const before = received.length;
    await say("RI2 wiadomość do modelu", /Gotowe\./);
    for (let i = 0; i < 50 && received.length === before; i++) await page.waitForTimeout(100);
    assert.equal(received.length, before + 1, "one request reached the provider");
    assert.ok(received.at(-1).includes(SECRET), "the person's own setting goes out as it is");
    await menu("request");
    const ri = page.getByTestId("request-inspector");
    await ri.first().waitFor({ timeout: 10000 });
    await page.getByTestId("ri-tab-kernel").first().click();
    const words = page.getByTestId("ri-kernel-words").first();
    await words.waitFor({ timeout: 15000 });
    const said = await words.innerText();
    assert.match(said, /Instrukcja systemowa: znaków \d+/);
    assert.match(said, /Twoja wiadomość: znaków \d+/);
    assert.match(said, /Ukryte przez jądro przed pokazaniem: \d+/);
    assert.equal(await page.getByTestId("ri-kernel-json").first().isVisible(), false, "the JSON is folded");
    await page.getByTestId("ri-kernel-details").first().locator("summary").click();
    const shown = await page.getByTestId("ri-kernel-json").first().innerText();
    assert.ok(!shown.includes(SECRET) && !shown.includes("local-fake-provider-only"), "no secret in the kernel's answer");
    assert.ok(!(await page.evaluate(() => document.body.innerText)).includes(SECRET), "no secret anywhere on the screen");
    const view = JSON.parse(shown);
    assert.equal(view.kind, "sent");
    const unmasked = JSON.parse(JSON.stringify(view.body).split("[redacted:secrets_store:github_token]").join(SECRET));
    assert.deepEqual(unmasked, JSON.parse(received.at(-1)), "the kernel's record is the provider's body, minus the masks");
    assert.equal(view.body.messages.at(-1).content, "RI2 wiadomość do modelu");
    await shot("wgaps-3b-request-kernel");
    // „Podgląd następnego”: built from the text in the field now, nothing reaches the provider
    await page.keyboard.press("Escape");
    await back();
    await page.waitForTimeout(300);
    await page.getByTestId("chat-input").fill("RI2 następna wiadomość");
    await menu("request");
    await page.getByTestId("ri-tab-kernel").first().click();
    await page.getByTestId("ri-kernel-next").first().click();
    const kernel = page.locator('[data-testid="request-inspector-kernel"][data-kind="preview"][data-state="ok"]');
    await kernel.first().waitFor({ timeout: 15000 });
    const next = JSON.parse(await page.getByTestId("ri-kernel-json").first().textContent());
    assert.equal(next.body.messages.at(-1).content, "RI2 następna wiadomość", "inputs: " + JSON.stringify(await page.evaluate(() => [...document.querySelectorAll('[data-testid="chat-input"]')].map(e => e.value))));
    assert.ok(!JSON.stringify(next).includes(SECRET));
    assert.equal(received.length, before + 1, "the preview sent nothing");
    // RI-2b: the toggle „Pokaż wartości z ustawień" writes request_inspector_show_setting_values; values that only look like a secret show, the store secret stays masked
    await api("PATCH", "/api/config", { system_prompt: "WGAPS-SYS password: hunter22xyz " + SECRET });
    const toggle = page.getByTestId("ri-show-setting-values").first();
    assert.equal(await toggle.isChecked(), false, "preset: masked");
    await toggle.check();
    for (let i = 0; i < 50 && (await api("GET", "/api/config")).request_inspector_show_setting_values !== true; i++) await page.waitForTimeout(100);
    assert.equal((await api("GET", "/api/config")).request_inspector_show_setting_values, true, "the toggle wrote the setting");
    await page.getByTestId("ri-kernel-refresh").first().click();
    await page.waitForTimeout(600);
    const shownOn = await page.getByTestId("ri-kernel-json").first().textContent();
    assert.ok(shownOn.includes("hunter22xyz"), "setting value shown when on");
    assert.ok(!shownOn.includes(SECRET) && !shownOn.includes("local-fake-provider-only"), "store secret and route credential stay masked");
    await toggle.uncheck();
    for (let i = 0; i < 50 && (await api("GET", "/api/config")).request_inspector_show_setting_values !== false; i++) await page.waitForTimeout(100);
    await page.getByTestId("ri-kernel-refresh").first().click();
    await page.waitForTimeout(600);
    assert.ok(!(await page.getByTestId("ri-kernel-json").first().textContent()).includes("hunter22xyz"), "masked again when off");
    await page.getByTestId("chat-input").fill("");
    await api("PATCH", "/api/config", { system_prompt: "" });
  });

  await step("4 an imported profile wanting the list on the right: width preset, chip, „Przestaw” moves the list, Cofnij takes it back", async () => {
    await page.goto(base + "/");
    await page.getByTestId("chat-input").waitFor();
    await menu("layouts", "layouts-sheet");
    await page.locator('[data-testid="layouts-shipped"][data-layout="siatka"]').click();
    await page.locator('[data-testid="dock-leaf-L0"]').waitFor({ timeout: 10000 });
    const box = async () => page.locator('[data-testid="dock-leaf-L0"]').boundingBox();
    const area = async () => page.evaluate(() => { const r = [...document.querySelectorAll(".dock-leaf[data-leaf]")].map(e => e.getBoundingClientRect()); return { l: Math.min(...r.map(x => x.left)), r: Math.max(...r.map(x => x.right)) }; });
    const before = await box();
    // the grid has no ⋮ menu: the style comes from a sentence (layout.style matches the imported profile by its label)
    await say("układ w stylu Righty", /Styl układu: Righty \(test\)/);
    const chip = page.locator('[data-testid="window-advisory"][data-code="profile_moves_dock"]');
    await chip.waitFor({ timeout: 10000 });
    const a = await area(), w = await box();
    const share = w.width / (a.r - a.l);
    assert.ok(Math.abs(share - 400 / 1280) < 0.03, `the list takes the profile's width as a preset: share ${share.toFixed(3)} (was ${(before.width / (a.r - a.l)).toFixed(3)})`);
    assert.ok(w.x < (a.l + a.r) / 2, "still on the left before the fix");
    await shot("wgaps-4-moves-dock-chip");
    await chip.getByTestId("window-advisory-fix").click();
    await page.waitForFunction(() => { const e = document.querySelector('[data-testid="dock-leaf-L0"]'); return e && e.getBoundingClientRect().left > window.innerWidth / 2; }, null, { timeout: 10000 });
    await chip.waitFor({ state: "detached", timeout: 10000 });
    await say("cofnij układ");
    await page.waitForFunction(() => { const e = document.querySelector('[data-testid="dock-leaf-L0"]'); return e && e.getBoundingClientRect().left < window.innerWidth / 2; }, null, { timeout: 10000 });
  });

  await step("5 no page errors", async () => { assert.deepEqual(errors, []); });
} catch (e) {
  failed = true;
  console.error(`[windows-gaps] FAIL setup\n${e.stack}`);
} finally {
  await browser?.close();
  server.kill();
  provider.close();
}
console.log(`[windows-gaps] shots in ${SHOTS}`);
process.exit(failed ? 1 : 0);
