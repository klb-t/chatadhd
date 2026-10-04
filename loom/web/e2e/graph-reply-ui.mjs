#!/usr/bin/env node
// Offline real React component + HTTP contract double. Compiler output is the
// pinned W4 fixture from the Python graph reply compiler; no model calls.
import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { createServer } from "node:http";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import { chromium } from "playwright";
import ts from "typescript";

const root = fileURLToPath(new URL("../", import.meta.url));
const fixturePath = fileURLToPath(new URL("../../src/packet/tests/reply-fixtures.json", import.meta.url));
const fixtureCommit = "3caa6b4dcfb6412294bf816c1105bcf99afacdbc";
const fixtureFile = "loom/src/packet/tests/reply-fixtures.json";
const fixtureText = existsSync(fixturePath) ? readFileSync(fixturePath, "utf8") : execFileSync("git", ["show", `${fixtureCommit}:${fixtureFile}`], { cwd: root, encoding: "utf8" });
const fixtureDocument = JSON.parse(fixtureText);
assert.equal(fixtureDocument.reference_source_sha256, "50fffaffbf1abcbb2de1b626ae51f270a5842cd3e023c1dc3cee8011a401a903");
const fixture = fixtureDocument.cases[1];
assert.equal(JSON.parse(fixture.request.raw).schema, "loom.graph_reply/2");
assert.equal(JSON.parse(fixture.request.raw).nodes[0].text, null);
const inspectionSource = ts.transpileModule(readFileSync(new URL("../src/graph/reply-inspection.ts", import.meta.url), "utf8"), {
  compilerOptions: { module: ts.ModuleKind.ES2022, target: ts.ScriptTarget.ES2022 },
}).outputText;
const inspection = await import(`data:text/javascript;base64,${Buffer.from(inspectionSource).toString("base64")}`);
const groups = [];
function pass(name) { groups.push(name); console.log(`[graph-reply-ui] PASS ${name}`); }
const fragments = inspection.inspectReply(fixture.expected);
assert.deepEqual(fragments.map(fragment => fragment.text), ["Aą🙂B", "Aą🙂", "B"]);
assert.deepEqual(fragments.map(fragment => fragment.span.char_len), [4, 3, 1]);
assert.deepEqual(fragments.map(fragment => fragment.span.byte_len), [8, 7, 1]);
assert.ok(fragments.every(fragment => fragment.origin === "model" && fragment.content_verification === "unverified"));
assert.throws(() => inspection.inspectReply({ ...fixture.expected, spans: { bad: { char_start: 4, char_len: 3 } } }), /Invalid compiler/);
pass("Python fixture code-point spans preserve supplementary Unicode and model provenance");

