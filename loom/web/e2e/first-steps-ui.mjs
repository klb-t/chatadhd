#!/usr/bin/env node
// P16: the first steps of a person, in the browser, against the real loom-server with accounts.
//   1. The sign-in screen: the Login field takes the same theme tokens as the other inputs (it had a light background in the dark theme).
//   2. The first message of a page at the width of a phone (390x800): after the sign-in the very first send is answered. One run is one
//      new browser context (no cookies, no storage: the first send of a page, with its lazy device declaration); FIRST_SEND_RUNS runs.
//   3. "New conversation", then Send at once (the conversation is still being made, or its view still loads): the message is not dropped
//      and not sent into another conversation; the provider gets exactly one request and the answer shows. Desktop and 390x800.
//   4. A declaration of the devices that is refused, fails or never answers does not hold the first send back.
//   5. The composer with its Send button, and a proposal with its answers, fit the viewport at every width from 320 to 1920 px.
// The model is an SSE test double that counts its requests (the e2e fixture's mock_llm_server.py streams the same text). Nothing leaves the machine.
//
// Prereqs: loom-server built (LOOM_BUILD_SERVER=ON) and `npm run build` in loom/web.
import assert from "node:assert/strict";
import { spawn, spawnSync } from "node:child_process";
import { createServer as createHttpServer } from "node:http";
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
const complete = suiteCompletionGuard("first-steps-ui", 6, groups);
let mock, server, browser;
const providerRequests = [];
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
  // The model double: every chat request is kept; the answer streams in two deltas like the e2e fixture's.
  mock = createHttpServer(async (request, response) => {
    const chunks = []; for await (const chunk of request) chunks.push(chunk);
    if (request.method === "POST") providerRequests.push(JSON.parse(Buffer.concat(chunks).toString() || "{}"));
    if (request.method !== "POST") { response.writeHead(200, { "Content-Type": "application/json" }).end('{"data":[]}'); return; }
    response.writeHead(200, { "Content-Type": "text/event-stream" });
    for (const delta of [{ role: "assistant" }, { content: "Hello from the mock model! " }, { content: "Streaming works end to end." }, {}])
      response.write("data: " + JSON.stringify({ id: "1", choices: [{ delta, ...(Object.keys(delta).length ? {} : { finish_reason: "stop" }) }] }) + "\n\n");
    response.end("data: [DONE]\n\n");
  });
  await new Promise(resolve => mock.listen(mockPort, "127.0.0.1", resolve));

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

  // One walk: a new account, the sign-in, "New conversation", then Send at once. `slow` delays what the page waits for (the creation of the
  // conversation, or its view), so the click lands inside that wait on any machine; `declare` says what the kernel does with the declaration.
  async function walk({ width, height, slow, declare = "ok", label }) {
    const person = `immediate-${label}-${Math.random().toString(16).slice(2, 8)}`;
    await provision(person);
    const context = await browser.newContext({ viewport: { width, height }, isMobile: width < 500, hasTouch: width < 500 });
    const page = await context.newPage();
    const failures = [];
    page.on("pageerror", error => failures.push("page error: " + error.message));
    const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
    if (slow === "creation") await page.route(url => url.pathname === "/api/conversations", async route => { if (route.request().method() === "POST") await delay(900); await route.continue(); });
    if (slow === "view") await page.route(url => /^\/api\/conversations\/[^/]+\/view$/.test(url.pathname), async route => { await delay(900); await route.continue(); });
    if (declare === "refused") await page.route("**/api/client/declare", route => route.fulfill({ status: 403, contentType: "application/json", body: JSON.stringify({ error: { code: "forbidden", message: "refused" } }) }));
    if (declare === "failed") await page.route("**/api/client/declare", route => route.abort("connectionreset"));
    if (declare === "silent") await page.route("**/api/client/declare", () => new Promise(() => undefined));  // never answered
    const chats = [];
    page.on("request", request => { if (new URL(request.url()).pathname === "/api/chat") chats.push(request.postDataJSON()); });
    try {
      await page.goto(base, { waitUntil: "domcontentloaded" });
      await page.getByTestId("auth-login").fill(person);
      await page.getByTestId("auth-password").fill(password);
      await page.getByTestId("auth-submit").click();
      await page.getByTestId("chat-view").waitFor();
      if (width < 500) await page.getByTestId("toggle-sidebar").click();  // the list is behind the menu button on a phone
      const before = providerRequests.length;
      const message = `Immediate send ${label}`;
      await page.getByTestId("new-conversation").click();
      if (width < 500) await page.getByTestId("toggle-sidebar").click();
      await page.fill('[data-testid="chat-input"]', message);
      const started = Date.now();
      await page.click('[data-testid="send-chat"]');
      const answered = page.locator('[data-testid="message"][data-role="assistant"] .body').filter({ hasText: expectedAnswer });
      await answered.first().waitFor({ timeout: answerWithinMs }).catch(async error => {
        throw new Error(`${label}: no answer within ${answerWithinMs} ms (${error.message.split("\n")[0]}); chat requests ${JSON.stringify(chats)}; ${failures.join("; ")}; note: ${await page.getByTestId("send-queued").count() ? await page.getByTestId("send-queued").innerText() : "none"}; field: ${JSON.stringify(await page.inputValue('[data-testid="chat-input"]'))}; send enabled: ${await page.getByTestId("send-chat").isEnabled().catch(() => "n/a")}`);
      });
      await delay(300);  // a second request would have shown by now
      assert.equal(providerRequests.length, before + 1, `${label}: the provider gets exactly one request`);
      assert.equal(chats.length, 1, `${label}: one chat request leaves the page`);
      const conversations = await (await context.request.get(base + "/api/conversations")).json();
      assert.equal(conversations.length, 1, `${label}: the message went into the conversation just made, not into another or a new one`);
      assert.equal(chats[0].conv_id, conversations[0].id, `${label}: the request names that conversation`);
      assert.equal(chats[0].message, message, `${label}: the text as typed`);
      if (declare !== "ok") assert.equal("client_id" in chats[0], false, `${label}: without a declaration the send goes without a client id`);
      const messages = await (await context.request.get(`${base}/api/conversations/${conversations[0].id}/messages`)).json();
      assert.deepEqual(messages.map(row => row.role), ["user", "assistant"], `${label}: the person's message once, one answer`);
      assert.equal((await page.locator('[data-testid="message"][data-role="user"]').count()), 1, `${label}: shown once`);
      assert.equal(await page.getByTestId("send-queued").count(), 0, `${label}: the waiting note is gone`);
      assert.deepEqual(failures, [], `${label}: no page errors`);
      return Date.now() - started;
    } finally {
      await context.close();
    }
  }

  await check("New conversation, then Send at once: a message typed while the conversation is made, or while its view loads, goes once, into it (desktop and 390x800)", async () => {
    const took = [];
    for (const [width, height] of [[1280, 800], [390, 800]])
      for (const slow of ["creation", "view", "none"])
        took.push(`${width}/${slow}: ${await walk({ width, height, slow, label: `${width}-${slow}` })} ms`);
    console.log("[first-steps-ui] answered after " + took.join("; "));
  });

  await check("a declaration of the devices that is refused, fails or never answers does not hold the first send back (390x800)", async () => {
    const took = [];
    for (const declare of ["refused", "failed", "silent"])
      took.push(`${declare}: ${await walk({ width: 390, height: 800, slow: "none", declare, label: `declaration-${declare}` })} ms`);
    console.log("[first-steps-ui] answered after " + took.join("; "));
  });

  await check("layout: the composer with Send, and a proposal with its two answers, fit the viewport at 320, 390, 720, 721, 1100 and 1920 px", async () => {
    for (const width of [320, 390, 720, 721, 1100, 1920]) {
      const person = `layout-${width}-${Math.random().toString(16).slice(2, 6)}`;
      await provision(person);
      const context = await browser.newContext({ viewport: { width, height: 800 }, isMobile: width < 500, hasTouch: width < 500 });
      const page = await context.newPage();
      try {
        await page.goto(base, { waitUntil: "domcontentloaded" });
        await page.getByTestId("auth-login").fill(person);
        await page.getByTestId("auth-password").fill(password);
        await page.getByTestId("auth-submit").click();
        await page.getByTestId("chat-view").waitFor();
        const measure = () => page.evaluate(() => {
          const box = (selector) => { const node = document.querySelector(selector); if (!node) return null; const r = node.getBoundingClientRect(); return { left: r.left, right: r.right, top: r.top, bottom: r.bottom }; };
          const scroll = document.querySelector(".chat-scroll");
          return { width: window.innerWidth, height: window.innerHeight, page: document.documentElement.scrollWidth,
            scroll: scroll ? { scroll: scroll.scrollWidth, client: scroll.clientWidth } : null,
            send: box('[data-testid="send-chat"]'), input: box('[data-testid="chat-input"]'), composer: box(".composer"),
            confirm: box('[data-testid="chat-action-proposal-confirm"]'), reject: box('[data-testid="chat-action-proposal-reject"]'),
            proposal: box('[data-testid="chat-action-proposal"]') };
        });
        const inside = (name, r, m) => { assert.ok(r, `${width}: ${name} is there`); assert.ok(r.left >= -0.5 && r.right <= m.width + 0.5, `${width}: ${name} fits the width (${Math.round(r.left)}..${Math.round(r.right)} of ${m.width})`); };
        let seen = await measure();
        for (const name of ["send", "input", "composer"]) inside(name, seen[name], seen);
        assert.ok(seen.page <= seen.width, `${width}: the page does not scroll sideways (${seen.page})`);
        // a proposal whose lines are long (the money line says why a price is unknown): it wraps, its answers stay on the screen
        await page.fill('[data-testid="chat-input"]', "pokaż temperature");
        await page.click('[data-testid="send-chat"]');
        await page.getByTestId("chat-action-proposal").waitFor();
        seen = await measure();
        for (const name of ["send", "input", "composer", "proposal", "confirm", "reject"]) inside(name, seen[name], seen);
        assert.ok(seen.scroll.scroll <= seen.scroll.client, `${width}: the messages do not scroll sideways (${seen.scroll.scroll} > ${seen.scroll.client})`);
        assert.ok(seen.page <= seen.width, `${width}: the page does not scroll sideways after the proposal (${seen.page})`);
        // and the person can answer without scrolling the page sideways
        await page.getByTestId("chat-action-proposal-reject").click();
        await page.getByTestId("chat-action-proposal").waitFor({ state: "detached" }).catch(async error => {
          throw new Error(`${width}: the proposal stays after "Drop it": ${(await page.getByTestId("chat-action-proposal").innerText()).replace(/\n+/g, " | ").slice(0, 400)}; ${error.message.split("\n")[0]}`);
        });
      } finally {
        await context.close();
      }
    }
  });

  complete.complete();
  console.log(`[first-steps-ui] ${groups.length}/6 groups passed`);
} finally {
  if (browser) await browser.close().catch(() => undefined);
  await stopChild(server).catch(() => undefined);
  mock?.close();
  mock?.closeAllConnections?.();
  rmSync(root, { recursive: true, force: true });
}
