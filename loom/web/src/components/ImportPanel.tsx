import { useCallback, useRef, useState } from "react";
import { api } from "../api";
import type { ImportChunk } from "../api/types";
import { dataLabel, useShellLanguage } from "../shell/text";

const WORDS = {
  pickFile: { en: "Choose a file…", pl: "Wybierz plik…" },
  pickFolder: { en: "Choose a folder…", pl: "Wybierz folder…" },
  pickHint: {
    en: "ZIP or JSON exports of Claude.ai and ChatGPT, JSONL, HTML, MHT, SQLite, Markdown, text, screenshots. A folder imports every file it can read.",
    pl: "Eksporty ZIP lub JSON z Claude.ai i ChatGPT, JSONL, HTML, MHT, SQLite, Markdown, tekst, zrzuty ekranu. Folder importuje każdy plik, który da się odczytać.",
  },
  title: { en: "Title override (optional)", pl: "Własny tytuł (opcjonalnie)" },
  importing: { en: "Importing…", pl: "Importuję…" },
} as const;

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

  const language = useShellLanguage();
  const w = (k: keyof typeof WORDS) => dataLabel(WORDS[k], language, WORDS[k].en);
  const native = typeof api.pickImport === "function";

  const run = useCallback((begin: (h: Parameters<typeof api.importFile>[2]) => () => void) => {
    setBusy(true);
    setProgress(null);
    setResult(null);
    setError(null);
    unsubRef.current = begin({
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
  }, [onImported]);
  const start = useCallback(() => {
    if (file) run((h) => api.importFile(file, title || undefined, h));
  }, [file, title, run]);
  const pick = useCallback((kind: "file" | "folder") => {
    if (api.pickImport) run((h) => api.pickImport!(kind, title || undefined, h));
  }, [title, run]);

  const pct = progress && progress.total > 0 ? Math.round((progress.current / progress.total) * 100) : null;

  return (
    <div data-testid="import-panel">
      {native ? (
      <div className="form-row">
        <div className="empty-state" style={{ textAlign: "left", padding: 0 }}>{w("pickHint")}</div>
        <button className="primary" onClick={() => pick("file")} disabled={busy} data-testid="import-pick-file">
          {busy ? w("importing") : w("pickFile")}
        </button>{" "}
        <button onClick={() => pick("folder")} disabled={busy} data-testid="import-pick-folder">{w("pickFolder")}</button>
      </div>
      ) : (
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
      )}
      <div className="form-row">
        <label htmlFor="import-title">{w("title")}</label>
        <input id="import-title" type="text" value={title} onChange={(e) => setTitle(e.target.value)} />
      </div>
      {!native && (
      <button className="primary" onClick={start} disabled={!file || busy} data-testid="start-import">
        {busy ? "Importing…" : "Import"}
      </button>
      )}

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
