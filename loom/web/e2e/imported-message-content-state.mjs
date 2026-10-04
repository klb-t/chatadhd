// Real native-import fixture projection checks. No model calls or private exports.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import ts from "typescript";

const source = readFileSync(new URL("../src/content/imported-message.ts", import.meta.url), "utf8");
const js = ts.transpileModule(source, { compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022 } }).outputText;
const m = await import(`data:text/javascript;base64,${Buffer.from(js).toString("base64")}`);
const fixture = JSON.parse(readFileSync(new URL("./fixtures/imported-message-content.json", import.meta.url), "utf8"));
const find = (provider, key) => fixture.messages[provider].find(message => message.metadata.export.key === key);
let groups = 0;
function check(name, run) { run(); groups++; console.log(`PASS ${name}`); }

check("all 13 actual native messages retain exact source objects and classifications", () => {
  for (const message of Object.values(fixture.messages).flat()) {
    const before = JSON.stringify(message);
    const exported = message.metadata.export;
    const view = m.projectImportedMessage(message);
    assert.strictEqual(view.raw, exported.raw);
    assert.strictEqual(view.metadata, exported);
    assert.equal(view.blocks.length, exported.blocks.length);
    assert.deepEqual(view.blocks.map(block => block.kind), exported.blocks.map(block => block.kind));
    assert.deepEqual(view.blocks.map(block => block.inferred), exported.blocks.map(block => block.inferred === true));
    assert.equal(view.references.length, exported.attachments.length + exported.pointers.length + exported.citations.length);
    assert.equal(JSON.stringify(message), before);
  }
});

check("OpenAI indexless thoughts and recap read preserved content in exact order", () => {
  const message = find("openai", "c1-th1"); const view = m.projectImportedMessage(message);
  view.blocks.forEach((block, index) => {
    assert.equal(block.path, `/content/thoughts/${index}`);
    assert.strictEqual(block.raw, message.metadata.export.raw.content.thoughts[index]);
    assert.equal(block.resolved, true);
    const raw = block.raw;
    if (typeof raw.content === "string") assert.ok(block.segments.some(segment => segment.text === raw.content));
    if (typeof raw.summary === "string") assert.ok(block.segments.some(segment => segment.text === raw.summary));
  });
  const recap = find("openai", "c1-rr1"); const block = m.projectImportedMessage(recap).blocks[0];
  assert.equal(block.path, "/content");
  assert.ok(block.segments.some(segment => segment.text === recap.metadata.export.raw.content.content));
});

check("OpenAI custom context remains two independent located source strings", () => {
  const message = find("openai", "c4-ci"); const view = m.projectImportedMessage(message);
  assert.deepEqual(view.blocks.map(block => block.path), ["/content/user_profile", "/content/user_instructions"]);
  assert.deepEqual(view.blocks.map(block => block.segments[0].text), [message.metadata.export.raw.content.user_profile, message.metadata.export.raw.content.user_instructions]);
  const memory = find("openai", "c4-mem");
  assert.ok(m.projectImportedMessage(memory).blocks[0].segments.some(segment => segment.text === memory.metadata.export.raw.content.model_set_context));
});

check("Anthropic thoughts, nested tool results and artifact HTML stay inert source data", () => {
  const message = find("anthropic", "m-a1"); const view = m.projectImportedMessage(message);
  view.blocks.forEach((block, index) => {
    assert.equal(block.path, `/content/${index}`);
    assert.strictEqual(block.raw, message.metadata.export.raw.content[index]);
    assert.equal(block.resolved, true);
  });
  assert.ok(view.blocks[0].segments.some(segment => segment.text === view.blocks[0].raw.thinking));
  assert.equal(view.blocks[0].segments.filter(segment => segment.label === "Exported summary").length, 2);
  assert.equal(view.blocks[2].toolName, "web_search");
  assert.deepEqual(JSON.parse(view.blocks[2].segments[0].text), view.blocks[2].raw.input);
  assert.deepEqual(JSON.parse(view.blocks[3].segments.find(segment => segment.label === "Tool result").text), view.blocks[3].raw.content);
  const artifact = view.blocks[4];
  assert.equal(artifact.toolName, "artifacts");
  assert.equal(JSON.parse(artifact.segments[0].text).content, "<ul><li>Zażółć</li></ul>");
  assert.equal(view.blocks[7].kind, "media"); assert.equal(view.blocks[7].segments.length, 0);
  assert.equal(view.blocks[8].segments[0].text, "Spec.");
});

check("media reference binding and unresolved extracted attachments remain distinct", () => {
  const video = m.projectImportedMessage(find("openai", "c3-u3"));
  assert.ok(video.references.some(reference => reference.kind === "media-pointer" && reference.resolved === true));
  assert.ok(video.references.every(reference => reference.href === null));
  const attachments = m.projectImportedMessage(find("anthropic", "m-u1"));
  assert.equal(attachments.references.length, 3);
  assert.ok(attachments.references.every(reference => reference.resolved === false));
  assert.equal(attachments.references[0].extractedText, "Notatki: lista, filtr.");
  assert.ok(attachments.references.every(reference => reference.href === null));
});

check("unknown widgets and original message versus edited text are never conflated", () => {
  const message = structuredClone(find("openai", "c4-a3"));
  const preserved = JSON.stringify(message.metadata.export.raw);
  message.text = "Owner's edited message.";
  const view = m.projectImportedMessage(message);
  assert.equal(view.blocks[0].kind, "unknown");
  assert.deepEqual(view.blocks[0].raw.widget.series, [1, 2, 3]);
  assert.equal(JSON.stringify(view.raw), preserved);
  assert.equal(message.text, "Owner's edited message.");
  const claude = m.projectImportedMessage(find("anthropic", "m-a1"));
  assert.deepEqual(claude.blocks[9].raw.payload.x, [1, 2]);
});

check("partial descriptors visibly retain missing payload and native inference", () => {
  const message = { metadata: { export: { provider: "anthropic", raw: { content: [{ type: "future" }] },
    blocks: [{ kind: "unknown", type: "future", index: 30, inferred: true, guess: "reasoning" }] } } };
  const block = m.projectImportedMessage(message).blocks[0];
  assert.equal(block.resolved, false); assert.equal(block.raw, undefined);
  assert.equal(block.inferred, true); assert.equal(block.guess, "reasoning"); assert.equal(block.kind, "unknown");
  const withoutSummary = { metadata: { export: { raw: { content: [{ type: "unknown", payload: [2] }] } } } };
  assert.deepEqual(m.projectImportedMessage(withoutSummary).blocks[0].raw.payload, [2]);
  assert.equal(m.projectImportedMessage({ metadata: {} }), null);
});

check("pointer navigation is RFC6901-safe and citations reject active or credential URLs", () => {
  assert.deepEqual(m.importedPointer({ "a/b": { "~key": [null, "found"] } }, "/a~1b/~0key/1"), { found: true, value: "found" });
  for (const pointer of ["/constructor", "/toString", "/__proto__", "/array/01", "/array/-1", "/array/2", "/x~2"]) {
    assert.equal(m.importedPointer({ array: ["a"] }, pointer).found, false);
  }
  for (const url of ["javascript:alert(1)", "data:text/html,<script>x</script>", "file:///tmp/private", "sediment://asset", "/preview", "https://key:secret@example.org/"]) assert.equal(m.importedCitationUrl(url), null);
  assert.equal(m.importedCitationUrl("https://example.invalid/article"), "https://example.invalid/article");
});

console.log(`[imported-message-content-state] ${groups}/${groups} groups passed; 13 native fixture messages, offline`);
