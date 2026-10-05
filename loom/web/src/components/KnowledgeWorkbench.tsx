import { useCallback, useEffect, useId, useMemo, useRef, useState } from "react";
import { api } from "../api";
import {
  asArray, asRecord, displayText,
  type KnowledgeApi, type KnowledgeCollection, type KnowledgeContextResult,
  type KnowledgeRecord, type KnowledgeRun,
} from "../api/knowledge";
import "./knowledge.css";
import { NATIVE_INT_MAX, SEMANTIC_BUDGET_KEY, nativeResourceInteger, useResourceParameters, useResourcePresets, validSemanticBudget } from "../context/resource-controls";
import ResourcePresetEditor from "./ResourcePresetEditor";

import { VIEWS, STORAGE_KEY, SAVED_KEY, loadWorkspace, parseWorkspace, addPane, changeParameter, connect, unlink, duplicatePane, setWorkspaceRun,
  type ViewKind, type Pane, type Parameters, type Parameter, type Workspace } from "../workspace/state";
type Selection = { kind: string; record: KnowledgeRecord };
type Dataset = Partial<Record<KnowledgeCollection, KnowledgeRecord[]>>;
const COLLECTIONS: KnowledgeCollection[] = ["entities", "claims", "principles", "operators", "instances", "products"];
type Change = <K extends Parameter>(parameter: K, value: Parameters[K]) => void;
const idOf = (row: KnowledgeRecord): string => displayText(row.id || asRecord(row.unit).id);
const errorText = (error: unknown): string => error instanceof Error ? error.message : String(error);
function evidenceOf(row: KnowledgeRecord): KnowledgeRecord {
  return row.assessment ? asRecord(row.assessment) : row;
}
function labelOf(row: KnowledgeRecord, entities: Map<string, KnowledgeRecord>): string {
  if (row.predicate) {
    const subject = entities.get(displayText(row.subject));
    const object = entities.get(displayText(row.object));
    return `${displayText(subject?.label || row.subject)} · ${displayText(row.predicate)} · ${displayText(object?.label || row.object || row.value)}`;
  }
  return displayText(row.label || row.statement || row.situation || row.title || asRecord(row.unit).title || row.id || asRecord(row.unit).id);
}
function Evidence({ row }: { row: KnowledgeRecord }) {
  const assessment = evidenceOf(row);
  const evidence = displayText(assessment.evidence_class);
  const confidence = assessment.confidence;
  return <div className="kb-badges">
    {evidence && <span className={`kb-badge evidence-${evidence}`}>{evidence}</span>}
    {!!assessment.origin && <span className="kb-badge">{displayText(assessment.origin)}</span>}
    {typeof confidence === "number" && <span className="kb-badge" title="Confidence reported by the engine; not a guarantee">{Math.round(confidence * 100)}% confidence</span>}
    {!!(assessment.status || row.validation_status) && <span className="kb-badge">{displayText(assessment.status || row.validation_status)}</span>}
  </div>;
}
function JsonDetail({ label, value, open = false }: { label: string; value: unknown; open?: boolean }) {
  return <details className="kb-detail" open={open || undefined}><summary>{label}</summary><pre>{JSON.stringify(value ?? null, null, 2)}</pre></details>;
}

export default function KnowledgeWorkbench({ onClose, onDataChanged }: { onClose: () => void; onDataChanged?: () => void }) {
  const knowledge = api.knowledge;
  const [loaded] = useState(loadWorkspace);
  const [workspace, setWorkspace] = useState(loaded.workspace);
  const [storageError, setStorageError] = useState(loaded.error);
  const [saveStatus, setSaveStatus] = useState("");
  const [addKind, setAddKind] = useState<ViewKind>("graph");
  const [runs, setRuns] = useState<KnowledgeRun[]>([]);
  const [error, setError] = useState("");
  const [refresh, setRefresh] = useState(0);
  const [inspection, setInspection] = useState<{ pane: string; run: string; limit: number; refresh: number; selection: Selection; entities: Map<string, KnowledgeRecord> } | null>(null);
  const { panes, run: runId, limit } = workspace;
  const inspectedPane = panes.find((p) => p.id === inspection?.pane);
  const currentInspection = inspection && inspectedPane && inspection.run === inspectedPane.parameters.run &&
    inspection.limit === inspectedPane.parameters.limit && inspection.refresh === refresh &&
    inspection.selection.kind === inspectedPane.parameters.selection?.kind &&
    (inspection.selection.kind === "catalog" || inspectedPane.parameters.selection?.run === inspection.run) &&
    idOf(inspection.selection.record) === inspectedPane.parameters.selection?.id ? inspection : null;
  useEffect(() => {
    if (storageError) return;
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify(workspace)); }
    catch (failure) { setStorageError(`Workspace changes are not saved: ${errorText(failure)}`); }
  }, [workspace, storageError]);
  useEffect(() => {
    if (!knowledge) return;
    let active = true;
    knowledge.listRuns().then((next) => {
      if (!active) return;
      setRuns(next); setError("");
      // Never replace an explicitly saved, now unavailable run with newer data.
      setWorkspace((current) => current.run ? current : setWorkspaceRun(current, next.find((run) => run.status === "done")?.id ?? next[0]?.id ?? ""));
    }).catch((failure) => active && setError(errorText(failure)));
    return () => { active = false; };
  }, [knowledge, refresh]);
  const save = () => {
    try { localStorage.setItem(SAVED_KEY, JSON.stringify(workspace)); setSaveStatus("Perspective saved in this browser."); }
    catch (failure) { setSaveStatus(`Save failed: ${errorText(failure)}`); }
  };
  const restore = () => {
    try {
      const raw = localStorage.getItem(SAVED_KEY);
      if (!raw) { setSaveStatus("No saved perspective yet."); return; }
      setWorkspace(parseWorkspace(raw)); setInspection(null); setRefresh((n) => n + 1); setSaveStatus("Perspective restored; referenced data is reloaded from the server.");
    } catch (failure) { setSaveStatus(`Restore failed: ${errorText(failure)}`); }
  };
  return <section className={`knowledge-workbench kb-profile-${workspace.profile}`} aria-label="Knowledge workbench" data-testid="knowledge-workbench">
    <header className="kb-heading"><div><h2>Knowledge workbench</h2><p>Compose views, choose parameter links, or unlink an independent reference. Perspectives are saved in this browser.</p></div>
      <button className="icon-btn" aria-label="Close knowledge workbench" onClick={onClose}>×</button></header>
    {!knowledge ? <div className="empty-state" role="status">The knowledge workbench is not available through this Android bridge yet. Open Loom through its HTTP server to use knowledge analysis.</div> : <>
      <div className="kb-toolbar">
        <label>Knowledge run<select aria-label="Knowledge run" value={runId} onChange={(event) => { setWorkspace((w) => setWorkspaceRun(w, event.target.value)); setInspection(null); }}>
          {!runs.some((r) => r.id === runId) && <option value={runId}>{runId ? `Unavailable · ${runId}` : "No run yet"}</option>}
          {runs.map((run) => <option key={run.id} value={run.id}>{run.status} · {run.id}</option>)}
        </select></label>
        <label>Records per collection<input aria-label="Record limit" type="number" min={1} value={limit} onChange={(event) => { const value = Number(event.target.value); if (Number.isSafeInteger(value) && value > 0) setWorkspace((w) => ({ ...w, limit: value, panes: w.panes.map((p) => p.followRun ? { ...p, parameters: { ...p.parameters, limit: value } } : p) })); }} /></label>
        <button onClick={() => { setRefresh((key) => key + 1); setInspection(null); }}>Refresh</button>
        <label>Add view<select aria-label="View type" value={addKind} onChange={(event) => setAddKind(event.target.value as ViewKind)}>{Object.entries(VIEWS).map(([kind, label]) => <option key={kind} value={kind}>{label}</option>)}</select></label>
        <button onClick={() => setWorkspace((w) => addPane(w, addKind))} data-testid="kb-add-view">Add view</button>
        <label>View profile<select aria-label="View profile" value={workspace.profile} onChange={(event) => setWorkspace((w) => ({ ...w, profile: event.target.value as Workspace["profile"] }))}><option value="adaptive">Adaptive</option><option value="stacked">Stacked</option><option value="compact">Compact</option></select></label>
        <button onClick={save}>Save perspective</button><button onClick={restore}>Restore perspective</button>
      </div>
      <p className="kb-status" role="status">{saveStatus || "Layout, filters, references and links autosave locally. Data remains on the server. View profiles change presentation only."}</p>
      {storageError && <p className="kb-error" role="alert">{storageError} Autosave is paused; the in-memory workspace still works.</p>}
      {error && <p className="kb-error" role="alert">{error}</p>}
      <RunControls knowledge={knowledge} onDone={(nextRun) => { if (nextRun) setWorkspace((w) => setWorkspaceRun(w, nextRun)); setRefresh((key) => key + 1); onDataChanged?.(); }} />
      <div className="kb-workspace"><div className="kb-panels">
        {panes.map((pane, index) => <WorkspacePane key={pane.id} pane={pane} index={index} workspace={workspace} setWorkspace={setWorkspace} knowledge={knowledge} runs={runs} refresh={refresh} onDataChanged={onDataChanged}
          onInspect={(selection, entities) => { setInspection({ pane: pane.id, run: pane.parameters.run, limit: pane.parameters.limit, refresh, selection, entities }); setWorkspace((w) => w.active === pane.id || !w.panes.some((p) => p.id === pane.id) ? w : ({ ...w, active: pane.id })); }} />)}
        {!panes.length && <div className="empty-state">Add a view to explore the knowledge model.</div>}
      </div><Inspector selection={currentInspection?.selection ?? null} entities={currentInspection?.entities ?? new Map()} /></div>
    </>}
  </section>;
}

