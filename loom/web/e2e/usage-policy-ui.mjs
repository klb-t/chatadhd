// Real React, fake HTTP ledger: no models, paid requests or production dispatch.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { createServer } from "node:http";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import { chromium } from "playwright";

const root = fileURLToPath(new URL("../", import.meta.url));
const preset = { schema: "loom.usage_policy/1", growth_factor: 10, baseline_window: 32, include_reservations: true, initial_baselines: {} };
let storedOverride = null, config = { temperature: 0.7, default_model: "offline", api_key: "fixture-credential-never-show" };
let requestSequence = 0, failConfirm = true;
const commands = [], configWrites = [], receipts = new Map();
// Fixture hashes test response rendering, not the native canonicalizer itself.
const hash = value => createHash("sha256").update(JSON.stringify(value)).digest("hex");
const bundle = await build({
  stdin: { contents: `
    import React from "react";
    import { createRoot } from "react-dom/client";
    import SettingsPanel from "./src/components/SettingsPanel";
    import UsagePolicyPanel from "./src/components/UsagePolicyPanel";
    import { api } from "./src/api";
    window.__failSave = false; window.__failLoad = true;
    async function post(path, body) {
      const response = await fetch(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error.message);
      return result;
    }
    Object.assign(api, {
      getConfig: async () => { if (window.__failLoad) throw new Error("offline load fixture"); return post("/config", { action: "read" }); },
      setConfig: async patch => { if (window.__failSave) throw new Error("offline save fixture"); return post("/config", { patch }); },
      setConfigKey: async (key, value) => { if (window.__failSave) throw new Error("offline save fixture"); return post("/config", { patch: { [key]: value } }); },
      getModels: async () => [], hasSecret: async () => false,
      usagePolicy: async command => post("/policy", command)
    });
    createRoot(document.getElementById("root")).render(location.pathname === "/unsupported"
      ? <UsagePolicyPanel transport={{ setConfigKey: api.setConfigKey }} /> : <SettingsPanel />);
  `, resolveDir: root, loader: "tsx" },
  bundle: true, write: false, format: "iife", platform: "browser", outfile: "/tmp/usage-policy-ui.js", jsx: "automatic",
});
const js = bundle.outputFiles.find(file => file.path.endsWith(".js")).text;
const css = bundle.outputFiles.find(file => file.path.endsWith(".css"))?.text ?? "";
const json = (response, body, status = 200) => { response.statusCode = status; response.setHeader("Content-Type", "application/json"); response.end(JSON.stringify(body)); };
function decision(estimate, receiptId) {
  return { operation_id: estimate.operation_id, baseline_key: estimate.baseline_key, estimate,
    options: { ...preset, include_reservations: true }, status: "requires_confirmation", authorized: false, receipt_id: receiptId,
    resources: { custom_units: { estimate: 20, baseline: 1, baseline_source: "instrument_measured", baseline_samples: 4,
      reserved: 1, projected: 21, ratio: 21, requires_confirmation: true, reserved_lower_bound: 1, projected_lower_bound: 21, projection_status: "estimated" },
      cpu_seconds: { estimate: null, baseline: null, baseline_source: "unavailable", baseline_samples: 0, reserved: null,
        projected: null, ratio: null, requires_confirmation: false, reserved_lower_bound: 0, projected_lower_bound: 0, projection_status: "partial_unknown" } } };
}
const server = createServer(async (request, response) => {
  if (request.url === "/component.js") { response.setHeader("Content-Type", "text/javascript"); response.end(js); return; }
  if (request.url === "/component.css") { response.setHeader("Content-Type", "text/css"); response.end(css); return; }
  if (request.url === "/config" || request.url === "/policy") {
    const chunks = []; for await (const chunk of request) chunks.push(chunk);
    const body = JSON.parse(Buffer.concat(chunks).toString());
    if (request.url === "/config") {
      if (body.patch) { configWrites.push(body.patch); config = { ...config, ...body.patch }; if ("loom_usage_policy" in body.patch) storedOverride = body.patch.loom_usage_policy; }
      json(response, config); return;
    }
    commands.push(body);
    if (body.action === "settings") { json(response, { preset, stored_override: storedOverride, effective: storedOverride ?? preset, source: storedOverride ? "stored_override" : "preset", ledger_path: "fixture/usage-policy.sqlite", capabilities: { preset_application: "next_policy_open" } }); return; }
    if (body.action === "preview_settings") {
      if (body.override.growth_factor !== undefined && body.override.growth_factor <= 0) {
        json(response, { error: { message: "growth_factor must be positive" } }, 400); return;
      }
      const effective = { ...preset, ...(storedOverride ?? {}) }, proposed = { ...preset, ...body.override };
      json(response, { preset, stored_override: storedOverride, effective, source: storedOverride ? "configured" : "preset",
        preset_source: "legacy_code_pending_pack_migration", override_semantics: "replace_top_level_fields",
        hashes: { algorithm: "sha256", representation: "loom.canonical_json", preset: hash(preset), stored_override: storedOverride === null ? null : hash(storedOverride), effective: hash(effective) },
        preview: { override: body.override, effective: proposed, hashes: { override: hash(body.override), effective: hash(proposed) }, effective_changed: JSON.stringify(effective) !== JSON.stringify(proposed), persisted: false },
        ledger_path: "fixture/usage-policy.sqlite", capabilities: { preset_application: "next_policy_open" } }); return;
    }
    if (body.action === "preview") { json(response, decision(body.estimate, "preview-only")); return; }
    if (body.action === "request") { const result = decision(body.estimate, `receipt-${++requestSequence}`); receipts.set(result.operation_id, result); json(response, result); return; }
    if (body.action === "confirm") {
      if (failConfirm) { failConfirm = false; json(response, { error: { message: "usage projection changed; request a fresh receipt" } }, 409); return; }
      const receipt = receipts.get(body.operation_id);
      if (receipt.receipt_id !== body.receipt_id) { json(response, { error: { message: "wrong receipt" } }, 409); return; }
      json(response, { ...receipt, status: body.approved ? "allowed" : "denied", authorized: body.approved, confirmation: { approved: body.approved, ref: body.confirmation_ref } }); return;
    }
    if (body.action === "inspect") { json(response, { baseline_key: body.baseline_key, baseline: { custom_units: { value: 1, source: "instrument_measured", samples: 4 } }, reservations: { cpu_seconds: null }, operations: [{ operation_id: "interrupted-op", status: "unresolved", remaining: { cpu_seconds: null }, actual: {}, actual_provenance: "declared" }], events: [] }); return; }
    json(response, { error: { message: "unsupported fixture action" } }, 400); return;
  }
  response.setHeader("Content-Type", "text/html"); response.end('<!doctype html><html><head><link rel="stylesheet" href="/component.css"></head><body><div id="root"></div><script src="/component.js"></script></body></html>');
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
let browser;
try {
  browser = await chromium.launch({ headless: true, args: ["--no-sandbox", "--disable-dev-shm-usage"] });
  const page = await browser.newPage(); const errors = []; page.on("pageerror", error => errors.push(error.message));
  const origin = `http://127.0.0.1:${server.address().port}`;
  await page.goto(origin);
  await page.locator('[data-testid="settings-load-error"]').waitFor();
  assert.match(await page.locator('[data-testid="settings-load-error"]').innerText(), /offline load fixture/);
  await page.evaluate(() => { window.__failLoad = false; });
  await page.getByText("Retry configuration", { exact: true }).click();
  await page.locator('[data-testid="usage-policy-json"]').waitFor();
  assert.equal(await page.locator('[data-testid="expert-config-current"]').textContent().then(text => text.includes("fixture-credential-never-show")), false);
  assert.deepEqual(JSON.parse(await page.locator('[data-testid="usage-policy-json"]').inputValue()), preset);
  const policy = { ...preset, growth_factor: 12.5, baseline_window: null, initial_baselines: { "custom/cohort/unit-v2": { custom_units: null } }, extensions: { owner_variant: "anything" } };
  await page.locator('[data-testid="usage-policy-json"]').fill(JSON.stringify(policy));
  const configWritesBeforePolicyPreview = configWrites.length;
  await page.locator('[data-testid="usage-policy-preview"]').click();
  await page.locator('[data-testid="usage-policy-preview-result"]').waitFor();
  assert.deepEqual(commands.at(-1), { action: "preview_settings", override: policy });
  const previewSettings = JSON.parse(await page.locator('[data-testid="usage-policy-preview-json"]').textContent());
  assert.deepEqual(previewSettings.effective, preset, "preview leaves current settings snapshot unchanged");
  assert.deepEqual(previewSettings.preview.effective, policy);
  assert.equal(previewSettings.preview.persisted, false);
  assert.equal(previewSettings.preview.effective_changed, true);
  assert.equal(previewSettings.hashes.effective, hash(preset));
  assert.equal(previewSettings.preview.hashes.override, hash(policy));
  assert.equal(previewSettings.preview.hashes.effective, hash(policy));
  assert.equal(configWrites.length, configWritesBeforePolicyPreview);
  assert.equal(storedOverride, null);
  assert.equal(requestSequence, 0, "policy settings preview records no ledger admission");
  assert.equal(commands.some(command => ["preview", "request", "confirm", "complete", "cancel", "inspect"].includes(command.action)), false);
  assert.deepEqual(JSON.parse(await page.locator('[data-testid="usage-policy-json"]').inputValue()), policy);
  const invalidPolicy = '{"growth_factor":0}';
  await page.locator('[data-testid="usage-policy-json"]').fill(invalidPolicy);
  await page.locator('[data-testid="usage-policy-preview"]').click();
  await page.waitForFunction(() => document.querySelector('[data-testid="usage-policy-error"]')?.textContent.includes("growth_factor must be positive"));
  assert.equal(await page.locator('[data-testid="usage-policy-json"]').inputValue(), invalidPolicy);
  assert.equal(configWrites.length, configWritesBeforePolicyPreview);
  const commandsBeforeInvalidJson = commands.length;
  await page.locator('[data-testid="usage-policy-json"]').fill("{invalid");
  await page.locator('[data-testid="usage-policy-preview"]').click();
  await page.waitForFunction(() => document.querySelector('[data-testid="usage-policy-error"]')?.textContent.includes("JSON"));
  assert.equal(await page.locator('[data-testid="usage-policy-json"]').inputValue(), "{invalid");
  assert.equal(commands.length, commandsBeforeInvalidJson, "malformed draft never reaches the host");
  assert.equal(configWrites.length, configWritesBeforePolicyPreview);
  await page.locator('[data-testid="usage-policy-json"]').fill(JSON.stringify(policy));
  await page.evaluate(() => { window.__failSave = true; });
  await page.locator('[data-testid="usage-policy-save"]').click();
  await page.locator('[data-testid="usage-policy-error"]').waitFor();
  assert.deepEqual(JSON.parse(await page.locator('[data-testid="usage-policy-json"]').inputValue()), policy);
  assert.equal(configWrites.length, 0);
  await page.locator('[data-testid="usage-settings-refresh"]').click();
  await page.waitForFunction(() => document.querySelector('[data-testid="usage-policy-notice"]')?.textContent.includes("unsaved policy draft"));
  assert.deepEqual(JSON.parse(await page.locator('[data-testid="usage-policy-json"]').inputValue()), policy);
  await page.evaluate(() => { window.__failSave = false; });
  await page.locator('[data-testid="usage-policy-save"]').click();
  await page.waitForFunction(() => document.querySelector('[data-testid="usage-effective"]')?.textContent.includes("12.5"));
  assert.deepEqual(configWrites.at(-1), { loom_usage_policy: policy });
  assert.deepEqual(storedOverride, policy);

  const estimate = { operation_id: "op-1", baseline_key: "custom/cohort/unit-v2", resources: { custom_units: 20, cpu_seconds: null }, extensions: { custom: true } };
  await page.locator('[data-testid="usage-estimate-json"]').fill(JSON.stringify(estimate));
  await page.locator('[data-testid="usage-preview"]').click();
  await page.locator('[data-testid="usage-preview-result"]').waitFor();
  assert.equal(await page.locator('[data-testid="usage-receipt"]').count(), 0);
  assert.equal(commands.filter(command => command.action === "confirm").length, 0);
  await page.locator('[data-testid="usage-request"]').click();
  await page.locator('[data-testid="usage-receipt"]').waitFor();
  assert.equal(await page.locator('[data-testid="usage-receipt-id"]').innerText(), "receipt-1");
  assert.match(await page.locator('[data-testid="usage-receipt"]').innerText(), /custom_units/);
  assert.match(await page.locator('[data-testid="usage-receipt"]').innerText(), /unknown/);
  assert.equal(await page.locator('[data-testid="usage-approve"]').isDisabled(), true);
  assert.equal(commands.filter(command => command.action === "confirm").length, 0);
  await page.locator('[data-testid="usage-confirmation-ref"]').fill("owner-ref-1");
  // Editing the estimate does not mutate the receipt being approved/refreshed.
  await page.locator('[data-testid="usage-estimate-json"]').fill(JSON.stringify({ ...estimate, operation_id: "edited-unsent" }));
  await page.locator('[data-testid="usage-approve"]').click();
  await page.locator('[data-testid="usage-receipt-stale"]').waitFor();
  assert.equal(await page.locator('[data-testid="usage-approve"]').isDisabled(), true);
  assert.equal(await page.locator('[data-testid="usage-reject"]').isDisabled(), true);
  assert.equal(commands.filter(command => command.action === "request").length, 1);
  assert.deepEqual(commands.at(-1), { action: "confirm", operation_id: "op-1", receipt_id: "receipt-1", approved: true, confirmation_ref: "owner-ref-1" });
  await page.locator('[data-testid="usage-receipt-refresh"]').click();
  await page.waitForFunction(() => document.querySelector('[data-testid="usage-receipt-id"]')?.textContent === "receipt-2");
  assert.deepEqual(commands.at(-1), { action: "request", estimate });
  assert.equal(await page.locator('[data-testid="usage-confirmation-ref"]').inputValue(), "");
  assert.equal(await page.locator('[data-testid="usage-approve"]').isDisabled(), true);
  await page.locator('[data-testid="usage-confirmation-ref"]').fill("owner-ref-2");
  await page.locator('[data-testid="usage-approve"]').click();
  await page.waitForFunction(() => document.querySelector('[data-testid="usage-policy-notice"]')?.textContent.includes("Approval recorded"));
  assert.deepEqual(commands.at(-1), { action: "confirm", operation_id: "op-1", receipt_id: "receipt-2", approved: true, confirmation_ref: "owner-ref-2" });
  assert.equal(await page.locator('[data-testid="usage-approve"]').count(), 0);
  await page.locator('[data-testid="usage-estimate-json"]').fill(JSON.stringify({ ...estimate, operation_id: "op-reject" }));
  await page.locator('[data-testid="usage-request"]').click();
  await page.waitForFunction(() => document.querySelector('[data-testid="usage-receipt-id"]')?.textContent === "receipt-3");
  await page.locator('[data-testid="usage-confirmation-ref"]').fill("owner-reject-ref");
  await page.locator('[data-testid="usage-reject"]').click();
  await page.waitForFunction(() => document.querySelector('[data-testid="usage-policy-notice"]')?.textContent.includes("Rejection recorded"));
  assert.equal(commands.at(-1).approved, false);
  await page.locator('[data-testid="usage-inspect-cohort"]').fill(estimate.baseline_key);
  await page.locator('[data-testid="usage-inspect"]').click();
  await page.locator('[data-testid="usage-inspection"]').waitFor();
  assert.match(await page.locator('[data-testid="usage-inspected-operation"]').innerText(), /interrupted-op — unresolved; remaining:.*cpu_seconds/s);

  const generic = { temperature: 3.5, default_model: "owner-custom-model", custom_anything: { arbitrary: 9000000000, explicit_null: null } };
  await page.locator('[data-testid="expert-config-json"]').fill(JSON.stringify(generic));
  await page.evaluate(() => { window.__failSave = true; });
  await page.locator('[data-testid="expert-config-save"]').click();
  await page.locator('[data-testid="settings-save-error"]').waitFor();
  assert.deepEqual(JSON.parse(await page.locator('[data-testid="expert-config-json"]').inputValue()), generic);
  await page.evaluate(() => { window.__failSave = false; });
  await page.locator('[data-testid="expert-config-save"]').click();
  await page.waitForFunction(() => document.querySelector('[data-testid="expert-config"]')?.textContent.includes("Configuration patch saved"));
  assert.deepEqual(configWrites.at(-1), generic);
  assert.equal(await page.locator('[data-testid="cfg-default-model"]').inputValue(), "owner-custom-model");
  assert.equal(await page.locator('#cfg-temp').inputValue(), "3.5");
  const beforeSecret = configWrites.length;
  await page.locator('[data-testid="expert-config-json"]').fill('{"api_key":"do-not-store"}');
  await page.locator('[data-testid="expert-config-save"]').click();
  await page.locator('[data-testid="settings-save-error"]').waitFor();
  assert.match(await page.locator('[data-testid="settings-save-error"]').innerText(), /Store credentials/);
  assert.equal(configWrites.length, beforeSecret);
  await page.goto(`${origin}/unsupported`);
  await page.locator('[data-testid="usage-policy-error"]').waitFor();
  assert.match(await page.locator('[data-testid="usage-policy-error"]').innerText(), /unavailable in this host/);
  assert.equal(await page.locator('[data-testid="usage-preview"]').isDisabled(), true);
  assert.deepEqual(errors, []);
  console.log("[usage-policy-ui] 1/1 group passed: preset/override/effective, read-only override preview + hashes + invalid draft preservation, whole-object policy, failed draft preservation, preview vs request, exact approval/rejection, stale lockout/manual refresh, unknown reservations, config/secret separation, unavailable host");
} finally { await browser?.close(); await new Promise(resolve => server.close(resolve)); }
