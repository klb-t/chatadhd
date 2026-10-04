import { useMemo, useState } from "react";
import { marked } from "marked";
import DOMPurify from "dompurify";
import type { Message } from "../api/types";
import { importedJson, projectImportedMessage } from "../content/imported-message";
import "./imported-message-content.css";

const labels: Record<string, string> = {
  text: "Source text", reasoning: "Exported reasoning", tool_call: "Tool call", tool_result: "Tool result",
  code: "Code", media: "Media reference", document: "Document", transcript: "Transcript", context: "Exported context",
  quote: "Quote", browse: "Browsing result", error: "Source error", unknown: "Unknown source block",
};

/** Imported Markdown may contain media links; source inspection never loads them. */
function importedMarkdown(text: string): { __html: string } {
  const markdown = marked.parse(text, { async: false, breaks: true }) as string;
  return { __html: DOMPurify.sanitize(markdown, {
    USE_PROFILES: { html: true },
    FORBID_TAGS: ["style", "img", "audio", "video", "source", "track", "iframe", "object", "embed", "form", "input", "button", "textarea", "select"],
    FORBID_ATTR: ["style", "src", "srcset", "poster", "background", "ping"],
    ADD_ATTR: ["target", "rel"],
  }) };
}

function SourceJson({ title, value, testId }: { title: string; value: unknown; testId?: string }) {
  const [open, setOpen] = useState(false);
  return <details className="imported-source-json" onToggle={event => setOpen(event.currentTarget.open)}>
    <summary>{title}</summary>
    {open && <pre data-testid={testId}>{importedJson(value)}</pre>}
  </details>;
}

interface Props { message: Message; includeVisibleText?: boolean }

/** Pure source display: text/JSON and user-clicked citations; no tools, code or media fetches. */
export default function ImportedMessageContent({ message, includeVisibleText = true }: Props) {
  const source = useMemo(() => projectImportedMessage(message), [message]);
  if (!source) return null;
  return <div className="imported-message-content" data-testid="imported-message-content">
    {includeVisibleText && <div className="body" dangerouslySetInnerHTML={importedMarkdown(message.text)} />}
    <details className="imported-source-details" data-testid="imported-source-details">
      <summary>Imported source · {source.provider} · {source.blocks.length} blocks · {source.references.length} references</summary>
      <p>Source blocks reflect the original import and remain separate from current message text.</p>
      {source.hidden && <p>Source visibility: hidden{source.hiddenReasons.length ? ` (${source.hiddenReasons.join(", ")})` : ""}.</p>}
      {source.blocks.map(block => <section className="imported-source-block" key={block.id} data-testid="imported-source-block" data-kind={block.kind}>
        <h4>{labels[block.kind] ?? block.kind} · {block.type}{block.toolName && ` · ${block.toolName}`}{block.language && ` · ${block.language}`}</h4>
        {block.path && <p className="imported-source-path">Source pointer: <code>{block.path}</code></p>}
        {block.inferred && <p>Importer classification is inferred{block.guess && ` (${block.guess})`}.</p>}
        {!block.resolved && <p>The block payload is not independently located. The complete preserved source is available below.</p>}
        {block.segments.map((segment, index) => <div key={index}>
          <strong>{segment.label}</strong><pre className="imported-source-text">{segment.text}</pre>
        </div>)}
        <SourceJson title="Raw source block" value={block.raw} />
        <SourceJson title="Native importer block summary" value={block.summary} />
      </section>)}
      {source.references.map(reference => <section className="imported-source-reference" key={reference.id} data-testid="imported-source-reference" data-kind={reference.kind}>
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
