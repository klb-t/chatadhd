#!/usr/bin/env node
// M1-W2b-1: the screen "Ty" (MeView.tsx) on a phone (390x844, touch) against the REAL loom-server (form mode, NO model). The mock model server
// (mock_llm_server.py) only proves the point: it must receive 0 requests. A synthetic person is seeded through the API (the interview started, the
// level chosen, two answers written with the confirm flow, one topic closed with "nigdy", two notes of memory - one without a recorded origin -, one
// unit with a conversation in it, a person and a topic as nodes the analysis found), then: ⋮ › Ty shows the sections (label and count are the
// kernel's); a section opened by data (not by label) lists its entries with the evidence chip (class in words) and the provenance line (the kernel's
// words); a "nigdy" slot shows no value; "Popraw" on a slot types a value, asks (Tak/Nie in the proposal block) and writes it; "Zapomnij" on a note
// asks and removes it and the count drops; preferences show who set them and the chain on a tap; an inferred entry is drawn with the preset opacity;
// "Poznaj mnie" opens the interview conversation; Back closes the layers in order. Every word is taken from presentation.graph_screen of the pack or
// from the kernel's own answer to the same question. With M1_SHOTS_DIR=<dir> the screenshots go there, otherwise to a fresh temp dir.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { auditPage } from "./m1-audit.mjs";
import { request, runChild } from "./harness-lifecycle.mjs";
import { stableShot, trackRequests } from "./stable-shot.mjs";
import { makeTmpDir } from "./tmp-dirs.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const SHOTS = process.env.M1_SHOTS_DIR || makeTmpDir("loom-m1-me-shots-", { evidence: true });
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[m1-me] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });
const viewPack = JSON.parse(readFileSync(path.join(LOOM, "data/profiles/conversation_view.pack"), "utf8"));
const words = viewPack.entries.find(r => r.key === "presentation.graph_screen").value.locales.pl;
const say = (id, params = {}) => { assert.ok(typeof words[id] === "string", `the pack has the word ${id}`); return words[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? ""); };
const INFERRED_PRESET = 0.72;   // me-view-state.ts INFERRED_OPACITY_PRESET (the preset; the setting me_inferred_opacity changes it)

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await request(url, { timeoutMs: 3000 })).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error(`timed out waiting for ${url}`); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }
/** Polls until the check returns a truthy value (a condition, never a fixed time). */
async function until(what, check, ms = 15000) { for (const end = Date.now() + ms; Date.now() < end;) { const v = await check(); if (v) return v; await new Promise(r => setTimeout(r, 120)); } throw new Error(`timed out waiting for ${what}`); }

