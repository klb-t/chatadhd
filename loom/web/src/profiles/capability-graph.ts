import type { GraphPacketExpectedRows, GraphPacketStoreAcceptRequest } from "../api/graph-packets";
import type { OperationEvidence } from "../api/operations";
import { makeApplicationProfileGraphAcceptance, type ApplicationProfileSource } from "./graph";
import type { ApplicationProfile } from "./runtime";

const SERIALIZER = "loom.application_capability_graph/1";
function canonical(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  if (value && typeof value === "object") return `{${Object.keys(value).sort().map(key => `${JSON.stringify(key)}:${canonical((value as Record<string, unknown>)[key])}`).join(",")}}`;
  const result = JSON.stringify(value);
  if (result === undefined || (typeof value === "number" && !Number.isFinite(value))) throw new Error("Capability projection requires finite JSON data.");
  return result;
}
async function sha256(text: string) {
  if (!globalThis.crypto?.subtle) throw new Error("Capability graph hashing needs Web Crypto in a secure context.");
  return [...new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text)))].map(byte => byte.toString(16).padStart(2, "0")).join("");
}
function unicode(text: string) {
  for (let i = 0; i < text.length; i++) {
    const unit = text.charCodeAt(i);
    if (unit >= 0xd800 && unit <= 0xdbff) {
      const next = text.charCodeAt(++i);
      if (!(next >= 0xdc00 && next <= 0xdfff)) throw new Error("Capability evidence contains invalid Unicode.");
    } else if (unit >= 0xdc00 && unit <= 0xdfff) throw new Error("Capability evidence contains invalid Unicode.");
  }
}

/** Additive projection. It never changes the original profile serializer or asserts tested service parity. */
export async function makeCapabilityGraphAcceptance(profile: ApplicationProfile, evidence: readonly OperationEvidence[],
  source: ApplicationProfileSource, expectedRows?: GraphPacketExpectedRows): Promise<GraphPacketStoreAcceptRequest> {
  // Reuse the original boundary to verify exact source and derive its identity;
  // this function does not persist or duplicate that profile's existing rows.
  const original = await makeApplicationProfileGraphAcceptance(profile, source);
  const profileEntity = original.selection.entities[0];
  const seen = new Set<string>();
  for (const entry of evidence) {
    for (const text of [entry.operation, entry.capability, entry.detail, ...entry.evidence]) {
      if (typeof text !== "string") throw new Error("Capability evidence fields must be strings.");
      unicode(text);
    }
    if (!entry.operation.trim() || !entry.capability.trim() || !["native", "equivalent", "limited", "unavailable"].includes(entry.status)) throw new Error("Invalid capability evidence.");
    const identity = JSON.stringify([entry.operation, entry.capability]);
    if (seen.has(identity)) throw new Error("Duplicate capability evidence identity.");
    seen.add(identity);
  }
  const knownAt = source.knownAt ?? null;
  const text = JSON.stringify({ schema: SERIALIZER, profile_source: source.text, source_ref: source.sourceRef,
    profile_entity: profileEntity, operation_evidence: evidence });
  const textHash = await sha256(text);
  const projectionHash = await sha256(canonical({ profile_entity: profileEntity, evidence, source_ref: source.sourceRef,
    actor: source.actor, known_at: knownAt, text_sha256: textHash }));
  const rootId = `e_acp_${projectionHash}`;
  const sourceId = `o_acp_${projectionHash}`;
  const entity = (id: string, kind: string, label: string, parent: string, attrs: Record<string, unknown>) => ({
    id, kind, canonical_key: `${kind}:${id}`, label, labels: {}, aliases: [], parent, first_seen: "", last_seen: "",
    evidence_class: "user", origin: "user", confidence: 1, status: "active", attrs,
  });
  const entities = [entity(rootId, "application_profile_capabilities", `${profile.label} capabilities`, "", {
    serializer: SERIALIZER, profile_id: profile.id, profile_revision: profile.profile_revision, target: profile.target,
    profile_entity: profileEntity, source_observation: sourceId, original_service_parity: "not_asserted",
  })];
  for (const entry of evidence) {
    const id = `e_ac_${await sha256(canonical({ projection: projectionHash, operation: entry.operation, capability: entry.capability }))}`;
    entities.push(entity(id, "application_capability", entry.operation, rootId, { serializer: SERIALIZER,
      profile_id: profile.id, profile_revision: profile.profile_revision, profile_entity: profileEntity,
      operation: entry.operation, capability: entry.capability, availability: entry.status, detail: entry.detail,
      evidence_refs: [...entry.evidence], source_observation: sourceId, original_service_parity: "not_asserted" }));
  }
  const observation = { id: sourceId, unit: `u_acp_${projectionHash}`, kind: "code_block", text,
    locator: { source: source.sourceRef, member: "", json_pointer: "", byte_start: 0, byte_len: new TextEncoder().encode(text).length,
      time_start: null, time_end: null, line: null }, lang: "", date: knownAt ?? "", ordinal: 0,
    artifact_type: "application_capability_json", speaker: source.actor,
    attrs: { serializer: SERIALIZER, raw_sha256: textHash, profile_entity: profileEntity, capability_root: rootId } };
  const record = { observation, known_at: knownAt, text_sha256: textHash };
  const origin = { kind: "recorded", actor: source.actor, model: null, recipe_sha256: null, response_sha256: null };
  const entityProvenance: Record<string, unknown> = {};
  for (const row of entities) entityProvenance[row.id] = { known_at: knownAt, origin, record_sha256: await sha256(canonical(row)) };
  const packet: Record<string, unknown> = { schema: "loom.graph_packet/1", definitions: [], entities, claims: [], sources: [record],
    task: { operation: "application.capabilities.persist", profile_entity: profileEntity, original_service_parity: "not_asserted" },
    provenance: { definitions: {}, entities: entityProvenance, claims: {},
      sources: { [sourceId]: { known_at: knownAt, origin, record_sha256: await sha256(canonical(record)) } } }, history: [] };
  packet.packet_id = await sha256(canonical(packet));
  return { operation: "accept", target: `application_capabilities:${projectionHash}`, packet,
    selection: { entities: entities.map(row => row.id), claims: [], sources: [sourceId] },
    expected_rows: expectedRows ?? { entities: Object.fromEntries(entities.map(row => [row.id, null])), claims: {}, sources: { [sourceId]: null } },
    explicitly_accepted: true };
}