function applyWithPython(policy) {
  const applied = spawnSync(process.env.PYTHON || "python3", ["-c", [
    "import json,sys", "sys.path.insert(0,sys.argv[1])", "from agentic_graph_v1.packet import apply_diff",
    "x=json.load(sys.stdin)", "packet,receipt=apply_diff(x['packet'],x['diff'],x['policy'],explicitly_accepted=True)",
    "print(json.dumps({'packet':packet,'receipt':receipt},ensure_ascii=False))",
  ].join("\n"), fileURLToPath(new URL("../../tools/structure", import.meta.url))], {
    input: JSON.stringify({ packet: fixture.request.packet, diff: fixture.expected.diff, policy }), encoding: "utf8", timeout: 15000,
  });
  assert.ifError(applied.error);
  assert.equal(applied.status, 0, applied.stderr);
  return JSON.parse(applied.stdout);
}
const bundle = await build({
  stdin: { contents: `
    import React from "react";
    import { createRoot } from "react-dom/client";
    import GraphReplyWorkbench from "./src/components/GraphReplyWorkbench";
    const fixture = ${JSON.stringify(fixture)};
    const root = createRoot(document.getElementById("root"));
    const request = (path, body) => fetch(path, { method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify(body) }).then(response => response.json());
    const packet = command => request("/api/packet?mode=" + (window.__mockMode || "normal"), command);
    const usagePolicy = command => request("/api/usage-policy", command);
    const address = (action, fragment) => { window.__address = { action, fragment }; };
    window.renderWorkbench = overrides => root.render(<GraphReplyWorkbench packet={packet} usagePolicy={usagePolicy}
      responseText={fixture.request.raw} requestId={fixture.request.host.request_id} turnId={fixture.request.host.turn_id}
      model={fixture.request.host.model} onAddressFragment={address} {...overrides} />);
    window.renderWorkbench({});
  `, resolveDir: root, loader: "tsx" },
  bundle: true, write: false, format: "iife", platform: "browser", outfile: "/tmp/loom-graph-reply-ui.js", jsx: "automatic",
});
const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
const css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";
const packetRequests = [], confirmations = [], confirmed = new Set(), serverErrors = [], delayed = [];
const server = createServer(async (req, res) => {
  const url = new URL(req.url, "http://localhost");
  if (req.method === "POST") {
    let bytes = "";
    for await (const chunk of req) bytes += chunk;
    try {
      const command = JSON.parse(bytes);
      res.setHeader("Content-Type", "application/json");
      if (url.pathname === "/api/usage-policy") {
        confirmations.push(command);
        assert.equal(command.action, "confirm");
        assert.equal(command.receipt_id, `usage_mock:${command.operation_id}`);
        if (command.approved) confirmed.add(command.operation_id);
        res.end(JSON.stringify({ authorized: command.approved, status: command.approved ? "allowed" : "denied" })); return;
      }
      assert.equal(url.pathname, "/api/packet");
      packetRequests.push(command);
      if (command.usage_estimate && !confirmed.has(command.usage_estimate.operation_id)) {
        const operationId = command.usage_estimate.operation_id;
        res.end(JSON.stringify({ executed: false, usage_decision: { status: "requires_confirmation", authorized: false, operation_id: operationId, receipt_id: `usage_mock:${operationId}`, estimate: command.usage_estimate, resources: { calls: { ratio: 10, requires_confirmation: true } } } })); return;
      }
      let result;
      if (command.operation === "make") result = fixture.request.packet;
      else if (command.operation === "compile_reply") {
        assert.deepEqual(command.packet, fixture.request.packet);
        if (command.raw !== fixture.request.raw) {
          const raw = Buffer.from(command.raw, "utf8");
          res.statusCode = 400; res.end(JSON.stringify({ error: { code: "invalid_argument", message: "graph_reply_first_invalid_response", raw_capture: { sha256: createHash("sha256").update(raw).digest("hex"), byte_len: raw.length, raw_base64: raw.toString("base64") } } })); return;
        }
        for (const field of ["request_id", "turn_id", "model"]) assert.equal(command.host[field], fixture.request.host[field]);
        result = url.searchParams.get("mode") === "malformed" ? { schema: "unexpected" } : fixture.expected;
      } else if (command.operation === "validate_compilation") {
        assert.deepEqual(command.packet, fixture.request.packet); assert.deepEqual(command.compilation, fixture.expected); result = fixture.expected;
      } else if (command.operation === "apply_compiled_reply") {
        assert.deepEqual(command.packet, fixture.request.packet); assert.deepEqual(command.compilation, fixture.expected);
        assert.equal(command.explicitly_accepted, true);
        result = applyWithPython(command.policy);
      } else throw new Error(`Unexpected operation: ${command.operation}`);
      const send = () => res.end(JSON.stringify(command.usage_estimate ? { executed: true, result, usage_decision: { authorized: true }, usage_settlement: { status: "completed" } } : result));
      if (url.searchParams.get("mode") === "delay") delayed.push(send); else send();
    } catch (failure) { serverErrors.push(failure); res.statusCode = 500; res.end(JSON.stringify({ error: { message: String(failure) } })); }
    return;
  }
  if (url.pathname === "/component.js") { res.setHeader("Content-Type", "text/javascript"); res.end(js); return; }
  if (url.pathname === "/component.css") { res.setHeader("Content-Type", "text/css"); res.end(css); return; }
  res.setHeader("Content-Type", "text/html");
  res.end('<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="stylesheet" href="/component.css"></head><body><div id="root"></div><script src="/component.js"></script></body></html>');
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
let browser;
try {
  browser = await chromium.launch({ headless: true, args: ["--no-sandbox"] });
  const page = await browser.newPage({ viewport: { width: 390, height: 844 } });
  const pageErrors = [];
  page.on("pageerror", failure => pageErrors.push(failure.message));
  const reload = async () => { await page.goto(`http://127.0.0.1:${server.address().port}/`); await page.getByTestId("graph-reply-workbench").waitFor(); };
  const seedPacket = async () => {
    await page.getByText("Base packet and host metadata", { exact: true }).click();
    await page.getByRole("button", { name: "Create empty packet", exact: true }).click();
    await page.waitForFunction(() => document.querySelector('[aria-label="Base GraphPacket JSON"]').value.includes('"packet_id"'));
  };
  const compile = async () => { await page.getByRole("button", { name: "Compile candidate", exact: true }).click(); await page.getByTestId("gr-rendered-response").waitFor(); };

  await reload();
  assert.equal(packetRequests.length, 0, "Showing the workbench dispatches no operation");
  await seedPacket();
  await compile();
  assert.equal(await page.getByTestId("gr-rendered-response").textContent(), "Aą🙂B");
  assert.equal(await page.getByTestId("gr-fragment").count(), 3);
  assert.equal(await page.getByTestId("gr-fragment").nth(1).locator("pre").first().textContent(), "Aą🙂");
  assert.equal(packetRequests.filter(request => request.operation === "apply_compiled_reply").length, 0);
  assert.equal(packetRequests.at(-1).raw, fixture.request.raw, "Wire whitespace preserved exactly");
  await page.getByTestId("gr-fragment").nth(1).getByRole("button", { name: "Expand this node", exact: true }).click();
  let address = await page.evaluate(() => window.__address);
  assert.equal(address.action, "expand"); assert.deepEqual(address.fragment, fragments[1]);
  await page.getByTestId("gr-fragment").nth(2).getByRole("button", { name: "Correct this node", exact: true }).click();
  address = await page.evaluate(() => window.__address);
  assert.equal(address.action, "correct"); assert.deepEqual(address.fragment, fragments[2]);
  await page.getByRole("button", { name: "Validate compilation", exact: true }).click();
  await page.waitForFunction(() => document.querySelector('[data-testid="gr-status"]').textContent.includes("replay validated"));
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, "Narrow viewport has no document overflow");
  pass("candidate compilation, exact raw wire, native replay validation, semantic fragment addresses and narrow layout");

  await page.getByLabel("Graph reply apply policy").selectOption("auto");
  assert.equal(packetRequests.filter(request => request.operation === "apply_compiled_reply").length, 0, "Selecting policy does not dispatch");
  await page.getByRole("button", { name: "Apply compiled reply", exact: true }).click();
  await page.getByTestId("gr-applied-result").waitFor();
  const applied = JSON.parse(await page.getByTestId("gr-applied-result").textContent());
  assert.equal(applied.receipt.canonical_store_written, false);
  assert.equal(applied.receipt.acceptance_establishes_content_truth, false);
  assert.equal(applied.receipt.policy.acceptance, "auto");
  assert.ok(applied.packet.entities.length > fixture.request.packet.entities.length);
  assert.equal(await page.getByRole("button", { name: "Apply compiled reply", exact: true }).isEnabled(), false);
  await page.getByLabel("Host metadata JSON").fill(JSON.stringify(fixture.request.host));
  assert.equal(await page.getByTestId("gr-rendered-response").count(), 0);
  assert.equal(await page.getByRole("button", { name: "Apply compiled reply", exact: true }).isEnabled(), false);
  pass("explicit apply returns actual Python algebra packet/receipt and any input edit invalidates prior compilation");

  await reload(); await seedPacket();
  const invalid = 'first invalid response\r\n<script>window.__ran=true</script> Żółć 🙂';
  await page.evaluate(raw => window.renderWorkbench({ responseText: raw }), invalid);
  await page.waitForFunction(() => document.querySelector('[aria-label="Raw model response"]').value.startsWith("first invalid"));
  await page.getByRole("button", { name: "Compile candidate", exact: true }).click();
  await page.getByTestId("gr-error").waitFor();
  assert.equal(packetRequests.at(-1).raw, invalid, "Initial state submits exact CRLF bytes despite textarea display normalization");
  await page.getByText("Original response", { exact: true }).click();
  assert.equal(await page.getByTestId("gr-original-response").textContent(), invalid);
  await page.getByText("Native response and accounting", { exact: true }).click();
  const failed = JSON.parse(await page.getByTestId("gr-native-response").textContent());
  assert.equal(failed.error.raw_capture.byte_len, Buffer.byteLength(invalid));
  assert.equal(Buffer.from(failed.error.raw_capture.raw_base64, "base64").toString("utf8"), invalid);
  assert.equal(await page.evaluate(() => window.__ran), undefined);
  assert.equal(await page.locator("script").count(), 1, "Raw model content stays inert");
  assert.equal(await page.getByRole("button", { name: "Apply compiled reply", exact: true }).isEnabled(), false);
  pass("first schema failure preserves original CRLF/Unicode text and server exact-byte capture as inert source");

  await reload(); await seedPacket();
  await page.getByText("Usage estimate", { exact: true }).click();
  await page.getByLabel("Request the shared usage policy").check();
  await page.getByRole("button", { name: "Compile candidate", exact: true }).click();
  await page.getByTestId("gr-usage-receipt").waitFor();
  const heldCommand = packetRequests.at(-1);
  assert.equal(await page.getByTestId("gr-rendered-response").count(), 0);
  await page.getByRole("button", { name: "Confirm increase and retry exact operation", exact: true }).click();
  await page.getByTestId("gr-rendered-response").waitFor();
  assert.deepEqual(packetRequests.at(-1), heldCommand);
  assert.equal(confirmations.at(-1).receipt_id, `usage_mock:${heldCommand.usage_estimate.operation_id}`);
  assert.equal(confirmations.at(-1).approved, true);
  pass("×10 hold binds explicit confirmation to its exact receipt and retries the identical command");

  await page.getByLabel("Raw model response").fill(fixture.request.raw + " ");
  await page.getByRole("button", { name: "Compile candidate", exact: true }).click();
  await page.getByTestId("gr-usage-receipt").waitFor();
  const beforeDecline = packetRequests.length;
  await page.getByRole("button", { name: "Decline increase", exact: true }).click();
  await page.waitForFunction(() => document.querySelector('[data-testid="gr-status"]').textContent.includes("declined"));
  assert.equal(packetRequests.length, beforeDecline);
  assert.equal(confirmations.at(-1).approved, false);
  pass("declined usage never retries or compiles the held operation");

  await reload(); await page.evaluate(() => window.renderWorkbench({ packet: undefined, usagePolicy: undefined }));
  await page.waitForFunction(() => document.body.textContent.includes("Native packet API is unavailable"));
  assert.equal(await page.getByRole("button", { name: "Compile candidate", exact: true }).isEnabled(), false);
  assert.equal(await page.getByLabel("Raw model response").inputValue(), fixture.request.raw);
  await page.evaluate(() => window.renderWorkbench({ packet: undefined, responseText: "replacement source" }));
  await page.waitForFunction(() => document.querySelector('[aria-label="Raw model response"]').value === "replacement source");
  pass("missing capability is visible while raw response and input changes remain available");

  await reload(); await seedPacket(); await page.evaluate(() => window.__mockMode = "malformed");
  await page.getByRole("button", { name: "Compile candidate", exact: true }).click();
  await page.getByTestId("gr-error").waitFor();
  assert.match(await page.getByTestId("gr-error").innerText(), /did not return a graph reply compilation/);
  assert.equal(await page.getByTestId("gr-fragment").count(), 0);
  assert.equal(await page.getByLabel("Raw model response").inputValue(), fixture.request.raw);
  pass("malformed native DTO fails visibly without replacing source text or crashing React");

  await reload(); await seedPacket(); await page.evaluate(() => window.__mockMode = "delay");
  const awaitingRequest = page.waitForRequest(request => request.url().includes("/api/packet?mode=delay"));
  await page.getByRole("button", { name: "Compile candidate", exact: true }).click();
  await awaitingRequest;
  await page.evaluate(() => window.renderWorkbench({ responseText: "new assistant response", turnId: "next-turn" }));
  await page.waitForFunction(() => document.querySelector('[aria-label="Raw model response"]').value === "new assistant response");
  // The server callback is registered before its incoming request completes.
  const deadline = Date.now() + 5000;
  while (!delayed.length && Date.now() < deadline) await new Promise(resolve => setTimeout(resolve, 10));
  assert.equal(delayed.length, 1);
  const responseFinished = page.waitForResponse(response => response.url().includes("/api/packet?mode=delay"));
  delayed.shift()(); await responseFinished;
  await page.getByRole("button", { name: "Compile candidate", exact: true }).waitFor({ state: "visible" });
  assert.equal(await page.getByTestId("gr-rendered-response").count(), 0);
  assert.equal(await page.getByLabel("Raw model response").inputValue(), "new assistant response");
  pass("late compilation cannot attach the preceding turn's graph to a replacement response");
  assert.deepEqual(pageErrors, []); assert.deepEqual(serverErrors, []);
  console.log(`[graph-reply-ui] ${groups.length}/${groups.length} groups passed; fixture compiler ${fixtureDocument.reference_source_sha256}; all endpoints synthetic/local`);
} finally {
  for (const send of delayed) send();
  await browser?.close();
  await new Promise(resolve => server.close(resolve));
}
