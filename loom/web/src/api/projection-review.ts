import type { ActionsApi } from "./loom-api";
import type { SourceParts } from "./text-sources";
import { sourceSelector } from "./text-sources-state.mjs";

export interface ProjectionRequest {
  operation: "projection_check" | "projection_reproject";
  projection_node: string;
  selected_nodes: string[];
  expected_receipt_sha256?: string;
}
export interface ProjectionReview {
  schema: "loom.projection_upgrade/1";
  old_projection: string;
  old_receipt_sha256: string;
  saved_integrity: "verified";
  current_replay: "compatible" | "refused" | "not_exercised";
  refusals: { node_id: string; code: string }[];
  selection_scope: "account_global";
  selection_changed: false;
}
export interface ProjectionUpgrade extends ProjectionReview {
  new_projection: string;
  new_receipt_sha256: string;
  new_source: string;
  mapping: { old_node: string; candidates: string[]; status: "exact_coordinate_proposal" | "decision_required" }[];
  activation: "explicit_selector_update_required";
  rollback: "old_projection_retained_restore_precopy_for_old_runtime_if_replay_refused";
  upgrade_node: string;
}
export interface CheckedProjection {
  receipt: ProjectionReview;
  selectedNodes: string[];
  selectorSnapshot: string;
}
type Host = { actions?: Pick<ActionsApi, "clientBlobText">; getConfig(): Promise<Record<string, unknown>> };
const object = (v: unknown): v is Record<string, unknown> => v !== null && typeof v === "object" && !Array.isArray(v);
const hash = (v: unknown): v is string => typeof v === "string" && /^[a-f0-9]{64}$/.test(v);
const id = (v: unknown): v is string => typeof v === "string" && v.length > 0;
const ids = (v: unknown): v is string[] => Array.isArray(v) && v.every(id) && new Set(v).size === v.length;
const exactKeys = (v: Record<string, unknown>, keys: string[]) => Object.keys(v).length === keys.length && keys.every(k => Object.prototype.hasOwnProperty.call(v, k));
function invalid(): never { throw Error("projection_review_invalid"); }
const active = (signal?: AbortSignal) => { if (signal?.aborted) throw Error("projection_review_aborted"); };

/** Only owned occurrences go to the native operation. The account-wide selector is never written. */
export function projectionRequest(projection: SourceParts, selectedNodes: string[], receipt?: ProjectionReview): ProjectionRequest {
  if (projection.method.format === "zip" || projection.coordinate === "archive_members") throw Error("archive_projection_upgrade_unsupported");
  if (!id(projection.projection_node) || !ids(projection.occurrence_nodes) || !ids(selectedNodes) ||
      selectedNodes.some(n => !projection.occurrence_nodes.includes(n))) throw Error("projection_selection_not_owned");
  if (receipt && (receipt.old_projection !== projection.projection_node || !hash(receipt.old_receipt_sha256))) invalid();
  return { operation: receipt ? "projection_reproject" : "projection_check", projection_node: projection.projection_node,
    selected_nodes: [...selectedNodes], ...(receipt ? { expected_receipt_sha256: receipt.old_receipt_sha256 } : {}) };
}

