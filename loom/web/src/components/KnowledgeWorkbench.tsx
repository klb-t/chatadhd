import { useCallback, useEffect, useId, useMemo, useState } from "react";
import { api } from "../api";
import {
  asArray, asRecord, displayText,
  type KnowledgeApi, type KnowledgeCollection, type KnowledgeContextResult,
  type KnowledgeRecord, type KnowledgeRun,
} from "../api/knowledge";
import "./knowledge.css";

type ViewKind = KnowledgeCollection | "graph" | "context" | "catalog";
type Pane = { id: string; kind: ViewKind };
type Selection = { kind: string; record: KnowledgeRecord };
type Dataset = Partial<Record<KnowledgeCollection, KnowledgeRecord[]>>;
const COLLECTIONS: KnowledgeCollection[] = ["entities", "claims", "principles", "operators", "instances", "products"];
const VIEWS: Record<ViewKind, string> = {
  entities: "Entities", claims: "Claims", graph: "Knowledge graph", principles: "Principles",
  operators: "Operators", context: "Context trace", catalog: "Source catalog",
  instances: "Project instances", products: "Products",
};
const LAYOUT_KEY = "loom.knowledge.layout.v1";
let paneSequence = 0;
const newPane = (kind: ViewKind): Pane => ({ id: `view-${Date.now()}-${++paneSequence}`, kind });
function initialPanes(): Pane[] {
  try {
    const saved: unknown = JSON.parse(localStorage.getItem(LAYOUT_KEY) ?? "null");
    if (Array.isArray(saved) && saved.length && saved.every((kind) => typeof kind === "string" && kind in VIEWS)) {
      return saved.map((kind) => newPane(kind as ViewKind));
    }
  } catch { /* Storage may be disabled. The workbench still works. */ }
  return [newPane("entities"), newPane("claims"), newPane("graph")];
}
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
  const [panes, setPanes] = useState<Pane[]>(initialPanes);
  const [addKind, setAddKind] = useState<ViewKind>("graph");
  const [runs, setRuns] = useState<KnowledgeRun[]>([]);
  const [runId, setRunId] = useState("");
  const [data, setData] = useState<Dataset>({});
  const [limit, setLimit] = useState(1000);
  const [busy, setBusy] = useState(false);
  const [errors, setErrors] = useState<string[]>([]);
  const [refresh, setRefresh] = useState(0);
  const [selection, setSelection] = useState<Selection | null>(null);
  const [focus, setFocus] = useState("");
  const entityMap = useMemo(() => new Map((data.entities ?? []).map((row) => [idOf(row), row])), [data.entities]);

  useEffect(() => {
    try { localStorage.setItem(LAYOUT_KEY, JSON.stringify(panes.map((pane) => pane.kind))); } catch { /* optional layout persistence */ }
  }, [panes]);

  useEffect(() => {
    if (!knowledge) return;
    let active = true;
    knowledge.listRuns().then((next) => {
      if (!active) return;
      setRuns(next);
      setRunId((current) => next.some((run) => run.id === current) ? current : (next.find((run) => run.status === "done")?.id ?? next[0]?.id ?? ""));
    }).catch((error) => active && setErrors([errorText(error)]));
    return () => { active = false; };
  }, [knowledge, refresh]);

  useEffect(() => {
    if (!knowledge || !runId) { setData({}); setBusy(false); return; }
    let active = true;
    setBusy(true);
    setSelection(null);
    setFocus("");
    setData({});
    Promise.allSettled(COLLECTIONS.map((what) => knowledge.query(what, { run: runId, limit }))).then((results) => {
      if (!active) return;
      const next: Dataset = {};
      const failures: string[] = [];
      results.forEach((result, index) => {
        if (result.status === "fulfilled") next[COLLECTIONS[index]] = result.value.items;
        else failures.push(`${VIEWS[COLLECTIONS[index]]}: ${errorText(result.reason)}`);
      });
      setData(next);
      setErrors(failures);
      setBusy(false);
    });
    return () => { active = false; };
  }, [knowledge, runId, limit, refresh]);

  const select = useCallback((kind: string, record: KnowledgeRecord) => {
    setSelection({ kind, record });
    if (kind === "entities") setFocus(idOf(record));
    else if (kind === "claims") setFocus(displayText(record.subject));
  }, []);

  return <section className="knowledge-workbench" aria-label="Knowledge workbench" data-testid="knowledge-workbench">
    <header className="kb-heading"><div><h2>Knowledge workbench</h2><p>Sources → claims → principles → context. Views share selection; each keeps its own filters.</p></div>
      <button className="icon-btn" aria-label="Close knowledge workbench" onClick={onClose}>×</button></header>
    {!knowledge ? <div className="empty-state" role="status">The knowledge workbench is not available through this Android bridge yet. Chat, memory and the existing graph remain available. Open Loom through its HTTP server to use knowledge analysis.</div> : <>
      <div className="kb-toolbar">
        <label>Knowledge run<select aria-label="Knowledge run" value={runId} onChange={(event) => setRunId(event.target.value)}>
          {!runs.length && <option value="">No run yet</option>}
          {runs.map((run) => <option key={run.id} value={run.id}>{run.status} · {run.id}</option>)}
        </select></label>
        <label>Records per collection<select aria-label="Record limit" value={limit} onChange={(event) => setLimit(Number(event.target.value))}>{[250, 1000, 5000, 10000].map((count) => <option key={count} value={count}>{count.toLocaleString()}</option>)}</select></label>
        <button onClick={() => setRefresh((key) => key + 1)} disabled={busy}>Refresh</button>
        <label>Add coordinated view<select aria-label="View type" value={addKind} onChange={(event) => setAddKind(event.target.value as ViewKind)}>{Object.entries(VIEWS).map(([kind, label]) => <option key={kind} value={kind}>{label}</option>)}</select></label>
        <button onClick={() => setPanes((current) => [...current, newPane(addKind)])} data-testid="kb-add-view">Add view</button>
      </div>
      <RunControls knowledge={knowledge} onDone={(nextRun) => { if (nextRun) setRunId(nextRun); setRefresh((key) => key + 1); onDataChanged?.(); }} />
      <div className="kb-status" role="status">{busy ? "Loading knowledge…" : runs.length ? `${(data.entities ?? []).length} entities · ${(data.claims ?? []).length} claims · ${(data.principles ?? []).length} principles` : "No knowledge run yet. Analyze a source below or catalog it first."}
        {focus && <button className="kb-focus" onClick={() => setFocus("")}>Focus: {displayText(entityMap.get(focus)?.label || focus)} ×</button>}
        {selection && <a className="kb-inspect-link" href="#knowledge-inspector">Inspect selection ↓</a>}
      </div>
      {errors.map((error) => <p className="kb-error" role="alert" key={error}>{error}</p>)}
      <div className="kb-workspace">
        <div className="kb-panels">
          {panes.map((pane, index) => <section className="kb-pane" key={pane.id} data-testid={`kb-pane-${pane.kind}`} aria-label={`${VIEWS[pane.kind]} view ${index + 1}`}>
            <header className="kb-pane-header"><h3>{VIEWS[pane.kind]}</h3><div>
              <button title="Duplicate this view" aria-label={`Duplicate ${VIEWS[pane.kind]} view`} onClick={() => setPanes((current) => [...current, newPane(pane.kind)])}>+</button>
              <button aria-label={`Close ${VIEWS[pane.kind]} view`} onClick={() => setPanes((current) => current.filter((item) => item.id !== pane.id))}>×</button>
            </div></header>
            {pane.kind === "context" ? <ContextPane knowledge={knowledge} run={runId} focus={focus} entities={entityMap} onSelect={select} data={data} /> :
              pane.kind === "catalog" ? <CatalogPane knowledge={knowledge} onSelect={select} refresh={refresh} onImported={onDataChanged} /> :
              pane.kind === "graph" ? <KnowledgeGraph data={data} focus={focus} onSelect={select} /> :
              <CollectionPane kind={pane.kind} rows={data[pane.kind] ?? []} entities={entityMap} focus={focus} selection={selection} onSelect={select} limit={limit} />}
          </section>)}
          {!panes.length && <div className="empty-state">Add a view to explore the knowledge model.</div>}
        </div>
        <Inspector selection={selection} entities={entityMap} />
      </div>
    </>}
  </section>;
}

