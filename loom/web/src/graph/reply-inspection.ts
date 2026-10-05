/** Addresses use compiler code-point spans, never JavaScript UTF-16 offsets. */
export type PacketCommandApi = (command: Record<string, unknown>) => Promise<Record<string, unknown>>;
export type GraphReplyAction = "expand" | "correct";
export interface GraphReplyFragmentAddress {
  schema: "loom.graph_reply_fragment/1";
  compilation_sha256: string;
  base_packet_sha256: string;
  turn_entity_id: string;
  entity_id: string;
  local_id: string;
  request_id: string;
  turn_id: string;
  model: string;
  source_observation_id: string;
  span: Record<string, unknown>;
  text: string;
  role: string;
  origin: "model";
  content_verification: "unverified";
}
export function record(value: unknown): Record<string, unknown> {
  return value && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : {};
}
export function parseObject(raw: string, label: string): Record<string, unknown> {
  const parsed: unknown = JSON.parse(raw);
  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) throw new Error(`${label} must be a JSON object.`);
  return parsed as Record<string, unknown>;
}
export function packetFailure(value: Record<string, unknown>): string | null {
  if (!value.error) return null;
  const detail = record(value.error);
  return typeof detail.message === "string" ? detail.message : typeof value.error === "string" ? value.error : JSON.stringify(value.error);
}
export function packetResult(value: Record<string, unknown>): Record<string, unknown> {
  const failure = packetFailure(value);
  if (failure) throw new Error(failure);
  if (value.executed === false && value.replayed !== true) throw new Error("Operation was held by the usage policy or awaits dispatch recovery.");
  const result = value.executed === true || value.replayed === true ? record(value.result) : value;
  const nestedFailure = packetFailure(result);
  if (nestedFailure) throw new Error(nestedFailure);
  return result;
}
const string = (value: unknown) => typeof value === "string" ? value : "";
export function inspectReply(compilation: Record<string, unknown>): GraphReplyFragmentAddress[] {
  if (compilation.schema !== "loom.graph_reply_compilation/1" || typeof compilation.response_text !== "string") throw new Error("Server did not return a graph reply compilation.");
  const spans = record(compilation.spans), ids = record(compilation.node_ids), host = record(compilation.host);
  const additions = record(record(compilation.diff).entities).add;
  const entities = new Map((Array.isArray(additions) ? additions : []).map((value) => { const entity = record(value); return [string(entity.id), entity] as const; }));
  const points = Array.from(compilation.response_text);
  return Object.entries(spans).map(([localId, value]) => {
    const span = record(value), start = span.char_start, length = span.char_len;
    if (typeof start !== "number" || typeof length !== "number" || !Number.isSafeInteger(start) || !Number.isSafeInteger(length) || start < 0 || length < 0 || start + length > points.length) throw new Error(`Invalid compiler code-point span for ${localId}.`);
    const entityId = string(ids[localId]);
    if (!entityId) throw new Error(`Missing compiled identity for ${localId}.`);
    const attrs = record(entities.get(entityId)?.attrs);
    return { schema: "loom.graph_reply_fragment/1", compilation_sha256: string(compilation.compilation_sha256), base_packet_sha256: string(compilation.base_packet_sha256),
      turn_entity_id: string(compilation.turn_entity_id), entity_id: entityId, local_id: localId, request_id: string(host.request_id), turn_id: string(host.turn_id), model: string(host.model),
      source_observation_id: string(attrs.source_observation_id), span, text: points.slice(start, start + length).join(""), role: string(attrs.role), origin: "model", content_verification: "unverified" };
  });
}
