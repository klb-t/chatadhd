/** Native graph methods are data. This adapter installs no execution mechanism. */
export type MethodJson = Record<string, unknown>;
export interface MethodsTransport {
  methods?: (command: MethodJson, options?: { signal?: AbortSignal }) => Promise<MethodJson>;
  getConfig?: () => Promise<MethodJson>;
  setConfigKey?: (key: string, value: unknown) => Promise<MethodJson>;
}
export function methodObject(value: unknown): MethodJson {
  return value !== null && typeof value === "object" && !Array.isArray(value) ? value as MethodJson : {};
}
export function methodRows(value: unknown): MethodJson[] {
  const rows = Array.isArray(value) ? value : Object.values(methodObject(value));
  return rows.map(methodObject);
}
export function methodPretty(value: unknown): string { return JSON.stringify(value ?? null, null, 2); }
export function parseMethodObject(text: string, label: string): MethodJson {
  const parsed: unknown = JSON.parse(text.startsWith("\uFEFF") ? text.slice(1) : text);
  if (parsed === null || typeof parsed !== "object" || Array.isArray(parsed)) throw new Error(`${label} must be a JSON object.`);
  return parsed as MethodJson;
}
export function parseReceiptIds(text: string): string[] {
  const parsed: unknown = JSON.parse(text);
  if (!Array.isArray(parsed) || parsed.some(value => typeof value !== "string" || !value.trim())) throw new Error("Receipt IDs must be an array of nonempty native IDs.");
  if (new Set(parsed).size !== parsed.length) throw new Error("Receipt IDs must not repeat.");
  return parsed as string[];
}
export async function methodCommand(transport: MethodsTransport, command: MethodJson, signal?: AbortSignal): Promise<MethodJson> {
  if (!transport.methods) throw new Error("The graph method API is unavailable in this host.");
  const result = await transport.methods(command, { signal });
  const error = methodObject(result.error);
  if (typeof error.message === "string") throw new Error(error.message);
  return result;
}
/** Semantic roles come only from the caller's native vocabulary. */
export function methodEntityRole(entity: MethodJson, vocabulary: unknown): string | null {
  const entry = Object.entries(methodObject(methodObject(vocabulary).kinds)).find(([, kind]) => kind === entity.kind);
  return entry?.[0] ?? null;
}
export function methodPredicateRole(claim: MethodJson, vocabulary: unknown): string | null {
  const entry = Object.entries(methodObject(methodObject(vocabulary).predicates)).find(([, predicate]) => predicate === claim.predicate);
  return entry?.[0] ?? null;
}
export function methodClaimActive(claim: MethodJson): boolean { return methodObject(claim.assessment).status === "active"; }

export interface MethodResultLineage {
  entity: MethodJson;
  methodVersions: MethodJson[];
  runs: MethodJson[];
  claims: MethodJson[];
  complete: boolean;
}
/** A view of real Claims, never a reconstruction from labels or run metadata. */
export function methodResultLineage(snapshot: MethodJson): MethodResultLineage[] {
  const entities = methodRows(snapshot.entities), claims = methodRows(snapshot.claims);
  const byId = new Map(entities.map(row => [row.id, row]));
  const grouped = new Map<unknown, MethodJson[]>();
  for (const claim of claims) {
    const role = methodPredicateRole(claim, snapshot.vocabulary);
    if (role !== "produced_by_method_version" && role !== "produced_in_run") continue;
    const rows = grouped.get(claim.subject) ?? []; rows.push(claim); grouped.set(claim.subject, rows);
  }
  return [...grouped].flatMap(([id, relations]) => {
    const entity = byId.get(id);
    if (!entity) return [];
    const endpoints = (predicateRole: string, entityRole: string) => relations.flatMap(claim => {
      const target = byId.get(claim.object);
      return methodClaimActive(claim) && entity.status === "active" && target?.status === "active" &&
        methodPredicateRole(claim, snapshot.vocabulary) === predicateRole && methodEntityRole(target, snapshot.vocabulary) === entityRole ? [target] : [];
    });
    const methodVersions = endpoints("produced_by_method_version", "method_version"), runs = endpoints("produced_in_run", "run");
    return [{ entity, methodVersions, runs, claims: relations, complete: methodVersions.length > 0 && runs.length > 0 }];
  });
}
/** Only an exact native preview may become an acceptance command. */
export function methodAcceptance(preview: MethodJson): MethodJson {
  const raw = preview.accept_request_json;
  const request = typeof raw === "string" ? parseMethodObject(raw, "Native acceptance JSON") : methodObject(preview.accept_request);
  if (request.operation !== "accept" || request.explicitly_accepted !== true || typeof request.target !== "string" || !request.target.trim()) throw new Error("Native preview did not return a complete acceptance request.");
  if (methodObject(request.packet).schema !== "loom.graph_packet/1") throw new Error("Native preview did not return a graph packet.");
  const selection = methodObject(request.selection), expected = methodObject(request.expected_rows);
  for (const collection of ["entities", "claims", "sources"]) {
    const ids = selection[collection];
    if (!Array.isArray(ids) || ids.some(id => typeof id !== "string") || new Set(ids).size !== ids.length) throw new Error("Native preview selection is incomplete.");
    const expectations = methodObject(expected[collection]);
    if (Object.keys(expectations).length !== ids.length || ids.some(id => !(id in expectations) || !(expectations[id] === null || typeof expectations[id] === "string"))) throw new Error("Native preview CAS expectations are incomplete.");
  }
  return typeof raw === "string" ? { operation: "accept", request_json: raw } : request;
}

