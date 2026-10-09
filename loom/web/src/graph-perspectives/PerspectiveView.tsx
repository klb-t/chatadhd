import { Profiler, useEffect, useMemo, useRef, useState, type ComponentType } from "react";
import type { LayerAction } from "../onboarding/types";
import type { CapabilityDescriptor, NativeResolution, NavigationState, ObjectRef, PermissionContext, Perspective, SelectionResult, SourceAdapter, SourceRelation } from "./types";
import { compilePerspective } from "./plan";
import { selectPerspective } from "./select";
import { createNavigation, goBack, goForward, navigate, referenceKey } from "./navigation";
import { exportPerspective, importPerspective } from "./persistence";
import { comparePerspectives, explainVisibility } from "./comparison";
import { compareObjectVersions } from "./versions";
import type { AnalysisExport } from "./workflow";
import type { PerspectiveRendererProps } from "./renderer-bridge";
import "./perspective.css";

export interface PerspectivePreset { id: string; label: string; description: string; actions: LayerAction[]; focus?: ObjectRef }
export interface PerspectiveCatalog {
  ui: Record<string, string>;
  controls: { level: string; storageKey: string; [key: string]: unknown };
  capabilities: CapabilityDescriptor[];
  presets: PerspectivePreset[];
  snapshots?: { id: string; label: string }[];
  targets?: { id: string; label: string; ref: ObjectRef }[];
  analysisExport?: { selectionMode: "anchors" | "exact"; [key: string]: unknown };
  [key: string]: unknown;
}
export interface PerspectiveViewProps {
  initial: Perspective;
  catalog: PerspectiveCatalog;
  adapters: SourceAdapter[];
  permission: PermissionContext;
  resolve(perspective: Perspective, signal?: AbortSignal): Promise<NativeResolution>;
  Renderer: ComponentType<PerspectiveRendererProps>;
  onResult?(result: SelectionResult, perspective: Perspective): void;
  onTiming?(duration: number): void;
  prepareAnalysis?(perspective: Perspective, selection: SelectionResult, mode: "anchors" | "exact"): AnalysisExport;
}
const json = (value: unknown) => JSON.stringify(value, null, 2);
const record = (value: unknown): Record<string, unknown> => value && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : {};

/** Presentation of a single headless result. Every control edits intent; only the
 * host native resolver and selectPerspective decide the effective projection. */
