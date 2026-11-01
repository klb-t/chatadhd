import { useEffect, useRef, useState } from "react";
import { api } from "../api";
import { loadImageRegion, loadImageRepresentations, saveImageRegionSelection,
  type ImageRegion, type ImageRepresentation, type RegionRequest } from "../api/image-region-context";
import { createSourceTaskGate, sourceSelector } from "../api/text-sources";
import words from "../content/image-region-context.words.json";
import { dataLabel, useShellLanguage } from "../shell/text";

interface Props { onGraph: (nodeId: string) => void }
type Word = keyof typeof words.labels;

/** Reads existing OCR nodes only. Opening, selecting a row and showing the graph never change account context. */
export default function ImageRegionContextPanel(props: Props) {
  const [open, setOpen] = useState(false), language = useShellLanguage();
  return <details data-testid="image-region-context-panel" onToggle={e => setOpen(e.currentTarget.open)}>
    <summary>{dataLabel(words.labels.title, language, words.labels.title.pl)}</summary>
    {open && <RegionBrowser {...props} />}
  </details>;
}

function RegionBrowser({ onGraph }: Props) {
  const language = useShellLanguage();
  const w = (key: Word) => dataLabel(words.labels[key], language, words.labels[key].pl);
  const [kind, setKind] = useState(words.presets.representation_kind), [limit, setLimit] = useState(String(words.presets.node_limit));
  const [window, setWindow] = useState<{ count: number; limit: number } | null>(null);
  const [representations, setRepresentations] = useState<ImageRepresentation[]>([]), [representationId, setRepresentationId] = useState("");
  const [regionId, setRegionId] = useState(""), [region, setRegion] = useState<ImageRegion | null>(null);
  const [config, setConfig] = useState<Record<string, unknown> | null>(null), [hops, setHops] = useState(String(words.presets.relation_hops));
  const [busy, setBusy] = useState(false), [error, setError] = useState(""), [notice, setNotice] = useState("");
  const alive = useRef(false), gate = useRef(createSourceTaskGate()), controller = useRef<AbortController | null>(null);
  const representation = representations.find(r => r.nodeId === representationId);
  // The integer bound belongs to the existing HTTP filter's signed int parser, not a product limit.
  const validLimit = limit.trim() !== "" && Number.isSafeInteger(Number(limit)) && Number(limit) > 0 && Number(limit) <= 2147483647;
  const validHops = hops.trim() !== "" && Number.isSafeInteger(Number(hops)) && Number(hops) >= 0;
  let selector: ReturnType<typeof sourceSelector> | null = null;
  try { if (config) selector = sourceSelector(config); } catch { /* No write from unvalidated account state. */ }

  const run = async (operation: (request: RegionRequest) => Promise<void>) => {
    const ticket = gate.current.begin();
    if (!ticket) return;
    const abort = new AbortController();
    controller.current = abort;
    const isCurrent = () => alive.current && ticket.isCurrent() && !abort.signal.aborted;
    setBusy(true); setError(""); setNotice("");
    try { await operation({ signal: abort.signal, isCurrent }); }
    catch (failure) { if (isCurrent()) { setError(failure instanceof Error ? failure.message : String(failure)); setConfig(null); } }
    finally { if (isCurrent()) setBusy(false); ticket.finish(); if (controller.current === abort) controller.current = null; }
  };
  const load = async (request: RegionRequest) => {
    if (!validLimit) return;
    const result = await loadImageRepresentations(api, { ...(kind.trim() ? { kind: kind.trim() } : {}), limit: Number(limit) }, request);
    if (!request.isCurrent?.()) return;
    setRepresentations(result.representations); setConfig(result.config);
    setWindow({ count: result.recordCount, limit: Number(limit) });
    setHops(String(result.selector.hops ?? words.presets.relation_hops));
    setRepresentationId(id => result.representations.some(r => r.nodeId === id) ? id : "");
    setRegionId(""); setRegion(null);
  };
  useEffect(() => {
    alive.current = true;
    void run(load);
    return () => { alive.current = false; controller.current?.abort(); gate.current.invalidate(); };
  }, []); // Each open reads account state anew; native graph/config methods have no transport AbortSignal option.

  const chooseRegion = (id: string) => {
    setRegionId(id); setRegion(null); setNotice("");
    if (!id || !representation) return;
    void run(async request => {
      const result = await loadImageRegion(api, representation, id, request);
      if (request.isCurrent?.()) setRegion(result);
    });
  };
  const save = (include: boolean) => {
    if (!region || !selector || (include && !validHops)) return;
    void run(async request => {
      const saved = await saveImageRegionSelection(api, region, include, Number(hops), request);
      if (request.isCurrent?.()) { setConfig(saved); setNotice(w("saved")); }
    });
  };
  const supported = representation?.status === "recognized" && representation.evidence === "inferred" && representation.regionNodes.length > 0;

  return <div data-testid="image-region-context-browser" style={{ overflowWrap: "anywhere" }}>
    <p>{w("scope")}</p><p>{w("context")}</p><p>{w("metadata")}</p>
    <fieldset disabled={busy}>
      <label>{w("kind")}<input data-testid="image-region-kind" value={kind} onChange={e => setKind(e.target.value)} /></label>
      <label>{w("limit")}<input data-testid="image-region-limit" type="number" min="1" max="2147483647" value={limit} onChange={e => setLimit(e.target.value)} /></label>
      <button type="button" data-testid="image-region-reload" disabled={!validLimit} onClick={() => void run(load)}>{w("reload")}</button>
      {window && <p data-testid="image-region-window">{w("window")}: {window.count} / {window.limit} / {representations.length}</p>}
      <p>{w("partial")}</p>
      {window && !representations.length && <p>{w("empty")}</p>}
      <label>{w("representation")}<select data-testid="image-region-representation" value={representationId} onChange={e => {
        setRepresentationId(e.target.value); setRegionId(""); setRegion(null); setNotice("");
      }}>
        <option value="">{w("choose")}</option>
        {representations.map(r => <option key={r.nodeId} value={r.nodeId}>{r.originalHash} · {r.representationHash}</option>)}
      </select></label>
      {representation && <div data-testid="image-region-representation-metadata" data-original-hash={representation.originalHash}>
        <p>{w("original")}: <code>{representation.originalHash}</code></p>
        <p>{w("originalSource")}: <code>{representation.sourceId}</code></p>
        <p>{w("representationHash")}: <code>{representation.representationHash}</code></p>
        <p>{w("status")}: {dataLabel(words.statuses[representation.status as keyof typeof words.statuses], language, representation.status)}</p>
        {representation.reason && <p>{w("reason")}: <code>{representation.reason}</code></p>}
        <details><summary>{w("method")}</summary><pre>{JSON.stringify(representation.method, null, 2)}</pre></details>
        {!supported && <p>{w("noRegions")}</p>}
        {supported && <label>{w("region")}<select data-testid="image-region-select" value={regionId} onChange={e => chooseRegion(e.target.value)}>
          <option value="">{w("choose")}</option>{representation.regionNodes.map(id => <option key={id} value={id}>{id}</option>)}
        </select></label>}
      </div>}
      {region && <div data-testid="image-region-detail" data-node-id={region.nodeId} data-evidence="inferred" data-confidence="null">
        <p><code>{region.nodeId}</code></p><p>{w("regionId")}: <code>{region.regionId}</code></p>
        <p>{w("inferred")}</p><p>{w("confidence")}</p>
        <p>{w("observation")}: <code>{region.observationId}</code> · <code>{region.pointer}</code></p>
        <pre data-testid="image-region-text" style={{ whiteSpace: "pre-wrap" }}>{region.text}</pre>
        <p>{w("frame")}: <code>{JSON.stringify(region.frame)}</code></p>
        <p>{w("bounds")}: <code>{JSON.stringify(region.bounds)}</code></p>
        <svg role="img" aria-label={w("diagram")} viewBox={`0 0 ${region.frame.extent[0]} ${region.frame.extent[1]}`}
          style={{ display: "block", width: "100%", maxHeight: "20vh", border: "1px solid currentColor" }}>
          <title>{w("diagram")}</title>
          <rect x={region.bounds.origin[0]} y={region.bounds.origin[1]} width={region.bounds.size[0]} height={region.bounds.size[1]}
            fill="currentColor" fillOpacity="0.15" stroke="currentColor" vectorEffect="non-scaling-stroke" />
        </svg>
        <p>{w("diagram")}</p><p>{w("relation")}: <code>{region.relation}</code> → <code>{representation?.nodeId}</code></p>
        <label>{w("hops")}<input data-testid="image-region-hops" type="number" min="0" step="1" value={hops} onChange={e => setHops(e.target.value)} /></label>
        {selector && <p data-testid="image-region-selected">{w(selector.ids.includes(region.nodeId) ? "selected" : "notSelected")}</p>}
        <button type="button" data-testid="image-region-add" disabled={!selector || !validHops || !api.mergeConfigKey} onClick={() => save(true)}>{w("add")}</button>
        <button type="button" data-testid="image-region-remove" disabled={!selector?.ids.includes(region.nodeId) || !api.mergeConfigKey} onClick={() => save(false)}>{w("remove")}</button>
        <button type="button" data-testid="image-region-graph" onClick={() => onGraph(region.nodeId)}>{w("graph")}</button>
      </div>}
    </fieldset>
    {selector && <details data-testid="image-region-account-selection"><summary>{w("accountSelection")}</summary>
      <p>{w(selector.enabled ? "enabled" : "disabled")} · {selector.hops ?? w("unsetHops")}</p>
      <ul>{selector.ids.map((id, index) => <li key={`${index}-${id}`}><code>{id}</code></li>)}</ul>
    </details>}
    {region && !api.mergeConfigKey && <p role="status">{w("details")}: <code>text_source_merge_unavailable</code></p>}
    {busy && <p role="status">{w("busy")}</p>}
    {notice && <p role="status" data-testid="image-region-notice">{notice}</p>}
    {error && <div role="alert" data-testid="image-region-error"><p>{w("error")}</p><details><summary>{w("details")}</summary><code>{error}</code></details></div>}
  </div>;
}
