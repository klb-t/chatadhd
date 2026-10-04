// Isolated real React component check: inert source rendering and no automatic remote media.
import assert from "node:assert/strict";
import { createServer } from "node:http";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import { chromium } from "playwright";

const root = fileURLToPath(new URL("../", import.meta.url));
const bundle = await build({
  stdin: { contents: `
    import React from "react";
    import { createRoot } from "react-dom/client";
    import ImportedMessageContent from "./src/components/ImportedMessageContent";
    import fixture from "./e2e/fixtures/imported-message-content.json";
    const message = structuredClone(fixture.messages.anthropic[0]);
    message.text = 'Current **edited** message.\\n![remote](https://example.invalid/remote.png)\\n' +
      '<span style="background-image:url(https://example.invalid/css.png)">inert style</span>' +
      '<svg><image href="https://example.invalid/svg.png" /></svg>' +
      '<img src="https://example.invalid/html.png" onerror="window.__sourceScriptRan=true">' +
      '<script>window.__sourceScriptRan=true</script>';
    message.metadata.export.raw.content[4].input.content = '<img src="https://example.invalid/artifact.png"><script>window.__sourceScriptRan=true</script>';
    createRoot(document.getElementById("root")).render(<ImportedMessageContent message={message} />);
  `, resolveDir: root, loader: "tsx" },
  bundle: true, write: false, format: "iife", platform: "browser", outfile: "/tmp/imported-message-component.js",
  jsx: "automatic",
});
const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
const css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";
const server = createServer((req, res) => {
  if (req.url === "/component.js") { res.setHeader("Content-Type", "text/javascript"); res.end(js); return; }
  if (req.url === "/component.css") { res.setHeader("Content-Type", "text/css"); res.end(css); return; }
  res.setHeader("Content-Type", "text/html");
  res.end('<!doctype html><html><head><link rel="stylesheet" href="/component.css"></head><body><div id="root"></div><script src="/component.js"></script></body></html>');
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
let browser;
try {
  browser = await chromium.launch({ headless: true, args: ["--no-sandbox"] });
  const page = await browser.newPage();
  const errors = [], remote = [];
  page.on("pageerror", error => errors.push(error.message));
  page.on("request", request => { if (request.url().startsWith("https://example.invalid/")) remote.push(request.url()); });
  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.locator('[data-testid="imported-message-content"]').waitFor();
  assert.match(await page.locator(".body").innerText(), /Current edited message/);
  assert.equal(await page.locator(".body strong").innerText(), "edited");
  assert.equal(await page.locator("img,svg,video,audio,iframe,object,embed").count(), 0);
  await page.locator('[data-testid="imported-source-details"] > summary').click();
  assert.equal(await page.locator('[data-testid="imported-source-block"]').count(), 10);
  const reasoning = page.locator('[data-testid="imported-source-block"][data-kind="reasoning"]');
  assert.match(await reasoning.innerText(), /Użytkownik chce widok listy; artefakt HTML/);
  assert.match(await reasoning.innerText(), /Plan widoku/);
  const artifacts = page.locator('[data-testid="imported-source-block"][data-kind="tool_call"]').nth(1);
  assert.match(await artifacts.locator("pre").innerText(), /<img src=\\?"https:\/\/example.invalid\/artifact.png/);
  const unknown = page.locator('[data-testid="imported-source-block"][data-kind="unknown"]');
  await unknown.getByText("Raw source block", { exact: true }).click();
  assert.match(await unknown.locator("pre").innerText(), /"payload"/);
  await page.getByText("Complete preserved source message", { exact: true }).click();
  assert.match(await page.locator('[data-testid="imported-source-raw"]').innerText(), /"future"|"web_widget"/);
  assert.match(await page.locator(".body").innerText(), /Current edited message/);
  assert.equal(await page.evaluate(() => window.__sourceScriptRan), undefined);
  assert.deepEqual(remote, []); assert.deepEqual(errors, []);
  console.log("[imported-message-content-browser] 1/1 group passed: current edited Markdown, native source blocks, unknown JSON, inert artifacts, zero remote media requests");
} finally {
  await browser?.close();
  await new Promise(resolve => server.close(resolve));
}
