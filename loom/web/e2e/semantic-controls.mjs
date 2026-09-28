#!/usr/bin/env node
// Isolated browser contract test: all API calls are fulfilled in Playwright.
// No native server, secret, source-file import or model-provider call is used.
// Run after npm run build: node e2e/semantic-controls.mjs
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import path from "node:path";
import { preview } from "vite";
import { chromium } from "playwright";

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const server = await preview({ root: webRoot, preview: { host: "127.0.0.1", port: 0 } });
const address = server.httpServer.address();
const base = `http://127.0.0.1:${address.port}`;
let browser;
try {
  browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE ? { executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE } : {}) });
  const page = await browser.newPage({ viewport: { width: 1500, height: 1000 } });
  const calls = [], unexpected = [], pageErrors = [];
  let config = { semantic_model: "", semantic_analysis: true };
  const runs = [];
  const support = [{ observation: "obs_a", quote: "<b>Keep the scope.</b>", byte_start: 0, byte_len: 22,
    locator: { source: "/audit/source.txt", unit: "unit_a" }, observation_text_hash: "source_hash" }];
  const candidates = Array.from({ length: 26 }, (_, i) => ({
    id: `ca_${i}`, kind: "semantic_structure", status: "candidate", created: "2026-09-28",
    payload: { run_id: "kr_auto", group: { unit: "unit_a", source: "/audit/source.txt", branch: "b1" },
      proposal: { kind: "structure", unknowns: ["scope completeness"], claim: { subject: `draft_${i}`, predicate: "preserves", object: "scope", qualifiers: { scope: "chunk_a" }, assessment: { basis: { support }, premises: { claims: [] } } } },
      provenance: { model: "mock/cheap", prompt_hash: "prompt_hash", response_hash: "response_hash" } },
    support, eval: { grounding: "exact_observation_bytes", review: "pending", logical_semantics: "unvalidated", promoted: false },
  }));
  page.on("pageerror", (error) => pageErrors.push(error.message));
  await page.route("**/*", async (route) => {
    const request = route.request(), url = new URL(request.url());
    if (url.origin !== base) { unexpected.push(request.url()); await route.abort(); return; }
    if (!url.pathname.startsWith("/api/")) { await route.continue(); return; }
    const body = request.postData() ? request.postDataJSON() : undefined;
    calls.push({ path: url.pathname, method: request.method(), body });
    let result;
    if (url.pathname === "/api/config") result = config;
    else if (url.pathname === "/api/models" || url.pathname === "/api/conversations") result = [];
    else if (url.pathname === "/api/semantic/status") result = { pending: 0, processed: 0, paused: false, mode: "mock", rate: 0 };
    else if (url.pathname === "/api/knowledge/runs") result = runs;
    else if (url.pathname === "/api/knowledge/query") {
      if (body.what === "candidates") {
        const items = candidates.slice(body.offset, body.offset + body.limit);
        result = { run: body.run, items, total: candidates.length, limit: body.limit, offset: body.offset,
          has_more: body.offset + items.length < candidates.length, interpretation: "unpromoted_candidates_not_canonical_claims" };
      } else result = { run: body.run, items: [] };
    } else if (url.pathname === "/api/knowledge/run") {
      const on = body.llm === "auto", run = on ? "kr_auto" : "kr_off";
      const semantic = on ? { status: "partial", accepted: 2, rejected: 1, failed: 0, requests: 2, cache_hits: 0, input_bytes: 800,
        candidate_ids: ["ca_0", "ca_1"], rejections: [{ chunk: "chunk_a", reason: "quote_mismatch" }],
        omitted_observations: 1, skipped: [{ observation: "obs_omitted", reason: "input_budget" }] } : { status: "off", accepted: 0, rejected: 0, requests: 0, failed: 0, cache_hits: 0, input_bytes: 0, candidate_ids: [], rejections: [], skipped: [] };
      result = { status: "done", error: "", run, stages: [{ stage: "extract", cache_hit: on, stats: { semantic } }] };
      runs.unshift({ id: run, status: "done" });
    } else { unexpected.push(`${request.method()} ${url.pathname}`); result = { error: { code: "unexpected_mock_request", message: url.pathname } }; }
    await route.fulfill({ contentType: "application/json", body: JSON.stringify(result) });
  });
  await page.goto(base);
  await page.getByTestId("nav-knowledge").click();
  const workbench = page.getByTestId("knowledge-workbench");
  await page.getByRole("button", { name: "Hide chat", exact: true }).click();
  await workbench.locator(".kb-run-controls > summary").click();
  await workbench.getByLabel("Analysis source paths").fill("/audit/source.txt");
  const toggle = workbench.getByLabel("Use the configured semantic model for candidate proposals");
  assert.equal(await toggle.isChecked(), false);
  const analyze = workbench.getByRole("button", { name: "Analyze sources", exact: true });
  await analyze.click();
  await workbench.getByTestId("kb-semantic-result").getByText("Semantic proposals: off", { exact: false }).waitFor();
  const off = calls.find((call) => call.path === "/api/knowledge/run").body;
  assert.equal(off.llm, "off");
  assert.equal(off.stage_params.extract, undefined);

  await toggle.check();
  await workbench.getByText("No semantic model configured.", { exact: false }).waitFor();
  config = { semantic_model: "mock/cheap", semantic_analysis: true };
  await workbench.getByRole("button", { name: "Refresh model settings" }).click();
  await workbench.getByText("Semantic model: mock/cheap", { exact: true }).waitFor();
  await workbench.getByLabel("Maximum model requests", { exact: true }).fill("9");
  assert.equal(await analyze.isDisabled(), true, "out-of-range request count cannot submit");
  await workbench.getByLabel("Maximum model requests", { exact: true }).fill("2");
  await workbench.getByLabel("Maximum total input bytes", { exact: true }).fill("8192");
  await workbench.getByLabel("Maximum output tokens per request", { exact: true }).fill("512");
  await workbench.getByText("More semantic limits", { exact: true }).click();
  for (const [label, value] of [["Maximum observations per chunk", "6"], ["Maximum input bytes per chunk", "4096"],
    ["Maximum proposals per response", "8"], ["Maximum response bytes", "32768"], ["Request timeout in milliseconds", "5000"]]) {
    await workbench.getByLabel(label, { exact: true }).fill(value);
  }
  await analyze.click();
  const summary = workbench.getByTestId("kb-semantic-result");
  await summary.getByText("Semantic proposals: partial", { exact: false }).waitFor();
  const auto = calls.filter((call) => call.path === "/api/knowledge/run").at(-1).body;
  assert.equal(auto.llm, "auto");
  assert.deepEqual(auto.stage_params.extract.semantic, { max_requests: 2, max_observations: 6, max_chunk_bytes: 4096,
    max_input_bytes: 8192, max_output_tokens: 512, max_proposals: 8, max_response_bytes: 32768, timeout_ms: 5000 });
  assert.match(await summary.innerText(), /Accepted proposals: 2.*Stored candidate IDs: 2.*Rejected proposals: 1/s);
  assert.match(await summary.innerText(), /Skipped entries: 1/);
  assert.match(await summary.innerText(), /Omitted observations: 1/);
  assert.match(await summary.innerText(), /extract result reused/);
  await summary.getByText("Skipped inputs and reasons", { exact: true }).click();
  assert.match(await summary.innerText(), /input_budget/);

  await workbench.getByLabel("View type", { exact: true }).selectOption("candidates");
  await workbench.getByTestId("kb-add-view").click();
  const pane = workbench.getByTestId("kb-pane-candidates");
  await pane.getByText("25 shown · 26 matching candidates", { exact: true }).waitFor();
  await pane.locator(".kb-record").first().click();
  const inspector = workbench.getByTestId("kb-inspector");
  assert.equal(await inspector.locator("blockquote").innerText(), support[0].quote);
  assert.equal(await inspector.locator("blockquote b").count(), 0, "source markup remains plain text");
  assert.match(await inspector.innerText(), /Unpromoted candidate interpretation/);
  assert.match(await inspector.innerText(), /prompt_hash/);
  await pane.getByLabel("Next candidate page", { exact: true }).click();
  await pane.getByText("1 shown · 26 matching candidates", { exact: true }).waitFor();
  assert.equal(await pane.getByLabel("Next candidate page", { exact: true }).isDisabled(), true);
  assert.equal(await pane.getByLabel("Previous candidate page", { exact: true }).isEnabled(), true);
  const candidateQuery = calls.filter((call) => call.path === "/api/knowledge/query" && call.body.what === "candidates").at(-1).body;
  assert.deepEqual(candidateQuery, { run: "kr_auto", kind: "semantic_structure", limit: 25, offset: 25, what: "candidates" });
  assert.equal(await workbench.getByTestId("kb-pane-claims").count(), 1);
  assert.equal(await workbench.getByTestId("kb-pane-graph").count(), 1);
  assert.equal(calls.filter((call) => /judge|promot|chat/.test(call.path)).length, 0);
  assert.deepEqual(unexpected, []);
  assert.deepEqual(pageErrors, []);
  console.log("[semantic-controls] PASS: default off, explicit bounded auto, missing-model message, result diagnostics, candidate provenance/pagination, retained views; all API calls mocked");
} finally {
  if (browser) await browser.close();
  await new Promise((resolve) => server.httpServer.close(resolve));
}
