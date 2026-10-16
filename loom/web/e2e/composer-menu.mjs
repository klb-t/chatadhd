#!/usr/bin/env node
// P33: the + menu next to the message field. Real loom-server + mock LLM + Chromium at phone width (390x844), Polish labels.
import { spawn } from "node:child_process";
import { mkdtempSync, existsSync, mkdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import net from "node:net";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const SHOTS = path.join(here, "screenshots");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
if (!existsSync(SERVER_BIN) || !existsSync(DIST)) { console.error("[composer-menu] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const mockPort = await freePort();
const mock = spawn("python3", [path.join(here, "mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });
const dataDir = mkdtempSync(path.join(tmpdir(), "loom-composer-menu-"));
const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", dataDir, "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
await waitUp(`${base}/api/healthz`);
const api = (m, p, b) => fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined });
const last = async () => (await fetch(`http://127.0.0.1:${mockPort}/__last`)).json();

let browser, failed = false;
const step = async (name, fn) => { try { await fn(); console.log(`[composer-menu] PASS ${name}`); } catch (e) { failed = true; console.error(`[composer-menu] FAIL ${name}\n${e.stack}`); } };
const PNG_1PX = Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==", "base64");

try {
  await api("PUT", "/api/config/base_url", { value: `http://127.0.0.1:${mockPort}` });
  await api("PUT", "/api/config/default_model", { value: "mock/e2e-model" });
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  await api("POST", "/api/secrets/api_key", { value: "sk-e2e-mock-key" });
  await api("POST", "/api/models/refresh");
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const context = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true });
  const page = await context.newPage();
  const png = (n) => page.screenshot({ path: path.join(SHOTS, `composer-menu-phone-${n}.png`) });
  await page.goto(base, { waitUntil: "domcontentloaded" });
  await page.getByTestId("chat-shell").waitFor();
  const menu = page.getByTestId("composer-menu");
  const plus = page.getByTestId("shell-attach");

  await step("+ opens a menu with Polish plain words, not the import panel", async () => {
    await plus.click();
    await menu.waitFor();
    assert.equal(await page.getByTestId("import-panel").count(), 0, "the import panel does not open directly");
    const text = await menu.innerText();
    for (const w of ["Załącz plik", "Wybierz model", "Narzędzia", "Projekt i kontekst", "Importuj rozmowy"]) assert.ok(text.includes(w), `menu has: ${w}`);
    assert.ok(!/cm_|attach_file|web_search/.test(text), "no internal ids in the menu");
    const box = await menu.boundingBox();
    assert.ok(box.x >= 0 && box.x + box.width <= 390, "the menu fits the phone width");
    for (const row of await menu.locator("[data-cm-row]").all()) assert.ok((await row.boundingBox()).height >= 44, "rows are touch targets");
    await png("01-menu");
  });

  await step("keyboard: arrows move, Escape closes and returns focus to +", async () => {
    await page.keyboard.press("ArrowDown");
    const a = await page.evaluate(() => document.activeElement?.getAttribute("data-testid"));
    await page.keyboard.press("ArrowDown");
    const b = await page.evaluate(() => document.activeElement?.getAttribute("data-testid"));
    assert.notEqual(a, b);
    await page.keyboard.press("Escape");
    await menu.waitFor({ state: "detached" });
    assert.equal(await page.evaluate(() => document.activeElement?.getAttribute("data-testid")), "shell-attach");
  });

  await step("Tools submenu: an unavailable tool is visible with a plain reason", async () => {
    await plus.click();
    await page.getByTestId("composer-menu-tools").click();
    for (const t of ["Wyszukiwanie w sieci", "Tryb agenta", "Głębokie myślenie", "Generuj obraz", "Generuj muzykę"]) assert.ok((await menu.innerText()).includes(t), t);
    const reason = page.getByTestId("composer-menu-reason-image");
    assert.match(await reason.innerText(), /dostawcy obrazów/);
    assert.equal(await page.getByTestId("composer-menu-image").getAttribute("aria-disabled"), "true");
    await png("02-tools");
    await page.keyboard.press("Escape");
  });

  await step("attach a file: chip above the field, removable; an image shows a preview", async () => {
    await plus.click();
    const chooser = page.waitForEvent("filechooser");
    await page.getByTestId("composer-menu-attach_file").click();
    (await chooser).setFiles([{ name: "notatki.txt", mimeType: "text/plain", buffer: Buffer.from("Sekretne słowo: PAPUGA") }, { name: "pixel.png", mimeType: "image/png", buffer: PNG_1PX }]);
    await page.locator('[data-testid="attachment-chip"][data-state="ready"]').nth(1).waitFor();
    assert.equal(await page.getByTestId("attachment-chip").count(), 2);
    await page.getByTestId("attachment-preview").waitFor();
    await png("03-chips");
    await page.locator('[data-testid="attachment-chip"][data-name="pixel.png"] [data-testid="attachment-remove"]').click();
    assert.equal(await page.getByTestId("attachment-chip").count(), 1);
  });

  await step("the model chosen in the menu is the one sent", async () => {
    await plus.click();
    await page.getByTestId("composer-menu-model").click();
    await page.locator('[data-testid="composer-model-option"][data-model="mock/second-model"]').click();
    await menu.waitFor({ state: "detached" });
    assert.equal(await page.getByTestId("model-picker").inputValue(), "mock/second-model", "same state as the header picker");
  });

  await step("web search toggled in Tools shows a chip; the message goes out with the file, model and the tool", async () => {
    await plus.click();
    await page.getByTestId("composer-menu-tools").click();
    await page.getByTestId("composer-menu-web_search").click();
    assert.equal(await page.getByTestId("composer-menu-web_search").getAttribute("aria-checked"), "true");
    await page.keyboard.press("Escape");
    await page.getByTestId("tool-chip-web_search").waitFor();
    await page.getByTestId("chat-input").fill("Co jest w pliku?");
    await page.getByTestId("send-chat").click();
    await page.locator('[data-testid="message"][data-role="assistant"]').first().waitFor({ timeout: 20000 });
    const body = await last();
    assert.equal(body.model, "mock/second-model");
    assert.equal(body.plugins?.some?.(p => p.id === "web") || body.web_search === true, true, "web search was requested");
    const sent = JSON.stringify(body.messages);
    assert.ok(sent.includes("PAPUGA") && sent.includes("notatki.txt"), "the file text reached the model");
    assert.equal(await page.getByTestId("attachment-chip").count(), 0, "chips are cleared after sending");
    await page.getByTestId("message-attachment").first().waitFor();
    await png("04-sent");
  });

  await step("an image attachment reaches the model as an image part", async () => {
    await plus.click();
    const chooser = page.waitForEvent("filechooser");
    await page.getByTestId("composer-menu-attach_file").click();
    (await chooser).setFiles([{ name: "pixel.png", mimeType: "image/png", buffer: PNG_1PX }]);
    await page.locator('[data-testid="attachment-chip"][data-state="ready"]').waitFor();
    await page.getByTestId("chat-input").fill("Co na obrazku?");
    await page.getByTestId("send-chat").click();
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="message"][data-role="assistant"]').length >= 2, null, { timeout: 20000 });
    assert.ok(JSON.stringify((await last()).messages).includes("image_url"));
  });

  await step("Import conversations is reachable from the menu", async () => {
    await plus.click();
    await page.getByTestId("composer-menu-import").click();
    await page.getByTestId("import-panel").waitFor();
    await png("05-import");
  });

} finally {
  await browser?.close();
  server.kill("SIGTERM");
  mock.kill("SIGTERM");
}
process.exit(failed ? 1 : 0);
