#!/usr/bin/env node
// P13: the real App against the real loom-server, in Chromium with fake media devices. The page declares its devices; the chat command
// "Nagraj notatkę głosową i ją streść" is proposed as record -> transcribe -> summarise; after "Run it" the run pauses on the recording
// step, the person records in the browser (MediaRecorder on the fake microphone), the audio is sent to the kernel, which transcribes it
// on a test double of the speech provider (the data of the media profile points at it), asks a test double of the model for the summary,
// and the conversation shows the result. Nothing leaves the machine.
//
// Prereqs: loom-server built (LOOM_BUILD_SERVER=ON) and `npm run build` in loom/web.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer as createHttpServer } from "node:http";
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import net from "node:net";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";
import { suiteCompletionGuard } from "./harness-lifecycle.mjs";

const web = fileURLToPath(new URL("../", import.meta.url));
const loom = path.resolve(web, "..");
const serverBin = process.env.LOOM_SERVER_BIN || path.join(loom, "build/dev/server/loom-server");
const dist = path.join(web, "dist");
assert.ok(existsSync(serverBin) && existsSync(path.join(dist, "index.html")), "Build the native server and the App first.");
const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";

const groups = [];
const complete = suiteCompletionGuard("client-devices-native", 5, groups);
const transcript = "We ship on Friday if the tests pass.";
const summary = "Ship Friday, tests permitting.";
const speech = [], chats = [], pageErrors = [];
let server, browser, serverLog = "";

function freePort() {
  return new Promise((resolve, reject) => {
    const probe = net.createServer();
    probe.once("error", reject);
    probe.listen(0, "127.0.0.1", () => { const { port } = probe.address(); probe.close(() => resolve(port)); });
  });
}
async function readBody(request) { const chunks = []; for await (const chunk of request) chunks.push(chunk); return Buffer.concat(chunks); }

// The test doubles: a speech-recognition provider (OpenAI-shaped transcriptions) and a chat model.
const fake = createHttpServer(async (request, response) => {
  const body = await readBody(request);
  if (request.url === "/audio/transcriptions") {
    speech.push({ bytes: body.length, type: request.headers["content-type"] ?? "", head: body.subarray(0, 4000).toString("latin1") });
    response.writeHead(200, { "Content-Type": "application/json" });
    response.end(JSON.stringify({ text: transcript, language: "en", duration: 1.5 }));
    return;
  }
  if (request.url === "/chat/completions") {
    chats.push(JSON.parse(body.toString()));
    response.writeHead(200, { "Content-Type": "application/json" });
    response.end(JSON.stringify({ choices: [{ message: { role: "assistant", content: summary } }], usage: { total_tokens: 9 } }));
    return;
  }
  response.writeHead(404).end();
});
await new Promise(resolve => fake.listen(0, "127.0.0.1", resolve));
const fakeBase = "http://127.0.0.1:" + fake.address().port;

const data = mkdtempSync(path.join(tmpdir(), "loom-client-devices-"));
mkdirSync(path.join(data, "profiles"), { recursive: true });
// The speech provider of the media profile is data: this overlay points it at the test double.
writeFileSync(path.join(data, "profiles/media.pack"), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain: "media",
  overrides: { providers: { groq: { endpoint: fakeBase + "/audio/transcriptions" } } } }));
writeFileSync(path.join(data, "profiles/worker.pack"), JSON.stringify({ schema: "loom.runtime_profile_overlay/1", domain: "worker", overrides: { startup_delay_ms: 600000 } }));
const port = await freePort();
const base = "http://127.0.0.1:" + port;

async function api(method, route, body) {
  const response = await fetch(base + route, { method, headers: { "Content-Type": "application/json" }, ...(body === undefined ? {} : { body: JSON.stringify(body) }) });
  return response.json();
}
async function check(name, run) { await run(); groups.push(name); console.log("[client-devices-native] PASS " + name); }
async function until(predicate, label, ms = 15000) {
  const deadline = Date.now() + ms;
  for (;;) {
    const value = await predicate();
    if (value) return value;
    if (Date.now() > deadline) throw new Error("Timed out waiting for " + label);
    await new Promise(resolve => setTimeout(resolve, 100));
  }
}

