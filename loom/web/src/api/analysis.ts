/** Native analysis preparation is a handle to exact server-owned bytes. */
export type AnalysisObject = Record<string, unknown>;
export type AnalysisCommand = (command: AnalysisObject, options?: { signal?: AbortSignal }) => Promise<AnalysisObject>;
export interface AnalysisTransport { analysis?: AnalysisCommand; methods?: AnalysisCommand }
export interface AnalysisPrepared extends AnalysisObject {
  schema: "loom.analysis_prepared/1";
  prepared_id: string;
  request_identity_hash: string;
  request_hash: string;
  contract_hash: string;
  status: string;
  attempted: boolean;
  request: AnalysisObject & { body_bytes: string };
  contract: AnalysisObject;
  result: AnalysisObject;
}
export function analysisObject(value: unknown): AnalysisObject {
  return value !== null && typeof value === "object" && !Array.isArray(value) ? value as AnalysisObject : {};
}
export function analysisJson(value: unknown) { return JSON.stringify(value ?? null, null, 2); }
export function parseAnalysisObject(text: string, label: string): AnalysisObject {
  const value: unknown = JSON.parse(text);
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error(`${label} must be a JSON object.`);
  return value as AnalysisObject;
}
export function analysisFailure(value: AnalysisObject): string | null {
  const error = analysisObject(value.error);
  return typeof error.message === "string" ? error.message : typeof error.code === "string" ? error.code : null;
}
export async function analysisCall(command: AnalysisCommand | undefined, payload: AnalysisObject, signal?: AbortSignal): Promise<AnalysisObject> {
  if (!command) throw new Error("Native analysis is unavailable in this host.");
  // Capture the command before the async boundary. The caller may keep editing.
  const captured = JSON.parse(JSON.stringify(payload)) as AnalysisObject;
  const result = await command(captured, signal ? { signal } : undefined);
  const failure = analysisFailure(result);
  if (failure) throw new Error(failure);
  return result;
}
export function readAnalysisPrepared(value: AnalysisObject): AnalysisPrepared {
  const request = analysisObject(value.request);
  if (value.schema !== "loom.analysis_prepared/1" ||
      !["prepared_id", "request_identity_hash", "request_hash", "contract_hash", "status"].every(key => typeof value[key] === "string" && String(value[key]).length > 0) ||
      typeof value.attempted !== "boolean" || typeof request.body_bytes !== "string" ||
      !value.contract || typeof value.contract !== "object" || Array.isArray(value.contract))
    throw new Error("Native preparation returned an incomplete exact-request handle.");
  return value as AnalysisPrepared;
}
export function exactAnalysisCommand(prepared: AnalysisPrepared, confirmation?: AnalysisObject): AnalysisObject {
  return { operation: "execute", prepared_id: prepared.prepared_id, request_identity_hash: prepared.request_identity_hash,
    ...(confirmation ? { confirmation: JSON.parse(JSON.stringify(confirmation)) as AnalysisObject } : {}) };
}
export function methodVersions(snapshot: AnalysisObject): AnalysisObject[] {
  const kinds = analysisObject(analysisObject(snapshot.vocabulary).kinds);
  const entities = Array.isArray(snapshot.entities) ? snapshot.entities : Object.values(analysisObject(snapshot.entities));
  return entities.map(analysisObject).filter(entity => entity.kind === kinds.method_version);
}
export function methodIdentityClaims(snapshot: AnalysisObject, versionId: string): string[] {
  const predicate = analysisObject(analysisObject(snapshot.vocabulary).predicates).version_of;
  const claims = Array.isArray(snapshot.claims) ? snapshot.claims : Object.values(analysisObject(snapshot.claims));
  return claims.map(analysisObject).filter(claim => claim.subject === versionId && claim.predicate === predicate &&
    analysisObject(claim.assessment).status === "active" && typeof claim.id === "string").map(claim => claim.id as string);
}
