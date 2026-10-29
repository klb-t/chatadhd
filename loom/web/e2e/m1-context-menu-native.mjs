#!/usr/bin/env node
// M1-W1b: the composer side of the context on a phone (390x844, touch) against the REAL loom-server with the mock model (mock_llm_server.py): no network.
// + > Kontekst (Szerzej, Głębiej, Węziej, Płycej as chips, Profil selektora, Przepływ tekst <-> graf, Podgląd wysyłki, Profil i pamięć, Twierdzenia z analizy),
// the strip above the composer (what the next request carries), the sheet of the answer (W1a) showing the larger Graf section, the model chip with the
// sheet of where the model comes from. Every word is taken from the pack (presentation.ui_shell / context_receipt); the cross-checks read the data.
// With M1_SHOTS_DIR=<dir> the screenshots go there, otherwise to a fresh temp directory (the repository stays clean).
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { auditPage } from "./m1-audit.mjs";
import { fileURLToPath } from "node:url";
import { fill, forSource, modifiedSentence, ratioSource } from "./m1-words.mjs";
import { stableShot, trackRequests } from "./stable-shot.mjs";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-m1-menu-shots-", { evidence: true });
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[m1-context-menu] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });
const viewPack = JSON.parse(readFileSync(path.join(LOOM, "data/profiles/conversation_view.pack"), "utf8"));
const wordsOf = key => viewPack.entries.find(r => r.key === key).value.locales.pl;
const w = wordsOf("presentation.context_receipt");
const ui = wordsOf("presentation.ui_shell");
const menuData = viewPack.entries.find(r => r.key === "chat.composer_menu").value;
const say = (id, params = {}) => w[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? "");
const profilesPack = JSON.parse(readFileSync(path.join(LOOM, "data/runtime/chat_context.pack"), "utf8")).defaults.selector_profiles;
const compat = profilesPack.find(p => p.id === "compat");

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const mockPort = await freePort(), port = await freePort();
const mock = spawn("python3", [path.join(here, "mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", makeTmpDir("loom-m1-menu-"), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
const AUDIT = [];   // the lines a screenshot's screen shows that look like developer text (m1-audit.mjs)
let browser, failed = false;
const step = async (name, fn) => { try { await fn(); console.log(`[m1-context-menu] PASS ${name}`); } catch (e) { failed = true; console.error(`[m1-context-menu] FAIL ${name}\n${e.stack}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };
const sendApi = async (message, conv) => { const r = await fetch(base + "/api/chat", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify(conv ? { message, conv_id: conv } : { message, new_conversation: true }) }); const t = await r.text(); return (t.match(/"conv_id":"([^"]+)"/) || [])[1]; };

try {
  await waitUp(`${base}/api/healthz`);
  await api("PUT", "/api/config/base_url", { value: `http://127.0.0.1:${mockPort}` });
  await api("PUT", "/api/config/default_model", { value: "mock/e2e-model" });
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  await api("PUT", "/api/config/graph_memory_max_nodes", { value: 2 });   // a small base, so that "szerzej" (x2) has room to show
  await api("POST", "/api/secrets/api_key", { value: "sk-e2e-mock-key" });
  await api("POST", "/api/models/refresh");   // the mock provider lists three models: the picker has options
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


  // ---- helpers of the + menu ----
  const openMenu = async () => { await page.getByTestId("shell-attach").tap(); await page.getByTestId("composer-menu").waitFor(); };
  const openContext = async () => { await openMenu(); await page.getByTestId("composer-menu-context").tap(); await page.getByTestId("composer-menu-wider").waitFor(); };
  const closeMenu = async () => { await page.getByTestId("composer-menu-backdrop").click({ position: { x: 5, y: 5 } }); await page.getByTestId("composer-menu").waitFor({ state: "detached" }); };
  const stripNext = () => page.getByTestId("context-strip-next");
  const smallInMenu = async () => (await page.locator(".composer-menu button").evaluateAll(els => els.map(el => { const r = el.getBoundingClientRect(); return { id: el.dataset.testid, w: Math.round(r.width), h: Math.round(r.height) }; }).filter(b => b.h < 44 || b.w < 44)));

  await page.goto(base + "/");
  await page.getByTestId("chat-shell").waitFor();
  let baseGraph = 0;

  await step("the strip above the composer says the profile in words before anything is sent", async () => {
    const strip = page.getByTestId("context-strip");
    await strip.waitFor();
    // the profile comes from an effect (the scope chain): wait for the text, no sleep
    await page.waitForFunction(() => /^Profil: /.test(document.querySelector('[data-testid="context-strip-profile"]')?.textContent ?? ""));
    const text = await page.getByTestId("context-strip-profile").innerText();
    console.log("[m1-context-menu] rendered strip: " + JSON.stringify(text));
    assert.equal(await stripNext().count(), 0, "no changes waiting: the strip does not invent any");
    assert.equal(await strip.getAttribute("data-mode"), "chips", "chips is the preset of composer_context_strip");
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth) <= 390);
  });

  await step("+ > Kontekst lists the new entries and every old one, in words; steps are chips of 44 px; nothing from the old menu is gone", async () => {
    await openContext();
    await shot("m1-menu-context");
    const ctxItems = menuData.items.find(i => i.id === "context").items;
    for (const item of ctxItems) {
      const row = page.getByTestId(`composer-menu-${item.id}`);
      assert.equal(await row.count(), 1, `${item.id} is in the menu`);
      assert.equal((await row.innerText()).split("\n")[0].trim(), ui[item.label], `${item.id} says its word from the pack`);
    }
    assert.deepEqual(await smallInMenu(), [], "every button of the menu is at least 44 x 44 px");
    assert.equal(await page.getByTestId("composer-menu-steps-help").innerText(), ui.cm_steps_help);
    assert.equal(await page.getByTestId("composer-menu-steps-add-up").innerText(), ui.cm_steps_add_up, "the submenu says that clicks add up");
    assert.equal(await page.getByTestId("composer-menu-memory").innerText().then(x => x.split("\n")[0].trim()), "Profil i pamięć (z węzła Ty)");
    assert.equal(await page.getByTestId("composer-menu-knowledge").innerText().then(x => x.split("\n")[0].trim()), "Twierdzenia z analizy");
    console.log("[m1-context-menu] rendered Kontekst submenu: " + JSON.stringify((await page.getByTestId("composer-menu").innerText()).replace(/\n+/g, " | ")));
    // the memory switch still switches (rename only): on -> chip is off-state words; back
    await page.getByTestId("composer-menu-memory").tap();
    assert.equal(await page.getByTestId("composer-menu-memory").getAttribute("aria-checked"), "false", "the memory switch still turns off");
    await page.getByTestId("composer-menu-memory").tap();
    assert.equal(await page.getByTestId("composer-menu-memory").getAttribute("aria-checked"), "true");
    await closeMenu();
  });

  await step("the base: a first question; the sheet of its answer measures the Graf section", async () => {
    await send("how do I fix the database API server code?");
    await openLast();
    baseGraph = await graphTokens();
    assert.ok(baseGraph > 0, "the seeded graph reaches the request");
    await closeSheet();
  });

  await step("Szerzej: the kernel's sentence says the next request only, the strip says Następne zapytanie: szerzej; after sending the strip is clear and the answer holds more Graf", async () => {
    await openContext();
    await page.getByTestId("composer-menu-wider").tap();
    await page.getByTestId("composer-menu-modify-result").waitFor();
    assert.equal(await page.getByTestId("composer-menu-modify-result").getAttribute("data-kind"), "applied");
    const said = await page.getByTestId("composer-menu-modify-result").innerText();
    console.log("[m1-context-menu] rendered kernel answer: " + JSON.stringify(said));
    assert.match(said, modifiedSentence(w, w["action.wider"]), "the sentence is the pack's, in the screen's language, with the estimate in words (a number with the sign, 'much less than usual' or 'unknown - no base', never '0×')");
    assert.ok(!/(^|\D)0×/.test(said), "no '0×' in the estimate");
    await shot("m1-menu-wider-applied");
    assert.ok(await page.getByTestId("composer-menu-reset").count() === 1, "taking the change back is offered");
    await closeMenu();
    await stripNext().waitFor();
    assert.equal(await stripNext().innerText(), `Następne zapytanie: ${w["action.wider"]}`);
    await shot("m1-strip-next-wider");
    await send("and the database API server code again?");
    await stripNext().waitFor({ state: "detached" });
    await openLast();
    const wider = await graphTokens();
    assert.ok(wider > baseGraph, `Graf grows for the widened request (${baseGraph} -> ${wider} tokens)`);
    assert.match(await page.getByTestId("context-receipt-modifiers").innerText(), new RegExp(w["action.wider"]));
    await closeSheet();
    await send("and the database API server code once more?");
    await openLast();
    assert.equal(await graphTokens(), baseGraph, "the next request is back to the base");
    await closeSheet();
  });

  await step("one number for one request: the estimate after Szerzej (with the text typed) and the guard of the receipt after sending say the same, with the same base named", async () => {
    const typed = "and the database API server code, same question as a third time?";
    await page.getByTestId("chat-input").fill(typed);
    await openContext();
    await page.getByTestId("composer-menu-wider").tap();
    await page.getByTestId("composer-menu-modify-result").waitFor();
    assert.equal(await page.getByTestId("composer-menu-modify-result").getAttribute("data-kind"), "applied");
    const said = await page.getByTestId("composer-menu-modify-result").innerText();
    console.log("[m1-context-menu] rendered estimate with the typed text: " + JSON.stringify(said));
    const estimate = said.match(new RegExp(fill(w["modify.ratio"], { for: forSource(w), ratio: `(${ratioSource(w)})` })))?.[1];
    assert.ok(estimate, "the estimate sentence is the pack's");
    assert.ok(said.includes(w["for.text"]), "the estimate says it was compiled with the typed text");
    await closeMenu();
    const answers = await assistants().count();
    await page.getByTestId("send-chat").tap();
    // the new answer is complete and its line is drawn (counts are awaited, no sleep): only then is the last receipt the one of this request
    await page.waitForFunction(n => document.querySelectorAll('[data-testid="message"][data-role="assistant"]').length > n && !document.querySelector('[data-testid="streaming-message"]'), answers, { timeout: 30000 });
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="message"][data-role="assistant"]').length === document.querySelectorAll('[data-testid="context-line"]').length);
    await openLast();
    const guardText = await page.getByTestId("context-receipt-guard").innerText();
    console.log("[m1-context-menu] rendered guard of the same request: " + JSON.stringify(guardText));
    const guard = guardText.match(new RegExp("^" + fill(w["guard.ok"], { ratio: `(${ratioSource(w)})` }) + "$"))?.[1];
    assert.ok(guard, "the guard line is the pack's sentence");
    assert.equal(guard, estimate, "the estimate before sending and the guard after it are the same number against the same named base");
    await closeSheet();
  });

  await step("a change can be taken back: Szerzej then Cofnij leaves the strip clear and the next request at the base", async () => {
    await openContext();
    await page.getByTestId("composer-menu-wider").tap();
    await page.getByTestId("composer-menu-reset").waitFor();
    await page.getByTestId("composer-menu-reset").tap();
    await page.getByTestId("composer-menu-reset").waitFor({ state: "detached" });
    await closeMenu();
    assert.equal(await stripNext().count(), 0);
  });

  await step("Profil selektora: the list from the effective view with status words; Zgodność is for the next request only (strip, then only Instrukcja, Rozmowa, Pytanie)", async () => {
    await openContext();
    await page.getByTestId("composer-menu-selector_profile").tap();
    const row = page.getByTestId("composer-profile-compat");
    await row.waitFor();
    assert.equal(await page.getByTestId("composer-profiles-help").innerText(), ui.cm_profiles_help);
    assert.equal(await page.getByTestId("composer-profile-status-compat").innerText(), ui[`cm_status_${compat.status}`]);
    assert.ok(await page.locator('[data-testid^="composer-profile-status-"]').evaluateAll(els => els.some(el => el.textContent.trim() === "kalibracja")), "profiles in calibration say so");
    assert.ok((await row.innerText()).includes(compat.label.pl));
    assert.deepEqual(await smallInMenu(), []);
    await shot("m1-menu-profiles");
    await row.tap();
    await page.getByTestId("composer-menu-modify-result").waitFor();
    assert.equal(await page.getByTestId("composer-menu-modify-result").getAttribute("data-kind"), "applied");
    await page.evaluate(() => window.__loomBack());   // Back leaves the profile list, then the menu
    await closeMenu().catch(async () => { await page.keyboard.press("Escape"); });
    await stripNext().waitFor();
    assert.equal(await stripNext().innerText(), `Następne zapytanie: profil ${compat.label.pl}`);
    await send("with the compatibility profile now?");
    await stripNext().waitFor({ state: "detached" });
    await openLast();
    assert.deepEqual((await sectionsWithItems()).sort(), ["history", "question", "system"], "only the instruction, the conversation and the question went");
    assert.ok((await page.getByTestId("context-receipt-profile").innerText()).startsWith(`Profil: ${compat.label.pl}`));
    await closeSheet();
  });

  await step("Podgląd wysyłki: unavailable with its reason while the field is empty; with a text it shows the request to be sent once and can be discarded", async () => {
    await openContext();
    const row = page.getByTestId("composer-menu-preview_request");
    assert.equal(await row.getAttribute("aria-disabled"), "true");
    assert.equal(await page.getByTestId("composer-menu-reason-preview_request").innerText(), ui.cm_reason_preview_empty);
    await closeMenu();
    await page.getByTestId("chat-input").fill("a question to preview");
    await openContext();
    assert.equal(await page.getByTestId("composer-menu-preview_request").getAttribute("aria-disabled"), null);
    assert.equal(await page.getByTestId("composer-menu-help-preview_request").innerText(), ui.cm_preview_help);
    await page.getByTestId("composer-menu-preview_request").tap();
    const box = page.getByTestId("one-call-request");
    await box.waitFor();
    assert.match(await box.inputValue(), /a question to preview/);
    assert.equal(await page.getByTestId("chat-preview-request").locator("strong").innerText(), ui.pv_title);
    await shot("m1-menu-preview");
    await page.getByTestId("discard-one-call-request").tap();
    await box.waitFor({ state: "detached" });
    await page.getByTestId("chat-input").fill("");
  });

  await step("Przepływ tekst <-> graf opens the card of the setting chat_flow in the navigator; Back returns to the chat", async () => {
    await openContext();
    assert.equal(await page.getByTestId("composer-menu-help-flow_text_graph").innerText(), ui.cm_flow_help);
    await page.getByTestId("composer-menu-flow_text_graph").tap();
    await page.getByTestId("graph-navigator").waitFor();
    await page.getByTestId("node-card").and(page.locator('[data-address="setting:chat_flow"]')).waitFor();
    await shot("m1-menu-flow-card");
    // Back closes the card first (the card is a layer), then the navigator: press until the navigator is gone (each press waits for its effect)
    for (let i = 0; i < 4; i++) {
      await page.evaluate(() => window.__loomBack());
      if (await page.getByTestId("panel-navigator").waitFor({ state: "detached", timeout: 1500 }).then(() => true, () => false)) break;
    }
    assert.equal(await page.getByTestId("panel-navigator").count(), 0, "Back leaves the navigator");
    await page.getByTestId("chat-input").waitFor();
  });

  await step("the model chip: a short name; the info button opens the sheet with the level, the class and the price in words; the picker is still the same select", async () => {
    const picker = page.getByTestId("model-picker");
    const options = await picker.evaluate(el => [...el.options].map(o => ({ value: o.value, text: o.textContent })));
    assert.ok(options.some(o => o.value === "mock/second-model" && o.text === "second-model"), `options show the short name: ${JSON.stringify(options)}`);
    // With a conversation open, a pick of the picker is written to the conversation level of the scope chain: wait for that write before asking where the model comes from
    const written = page.waitForResponse(r => /\/api\/conversations\//.test(r.url()) && r.request().method() !== "GET");
    await picker.selectOption("mock/second-model");
    await written;
    assert.equal(await picker.evaluate(el => el.selectedOptions[0].textContent), "second-model", "the chip shows the short name");
    assert.ok(await page.getByTestId("model-chip").evaluate(el => el.getBoundingClientRect().right <= 390), "the chip fits the phone");
    const rolesAnswer = page.waitForResponse(r => /\/api\/roles\/resolved/.test(r.url()) && r.request().method() === "POST");
    await page.getByTestId("model-provenance-open").tap();
    const sheet = page.getByTestId("model-provenance");
    await sheet.and(page.locator('[data-state="ready"]')).waitFor();
    await shot("m1-model-provenance");
    assert.equal(await page.getByTestId("model-provenance-model").innerText(), "Model: mock/second-model");
    assert.equal(await page.getByTestId("model-provenance-set-by").innerText(), "Ustawiony na poziomie: Rozmowa", "the pick is the conversation's own: the level of the chain that set it is named");
    assert.ok((await page.getByTestId("model-provenance-chain").locator('[data-level="conversation"]').innerText()).endsWith("podał wartość"), "the chain says which level supplied the value");
    assert.match(await page.getByTestId("model-provenance-class").innerText(), /^Klasa: /);
    // The class line is the pack's short words; the kernel's own sentence (an engineer's English sentence) is word for word in a folded block, closed by default.
    const classLine = await page.getByTestId("model-provenance-class").innerText();
    const kernelRole = (await (await rolesAnswer).json()).roles.find(r => r.id === "chat.default");   // the very answer the sheet read
    console.log("[m1-context-menu] kernel role of the answer: " + JSON.stringify({ model: kernelRole?.model, tier: kernelRole?.tier }));
    if (kernelRole?.model === "mock/second-model" && kernelRole.tier?.reason && ["unknown", "none"].includes(kernelRole.tier.id)) {
      assert.equal(classLine, ui.mp_class_plain, "an unknown class is said in the pack's short words");
      const details = page.getByTestId("model-provenance-details");
      assert.equal(await details.locator("button.disclosure-head").getAttribute("aria-expanded"), "false", "the technical details are folded by default");
      assert.equal(await details.locator(".disclosure-label").innerText(), ui.mp_details);
      assert.equal(await page.getByTestId("model-provenance-technical").isVisible(), false, "folded: the kernel's sentence is not shown");
      await details.locator("button.disclosure-head").tap();
      await page.getByTestId("model-provenance-technical").waitFor({ state: "visible" });
      assert.equal(await page.getByTestId("model-provenance-technical").innerText(), kernelRole.tier.reason, "the kernel's sentence is kept word for word");
      await shot("m1-model-provenance-details");
      await details.locator("button.disclosure-head").tap();
      await page.getByTestId("model-provenance-technical").waitFor({ state: "hidden" });
    } else assert.equal(await page.getByTestId("model-provenance-details").count(), 0, "a known class has nothing to fold");
    assert.equal(await page.getByTestId("model-provenance-price").innerText(), ui.mp_price_unknown, "the model list has no price: unknown, not zero");
    assert.ok((await page.getByTestId("model-provenance-chain").locator("li").count()) > 0, "the levels that said something are listed");
    assert.match(await page.getByTestId("model-provenance-chain-rest").innerText(), /^Pozostałe poziomy nie mają wartości/, "the other levels are named in one line");
    console.log("[m1-context-menu] rendered model provenance: " + JSON.stringify((await sheet.innerText()).replace(/\n+/g, " | ")));
    const small = await page.locator(".crs button").evaluateAll(els => els.map(el => { const r = el.getBoundingClientRect(); return { id: el.dataset.testid, w: Math.round(r.width), h: Math.round(r.height) }; }).filter(b => b.h < 44 || b.w < 44));
    assert.deepEqual(small, []);
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth) <= 390);
    await page.evaluate(() => window.__loomBack());
    await sheet.waitFor({ state: "detached" });
    const cleared = page.waitForResponse(r => /\/api\/conversations\//.test(r.url()) && r.request().method() !== "GET");
    await picker.selectOption("");
    await cleared;
    await page.getByTestId("model-provenance-open").tap();
    await sheet.and(page.locator('[data-state="ready"]')).waitFor();
    assert.equal(await page.getByTestId("model-provenance-model").innerText(), "Model: mock/e2e-model", "the default model is the one the chain gives");
    // R67-fix: the empty option names the model the next request WILL use (the chain's answer, short name) plus the pack's default marker (tb_model_default) - not the old words (cm_model_default).
    const resolvedModel = (await page.getByTestId("model-provenance-model").innerText()).replace(/^Model: /, "");
    const defaultLabel = ui.tb_model_default.replace("{{model}}", resolvedModel.slice(resolvedModel.lastIndexOf("/") + 1));
    assert.notEqual(defaultLabel, ui.cm_model_default, "the truthful label is not the placeholder");
    assert.equal(await picker.evaluate(el => el.selectedOptions[0].textContent), defaultLabel, "the empty option reads: resolved model + the pack's default marker");
    assert.match(await page.getByTestId("model-provenance-set-by").innerText(), /^Ustawiony na poziomie: |^Żaden poziom/);
    await page.getByTestId("model-provenance-close").tap();
    await sheet.waitFor({ state: "detached" });
  });

  await step("composer_context_strip = hidden removes the strip; full says it in a sentence", async () => {
    await api("PUT", "/api/config/composer_context_strip", { value: "hidden" });
    await page.reload();
    await page.getByTestId("chat-shell").waitFor();
    await page.getByTestId("chat-input").waitFor();
    await page.waitForFunction(() => !document.querySelector('[data-testid="context-strip"]'));
    await api("PUT", "/api/config/composer_context_strip", { value: "full" });
    await page.reload();
    await page.getByTestId("context-strip-full").waitFor();
    const full = await page.getByTestId("context-strip-full").innerText();
    console.log("[m1-context-menu] rendered full strip: " + JSON.stringify(full));
    assert.ok(full.includes("Następne zapytanie pójdzie z profilem"));
    await api("PUT", "/api/config/composer_context_strip", { value: "chips" });
  });

  await step("no screen shown so far has developer text: a raw key or an English sentence (m1-audit.mjs; the person's own messages and the fold Szczegóły techniczne are skipped)", async () => {
    assert.deepEqual(AUDIT.map(a => `${a.shot}: ${a.why} | ${a.line} | ${a.where}`), [], "every line of the screens is in the words of the pack");
  });

  assert.deepEqual(errors, [], "no page errors");
} finally {
  await browser?.close().catch(() => {});
  server.kill("SIGTERM"); mock.kill("SIGTERM");
}
for (const a of AUDIT) console.log(`[m1-context-menu] AUDIT ${a.shot}: ${a.why} | ${a.line} | ${a.where}`);
console.log(`[m1-context-menu] screenshots in ${SHOTS}`);
process.exit(failed ? 1 : 0);
