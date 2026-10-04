import type { GraphPacketExpectedRows, GraphPacketStoreAcceptRequest, GraphPacketStoreResult } from "../api/graph-packets";
import { createProfileRegistry, registerProfile, type ApplicationProfile, type ProfileRegistry } from "./runtime";

export interface ApplicationProfileSource { text: string; sourceRef: string; actor: string; knownAt?: string | null }
const SERIALIZER = "loom.application_profile_graph/1";

/** Parse JSON without changing the original UTF-8 source text or its hash. */
export function parseApplicationProfileSource(text: string): unknown {
  try { return JSON.parse(text.startsWith("\uFEFF") ? text.slice(1) : text); }
  catch { throw new Error("The original profile source must contain valid JSON."); }
}

function canonical(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  if (value && typeof value === "object") {
    return `{${Object.keys(value).sort().map(key => `${JSON.stringify(key)}:${canonical((value as Record<string, unknown>)[key])}`).join(",")}}`;
  }
  const result = JSON.stringify(value);
  if (result === undefined || (typeof value === "number" && !Number.isFinite(value))) throw new Error("Profile graph values must be JSON data.");
  return result;
}
async function sha256(text: string): Promise<string> {
  if (!globalThis.crypto?.subtle) throw new Error("Profile graph hashing needs Web Crypto in a secure context.");
  const hash = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
  return [...new Uint8Array(hash)].map(byte => byte.toString(16).padStart(2, "0")).join("");
}
function nonempty(value: unknown, field: string): asserts value is string {
  if (typeof value !== "string" || !value.trim()) throw new Error(`${field} must be a nonempty string.`);
}
function timestamp(value: string): boolean {
  const match = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.\d+)?(Z|[+-]\d{2}:\d{2})$/.exec(value);
  if (!match || !Number.isFinite(Date.parse(value))) return false;
  const [year, month, day, hour, minute, second] = match.slice(1, 7).map(Number);
  const leap = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
  const days = [31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
  const zone = match[7];
  return year >= 1 && month >= 1 && month <= 12 && day >= 1 && day <= days[month - 1] &&
    hour <= 23 && minute <= 59 && second <= 59 &&
    (zone === "Z" || (Number(zone.slice(1, 3)) <= 23 && Number(zone.slice(4)) <= 59));
}
function validUtf16(value: unknown): void {
  if (typeof value === "string") {
    // Reject unpaired surrogates instead of silently replacing source labels
    // at the native UTF-8 JSON boundary. Original JSON text remains available.
    for (let index = 0; index < value.length; index++) {
      const unit = value.charCodeAt(index);
      if (unit >= 0xd800 && unit <= 0xdbff) {
        const next = value.charCodeAt(++index);
        if (!(next >= 0xdc00 && next <= 0xdfff)) throw new Error("Profile graph text must be valid Unicode.");
      } else if (unit >= 0xdc00 && unit <= 0xdfff) throw new Error("Profile graph text must be valid Unicode.");
    }
  } else if (Array.isArray(value)) value.forEach(validUtf16);
  else if (value && typeof value === "object") for (const [key, item] of Object.entries(value)) { validUtf16(key); validUtf16(item); }
}

