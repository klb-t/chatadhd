import type { ChatContextPlan, ChatContextThesis } from "../api/types";

export interface ThesisDraft {
  key: number;
  id: string;
  text: string;
  targetsMode: "inherit" | "custom";
  targets: string;
  claimsMode: "inherit" | "custom";
  claims: string;
  hops: string;
  detail: "inherit" | "label" | "summary" | "full" | "raw";
  counter: boolean;
  weight: string;
}
export interface PlanDraft { id: string; sourceRef: string; theses: ThesisDraft[] }
export function newThesis(key: number): ThesisDraft {
  return { key, id: `thesis-${key}`, text: "", targetsMode: "inherit", targets: "", claimsMode: "inherit", claims: "", hops: "", detail: "inherit", counter: true, weight: "1" };
}
export function newPlan(): PlanDraft { return { id: "chat-plan", sourceRef: "", theses: [newThesis(1)] }; }

function anchorIds(text: string, label: string): string[] {
  // One ID per line preserves spaces within native IDs. Blank means clear.
  const ids = text.split(/\r?\n/).map((id) => id.trim()).filter(Boolean);
  if (new Set(ids).size !== ids.length) throw new Error(`${label}: duplicate anchor IDs.`);
  return ids;
}

export function buildRetrievalPlan(draft: PlanDraft): ChatContextPlan {
  if (!draft.id.trim()) throw new Error("Retrieval plan needs a non-empty ID.");
  if (!draft.theses.length) throw new Error("Retrieval plan needs at least one thesis.");
  const ids = new Set<string>();
  const theses = draft.theses.map((item, index): ChatContextThesis => {
    const label = `Thesis ${index + 1}`;
    if (!item.id.trim() || !item.text.trim()) throw new Error(`${label}: ID and query text are required.`);
    if (ids.has(item.id)) throw new Error(`${label}: duplicate thesis ID.`);
    ids.add(item.id);
    const weight = Number(item.weight);
    if (!item.weight.trim() || !Number.isFinite(weight) || weight <= 0) throw new Error(`${label}: budget weight must be a positive finite number.`);
    const hops = item.hops.trim() ? Number(item.hops) : undefined;
    if (hops !== undefined && (!Number.isSafeInteger(hops) || hops < 0 || hops > 2147483647)) throw new Error(`${label}: graph reach must be a non-negative whole number within the supported integer range.`);
    return {
      id: item.id, text: item.text,
      ...(item.targetsMode === "custom" && { targets: anchorIds(item.targets, label) }),
      ...(item.claimsMode === "custom" && { claims: anchorIds(item.claims, label) }),
      ...(hops !== undefined && { relation_hops: hops }),
      ...(item.detail !== "inherit" && { detail_resolution: item.detail }),
      require_counter_evidence: item.counter,
      budget_weight: weight,
    };
  });
  let sourceRef: unknown;
  if (draft.sourceRef.trim()) {
    try { sourceRef = JSON.parse(draft.sourceRef); }
    catch { throw new Error("Plan source reference must be valid JSON, or left empty."); }
  }
  return { id: draft.id, ...(draft.sourceRef.trim() && { source_ref: sourceRef }), theses };
}
