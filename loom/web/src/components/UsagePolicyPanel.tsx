import { useCallback, useEffect, useState } from "react";
import { api } from "../api";
import type { ConfigMap } from "../api/types";
import "./usage-policy.css";

type JsonObject = Record<string, unknown>;
interface PolicyTransport {
  usagePolicy?: (command: JsonObject) => Promise<JsonObject>;
  setConfigKey: (key: string, value: unknown) => Promise<ConfigMap>;
}

function object(value: unknown): JsonObject {
  return value && typeof value === "object" && !Array.isArray(value) ? value as JsonObject : {};
}
function parseObject(value: string, label: string): JsonObject {
  const parsed: unknown = JSON.parse(value);
  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) throw new Error(`${label} must be a JSON object.`);
  return parsed as JsonObject;
}
function pretty(value: unknown) { return JSON.stringify(value ?? null, null, 2); }
function describe(value: unknown): string {
  if (value == null) return "unknown";
  return typeof value === "object" ? JSON.stringify(value) : String(value);
}
function errorText(error: unknown) { return error instanceof Error ? error.message : String(error); }

function ResourceTable({ resources }: { resources: unknown }) {
  const rows = Object.entries(object(resources));
  if (!rows.length) return <p>No resource diagnostics returned.</p>;
  return <div className="usage-table-scroll"><table className="usage-resource-table">
    <thead><tr><th>Resource / unit</th><th>Baseline</th><th>Source / samples</th><th>Estimate</th><th>Reserved</th><th>Projected</th><th>Growth</th><th>Confirmation</th></tr></thead>
    <tbody>{rows.map(([name, raw]) => {
      const row = object(raw);
      return <tr key={name} data-triggering={row.requires_confirmation === true}>
        <th scope="row">{name}</th><td>{describe(row.baseline)}</td>
        <td>{describe(row.baseline_source)} / {describe(row.baseline_samples)}</td>
        <td>{describe(row.estimate)}</td><td>{describe(row.reserved)}<small>Known lower bound: {describe(row.reserved_lower_bound)}</small></td>
        <td>{describe(row.projected)}<small>{describe(row.projection_status)}; lower bound: {describe(row.projected_lower_bound)}</small></td>
        <td>{row.ratio == null ? "unknown" : `×${String(row.ratio)}`}</td><td>{row.requires_confirmation === true ? "Required" : "Not triggered"}</td>
      </tr>;
    })}</tbody>
  </table></div>;
}

