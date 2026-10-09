import type { GraphData, GraphNode } from "../api/types";
import type { Workspace } from "../workspace/state";

/** References remain addressable before projection, loading or rendering. */
export interface ObjectRef {
  source: string;
  selector: string;
  canonicalId?: string;
  representation?: string;
  snapshot?: string;
  [key: string]: unknown;
}
export type Availability = "available" | "unloaded" | "unavailable" | "unknown" | "denied";
export type EvidenceStatus = "source" | "computed" | "inferred" | "unknown" | string;
export interface SourceObject {
  ref: ObjectRef;
  label: string;
  kind: string;
  status: Availability;
  evidence: EvidenceStatus;
  properties?: Record<string, unknown>;
  confidence?: number;
  reason?: string;
  /** Related representations explicitly asserted by the adapter, never guessed. */
  representations?: ObjectRef[];
  [key: string]: unknown;
}
export interface SourceRelation {
  id: string;
  from: ObjectRef;
  to: ObjectRef;
  kind: string;
  evidence: EvidenceStatus;
  basis?: unknown;
  properties?: Record<string, unknown>;
  [key: string]: unknown;
}
export interface StructureDescriptor {
  id: string;
  label: string;
  relations: string[];
  directions?: Direction[];
  description?: string;
  [key: string]: unknown;
}
export interface CapabilityDescriptor {
  id: string;
  label: string;
  /** Open capability id, bound to one universal operation understood by a compiler/adapter. */
  target: string;
  status: "supported" | "limited" | "unsupported";
  reason?: string;
  options?: unknown[];
  [key: string]: unknown;
}
export interface SourceDescriptor {
  id: string;
  label: string;
  structures: StructureDescriptor[];
  capabilities?: CapabilityDescriptor[];
  status?: Availability;
  [key: string]: unknown;
}
export type Direction = "incoming" | "outgoing" | "both";
export interface ReadContext {
  signal?: AbortSignal;
  /** Explicit caller authorization; changing a perspective never changes this. */
  permission: PermissionContext;
}
export interface NeighborRequest {
  structure: string;
  relations: string[];
  direction: Direction;
  snapshot?: string;
  cursor?: string;
  limit: number;
  /** Adapter-defined capability targets pass through without a core schema change. */
  parameters?: Record<string, unknown>;
}
export interface NeighborPage {
  relations: SourceRelation[];
  nextCursor?: string;
  total?: number;
  status?: Availability;
  reason?: string;
}
export interface SourceAdapter {
  descriptor: SourceDescriptor;
  resolve(ref: ObjectRef, context: ReadContext): Promise<SourceObject>;
  neighbors(ref: ObjectRef, request: NeighborRequest, context: ReadContext): Promise<NeighborPage>;
}
export interface PermissionContext {
  id: string;
  canRead(ref: ObjectRef): boolean;
}
/** R40 resolution is supplied by the existing native layered-defaults service. */
export interface EffectiveComponent {
  id: string;
  value?: unknown;
  status: "effective" | "disabled" | "excluded" | "proposal" | "missing" | string;
  origin?: unknown;
  reason?: string;
  [key: string]: unknown;
}
export interface NativeResolution {
  schema?: string;
  components: EffectiveComponent[];
  provenance?: unknown;
  [key: string]: unknown;
}
export interface Perspective {
  schema: "loom.graph_perspective/1";
  id: string;
  label?: string;
  /** References/intent only. Never resolve layers or exclusions in React. */
  components: Record<string, unknown>;
  focus: ObjectRef | null;
  /** Optional existing workspace preserved alongside this additive extension. */
  workspace?: Workspace;
  analysis?: { selected: ObjectRef[]; policyRef?: string; linkedToPerspective?: boolean };
  [key: string]: unknown;
}
export interface LocalResolutionRule {
  id: string;
  match: { focus?: boolean; distance?: number; kind?: string; source?: string; selectors?: string[] };
  resolution: string;
  aggregate?: { id: string; label: string };
  [key: string]: unknown;
}
export interface VisualRule {
  id: string;
  dimension: "relevance" | "time" | "uncertainty" | string;
  match: { evidence?: string; status?: Availability; kind?: string; snapshot?: string; distance?: number; focus?: boolean };
  style: { color?: string; opacity?: number; blur?: number; group?: string; label?: string };
  explanation: string;
  [key: string]: unknown;
}
export interface QueryPlan {
  schema: "loom.graph_perspective_plan/1";
  perspectiveId: string;
  focus: ObjectRef | null;
  structure: string;
  relations: string[];
  direction: Direction;
  hops: number;
  resolution: string;
  localResolution: LocalResolutionRule[];
  snapshot?: string;
  compareSnapshots: string[];
  evidence: string[];
  queryBudget: number;
  renderBudget: number;
  pageSize: number;
  visual: VisualRule[];
  values: Record<string, unknown>;
  explanation: EffectiveComponent[];
  unsupported: { id: string; reason: string; value?: unknown }[];
  errors: string[];
  sourcePerspective: Perspective;
}
export interface ProjectedObject extends SourceObject {
  key: string;
  canonicalKey: string;
  distance: number;
  resolution: string;
  visual: { opacity: number; blur: number; color?: string; group?: string; label?: string };
  visualReasons: { dimension: string; rule: string; explanation: string }[];
  aggregateMembers?: ObjectRef[];
}
export interface Omission {
  ref?: ObjectRef;
  reason: "render_budget" | "query_budget" | "evidence_filter" | "aggregate" | "permission" | "adapter_missing" | "source_status";
  count?: number;
  detail?: string;
}
export interface SelectionResult {
  plan: QueryPlan;
  objects: ProjectedObject[];
  relations: SourceRelation[];
  graph: GraphData;
  /** Distinct from visible objects; view selection never changes analysis membership. */
  analysis: ObjectRef[];
  permissionId: string;
  omissions: Omission[];
  continuation: { ref: ObjectRef; cursor?: string; distance: number }[];
  complete: boolean;
  metrics: { selectionMs: number; resolvedObjects: number; neighborCalls: number; discoveredObjects: number; renderedObjects: number };
}
export interface NavigationEntry { ref: ObjectRef; structure: string; snapshot?: string }
export interface NavigationState { current: NavigationEntry; back: NavigationEntry[]; forward: NavigationEntry[] }
export type PresentationNode = GraphNode & { perspective: ProjectedObject };
