import type { Message } from "../api/types";

/** A read-only projection of the native importer's preserved source metadata. */
export interface ImportedSegment { label: string; text: string }
export interface ImportedBlock {
  id: string;
  kind: string;
  type: string;
  path: string | null;
  resolved: boolean;
  inferred: boolean;
  guess: string | null;
  summary: unknown;
  raw: unknown;
  segments: ImportedSegment[];
  toolName: string | null;
  language: string | null;
  media: ImportedMedia | null;
}
export interface ImportedMedia {
  mime: string | null;
  encoding: string | null;
  data: string | null;
  href: string | null;
}
export interface ImportedArtifact {
  id: string;
  title: string;
  type: string | null;
  language: string | null;
  command: string | null;
  version: string | null;
  content: string | null;
  path: string | null;
  resolved: boolean;
  parsedFromText: boolean;
  raw: unknown;
  summary: unknown;
}
export interface ImportedReference {
  id: string;
  kind: "attachment" | "media-pointer" | "citation";
  label: string;
  path: string | null;
  raw: unknown;
  summary: unknown;
  resolved: boolean | null;
  member: string | null;
  blobHash: string | null;
  href: string | null;
  extractedText: string | null;
}
export interface ImportedMessageContent {
  provider: string;
  blocks: ImportedBlock[];
  references: ImportedReference[];
  artifacts: ImportedArtifact[];
  raw: unknown;
  metadata: Record<string, unknown>;
  hidden: boolean;
  hiddenReasons: string[];
}

function record(value: unknown): Record<string, unknown> | null {
  return value !== null && typeof value === "object" && !Array.isArray(value)
    ? value as Record<string, unknown> : null;
}
function text(value: unknown): string | null { return typeof value === "string" ? value : null; }
function entries(value: unknown): unknown[] { return Array.isArray(value) ? value : []; }
function nonnegative(value: unknown): value is number { return Number.isSafeInteger(value) && Number(value) >= 0; }

/** Pointer navigation never reads inherited properties or dereferences a URL/file. */
export function importedPointer(source: unknown, pointer: string): { found: boolean; value?: unknown } {
  if (pointer === "") return { found: true, value: source };
  if (!pointer.startsWith("/")) return { found: false };
  let value = source;
  for (const encoded of pointer.slice(1).split("/")) {
    if (/~(?:[^01]|$)/.test(encoded)) return { found: false };
    const part = encoded.replace(/~1/g, "/").replace(/~0/g, "~");
    if (Array.isArray(value)) {
      if (!/^(?:0|[1-9]\d*)$/.test(part) || !Object.prototype.hasOwnProperty.call(value, part)) return { found: false };
      value = value[Number(part)];
    } else {
      const object = record(value);
      if (!object || !Object.prototype.hasOwnProperty.call(object, part)) return { found: false };
      value = object[part];
    }
  }
  return { found: true, value };
}

export function importedJson(value: unknown): string {
  try { return JSON.stringify(value, null, 2) ?? "No value in preserved source."; }
  catch { return "Source value cannot be represented as JSON."; }
}

/** Only an explicit user click may navigate a citation; media URLs stay plain data. */
export function importedCitationUrl(value: unknown): string | null {
  if (typeof value !== "string") return null;
  try {
    const url = new URL(value);
    return ["http:", "https:"].includes(url.protocol) && !url.username && !url.password ? url.href : null;
  } catch { return null; }
}

function blockPath(provider: string, descriptor: Record<string, unknown>, index: number, raw: unknown): string | null {
  if (provider === "anthropic") {
    if (descriptor.source === "text") return "/text";
    if (nonnegative(descriptor.index)) return `/content/${descriptor.index}`;
    return null;
  }
  if (provider === "openai") {
    if (nonnegative(descriptor.part)) return `/content/parts/${descriptor.part}`;
    const content = record(record(raw)?.content);
    // These types emit descriptors in source-array/key order without an index.
    if (content?.content_type === "thoughts") return `/content/thoughts/${index}`;
    if (content?.content_type === "user_editable_context") {
      const keys = ["user_profile", "user_instructions"].filter(key => typeof content[key] === "string");
      return keys[index] ? `/content/${keys[index]}` : null;
    }
    if (content) return "/content";
  }
  return null;
}