function WorkspacePane({ pane, index, workspace, setWorkspace, knowledge, runs, refresh, onDataChanged, onInspect }: {
  pane: Pane; index: number; workspace: Workspace; setWorkspace: React.Dispatch<React.SetStateAction<Workspace>>;
  knowledge: KnowledgeApi; runs: KnowledgeRun[]; refresh: number; onDataChanged?: () => void;
  onInspect: (selection: Selection, entities: Map<string, KnowledgeRecord>) => void;
}) {
  const p = pane.parameters;
  const [loaded, setLoaded] = useState<{ key: string; data: Dataset; errors: string[]; fetched: string } | null>(null);
  const [linkSource, setLinkSource] = useState("");
  const [linkParameter, setLinkParameter] = useState<Parameter>("selection");
  const key = JSON.stringify([p.run, p.limit, refresh]);
  const run = runs.find((r) => r.id === p.run);
  const usable = !!run;
  const data = usable && loaded?.key === key ? loaded.data : {};
  const busy = usable && loaded?.key !== key;
  const errors = loaded?.key === key ? loaded.errors : [];
  const entities = useMemo(() => new Map((data.entities ?? []).map((row) => [idOf(row), row])), [data.entities]);
  const focus = p.selection?.run === p.run ? p.selection.focus : "";
  const selected = p.selection?.run === p.run ? (data[p.selection.kind as KnowledgeCollection] ?? []).find((r) => idOf(r) === p.selection?.id) : undefined;
  const selection = selected && p.selection ? { kind: p.selection.kind, record: selected } : null;
  const change: Change = (parameter, value) => setWorkspace((w) => changeParameter(w, pane.id, parameter, value));
  useEffect(() => {
    if (!usable) return;
    let active = true;
    Promise.allSettled(COLLECTIONS.map((what) => knowledge.query(what, { run: p.run, limit: p.limit }))).then((results) => {
      if (!active) return;
      const next: Dataset = {}, failures: string[] = [];
      results.forEach((result, i) => {
        if (result.status === "fulfilled" && result.value.run === p.run) {
          next[COLLECTIONS[i]] = result.value.items;
          if (result.value.has_more || result.value.items.length >= p.limit) failures.push(`${VIEWS[COLLECTIONS[i]]}: loaded prefix only; increase the record limit for coverage.`);
        } else failures.push(`${VIEWS[COLLECTIONS[i]]}: ${result.status === "rejected" ? errorText(result.reason) : "server returned a different run; data not displayed"}`);
      });
      setLoaded({ key, data: next, errors: failures, fetched: new Date().toISOString() });
    });
    return () => { active = false; };
  }, [knowledge, key, usable, p.run, p.limit]);
  // Restore the active inspector from actual freshly fetched records, not saved row copies.
  useEffect(() => { if (selection && workspace.active === pane.id) onInspect(selection, entities); }, [loaded, workspace.active, p.selection]); // eslint-disable-line react-hooks/exhaustive-deps
  const select = (kind: string, record: KnowledgeRecord) => {
    const nextFocus = kind === "entities" ? idOf(record) : kind === "claims" ? displayText(record.subject) : focus;
    change("selection", { kind, id: idOf(record), focus: nextFocus, run: p.run });
    onInspect({ kind, record }, entities);
  };
  const inspectRef = useRef(onInspect);
  inspectRef.current = onInspect;
  const resolveSelection = useCallback((kind: string, record: KnowledgeRecord) => {
    if (workspace.active === pane.id && p.selection?.kind === kind && p.selection.id === idOf(record) && (kind === "catalog" || p.selection.run === p.run)) inspectRef.current({ kind, record }, entities);
  }, [workspace.active, pane.id, p.run, p.selection?.run, p.selection?.kind, p.selection?.id, entities]);
  const links = workspace.bindings.filter((b) => b.source === pane.id || b.target === pane.id);
  return <section className="kb-pane" data-view-id={pane.id} data-testid={`kb-pane-${pane.kind}`} aria-label={`${VIEWS[pane.kind]} view ${index + 1}`}>
    <header className="kb-pane-header"><h3>{index + 1} · {VIEWS[pane.kind]}</h3><div>
      <button title="Duplicate as an independent reference" aria-label={`Duplicate ${VIEWS[pane.kind]} view`} onClick={() => setWorkspace((w) => duplicatePane(w, pane.id))}>+</button>
      <button aria-label={`Close ${VIEWS[pane.kind]} view`} onClick={() => setWorkspace((w) => ({ ...unlink(w, pane.id), active: w.active === pane.id ? "" : w.active, panes: w.panes.filter((item) => item.id !== pane.id) }))}>×</button>
    </div></header>
    <details className="kb-view-settings"><summary>View settings & links</summary>
      <code className="kb-id">{pane.id}</code>
      <label className="kb-check"><input type="checkbox" checked={pane.followRun} onChange={(e) => setWorkspace((w) => ({ ...w, panes: w.panes.map((v) => v.id === pane.id ? { ...v, followRun: e.target.checked, parameters: e.target.checked ? { ...v.parameters, run: w.run, limit: w.limit, selection: null } : v.parameters } : v) }))} />Follow workspace run and limit</label>
      <label>Data run<select aria-label="View data run" value={p.run} onChange={(e) => { change("run", e.target.value); change("selection", null); }} disabled={pane.followRun}>
        {!usable && <option value={p.run}>{p.run ? `Unavailable · ${p.run}` : "No run yet"}</option>}{runs.map((r) => <option key={r.id} value={r.id}>{r.status} · {r.id}</option>)}
      </select></label>
      <label>Record limit<input aria-label="View record limit" type="number" min={1} value={p.limit} onChange={(e) => { const n = Number(e.target.value); if (Number.isSafeInteger(n) && n > 0) change("limit", n); }} /></label>
      <div className="kb-actions"><button onClick={() => setWorkspace((w) => unlink(w, pane.id))}>Unlink as reference</button><button onClick={() => change("selection", null)}>Clear view focus</button></div>
      <p className="kb-muted">{links.length} parameter links · {pane.followRun ? "following workspace run" : "independent run"}. Unlink keeps current parameters; it does not freeze server data.</p>
      <label>Follow view<select aria-label="Link source view" value={linkSource} onChange={(e) => setLinkSource(e.target.value)}><option value="">Choose source</option>{workspace.panes.filter((v) => v.id !== pane.id).map((v) => <option key={v.id} value={v.id}>{workspace.panes.indexOf(v) + 1} · {VIEWS[v.kind]}</option>)}</select></label>
      <label>Parameter<select aria-label="Link parameter" value={linkParameter} onChange={(e) => setLinkParameter(e.target.value as Parameter)}>{(["selection", "filter", "depth", "confidence", "evidence", "fade", "maxNodes"] as Parameter[]).map((k) => <option key={k} value={k}>{k}</option>)}</select></label>
      <button disabled={!workspace.panes.some((v) => v.id === linkSource)} onClick={() => setWorkspace((w) => connect(w, linkSource, pane.id, linkParameter))}>Link parameter</button>
      {links.map((link) => <p className="kb-link" key={`${link.source}/${link.target}/${link.parameter}`}>{workspace.panes.findIndex((v) => v.id === link.source) + 1} → {workspace.panes.findIndex((v) => v.id === link.target) + 1} · {link.parameter} <button aria-label={`Detach ${link.parameter} link`} onClick={() => setWorkspace((w) => ({ ...w, bindings: w.bindings.filter((b) => b !== link) }))}>Detach</button></p>)}
      <JsonDetail label="Data version and execution status" value={{ requested_run: p.run, run: run ?? null, retrieved_at: loaded?.key === key ? loaded.fetched : null, projection: pane.kind, status: busy ? "loading" : !usable ? "run unavailable" : errors.length ? "partial" : "loaded", saved_selection: p.selection, snapshot: "Run reference; immutable server snapshot not guaranteed" }} />
    </details>
    <p className="kb-run-status" role="status">{pane.kind === "catalog" ? "Live catalog · not scoped to a knowledge run" : `Run: ${p.run || "none"} · ${!usable ? "unavailable" : busy ? "loading" : run.status}`}{focus && ` · focus: ${displayText(entities.get(focus)?.label || focus)}`}</p>
    {errors.map((e) => <p className="kb-warning" key={e}>{e}</p>)}
    {p.selection && p.selection.kind !== "catalog" && (p.selection.run !== p.run || (COLLECTIONS.includes(p.selection.kind as KnowledgeCollection) && !busy && !selection)) && <p className="kb-warning">Saved selection is outside this loaded collection or run. No replacement has been selected.</p>}
    {pane.kind === "context" ? <ContextPane knowledge={knowledge} run={usable ? p.run : ""} focus={focus} entities={entities} onSelect={select} data={data} parameters={p} change={change} refresh={refresh} /> :
      pane.kind === "catalog" ? <CatalogPane knowledge={knowledge} onSelect={select} refresh={refresh} onImported={onDataChanged} parameters={p} change={change} onResolve={resolveSelection} /> :
      pane.kind === "candidates" ? <CandidatePane knowledge={knowledge} run={usable ? p.run : ""} refresh={refresh} onSelect={select} entities={entities} parameters={p} change={change} onResolve={resolveSelection} /> :
      pane.kind === "graph" ? <KnowledgeGraph data={data} focus={focus} onSelect={select} parameters={p} change={change} /> :
      <CollectionPane kind={pane.kind} rows={data[pane.kind] ?? []} entities={entities} focus={focus} selection={selection} onSelect={select} limit={p.limit} parameters={p} change={change} />}
  </section>;
}

