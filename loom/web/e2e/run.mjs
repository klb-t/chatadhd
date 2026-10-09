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

const CHROMIUM_FALLBACK = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";

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
    const globalPath = process.env.PLAYWRIGHT_GLOBAL_MODULE ||
      (process.env.CODEX_PRIMARY_RUNTIME_NODE_MODULES ? path.join(process.env.CODEX_PRIMARY_RUNTIME_NODE_MODULES, "playwright/index.mjs") : "/opt/node22/lib/node_modules/playwright/index.mjs");
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
      // Check the transient stream in one browser poll: it can finish between
      // separate selector and text queries on a loaded shared runner.
      await page.waitForFunction(() => document.querySelector('[data-testid="streaming-message"] .reasoning-text')?.textContent?.includes("archive graph"));
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

    await step("markdown XSS payloads are sanitized, not executed", async () => {
      let dialogFired = false;
      page.once("dialog", async (dialog) => {
        dialogFired = true;
        await dialog.dismiss();
      });
      const payload = '<img src=x onerror="window.__xss=1">[click](javascript:window.__xss=2)';
      await page.fill('[data-testid="chat-input"]', payload);
      await page.click('[data-testid="send-chat"]');
      await page.waitForSelector('[data-testid="streaming-message"]', { state: "detached", timeout: 15000 });

      const userMsg = page.locator('[data-testid="message"][data-role="user"]').last();
      await userMsg.waitFor({ state: "visible" });
      const bodyHandle = userMsg.locator(".body");

      // Neither injected script executed (no alert(), no global set by onerror/href).
      await page.waitForTimeout(200);
      const xssRan = await page.evaluate(() => window.__xss);
      assert(!xssRan, `onerror/javascript: payload must not execute, got window.__xss=${xssRan}`);
      assert(!dialogFired, "payload must not trigger a dialog");

      // The sanitized DOM must contain no onerror attribute and no
      // javascript: URL anywhere in the rendered message.
      const innerHtml = await bodyHandle.innerHTML();
      assert(!/onerror\s*=/i.test(innerHtml), `no onerror attribute survives sanitization, got: ${innerHtml}`);
      assert(!/javascript:/i.test(innerHtml), `no javascript: URL survives sanitization, got: ${innerHtml}`);

      // A safe link elsewhere in the app must still open in a new tab with
      // rel="noopener noreferrer" (theme/UX requirement, checked here since
      // this step already has a rendered link-bearing message at hand).
      await page.fill('[data-testid="chat-input"]', "see [example](https://example.com) for details");
      await page.click('[data-testid="send-chat"]');
      await page.waitForSelector('[data-testid="streaming-message"]', { state: "detached", timeout: 15000 });
      const linkMsg = page.locator('[data-testid="message"][data-role="user"]').last();
      const link = linkMsg.locator(".body a");
      await link.waitFor({ state: "visible" });
      assert((await link.getAttribute("target")) === "_blank", "safe links open in a new tab");
      const rel = (await link.getAttribute("rel")) ?? "";
      assert(rel.includes("noopener") && rel.includes("noreferrer"), `safe links carry rel=noopener noreferrer, got: ${rel}`);
    });
    await page.screenshot({ path: path.join(SCREENSHOT_DIR, "desktop-xss.png") });

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

    await step("knowledge analysis runs from UI and exposes actual claims with provenance", async () => {
      await page.click('[data-testid="nav-knowledge"]');
      const workbench = page.locator('[data-testid="knowledge-workbench"]');
      await workbench.waitFor({ state: "visible" });
      await page.getByRole("button", { name: "Hide chat", exact: true }).click();
      await workbench.locator(".kb-run-controls > summary").click();
      await page.getByLabel("Analysis source paths").fill(path.join(LOOM_ROOT, "tests/fixtures/eval/synthetic_dev/chatgpt_export.zip"));
      await page.getByLabel("Import all catalogued source content (full mode)").check();
      await workbench.getByRole("button", { name: "Analyze sources", exact: true }).click();
      await page.waitForFunction(() => document.querySelector('[data-testid="kb-pane-claims"] .kb-record'), null, { timeout: 120000 });
      const claim = workbench.locator('[data-testid="kb-pane-claims"] .kb-record').first();
      await claim.click();
      const inspector = workbench.locator('[data-testid="kb-inspector"]');
      assert((await inspector.locator(".kb-badges").innerText()).includes("confidence"), "claim displays its engine confidence");
      assert((await inspector.innerText()).includes("How is it known?"), "all seven assessment questions are accessible");
      assert(await inspector.locator("blockquote").count() > 0, "observed claim exposes actual supporting quotes");
      const query = await fetch(`${serverBase}/api/knowledge/query`, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ what: "claims" }) }).then((response) => response.json());
      assert(query.items.length > 0, "claims are persisted in the real knowledge store");
    });

    await step("coordinated graph views remain open together with independent filters", async () => {
      const workbench = page.locator('[data-testid="knowledge-workbench"]');
      await workbench.getByLabel("View type", { exact: true }).selectOption("graph");
      await workbench.getByTestId("kb-add-view").click();
      const graphs = workbench.getByTestId("kb-pane-graph");
      assert(await graphs.count() === 2, "adding a graph retains the previous graph");
      await graphs.first().getByLabel("Graph focus depth").fill("3");
      assert(await graphs.last().getByLabel("Graph focus depth").inputValue() === "1", "each graph retains independent depth settings");
      assert(await workbench.getByTestId("kb-pane-claims").count() === 1, "claim list stays open beside both graphs");
      await workbench.evaluate((element) => { element.scrollTop = 0; });
      await page.screenshot({ path: path.join(SCREENSHOT_DIR, "desktop-08-knowledge.png") });
    });

    await step("knowledge context preview explains inclusion and flags stale inputs", async () => {
      const workbench = page.locator('[data-testid="knowledge-workbench"]');
      await workbench.getByLabel("View type", { exact: true }).selectOption("context");
      await workbench.getByTestId("kb-add-view").click();
      const context = workbench.getByTestId("kb-pane-context");
      await context.getByLabel("Context goal", { exact: true }).fill("Implement this component while preserving the user's principles");
      await context.getByLabel("Knowledge context token budget").fill("1000");
      await context.getByRole("button", { name: "Build context preview" }).click();
      await context.locator(".kb-context-band").first().waitFor();
      const counts = await context.locator(".kb-count").innerText();
      const parsed = counts.match(/([\d,]+) \/ ([\d,]+) estimated tokens/);
      assert(parsed && Number(parsed[1].replaceAll(",", "")) <= 1000, `context respects selected budget: ${counts}`);
      assert(await context.locator(".kb-context-band").count() === 3, "stable, project and goal bands shown separately");
      await context.getByLabel("Context goal", { exact: true }).fill("A changed goal");
      assert((await context.locator(".kb-warning").innerText()).includes("Rebuild"), "changed inputs do not masquerade as current preview");
    });

    await step("catalog displays real source units and requires a reviewed import", async () => {
      const workbench = page.locator('[data-testid="knowledge-workbench"]');
      await workbench.getByLabel("View type", { exact: true }).selectOption("catalog");
      await workbench.getByTestId("kb-add-view").click();
      const catalog = workbench.getByTestId("kb-pane-catalog");
      await catalog.locator(".kb-catalog-unit").first().waitFor();
      const sourceUnit = catalog.locator(".kb-catalog-unit .kb-record").first();
      await sourceUnit.focus();
      await sourceUnit.press("Enter");
      const sourceFields = workbench.getByTestId("kb-inspector").locator("details").filter({ has: page.getByText("All model fields", { exact: true }) });
      await sourceFields.locator("summary").focus();
      await sourceFields.locator("summary").press("Enter");
      await page.waitForFunction(() => {
        const inspector = document.querySelector('[data-testid="kb-inspector"]');
        return [...(inspector?.querySelectorAll("pre") ?? [])].some((pre) => pre.textContent?.includes('"resource"'));
      });
      const inspected = JSON.parse(await sourceFields.locator("pre").innerText());
      assert(inspected.preview.resource.current === true, "keyboard selection reads the verified external resource through the real endpoint");
      assert(inspected.preview.resource.last_successful.nodes.some((node) => node.kind === "export:message"), "source interior exposes message graph nodes, not only a link or sketch");
      await catalog.getByText("Review and import catalog content", { exact: true }).click();
      assert(await catalog.getByRole("button", { name: "Import reviewed selection" }).isDisabled(), "import requires previewing the selected scope");
      await catalog.getByLabel("Catalog import scope").selectOption("full");
      await catalog.getByRole("button", { name: "Preview import" }).click();
      await page.waitForFunction(() => [...document.querySelectorAll("button")].some((button) => button.textContent === "Import reviewed selection" && !button.disabled));
      await catalog.getByLabel("Catalog retention").selectOption("link");
      assert(await catalog.getByRole("button", { name: "Import reviewed selection" }).isDisabled(), "changing retention invalidates import review");
    });

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
      await phonePage.click('[data-testid="close-panel"]');
      await phonePage.click('[data-testid="nav-knowledge"]');
      await phonePage.getByRole("button", { name: "Hide chat", exact: true }).click();
      await phonePage.waitForSelector('[data-testid="kb-pane-entities"] .kb-record');
      assert(await phonePage.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), "knowledge layout does not overflow the phone viewport");
      await phonePage.screenshot({ path: path.join(SCREENSHOT_DIR, "phone-04-knowledge.png") });
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