function blockSegments(kind: string, raw: unknown): ImportedSegment[] {
  if (typeof raw === "string") return [{ label: "Source text", text: raw }];
  const object = record(raw);
  if (!object) return [];
  const segments: ImportedSegment[] = [];
  const add = (label: string, value: unknown) => { if (typeof value === "string") segments.push({ label, text: value }); };
  if (["text", "quote", "transcript", "code", "error", "browse", "context", "unknown"].includes(kind)) add("Source text", object.text);
  if (kind === "reasoning") {
    add("Exported thinking", object.thinking);
    add("Exported content", object.content);
    add("Exported summary", object.summary);
    for (const summary of entries(object.summaries)) add("Exported summary", record(summary)?.summary);
  }
  if (kind === "tool_call") {
    if (Object.prototype.hasOwnProperty.call(object, "input")) segments.push({ label: "Tool input", text: importedJson(object.input) });
    add("Tool message", object.message);
  }
  if (kind === "tool_result") {
    add("Tool output", object.text);
    if (Object.prototype.hasOwnProperty.call(object, "content")) segments.push({ label: "Tool result", text: importedJson(object.content) });
    add("Tool message", object.message);
  }
  if (kind === "context") add("Exported model context", object.model_set_context);
  if (kind === "browse") { add("Browsing result", object.result); add("Browsing summary", object.summary); }
  if (kind === "document") {
    const source = record(object.source);
    if (source?.type === "text") add("Document source text", source.data);
  }
  return segments;
}

function block(descriptor: unknown, index: number, provider: string, raw: unknown, forcedPath?: string): ImportedBlock {
  const summary = record(descriptor) ?? {};
  const path = forcedPath ?? blockPath(provider, summary, index, raw);
  const located = path === null ? { found: false, value: undefined } : importedPointer(raw, path);
  const kind = text(summary.kind) ?? "unknown";
  const value = record(located.value);
  return {
    id: `block-${index}`, kind, type: text(summary.type) ?? "unknown", path,
    resolved: located.found, inferred: summary.inferred === true, guess: text(summary.guess),
    summary: descriptor, raw: located.value, segments: blockSegments(kind, located.value),
    toolName: text(value?.name), language: text(value?.language),
    media: kind === "media" ? importedMedia(located.value) : null,
  };
}

/** Metadata only: neither a reference URL nor retained media bytes are loaded here. */
export function importedMedia(raw: unknown): ImportedMedia | null {
  const object = record(raw);
  if (!object) return null;
  const source = record(object.source) ?? object;
  return {
    mime: text(source.media_type) ?? text(source.mime_type),
    encoding: text(source.type), data: text(source.data),
    href: importedCitationUrl(source.url ?? object.url),
  };
}

/** Passive browser decoders only; unknown MIME types remain inspectable as source JSON. */
export function importedMediaRenderer(mime: string | null): "image" | "audio" | "video" | null {
  if (!mime) return null;
  if (/^image\/(?:png|jpeg|gif|webp|avif|bmp)$/i.test(mime)) return "image";
  if (/^audio\/(?:mpeg|mp3|mp4|ogg|wav|x-wav|webm|flac)$/i.test(mime)) return "audio";
  if (/^video\/(?:mp4|ogg|webm)$/i.test(mime)) return "video";
  return null;
}

/** Called only after an explicit preview click. It never dereferences an imported URL. */
export function importedMediaDataUrl(media: ImportedMedia): string | null {
  if (media.encoding !== "base64" || !media.data || !importedMediaRenderer(media.mime)) return null;
  const encoded = media.data.replace(/\s/g, "");
  if (!/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(encoded)) return null;
  return `data:${media.mime};base64,${encoded}`;
}

