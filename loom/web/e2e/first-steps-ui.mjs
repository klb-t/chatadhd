#!/usr/bin/env node
// P16: the first steps of a person, in the browser, against the real loom-server with accounts.
//   1. The sign-in screen: the Login field takes the same theme tokens as the other inputs (it had a light background in the dark theme).
//   2. The first message of a page at the width of a phone (390x800): after the sign-in the very first send is answered. One run is one
//      new browser context (no cookies, no storage: the first send of a page, with its lazy device declaration); FIRST_SEND_RUNS runs.
// The model is the SSE test double of the e2e fixtures (mock_llm_server.py). Nothing leaves the machine.
//
// Prereqs: loom-server built (LOOM_BUILD_SERVER=ON) and `npm run build` in loom/web.
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import net from "node:net";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";
import { suiteCompletionGuard, stopChild } from "./harness-lifecycle.mjs";

const web = fileURLToPath(new URL("../", import.meta.url));
const loom = path.resolve(web, "..");
const serverBin = process.env.LOOM_SERVER_BIN || path.join(loom, "build/dev/server/loom-server");
const dist = path.join(web, "dist");
assert.ok(existsSync(serverBin) && existsSync(path.join(dist, "index.html")), "Build the native server and the App first.");
const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
const runs = Number(process.env.FIRST_SEND_RUNS || 12);
const answerWithinMs = Number(process.env.FIRST_SEND_TIMEOUT_MS || 15000);
const expectedAnswer = "Streaming works end to end";
const login = "tester", password = "correct horse battery";

const groups = [];
const complete = suiteCompletionGuard("first-steps-ui", 3, groups);
let mock, server, browser;
let serverLog = "";
const root = mkdtempSync(path.join(tmpdir(), "loom-first-steps-"));

