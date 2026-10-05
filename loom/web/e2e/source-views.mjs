// Public native fixture, real React renderers and native-ID branch topology. Entirely offline.
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import ts from "typescript";
import { build } from "esbuild";
import { chromium } from "playwright";

const root = fileURLToPath(new URL("../", import.meta.url));
const projection = readFileSync(new URL("../src/content/imported-message.ts", import.meta.url), "utf8");
const jsProjection = ts.transpileModule(projection, { compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022 } }).outputText;
const projectionApi = await import(`data:text/javascript;base64,${Buffer.from(jsProjection).toString("base64")}`);
const fixture = JSON.parse(readFileSync(new URL("./fixtures/imported-message-content.json", import.meta.url), "utf8"));
let groups = 0;
const check = async (name, run) => { await run(); groups++; console.log(`PASS ${name}`); };

await check("native artifact descriptors project exact retained fields without modifying source", () => {
  const original = JSON.stringify(fixture);
  const anthropic = projectionApi.projectImportedMessage(fixture.messages.anthropic[0]);
  assert.equal(anthropic.artifacts.length, 1);
  assert.strictEqual(anthropic.artifacts[0].raw, fixture.messages.anthropic[0].metadata.export.raw.content[4].input);
  assert.equal(anthropic.artifacts[0].content, "<ul><li>Zażółć</li></ul>");
  assert.equal(anthropic.artifacts[0].version, "ver-1");
  const source = fixture.messages.openai.find(message => message.metadata.export.key === "c1-a4");
  const openai = projectionApi.projectImportedMessage(source);
  assert.equal(openai.artifacts[0].title, "plan-migracji");
  assert.equal(openai.artifacts[0].content, "# Plan\n1. Schemat\n2. Dane\n3. Weryfikacja");
  assert.equal(openai.artifacts[0].parsedFromText, true);
  assert.strictEqual(openai.raw, source.metadata.export.raw);
  const partSource = structuredClone(source);
  partSource.metadata.export.raw.content.parts = [partSource.metadata.export.raw.content.text];
  delete partSource.metadata.export.raw.content.text;
  const partArtifact = projectionApi.projectImportedMessage(partSource).artifacts[0];
  assert.equal(partArtifact.path, "/content/parts/0");
  assert.equal(partArtifact.content, openai.artifacts[0].content);
  assert.equal(JSON.stringify(fixture), original);
  const missing = projectionApi.projectImportedMessage({ metadata: { export: { raw: {}, artifacts: [{ path: "/missing", title: "Absent", type: "text/html" }] } } });
  assert.equal(missing.artifacts[0].resolved, false);
  assert.equal(missing.artifacts[0].content, null);
});
await check("media requires passive MIME, retained base64 and explicit activation", () => {
  assert.equal(projectionApi.importedMediaDataUrl({ mime: "image/svg+xml", encoding: "base64", data: "PHN2Zz4=", href: null }), null);
  assert.equal(projectionApi.importedMediaDataUrl({ mime: "text/html", encoding: "base64", data: "PHNjcmlwdD4=", href: null }), null);
  assert.equal(projectionApi.importedMediaDataUrl({ mime: "image/png", encoding: "url", data: "https://example.invalid/i.png", href: null }), null);
  assert.equal(projectionApi.importedMediaDataUrl({ mime: "image/png", encoding: "base64", data: "not!base64", href: null }), null);
  assert.equal(projectionApi.importedMediaDataUrl({ mime: "image/png", encoding: "base64", data: "aGVs\nbG8=", href: null }), "data:image/png;base64,aGVsbG8=");
});

