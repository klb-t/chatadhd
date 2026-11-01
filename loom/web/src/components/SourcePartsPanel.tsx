import { useEffect, useId, useRef, useState } from "react";
import { api } from "../api";
import { createSourceTaskGate, materializedSource, sourceDerivation, sourceOriginalHash, materializationRequest, readSourceOccurrence, readSourceParts, saveSourceSelection, sourceSelector, textSources,
  type SourceOccurrence, type SourceParts, type TextSource } from "../api/text-sources";
import words from "../content/text-sources.words.json";
import { dataLabel, useShellLanguage } from "../shell/text";
import AttachmentOriginal from "./AttachmentOriginal";
import SourceProjectionReview from "./SourceProjectionReview";
import { validateUpgradeTarget, type ProjectionUpgrade } from "../api/projection-review";

type Word = Exclude<keyof typeof words, "formats" | "defaults" | "blockKinds" | "warnings">;
interface Props { upload?: boolean; onGraph?: (id: string) => void; graphEnabled?: boolean; onEnableGraph?: () => void }

/** A view over SourceRecord, G1 projection receipts and the existing native selector; no local source database. */
export default function SourcePartsPanel(props: Props) {
  const [open, setOpen] = useState(false);
  const language = useShellLanguage();
  return <details data-testid="source-parts-panel" onToggle={e => setOpen(e.currentTarget.open)} style={{ marginBlock: 12, maxHeight: "45vh", overflow: "auto", flexShrink: 0 }}>
    <summary>{dataLabel(words.title, language, words.title.en)}</summary>
    {open && <SourcePartsBrowser {...props} />}
  </details>;
}