const mockPort = await freePort(), port = await freePort();
const dataDir = makeTmpDir("loom-m1-me-");
// A note written before M1 has no recorded origin: the kernel says "pochodzenie niezapisane - starsza notatka" for it.
writeFileSync(path.join(dataDir, "memory.json"), JSON.stringify([{ id: "0e2e00000001", content: "Lubię herbatę bez cukru", parent_id: null, node_type: "text", active: true, depth: 0, weight: 1.0, tags: [], created: "2026-09-01T10:00:00Z", metadata: {} }], null, 2));
const mock = spawn("python3", [path.join(here, "mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", dataDir, "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
const AUDIT = [];
let browser, failed = false;
const step = async (name, fn) => { try { await fn(); console.log(`[m1-me] PASS ${name}`); } catch (e) { failed = true; console.error(`[m1-me] FAIL ${name}\n${e.stack}`); } };
const api = async (m, p, b) => { const r = await request(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status} ${r.ok ? "" : r.text}`); return r.json(); };
const mockCount = async () => (await request(`http://127.0.0.1:${mockPort}/__count`)).json();
const APP = { class: "application" };
/** An action on the application; a run that waits for the person's yes is answered with yes (the seed is the person's own act). */
async function runApp(action, args) {
  let run = await api("POST", "/api/actions/run", { action, args, element: APP });
  if (run.status === "awaiting_confirmation") run = await api("POST", "/api/actions/confirm", { run_id: run.id, approved: true, receipt_id: run.pending.receipt_id });
  assert.equal(run.status, "completed", `${action}: ${JSON.stringify(run.error)}`);
  return run;
}
const navigate = focus => api("POST", "/api/graph/navigate", { focus, lang: "pl" });

try {
  await waitUp(`${base}/api/healthz`);
  await api("PUT", "/api/config/base_url", { value: `http://127.0.0.1:${mockPort}` });
  await api("PUT", "/api/config/default_model", { value: "mock/e2e-model" });
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  await api("POST", "/api/secrets/api_key", { value: "sk-e2e-mock-key" });

  // ───────── the synthetic person ─────────
  const started = await api("POST", "/api/profile/interview", { op: "start", lang: "pl" });
  const interviewConv = started.result.data.conv_id;
  await api("POST", "/api/profile/interview", { op: "say", conv_id: interviewConv, lang: "pl", text: started.result.data.question.levels.find(l => l.id === "minimal").label });
  for (const [slot, value] of [["communication.style", "bezpośrednio"], ["communication.language", "polski"]]) {
    let run = await api("POST", "/api/profile/set", { slot, value });
    assert.equal(run.status, "awaiting_confirmation");
    run = await api("POST", "/api/actions/confirm", { run_id: run.id, approved: true, receipt_id: run.pending.receipt_id });
    assert.equal(run.status, "completed");
  }
  await api("POST", "/api/profile/interview", { op: "never", conv_id: interviewConv, lang: "pl", slot: "health.needs" });
  await runApp("memory.remember", { content: "Mam kota Filemona" });
  await runApp("units.define", { kind: "project", id: "remont", label: "Remont" });
  const talk = await api("POST", "/api/conversations", { title: "Rozmowa o remoncie" });
  await runApp("conversation.set_unit", { conv_id: talk.id, kind: "project", id: "remont" });
  const gardenTalk = await api("POST", "/api/conversations", { title: "Rozmowa o ogrodzie" });  // a second conversation: the picker of "Dodaj rozmowę do jednostki" lists it
  // an analysis found a person and a topic (nodes of the graph): the kernel shows them as conclusions
  const seeded = await runChild("python3", ["-c", "import sqlite3,sys; c=sqlite3.connect(sys.argv[1]); [c.execute('insert into nodes(id,kind,label,content,tags,metadata,created) values (?,?,?,?,?,?,?)',r) for r in [('n_e2eanna0001','person','Anna Testowa','','[]','{}','2026-10-22T10:00:00Z'),('n_e2etopic001','topic','kuchnia','','[]','{}','2026-10-22T10:00:00Z')]]; c.commit()", path.join(dataDir, "chatadhd.db")]);
  assert.equal(seeded.status, 0, `seeding nodes: ${seeded.stderr}`);

  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const context = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
  const page = await context.newPage();
  trackRequests(page);
  const errors = [];
  page.on("pageerror", e => errors.push(String(e)));
  const shot = async name => { await stableShot(page, path.join(SHOTS, `${name}.png`)); };
  const audit = async name => { for (const f of await auditPage(page)) AUDIT.push({ shot: name, ...f }); };
  const back = () => page.evaluate(() => window.__loomBack());
  const nav = () => page.getByTestId("graph-navigator");
  const fits = async () => assert.ok(await page.evaluate(() => document.documentElement.scrollWidth) <= 390, "no horizontal page scroll");
  const section = addr => page.locator(`[data-testid="me-section-${addr}"]`);
  const sectionHead = addr => section(addr).locator(".disclosure-head").first();
  const row = addr => page.locator(`[data-testid="me-row-${addr}"]`);
  const openSection = async addr => {
    if ((await sectionHead(addr).getAttribute("aria-expanded")) !== "true") await sectionHead(addr).tap();
    await page.locator(`[data-testid="me-section-rows-${addr}"], [data-testid="me-section-body-${addr}"] [data-testid="me-section-empty"]`).first().waitFor();
  };
  const sizesOk = async scope => {
    const small = await scope.locator("button").evaluateAll(els => els.map(el => { const r = el.getBoundingClientRect(); return { id: el.dataset.testid, w: Math.round(r.width), h: Math.round(r.height) }; }).filter(b => b.w > 0 && (b.w < 44 || b.h < 44)));
    assert.deepEqual(small, [], "every button is at least 44 x 44 px");
  };

  await page.goto(base + "/");
  await page.getByTestId("chat-shell").waitFor();
  const requestsBefore = await mockCount();
  let ty, parts, byKind;   // the kernel's answer for Ty and the entries of each part, read from the kernel (never typed here)

  await step("1 ⋮ › Ty opens the page of the person: the sections are the kernel's parts, each with the kernel's label and count; the generic rows and the map wait closed under it", async () => {
    ty = await navigate("party:self");
    parts = ty.children.filter(c => c.kind === "person_subgraph");
    assert.ok(parts.length >= 6, `the person has parts (${parts.length})`);
    byKind = {};
    for (const p of parts) byKind[p.address] = await navigate(p.address);
    await page.getByTestId("shell-menu-toggle").tap();
    await page.getByTestId("shell-menu-me").tap();
    await page.getByTestId("me-view").waitFor();
    await page.locator('[data-testid="graph-navigator"][data-focus="party:self"]').waitFor();
    assert.equal(await page.getByTestId("me-view").getAttribute("data-parts"), String(parts.length));
    for (const p of parts) {
      const head = (await sectionHead(p.address).innerText()).replace(/\s+/g, " ").trim();
      assert.equal(head, say("me.section.head", { label: p.label, count: String(p.count) }), `${p.address}: label and count are the kernel's`);
      assert.equal(p.count, byKind[p.address].counts.total, `${p.address}: the count of the part is the number of its entries`);
    }
    assert.deepEqual(await page.locator('[data-testid^="me-section-subgraph:"]:not([data-testid*="body"]):not([data-testid*="rows"])').evaluateAll(els => els.map(e => e.dataset.testid.replace("me-section-", ""))), parts.map(p => p.address), "the kernel's order");
    assert.equal(await page.getByTestId("me-title").innerText(), ty.focus.label);
    assert.equal(await page.getByTestId("me-lead").innerText(), ty.focus.why, "the sentence of the kernel's card");
    assert.equal(await page.getByTestId("me-actions-interview").getByTestId("me-action").innerText(), say("me.interview.start"));
    // the generic rows and the map are there, closed, under the page
    assert.equal(await page.getByTestId("nav-list-disclosure").locator(".disclosure-head").first().innerText(), say("me.rows.show"));
    assert.equal(await page.getByTestId("nav-list-disclosure").locator(".disclosure-head").first().getAttribute("aria-expanded"), "false");
    assert.equal(await page.getByTestId("nav-map-disclosure").locator(".disclosure-head").first().innerText(), say("me.map.show"));
    assert.equal(await page.getByTestId("nav-focus-why").count(), 0, "the sentence is said once (in the header)");
    await fits(); await sizesOk(page.getByTestId("me-view"));
    await audit("01-me-sections"); await shot("01-me-sections");
    console.log("[m1-me] sections: " + (await page.locator('[data-testid^="me-section-subgraph:"] > .disclosure-head').allInnerTexts()).map(s => s.replace(/\s+/g, " ").trim()).join(" | "));
  });

  const holderOf = pred => parts.find(p => byKind[p.address].children.some(pred));
  await step("2 a section found by the kernel's data opens and lists every entry with the evidence chip (class in words) and the provenance line (the kernel's words); the state is remembered through the settings", async () => {
    const holder = holderOf(c => c.kind === "profile_slot" && c.status === "known");
    assert.ok(holder, "some part holds a known interview slot");
    await openSection(holder.address);
    const kernel = byKind[holder.address];
    assert.deepEqual(await page.locator(`[data-testid="me-section-rows-${holder.address}"] > li`).evaluateAll(els => els.map(e => e.dataset.address)), kernel.children.map(c => c.address), "the entries, in the kernel's order");
    for (const c of kernel.children) {
      const r = row(c.address);
      assert.equal(await r.getByTestId("me-row-label").innerText(), c.label);
      if (c.evidence === null) {
        // an entry that holds no value has no evidence: a state chip in the pack's word (bez odpowiedzi) instead of an evidence chip, and no provenance line
        assert.equal(await r.getByTestId("me-row-chip").count(), 0, `${c.address}: no evidence chip on an empty slot`);
        assert.equal(await r.getByTestId("me-row-provenance").count(), 0, `${c.address}: no provenance line on an empty slot`);
        assert.equal(await r.getByTestId("me-row-state-chip").innerText(), say(`me.state.${c.state}`), `${c.address}: the state chip is the state in words`);
        console.log(`[m1-me] empty slot ${c.address}: state=${c.state}, chip="${await r.getByTestId("me-row-state-chip").innerText()}", no evidence chip`);
      } else {
        assert.equal(await r.getByTestId("me-row-state-chip").count(), 0, `${c.address}: a slot with evidence has no state chip`);
        assert.equal(await r.getByTestId("me-row-chip").innerText(), say(`me.class.${c.evidence.class}`), `${c.address}: the chip is the class in words`);
        const prov = (await r.getByTestId("me-row-provenance").innerText()).replace(/\s+/g, " ");
        assert.ok(prov.startsWith(say("me.provenance.line", { human: c.evidence.human }).slice(0, -1) ) || prov.includes(c.evidence.human), `${c.address}: the provenance line has the kernel's words (${prov})`);
        assert.equal(await r.getByTestId("me-row-provenance").getAttribute("data-method"), c.evidence.provenance.method);
        if (c.kind === "profile_slot" && c.status === "known") console.log(`[m1-me] own answer ${c.address}: class=${c.evidence.class}, chip="${await r.getByTestId("me-row-chip").innerText()}"`);
      }
      assert.ok(await r.locator('[data-testid="me-action"]').count() === (c.actions ?? []).filter(a => a.implementation === "implemented" || !a.implementation).length, `${c.address}: the buttons are the node's actions[]`);
    }
    const known = kernel.children.find(c => c.kind === "profile_slot" && c.status === "known");
    assert.equal(known.evidence.class, "said", "a person's own interview answer is class said, not observed");
    assert.equal(await row(known.address).getByTestId("me-row-chip").innerText(), "powiedziane", "and its chip says so");
    assert.equal(await row(known.address).getByTestId("me-row-value").innerText(), known.value_human);
    assert.equal(await row(known.address).getByTestId("me-row-state").count(), 0);
    await fits(); await sizesOk(section(holder.address));
    await sectionHead(holder.address).evaluate(el => el.scrollIntoView({ block: "start" }));   // the screenshot shows the head and the first entries, not the bottom bar over them
    await audit("02-me-identity-open"); await shot("02-me-identity-open");
    // remembered: the setting holds the address (PUT /api/config/<key>), a reload brings the page back with the section open
    await until("the setting me_open_sections", async () => (await api("GET", "/api/config")).me_open_sections?.includes(holder.address));
    await page.reload(); await page.getByTestId("chat-shell").waitFor();
    await page.getByTestId("shell-menu-toggle").tap(); await page.getByTestId("shell-menu-me").tap(); await page.getByTestId("me-view").waitFor();
    await sectionHead(holder.address).waitFor();
    assert.equal(await sectionHead(holder.address).getAttribute("aria-expanded"), "true", "the section the person left open is open");
    await page.locator(`[data-testid="me-section-rows-${holder.address}"]`).waitFor();
  });

  await step("3 a slot the person closed ('nigdy') shows no value, only the kernel's word for its state", async () => {
    const holder = holderOf(c => c.kind === "profile_slot" && c.status === "never");
    assert.ok(holder, "some part holds a closed slot");
    await openSection(holder.address);
    const never = byKind[holder.address].children.find(c => c.kind === "profile_slot" && c.status === "never");
    assert.equal(never.value, null, "the kernel gives no value for it");
    const r = row(never.address);
    await r.scrollIntoViewIfNeeded();
    assert.equal(await r.getByTestId("me-row-value").count(), 0, "no value on the screen");
    assert.equal(await r.getByTestId("me-row-state").innerText(), never.value_human);
    assert.equal(await r.getAttribute("data-status"), "never");
    await audit("03-me-never-slot"); await shot("03-me-never-slot");
  });

  /** Popraw on the first known slot: types a value, the rules ask, the answer is given in the proposal block. */
  let corrected;
  const SEEDED_SLOT = "party:self/communication.style";   // written in the seed above ("bezpośrednio"): a free-text answer, so any text is valid
  const popraw = async (value, answer) => {
    const holder = holderOf(c => c.address === SEEDED_SLOT && c.status === "known");
    assert.ok(holder, "the part that holds the seeded answer");
    await openSection(holder.address);
    const slot = byKind[holder.address].children.find(c => c.address === SEEDED_SLOT);
    const r = row(slot.address);
    await r.scrollIntoViewIfNeeded();
    await r.locator('[data-action="profile.set"]').tap();
    await page.getByTestId("me-input-form").waitFor();
    assert.equal(await page.getByTestId("me-input-value").inputValue(), slot.value_human, "the field starts with the present value");
    await page.getByTestId("me-input-value").fill(value);
    await page.getByTestId("me-input-next").tap();
    await page.getByTestId("me-gate").waitFor();
    return { holder, slot, r, ask: (await page.getByTestId("me-proposal-ask").innerText()).replace(/\s+/g, " ") };
  };
  await step("4 Popraw → a field for the value → the question of the rules in the proposal block (Tak/Nie in plain words); Nie changes nothing", async () => {
    const TRIED = "po imieniu";   // typed, asked about, then refused: nothing is written
    const { slot, holder, ask, r } = await popraw(TRIED, "no");
    const title = slot.actions.find(a => a.id === "profile.set").label;
    assert.ok(ask.includes(title) && ask.includes(slot.label) && ask.includes(TRIED), `the question names the action, the entry and the value: ${ask}`);
    const block = page.getByTestId("me-proposal");
    const text = (await block.innerText()).replace(/\s+/g, " ").trim();
    assert.equal(text, `${say("me.confirm.yes")} ${say("me.confirm.no")}`, "the block holds nothing but the two answers");
    assert.ok(!/[=_]/.test(text), "no kernel field in the block");
    assert.ok((await page.getByTestId("me-proposal-reason").count()) >= 1, "the kernel's reason for asking is said");
    await sizesOk(block);
    await block.scrollIntoViewIfNeeded();
    await audit("04-popraw-proposal"); await shot("04-popraw-proposal");
    console.log("[m1-me] Popraw proposal: " + ask + " | reason: " + (await page.getByTestId("me-proposal-reason").first().innerText()));
    await page.getByTestId("me-proposal-reject").tap();
    await page.getByTestId("me-gate").waitFor({ state: "detached" });
    assert.equal(await r.getByTestId("me-action-note").innerText(), say("me.confirm.declined"));
    const after = (await navigate(holder.address)).children.find(c => c.address === slot.address);
    assert.equal(after.value, slot.value, "Nie: nothing changed");
    assert.equal(await r.getByTestId("me-row-value").innerText(), slot.value_human);
    corrected = slot;
  });
  await step("5 Popraw → Tak: the value changes in the kernel and on the screen, the provenance line is the kernel's, and the page says it is done", async () => {
    const { holder, slot, r } = await popraw("luźno", "yes");
    await page.getByTestId("me-proposal-confirm").tap();
    await page.getByTestId("me-gate").waitFor({ state: "detached" });
    const after = await until("the new value in the kernel", async () => { const c = (await navigate(holder.address)).children.find(x => x.address === slot.address); return c.value_human === "luźno" ? c : null; });
    await until("the new value on the screen", async () => (await row(slot.address).getByTestId("me-row-value").innerText()) === after.value_human);
    const prov = (await row(slot.address).getByTestId("me-row-provenance").innerText()).replace(/\s+/g, " ");
    assert.ok(prov.includes(after.evidence.human), `the provenance line says what the kernel says now: ${prov}`);
    assert.equal(after.evidence.provenance.method, "corrected", "the kernel recorded that the person corrected it");
    assert.ok(after.evidence.provenance.time, "with the time");
    assert.ok(prov.includes(say("evidence.method.corrected")), `and the line says so: ${prov}`);
    assert.equal(after.evidence.class, "said");
    const title = slot.actions.find(a => a.id === "profile.set").label;
    assert.equal(await page.getByTestId("me-done").innerText(), say("me.confirm.done", { action: title }));
    console.log(`[m1-me] after Popraw: ${slot.label} = ${after.value_human}; provenance line: ${prov}; kernel evidence: ${JSON.stringify(after.evidence.provenance)}`);
    await r.scrollIntoViewIfNeeded();
    await audit("05-popraw-done"); await shot("05-popraw-done");
  });

  await step("6 Zapomnij on a note → the question → Tak: the note is gone and the count of 'Co pamiętam' drops; a note without an origin says so", async () => {
    const holder = holderOf(c => c.kind === "memory_note");
    assert.ok(holder, "some part holds notes");
    const before = byKind[holder.address];
    assert.equal(before.children.length, 2, "the two seeded notes");
    await openSection(holder.address);
    const legacy = before.children.find(c => c.label.includes("herbatę"));
    assert.ok(/niezapisane/.test(legacy.evidence.human) && !/M1/.test(legacy.evidence.human), `the kernel says the origin was not recorded, without a project name: ${legacy.evidence.human}`);
    const legacyLine = row(legacy.address).getByTestId("me-row-provenance");
    assert.ok((await legacyLine.innerText()).replace(/\s+/g, " ").startsWith(say("me.provenance.line", { human: legacy.evidence.human }).replace(/\.?$/, "")), "the line says the kernel's words: the origin is unknown");
    assert.equal(await legacyLine.getAttribute("data-time"), legacy.evidence.provenance.time, "and when the note was written");
    const note = before.children.find(c => c.label.includes("Filemona"));
    const r = row(note.address);
    await r.scrollIntoViewIfNeeded();
    await sectionHead(holder.address).evaluate(el => el.scrollIntoView({ block: "start" }));
    await audit("06-notes"); await shot("06-notes");
    await r.locator('[data-action="memory.forget"]').tap();
    await page.getByTestId("me-gate").waitFor();
    const ask = (await page.getByTestId("me-proposal-ask").innerText()).replace(/\s+/g, " ");
    assert.ok(ask.includes(note.actions[0].label) && ask.includes(note.label), `the question names the action and the note: ${ask}`);
    const reasons = await page.getByTestId("me-proposal-reason").allInnerTexts();
    assert.ok(reasons.length >= 1 && reasons.every(x => x.trim() !== ""), "the kernel's reason is said");
    await page.getByTestId("me-gate").scrollIntoViewIfNeeded();
    await audit("07-zapomnij-proposal"); await shot("07-zapomnij-proposal");
    console.log("[m1-me] Zapomnij proposal: " + ask + " | reason: " + reasons.join(" / "));
    await page.getByTestId("me-proposal-confirm").tap();
    await row(note.address).waitFor({ state: "detached" });
    const after = await navigate(holder.address);
    assert.deepEqual(after.children.map(c => c.address), [legacy.address], "the kernel forgot the note");
    const head = await until("the count in the header", async () => { const h = (await sectionHead(holder.address).innerText()).replace(/\s+/g, " ").trim(); return h === say("me.section.head", { label: holder.label, count: "1" }) ? h : null; });
    assert.equal(holder.count, 2); console.log(`[m1-me] header of the memory part: ${holder.count} → ${head}`);
    await sectionHead(holder.address).evaluate(el => el.scrollIntoView({ block: "start" }));
    await audit("08-zapomnij-done"); await shot("08-zapomnij-done");
    // Nothing vanishes silently: the note is on the folded list "Zapomniane" with the record of forgetting; the model's memory no longer has it.
    const forgottenAddr = "subgraph:self/forgotten";
    await section(forgottenAddr).waitFor();
    assert.equal(await sectionHead(forgottenAddr).getAttribute("aria-expanded"), "false", "the list of forgotten entries is folded by default");
    assert.ok((await sectionHead(forgottenAddr).innerText()).includes("Zapomniane"), "its head is the pack's word");
    await openSection(forgottenAddr);
    const gone = await navigate(forgottenAddr);
    assert.equal(gone.children.length, 1);
    assert.equal(gone.children[0].status, "forgotten");
    assert.ok(gone.children[0].label.includes("Filemona"), "the words are kept by the preset");
    await row(gone.children[0].address).waitFor();
    await audit("08b-zapomniane"); await shot("08b-zapomniane");
    // The details of a forgotten row say when and by whom, in words; the way back (Przywróć) is offered because the words were kept.
    const frow = row(gone.children[0].address);
    await page.getByTestId(`me-details-${gone.children[0].address}`).locator(".disclosure-head").first().tap();
    const detail = await until("the details of the forgotten entry", async () => { const t = (await page.getByTestId(`me-details-${gone.children[0].address}`).getByTestId("me-detail-line").allInnerTexts()).join(" | "); return t.includes("Zapomniane") ? t : null; });
    assert.ok(/przez Ciebie/.test(detail) && /zachowana/.test(detail) && !/user|true|kept/.test(detail), `time and who in words, no raw ids: ${detail}`);
    assert.equal(await frow.locator('[data-action="memory.restore"]').innerText(), say("me.action.memory.restore"));
    await audit("08c-zapomniane-szczegoly"); await shot("08c-zapomniane-szczegoly");
  });

  await step("6b Przywróć on a forgotten entry → the question → Tak: it is active again, back in 'Co pamiętam', gone from 'Zapomniane'; the record of forgetting is kept in its history", async () => {
    const holder = holderOf(c => c.kind === "memory_note");
    const forgottenAddr = "subgraph:self/forgotten";
    const gone = await navigate(forgottenAddr);
    assert.equal(gone.children.length, 1);
    const entry = gone.children[0];
    await row(entry.address).locator('[data-action="memory.restore"]').tap();
    await page.getByTestId("me-gate").waitFor();
    const ask = (await page.getByTestId("me-proposal-ask").innerText()).replace(/\s+/g, " ");
    assert.ok(ask.includes(say("me.action.memory.restore")), `the question names the action: ${ask}`);
    const reasons = (await page.getByTestId("me-proposal-reason").allInnerTexts()).join(" ");
    assert.ok(!/[{}]/.test(reasons), `no token left in the reason: ${reasons}`);
    await page.getByTestId("me-proposal-confirm").tap();
    await until("the kernel brought it back", async () => (await navigate(forgottenAddr)).children.length === 0);
    const back = await navigate(holder.address);
    assert.ok(back.children.some(c => c.label.includes("Filemona") && c.status === "active"), "back in the notes, active");
    assert.equal(back.children.length, 2);
    await until("the screen shows it", async () => (await section(holder.address).innerText()).includes("Filemona") || (await row(entry.address).count()) === 1);
    await audit("08d-przywrocone"); await shot("08d-przywrocone");
  });

  await step("7 a preference says who set it and the chain on a tap; an inferred entry has the preset look and says it is a conclusion", async () => {
    const prefPart = holderOf(c => c.kind === "preference");
    await openSection(prefPart.address);
    const pref = byKind[prefPart.address].children.find(c => c.kind === "preference" && c.provenance.chip?.label);
    const r = row(pref.address);
    await r.scrollIntoViewIfNeeded();
    assert.equal(await r.getByTestId("me-row-setby").innerText(), say("me.chip.set_by", { who: pref.provenance.chip.label }));
    await page.getByTestId(`me-details-${pref.address}`).locator(".disclosure-head").first().tap();
    await page.getByTestId("me-chain").waitFor();
    const levels = (await api("POST", "/api/graph/card", { address: pref.address, lang: "pl" })).attrs.resolved.inheritance;
    assert.deepEqual(await page.locator('[data-testid="me-chain"] > li').evaluateAll(els => els.map(e => e.dataset.testid.replace("me-chain-", ""))), levels.map(l => l.level), "the whole chain, the kernel's levels");
    await audit("09-preference-chain"); await shot("09-preference-chain");
    // inferred
    const infPart = holderOf(c => c.evidence?.class === "inferred");
    assert.ok(infPart, "some part holds a conclusion");
    await openSection(infPart.address);
    const inf = byKind[infPart.address].children.find(c => c.evidence.class === "inferred");
    const ir = row(inf.address);
    await ir.scrollIntoViewIfNeeded();
    assert.equal(await ir.getAttribute("data-inferred"), "true");
    assert.equal(await ir.getByTestId("me-row-chip").innerText(), say("me.class.inferred"));
    assert.equal(await ir.getByTestId("me-row-inferred-note").innerText(), say("me.inferred.note"));
    const opacity = async loc => Number(await loc.evaluate(el => getComputedStyle(el).opacity));
    assert.equal(await opacity(ir.locator(".me-row-top")), INFERRED_PRESET, "the preset look");
    const plain = page.locator('[data-testid^="me-row-"][data-inferred]:not([data-inferred="true"]), .me-row:not([data-inferred="true"])').first();
    assert.equal(await opacity(plain.locator(".me-row-top")), 1, "an entry that is not a conclusion is drawn in full");
    await audit("10-inferred"); await shot("10-inferred");
  });

  await step("7b Odrzuć / Potwierdź on a conclusion: the question → Tak; the entry stays as rejected (no evidence, the kernel's state word) and then becomes the person's word with the time; the buttons follow the state", async () => {
    const holder = holderOf(c => c.address === "interest:kuchnia");
    assert.ok(holder, "some part holds the topic");
    await openSection(holder.address);
    const addr = "interest:kuchnia";
    const r = row(addr);
    await r.scrollIntoViewIfNeeded();
    assert.ok(await r.locator('[data-action="inference.confirm"]').count() === 1 && await r.locator('[data-action="inference.reject"]').count() === 1, "both buttons on a conclusion");
    assert.equal(await r.locator('[data-action="inference.reject"]').innerText(), say("me.action.inference.reject"));
    await r.locator('[data-action="inference.reject"]').tap();
    await page.getByTestId("me-gate").waitFor();
    assert.equal(await page.getByTestId("me-input-form").count(), 0, "the node fills its only argument: the form is not shown");
    await audit("12-reject-proposal"); await shot("12-reject-proposal");
    await page.getByTestId("me-proposal-confirm").tap();
    const rejected = await until("the kernel's rejected entry", async () => (await navigate(holder.address)).children.find(c => c.address === addr && c.state === "rejected"));
    assert.equal(rejected.evidence, null);
    assert.equal((await api("POST", "/api/graph/card", { address: addr, lang: "pl" })).attrs.review.decision, "rejected");
    await until("the screen says it", async () => (await row(addr).innerText()).includes(say("me.state.rejected")));
    assert.equal(await row(addr).locator('[data-action="inference.reject"]').count(), 0, "a rejected entry offers only Potwierdź");
    await audit("13-rejected"); await shot("13-rejected");
    await row(addr).locator('[data-action="inference.confirm"]').tap();
    await page.getByTestId("me-gate").waitFor();
    await page.getByTestId("me-proposal-confirm").tap();
    const said = await until("the confirmed entry", async () => (await navigate(holder.address)).children.find(c => c.address === addr && c.evidence?.class === "said"));
    assert.equal(said.evidence.provenance.method, "user_confirmed"); assert.ok(said.evidence.provenance.time);
    await until("the chip says it", async () => (await row(addr).getByTestId("me-row-chip").innerText()) === say("me.class.said"));
    assert.notEqual(await row(addr).getAttribute("data-inferred"), "true", "no longer drawn as a conclusion");
    await audit("14-confirmed"); await shot("14-confirmed");
  });

  await step("7c Dodaj rozmowę do jednostki: a picker of conversation titles (no ids), the question names the title; Tak puts the conversation in the unit", async () => {
    const holder = holderOf(c => c.address.startsWith("unit:"));
    assert.ok(holder, "some part holds the unit");
    await openSection(holder.address);
    const unit = (await navigate(holder.address)).children.find(c => c.address.startsWith("unit:"));
    const other = gardenTalk;
    const r = row(unit.address);
    await r.scrollIntoViewIfNeeded();
    await r.locator('[data-action="conversation.set_unit"]').tap();
    const select = page.getByTestId("me-input-conv_id");
    await select.waitFor();
    assert.equal(await select.evaluate(el => el.tagName), "SELECT", "a picker, not a text field");
    const options = await select.locator("option").evaluateAll(os => os.map(o => ({ value: o.value, text: o.textContent })));
    assert.ok(options.some(o => o.text === "Rozmowa o ogrodzie" && o.value === other.id), `the title is shown, the id is the value: ${JSON.stringify(options)} vs ${other.id}`);
    assert.ok(options.every(o => o.text !== o.value || o.value === ""), "no raw id on screen");
    await audit("15-unit-picker"); await shot("15-unit-picker");
    await select.selectOption({ label: "Rozmowa o ogrodzie" });
    await page.getByTestId("me-input-next").tap();
    await page.getByTestId("me-gate").waitFor();
    assert.ok((await page.getByTestId("me-proposal-ask").innerText()).includes("Rozmowa o ogrodzie"), "the question names the conversation by its title");
    await page.getByTestId("me-proposal-confirm").tap();
    const conv = await until("the conversation in the unit", async () => { const l = await api("GET", "/api/conversations?limit=50"); return (Array.isArray(l) ? l : l.conversations).find(c => c.id === other.id && c.metadata?.units?.project === "remont"); });
    assert.equal(conv.metadata.units.project, "remont");
  });

  await step("7d Popraw na poziomie (preferencja): poziom i rozmowa z listy słów (bez identyfikatorów), pytanie o zgodę; Tak ustawia wartość tylko dla tej rozmowy, a wiersz mówi \"ustawił: Ta rozmowa\"", async () => {
    const prefPart = holderOf(c => c.address === "preference:length");
    assert.ok(prefPart, "some part holds the preference 'length'");
    await openSection(prefPart.address);
    const pref = byKind[prefPart.address].children.find(c => c.address === "preference:length");
    const r = row(pref.address);
    await r.scrollIntoViewIfNeeded();
    const before = await api("POST", "/api/graph/card", { address: pref.address, lang: "pl" });
    await r.locator('[data-action="rules.set"]').tap();
    await page.getByTestId("me-input-form").waitFor();
    const level = page.getByTestId("me-input-scope"), instance = page.getByTestId("me-input-scope_id");
    for (const sel of [level, instance]) assert.equal(await sel.evaluate(el => el.tagName), "SELECT", "a picker, not a text field");
    const levels = await level.locator("option").evaluateAll(os => os.map(o => ({ value: o.value, text: o.textContent })));
    assert.ok(levels.some(o => o.value === "person" && o.text === say("chip.person")), `Ty is among the levels: ${JSON.stringify(levels)}`);
    assert.ok(levels.some(o => o.value === "conversation" && o.text === say("chip.conversation")), `the conversation level in words: ${JSON.stringify(levels)}`);
    assert.ok(!levels.some(o => o.value === "installation"), "the preset level is not a level a person writes");
    const instances = await instance.locator("option").evaluateAll(os => os.map(o => ({ value: o.value, text: o.textContent })));
    assert.ok(instances.some(o => o.text === "Rozmowa o remoncie" && o.value === talk.id), `the title is shown, the id is the value: ${JSON.stringify(instances)}`);
    assert.ok(instances.every(o => o.text !== o.value || o.value === ""), "no raw id on screen");
    await level.selectOption({ value: "conversation" });
    await instance.selectOption({ label: "Rozmowa o remoncie" });
    await page.getByTestId("me-input-value").fill("long");
    await audit("16-level-picker"); await shot("16-level-picker");
    await page.getByTestId("me-input-next").tap();
    await page.getByTestId("me-gate").waitFor();
    const ask = (await page.getByTestId("me-proposal-ask").innerText()).replace(/\s+/g, " ");
    assert.ok(ask.includes("Rozmowa o remoncie"), `the question names the conversation by its title: ${ask}`);
    assert.equal((await api("POST", "/api/graph/card", { address: pref.address, lang: "pl", conv_id: talk.id })).value, before.value, "nothing is written before the yes");
    await page.getByTestId("me-proposal-confirm").tap();
    await page.getByTestId("me-gate").waitFor({ state: "detached" });
    const after = await until("the conversation's value in the kernel", async () => { const c = await api("POST", "/api/graph/card", { address: pref.address, lang: "pl", conv_id: talk.id }); return c.value === "long" ? c : null; });
    assert.equal(after.provenance.scope, "conversation");
    assert.equal((await api("POST", "/api/graph/card", { address: pref.address, lang: "pl" })).value, before.value, "the person's level is untouched");
    await audit("17-level-done"); await shot("17-level-done");
    // the same value on the screen: with that conversation open in the chat, the row of the preference says in words which level set it
    for (let i = 0; i < 4 && (await nav().count()) > 0; i++) { await back(); await page.waitForTimeout(150); }   // Back closes the layers of the panel
    await nav().waitFor({ state: "detached" });
    await page.getByTestId("shell-drawer-toggle").tap();
    await page.locator('[data-testid="shell-conv"]', { hasText: "Rozmowa o remoncie" }).first().tap();
    await page.getByTestId("shell-menu-toggle").tap(); await page.getByTestId("shell-menu-me").tap(); await page.getByTestId("me-view").waitFor();
    await openSection(prefPart.address);
    const line = await until("the row names the level that set the value", async () => {
      const el = row(pref.address).getByTestId("me-row-setby");
      return (await el.count()) > 0 ? (await el.first().innerText()).replace(/\s+/g, " ").trim() : null;
    });
    assert.equal(line, say("me.chip.set_by", { who: "Ta rozmowa" }), "the row says who set the value: this conversation");
    console.log(`[m1-me] Popraw na poziomie: ${pref.label} = ${after.value_human} (${after.provenance.chip.label})`);
  });

  await step("8 the entry point 'Poznaj mnie' opens the conversation of the interview (no model) and closes the panel", async () => {
    await page.evaluate(() => window.scrollTo(0, 0)); await page.getByTestId("me-view").evaluate(el => el.scrollIntoView({ block: "start" }));
    await page.getByTestId("me-actions-interview").getByTestId("me-action").tap();
    await nav().waitFor({ state: "detached" });
    await page.getByTestId("interview-info").waitFor();
    assert.equal(await page.getByTestId("interview-info-line").innerText(), (viewPack.entries.find(r => r.key === "presentation.interview").value.locales.pl)["info.line"]);
    const conv = (await api("GET", "/api/conversations?limit=10"));
    assert.ok((Array.isArray(conv) ? conv : conv.conversations).some(c => c.metadata?.flow === "profile.interview"), "the conversation of the interview exists in the kernel");
    await audit("11-interview-opened"); await shot("11-interview-opened");
  });

  await step("9 Back closes the layers in order: a part drilled into from 'Wszystkie wiersze', then Ty, then the panel", async () => {
    await page.getByTestId("shell-menu-toggle").tap(); await page.getByTestId("shell-menu-me").tap(); await page.getByTestId("me-view").waitFor();
    await page.getByTestId("nav-list-disclosure").locator(".disclosure-head").first().tap();
    const part = parts[0];
    await page.getByTestId(`nav-row-${part.address}`).waitFor();
    await audit("12-all-rows");
    await page.getByTestId(`nav-row-${part.address}`).tap();
    await page.locator(`[data-testid="graph-navigator"][data-focus="${part.address}"]`).waitFor();
    await page.getByTestId("me-view").waitFor({ state: "detached" });   // inside a part the page of the person is not drawn: the generic rows are
    assert.equal(await back(), true);
    await page.locator('[data-testid="graph-navigator"][data-focus="party:self"]').waitFor();
    await page.getByTestId("me-view").waitFor();
    assert.equal(await back(), true);
    await nav().waitFor({ state: "detached" });
    assert.equal(await back(), false);
  });

  await step("9b Poznaj mnie od nowa: the question says the answers stay in the history; Nie changes nothing; Tak puts the answers back to unknown (the 'never' field stays), keeps the history and opens the interview from its first question", async () => {
    const show = async () => (await api("POST", "/api/actions/run", { action: "profile.show", args: {}, element: APP })).result.data.slots;
    const stateOf = async slot => (await show()).find(r => r.slot === slot)?.state;
    assert.equal(await stateOf("communication.style"), "known");
    await page.getByTestId("shell-menu-toggle").tap(); await page.getByTestId("shell-menu-me").tap(); await page.getByTestId("me-view").waitFor();
    const holder = parts.find(p => p.actions?.some(a => a.id === "profile.restart"));
    assert.ok(holder, "a part of the kernel offers the restart: " + JSON.stringify(parts.map(p => [p.address, (p.actions ?? []).map(a => a.id)])));
    await openSection(holder.address);
    const button = page.locator('[data-action="profile.restart"]').first();
    await button.scrollIntoViewIfNeeded();
    assert.equal(await button.innerText(), say("me.action.profile.restart"));
    await button.tap();
    await page.getByTestId("me-gate").waitFor();
    assert.equal(await page.getByTestId("me-input-form").count(), 0, "nothing to type: the preset says what is reset");
    assert.ok((await page.getByTestId("me-proposal-ask").innerText()).length > 0);
    assert.ok((await page.getByTestId("me-gate").innerText()).includes("historii"), "the question says the earlier answers stay in the history");
    await audit("16-restart-proposal"); await shot("16-restart-proposal");
    await page.getByTestId("me-proposal-reject").tap();
    assert.equal(await stateOf("communication.style"), "known", "Nie changes nothing");
    await button.tap();
    await page.getByTestId("me-gate").waitFor();
    await page.getByTestId("me-proposal-confirm").tap();
    await nav().waitFor({ state: "detached" });
    await page.getByTestId("interview-info").waitFor();
    assert.equal(await stateOf("communication.style"), "unknown");
    assert.equal(await stateOf("health.needs"), "never", "what the person closed stays closed");
    await audit("17-restarted"); await shot("17-restarted");
  });

  await step("10 the model server received 0 requests; no page errors; the text audit found no developer text on any screenshot", async () => {
    const after = await mockCount();
    assert.equal(after.chat, 0, "no chat request ever reached the model");
    assert.equal(after.requests, requestsBefore.requests, "no request at all reached the model server");
    console.log(`[m1-me] requests to the mock model: ${after.requests} (chat completions: ${after.chat})`);
    assert.deepEqual(errors, [], "no page errors");
    assert.deepEqual(AUDIT, [], "lines that look like developer text: " + JSON.stringify(AUDIT, null, 1));
  });
} finally {
  await browser?.close();
  server.kill();
  mock.kill();
}
if (failed) process.exit(1);
