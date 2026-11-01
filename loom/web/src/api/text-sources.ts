/** Existing G1 actions. Raw UTF-8 byte coordinates; never PDF pages or OCR pixels. */
export interface TextSourceRequest {
  hash: string; source_key: string; name: string; format: string;
  language?: string; previous_source_id?: string;
}
export interface TextSource {
  id: string; title: string; blob_hash: string; format: string;
  parser: string; metadata: { source_key: string; previous_source_id?: string };
}
export interface SourceParts {
  schema: "loom.source_text_parts/1"; source_id: string; source_node: string;
  source_sha256: string; projection_node: string; version_node: string;
  occurrence_nodes: string[]; coordinate: "source_utf8_bytes";
  status: string; reason: string; method: Record<string, unknown>;
}
export interface SourceOccurrence {
  node_id: string; text: string; source: TextSource;
  provenance: { schema: string; source_id: string; source_sha256: string;
    coordinate: string; byte_range: [number, number]; index: number };
}

export { textSources, readSourceParts, readSourceOccurrence, sourceSelector, sourceSelectionPatch, materializationRequest, saveSourceSelection } from "./text-sources-state.mjs";