/** Admission ledger controls only. This component never dispatches the estimated operation. */
export default function UsagePolicyPanel({ transport = api, onConfigSaved }: {
  transport?: PolicyTransport;
  onConfigSaved?: (config: ConfigMap) => void;
}) {
  const [settings, setSettings] = useState<JsonObject | null>(null);
  const [policyDraft, setPolicyDraft] = useState("");
  const [policyDirty, setPolicyDirty] = useState(false);
  const [policyPreview, setPolicyPreview] = useState<JsonObject | null>(null);
  const [previewedPolicyDraft, setPreviewedPolicyDraft] = useState("");
  const [estimateDraft, setEstimateDraft] = useState('{\n  "operation_id": "",\n  "baseline_key": "",\n  "resources": {}\n}');
  const [preview, setPreview] = useState<JsonObject | null>(null);
  const [receipt, setReceipt] = useState<JsonObject | null>(null);
  const [receiptStale, setReceiptStale] = useState(false);
  const [confirmationRef, setConfirmationRef] = useState("");
  const [cohort, setCohort] = useState("");
  const [inspection, setInspection] = useState<JsonObject | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const command = useCallback(async (payload: JsonObject) => {
    if (!transport.usagePolicy) throw new Error("Usage policy commands are unavailable in this host. The host needs the usage-policy API.");
    const result = await transport.usagePolicy(payload);
    // Mock and older transports may pass through the native error envelope.
    const nativeError = object(result.error);
    if (typeof nativeError.message === "string") throw new Error(nativeError.message);
    return result;
  }, [transport]);
  const readSettings = useCallback(async () => {
    const result = await command({ action: "settings" });
    setSettings(result);
    return result;
  }, [command]);
  useEffect(() => {
    let active = true;
    command({ action: "settings" }).then(result => {
      if (!active) return;
      setSettings(result); setPolicyDraft(pretty(result.effective));
    }).catch(cause => { if (active) setError(errorText(cause)); });
    return () => { active = false; };
  }, [command]);

  const run = async (operation: () => Promise<void>) => {
    setBusy(true); setError(""); setNotice("");
    try { await operation(); } catch (cause) { setError(errorText(cause)); } finally { setBusy(false); }
  };
  const refreshSettings = () => run(async () => {
    const result = await readSettings();
    if (!policyDirty) setPolicyDraft(pretty(result.effective));
    setNotice(policyDirty ? "Settings refreshed; your unsaved policy draft is preserved." : "Settings refreshed.");
  });
  const savePolicy = () => run(async () => {
    const policy = parseObject(policyDraft, "Policy");
    const config = await transport.setConfigKey("loom_usage_policy", policy);
    onConfigSaved?.(config); setPolicyDirty(false);
    setNotice("Policy saved. It applies on the next policy open; this is not an execution approval.");
    await readSettings();
  });
  const previewPolicy = () => run(async () => {
    const override = parseObject(policyDraft, "Policy override");
    const result = await command({ action: "preview_settings", override });
    setPolicyPreview(result); setPreviewedPolicyDraft(policyDraft);
    setNotice("Policy override previewed without saving configuration or opening the ledger. This advisory snapshot is not an execution approval.");
  });
  const estimateOperation = (action: "preview" | "request") => run(async () => {
    const estimate = parseObject(estimateDraft, "Estimate");
    const result = await command({ action, estimate });
    if (action === "preview") { setPreview(result); setNotice("Preview only: no reservation, execution approval or dispatch."); }
    else { setReceipt(result); setReceiptStale(false); setConfirmationRef(""); setNotice("Admission request recorded in the policy ledger. No operation was dispatched."); }
  });
  const confirm = (approved: boolean) => run(async () => {
    if (!receipt || receiptStale || receipt.status !== "requires_confirmation" || !confirmationRef.trim()) return;
    try {
      const result = await command({ action: "confirm", operation_id: receipt.operation_id,
        receipt_id: receipt.receipt_id, approved, confirmation_ref: confirmationRef.trim() });
      setReceipt(result);
      setNotice(approved ? "Approval recorded for this exact receipt. No operation was dispatched." : "Rejection recorded for this exact receipt.");
    } catch (cause) {
      // Do not automatically retry, refresh or approve an unknown/new projection.
      setReceiptStale(true);
      throw cause;
    }
  });
  const refreshReceipt = () => run(async () => {
    if (!receipt) return;
    const result = await command({ action: "request", estimate: receipt.estimate });
    setReceipt(result); setReceiptStale(false); setConfirmationRef("");
    setNotice("Receipt refreshed using its original estimate. Review it and enter a new decision reference before confirming.");
  });
  const inspect = () => run(async () => {
    setInspection(await command({ action: "inspect", baseline_key: cohort }));
  });
  const pending = receipt?.status === "requires_confirmation";
  const canConfirm = pending && !receiptStale && Boolean(confirmationRef.trim()) && !busy;
  const operations = Array.isArray(inspection?.operations) ? inspection.operations.map(object) : [];

  return <section className="usage-policy-panel" data-testid="usage-policy-panel" aria-label="Usage policy">
    <div className="section-title">Usage policy and growth confirmation</div>
    <p>These controls read and write the usage-policy ledger. They do not dispatch imports, chat, models or other operations. Production adapters must adopt the policy lifecycle separately.</p>
    <p>Defaults are editable presets. A cohort groups comparable operations and resource units chosen by the caller. Unknown usage is shown as unknown; it never means zero.</p>
    {error && <div role="alert" data-testid="usage-policy-error">{error}</div>}
    {notice && <p role="status" data-testid="usage-policy-notice">{notice}</p>}
    <button onClick={refreshSettings} disabled={busy || !transport.usagePolicy} data-testid="usage-settings-refresh">Refresh policy settings</button>
    {settings && <>
      <p>Source: <strong>{describe(settings.source)}</strong>. Ledger: <code>{describe(settings.ledger_path)}</code>. Preset application: <code>{describe(object(settings.capabilities).preset_application)}</code>.</p>
      {(settings.hashes || settings.preset_source || settings.override_semantics) && <details><summary>Policy snapshot source and hashes</summary><pre data-testid="usage-settings-metadata">{pretty({ preset_source: settings.preset_source, override_semantics: settings.override_semantics, hashes: settings.hashes })}</pre></details>}
      <div className="usage-settings-values">
        <details><summary>Preset</summary><pre data-testid="usage-preset">{pretty(settings.preset)}</pre></details>
        <details><summary>Stored owner override</summary><pre data-testid="usage-override">{pretty(settings.stored_override)}</pre></details>
        <details open><summary>Effective policy</summary><pre data-testid="usage-effective">{pretty(settings.effective)}</pre></details>
      </div>
      <div className="form-row"><label htmlFor="usage-policy-json">Complete policy object (including arbitrary cohorts, resource names and extensions)</label>
        <textarea id="usage-policy-json" className="usage-json-editor" rows={12} value={policyDraft} onChange={event => { setPolicyDraft(event.target.value); setPolicyDirty(true); }} data-testid="usage-policy-json" />
      </div>
      <p>Saving replaces the complete top-level <code>loom_usage_policy</code> value. Nested objects are not merge-patched; null is a stored value. An empty override restores preset values and remains a stored override. Outstanding receipts keep their own snapshots.</p>
      <div className="usage-actions">
        <button onClick={previewPolicy} disabled={busy || !transport.usagePolicy} data-testid="usage-policy-preview">Preview policy override</button>
        <button onClick={savePolicy} disabled={busy} data-testid="usage-policy-save">Save complete policy</button>
      </div>
      {policyPreview && <details open data-testid="usage-policy-preview-result"><summary>Advisory policy override preview — no settings saved</summary>
        <p>Effective policy changed: <strong>{describe(object(policyPreview.preview).effective_changed)}</strong>. Persisted: <strong>{describe(object(policyPreview.preview).persisted)}</strong>.</p>
        <p>The snapshot hashes identify parsed JSON. Previewing is not a compare-and-set write; settings may change before saving.</p>
        {policyDraft !== previewedPolicyDraft && <p role="status">The draft changed after this preview. The response below describes the captured override.</p>}
        <pre data-testid="usage-policy-preview-json">{pretty(policyPreview)}</pre>
      </details>}
      <details><summary>Host capabilities</summary><pre>{pretty(settings.capabilities)}</pre></details>
    </>}
    <div className="section-title">Estimate and admission ledger</div>
    <div className="form-row"><label htmlFor="usage-estimate-json">Estimate: operation ID, comparable cohort and resource amounts (null = unknown)</label>
      <textarea id="usage-estimate-json" className="usage-json-editor" rows={8} value={estimateDraft} onChange={event => setEstimateDraft(event.target.value)} data-testid="usage-estimate-json" />
    </div>
    <div className="usage-actions">
      <button onClick={() => estimateOperation("preview")} disabled={busy || !transport.usagePolicy} data-testid="usage-preview">Preview (read only)</button>
      <button onClick={() => estimateOperation("request")} disabled={busy || !transport.usagePolicy} data-testid="usage-request">Record admission request</button>
    </div>
    {preview && <details open data-testid="usage-preview-result"><summary>Preview — {describe(preview.status)}, no authorization</summary><ResourceTable resources={preview.resources} /><pre>{pretty(preview)}</pre></details>}
    {receipt && <div className="usage-receipt" data-testid="usage-receipt">
      <h3>Recorded receipt</h3>
      <p>Operation: <code>{describe(receipt.operation_id)}</code>. Cohort: <code>{describe(receipt.baseline_key)}</code>.</p>
      <p>Receipt: <code data-testid="usage-receipt-id">{describe(receipt.receipt_id)}</code>. Status: <strong>{describe(receipt.status)}</strong>. Ledger authorization: <strong>{describe(receipt.authorized)}</strong>.</p>
      <p>This receipt addresses its recorded estimate below. Editing the draft above does not change it. An authorization is not proof of dispatch and cannot authorize another operation.</p>
      <ResourceTable resources={receipt.resources} />
      <details open><summary>Exact estimate, policy snapshot and full receipt</summary><pre data-testid="usage-receipt-json">{pretty(receipt)}</pre></details>
      {receiptStale && <p role="alert" data-testid="usage-receipt-stale">Confirmation failed. This receipt is locked until you explicitly refresh and review it. No automatic retry or approval.</p>}
      {pending && <>
        <div className="form-row"><label htmlFor="usage-confirmation-ref">Owner decision reference for this exact receipt</label>
          <input id="usage-confirmation-ref" value={confirmationRef} onChange={event => setConfirmationRef(event.target.value)} data-testid="usage-confirmation-ref" />
        </div>
        <div className="usage-actions">
          <button onClick={() => confirm(true)} disabled={!canConfirm} data-testid="usage-approve">Approve this receipt</button>
          <button onClick={() => confirm(false)} disabled={!canConfirm} data-testid="usage-reject">Reject this receipt</button>
          <button onClick={refreshReceipt} disabled={busy} data-testid="usage-receipt-refresh">Refresh recorded receipt</button>
        </div>
      </>}
    </div>}
    <div className="section-title">Inspect cohort and unresolved reservations</div>
    <div className="form-row"><label htmlFor="usage-inspect-cohort">Comparable cohort / baseline key</label><input id="usage-inspect-cohort" value={cohort} onChange={event => setCohort(event.target.value)} data-testid="usage-inspect-cohort" /></div>
    <button onClick={inspect} disabled={busy || !cohort.trim() || !transport.usagePolicy} data-testid="usage-inspect">Inspect ledger</button>
    {inspection && <div data-testid="usage-inspection">
      <p>Inspected cohort: <code>{describe(inspection.baseline_key)}</code></p>
      <details open><summary>Baselines and reservations</summary><pre>{pretty({ baseline: inspection.baseline, reservations: inspection.reservations })}</pre></details>
      {operations.map((operation, index) => <details key={`${String(operation.operation_id)}-${index}`} data-testid="usage-inspected-operation">
        <summary>{describe(operation.operation_id)} — {describe(operation.status)}; remaining: {pretty(operation.remaining)}</summary>
        <p>Actual measurement provenance and unknown dimensions are retained in the complete operation record. Completion or cancellation must be recorded by the execution adapter or an explicit recovery action.</p>
        <pre>{pretty(operation)}</pre>
      </details>)}
      <details><summary>Full inspection, including recorded events</summary><pre>{pretty(inspection)}</pre></details>
    </div>}
  </section>;
}
