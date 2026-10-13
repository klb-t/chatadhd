#!/usr/bin/env node
// Settings field metadata: parsing and labels (pure helpers) and, when LOOM_SETTINGS_FIELDS_JSON
// points at a real /api/settings/fields response, completeness of that response.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import ts from "typescript";

const source = ts.transpileModule(readFileSync(new URL("../src/settings/field-meta.ts", import.meta.url), "utf8"), {
  compilerOptions: { target: ts.ScriptTarget.ES2022, module: ts.ModuleKind.ES2022 } }).outputText;
const m = await import(`data:text/javascript;base64,${Buffer.from(source).toString("base64")}`);

const doc = m.parseSettingsFields({ schema: "loom.settings_fields/1", edit_rules: [{ id: "deny", edit: "read_only", enforced: true }],
  fields: [
    { key: "theme", scope: "config", default: "present", default_value: "dark", edit: "optional" },
    { key: "api_key", scope: "secret", default: "absent", edit: "required", sensitive: true },
    { key: "_config_version", scope: "config", default: "absent", edit: "read_only" },
    { key: "x", scope: "config", default: "absent", edit: "custom_rule" } ] });
assert.equal(m.defaultLabel(doc.fields[0]), 'default: "dark"');
assert.equal(m.defaultLabel(doc.fields[1]), "no default");
assert.equal(m.defaultLabel({ ...doc.fields[0], sensitive: true }), "default");  // never print a sensitive value
assert.equal(m.isReadOnly(doc.fields[2]), true);
assert.equal(m.isReadOnly(doc.fields[3]), false);
assert.equal(m.editLabel(doc.fields[3], doc), "custom_rule");  // open rule strings pass through
assert.throws(() => m.parseSettingsFields({}), /malformed/);

if (process.env.LOOM_SETTINGS_FIELDS_JSON) {
  const real = m.parseSettingsFields(JSON.parse(readFileSync(process.env.LOOM_SETTINGS_FIELDS_JSON, "utf8")));
  for (const f of real.fields) {
    assert.ok(f.edit && f.description, `field ${f.key} lacks edit rule or description`);
    assert.ok(f.default === "present" || f.default === "absent", `field ${f.key}: default must be present|absent`);
    if (f.scope === "secret") assert.equal(f.default_value, undefined);
  }
}
console.log("[settings-fields] PASS");
