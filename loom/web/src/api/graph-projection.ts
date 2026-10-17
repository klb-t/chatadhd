// M1-K0: the graph projection (loom/src/graph/projection.h) - the ROM, settings, You, conversations, flows, roles, perspectives,
// devices, environments ... as nodes of one navigable graph, computed by the kernel on demand (nothing is written to the graph).
// Reached through loom_invoke("actions", ...) / POST /api/graph/navigate | /api/graph/card | /api/settings/resolved |
// /api/roles/resolved | /api/rom/override (routes from the action catalogue). Types only; the navigator UI is M1-W0.

/** declared (ROM, presets) | observed (what you said or did) | derived | inferred (an analysis or model concluded it) | extrapolated. */
export type EvidenceClass = "declared" | "observed" | "derived" | "inferred" | "extrapolated";

export interface GraphEvidence {
  class: EvidenceClass | string;
  confidence: number | null;
  provenance: { layer: string; source_ref: string; method: string; time: string | null };
}

export interface GraphChip {
  id: string;
  label: string;
}

export interface GraphProvenance {
  layer: string;
  layers: string[];
  scope?: string;
  chip?: GraphChip;
}

export interface GraphNodeRow {
  address: string;
  label: string;
  kind: string;
  source?: string;
  value?: unknown;
  value_human?: string;
  /** Children of this node; absent with count_unknown_reason when no source can say (never a made-up 0). */
  count?: number;
  count_unknown_reason?: string;
  count_partial?: { source: string; reason: string }[];
  provenance: GraphProvenance;
  evidence: GraphEvidence;
  evidence_overrides?: Record<string, GraphEvidence>;
  has_children: boolean;
  status?: string;
  why?: string;
  order?: number;
  same_as?: string[];
  depth?: number;
  /** An area row: the graph entry counters of the area. */
  counts?: Record<string, unknown>;
  empty_reason?: string;
}

export interface GraphNavigateRequest {
  focus?: string;
  perspectives?: string[];
  hops?: number;
  /** Children per level; default the setting navigator_children_limit (0 = no limit). */
  limit?: number;
  query?: string;
  offset?: number;
  before?: string;
  lang?: string;
  conv_id?: string;
}

export interface GraphEdgeRow {
  src: string;
  dst: string;
  kind: string;
  /** The edge kind's meaning in the language asked; null when no kind describes it. */
  meaning: string | null;
}

export interface GraphNavigateResult {
  schema: "loom.graph_navigate/1";
  focus: {
    address: string;
    label: string;
    kind: string;
    evidence: GraphEvidence;
    provenance: GraphProvenance;
    why?: string | null;
    value?: unknown;
    value_human?: string | null;
  };
  children: GraphNodeRow[];
  edges: GraphEdgeRow[];
  breadcrumbs: { address: string; label: string }[];
  counts: { total: number; shown: number; collapsed: number };
  hidden_by_you: { count: number; items: { source: string; id: string; by?: unknown }[] };
  unknown: { source: string; reason: string }[];
  pages: Record<string, { limit?: number; offset?: number; next_offset?: number; before?: string; next_before?: string; total?: number }>;
  limit: { value: number; from: "request" | "navigator_children_limit" };
  hops: number;
  handed_from?: string;
  perspectives?: string[];
  hidden_sources?: unknown[];
}

export interface InheritanceLevel {
  level: string;
  label: string;
  role: "supplied" | "overridden" | "conflict" | "inactive" | "none" | string;
  active: boolean;
  scope_id?: string;
  value?: unknown;
  /** A level of an organisational unit (project, team ...); no_unit: the conversation has none of that kind ("bez projektu"). */
  unit_level?: boolean;
  no_unit?: boolean;
}

export interface FieldProvenance {
  scope: string;
  layer: string;
  source?: string;
  scope_id?: string;
  chip: GraphChip;
  /** The person level holds the same value as the preset (config.json carries the presets too): not "you said". */
  same_as_preset?: boolean;
  chain_unavailable?: string;
}

export interface ResolvedField {
  key: string;
  address: string;
  human: { label: string; why: string; effect: string; levels: string[]; group: string; subgroup?: string; role?: string };
  value: unknown;
  value_human: string;
  provenance: FieldProvenance;
  inheritance: InheritanceLevel[];
  edit_rule: string;
  evidence: GraphEvidence;
  flows_reading: string[];
  /** Planned: proposals_status of the answer says why it is empty today. */
  proposals: unknown[];
  status?: string;
  profile_value?: { profile: string; pointer: string };
  secret?: boolean;
}

export interface SettingsResolved {
  schema: "loom.settings_resolved/1";
  conv_id: string | null;
  groups: { id: string; label: string; why: string; order: number; provenance: GraphProvenance; fields: ResolvedField[] }[];
  hidden_keys: string[];
  profile_values: string[];
  proposals_status: { state?: string; today?: string; reason?: string };
  counts: { fields: number; hidden: number; profile_values: number };
}

export interface RoleChainStep {
  step: number;
  type: "explicit" | "role" | "relative_to" | "none" | string;
  setting?: string;
  role?: string;
  value: unknown;
  meaning: string;
  decides: boolean;
}

export interface RoleResolved {
  id: string;
  label: string;
  why: string;
  status: { state: string; today?: string; reason?: string };
  policy: string | null;
  model: string | null;
  model_human: string;
  via: string;
  reason?: string;
  tier: { id: string; reason: string };
  chain: RoleChainStep[];
  flow: { id: string; status: string; does: string[]; planned: string[]; calls: string } | null;
  mode?: { modes: unknown; batch_threshold: unknown; batch_key_set?: boolean | null };
  cost: { per_million?: { input: number; output: number | null; currency: string }; per_million_unknown_reason?: string; last_7d?: unknown; last_7d_unknown_reason?: string };
  flows_reading: { id: string; title: string }[];
  settings: { key: string; label: string }[];
}

export interface RolesResolved {
  schema: "loom.roles_resolved/1";
  conv_id: string | null;
  roles: RoleResolved[];
}

export interface GraphCardResult extends GraphNodeRow {
  schema: "loom.graph_card/1";
  effect?: string;
  levels?: string[];
  edit_rule?: string;
  inheritance?: InheritanceLevel[];
  flows_reading?: { id: string; title: string }[];
  relations: Record<string, { meaning: string | null; out: { address: string; label: string }[]; in: { address: string; label: string }[] }>;
  actions: { id: string; decision: unknown; label: unknown }[];
  actions_unknown_reason?: string;
  evidence_per_property: Record<string, GraphEvidence>;
  breadcrumbs: { address: string; label: string }[];
  children_count: number;
  role?: RoleResolved;
  item?: Record<string, unknown>;
  rom_item?: Record<string, unknown>;
  can_override?: { action: "rom.override"; file: string; pointer: string };
}

export interface RomOverrideRequest {
  id: string;
  /** The fields of the ROM item your copy changes (label, why, hidden ...). */
  patch: Record<string, unknown>;
}