/** Validate the native v1 response against the operation, receipt and exact selected-node set. */
export function readProjectionReview(value: unknown, request: ProjectionRequest, projection: SourceParts): ProjectionReview | ProjectionUpgrade {
  const baseKeys = ["schema", "old_projection", "old_receipt_sha256", "saved_integrity", "current_replay", "refusals", "selection_scope", "selection_changed"];
  const upgradeKeys = ["new_projection", "new_receipt_sha256", "new_source", "mapping", "activation", "rollback", "upgrade_node"];
  const upgrading = request.operation === "projection_reproject";
  if (!object(value) || !exactKeys(value, upgrading ? [...baseKeys, ...upgradeKeys] : baseKeys) ||
      value.schema !== "loom.projection_upgrade/1" || value.old_projection !== request.projection_node ||
      value.old_projection !== projection.projection_node || !hash(value.old_receipt_sha256) ||
      (upgrading && value.old_receipt_sha256 !== request.expected_receipt_sha256) ||
      value.saved_integrity !== "verified" || value.selection_scope !== "account_global" || value.selection_changed !== false ||
      typeof value.current_replay !== "string" || !["compatible", "refused", "not_exercised"].includes(value.current_replay) || !Array.isArray(value.refusals)) invalid();
  const seen = new Set<string>();
  for (const refusal of value.refusals as unknown[]) {
    if (!object(refusal) || !exactKeys(refusal, ["node_id", "code"]) || !id(refusal.node_id) || !id(refusal.code) ||
        !projection.occurrence_nodes.includes(refusal.node_id) || seen.has(refusal.node_id)) invalid();
    seen.add(refusal.node_id);
  }
  if ((value.current_replay === "not_exercised") !== (projection.occurrence_nodes.length === 0) ||
      (value.current_replay === "refused") !== (seen.size > 0)) invalid();
  if (upgrading) {
    if (!id(value.new_projection) || !hash(value.new_receipt_sha256) || !id(value.new_source) || !id(value.upgrade_node) ||
        value.activation !== "explicit_selector_update_required" ||
        value.rollback !== "old_projection_retained_restore_precopy_for_old_runtime_if_replay_refused" ||
        !Array.isArray(value.mapping) || value.mapping.length !== request.selected_nodes.length) invalid();
    const mapped = new Set<string>();
    for (const entry of value.mapping as unknown[]) {
      if (!object(entry) || !exactKeys(entry, ["old_node", "candidates", "status"]) || !id(entry.old_node) ||
          !request.selected_nodes.includes(entry.old_node) || mapped.has(entry.old_node) || !ids(entry.candidates) ||
          entry.status !== (entry.candidates.length === 1 ? "exact_coordinate_proposal" : "decision_required")) invalid();
      mapped.add(entry.old_node);
    }
  }
  return value as unknown as ProjectionReview | ProjectionUpgrade;
}

/** Bind readback of the resulting receipt to its source and the candidate occurrence IDs. */
export function validateUpgradeTarget(upgrade: ProjectionUpgrade, projection: SourceParts): void {
  if (projection.projection_node !== upgrade.new_projection || projection.source_id !== upgrade.new_source ||
      !ids(projection.occurrence_nodes) || upgrade.mapping.some(m => m.candidates.some(n => !projection.occurrence_nodes.includes(n)))) invalid();
}

async function selection(host: Host, projection: SourceParts, signal?: AbortSignal) {
  active(signal);
  const config = await host.getConfig();
  active(signal);
  const selector = sourceSelector(config);
  return { selectedNodes: [...new Set(selector.ids)].filter(n => projection.occurrence_nodes.includes(n)),
    selectorSnapshot: JSON.stringify(selector) };
}

async function send(host: Host, request: ProjectionRequest, projection: SourceParts, signal?: AbortSignal) {
  active(signal);
  if (!host.actions?.clientBlobText) throw Error("projection_review_transport_unavailable");
  // HTTP and JNI already forward JSON to the same native materialize dispatcher.
  // The shared TS signature describes only ordinary materialization; widen locally, never add fake hash/name fields.
  const action = host.actions.clientBlobText as unknown as (request: ProjectionRequest) => Promise<unknown>;
  const response = await action.call(host.actions, request);
  // This legacy action has no transport AbortSignal. Aborting suppresses stale completions;
  // a write already dispatched may still finish on the server and must not be retried automatically.
  active(signal);
  return readProjectionReview(response, request, projection);
}

export async function checkProjection(host: Host, projection: SourceParts, signal?: AbortSignal): Promise<CheckedProjection> {
  const selected = await selection(host, projection, signal);
  const receipt = await send(host, projectionRequest(projection, selected.selectedNodes), projection, signal);
  return { receipt, ...selected };
}

/** Calling this requires the UI's explicit confirmation of this checked receipt. No automatic selector activation. */
export async function reprojectProjection(host: Host, projection: SourceParts, checked: CheckedProjection, signal?: AbortSignal): Promise<ProjectionUpgrade> {
  const fresh = await selection(host, projection, signal);
  if (fresh.selectorSnapshot !== checked.selectorSnapshot || JSON.stringify(fresh.selectedNodes) !== JSON.stringify(checked.selectedNodes))
    throw Error("projection_review_selection_stale");
  return await send(host, projectionRequest(projection, checked.selectedNodes, checked.receipt), projection, signal) as ProjectionUpgrade;
}
