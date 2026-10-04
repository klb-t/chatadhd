// Real React resource forms; fake HTTP only, with explicit native-refusal fixture.
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import { chromium } from "playwright";

const root = fileURLToPath(new URL("../", import.meta.url));
const contextCalls = [], semanticCalls = [];
const bundle = await build({
  stdin: { contents: `
    import React from "react";
    import { createRoot } from "react-dom/client";
    import ContextSlider from "./src/components/ContextSlider";
    import KnowledgeWorkbench from "./src/components/KnowledgeWorkbench";
    import { api } from "./src/api";
    async function post(action, value) {
      const response = await fetch("/fixture", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ action, value }) });
      const result = await response.json(); if (!response.ok) throw new Error(result.error.message); return result;
    }
    Object.assign(api, {
      selectContext: request => post("context", request),
      getConfig: async () => ({ semantic_model: "offline-fixture", semantic_analysis: true }),
      knowledge: { listRuns: async () => [{ id: "run-fixture", status: "done" }],
        query: async (_what, query) => ({ run: query.run, items: [] }),
        run: request => post("semantic", request), cancel: async () => ({}) }
    });
    createRoot(document.getElementById("root")).render(<><ContextSlider convId="conversation-fixture" /><KnowledgeWorkbench onClose={() => {}} /></>);
  `, resolveDir: root, loader: "tsx" },
  bundle: true, write: false, format: "iife", platform: "browser", outfile: "/tmp/resource-controls.js", jsx: "automatic",
});
const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
const css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";
const server = createServer(async (request, response) => {
  if (request.url === "/component.js") { response.setHeader("Content-Type", "text/javascript"); response.end(js); return; }
  if (request.url === "/component.css") { response.setHeader("Content-Type", "text/css"); response.end(css); return; }
  if (request.url === "/fixture") {
    const chunks = []; for await (const chunk of request) chunks.push(chunk);
    const body = JSON.parse(Buffer.concat(chunks).toString()); response.setHeader("Content-Type", "application/json");
    if (body.action === "context") { contextCalls.push(body.value); response.end(JSON.stringify({ items: [], token_estimate: 0, truncated: false, prompt_text: `Preview ${contextCalls.length}: depth ${body.value.depth}, budget ${body.value.max_tokens}` })); return; }
    semanticCalls.push(body.value); response.statusCode = 422;
    response.end(JSON.stringify({ error: { message: "Native fixture refusal: semantic limit outside supported bounds: max_requests" } })); return;
  }
  response.setHeader("Content-Type", "text/html"); response.end('<!doctype html><html><head><link rel="stylesheet" href="/component.css"></head><body><div id="root"></div><script src="/component.js"></script></body></html>');
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
let browser, groups = 0;
try {
  browser = await chromium.launch({ headless: true, args: ["--no-sandbox", "--disable-dev-shm-usage"] });
  const page = await browser.newPage(); const errors = [], outside = [];
  page.on("pageerror", error => errors.push(error.message));
  page.on("request", request => { if (!request.url().startsWith("http://127.0.0.1:")) outside.push(request.url()); });
  await page.goto(`http://127.0.0.1:${server.address().port}`);
  const context = page.getByTestId("context-slider"), workbench = page.getByTestId("knowledge-workbench");
  await context.getByTestId("context-preview").waitFor();
  assert.deepEqual(contextCalls[0], { text: "What have we discussed so far?", depth: 2, max_tokens: 2000, conv_id: "conversation-fixture" });
  assert.equal(await context.getByTestId("context-auto-preview").isChecked(), true);
  assert.equal(await context.getByLabel("Context preview delay in milliseconds").inputValue(), "300");
  assert.equal(await context.getByTestId("context-depth").getAttribute("max"), "4");
  assert.equal(await context.getByTestId("context-tokens").getAttribute("max"), "16000");
  await workbench.locator(".kb-run-controls > summary").click();
  const useModel = workbench.getByLabel("Use the configured semantic model for candidate proposals");
  assert.equal(await useModel.isChecked(), false);
  await useModel.check();
  const original = { max_requests: 4, max_observations: 16, max_chunk_bytes: 16000, max_input_bytes: 64000, max_output_tokens: 1600, max_proposals: 16, max_response_bytes: 128000, timeout_ms: 30000 };
  assert.deepEqual(await page.evaluate(() => JSON.parse(localStorage.getItem("loom.semantic-analysis.budget.v1"))), original);
  groups++;

  await context.getByTestId("context-auto-preview").uncheck();
  const countBeforeManual = contextCalls.length;
  await context.getByLabel("Probe text", { exact: true }).fill("Owner-selected probe");
  await context.getByTestId("context-depth-number").fill("9");
  await context.getByTestId("context-max_tokens-number").fill("64000");
  await context.getByLabel("Context preview delay in milliseconds").fill("450");
  assert.equal(await context.getByTestId("context-preview-build").isEnabled(), true);
  assert.equal(contextCalls.length, countBeforeManual, "manual edits do not dispatch preview");
  await context.getByTestId("context-preview-build").click();
  await page.waitForFunction(() => document.querySelector('[data-testid="context-preview"]')?.textContent.includes("depth 9, budget 64000"));
  assert.deepEqual(contextCalls.at(-1), { text: "Owner-selected probe", depth: 9, max_tokens: 64000, conv_id: "conversation-fixture" });
  await context.getByTestId("context-depth-number").fill("1.5");
  assert.equal(await context.getByTestId("context-preview-build").isDisabled(), true);
  await context.getByTestId("context-depth-number").fill("2147483648");
  assert.equal(await context.getByTestId("context-preview-build").isDisabled(), true);
  await context.getByTestId("context-depth-number").fill("9");
  await page.reload();
  assert.equal(await context.getByTestId("context-depth-number").inputValue(), "9");
  assert.equal(await context.getByTestId("context-max_tokens-number").inputValue(), "64000");
  assert.equal(await context.getByTestId("context-auto-preview").isChecked(), false);
  assert.equal(await context.getByLabel("Context preview delay in milliseconds").inputValue(), "450");
  assert.equal(contextCalls.length, countBeforeManual + 1, "restored manual mode never auto-previews");
  groups++;

  await workbench.locator(".kb-run-controls > summary").click(); await useModel.check();
  await workbench.getByLabel("Analysis source paths").fill("/fixture/public-source.txt");
  await workbench.getByText("More semantic limits", { exact: true }).click();
  const chosen = { max_requests: 9, max_input_bytes: 300000, max_output_tokens: 8192, max_observations: 128,
    max_chunk_bytes: 128000, max_proposals: 128, max_response_bytes: 512000, timeout_ms: 120000 };
  const fields = [["Maximum model requests", "max_requests"], ["Maximum total input bytes", "max_input_bytes"],
    ["Maximum output tokens per request", "max_output_tokens"], ["Maximum observations per chunk", "max_observations"],
    ["Maximum input bytes per chunk", "max_chunk_bytes"], ["Maximum proposals per response", "max_proposals"],
    ["Maximum response bytes", "max_response_bytes"], ["Request timeout in milliseconds", "timeout_ms"]];
  for (const [label, key] of fields) await workbench.getByLabel(label, { exact: true }).fill(String(chosen[key]));
  const analyze = workbench.getByRole("button", { name: "Analyze sources", exact: true });
  assert.equal(await analyze.isEnabled(), true);
  await analyze.click();
  await workbench.getByRole("alert").filter({ hasText: "Native fixture refusal" }).waitFor();
  assert.deepEqual(semanticCalls.at(-1).stage_params.extract.semantic, { ...chosen, representation: "relation_v1" });
  assert.deepEqual(await page.evaluate(() => JSON.parse(localStorage.getItem("loom.semantic-analysis.budget.v1"))), chosen);
  await workbench.getByLabel("Maximum model requests", { exact: true }).fill("-1");
  assert.equal(await analyze.isDisabled(), true);
  assert.deepEqual(await page.evaluate(() => JSON.parse(localStorage.getItem("loom.semantic-analysis.budget.v1"))), chosen, "invalid draft never replaces stored chosen parameters");
  await page.reload(); await workbench.locator(".kb-run-controls > summary").click();
  assert.equal(await useModel.isChecked(), false, "reload does not opt into provider calls"); await useModel.check();
  assert.equal(await workbench.getByLabel("Maximum model requests", { exact: true }).inputValue(), "9");
  assert.equal(await workbench.getByLabel("Maximum output tokens per request", { exact: true }).inputValue(), "8192");
  groups++;

  const editor = context.locator(".resource-preset-editor");
  await editor.locator("summary").first().click();
  const overlay = { context_preview: { sliders: { max_tokens: { max: 96000 } }, defaults: { auto_preview: false, debounce_ms: 750 } }, semantic_analysis: { defaults: { max_output_tokens: 12000 } } };
  await editor.getByLabel("Resource preset override JSON").fill(JSON.stringify(overlay));
  await editor.getByRole("button", { name: "Save browser preset override" }).click();
  await editor.getByRole("status").waitFor();
  assert.equal(await context.getByTestId("context-tokens").getAttribute("max"), "96000");
  assert.equal(await context.getByTestId("context-max_tokens-number").inputValue(), "64000", "new defaults do not erase chosen parameters");
  const invalidOverlay = '{"context_preview":{"sliders":{"depth":{"max":-1}}}}';
  await editor.getByLabel("Resource preset override JSON").fill(invalidOverlay);
  await editor.getByRole("button", { name: "Save browser preset override" }).click();
  await editor.getByRole("alert").waitFor();
  assert.equal(await editor.getByLabel("Resource preset override JSON").inputValue(), invalidOverlay);
  assert.deepEqual(await page.evaluate(() => JSON.parse(localStorage.getItem("loom.resource-controls.presets.v1"))), overlay);
  await page.reload();
  assert.equal(await context.getByTestId("context-tokens").getAttribute("max"), "96000");
  assert.equal(await context.getByTestId("context-depth-number").inputValue(), "9");
  await page.evaluate(() => { localStorage.removeItem("loom.context-preview.parameters.v1"); localStorage.removeItem("loom.semantic-analysis.budget.v1"); });
  await page.reload();
  assert.equal(await context.getByTestId("context-auto-preview").isChecked(), false);
  assert.equal(await context.getByLabel("Context preview delay in milliseconds").inputValue(), "750");
  await workbench.locator(".kb-run-controls > summary").click(); await useModel.check();
  assert.equal(await workbench.getByLabel("Maximum output tokens per request", { exact: true }).inputValue(), "12000");
  groups++;

  assert.deepEqual(errors, []); assert.deepEqual(outside, []);
  console.log(`[resource-controls] ${groups}/4 groups passed: identical defaults, >old slider ranges + manual/persisted preview, >all old semantic ceilings + explicit native refusal + persistence, generic persisted preset overlay`);
} finally { await browser?.close(); await new Promise(resolve => server.close(resolve)); }
