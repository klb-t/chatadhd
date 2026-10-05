#!/usr/bin/env node
// Real settings/retrieval helpers with authored browser-storage fixtures only.
import assert from "node:assert/strict";
import { readFileSync, existsSync, mkdirSync, writeFileSync } from "node:fs";
import path from "node:path";
import ts from "typescript";

function moduleUrl(file, replacements = []) {
  let source = ts.transpileModule(readFileSync(new URL(file, import.meta.url), "utf8"), {
    compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022 },
  }).outputText;
  for (const [from, to] of replacements) source = source.replaceAll(`from "${from}"`, `from "${to}"`);
  return `data:text/javascript;base64,${Buffer.from(source).toString("base64")}`;
}
const planUrl = moduleUrl("../src/context/retrieval-plan.ts");
const plans = await import(planUrl);
const settings = await import(moduleUrl("../src/context/chat-settings.ts", [["./retrieval-plan", planUrl]]));
const records = new Map(), groups = [];
const previous = globalThis.localStorage;
let rejectKey = null;
globalThis.localStorage = { getItem: key => records.get(key) ?? null, setItem: (key, value) => {
  if (key === rejectKey) throw new DOMException("Authored quota failure", "QuotaExceededError");
  records.set(key, String(value));
} };
async function check(name, run) { await run(); groups.push(name); console.log(`[chat-settings-state] PASS ${name}`); }
const envelope = value => JSON.stringify({ schema: "loom.chat_settings/1", value });
try {
  await check("registered and unavailable channel IDs remain explicit transport data without guessed availability", () => {
    const channels = [{ id: "graph", limit: 25 }, { id: "tfidf", min_score: 0.2 }, { id: "fixture.unavailable.vector", limit: 80, min_score: -2 }];
    assert.deepEqual(settings.parseCandidateChannels(JSON.stringify(channels)), channels);
    assert.deepEqual(settings.parseCandidateChannels("[]"), []);
    assert.equal(settings.parseCandidateChannels('[{"id":"fixture.unavailable.vector"}]')[0].id, "fixture.unavailable.vector");
  });
  await check("channel page sizes exceed previous UI presets and retain the native integer boundary", () => {
    assert.deepEqual(settings.parseCandidateChannels('[{"id":"graph","limit":1000000,"min_score":99}]'), [{ id: "graph", limit: 1000000, min_score: 99 }]);
    assert.equal(settings.parseCandidateChannels('[{"id":"graph","limit":2147483647}]')[0].limit, 2147483647);
    for (const limit of [0, -1, 1.5, 2147483648]) {
      assert.throws(() => settings.parseCandidateChannels(JSON.stringify([{ id: "graph", limit }])), /native integer/);
    }
  });
  await check("unsupported method weights and parameters fail visibly instead of being silently discarded", () => {
    for (const weight of [0.5, 1000000]) {
      assert.throws(() => settings.parseCandidateChannels(JSON.stringify([{ id: "graph", weight }])), /method weights require the method registry API/);
    }
    assert.throws(() => settings.parseCandidateChannels('[{"id":"future.method","parameters":{"model":"fixture"}}]'), /method registry API/);
    assert.throws(() => settings.parseCandidateChannels('[{"id":"graph"},{"id":"graph"}]'), /unique/);
    assert.throws(() => settings.parseCandidateChannels('[{"id":" "}]'), /nonempty/);
    assert.throws(() => settings.parseCandidateChannels('[{"id":"graph","min_score":null}]'), /finite/);
  });
  await check("empty and malformed stored values retain their exact original bytes", () => {
    for (const bytes of ["", "{malformed", "null", JSON.stringify({ schema: "future.settings/2", value: {} }), envelope([])]) {
      records.set("loom.chat-settings.corrupt", bytes);
      const read = settings.readChatSettings("corrupt");
      assert.ok(read.error); assert.match(read.error, /preserved/); assert.deepEqual(read.value, {});
      assert.equal(records.get("loom.chat-settings.corrupt"), bytes);
    }
    assert.deepEqual(settings.readChatSettings("absent"), { value: {} });
    assert.equal(records.has("loom.chat-settings.absent"), false);
  });
  await check("invalid known setting types are rejected before restoration without repairing stored values", () => {
    for (const value of [{ model: 42 }, { includeMemory: "false" }, { usePlan: [] }, { contextBudget: 1000000 },
      { contextDetail: "unsupported" }, { contextDetail: ["auto"] }, { traceContext: ["on"] }, { traceContext: false },
      { planDraft: { id: "p", sourceRef: "", theses: [null] } }]) {
      const bytes = envelope(value); records.set("loom.chat-settings.invalid", bytes);
      const read = settings.readChatSettings("invalid"); assert.ok(read.error);
      assert.equal(records.get("loom.chat-settings.invalid"), bytes);
    }
  });
  await check("duplicate draft thesis identities cannot enter a restored plan", () => {
    const draft = plans.newPlan(); draft.theses[0].text = "First authored query";
    draft.theses.push({ ...plans.newThesis(draft.theses[0].key), id: "different-thesis", text: "Second authored query" });
    const before = JSON.stringify(draft);
    assert.throws(() => settings.storedPlan(draft), /keys must be unique/);
    assert.equal(JSON.stringify(draft), before);
    const bytes = envelope({ usePlan: true, planDraft: draft }); records.set("loom.chat-settings.duplicate-plan", bytes);
    assert.ok(settings.readChatSettings("duplicate-plan").error);
    assert.equal(records.get("loom.chat-settings.duplicate-plan"), bytes);
  });
  await check("independent view settings and full context plans survive restart and changes stay isolated", () => {
    const firstPlan = plans.newPlan(); firstPlan.id = "view-one-plan"; firstPlan.theses[0].text = "Compare authored graph evidence";
    firstPlan.theses[0].targetsMode = "custom"; firstPlan.theses[0].targets = "entity one\nentity two";
    firstPlan.theses[0].hops = "1000000"; firstPlan.theses[0].weight = "1000000000";
    const first = { model: "fixture/model-one", useKnowledge: true, usePlan: true, planDraft: firstPlan,
      includeMemory: false, contextBudget: "1000000", contextDetail: "raw", channels: '[{"id":"fixture.unavailable.vector","limit":1000000}]' };
    const second = { model: "fixture/model-two", useKnowledge: false, usePlan: false, planDraft: plans.newPlan(),
      includeMemory: true, contextBudget: "4000", contextDetail: "auto", channels: "[]" };
    settings.writeChatSettings("view-one", first); settings.writeChatSettings("view-two", second);
    assert.deepEqual(settings.readChatSettings("view-one").value, first);
    assert.deepEqual(settings.readChatSettings("view-two").value, second);
    const compiled = plans.buildRetrievalPlan(settings.storedPlan(settings.readChatSettings("view-one").value.planDraft));
    assert.deepEqual(compiled.theses[0].targets, ["entity one", "entity two"]);
    assert.equal(compiled.theses[0].relation_hops, 1000000); assert.equal(compiled.theses[0].budget_weight, 1000000000);
    const secondBytes = records.get("loom.chat-settings.view-two");
    settings.writeChatSettings("view-one", { ...first, model: "fixture/changed-model" });
    assert.equal(settings.readChatSettings("view-one").value.model, "fixture/changed-model");
    assert.equal(records.get("loom.chat-settings.view-two"), secondBytes);
    assert.deepEqual(settings.readChatSettings("view-two").value, second);
  });
  await check("failed settings persistence leaves both views' previous values intact", () => {
    const firstBytes = records.get("loom.chat-settings.view-one"), secondBytes = records.get("loom.chat-settings.view-two");
    rejectKey = "loom.chat-settings.view-one";
    assert.throws(() => settings.writeChatSettings("view-one", { model: "fixture/rejected-write" }), /quota failure/);
    rejectKey = null;
    assert.equal(records.get("loom.chat-settings.view-one"), firstBytes); assert.equal(records.get("loom.chat-settings.view-two"), secondBytes);
  });
  if (process.env.CHAT_SETTINGS_EVIDENCE_DIR) {
    const directory = process.env.CHAT_SETTINGS_EVIDENCE_DIR; mkdirSync(directory, { recursive: true });
    const filename = path.join(directory, "chat-settings-state-results.json"); assert.ok(!existsSync(filename), "Use a fresh evidence directory.");
    writeFileSync(filename, JSON.stringify({ groups, passed: groups.length, fixture: "authored-local-storage", paidCalls: 0 }, null, 2));
  }
  console.log(`[chat-settings-state] ${groups.length}/${groups.length} passed; zero provider calls.`);
} finally { globalThis.localStorage = previous; }