/** Prepare only. Calling this function does not write or accept anything. */
export async function makeApplicationProfileGraphAcceptance(profile: ApplicationProfile, source: ApplicationProfileSource,
  expectedRows?: GraphPacketExpectedRows): Promise<GraphPacketStoreAcceptRequest> {
  nonempty(source.text, "source.text"); nonempty(source.sourceRef, "source.sourceRef"); nonempty(source.actor, "source.actor");
  const knownAt = source.knownAt ?? null;
  if (knownAt !== null && (typeof knownAt !== "string" || !timestamp(knownAt))) {
    throw new Error("source.knownAt must be a timestamp with timezone, or null when unknown.");
  }
  const parsed = parseApplicationProfileSource(source.text);
  // Recheck the data contract at this persistence boundary. These vocabulary
  // entries authorize serialization only; they install no executable adapter.
  const validationRegistry = createProfileRegistry({ renderers: [profile.presentation.renderer],
    operations: profile.actions.map(action => action.operation) });
  registerProfile(validationRegistry, parsed);
  if (canonical(parsed) !== canonical(profile)) throw new Error("Original source JSON differs from the registered profile revision.");
  validUtf16(profile); validUtf16(source);
  const rawSha256 = await sha256(source.text);
  const definitionSha256 = await sha256(canonical(profile));
  const identity = { id: profile.id, profile_revision: profile.profile_revision, target: profile.target, definition_sha256: definitionSha256 };
  const identityHash = await sha256(canonical(identity));
  const entityId = `e_ap_${identityHash}`;
  const sourceId = `o_ap_${await sha256(canonical({ raw_sha256: rawSha256, source_ref: source.sourceRef, actor: source.actor, known_at: knownAt }))}`;
  const origin = { kind: "recorded", actor: source.actor, model: null, recipe_sha256: null, response_sha256: null };
  const entity = {
    id: entityId, kind: "application_profile", canonical_key: `application_profile:${identityHash}`,
    label: profile.label, labels: {}, aliases: [], parent: "", first_seen: "", last_seen: "",
    evidence_class: "user", origin: "user", confidence: 1, status: "active",
    attrs: { serializer: SERIALIZER, profile_id: profile.id, profile_revision: profile.profile_revision,
      target: profile.target, definition_sha256: definitionSha256, raw_source: source.text,
      raw_sha256: rawSha256, source_observation: sourceId },
  };
  const observation = {
    id: sourceId, unit: `u_ap_${sourceId.slice(5)}`, kind: "code_block", text: source.text,
    locator: { source: source.sourceRef, member: "", json_pointer: "", byte_start: 0,
      byte_len: new TextEncoder().encode(source.text).length, time_start: null, time_end: null, line: null },
    lang: "", date: knownAt ?? "", ordinal: 0, artifact_type: "application_profile_json", speaker: source.actor,
    attrs: { serializer: SERIALIZER, raw_sha256: rawSha256, profile_entity: entityId },
  };
  const record = { observation, known_at: knownAt, text_sha256: rawSha256 };
  // Full profile numbers stay inside exact source text. Packet metadata uses
  // integer/nullable DTO fields so its canonical hashes agree with native JSON.
  const packet: Record<string, unknown> = {
    schema: "loom.graph_packet/1", definitions: [], entities: [entity], claims: [], sources: [record],
    task: { operation: "application.profile.persist", profile_identity: identity, raw_sha256: rawSha256,
      source_ref: source.sourceRef, transformation_history: "not_asserted" },
    provenance: { definitions: {}, entities: { [entityId]: { known_at: knownAt, origin, record_sha256: await sha256(canonical(entity)) } },
      claims: {}, sources: { [sourceId]: { known_at: knownAt, origin, record_sha256: await sha256(canonical(record)) } } },
    history: [],
  };
  packet.packet_id = await sha256(canonical(packet));
  return { operation: "accept", target: `application_profile:${identityHash}`, packet,
    selection: { entities: [entityId], claims: [], sources: [sourceId] },
    expected_rows: expectedRows ?? { entities: { [entityId]: null }, claims: {}, sources: { [sourceId]: null } },
    explicitly_accepted: true };
}

/** Validate a fresh native read/replay result before registering its profile. */
export async function readProfileFromReceipt(result: GraphPacketStoreResult, registry: ProfileRegistry) {
  if (result.row_drift?.matches !== true) throw new Error("Stored profile rows have drifted; the original receipt is retained.");
  const receipt = result.receipt;
  if (!receipt || receipt.acceptance_establishes_content_truth !== false || receipt.explicitly_accepted !== true) {
    throw new Error("Expected an explicitly accepted native GraphPacket receipt.");
  }
  const packet = receipt.packet;
  const entities = packet?.entities as Record<string, unknown>[] | undefined;
  const sources = packet?.sources as { observation: Record<string, unknown>; known_at: string | null }[] | undefined;
  if (!Array.isArray(entities) || entities.length !== 1 || !Array.isArray(sources) || sources.length !== 1) {
    throw new Error("Receipt does not contain one application profile and its exact source.");
  }
  const entity = entities[0]; const attrs = entity.attrs as Record<string, unknown> | undefined;
  if (entity.kind !== "application_profile" || attrs?.serializer !== SERIALIZER) throw new Error("Receipt is not an application profile projection.");
  const raw = attrs.raw_source;
  nonempty(raw, "receipt.raw_source");
  const temporary = createProfileRegistry({ renderers: [...registry.renderers], operations: [...registry.operations] });
  const profile = registerProfile(temporary, parseApplicationProfileSource(raw));
  const observation = sources[0].observation;
  const locator = observation.locator as Record<string, unknown>;
  const provenance = packet.provenance as { entities: Record<string, { origin: { actor: string } }> };
  const source: ApplicationProfileSource = { text: raw, sourceRef: locator?.source as string,
    actor: provenance?.entities?.[entity.id as string]?.origin?.actor, knownAt: sources[0].known_at };
  const expected = await makeApplicationProfileGraphAcceptance(profile, source);
  if (canonical(expected.packet) !== canonical(packet) || receipt.target !== expected.target ||
      canonical(receipt.selection) !== canonical(expected.selection)) throw new Error("Stored profile source, identity or provenance differs from its projection.");
  const requestPayload = { target: receipt.target, packet, selection: receipt.selection,
    expected_rows: receipt.expected_rows, explicitly_accepted: true };
  const requestHash = await sha256(canonical(requestPayload));
  if (receipt.id !== `gpr_${requestHash}` || receipt.request_sha256 !== requestHash ||
      typeof receipt.receipt_sha256 !== "string" || !/^[\da-f]{64}$/.test(receipt.receipt_sha256)) {
    throw new Error("Stored profile receipt identity differs from its acceptance request.");
  }
  // Native read/replay has already verified receipt_sha256 and current native
  // row snapshots; browser number parsing cannot re-create every float token.
  return { profile: registerProfile(registry, profile), source, receiptId: receipt.id, runId: receipt.run_id,
    entityId: entity.id as string, rawSha256: attrs.raw_sha256 as string, definitionSha256: attrs.definition_sha256 as string };
}
