import { Fragment, useCallback, useRef, useState } from "react";
import { api } from "../api";
import type { ImportChunk } from "../api/types";
import { dataLabel, useShellLanguage, useShellText } from "../shell/text";
import Disclosure from "../shell/Disclosure";
import { absentLine, importView } from "./import-truth-state";
import "./import-truth.css";

const WORDS = {
  pickFile: { en: "Choose a file…", pl: "Wybierz plik…" },
  pickFolder: { en: "Choose a folder…", pl: "Wybierz folder…" },
  pickHint: {
    en: "ZIP or JSON exports of Claude.ai and ChatGPT, JSONL, HTML, MHT, SQLite, Markdown, text, screenshots. A folder imports every file it can read.",
    pl: "Eksporty ZIP lub JSON z Claude.ai i ChatGPT, JSONL, HTML, MHT, SQLite, Markdown, tekst, zrzuty ekranu. Folder importuje każdy plik, który da się odczytać.",
  },
  title: { en: "Title override (optional)", pl: "Własny tytuł (opcjonalnie)" },
  importing: { en: "Importing…", pl: "Importuję…" },
  fileLabel: { en: "File (ZIP, JSON, JSONL, HTML, MHT, SQLite, Markdown, text, screenshot)", pl: "Plik (ZIP, JSON, JSONL, HTML, MHT, SQLite, Markdown, tekst, zrzut ekranu)" },
  start: { en: "Import", pl: "Importuj" },
} as const;

interface Props {
  onImported: () => void;
}

// Import as graph nodes (P1): the kernel's summary sentence (runtime/import_graph.pack templates), in the reader's language when the
// kernel carries it, else in its preset language. Words only: the kernel never puts raw JSON in it.
function graphSummary(result: Record<string, unknown>, language: string): string | null {
  const graph = result.import_graph as { summary?: { text?: unknown; texts?: Record<string, unknown> } } | undefined;
  const summary = graph?.summary;
  if (!summary) return null;
  const mine = summary.texts?.[language];
  if (typeof mine === "string" && mine) return mine;
  return typeof summary.text === "string" && summary.text ? summary.text : null;
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
  const t = useShellText();
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
          {w("fileLabel")}
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
        {busy ? w("importing") : w("start")}
      </button>
      )}

      {busy && progress && (
        <div style={{ marginTop: 12 }} data-testid="import-progress" data-status={progress.status}>
          <div className="empty-state" style={{ textAlign: "left", padding: 0 }}>
            {w("importing")} {pct !== null ? `(${pct}%)` : `(${progress.current}/${progress.total || "?"})`}
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

      {result && (() => {
        const view = importView(result, language, t);
        const absent = absentLine(view, t);
        return (
          <div style={{ marginTop: 12 }} data-testid="import-result" data-truth={view.hasTruth}>
            <div className={`pill import-summary ${view.allSaved ? "ok" : "warn"}`} data-testid="import-summary" role="status">{view.summary ?? view.legacyLine}</div>
            {graphSummary(result, language) && <p className="import-graph-summary" data-testid="import-graph-summary">{graphSummary(result, language)}</p>}
            {view.rows.length > 0 && (
              <table className="import-truth" data-testid="import-truth-table" aria-label={t("import_table")}>
                <thead><tr><th scope="col">{t("import_col_kind")}</th><th scope="col">{t("import_col_found")}</th><th scope="col">{t("import_col_saved")}</th></tr></thead>
                <tbody>
                  {view.rows.map(row => (
                    <Fragment key={row.kind}>
                      <tr data-testid={`import-row-${row.kind}`} data-status={row.status} data-found={row.found ?? "null"} data-saved={row.saved}>
                        <th scope="row">{row.label}</th>
                        <td>{row.foundText}</td>
                        <td>{row.savedText}<span className="import-status">{row.statusText}</span></td>
                      </tr>
                      {(row.reason || row.notes.length > 0) && (
                        <tr className="import-reason" data-testid={`import-row-reason-${row.kind}`}>
                          <td colSpan={3}>{row.reason}{row.notes.map((note, i) => <span key={i} className="import-note">{note}</span>)}</td>
                        </tr>
                      )}
                    </Fragment>
                  ))}
                </tbody>
              </table>
            )}
            {absent && <p className="import-absent" data-testid="import-absent">{absent}</p>}
            <Disclosure testId="import-details" className="import-details" labels={{ show: t("import_details_show"), hide: t("import_details_hide") }}>
              <pre data-testid="import-raw" className="import-raw" data-audit-skip>{JSON.stringify(result, null, 2)}</pre>
            </Disclosure>
          </div>
        );
      })()}
    </div>
  );
}
