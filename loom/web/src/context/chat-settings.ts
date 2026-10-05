import type { CandidateChannelRequest } from "../api/types";
import { newPlan, type PlanDraft } from "./retrieval-plan";

export function storedPlan(value: unknown): PlanDraft {
  if (value === undefined) return newPlan();
  if (!value || typeof value !== "object" || Array.isArray(value)) throw Error("Stored context plan must be an object.");
  const draft = value as Record<string, unknown>;
  if (typeof draft.id !== "string" || typeof draft.sourceRef !== "string" || !Array.isArray(draft.theses)) throw Error("Stored context plan is malformed.");
  const keys = new Set<number>();
  for (const item of draft.theses) {
    if (!item || typeof item !== "object" || Array.isArray(item)) throw Error("Stored context thesis is malformed.");
    for (const field of ["id", "text", "targets", "claims", "hops", "weight"]) if (typeof item[field] !== "string") throw Error(`Stored thesis ${field} has an invalid type.`);
    if (keys.has(item.key)) throw Error("Stored thesis keys must be unique.");
    keys.add(item.key);
    if (!Number.isSafeInteger(item.key) || typeof item.counter !== "boolean" || !["inherit", "custom"].includes(item.targetsMode) || !["inherit", "custom"].includes(item.claimsMode) || !["inherit", "label", "summary", "full", "raw"].includes(item.detail)) throw Error("Stored thesis controls are malformed.");
  }
  return draft as unknown as PlanDraft;
}

export function parseCandidateChannels(raw: string): CandidateChannelRequest[] {
  const value: unknown = JSON.parse(raw);
  if (!Array.isArray(value)) throw new Error("Candidate channels must be a JSON array.");
  const ids = new Set<string>();
  for (const channel of value) {
    if (!channel || typeof channel !== "object" || Array.isArray(channel)) throw new Error("Each channel must be an object.");
    const row = channel as Record<string, unknown>;
    if (typeof row.id !== "string" || !row.id.trim() || ids.has(row.id)) throw new Error("Channel IDs must be nonempty and unique.");
    ids.add(row.id);
    if (row.limit !== undefined && (!Number.isSafeInteger(row.limit) || Number(row.limit) <= 0 || Number(row.limit) > 2147483647)) throw new Error("Channel limit must be a positive native integer.");
    if (row.min_score !== undefined && (typeof row.min_score !== "number" || !Number.isFinite(row.min_score))) throw new Error("Channel threshold must be finite.");
    if (Object.keys(row).some(key => !["id", "limit", "min_score"].includes(key))) throw new Error("This native channel API supports id, limit and min_score; method weights require the method registry API.");
  }
  return value as CandidateChannelRequest[];
}

export function readChatSettings(key: string): { value: Record<string, unknown>; error?: string } {
  try {
    const raw = localStorage.getItem(`loom.chat-settings.${key}`);
    if (raw === null) return { value: {} };
    const parsed: unknown = JSON.parse(raw);
    if (!parsed || typeof parsed !== "object" || Array.isArray(parsed) || (parsed as Record<string, unknown>).schema !== "loom.chat_settings/1") throw new Error("Stored chat settings have an unsupported shape.");
    const value = (parsed as Record<string, unknown>).value;
    if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("Stored chat settings are not an object.");
    const row = value as Record<string, unknown>;
    for (const field of ["model", "contextQuery", "contextProject", "contextTargets", "contextRun", "contextLanguage", "contextBudget", "contextHops", "channels", "scanLimit"]) {
      if (row[field] !== undefined && typeof row[field] !== "string") throw Error(`Stored ${field} has an invalid type.`);
    }
    for (const field of ["useKnowledge", "usePlan", "includeMemory", "includeGraphMemory", "includeHistory", "counterEvidence", "lexicalShadow"]) {
      if (row[field] !== undefined && typeof row[field] !== "boolean") throw Error(`Stored ${field} has an invalid type.`);
    }
    if (row.contextDetail !== undefined && (typeof row.contextDetail !== "string" || !["auto", "label", "summary", "full", "raw"].includes(row.contextDetail))) throw Error("Stored context detail is invalid.");
    if (row.traceContext !== undefined && (typeof row.traceContext !== "string" || !["auto", "on", "off"].includes(row.traceContext))) throw Error("Stored context recording is invalid.");
    storedPlan(row.planDraft);
    return { value: row };
  } catch (error) { return { value: {}, error: `${error instanceof Error ? error.message : String(error)} Original stored data has been preserved.` }; }
}

export function writeChatSettings(key: string, value: Record<string, unknown>): void {
  localStorage.setItem(`loom.chat-settings.${key}`, JSON.stringify({ schema: "loom.chat_settings/1", value }));
}