function artifacts(exported: Record<string, unknown>, raw: unknown): ImportedArtifact[] {
  const project = (descriptor: unknown, index: number, path: string | null, value: unknown, resolved: boolean, parsedFromText: boolean): ImportedArtifact => {
    const summary = record(descriptor) ?? {};
    const object = record(value) ?? {};
    return {
      id: `artifact-${index}`, title: text(object.title) ?? text(object.name) ?? text(summary.title) ?? text(summary.name) ?? `Artifact ${index + 1}`,
      type: text(object.type) ?? text(summary.type), language: text(object.language), command: text(object.command),
      version: text(object.version_uuid) ?? text(object.version) ?? text(summary.version_uuid), content: text(object.content),
      path, resolved, parsedFromText, raw: value, summary: descriptor,
    };
  };
  const declared = entries(exported.artifacts).map((descriptor, index) => {
    const path = text(record(descriptor)?.path);
    const located = path === null ? { found: false, value: undefined } : importedPointer(raw, path);
    return project(descriptor, index, path, located.value, located.found, false);
  });
  if (record(exported.artifact)) {
    // The native importer explicitly classified this as a parsed text artifact.
    // The complete message retains the source string; parsed fields are a display derivative.
    const nativeText = importedPointer(raw, "/content/text");
    const path = typeof nativeText.value === "string" && nativeText.value.length ? "/content/text" : "/content/parts/0";
    const located = importedPointer(raw, path);
    let value: unknown;
    if (record(exported.artifact)?.parsed === true && typeof located.value === "string") {
      try { value = JSON.parse(located.value); } catch { /* original source remains intact */ }
    }
    declared.push(project(exported.artifact, declared.length, path, value, value !== undefined, true));
  }
  return declared;
}

function reference(summary: unknown, index: number, kind: ImportedReference["kind"], raw: unknown): ImportedReference {
  const descriptor = record(summary) ?? {};
  const path = text(descriptor.path);
  const located = path === null ? { found: false, value: undefined } : importedPointer(raw, path);
  const value = record(located.value);
  const metadata = record(value?.metadata);
  const details = record(value?.details);
  const href = kind === "citation" ? importedCitationUrl(descriptor.url ?? value?.url ?? details?.url ?? metadata?.url) : null;
  return {
    id: `${kind}-${index}`, kind,
    label: text(descriptor.name) || text(value?.file_name) || text(value?.name) || text(value?.title) || text(metadata?.title) || `${kind} ${index + 1}`,
    path, raw: located.value, summary,
    resolved: typeof descriptor.resolved === "boolean" ? descriptor.resolved : null,
    member: text(descriptor.member), blobHash: text(descriptor.blob_hash), href,
    extractedText: text(value?.extracted_content),
  };
}

/** No guesses from names: native classifications retain their explicit inferred flag. */
export function projectImportedMessage(message: Pick<Message, "metadata">): ImportedMessageContent | null {
  const exported = record(record(message.metadata)?.export);
  if (!exported) return null;
  const provider = text(exported.provider) ?? "unknown";
  const raw = exported.raw;
  const declared = entries(exported.blocks);
  const blocks = declared.map((summary, index) => block(summary, index, provider, raw));
  if (!declared.length) {
    const content = record(raw)?.content;
    if (Array.isArray(content)) content.forEach((value, index) => blocks.push(block({ kind: "unknown", type: text(record(value)?.type) ?? "unknown" }, index, provider, raw, `/content/${index}`)));
    else if (content !== undefined) blocks.push(block({ kind: "unknown", type: text(record(content)?.content_type) ?? "unknown" }, 0, provider, raw, "/content"));
  }
  return {
    provider, blocks, artifacts: artifacts(exported, raw), references: [
      ...entries(exported.attachments).map((summary, index) => reference(summary, index, "attachment", raw)),
      ...entries(exported.pointers).map((summary, index) => reference(summary, index, "media-pointer", raw)),
      ...entries(exported.citations).map((summary, index) => reference(summary, index, "citation", raw)),
    ],
    raw, metadata: exported, hidden: exported.hidden === true,
    hiddenReasons: entries(exported.hidden_reasons).filter((value): value is string => typeof value === "string"),
  };
}
