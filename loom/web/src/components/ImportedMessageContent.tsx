import { createContext, useContext, useMemo, useState } from "react";
import { marked } from "marked";
import DOMPurify from "dompurify";
import type { Message } from "../api/types";
import { useShellText } from "../shell/text";
import { importedCitationUrl, importedJson, importedMediaDataUrl, importedMediaRenderer, projectImportedMessage } from "../content/imported-message";
import type { ImportedArtifact, ImportedBlock } from "../content/imported-message";
import "./imported-message-content.css";

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

/** Unknown source kinds retain their recorded kind; translated labels come only from catalogs. */
function sourceLabel(t: ReturnType<typeof useShellText>, id: string, fallback: string): string {
  const label = t(id);
  return label === id ? fallback : label;
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
  const t = useShellText();
  const [preview, setPreview] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const media = block.media;
  if (!media) return null;
  const renderer = importedMediaRenderer(media.mime);
  const src = preview ? importedMediaDataUrl(media) : null;
  const eligible = media.encoding === "base64" && !!media.data && !!renderer;
  return <div className="imported-media" data-testid="imported-media">
    <p>Media format: {media.mime ?? "not recorded"} · encoding: {media.encoding ?? "not recorded"}.</p>
    {eligible && <button type="button" onClick={() => { setPreview(!preview); setError(null); }}>{preview ? t("lit_close_retained_media_preview") : t("lit_preview_retained_media")}</button>}
    {!eligible && <p>{t("lit_retained_media_preview_is_unavailable")}</p>}
    {preview && !src && <p role="alert">Retained media has invalid encoding.</p>}
    {src && renderer === "image" && <img src={src} alt={t("lit_retained_imported_source_media")} onError={() => setError(t("lit_the_browser_could_not_decode_the_retai"))} />}
    {src && renderer === "audio" && <audio src={src} controls preload="none" onError={() => setError(t("lit_the_browser_could_not_decode_the_retai"))} />}
    {src && renderer === "video" && <video src={src} controls preload="none" onError={() => setError(t("lit_the_browser_could_not_decode_the_retai"))} />}
    {error && <p role="alert">{error}</p>}
    {media.href && <a href={media.href} target="_blank" rel="noopener noreferrer">Open referenced media in a new tab</a>}
  </div>;
}

function Artifact({ artifact, wrapCode }: { artifact: ImportedArtifact; wrapCode: boolean }) {
  const t = useShellText();
  const s = useShellText();
  const [mode, setMode] = useState("source");
  const opaqueEvidence = useContext(OpaqueSourceEvidence);
  const isHtml = artifact.type === "text/html";
  const isMarkdown = artifact.type === "text/markdown" || artifact.type === "document";
  const supportsPreview = isHtml || isMarkdown;
  return <section className="imported-artifact" data-testid="imported-artifact">
    <h4>{artifact.title} · {artifact.type ?? "unrecorded artifact type"}</h4>
    {artifact.path && <p className="imported-source-path">{s("src_pointer")} <code>{artifact.path}</code></p>}
    {artifact.version && <p>{t("lit_source_version")} <code>{artifact.version}</code></p>}
    {artifact.command && <p>Recorded operation: {artifact.command} (source only).</p>}
    {artifact.parsedFromText && <p>{t("lit_display_derivative_parsed_from_the_ret")}</p>}
    {!artifact.resolved && <p>{t("lit_the_declared_artifact_payload_is_not_l")}</p>}
    <label>{t("lit_artifact_view")} <select value={mode} onChange={event => setMode(event.target.value)}>
      <option value="source">{t("lit_source")}</option>{!opaqueEvidence && <option value="json">{t("lit_artifact_fields")}</option>}
      {supportsPreview && <option value="preview">{t("lit_document_preview")}</option>}
    </select></label>
    {mode === "json" && !opaqueEvidence && <pre>{importedJson(artifact.raw)}</pre>}
    {mode === "source" && artifact.content !== null && <pre className={`imported-source-code${wrapCode ? " wrap" : ""}`}><code data-language={artifact.language ?? ""}>{artifact.content}</code></pre>}
    {mode === "preview" && artifact.content !== null && <>
      <p className="imported-preview-note">Document projection: scripts, styling and embedded resources are omitted. Source remains available.</p>
      <div className="imported-artifact-preview" data-testid="imported-artifact-preview" dangerouslySetInnerHTML={isHtml ? importedHtml(artifact.content) : importedMarkdown(artifact.content)} />
    </>}
    {artifact.content === null && <p>{t("lit_no_standalone_text_content_is_recorded")}</p>}
    <SourceJson title={t("lit_native_artifact_descriptor")} value={artifact.summary} />
  </section>;
}

