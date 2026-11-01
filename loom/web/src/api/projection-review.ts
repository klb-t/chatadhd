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
  archive_preview?: ArchivePreview;
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
  const archive = projection.method.format === "zip" || projection.coordinate === "archive_members";
  if (archive && receipt) throw Error("archive_projection_upgrade_unsupported");
  if (archive && (projection.method.format !== "zip" || projection.coordinate !== "archive_members" || projection.occurrence_nodes.length || selectedNodes.length)) invalid();
  if (!id(projection.projection_node) || !ids(projection.occurrence_nodes) || !ids(selectedNodes) ||
      selectedNodes.some(n => !projection.occurrence_nodes.includes(n))) throw Error("projection_selection_not_owned");
  if (receipt && (receipt.old_projection !== projection.projection_node || !hash(receipt.old_receipt_sha256))) invalid();
  return { operation: receipt ? "projection_reproject" : "projection_check", projection_node: projection.projection_node,
    selected_nodes: [...selectedNodes], ...(receipt ? { expected_receipt_sha256: receipt.old_receipt_sha256 } : {}) };
}

export interface ArchivePreview {
  schema: "loom.archive_projection_preview/1";
  root: { node_id: string; source_id: string; sha256: string; locator: Record<string, unknown> };
  structure_nodes: ArchiveRow[];
  children: ArchiveChild[];
  manifest_verified: true;
  runtime: Record<string, unknown>;
  descriptor_sha256: string;
  mapping: { status: "not_exercised"; candidates: []; reason: "archive_tree_migration_not_implemented" };
  reproject_supported: false;
  limits: string[];
}
export interface ArchiveRow {
  node_id: string; parent: string; kind: "folder" | "file"; title: string; locator: Record<string, unknown>;
  sha256?: string; source_id?: string; source_key?: string; provenance_id?: string; archive_sha256?: string;
}
export interface ArchiveChild {
  member_node: string; member_source: string; original_sha256: string; locator: Record<string, unknown>;
  projection?: SourceParts; review?: ProjectionReview;
  current_replay?: "not_exercised"; reason?: "format_not_admitted";
}

// Object-key order is not identity; IDs, member locators and hashes are.
const canonical = (v: unknown): string => JSON.stringify(v, (_, value) => object(value)
  ? Object.fromEntries(Object.entries(value).sort(([a], [b]) => a.localeCompare(b))) : value);
