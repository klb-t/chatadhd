// Knowledge-layer contract, deliberately separate from the legacy chat graph.
// Open model fields survive as JSON; UI labels must not turn inference into fact.
export type KnowledgeRecord = Record<string, unknown>;
export type KnowledgeCollection = "entities" | "claims" | "principles" | "operators" | "instances" | "products";
export interface KnowledgeQueryResult { run: string; items: KnowledgeRecord[] }
export interface KnowledgeRun extends KnowledgeRecord { id: string; status: string }
export interface KnowledgeContextRequest {
  text: string;
  targets: string[];
  project?: string;
  budget_tokens: number;
  run?: string;
  lang?: string;
}
export interface KnowledgeContextItem extends KnowledgeRecord {
  ref: string;
  ref_kind: string;
  band: string;
  resolution: string;
  why: string;
  tokens: number;
  text: string;
  factors: KnowledgeRecord;
  required_by: string[];
}
export interface KnowledgeContextResult {
  context_set: {
    id: string;
    goal: KnowledgeRecord;
    budget_tokens: number;
    used_tokens: number;
    items: KnowledgeContextItem[];
    dropped: KnowledgeContextItem[];
  };
  text?: string;
  prompt?: string;
}
export interface KnowledgeApi {
  listRuns(): Promise<KnowledgeRun[]>;
  query(what: KnowledgeCollection, filters?: KnowledgeRecord): Promise<KnowledgeQueryResult>;
  run(config: KnowledgeRecord): Promise<KnowledgeRecord>;
  cancel(): Promise<void>;
  buildContext(request: KnowledgeContextRequest): Promise<KnowledgeContextResult>;
  catalogUnits(filters?: KnowledgeRecord): Promise<KnowledgeRecord[]>;
  catalogScan(config: KnowledgeRecord): Promise<KnowledgeRecord>;
  catalogPreview(id: string): Promise<KnowledgeRecord>;
  catalogOverride(override: KnowledgeRecord): Promise<KnowledgeRecord>;
  catalogImport(options: KnowledgeRecord): Promise<KnowledgeRecord>;
}

export function asRecord(value: unknown): KnowledgeRecord {
  return value && typeof value === "object" && !Array.isArray(value) ? value as KnowledgeRecord : {};
}
export function asArray(value: unknown): unknown[] { return Array.isArray(value) ? value : []; }
export function displayText(value: unknown): string {
  if (value == null) return "";
  if (typeof value === "string") return value;
  if (typeof value !== "object") return String(value);
  const record = asRecord(value);
  if (typeof record.en === "string") return record.en;
  if (typeof record.pl === "string") return record.pl;
  return JSON.stringify(value);
}
