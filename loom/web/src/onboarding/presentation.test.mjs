import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { controlOrder, errorPresentation, formatTemplate, layerExplanation, message, presentationStyle, PresentationError, resolvePresentation, vocabulary } from "./presentation.mjs";
import { normalizeNativeSnapshot } from "./native-snapshot.ts";

const canonical = JSON.parse(await readFile(new URL("../../../data/onboarding/ui.pack", import.meta.url), "utf8"));
const clone = value => structuredClone(value);

test("generated embedding equals canonical presentation data and exact former EN defaults", () => {
  const p = resolvePresentation();
  assert.deepEqual(p.pack, canonical);
  assert.equal(p.locale, "en");
  assert.equal(p.defaults.mode, "conversation");
  assert.equal(p.defaults.input, "text");
  assert.equal(p.defaults.json_indent, 2);
  assert.deepEqual(p.defaults.rows, { field: 3, correction: 3, scenario: 14, privacy_rule: 6, knowledge: 3, default: 3 });
  assert.equal(p.defaults.provider_separator, "\n");
  assert.deepEqual(p.defaults.modes.map(item => message(p, item.label)), ["Conversation", "Form"]);
  assert.deepEqual(p.defaults.boolean_options.map(item => [message(p, item.label), item.value]), [["True", true], ["False", false]]);
  assert.equal(message(p, "session.prefix", { status: "active" }), "Session: active. Method: ");
  assert.equal(message(p, "field.label_prefix", { label: "Project" }) + message(p, "field.state", { status: "unknown", suffix: "" }), "Project (unknown)");
  assert.equal(vocabulary(p, "review_state", "confirmed"), "confirmed");
});

test("all six native layer explanation strings retain exact EN parity", () => {
  const p = resolvePresentation();
  const expected = {
    missing: "No current default or user value exists for this key.",
    excluded: "A durable user exclusion blocks this stable id across pack updates.",
    disabled: "The user retained this default but disabled its use.",
    user: "An explicit user value overrides the built-in graph layer.",
    proposal: "This new default touches an excluded area and awaits user acceptance.",
    builtin: "The built-in graph default applies because no user suppression or override exists.",
  };
  for (const [id, text] of Object.entries(expected)) assert.equal(message(p, `layer.${id}`), text);
});

test("changing only supplied pack values changes controls, labels, locale, geometry and editor settings", () => {
  const edited = clone(canonical);
  edited.default_locale = "pl";
  edited.locales.pl["field.submit"] = "Własny napis";
  edited.defaults.mode = "form";
  edited.defaults.input = "json";
  edited.defaults.rows.scenario = 25;
  edited.defaults.json_indent = 4;
  edited.defaults.control_order.section = ["skip", "confirmed", "repeat"];
  edited.defaults.presentation_tokens["--onboarding-width"] = "1100px";
  edited.defaults.provider_separator = ";";
  const p = resolvePresentation({ available: true, status: "effective", value: edited });
  assert.equal(message(p, "field.submit"), "Własny napis");
  assert.equal(p.defaults.mode, "form");
  assert.equal(p.defaults.input, "json");
  assert.equal(p.defaults.rows.scenario, 25);
  assert.equal(p.defaults.json_indent, 4);
  assert.equal(p.defaults.provider_separator, ";");
  assert.equal(presentationStyle(p)["--onboarding-width"], "1100px");
  assert.deepEqual(controlOrder(p, "section", { confirmed: 1, corrected: 1, rejected: 1, skip: 1, repeat: 1 }), ["skip", "confirmed", "repeat"]);
  assert.equal(canonical.default_locale, "en");
});

test("declared native presentation suppression does not restore generated English defaults", () => {
  for (const status of ["disabled", "excluded", "proposal", "missing"]) {
    let failure;
    try { resolvePresentation({ available: false, status, value: canonical }); } catch (error) { failure = error; }
    assert.equal(failure.code, "error.presentation");
    assert.equal(failure.message, "error.presentation");
    assert.deepEqual(errorPresentation(failure), { text: "error.presentation", details: "error.presentation" });
    assert.throws(() => resolvePresentation({ available: true, status, value: canonical }), error => error.machineOnly);
  }
});

test("broken data, missing locale/message and unregistered controls are explicit errors", () => {
  assert.throws(() => resolvePresentation(null), PresentationError);
  const missing = clone(canonical); delete missing.defaults.rows;
  assert.throws(() => resolvePresentation(missing), error => error.code === "error.presentation_invalid");
  const partial = clone(canonical); delete partial.locales.pl["layer.user"];
  assert.throws(() => resolvePresentation(partial), PresentationError);
  assert.throws(() => resolvePresentation(canonical, "absent"), error => error.code === "error.presentation_locale");
  assert.throws(() => message(resolvePresentation(), "absent"), error => error.code === "error.presentation_message");
  const omitted = clone(canonical); omitted.defaults.modes = []; omitted.defaults.preference_modes = [];
  omitted.defaults.control_order.section = [];
  assert.deepEqual(resolvePresentation(omitted).defaults.modes, []);
  assert.deepEqual(controlOrder(resolvePresentation(omitted), "section", {}), []);
  const unknown = clone(canonical); unknown.defaults.control_order.section.push("invented_operation");
  assert.throws(() => resolvePresentation(unknown), PresentationError);
  assert.throws(() => controlOrder(resolvePresentation(), "section", {}), PresentationError);
});

