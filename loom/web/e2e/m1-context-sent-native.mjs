#!/usr/bin/env node
// M1-W1a: the sheet "Co wysłaliśmy" on a phone (390x844, touch) against the REAL loom-server with the mock model (mock_llm_server.py): no network.
// A conversation with a graph behind it is seeded through the API; then in the UI: the line "Kontekst: ... — zobacz" under an answer opens the sheet
// (sections as lists, tokens, "Pominięte", header, disclaimer); "Szerzej" is tapped in the sheet and the NEXT message is compiled wider (the Graf section
// of that request holds more) while the one after it is back to the base; the profile "Zgodność" leaves only the sections Instrukcja, Rozmowa and
// Pytanie; with the receipt switched off the sheet says "Brak śladu (zapisane przed M1)" and "Pokaż w grafie" stays disabled with the kernel's reason;
// "Pokaż w grafie" WORKS (K1c-3b): the button is enabled, a tap opens the navigator on the request with the perspective "Wysłane do modelu", the rows are the
// sent elements with their tokens (an estimate) and the left-out ones worded as left out, the derived rows say they are a view of the record, a derived card
// shows the kernel's sentence and the tokens as an estimate, Back returns to the chat; a refusal of the graph is the reason of the disabled button; no JSON
// of the trace in chat mode. Every word is taken from the pack (presentation.context_receipt, graph_screen) or from the kernel's own answer to the same question.
// With M1_SHOTS_DIR=<dir> the screenshots go there, otherwise to a fresh temp directory (the repository stays clean).
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import net from "node:net";
import path from "node:path";
import { auditPage } from "./m1-audit.mjs";
import { fileURLToPath } from "node:url";
import { guardLine, whyWord } from "./m1-words.mjs";
import { stableShot, trackRequests } from "./stable-shot.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const SHOTS = process.env.M1_SHOTS_DIR || mkdtempSync(path.join(tmpdir(), "loom-m1-context-shots-"));
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[m1-context-sent] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });
const viewPack = JSON.parse(readFileSync(path.join(LOOM, "data/profiles/conversation_view.pack"), "utf8"));
const wordsOf = key => viewPack.entries.find(r => r.key === key).value.locales.pl;
const w = wordsOf("presentation.context_receipt");
const gw = wordsOf("presentation.graph_screen");
const gsay = (id, params = {}) => gw[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? "");
const say = (id, params = {}) => w[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? "");
const profilesPack = JSON.parse(readFileSync(path.join(LOOM, "data/runtime/chat_context.pack"), "utf8")).defaults.selector_profiles;
const compat = profilesPack.find(p => p.id === "compat");

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const mockPort = await freePort(), port = await freePort();
const mock = spawn("python3", [path.join(here, "mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-m1-context-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
const AUDIT = [];   // the lines a screenshot's screen shows that look like developer text (m1-audit.mjs)
let browser, failed = false;
const step = async (name, fn) => { try { await fn(); console.log(`[m1-context-sent] PASS ${name}`); } catch (e) { failed = true; console.error(`[m1-context-sent] FAIL ${name}\n${e.stack}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };
const sendApi = async (message, conv) => { const r = await fetch(base + "/api/chat", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify(conv ? { message, conv_id: conv } : { message, new_conversation: true }) }); const t = await r.text(); return (t.match(/"conv_id":"([^"]+)"/) || [])[1]; };

try {
  await waitUp(`${base}/api/healthz`);
  await api("PUT", "/api/config/base_url", { value: `http://127.0.0.1:${mockPort}` });
  await api("PUT", "/api/config/default_model", { value: "mock/e2e-model" });
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  await api("PUT", "/api/config/graph_memory_max_nodes", { value: 2 });   // a small base, so that "szerzej" (x2) has room to show
  await api("POST", "/api/secrets/api_key", { value: "sk-e2e-mock-key" });
  // the graph behind the conversation: four earlier conversations about the same subject (the live analysis tags them)
  let conv;
  for (const m of ["we discussed the database and the API design of the server code", "the database schema and the API endpoint for the server software",
    "deploy the server code, database migration, API gateway", "the python code, database query and API bug in the server"]) conv = await sendApi(m, undefined);

  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const context = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
  const page = await context.newPage();
  trackRequests(page);
  const errors = [];
  page.on("pageerror", e => errors.push(String(e)));
  const shot = async name => { await stableShot(page, path.join(SHOTS, `${name}.png`)); await audit(name); };
  const audit = async name => { for (const f of await auditPage(page)) AUDIT.push({ shot: name, ...f }); };
  const sheet = () => page.getByTestId("context-receipt");
  const assistants = () => page.locator('[data-testid="message"][data-role="assistant"]');
  // send in the UI and wait until the answer is complete and its line is drawn (no sleep: the counts and the stream's end are awaited)
  const send = async text => {
    const before = await assistants().count();
    await page.getByTestId("chat-input").fill(text);
    await page.getByTestId("send-chat").tap();
    await page.waitForFunction(n => document.querySelectorAll('[data-testid="message"][data-role="assistant"]').length > n && !document.querySelector('[data-testid="streaming-message"]'), before, { timeout: 30000 });
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="message"][data-role="assistant"]').length === document.querySelectorAll('[data-testid="context-line"]').length);
  };
  const openLast = async () => {
    await page.getByTestId("context-line-open").last().tap();
    await sheet().and(page.locator('[data-state="receipt"]')).waitFor();
    // the graph question is answered by an effect: wait until the button has its state (yes | no) before anything is read from it
    await page.locator('[data-testid="receipt-show-in-graph"]:not([data-graph="checking"])').waitFor();
    await audit("sheet");
  };
  const closeSheet = async () => { await page.getByTestId("context-receipt-close").tap(); await sheet().waitFor({ state: "detached" }); };
  const sectionsWithItems = () => page.locator('[data-testid^="receipt-section-"][data-items]').evaluateAll(els => els.filter(el => Number(el.dataset.items) > 0).map(el => el.dataset.class));
  const graphTokens = async () => { const s = page.getByTestId("receipt-section-graph"); return (await s.count()) ? Number(await s.getAttribute("data-tokens")) : 0; };
  // K1c-3b: the graph from the sheet. The kernel's answer to the same question is the expectation (its sentences and numbers are never typed here).
  const until = async (read, what, ms = 10000) => { let last; for (const end = Date.now() + ms; Date.now() < end;) { last = await read(); if (last) return last; await page.waitForTimeout(50); } throw new Error(`timed out waiting for ${what} (last: ${JSON.stringify(last)})`); };
  const back = () => page.evaluate(() => window.__loomBack());
  const navigatorAt = focus => page.locator(`[data-testid="graph-navigator"][data-focus="${focus}"][data-perspectives="sent_to_model"]`);
  const openGraphFromSheet = async () => {
    const request = await sheet().getAttribute("data-request-msg");
    const focus = "msg:" + request.replace(/^m_/, "");
    const expected = await api("POST", "/api/graph/navigate", { focus, perspectives: ["sent_to_model"], lang: "pl" });
    await page.locator('[data-testid="receipt-show-in-graph"][data-graph="yes"]').waitFor();
    assert.equal(await page.getByTestId("receipt-show-in-graph").isDisabled(), false, "the button is enabled");
    assert.equal(await page.getByTestId("receipt-show-in-graph-reason").count(), 0, "no reason line while the button works");
    await page.getByTestId("receipt-show-in-graph").tap();
    await sheet().waitFor({ state: "detached" });
    await navigatorAt(focus).waitFor();
    await page.getByTestId("nav-rows").waitFor();
    await page.getByTestId("nav-loading").waitFor({ state: "detached" });
    return { focus, expected };
  };
  const navRows = () => page.locator('[data-testid="nav-rows"] > li').evaluateAll(els => els.map(el => ({
    address: el.querySelector(".gnav-row-main").dataset.testid.replace(/^nav-row-/, ""), label: el.querySelector(".gnav-label").textContent,
    derived: el.dataset.derived === "true", tokens: el.dataset.sentTokens ?? null, leftOut: el.dataset.leftOut === "true", why: el.querySelector('[data-testid="nav-row-why"]')?.textContent ?? null,
    chips: Object.fromEntries([...el.querySelectorAll('[data-testid="nav-sent"],[data-testid="nav-left-out"],[data-testid="nav-derived"],[data-testid="nav-left-out-reason"]')].map(c => [c.dataset.testid, c.textContent])),
  })));
  // The rows in the page are the rows of the kernel's answer; returns them once they are all drawn.
  const rowsOfRecord = async expected => {
    const rows = await until(async () => { const r = await navRows(); return r.length === expected.children.length ? r : null; }, "the rows of the record");
    assert.deepEqual(rows.map(r => r.address), expected.children.map(c => c.address), "the rows are the kernel's, in its order");
    for (const row of rows) assert.equal(row.label, expected.children.find(c => c.address === row.address).label);
    return rows;
  };
  // Back closes the layers: the card (when open), then the navigator (the step of the request goes with it); the chat is under it and nothing is left on the stack.
  const leaveGraph = async () => {
    if (await page.getByTestId("node-card").count() > 0) { assert.equal(await back(), true, "Back closes the card"); await page.getByTestId("node-card").waitFor({ state: "detached" }); }
    assert.equal(await page.getByTestId("graph-navigator").count(), 1, "the card closed, the navigator did not");
    assert.equal(await back(), true, "Back closes the navigator");
    await page.getByTestId("graph-navigator").waitFor({ state: "detached" });
    await page.getByTestId("chat-input").waitFor();
    assert.equal(await sheet().count(), 0, "back in the chat, the sheet is not left open under it");
    assert.equal(await back(), false, "no layer is left behind");
  };
  const noScrollOrSmall = async () => {
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth) <= 390, "no horizontal page scroll");
    const small = await page.locator(".crs button").evaluateAll(els => els.filter(el => !el.disabled || true).map(el => { const r = el.getBoundingClientRect(); return { id: el.dataset.testid, w: Math.round(r.width), h: Math.round(r.height) }; }).filter(b => b.h < 44 || b.w < 44));
    assert.deepEqual(small, [], "every button of the sheet is at least 44 x 44 px");
  };

  await page.goto(base + "/");
  await page.getByTestId("chat-shell").waitFor();
  let baseGraph = 0;

  await step("the line under an answer says the profile, the number of items and tokens; no JSON of the trace in chat mode", async () => {
    await send("how do I fix the database API server code?");
    const line = page.getByTestId("context-line-open").last();
    const text = (await line.innerText()).trim();
    assert.match(text, new RegExp("^" + say("line", { summary: "" }).split(" — ")[0].replace(/[.*+?^${}()|[\]\\]/g, "\\$&")), "starts with the word of the pack");
    assert.ok(text.endsWith(" — zobacz") && !text.includes(say("missing")), `the request has its receipt: ${text}`);
    assert.equal(await page.getByTestId("context-trace-json").count(), 0, "no <pre> JSON of the trace in chat mode");
    assert.equal(await page.locator("pre[data-testid=context-trace-json]").count(), 0);
  });

  await step("the sheet: sections as lists in the order sent, tokens, the left-out list, the disclaimer; the cost is unknown in words", async () => {
    await openLast();
    await shot("m1-context-sheet-top");
    assert.equal(await page.getByTestId("context-receipt-title").innerText(), w.title);
    const withItems = await sectionsWithItems();
    assert.ok(withItems.includes("system") && withItems.includes("question"), `Instrukcja and Pytanie are listed: ${withItems}`);
    baseGraph = await graphTokens();
    assert.ok(baseGraph > 0, "the seeded graph reaches the request (the Graf section holds the graph context)");
    assert.equal(await page.getByTestId("receipt-section-title-system").innerText(), w["section.system"]);
    assert.match(await page.getByTestId("context-receipt-tokens").innerText(), /^\d+ tokenów \(szacunek\)$/);
    assert.equal(await page.getByTestId("context-receipt-cost").innerText(), w["cost.unknown"]);
    assert.equal(await page.getByTestId("context-receipt-disclaimer").innerText(), w.disclaimer);
    assert.ok(await page.getByTestId("context-receipt-prefix").innerText().then(x => x.startsWith("prefiks stały")));
    const guardState = await page.getByTestId("context-receipt-guard").getAttribute("data-state");
    assert.ok(["ok", "asked", "unknown"].includes(guardState), `the guard has a state: ${guardState}`);
    const guardText = await page.getByTestId("context-receipt-guard").innerText();
    console.log("[m1-context-sent] rendered guard line: " + JSON.stringify(guardText));
    assert.match(guardText, guardLine(w, guardState), "the guard line is the pack's sentence with the pack's words for the ratio (a number with the sign, 'much less than usual' or 'unknown - no base')");
    assert.ok(!/(^|\D)0×/.test(guardText), "no '0×' for a tiny ratio or a missing base");
    // The order and the flow are said in the pack's short words (the internal name stays only as a data attribute and as the fallback of a name the pack has no word for).
    for (const [kind, header] of [["order", "header.order"], ["flow", "header.flow"]]) {
      const line = page.getByTestId(`context-receipt-${kind}`);
      if (await line.count() === 0) continue;
      const name = await line.getAttribute("data-name");
      assert.ok(name, `the ${kind} line carries its internal name`);
      assert.ok(w[`${kind}.${name}`], `the pack has a word for the ${kind} ${name}`);
      const said = await line.innerText();
      console.log(`[m1-context-sent] rendered ${kind} line: ` + JSON.stringify(said));
      assert.equal(said, w[header].replace(`{{${kind}}}`, w[`${kind}.${name}`]), `the ${kind} is said in words`);
      assert.ok(!said.includes(name), `the internal name ${name} is not shown`);
    }
    assert.equal(await page.getByTestId("context-receipt-dropped-title").innerText(), w["dropped.title"]);
    const itemText = await page.getByTestId("receipt-section-system").locator('[data-testid="receipt-item"]').first().evaluate(el => ({ ws: getComputedStyle(el).whiteSpace, text: el.innerText }));
    assert.equal(itemText.ws, "pre-wrap");
    assert.ok(itemText.text.startsWith(w["ref.system_prompt"]), itemText.text);
    console.log("[m1-context-sent] rendered section Graf: " + JSON.stringify((await page.getByTestId("receipt-section-graph").innerText()).replace(/\n+/g, " | ")));
    await noScrollOrSmall();
  });

  await step("Pokaż w grafie works: the button is enabled, the navigator opens on the request with the sent elements and their tokens (an estimate); derived rows say they are a view of the record", async () => {
    const { focus, expected } = await openGraphFromSheet();
    assert.equal(expected.receipt.present, true);
    assert.deepEqual(expected.perspectives, ["sent_to_model"]);
    const rows = await rowsOfRecord(expected);
    await shot("m1-sent-graph");
    const sentEdges = expected.edges.filter(e => e.kind === "sent_to_model");
    assert.ok(sentEdges.length >= 2 && rows.some(r => r.derived) && rows.some(r => !r.derived), `sent elements, derived and real: ${sentEdges.length} links`);
    for (const row of rows.filter(r => !r.leftOut)) {
      const own = sentEdges.filter(e => e.dst === row.address), child = expected.children.find(c => c.address === row.address);
      assert.ok(own.length > 0, `${row.address} is reached by a sent link`);
      const weight = own.reduce((n, e) => n + e.weight, 0);
      assert.equal(Number(row.tokens), weight, `${row.address}: the weight of the link is the tokens`);
      const tokens = child.derived && child.value_human && own.length === 1 ? child.value_human : gsay("sent.tokens", { n: String(weight) });
      assert.equal(row.chips["nav-sent"], `${own[0].label}: ${tokens}`, "the row says the kernel's name of the link and the tokens as an estimate");
      assert.match(row.chips["nav-sent"], /szacunek/);
      assert.equal(row.chips["nav-derived"], child.derived ? gsay("sent.derived") : undefined, "only derived rows carry the mark");
    }
    // K1c-4: a sent element of the record says why in the pack's words; the kernel's English sentence stays in the record (the answer below) and is not on the screen
    const whys = [];
    for (const row of rows.filter(r => r.derived && !r.leftOut)) {
      const child = expected.children.find(c => c.address === row.address);
      const word = whyWord(w, child.receipt, child.why);
      assert.ok(child.why && child.receipt.ref, "the kernel's record keeps its sentence and the ref of the element");
      assert.equal(row.why, word, `${row.address}: the reason is the word of the pack`);
      assert.notEqual(row.why, child.why, `${row.address}: the English sentence of the record is not what the screen says`);
      whys.push({ ref: child.receipt.ref, kernel: child.why, screen: row.why });
    }
    assert.ok(whys.length >= 2, "at least two sent elements of the record say why");
    console.log("[m1-context-sent] rendered why (record -> screen): " + JSON.stringify(whys));
    await audit("sent-rows");
    const sample = rows.find(r => r.derived), real = rows.find(r => !r.derived && r.chips["nav-sent"]);
    console.log("[m1-context-sent] rendered sent row (real): " + JSON.stringify(real));
    console.log("[m1-context-sent] rendered sent row (derived): " + JSON.stringify(sample));
    const note = id => page.getByTestId(`nav-${id}`).innerText();
    const counts = expected.receipt.counts;
    const summary = gsay("sent.summary", { sent: String(counts.sent), left_out: String(counts.left_out) }) + " · " + gsay("sent.total", { tokens: gsay("sent.tokens", { n: String(expected.receipt.receipt.tokens_est) }) });
    assert.equal(await note("sent-summary"), summary);
    assert.equal(await note("sent-derived-note"), expected.children.find(c => c.derived).derived_note, "once, in the kernel's sentence");
    assert.equal(await note("sent-note"), expected.receipt.note, "a record is not proof: the kernel's sentence");
    assert.equal(await page.getByTestId("nav-sent-handed").count(), 0, "opened on the request itself, nothing was handed over");
    assert.equal(await page.locator('[data-testid="nav-sent-derived-note"]').count(), 1, "the derived sentence is said once, not per row");
    for (const id of ["sent-summary", "sent-derived-note", "sent-note"]) console.log(`[m1-context-sent] rendered note nav-${id}: ` + JSON.stringify(await note(id)));
    const perspective = (await api("GET", "/api/perspectives")).perspectives.find(p => p.id === "sent_to_model").entry.label.pl;
    await until(async () => (await page.getByTestId("nav-perspectives").innerText()) === gsay("perspective.active", { list: perspective }), "the ticked perspective, named in words under the bar");
    assert.ok(!(await page.getByTestId("nav-perspectives").innerText()).includes("sent_to_model"), "not by its internal id");
    assert.equal(await page.locator('[data-testid="nav-rows"] [data-testid="nav-chip"]').evaluateAll(els => els.some(el => /^chip\./.test(el.textContent))), false, "no missing word (a word that is missing shows its id)");
    assert.equal(await page.getByTestId("nav-empty").count(), 0);
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth) <= 390, "no horizontal page scroll");

    // a derived row opens its card: the mark, the kernel's sentence, the tokens as an estimate
    const derivedRow = expected.children.find(c => c.derived && c.kind !== "left_out");
    await page.getByTestId(`nav-row-${derivedRow.address}`).tap();
    await page.locator(`[data-testid="node-card"][data-address="${derivedRow.address}"]`).waitFor();
    const card = await api("POST", "/api/graph/card", { address: derivedRow.address, lang: "pl" });
    assert.equal(card.derived, true);
    await page.getByTestId("card-derived-note").waitFor();
    await page.getByTestId("card-value").waitFor();
    assert.equal(await page.getByTestId("card-derived").innerText(), gsay("sent.derived"));
    assert.ok((await page.getByTestId("card-derived-note").innerText()).endsWith(card.derived_note), "the card says the kernel's sentence: a view of the record, not a stored node");
    const value = await page.getByTestId("card-value").innerText();
    assert.ok(value.includes(card.value_human) && /szacunek/.test(card.value_human), `the card gives the tokens as an estimate: ${value}`);
    assert.equal(await page.getByTestId("card-why").innerText(), `${gsay("card.why")} ${whyWord(w, card.receipt, card.why)}`, "the card says why in the pack's words, not in the kernel's English");
    assert.notEqual(card.why, whyWord(w, card.receipt, card.why));
    await audit("sent-card");
    console.log("[m1-context-sent] rendered derived card: " + JSON.stringify({ mark: await page.getByTestId("card-derived").innerText(), note: await page.getByTestId("card-derived-note").innerText(), value }));
    await page.getByTestId("card-derived-note").scrollIntoViewIfNeeded();
    await shot("m1-sent-card");
    await leaveGraph();
  });

  await step("Następnym razem › szerzej: the NEXT request holds more in Graf, the one after it is back to the base", async () => {
    await openLast();
    await page.getByTestId("receipt-step-wider").tap();
    await page.getByTestId("receipt-modify-result").waitFor();
    assert.equal(await page.getByTestId("receipt-modify-result").getAttribute("data-kind"), "applied");
    await page.getByTestId("receipt-modify-result").evaluate(el => el.scrollIntoView({ block: "end" }));   // the shot shows the sentence the kernel's answer became
    await shot("m1-context-sheet-wider-applied");
    await closeSheet();
    await send("and the database API server code again?");
    await openLast();
    const wider = await graphTokens();
    await shot("m1-sheet-wider-request");
    assert.ok(wider > baseGraph, `Graf grows for the widened request (${baseGraph} -> ${wider} tokens)`);
    assert.match(await page.getByTestId("context-receipt-modifiers").innerText(), new RegExp(w["action.wider"]));
    await closeSheet();
    await send("and the database API server code once more?");
    await openLast();
    assert.equal(await graphTokens(), baseGraph, "the next request is back to the base");
    await shot("m1-sheet-back-to-base");
    assert.equal(await page.getByTestId("context-receipt-modifiers").innerText(), w["header.modifiers_none"]);
    await closeSheet();
  });

  await step("Zmień profil › Zgodność: the next request holds only Instrukcja, Rozmowa and Pytanie; the profile and its status are in the header", async () => {
    await openLast();
    await page.getByTestId("receipt-change-profile").tap();
    const row = page.getByTestId("receipt-profile-compat");
    await row.waitFor();
    assert.equal(await page.getByTestId("receipt-profile-status-compat").innerText(), w[`status.${compat.status}`], "the status of the profile is shown");
    assert.ok((await row.innerText()).includes(compat.label.pl));
    assert.ok(await page.locator('[data-testid^="receipt-profile-status-"]').evaluateAll(els => els.some(el => el.textContent.trim() === "kalibracja")), "profiles in calibration say so");
    await row.tap();
    await page.getByTestId("receipt-modify-result").waitFor();
    assert.equal(await page.getByTestId("receipt-modify-result").getAttribute("data-kind"), "applied");
    await closeSheet();
    await send("with the compatibility profile now?");
    await openLast();
    assert.deepEqual((await sectionsWithItems()).sort(), ["history", "question", "system"], "only the instruction, the conversation and the question went");
    assert.ok((await page.getByTestId("context-receipt-profile").innerText()).startsWith(`Profil: ${compat.label.pl}`));
    assert.ok((await page.getByTestId("receipt-dropped").allInnerTexts()).some(x => x.includes(w["section.memory"])), "what the profile switched off is in the left-out list");
    await shot("m1-context-sheet-compat");
    const { expected } = await openGraphFromSheet();
    const rows = await rowsOfRecord(expected);
    const leftEdges = expected.edges.filter(e => e.kind === "left_out_of_model");
    assert.ok(leftEdges.length >= 1 && expected.receipt.counts.left_out >= 1, "the profile left something out and the graph says so");
    for (const e of leftEdges) {
      const row = rows.find(r => r.address === e.dst);
      assert.equal(row.leftOut, true);
      assert.equal(row.chips["nav-left-out"], e.label, "worded as left out in the kernel's name of the link");
      assert.ok(row.label.includes(e.metadata.reason_human) || row.chips["nav-left-out-reason"] === e.metadata.reason_human, `the reason is said in the kernel's words: ${JSON.stringify(row)}`);
      assert.equal(row.chips["nav-sent"], undefined, "a left-out element is not shown as sent");
    }
    const group = rows.find(r => r.leftOut);
    console.log("[m1-context-sent] rendered left-out row: " + JSON.stringify(group));
    assert.ok((await page.getByTestId("nav-sent-summary").innerText()).startsWith(gsay("sent.summary", { sent: String(expected.receipt.counts.sent), left_out: String(expected.receipt.counts.left_out) })));
    await page.getByTestId("nav-sent-summary").scrollIntoViewIfNeeded();
    await shot("m1-sent-graph-notes");
    await page.getByTestId(`nav-row-${group.address}`).scrollIntoViewIfNeeded();
    await shot("m1-sent-graph-left-out");
    await leaveGraph();
  });

  await step("with the receipt switched off the sheet says there is no receipt, in words; the menu entry opens the sheet too", async () => {
    await api("PUT", "/api/config/context_receipt_store", { value: "never" });
    await send("a message without a receipt");
    await api("PUT", "/api/config/context_receipt_store", { value: "auto" });
    await page.getByTestId("context-line-open").last().tap();
    await sheet().and(page.locator('[data-state="missing"]')).waitFor();
    assert.equal(await page.getByTestId("context-receipt-missing-word").innerText(), w.missing);
    assert.ok((await page.getByTestId("context-line-open").last().innerText()).includes(w.missing), "the line says it too");
    // the honest negative: the graph has no record either - the button is visible, disabled, and the reason is the kernel's own sentence for it
    const request = await sheet().getAttribute("data-request-msg");
    const none = await api("POST", "/api/graph/navigate", { focus: "msg:" + request.replace(/^m_/, ""), perspectives: ["sent_to_model"], lang: "pl" });
    assert.equal(none.receipt.present, false);
    assert.ok(none.receipt.text && none.receipt.missing_reason, "the kernel says why there is no record");
    assert.equal(none.children.length, 0);
    await page.locator('[data-testid="receipt-show-in-graph"][data-graph="no"]').waitFor();
    assert.equal(await page.getByTestId("receipt-show-in-graph").isDisabled(), true);
    assert.equal(await page.getByTestId("receipt-show-in-graph-reason").getAttribute("data-kernel-answer"), none.receipt.text);
    // the reason is said in the sheet's own plain words (the kernel's sentence names a setting key); the kernel's sentence stays on the element
    assert.equal(await page.getByTestId("receipt-show-in-graph-reason").innerText(), w["action.show_in_graph.disabled"].replace("{{reason}}", w.missing));
    console.log("[m1-context-sent] rendered no-record negative: " + JSON.stringify({ sheet: await page.getByTestId("context-receipt-missing-word").innerText(), button: await page.getByTestId("receipt-show-in-graph").innerText(), disabled: true, reason: await page.getByTestId("receipt-show-in-graph-reason").innerText() }));
    await shot("m1-context-sheet-missing");
    await closeSheet();
    await page.getByTestId("shell-msg-actions").last().getByTestId("msg-receipt").evaluate(el => el.click());
    await sheet().and(page.locator('[data-state="missing"]')).waitFor();
    await closeSheet();
  });

  await step("a refusal of the graph is the reason of the disabled button, in the kernel's words (a faked 404: the real kernel answers every message with a record)", async () => {
    await page.route("**/api/graph/navigate", route => route.fulfill({ status: 404, contentType: "application/json", body: JSON.stringify({ error: { code: "not_found", message: "no node at 'msg:test' in the graph projection" } }) }));
    try {
      await send("a message while the graph refuses");
      await page.getByTestId("context-line-open").last().tap();
      await sheet().and(page.locator('[data-state="receipt"]')).waitFor();
      await page.locator('[data-testid="receipt-show-in-graph"][data-graph="no"]').waitFor();
      assert.equal(await page.getByTestId("receipt-show-in-graph").isDisabled(), true);
      const answer = await page.getByTestId("receipt-show-in-graph-reason").getAttribute("data-kernel-answer");
      assert.ok(answer.length > 0 && answer.includes("no node at"), `the refusal is kept: ${answer}`);
      assert.equal(await page.getByTestId("receipt-show-in-graph-reason").innerText(), w["action.show_in_graph.disabled"].replace("{{reason}}", answer));
      await closeSheet();
    } finally { await page.unroute("**/api/graph/navigate"); }
  });

  await step("Back closes the sheet before anything else", async () => {
    await send("one more with a receipt");
    await openLast();
    await page.evaluate(() => window.__loomBack());
    await sheet().waitFor({ state: "detached" });
  });

  await step("a memory node that went with a request says why in the pack's words on the sheet (K1c-4), not in the kernel's English", async () => {
    const created = await api("POST", "/api/memory", { content: "the person prefers short answers about the server", node_type: "text" });
    await send("one more with a memory node");
    await openLast();
    const items = await page.locator('[data-testid="receipt-item"]').evaluateAll(els => els.map(el => ({ text: el.querySelector('[data-testid="receipt-item-text"]')?.textContent ?? "", chip: el.querySelector('[data-testid="receipt-item-chip"]')?.textContent ?? "", why: el.querySelector('[data-testid="receipt-item-why"]')?.textContent ?? null })));
    const node = items.find(i => i.chip === w["chip.memory_node"] && i.why !== null);
    assert.ok(node, `a memory node of the request has a reason line: ${JSON.stringify(items)}`);
    assert.equal(node.why, w["why.memory_node"], "the pack's word");
    assert.equal(node.text, say("item.memory_node", { type: say("node_type.text") }), "the type of the node is said in words, not as the kernel's label");
    console.log("[m1-context-sent] rendered why on the sheet (memory node): " + JSON.stringify(node));
    await page.getByTestId("receipt-item-why").first().scrollIntoViewIfNeeded();
    await shot("m1-context-sheet-memory-why");
    await closeSheet();
    const id = created.id ?? created.node?.id;
    if (id) await api("DELETE", `/api/memory/${id}`);
  });

  await step("no screen shown so far has developer text: a raw key or an English sentence (m1-audit.mjs; the person's own messages and the fold Szczegóły techniczne are skipped)", async () => {
    assert.deepEqual(AUDIT.map(a => `${a.shot}: ${a.why} | ${a.line} | ${a.where}`), [], "every line of the screens is in the words of the pack");
  });

  assert.deepEqual(errors, [], "no page errors");
} finally {
  await browser?.close().catch(() => {});
  server.kill("SIGTERM"); mock.kill("SIGTERM");
}
for (const a of AUDIT) console.log(`[m1-context-sent] AUDIT ${a.shot}: ${a.why} | ${a.line} | ${a.where}`);
console.log(`[m1-context-sent] screenshots in ${SHOTS}`);
process.exit(failed ? 1 : 0);
