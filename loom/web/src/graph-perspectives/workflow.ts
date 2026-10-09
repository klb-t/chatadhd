import { buildRetrievalPlan, type PlanDraft, type ThesisDraft } from "../context/retrieval-plan";
import type { ChatContextPlan } from "../api/types";
import type { ObjectRef, PermissionContext, Perspective, QueryPlan } from "./types";

export interface NativeAnalysisAnchor { kind: "entity" | "claim"; id: string }
export interface AnalysisExportOptions {
  id: string;
  thesisId: string;
  text: string;
  /** Native ChatContextPlan anchors are nonexclusive; exact membership is unsupported. */
  selectionMode: "anchors" | "exact";
  permission: PermissionContext;
  /** Host verifies that this is a native ID. canonicalId alone is not such proof. */
  resolveAnchor(ref: ObjectRef): NativeAnalysisAnchor | undefined;
  relationHops?: number;
  detailResolution?: ThesisDraft["detail"];
  requireCounterEvidence: boolean;
  budgetWeight: number;
  queryPlan?: QueryPlan;
}
export interface AnalysisExport {
  schema: "loom.graph_perspective_analysis_export/1";
  status: "ready" | "partial" | "blocked";
  plan: ChatContextPlan | null;
  selected: ObjectRef[];
  unsupported: { ref: ObjectRef; reason: string }[];
  limitations: string[];
  permissionId: string;
  executed: false;
  selectionMode: "anchors" | "exact";
  blockedReason?: string;
  /** Local review sidecar only, never inserted wholesale into the native plan. */
  sourcePlan?: QueryPlan;
}
/** Preparation only: no transport, mutation, approval change or implicit visible→analysis selection. */
export function preparePerspectiveAnalysis(perspective: Perspective, options: AnalysisExportOptions): AnalysisExport {
  if (!["anchors", "exact"].includes(options.selectionMode)) throw new Error("analysis_selection_mode_required");
  if (options.detailResolution !== undefined && !["inherit", "label", "summary", "full", "raw"].includes(options.detailResolution)) throw new Error("unsupported_native_analysis_resolution");
  if (typeof options.requireCounterEvidence !== "boolean") throw new Error("invalid_native_counter_evidence_choice");
  const selected: ObjectRef[] = JSON.parse(JSON.stringify(perspective.analysis?.selected ?? []));
  const unsupported: AnalysisExport["unsupported"] = [];
  const permitted: ObjectRef[] = [];
  const entities = new Set<string>(), claims = new Set<string>();
  for (const ref of selected) {
    if (!options.permission.canRead(ref)) { unsupported.push({ ref, reason: "permission_denied" }); continue; }
    permitted.push(ref);
    const anchor = options.resolveAnchor(ref);
    if (!anchor || !["entity", "claim"].includes(anchor.kind) || typeof anchor.id !== "string" || !anchor.id || anchor.id.trim() !== anchor.id || /[\r\n]/.test(anchor.id)) {
      unsupported.push({ ref, reason: "native_anchor_unavailable" }); continue;
    }
    (anchor.kind === "entity" ? entities : claims).add(anchor.id);
  }
  const limitations = ["prepared_not_executed", "permission_revalidation_required_at_execution", "presentation_mappings_not_analysis_policy", "external_reference_hydration_requires_host_adapter",
    "native_anchors_are_not_membership_allowlist", "native_context_can_include_principles_preferences_dependencies_and_other_candidates"];
  const result: AnalysisExport = { schema: "loom.graph_perspective_analysis_export/1", status: "blocked", plan: null, selected, unsupported, limitations, permissionId: options.permission.id, executed: false,
    selectionMode: options.selectionMode,
    ...(options.queryPlan && { sourcePlan: JSON.parse(JSON.stringify(options.queryPlan)) as QueryPlan }) };
  if (options.selectionMode === "exact") return { ...result, blockedReason: "exact_membership_not_supported_by_native_plan" };
  // Explicitly empty selection must not inherit the native ambient target scope.
  if (!entities.size && !claims.size) return result;
  const provenance = {
    schema: result.schema, perspective_id: perspective.id, permission_id: options.permission.id,
    selection_policy: "explicit_analysis_anchors", selection_mode: options.selectionMode, selected: permitted,
    unsupported: unsupported.filter(item => item.reason !== "permission_denied"),
    denied_count: unsupported.filter(item => item.reason === "permission_denied").length,
    analysis_policy_ref: perspective.analysis?.policyRef ?? null,
    perspective_link_declared: perspective.analysis?.linkedToPerspective === true,
    query_plan: options.queryPlan ? { schema: options.queryPlan.schema, perspectiveId: options.queryPlan.perspectiveId } : null, limitations,
  };
  const draft: PlanDraft = { id: options.id, sourceRef: JSON.stringify(provenance), theses: [{
    key: 1, id: options.thesisId, text: options.text, targetsMode: "custom", targets: [...entities].join("\n"),
    claimsMode: "custom", claims: [...claims].join("\n"), hops: options.relationHops === undefined ? "" : String(options.relationHops),
    detail: options.detailResolution ?? "inherit", counter: options.requireCounterEvidence, weight: String(options.budgetWeight),
  }] };
  result.plan = buildRetrievalPlan(draft);
  result.status = unsupported.length ? "partial" : "ready";
  return result;
}
