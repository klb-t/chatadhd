#!/usr/bin/env node
// M1-W2a: the interview about the person, STARTED from the empty chat and HELD in the conversation window, on a phone (390x844, touch) against the REAL
// loom-server (form mode, NO model). The mock model server (mock_llm_server.py) is only there to prove the point: it must receive 0 requests while the
// interview runs. Part A (the card): the empty chat shows the card "Poznajmy się" (setting onboarding_card = show, interview not started); "Później" hides
// it and writes `later` (through the settings of the kernel, not browser storage), the next launch (a reload) turns it back into `show` and shows the
// card; "Nigdy nie pytaj" writes `never` and a reload shows no card; with the setting reset by hand the card is back. Part B (the interview): "Zacznij"
// opens the conversation; the info line says "bez modelu, tylko formularz" with the number of questions left; the first question is three level cards with
// the previews of the kernel; a tap on "Minimalnie" is the person's answer and the next question comes; an answer typed; the quick replies "Pomiń",
// "Nie teraz", "Nigdy", "Podsumowanie", "Ile zostało" send the control word as the person's message; "nie pytaj o zdrowie" gives the proposal "Tak
// zrozumiałem" (ActionProposalBlock) with Tak/Nie, "Tak" writes it (health: never, asked of the kernel, not typed here) and the next question appears; the
// composer of the interview has no model chip and no "+" menu; Back closes the layers in order. Every word is taken from presentation.interview of
// conversation_view.pack or from the kernel's own answer to the same question. With M1_SHOTS_DIR=<dir> the screenshots go there, otherwise to a fresh temp dir.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { auditPage } from "./m1-audit.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const SHOTS = process.env.M1_SHOTS_DIR || mkdtempSync(path.join(tmpdir(), "loom-m1-profile-shots-"));
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[m1-profile] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });
const viewPack = JSON.parse(readFileSync(path.join(LOOM, "data/profiles/conversation_view.pack"), "utf8"));
const w = viewPack.entries.find(r => r.key === "presentation.interview").value.locales.pl;
const say = (id, params = {}) => w[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? "");
const chipIds = w["chips.order"].split(/\s+/);

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error(`timed out waiting for ${url}`); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const mockPort = await freePort(), port = await freePort();
const mock = spawn("python3", [path.join(here, "mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-m1-profile-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
const AUDIT = [];
let browser, failed = false;
const step = async (name, fn) => { try { await fn(); console.log(`[m1-profile] PASS ${name}`); } catch (e) { failed = true; console.error(`[m1-profile] FAIL ${name}\n${e.stack}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json(); };
const run = async (action, body) => { const r = await api("POST", `/api/profile/${action}`, body); return r; };
const mockCount = async () => (await (await fetch(`http://127.0.0.1:${mockPort}/__count`)).json());

try {
  await waitUp(`${base}/api/healthz`);
  await api("PUT", "/api/config/base_url", { value: `http://127.0.0.1:${mockPort}` });
  await api("PUT", "/api/config/default_model", { value: "mock/e2e-model" });
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  await api("POST", "/api/secrets/api_key", { value: "sk-e2e-mock-key" });

  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const context = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
  const page = await context.newPage();
  const errors = [];
  page.on("pageerror", e => errors.push(String(e)));
  const shot = async name => { await page.evaluate(() => Promise.all(document.getAnimations().map(a => a.finished.catch(() => {})))); await page.screenshot({ path: path.join(SHOTS, `${name}.png`) }); };
  const audit = async name => { for (const f of await auditPage(page)) AUDIT.push({ shot: name, ...f }); };
  const back = () => page.evaluate(() => window.__loomBack());
  const emptyChat = state => page.locator(`[data-testid="shell-empty"][data-interview="${state}"]`);
  const config = async () => api("GET", "/api/config");
  const messages = () => page.locator('[data-testid="message"]');
  const assistants = () => page.locator('[data-testid="message"][data-role="assistant"]');
  const lastText = async role => (await page.locator(`[data-testid="message"][data-role="${role}"] .body`).last().innerText()).trim();
  // A message in the UI is complete when the count is reached and nothing streams: the counts are awaited, never a time.
  const settled = async count => {
    await page.waitForFunction(n => document.querySelectorAll('[data-testid="message"]').length >= n && !document.querySelector('[data-testid="streaming-message"]'), count, { timeout: 30000 });
  };
  const sendTyped = async text => {
    const n = await messages().count();
    await page.getByTestId("chat-input").fill(text);
    await page.getByTestId("send-chat").tap();
    await settled(n + 2);
  };
  const tapChip = async id => {
    const n = await messages().count();
    await page.locator(`[data-testid="interview-chip"][data-chip="${id}"]`).tap();
    await settled(n + 2);
  };

  await page.goto(base + "/");
  await page.getByTestId("chat-shell").waitFor();

  // ───────── Part A: the card in the empty chat ─────────
  await step("A1 the empty chat shows the card with the owner's sentence, the promise 'bez modelu' and the three buttons", async () => {
    await emptyChat("shown").waitFor();
    const card = page.getByTestId("interview-card");
    assert.equal((await card.getAttribute("role")), "group");
    assert.equal(await page.getByTestId("interview-card-title").innerText(), say("card.title"));
    assert.equal(await page.getByTestId("interview-card-body").innerText(), say("card.body"));
    assert.equal(await page.getByTestId("interview-card-privacy").innerText(), say("card.privacy"));
    assert.match(say("card.privacy"), /bez modelu/);
    assert.equal(await page.getByTestId("interview-card-start").innerText(), say("card.start"));
    assert.equal(await page.getByTestId("interview-card-later").innerText(), say("card.later"));
    assert.equal(await page.getByTestId("interview-card-never").innerText(), say("card.never"));
    const note = await page.getByTestId("interview-card-note").innerText();
    assert.ok(note.includes(say("card.never_note")) && note.includes(say("card.later_note")), "the note says what Później and Nigdy do and how to bring the card back");
    const small = await page.locator(".interview-card button").evaluateAll(els => els.map(el => { const r = el.getBoundingClientRect(); return { id: el.dataset.testid, w: Math.round(r.width), h: Math.round(r.height) }; }).filter(b => b.w < 44 || b.h < 44));
    assert.deepEqual(small, [], "every button of the card is at least 44 x 44 px");
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth) <= 390, "no horizontal page scroll");
    await audit("01-empty-chat-card"); await shot("01-empty-chat-card");
  });

  await step("A2 'Później' hides the card, writes `later` through the settings and the next launch brings the card back as `show`", async () => {
    await page.getByTestId("interview-card-later").tap();
    await page.getByTestId("interview-card").waitFor({ state: "detached" });
    await page.getByTestId("interview-card-decision").waitFor();
    assert.equal(await page.getByTestId("interview-card-decision").getAttribute("data-decision"), "later");
    assert.equal(await page.getByTestId("interview-card-decision").innerText(), say("card.later_note"));
    assert.equal((await config()).onboarding_card, "later", "the kernel's setting holds the choice");
    await audit("02-later"); await shot("02-later");
    await page.reload();
    await page.getByTestId("chat-shell").waitFor();
    await emptyChat("shown").waitFor();
    assert.equal((await config()).onboarding_card, "show", "the launch turned `later` back into `show`");
  });

  await step("A3 'Nigdy nie pytaj' writes `never`; the card does not come back at the next launch; with the setting reset by hand it does", async () => {
    await page.getByTestId("interview-card-never").tap();
    await page.getByTestId("interview-card").waitFor({ state: "detached" });
    assert.equal(await page.getByTestId("interview-card-decision").getAttribute("data-decision"), "never");
    assert.equal(await page.getByTestId("interview-card-decision").innerText(), say("card.never_note"));
    assert.equal((await config()).onboarding_card, "never");
    await audit("03-never"); await shot("03-never");
    await page.reload();
    await page.getByTestId("chat-shell").waitFor();
    await emptyChat("hidden").waitFor();
    assert.equal(await page.getByTestId("interview-card").count(), 0, "never: no card at the next launch");
    assert.equal((await config()).onboarding_card, "never", "a launch does not undo `never`");
    await api("PUT", "/api/config/onboarding_card", { value: "show" });
    await page.reload();
    await page.getByTestId("chat-shell").waitFor();
    await emptyChat("shown").waitFor();
  });

  // ───────── Part B: the interview ─────────
  const requestsBefore = await mockCount();
  let convId;
  await step("B1 'Zacznij' opens the conversation of the interview: the info line, the first question as three level cards with the kernel's previews", async () => {
    await page.getByTestId("interview-card-start").tap();
    await page.getByTestId("interview-info").waitFor();
    await page.getByTestId("interview-level-question").waitFor();
    // the kernel's own first message is the expectation
    const list = await api("GET", "/api/conversations?limit=5");
    const conv = (Array.isArray(list) ? list : list.conversations ?? []).find(c => c.metadata?.flow === "profile.interview");
    assert.ok(conv, "the kernel opened the conversation with metadata.flow");
    convId = conv.id;
    const msgs = await api("GET", `/api/conversations/${convId}/messages`);
    const levels = msgs[0].metadata.interview.question.levels;
    assert.equal(levels.length, 3);
    const cards = page.getByTestId("interview-level");
    assert.equal(await cards.count(), 3);
    for (let i = 0; i < 3; i++) {
      const card = cards.nth(i);
      assert.equal(await card.getAttribute("data-level"), levels[i].id);
      assert.equal(await card.getByTestId("interview-level-label").innerText(), levels[i].label);
      assert.equal(await card.getByTestId("interview-level-asks").innerText(), levels[i].preview.asks);
      assert.equal(await card.getByTestId("interview-level-stores").innerText(), levels[i].preview.stores);
      assert.equal(await card.getByTestId("interview-level-model-sees").innerText(), levels[i].preview.model_sees);
      const labels = await card.locator("dt").allInnerTexts();
      assert.deepEqual(labels, [say("level.asks"), say("level.stores"), say("level.model_sees")]);
    }
    assert.equal(await page.getByTestId("interview-info-line").innerText(), say("info.line"));
    assert.equal(await page.getByTestId("interview-info-why").innerText(), say("info.why"));
    await page.locator('[data-testid="interview-info-progress"]').waitFor();
    assert.equal(await page.getByTestId("interview-info-progress").innerText(), say("info.level_pending"));
    assert.equal(await page.getByTestId("interview-level-hint").innerText(), say("level.hint"));
    // the quick replies are there from the first question: skip / not now / never / later are always on offer
    assert.deepEqual(await page.getByTestId("interview-chip").evaluateAll(els => els.map(el => el.dataset.chip)), chipIds);
    // a form without a model: no model chip, no + menu, no context strip
    assert.equal(await page.getByTestId("model-chip").count(), 0, "no model chip in the interview");
    assert.equal(await page.getByTestId("shell-attach").count(), 0, "no + menu in the interview");
    const small = await page.locator(".interview-level, .interview-chips button").evaluateAll(els => els.map(el => { const r = el.getBoundingClientRect(); return { id: el.dataset.testid, w: Math.round(r.width), h: Math.round(r.height) }; }).filter(b => b.w < 44 || b.h < 44));
    assert.deepEqual(small, [], "cards and chips are at least 44 x 44 px");
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth) <= 390, "no horizontal page scroll");
    await audit("04-level-cards"); await shot("04-level-cards");
    await page.getByTestId("interview-level").first().scrollIntoViewIfNeeded();
    await page.getByTestId("interview-level-question").evaluate(el => el.scrollIntoView({ block: "start" }));
    await audit("05-level-cards-start"); await shot("05-level-cards-start");
  });

  await step("B2 a tap on a level card is the person's answer (the label, as a normal message) and the next question comes", async () => {
    const levels = (await api("GET", `/api/conversations/${convId}/messages`))[0].metadata.interview.question.levels;
    const chosen = levels.find(l => l.id === "detailed");   // the level with the most questions: the whole walk below has room
    await page.locator('[data-testid="interview-level"][data-level="detailed"]').tap();
    await settled(3);
    assert.equal(await lastText("user"), chosen.label, "the transcript says what the person chose, in the words of the card");
    await page.getByTestId("interview-level").first().waitFor({ state: "detached" });
    // the plain text of the first message is back (the cards belong to the question that waits)
    const expected = await run("interview", { op: "status", lang: "pl" });
    const remaining = expected.result.data.remaining;
    assert.ok(remaining >= 4, "the detailed level has questions to walk through");
    await page.waitForFunction(text => document.querySelector('[data-testid="interview-info-progress"]')?.textContent === text, say("info.progress", { n: String(remaining) }));
    assert.deepEqual(await page.getByTestId("interview-chip").evaluateAll(els => els.map(el => el.dataset.chip)), chipIds, "the new question has the quick replies");
    await audit("06-after-level"); await shot("06-after-level");
  });

  await step("B3 the quick replies send their control words as the person's messages; a typed answer; skip / not now / never", async () => {
    const wordOf = id => say(`chip.${id}.say`);
    const asked = async (id, what) => {
      await tapChip(id);
      assert.equal(await lastText("user"), wordOf(id), `${what}: the chip sent the control word, nothing else`);
      assert.ok((await lastText("assistant")).length > 0, `${what}: the kernel answered`);
    };
    await asked("status", "Ile zostało");
    // the answer to a status asks nothing: the replies under it are "go on", "summary" and "how many are left", never skip / never
    await page.locator('[data-testid="interview-chips"][data-idle="true"]').waitFor();
    assert.deepEqual(await page.getByTestId("interview-chip").evaluateAll(els => els.map(el => el.dataset.chip)), w["chips.idle"].split(/\s+/));
    await audit("07-status"); await shot("07-status");
    await asked("review", "Podsumowanie");
    await asked("next", "Dalej");   // the question comes back and with it the whole row of replies
    await page.locator('[data-testid="interview-chips"]:not([data-idle])').waitFor();
    await sendTyped("Mów mi Ala");
    assert.equal(await lastText("user"), "Mów mi Ala");
    assert.ok((await lastText("assistant")).length > 0);
    await audit("08-answer"); await shot("08-answer");
    await asked("skip", "Pomiń"); await audit("09-skip"); await shot("09-skip");
    await asked("decline", "Nie teraz");
    await asked("never", "Nigdy"); await audit("10-never"); await shot("10-never");
    await page.locator('[data-testid="interview-chip"][data-chip="skip"]').waitFor();   // a question still waits: the quick replies are under it
  });

  await step("B4 'nie pytaj o zdrowie' gives the proposal 'Tak zrozumiałem' with Tak / Nie in plain words; 'Tak' writes it and the next question appears", async () => {
    // the interview goes on from a fresh section so that a question waits: "dalej" is a control word of the kernel
    await sendTyped("nie pytaj o zdrowie");
    const proposal = page.getByTestId("chat-action-proposal");
    await proposal.waitFor();
    const text = (await proposal.innerText()).replace(/\s+/g, " ");
    console.log("[m1-profile] proposal block says: " + text);
    // the sentence above says what was understood; the block is only the two answers - never the kernel's fields (kind=remark, user_id=local ...)
    assert.equal(await proposal.getAttribute("data-plain"), "true");
    assert.equal(text, `${say("proposal.yes")} ${say("proposal.no")}`, "the block holds nothing but the two answers");
    assert.ok(!/[=_]/.test(text), "no raw kernel field in the block");
    assert.match(await lastText("assistant"), /Tak zrozumiałem/, "the kernel's sentence 'Tak zrozumiałem' is in the conversation");
    assert.ok(await proposal.locator('[data-testid="chat-action-proposal-confirm"]').isVisible() && await proposal.getByTestId("chat-action-proposal-reject").isVisible(), "yes and no");
    await audit("11-proposal"); await shot("11-proposal");
    assert.equal(await page.getByTestId("interview-chip").count(), 0, "a proposal waits for yes or no: no quick replies under it");
    const before = await messages().count();
    await page.locator('[data-testid="chat-action-proposal-confirm"]').tap();
    await page.waitForFunction(n => document.querySelectorAll('[data-testid="message"]').length > n, before, { timeout: 30000 });
    await page.getByTestId("chat-action-proposal").waitFor({ state: "detached" });
    // the kernel is asked what was written (never typed here): the health topic is closed
    const shown = await run("show", { lang: "pl" });
    const slots = shown.result.data.slots ?? [];
    const health = slots.filter(s => /health|zdrowie/i.test(JSON.stringify(s)) && s.state === "never");
    assert.ok(health.length >= 1, `the kernel wrote the topic health as never (${slots.length} slots)`);
    await page.locator('[data-testid="interview-chip"][data-chip="skip"]').waitFor();   // the next question is under the answer
    await audit("12-after-yes"); await shot("12-after-yes");
  });

  await step("B5 the model server received 0 requests during the whole interview; no page errors", async () => {
    const after = await mockCount();
    assert.equal(after.chat, 0, "no chat request ever reached the model");
    assert.equal(after.requests, requestsBefore.requests, "no request at all reached the model server during the interview");
    console.log(`[m1-profile] requests to the mock model: ${after.requests} (chat completions: ${after.chat})`);
    assert.deepEqual(errors, [], "no page errors");
  });

  await step("B6 Back closes the layers in order: the drawer first, then nothing is left behind", async () => {
    await page.getByTestId("shell-drawer-toggle").tap();
    await page.getByTestId("shell-drawer").waitFor();
    assert.equal(await back(), true, "Back closes the drawer");
    await page.getByTestId("shell-drawer").waitFor({ state: "detached" });
    assert.equal(await page.getByTestId("interview-info").count(), 1, "the interview is still on the screen");
    assert.equal(await back(), false, "no layer is left behind");
  });

  await step("the text audit found no developer text on any screenshot", async () => {
    assert.deepEqual(AUDIT, [], "lines that look like developer text: " + JSON.stringify(AUDIT, null, 1));
  });
} finally {
  await browser?.close();
  server.kill();
  mock.kill();
}
if (failed) process.exit(1);
