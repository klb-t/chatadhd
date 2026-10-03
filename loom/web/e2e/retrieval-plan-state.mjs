// Form semantics that affect the native request, without a provider or DOM.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import ts from "typescript";
const source = readFileSync(new URL("../src/context/retrieval-plan.ts", import.meta.url), "utf8");
const js = ts.transpileModule(source, { compilerOptions: { target: ts.ScriptTarget.ES2020, module: ts.ModuleKind.ES2020 } }).outputText;
const { newPlan, newThesis, buildRetrievalPlan } = await import(`data:text/javascript;base64,${Buffer.from(js).toString("base64")}`);
const draft = newPlan();
draft.theses[0].text = "  Preserve the authored query\nexactly.  ";
const inherited = buildRetrievalPlan(draft);
assert.equal(inherited.theses[0].text, draft.theses[0].text);
for (const key of ["targets", "claims", "relation_hops", "detail_resolution"]) assert.equal(Object.hasOwn(inherited.theses[0], key), false);
const custom = structuredClone(draft);
Object.assign(custom.theses[0], { targetsMode: "custom", targets: "entity with spaces\nsecond-id", claimsMode: "custom", claims: "", hops: "0", detail: "raw", counter: false, weight: "0.25" });
custom.sourceRef = '{"draft":"authored","verified":false}';
const result = buildRetrievalPlan(custom);
assert.deepEqual(result.theses[0].targets, ["entity with spaces", "second-id"]);
assert.deepEqual(result.theses[0].claims, []);
assert.equal(result.theses[0].relation_hops, 0);
assert.equal(result.theses[0].detail_resolution, "raw");
assert.equal(result.theses[0].require_counter_evidence, false);
assert.equal(result.theses[0].budget_weight, 0.25);
assert.deepEqual(result.source_ref, { draft: "authored", verified: false });
assert.equal(draft.theses[0].targetsMode, "inherit", "conversion and validation do not mutate saved draft");
for (const mutate of [
  d => d.theses.push({ ...d.theses[0], key: 2 }),
  d => d.theses[0].text = " ",
  d => d.theses[0].hops = "1.5",
  d => d.theses[0].weight = "0",
  d => d.theses[0].weight = "Infinity",
  d => { d.theses[0].targetsMode = "custom"; d.theses[0].targets = "same\nsame"; },
  d => d.sourceRef = "{invalid",
  d => d.theses = [],
]) { const bad = structuredClone(draft); mutate(bad); assert.throws(() => buildRetrievalPlan(bad)); }
const many = { ...draft, theses: Array.from({ length: 120 }, (_, i) => ({ ...newThesis(i + 1), text: `Query ${i}` })) };
assert.equal(buildRetrievalPlan(many).theses.length, 120, "no product limit on thesis count");
console.log("[retrieval-plan-state] PASS: inheritance vs explicit empty, exact text, scoped detail/weights, declared source, validation, and no thesis count limit");
