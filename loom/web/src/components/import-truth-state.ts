// M1-G1b (1): what the import page says after an import. The kernel answers loom_import_file_ex with an additive `truth` (G1a, loom/src/import/
// import_truth.cpp): per kind of element how many were found, how many were saved, a status (all | some | none | not_counted | empty), the reason when not all of
// it was saved, notes, and one summary sentence in every language ({pl, en}). The kinds, the reasons and the sentence are the kernel's data; this file only
// turns that answer into rows for the screen with the screen's own words (presentation.ui_shell import_*). A kind it has never heard of is a row like any
// other (its label comes with the answer), a status it has not heard of is shown as it is, and nothing the kernel counted is left out: the kinds with nothing
// found are named in one line. An older kernel answers without `truth`: the old line stays, but in the language of the screen.
import type { ShellT } from "../shell/text";

type Words = Record<string, unknown> | null | undefined;
export interface TruthRow {
  kind: string; label: string; status: string; statusText: string;
  /** null = the reader does not read this kind at all (not a made-up zero). */
  found: number | null; saved: number; foundText: string; savedText: string;
  reason: string | null; notes: string[];
}
export interface ImportView {
  hasTruth: boolean;
  /** The kernel's sentence in the language of the screen; null without `truth`. */
  summary: string | null;
  /** The old one-line result, used when the kernel gave no `truth`. */
  legacyLine: string | null;
  rows: TruthRow[];
  /** Labels of the kinds the archive had nothing of (the kernel's `shown: false`): named in one line, not hidden. */
  absent: string[];
  /** Whether every row says all saved (or empty): the colour of the summary. */
  allSaved: boolean;
}

/** A text of the answer that comes as {pl, en}: the screen's language, else English, else any text it has. */
export function pickText(value: unknown, language: string): string {
  if (typeof value === "string") return value;
  if (!value || typeof value !== "object") return "";
  const table = value as Record<string, unknown>;
  for (const key of [language, "en"]) if (typeof table[key] === "string") return table[key] as string;
  const any = Object.values(table).find(v => typeof v === "string");
  return typeof any === "string" ? any : "";
}

const num = (v: unknown): number | null => typeof v === "number" && Number.isFinite(v) ? v : null;
/** The word of an id; undefined when the screen has no catalogue (the translator then returns the id). */
const word = (t: ShellT, id: string, params?: Record<string, string>): string | undefined => { const text = t(id, params); return text === id ? undefined : text; };

export function importView(result: Words, language: string, t: ShellT): ImportView {
  const truth = result && typeof result.truth === "object" && result.truth !== null ? result.truth as Record<string, unknown> : null;
  const elements = truth && Array.isArray(truth.elements) ? truth.elements as Array<Record<string, unknown>> : null;
  if (!truth || !elements) {
    const n = String(num(result?.messages) ?? 0);
    return { hasTruth: false, summary: null, legacyLine: word(t, "import_summary_legacy", { n }) ?? `Imported ${n} messages`, rows: [], absent: [], allSaved: true };
  }
  const rows: TruthRow[] = [], absent: string[] = [];
  for (const e of elements) {
    const kind = typeof e.kind === "string" ? e.kind : "";
    const label = pickText(e.label, language) || kind;
    if (e.shown === false) { absent.push(label); continue; }
    const status = typeof e.status === "string" ? e.status : "";
    const found = num(e.found), saved = num(e.imported) ?? 0;
    const reasonText = e.reason && typeof e.reason === "object" ? pickText((e.reason as Record<string, unknown>).text, language) : "";
    const notes = Array.isArray(e.notes) ? (e.notes as Array<Record<string, unknown>>).map(n => pickText(n.text, language)).filter(Boolean) : [];
    rows.push({
      kind, label, status, statusText: word(t, `import_status_${status}`) ?? status,
      found, saved, foundText: found === null ? (word(t, "import_not_counted") ?? "-") : String(found), savedText: String(saved),
      reason: reasonText ? (word(t, "import_reason", { reason: reasonText }) ?? reasonText) : null, notes,
    });
  }
  const summary = pickText(truth.summary, language);
  return {
    hasTruth: true, summary: summary || null, legacyLine: null, rows, absent,
    allSaved: rows.every(r => r.status === "all" || r.status === "empty"),
  };
}

/** The line "Nie było w archiwum: A, B." — empty when nothing was absent. */
export function absentLine(view: ImportView, t: ShellT): string | null {
  return view.absent.length === 0 ? null : (word(t, "import_absent", { kinds: view.absent.join(", ") }) ?? view.absent.join(", "));
}
