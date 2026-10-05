import { useEffect, useRef, useState } from "react";
import { api } from "../api";
import { analysisCall, analysisJson, analysisObject, exactAnalysisCommand, methodIdentityClaims, methodVersions, parseAnalysisObject, readAnalysisPrepared,
  type AnalysisObject, type AnalysisPrepared, type AnalysisTransport } from "../api/analysis";
import "./analysis-panel.css";

const errorText = (error: unknown) => error instanceof Error ? error.message : String(error);
const nativeJson = (value: AnalysisObject, key: string) => {
  if (typeof value[key] !== "string" || !value[key]) throw new Error(`Native ${key} is unavailable; reload through the lossless native API.`);
  return value[key] as string;
};
// Omitted controls inherit the native contract. Unknown forecasts stay unknown.
const defaults = {};

export default function AnalysisPanel({ transport = api }: { transport?: AnalysisTransport }) {
  const [catalog, setCatalog] = useState<AnalysisObject[]>([]);
  const [selected, setSelected] = useState("semantic.analysis");
  const [contractDraft, setContractDraft] = useState("");
  const [bindingsDraft, setBindingsDraft] = useState(analysisJson({ text: "", input_json: {} }));
  const [patchDraft, setPatchDraft] = useState("{}");
  const [executionDraft, setExecutionDraft] = useState(analysisJson(defaults));
  const [model, setModel] = useState("");
  const [provider, setProvider] = useState("");
  const [exactBody, setExactBody] = useState("");
  const [bodyOverride, setBodyOverride] = useState(false);
  const [prepared, setPrepared] = useState<AnalysisPrepared | null>(null);
  const [held, setHeld] = useState<AnalysisPrepared | null>(null);
  const [confirmationRef, setConfirmationRef] = useState("");
  const [busy, setBusy] = useState(false);
  const [dirty, setDirty] = useState(false);
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const [profiles, setProfiles] = useState<AnalysisObject[]>([]);
  const [profileId, setProfileId] = useState("");
  const [snapshot, setSnapshot] = useState<AnalysisObject | null>(null);
  const [versionId, setVersionId] = useState("");
  const [actor, setActor] = useState("owner");
  const [targetDraft, setTargetDraft] = useState("");
  const [resultKind, setResultKind] = useState("");
  const [methodProfileJson, setMethodProfileJson] = useState<string | null>(null);
  const [methodReceipts, setMethodReceipts] = useState<string[]>([]);
  const [resultReceipt, setResultReceipt] = useState<AnalysisObject | null>(null);
  const [versionPreview, setVersionPreview] = useState<AnalysisObject | null>(null);
  const [methodRef, setMethodRef] = useState<AnalysisObject | null>(null);
  const generation = useRef(0), mounted = useRef(true);

  useEffect(() => {
    mounted.current = true;
    const abort = new AbortController();
    analysisCall(transport.analysis?.bind(transport), { operation: "catalog" }, abort.signal).then(async result => {
      if (!mounted.current) return;
      setCatalog(Array.isArray(result.contracts) ? result.contracts.map(analysisObject) : []);
      const runtime = analysisObject(result.runtime);
      setModel(typeof runtime.model === "string" ? runtime.model : "");
      setProvider(typeof runtime.provider === "string" ? runtime.provider : "");
      const effective = await analysisCall(transport.analysis?.bind(transport), { operation: "resolve", contract_id: "semantic.analysis" }, abort.signal);
      if (mounted.current && generation.current === 0) setContractDraft(nativeJson(effective, "contract_json"));
    }).catch(cause => { if (mounted.current && !abort.signal.aborted) setError(errorText(cause)); });
    if (transport.methods) analysisCall(transport.methods?.bind(transport), { action: "catalog" }, abort.signal).then(result => {
      if (mounted.current) setProfiles(Array.isArray(result.profiles) ? result.profiles.map(analysisObject) : []);
    }).catch(() => { /* Optional graph registry availability is shown below. */ });
    return () => { mounted.current = false; generation.current++; abort.abort(); };
  }, [transport]);

  const discard = (value: AnalysisPrepared | null) => {
    if (value && !value.attempted) void analysisCall(transport.analysis?.bind(transport), { operation: "discard", prepared_id: value.prepared_id }).catch(() => {});
  };
  const invalidate = () => {
    generation.current++; discard(prepared); setPrepared(null); setHeld(null); setConfirmationRef("");
    setVersionPreview(null); setMethodRef(null); setResultReceipt(null); setDirty(true); setNotice(""); setError("");
  };
  const run = async (work: (epoch: number) => Promise<void>) => {
    const epoch = generation.current;
    setBusy(true); setError(""); setNotice("");
    try { await work(epoch); }
    catch (cause) { if (mounted.current && epoch === generation.current) setError(errorText(cause)); }
    finally { if (mounted.current) setBusy(false); }
  };
  const loadContract = () => run(async epoch => {
    const result = await analysisCall(transport.analysis?.bind(transport), { operation: "resolve", contract_id: selected });
    if (!mounted.current || epoch !== generation.current) return;
    discard(prepared); generation.current++; setContractDraft(nativeJson(result, "contract_json")); setPrepared(null); setHeld(null);
    setVersionPreview(null); setMethodRef(null); setDirty(false); setBodyOverride(false); setExactBody("");
    setNotice("Effective native contract loaded. Existing source bindings are preserved.");
  });
  const prepare = () => run(async epoch => {
    parseAnalysisObject(contractDraft, "Prompt contract");
    const resolved = await analysisCall(transport.analysis?.bind(transport), { operation: "resolve", prompt_snapshot_json: contractDraft });
    const selectedVersion = snapshot ? methodVersions(snapshot).find(item => item.id === versionId) : undefined;
    const registeredContractHash = analysisObject(analysisObject(selectedVersion?.attrs).definition).analysis_contract_sha256;
    const matchingMethod = registeredContractHash === resolved.contract_hash;
    const result = readAnalysisPrepared(await analysisCall(transport.analysis?.bind(transport), {
      operation: "prepare", prompt_snapshot_json: contractDraft,
      bindings: parseAnalysisObject(bindingsDraft, "Bindings"), request_patch: parseAnalysisObject(patchDraft, "Request patch"),
      execution: parseAnalysisObject(executionDraft, "Execution presets and forecasts"), model, provider,
      ...(bodyOverride ? { body_bytes: exactBody } : {}), ...(methodRef ? { method_ref: methodRef } : {})
      , ...(matchingMethod && methodProfileJson && versionId && targetDraft.trim() ? { method: {
        profile_json: methodProfileJson, receipt_ids: methodReceipts,
        selection_json: JSON.stringify({ members: [{ method_version_id: versionId }], fusion: null }),
        target: targetDraft.trim(), known_at: new Date().toISOString(),
        origin: { kind: "user", actor: actor.trim(), model: null, recipe_sha256: null, response_sha256: null }
      } } : {})
    }));
    if (!mounted.current || epoch !== generation.current) { discard(result); return; }
    discard(prepared); setPrepared(result); setHeld(null); setConfirmationRef("");
    if (!bodyOverride) setExactBody(result.request.body_bytes);
    setNotice("Prepared without a provider call or usage reservation. Review the complete query before sending.");
  });
  const send = (value = prepared, confirmation?: AnalysisObject) => run(async epoch => {
    if (!value) throw new Error("Prepare and review this draft first.");
    // Only the immutable identity is sent; drafts are never reconstructed at dispatch.
    let result: AnalysisPrepared;
    try { result = readAnalysisPrepared(await analysisCall(transport.analysis?.bind(transport), exactAnalysisCommand(value, confirmation))); }
    catch (cause) {
      if (mounted.current && epoch === generation.current) {
        setPrepared({ ...value, attempted: true, status: "outcome_unknown" }); setHeld(null);
        setNotice("The outcome is unknown. Inspect this handle; it will not be sent again automatically.");
      }
      throw cause;
    }
    if (!mounted.current || epoch !== generation.current) return;
    if (result.prepared_id !== value.prepared_id || result.request_identity_hash !== value.request_identity_hash)
      throw new Error("Execution returned a different prepared identity. No retry was made.");
    setPrepared(result); setHeld(result.status === "requires_confirmation" ? result : null); setConfirmationRef("");
    setNotice(result.attempted ? "The single attempt is retained with first-response provenance and validation." : `No provider call was made: ${result.status}.`);
  });
  const inspect = () => run(async epoch => {
    if (!prepared) return;
    const result = readAnalysisPrepared(await analysisCall(transport.analysis?.bind(transport), { operation: "inspect", prepared_id: prepared.prepared_id }));
    if (mounted.current && epoch === generation.current) { setPrepared(result); setHeld(result.status === "requires_confirmation" ? result : null); setNotice("Saved attempt inspected. No provider call was made."); }
  });
  const setMode = (mode: string) => run(async epoch => {
    parseAnalysisObject(contractDraft, "Prompt contract");
    const resolved = await analysisCall(transport.analysis?.bind(transport), { operation: "resolve", prompt_snapshot_json: contractDraft, prompt_patch: { validation_mode: mode } });
    if (!mounted.current || epoch !== generation.current) return;
    const text = nativeJson(resolved, "contract_json");
    invalidate(); setContractDraft(text);
  });
  const loadProfile = () => run(async epoch => {
    const entry = profiles.find(item => item.id === profileId);
    if (!entry) throw new Error("Choose an available native method profile.");
    const loaded = await analysisCall(transport.methods?.bind(transport), { action: "load", profile_json: nativeJson(entry, "profile_json"), receipt_ids: entry.receipt_ids ?? [] });
    if (mounted.current && epoch === generation.current) {
      setSnapshot(loaded); const versions = methodVersions(loaded); setVersionId(String(versions[0]?.id ?? "")); setVersionPreview(null);
      setMethodProfileJson(nativeJson(loaded, "profile_json")); setMethodReceipts(Array.isArray(loaded.receipts) ? loaded.receipts.map(item => String(analysisObject(item).receipt_id)) : []);
      const definition = analysisObject(analysisObject(versions[0]?.attrs).definition);
      setResultKind(typeof definition.analysis_result_kind === "string" ? definition.analysis_result_kind : "");
      setNotice("Native method versions loaded. Choose the immutable version to extend.");
    }
  });
  const previewVersion = () => run(async epoch => {
    if (!snapshot) throw new Error("Load a native method profile first.");
    const entity = methodVersions(snapshot).find(item => item.id === versionId);
    if (!entity) throw new Error("Choose an actual native method version.");
    const identityClaims = methodIdentityClaims(snapshot, versionId);
    if (!identityClaims.length) throw new Error("The selected version has no active native method identity Claim.");
    parseAnalysisObject(contractDraft, "Prompt contract");
    parseAnalysisObject(executionDraft, "Execution presets"); parseAnalysisObject(patchDraft, "Request patch");
    const resolved = await analysisCall(transport.analysis?.bind(transport), { operation: "resolve", prompt_snapshot_json: contractDraft });
    const attrsPatch = `{"definition":{"analysis_contract":${nativeJson(resolved, "contract_json")},"analysis_contract_sha256":${JSON.stringify(resolved.contract_hash)},"execution_capability":"analysis.http","analysis_result_kind":${JSON.stringify(resultKind.trim())},"analysis_execution_presets":${executionDraft},"analysis_request_patch":${patchDraft}}}`;
    const result = await analysisCall(transport.methods?.bind(transport), { action: "version_edit", snapshot_json: nativeJson(snapshot, "snapshot_json"), entity_id: entity.id,
      attrs_patch_json: attrsPatch,
      outgoing_claim_ids: identityClaims,
      attrs_remove_paths: ["recipe_sha256", "prompt_sha256", "preset_sha256", "parameter_set_sha256"].map(key => ["definition", key]),
      target: targetDraft.trim(), known_at: new Date().toISOString(), actor: actor.trim() });
    if (mounted.current && epoch === generation.current) { setVersionPreview(result); setNotice("New immutable method version prepared. Review the native acceptance request before saving."); }
  });
  const acceptVersion = () => run(async epoch => {
    if (!versionPreview || typeof versionPreview.accept_request_json !== "string") throw new Error("Preview a new method version first.");
    const preview = versionPreview;
    const result = await analysisCall(transport.methods?.bind(transport), { action: "accept", request_json: nativeJson(preview, "accept_request_json") });
    if (!mounted.current || epoch !== generation.current) return;
    const receipt = analysisObject(result.receipt ?? result);
    const receiptId = String(receipt.receipt_id ?? receipt.id ?? "");
    if (!receiptId) throw new Error("Native acceptance did not return a receipt identity.");
    const profileJson = nativeJson(preview, "profile_json");
    const receipts = [...methodReceipts, receiptId];
    const loaded = await analysisCall(transport.methods?.bind(transport), { action: "load", profile_json: profileJson, receipt_ids: receipts });
    if (!mounted.current || epoch !== generation.current) return;
    setSnapshot(loaded); setMethodProfileJson(profileJson); setMethodReceipts(receipts); setVersionId(String(preview.new_version_id));
    setMethodRef({ version_id: preview.new_version_id, definition_sha256: preview.definition_sha256,
      source_id: preview.source_id, acceptance_receipt_id: receiptId });
    setVersionPreview(null); setDirty(false); discard(prepared); setPrepared(null); setHeld(null);
    setNotice("New graph method version saved through native acceptance. Prepare again to bind the actual method, parameters and run.");
  });
  const acceptResult = () => run(async epoch => {
    if (!prepared?.result.accept_request_json) throw new Error("This attempt has no native bound result packet.");
    const result = await analysisCall(transport.methods?.bind(transport), { action: "accept", request_json: nativeJson(prepared.result, "accept_request_json") });
    if (mounted.current && epoch === generation.current) { setResultReceipt(result); setNotice("The result packet and its actual run/version edges were accepted. Model content remains unverified."); }
  });

  const decision = analysisObject(prepared?.result.usage_decision);
  const versions = snapshot ? methodVersions(snapshot) : [];
  return <section className="analysis-panel" aria-label="Analysis workbench" data-testid="analysis-panel">
    <h2>Analysis workbench</h2>
    <p>Inspect and edit the native prompt, output schema, parameters and presets. Preparation makes no provider call. Send uses the saved exact request once; credentials stay redacted.</p>
    {!transport.analysis && <p role="status">Native analysis is unavailable in this host. Drafts remain editable.</p>}
    <div className="analysis-actions"><label>Native contract<select aria-label="Native analysis contract" value={selected} onChange={event => setSelected(event.target.value)}>
      {catalog.map(item => <option key={String(item.id)} value={String(item.id)}>{String(item.id)} · v{String(item.version)}</option>)}</select></label>
      <button disabled={busy || !transport.analysis} onClick={() => void loadContract()}>Load effective contract</button></div>
    {dirty && <p data-testid="analysis-draft-dirty">Unsaved draft. Loading a contract replaces its draft; refreshing a prepared result preserves edits.</p>}
    <label>Prompt contract JSON: prompts, output schema, parameters and presets<textarea data-testid="analysis-contract" value={contractDraft} spellCheck={false} onChange={event => { invalidate(); setContractDraft(event.target.value); }} /></label>
    <div className="analysis-actions" aria-label="Schema validation mode"><span>Schema validation</span>{["strict", "lenient", "off"].map(mode => <button key={mode} disabled={busy} onClick={() => void setMode(mode)}>{mode}</button>)}</div>
    <div className="analysis-grid"><label>Model<input data-testid="analysis-model" value={model} onChange={event => { invalidate(); setModel(event.target.value); }} /></label>
      <label>Provider base URL<input data-testid="analysis-provider" value={provider} onChange={event => { invalidate(); setProvider(event.target.value); }} /></label></div>
    <label>Source and query bindings JSON<textarea data-testid="analysis-bindings" value={bindingsDraft} spellCheck={false} onChange={event => { invalidate(); setBindingsDraft(event.target.value); }} /></label>
    <label>Final provider request patch JSON<textarea data-testid="analysis-request-patch" value={patchDraft} spellCheck={false} onChange={event => { invalidate(); setPatchDraft(event.target.value); }} /></label>
    <label>Execution presets and usage forecasts JSON<textarea data-testid="analysis-execution" value={executionDraft} spellCheck={false} onChange={event => { invalidate(); setExecutionDraft(event.target.value); }} /></label>
    <p>Expected output tokens, response bytes and cost may be numbers or null. Output caps are presets, not forecasts.</p>
    <label className="analysis-check"><input type="checkbox" data-testid="analysis-body-override" checked={bodyOverride} onChange={event => { invalidate(); setBodyOverride(event.target.checked); }} />Use the exact edited body bytes</label>
    <label>Complete provider body, including whitespace<textarea data-testid="analysis-body" value={exactBody} spellCheck={false} onChange={event => { invalidate(); setExactBody(event.target.value); setBodyOverride(true); }} /></label>
    <div className="analysis-actions"><button data-testid="analysis-prepare" disabled={busy || !transport.analysis || !contractDraft} onClick={() => void prepare()}>Prepare full query</button>
      <button data-testid="analysis-send" disabled={busy || !prepared || prepared.attempted || prepared.graph_binding_available !== true || !!held || !transport.analysis} onClick={() => void send()}>Send this exact query once</button>
      <button data-testid="analysis-inspect" disabled={busy || !prepared || !transport.analysis} onClick={() => void inspect()}>Inspect saved attempt</button></div>
    {notice && <p role="status" data-testid="analysis-notice">{notice}</p>}
    {error && <p role="alert" data-testid="analysis-error">{error}</p>}
    {prepared && <details open><summary>Prepared request and backend query</summary><pre data-testid="analysis-prepared">{analysisJson(prepared)}</pre></details>}
    {prepared && prepared.graph_binding_available !== true && <p role="status">Execution needs a saved native analysis method version. The query preview remains available.</p>}
    {prepared && Boolean(prepared.result.accept_request_json) && <details open><summary>Result packet with native method and run lineage</summary><pre data-testid="analysis-result-graph">{analysisJson(prepared.result)}</pre>
      <button data-testid="analysis-result-accept" disabled={busy || !transport.methods || !!resultReceipt} onClick={() => void acceptResult()}>Accept this result graph packet</button>
      {resultReceipt && <pre data-testid="analysis-result-receipt">{analysisJson(resultReceipt)}</pre>}</details>}
    {held && <section aria-label="Analysis usage confirmation"><h3>Review this usage receipt</h3><pre data-testid="analysis-receipt">{analysisJson(decision)}</pre>
      <label>Decision reference<input data-testid="analysis-confirm-ref" value={confirmationRef} onChange={event => setConfirmationRef(event.target.value)} /></label>
      <button data-testid="analysis-confirm" disabled={busy || !confirmationRef.trim()} onClick={() => void send(held, { receipt_id: decision.receipt_id, approved: true, ref: confirmationRef.trim() })}>Confirm increase and send the held query</button>
      <button data-testid="analysis-decline" disabled={busy || !confirmationRef.trim()} onClick={() => void send(held, { receipt_id: decision.receipt_id, approved: false, ref: confirmationRef.trim() })}>Decline</button></section>}
    <details><summary>Save a new graph method version</summary>
      {!transport.methods || !profiles.length ? <p>Saving requires an available native method profile and an actual immutable version. No local substitute is created.</p> : <>
        <label>Native method profile<select data-testid="analysis-method-profile" value={profileId} onChange={event => { invalidate(); setProfileId(event.target.value); setSnapshot(null); setMethodProfileJson(null); setMethodReceipts([]); setVersionPreview(null); }}><option value="">Choose profile</option>
          {profiles.map(item => <option key={String(item.id)} value={String(item.id)}>{String(item.id)}</option>)}</select></label>
        <button disabled={busy || !profileId} onClick={() => void loadProfile()}>Load native method versions</button>
        <label>Version to extend<select data-testid="analysis-method-version" value={versionId} onChange={event => { invalidate(); setVersionId(event.target.value); setVersionPreview(null); }}>
          {versions.map(item => <option key={String(item.id)} value={String(item.id)}>{String(item.label ?? item.id)}</option>)}</select></label>
        <label>Owner actor<input value={actor} onChange={event => { invalidate(); setActor(event.target.value); setVersionPreview(null); }} /></label>
        <label>Native library target<input data-testid="analysis-method-target" value={targetDraft} onChange={event => { invalidate(); setTargetDraft(event.target.value); setVersionPreview(null); }} /></label>
        <label>Result entity kind (declared method data)<input data-testid="analysis-result-kind" value={resultKind} onChange={event => { setResultKind(event.target.value); setVersionPreview(null); }} /></label>
        <button data-testid="analysis-version-preview" disabled={busy || !snapshot || !versionId || !actor.trim() || !targetDraft.trim() || !resultKind.trim()} onClick={() => void previewVersion()}>Preview new immutable version</button>
        {versionPreview && <><pre data-testid="analysis-version-request">{analysisJson(versionPreview)}</pre><button data-testid="analysis-version-accept" disabled={busy} onClick={() => void acceptVersion()}>Save this new method version</button></>}
        {methodRef && <pre data-testid="analysis-method-ref">{analysisJson(methodRef)}</pre>}
        <p>The new version keeps the native method identity and declares this host's analysis.http mechanism, exact contract and your result kind. Its parameter and run relations bind the actual request at preparation. Saving makes no provider call.</p>
      </>}
    </details>
  </section>;
}
