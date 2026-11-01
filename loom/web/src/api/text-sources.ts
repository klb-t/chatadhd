/** Existing G1 actions. Original or derived UTF-8 byte coordinates; never ZIP/XML offsets or pages. */
export interface TextSourceRequest {
  hash: string; source_key: string; name: string; format: string;
  language?: string; previous_source_id?: string;
}
export interface DocxDerivation {
  schema: "loom.docx_source_derivation/1" | "loom.pdf_source_derivation/1" | "loom.host_source_derivation/1"; original_source_id: string; original_sha256: string;
  original_size: number; text_sha256: string; extraction_blob: string; transform_sha256: string;
  transform: Record<string, unknown>;
}
export interface SourceBlock {
  ordinal: number; text: string;
  attrs: { recognized_kind: string; original_locator: string | Record<string, unknown>; evidence_class: string; coordinate: string };
  locator: { source: string; byte_start: number; byte_len: number };
}
export interface TextSource {
  id: string; title: string; blob_hash: string; format: string;
  parser: string; metadata: { source_key: string; previous_source_id?: string; coordinate?: string; derivation?: DocxDerivation };
}
export interface SourceParts {
  schema: "loom.source_text_parts/1"; source_id: string; source_node: string;
  source_sha256: string; projection_node: string; version_node: string;
  occurrence_nodes: string[]; coordinate: "source_utf8_bytes" | "derived_utf8_bytes" | "archive_members";
  structure_nodes?: { node_id: string; parent?: string; kind?: string; title?: string; locator?: Record<string, unknown>; attrs?: Record<string, unknown> }[];
  children?: SourceParts[];
  derivation?: DocxDerivation; warnings?: string[]; observations?: SourceBlock[];
  status: string; reason: string; method: Record<string, unknown>;
}
export interface SourceOccurrence {
  node_id: string; text: string; source: TextSource;
  provenance: { schema: string; source_id: string; source_sha256: string;
    coordinate: string; derivation?: DocxDerivation; byte_range: [number, number]; index: number };
}

export { sourceDerivation, sourceOriginalHash, materializedSource, createSourceTaskGate, textSources, readSourceParts, readSourceOccurrence, sourceSelector, sourceSelectionPatch, materializationRequest, saveSourceSelection } from "./text-sources-state.mjs";
