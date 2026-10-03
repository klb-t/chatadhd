import { useCallback, useRef, useState } from "react";
import { api } from "../api";
import type { ImportChunk } from "../api/types";

interface Props {
  onImported: () => void;
}

export default function ImportPanel({ onImported }: Props) {
  const [file, setFile] = useState<File | null>(null);
  const [title, setTitle] = useState("");
  const [progress, setProgress] = useState<{ current: number; total: number; status: string } | null>(null);
  const [result, setResult] = useState<Record<string, unknown> | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const unsubRef = useRef<(() => void) | null>(null);

  const start = useCallback(() => {
    if (!file) return;
    setBusy(true);
    setProgress(null);
    setResult(null);
    setError(null);
    unsubRef.current = api.importFile(file, title || undefined, {
      onChunk: (chunk: ImportChunk) => {
        if (chunk.type === "progress") {
          setProgress({ current: chunk.current, total: chunk.total, status: chunk.status });
        } else if (chunk.type === "error") {
          setError(chunk.message);
          setBusy(false);
        } else if (chunk.type === "done") {
          setResult(chunk as Record<string, unknown>);
          setBusy(false);
          onImported();
        }
      },
      onError: (msg) => {
        setError(msg);
        setBusy(false);
      },
      onDone: () => setBusy(false),
    });
  }, [file, title, onImported]);

  const pct = progress && progress.total > 0 ? Math.round((progress.current / progress.total) * 100) : null;

  return (
    <div data-testid="import-panel">
      <div className="form-row">
        <label htmlFor="import-file">
          File (ZIP, JSON, JSONL, HTML, MHT, SQLite, Markdown, text, screenshot)
        </label>
        <input
          id="import-file"
          type="file"
          data-testid="import-file-input"
          onChange={(e) => setFile(e.target.files?.[0] ?? null)}
        />
      </div>
      <div className="form-row">
        <label htmlFor="import-title">Title override (optional)</label>
        <input id="import-title" type="text" value={title} onChange={(e) => setTitle(e.target.value)} />
      </div>
      <button className="primary" onClick={start} disabled={!file || busy} data-testid="start-import">
        {busy ? "Importing…" : "Import"}
      </button>

      {progress && (
        <div style={{ marginTop: 12 }} data-testid="import-progress">
          <div className="empty-state" style={{ textAlign: "left", padding: 0 }}>
            {progress.status} {pct !== null ? `(${pct}%)` : `(${progress.current}/${progress.total || "?"})`}
          </div>
          <div style={{ height: 6, background: "var(--input-bg)", borderRadius: 4, overflow: "hidden", marginTop: 4 }}>
            <div
              style={{
                height: "100%",
                width: `${pct ?? 0}%`,
                background: "var(--accent)",
                transition: "width 0.2s",
              }}
            />
          </div>
        </div>
      )}

      {error && (
        <div className="empty-state" style={{ color: "var(--err)" }} data-testid="import-error">
          {error}
        </div>
      )}

      {result && (
        <div style={{ marginTop: 12 }} data-testid="import-result">
          <div className="pill ok">Imported {String(result.messages ?? 0)} messages</div>
          <pre
            style={{
              whiteSpace: "pre-wrap",
              fontSize: 11,
              background: "var(--input-bg)",
              padding: 8,
              borderRadius: 8,
              marginTop: 8,
              maxHeight: 200,
              overflowY: "auto",
            }}
          >
            {JSON.stringify(result, null, 2)}
          </pre>
        </div>
      )}
    </div>
  );
}