function archivePreview(value: unknown, projection: SourceParts, ancestors: Set<string>): ArchivePreview {
  if (!object(value) || !exactKeys(value, ["schema", "root", "structure_nodes", "children", "manifest_verified", "runtime", "descriptor_sha256", "mapping", "reproject_supported", "limits"]) ||
      value.schema !== "loom.archive_projection_preview/1" || value.manifest_verified !== true || value.reproject_supported !== false ||
      !object(value.runtime) || canonical(value.runtime) !== canonical(projection.method.runtime) || !hash(value.descriptor_sha256) ||
      !ids(value.limits) || !object(value.mapping) || !exactKeys(value.mapping, ["status", "candidates", "reason"]) ||
      value.mapping.status !== "not_exercised" || value.mapping.reason !== "archive_tree_migration_not_implemented" ||
      !Array.isArray(value.mapping.candidates) || value.mapping.candidates.length ||
      !object(value.root) || !exactKeys(value.root, ["node_id", "source_id", "sha256", "locator"]) ||
      value.root.node_id !== projection.source_node || value.root.source_id !== projection.source_id ||
      value.root.sha256 !== projection.source_sha256 || !hash(value.root.sha256) || !object(value.root.locator) ||
      value.root.locator.source !== projection.source_id || value.root.locator.member !== "" ||
      !Array.isArray(value.structure_nodes) || !Array.isArray(value.children) ||
      !Array.isArray(projection.structure_nodes) || !Array.isArray(projection.children)) invalid();
  const root = value.root;
  const known = new Map(projection.structure_nodes.map(n => [n.node_id, n]));
  if (known.size !== projection.structure_nodes.length || known.size !== value.structure_nodes.length) invalid();
  const visited = new Map<string, string>([[projection.source_node, "archive"]]);
  const files = new Map<string, ArchiveRow>();
  for (const row of value.structure_nodes as unknown[]) {
    const common = ["node_id", "parent", "kind", "title", "locator"];
    if (!object(row) || !id(row.node_id) || !id(row.parent) || visited.has(row.node_id) ||
        !["archive", "folder"].includes(visited.get(row.parent) ?? "") || !["folder", "file"].includes(String(row.kind)) ||
        !id(row.title) || !object(row.locator) || row.locator.source !== root.source_id || !id(row.locator.member)) invalid();
    const isFile = row.kind === "file";
    if (!exactKeys(row, isFile ? [...common, "sha256", "source_id", "source_key", "provenance_id", "archive_sha256"] : common)) invalid();
    const expected = known.get(row.node_id);
    if (!expected || expected.parent !== row.parent || expected.kind !== row.kind || expected.title !== row.title ||
        canonical(expected.locator) !== canonical(row.locator)) invalid();
    if (isFile && (!hash(row.sha256) || !id(row.source_id) || !id(row.source_key) || !id(row.provenance_id) ||
        row.archive_sha256 !== root.sha256 || (expected as unknown as Record<string, unknown>).sha256 !== row.sha256)) invalid();
    visited.set(row.node_id, String(row.kind));
    if (isFile) files.set(row.node_id, row as unknown as ArchiveRow);
  }
  const saved = new Map<string, Record<string, unknown>>();
  for (const raw of projection.children as unknown[]) {
    if (!object(raw) || !id(raw.source_id) || saved.has(raw.source_id)) invalid();
    saved.set(raw.source_id, raw);
  }
  if (value.children.length !== files.size || saved.size !== files.size) invalid();
  const joined = new Set<string>(), sources = new Set<string>();
  for (const raw of value.children as unknown[]) {
    if (!object(raw) || !id(raw.member_node) || joined.has(raw.member_node) || !id(raw.member_source) ||
        !hash(raw.original_sha256) || !object(raw.locator)) invalid();
    const member = files.get(raw.member_node);
    if (!member || member.source_id !== raw.member_source || member.sha256 !== raw.original_sha256 ||
        canonical(member.locator) !== canonical(raw.locator)) invalid();
    joined.add(raw.member_node);
    const common = ["member_node", "member_source", "original_sha256", "locator"];
    if (raw.projection !== undefined) {
      if (!exactKeys(raw, [...common, "projection", "review"]) || !object(raw.projection) || !id(raw.projection.source_id) ||
          !id(raw.projection.projection_node) || !ids(raw.projection.occurrence_nodes) || !object(raw.projection.method) ||
          ancestors.has(raw.projection.projection_node) || sources.has(raw.projection.source_id)) invalid();
      const child = saved.get(raw.projection.source_id);
      if (!child || canonical(child) !== canonical(raw.projection) ||
          (object(child.derivation) ? child.derivation.original_sha256 : child.source_sha256) !== raw.original_sha256) invalid();
      sources.add(raw.projection.source_id);
      readProjectionReview(raw.review, { operation: "projection_check", projection_node: raw.projection.projection_node, selected_nodes: [] },
        raw.projection as unknown as SourceParts, ancestors);
    } else {
      if (!exactKeys(raw, [...common, "current_replay", "reason"]) || raw.current_replay !== "not_exercised" || raw.reason !== "format_not_admitted" || sources.has(raw.member_source)) invalid();
      const child = saved.get(raw.member_source);
      if (!child || !exactKeys(child, ["source_id", "status", "reason", "member"]) || child.status !== "retained_original" ||
          child.reason !== raw.reason || child.member !== raw.locator.member) invalid();
      sources.add(raw.member_source);
    }
  }
  return value as unknown as ArchivePreview;
}

/** Validate the native v1 response against the operation, receipt and exact selected-node set. */
export function readProjectionReview(value: unknown, request: ProjectionRequest, projection: SourceParts, ancestors = new Set<string>()): ProjectionReview | ProjectionUpgrade {
  const baseKeys = ["schema", "old_projection", "old_receipt_sha256", "saved_integrity", "current_replay", "refusals", "selection_scope", "selection_changed"];
  const upgradeKeys = ["new_projection", "new_receipt_sha256", "new_source", "mapping", "activation", "rollback", "upgrade_node"];
  const upgrading = request.operation === "projection_reproject";
  const archive = projection.method.format === "zip" || projection.coordinate === "archive_members";
  if (archive && (upgrading || projection.method.format !== "zip" || projection.coordinate !== "archive_members" || projection.occurrence_nodes.length)) invalid();
  if (ancestors.has(projection.projection_node)) invalid();
  const path = new Set(ancestors).add(projection.projection_node);
  if (!object(value) || !exactKeys(value, upgrading ? [...baseKeys, ...upgradeKeys] : archive ? [...baseKeys, "archive_preview"] : baseKeys) ||
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
  if (archive) archivePreview(value.archive_preview, projection, path);
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