function RunControls({ knowledge, onDone }: { knowledge: KnowledgeApi; onDone: (run: string) => void }) {
  const [sources, setSources] = useState("");
  const [full, setFull] = useState(false);
  const [priors, setPriors] = useState(true);
  const [running, setRunning] = useState(false);
  const [result, setResult] = useState<KnowledgeRecord | null>(null);
  const [error, setError] = useState("");
  const run = async () => {
    setRunning(true); setError(""); setResult(null);
    try {
      const outcome = await knowledge.run({ sources: sources.split("\n").map((path) => path.trim()).filter(Boolean), priors, llm: "off", stage_params: { catalog: { import: { mode: full ? "full" : "selective" } } } });
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
      <p className="kb-muted">Selective mode uses the catalog profile. Analysis runs locally with language-model refinement off. Original sources are preserved.</p>
      <div className="kb-actions"><button className="primary" disabled={running || !sources.trim()} onClick={run}>{running ? "Analyzing…" : "Analyze sources"}</button>
        {running && <button onClick={() => knowledge.cancel().catch((failure) => setError(errorText(failure)))}>Cancel analysis</button>}</div>
      {error && <p className="kb-error" role="alert">{error}</p>}
      {result && <JsonDetail label="Analysis result" value={result} />}
    </div>
  </details>;
}

function CollectionPane({ kind, rows, entities, focus, selection, onSelect, limit }: {
  kind: KnowledgeCollection; rows: KnowledgeRecord[]; entities: Map<string, KnowledgeRecord>;
  focus: string; selection: Selection | null; onSelect: (kind: string, row: KnowledgeRecord) => void; limit: number;
}) {
  const [filter, setFilter] = useState("");
  const [confidence, setConfidence] = useState(0);
  const [follow, setFollow] = useState(true);
  const [evidence, setEvidence] = useState("");
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
      {(kind === "claims" || kind === "instances") && <label className="kb-check"><input type="checkbox" checked={follow} onChange={(event) => setFollow(event.target.checked)} />Follow shared entity focus</label>}
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

function KnowledgeGraph({ data, focus, onSelect }: { data: Dataset; focus: string; onSelect: (kind: string, row: KnowledgeRecord) => void }) {
  const marker = useId().replace(/:/g, "");
  const [filter, setFilter] = useState("");
  const [depth, setDepth] = useState(1);
  const [fade, setFade] = useState(20);
  const [maxNodes, setMaxNodes] = useState(60);
  const [confidence, setConfidence] = useState(0);
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
    <div className="kb-count">{nodes.length} / {matching.length} matching entities · {edges.length} relations · select a node to focus all linked views</div>
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

function Inspector({ selection, entities }: { selection: Selection | null; entities: Map<string, KnowledgeRecord> }) {
  const row = selection?.record;
  const assessment = row ? evidenceOf(row) : {};
  const basis = asRecord(assessment.basis);
  return <aside id="knowledge-inspector" className="kb-inspector" aria-label="Knowledge inspector" data-testid="kb-inspector"><h3>Selection & provenance</h3>
    {!row ? <p className="kb-muted">Select an entity, claim, principle or catalog unit in any view. The inspector retains all fields, including uncertainty and missing evidence.</p> : <>
      <h4>{labelOf(row, entities)}</h4><code className="kb-id">{idOf(row)}</code><Evidence row={row} />
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

function ContextPane({ knowledge, run, focus, entities, onSelect, data }: {
  knowledge: KnowledgeApi; run: string; focus: string; entities: Map<string, KnowledgeRecord>;
  onSelect: (kind: string, row: KnowledgeRecord) => void; data: Dataset;
}) {
  const [text, setText] = useState("");
  const [budget, setBudget] = useState(4000);
  const [follow, setFollow] = useState(true);
  const [result, setResult] = useState<KnowledgeContextResult | null>(null);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [builtFor, setBuiltFor] = useState("");
  const requestKey = JSON.stringify({ run, text, budget, focus: follow ? focus : "" });
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
    <label className="kb-check"><input type="checkbox" checked={follow} onChange={(event) => setFollow(event.target.checked)} />Use shared entity focus as target</label>
    <p className="kb-muted">Target: {follow && focus ? displayText(entities.get(focus)?.label || focus) : "none selected"}. Token counts are estimates. Previewing does not send a model request.</p>
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

function CatalogPane({ knowledge, onSelect, refresh, onImported }: { knowledge: KnowledgeApi; onSelect: (kind: string, row: KnowledgeRecord) => void; refresh: number; onImported?: () => void }) {
  const [sources, setSources] = useState("");
  const [filter, setFilter] = useState("");
  const [rows, setRows] = useState<KnowledgeRecord[]>([]);
  const [offset, setOffset] = useState(0);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [tick, setTick] = useState(0);
  const [result, setResult] = useState<KnowledgeRecord | null>(null);
  const [mode, setMode] = useState("selective");
  const [storeMode, setStoreMode] = useState("copy");
  const [reviewed, setReviewed] = useState("");
  const [selectedOnly, setSelectedOnly] = useState(false);
  const importKey = JSON.stringify({ mode, store_mode: storeMode, tick, refresh });
  useEffect(() => {
    let active = true;
    setError("");
    knowledge.catalogUnits({ limit: 50, offset, ...(filter ? { text: filter } : {}), ...(selectedOnly ? { selected: true } : {}) }).then((items) => active && setRows(items)).catch((failure) => active && setError(errorText(failure)));
    return () => { active = false; };
  }, [knowledge, filter, offset, selectedOnly, tick, refresh]);
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
        <button className="kb-record" onClick={() => { onSelect("catalog", row); action(() => knowledge.catalogPreview(id)).then((preview) => { if (preview) onSelect("catalog", { ...row, preview }); }); }}>
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
