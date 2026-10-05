import data from "../profiles/presets/graph-chat.json";
import { record } from "./reply-inspection";

export const graphChatPresets = data.presets;
export const graphChatInstallation = { target: data.library_target, actor: data.installation_actor };
function canonical(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  if (value && typeof value === "object") return `{${Object.entries(value).sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0).map(([key, item]) => `${JSON.stringify(key)}:${canonical(item)}`).join(",")}}`;
  return JSON.stringify(value);
}
async function digest(text: string): Promise<string> {
  return [...new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text)))].map(byte => byte.toString(16).padStart(2, "0")).join("");
}
/** Universal DTO construction; method choices and native vocabulary are data. */
export async function instantiateGraphChatPreset(id: string): Promise<Record<string, unknown>> {
  const preset = data.presets.find(value => value.id === id);
  if (!preset) throw new Error("Unknown graph chat preset.");
  const ids = new Map<string, string>();
  const entities: Record<string, unknown>[] = [];
  for (const spec of preset.entities) {
    const attrs = structuredClone(spec.attrs) as Record<string, unknown>;
    let versionHash = "";
    if (attrs.definition) { versionHash = await digest(canonical(attrs.definition)); attrs.definition_sha256 = versionHash; }
    if (typeof attrs.text === "string") { versionHash = await digest(attrs.text); attrs.text_sha256 = versionHash; }
    const entityId = versionHash ? `${spec.id}:${versionHash}` : spec.id;
    ids.set(spec.id, entityId);
    const kind = (data.vocabulary.kinds as Record<string, string>)[spec.role];
    if (!kind) throw new Error(`Missing native kind for ${spec.role}.`);
    entities.push({ ...structuredClone(data.entity_template), id: entityId, canonical_key: entityId, kind, label: spec.label, attrs });
  }
  const claims = await Promise.all(preset.claims.map(async spec => {
    const subject = ids.get(spec.subject), object = ids.get(spec.object), predicate = (data.vocabulary.predicates as Record<string, string>)[spec.predicate];
    if (!subject || !object || !predicate) throw new Error("Graph preset contains an unresolved native relation.");
    return { ...structuredClone(data.claim_template), id: `graph-preset-claim:${await digest(canonical({ subject, predicate, object }))}`, subject, predicate, object };
  }));
  const selection = structuredClone(preset.selection);
  for (const member of selection.members) member.method_version_id = ids.get(member.method_version_id) ?? member.method_version_id;
  const profile = { vocabulary: structuredClone(data.vocabulary), entities, claims, sources: [], selection };
  return { ...structuredClone(preset.options), profile };
}
export function selectGraphChatProfile(options: Record<string, unknown>, entry: Record<string, unknown>): Record<string, unknown> {
  const profile = record(entry.profile);
  if (!Object.keys(profile).length) throw new Error("Saved profile did not contain a native method profile.");
  return { ...options, profile, ...(Array.isArray(entry.receipt_ids) && { receipt_ids: entry.receipt_ids }) };
}