function RunControls({ knowledge, onDone }: { knowledge: KnowledgeApi; onDone: (run: string) => void }) {
  const [sources, setSources] = useState("");
  const [full, setFull] = useState(false);
  const [priors, setPriors] = useState(true);
  const [running, setRunning] = useState(false);
  const [result, setResult] = useState<KnowledgeRecord | null>(null);
  const [error, setError] = useState("");
  const [useModel, setUseModel] = useState(false);
  const [representation, setRepresentation] = useState("relation_v1");
  const [modelConfig, setModelConfig] = useState<KnowledgeRecord | null>(null);
  const [modelError, setModelError] = useState("");
  const { presets, error: presetError } = useResourcePresets();
  const { value: semanticBudget, setValue: setSemanticBudget, storageError: budgetStorageError } = useResourceParameters<Record<string, number>>(SEMANTIC_BUDGET_KEY, presets.semantic_analysis.defaults, validSemanticBudget);
  const budgetFields = presets.semantic_analysis.fields;
  const invalidBudget = !validSemanticBudget(semanticBudget) || budgetFields.some(({ key }) => !nativeResourceInteger(semanticBudget[key]));
  const loadModelConfig = useCallback(async () => {
    setModelError("");
    try { setModelConfig(await api.getConfig()); }
    catch (failure) { setModelConfig(null); setModelError(errorText(failure)); }
  }, []);
  useEffect(() => { void loadModelConfig(); }, [loadModelConfig]);
  const semanticModel = displayText(modelConfig?.semantic_model);
  const extractStage = asArray(result?.stages).map(asRecord).find((stage) => stage.stage === "extract");
  const semantic = asRecord(asRecord(extractStage?.stats).semantic);
  const hasSemantic = Object.keys(semantic).length > 0;
  const run = async () => {
    if (useModel && invalidBudget) { setError(`Use finite nonnegative whole-number semantic parameters representable by the native integer fields (0–${NATIVE_INT_MAX}).`); return; }
    setRunning(true); setError(""); setResult(null);
    try {
      const outcome = await knowledge.run({ sources: sources.split("\n").map((path) => path.trim()).filter(Boolean), priors, llm: useModel ? "auto" : "off",
        stage_params: { catalog: { import: { mode: full ? "full" : "selective" } }, ...(useModel ? { extract: { semantic: { ...semanticBudget, representation } } } : {}) } });
      setResult(outcome);
      if (outcome.status !== "done") setError(`Analysis ${displayText(outcome.status) || "did not complete"}: ${displayText(outcome.error) || "inspect the stage results"}`);
      onDone(displayText(outcome.run));
    } catch (failure) { setError(errorText(failure)); }
    finally { setRunning(false); }
  };
  return <details className="kb-run-controls"><summary>Analyze sources</summary>
    <div className="kb-run-form"><label>Paths on the Loom server, one per line<textarea aria-label="Analysis source paths" rows={2} placeholder="/data/exports/archive.zip" value={sources} onChange={(event) => setSources(event.target.value)} /></label>
      <label className="kb-check"><input type="checkbox" checked={full} onChange={(event) => setFull(event.target.checked)} />Import all catalogued source content (full mode)</label>
      <label className="kb-check"><input type="checkbox" checked={priors} onChange={(event) => setPriors(event.target.checked)} />Include candidate principles from the data pack</label>
      <label className="kb-check"><input type="checkbox" checked={useModel} disabled={running} onChange={(event) => setUseModel(event.target.checked)} />Use the configured semantic model for candidate proposals</label>
      {useModel && <div data-testid="kb-semantic-controls">
        <p className="kb-muted">Selected source excerpts are sent to the configured model provider and may incur its charges. Proposals remain candidates; they do not become accepted claims automatically.</p>
        <p role="status">{modelConfig ? semanticModel ? `Semantic model: ${semanticModel}` : "No semantic model configured. Set Semantic model in Settings, then refresh here." : modelError ? `Model settings unavailable: ${modelError}` : "Loading model settings…"}</p>
        {modelConfig?.semantic_analysis === false && <p className="kb-warning">Semantic analysis is disabled in Settings. The engine will report model proposals as unavailable until it is enabled.</p>}
        <button disabled={running} onClick={() => { void loadModelConfig(); }}>Refresh model settings</button>
        <label>Candidate representation<select aria-label="Semantic candidate representation" value={representation} disabled={running} onChange={(event) => setRepresentation(event.target.value)}><option value="relation_v1">Relation proposals</option><option value="occurrence_graph_v1">Occurrence graph proposals (experimental)</option></select></label>
        {representation === "occurrence_graph_v1" && <p className="kb-muted">Proposes local entities, claim relationships, scopes and explicit unknowns. Draft checks verify the supplied structure and source spans; interpretation and inference remain unreviewed. Existing views stay open.</p>}
        <fieldset disabled={running}><legend>Semantic model budget</legend>
          <div className="kb-pane-controls">{budgetFields.slice(0, 3).map(({ key, label, suggested_min, suggested_max }) => <label key={key}>{label} (preset suggestion {suggested_min}–{suggested_max})<input aria-label={label} type="number" min={0} max={NATIVE_INT_MAX} step={1} value={Number.isNaN(semanticBudget[key]) ? "" : semanticBudget[key]} onChange={(event) => setSemanticBudget((current) => ({ ...current, [key]: event.target.value === "" ? NaN : Number(event.target.value) }))} /></label>)}</div>
          <details><summary>More semantic limits</summary><div className="kb-pane-controls">{budgetFields.slice(3).map(({ key, label, suggested_min, suggested_max }) => <label key={key}>{label} (preset suggestion {suggested_min}–{suggested_max})<input aria-label={label} type="number" min={0} max={NATIVE_INT_MAX} step={1} value={Number.isNaN(semanticBudget[key]) ? "" : semanticBudget[key]} onChange={(event) => setSemanticBudget((current) => ({ ...current, [key]: event.target.value === "" ? NaN : Number(event.target.value) }))} /></label>)}</div></details>
          <p className="kb-muted">Input limits count prompt and context bytes, including source excerpts. Request limits include cached chunks. Output-token limits apply to each request. Zero requests or zero total input permits no model work. These limits bound work, not its price.</p>
          <p className="kb-muted">Chosen parameters are saved in this browser. Suggested ranges are editable presets; values above them are sent unchanged. The native engine or provider may report unsupported capabilities, which remain visible below.</p>
          <ResourcePresetEditor />
        </fieldset>
        {invalidBudget && <p className="kb-error" role="alert">Use finite nonnegative whole-number semantic parameters representable by the native integer fields (0–{NATIVE_INT_MAX}).</p>}
        {[presetError, budgetStorageError].filter(Boolean).map(message => <p className="kb-error" role="alert" key={message}>{message}</p>)}
      </div>}
      <p className="kb-muted">Selective mode uses the catalog profile. Original sources are preserved. {useModel ? "Model proposals use the limits above and the current server settings." : "Language-model proposals are off."}</p>
      <div className="kb-actions"><button className="primary" disabled={running || !sources.trim() || (useModel && invalidBudget)} onClick={run}>{running ? "Analyzing…" : "Analyze sources"}</button>
        {running && <button onClick={() => knowledge.cancel().catch((failure) => setError(errorText(failure)))}>Cancel analysis</button>}</div>
      {error && <p className="kb-error" role="alert">{error}</p>}
      {result && <div data-testid="kb-semantic-result">
        <p role="status">Analysis: {displayText(result.status) || "unknown"} · Semantic proposals: {hasSemantic ? displayText(semantic.status) || "status not reported" : "no semantic result reported"}{extractStage?.cache_hit ? " · extract result reused" : ""}</p>
        {!!semantic.reason && <p className="kb-warning">{displayText(semantic.reason)}</p>}
        {hasSemantic && <>
          {(semantic.representation || asRecord(semantic.identity).representation) === "occurrence_graph_v1" ?
            <p>Accepted graph bundles: {displayText(semantic.accepted_bundles) || "not reported"} · Entity drafts: {displayText(semantic.entity_drafts) || "not reported"} · Claim drafts: {displayText(semantic.claim_drafts) || "not reported"} · Abstentions: {displayText(semantic.abstentions) || "not reported"} · Stored candidate IDs: {asArray(semantic.candidate_ids).length} · Rejected proposals: {displayText(semantic.rejected) || "not reported"}</p> :
            <p>Accepted proposals: {displayText(semantic.accepted) || "not reported"} · Stored candidate IDs: {asArray(semantic.candidate_ids).length} · Rejected proposals: {displayText(semantic.rejected) || "not reported"}</p>}
          <p>Model requests: {displayText(semantic.requests) || "not reported"} · Cached responses: {displayText(semantic.cache_hits) || "not reported"} · Input bytes: {displayText(semantic.input_bytes) || "not reported"} · Failed requests: {displayText(semantic.failed) || "not reported"} · Omitted observations: {displayText(semantic.omitted_observations) || "not reported"} · Skipped entries: {asArray(semantic.skipped).length}</p>
          <p className="kb-muted">Candidate proposals are unpromoted interpretations. Counts do not establish extraction quality.</p>
          {!!asArray(semantic.rejections).length && <JsonDetail label="Rejected proposals and reasons" value={semantic.rejections} />}
          {!!asArray(semantic.skipped).length && <JsonDetail label="Skipped inputs and reasons" value={semantic.skipped} />}
          {!!asArray(semantic.candidate_ids).length && <JsonDetail label="Stored semantic candidate IDs" value={semantic.candidate_ids} />}
          <JsonDetail label="Complete semantic result" value={semantic} />
        </>}
      </div>}
      {result && <JsonDetail label="Analysis result" value={result} />}
    </div>
  </details>;
}

