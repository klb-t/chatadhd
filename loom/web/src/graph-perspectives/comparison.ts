import { referenceKey, objectKey } from "./navigation";
import type { ObjectRef, Omission, Perspective, ProjectedObject, QueryPlan, SelectionResult } from "./types";

export interface PerspectiveDifference { path: string; kind: "added" | "removed" | "changed"; before?: unknown; after?: unknown }
const container = (value: unknown): value is Record<string, unknown> => !!value && typeof value === "object" && !Array.isArray(value);
const escape = (value: string) => value.replace(/~/g, "~0").replace(/\//g, "~1");
function differences(before: unknown, after: unknown, path = ""): PerspectiveDifference[] {
  if (JSON.stringify(before) === JSON.stringify(after)) return [];
  if (container(before) && container(after)) return [...new Set([...Object.keys(before), ...Object.keys(after)])].sort().flatMap(key => {
    const next = `${path}/${escape(key)}`;
    if (!Object.prototype.hasOwnProperty.call(before, key)) return [{ path: next, kind: "added", after: after[key] } as PerspectiveDifference];
    if (!Object.prototype.hasOwnProperty.call(after, key)) return [{ path: next, kind: "removed", before: before[key] } as PerspectiveDifference];
    return differences(before[key], after[key], next);
  });
  return [{ path, kind: "changed", before, after }];
}
export function comparePerspectives(before: Perspective, after: Perspective): PerspectiveDifference[] { return differences(before, after); }
export function comparePlans(before: QueryPlan, after: QueryPlan): PerspectiveDifference[] { return differences(before, after); }
export interface VisibilityExplanation {
  state: "visible" | "represented" | "omitted" | "unresolved" | "outside_selection";
  exact: ProjectedObject[];
  representations: ProjectedObject[];
  omissions: Omission[];
  complete: boolean;
  reason: string;
}
/** Reports recorded selection evidence; absence from a partial result is never a claim of nonexistence. */
export function explainVisibility(result: SelectionResult, ref: ObjectRef): VisibilityExplanation {
  const key = referenceKey(ref), canonical = objectKey(ref);
  const exact = result.objects.filter(item => referenceKey(item.ref) === key);
  const representations = result.objects.filter(item => !exact.includes(item) && (objectKey(item.ref) === canonical || item.aggregateMembers?.some(member => referenceKey(member) === key)));
  const omissions = result.omissions.filter(item => item.ref ? referenceKey(item.ref) === key || objectKey(item.ref) === canonical : true);
  if (exact.length) return { state: "visible", exact, representations, omissions, complete: result.complete, reason: "included_by_selection_plan" };
  if (representations.length) return { state: "represented", exact, representations, omissions, complete: result.complete, reason: "visible_representation_or_aggregate" };
  if (omissions.some(item => item.ref && (referenceKey(item.ref) === key || objectKey(item.ref) === canonical))) return { state: "omitted", exact, representations, omissions, complete: result.complete, reason: "recorded_selection_omission" };
  return { state: result.complete ? "outside_selection" : "unresolved", exact, representations, omissions, complete: result.complete, reason: result.complete ? "not_returned_by_this_selection" : "selection_incomplete_no_absence_claim" };
}
