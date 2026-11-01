import { useEffect, useRef, useState } from "react";
import { api } from "../api";
import { checkProjection, reprojectProjection, type CheckedProjection, type ProjectionUpgrade, type ArchivePreview } from "../api/projection-review";
import AttachmentOriginal from "./AttachmentOriginal";
import type { SourceParts } from "../api/text-sources";
import words from "../content/projection-review.words.json";
import { dataLabel, useShellLanguage } from "../shell/text";

interface Props {
  projection: SourceParts;
  run: (operation: (isCurrent: () => boolean) => Promise<void>) => Promise<void>;
  refresh: (isCurrent: () => boolean, result: ProjectionUpgrade) => Promise<void>;
}

type Label = (key: keyof typeof words.labels) => string;

/** Render only the validated native descriptor; parent IDs, never labels, define the tree. */
function ArchiveDescriptor({ preview, w }: { preview: ArchivePreview; w: Label }) {
  const children = new Map(preview.children.map(child => [child.member_node, child]));
  const branches = (parent: string): JSX.Element => <ul>{preview.structure_nodes.filter(row => row.parent === parent).map(row => {
    const child = children.get(row.node_id);
    return <li key={row.node_id} data-testid="archive-entry" data-node-id={row.node_id} data-parent-id={row.parent} data-kind={row.kind}>
      <strong>{w(row.kind)}: {row.title}</strong>
      <p>{w("archiveNode")}: <code>{row.node_id}</code></p>
      <p>{w("archiveLocator")}: <code>{JSON.stringify(row.locator)}</code></p>
      {row.kind === "folder" ? branches(row.node_id) : <>
        <p>{w("archiveOriginal")}: <code>{row.sha256}</code></p>
        <p>{w("archiveSource")}: <code>{row.source_id}</code></p>
        <p>{w("archiveProvenance")}: <code>{row.provenance_id}</code></p>
        <AttachmentOriginal reference={"blob:" + row.sha256} name={row.title} />
        {child?.projection && child.review ? <div data-testid="archive-child-projection" data-projection-id={child.projection.projection_node}>
          <p>{w("archiveChild")}: <code>{child.projection.projection_node}</code> ({String(child.projection.method.format)})</p>
          <p>{w("archiveSource")}: <code>{child.projection.source_id}</code></p>
          <p data-testid="archive-child-replay" data-status={child.review.current_replay}>{w("replay")}: {w(child.review.current_replay)}</p>
          <ul>{child.review.refusals.map(r => <li key={r.node_id}><code>{r.node_id}</code>: {r.code}</li>)}</ul>
          <p>{w("archiveParts")}: {child.projection.occurrence_nodes.length}</p>
          <ul>{child.projection.occurrence_nodes.map(id => <li key={id}><code>{id}</code></li>)}</ul>
          {child.review.archive_preview && <ArchiveDescriptor preview={child.review.archive_preview} w={w} />}
        </div> : <p data-testid="archive-child-unknown" data-status="not_exercised">{w("archiveUnknown")}</p>}
      </>}
    </li>;
  })}</ul>;
  return <div data-testid="archive-preview" data-source-id={preview.root.source_id}>
    <h5>{w("archiveTitle")}</h5>
    <p>{w("archiveScope")}</p>
    <p>{w("archiveSource")}: <code>{preview.root.source_id}</code></p>
    <p>{w("archiveOriginal")}: <code>{preview.root.sha256}</code></p>
    <AttachmentOriginal reference={"blob:" + preview.root.sha256} name={preview.root.source_id + ".zip"} />
    <p>{w("archiveDescriptorHash")}: <code>{preview.descriptor_sha256}</code></p>
    {branches(preview.root.node_id)}
    <p data-testid="archive-mapping-status" data-status={preview.mapping.status}>{w("archiveMapping")}</p>
    <details><summary>{w("archiveLimits")}</summary><ul>{preview.limits.map(limit => <li key={limit}><code>{limit}</code></li>)}</ul></details>
  </div>;
}