function SourcePartsBrowser({ upload, onGraph, graphEnabled, onEnableGraph }: Props) {
  const language = useShellLanguage(), uid = useId();
  const w = (id: Word) => dataLabel(words[id], language, words[id].en);
  const [sources, setSources] = useState<TextSource[]>([]), [selected, setSelected] = useState("");
  const [recordCount, setRecordCount] = useState(0), [limit, setLimit] = useState(String(words.defaults.source_limit));
  const [projections, setProjections] = useState<SourceParts[]>([]), [projectionId, setProjectionId] = useState("");
  const [nodeId, setNodeId] = useState(""), [occurrence, setOccurrence] = useState<SourceOccurrence | null>(null);
  const [config, setConfig] = useState<Record<string, unknown> | null>(null), [hops, setHops] = useState(String(words.defaults.hops));
  const [file, setFile] = useState<File | null>(null), [format, setFormat] = useState(""), [revision, setRevision] = useState("");
  const uploadKey = useRef("");
  const fileInput = useRef<HTMLInputElement>(null);
  const [busy, setBusy] = useState(false), [error, setError] = useState(""), [notice, setNotice] = useState("");
  const alive = useRef(true), generation = useRef(0);
  const loadGeneration = useRef(0), taskGate = useRef(createSourceTaskGate()), readGeneration = useRef(0);
  const source = sources.find(s => s.id === selected);
  const projection = projections.find(p => p.projection_node === projectionId && p.source_id === source?.id);
  const derivation = source ? sourceDerivation(source) : null;
  const originalHash = source ? sourceOriginalHash(source) : "";
  const blockLabel = (kind: string) => {
    const labels = words.blockKinds[kind as keyof typeof words.blockKinds];
    return labels ? dataLabel(labels, language, labels.en) : w("unknownBlock");
  };
  const warningLabel = (code: string) => {
    const labels = words.warnings[code as keyof typeof words.warnings];
    return (labels ? dataLabel(labels, language, labels.en) : w("unknownWarning") + " " + code).split("{file}").join(source?.title ?? "");
  };
  const validLimit = Number.isSafeInteger(Number(limit)) && Number(limit) > 0 && Number(limit) <= 2147483647;
  const validHops = hops.trim() !== "" && Number.isSafeInteger(Number(hops)) && Number(hops) >= 0;
  let selector: ReturnType<typeof sourceSelector> | null = null;
  try { if (config) selector = sourceSelector(config); } catch { /* run() displays read failures; no write based on malformed state */ }
  const run = async (operation: (isCurrent: () => boolean) => Promise<void>) => {
    const task = taskGate.current.begin();
    if (!task) return;
    const current = () => alive.current && task.isCurrent();
    setBusy(true); setError(""); setNotice("");
    try { await operation(current); } catch (e) { if (current()) setError(e instanceof Error ? e.message : String(e)); }
    finally { if (current()) setBusy(false); task.finish(); }
  };
  const load = async (isCurrent: () => boolean, preferred?: { id: string; key: string; hash: string }) => {
    if (!validLimit) return;
    const token = ++loadGeneration.current;
    const [rows, cfg] = await Promise.all([api.listSources(Number(limit)), api.getConfig()]);
    const nativeSelection = sourceSelector(cfg);
    if (!isCurrent() || loadGeneration.current !== token) return;
    const list = textSources(rows);
    const chosen = preferred && list.find(s => s.metadata.source_key === preferred.key && sourceOriginalHash(s) === preferred.hash);
    if (preferred && !chosen) throw Error("text_source_upload_registration_missing");
    setSources(list); setRecordCount(rows.length); setConfig(cfg);
    setSelected(current => chosen?.id ?? (list.some(s => s.id === current) ? current : list[0]?.id ?? ""));
    setHops(String(nativeSelection.hops ?? words.defaults.hops));
  };
  useEffect(() => {
    alive.current = true;
    void run(current => load(current));
    return () => { alive.current = false; generation.current++; loadGeneration.current++; readGeneration.current++; taskGate.current.invalidate(); };
  }, []); // Mount/reopen reads native state; nothing is materialized by reading.
  useEffect(() => {
    const token = ++generation.current;
    readGeneration.current++;
    setProjections([]); setProjectionId(""); setNodeId(""); setOccurrence(null);
    if (!source || source.parser !== "loom.source_parts" || !validLimit) return;
    // Read projection receipts directly: a 2-hop expansion may reach its node cap before the receipt.
    void api.getNodes({ kind: "text_source_projection", limit: Number(limit) }).then(nodes => {
      const parts = nodes.filter(n => n.metadata?.schema === "loom.source_text_parts/1" && n.metadata.source_id === source.id)
        .map(n => readSourceParts(n.metadata, source));
      if (alive.current && generation.current === token) {
        setProjections(parts); setProjectionId(parts[0]?.projection_node ?? ""); setNodeId(parts[0]?.occurrence_nodes[0] ?? "");
      }
    }).catch(e => { if (alive.current && generation.current === token) setError(String(e)); });
    return () => { generation.current++; };
  }, [source]);

  const refreshProjectionReview = async (isCurrent: () => boolean, result: ProjectionUpgrade) => {
    if (!source || !projection || !validLimit) throw Error("projection_review_refresh_missing");
    const token = ++loadGeneration.current, sourceToken = generation.current;
    const [rows, nodes, cfg] = await Promise.all([
      api.listSources(Number(limit)), api.getNodes({ kind: "text_source_projection", limit: Number(limit) }), api.getConfig(),
    ]);
    if (!isCurrent() || token !== loadGeneration.current || sourceToken !== generation.current) return;
    sourceSelector(cfg);
    const list = textSources(rows);
    const parts = nodes.filter(n => n.metadata?.schema === "loom.source_text_parts/1" && n.metadata.source_id === source.id)
      .map(n => readSourceParts(n.metadata, source));
    const nextSource = list.find(s => s.id === result.new_source);
    const nextReceipt = nodes.find(n => n.id === result.new_projection);
    setConfig(cfg);
    // A bounded list may no longer include the old version. Keep its visible receipt and explicit choice;
    // the user can increase the existing list limit. No selector write or automatic activation occurs.
    if (!list.some(s => s.id === source.id) || !parts.some(p => p.projection_node === projection.projection_node) || !nextSource || !nextReceipt)
      throw Error("projection_review_refresh_missing");
    validateUpgradeTarget(result, readSourceParts(nextReceipt.metadata, nextSource));
    // Preserve the immutable selected descriptor's identity so refreshing does not reset the open projection/review.
    setSources(list.map(s => s.id === source.id ? source : s)); setRecordCount(rows.length); setProjections(parts);
  };

  const uploadOriginal = async (isCurrent: () => boolean) => {
    const send = api.actions?.clientBlobUpload;
    if (!send || !file) throw Error(w("unsupported"));
    const previous = sources.find(s => s.id === revision);
    if (revision && !previous) throw Error("text_source_previous_unavailable");
    if (previous && format === "docx" && !sourceDerivation(previous)) throw Error(w("revisionInvalid"));
    const key = previous?.metadata.source_key ?? uploadKey.current;
    const stored = await send(file, { name: file.name, source_key: key, format,
      ...(previous ? { previous_source_id: previous.id } : {}) });
    if (stored.source_key !== key || !stored.source_id) throw Error(w("missingHook"));
    if (!isCurrent()) return;
    await load(isCurrent, { id: stored.source_id, key, hash: stored.hash });
    if (isCurrent()) { setNotice(w("uploaded")); setFile(null); uploadKey.current = ""; if (fileInput.current) fileInput.current.value = ""; }
  };
  const materialize = async (isCurrent: () => boolean) => {
    const action = api.actions?.clientBlobText;
    if (!source || !action) throw Error(w("unsupported"));
    const result = await action(materializationRequest(source, language));
    // The upload receipt and G1 source have distinct IDs; reload the canonical descriptor.
    if (!isCurrent()) return;
    const rows = await api.listSources(Number(limit));
    if (!isCurrent()) return;
    const made = materializedSource(rows, result, source);
    setSources(textSources(rows)); setRecordCount(rows.length); setSelected(made.id);
  };
  const read = async (isCurrent: () => boolean) => {
    const action = api.actions?.clientBlobTextRead;
    if (!source || !nodeId || !action) throw Error(w("unsupported"));
    const token = readGeneration.current, sourceToken = generation.current;
    setOccurrence(null);
    const read = readSourceOccurrence(await action({ node_id: nodeId }), source, nodeId);
    if (isCurrent() && readGeneration.current === token && generation.current === sourceToken) setOccurrence(read);
  };
  const select = async (id: string, include: boolean, isCurrent: () => boolean) => {
    if (!api.mergeConfigKey) throw Error(w("unsupported"));
    // Fresh read and a server-side merge preserve unrelated execution/graph_reply/method settings.
    const saved = await saveSourceSelection(api, id, include, Number(hops), isCurrent);
    if (isCurrent()) { setConfig(saved); setNotice(w("saved")); }
  };

  return <div data-testid="source-parts-browser" style={{ overflowWrap: "anywhere" }}>
    <p>{w("limits")}</p><p>{w("scope")}</p>
    <fieldset disabled={busy}>
      <label>{w("limit")}<input data-testid="source-list-limit" type="number" min="1" max="2147483647" value={limit} onChange={e => setLimit(e.target.value)} /></label>
      <button type="button" data-testid="source-reload" disabled={!validLimit} onClick={() => void run(current => load(current))}>{w("reload")}</button>
      <p data-testid="source-window">{w("window")}: {recordCount} / {limit}</p>
      {upload && <div>
        <label htmlFor={uid + "file"}>{w("upload")}</label>
        <input ref={fileInput} id={uid + "file"} data-testid="source-upload-file" type="file" onChange={e => {
          const next = e.target.files?.[0] ?? null; setFile(next); uploadKey.current = "upload:" + crypto.randomUUID();
          const ext = next?.name.split(".").pop()?.toLowerCase();
          setFormat(Object.entries(words.formats).find(([, extensions]) => extensions.includes(ext ?? ""))?.[0] ?? "");
        }} />
        <label>{w("format")}<select data-testid="source-upload-format" value={format} onChange={e => setFormat(e.target.value)}>
          <option value="">{w("opaque")}</option>{(Object.keys(words.formats) as (keyof typeof words.formats)[]).map(f => <option key={f} value={f}>{w(f)}</option>)}
        </select></label>
        <label>{w("revise")}<select data-testid="source-upload-revision" value={revision} onChange={e => setRevision(e.target.value)}>
          <option value="">{w("new")}</option>{sources.filter(s => s.parser === "loom.source_parts" && (format !== "docx" || sourceDerivation(s))).map(s => <option key={s.id} value={s.id}>{s.title} · {sourceOriginalHash(s).slice(0, 12)} · {s.metadata.source_key}</option>)}
        </select></label>
        <button type="button" data-testid="source-upload" disabled={!file || !api.actions?.clientBlobUpload} onClick={() => void run(uploadOriginal)}>{w("upload")}</button>
      </div>}
      <label>{w("version")}<select data-testid="source-version" value={selected} onChange={e => { readGeneration.current++; setOccurrence(null); setSelected(e.target.value); }}>
        <option value="">—</option>{sources.map(s => <option key={s.id} value={s.id}>{s.title} · {sourceOriginalHash(s).slice(0, 12)} · {s.metadata.source_key}</option>)}
      </select></label>
      {!sources.length && <p>{w("empty")}</p>}
      {source && <div data-testid="source-descriptor" data-source-id={source.id} data-source-key={source.metadata.source_key} data-hash={source.blob_hash} data-original-hash={originalHash}>
        <p>{source.title} · {source.metadata.source_key}</p>
        <div data-testid="source-original"><p>{w("original")} · SHA256: {originalHash}</p>
          <AttachmentOriginal key={originalHash} reference={"blob:" + originalHash} name={source.title} />
        </div>
        {derivation && <div data-testid="source-derived"><p>{w("derived")} · SHA256: {source.blob_hash}</p>
          <AttachmentOriginal key={source.blob_hash} reference={"blob:" + source.blob_hash} name={source.title + ".txt"} />
          <p>{w("derivedAbout")}</p>
        </div>}
        {source.metadata.previous_source_id && <p data-testid="source-previous">{w(derivation ? "previousDerived" : "previous")}: {source.metadata.previous_source_id}</p>}
        <button type="button" data-testid="source-materialize" disabled={!Object.keys(words.formats).includes(source.format) || !api.actions?.clientBlobText} onClick={() => void run(materialize)}>{w("materialize")}</button>
        {source.parser !== "loom.source_parts" && <p>{w("pending")}</p>}
        {source.parser === "loom.source_parts" && !projections.length && <p>{w("projectionWindow")}</p>}
        <label>{w("projection")}<select data-testid="source-projection" value={projectionId} onChange={e => {
          readGeneration.current++; setProjectionId(e.target.value); setNodeId(projections.find(p => p.projection_node === e.target.value)?.occurrence_nodes[0] ?? ""); setOccurrence(null);
        }}>{projections.map(p => <option key={p.projection_node} value={p.projection_node}>{p.projection_node}</option>)}</select></label>
        {projection && <>
          <SourceProjectionReview key={source.id + ":" + projection.projection_node} projection={projection} run={run} refresh={refreshProjectionReview} />
          <p data-testid="source-partial" data-status={projection.status} data-reason={projection.reason}>{w("partial")} {projection.occurrence_nodes.length}</p>
          {derivation && <>
            <div data-testid="source-warnings"><p>{w("warningTitle")}</p>
              <ul>{projection.warnings?.map((code, i) => <li key={i} data-warning={code}>{warningLabel(code)}</li>)}</ul>
            </div>
            <p>{w("recognized")}</p>
            <ol data-testid="source-blocks">{projection.observations?.map(block => <li key={block.ordinal} data-kind={block.attrs.recognized_kind}>
              {blockLabel(block.attrs.recognized_kind)} · {block.ordinal + 1} · {typeof block.attrs.original_locator === "string" ? block.attrs.original_locator : JSON.stringify(block.attrs.original_locator)}
              <pre style={{ whiteSpace: "pre-wrap" }}>{block.text || w("emptyBlock")}</pre>
            </li>)}</ol>
          </>}
          {projection.structure_nodes && <div data-testid="source-structure"><p>{w("structure")}</p><ol>
            {projection.structure_nodes.map(n => <li key={n.node_id} data-node-id={n.node_id} data-parent-id={n.parent} data-kind={n.kind}>
              {n.kind} · {n.title} <code>{JSON.stringify(n.locator ?? {})}</code>
              {onGraph && <button type="button" onClick={() => onGraph(n.node_id)}>{w("graph")}</button>}
            </li>)}
          </ol></div>}
          {projection.children && <div data-testid="source-archive-children"><p>{w("members")}</p><ul>
            {projection.children.map(child => <li key={child.source_id} data-source-id={child.source_id}>
              {sources.find(s => s.id === child.source_id)?.title ?? child.source_id} · {child.status}
              {sources.some(s => s.id === child.source_id) && <button type="button" onClick={() => {setSelected(child.source_id);setOccurrence(null);}}>{w("openMember")}</button>}
            </li>)}
          </ul></div>}
          <label>{w("occurrence")}<select data-testid="source-occurrence" value={nodeId} onChange={e => { readGeneration.current++; setNodeId(e.target.value); setOccurrence(null); }}>
            {projection.occurrence_nodes.map((id, i) => <option key={id} value={id}>{i + 1} · {projection.method.level === "recognized_block" && projection.observations?.[i] ? blockLabel(projection.observations[i].attrs.recognized_kind) + " · " : ""}{id}</option>)}
          </select></label>
          <button type="button" data-testid="source-read" disabled={!nodeId || !api.actions?.clientBlobTextRead} onClick={() => void run(read)}>{w("read")}</button>
          {onGraph && <button type="button" data-testid="source-show-graph" onClick={() => onGraph(nodeId || projection.source_node)}>{w("graph")}</button>}
        </>}
        {occurrence && occurrence.source.id === source?.id && occurrence.node_id === nodeId && <div data-testid="source-read-result" data-node-id={occurrence.node_id}>
          <p>{w(derivation ? "derivedRange" : "range")}: [{occurrence.provenance.byte_range.join(", ")})</p>
          <pre data-testid="source-text" style={{ whiteSpace: "pre-wrap", maxHeight: 240, overflow: "auto" }}>{occurrence.text}</pre>
          <label>{w("hops")}<input data-testid="source-hops" type="number" min="0" value={hops} onChange={e => setHops(e.target.value)} /></label>
          <button type="button" data-testid="source-select" disabled={!selector || !validHops || !api.mergeConfigKey} onClick={() => void run(current => select(occurrence.node_id, true, current))}>{w("add")}</button>
        </div>}
      </div>}
      {selector && <div data-testid="source-selector" data-enabled={selector.enabled}>
        <p>{w(selector.enabled ? "enabled" : "disabled")}</p><p>{w("selected")}</p>
        {selector.ids.map((id, i) => <div key={id + ":" + i} data-testid="source-selected-id" data-node-id={id}>{id} <button type="button" onClick={() => void run(current => select(id, false, current))}>{w("remove")}</button></div>)}
      </div>}
      {graphEnabled === false && <p role="status">{w("graphOff")} {onEnableGraph && <button type="button" onClick={onEnableGraph}>{w("enableGraph")}</button>}</p>}
    </fieldset>
    {busy && <p role="status">{w("busy")}</p>}{notice && <p role="status" data-testid="source-notice">{notice}</p>}
    {error && <p role="alert" data-testid="source-error">{w("error")} {error}</p>}
  </div>;
}