/** Pure source display: text/JSON and user-clicked citations; no tools, code or media fetches. */
export default function ImportedMessageContent({ message, includeVisibleText = true, initialView, opaqueSourceEvidence }: Props) {
  const s = useShellText();
  const source = useMemo(() => projectImportedMessage(message), [message]);
  const [view, setView] = useState<ImportedSourceView>({ blocks: true, artifacts: true, references: true, wrapCode: false, ...initialView });
  if (!source) return null;
  return <OpaqueSourceEvidence.Provider value={Boolean(opaqueSourceEvidence)}><div className="imported-message-content" data-testid="imported-message-content">
    {opaqueSourceEvidence && <p>{opaqueSourceEvidence.description}</p>}
    {includeVisibleText && <div className="body" dangerouslySetInnerHTML={importedMarkdown(message.text)} />}
    <details className="imported-source-details" data-testid="imported-source-details">
      <summary>{s("src_summary", { provider: source.provider, blocks: String(source.blocks.length), references: String(source.references.length), artifacts: String(source.artifacts.length) })}</summary>
      <p>{s("src_intro")}</p>
      <div className="imported-source-controls" aria-label={s("src_options")}>
        {(["blocks", "artifacts", "references", "wrapCode"] as const).map(key => <label key={key}><input type="checkbox" checked={view[key]} onChange={event => setView(current => ({ ...current, [key]: event.target.checked }))} />{key === "wrapCode" ? s("src_wrap") : s(`src_show_${key}`)}</label>)}
      </div>
      {source.hidden && <p>{s("src_hidden", { reasons: source.hiddenReasons.length ? ` (${source.hiddenReasons.join(", ")})` : "" })}</p>}
      {view.blocks && source.blocks.map(block => <section className="imported-source-block" key={block.id} data-testid="imported-source-block" data-kind={block.kind}>
        <h4>{sourceLabel(s, `src_kind_${block.kind}`, block.kind)} · {block.type}{block.toolName && ` · ${block.toolName}`}{block.language && ` · ${block.language}`}</h4>
        {block.path && <p className="imported-source-path">{s("src_pointer")} <code>{block.path}</code></p>}
        {block.inferred && <p>{s("src_inferred", { guess: block.guess ? ` (${block.guess})` : "" })}</p>}
        {!block.resolved && <p>{s("src_unlocated")}</p>}
        {block.segments.map((segment, index) => <div key={index}>
          <strong>{segment.label}</strong>{block.kind === "code" ? <pre className={`imported-source-code${view.wrapCode ? " wrap" : ""}`}><code data-language={block.language ?? ""}>{segment.text}</code></pre> : <pre className="imported-source-text">{segment.text}</pre>}
        </div>)}
        {block.kind === "media" && <MediaBlock block={block} />}
        <SourceJson title={s("src_raw_block")} value={block.raw} />
        <SourceJson title={s("src_native_block")} value={block.summary} />
      </section>)}
      {view.artifacts && source.artifacts.map(artifact => <Artifact key={artifact.id} artifact={artifact} wrapCode={view.wrapCode} />)}
      {view.references && source.references.map(reference => <section className="imported-source-reference" key={reference.id} data-testid="imported-source-reference" data-kind={reference.kind}>
        <h4>{sourceLabel(s, `src_refkind_${reference.kind.replace(/-/g, "_")}`, reference.kind)}: {reference.label}</h4>
        {reference.path && <p className="imported-source-path">{s("src_pointer")} <code>{reference.path}</code></p>}
        {reference.resolved !== null && <p>{reference.resolved ? s("src_resolved") : s("src_unresolved")}{reference.member && ` · ${reference.member}`}</p>}
        {reference.blobHash && <p className="imported-source-path">{s("src_hash")} <code>{reference.blobHash}</code></p>}
        {reference.href && <a href={reference.href} target="_blank" rel="noopener noreferrer">{s("src_open_citation")}</a>}
        {reference.extractedText !== null && <pre className="imported-source-text">{reference.extractedText}</pre>}
        <SourceJson title={s("src_raw_ref")} value={reference.raw} />
        <SourceJson title={s("src_native_ref")} value={reference.summary} />
      </section>)}
      <SourceJson title={s("src_whole")} value={source.raw} testId="imported-source-raw" />
      <SourceJson title={s("src_meta")} value={source.metadata} testId="imported-source-metadata" />
    </details>
  </div></OpaqueSourceEvidence.Provider>;
}
