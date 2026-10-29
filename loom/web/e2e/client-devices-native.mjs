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
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import net from "node:net";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";
import { suiteCompletionGuard } from "./harness-lifecycle.mjs";
import { makeTmpDir } from "./tmp-dirs.mjs";

const web = fileURLToPath(new URL("../", import.meta.url));
const loom = path.resolve(web, "..");
const serverBin = process.env.LOOM_SERVER_BIN || path.join(loom, "build/dev/server/loom-server");
const dist = path.join(web, "dist");
assert.ok(existsSync(serverBin) && existsSync(path.join(dist, "index.html")), "Build the native server and the App first.");
const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || "/opt/pw-browsers/chromium-1194/chrome-linux/chrome";

const groups = [];
const complete = suiteCompletionGuard("client-devices-native", 8, groups);
const transcript = "We ship on Friday if the tests pass.";
const summary = "Ship Friday, tests permitting.";
const speech = [], chats = [], pageErrors = [], messageReads = [], eventFeeds = [], bytesCalls = [], uploadAnswers = [];
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

const data = makeTmpDir("loom-client-devices-");
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
  page.setDefaultTimeout(20000);
  page.on("pageerror", error => pageErrors.push(error.message));
  page.on("response", async response => {
    if (new URL(response.url()).pathname === "/api/client/blob/upload") uploadAnswers.push({ status: response.status(), body: await response.json() });
  });
  page.on("request", request => {
    const url = new URL(request.url());
    if (/\/api\/conversations\/[^/]+\/(messages|view)$/.test(url.pathname)) messageReads.push(url.pathname);
    if (url.pathname === "/api/events/stream") eventFeeds.push(url.search);
    if (url.pathname === "/api/client/blob/upload" || url.pathname === "/api/actions/client_result" || url.pathname.startsWith("/api/client/blob"))
      bytesCalls.push({ path: url.pathname, method: request.method(), type: request.headers()["content-type"] ?? "", request,
        body: url.pathname === "/api/actions/client_result" ? request.postData() ?? "" : "" });
  });
  await page.goto((base).replace(/\/?$/, "/#warsztat"), { waitUntil: "domcontentloaded" });
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
    assert.match(await step.getByTestId("chat-client-step-prompt").innerText(), /mikrofon/i, "the conversation is Polish, so the step is asked in Polish (one language per answer)");
    assert.equal(speech.length, 0, "the kernel waits for the device");
    await step.getByTestId("chat-client-step-start").click();
    await step.getByTestId("chat-client-step-recording").waitFor();
    assert.equal(await step.getAttribute("data-recording"), "true");
    await page.waitForTimeout(1500);  // the fake microphone has something to record
    await step.getByTestId("chat-client-step-stop").click();
    await until(() => speech.length === 1, "the transcription request");
  });

  await check("the recording crossed as a multipart upload; the answer to the kernel names it by hash and carries no bytes", async () => {
    const uploaded = bytesCalls.filter(call => call.path === "/api/client/blob/upload");
    assert.equal(uploaded.length, 1, "one upload for one recording");
    assert.equal(uploaded[0].method, "POST");
    assert.match(uploaded[0].type, /^multipart\/form-data; boundary=/);
    await until(() => uploadAnswers.length === 1, "the answer to the upload");
    assert.equal(uploadAnswers[0].status, 200);
    assert.ok(uploadAnswers[0].body.size > 1000, "the upload is the audio itself (" + uploadAnswers[0].body.size + " bytes)");
    assert.match(uploadAnswers[0].body.mime, /^audio\/webm/);
    const answers = bytesCalls.filter(call => call.path === "/api/actions/client_result");
    assert.equal(answers.length, 1);
    const answer = JSON.parse(answers[0].body);
    assert.equal(answer.state, "ok");
    assert.equal(answer.parts.length, 1);
    assert.deepEqual(Object.keys(answer.parts[0]).sort(), ["blob_hash", "kind", "mime"], "a part of the answer is a kind, a type and a hash");
    assert.equal(answer.parts[0].kind, "audio");
    assert.match(answer.parts[0].blob_hash, /^[0-9a-f]{64}$/);
    assert.ok(answers[0].body.length < 600, "the answer is small, there are no bytes in it (" + answers[0].body.length + " characters)");
    assert.ok(!answers[0].body.includes("base64"));
    // the kernel holds exactly the bytes that were uploaded, and gives them back as a stream
    const raw = await fetch(base + "/api/client/blob/" + answer.parts[0].blob_hash + "/raw");
    assert.equal(raw.status, 200);
    assert.match(raw.headers.get("content-type"), /^audio\/webm/);
    const stored = Buffer.from(await raw.arrayBuffer());
    assert.equal(stored.length, uploadAnswers[0].body.size, "the kernel gives back as many bytes as it was given");
    assert.equal(answer.parts[0].blob_hash, uploadAnswers[0].body.hash, "the answer names the blob that was uploaded");
    assert.equal(createHash("sha256").update(stored).digest("hex"), answer.parts[0].blob_hash, "and the hash is the sha-256 of the bytes");
    assert.equal((await fetch(base + "/api/client/blob/" + "0".repeat(64) + "/raw")).status, 404);
    assert.equal((await fetch(base + "/api/client/blob", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ hash: answer.parts[0].blob_hash }) })).status, 404, "the base64 route is gone");
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

  await check("a step started elsewhere reaches the open page by an event of the kernel - no refresh by hand, no polling; its closing too", async () => {
    assert.equal(eventFeeds.length, 1, "one event feed for the whole life of the page");
    assert.match(decodeURIComponent(eventFeeds[0]), /event=action:client_step/);
    await page.fill('[data-testid="chat-input"]', "Nagraj notatkę głosową i ją streść");
    await page.click('[data-testid="send-chat"]');
    await page.getByTestId("chat-action-proposal").last().waitFor();
    assert.equal(await page.getByTestId("chat-client-step").count(), 0, "nothing asks the device yet");
    // the proposal of this page's conversation, answered from outside the page (another tab, another client, the CLI)
    const conversations = await api("GET", "/api/conversations");
    const conv = conversations.find(row => row.id).id;
    const messages = await api("GET", "/api/conversations/" + conv + "/messages");
    const proposal = [...messages].reverse().find(row => row.metadata?.action_proposal?.status === "pending").metadata.action_proposal;
    assert.match(proposal.run_id, /^ar_/);
    await page.waitForTimeout(1200);  // the page's own reads after its send are over
    const sentBefore = chats.length;
    const readsBefore = messageReads.length;
    const answered = await api("POST", "/api/actions/confirm", { run_id: proposal.run_id, approved: true, receipt_id: proposal.receipt_id, ref: "cli" });
    assert.equal(answered.status, "awaiting_client");
    // the block appears in the open page; the page asked for nothing but the messages the event told it to read
    const step = page.getByTestId("chat-client-step");
    await step.waitFor();
    assert.equal(await step.getAttribute("data-action"), "device.record_audio");
    assert.ok(messageReads.length > readsBefore, "the page read the messages again when the event came");
    await page.waitForTimeout(1500);
    const settled = messageReads.length;
    await page.waitForTimeout(1500);
    assert.equal(messageReads.length, settled, "no polling: nothing is read while nothing happens");
    assert.equal(eventFeeds.length, 1);
    assert.equal(chats.length, sentBefore, "nothing went to the model");
    // cancelled elsewhere: the question goes from the page the same way
    const cancelled = await api("POST", "/api/actions/cancel", { run_id: proposal.run_id, ref: "cli" });
    assert.equal(cancelled.status, "cancelled");
    await step.waitFor({ state: "detached" });
    assert.equal(speech.length, 1, "no second recording was ever sent");
  });
  await check("\"przeanalizuj ten folder\": the browser picks a folder, its files go up one by one, the answer is a manifest, and the archive analysis runs on them", async () => {
    // a folder on the machine of the browser: two files, one in a subfolder, and an empty one
    const picked = path.join(makeTmpDir("loom-pick-folder-"), "notes");
    mkdirSync(path.join(picked, "sub"), { recursive: true });
    writeFileSync(path.join(picked, "plan.md"), "# Atlas plan\n\nWe decided to ship the Atlas release on Friday.\n");
    writeFileSync(path.join(picked, "sub", "ideas.txt"), "Idea: build the Atlas importer first.\nBug: the importer drops accents.\n");
    writeFileSync(path.join(picked, "empty.txt"), "");
    const uploadsBefore = bytesCalls.filter(call => call.path === "/api/client/blob/upload").length;
    const answersBefore = bytesCalls.filter(call => call.path === "/api/actions/client_result").length;
    await page.fill('[data-testid="chat-input"]', "przeanalizuj ten folder");
    await page.click('[data-testid="send-chat"]');
    const proposal = page.getByTestId("chat-action-proposal").last();
    await proposal.waitFor();
    const said = await page.locator('[data-testid="message"][data-role="assistant"]').last().locator(".body").innerText();
    assert.match(said, /Na Twoim urządzeniu: Wybierz folder/, "the proposal says the device will be asked for a folder");
    await proposal.getByTestId("chat-action-proposal-confirm").click();
    const step = page.getByTestId("chat-client-step");
    await step.waitFor();
    assert.equal(await step.getAttribute("data-action"), "device.pick_folder");
    assert.match(await step.getByTestId("chat-client-step-prompt").innerText(), /nie do żadnego modelu/, "the person is told the files go to this program, not to a model");
    assert.equal(await step.getByTestId("chat-client-step-start").innerText(), "Wybierz folder");
    // the folder chooser opens on the click; the test answers it with the folder
    const [chooser] = await Promise.all([page.waitForEvent("filechooser"), step.getByTestId("chat-client-step-start").click()]);
    assert.equal(chooser.isMultiple(), true);
    await chooser.setFiles(picked);
    await step.waitFor({ state: "detached" });
    // one upload per file, then one small answer: a manifest, no bytes
    const uploaded = bytesCalls.filter(call => call.path === "/api/client/blob/upload").length - uploadsBefore;
    assert.equal(uploaded, 3, "every file of the folder was uploaded (the empty one too)");
    const answers = bytesCalls.filter(call => call.path === "/api/actions/client_result").slice(answersBefore);
    assert.equal(answers.length, 1);
    const answer = JSON.parse(answers[0].body);
    assert.equal(answer.state, "ok");
    assert.equal(answer.parts.length, 1);
    assert.equal(answer.parts[0].kind, "json");
    const manifest = answer.parts[0].value;
    assert.equal(manifest.schema, "loom.folder_manifest/1");
    assert.equal(manifest.name, "notes");
    assert.deepEqual(manifest.files.map(row => row.path).sort(), ["empty.txt", "plan.md", "sub/ideas.txt"]);
    for (const row of manifest.files) assert.match(row.blob_hash, /^[0-9a-f]{64}$/);
    assert.ok(!answers[0].body.includes("base64"));
    // the kernel wrote the files back as a folder and the pipeline was started on it: the conversation says so
    await page.locator('[data-testid="message"][data-role="assistant"] .body', { hasText: /analiz/i }).last().waitFor();
    const uploadsDir = path.join(data, "tmp", "uploads");
    const written = readdirSync(uploadsDir).filter(name => name.startsWith("folder_"));
    assert.equal(written.length, 1);
    const root = path.join(uploadsDir, written[0], "notes");
    assert.equal(readFileSync(path.join(root, "plan.md"), "utf8"), "# Atlas plan\n\nWe decided to ship the Atlas release on Friday.\n");
    assert.equal(readFileSync(path.join(root, "sub", "ideas.txt"), "utf8"), "Idea: build the Atlas importer first.\nBug: the importer drops accents.\n");
    assert.equal(readFileSync(path.join(root, "empty.txt"), "utf8"), "");
    assert.equal(chats.length, 1, "no model was asked: the analysis is local");
    assert.equal(await page.getByTestId("chat-error").count(), 0);
    rmSync(path.dirname(picked), { recursive: true, force: true });
  });
  assert.deepEqual(pageErrors, []);
  complete.complete();
  console.log("[client-devices-native] " + groups.length + "/8 groups passed");
} catch (error) {
  console.error(serverLog.split("\n").slice(-30).join("\n"));
  console.error(error?.stack ?? String(error));
  throw error;
} finally {
  await browser?.close();
  if (server && server.exitCode === null) { server.kill("SIGTERM"); await new Promise(resolve => server.once("exit", resolve)); }
  await new Promise(resolve => fake.close(resolve));
  rmSync(data, { recursive: true, force: true });
}
