import { useEffect, useMemo, useRef, useState } from "react";
import { methodAcceptance, methodClaimActive, methodCommand, methodEntityRole, methodObject, methodPredicateRole,
  methodPretty, methodResultLineage, methodRows, parseMethodObject, parseReceiptIds, saveAcceptedMethodProfile, type MethodJson, type MethodsTransport } from "../methods/graph-methods";
import "./methods-panel.css";

function text(value: unknown): string { return typeof value === "string" ? value : value == null ? "unknown" : JSON.stringify(value); }
function failure(error: unknown): string { return error instanceof Error ? error.message : String(error); }
function FullRecord({ value, label }: { value: unknown; label: string }) {
  return <details><summary>{label}</summary><pre>{methodPretty(value)}</pre></details>;
}

/** Browsing, validation and graph acceptance only; this panel never executes a method. */
export default function MethodsPanel({ transport = {} }: { transport?: MethodsTransport }) {
  const [catalog, setCatalog] = useState<MethodJson | null>(null), [capabilities, setCapabilities] = useState<MethodJson | null>(null);
  const [profileId, setProfileId] = useState(""), [profileDraft, setProfileDraft] = useState("{}"), [receiptsDraft, setReceiptsDraft] = useState("[]");
  const [profileSource, setProfileSource] = useState<MethodJson | null>(null), [saveSlot, setSaveSlot] = useState("");
  const [snapshot, setSnapshot] = useState<MethodJson | null>(null), [selectionDraft, setSelectionDraft] = useState("{}");
  const [resolution, setResolution] = useState<MethodJson | null>(null), [entityId, setEntityId] = useState(""), [attrsDraft, setAttrsDraft] = useState("{}");
  const [target, setTarget] = useState(""), [actor, setActor] = useState(""), [knownAt, setKnownAt] = useState("");
  const [preview, setPreview] = useState<MethodJson | null>(null), [previewStamp, setPreviewStamp] = useState("");
  const [previewKind, setPreviewKind] = useState<"version" | "profile">("version"), [previewReceiptIds, setPreviewReceiptIds] = useState<string[]>([]);
  const [receipt, setReceipt] = useState<MethodJson | null>(null), [lineage, setLineage] = useState<MethodJson | null>(null);
  const [accepted, setAccepted] = useState<{ profile: MethodJson; profileJson: string; receiptIds: string[] } | null>(null), [configSaved, setConfigSaved] = useState(false);
  const [readId, setReadId] = useState(""), [receiptRead, setReceiptRead] = useState<MethodJson | null>(null);
  const [filter, setFilter] = useState(""), [busy, setBusy] = useState(false), [error, setError] = useState(""), [notice, setNotice] = useState("");
  const [producerFilter, setProducerFilter] = useState("");
  const controller = useRef<AbortController | null>(null), alive = useRef(true), locked = useRef(false);
  const stamp = JSON.stringify([snapshot?.snapshot_sha256, profileDraft, receiptsDraft, entityId, attrsDraft, target, actor, knownAt]);
  const entities = useMemo(() => methodRows(snapshot?.entities), [snapshot]), claims = useMemo(() => methodRows(snapshot?.claims), [snapshot]);
  const selected = entities.find(row => row.id === entityId), vocabulary = snapshot?.vocabulary;
  const selectedAttrs = methodObject(selected?.attrs), selectedRole = selected ? methodEntityRole(selected, vocabulary) : null;
  const editable = !!selected && ("definition" in selectedAttrs || "text" in selectedAttrs);
  const profiles = methodRows(catalog?.profiles), configSlots = methodRows(catalog?.config_slots), receipts = methodRows(catalog?.receipts);
  const resultRows = useMemo(() => snapshot ? methodResultLineage(snapshot) : [], [snapshot]);
  const visibleResults = producerFilter ? resultRows.filter(row => row.claims.some(claim => methodPredicateRole(claim, vocabulary) === "produced_by_method_version" && claim.object === producerFilter)) : resultRows;
  const producerIds = [...new Set(resultRows.flatMap(row => row.claims.filter(claim => methodPredicateRole(claim, vocabulary) === "produced_by_method_version").map(claim => text(claim.object))))];

  useEffect(() => {
    alive.current = true;
    const abort = new AbortController(); controller.current = abort;
    if (transport.methods) Promise.all([
      methodCommand(transport, { operation: "capabilities" }, abort.signal),
      methodCommand(transport, { operation: "catalog" }, abort.signal),
    ]).then(([caps, list]) => { if (alive.current && !abort.signal.aborted) { setCapabilities(caps); setCatalog(list); } })
      .catch(cause => { if (alive.current && !abort.signal.aborted) setError(failure(cause)); });
    return () => { alive.current = false; abort.abort(); controller.current?.abort(); };
  }, [transport]);
  const run = async (fn: (signal: AbortSignal) => Promise<void>) => {
    if (locked.current) return;
    locked.current = true; setBusy(true); setError(""); setNotice("");
    const abort = new AbortController(); controller.current = abort;
    try { await fn(abort.signal); } catch (cause) { if (alive.current && !abort.signal.aborted) setError(failure(cause)); }
    finally { locked.current = false; if (alive.current) setBusy(false); }
  };
  const command = (body: MethodJson, signal: AbortSignal) => methodCommand(transport, body, signal);
  const chooseEntity = (row: MethodJson, scope: MethodJson | null = snapshot) => {
    const raw = methodObject(scope?.edit_attrs_json)[text(row.id)];
    setEntityId(text(row.id)); setAttrsDraft(typeof raw === "string" ? raw : methodPretty(row.attrs)); setPreview(null); setPreviewStamp("");
  };
  const installSnapshot = (value: MethodJson) => {
    setSnapshot(value); setResolution(null); setLineage(null); setPreview(null); setPreviewStamp(""); setProducerFilter("");
    setSelectionDraft(methodPretty(methodObject(value.profile).selection ?? {}));
    const first = methodRows(value.entities)[0];
    if (first) chooseEntity(first, value); else { setEntityId(""); setAttrsDraft("{}"); }
  };
  const chooseProfile = (id: string) => {
    setProfileId(id); setSnapshot(null); setPreview(null); setResolution(null); setLineage(null);
    const row = profiles.find(item => item.id === id);
    setProfileSource(row ?? null); setSaveSlot(row ? text(row.id) : "");
    setProfileDraft(row ? typeof row.profile_json === "string" ? row.profile_json : methodPretty(row.profile) : "{}"); setReceiptsDraft(methodPretty(row?.receipt_ids ?? []));
  };
  const load = () => run(async signal => {
    parseMethodObject(profileDraft, "Profile");
    const result = await command({ operation: "load", profile_json: profileDraft, receipt_ids: parseReceiptIds(receiptsDraft) }, signal);
    if (signal.aborted) return;
    installSnapshot(result);
    if (profileSource || "selection" in methodObject(result.profile)) {
      const resolved = await command({ operation: "resolve", snapshot_json: result.snapshot_json, selection_json: typeof profileSource?.selection_overlay_json === "string" ? profileSource.selection_overlay_json : methodPretty(methodObject(profileSource?.selection_overlay)) }, signal);
      if (!signal.aborted) setSelectionDraft(typeof resolved.selection_json === "string" ? resolved.selection_json : methodPretty(resolved.selection));
    }
    if (!signal.aborted) setNotice("Native graph loaded. Definitions do not establish execution or content truth.");
  });
  const refresh = () => run(async signal => {
    const result = await command({ operation: "catalog" }, signal); if (!signal.aborted) setCatalog(result);
  });
  const resolve = () => run(async signal => {
    if (!snapshot) throw new Error("Load the native graph first.");
    parseMethodObject(selectionDraft, "Selection");
    const result = await command({ operation: "resolve", snapshot_json: snapshot.snapshot_json, selection_json: selectionDraft }, signal);
    if (!signal.aborted) setResolution(result);
  });
  const edit = () => run(async signal => {
    if (!snapshot || !selected || !editable) throw new Error("Choose a native version with an editable definition or prompt.");
    if (!target.trim() || !actor.trim() || !knownAt.trim()) throw new Error("Target library, actor and a known-at timestamp are required.");
    parseMethodObject(attrsDraft, "Version attributes");
    const result = await command({ operation: "version_edit", snapshot_json: snapshot.snapshot_json, entity_id: entityId,
      attrs_json: attrsDraft, target, actor, known_at: knownAt }, signal);
    methodAcceptance(result);
    if (!signal.aborted) { setPreview(result); setPreviewStamp(stamp); setPreviewKind("version");
      setPreviewReceiptIds(methodRows(snapshot.receipts).map(row => text(row.receipt_id))); }
  });
  const previewProfile = () => run(async signal => {
    if (!target.trim() || !actor.trim() || !knownAt.trim()) throw new Error("Target library, actor and a known-at timestamp are required.");
    parseMethodObject(profileDraft, "Profile"); const ids = parseReceiptIds(receiptsDraft);
    await command({ operation: "load", profile_json: profileDraft, receipt_ids: ids }, signal);
    const result = await command({ operation: "profile_preview", profile_json: profileDraft, receipt_ids: ids, target, actor, known_at: knownAt }, signal);
    methodAcceptance(result);
    if (!signal.aborted) { setPreview(result); setPreviewStamp(stamp); setPreviewKind("profile"); setPreviewReceiptIds(ids); }
  });
  const accept = () => run(async signal => {
    if (!preview || previewStamp !== stamp) throw new Error("Validate the current draft again before accepting it.");
    const result = await command(methodAcceptance(preview), signal);
    const id = methodObject(result.receipt).id;
    if (!signal.aborted) {
      setReceipt(result); setPreviewStamp(""); setConfigSaved(false);
      if (typeof id !== "string" || !id || !Object.keys(methodObject(preview.profile)).length) throw new Error("Library acceptance returned without a native receipt or frozen profile; inspect the retained acceptance result.");
      if (previewReceiptIds.some(value => !value)) throw new Error("Loaded snapshot receipt IDs are invalid; inspect the retained library acceptance.");
      setAccepted({ profile: methodObject(preview.profile), profileJson: typeof preview.profile_json === "string" ? preview.profile_json : methodPretty(preview.profile), receiptIds: [...new Set([...previewReceiptIds, id])] });
      if (previewKind === "profile") setSelectionDraft(methodPretty(methodObject(preview.profile).selection ?? {}));
      setNotice("Native acceptance recorded. Original versions and their result relations are retained. Global native config has not been changed.");
    }
  });
  const read = () => run(async signal => {
    const result = await command({ operation: "read", receipt_id: readId }, signal);
    if (!signal.aborted) setReceiptRead(result);
  });
  const results = () => run(async signal => {
    if (!snapshot) throw new Error("Load the native graph first.");
    const result = await command({ operation: "result_lineage", snapshot_json: snapshot.snapshot_json }, signal);
    if (!signal.aborted) setLineage(result);
  });
  const saveProfile = () => run(async signal => {
    if (!accepted) throw new Error("Accept the native graph version before saving its profile selection.");
    const descriptor = configSlots.find(row => row.id === saveSlot);
    if (!descriptor) throw new Error("Choose an existing native config slot.");
    parseMethodObject(selectionDraft, "Selection");
    const loaded = await command({ operation: "load", profile_json: accepted.profileJson, receipt_ids: accepted.receiptIds }, signal);
    await command({ operation: "resolve", snapshot_json: loaded.snapshot_json, selection_json: selectionDraft }, signal);
    if (signal.aborted) return;
    await saveAcceptedMethodProfile(transport, descriptor, accepted.profile, accepted.receiptIds, { profileJson: accepted.profileJson, selectionJson: selectionDraft });
    if (!signal.aborted) { setConfigSaved(true); setProfileId(saveSlot); setReceiptsDraft(methodPretty(accepted.receiptIds));
      setNotice("Accepted profile selection saved in global native config and verified by readback.");
      const list = await command({ operation: "catalog" }, signal); if (!signal.aborted) { setCatalog(list); const source = methodRows(list.profiles).find(row => row.id === saveSlot) ?? null;
        setProfileSource(source); if (typeof source?.profile_json === "string") setProfileDraft(source.profile_json); } }
  });

  return <section className="methods-panel" data-testid="methods-panel">
    <h2>Graph methods</h2>
    <p>Methods, parameters, prompts, recipes, presets and combinations are native graph records. Availability comes from the host; measured execution comes from recorded runs.</p>
    {!transport.methods && <p role="status" data-testid="methods-unavailable">Graph method commands are unavailable in this host.</p>}
    {error && <p role="alert">{error}</p>}{notice && <p role="status">{notice}</p>}
    <div className="methods-actions"><button onClick={refresh} disabled={busy || !transport.methods}>Refresh library</button></div>
    {capabilities && <FullRecord value={capabilities} label="Native capabilities and availability" />}
    <div className="methods-profile-fields">
      <label>Method profile<select aria-label="Method profile" value={profileId} onChange={event => chooseProfile(event.target.value)} data-testid="methods-profile"><option value="">New profile / supplied JSON</option>
        {profiles.map(row => <option key={text(row.id)} value={text(row.id)}>{text(row.label ?? row.id)}</option>)}</select></label>
      <label>Profile JSON<textarea value={profileDraft} onChange={event => { setProfileDraft(event.target.value); setPreview(null); setSnapshot(null); }} spellCheck={false} data-testid="methods-profile-json" /></label>
      <label>Library receipt IDs (JSON)<textarea value={receiptsDraft} onChange={event => { setReceiptsDraft(event.target.value); setPreview(null); setSnapshot(null); }} spellCheck={false} data-testid="methods-receipts-json" /></label>
    </div>
    <div className="methods-actions"><button onClick={load} disabled={busy || !transport.methods}>Load graph</button></div>
    <p>A profile selects exact versions and receipt IDs. Parameter precedence, signed weights and nested members remain editable JSON data.</p>
    <label>Target library<input value={target} onChange={event => { setTarget(event.target.value); setPreview(null); }} data-testid="methods-target" /></label>
    <label>Actor<input value={actor} onChange={event => { setActor(event.target.value); setPreview(null); }} data-testid="methods-actor" /></label>
    <label>Known at (timestamp with timezone)<input value={knownAt} onChange={event => { setKnownAt(event.target.value); setPreview(null); }} data-testid="methods-known-at" /></label>
    <button onClick={previewProfile} disabled={busy || !transport.methods || !target.trim() || !actor.trim() || !knownAt.trim()}>Validate profile for library</button>
    <p>Profile validation previews the complete packet before acceptance. Changing an existing immutable definition requires the new-version editor below.</p>
    {catalog && <FullRecord value={catalog} label="Complete native library catalog" />}
    {profileSource && <FullRecord value={profileSource} label="Original profile record and source" />}
    <label>Read native receipt ID<input value={readId} onChange={event => setReadId(event.target.value)} list="methods-receipt-ids" /></label>
    <datalist id="methods-receipt-ids">{receipts.map(row => <option key={text(row.id ?? row.receipt_id)} value={text(row.id ?? row.receipt_id)} />)}</datalist>
    <button onClick={read} disabled={busy || !transport.methods || !readId.trim()}>Read receipt</button>
    {receiptRead && <div data-testid="methods-receipt-read"><p>Row drift matches: {text(methodObject(receiptRead.row_drift).matches)}</p><FullRecord value={receiptRead} label="Complete native receipt and row drift" /></div>}

    {snapshot && <>
      <p data-testid="methods-snapshot">Snapshot: <code>{text(snapshot.snapshot_sha256)}</code>; {entities.length} entities, {claims.length} claims, {methodRows(snapshot.sources).length} sources.</p>
      <FullRecord value={vocabulary} label="Caller-defined native kinds and predicates" />
      <FullRecord value={snapshot} label="Complete native snapshot, source captures and receipt hashes" />
      <label>Filter graph records<input value={filter} onChange={event => setFilter(event.target.value)} /></label>
      <div className="methods-table-scroll"><table><thead><tr><th>Role / native kind</th><th>Native ID / label</th><th>Status</th><th>Definition / text hash</th><th>Inspect</th></tr></thead><tbody>
        {entities.filter(row => [row.id, row.label, row.kind, methodEntityRole(row, vocabulary)].some(value => text(value).toLocaleLowerCase().includes(filter.toLocaleLowerCase()))).map(row => {
          const attrs = methodObject(row.attrs);
          return <tr key={text(row.id)} data-testid="methods-entity" data-entity-id={text(row.id)}><td>{methodEntityRole(row, vocabulary) ?? "Unmapped kind"}<small>{text(row.kind)}</small></td>
            <td><code>{text(row.id)}</code><small>{text(row.label)}</small></td><td>{text(row.status)}</td><td><code>{text(attrs.definition_sha256 ?? attrs.text_sha256)}</code></td>
            <td><button onClick={() => chooseEntity(row)}>Inspect record</button></td></tr>;
        })}</tbody></table></div>
      {selected && <article className="methods-record" data-testid="methods-selected-record">
        <h3>{text(selected.label ?? selected.id)}</h3><p>{selectedRole ?? "Unmapped kind"}: <code>{entityId}</code></p>
        <FullRecord value={selected} label="Complete original native entity" />
        {selectedRole === "method_version" && <button onClick={() => setProducerFilter(entityId)} data-testid="methods-results-from-version">Results from this version</button>}
        <div data-testid="methods-edges">{claims.filter(row => row.subject === entityId || row.object === entityId).map(row => <details key={text(row.id)}>
          <summary>{methodPredicateRole(row, vocabulary) ?? text(row.predicate)}: {text(row.subject)} → {text(row.object)} ({methodClaimActive(row) ? "active" : text(methodObject(row.assessment).status)})</summary><pre>{methodPretty(row)}</pre>
        </details>)}</div>
        {Array.isArray(methodObject(selectedAttrs.definition).members) && <ol data-testid="methods-members">{(methodObject(selectedAttrs.definition).members as unknown[]).map((raw, index) => {
          const member = methodObject(raw), id = member.method_version_id ?? member.combination_version_id, child = entities.find(row => row.id === id);
          return <li key={index}><code>{text(id)}</code><pre>{methodPretty(raw)}</pre>{child && <button onClick={() => chooseEntity(child)}>Inspect member {index + 1}</button>}</li>;
        })}</ol>}
        {editable && <>
          <label>New version attributes (JSON)<textarea value={attrsDraft} onChange={event => { setAttrsDraft(event.target.value); setPreview(null); }} spellCheck={false} data-testid="methods-version-json" /></label>
          <button onClick={edit} disabled={busy || !target.trim() || !actor.trim() || !knownAt.trim()} data-testid="methods-preview-button">Validate new version</button>
          <p>The host computes the new identity and hash. Existing version definitions and executed-result relations keep their original IDs.</p>
        </>}
      </article>}
      <label>Selection and parameter layers (JSON)<textarea value={selectionDraft} onChange={event => { setSelectionDraft(event.target.value); setResolution(null); setConfigSaved(false); }} spellCheck={false} data-testid="methods-selection-json" /></label>
      <button onClick={resolve} disabled={busy}>Resolve selection without execution</button>
      {resolution && <div data-testid="methods-resolution"><p>Resolution: <code>{text(resolution.resolution_sha256)}</code></p>
        {methodRows(resolution.leaves).map((row, index) => <details key={index} data-testid="methods-resolved-leaf"><summary>Occurrence {index + 1}: {text(row.method_version_id)}; available: {text(row.available)}; weight: {text(row.weight)}</summary><pre>{methodPretty(row)}</pre></details>)}
        <FullRecord value={resolution} label="Complete resolution, repeated paths, parameters and unavailable capabilities" /></div>}
      <h3>Found-result provenance</h3><p>These links are native Claims. Missing or inactive run/version links stay incomplete; accepting a graph does not verify a producer's execution or content truth.</p>
      <label>Producer method-version ID<select aria-label="Producer method-version ID" value={producerFilter} onChange={event => setProducerFilter(event.target.value)} data-testid="methods-producer-filter"><option value="">All recorded producers</option>
        {[...new Set([...producerIds, ...(producerFilter ? [producerFilter] : [])])].map(id => <option key={id} value={id}>{id}</option>)}</select></label>
      <button onClick={() => setProducerFilter("")}>Show all results</button>
      {visibleResults.length === 0 && <p data-testid="methods-no-results">{producerFilter ? "No result provenance Claims for this exact version." : "No result provenance Claims in this graph."}</p>}
      {visibleResults.map(row => <article key={text(row.entity.id)} data-testid="methods-result" data-result-id={text(row.entity.id)} data-complete={row.complete}>
        <p><code>{text(row.entity.id)}</code> — {row.complete ? "Active method-version and run relations" : "Incomplete active provenance"}</p>
        <p>Method versions: {row.methodVersions.map(version => text(version.id)).join(", ") || "unknown"}; runs: {row.runs.map(runRow => text(runRow.id)).join(", ") || "unknown"}</p>
        <FullRecord value={row} label="Full result entity, exact endpoints and all provenance Claims" />
      </article>)}
      <button onClick={results} disabled={busy}>Read native result lineage</button>
      {lineage && <div data-testid="methods-native-lineage"><FullRecord value={lineage} label="Complete native result lineage response" /></div>}
    </>}
    {preview && <div data-testid="methods-version-preview"><p>{previewKind === "version" ? <>New native version: <code>{text(preview.new_version_id)}</code></> : "Validated profile packet; its selection is unchanged."}</p>
      <FullRecord value={preview} label="Validated packet, version edges, exact selection and CAS expectations" />
      <button onClick={accept} disabled={busy || previewStamp !== stamp} data-testid="methods-accept-button">{previewKind === "version" ? "Accept new version into library" : "Accept profile into library"}</button>
    </div>}
    {receipt && <div data-testid="methods-accepted"><p>Receipt: <code>{text(methodObject(receipt.receipt).id)}</code>; row drift matches: {text(methodObject(receipt.row_drift).matches)}</p>
      <FullRecord value={receipt} label="Complete acceptance result" /></div>}
    {accepted && <div data-testid="methods-config-selection"><p>{configSaved ? "Global native config selection saved and read back." : "Library receipt is committed. Global native config selection has not been saved."}</p>
      <p>Saving uses the selection JSON after native validation. Publication does not select the new version automatically; choose its exact ID explicitly. The accepted graph rows stay unchanged.</p>
      {!snapshot && <label>Selection and parameter layers (JSON)<textarea value={selectionDraft} onChange={event => { setSelectionDraft(event.target.value); setConfigSaved(false); }} spellCheck={false} data-testid="methods-selection-json" /></label>}
      <label>Global native config slot<select aria-label="Global native config slot" value={saveSlot} onChange={event => { setSaveSlot(event.target.value); setConfigSaved(false); }} data-testid="methods-save-slot"><option value="">Choose an existing native config slot</option>
        {configSlots.map(row => <option key={text(row.id)} value={text(row.id)}>{text(row.label ?? row.id)}</option>)}</select></label>
      <button onClick={saveProfile} disabled={busy || !saveSlot || !transport.methods} data-testid="methods-save-config">Save accepted profile selection</button>
      <FullRecord value={accepted} label="Frozen accepted profile and receipt IDs to save" />
    </div>}
  </section>;
}