test("templates are inert, canonical for structured parameters and reject malformed/missing placeholders", () => {
  assert.equal(formatTemplate("before {{value}} after", { value: "{{secret}}<script>" }), "before {{secret}}<script> after");
  assert.equal(formatTemplate("{{value}}", { value: { z: 2, a: { b: true, a: null } } }), '{"a":{"a":null,"b":true},"z":2}');
  for (const template of ["Hello {{field", "Hello {{}}", "Hello {{outer {{inner}}"]) assert.throws(() => formatTemplate(template, {}), error => error.code === "error.template_syntax");
  for (const value of [null, [], new Date(0), "synthetic"]) assert.throws(() => formatTemplate("literal", value), error => error.code === "error.template_parameters");
  assert.equal(formatTemplate("{{value}}", Object.assign(Object.create(null), { value: "plain" })), "plain");
  assert.throws(() => formatTemplate("{{absent}}", {}), error => error.code === "error.template_parameter");
});

test("PL locale comes from presentation or explicit UI choice, never personal language data", () => {
  const p = resolvePresentation(canonical, "pl");
  assert.equal(message(p, "mode.form"), "Formularz");
  assert.equal(vocabulary(p, "status", "never"), "nigdy");
  assert.equal(vocabulary(p, "status", "future_native_state"), "future_native_state");
  assert.equal(resolvePresentation(canonical).locale, "en");
  assert.equal(resolvePresentation(canonical, "").locale, "en");
});

test("ordinary diagnostics translate machine errors and keep parser/native English only in inspectable details", () => {
  const p = resolvePresentation(canonical, "pl");
  const external = errorPresentation(new SyntaxError("Unexpected token: synthetic JSON"), p);
  assert.equal(external.text, canonical.locales.pl["error.failure"]);
  assert.equal(external.details, "Unexpected token: synthetic JSON");
  const own = errorPresentation(new PresentationError("error.number"), p);
  assert.equal(own.text, "Wprowadź liczbę.");
  let invalidNative; try { normalizeNativeSnapshot(null); } catch (failure) { invalidNative = failure; }
  assert.equal(errorPresentation(invalidNative, p).text, "Natywny kreator: snapshot musi mieć typ obiekt.");
});

test("native nullable explanation and presentation envelope project without mutating profile or restoring suppressed reason", async () => {
  const scenario = JSON.parse(await readFile(new URL("../../../data/onboarding/scenario.pack", import.meta.url), "utf8"));
  const raw = {
    scenario_definition: scenario,
    presentation: { available: false, status: "excluded" },
    profile: { fields: {}, privacy: { rules: [] }, candidates: {}, history: [], settings: { preference_mode: "ask" }, session: {
      status: "active", section: scenario.sections[0].id,
      sections: Object.fromEntries(scenario.sections.map(section => [section.id, { status: "pending", summary: null }])) } },
    effectiveDefaults: [{ revision: 7, entity: { revision: 7 }, source: { pack_id: "synthetic" }, value: "synthetic", key: "presentation.onboarding", id: "presentation.onboarding/v1", area: "presentation", status: "excluded", layer: "user_exclusion", explanation: null }],
  };
  const original = clone(raw);
  const projected = normalizeNativeSnapshot(raw);
  assert.deepEqual(projected.presentation, raw.presentation);
  assert.equal(projected.defaults[0].reason, null);
  assert.equal(layerExplanation(resolvePresentation(), projected.defaults[0]), null);
  assert.deepEqual(raw, original);
  raw.effectiveDefaults[0].explanation = "Native data reason";
  raw.effectiveDefaults[0].status = "effective"; raw.effectiveDefaults[0].layer = "builtin";
  const translated = clone(canonical);
  translated.locales.en["layer.builtin"] = "Revision {{revision}}; source {{source}}; entity {{entity}}; value {{value}}";
  const entry = normalizeNativeSnapshot(raw).defaults[0];
  assert.equal(layerExplanation(resolvePresentation(translated), entry), 'Revision 7; source {"pack_id":"synthetic"}; entity {"revision":7}; value synthetic');
  translated.locales.en["layer.builtin"] = "Original {{explanation}}";
  assert.equal(layerExplanation(resolvePresentation(translated), entry), "Original null");
});

test("product source contains no hand-authored visible JSX strings or CSS token fallback values", async () => {
  for (const file of ["OnboardingPanel.tsx", "WhatAppKnows.tsx", "presentation-context.tsx"]) {
    const source = await readFile(new URL(file, import.meta.url), "utf8");
    assert.equal(/>\s*[A-Za-z][^{}<>]*<\/[A-Za-z]/.test(source), false, file);
    assert.equal(/rows=\{\d+\}/.test(source), false, file);
  }
  const css = await readFile(new URL("onboarding.css", import.meta.url), "utf8");
  assert.equal(/#[\da-f]{3,6}|\d+px\b/i.test(css), false);
});
