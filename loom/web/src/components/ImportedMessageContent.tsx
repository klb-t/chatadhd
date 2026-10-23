import { createContext, useContext, useMemo, useState } from "react";
import { marked } from "marked";
import DOMPurify from "dompurify";
import type { Message } from "../api/types";
import { useShellText } from "../shell/text";
import { importedCitationUrl, importedJson, importedMediaDataUrl, importedMediaRenderer, projectImportedMessage } from "../content/imported-message";
import type { ImportedArtifact, ImportedBlock } from "../content/imported-message";
import "./imported-message-content.css";

const labels: Record<string, string> = {
  text: "Source text", reasoning: "Exported reasoning", tool_call: "Tool call", tool_result: "Tool result",
  code: "Code", media: "Media reference", document: "Document", transcript: "Transcript", context: "Exported context",
  quote: "Quote", browse: "Browsing result", error: "Source error", unknown: "Unknown source block",
};

/** Imported Markdown may contain media links; source inspection never loads them. */
function importedMarkdown(text: string): { __html: string } {
  const markdown = marked.parse(text, { async: false, breaks: true }) as string;
  return importedHtml(markdown);
}

/** Preview preserves document structure, never styles, scripts or subresource requests. */
function importedHtml(html: string): { __html: string } {
  const fragment = DOMPurify.sanitize(html, {
    USE_PROFILES: { html: true },
    FORBID_TAGS: ["style", "img", "audio", "video", "source", "track", "iframe", "object", "embed", "form", "input", "button", "textarea", "select"],
    FORBID_ATTR: ["style", "src", "srcset", "poster", "background", "ping"],
    RETURN_DOM_FRAGMENT: true,
  });
  fragment.querySelectorAll("a").forEach(link => {
    const href = importedCitationUrl(link.getAttribute("href"));
    if (href) link.setAttribute("href", href); else link.removeAttribute("href");
    link.setAttribute("target", "_blank"); link.setAttribute("rel", "noopener noreferrer");
  });
  const container = document.createElement("div"); container.append(fragment);
  return { __html: container.innerHTML };
}

const OpaqueSourceEvidence = createContext(false);

/** The words of the imported source block come from the pack (presentation.ui_shell src_*, pl + en); a screen with no catalogue keeps the English text written here. */
function useSrc() {
  const t = useShellText();
  return (id: string, fallback: string, params: Record<string, string> = {}): string => {
    const text = t(id, params);
    return text !== id ? text : fallback.replace(/\{\{(\w+)\}\}/g, (_, key: string) => params[key] ?? "");
  };
}

function SourceJson({ title, value, testId }: { title: string; value: unknown; testId?: string }) {
  const [open, setOpen] = useState(false);
  const opaqueEvidence = useContext(OpaqueSourceEvidence);
  if (opaqueEvidence) return null;
  return <details className="imported-source-json" onToggle={event => setOpen(event.currentTarget.open)}>
    <summary>{title}</summary>
    {open && <pre data-testid={testId}>{importedJson(value)}</pre>}
  </details>;
}

export interface ImportedSourceView {
  blocks: boolean;
  artifacts: boolean;
  references: boolean;
  wrapCode: boolean;
}
interface Props {
  message: Message; includeVisibleText?: boolean; initialView?: Partial<ImportedSourceView>;
  /** Source views expose authoritative JSON at their native response boundary.
   * Keep existing stored-message inspection unchanged unless this is supplied. */
  opaqueSourceEvidence?: { description: string };
}

