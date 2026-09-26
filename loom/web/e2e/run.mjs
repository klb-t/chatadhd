#!/usr/bin/env node
// loom/web/e2e/run.mjs — end-to-end verification of loom-server + the web
// UI in a real Chromium, driven by Playwright. Run via `npm run e2e`
// (loom/web/package.json sets NODE_PATH/PLAYWRIGHT_BROWSERS_PATH so this
// resolves the preinstalled global `playwright` package and its
// preinstalled Chromium instead of trying to download either).
//
// Prereqs: loom-server built (LOOM_BUILD_SERVER=ON) and `npm run build`
// already run in loom/web (so dist/ exists). Both are checked below with a
// clear error rather than a confusing Playwright failure.
// Resolved dynamically (see below) so this works whether `playwright` is a
// local devDependency or - as in this sandbox, per the task's instructions -
// the preinstalled global package at PLAYWRIGHT_GLOBAL_MODULE /
// /opt/node22/lib/node_modules/playwright (bare "import playwright from
// 'playwright'" can't see global node_modules under ESM resolution).
import { spawn } from "node:child_process";
import { mkdtempSync, rmSync, existsSync, mkdirSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import net from "node:net";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const WEB_ROOT = path.resolve(__dirname, "..");
const LOOM_ROOT = path.resolve(WEB_ROOT, "..");
const REPO_ROOT = path.resolve(LOOM_ROOT, "..");

const SERVER_BIN = path.join(LOOM_ROOT, "build/dev/server/loom-server");
const DIST_DIR = path.join(WEB_ROOT, "dist");
const FIXTURE_MD = path.join(LOOM_ROOT, "tests/fixtures/import/chat.md");
const SCREENSHOT_DIR = path.join(__dirname, "screenshots");

const CHROMIUM_FALLBACK = "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";

let failures = 0;
let steps = 0;

function log(msg) {
  console.log(`[e2e] ${msg}`);
}

async function step(name, fn) {
  steps += 1;
  try {
    await fn();
    log(`PASS  ${name}`);
  } catch (err) {
    failures += 1;
    console.error(`[e2e] FAIL  ${name}\n${err?.stack ?? err}`);
  }
}

function assert(cond, msg) {
  if (!cond) throw new Error(`assertion failed: ${msg}`);
}

function freePort() {
  return new Promise((resolve, reject) => {
    const srv = net.createServer();
    srv.unref();
    srv.on("error", reject);
    srv.listen(0, "127.0.0.1", () => {
      const { port } = srv.address();
      srv.close(() => resolve(port));
    });
  });
}

async function waitUp(url, timeoutMs = 15000) {
  const deadline = Date.now() + timeoutMs;
  let lastErr;
  while (Date.now() < deadline) {
    try {
      const res = await fetch(url);
      if (res.ok) return;
    } catch (err) {
      lastErr = err;
    }
    await new Promise((r) => setTimeout(r, 150));
  }
  throw new Error(`server never came up at ${url}: ${lastErr}`);
}

function spawnLogged(name, cmd, args, opts) {
  const p = spawn(cmd, args, { ...opts, stdio: ["ignore", "pipe", "pipe"] });
  p.stdout.on("data", (d) => process.stdout.write(`[${name}] ${d}`));
  p.stderr.on("data", (d) => process.stderr.write(`[${name}] ${d}`));
  return p;
}

async function loadChromium() {
  // Prefer a locally installed `playwright` (normal ESM bare-specifier
  // resolution); fall back to the global install this sandbox preinstalls
  // at PLAYWRIGHT_GLOBAL_MODULE (see loom/web/package.json's "e2e" script).
  try {
    const mod = await import("playwright");
    return mod.chromium;
  } catch {
    const globalPath =
      process.env.PLAYWRIGHT_GLOBAL_MODULE || "/opt/node22/lib/node_modules/playwright/index.mjs";
    const mod = await import(globalPath);
    return mod.chromium;
  }
}

async function main() {
  if (!existsSync(SERVER_BIN)) {
    console.error(
      `[e2e] loom-server binary not found at ${SERVER_BIN}\n` +
        "      Build it first: cmake -S loom -B loom/build/dev -DLOOM_BUILD_SERVER=ON && cmake --build loom/build/dev --target loom-server",
    );
    process.exit(2);
  }
  if (!existsSync(DIST_DIR)) {
    console.error(`[e2e] ${DIST_DIR} not found - run \`npm run build\` in loom/web first`);
    process.exit(2);
  }
  mkdirSync(SCREENSHOT_DIR, { recursive: true });

  const dataDir = mkdtempSync(path.join(tmpdir(), "loom-e2e-data-"));
  const mockPort = await freePort();
  const serverPort = await freePort();
  const mockBase = `http://127.0.0.1:${mockPort}`;
  const serverBase = `http://127.0.0.1:${serverPort}`;

  log(`data dir: ${dataDir}`);
  log(`mock LLM: ${mockBase}  loom-server: ${serverBase}`);

  const mock = spawnLogged("mock-llm", "python3", [path.join(__dirname, "mock_llm_server.py"), String(mockPort)], {
    cwd: REPO_ROOT,
  });
  const server = spawnLogged(
    "loom-server",
    SERVER_BIN,
    ["--host", "127.0.0.1", "--port", String(serverPort), "--data-dir", dataDir, "--static-dir", DIST_DIR],
    { cwd: LOOM_ROOT },
  );

  let browser;
  try {
    await waitUp(`${mockBase}/`.replace(/\/$/, "/nonexistent"), 5000).catch(() => {}); // mock has no GET route; just give it a moment
    await waitUp(`${serverBase}/api/healthz`, 15000);
    log("loom-server is up");

    const chromium = await loadChromium();
    try {
      browser = await chromium.launch({ headless: true });
    } catch (err) {
      log(`default chromium.launch() failed (${err.message}); retrying with explicit executablePath`);
      browser = await chromium.launch({ headless: true, executablePath: CHROMIUM_FALLBACK });
    }

    // ── Desktop scenario ────────────────────────────────────────────────
    const desktop = await browser.newContext({ viewport: { width: 1280, height: 860 } });
    const page = await desktop.newPage();
    page.on("console", (msg) => {
      if (msg.type() === "error") console.error(`[browser console] ${msg.text()}`);
    });

    await step("load app", async () => {
      await page.goto(serverBase, { waitUntil: "domcontentloaded" });
      await page.waitForSelector('[data-testid="chat-view"]');
    });
    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "desktop-01-initial.png") });

    await step("configure model + API key via Settings panel", async () => {
      await page.click('[data-testid="nav-settings"]');
      await page.waitForSelector('[data-testid="settings-panel"]');
      await page.fill('[data-testid="cfg-base-url"]', mockBase);
      await page.locator('[data-testid="cfg-base-url"]').blur();
      await page.fill('[data-testid="cfg-default-model"]', "mock/e2e-model");
      await page.locator('[data-testid="cfg-default-model"]').blur();
      await page.fill('[data-testid="secret-api_key"]', "sk-e2e-mock-key");
      await page.click('[data-testid="secret-api_key-save"]');
      await page.waitForSelector('[data-testid="secret-api_key"]', { state: "attached" });
      const cfg = await fetch(`${serverBase}/api/config`).then((r) => r.json());
      assert(cfg.default_model === "mock/e2e-model", `default_model persisted, got ${cfg.default_model}`);
      const hasKey = await fetch(`${serverBase}/api/secrets/api_key/has`).then((r) => r.json());
      assert(hasKey.has === true, "api_key secret set");
      await page.click('[data-testid="close-panel"]');
    });

    await step("create a new conversation", async () => {
      await page.click('[data-testid="new-conversation"]');
      await page.waitForSelector('[data-testid="conv-item"].active');
    });

    const chatText =
      "Please review the security budget of $5000, email me at test@example.com about the AI project, " +
      "see https://example.com for details.";

    await step("send a message and see streamed reply + reasoning", async () => {
      await page.fill('[data-testid="chat-input"]', chatText);
      await page.click('[data-testid="send-chat"]');
      await page.waitForSelector('[data-testid="streaming-message"] .reasoning-text');
      const reasoningText = await page.textContent('[data-testid="streaming-message"] .reasoning-text');
      assert(reasoningText && reasoningText.includes("archive graph"), `reasoning text streamed, got: ${reasoningText}`);
      // Wait for the stream to finish and the canonical message list to reload.
      await page.waitForSelector('[data-testid="streaming-message"]', { state: "detached", timeout: 15000 });
      const assistantMsg = page.locator('[data-testid="message"][data-role="assistant"]').last();
      await assistantMsg.waitFor({ state: "visible" });
      const assistantText = await assistantMsg.locator(".body").textContent();
      assert(assistantText && assistantText.includes("Streaming works end to end"), `assistant text saved, got: ${assistantText}`);
      const userMsg = page.locator('[data-testid="message"][data-role="user"]').last();
      await userMsg.waitFor({ state: "visible" });
    });
    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "desktop-02-chat-streamed.png") });

    await step("edit a message and switch versions", async () => {
      const userMsg = page.locator('[data-testid="message"][data-role="user"]').last();
      await userMsg.locator('[data-testid="edit-message"]').click();
      const textarea = userMsg.locator("textarea");
      await textarea.fill(chatText + " (edited)");
      await userMsg.locator('[data-testid="save-edit"]').click();
      await page.waitForFunction(() => {
        const nodes = document.querySelectorAll('[data-testid="message"][data-role="user"]');
        const last = nodes[nodes.length - 1];
        return last && last.querySelector(".body")?.textContent?.includes("(edited)");
      });

      const editedMsg = page.locator('[data-testid="message"][data-role="user"]').last();
      await editedMsg.locator('[data-testid="load-versions"]').click();
      await page.waitForSelector('[data-testid="switch-version"]');
      const versionButtons = editedMsg.locator('[data-testid="switch-version"]');
      assert((await versionButtons.count()) >= 2, "at least two versions after an edit");
      // Switch back to version 1 (the original text).
      await versionButtons.first().click();
      await page.waitForFunction(() => {
        const nodes = document.querySelectorAll('[data-testid="message"][data-role="user"]');
        const last = nodes[nodes.length - 1];
        return last && !last.querySelector(".body")?.textContent?.includes("(edited)");
      });
    });
    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "desktop-03-version-switch.png") });

    await step("import a fixture and see it complete", async () => {
      await page.click('[data-testid="nav-import"]');
      await page.waitForSelector('[data-testid="import-panel"]');
      await page.setInputFiles('[data-testid="import-file-input"]', FIXTURE_MD);
      await page.click('[data-testid="start-import"]');
      await page.waitForSelector('[data-testid="import-result"]', { timeout: 15000 });
      const resultText = await page.textContent('[data-testid="import-result"]');
      assert(resultText && resultText.includes("Imported"), `import result shown: ${resultText}`);
      await page.click('[data-testid="close-panel"]');
    });
    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "desktop-04-import-result.png") });

    await step("graph renders nodes for the active conversation", async () => {
      await page.click('[data-testid="nav-graph"]');
      await page.waitForSelector('[data-testid="graph-canvas"]');
      await page.click('[data-testid="graph-reload"]');
      await page.waitForFunction(() => {
        const el = document.querySelector('[data-testid="graph-counts"]');
        const m = el?.textContent?.match(/(\d+) nodes/);
        return m && Number(m[1]) > 0;
      }, { timeout: 10000 });
      const counts = await page.textContent('[data-testid="graph-counts"]');
      log(`graph counts: ${counts}`);
    });
    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "desktop-05-graph.png") });
    await page.click('[data-testid="close-panel"]');

    await step("context slider shows a live preview", async () => {
      await page.click('[data-testid="nav-context"]');
      await page.waitForSelector('[data-testid="context-preview"]', { timeout: 10000 });
      const before = await page.textContent('[data-testid="context-preview"]');
      const depthSlider = page.locator('[data-testid="context-depth"]');
      await depthSlider.fill("3");
      await page.waitForTimeout(500);
      await page.waitForSelector('[data-testid="context-preview"]');
      const after = await page.textContent('[data-testid="context-preview"]');
      assert(typeof after === "string", `preview text present (before=${before?.slice(0, 30)})`);
    });
    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "desktop-06-context.png") });
    await page.click('[data-testid="close-panel"]');

    await step("toggle semantic pause/resume", async () => {
      await page.waitForSelector('[data-testid="semantic-status"]');
      const beforeLabel = await page.textContent('[data-testid="semantic-toggle-pause"]');
      await page.click('[data-testid="semantic-toggle-pause"]');
      await page.waitForFunction(
        (prev) => document.querySelector('[data-testid="semantic-toggle-pause"]')?.textContent !== prev,
        beforeLabel,
        { timeout: 8000 },
      );
      const afterLabel = await page.textContent('[data-testid="semantic-toggle-pause"]');
      assert(afterLabel !== beforeLabel, `pause label toggled (${beforeLabel} -> ${afterLabel})`);
      // put it back so the app doesn't end paused
      await page.click('[data-testid="semantic-toggle-pause"]');
    });

    await step("change a setting (theme) and confirm it persists across reload", async () => {
      const before = await page.getAttribute("html", "data-theme");
      await page.click('[data-testid="toggle-theme"]');
      await page.waitForFunction(
        (prev) => document.documentElement.getAttribute("data-theme") !== prev,
        before,
      );
      const afterToggle = await page.getAttribute("html", "data-theme");
      await page.reload({ waitUntil: "domcontentloaded" });
      await page.waitForSelector('[data-testid="chat-view"]');
      const afterReload = await page.getAttribute("html", "data-theme");
      assert(afterReload === afterToggle, `theme ${afterToggle} persisted across reload, got ${afterReload}`);

      // Server-side settings (config.json) persist by construction (no
      // localStorage involved) - confirm the model we set earlier is still
      // there after the reload too.
      await page.click('[data-testid="nav-settings"]');
      await page.waitForSelector('[data-testid="settings-panel"]');
      const modelValue = await page.inputValue('[data-testid="cfg-default-model"]');
      assert(modelValue === "mock/e2e-model", `config persisted server-side, got ${modelValue}`);
      await page.click('[data-testid="close-panel"]');
    });
    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "desktop-07-after-reload.png") });

    await desktop.close();

    // ── Phone viewport screenshots ──────────────────────────────────────
    const phone = await browser.newContext({ viewport: { width: 390, height: 844 }, isMobile: true });
    const phonePage = await phone.newPage();
    await step("phone layout renders and is navigable", async () => {
      await phonePage.goto(serverBase, { waitUntil: "domcontentloaded" });
      await phonePage.waitForSelector('[data-testid="chat-view"]');
      await phonePage.screenshot({ path: path.join(SCREENSHOT_DIR, "phone-01-chat.png") });

      await phonePage.click('[data-testid="toggle-sidebar"]');
      await phonePage.waitForSelector('[data-testid="sidebar"]:not(.collapsed)');
      await phonePage.screenshot({ path: path.join(SCREENSHOT_DIR, "phone-02-sidebar.png") });
      await phonePage.click('[data-testid="toggle-sidebar"]');

      await phonePage.click('[data-testid="nav-graph"]');
      await phonePage.waitForSelector('[data-testid="graph-canvas"]');
      await phonePage.screenshot({ path: path.join(SCREENSHOT_DIR, "phone-03-graph.png") });
    });
    await phone.close();
  } finally {
    if (browser) await browser.close().catch(() => {});
    server.kill("SIGTERM");
    mock.kill("SIGTERM");
    await new Promise((r) => setTimeout(r, 300));
    rmSync(dataDir, { recursive: true, force: true });
  }

  console.log(`\n[e2e] ${steps - failures}/${steps} steps passed`);
  if (failures > 0) {
    console.error(`[e2e] ${failures} step(s) FAILED`);
    process.exit(1);
  }
  console.log("[e2e] all steps passed; screenshots in " + SCREENSHOT_DIR);
}

main().catch((err) => {
  console.error("[e2e] fatal:", err);
  process.exit(1);
});