export default function PerspectiveView({ initial, catalog, adapters, permission, resolve, Renderer, onResult, onTiming, prepareAnalysis }: PerspectiveViewProps) {
  const t = (key: string) => catalog.ui[key] ?? key;
  const [perspective, setPerspective] = useState< Perspective >(() => structuredClone(initial));
  const [level, setLevel] = useState(catalog.controls.level);
  const [selection, setSelection] = useState<SelectionResult | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [notice, setNotice] = useState("");
  const [presetId, setPresetId] = useState(String(catalog.controls.defaultPreset ?? catalog.presets[0]?.id ?? ""));
  const [customized, setCustomized] = useState(false);
  const [focusText, setFocusText] = useState(json(initial.focus));
  const [documentText, setDocumentText] = useState(exportPerspective(initial));
  const [relation, setRelation] = useState<SourceRelation | null>(null);
  const [comparison, setComparison] = useState<unknown>(null);
  const [visibility, setVisibility] = useState<unknown>(null);
  const [analysisMode, setAnalysisMode] = useState<"anchors" | "exact">(catalog.analysisExport?.selectionMode ?? "exact");
  const [analysisExport, setAnalysisExport] = useState<AnalysisExport | null>(null);
  const initialStructure = String(initial.components[catalog.capabilities.find(item => item.target === "structure")?.id ?? ""] ?? "");
  const [navigation, setNavigation] = useState<NavigationState | null>(() => initial.focus ? createNavigation(initial.focus, initialStructure) : null);
  const resultCallback = useRef(onResult);
  resultCallback.current = onResult;
  const labels = useMemo(() => ({ graph: catalog.ui.graph, focus: catalog.ui.focus, inspectRelation: catalog.ui.inspectRelation }), [catalog]);
  const componentId = (target: string) => catalog.capabilities.find(item => item.target === target)?.id;
  const getValue = (target: string) => selection?.plan.values[target];
  const appendActions = (current: Perspective, actions: LayerAction[]): Perspective => ({
    ...current,
    layerActions: [...(Array.isArray(current.layerActions) ? current.layerActions : []), ...actions],
  });
  const changeComponent = (id: string, value: unknown) => {
    setCustomized(true);
    setPerspective(current => appendActions(current, [{ op: "override", key: id, value: value as Extract<LayerAction, { op: "override" }>["value"] }]));
  };
  useEffect(() => {
    const controller = new AbortController();
    const target = perspective;
    setBusy(true); setError("");
    resolve(target, controller.signal).then(resolution => {
      if (controller.signal.aborted) return null;
      const plan = compilePerspective(target, resolution, catalog.capabilities);
      if (plan.errors.length) throw new Error(plan.errors.join("; "));
      return selectPerspective(plan, adapters, permission, { signal: controller.signal });
    }).then(result => {
      if (!result || controller.signal.aborted) return;
      setSelection(result); setBusy(false);
      if (catalog.presets.some(item => item.id === result.plan.structure)) setPresetId(result.plan.structure);
      resultCallback.current?.(result, target);
    }).catch(failure => { if (!controller.signal.aborted) { setError(String(failure)); setBusy(false); } });
    return () => controller.abort();
  }, [perspective, catalog, adapters, permission, resolve]);
  useEffect(() => { setFocusText(json(perspective.focus)); setDocumentText(exportPerspective(perspective)); }, [perspective]);

  const capture = (state: NavigationState): NavigationState => ({ ...state, current: { ...state.current, perspective: structuredClone(perspective) } } as NavigationState);
  const selectRef = (ref: ObjectRef) => {
    const structure = selection?.plan.structure ?? initialStructure;
    setNavigation(current => current ? navigate(capture(current), { ref, structure, snapshot: ref.snapshot }) : createNavigation(ref, structure));
    setPerspective(current => ({ ...current, focus: structuredClone(ref) }));
    setRelation(null);
  };
  const replayNavigation = (next: NavigationState) => {
    setNavigation(next); setCustomized(true);
    const previous = (next.current as NavigationState["current"] & { perspective?: Perspective }).perspective;
    setPerspective(current => ({ ...structuredClone(previous ?? current), focus: structuredClone(next.current.ref) }));
  };
  const setPreset = (id: string) => {
    const preset = catalog.presets.find(item => item.id === id);
    if (!preset) return;
    setPresetId(id); setCustomized(false);
    const next = appendActions(perspective, preset.actions);
    const structureAction = preset.actions.find(action => action.op === "override" && action.key === componentId("structure"));
    const structure = String(structureAction && "value" in structureAction ? structureAction.value : selection?.plan.structure ?? "");
    if (perspective.focus) setNavigation(current => current ? navigate(capture(current), { ref: perspective.focus!, structure, snapshot: perspective.focus!.snapshot }) : createNavigation(perspective.focus!, structure));
    setPerspective(next);
  };
  const setTemporal = (key: string, value: unknown) => {
    const id = componentId("temporal");
    if (!id) return;
    const temporal = { ...record(getValue("temporal")), [key]: value };
    if (key === "snapshot" && value === "") delete temporal.snapshot;
    changeComponent(id, temporal);
    if (key === "snapshot" && perspective.focus) {
      const ref: ObjectRef = { ...perspective.focus };
      if (typeof value === "string" && value) ref.snapshot = value;
      else delete ref.snapshot;
      if (ref.snapshot !== perspective.focus.snapshot) delete ref.representation;
      setNavigation(current => current ? navigate(capture(current), { ref, structure: selection?.plan.structure ?? initialStructure, snapshot: ref.snapshot }) : createNavigation(ref, selection?.plan.structure ?? initialStructure));
      setPerspective(current => ({ ...current, focus: ref }));
    }
  };
  const acceptDocument = (raw: string) => {
    try {
      const result = importPerspective(raw);
      setPerspective(result.perspective); setCustomized(true);
      if (result.perspective.focus) setNavigation(createNavigation(result.perspective.focus, String(result.perspective.components[componentId("structure") ?? ""] ?? "")));
      setNotice(result.warnings.join("; ")); setError("");
    } catch (failure) { setError(String(failure)); }
  };
  const downloadJson = (value: unknown, filename: string) => {
    const blob = new Blob([typeof value === "string" ? value : json(value)], { type: "application/json" });
    const url = URL.createObjectURL(blob), anchor = document.createElement("a");
    anchor.href = url; anchor.download = filename; anchor.click(); URL.revokeObjectURL(url);
  };
  const download = () => downloadJson(exportPerspective(perspective), `${perspective.id}.json`);
  const effectiveFocusSnapshot = selection?.plan.snapshot ?? perspective.focus?.snapshot;
  const selected = selection?.objects.find(item => perspective.focus && (referenceKey(item.ref) === referenceKey(perspective.focus) || (item.ref.source === perspective.focus.source && (item.ref.selector === perspective.focus.selector || (perspective.focus.canonicalId !== undefined && item.ref.canonicalId === perspective.focus.canonicalId)) && (effectiveFocusSnapshot === undefined || item.ref.snapshot === effectiveFocusSnapshot))));
  const versionComparisons = useMemo(() => selection ? compareObjectVersions(selection.objects) : [], [selection]);
  const temporal = record(getValue("temporal"));
  const snapshots = catalog.snapshots ?? [];
  return <section className="gp-view" data-testid="perspective-view" data-ready={Boolean(selection && !busy)} aria-busy={busy}>
    <header className="gp-header"><div><p className="gp-kicker">{t("subtitle")}</p><h1>{t("title")}</h1></div>
      <label>{t("interfaceLevel")}<select data-testid="gp-level" aria-label={t("interfaceLevel")} value={level} onChange={event => setLevel(event.target.value)}>{["basic", "advanced", "expert"].map(item => <option key={item} value={item}>{t(item)}</option>)}</select></label>
    </header>
    <p className="gp-local">{t("localOnly")}</p>
    <div className="gp-toolbar">
      <label>{t("preset")}<select data-testid="perspective-preset" aria-label={t("preset")} value={presetId} onChange={event => setPreset(event.target.value)}>{catalog.presets.map(item => <option key={item.id} value={item.id}>{item.label}</option>)}</select></label>
      <label>{t("scenario")}<select data-testid="scenario-target" aria-label={t("scenario")} value="" onChange={event => {
        const preset = catalog.presets.find(item => item.id === event.target.value);
        if (preset?.focus) {
          const next = { ...appendActions(perspective, preset.actions), focus: preset.focus };
          const action = preset.actions.find(item => item.op === "override" && item.key === componentId("structure"));
          const structure = String(action && "value" in action ? action.value : selection?.plan.structure ?? "");
          setNavigation(current => current ? navigate(capture(current), { ref: preset.focus!, structure, snapshot: preset.focus!.snapshot }) : createNavigation(preset.focus!, structure));
          setPresetId(preset.id); setCustomized(false); setPerspective(next);
        }
      }}><option value="">{t("focus")}</option>{catalog.presets.filter(item => item.focus).map(item => <option key={item.id} value={item.id}>{item.label}</option>)}</select></label>
      {!!catalog.targets?.length && <label>{t("target")}<select data-testid="object-target" aria-label={t("target")} value="" onChange={event => { const target = catalog.targets?.find(item => item.id === event.target.value); if (target) selectRef(target.ref); }}><option value="">{t("focus")}</option>{catalog.targets.map(item => <option key={item.id} value={item.id}>{item.label}</option>)}</select></label>}
      <button data-testid="gp-back" onClick={() => navigation && replayNavigation(goBack(capture(navigation)))} disabled={!navigation?.back.length}>{t("back")}</button>
      <button data-testid="gp-forward" onClick={() => navigation && replayNavigation(goForward(capture(navigation)))} disabled={!navigation?.forward.length}>{t("forward")}</button>
      <button data-testid="gp-save" onClick={() => { try { localStorage.setItem(catalog.controls.storageKey, exportPerspective(perspective)); setNotice(t("saved")); } catch (failure) { setError(String(failure)); } }}>{t("save")}</button>
      <button data-testid="gp-compare-saved" onClick={() => { try { const raw = localStorage.getItem(catalog.controls.storageKey); if (raw === null) throw new Error(t("restoreError")); setComparison(comparePerspectives(importPerspective(raw).perspective, perspective)); } catch (failure) { setError(String(failure)); } }}>{t("profileDiff")}</button>
      <button data-testid="gp-restore" onClick={() => { const raw = localStorage.getItem(catalog.controls.storageKey); if (raw !== null) { acceptDocument(raw); setNotice(t("restored")); } else setError(t("restoreError")); }}>{t("restore")}</button>
    </div>
    <p className="gp-preset-description">{customized && <strong>{t("custom")} · </strong>}{catalog.presets.find(item => item.id === presetId)?.description}</p>
    {!!snapshots.length && <div className="gp-toolbar">
      <label>{t("snapshot")}<select data-testid="gp-snapshot" aria-label={t("snapshot")} value={String(temporal.snapshot ?? "")} onChange={event => setTemporal("snapshot", event.target.value)}><option value="">{t("none")}</option>{snapshots.map(item => <option key={item.id} value={item.id}>{item.label}</option>)}</select></label>
      <label>{t("compareSnapshot")}<select data-testid="gp-compare-snapshots" aria-label={t("compareSnapshot")} value={Array.isArray(temporal.compareSnapshots) ? String(temporal.compareSnapshots[0] ?? "") : ""} onChange={event => setTemporal("compareSnapshots", event.target.value ? [event.target.value] : [])}><option value="">{t("none")}</option>{snapshots.map(item => <option key={item.id} value={item.id}>{item.label}</option>)}</select></label>
    </div>}
    {!!versionComparisons.length && <details open data-testid="version-differences"><summary>{t("versionDiff")} ({versionComparisons.length})</summary>{versionComparisons.map((comparison, index) => <div key={`${comparison.canonicalKey}-${index}`}><code>{comparison.canonicalKey}</code><p>{comparison.method} · {comparison.status}</p><div className="gp-table-scroll"><table><thead><tr><th>{t("component")}</th><th>{comparison.left.snapshot}</th><th>{comparison.right.snapshot}</th></tr></thead><tbody>{comparison.changes.map(change => <tr key={change.path}><th>{change.path}</th><td><code>{json(change.left)}</code></td><td><code>{json(change.right)}</code></td></tr>)}</tbody></table></div></div>)}</details>}
    {comparison !== null && <details open data-testid="perspective-comparison"><summary>{t("profileDiff")}</summary><pre>{json(comparison)}</pre></details>}
    {error && <p role="alert" data-testid="perspective-error">{error}</p>}
    <p role="status" className="gp-status">{busy ? t("loading") : notice}</p>
    {selection && <>
      <div className="gp-metrics" aria-live="polite">
        <span>{t("visible")}: <strong data-testid="visible-count">{selection.objects.length}</strong></span>
        <span>{t("analysis")}: <strong data-testid="analysis-count">{selection.analysis.length}</strong></span>
        <span>{t("permitted")}: <code>{selection.permissionId}</code></span>
        <span>{t("selectionTime")}: {selection.metrics.selectionMs.toFixed(2)} ms</span>
        <span data-testid="completeness">{selection.complete ? t("complete") : t("partial")}</span>
      </div>
      <p className="gp-policy">{t("permissionsNotice")}</p>
      <div className="gp-content">
        <div className="gp-graph-panel"><Profiler id="graph-perspectives-renderer" onRender={(_id, _phase, duration) => onTiming?.(duration)}><Renderer selection={selection} onSelect={selectRef} onInspectRelation={setRelation} labels={labels} /></Profiler>
          <div className="gp-object-list" aria-label={t("navigation")}>{selection.objects.map(item => <button key={item.key} data-testid="object-entry" data-object-key={item.key} onClick={() => selectRef(item.ref)} aria-current={selected?.key === item.key ? "true" : undefined}>
            <strong>{item.visual.label ?? item.label}</strong><span>{item.kind} · {item.resolution}</span>{item.visual.group && <span>{item.visual.group}</span>}<span>{item.status} · {item.evidence}</span>{item.presentation?.fields.map(field => <span key={field.id}><strong>{field.label}</strong>: {typeof field.value === "string" ? field.value : json(field.value)}</span>)}{item.presentation?.status === "unsupported" && <span>{t("unsupported")}: {item.presentation.missing.join(", ")}</span>}
          </button>)}</div>
        </div>
        <aside className="gp-inspector">
          <h2>{t("why")}</h2>
          {selected ? <><h3>{selected.label}</h3><dl><dt>{t("canonicalIdentity")}</dt><dd data-testid="canonical-identity">{selected.canonicalKey}</dd><dt>{t("representation")}</dt><dd>{selected.ref.representation ?? t("none")}</dd><dt>{t("sourceStatus")}</dt><dd data-testid="source-status">{selected.status}{selected.reason ? ` · ${selected.reason}` : ""}</dd></dl>
            {!!selected.presentation?.fields.length && <dl>{selected.presentation.fields.map(field => <div key={field.id}><dt>{field.label}</dt><dd>{typeof field.value === "string" ? field.value : json(field.value)}</dd></div>)}</dl>}
            <ul>{selected.visualReasons.map((reason, index) => <li key={`${reason.rule}-${index}`}><strong>{reason.dimension}</strong>: {reason.explanation}</li>)}</ul>
            <button data-testid="gp-analysis-add" onClick={() => setPerspective(current => ({ ...current, analysis: { ...current.analysis, selected: [...(current.analysis?.selected ?? []).filter(ref => referenceKey(ref) !== referenceKey(selected.ref)), selected.ref] } }))}>{t("analysisAdd")}</button>
            <p className="gp-policy">{t("analysisNotice")}</p>
            <details><summary>{t("details")}</summary><pre>{json(selected)}</pre></details>
            {selected.representations?.map(ref => <button key={referenceKey(ref)} onClick={() => selectRef(ref)}>{t("representation")}: {ref.representation ?? ref.selector}</button>)}
          </> : <pre data-testid="unrendered-focus">{json(perspective.focus)}</pre>}
          <h3>{t("related")}</h3>
          <div className="gp-relations">{selection.relations.map(item => {
            const target = perspective.focus && referenceKey(item.to) === referenceKey(perspective.focus) ? item.from : item.to;
            return <button key={item.id} data-relation-id={item.id} onClick={() => selectRef(target)}>{item.kind}<span>{target.selector}</span><small>{item.evidence}</small></button>;
          })}</div>
          {relation && <details open><summary>{t("relation")}</summary><pre>{json(relation)}</pre></details>}
        </aside>
      </div>
      {!!selection.plan.unsupported.length && <details open={level === "expert"} data-testid="unsupported-capabilities"><summary>{t("unsupported")} ({selection.plan.unsupported.length})</summary><pre>{json(selection.plan.unsupported)}</pre></details>}
      {!!selection.omissions.length && <details open={level === "expert"} data-testid="omissions"><summary>{t("whyHidden")} ({selection.omissions.length})</summary><pre>{json({ omissions: selection.omissions, continuation: selection.continuation })}</pre></details>}
    </>}
    <details className="gp-address" open={level !== "basic"}><summary>{t("focus")}</summary><label>{t("focus")}<textarea aria-label={t("focus")} data-testid="focus-selector" value={focusText} onChange={event => setFocusText(event.target.value)} /></label><button data-testid="gp-focus-go" onClick={() => { try { const ref = JSON.parse(focusText) as ObjectRef; if (!ref || typeof ref.source !== "string" || typeof ref.selector !== "string") throw new Error("graph_perspectives.invalid_ref"); selectRef(ref); } catch (failure) { setError(String(failure)); } }}>{t("focusAction")}</button><button data-testid="gp-visibility-query" disabled={!selection} onClick={() => { try { const ref = JSON.parse(focusText) as ObjectRef; if (!ref || typeof ref.source !== "string" || typeof ref.selector !== "string") throw new Error("graph_perspectives.invalid_ref"); if (selection) setVisibility(explainVisibility(selection, ref)); } catch (failure) { setError(String(failure)); } }}>{t("visibilityQuery")}</button>{visibility !== null && <pre data-testid="visibility-explanation">{json(visibility)}</pre>}</details>
    {level !== "basic" && <section className="gp-components"><h2>{t("configuration")}</h2>{catalog.capabilities.map(descriptor => <ComponentEditor key={descriptor.id} descriptor={descriptor} value={selection?.plan.explanation.find(row => row.id === descriptor.id)?.value} explanation={selection?.plan.explanation.find(row => row.id === descriptor.id)} labels={catalog.ui} onApply={value => changeComponent(descriptor.id, value)} onAction={op => { setCustomized(true); setPerspective(current => appendActions(current, [{ op, key: descriptor.id }])); }} />)}</section>}
    <details className="gp-transfer" open={level === "expert"}><summary>{t("import")} / {t("export")}</summary>
      <label>{t("raw")}<textarea data-testid="perspective-json" aria-label={t("raw")} value={documentText} onChange={event => setDocumentText(event.target.value)} /></label>
      <div className="gp-toolbar"><button data-testid="gp-import" onClick={() => acceptDocument(documentText)}>{t("applyJson")}</button><button data-testid="gp-export" onClick={download}>{t("download")}</button><label>{t("importFile")}<input type="file" accept="application/json,.json" onChange={event => { const file = event.target.files?.[0]; if (file) void file.text().then(acceptDocument).catch(failure => setError(String(failure))); }} /></label></div>
    </details>
    {selection && <section className="gp-workflow"><h2>{t("analysis")}</h2><p>{t("analysisNotice")}</p><div className="gp-toolbar">
      <button data-testid="gp-plan-export" onClick={() => downloadJson(selection.plan, `${perspective.id}.plan.json`)}>{t("queryPlanExport")}</button>
      <label>{t("analysisMode")}<select data-testid="gp-analysis-mode" aria-label={t("analysisMode")} value={analysisMode} onChange={event => setAnalysisMode(event.target.value as "anchors" | "exact")}>{(["exact", "anchors"] as const).map(mode => <option key={mode} value={mode}>{t(mode)}</option>)}</select></label>
      <button data-testid="gp-workflow-prepare" disabled={!prepareAnalysis} onClick={() => { try { if (prepareAnalysis) setAnalysisExport(prepareAnalysis(perspective, selection, analysisMode)); } catch (failure) { setError(String(failure)); } }}>{t("analysisExport")}</button>
    </div>{!prepareAnalysis && <p>{t("unsupported")}</p>}{analysisExport && <details open><summary>{t("analysisExport")} · {analysisExport.status}</summary><pre data-testid="analysis-export">{json(analysisExport)}</pre><button onClick={() => downloadJson(analysisExport, `${perspective.id}.analysis.json`)}>{t("download")}</button></details>}</section>}
    {selection && <details><summary>{t("plan")}</summary><pre data-testid="query-plan">{json(selection.plan)}</pre><pre data-testid="selection-json">{json({ objectKeys: selection.objects.map(item => item.key), graph: selection.graph, analysis: selection.analysis, omissions: selection.omissions })}</pre></details>}
  </section>;
}
function ComponentEditor({ descriptor, value, explanation, labels, onApply, onAction }: {
  descriptor: CapabilityDescriptor; value: unknown; explanation?: unknown; labels: Record<string, string>; onApply(value: unknown): void;
  onAction(op: "disable" | "exclude" | "reenable" | "clear_override"): void;
}) {
  const [draft, setDraft] = useState(json(value) ?? "null"), [error, setError] = useState("");
  useEffect(() => setDraft(json(value) ?? "null"), [value]);
  return <details className="gp-component"><summary>{descriptor.label} · {descriptor.status}</summary>
    <p>{descriptor.reason}</p><pre>{json(explanation)}</pre><textarea aria-label={descriptor.label} value={draft} onChange={event => setDraft(event.target.value)} />
    <div className="gp-toolbar"><button onClick={() => { try { onApply(JSON.parse(draft)); setError(""); } catch (failure) { setError(String(failure)); } }}>{labels.applyComponent}</button>{(["disable", "exclude", "reenable", "clear_override"] as const).map(op => <button key={op} onClick={() => onAction(op)}>{labels[op === "clear_override" ? "clearOverride" : op]}</button>)}</div>
    {error && <p role="alert">{error}</p>}
  </details>;
}