function comparable(value: unknown): string {
  if (Array.isArray(value)) return `[${value.map(comparable).join(",")}]`;
  if (value !== null && typeof value === "object") return `{${Object.keys(value).sort().map(key => `${JSON.stringify(key)}:${comparable((value as MethodJson)[key])}`).join(",")}}`;
  return JSON.stringify(value);
}
export function methodProfilePath(descriptor: MethodJson): string[] {
  const path = descriptor.config_path;
  if (!Array.isArray(path) || path.length < 2 || path.some(key => typeof key !== "string" || !key || ["__proto__", "constructor", "prototype"].includes(key))) throw new Error("Native catalog did not declare a safe profile config path.");
  return path as string[];
}
function atPath(value: unknown, path: string[]): unknown {
  for (const key of path) value = methodObject(value)[key];
  return value;
}
/** Persist a frozen selection of already accepted rows, preserving fresh siblings. */
export async function saveAcceptedMethodProfile(transport: MethodsTransport, descriptor: MethodJson,
  profile: MethodJson, receiptIds: string[], original?: { profileJson: string; selectionJson: string }): Promise<MethodJson> {
  if (!transport.methods) throw new Error("Native config CAS persistence is unavailable in this host. The accepted library receipt is retained.");
  const path = methodProfilePath(descriptor);
  const receiptPath = descriptor.receipt_path;
  if (!Array.isArray(receiptPath) || receiptPath.length !== path.length || receiptPath.some(key => typeof key !== "string" || !key || ["__proto__", "constructor", "prototype"].includes(key)) || receiptPath.slice(0, -1).some((key, index) => key !== path[index])) throw new Error("Native catalog did not declare the profile's receipt config path.");
  const selectionPath = descriptor.selection_path;
  if (!Array.isArray(selectionPath) || selectionPath.length !== path.length || selectionPath.some(key => typeof key !== "string" || !key || ["__proto__", "constructor", "prototype"].includes(key)) || selectionPath.slice(0, -1).some((key, index) => key !== path[index])) throw new Error("Native catalog did not declare the profile's selection config path.");
  const fresh = await methodCommand(transport, { operation: "chat_settings" });
  const hash = fresh[`${path[0]}_sha256`];
  if (fresh.scope !== "global_native_config" || typeof hash !== "string" || !/^[\da-f]{64}$/.test(hash)) throw new Error("Native config did not return its CAS identity. The accepted library receipt is retained.");
  if (fresh[path[0]] === null || typeof fresh[path[0]] !== "object" || Array.isArray(fresh[path[0]])) throw new Error("Native config root must be an object; its original value is retained.");
  const profileJson = original?.profileJson ?? methodPretty(profile), selectionJson = original?.selectionJson ?? methodPretty(profile.selection);
  const frozenProfile = { ...parseMethodObject(profileJson, "Accepted native profile JSON"), selection: parseMethodObject(selectionJson, "Reviewed selection JSON") };
  await methodCommand(transport, { operation: "save_profile_selection", slot_id: descriptor.id,
    profile_json: profileJson, receipt_ids: receiptIds, selection_json: selectionJson, expected_context_execution_sha256: hash });
  const readback = await methodCommand(transport, { operation: "chat_settings" });
  if (comparable(atPath(readback, path)) !== comparable(frozenProfile) || comparable(atPath(readback, receiptPath as string[])) !== comparable(receiptIds) || comparable(atPath(readback, selectionPath as string[])) !== comparable(frozenProfile.selection)) throw new Error("Native config readback differs from the accepted profile selection. The library receipt is retained; refresh config before retrying its save.");
  return readback;
}