function MediaBlock({ block }: { block: ImportedBlock }) {
  const [preview, setPreview] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const media = block.media;
  if (!media) return null;
  const renderer = importedMediaRenderer(media.mime);
  const src = preview ? importedMediaDataUrl(media) : null;
  const eligible = media.encoding === "base64" && !!media.data && !!renderer;
  return <div className="imported-media" data-testid="imported-media">
    <p>Media format: {media.mime ?? "not recorded"} · encoding: {media.encoding ?? "not recorded"}.</p>
    {eligible && <button type="button" onClick={() => { setPreview(!preview); setError(null); }}>{preview ? "Close retained media preview" : "Preview retained media"}</button>}
    {!eligible && <p>Retained media preview is unavailable for this payload. Original fields remain in raw source.</p>}
    {preview && !src && <p role="alert">Retained media has invalid encoding.</p>}
    {src && renderer === "image" && <img src={src} alt="Retained imported source media" onError={() => setError("The browser could not decode the retained media bytes.")} />}
    {src && renderer === "audio" && <audio src={src} controls preload="none" onError={() => setError("The browser could not decode the retained media bytes.")} />}
    {src && renderer === "video" && <video src={src} controls preload="none" onError={() => setError("The browser could not decode the retained media bytes.")} />}
    {error && <p role="alert">{error}</p>}
    {media.href && <a href={media.href} target="_blank" rel="noopener noreferrer">Open referenced media in a new tab</a>}
  </div>;
}

function Artifact({ artifact, wrapCode }: { artifact: ImportedArtifact; wrapCode: boolean }) {
  const s = useSrc();
  const [mode, setMode] = useState("source");
  const opaqueEvidence = useContext(OpaqueSourceEvidence);
  const isHtml = artifact.type === "text/html";
  const isMarkdown = artifact.type === "text/markdown" || artifact.type === "document";
  const supportsPreview = isHtml || isMarkdown;
  return <section className="imported-artifact" data-testid="imported-artifact">
    <h4>{artifact.title} · {artifact.type ?? "unrecorded artifact type"}</h4>
    {artifact.path && <p className="imported-source-path">{s("src_pointer", "Source pointer:")} <code>{artifact.path}</code></p>}
    {artifact.version && <p>Source version: <code>{artifact.version}</code></p>}
    {artifact.command && <p>Recorded operation: {artifact.command} (source only).</p>}
    {artifact.parsedFromText && <p>Display derivative parsed from the retained source text.</p>}
    {!artifact.resolved && <p>The declared artifact payload is not located in retained source.</p>}
    <label>Artifact view <select value={mode} onChange={event => setMode(event.target.value)}>
      <option value="source">Source</option>{!opaqueEvidence && <option value="json">Artifact fields</option>}
      {supportsPreview && <option value="preview">Document preview</option>}
    </select></label>
    {mode === "json" && !opaqueEvidence && <pre>{importedJson(artifact.raw)}</pre>}
    {mode === "source" && artifact.content !== null && <pre className={`imported-source-code${wrapCode ? " wrap" : ""}`}><code data-language={artifact.language ?? ""}>{artifact.content}</code></pre>}
    {mode === "preview" && artifact.content !== null && <>
      <p className="imported-preview-note">Document projection: scripts, styling and embedded resources are omitted. Source remains available.</p>
      <div className="imported-artifact-preview" data-testid="imported-artifact-preview" dangerouslySetInnerHTML={isHtml ? importedHtml(artifact.content) : importedMarkdown(artifact.content)} />
    </>}
    {artifact.content === null && <p>No standalone text content is recorded.</p>}
    <SourceJson title="Native artifact descriptor" value={artifact.summary} />
  </section>;
}