try {
  server = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", data, "--static-dir", dist], { cwd: loom, stdio: ["ignore", "pipe", "pipe"] });
  for (const out of [server.stdout, server.stderr]) out.on("data", chunk => { serverLog += chunk.toString(); });
  await until(async () => { try { return (await fetch(base + "/api/healthz")).ok; } catch { return false; } }, "the server");
  await api("PATCH", "/api/config", { base_url: fakeBase, default_model: "fake/model" });
  await api("POST", "/api/secrets/api_key", { value: "sk-test" });
  await api("POST", "/api/secrets/groq_api_key", { value: "gk-test" });

  browser = await chromium.launch({ headless: true, executablePath,
    args: ["--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream", "--autoplay-policy=no-user-gesture-required"] });
  const context = await browser.newContext({ viewport: { width: 1280, height: 900 }, permissions: ["microphone"] });
  const page = await context.newPage();
  page.on("pageerror", error => pageErrors.push(error.message));
  await page.goto(base, { waitUntil: "domcontentloaded" });
  await page.getByTestId("chat-view").waitFor();
  await page.click('[data-testid="new-conversation"]');
  await page.waitForSelector('[data-testid="conv-item"].active');

  await check("opening the page declares nothing: viewing is not a reason to tell the kernel about devices", async () => {
    await page.waitForTimeout(1500);  // longer than a background timer of the old design would have waited
    const clients = (await api("POST", "/api/client/capabilities", {})).clients;
    assert.deepEqual(clients, [], "no client is known before the person starts something");
  });

  await check("a chat sentence with two actions is proposed as record -> transcribe -> summarise, with what runs on the device said", async () => {
    await page.fill('[data-testid="chat-input"]', "Nagraj notatkę głosową i ją streść");
    await page.click('[data-testid="send-chat"]');
    const proposal = page.getByTestId("chat-action-proposal");
    await proposal.waitFor();
    const message = page.locator('[data-testid="message"][data-role="assistant"]').last();
    const text = await message.locator(".body").innerText();
    assert.match(text, /Na Twoim urządzeniu/);
    assert.match(text, /Nagraj dźwięk/);
    assert.match(await proposal.getByTestId("chat-action-proposal-plan").innerText(), /Nagraj dźwięk.*Zapisz nagranie jako tekst.*streść/is);
    assert.equal(speech.length, 0, "nothing is sent while the person has not said yes");
    assert.equal(chats.length, 0);
    // the declaration was made right before that send, for this client only
    const known = (await api("POST", "/api/client/capabilities", {})).clients;
    assert.equal(known.length, 1);
    const declared = await api("POST", "/api/client/capabilities", { client_id: known[0].client_id });
    assert.match(declared.client_id, /^web-[0-9a-f]{12}$/);
    assert.equal(declared.client.kind, "web");
    assert.equal(declared.capabilities.captures_audio.runnable, true, "the fake microphone is there");
    assert.equal(declared.capabilities.captures_audio.via, "MediaRecorder");
    assert.ok(declared.runnable_actions.includes("device.record_audio"));
    assert.ok(declared.runnable_actions.includes("device.pick_file"));
    // as another client (or the server side) the recording is still planned, with its reason
    const server_side = await api("POST", "/api/actions/for", { element: "device:microphone" });
    const planned = server_side.actions.find(row => row.action === "device.record_audio");
    assert.equal(planned.callable, false);
    assert.ok(planned.planned_reason.en);
    const mine = await api("POST", "/api/actions/for", { element: "device:microphone", client_id: declared.client_id });
    assert.equal(mine.actions.find(row => row.action === "device.record_audio").runs_on, "client");
  });

  await check("after yes the run waits for this device; the person records, stops, and the answer goes to the kernel", async () => {
    await page.getByTestId("chat-action-proposal-confirm").click();
    const step = page.getByTestId("chat-client-step");
    await step.waitFor();
    assert.equal(await step.getAttribute("data-action"), "device.record_audio");
    assert.match(await step.getByTestId("chat-client-step-prompt").innerText(), /microphone/i);
    assert.equal(speech.length, 0, "the kernel waits for the device");
    await step.getByTestId("chat-client-step-start").click();
    await step.getByTestId("chat-client-step-recording").waitFor();
    assert.equal(await step.getAttribute("data-recording"), "true");
    await page.waitForTimeout(1500);  // the fake microphone has something to record
    await step.getByTestId("chat-client-step-stop").click();
    await until(() => speech.length === 1, "the transcription request");
  });

  await check("the kernel sent the recording to the speech provider, the transcript to the model, and the conversation shows the summary", async () => {
    const heard = speech[0];
    assert.ok(heard.bytes > 1000, "the recording of the fake microphone is audio, not a stub (" + heard.bytes + " bytes)");
    assert.match(heard.type, /multipart\/form-data/);
    assert.match(heard.head, /filename="audio\.webm"/);
    await until(() => chats.length === 1, "the summary request");
    assert.ok(JSON.stringify(chats[0].messages).includes(transcript), "the model is asked to condense the transcript");
    await page.locator('[data-testid="message"][data-role="assistant"] .body', { hasText: summary }).first().waitFor();
    await page.getByTestId("chat-client-step").waitFor({ state: "detached" });
    assert.equal(await page.getByTestId("chat-error").count(), 0);
  });

  await check("the recording, the transcript and the summary are in the graph, derived from the microphone and produced by the run", async () => {
    const rows = async route => { const listed = await api("GET", route); return Array.isArray(listed) ? listed : listed.nodes ?? listed.edges ?? []; };
    const forms = await rows("/api/graph/nodes?kind=audio_form");
    assert.equal(forms.length, 1, "one recording");
    const edges = await rows("/api/graph/edges?node_id=" + forms[0].id);
    const kinds = new Set(edges.map(edge => edge.link_type));
    for (const wanted of ["derived_from", "produced_by", "expresses", "stored_as"]) assert.ok(kinds.has(wanted), "the recording has a " + wanted + " link");
    const devices = await rows("/api/graph/nodes?kind=microphone");
    assert.equal(devices.length, 1);
    assert.ok(edges.some(edge => edge.link_type === "derived_from" && (edge.dst === devices[0].id || edge.target === devices[0].id)), "derived from the microphone node");
    const texts = await rows("/api/graph/nodes?kind=text_form");
    assert.ok(texts.some(node => (node.content ?? "").includes(summary)), "the summary is a text form");
  });
  assert.deepEqual(pageErrors, []);
  complete.complete();
  console.log("[client-devices-native] " + groups.length + "/5 groups passed");
} catch (error) {
  console.error(serverLog.split("\n").slice(-30).join("\n"));
  throw error;
} finally {
  await browser?.close();
  if (server && server.exitCode === null) { server.kill("SIGTERM"); await new Promise(resolve => server.once("exit", resolve)); }
  await new Promise(resolve => fake.close(resolve));
  rmSync(data, { recursive: true, force: true });
}
