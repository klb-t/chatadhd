import { useEffect, useId, useRef, useState } from "react";
import { api } from "../api";
import { materializationRequest, readSourceOccurrence, readSourceParts, saveSourceSelection, sourceSelector, textSources,
  type SourceOccurrence, type SourceParts, type TextSource } from "../api/text-sources";
import words from "../content/text-sources.words.json";
import { dataLabel, useShellLanguage } from "../shell/text";
import AttachmentOriginal from "./AttachmentOriginal";

type Word = Exclude<keyof typeof words, "formats" | "defaults">;
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
  const loadGeneration = useRef(0), operationGeneration = useRef(0);
  const source = sources.find(s => s.id === selected), projection = projections.find(p => p.projection_node === projectionId);
  const validLimit = Number.isSafeInteger(Number(limit)) && Number(limit) > 0 && Number(limit) <= 2147483647;
  const validHops = hops.trim() !== "" && Number.isSafeInteger(Number(hops)) && Number(hops) >= 0;
  let selector: ReturnType<typeof sourceSelector> | null = null;
  try { if (config) selector = sourceSelector(config); } catch { /* run() displays read failures; no write based on malformed state */ }
  const run = async (operation: () => Promise<void>) => {
    const token = ++operationGeneration.current;
    setBusy(true); setError(""); setNotice("");
    try { await operation(); } catch (e) { if (alive.current && operationGeneration.current === token) setError(e instanceof Error ? e.message : String(e)); }
    finally { if (alive.current && operationGeneration.current === token) setBusy(false); }
  };
  const load = async (preferred?: { id: string; key: string; hash: string }) => {
    if (!validLimit) return;
    const token = ++loadGeneration.current;
    const [rows, cfg] = await Promise.all([api.listSources(Number(limit)), api.getConfig()]);
    const nativeSelection = sourceSelector(cfg);
    if (!alive.current || loadGeneration.current !== token) return;
    const list = textSources(rows);
    setSources(list); setRecordCount(rows.length); setConfig(cfg);
    const chosen = preferred && list.find(s => s.id === preferred.id || (s.metadata.source_key === preferred.key && s.blob_hash === preferred.hash));
    if (preferred && !chosen) throw Error("text_source_upload_registration_missing");
    setSelected(current => chosen?.id ?? (list.some(s => s.id === current) ? current : list[0]?.id ?? ""));
    setHops(String(nativeSelection.hops ?? words.defaults.hops));
  };
  useEffect(() => {
    alive.current = true;
    void run(() => load());
    return () => { alive.current = false; generation.current++; loadGeneration.current++; operationGeneration.current++; };
  }, []); // Mount/reopen reads native state; nothing is materialized by reading.
  useEffect(() => {
    const token = ++generation.current;
    setProjections([]); setProjectionId(""); setNodeId(""); setOccurrence(null);
    if (!source || source.parser !== "loom.source_parts") return;
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

  const uploadOriginal = async () => {
    const send = api.actions?.clientBlobUpload;
    if (!send || !file) throw Error(w("unsupported"));
    const previous = sources.find(s => s.id === revision);
    if (revision && !previous) throw Error("text_source_previous_unavailable");
    const key = previous?.metadata.source_key ?? uploadKey.current;
    const stored = await send(file, { name: file.name, source_key: key, format,
      ...(previous ? { previous_source_id: previous.id } : {}) });
    if (stored.source_key !== key || !stored.source_id) throw Error(w("missingHook"));
    if (!alive.current) return;
    await load({ id: stored.source_id, key, hash: stored.hash });
    if (alive.current) { setNotice(w("uploaded")); setFile(null); uploadKey.current = ""; if (fileInput.current) fileInput.current.value = ""; }
  };
  const materialize = async () => {
    const action = api.actions?.clientBlobText;
    if (!source || !action) throw Error(w("unsupported"));
    const result = await action(materializationRequest(source, language));
    // The upload receipt and G1 source have distinct IDs; reload the canonical descriptor.
    const rows = textSources(await api.listSources(Number(limit)));
    const made = rows.find(s => s.parser === "loom.source_parts" && s.metadata.source_key === source.metadata.source_key && s.blob_hash === source.blob_hash);
    if (!made) throw Error("text_source_descriptor_missing");
    readSourceParts(result, made);
    if (alive.current) { setSources(rows); setSelected(made.id); }
  };
  const read = async () => {
    const action = api.actions?.clientBlobTextRead;
    if (!source || !nodeId || !action) throw Error(w("unsupported"));
    const token = generation.current;
    const read = readSourceOccurrence(await action({ node_id: nodeId }), source, nodeId);
    if (alive.current && generation.current === token) setOccurrence(read);
  };
  const select = async (id: string, include: boolean) => {
    if (!api.mergeConfigKey) throw Error(w("unsupported"));
    // Fresh read and a server-side merge preserve unrelated execution/graph_reply/method settings.
    const saved = await saveSourceSelection(api, id, include, Number(hops));
    if (alive.current) { setConfig(saved); setNotice(w("saved")); }
  };

  return <div data-testid="source-parts-browser" style={{ overflowWrap: "anywhere" }}>
    <p>{w("limits")}</p><p>{w("scope")}</p>
    <fieldset disabled={busy}>
      <label>{w("limit")}<input data-testid="source-list-limit" type="number" min="1" max="2147483647" value={limit} onChange={e => setLimit(e.target.value)} /></label>
      <button type="button" data-testid="source-reload" disabled={!validLimit} onClick={() => void run(() => load())}>{w("reload")}</button>
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
          <option value="">{w("new")}</option>{sources.filter(s => s.parser === "loom.source_parts").map(s => <option key={s.id} value={s.id}>{s.title} · {s.blob_hash.slice(0, 12)} · {s.metadata.source_key}</option>)}
        </select></label>
        <button type="button" data-testid="source-upload" disabled={!file || !api.actions?.clientBlobUpload} onClick={() => void run(uploadOriginal)}>{w("upload")}</button>
      </div>}
      <label>{w("version")}<select data-testid="source-version" value={selected} onChange={e => setSelected(e.target.value)}>
        <option value="">—</option>{sources.map(s => <option key={s.id} value={s.id}>{s.title} · {s.blob_hash.slice(0, 12)} · {s.metadata.source_key}</option>)}
      </select></label>
      {!sources.length && <p>{w("empty")}</p>}
      {source && <div data-testid="source-descriptor" data-source-id={source.id} data-source-key={source.metadata.source_key} data-hash={source.blob_hash}>
        <p>{source.title} · {source.metadata.source_key}<br />SHA256: {source.blob_hash}</p>
        <AttachmentOriginal reference={"blob:" + source.blob_hash} name={source.title} />
        <button type="button" data-testid="source-materialize" disabled={!Object.keys(words.formats).includes(source.format) || !api.actions?.clientBlobText} onClick={() => void run(materialize)}>{w("materialize")}</button>
        {source.parser !== "loom.source_parts" && <p>{w("pending")}</p>}
        {source.parser === "loom.source_parts" && !projections.length && <p>{w("projectionWindow")}</p>}
        <label>{w("projection")}<select data-testid="source-projection" value={projectionId} onChange={e => {
          setProjectionId(e.target.value); setNodeId(projections.find(p => p.projection_node === e.target.value)?.occurrence_nodes[0] ?? ""); setOccurrence(null);
        }}>{projections.map(p => <option key={p.projection_node} value={p.projection_node}>{p.projection_node}</option>)}</select></label>
        {projection && <>
          <p data-testid="source-partial" data-reason={projection.reason}>{w("partial")} {projection.occurrence_nodes.length}</p>
          <label>{w("occurrence")}<select data-testid="source-occurrence" value={nodeId} onChange={e => { setNodeId(e.target.value); setOccurrence(null); }}>
            {projection.occurrence_nodes.map((id, i) => <option key={id} value={id}>{i + 1} · {id}</option>)}
          </select></label>
          <button type="button" data-testid="source-read" disabled={!nodeId || !api.actions?.clientBlobTextRead} onClick={() => void run(read)}>{w("read")}</button>
          {onGraph && <button type="button" data-testid="source-show-graph" onClick={() => onGraph(nodeId || projection.source_node)}>{w("graph")}</button>}
        </>}
        {occurrence && <div data-testid="source-read-result" data-node-id={occurrence.node_id}>
          <p>{w("range")}: [{occurrence.provenance.byte_range.join(", ")})</p>
          <pre data-testid="source-text" style={{ whiteSpace: "pre-wrap", maxHeight: 240, overflow: "auto" }}>{occurrence.text}</pre>
          <label>{w("hops")}<input data-testid="source-hops" type="number" min="0" value={hops} onChange={e => setHops(e.target.value)} /></label>
          <button type="button" data-testid="source-select" disabled={!selector || !validHops || !api.mergeConfigKey} onClick={() => void run(() => select(occurrence.node_id, true))}>{w("add")}</button>
        </div>}
      </div>}
      {selector && <div data-testid="source-selector" data-enabled={selector.enabled}>
        <p>{w(selector.enabled ? "enabled" : "disabled")}</p><p>{w("selected")}</p>
        {selector.ids.map((id, i) => <div key={id + ":" + i} data-testid="source-selected-id" data-node-id={id}>{id} <button type="button" onClick={() => void run(() => select(id, false))}>{w("remove")}</button></div>)}
      </div>}
      {graphEnabled === false && <p role="status">{w("graphOff")} {onEnableGraph && <button type="button" onClick={onEnableGraph}>{w("enableGraph")}</button>}</p>}
    </fieldset>
    {busy && <p role="status">{w("busy")}</p>}{notice && <p role="status" data-testid="source-notice">{notice}</p>}
    {error && <p role="alert" data-testid="source-error">{w("error")} {error}</p>}
  </div>;
}
