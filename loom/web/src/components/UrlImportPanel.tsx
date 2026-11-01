import { useEffect, useRef, useState } from "react";
import { api } from "../api";
import { dataLabel, useShellLanguage } from "../shell/text";
import words from "./url-import-words.json";
import { urlImportFile, type UrlImportMode } from "./url-import-request.mjs";

type Result = Record<string, unknown>;
export default function UrlImportPanel({ onImported, disabled = false }: { onImported: () => void; disabled?: boolean }) {
  const language = useShellLanguage();
  const w = (key: keyof typeof words) => dataLabel(words[key], language, words[key].en);
  const [url, setUrl] = useState("");
  const [mode, setMode] = useState<UrlImportMode>("page");
  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState<Result | null>(null);
  const [error, setError] = useState("");
  const stop = useRef<(() => void) | null>(null);
  useEffect(() => () => stop.current?.(), []);
  const start = () => {
    const file = urlImportFile(url, mode);
    if (!file) { setError(w("invalid")); return; }
    setBusy(true); setError(""); setResult(null);
    stop.current = api.importFile(file, undefined, {
      onChunk: chunk => {
        if (chunk.type === "done") {
          setResult(chunk as Result); setBusy(false);
          if (Number((chunk as Result).messages) > 0) onImported();
        } else if (chunk.type === "error") { setError(chunk.message); setBusy(false); }
      },
      onError: message => { setError(message); setBusy(false); },
      onDone: () => setBusy(false),
    });
  };
  return <div data-testid="url-import-panel">
    <div className="form-row">
      <label htmlFor="import-url">{w("url")}</label>
      <input id="import-url" type="url" value={url} onChange={e => setUrl(e.target.value)} disabled={disabled || busy} />
    </div>
    <div className="form-row">
      <label htmlFor="import-url-mode">{w("mode")}</label>
      <select id="import-url-mode" value={mode} onChange={e => setMode(e.target.value as UrlImportMode)} disabled={disabled || busy}>
        <option value="page">{w("page")}</option><option value="share">{w("share")}</option>
      </select>
    </div>
    <p>{w("hint")}</p>
    <button onClick={start} disabled={disabled || busy || !url} data-testid="url-import-start">{w(busy ? "busy" : "start")}</button>
    {error && <p role="alert">{error}</p>}
    {result && <UrlImportResult result={result} />}
  </div>;
}

export function UrlImportResult({ result }: { result: Result }) {
  const language = useShellLanguage();
  const w = (key: keyof typeof words) => dataLabel(words[key], language, words[key].en);
  const status = result?.status;
  const statusKey = status === "complete" || status === "partial" || status === "denied" ? status : "unsupported";
  const reason = typeof result?.reason === "string" && Object.prototype.hasOwnProperty.call(words, result.reason) ? result.reason as keyof typeof words : null;
  const attachments = Array.isArray(result?.attachments) ? result.attachments as Result[] : [];
  return <div role="status" data-testid="url-import-result" data-status={statusKey}>
      <p>{w(statusKey)}</p>{reason && <p>{w(reason)}</p>}
      {statusKey !== "complete" && <p>{w("file_fallback")}</p>}
      {Boolean(result.branch) && <p>{w("branch_saved")}</p>}
      <p>{w("messages")}: {Number(result.messages) || 0}</p>
      <p>{w("source")}: {String(result.source_url ?? "")}</p>
      <p>{w("final")}: {String(result.final_url ?? "")}</p>
      {attachments.length > 0 && <ul aria-label={w("attachments")}>{attachments.map((a, index) => {
        const key = a.status === "complete" || a.status === "denied" || a.status === "missing" || a.status === "partial" || a.status === "reference" ? a.status : "unsupported";
        return <li key={index}>{index + 1}: {w(key)}</li>;
      })}</ul>}
    </div>;
}
