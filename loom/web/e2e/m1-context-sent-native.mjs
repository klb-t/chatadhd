#!/usr/bin/env node
// M1-W1a: the sheet "Co wysłaliśmy" on a phone (390x844, touch) against the REAL loom-server with the mock model (mock_llm_server.py): no network.
// A conversation with a graph behind it is seeded through the API; then in the UI: the line "Kontekst: ... — zobacz" under an answer opens the sheet
// (sections as lists, tokens, "Pominięte", header, disclaimer); "Szerzej" is tapped in the sheet and the NEXT message is compiled wider (the Graf section
// of that request holds more) while the one after it is back to the base; the profile "Zgodność" leaves only the sections Instrukcja, Rozmowa and
// Pytanie; with the receipt switched off the sheet says "Brak śladu (zapisane przed M1)"; "Pokaż w grafie" stays visible and disabled with its reason
// while the kernel has no source for the perspective; no JSON of the trace in chat mode. Every word is taken from the pack (presentation.context_receipt).
// With M1_SHOTS_DIR=<dir> the screenshots go there, otherwise to a fresh temp directory (the repository stays clean).
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { guardLine } from "./m1-words.mjs";

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
  const errors = [];
  page.on("pageerror", e => errors.push(String(e)));
  const shot = async name => { await page.evaluate(() => Promise.all(document.getAnimations().map(a => a.finished.catch(() => {})))); await page.screenshot({ path: path.join(SHOTS, `${name}.png`) }); };
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
    // the graph question is answered by an effect: wait for the reason to carry the kernel's answer before anything is read from the button
    await page.getByTestId("receipt-show-in-graph-reason").waitFor();
  };
  const closeSheet = async () => { await page.getByTestId("context-receipt-close").tap(); await sheet().waitFor({ state: "detached" }); };
  const sectionsWithItems = () => page.locator('[data-testid^="receipt-section-"][data-items]').evaluateAll(els => els.filter(el => Number(el.dataset.items) > 0).map(el => el.dataset.class));
  const graphTokens = async () => { const s = page.getByTestId("receipt-section-graph"); return (await s.count()) ? Number(await s.getAttribute("data-tokens")) : 0; };
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

  await step("Pokaż w grafie stays visible and disabled with its reason while the kernel has no source for the perspective", async () => {
    const button = page.getByTestId("receipt-show-in-graph");
    await button.waitFor();
    assert.equal(await button.innerText(), w["action.show_in_graph"]);
    assert.equal(await button.isDisabled(), true);
    assert.equal(await page.getByTestId("receipt-show-in-graph-reason").innerText(), w["action.show_in_graph.disabled"]);
    assert.ok((await page.getByTestId("receipt-show-in-graph-reason").getAttribute("data-kernel-answer")).length > 0, "the kernel's own answer is kept on the element");
  });

  await step("Następnym razem › szerzej: the NEXT request holds more in Graf, the one after it is back to the base", async () => {
    await page.getByTestId("receipt-step-wider").tap();
    await page.getByTestId("receipt-modify-result").waitFor();
    assert.equal(await page.getByTestId("receipt-modify-result").getAttribute("data-kind"), "applied");
    await shot("m1-context-sheet-wider-applied");
    await closeSheet();
    await send("and the database API server code again?");
    await openLast();
    const wider = await graphTokens();
    assert.ok(wider > baseGraph, `Graf grows for the widened request (${baseGraph} -> ${wider} tokens)`);
    assert.match(await page.getByTestId("context-receipt-modifiers").innerText(), new RegExp(w["action.wider"]));
    await closeSheet();
    await send("and the database API server code once more?");
    await openLast();
    assert.equal(await graphTokens(), baseGraph, "the next request is back to the base");
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
    await closeSheet();
  });

  await step("with the receipt switched off the sheet says there is no receipt, in words; the menu entry opens the sheet too", async () => {
    await api("PUT", "/api/config/context_receipt_store", { value: "never" });
    await send("a message without a receipt");
    await api("PUT", "/api/config/context_receipt_store", { value: "auto" });
    await page.getByTestId("context-line-open").last().tap();
    await sheet().and(page.locator('[data-state="missing"]')).waitFor();
    assert.equal(await page.getByTestId("context-receipt-missing-word").innerText(), w.missing);
    assert.ok((await page.getByTestId("context-line-open").last().innerText()).includes(w.missing), "the line says it too");
    await shot("m1-context-sheet-missing");
    await closeSheet();
    await page.getByTestId("shell-msg-actions").last().getByTestId("msg-receipt").evaluate(el => el.click());
    await sheet().and(page.locator('[data-state="missing"]')).waitFor();
    await closeSheet();
  });

  await step("Back closes the sheet before anything else", async () => {
    await send("one more with a receipt");
    await openLast();
    await page.evaluate(() => window.__loomBack());
    await sheet().waitFor({ state: "detached" });
  });

  assert.deepEqual(errors, [], "no page errors");
} finally {
  await browser?.close().catch(() => {});
  server.kill("SIGTERM"); mock.kill("SIGTERM");
}
console.log(`[m1-context-sent] screenshots in ${SHOTS}`);
process.exit(failed ? 1 : 0);
