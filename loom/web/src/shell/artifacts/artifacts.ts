// W8 (OKNA §7.1, UI08): blocks of an assistant's answer as artifacts - pure functions, data in shell/data/artifacts.json.
// splitArtifacts cuts a text into Markdown segments and fenced blocks (``` or ~~~, CommonMark rule: the closing fence uses the same
// character and is at least as long; an unclosed fence runs to the end, as CommonMark reads it). A block gets a format from its fence word,
// else from sniffing, else the catch-all format ("*"). Nothing here touches the DOM.
import artifactsFile from "../data/artifacts.json";

export interface ArtifactFormat { id: string; fence: string[]; sniff?: string; preview?: string; ext: string; mime: string }
export interface ArtifactsData {
  formats: ArtifactFormat[]; ext_by_lang?: Record<string, string>; min_lines?: number; placement?: string; placements?: Record<string, Record<string, string>>; html_scripts?: boolean; file_stem?: string; which_words?: Record<string, number>;
}
export const ARTIFACTS: ArtifactsData = artifactsFile as ArtifactsData;
/** One block of an answer opened as a window: what the window shows (kept in the window's params, so it travels with the layout). */
export interface Artifact { format: string; lang: string; content: string; message?: string; index: number }
export type Segment = { kind: "text"; text: string } | { kind: "artifact"; artifact: Artifact };

const FENCE = /^( {0,3})(`{3,}|~{3,})(.*)$/;

export function formatOf(lang: string, content: string, data: ArtifactsData = ARTIFACTS): ArtifactFormat {
  const word = lang.trim().toLowerCase();
  if (word) { const f = data.formats.find((x) => x.fence.includes(word)); if (f) return f; }
  else for (const f of data.formats) {
    if (!f.sniff) continue;
    try { if (new RegExp(f.sniff, "i").test(content)) return f; } catch { /* a broken pattern in data sniffs nothing */ }
  }
  return data.formats.find((x) => x.fence.includes("*")) ?? { id: "code", fence: ["*"], ext: "txt", mime: "text/plain" };
}

export function splitArtifacts(text: string, message?: string, data: ArtifactsData = ARTIFACTS): Segment[] {
  const lines = text.split("\n");
  const out: Segment[] = [];
  let buf: string[] = [], index = 0;
  const flush = () => { if (buf.length) { const t = buf.join("\n"); if (t.trim()) out.push({ kind: "text", text: t }); buf = []; } };
  const min = Math.max(0, Number.isFinite(data.min_lines) ? Number(data.min_lines) : 1);
  for (let i = 0; i < lines.length; i++) {
    const m = FENCE.exec(lines[i]);
    if (!m || (m[2][0] === "`" && m[3].includes("`"))) { buf.push(lines[i]); continue; }
    const ch = m[2][0], len = m[2].length, lang = m[3].trim().split(/\s+/)[0] ?? "";
    let j = i + 1;
    const body: string[] = [];
    for (; j < lines.length; j++) {
      const c = /^ {0,3}(`{3,}|~{3,})\s*$/.exec(lines[j]);
      if (c && c[1][0] === ch && c[1].length >= len) break;
      body.push(lines[j]);
    }
    if (body.length < min) { buf.push(...lines.slice(i, Math.min(j + 1, lines.length))); i = j; continue; }
    flush();
    const content = body.join("\n");
    out.push({ kind: "artifact", artifact: { format: formatOf(lang, content, data).id, lang, content, message, index: index++ } });
    i = j;
  }
  flush();
  return out;
}

export const formatById = (id: string, data: ArtifactsData = ARTIFACTS): ArtifactFormat | undefined => data.formats.find((f) => f.id === id);

/** The file name „Zapisz” proposes: <stem>-<n>.<ext> (ext from the fence word, else the format's). */
export function fileNameOf(a: Artifact, data: ArtifactsData = ARTIFACTS): string {
  const ext = data.ext_by_lang?.[a.lang.toLowerCase()] ?? formatById(a.format, data)?.ext ?? "txt";
  return `${data.file_stem || "artifact"}-${a.index + 1}.${ext}`;
}

/** The sandbox of an HTML/SVG preview: never allow-same-origin (the page cannot reach the app, its storage or its API). */
export function sandboxOf(scripts: boolean): string { return scripts ? "allow-scripts" : ""; }

/** artifact_html_scripts (settings field) over the preset of artifacts.json; true / "true" / 1 = on. */
export function scriptsOn(config: Record<string, unknown> | null | undefined, data: ArtifactsData = ARTIFACTS): boolean {
  const v = config?.artifact_html_scripts;
  if (v === undefined || v === null || v === "") return data.html_scripts === true;
  return v === true || v === "true" || v === 1 || v === "1";
}

/** A window step's validation: a value read back from a layout (localStorage, a journal) is an artifact only when it has these fields. */
export function artifactOf(v: unknown): Artifact | null {
  const a = v as Artifact | null;
  return a && typeof a === "object" && typeof a.content === "string" && typeof a.format === "string" ? { format: a.format, lang: typeof a.lang === "string" ? a.lang : "", content: a.content, message: typeof a.message === "string" ? a.message : undefined, index: Number.isFinite(a.index) ? a.index : 0 } : null;
}

/** The event ChatView sends to its shell for „Otwórz w oknie” (the shell opens a window of the kind `artifact`). */
export const OPEN_ARTIFACT_EVENT = "loom:open-artifact";

/**
 * W-gaps (artifact.open / copy / save from a sentence): the block a word names among the answers of a conversation. The blocks are those
 * splitArtifacts finds in the assistant messages, in order; the word maps through artifacts.json which_words (>= 0 from the first block,
 * < 0 from the last; empty = -1). `index` is -1 / null when the word is unknown / the block does not exist.
 */
export function pickArtifact(messages: readonly { id?: string; role?: string; text?: string }[], word: string, data: ArtifactsData = ARTIFACTS): { artifact: Artifact | null; count: number; known: boolean } {
  const all: Artifact[] = [];
  for (const m of messages) if (m.role === "assistant" && typeof m.text === "string")
    for (const s of splitArtifacts(m.text, m.id, data)) if (s.kind === "artifact") all.push(s.artifact);
  const w = word.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase().trim();
  const map = data.which_words ?? {};
  const n = w === "" ? -1 : (Object.prototype.hasOwnProperty.call(map, w) ? map[w] : (/^-?\d+$/.test(w) ? Number(w) : NaN));
  if (!Number.isFinite(n)) return { artifact: null, count: all.length, known: false };
  const i = n < 0 ? all.length + n : n;
  return { artifact: all[i] ?? null, count: all.length, known: true };
}
