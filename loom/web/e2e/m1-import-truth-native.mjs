#!/usr/bin/env node
// M1-G1b: the import tells the truth, the chat shows what was imported, the graph understands the owner's sentences, the help says what the screens are.
// On a phone (390x844, touch) against the REAL loom-server and a mock model server that must receive 0 requests. Everything asserted about the import is compared with
// what the kernel answered on the wire (the `done` chunk of POST /api/import), the words with the pack (presentation.ui_shell / graph_screen of conversation_view.pack):
// no sentence of the kernel and no label is typed here, except the two phrases the owner asked about, which are the point of the test.
// Fixture: tests/fixtures/exports/anthropic_2026_full.zip - SYNTHETIC (tools/gen_export_fixtures.py), never an export of a person.
// Checks: (1) Workshop > Import: the Polish summary with "znaleziono 3, zapisano 0" and its reason, the per-kind table (found / saved / reason), the technical details closed
// and opened; (2) the imported conversation: a chip for every attachment with its state, the "Imported source" block in Polish; (3) "pokaż katalog główny grafu" and
// "pokaż import w grafie" typed in the chat open the navigator at the root / at "Dane i korpusy" and the answer stays in the conversation; (4) the help "Co tu widzę?" of
// the navigator and of the old map (closed, opened, remembered in the settings); (5) the Workshop button of the old map no longer shares its words with the menu entry;
// (6) 0 model requests; the text audit (m1-audit.mjs) finds nothing on any screenshot.
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
const SHOTS = process.env.M1_SHOTS_DIR || mkdtempSync(path.join(tmpdir(), "loom-m1-import-truth-shots-"));
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
const FIXTURE = path.join(LOOM, "tests/fixtures/exports/anthropic_2026_full.zip");
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html")) || !existsSync(FIXTURE)) { console.error("[m1-import-truth] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });
const viewPack = JSON.parse(readFileSync(path.join(LOOM, "data/profiles/conversation_view.pack"), "utf8"));
const wordsOf = key => viewPack.entries.find(r => r.key === key).value.locales.pl;
const shell = wordsOf("presentation.ui_shell");
const graph = wordsOf("presentation.graph_screen");
const say = (words, id, params = {}) => words[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? "");

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const mockPort = await freePort(), port = await freePort();
const mock = spawn("python3", [path.join(here, "mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-m1-import-truth-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
const AUDIT = [];
let browser, failed = false;
const step = async (name, fn) => { try { await fn(); console.log(`[m1-import-truth] PASS ${name}`); } catch (e) { failed = true; console.error(`[m1-import-truth] FAIL ${name}\n${e.stack}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json().catch(() => ({})); };
const modelRequests = async () => (await (await fetch(`http://127.0.0.1:${mockPort}/__count`)).json()).requests;
const REPORT = {};   // what the card pastes

try {
  await waitUp(`${base}/api/healthz`);
  for (let i = 0; i < 100; i++) { try { await modelRequests(); break; } catch { await new Promise(r => setTimeout(r, 100)); } }
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
  const audit = async name => { for (const f of await auditPage(page)) AUDIT.push({ shot: name, ...f }); };
  const shot = async name => { await page.evaluate(() => Promise.all(document.getAnimations().map(a => a.finished.catch(() => {})))); await page.screenshot({ path: path.join(SHOTS, `${name}.png`) }); await audit(name); };
  const noSideScroll = async what => assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), `${what}: no page scroll wider than the phone`);
  const until = async (read, what, ms = 15000) => { let last; for (const end = Date.now() + ms; Date.now() < end;) { last = await read(); if (last) return last; await page.waitForTimeout(50); } throw new Error(`timed out waiting for ${what} (last: ${JSON.stringify(last)})`); };
  const text = async id => (await page.getByTestId(id).innerText()).replace(/\s+/g, " ").trim();
  const nav = () => page.getByTestId("graph-navigator");
  const toWorkshop = async () => {
    await page.getByTestId("shell-menu-toggle").tap();
    await page.getByTestId("shell-menu-workshop").tap();
  };
  const workshopPanel = async id => {
    if (!(await page.getByTestId(`nav-${id}`).isVisible())) await page.getByTestId("toggle-nav").tap();
    await page.getByTestId(`nav-${id}`).tap();
    await page.getByTestId(`panel-${id}`).waitFor();
  };
  const backToChat = async () => {
    if (!(await page.getByTestId("nav-chat").isVisible())) await page.getByTestId("toggle-nav").tap();
    await page.getByTestId("nav-chat").tap();
    await page.getByTestId("chat-shell").waitFor();
  };
  const sendText = async message => {
    const before = await page.locator('[data-testid="message"][data-role="assistant"]').count();
    await page.getByTestId("chat-input").fill(message);
    await page.getByTestId("send-chat").tap();
    await page.waitForFunction(n => document.querySelectorAll('[data-testid="message"][data-role="assistant"]').length > n && !document.querySelector('[data-testid="streaming-message"]'), before, { timeout: 30000 });
  };

  await page.goto(base);
  await page.getByTestId("chat-shell").waitFor();

  await step("(1) Workshop > Import: the Polish summary, the table found / saved / reason, the technical details behind a disclosure", async () => {
    await toWorkshop();
    await workshopPanel("import");
    // the kernel's answer on the wire: the page's own fetch is tee'd (Playwright's response.body() re-encodes a streamed text/event-stream body as Latin-1 text)
    await page.evaluate(() => {
      const original = window.fetch.bind(window);
      window.__importBody = null;
      window.fetch = async (...args) => {
        const response = await original(...args);
        if (String(args[0]).includes("/api/import")) response.clone().text().then(text => { window.__importBody = text; });
        return response;
      };
    });
    const done = (async () => {
      const body = await until(() => page.evaluate(() => window.__importBody), "the import answer on the wire", 60000);
      const chunks = body.split("\n\n").map(b => b.split("\n").find(l => l.startsWith("data:"))).filter(Boolean).map(l => JSON.parse(l.slice(5)));
      return chunks.find(c => c.type === "done");
    })();
    await page.getByTestId("import-file-input").setInputFiles(FIXTURE);
    await page.getByTestId("start-import").tap();
    const wire = await done;
    assert.ok(wire?.truth, "the kernel's done chunk carries truth");
    await page.getByTestId("import-result").waitFor();
    await page.getByTestId("import-summary").waitFor();

    // the sentence: the kernel's Polish text, with the numbers the owner asked about
    const summary = await text("import-summary");
    assert.equal(summary, wire.truth.summary.pl.replace(/\s+/g, " ").trim(), "the page shows the kernel's Polish sentence as it came");
    assert.match(summary, /znaleziono 3, zapisano 0/);
    assert.match(summary, /eksport Claude\.ai nie zawiera plików załączników/);
    assert.ok(!/Imported \d+ messages/.test(await page.getByTestId("import-result").innerText()), "the English line is gone");
    assert.equal(await page.getByTestId("import-summary").getAttribute("class").then(c => /\bwarn\b/.test(c)), true, "not everything was saved: the summary is not green");
    REPORT.summary = summary;

    // the table: one row per kind the kernel showed, the numbers and the status are the kernel's
    const shown = wire.truth.elements.filter(e => e.shown !== false), absent = wire.truth.elements.filter(e => e.shown === false);
    const rows = await page.locator('[data-testid^="import-row-"][data-status]').evaluateAll(els => els.map(el => ({ testid: el.dataset.testid, status: el.dataset.status, found: el.dataset.found, saved: el.dataset.saved, cells: [...el.children].map(c => c.innerText.replace(/\s+/g, " ").trim()) })));
    assert.deepEqual(rows.map(r => r.testid), shown.map(e => `import-row-${e.kind}`), "the rows are the kernel's kinds, in its order");
    for (const [i, e] of shown.entries()) {
      assert.equal(rows[i].status, e.status, e.kind);
      assert.equal(rows[i].found, String(e.found), e.kind); assert.equal(rows[i].saved, String(e.imported), e.kind);
      assert.equal(rows[i].cells[0], e.label.pl, `${e.kind}: the label is the kernel's Polish one`);
      assert.equal(rows[i].cells[1], e.found === null ? shell.import_not_counted : String(e.found));
      assert.ok(rows[i].cells[2].startsWith(String(e.imported)) && rows[i].cells[2].endsWith(shell[`import_status_${e.status}`]), `${e.kind}: saved cell "${rows[i].cells[2]}"`);
      const reason = page.getByTestId(`import-row-reason-${e.kind}`);
      if (e.reason) assert.equal(await reason.innerText().then(t => t.replace(/\s+/g, " ").trim()), say(shell, "import_reason", { reason: e.reason.text.pl }), `${e.kind}: the reason is the kernel's`);
      else assert.equal(await reason.count(), 0, `${e.kind}: no reason where all was saved`);
    }
    const attachments = rows.find(r => r.testid === "import-row-attachments");
    assert.equal(attachments.found, "3"); assert.equal(attachments.saved, "0"); assert.equal(attachments.status, "none");
    assert.equal(await text("import-absent"), say(shell, "import_absent", { kinds: absent.map(e => e.label.pl).join(", ") }), "the kinds the archive had nothing of are named");
    REPORT.table = rows.map(r => `${r.cells[0]}: znaleziono ${r.cells[1]}, zapisano ${r.cells[2]}`);
    REPORT.attachmentsReason = await page.getByTestId("import-row-reason-attachments").innerText();
    await noSideScroll("import result");

    // the raw answer behind the disclosure: closed, then open
    const head = page.getByTestId("import-details").locator(".disclosure-head");
    assert.equal(await head.getAttribute("aria-expanded"), "false", "closed by default");
    assert.equal(await head.innerText(), shell.import_details_show);
    assert.equal(await page.getByTestId("import-raw").isVisible(), false, "the raw JSON is not on the screen while closed");
    await head.tap();
    await page.getByTestId("import-raw").waitFor({ state: "visible" });
    assert.equal(await head.getAttribute("aria-expanded"), "true");
    assert.equal(await head.innerText(), shell.import_details_hide);
    const raw = JSON.parse(await page.getByTestId("import-raw").innerText());
    assert.equal(raw.truth.summary.pl, wire.truth.summary.pl, "the details are the same answer");
    assert.ok((await head.boundingBox()).height >= 44, "the disclosure head is a 44 px target");
    await shot("import-result");
    assert.deepEqual(errors, [], "no page errors");
  });

  await step("(2) the imported conversation: a chip for every attachment with its state, the Imported source block in Polish", async () => {
    const list = await api("GET", "/api/conversations?limit=50");
    const convs = Array.isArray(list) ? list : list.conversations;
    let target = null, attachments = [];
    for (const c of convs) {
      const messages = await api("GET", `/api/conversations/${c.id}/messages`);
      for (const m of (Array.isArray(messages) ? messages : messages.messages)) {
        const found = m.metadata?.export?.attachments;
        if (Array.isArray(found) && found.length > 0) { target = { conv: c.id, message: m }; attachments = found; break; }
      }
      if (target) break;
    }
    assert.ok(target, "the synthetic archive has a message with attachments");
    assert.equal(attachments.length, 3, "the same number the import counted");
    await page.goto(`${base}/?conv=1#czat/${target.conv}`);
    await page.getByTestId("chat-shell").waitFor();
    const chips = page.locator(`[data-testid="message"][data-role="${target.message.role}"]`).filter({ has: page.getByTestId("message-attachments") });
    await chips.first().waitFor();
    const names = await chips.first().getByTestId("message-attachment").locator(".chip-name").allInnerTexts();
    assert.deepEqual(names, attachments.map(a => a.name), "one chip per attachment, in the export's order");
    const states = await chips.first().getByTestId("message-attachment-state").allInnerTexts();
    assert.equal(states.length, 3);
    for (const s of states) assert.match(s, new RegExp(shell.att_names_only), "no file was saved: the chip says names only");
    REPORT.chips = (await chips.first().getByTestId("message-attachments").innerText()).replace(/\s+/g, " ");
    // the block keeps the original and now speaks Polish
    const details = chips.first().getByTestId("imported-source-details");
    const summaryLine = await details.locator("summary").first().innerText();
    assert.match(summaryLine, /^Oryginał z eksportu · anthropic · bloki: \d+ · odnośniki: \d+ · artefakty: \d+$/);
    await details.locator("summary").first().tap();
    await details.getByText(shell.src_intro, { exact: true }).waitFor();
    await details.getByText(shell.src_whole, { exact: true }).waitFor();
    assert.equal(await details.getByText("Complete preserved source message").count(), 0, "no English title left in the block's frame");
    const referenceTitles = await details.locator('[data-testid="imported-source-reference"] h4').allInnerTexts();
    assert.ok(referenceTitles.length >= 3 && referenceTitles.every(t => t.startsWith(shell.src_refkind_attachment + ":") || t.startsWith(shell.src_refkind_citation + ":") || t.startsWith(shell.src_refkind_media_pointer + ":")), referenceTitles.join(" | "));
    const resolved = await details.getByText(shell.src_unresolved, { exact: true }).count();
    assert.ok(resolved >= 3, "each attachment reference says the bytes were not found, in Polish");
    await chips.first().scrollIntoViewIfNeeded();
    await shot("imported-conversation");
    await noSideScroll("imported conversation");
    assert.deepEqual(errors, [], "no page errors");
  });

  await step("(3) 'pokaż katalog główny grafu' in the chat: the answer stays in the conversation and the navigator root opens; no model request", async () => {
    await page.getByTestId("shell-drawer-toggle").tap();
    await page.getByTestId("shell-new-chat").tap();
    await page.getByTestId("chat-input").waitFor();
    await sendText("pokaż katalog główny grafu");
    await sendText("tak");
    await nav().waitFor();
    await page.getByTestId("nav-rows").waitFor();
    assert.equal(await nav().getAttribute("data-focus"), "", "the root of the graph");
    const root = await api("POST", "/api/graph/navigate", { lang: "pl" });
    const rows = await page.locator('[data-testid="nav-rows"] > li .gnav-label').allInnerTexts();
    assert.deepEqual(rows, root.children.map(c => c.label), "the rows are the kernel's root rows");
    await shot("chat-root");
    await page.getByTestId("close-panel").tap();
    await nav().waitFor({ state: "detached" });
    const answer = await page.locator('[data-testid="message"][data-role="assistant"]').last().innerText();
    assert.match(answer, /Gotowe: Pokaż w grafie\./, "the answer is in the conversation");
    assert.match(answer, new RegExp(`Graf: ${root.counts.shown} z ${root.counts.total}`));
    assert.equal(await modelRequests(), 0, "no request reached the model");
  });

  await step("(3) 'pokaż import w grafie': the area Dane i korpusy with its counters sentence; the help of the navigator opens, is remembered in the settings", async () => {
    await sendText("pokaż import w grafie");
    await sendText("tak");
    await nav().waitFor();
    await until(async () => (await nav().getAttribute("data-focus")) === "area:data_corpora", "the navigator at the area");
    await page.getByTestId("node-card").waitFor();
    await page.getByTestId("card-loading").waitFor({ state: "detached" });
    const card = await page.getByTestId("node-card").innerText();
    assert.match(card, /Dane i korpusy/);
    const answer = await page.locator('[data-testid="message"][data-role="assistant"]').last().innerText();
    assert.match(answer, /Dane i korpusy — rozmowy: [1-9]\d*, wiadomości: [1-9]\d*, źródła: \d+, rozpoznane importy: \d+, duplikaty: \d+\./);
    assert.match(answer, /Ten obszar pokazuje dziś tylko liczniki/);
    REPORT.areaAnswer = answer.replace(/\s+/g, " ");

    // the help: under the path row, closed, opened by a tap, remembered through the settings (config), not the browser
    const help = page.getByTestId("nav-help");
    const head = help.locator(".disclosure-head");
    assert.equal(await head.getAttribute("aria-expanded"), "false", "closed by default");
    assert.equal(await head.innerText(), graph["help.show"]);
    assert.equal(await help.getByTestId("nav-help-line-1").isVisible(), false);
    const pathBox = await page.getByTestId("nav-path").boundingBox(), helpBox = await help.boundingBox();
    assert.ok(helpBox.y >= pathBox.y + pathBox.height - 1, "the help sits under the path row");
    await head.tap();
    await help.getByTestId("nav-help-line-1").waitFor({ state: "visible" });
    assert.equal(await head.getAttribute("aria-expanded"), "true");
    assert.equal(await help.getByTestId("nav-help-line-1").innerText(), graph["help.1"]);
    assert.equal(await help.getByTestId("nav-help-line-2").innerText(), graph["help.2"]);
    assert.equal(await help.getByTestId("nav-help-line-3").innerText(), graph["help.3"]);
    REPORT.help = [graph["help.1"], graph["help.2"], graph["help.3"]];
    await until(async () => (await api("GET", "/api/config")).navigator_help_open === true, "the setting saved in the config");
    assert.equal(await page.evaluate(() => { try { return Object.keys(localStorage).filter(k => /help/i.test(k)).length; } catch { return 0; } }), 0, "not in the browser's storage");
    assert.ok((await head.boundingBox()).height >= 44, "the help head is a 44 px target");
    await shot("navigator-help");
    await noSideScroll("navigator help");
    // reopened in the same session it starts as left; a reload reads the saved setting
    await page.getByTestId("close-panel").tap();
    await nav().waitFor({ state: "detached" });
    await page.reload();
    await page.getByTestId("chat-shell").waitFor();
    await page.getByTestId("shell-menu-toggle").tap();
    await page.getByTestId("shell-menu-graph").tap();
    await nav().waitFor();
    await until(async () => (await page.getByTestId("nav-help").locator(".disclosure-head").getAttribute("aria-expanded")) === "true", "the help open again after a reload (the setting)");
    await page.getByTestId("nav-help").locator(".disclosure-head").tap();
    await until(async () => (await api("GET", "/api/config")).navigator_help_open === false, "the setting saved closed");
    await page.getByTestId("close-panel").tap();
    await nav().waitFor({ state: "detached" });
    assert.equal(await modelRequests(), 0, "still no request to the model");
    assert.deepEqual(errors, [], "no page errors");
  });

  await step("(4)(5) the Workshop: the old map has its own words and its own help; no two controls share a label while opening different screens", async () => {
    await toWorkshop();
    if (!(await page.getByTestId("nav-graph").isVisible())) await page.getByTestId("toggle-nav").tap();
    assert.equal((await text("nav-graph")), shell.workshop_graph_old, "the Workshop button of the old map");
    assert.notEqual(shell.workshop_graph_old, shell.menu_this_conversation);
    // every visible label of the Workshop navigation is unique unless the buttons are the same screen
    const labels = await page.locator('[data-testid="app-nav"] button').evaluateAll(els => els.map(el => ({ id: el.dataset.testid ?? "", label: el.innerText.replace(/\s+/g, " ").trim() })));
    const seen = new Map();
    for (const l of labels.filter(l => l.label)) seen.set(l.label, [...(seen.get(l.label) ?? []), l.id]);
    assert.deepEqual([...seen].filter(([, ids]) => ids.length > 1), [], "no two Workshop buttons with the same words");
    await page.getByTestId("nav-graph").tap();
    await page.getByTestId("panel-graph").waitFor();
    assert.equal(await page.getByTestId("panel-graph").getAttribute("aria-label"), shell.workshop_graph_old, "the drawer carries the same words");
    const help = page.getByTestId("graph-help");
    await help.waitFor();
    const head = help.locator(".disclosure-head");
    assert.equal(await head.getAttribute("aria-expanded"), "false");
    await head.tap();
    for (const n of [1, 2, 3]) assert.equal(await help.getByTestId(`graph-help-line-${n}`).innerText(), graph[`help_old.${n}`]);
    await until(async () => (await api("GET", "/api/config")).graph_view_help_open === true, "the old map's setting saved");
    await shot("old-map-help");
    await page.getByTestId("close-panel").tap();
    await page.getByTestId("panel-graph").waitFor({ state: "detached" });
    await backToChat();
    // the menu entry still opens the navigator at the open conversation
    await page.getByTestId("shell-menu-toggle").tap();
    assert.equal((await text("shell-menu-this_conversation")), shell.menu_this_conversation);
    await page.getByTestId("shell-menu-this_conversation").tap();
    await nav().waitFor();
    await page.getByTestId("close-panel").tap();
    assert.equal(await modelRequests(), 0, "0 requests to the model");
    REPORT.modelRequests = await modelRequests();
    REPORT.workshopLabel = shell.workshop_graph_old;
    assert.deepEqual(errors, [], "no page errors");
  });

  await step("no screen shown has developer text: a raw key or an English sentence (m1-audit.mjs)", async () => {
    assert.deepEqual(AUDIT.map(a => `${a.shot}: ${a.why} | ${a.line} | ${a.where}`), [], "every line of the screens is in the words of the pack");
  });
} finally {
  await browser?.close();
  server.kill("SIGTERM"); mock.kill("SIGTERM");
}
console.log("[m1-import-truth] REPORT " + JSON.stringify(REPORT, null, 1));
if (failed) process.exit(1);
