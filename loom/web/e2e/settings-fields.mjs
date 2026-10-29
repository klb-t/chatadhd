#!/usr/bin/env node
// Settings field metadata: parsing and labels (pure helpers) and, when LOOM_SETTINGS_FIELDS_JSON
// points at a real /api/settings/fields response, completeness of that response.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { loadTsModule } from "./ts-module.mjs";

const m = await loadTsModule(new URL("../src/settings/field-meta.ts", import.meta.url));

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
// the human layer's defaults: a response without groups or profile values still parses into empty ones
const bare = m.parseSettingsFields({ fields: [] });
assert.deepEqual(bare.groups, {});
assert.deepEqual(bare.profile_values, []);
assert.equal(bare.groups_issues, undefined);
assert.deepEqual(m.parseSettingsFields({ fields: [], groups: [1, 2] }).groups, {});  // an array is not a groups object
assert.deepEqual(m.parseSettingsFields({ fields: [], groups_issues: [{ code: "x" }] }).groups_issues, [{ code: "x" }]);

if (process.env.LOOM_SETTINGS_FIELDS_JSON) {
  const real = m.parseSettingsFields(JSON.parse(readFileSync(process.env.LOOM_SETTINGS_FIELDS_JSON, "utf8")));
  for (const f of real.fields) {
    assert.ok(f.edit && f.description, `field ${f.key} lacks edit rule or description`);
    assert.ok(f.default === "present" || f.default === "absent", `field ${f.key}: default must be present|absent`);
    if (f.scope === "secret") assert.equal(f.default_value, undefined);
    // the human layer (M1-D0): a Polish label and a group the response lists, for every field and profile value
    assert.ok(f.human?.label?.pl, `field ${f.key} lacks human.label.pl`);
    assert.ok(f.human.group in real.groups, `field ${f.key}: group ${f.human.group} not in groups`);
  }
  assert.ok(Object.keys(real.groups).length > 0, "no groups");
  assert.ok(real.profile_values.length > 0, "no profile values");
  for (const p of real.profile_values) {
    assert.ok(p.human?.label?.pl, `profile value ${p.profile}${p.pointer} lacks human.label.pl`);
    assert.ok(p.human.group in real.groups, `profile value ${p.profile}${p.pointer}: group not in groups`);
    assert.ok("value" in p || "problem" in p, `profile value ${p.profile}${p.pointer}: neither value nor problem`);
  }
  assert.equal(real.groups_issues, undefined, "the shipped response has no groups issues");
}
console.log("[settings-fields] PASS");
