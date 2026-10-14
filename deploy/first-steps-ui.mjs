#!/usr/bin/env node
// Kroki 3-6 z docs/PIERWSZE_KROKI.md w prawdziwej przegladarce (Chromium headless przez Playwright):
// logowanie, klucz dostawcy w Ustawieniach, piec sprawdzen z okienka rozmowy. Uruchamia go first-steps-check.sh --ui.
// Wejscie (zmienne srodowiskowe): FS_BASE FS_LOGIN FS_PASS FS_PROVIDER FS_KEY FS_PROVIDER_LOG [FS_SHOTS=katalog na zrzuty]
import assert from "node:assert/strict";
import { existsSync, mkdirSync, readFileSync } from "node:fs";
import path from "node:path";

const env = name => process.env[name] ?? (() => { throw new Error("brak " + name); })();
const base = env("FS_BASE"), login = env("FS_LOGIN"), pass = env("FS_PASS"), provider = env("FS_PROVIDER"), key = env("FS_KEY");
const providerLog = env("FS_PROVIDER_LOG"), shots = process.env.FS_SHOTS || "";
if (shots) mkdirSync(shots, { recursive: true });

async function loadChromium() {
  try { return (await import("playwright")).chromium; } catch { /* nie ma lokalnie */ }
  const candidates = [process.env.PLAYWRIGHT_GLOBAL_MODULE, "/opt/node22/lib/node_modules/playwright/index.mjs",
    path.join(path.dirname(new URL(import.meta.url).pathname), "../loom/web/node_modules/playwright/index.mjs")].filter(Boolean);
  for (const c of candidates) if (existsSync(c)) return (await import(c)).chromium;
  throw new Error("nie znaleziono pakietu playwright (cd loom/web && npm ci)");
}
const log = m => console.error("[first-steps-ui] " + m);
const chromium = await loadChromium();
const browser = await chromium.launch({ headless: true });
const ctx = await browser.newContext({ viewport: { width: 820, height: 600 } });
const page = await ctx.newPage();
const errors = [];
page.on("pageerror", e => errors.push(e.message));
const shot = async name => { if (shots) await page.screenshot({ path: path.join(shots, name + ".png") }); };
const step = async (name, fn) => { await fn(); log("OK " + name); };