function CandidatePane({ knowledge, run, refresh, onSelect, entities, parameters, change, onResolve }: {
  knowledge: KnowledgeApi; run: string; refresh: number;
  onSelect: (kind: string, row: KnowledgeRecord) => void; entities: Map<string, KnowledgeRecord>; parameters: Parameters; change: Change; onResolve: (kind: string, row: KnowledgeRecord) => void;
}) {
  const kind = parameters.candidateKind;
  const setKind = (value: string) => change("candidateKind", value);
  const offset = parameters.offset;
  const setOffset = (value: number | ((current: number) => number)) => change("offset", typeof value === "function" ? value(parameters.offset) : value);
  const pageSize = parameters.pageSize;
  const setPageSize = (value: number) => change("pageSize", value);
  const [loadedRows, setRows] = useState<KnowledgeRecord[]>([]);
  const [loadedKey, setLoadedKey] = useState("");
  const queryKey = JSON.stringify([run, kind, pageSize, offset, refresh]);
  const rows = loadedKey === queryKey ? loadedRows : [];
  const [total, setTotal] = useState(0);
  const [hasMore, setHasMore] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  useEffect(() => {
    if (!run) { setRows([]); setTotal(0); setHasMore(false); setBusy(false); setError(""); return; }
    let active = true;
    setBusy(true); setError(""); setRows([]); setTotal(0); setHasMore(false);
    knowledge.query("candidates", { run, kind, limit: pageSize, offset }).then((result) => {
      if (!active) return;
      if (result.run !== run) throw new Error("Candidate result belongs to a different run");
      setLoadedKey(queryKey); setRows(result.items); setTotal(result.total ?? result.items.length); setHasMore(result.has_more === true);
    }).catch((failure) => { if (active) setError(errorText(failure)); }).finally(() => { if (active) setBusy(false); });
    return () => { active = false; };
  }, [knowledge, run, kind, pageSize, offset, refresh]);
  useEffect(() => {
    if (loadedKey !== queryKey || parameters.selection?.kind !== "candidates") return;
    const selected = loadedRows.find((row) => idOf(row) === parameters.selection?.id);
    if (selected) onResolve("candidates", selected);
  }, [loadedRows, loadedKey, queryKey, parameters.selection?.id, parameters.selection?.kind, onResolve]);
  return <>
    <div className="kb-pane-controls">
      <label>Candidate kind<input aria-label="Candidate kind" value={kind} placeholder="All kinds" onChange={(event) => { setKind(event.target.value); setOffset(0); }} /></label>
      <label>Page size<select aria-label="Candidate page size" value={pageSize} onChange={(event) => { setPageSize(Number(event.target.value)); setOffset(0); }}>{[25, 50, 100].map((value) => <option key={value} value={value}>{value}</option>)}</select></label>
    </div>
    <p className="kb-muted">Candidate proposals remain separate from canonical claims. Select one to inspect its draft claim, exact source quotes and model provenance.</p>
    {error && <p className="kb-error" role="alert">{error}</p>}
    <div className="kb-count" role="status">{busy ? "Loading candidate proposals…" : `${rows.length} shown · ${total} matching candidates`}</div>
    <div className="kb-records">{rows.map((row) => {
      const payload = asRecord(row.payload), proposal = asRecord(payload.proposal), bundle = asRecord(proposal.bundle);
      const graphProposal = payload.representation === "occurrence_graph_v1";
      return <button className="kb-record" key={idOf(row)} onClick={() => onSelect("candidates", row)}>
        <strong>{graphProposal ? `Occurrence graph · ${asArray(bundle.entity_drafts).length} entity drafts · ${asArray(bundle.claim_drafts).length} claim drafts` : labelOf(asRecord(proposal.claim), entities) || idOf(row)}</strong>
        <span className="kb-muted">{graphProposal ? "occurrence_graph_v1 · experimental" : displayText(proposal.kind || row.kind)} · unpromoted candidate</span><Evidence row={row} />
      </button>;
    })}{!busy && !rows.length && <p className="empty-state">{run ? "No matching candidate proposals in this run." : "Choose a knowledge run to inspect its proposals."}</p>}</div>
    <div className="kb-actions"><button aria-label="Previous candidate page" disabled={busy || !offset} onClick={() => setOffset((value) => Math.max(0, value - pageSize))}>Previous</button><span className="kb-muted">{offset + (rows.length ? 1 : 0)}–{offset + rows.length}</span><button aria-label="Next candidate page" disabled={busy || !hasMore} onClick={() => setOffset((value) => value + pageSize)}>Next</button></div>
  </>;
}

