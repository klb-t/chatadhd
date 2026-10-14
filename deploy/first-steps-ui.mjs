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
const ctx = await browser.newContext({ viewport: { width: Number(process.env.FS_WIDTH || 1100), height: Number(process.env.FS_HEIGHT || 760) } });
const page = await ctx.newPage();
const errors = [];
page.on("pageerror", e => errors.push(e.message));
// Dziennik zdarzen do diagnozy przy bledzie: konsola strony i kazde zapytanie /api/* z kodem odpowiedzi.
await page.addInitScript(() => { window.__clicks = []; for (const t of ["pointerdown", "pointerup", "click"]) document.addEventListener(t, e => window.__clicks.push(t + " " + (e.target.outerHTML || "").slice(0, 70) + " connected=" + e.target.isConnected), true); });
const trace = [];
const t0 = Date.now();
const note = m => trace.push(String(((Date.now() - t0) / 1000).toFixed(1)).padStart(6) + "s " + m);
page.on("console", m => { if (m.type() === "error" || m.type() === "warning") note("console." + m.type() + ": " + m.text().slice(0, 300)); });
page.on("request", r => { if (r.url().includes("/api/")) note("-> " + r.method() + " " + r.url().replace(base, "")); });
page.on("response", r => { if (r.url().includes("/api/")) note("<- " + r.status() + " " + r.url().replace(base, "")); });
page.on("requestfailed", r => note("!! " + r.method() + " " + r.url().replace(base, "") + " " + (r.failure()?.errorText ?? "")));
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
    await page.getByTestId("close-panel").click();
  });
  await step("6 nowa rozmowa", async () => {
    // Na telefonie (<= 720 px) lista rozmow jest schowana: najpierw ja otwieramy, potem zamykamy.
    const narrow = page.viewportSize().width <= 720;
    if (narrow) await page.getByTestId("toggle-sidebar").click();
    await page.getByTestId("new-conversation").click();
    if (narrow) { await page.waitForTimeout(300); if (await page.getByTestId("new-conversation").isVisible()) await page.getByTestId("toggle-sidebar").click(); }
    await page.getByTestId("chat-input").waitFor();
  });

  // Wysylanie jest chwilowo zablokowane, gdy widok rozmowy sie wczytuje ("conversation_view_loading", zasada R43: przycisk
  // dostaje `disabled` i `data-gate-reason`). Klikniecie w tym oknie jest ignorowane, wiec: czekamy az blokada zniknie,
  // klikamy, a gdy pole nie oprozni sie (klik trafil w zablokowany przycisk), klikamy ponownie.
  const send = async text => {
    const input = page.getByTestId("chat-input"), button = page.getByTestId("send-chat");
    await input.fill(text);
    for (let attempt = 0; attempt < 5; attempt++) {
      for (const end = Date.now() + 20000; Date.now() < end;) {
        const ready = await button.evaluate(b => !b.disabled && !b.getAttribute("data-gate-reason")).catch(() => false);
        if (ready) break;
        await page.waitForTimeout(50);
      }
      await button.click({ timeout: 5000 }).catch(() => {});
      for (const end = Date.now() + 3000; Date.now() < end;) {
        if ((await input.inputValue()) === "") return;
        await page.waitForTimeout(50);
      }
    }
    throw new Error("wiadomosc nie zostala wyslana (pole nie opróżnione): " + text);
  };
  const body = () => page.locator(".msg.assistant .body");
  const proposalThenYes = async (name, text, mustMatch) => {
    await send(text);
    const block = page.getByTestId("chat-action-proposal");
    await block.waitFor({ timeout: 60000 });
    let shown = "";
    for (const end = Date.now() + 15000; Date.now() < end;) {
      shown = await block.innerText().catch(() => "");
      if (mustMatch.test(shown)) break;
      await page.waitForTimeout(100);
    }
    assert.match(shown, mustMatch, name + ": propozycja: " + shown.slice(0, 300));
    if (name.startsWith("1") && shots) await block.screenshot({ path: path.join(shots, "04-propozycja.png") });
    await block.getByTestId("chat-action-proposal-confirm").click();
    await block.waitFor({ state: "detached", timeout: 60000 });
    assert.equal(await page.getByTestId("chat-error").count(), 0, name + ": blad w rozmowie");
  };

  await step("6.0 zwykla wiadomosc", async () => {
    await send("Zespol Acme planuje wydac wersje 2.0 w piatek, jesli testy przejda. Odpowiedz dwoma zdaniami.");
    await body().filter({ hasText: "Odpowiedz testowego dostawcy" }).first().waitFor({ timeout: 60000 });
  });
  await step("6.1 streść tę rozmowę", async () => {
    await proposalThenYes("1", "Streść tę rozmowę", /summar|stresz|Streszcz/i);
    await body().filter({ hasText: "Streszczenie testowe" }).first().waitFor({ timeout: 60000 });
  });
  await step("6.2 przetłumacz na niemiecki", async () => { await proposalThenYes("2", "Przetłumacz tę rozmowę na niemiecki", /translat|tłumacz|Tłumacz/i); });
  await step("6.3 wytłumacz prościej", async () => { await proposalThenYes("3", "Wytłumacz prościej tę rozmowę", /explain|prościej|Wyjaśnij|Wytłumacz/i); });
  await step("6.4 zawsze pytaj przed streszczeniem (reguła)", async () => {
    await proposalThenYes("4", "zawsze pytaj mnie przed summarise", /./);
    const rules = await (await ctx.request.post(base + "/api/rules/list", { data: {} })).text();
    assert.match(rules, /summarise/);
  });
  await step("6.5 pokaż ustawienie temperature", async () => { await proposalThenYes("5", "pokaż temperature", /ustawienie|setting/i); });

  const calls = readFileSync(providerLog, "utf8").split("\n").filter(Boolean).map(l => JSON.parse(l));
  assert.ok(calls.some(c => c.authorization === "Bearer " + key), "dostawca dostal klucz wpisany w Ustawieniach");
  assert.deepEqual(errors, [], "bledy strony: " + errors.join("; "));
  log("OK: wszystkie kroki w przegladarce");
} catch (e) {
  await shot("blad").catch(() => {});
  console.error(e);
  console.error("--- ostatnie zdarzenia strony ---\n" + trace.slice(-60).join("\n"));
  console.error("--- tekst pola rozmowy ---\n" + await page.getByTestId("chat-input").inputValue().catch(() => "?"));
  console.error("--- przycisk wysylania ---\n" + await page.getByTestId("send-chat").evaluate(b => b.outerHTML).catch(() => "brak (moze 'Stop')") +
    "\n--- Stop? " + await page.getByTestId("cancel-chat").count().catch(() => "?"));
  console.error("--- ostatnie klikniecia (cel w DOM) ---\n" + (await page.evaluate(() => window.__clicks.slice(-8).join("\n")).catch(() => "?")));
  const box = await page.getByTestId("send-chat").boundingBox().catch(() => null);
  console.error("--- pole Send: " + JSON.stringify(box) + " viewport " + JSON.stringify(page.viewportSize()));
  console.error("--- widoczny blad ---\n" + await page.getByTestId("chat-error").allInnerTexts().catch(() => "?"));
  process.exitCode = 1;
} finally {
  await browser.close();
}
