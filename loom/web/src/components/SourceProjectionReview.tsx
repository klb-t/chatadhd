import { useEffect, useRef, useState } from "react";
import { api } from "../api";
import { checkProjection, reprojectProjection, type CheckedProjection, type ProjectionUpgrade } from "../api/projection-review";
import type { SourceParts } from "../api/text-sources";
import words from "../content/projection-review.words.json";
import { dataLabel, useShellLanguage } from "../shell/text";

interface Props {
  projection: SourceParts;
  run: (operation: (isCurrent: () => boolean) => Promise<void>) => Promise<void>;
  refresh: (isCurrent: () => boolean, result: ProjectionUpgrade) => Promise<void>;
}

/** An explicit review of an existing receipt. Mounting this component performs no operation. */
export default function SourceProjectionReview({ projection, run, refresh }: Props) {
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
    if (request.current || lifetime.current?.signal.aborted || (upgrade && (!checked || !confirmed || result))) return;
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
  if (projection.method.format === "zip" || projection.coordinate === "archive_members")
    return <section data-testid="projection-review"><p role="status">{w("zipUnsupported")}</p></section>;
  const receipt = result ?? checked?.receipt;
  return <section data-testid="projection-review" aria-label={w("title")}>
    <h4>{w("title")}</h4>
    <p>{w("about")}</p>
    {!api.actions?.clientBlobText && <p role="status">{w("unavailable")}</p>}
    <button type="button" data-testid="projection-check" disabled={!api.actions?.clientBlobText} onClick={() => execute(false)}>{w("check")}</button>
    {receipt && <div data-testid="projection-review-receipt">
      <p>{w("oldProjection")}: <code>{receipt.old_projection}</code></p>
      <p>{w("receiptHash")}: <code>{receipt.old_receipt_sha256}</code></p>
      <p data-testid="projection-integrity">{w("integrity")}: {w(receipt.saved_integrity)}</p>
      <p data-testid="projection-replay" data-status={receipt.current_replay}>{w("replay")}: {w(receipt.current_replay)}</p>
      <ul>{receipt.refusals.map(f => <li key={f.node_id}>{f.node_id}: {f.code}</li>)}</ul>
      <p>{w("unchanged")}</p>
    </div>}
    {checked && !result && <div data-testid="projection-confirmation">
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