function CollectionPane({ kind, rows, entities, focus, selection, onSelect, limit, parameters, change }: {
  kind: KnowledgeCollection; rows: KnowledgeRecord[]; entities: Map<string, KnowledgeRecord>;
  focus: string; selection: Selection | null; onSelect: (kind: string, row: KnowledgeRecord) => void; limit: number; parameters: Parameters; change: Change;
}) {
  const filter = parameters.filter;
  const setFilter = (value: string) => change("filter", value);
  const confidence = parameters.confidence;
  const setConfidence = (value: number) => change("confidence", value);
  const follow = parameters.follow;
  const setFollow = (value: boolean) => change("follow", value);
  const evidence = parameters.evidence;
  const setEvidence = (value: string) => change("evidence", value);
  const visible = rows.filter((row) => {
    if (filter && !JSON.stringify(row).toLocaleLowerCase().includes(filter.toLocaleLowerCase())) return false;
    const assessment = evidenceOf(row);
    if (confidence > 0 && (typeof assessment.confidence !== "number" || assessment.confidence < confidence / 100)) return false;
    if (evidence && assessment.evidence_class !== evidence) return false;
    if (follow && focus && kind === "claims" && row.subject !== focus && row.object !== focus) return false;
    if (follow && focus && kind === "instances" && row.subject !== focus) return false;
    return true;
  });
  const evidences = [...new Set(rows.map((row) => displayText(evidenceOf(row).evidence_class)).filter(Boolean))].sort();
  return <>
    <div className="kb-pane-controls">
      <input type="search" aria-label={`Filter ${VIEWS[kind]}`} placeholder="Filter this view…" value={filter} onChange={(event) => setFilter(event.target.value)} />
      <label>Minimum confidence: {confidence}%<input aria-label={`${VIEWS[kind]} minimum confidence`} type="range" min={0} max={100} value={confidence} onChange={(event) => setConfidence(Number(event.target.value))} /></label>
      {!!evidences.length && <select aria-label={`${VIEWS[kind]} evidence class`} value={evidence} onChange={(event) => setEvidence(event.target.value)}><option value="">All evidence classes</option>{evidences.map((item) => <option key={item}>{item}</option>)}</select>}
      {(kind === "claims" || kind === "instances") && <label className="kb-check"><input type="checkbox" checked={follow} onChange={(event) => setFollow(event.target.checked)} />Follow this view’s entity focus</label>}
    </div>
    <div className="kb-count">{visible.length} shown / {rows.length} loaded{rows.length >= limit ? ` · collection may continue beyond the ${limit.toLocaleString()} record limit` : ""}</div>
    <div className="kb-records">
      {visible.map((row, index) => <button className={`kb-record${selection?.kind === kind && idOf(selection.record) === idOf(row) ? " selected" : ""}`} key={idOf(row) || index} onClick={() => onSelect(kind, row)}>
        <strong>{labelOf(row, entities)}</strong>
        {!!row.kind && <span className="kb-muted">{displayText(row.kind)}</span>}
        {(kind === "principles" || kind === "operators") && <span className="kb-muted">{displayText(row.level || row.produces)} {displayText(row.form)}</span>}
        {kind === "operators" && <span>→ {displayText(row.solution)}</span>}
        <Evidence row={row} />
      </button>)}
      {!visible.length && <p className="empty-state">No matching {VIEWS[kind].toLowerCase()}. Adjust the filters or analyze a source.</p>}
    </div>
  </>;
}

