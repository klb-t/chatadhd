#!/usr/bin/env node
// P33: the + menu next to the message field. Real loom-server + mock LLM + Chromium at phone width (390x844), Polish labels.
import { spawn } from "node:child_process";
import { mkdtempSync, existsSync, mkdirSync } from "node:fs";
import { tmpdir } from "node:os";
import path from "node:path";
import net from "node:net";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const LOOM = path.resolve(here, "../..");
const SERVER_BIN = path.join(LOOM, "build/dev/server/loom-server");
const DIST = path.join(LOOM, "web/dist");
// The tracked pictures in e2e/screenshots/ are evidence, not output (a run left the tree dirty): pictures go to E2E_SHOTS_DIR, else to the temp folder.
const SHOTS = process.env.E2E_SHOTS_DIR || path.join(tmpdir(), "loom-e2e-screenshots");
const CHROMIUM = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";
if (!existsSync(SERVER_BIN) || !existsSync(DIST)) { console.error("[composer-menu] build loom-server and `npm run build` first"); process.exit(2); }
mkdirSync(SHOTS, { recursive: true });

const freePort = () => new Promise((res, rej) => { const s = net.createServer(); s.listen(0, "127.0.0.1", () => { const { port } = s.address(); s.close(() => res(port)); }); s.on("error", rej); });
async function waitUp(url) { for (const end = Date.now() + 20000; Date.now() < end;) { try { if ((await fetch(url)).ok) return; } catch {} await new Promise(r => setTimeout(r, 150)); } throw new Error("never up " + url); }
async function chromium() { try { return (await import("playwright")).chromium; } catch { return (await import("/opt/node22/lib/node_modules/playwright/index.mjs")).chromium; } }
const until = async (what, probe, ms = 10000) => {
  let last;
  for (const end = Date.now() + ms; Date.now() < end; await new Promise(r => setTimeout(r, 100))) { last = await probe(); if (last) return last; }
  throw new Error(`never: ${what}`);
};