/** An explicit review of an existing receipt. Mounting this component performs no operation. */
export default function SourceProjectionReview({ projection, run, refresh }: Props) {
  const archive = projection.method.format === "zip" || projection.coordinate === "archive_members";
  const language = useShellLanguage();
  const w = (key: keyof typeof words.labels) => dataLabel(words.labels[key], language, words.labels[key].en);
  const [checked, setChecked] = useState<CheckedProjection | null>(null);
  const [confirmed, setConfirmed] = useState(false), [result, setResult] = useState<ProjectionUpgrade | null>(null);
  const [error, setError] = useState(""), [refreshError, setRefreshError] = useState("");
  const lifetime = useRef<AbortController | null>(null), request = useRef<AbortController | null>(null);
  useEffect(() => {
    const controller = new AbortController();
    lifetime.current = controller;
    return () => { controller.abort(); request.current?.abort(); };
  }, []);
  const message = (e: unknown) => {
    const code = e instanceof Error ? e.message : String(e);
    const label = words.errors[code as keyof typeof words.errors];
    return label ? dataLabel(label, language, label.en) : w("failed") + " " + code;
  };
  const execute = (upgrade: boolean) => {
    // Synchronous guard covers double clicks before React disables the enclosing fieldset.
    if (request.current || lifetime.current?.signal.aborted || (upgrade && (archive || !checked || !confirmed || result))) return;
    const controller = new AbortController();
    request.current = controller;
    void run(async parentCurrent => {
      const current = () => parentCurrent() && !controller.signal.aborted && !lifetime.current?.signal.aborted;
      setConfirmed(false); setError(""); setRefreshError("");
      try {
        if (upgrade && checked) {
          // Consume confirmation before dispatch; a failed/uncertain write requires a new check.
          setChecked(null);
          const made = await reprojectProjection(api, projection, checked, controller.signal);
          if (!current()) return;
          setResult(made);
          try { await refresh(current, made); }
          catch (e) { if (current()) setRefreshError(message(e)); }
        } else {
          setChecked(null); setResult(null);
          const next = await checkProjection(api, projection, controller.signal);
          if (current()) setChecked(next);
        }
      } catch (e) {
        if (current()) setError(message(e));
      } finally {
        if (request.current === controller) request.current = null;
      }
    }).finally(() => { if (request.current === controller) request.current = null; });
  };
  const receipt = result ?? checked?.receipt;
  return <section data-testid="projection-review" aria-label={w("title")}>
    <h4>{w("title")}</h4>
    <p>{w(archive ? "archiveAbout" : "about")}</p>
    {archive && <p role="status">{w("zipUnsupported")}</p>}
    {!api.actions?.clientBlobText && <p role="status">{w("unavailable")}</p>}
    <button type="button" data-testid="projection-check" disabled={!api.actions?.clientBlobText} onClick={() => execute(false)}>{w("check")}</button>
    {receipt && <div data-testid="projection-review-receipt">
      <p>{w("oldProjection")}: <code>{receipt.old_projection}</code></p>
      <p>{w("receiptHash")}: <code>{receipt.old_receipt_sha256}</code></p>
      <p data-testid="projection-integrity">{w("integrity")}: {w(receipt.saved_integrity)}</p>
      <p data-testid="projection-replay" data-status={receipt.current_replay}>{w("replay")}: {w(receipt.current_replay)}</p>
      <ul>{receipt.refusals.map(f => <li key={f.node_id}>{f.node_id}: {f.code}</li>)}</ul>
      <p>{w("unchanged")}</p>
      {receipt.archive_preview && <ArchiveDescriptor preview={receipt.archive_preview} w={w} />}
    </div>}
    {checked && !result && !archive && <div data-testid="projection-confirmation">
      <p>{w("selection")}: {checked.selectedNodes.length}</p>
      <ul>{checked.selectedNodes.map(n => <li key={n}><code>{n}</code></li>)}</ul>
      <p>{w("consequences")}</p>
      <p>{w("rollback")}</p>
      <label><input type="checkbox" data-testid="projection-confirm" checked={confirmed} onChange={e => setConfirmed(e.target.checked)} />{w("confirm")}</label>
      <button type="button" data-testid="projection-reproject" disabled={!confirmed} onClick={() => execute(true)}>{w("reproject")}</button>
    </div>}
    {result && <div data-testid="projection-upgrade-result">
      <p>{w("completed")}</p>
      <p>{w("newSource")}: <code>{result.new_source}</code></p>
      <p>{w("newProjection")}: <code>{result.new_projection}</code></p>
      <p>{w("newReceipt")}: <code>{result.new_receipt_sha256}</code></p>
      <p>{w("upgradeReceipt")}: <code>{result.upgrade_node}</code></p>
      <p>{w("mappingAbout")}</p>
      {!result.mapping.length && <p>{w("noMapping")}</p>}
      <ul>{result.mapping.map(m => <li key={m.old_node} data-testid="projection-mapping" data-status={m.status}>
        <code>{m.old_node}</code>: {w(m.status)}
        <ul>{m.candidates.map(n => <li key={n}><code>{n}</code></li>)}</ul>
      </li>)}</ul>
      <p>{w("activation")}</p>
    </div>}
    {refreshError && <div role="alert"><p>{w("refreshFailed")} {refreshError}</p>
      <button type="button" onClick={() => void run(async parentCurrent => {
        if (!result) return;
        const current = () => parentCurrent() && !lifetime.current?.signal.aborted;
        try { await refresh(current, result); if (current()) setRefreshError(""); }
        catch (e) { if (current()) setRefreshError(message(e)); }
      })}>{w("refresh")}</button>
    </div>}
    {error && <p role="alert" data-testid="projection-review-error">{error}</p>}
  </section>;
}