function KnowledgeGraph({ data, focus, onSelect, parameters, change }: { data: Dataset; focus: string; onSelect: (kind: string, row: KnowledgeRecord) => void; parameters: Parameters; change: Change }) {
  const marker = useId().replace(/:/g, "");
  const filter = parameters.filter;
  const setFilter = (value: string) => change("filter", value);
  const depth = parameters.depth;
  const setDepth = (value: number) => change("depth", value);
  const fade = parameters.fade;
  const setFade = (value: number) => change("fade", value);
  const maxNodes = parameters.maxNodes;
  const setMaxNodes = (value: number) => change("maxNodes", value);
  const confidence = parameters.confidence;
  const setConfidence = (value: number) => change("confidence", value);
  const entities = data.entities ?? [];
  const claims = data.claims ?? [];
  const neighborhood = useMemo(() => {
    const visited = new Set(focus ? [focus] : []);
    for (let step = 0; step < depth; step++) {
      const next = new Set(visited);
      for (const claim of claims) {
        if (visited.has(displayText(claim.subject)) && claim.object) next.add(displayText(claim.object));
        if (visited.has(displayText(claim.object))) next.add(displayText(claim.subject));
      }
      for (const id of next) visited.add(id);
    }
    return visited;
  }, [claims, focus, depth]);
  const matching = entities.filter((row) => (!filter || labelOf(row, new Map()).toLocaleLowerCase().includes(filter.toLocaleLowerCase())) && (confidence === 0 || Number(row.confidence ?? 0) >= confidence / 100));
  const nodes = [...matching].sort((a, b) => Number(neighborhood.has(idOf(b))) - Number(neighborhood.has(idOf(a))) || idOf(a).localeCompare(idOf(b))).slice(0, maxNodes);
  const height = Math.max(320, Math.ceil(nodes.length / 5) * 76 + 40);
  const positions = new Map(nodes.map((row, index) => [idOf(row), { x: 68 + (index % 5) * 132, y: 44 + Math.floor(index / 5) * 76 }]));
  const edges = claims.filter((claim) => positions.has(displayText(claim.subject)) && positions.has(displayText(claim.object)));
  return <>
    <div className="kb-pane-controls">
      <input type="search" aria-label="Filter graph entities" placeholder="Filter graph entities…" value={filter} onChange={(event) => setFilter(event.target.value)} />
      <label>Focus depth: {depth}<input aria-label="Graph focus depth" type="range" min={0} max={5} value={depth} onChange={(event) => setDepth(Number(event.target.value))} /></label>
      <label>Background opacity: {fade}%<input aria-label="Graph background opacity" type="range" min={5} max={100} value={fade} onChange={(event) => setFade(Number(event.target.value))} /></label>
      <label>Minimum confidence: {confidence}%<input aria-label="Graph minimum confidence" type="range" min={0} max={100} value={confidence} onChange={(event) => setConfidence(Number(event.target.value))} /></label>
      <label>Node limit<select aria-label="Graph node limit" value={maxNodes} onChange={(event) => setMaxNodes(Number(event.target.value))}>{[30, 60, 120, 300].map((count) => <option key={count}>{count}</option>)}</select></label>
    </div>
    <div className="kb-count">{nodes.length} / {matching.length} matching entities · {edges.length} relations · select a node to focus linked views</div>
    <div className="kb-graph-scroll">
      {!nodes.length ? <p className="empty-state">Analyze sources to build a knowledge graph.</p> : <svg className="kb-graph" viewBox={`0 0 680 ${height}`} role="group" aria-label="Knowledge entities and claim relations">
        <defs><marker id={marker} viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M 0 0 L 10 5 L 0 10 z" fill="var(--dim)" /></marker></defs>
        {edges.map((claim) => { const a = positions.get(displayText(claim.subject))!; const b = positions.get(displayText(claim.object))!; const length = Math.hypot(b.x - a.x, b.y - a.y) || 1; const end = { x: b.x - (b.x - a.x) / length * 15, y: b.y - (b.y - a.y) / length * 15 };
          return <g key={idOf(claim)} opacity={!focus || (neighborhood.has(displayText(claim.subject)) && neighborhood.has(displayText(claim.object))) ? 0.65 : fade / 100} className="kb-edge-target" role="button" tabIndex={0} aria-label={`Inspect relation ${displayText(claim.predicate)}`} onClick={() => onSelect("claims", claim)} onKeyDown={(event) => { if (event.key === "Enter" || event.key === " ") { event.preventDefault(); onSelect("claims", claim); } }}>
            <title>{displayText(claim.predicate)} · {displayText(asRecord(claim.assessment).evidence_class)}</title>
            <line x1={a.x} y1={a.y} x2={end.x} y2={end.y} stroke="transparent" strokeWidth="10" />
            <line x1={a.x} y1={a.y} x2={end.x} y2={end.y} className="kb-edge" markerEnd={`url(#${marker})`} />
          </g>;
        })}
        {nodes.map((row) => { const id = idOf(row); const position = positions.get(id)!; const label = displayText(row.label || id); const evidence = displayText(row.evidence_class);
          return <g key={id} className={`kb-node${id === focus ? " focused" : ""}`} transform={`translate(${position.x},${position.y})`} opacity={!focus || neighborhood.has(id) ? 1 : fade / 100} tabIndex={0} role="button" aria-label={`Focus ${label}`} onClick={() => onSelect("entities", row)} onKeyDown={(event) => { if (event.key === "Enter" || event.key === " ") { event.preventDefault(); onSelect("entities", row); } }}>
            <title>{label} · {displayText(row.kind)} · {evidence} · {displayText(row.origin)} · {typeof row.confidence === "number" ? `${Math.round(row.confidence * 100)}% confidence` : "confidence unavailable"}</title>
            <circle r={id === focus ? 14 : 10} strokeDasharray={evidence === "inferred" || evidence === "extrapolated" ? "3 2" : undefined} />
            <text y={27} textAnchor="middle">{label.length > 19 ? `${label.slice(0, 18)}…` : label}</text>
            <text y={41} textAnchor="middle" className="kb-node-evidence">{evidence || "unclassified"}</text>
          </g>;
        })}
      </svg>}
    </div>
  </>;
}

function CandidateStructure({ payload }: { payload: KnowledgeRecord }) {
  const proposal = asRecord(payload.proposal), bundle = asRecord(proposal.bundle), validation = asRecord(proposal.validation);
  const entities = asArray(bundle.entity_drafts).map(asRecord), claims = asArray(bundle.claim_drafts).map(asRecord);
  const support = (values: unknown) => <div className="kb-support">{asArray(values).map((value, index) => {
    const span = asRecord(value);
    return <div key={index}><blockquote>{displayText(span.quote) || "No quote supplied"}</blockquote><code className="kb-id">{displayText(span.observation)} · bytes {displayText(span.byte_start)} + {displayText(span.byte_len)}</code></div>;
  })}</div>;
  return <div data-testid="kb-candidate-structure">
    <p>Experimental occurrence graph · {entities.length} entity drafts · {claims.length} claim drafts</p>
    <p className="kb-muted">Draft format checks: {validation.valid === true ? "passed" : validation.valid === false ? "failed" : "not reported"}. Source interpretation and inference remain unreviewed.</p>
    <details className="kb-detail"><summary>Local entity drafts ({entities.length})</summary>{entities.map((entity, index) => <div key={displayText(entity.handle) || index}>
      <h4>{displayText(entity.label || entity.handle)} · {displayText(entity.kind)}</h4><code className="kb-id">{displayText(entity.handle)}</code>
      <JsonDetail label="Draft attributes" value={entity.attrs} />{support(entity.support)}
    </div>)}</details>
    <details className="kb-detail"><summary>Local claim drafts ({claims.length})</summary>{claims.map((claim, index) => <div key={displayText(claim.handle) || index}>
      <h4>{displayText(claim.subject)} · {displayText(claim.predicate)} · {displayText(claim.object || claim.value)}</h4>
      <code className="kb-id">{displayText(claim.handle)}</code>{support(asRecord(asRecord(claim.assessment).basis).support)}
      <JsonDetail label="Draft qualifiers and old claim premises" value={{ qualifiers: claim.qualifiers, premises: asRecord(claim.assessment).premises }} />
    </div>)}</details>
    <JsonDetail label="Draft roots" value={bundle.roots} />
    <JsonDetail label="Coverage and unsupported spans" value={bundle.coverage} open />
    <JsonDetail label="Unknowns in proposed structure" value={bundle.unknowns} open />
    <JsonDetail label="Structure check report" value={validation} />
    <JsonDetail label="Candidate source packet" value={{ packet_hash: payload.packet_hash, source_packet: payload.source_packet }} />
    <JsonDetail label="Complete proposed bundle" value={bundle} />
  </div>;
}