function freePort() {
  return new Promise((resolve, reject) => {
    const probe = net.createServer();
    probe.once("error", reject);
    probe.listen(0, "127.0.0.1", () => { const { port } = probe.address(); probe.close(() => resolve(port)); });
  });
}
async function until(predicate, label, ms = 15000) {
  const deadline = Date.now() + ms;
  for (;;) {
    const value = await predicate();
    if (value) return value;
    if (Date.now() > deadline) throw new Error("Timed out waiting for " + label);
    await new Promise(resolve => setTimeout(resolve, 100));
  }
}
async function check(name, run) { await run(); groups.push(name); console.log("[first-steps-ui] PASS " + name); }
/** "rgb(26, 26, 33)" or "#1a1a21" -> [26, 26, 33] */
function channels(color) {
  const hex = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(color.trim());
  if (hex) return hex.slice(1).map(part => parseInt(part, 16));
  const rgb = /^rgba?\(\s*(\d+),\s*(\d+),\s*(\d+)/.exec(color.trim());
  assert.ok(rgb, "a colour: " + color);
  return rgb.slice(1).map(Number);
}

try {
  const mockPort = await freePort();
  const port = await freePort();
  const base = "http://127.0.0.1:" + port;
  mock = spawn("python3", [path.join(web, "e2e/mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });

  // The account: made the way an operator makes it (loom-server user add ...).
  const accounts = path.join(root, "accounts");
  // An account may not reach local addresses (the operator's choice, a preset); the test double of the model is one, so this operator allows it.
  mkdirSync(path.join(accounts, "profiles"), { recursive: true });
  writeFileSync(path.join(accounts, "profiles/server_accounts.pack"), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain: "server_accounts",
    overrides: { allow_private_hosts: ["127.0.0.1"] } }));
  server = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--accounts-root", accounts, "--data-dir", path.join(root, "data"), "--static-dir", dist],
    { cwd: loom, stdio: ["ignore", "pipe", "pipe"] });
  for (const out of [server.stdout, server.stderr]) out.on("data", chunk => { serverLog += chunk.toString(); });
  await until(async () => { try { return (await fetch(base + "/api/healthz")).ok; } catch { return false; } }, "the server");

  // An account made the way an operator makes it (loom-server user add ...), whose provider is the test double.
  async function provision(name) {
    const made = spawnSync(serverBin, ["user", "add", "--accounts-root", accounts, "--password-stdin", name], { input: password + "\n", encoding: "utf8" });
    assert.equal(made.status, 0, "user add: " + made.stderr);
    const session = await (await fetch(base + "/api/auth/login", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ login: name, password }) })).json();
    const authed = { "Content-Type": "application/json", Cookie: "loom_session=" + session.token };
    assert.equal((await fetch(base + "/api/config", { method: "PATCH", headers: authed, body: JSON.stringify({ base_url: "http://127.0.0.1:" + mockPort, default_model: "mock/e2e-model" }) })).status, 200);
    assert.equal((await fetch(base + "/api/secrets/api_key", { method: "POST", headers: authed, body: JSON.stringify({ value: "sk-e2e-mock-key" }) })).status, 200);
  }
  await provision(login);

  browser = await chromium.launch({ headless: true, executablePath });

  await check("the sign-in screen: the Login field has the background of the other inputs, in the dark and in the amoled theme", async () => {
    const context = await browser.newContext({ viewport: { width: 1280, height: 800 } });
    const page = await context.newPage();
    await page.goto(base, { waitUntil: "domcontentloaded" });
    await page.getByTestId("auth-form").waitFor();
    const read = () => page.evaluate(() => {
      const style = (id) => getComputedStyle(document.querySelector(`[data-testid="${id}"]`));
      const token = getComputedStyle(document.documentElement).getPropertyValue("--input-bg");
      return { token, login: style("auth-login").backgroundColor, password: style("auth-password").backgroundColor,
               text: style("auth-login").color, page: getComputedStyle(document.body).backgroundColor };
    });
    for (const theme of ["dark", "amoled"]) {
      if (theme === "amoled") await page.evaluate(() => document.documentElement.setAttribute("data-theme", "amoled"));
      const seen = await read();
      const wanted = channels(seen.token);
      assert.deepEqual(channels(seen.login), wanted, `${theme}: the Login field uses the --input-bg token (${seen.token}), not the browser's white (${seen.login})`);
      assert.deepEqual(channels(seen.password), wanted, `${theme}: the Password field uses it too`);
      assert.ok(Math.max(...channels(seen.login)) < 64, `${theme}: dark, not light (${seen.login})`);
      assert.ok(Math.min(...channels(seen.text)) > 128, `${theme}: the typed text is light on it (${seen.text})`);
    }
    await context.close();
  });

  await check("a wrong password is refused in words and the screen stays; the right one opens the chat", async () => {
    const context = await browser.newContext({ viewport: { width: 390, height: 800 }, isMobile: true });
    const page = await context.newPage();
    await page.goto(base, { waitUntil: "domcontentloaded" });
    await page.getByTestId("auth-login").fill(login);
    await page.getByTestId("auth-password").fill("wrong");
    await page.getByTestId("auth-form").locator("button[type=submit], button.primary").first().click();
    await page.getByTestId("auth-form").locator("[role=alert], .error, p").filter({ hasText: /\S/ }).first().waitFor();
    assert.equal(await page.getByTestId("chat-view").count(), 0);
    await page.getByTestId("auth-password").fill(password);
    await page.getByTestId("auth-form").locator("button[type=submit], button.primary").first().click();
    await page.getByTestId("chat-view").waitFor();
    await context.close();
  });

  await check(`390x800: the first message after the sign-in is answered, ${runs} runs, each a page of its own`, async () => {
    const took = [];
    for (let run = 1; run <= runs; run += 1) {
      const person = `walker${run}`;  // a new account per run: its first conversation, its first message
      await provision(person);
      const context = await browser.newContext({ viewport: { width: 390, height: 800 }, isMobile: true, hasTouch: true });
      const page = await context.newPage();
      const failures = [];
      page.on("pageerror", error => failures.push("page error: " + error.message));
      page.on("requestfailed", request => { if (!request.url().endsWith("/api/events/stream")) failures.push("request failed: " + request.url() + " " + (request.failure()?.errorText ?? "")); });
      page.on("console", message => { if (message.type() === "error") failures.push("console: " + message.text()); });
      page.on("response", response => { if (response.status() >= 400) failures.push(`HTTP ${response.status()} ${new URL(response.url()).pathname}`); });
      try {
        await page.goto(base, { waitUntil: "domcontentloaded" });
        await page.getByTestId("auth-login").fill(person);
        await page.getByTestId("auth-password").fill(password);
        await page.getByTestId("auth-form").locator("button[type=submit], button.primary").first().click();
        await page.getByTestId("chat-view").waitFor();
        const message = `First message of run ${run}`;
        await page.fill('[data-testid="chat-input"]', message);
        const started = Date.now();
        await page.click('[data-testid="send-chat"]');
        const answer = page.locator('[data-testid="message"][data-role="assistant"]').last();
        try {
          await answer.waitFor({ state: "visible", timeout: answerWithinMs });
          await page.waitForFunction((needle) => [...document.querySelectorAll('[data-testid="message"][data-role="assistant"] .body')].some(node => (node.textContent || "").includes(needle)),
            expectedAnswer, { timeout: answerWithinMs });
        } catch (error) {
          throw new Error(`run ${run}: no answer within ${answerWithinMs} ms (${error.message.split("\n")[0]}); ${failures.join("; ")}; server: ${serverLog.slice(-400)}`);
        }
        const user = page.locator('[data-testid="message"][data-role="user"]').last();
        assert.equal((await user.locator(".body").innerText()).trim(), message, `run ${run}: the user's message is shown exactly once, as typed`);
        assert.equal(await page.locator('[data-testid="message"][data-role="user"]').count(), 1, `run ${run}: one message of the person`);
        assert.equal(await page.locator('[data-testid="message"][data-role="assistant"]').count(), 1, `run ${run}: one answer`);
        assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), `run ${run}: no horizontal overflow at the phone width`);
        took.push(Date.now() - started);
      } finally {
        await context.close();
      }
    }
    console.log(`[first-steps-ui] first answers after ${took.join(", ")} ms (limit ${answerWithinMs})`);
  });

  complete.complete();
  console.log(`[first-steps-ui] ${groups.length}/3 groups passed`);
} finally {
  if (browser) await browser.close().catch(() => undefined);
  await stopChild(server).catch(() => undefined);
  mock?.kill("SIGTERM");
  rmSync(root, { recursive: true, force: true });
}
