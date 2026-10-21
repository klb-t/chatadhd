#!/usr/bin/env node
// M1-W0 (7), the SETTINGS half (package P7): Ustawienia po ludzku (P5) and the role card (P6) on a phone (390x844, touch) against the
// real loom-server, no model, no network. Everything asserted is compared with what the kernel answers (POST /api/settings/resolved,
// POST /api/roles/resolved), with the pack (loom/data/runtime/settings_fields.pack: the counts of the groups must add up to the fields
// the pack declares — no literal) and with the catalogue words (presentation.settings_human / presentation.roles). Checks: the ⋮ menu
// has the eight entries of layouts.json and no Pamięć / Wiedza; menu ⋮ › Ustawienia opens the eight groups in the order of the data
// with "N pól · M Twoje"; no page scroll wider than 390; every tappable >= 44 px (the former settings screen under Wszystkie pola is
// untouched and excluded); Rozmowa › Temperatura: label, value in words, the kernel's provenance chip, details with the flows that
// read it; a planned field shows its reason; Pokaż tylko zmienione keeps only the fields that are yours; Modele › Role: the table of
// roles.resolved as returned, the card of Analiza w tle with the chain word for word and every flow that reads its key; Silnik ›
// Wszystkie pola = the former settings screen; Back closes the layers in order (role card → group → panel) through window.__loomBack.
// With M1_SHOTS_DIR=<dir> the screenshots go there (the report docs/reports/M1_CHECKPOINT_0/), otherwise to e2e/screenshots.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { existsSync, mkdirSync, mkdtempSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = process.env.LOOM_SERVER_BIN || path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
const SHOTS = process.env.M1_SHOTS_DIR || path.join(here, "screenshots");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
if (!existsSync(SERVER_BIN) || !existsSync(path.join(DIST, "index.html"))) { console.error("[m1-settings] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });
const viewPack = JSON.parse(readFileSync(path.join(LOOM, "data/profiles/conversation_view.pack"), "utf8"));
const wordsOf = key => viewPack.entries.find(r => r.key === key).value.locales.pl;
const sh = wordsOf("presentation.settings_human"), rl = wordsOf("presentation.roles");
const fieldsPack = JSON.parse(readFileSync(path.join(LOOM, "data/runtime/settings_fields.pack"), "utf8")).defaults;
const packFieldCount = fieldsPack.fields.length + fieldsPack.profile_values.length;
const menuOf = JSON.parse(readFileSync(path.join(LOOM, "web/src/shell/data/layouts.json"), "utf8")).layouts.czat.menu;
const BASELINE = new Set(["none", "rom", "installation", "app_preset", "engine"]);
const isYours = f => { const p = f.provenance; if (!p || p.same_as_preset || p.chip?.same_as_preset) return false; return !BASELINE.has(p.scope ?? "none"); };

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }

const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", mkdtempSync(path.join(tmpdir(), "loom-m1-settings-")), "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
let browser, failed = false;
const step = async (name, fn) => { try { await fn(); console.log(`[m1-settings] PASS ${name}`); } catch (e) { failed = true; console.error(`[m1-settings] FAIL ${name}\n${e.stack}`); } };
const api = async (m, p, b) => { const r = await fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined }); assert.ok(r.ok, `${m} ${p}: ${r.status}`); return r.json(); };