function Inspector({ selection, entities }: { selection: Selection | null; entities: Map<string, KnowledgeRecord> }) {
  const row = selection?.record;
  const assessment = row ? evidenceOf(row) : {};
  const basis = asRecord(assessment.basis);
  const candidatePayload = asRecord(row?.payload);
  const proposal = asRecord(candidatePayload.proposal);
  const draft = asRecord(proposal.claim);
  const graphProposal = candidatePayload.representation === "occurrence_graph_v1";
  return <aside id="knowledge-inspector" className="kb-inspector" aria-label="Knowledge inspector" data-testid="kb-inspector"><h3>Selection & provenance</h3>
    {!row ? <p className="kb-muted">Select an entity, claim, principle or catalog unit in any view. The inspector retains all fields, including uncertainty and missing evidence.</p> : <>
      <h4>{labelOf(row, entities)}</h4><code className="kb-id">{idOf(row)}</code><Evidence row={row} />
      {selection?.kind === "candidates" && <>
        <p className="kb-warning">Unpromoted candidate interpretation · review: {displayText(asRecord(row.eval).review) || "not reported"} · logical semantics: {displayText(asRecord(row.eval).logical_semantics) || "not reported"}</p>
        {graphProposal ? <CandidateStructure payload={candidatePayload} /> : <JsonDetail label="Draft claim" value={draft} open />}
        <div className="kb-support"><h4>Located source support</h4>
          {asArray(row.support).map((value, index) => { const support = asRecord(value); return <div key={index}><blockquote>{displayText(support.quote) || "No quote supplied"}</blockquote><code className="kb-id">{displayText(support.observation)}</code><JsonDetail label="Source locator and quote byte span" value={{ locator: support.locator, byte_start: support.byte_start, byte_len: support.byte_len, observation_text_hash: support.observation_text_hash }} /></div>; })}
          {!asArray(row.support).length && <p className="kb-muted">No source support recorded.</p>}
        </div>
        <JsonDetail label="Model and prompt provenance" value={candidatePayload.provenance} open />
        <JsonDetail label="Source group" value={candidatePayload.group} />
        {!graphProposal && <JsonDetail label="Unknowns and premises" value={{ unknowns: proposal.unknowns, premises: asRecord(draft.assessment).premises }} />}
        <JsonDetail label="Candidate checks and review status" value={row.eval} />
      </>}
      {!!row.assessment && <>
        <JsonDetail label="1 · What is claimed?" value={{ subject: row.subject, predicate: row.predicate, object: row.object, value: row.value, qualifiers: row.qualifiers }} />
        <div className="kb-support"><h4>2 · How is it known?</h4>
          {asArray(basis.support).map((value, index) => { const support = asRecord(value); return <div key={index}><blockquote>{displayText(support.quote) || "No quote supplied"}</blockquote><JsonDetail label={`Source locator · ${displayText(support.extractor) || "extractor unspecified"}`} value={support.locator} /><code className="kb-id">{displayText(support.observation)}</code></div>; })}
          {!asArray(basis.support).length && <p className="kb-muted">No observation support recorded.</p>}
          {!!basis.derivation && <JsonDetail label="Derivation method" value={basis.derivation} open />}
        </div>
        <JsonDetail label="3 · How certain?" value={{ evidence_class: assessment.evidence_class, origin: assessment.origin, confidence: assessment.confidence }} />
        <JsonDetail label="4 · What does it depend on?" value={assessment.premises} />
        <JsonDetail label="5 · What contradicts it?" value={{ counter: assessment.counter, status: assessment.status, alternatives: assessment.alternatives }} />
        <JsonDetail label="6 · What follows?" value={assessment.consequences} />
        <JsonDetail label="7 · What is missing?" value={assessment.open} />
        {!!assessment.expected_property && <JsonDetail label={`Expected property · ${displayText(assessment.check_state)}`} value={assessment.expected_property} open />}
      </>}
      {!!row.sources && <JsonDetail label="Sources" value={row.sources} open />}
      {!!row.evidence_for && <JsonDetail label="Supporting observations / decisions" value={row.evidence_for} />}
      {!!row.counterexamples && <JsonDetail label="Counterexamples and exceptions" value={{ counterexamples: row.counterexamples, exceptions: row.exceptions }} />}
      <JsonDetail label="All model fields" value={row} />
    </>}
  </aside>;
}

function ContextPane({ knowledge, run, focus, entities, onSelect, data, parameters, change, refresh }: {
  knowledge: KnowledgeApi; run: string; focus: string; entities: Map<string, KnowledgeRecord>;
  onSelect: (kind: string, row: KnowledgeRecord) => void; data: Dataset; parameters: Parameters; change: Change; refresh: number;
}) {
  const text = parameters.text;
  const setText = (value: string) => change("text", value);
  const budget = parameters.budget;
  const setBudget = (value: number) => change("budget", value);
  const follow = parameters.follow;
  const setFollow = (value: boolean) => change("follow", value);
  const [result, setResult] = useState<KnowledgeContextResult | null>(null);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [builtFor, setBuiltFor] = useState("");
  const requestKey = JSON.stringify({ run, text, budget, focus: follow ? focus : "", refresh });
  const build = async () => {
    setBusy(true); setError("");
    const key = requestKey;
    try { setResult(await knowledge.buildContext({ text, targets: follow && focus ? [focus] : [], budget_tokens: budget, ...(run ? { run } : {}) })); setBuiltFor(key); }
    catch (failure) { setError(errorText(failure)); }
    finally { setBusy(false); }
  };
  const context = result?.context_set;
  const selectRef = (ref: string) => {
    for (const kind of COLLECTIONS) { const row = data[kind]?.find((candidate) => idOf(candidate) === ref); if (row) { onSelect(kind, row); return; } }
  };
  return <div className="kb-context">
    <label>Goal / prompt<textarea aria-label="Context goal" rows={3} value={text} placeholder="What should this context help you do?" onChange={(event) => setText(event.target.value)} /></label>
    <label>Token budget: {budget.toLocaleString()}<input aria-label="Knowledge context token budget" type="range" min={200} max={32000} step={100} value={budget} onChange={(event) => setBudget(Number(event.target.value))} /></label>
    <label className="kb-check"><input type="checkbox" checked={follow} onChange={(event) => setFollow(event.target.checked)} />Use this view’s entity focus as target</label>
    <p className="kb-muted">Target: {follow && focus ? displayText(entities.get(focus)?.label || focus) : "none selected"}. Token counts are estimates. Previewing does not send a model request. Preview output is not restored from browser storage; rebuild it from the selected run.</p>
    <button className="primary" onClick={build} disabled={!text.trim() || !run || busy}>{busy ? "Selecting…" : "Build context preview"}</button>
    {error && <p className="kb-error" role="alert">{error}</p>}
    {context && <>
      {requestKey !== builtFor && <p className="kb-warning" role="status">Inputs changed. Rebuild to update this preview.</p>}
      <p className="kb-count">{context.used_tokens.toLocaleString()} / {context.budget_tokens.toLocaleString()} estimated tokens · {context.items.length} included · {context.dropped.length} dropped</p>
      <JsonDetail label="Selected goal type" value={context.goal} />
      {["stable", "project", "goal"].map((band) => <section className="kb-context-band" key={band}><h4>{band === "stable" ? "Stable prefix" : band === "project" ? "Project context" : "Goal-specific tail"}</h4>
        {context.items.filter((item) => item.band === band).map((item) => <article key={item.ref} className="kb-context-item">
          <button className="kb-ref" onClick={() => selectRef(item.ref)}>{item.ref_kind} · {item.ref}</button>
          <p>{item.text}</p><p className="kb-muted">{item.why} · {item.tokens} tokens · {item.resolution}</p>
          <JsonDetail label="Selection factors and dependencies" value={{ factors: item.factors, required_by: item.required_by }} />
        </article>)}
        {!context.items.some((item) => item.band === band) && <p className="kb-muted">No items selected in this band.</p>}
      </section>)}
      <JsonDetail label={`Dropped items and reasons (${context.dropped.length})`} value={context.dropped} />
      <details className="kb-detail"><summary>Rendered context</summary><pre>{result?.text || result?.prompt || "No rendered text returned."}</pre></details>
    </>}
  </div>;
}