try {
  await step("3 otwarcie web: formularz logowania", async () => {
    await page.goto(base, { waitUntil: "domcontentloaded" });
    await page.getByTestId("auth-form").waitFor({ timeout: 20000 });
    await shot("01-logowanie");
  });
  await step("3 logowanie kontem z kroku 2", async () => {
    await page.getByTestId("auth-login").fill(login);
    await page.getByTestId("auth-password").fill(pass);
    await page.getByTestId("auth-submit").click();
    await page.getByTestId("chat-view").waitFor({ timeout: 20000 });
    await shot("02-rozmowa");
  });
  await step("5 klucz dostawcy w Ustawieniach", async () => {
    await page.getByTestId("nav-settings").click();
    await page.getByTestId("settings-panel").waitFor();
    await page.getByTestId("cfg-base-url").fill(provider);
    await page.getByTestId("cfg-base-url").blur();
    await page.getByTestId("cfg-default-model").fill("test/first-steps");
    await page.getByTestId("cfg-default-model").blur();
    await page.getByTestId("secret-api_key").fill(key);
    await page.getByTestId("secret-api_key-save").click();
    // czekaj, az serwer potwierdzi zapis (pola zapisuja sie na blur bez czekania)
    for (const end = Date.now() + 15000; Date.now() < end;) {
      const cfg = await (await ctx.request.get(base + "/api/config")).json();
      const has = await (await ctx.request.get(base + "/api/secrets/api_key/has")).json();
      if (cfg.default_model === "test/first-steps" && cfg.base_url === provider && has.has === true) break;
      await page.waitForTimeout(150);
    }
    const cfg = await (await ctx.request.get(base + "/api/config")).json();
    assert.equal(cfg.base_url, provider);
    assert.equal(cfg.default_model, "test/first-steps");
    assert.equal((await (await ctx.request.get(base + "/api/secrets/api_key/has")).json()).has, true);
    await shot("03-ustawienia");
    // trasowanie intencji modelem (to tez ustawienie, domyslnie lokalne); tu sprawdzamy sciezke z modelem
    await ctx.request.patch(base + "/api/config", { data: { intent_routing_method: "model", intent_routing_model: "test/first-steps" } });
    await page.getByTestId("close-panel").click();
  });
  await step("6 nowa rozmowa", async () => {
    await page.getByTestId("new-conversation").click();
    await page.getByTestId("chat-input").waitFor();
  });

  const send = async text => { await page.getByTestId("chat-input").fill(text); await page.getByTestId("send-chat").click(); };
  const body = () => page.locator(".msg.assistant .body");
  const proposalThenYes = async (name, text, mustMatch) => {
    await send(text);
    const block = page.getByTestId("chat-action-proposal");
    await block.waitFor({ timeout: 30000 });
    assert.match(await block.innerText(), mustMatch, name + ": propozycja");
    if (name.startsWith("1")) await shot("04-propozycja");
    await block.getByTestId("chat-action-proposal-confirm").click();
    await block.waitFor({ state: "detached", timeout: 30000 });
    assert.equal(await page.getByTestId("chat-error").count(), 0, name + ": blad w rozmowie");
  };

  await step("6.0 zwykla wiadomosc", async () => {
    await send("Zespol Acme planuje wydac wersje 2.0 w piatek, jesli testy przejda. Odpowiedz dwoma zdaniami.");
    await body().filter({ hasText: "Odpowiedz testowego dostawcy" }).first().waitFor({ timeout: 30000 });
  });
  await step("6.1 streść tę rozmowę", async () => {
    await proposalThenYes("1", "Streść tę rozmowę", /summar|stresz|Streszcz/i);
    await body().filter({ hasText: "Streszczenie testowe" }).first().waitFor({ timeout: 30000 });
    await shot("05-streszczenie");
  });
  await step("6.2 przetłumacz na niemiecki", async () => { await proposalThenYes("2", "Przetłumacz na niemiecki", /translat|tłumacz|Tłumacz/i); });
  await step("6.3 wytłumacz prościej", async () => { await proposalThenYes("3", "Wytłumacz prościej tę rozmowę", /explain|prościej|Wyjaśnij|Wytłumacz/i); });
  await step("6.4 zawsze pytaj przed streszczeniem (reguła)", async () => {
    await proposalThenYes("4", "zawsze pytaj mnie przed summarise", /./);
    const rules = await (await ctx.request.post(base + "/api/rules/list", { data: {} })).text();
    assert.match(rules, /summarise/);
  });
  await step("6.5 pokaż ustawienie temperature", async () => {
    await send("pokaż temperature");
    // ustawienie jest tylko do odczytu: odpowiedz albo propozycja z "tak"
    const block = page.getByTestId("chat-action-proposal");
    if (await block.waitFor({ timeout: 10000 }).then(() => true, () => false)) await block.getByTestId("chat-action-proposal-confirm").click();
    await page.waitForTimeout(500);
    assert.equal(await page.getByTestId("chat-error").count(), 0);
  });

  const calls = readFileSync(providerLog, "utf8").split("\n").filter(Boolean).map(l => JSON.parse(l));
  assert.ok(calls.some(c => c.authorization === "Bearer " + key), "dostawca dostal klucz wpisany w Ustawieniach");
  assert.deepEqual(errors, [], "bledy strony: " + errors.join("; "));
  log("OK: wszystkie kroki w przegladarce");
} catch (e) {
  await shot("blad").catch(() => {});
  console.error(e);
  process.exitCode = 1;
} finally {
  await browser.close();
}
