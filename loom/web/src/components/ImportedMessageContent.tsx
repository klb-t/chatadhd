import { useMemo, useState } from "react";
import { marked } from "marked";
import DOMPurify from "dompurify";
import type { Message } from "../api/types";
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

function SourceJson({ title, value, testId }: { title: string; value: unknown; testId?: string }) {
  const [open, setOpen] = useState(false);
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
interface Props { message: Message; includeVisibleText?: boolean; initialView?: Partial<ImportedSourceView> }

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
  const [mode, setMode] = useState("source");
  const isHtml = artifact.type === "text/html";
  const isMarkdown = artifact.type === "text/markdown" || artifact.type === "document";
  const supportsPreview = isHtml || isMarkdown;
  return <section className="imported-artifact" data-testid="imported-artifact">
    <h4>{artifact.title} · {artifact.type ?? "unrecorded artifact type"}</h4>
    {artifact.path && <p className="imported-source-path">Source pointer: <code>{artifact.path}</code></p>}
    {artifact.version && <p>Source version: <code>{artifact.version}</code></p>}
    {artifact.command && <p>Recorded operation: {artifact.command} (source only).</p>}
    {artifact.parsedFromText && <p>Display derivative parsed from the retained source text.</p>}
    {!artifact.resolved && <p>The declared artifact payload is not located in retained source.</p>}
    <label>Artifact view <select value={mode} onChange={event => setMode(event.target.value)}>
      <option value="source">Source</option><option value="json">Artifact fields</option>
      {supportsPreview && <option value="preview">Document preview</option>}
    </select></label>
    {mode === "json" && <pre>{importedJson(artifact.raw)}</pre>}
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
export default function ImportedMessageContent({ message, includeVisibleText = true, initialView }: Props) {
  const source = useMemo(() => projectImportedMessage(message), [message]);
  const [view, setView] = useState<ImportedSourceView>({ blocks: true, artifacts: true, references: true, wrapCode: false, ...initialView });
  if (!source) return null;
  return <div className="imported-message-content" data-testid="imported-message-content">
    {includeVisibleText && <div className="body" dangerouslySetInnerHTML={importedMarkdown(message.text)} />}
    <details className="imported-source-details" data-testid="imported-source-details">
      <summary>Imported source · {source.provider} · {source.blocks.length} blocks · {source.references.length} references · {source.artifacts.length} artifacts</summary>
      <p>Source blocks reflect the original import and remain separate from current message text.</p>
      <div className="imported-source-controls" aria-label="Source display options">
        {(["blocks", "artifacts", "references", "wrapCode"] as const).map(key => <label key={key}><input type="checkbox" checked={view[key]} onChange={event => setView(current => ({ ...current, [key]: event.target.checked }))} />{key === "wrapCode" ? "Wrap code" : `Show ${key}`}</label>)}
      </div>
      {source.hidden && <p>Source visibility: hidden{source.hiddenReasons.length ? ` (${source.hiddenReasons.join(", ")})` : ""}.</p>}
      {view.blocks && source.blocks.map(block => <section className="imported-source-block" key={block.id} data-testid="imported-source-block" data-kind={block.kind}>
        <h4>{labels[block.kind] ?? block.kind} · {block.type}{block.toolName && ` · ${block.toolName}`}{block.language && ` · ${block.language}`}</h4>
        {block.path && <p className="imported-source-path">Source pointer: <code>{block.path}</code></p>}
        {block.inferred && <p>Importer classification is inferred{block.guess && ` (${block.guess})`}.</p>}
        {!block.resolved && <p>The block payload is not independently located. The complete preserved source is available below.</p>}
        {block.segments.map((segment, index) => <div key={index}>
          <strong>{segment.label}</strong>{block.kind === "code" ? <pre className={`imported-source-code${view.wrapCode ? " wrap" : ""}`}><code data-language={block.language ?? ""}>{segment.text}</code></pre> : <pre className="imported-source-text">{segment.text}</pre>}
        </div>)}
        {block.kind === "media" && <MediaBlock block={block} />}
        <SourceJson title="Raw source block" value={block.raw} />
        <SourceJson title="Native importer block summary" value={block.summary} />
      </section>)}
      {view.artifacts && source.artifacts.map(artifact => <Artifact key={artifact.id} artifact={artifact} wrapCode={view.wrapCode} />)}
      {view.references && source.references.map(reference => <section className="imported-source-reference" key={reference.id} data-testid="imported-source-reference" data-kind={reference.kind}>
        <h4>{reference.kind}: {reference.label}</h4>
        {reference.path && <p className="imported-source-path">Source pointer: <code>{reference.path}</code></p>}
        {reference.resolved !== null && <p>{reference.resolved ? "Resolved in source archive" : "File bytes unresolved"}{reference.member && ` · ${reference.member}`}</p>}
        {reference.blobHash && <p className="imported-source-path">Content hash: <code>{reference.blobHash}</code></p>}
        {reference.href && <a href={reference.href} target="_blank" rel="noopener noreferrer">Open citation source</a>}
        {reference.extractedText !== null && <pre className="imported-source-text">{reference.extractedText}</pre>}
        <SourceJson title="Raw reference" value={reference.raw} />
        <SourceJson title="Native importer reference summary" value={reference.summary} />
      </section>)}
      <SourceJson title="Complete preserved source message" value={source.raw} testId="imported-source-raw" />
      <SourceJson title="Complete native export metadata" value={source.metadata} testId="imported-source-metadata" />
    </details>
  </div>;
}