function CatalogPane({ knowledge, onSelect, refresh, onImported, parameters, change, onResolve }: { knowledge: KnowledgeApi; onSelect: (kind: string, row: KnowledgeRecord) => void; refresh: number; onImported?: () => void; parameters: Parameters; change: Change; onResolve: (kind: string, row: KnowledgeRecord) => void }) {
  const [sources, setSources] = useState("");
  const filter = parameters.filter;
  const setFilter = (value: string) => change("filter", value);
  const [loadedRows, setRows] = useState<KnowledgeRecord[]>([]);
  const [loadedKey, setLoadedKey] = useState("");
  const offset = parameters.offset;
  const setOffset = (value: number | ((current: number) => number)) => change("offset", typeof value === "function" ? value(parameters.offset) : value);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [tick, setTick] = useState(0);
  const [result, setResult] = useState<KnowledgeRecord | null>(null);
  const [mode, setMode] = useState("selective");
  const [storeMode, setStoreMode] = useState("copy");
  const [reviewed, setReviewed] = useState("");
  const selectedOnly = parameters.selectedOnly;
  const setSelectedOnly = (value: boolean) => change("selectedOnly", value);
  const importKey = JSON.stringify({ mode, store_mode: storeMode, tick, refresh });
  const queryKey = JSON.stringify([filter, offset, selectedOnly, tick, refresh]);
  const rows = loadedKey === queryKey ? loadedRows : [];
  useEffect(() => {
    let active = true;
    setError("");
    knowledge.catalogUnits({ limit: 50, offset, ...(filter ? { text: filter } : {}), ...(selectedOnly ? { selected: true } : {}) }).then((items) => { if (active) { setRows(items); setLoadedKey(queryKey); } }).catch((failure) => active && setError(errorText(failure)));
    return () => { active = false; };
  }, [knowledge, filter, offset, selectedOnly, tick, refresh]);
  useEffect(() => {
    if (loadedKey !== queryKey || parameters.selection?.kind !== "catalog") return;
    const selected = loadedRows.find((row) => idOf(row) === parameters.selection?.id);
    if (!selected) return;
    let active = true;
    onResolve("catalog", selected);
    knowledge.catalogPreview(idOf(selected)).then((preview) => {
      if (active) onResolve("catalog", { ...selected, preview });
    }).catch((failure) => { if (active) setError(errorText(failure)); });
    return () => { active = false; };
  }, [knowledge, loadedRows, loadedKey, queryKey, parameters.selection?.id, parameters.selection?.kind, onResolve]);
  const action = async (operation: () => Promise<KnowledgeRecord>, reload = false) => {
    setBusy(true); setError("");
    try { const outcome = await operation(); setResult(outcome); if (reload) { setTick((key) => key + 1); setOffset(0); setReviewed(""); } return outcome; }
    catch (failure) { setError(errorText(failure)); return null; }
    finally { setBusy(false); }
  };
  const previewImport = async () => {
    setReviewed("");
    if (await action(() => knowledge.catalogImport({ mode, store_mode: storeMode, dry_run: true }))) setReviewed(importKey);
  };
  return <div className="kb-catalog">
    <details className="kb-detail"><summary>Catalog a source without importing</summary>
      <label>Paths on the Loom server<textarea aria-label="Catalog source paths" rows={2} value={sources} onChange={(event) => setSources(event.target.value)} placeholder="/data/exports/archive.zip" /></label>
      <button disabled={busy || !sources.trim()} onClick={() => action(() => knowledge.catalogScan({ sources: sources.split("\n").map((path) => path.trim()).filter(Boolean), retain_raw: "selected" }), true)}>Scan sources</button>
    </details>
    <button disabled={busy} onClick={() => action(async () => { const scores = await knowledge.catalogScore({ llm: "off" }); const selection = await knowledge.catalogSelect(); return { scores, selection }; }, true)}>Score and select with catalog profile</button>
    <input type="search" aria-label="Filter catalog" value={filter} onChange={(event) => { setFilter(event.target.value); setOffset(0); }} placeholder="Search titles and sketches…" />
    <label className="kb-check"><input type="checkbox" checked={selectedOnly} onChange={(event) => { setSelectedOnly(event.target.checked); setOffset(0); }} />Selected units only</label>
    {error && <p className="kb-error" role="alert">{error}</p>}
    <div className="kb-records">
      {rows.map((row) => { const unit = asRecord(row.unit); const id = idOf(row); return <article className="kb-catalog-unit" key={id}>
        <button className="kb-record" onClick={() => onSelect("catalog", row)}>
          <strong>{displayText(unit.title || unit.id)}</strong><span className="kb-muted">{displayText(row.platform)} · {displayText(unit.kind)} · {displayText(row.n_msgs || 0)} messages · {asArray(row.attachments).length} attachments</span>
          <span className="kb-snippet">{displayText(row.head)}</span>
        </button>
        <div className="kb-actions"><button disabled={busy} onClick={() => action(() => knowledge.catalogOverride({ unit_id: id, action: "include", reason: "Owner included in knowledge workbench" }), true)}>Include</button><button disabled={busy} onClick={() => action(() => knowledge.catalogOverride({ unit_id: id, action: "exclude", reason: "Owner excluded in knowledge workbench" }), true)}>Exclude</button></div>
      </article>; })}
      {!rows.length && <p className="empty-state">No catalog units found. Scan a source to inspect it before importing.</p>}
    </div>
    <div className="kb-actions"><button disabled={!offset} onClick={() => setOffset((value) => Math.max(0, value - 50))}>Previous</button><span className="kb-muted">{offset + (rows.length ? 1 : 0)}–{offset + rows.length}</span><button disabled={rows.length < 50} onClick={() => setOffset((value) => value + 50)}>Next</button></div>
    <details className="kb-detail"><summary>Review and import catalog content</summary>
      <label>Import scope<select aria-label="Catalog import scope" value={mode} onChange={(event) => setMode(event.target.value)}><option value="selective">Selected units</option><option value="full">All catalogued units (full)</option></select></label>
      <label>Source retention<select aria-label="Catalog retention" value={storeMode} onChange={(event) => setStoreMode(event.target.value)}><option value="copy">Copy content</option><option value="link">Link to original source</option></select></label>
      <p className="kb-muted">Import scope applies to the catalog, independently of this view's search filter. Linked originals must remain available at their original location.</p>
      <div className="kb-actions"><button disabled={busy} onClick={previewImport}>Preview import</button><button className="primary" disabled={busy || reviewed !== importKey} onClick={() => action(() => knowledge.catalogImport({ mode, store_mode: storeMode, dry_run: false }), true).then((outcome) => { if (outcome) onImported?.(); })}>Import reviewed selection</button></div>
    </details>
    {result && <JsonDetail label="Latest catalog result" value={result} open />}
  </div>;
}