const mockPort = await freePort();
const mock = spawn("python3", [path.join(here, "mock_llm_server.py"), String(mockPort)], { stdio: "ignore" });
const dataDir = mkdtempSync(path.join(tmpdir(), "loom-composer-menu-"));
const port = await freePort();
const server = spawn(SERVER_BIN, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", dataDir, "--static-dir", DIST], { stdio: "ignore", cwd: LOOM });
const base = `http://127.0.0.1:${port}`;
await waitUp(`${base}/api/healthz`);
const api = (m, p, b) => fetch(base + p, { method: m, headers: { "content-type": "application/json" }, body: b ? JSON.stringify(b) : undefined });
const last = async () => (await fetch(`http://127.0.0.1:${mockPort}/__last`)).json();
const settingsOf = async (id) => (await (await api("GET", `/api/conversations/${id}`)).json()).metadata?.settings ?? {};
const messagesOf = async (id) => { const r = await (await api("GET", `/api/conversations/${id}/messages`)).json(); return Array.isArray(r) ? r : r.messages; };
const conversations = async () => { const r = await (await api("GET", "/api/conversations")).json(); return Array.isArray(r) ? r : r.conversations; };

let browser, failed = false;
const step = async (name, fn) => { try { await fn(); console.log(`[composer-menu] PASS ${name}`); } catch (e) { failed = true; console.error(`[composer-menu] FAIL ${name}\n${e.stack}`); } };
const PNG_1PX = Buffer.from("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==", "base64");

try {
  await api("PUT", "/api/config/base_url", { value: `http://127.0.0.1:${mockPort}` });
  await api("PUT", "/api/config/default_model", { value: "mock/e2e-model" });
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  await api("POST", "/api/secrets/api_key", { value: "sk-e2e-mock-key" });
  await api("POST", "/api/models/refresh");
  const launch = await chromium();
  browser = await launch.launch({ headless: true }).catch(() => launch.launch({ headless: true, executablePath: CHROMIUM }));
  // Reduced motion: the screenshots show the menu, not its fade-in (and the menu honours the preference).
  const context = await browser.newContext({ viewport: { width: 390, height: 844 }, hasTouch: true, reducedMotion: "reduce" });
  const page = await context.newPage();
  const png = (n) => page.screenshot({ path: path.join(SHOTS, `composer-menu-phone-${n}.png`) });
  await page.goto(base, { waitUntil: "domcontentloaded" });
  await page.getByTestId("chat-shell").waitFor();
  const menu = page.getByTestId("composer-menu");
  const plus = page.getByTestId("shell-attach");
  const picker = page.getByTestId("model-picker");
  const assistants = () => page.locator('[data-testid="message"][data-role="assistant"]').count();
  const attach = async (files) => {
    await plus.click();
    const chooser = page.waitForEvent("filechooser");
    await page.getByTestId("composer-menu-attach_file").click();
    (await chooser).setFiles(files);
    await until("chips ready", async () => (await page.locator('[data-testid="attachment-chip"][data-state="uploading"]').count()) === 0
      && (await page.getByTestId("attachment-chip").count()) >= files.length);
  };
  const openConversation = async (title) => {
    await page.getByTestId("shell-drawer-toggle").click();
    await page.locator('[data-testid="shell-conv"]', { hasText: title }).first().click();
  };
  const newChat = async () => { await page.getByTestId("shell-drawer-toggle").click(); await page.getByTestId("shell-new-chat").click(); };

  await step("+ opens a menu with Polish plain words, not the import panel; the photo entry is there", async () => {
    await plus.click();
    await menu.waitFor();
    assert.equal(await page.getByTestId("import-panel").count(), 0, "the import panel does not open directly");
    const text = await menu.innerText();
    for (const w of ["Załącz plik", "Zdjęcie lub aparat", "Wybierz model", "Narzędzia", "Kontekst", "Importuj rozmowy"]) assert.ok(text.includes(w), `menu has: ${w}`);
    assert.ok(!text.includes("Projekt"), "no project entry is promised");
    assert.ok(!/cm_|attach_file|web_search/.test(text), "no internal ids in the menu");
    const box = await menu.boundingBox();
    assert.ok(box.x >= 0 && box.x + box.width <= 390, "the menu fits the phone width");
    for (const row of await menu.locator("[data-cm-row]").all()) assert.ok((await row.boundingBox()).height >= 44, "rows are touch targets");
    await png("01-menu");
  });

  await step("keyboard: arrows move, Escape closes and returns focus to +", async () => {
    await page.keyboard.press("ArrowDown");
    const a = await page.evaluate(() => document.activeElement?.getAttribute("data-testid"));
    await page.keyboard.press("ArrowDown");
    const b = await page.evaluate(() => document.activeElement?.getAttribute("data-testid"));
    assert.notEqual(a, b);
    await page.keyboard.press("Escape");
    await menu.waitFor({ state: "detached" });
    assert.equal(await page.evaluate(() => document.activeElement?.getAttribute("data-testid")), "shell-attach");
  });

  await step("Tools: the agent and deep research are there; painting without an image model says so and leads to Settings; music says the truth", async () => {
    await plus.click();
    await page.getByTestId("composer-menu-tools").click();
    for (const t of ["Wyszukiwanie w sieci", "Głębokie badanie", "Głębokie myślenie", "Tryb agenta", "Generuj obraz", "Generuj muzykę"]) assert.ok((await menu.innerText()).includes(t), t);
    assert.notEqual(await page.getByTestId("composer-menu-agent").getAttribute("aria-disabled"), "true", "the agent is an existing action");
    await page.getByTestId("composer-menu-reason-image").waitFor();
    assert.match(await page.getByTestId("composer-menu-reason-image").innerText(), /Ustawieniach/);
    assert.equal(await page.getByTestId("composer-menu-image").getAttribute("aria-disabled"), "true");
    assert.match(await page.getByTestId("composer-menu-reason-music").innerText(), /nie ma takiej czynności/);
    await png("02-tools");
    await page.getByTestId("composer-menu-image").click({ force: true });  // aria-disabled: it does nothing but lead to where it is set
    await page.getByTestId("settings-panel").first().waitFor();
    await menu.waitFor({ state: "detached" });
    await page.goto(base, { waitUntil: "domcontentloaded" });
    await page.getByTestId("chat-shell").waitFor();
  });

  await step("attach a file: chip above the field, removable; an image shows a preview; the scope of a file is said", async () => {
    await attach([{ name: "notatki.txt", mimeType: "text/plain", buffer: Buffer.from("Sekretne słowo: PAPUGA") }, { name: "pixel.png", mimeType: "image/png", buffer: PNG_1PX }]);
    assert.equal(await page.getByTestId("attachment-chip").count(), 2);
    await page.getByTestId("attachment-preview").waitFor();
    assert.match(await page.getByTestId("attachment-scope").innerText(), /tylko z tą wiadomością/);
    const field = await page.getByTestId("chat-input").boundingBox();
    for (const chip of await page.getByTestId("attachment-chip").all()) assert.ok((await chip.boundingBox()).y + 1 < field.y, "chips sit above the field");
    assert.ok(field.width >= 150, `the field keeps its width next to the chips (${field.width}px)`);
    const plusBox = await plus.boundingBox();
    assert.ok(Math.abs(plusBox.y + plusBox.height - (field.y + field.height)) < 24, "+ stays on the row of the field");
    await png("03-chips");
    await page.locator('[data-testid="attachment-chip"][data-name="pixel.png"] [data-testid="attachment-remove"]').click();
    assert.equal(await page.getByTestId("attachment-chip").count(), 1);
  });

  await step("in a new chat the model chosen in the menu is the person's pick, the one shown above", async () => {
    await plus.click();
    await page.getByTestId("composer-menu-model").click();
    await page.locator('[data-testid="composer-model-option"][data-model="mock/second-model"]').click();
    await menu.waitFor({ state: "detached" });
    assert.equal(await picker.inputValue(), "mock/second-model", "same state as the header picker");
  });

  let convA = null;
  await step("web search toggled in Tools shows a chip; the message goes out with the file, model and the tool; the new conversation keeps its model", async () => {
    await plus.click();
    await page.getByTestId("composer-menu-tools").click();
    await page.getByTestId("composer-menu-web_search").click();
    assert.equal(await page.getByTestId("composer-menu-web_search").getAttribute("aria-checked"), "true");
    await page.keyboard.press("Escape");
    assert.match(await page.getByTestId("tool-chip-web_search").innerText(), /Wyszukiwanie w sieci: włączone/);
    await page.getByTestId("chat-input").fill("Co jest w pliku?");
    await page.getByTestId("send-chat").click();
    await page.locator('[data-testid="message"][data-role="assistant"]').first().waitFor({ timeout: 20000 });
    const body = await last();
    assert.equal(body.model, "mock/second-model");
    assert.equal(body.plugins?.some?.(p => p.id === "web") || body.web_search === true, true, "web search was requested");
    const sent = JSON.stringify(body.messages);
    assert.ok(sent.includes("PAPUGA") && sent.includes("notatki.txt"), "the file text reached the model");
    assert.equal(await page.getByTestId("attachment-chip").count(), 0, "chips are cleared after sending");
    await page.getByTestId("message-attachment").first().waitFor();
    await png("04-sent");
    // The conversation this send made keeps the model it was made with (it differs from the person's setting the chain would give).
    convA = (await conversations())[0].id;
    await until("model kept at the conversation level", async () => (await settingsOf(convA)).default_model === "mock/second-model");
    await page.getByTestId("tool-chip-web_search").locator("button").click();
    assert.equal(await page.getByTestId("tool-chip-web_search").count(), 0);
  });

  await step("an image attachment reaches the model as an image part", async () => {
    await attach([{ name: "pixel.png", mimeType: "image/png", buffer: PNG_1PX }]);
    await page.getByTestId("chat-input").fill("Co na obrazku?");
    await page.getByTestId("send-chat").click();
    await page.waitForFunction(() => document.querySelectorAll('[data-testid="message"][data-role="assistant"]').length >= 2, null, { timeout: 20000 });
    assert.ok(JSON.stringify((await last()).messages).includes("image_url"));
  });

  await step("files alone are a message: Send works with no words, and no empty text part goes", async () => {
    const before = await assistants();
    await attach([{ name: "lista.txt", mimeType: "text/plain", buffer: Buffer.from("mleko, chleb") }]);
    assert.equal(await page.getByTestId("chat-input").inputValue(), "");
    assert.equal(await page.getByTestId("send-chat").isEnabled(), true);
    await page.getByTestId("send-chat").click();
    await until("an answer", async () => (await assistants()) > before, 20000);
    const content = (await last()).messages.at(-1).content;
    assert.ok(Array.isArray(content) && content.length === 1 && content[0].text.includes("mleko"), JSON.stringify(content));
  });

  await step("a file the model cannot take is refused before anything is sent: the chip says why, the words come back", async () => {
    const before = (await messagesOf(convA)).length;
    await attach([{ name: "raport.pdf", mimeType: "application/pdf", buffer: Buffer.from("%PDF-1.7 raport") }]);
    await page.getByTestId("chat-input").fill("Streść raport");
    await page.getByTestId("send-chat").click();
    const chip = page.locator('[data-testid="attachment-chip"][data-name="raport.pdf"]');
    await until("refused chip", async () => (await chip.getAttribute("data-state")) === "failed");
    assert.match(await chip.innerText(), /nie da się wysłać tego rodzaju pliku/);
    assert.equal(await page.getByTestId("chat-input").inputValue(), "Streść raport");
    assert.match(await page.locator("body").innerText(), /Nic nie zostało wysłane\. raport\.pdf/);
    assert.equal((await messagesOf(convA)).length, before, "nothing was stored");
    await chip.getByTestId("attachment-remove").click();
    await page.getByTestId("chat-input").fill("");
  });

  await step("a send held for the usage confirmation keeps its files: they come back as chips; confirming sends them and clears them", async () => {
    let held = null;
    await page.route("**/api/chat", async (route) => {
      const body = route.request().postDataJSON();
      if (!body.usage_confirmation) {
        held = body;
        await route.fulfill({ status: 200, headers: { "content-type": "text/event-stream" }, body: `data: ${JSON.stringify({ type: "usage_pending", conv_id: convA,
          usage_pending: { status: "requires_confirmation", operation_id: "op-x10", receipt_id: "usage_r1", baseline_key: "chat.send|e2e|model",
            projected: { input_bytes: { projected: 60000, baseline: 250, ratio: 240 }, cost_usd: { projected: null, baseline: null, ratio: null } } } })}\n\n` });
      } else {
        held = body;
        await route.fulfill({ status: 200, headers: { "content-type": "text/event-stream" }, body: `data: ${JSON.stringify({ type: "done", conv_id: convA, text: "ok" })}\n\n` });
      }
    });
    await attach([{ name: "duzy.txt", mimeType: "text/plain", buffer: Buffer.from("x".repeat(100)) }]);
    await page.getByTestId("chat-input").fill("Przeczytaj duży plik");
    await page.getByTestId("send-chat").click();
    await page.getByTestId("chat-usage-pending").waitFor();
    assert.equal(held.attachments.length, 1);
    assert.equal(await page.locator('[data-testid="attachment-chip"][data-name="duzy.txt"]').count(), 1, "the file is back as a chip");
    await page.getByTestId("chat-usage-pending-reject").click();
    assert.equal(await page.locator('[data-testid="attachment-chip"][data-name="duzy.txt"]').count(), 1, "a refusal loses no file");
    assert.equal(await page.getByTestId("chat-input").inputValue(), "Przeczytaj duży plik");
    await page.getByTestId("send-chat").click();
    await page.getByTestId("chat-usage-pending").waitFor();
    await page.getByTestId("chat-usage-pending-confirm").click();
    await until("confirmed send", async () => held.usage_confirmation?.approved === true);
    assert.equal(held.attachments?.length, 1, "the confirmed send carries the file");
    await until("chip gone", async () => (await page.getByTestId("attachment-chip").count()) === 0);
    await page.unroute("**/api/chat");
  });

  await step("regenerate sends the earlier message with its own files, and leaves what is being written in the composer", async () => {
    await page.reload({ waitUntil: "domcontentloaded" });
    await page.getByTestId("chat-shell").waitFor();
    await openConversation("Co jest w pliku");
    await page.locator('[data-testid="message"][data-role="assistant"]').first().waitFor();
    await page.getByTestId("chat-input").fill("szkic następnej wiadomości");
    const before = await assistants();
    const answer = page.locator('[data-testid="message"][data-role="assistant"]').first();
    await answer.click({ button: "right" }).catch(() => undefined);
    await answer.getByTestId("msg-regenerate").click({ force: true });
    await until("regenerated", async () => JSON.stringify((await last()).messages).includes("PAPUGA") && (await assistants()) >= before);
    assert.equal(await page.getByTestId("chat-input").inputValue(), "szkic następnej wiadomości");
  });

  await step("a conversation's own model: chosen in the menu, kept at its level, used by a send without a model, never the person's pick", async () => {
    await plus.click();
    await page.getByTestId("composer-menu-model").click();
    await page.locator('[data-testid="composer-model-option"][data-model="mock/third-model"]').click();
    await menu.waitFor({ state: "detached" });
    await until("third model kept", async () => (await settingsOf(convA)).default_model === "mock/third-model");
    const resolved = await (await api("POST", "/api/scopes/resolve", { key: "default_model", conversation_id: convA })).json();
    assert.equal(resolved.scope, "conversation");
    assert.equal(resolved.value, "mock/third-model");
    // The kernel reads the same chain when a request names no model (another client, the command line); it differs from the person's setting.
    await (await api("POST", "/api/chat", { message: "Bez modelu w prośbie", conv_id: convA })).text();
    assert.equal((await last()).model, "mock/third-model", "a send without a model uses the conversation's");
    const stored = await page.evaluate(() => Object.keys(localStorage).filter(k => k.startsWith("loom.chat-settings.")).map(k => JSON.parse(localStorage.getItem(k)).value.model));
    assert.ok(!stored.includes("mock/third-model"), `the conversation's model is not saved as the person's pick (${stored})`);
  });

  await step("conversation A (own model) then B (none): B shows and sends the person's pick; with no pick, the chain's value", async () => {
    await (await api("POST", "/api/conversations", { title: "Rozmowa B" })).text();
    await page.reload({ waitUntil: "domcontentloaded" });
    await page.getByTestId("chat-shell").waitFor();
    await openConversation("Co jest w pliku");
    await until("A shows its own", async () => (await picker.inputValue()) === "mock/third-model");
    await openConversation("Rozmowa B");
    await until("B shows the person's pick", async () => (await picker.inputValue()) === "mock/second-model");
    // (the first message titles the conversation: it keeps "Rozmowa B" in its name)
    await page.getByTestId("chat-input").fill("Rozmowa B: pierwsza");
    await page.getByTestId("send-chat").click();
    await until("B sent", async () => (await last()).messages?.at(-1)?.content === "Rozmowa B: pierwsza", 20000);
    assert.equal((await last()).model, "mock/second-model");
    // The person goes back to no pick of their own (a new chat, model domyślny): A still opens with its own; B sends nothing and the chain decides.
    await newChat();
    await picker.selectOption("");
    await openConversation("Co jest w pliku");
    await until("A again", async () => (await picker.inputValue()) === "mock/third-model");
    await openConversation("Rozmowa B");
    await until("B follows the chain", async () => (await picker.inputValue()) === "");
    await page.getByTestId("chat-input").fill("Druga w B");
    await page.getByTestId("send-chat").click();
    await until("B sent again", async () => (await last()).messages?.at(-1)?.content === "Druga w B", 20000);
    assert.equal((await last()).model, "mock/e2e-model", "the chain's value: the person's setting");
    assert.equal(await page.getByTestId("composer-model-note").count(), 0, "no problem was reported");
  });

  await step("agent mode: the message goes to the agent (agent.run); files do not go with it and Send says why", async () => {
    await plus.click();
    await page.getByTestId("composer-menu-tools").click();
    await page.getByTestId("composer-menu-agent").click();
    await page.keyboard.press("Escape");
    assert.match(await page.getByTestId("tool-chip-agent").innerText(), /Tryb agenta: włączone/);
    await attach([{ name: "plik.txt", mimeType: "text/plain", buffer: Buffer.from("x") }]);
    await page.getByTestId("chat-input").fill("uporządkuj moje notatki");
    assert.equal(await page.getByTestId("send-chat").isDisabled(), true);
    assert.match(await page.getByTestId("attachment-action-note").innerText(), /Pliki nie idą do „Tryb agenta”/);
    await page.getByTestId("attachment-remove").click();
    const conv = (await conversations()).find(c => (c.title ?? "").includes("Rozmowa B")).id;
    await page.getByTestId("send-chat").click();
    await until("the agent's message", async () => (await messagesOf(conv)).some(m => m.role === "user" && m.metadata?.composer_action === "agent.run"), 20000);
    await page.getByTestId("tool-chip-agent").locator("button").click();
    assert.equal(await page.getByTestId("tool-chip-agent").count(), 0);
  });

  await step("a message from a new chat never lands in an older conversation, nor writes its model there", async () => {
    const conv = (await conversations()).find(c => (c.title ?? "").includes("Rozmowa B")).id;
    // The agent's reply of the step before may still be on its way into B: let it land, so the count below is B's settled state.
    await until("B settled", async () => (await messagesOf(conv)).at(-1)?.role === "assistant", 20000);
    const before = (await messagesOf(conv)).length;
    await newChat();
    await picker.selectOption("mock/second-model");
    await page.getByTestId("chat-input").fill("Z nowej rozmowy");
    await page.getByTestId("send-chat").click();
    await until("sent", async () => (await last()).messages?.at(-1)?.content === "Z nowej rozmowy", 20000);
    await new Promise(r => setTimeout(r, 1000));
    // R35: a blank page asks for a new conversation (new_conversation), so the kernel never puts the message into the one it remembers.
    const inB = await messagesOf(conv);
    assert.equal(inB.length, before, "the older conversation gets no message from a new chat");
    assert.ok(!inB.some(m => m.text === "Z nowej rozmowy"), "the new chat's words are not in the older conversation");
    assert.equal((await settingsOf(conv)).default_model, undefined, "an older conversation keeps having no model of its own");
    // The conversation made by this send keeps the model it was made with.
    const fresh = (await conversations())[0].id;
    assert.notEqual(fresh, conv);
    await until("the new conversation's model", async () => (await settingsOf(fresh)).default_model === "mock/second-model");
  });

  await step("Import conversations is reachable from the menu", async () => {
    await plus.click();
    await page.getByTestId("composer-menu-import").click();
    await page.getByTestId("import-panel").waitFor();
    await png("05-import");
  });

} finally {
  await browser?.close();
  server.kill("SIGTERM");
  mock.kill("SIGTERM");
}
process.exit(failed ? 1 : 0);