/** Pure source display: text/JSON and user-clicked citations; no tools, code or media fetches. */
export default function ImportedMessageContent({ message, includeVisibleText = true, initialView, opaqueSourceEvidence }: Props) {
  const s = useSrc();
  const source = useMemo(() => projectImportedMessage(message), [message]);
  const [view, setView] = useState<ImportedSourceView>({ blocks: true, artifacts: true, references: true, wrapCode: false, ...initialView });
  if (!source) return null;
  return <OpaqueSourceEvidence.Provider value={Boolean(opaqueSourceEvidence)}><div className="imported-message-content" data-testid="imported-message-content">
    {opaqueSourceEvidence && <p>{opaqueSourceEvidence.description}</p>}
    {includeVisibleText && <div className="body" dangerouslySetInnerHTML={importedMarkdown(message.text)} />}
    <details className="imported-source-details" data-testid="imported-source-details">
      <summary>{s("src_summary", "Imported source · {{provider}} · {{blocks}} blocks · {{references}} references · {{artifacts}} artifacts", { provider: source.provider, blocks: String(source.blocks.length), references: String(source.references.length), artifacts: String(source.artifacts.length) })}</summary>
      <p>{s("src_intro", "Source blocks reflect the original import and remain separate from current message text.")}</p>
      <div className="imported-source-controls" aria-label={s("src_options", "Source display options")}>
        {(["blocks", "artifacts", "references", "wrapCode"] as const).map(key => <label key={key}><input type="checkbox" checked={view[key]} onChange={event => setView(current => ({ ...current, [key]: event.target.checked }))} />{key === "wrapCode" ? s("src_wrap", "Wrap code") : s(`src_show_${key}`, `Show ${key}`)}</label>)}
      </div>
      {source.hidden && <p>{s("src_hidden", "Source visibility: hidden{{reasons}}.", { reasons: source.hiddenReasons.length ? ` (${source.hiddenReasons.join(", ")})` : "" })}</p>}
      {view.blocks && source.blocks.map(block => <section className="imported-source-block" key={block.id} data-testid="imported-source-block" data-kind={block.kind}>
        <h4>{s(`src_kind_${block.kind}`, labels[block.kind] ?? block.kind)} · {block.type}{block.toolName && ` · ${block.toolName}`}{block.language && ` · ${block.language}`}</h4>
        {block.path && <p className="imported-source-path">{s("src_pointer", "Source pointer:")} <code>{block.path}</code></p>}
        {block.inferred && <p>{s("src_inferred", "Importer classification is inferred{{guess}}.", { guess: block.guess ? ` (${block.guess})` : "" })}</p>}
        {!block.resolved && <p>{s("src_unlocated", "The block payload is not independently located. The complete preserved source is available below.")}</p>}
        {block.segments.map((segment, index) => <div key={index}>
          <strong>{segment.label}</strong>{block.kind === "code" ? <pre className={`imported-source-code${view.wrapCode ? " wrap" : ""}`}><code data-language={block.language ?? ""}>{segment.text}</code></pre> : <pre className="imported-source-text">{segment.text}</pre>}
        </div>)}
        {block.kind === "media" && <MediaBlock block={block} />}
        <SourceJson title={s("src_raw_block", "Raw source block")} value={block.raw} />
        <SourceJson title={s("src_native_block", "Native importer block summary")} value={block.summary} />
      </section>)}
      {view.artifacts && source.artifacts.map(artifact => <Artifact key={artifact.id} artifact={artifact} wrapCode={view.wrapCode} />)}
      {view.references && source.references.map(reference => <section className="imported-source-reference" key={reference.id} data-testid="imported-source-reference" data-kind={reference.kind}>
        <h4>{s(`src_refkind_${reference.kind.replace(/-/g, "_")}`, reference.kind)}: {reference.label}</h4>
        {reference.path && <p className="imported-source-path">{s("src_pointer", "Source pointer:")} <code>{reference.path}</code></p>}
        {reference.resolved !== null && <p>{reference.resolved ? s("src_resolved", "Resolved in source archive") : s("src_unresolved", "File bytes unresolved")}{reference.member && ` · ${reference.member}`}</p>}
        {reference.blobHash && <p className="imported-source-path">{s("src_hash", "Content hash:")} <code>{reference.blobHash}</code></p>}
        {reference.href && <a href={reference.href} target="_blank" rel="noopener noreferrer">{s("src_open_citation", "Open citation source")}</a>}
        {reference.extractedText !== null && <pre className="imported-source-text">{reference.extractedText}</pre>}
        <SourceJson title={s("src_raw_ref", "Raw reference")} value={reference.raw} />
        <SourceJson title={s("src_native_ref", "Native importer reference summary")} value={reference.summary} />
      </section>)}
      <SourceJson title={s("src_whole", "Complete preserved source message")} value={source.raw} testId="imported-source-raw" />
      <SourceJson title={s("src_meta", "Complete native export metadata")} value={source.metadata} testId="imported-source-metadata" />
    </details>
  </div></OpaqueSourceEvidence.Provider>;
}