try {
  await waitUp(`${base}/api/healthz`);
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  // The owner's situation after he picks a model for the analysis in the background: the kernel then knows the class of the model and
  // answers with the flow of every class (flow_by_class); without a model the card says there is no flow yet, with the reason.
  const bare = await api("POST", "/api/roles/resolved", { lang: "pl" });
  assert.equal(bare.roles.find(r => r.id === "analysis.fast").flow, null, "no model for the analysis yet: no flow (the card says so)");
  await api("POST", "/api/actions/run", { action: "settings.set", args: { key: "semantic_model", value: "google/gemini-2.5-flash" } });
  const resolved = await api("POST", "/api/settings/resolved", { lang: "pl" });
  const roles = await api("POST", "/api/roles/resolved", { lang: "pl" });
  assert.equal(resolved.counts.fields, packFieldCount, "the kernel counts the fields the pack declares (config + secret + profile values)");
  const groupsApi = [...resolved.groups].sort((a, b) => a.order - b.order);
  assert.equal(groupsApi.length, 8, "eight groups from the kernel");

  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  const context = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, isMobile: true });
  const page = await context.newPage();
  const errors = [];
  page.on("pageerror", e => errors.push(String(e)));
  const screen = () => page.getByTestId("settings-human");
  // a screenshot after the fade/slide animations end (opacity mid-animation would show the chat through the menu); viewport, not full page
  const shot = async name => { await page.evaluate(() => Promise.all(document.getAnimations().map(a => a.finished.catch(() => {})))); await page.screenshot({ path: path.join(SHOTS, `${name}.png`) }); };
  const back = () => page.evaluate(() => window.__loomBack());
  const noMissingWord = async () => {
    const texts = await screen().locator("button, span, p, b, h3, li").evaluateAll(els => els.map(el => el.textContent.trim()));
    const leaked = texts.filter(t => /^(group|groups|count|filter|field|planned|edit|status|chip|hidden|proposals|all_fields|total|yours|table|card|chain|class|flow|readers|mode|step|state|cost)\.[a-z_.]+$/.test(t));
    assert.deepEqual(leaked, [], "no catalogue id on screen (a missing word would show its id)");
  };
  const layoutFits = async () => {
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth) <= 390, "no horizontal page scroll");
    // the former settings screen (Silnik › Wszystkie pola, shum-legacy) is untouched by M1-W0 and keeps its 35-40 px buttons: excluded
    const small = await screen().locator("button").evaluateAll(els => els.filter(el => el.offsetParent !== null && !el.closest('[data-testid="shum-legacy"]')).map(el => {
      const r = el.getBoundingClientRect(); return { id: el.dataset.testid ?? el.textContent.trim().slice(0, 30), w: Math.round(r.width), h: Math.round(r.height) }; }).filter(b => b.w < 44 || b.h < 44));
    assert.deepEqual(small, [], "every tappable of the settings screens is at least 44 x 44 px");
  };
  const openGroup = async id => { await page.getByTestId(`shum-group-open-${id}`).tap(); await page.getByTestId(`shum-group-view-${id}`).waitFor(); };

  await page.goto(base + "/");
  await page.getByTestId("chat-shell").waitFor();

  await step("the ⋮ menu has the entries of layouts.json (Graf, Ustawienia, Ty, Ta rozmowa, Widoki, Analiza, Warsztat) and no Pamięć / Wiedza", async () => {
    await page.getByTestId("shell-menu-toggle").tap();
    await page.getByTestId("shell-menu").waitFor();
    const expected = menuOf.filter(e => !e.when).map(e => e.id);   // "connection" only in the Android shell (when: android_shell)
    const rows = await page.getByRole("menuitem").evaluateAll(els => els.map(el => el.dataset.testid.replace("shell-menu-", "")));
    assert.deepEqual(rows, expected, "the menu rows are the data's entries in order");
    for (const id of ["memory", "knowledge"]) assert.equal(await page.getByTestId(`shell-menu-${id}`).count(), 0, `no ${id} in the menu`);
    const labels = await page.getByRole("menuitem").evaluateAll(els => els.map(el => el.textContent.trim()));
    assert.ok(labels.every(l => !/^Pamięć|^Wiedza/.test(l)), `no Pamięć / Wiedza word: ${labels.join(" | ")}`);
    assert.ok(labels.every(l => !/^menu_/.test(l)), `every entry has its Polish word: ${labels.join(" | ")}`);
    const small = await page.getByRole("menuitem").evaluateAll(els => els.map(el => Math.round(el.getBoundingClientRect().height)).filter(h => h < 44));
    assert.deepEqual(small, [], "every menu row is at least 44 px high");
    await shot("m1-menu");
  });

  await step("menu ⋮ › Ustawienia: the eight groups of settings.resolved in the order of the data, each 'N pól · M Twoje'; the counts add up to the pack", async () => {
    await page.getByTestId("shell-menu-settings").tap();
    await page.getByTestId("panel-settings").waitFor();
    await screen().waitFor();
    await page.getByTestId("shum-groups").waitFor();
    const rows = await page.getByTestId("shum-groups").locator("li").evaluateAll(els => els.map(li => ({
      id: li.dataset.testid.replace("shum-group-", ""), fields: Number(li.dataset.fields), yours: Number(li.dataset.yours),
      label: li.querySelector(".gnav-label")?.textContent.trim(), count: li.querySelector('[data-testid^="shum-group-count-"]')?.textContent.trim() })));
    assert.deepEqual(rows.map(r => r.id), groupsApi.map(g => g.id), "the groups in the order of the data");
    for (const g of groupsApi) {
      const row = rows.find(r => r.id === g.id);
      assert.equal(row.label, g.label, `${g.id}: the Polish label of the kernel`);
      assert.equal(row.fields, g.fields.length, `${g.id}: field count`);
      assert.equal(row.yours, g.fields.filter(isYours).length, `${g.id}: 'Twoje' = values given on a person's level, not the preset`);
      assert.match(row.count, new RegExp(`^${row.fields}\\b.*\\b${row.yours}\\b`), `${g.id}: the count line says '${row.fields} … ${row.yours}' (${row.count})`);
      assert.ok(!/count\./.test(row.count), `${g.id}: a word, not an id`);
    }
    const sum = rows.reduce((n, r) => n + r.fields, 0);
    assert.equal(sum, resolved.counts.fields, "the counts of the groups add up to the kernel's count");
    assert.equal(sum, packFieldCount, "… which is the number of fields the pack declares");
    const total = page.getByTestId("shum-total");
    assert.equal(Number(await total.getAttribute("data-fields")), sum);
    assert.equal(Number(await total.getAttribute("data-yours")), rows.reduce((n, r) => n + r.yours, 0));
    assert.equal(await page.getByTestId("shum-filter").getAttribute("aria-checked"), "false", "Pokaż tylko zmienione is off at the start");
    await noMissingWord();
    await layoutFits();
    await shot("m1-settings-groups");
  });

  const rozmowa = groupsApi.find(g => g.id === "rozmowa");
  const temperature = rozmowa?.fields.find(f => f.key === "temperature");
  await step("Rozmowa › Temperatura: the Polish label, the value in words, the kernel's provenance chip; details show the flows that read it and the edit rule", async () => {
    assert.ok(temperature, "the kernel puts temperature in Rozmowa");
    await openGroup("rozmowa");
    assert.equal(Number(await page.getByTestId("shum-group-view-rozmowa").getAttribute("data-fields")), rozmowa.fields.length);
    assert.equal(await page.getByTestId("shum-back").textContent(), sh["group.back"]);
    assert.equal(await page.getByTestId("shum-group-why").textContent(), rozmowa.why);
    assert.equal(await page.getByTestId("shum-label-temperature").textContent(), temperature.human.label);
    assert.equal(await page.getByTestId("shum-value-temperature").textContent(), temperature.value_human);
    assert.equal(await page.getByTestId("shum-chip-provenance-temperature").textContent(), temperature.provenance.chip.label, "the chip is the kernel's");
    assert.equal(await page.getByTestId("shum-why-temperature").textContent(), temperature.human.why);
    const fieldsShown = await page.getByTestId("shum-fields-rozmowa").locator("li[data-testid^='shum-field-']").evaluateAll(els => els.map(li => li.dataset.testid.replace("shum-field-", "")));
    assert.deepEqual(fieldsShown.sort(), rozmowa.fields.map(f => f.key).sort(), "every field of the group is a row");
    await page.getByTestId("shum-field-main-temperature").tap();
    await page.getByTestId("shum-detail-temperature").waitFor();
    const flows = await page.getByTestId("shum-flows-temperature").textContent();
    for (const f of temperature.flows_reading) assert.ok(flows.includes(f.title), `the flow '${f.title}' reads temperature (${flows})`);
    assert.equal(await page.getByTestId("shum-effect-temperature").textContent(), temperature.human.effect);
    assert.ok((await page.getByTestId("shum-edit-temperature").textContent()).includes(sh[`edit.${temperature.edit_rule}`]), "the edit rule in words");
    await noMissingWord();
    await layoutFits();
    await shot("m1-settings-rozmowa");
  });

  await step("Pokaż tylko zmienione keeps only the fields that are yours (data-shown = Twoje of the group); off again shows them all", async () => {
    const view = page.getByTestId("shum-group-view-rozmowa");
    await page.getByTestId("shum-filter").tap();
    assert.equal(await page.getByTestId("shum-filter").getAttribute("aria-checked"), "true");
    await page.waitForFunction(({ n }) => document.querySelector('[data-testid="shum-group-view-rozmowa"]')?.dataset.shown === String(n), { n: rozmowa.fields.filter(isYours).length });
    const shown = await view.locator("li[data-testid^='shum-field-']").evaluateAll(els => els.map(li => li.dataset.testid.replace("shum-field-", "")));
    assert.deepEqual(shown.sort(), rozmowa.fields.filter(isYours).map(f => f.key).sort(), "exactly the fields with a value of yours");
    await page.getByTestId("shum-filter").tap();
    await page.waitForFunction(({ n }) => document.querySelector('[data-testid="shum-group-view-rozmowa"]')?.dataset.shown === String(n), { n: rozmowa.fields.length });
  });

  const plannedField = groupsApi.flatMap(g => g.fields.map(f => ({ g, f }))).find(({ f }) => f.status === "planned");
  await step("a planned field says it is planned, with the reason from the data (no field disappears)", async () => {
    assert.ok(plannedField, "the kernel marks at least one field as planned today");
    const { g, f } = plannedField;
    if (g.id !== "rozmowa") { await page.getByTestId("shum-back").tap(); await page.getByTestId("shum-groups").waitFor(); await openGroup(g.id); }
    assert.equal(await page.getByTestId(`shum-chip-status-${f.key}`).textContent(), sh["status.planned"]);
    const reason = await page.getByTestId(`shum-planned-${f.key}`).textContent();
    assert.ok(reason.includes(f.human.effect), `the reason is the field's own sentence (${reason})`);
    await page.getByTestId("shum-back").tap();
    await page.getByTestId("shum-groups").waitFor();
  });

  const analysis = roles.roles.find(r => r.id === "analysis.fast");
  await step("Modele › Role: the table of roles.resolved as returned (every role, its model in words, class chip); Analiza w tle opens the card", async () => {
    await openGroup("modele");
    await page.getByTestId("roles-table").waitFor();
    const rows = await page.getByTestId("roles-table").locator("li[data-testid^='role-row-']").evaluateAll(els => els.map(li => ({
      id: li.dataset.testid.replace("role-row-", ""), tier: li.dataset.tier, label: li.querySelector('[data-testid^="role-label-"]')?.textContent.trim(),
      model: li.querySelector('[data-testid^="role-model-"]')?.textContent.trim() })));
    assert.deepEqual(rows.map(r => r.id), roles.roles.map(r => r.id), "the roles in the order of the kernel");
    for (const r of roles.roles) {
      const row = rows.find(x => x.id === r.id);
      assert.equal(row.label, r.label, `${r.id}: label`);
      assert.equal(row.tier, r.tier.id, `${r.id}: class`);
      assert.ok(row.model.includes(r.model_human), `${r.id}: the model in words ('${row.model}' has '${r.model_human}')`);
    }
    assert.equal(await page.getByTestId("roles-title").textContent(), rl.title);
    await noMissingWord();
    await layoutFits();
    await shot("m1-settings-roles");
    assert.ok(analysis, "the kernel knows the role analysis.fast");
    await page.getByTestId("role-open-analysis.fast").tap();
    await page.getByTestId("role-card").waitFor();
    assert.equal(await page.getByTestId("role-card").getAttribute("data-role"), "analysis.fast");
    assert.equal(await page.getByTestId("shum-group-view-modele").getAttribute("data-role-open"), "1", "only the card shows while it is open");
  });

  await step("the role card: the policy chain word for word (one step decides), the class with its reason, the flow per class, and EVERY flow that reads the key", async () => {
    const card = page.getByTestId("role-card");
    assert.equal(await page.getByTestId("role-card-title").textContent(), analysis.label);
    assert.ok((await page.getByTestId("role-why").textContent()).includes(analysis.why));
    const steps = await card.locator("[data-testid^='role-chain-step-']").evaluateAll(els => els.map(el => ({ n: el.dataset.testid.replace("role-chain-step-", ""), type: el.dataset.type, decides: el.dataset.decides })));
    assert.equal(steps.length, analysis.chain.length, "one row per step of the kernel's chain");
    for (const s of analysis.chain) {
      assert.equal(await page.getByTestId(`role-chain-meaning-${s.step}`).textContent(), s.meaning, `step ${s.step}: the kernel's sentence word for word`);
      assert.equal(steps.find(x => x.n === String(s.step)).type, s.type);
      assert.equal(await page.getByTestId(`role-chain-decides-${s.step}`).count(), s.decides ? 1 : 0, `step ${s.step}: 'decides' as the data says`);
    }
    assert.equal(await page.getByTestId("role-chain").getAttribute("data-read"), analysis.chain_status === "read" ? "1" : "0");
    assert.ok((await page.getByTestId("role-tier-reason").textContent()).includes(analysis.tier.reason), "the class comes with the kernel's reason");
    await page.getByTestId("role-flow-by-class").waitFor();
    const classes = await card.locator("li[data-testid^='role-class-']").evaluateAll(els => els.map(el => ({ id: el.dataset.testid.replace("role-class-", ""), current: el.dataset.current === "1" })));
    assert.deepEqual(classes.map(c => c.id), analysis.flow_by_class.map(e => e.tier.id), "a flow for every class the kernel lists, in its order");
    assert.deepEqual(classes.filter(c => c.current).map(c => c.id), [analysis.tier.id], "the class of the chosen model is marked");
    assert.ok(await card.locator('[data-status="calibration"]').count() >= 1, "a flow marked kalibracja where the data says so");
    const readers = page.getByTestId("role-readers");
    const expected = (analysis.flows_reading ?? []).length + (analysis.flows_reading_setting ?? []).length;
    assert.equal(Number(await readers.getAttribute("data-count")), expected, "every flow that reads the role or its key is listed");
    for (const f of analysis.flows_reading_setting ?? []) assert.ok(await page.getByTestId(`role-reader-${f.id}`).count() >= 1, `reader ${f.id} (${f.title})`);
    for (const f of analysis.flows_reading ?? []) assert.ok(await page.getByTestId(`role-reader-${f.id}`).count() >= 1, `reader ${f.id}`);
    assert.ok((await page.getByTestId("role-readers-total").textContent()).includes(String(expected)));
    await noMissingWord();
    await layoutFits();
    await shot("m1-settings-role-card");
    await page.getByTestId("role-chain").scrollIntoViewIfNeeded();
    await shot("m1-settings-role-chain");
    await readers.scrollIntoViewIfNeeded();
    await shot("m1-settings-role-readers");
  });

  await step("Back closes the layers in order: the role card, then the group, then the panel; at the root it falls through", async () => {
    assert.equal(await back(), true, "the card");
    await page.getByTestId("role-card").waitFor({ state: "detached" });
    await page.getByTestId("roles-table").waitFor();
    assert.equal(await back(), true, "the group");
    await page.getByTestId("shum-group-view-modele").waitFor({ state: "detached" });
    await page.getByTestId("shum-groups").waitFor();
    assert.equal(await back(), true, "the panel");
    await page.getByTestId("panel-settings").waitFor({ state: "detached" });
    assert.equal(await back(), false, "nothing left: the shell lets the Android activity decide");
  });

  await step("Silnik › Wszystkie pola = the former settings screen (fields, connection, keys, JSON): nothing disappears", async () => {
    await page.getByTestId("shell-menu-toggle").tap();
    await page.getByTestId("shell-menu-settings").tap();
    await page.getByTestId("shum-groups").waitFor();
    await openGroup("silnik");
    const disclosure = page.getByTestId("shum-all-fields");
    await disclosure.waitFor();
    assert.ok((await page.getByTestId("shum-all-fields-why").textContent()).length > 10, "the line says why the old screen is here");
    await disclosure.locator(".disclosure-head").first().tap();
    await page.getByTestId("settings-panel").waitFor();
    await page.locator('[data-testid^="cfg-"]').first().waitFor();   // the former screen loads its fields from the server
    assert.ok(await page.locator('[data-testid^="cfg-"]').count() >= 1, "the former fields are there");
    await layoutFits();
    await shot("m1-settings-all-fields");
    assert.equal(await back(), true);
    await page.getByTestId("shum-groups").waitFor();
    assert.equal(await back(), true);
    await page.getByTestId("panel-settings").waitFor({ state: "detached" });
  });

  assert.deepEqual(errors, [], "no page errors");
} finally {
  await browser?.close();
  server.kill();
}
if (failed) process.exit(1);
