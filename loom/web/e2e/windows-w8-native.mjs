#!/usr/bin/env node
// W8 of OKNA_SYSTEM (acceptance, the REAL loom-server, a local fake provider - no model, no network). Desktop 1280x800. An answer with a
// fenced code block → the block sits in a frame with „Kopiuj” / „Otwórz w oknie” / „Zapisz”; „Kopiuj” puts the exact text on the clipboard;
// „Otwórz w oknie” → a floating window of the kind `artifact` with the code; „Zapisz” → a download artefakt-1.py. An answer with an HTML
// block → its window shows a preview in an iframe sandbox WITHOUT scripts (the page's script did not run) and says so; with the settings
// field artifact_html_scripts = true, after a reload the window comes back with the layout, the sandbox allows scripts (never same-origin),
// the script ran and the window shows the non-blocking advisory. No page errors.
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
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-w8-shots-", { evidence: true });
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[windows-w8] build loom-server and `npm run build` first"); process.exit(2); }

const CODE = 'print("cześć")\nfor i in range(3):\n    print(i)';
const HTML = '<!doctype html><html><body><h1 id="t">bez skryptu</h1><script>document.getElementById("t").textContent = "skrypt działa"</script></body></html>';
const REPLIES = [`Oto kod:\n\n\`\`\`python\n${CODE}\n\`\`\`\n\nKoniec.`, `Strona:\n\n\`\`\`html\n${HTML}\n\`\`\``];
let asked = 0;
const provider = createServer(async (req, res) => {
  if (req.method !== "POST" || !req.url.endsWith("/chat/completions")) { res.writeHead(404).end(); return; }
  const chunks = []; for await (const c of req) chunks.push(c);
  const body = JSON.parse(Buffer.concat(chunks).toString());
  const content = REPLIES[Math.min(asked++, REPLIES.length - 1)];
  if (body.stream) { res.writeHead(200, { "Content-Type": "text/event-stream" }); res.write(`data: ${JSON.stringify({ choices: [{ delta: { content } }] })}\n\n`); res.end("data: [DONE]\n\n"); }
  else { res.writeHead(200, { "Content-Type": "application/json" }); res.end(JSON.stringify({ choices: [{ message: { role: "assistant", content } }], usage: { prompt_tokens: 1, completion_tokens: 1 } })); }
});
await new Promise(r => provider.listen(0, "127.0.0.1", r));
const providerBase = `http://127.0.0.1:${provider.address().port}`;

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", makeTmpDir("loom-w8-"), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false, dump = async () => "";
const step = async (name, fn) => { if (failed) return; try { await fn(); console.log(`[windows-w8] PASS ${name}`); } catch (e) { failed = true; console.error(`[windows-w8] FAIL ${name}\n${e.stack}\n--- the screen says:\n${await dump().catch(() => "")}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };

try {
  await waitUp(`${base}/api/healthz`);
  await api("PATCH", "/api/config", { base_url: providerBase, semantic_analysis: false, stream: false, ui_language: "pl", window_restore_front: "saved" });
  await api("POST", "/api/secrets/api_key", { value: "local-fake-provider-only" });
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const errors = [];
  const desk = await browser.newContext({ viewport: { width: 1280, height: 800 }, acceptDownloads: true });
  await desk.grantPermissions(["clipboard-read", "clipboard-write"], { origin: base });
  const page = await desk.newPage();
  trackRequests(page);
  page.on("pageerror", e => errors.push(String(e)));
  dump = () => page.evaluate(() => document.body.innerText.slice(-1500));
  const shot = name => stableShot(page, path.join(SHOTS, `${name}.png`));
  const answers = page.locator('[data-testid="message"][data-role="assistant"]');
  const send = async (text, n) => {
    await page.getByTestId("chat-input").fill(text);
    await page.getByTestId("send-chat").click();
    await answers.nth(n - 1).locator('[data-testid="artifact-frame"]').waitFor({ timeout: 20000 });
  };
  const windows = page.locator('[data-testid="window-artifact"]');

  await page.goto(base + "/");
  await page.getByTestId("chat-input").waitFor();

  await step("1 an answer with a code block: the frame with Kopiuj / Otwórz w oknie / Zapisz and the exact code", async () => {
    await send("pokaż kod", 1);
    const frame = answers.nth(0).getByTestId("artifact-frame");
    assert.equal(await frame.getAttribute("data-format"), "code");
    for (const [id, word] of [["artifact-copy", "Kopiuj"], ["artifact-open", "Otwórz w oknie"], ["artifact-save", "Zapisz"]]) assert.equal((await frame.getByTestId(id).innerText()).trim(), word);
    assert.equal(await frame.getByTestId("artifact-code").innerText(), CODE);
    assert.match(await answers.nth(0).innerText(), /Oto kod:[\s\S]*Koniec\./, "the prose around the block stays");
    await shot("w8-1-frame");
  });

  await step("2 „Kopiuj” puts the block's text on the clipboard", async () => {
    await answers.nth(0).getByTestId("artifact-copy").click();
    await page.waitForFunction(() => document.querySelector('[data-testid="artifact-copy"]')?.textContent?.includes("Skopiowano"));
    assert.equal(await page.evaluate(() => navigator.clipboard.readText()), CODE);
  });

  await step("3 „Otwórz w oknie” → a floating window of the kind artifact with the code", async () => {
    await answers.nth(0).getByTestId("artifact-open").click();
    await windows.first().waitFor();
    assert.equal(await windows.count(), 1);
    assert.equal(await windows.first().getAttribute("data-mode"), "floating");
    assert.equal(await windows.first().getByTestId("artifact-window-code").innerText(), CODE);
    assert.match(await windows.first().getByTestId("window-titlebar-artifact").innerText(), /Artefakt: python/);
    await answers.nth(0).getByTestId("artifact-open").dispatchEvent("click");   // the same block again (the window covers it now): its window comes forward, no copy
    await page.waitForTimeout(200);
    assert.equal(await windows.count(), 1, "the same block opens one window");
    await shot("w8-2-window");
  });

  await step("4 „Zapisz” (in the window) → a download artefakt-1.py with the code", async () => {
    const [download] = await Promise.all([page.waitForEvent("download"), windows.first().getByTestId("artifact-save").click()]);
    assert.equal(download.suggestedFilename(), "artefakt-1.py");
    const file = await download.path();
    const { readFileSync } = await import("node:fs");
    assert.equal(readFileSync(file, "utf8"), CODE);
  });

  await step("5 an HTML block: the window previews it in a sandbox without scripts (the script did not run) and says so", async () => {
    await send("pokaż stronę", 2);
    const frame = answers.nth(1).getByTestId("artifact-frame");
    assert.equal(await frame.getAttribute("data-format"), "html");
    await frame.getByTestId("artifact-open").dispatchEvent("click");
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="window-artifact"]').length === 2);
    const w = windows.nth(1);
    const iframe = w.getByTestId("artifact-iframe");
    await iframe.waitFor();
    assert.equal(await iframe.getAttribute("sandbox"), "");
    await w.getByTestId("artifact-scripts-off").waitFor();
    await page.frameLocator('[data-testid="window-artifact"] >> nth=1 >> [data-testid="artifact-iframe"]').locator("#t").waitFor();
    assert.equal(await page.frameLocator('[data-testid="window-artifact"] >> nth=1 >> [data-testid="artifact-iframe"]').locator("#t").innerText(), "bez skryptu");
    await shot("w8-3-html-no-scripts");
  });

  await step("6 artifact_html_scripts = true: after a reload both windows came back, the script ran (allow-scripts, no same-origin), advisory shown", async () => {
    await api("PUT", "/api/config/artifact_html_scripts", { value: true });
    await page.reload();
    await page.getByTestId("chat-input").waitFor();
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="window-artifact"]').length === 2, null, { timeout: 15000 });
    const w = page.locator('[data-testid="window-artifact"]').filter({ has: page.getByTestId("artifact-iframe") });
    assert.equal(await w.getByTestId("artifact-iframe").getAttribute("sandbox"), "allow-scripts");
    await w.getByTestId("artifact-scripts-on").waitFor();
    const h = page.frameLocator('[data-testid="window-artifact"] [data-testid="artifact-iframe"]').locator("#t");
    for (let i = 0; i < 50 && (await h.innerText().catch(() => "")) !== "skrypt działa"; i++) await page.waitForTimeout(100);
    assert.equal(await h.innerText(), "skrypt działa");
    await shot("w8-4-html-scripts");
  });

  await step("7 no page errors", async () => { assert.deepEqual(errors, []); });
} catch (e) {
  failed = true;
  console.error(`[windows-w8] FAIL setup\n${e.stack}`);
} finally {
  await browser?.close();
  server.kill();
  provider.close();
}
console.log(`[windows-w8] shots in ${SHOTS}`);
process.exit(failed ? 1 : 0);