const bundle = await build({
  stdin: { contents: `
    import React, { useState } from "react";
    import { createRoot } from "react-dom/client";
    import ImportedMessageContent from "./src/components/ImportedMessageContent";
    import ConversationBranches, { conversationBranchPath, indexConversationBranches } from "./src/components/ConversationBranches";
    import fixture from "./e2e/fixtures/imported-message-content.json";
    const anthropic = structuredClone(fixture.messages.anthropic[0]);
    anthropic.text = 'Edited native text. ![external](https://example.invalid/native.png)';
    anthropic.metadata.export.raw.content[4].input.content = '<h2>Retained title</h2><ul><li>Source item</li></ul>' +
      '<img src="https://example.invalid/artifact.png"><svg><image href="https://example.invalid/svg.png" /></svg>' +
      '<iframe src="https://example.invalid/frame"></iframe><script>window.__sourceExecuted=true</script>' +
      '<span style="background-image:url(https://example.invalid/css.png)" onclick="window.__sourceExecuted=true">Passive text</span>' +
      '<a href="javascript:window.__sourceExecuted=true">Active link</a><a href="https://example.invalid/citation">Reference</a>';
    anthropic.metadata.export.raw.content[7].source.data = 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a57UAAAAASUVORK5CYII=';
    const openai = structuredClone(fixture.messages.openai.find(message => message.metadata.export.key === 'c1-a4'));
    const make = (id, parent_id, status, text = id) => ({ id, parent_id, conv_id:'c-public', status, text, role: id === 'root' ? 'user' : 'assistant', created:'2026-10-04T00:00:00Z' });
    const messages = [make('root',null,'active'), make('a0','root','active'), make('u0','a0','active'), make('tail','u0','active'),
      make('b0','root','version'), make('b1','b0','version'), make('b2','b1','excluded'), make('b3','b2','excluded'),
      make('orphan','absent','version'), make('cycle-a','cycle-b','deleted'), make('cycle-b','cycle-a','deleted')];
    const snapshot = JSON.stringify(messages);
    window.__sourceTopology = { conversationBranchPath, indexConversationBranches, make, messages, snapshot };
    function App() {
      const [selected, setSelected] = useState(null);
      const [receipt, setReceipt] = useState(null);
      return <><div id="anthropic"><ImportedMessageContent message={anthropic} /></div>
        <div id="openai"><ImportedMessageContent message={openai} /></div>
        <ConversationBranches messages={messages} selectedId={selected} onSelect={(message,path,diagnostics) => {
          setSelected(message.id); setReceipt({ id:message.id, path:path.map(row => row.id), ...diagnostics });
        }} /><pre id="branch-receipt">{JSON.stringify(receipt)}</pre></>;
    }
    createRoot(document.getElementById('root')).render(<App />);
  `, resolveDir: root, loader: "tsx" }, bundle: true, write: false, format: "iife", platform: "browser",
  outfile: "/tmp/loom-source-views.js", jsx: "automatic",
});
const script = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
const css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";
const server = createServer((req, res) => {
  if (req.url === "/app.js") { res.setHeader("Content-Type", "text/javascript"); res.end(script); return; }
  if (req.url === "/app.css") { res.setHeader("Content-Type", "text/css"); res.end(css); return; }
  res.setHeader("Content-Type", "text/html");
  res.end('<!doctype html><html><head><link rel="stylesheet" href="/app.css"></head><body><div id="root"></div><script src="/app.js"></script></body></html>');
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
let browser;
try {
  browser = await chromium.launch({ headless: true, args: ["--no-sandbox"] });
  const page = await browser.newPage();
  const errors = [], remote = [];
  page.on("pageerror", error => errors.push(error.message));
  page.on("request", request => { if (/^https?:\/\/example\.invalid/.test(request.url())) remote.push(request.url()); });
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.locator("#anthropic [data-testid=imported-source-details] > summary").click();
  await check("artifact projection preserves structure while scripts, styles and remote resources remain inert", async () => {
    const artifact = page.locator("#anthropic [data-testid=imported-artifact]");
    assert.match(await artifact.innerText(), /Widok listy/);
    assert.match(await artifact.innerText(), /ver-1/);
    assert.equal(await page.locator("img, video, audio, iframe, object, embed, svg").count(), 0);
    await artifact.locator("select").selectOption("preview");
    assert.equal(await artifact.locator("h2").innerText(), "Retained title");
    assert.equal(await artifact.locator("li").innerText(), "Source item");
    assert.equal(await artifact.locator("[style], [onclick], img, svg, iframe").count(), 0);
    assert.equal(await artifact.getByText("Active link", { exact: true }).getAttribute("href"), null);
    assert.equal(await artifact.getByText("Reference", { exact: true }).getAttribute("rel"), "noopener noreferrer");
    assert.equal(await page.evaluate(() => window.__sourceExecuted), undefined);
    assert.deepEqual(remote, []);
    await artifact.locator("select").selectOption("source");
    assert.match(await artifact.locator("pre code").innerText(), /<script>window\.__sourceExecuted=true/);
  });
  await check("retained media decodes only on click, then closes without remote requests", async () => {
    const media = page.locator("#anthropic [data-testid=imported-media]");
    assert.equal(await media.locator("img").count(), 0);
    await media.getByRole("button", { name: "Preview retained media", exact: true }).click();
    const image = media.locator("img");
    assert.match(await image.getAttribute("src"), /^data:image\/png;base64,/);
    await page.waitForFunction(() => document.querySelector("#anthropic .imported-media img")?.complete);
    assert.equal(await image.evaluate(element => element.naturalWidth), 1);
    await media.getByRole("button", { name: "Close retained media preview", exact: true }).click();
    assert.equal(await media.locator("img").count(), 0);
    assert.deepEqual(remote, []);
  });
  await check("source view filters never discard unknown JSON or original edited/source distinction", async () => {
    await page.locator("#anthropic").getByLabel("Show blocks", { exact: true }).uncheck();
    await page.locator("#anthropic").getByLabel("Show artifacts", { exact: true }).uncheck();
    assert.equal(await page.locator("#anthropic [data-testid=imported-source-block], #anthropic [data-testid=imported-artifact]").count(), 0);
    await page.locator("#anthropic").getByText("Complete preserved source message", { exact: true }).click();
    assert.match(await page.locator("#anthropic [data-testid=imported-source-raw]").innerText(), /"web_widget"/);
    assert.match(await page.locator("#anthropic .body").innerText(), /Edited native text/);
    await page.locator("#openai [data-testid=imported-source-details] > summary").click();
    const artifact = page.locator("#openai [data-testid=imported-artifact]");
    await artifact.locator("select").selectOption("preview");
    assert.equal(await artifact.locator("h1").innerText(), "Plan");
    assert.deepEqual(await artifact.locator("li").allTextContents(), ["Schemat", "Dane", "Weryfikacja"]);
    assert.deepEqual(remote, []);
  });
  await page.locator("[data-testid=conversation-branches] > details > summary").click();
  await check("branch navigation follows complete descendants without restoring messages", async () => {
    assert.equal(await page.locator(".conversation-branch-list li").count(), 11);
    await page.locator('[data-message-id="b3"] button').click();
    assert.deepEqual(JSON.parse(await page.locator("#branch-receipt").innerText()), { id: "b3", path: ["root", "b0", "b1", "b2", "b3"], missingParent: null, cycleAt: null });
    assert.equal(await page.locator('[data-message-id="b3"]').getAttribute("data-depth"), "4");
    await page.getByRole("navigation", { name: "Conversation branches" }).getByLabel("version", { exact: true }).uncheck();
    assert.equal(await page.locator('.conversation-branch-list [data-message-id="b0"]').count(), 0);
    assert.equal(await page.locator('.conversation-branch-list [data-message-id="b3"]').count(), 1);
    assert.equal(await page.locator('[aria-label="Selected ancestor path"] button').count(), 5);
    assert.equal(await page.evaluate(() => JSON.stringify(window.__sourceTopology.messages) === window.__sourceTopology.snapshot), true);
  });
  await check("missing parents and cycles are reported with every retained row accessible", async () => {
    const nav = page.getByRole("navigation", { name: "Conversation branches" });
    await nav.getByLabel("version", { exact: true }).check();
    await page.locator('[data-message-id="orphan"] button').click();
    assert.equal(JSON.parse(await page.locator("#branch-receipt").innerText()).missingParent, "absent");
    await page.locator('[data-message-id="cycle-b"] button').click();
    const receipt = JSON.parse(await page.locator("#branch-receipt").innerText());
    assert.equal(receipt.cycleAt, "cycle-b");
    assert.equal(receipt.path.length, 2);
    assert.match(await nav.getByRole("status").innerText(), /1 missing parents, 1 cycle edges/);
    await nav.getByLabel("Find message", { exact: true }).fill("b3");
    assert.equal(await nav.locator(".conversation-branch-list li").count(), 1);
  });
  await check("deep topology remains iterative and does not truncate descendants", async () => {
    const result = await page.evaluate(() => {
      const { make, indexConversationBranches, conversationBranchPath } = window.__sourceTopology;
      const messages = Array.from({ length: 12000 }, (_, index) => make(`d-${index}`, index ? `d-${index - 1}` : null, "version"));
      const tree = indexConversationBranches(messages);
      const path = conversationBranchPath(messages, "d-11999");
      return { rows: tree.rows.length, depth: tree.rows.at(-1).depth, path: path.messages.length, cycles: tree.cycleEdges.length };
    });
    assert.deepEqual(result, { rows: 12000, depth: 11999, path: 12000, cycles: 0 });
  });
  assert.deepEqual(remote, []); assert.deepEqual(errors, []);
  console.log(`[source-views] ${groups}/${groups} groups passed; zero model calls, zero remote fetches`);
} finally {
  await browser?.close();
  await new Promise(resolve => server.close(resolve));
}
