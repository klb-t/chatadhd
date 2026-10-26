// M1-K0: the graph projection (loom/src/graph/projection.h) - the ROM, settings, You, conversations, flows, roles, perspectives,
// devices, environments ... as nodes of one navigable graph, computed by the kernel on demand (nothing is written to the graph).
// Reached through loom_invoke("actions", ...) / POST /api/graph/navigate | /api/graph/card | /api/settings/resolved |
// /api/roles/resolved | /api/rom/override (routes from the action catalogue). Types only; the navigator UI is M1-W0.

/** declared (ROM, presets) | observed (what you did or what was measured) | said (what you told the app yourself: an interview answer, a correction) | derived | inferred
 *  (an analysis or model concluded it) | extrapolated. An open list: GraphEvidence.class is any string, a class the screen has no word for is shown as unknown. */
export type EvidenceClass = "declared" | "observed" | "said" | "derived" | "inferred" | "extrapolated";

/** A parameter the person still has to give for an action on a node (kernel M1-W2b-2a: derived from the action catalogue; the arguments the node
 *  fills itself are not listed). `kind` says how it is entered (an open value: free_text, ...), `label` is the kernel's words in the language asked. */
/** `levels` (data): the values of the parameter named by `depends_on` for which this choice applies; absent = for any. */
export interface ActionParamChoice { value: string; label: string; levels?: string[] }
export interface ActionParam { name: string; kind?: string; type?: string; required?: boolean; label?: string; choices?: ActionParamChoice[]; none?: string; depends_on?: string }

export interface GraphEvidence {
  class: EvidenceClass | string;
  confidence: number | null;
  provenance: { layer: string; source_ref: string; method: string; time: string | null };
  /** The evidence in words (graph_screen evidence.method.<method>, else evidence.<class>): a model's reply is never "you said". */
  human?: string;
}

export interface GraphChip {
  /** The level that gave the value (installation, person, conversation, a unit ...), secret, none, or code_preset (what the code does). */
  id: string;
  /** In words: graph_screen chip.<id>, else the level's own label. */
  label: string;
  same_as_preset?: boolean;
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
  /** count_unknown_reason in words (graph_screen reason.<code>). */
  count_unknown_human?: string;
  count_partial?: { source: string; reason: string }[];
  provenance: GraphProvenance;
  /** null for an entry that holds no value (an unanswered slot): an empty entry is no evidence of anything; `state` says why. */
  evidence: GraphEvidence | null;
  /** Why an entry has no evidence, in the kernel's open vocabulary: unanswered | declined | never ... (the screen has a word for each: me.state.<state>). */
  state?: string;
  evidence_overrides?: Record<string, GraphEvidence>;
  has_children: boolean;
  status?: string;
  why?: string;
  order?: number;
  same_as?: string[];
  depth?: number;
  /** An area row: the graph entry counters of the area, each with its name and, when unknown, its reason in words. */
  counts?: Record<string, { known: boolean; value?: number; by?: Record<string, number>; reason?: string; name?: string; reason_human?: string }>;
  empty_reason?: string;
  /** The words of the language asked are missing: which language is shown instead. */
  no_translation?: { lang: string; shown_lang: string; words: string };
  /** A node shown here although its own place is elsewhere (its parent stays the breadcrumbs). */
  shown_also_in?: string;
  /** M1-K2c-2: the actions of the catalogue that fit this node, with the arguments the node fills in (a slot, a note id ...). A part of Ty carries the actions of the part itself. */
  actions?: { id: string; label: string; decision?: string | null; implementation?: string | null; args?: Record<string, unknown>; params?: ActionParam[] }[];
  /** A part of Ty (kind person_subgraph): how many of its entries carry each evidence class. */
  evidence_classes?: Record<string, number>;
  value_unknown_reason?: string;
  /** K1c-3a: a node of the perspective sent_to_model that is a VIEW of the record of a request, not a stored node; derived_note says so in the language asked. */
  derived?: boolean;
  derived_note?: string;
  /** The record's own facts about a derived node (class, section, tokens_est, request | group, reason, count). */
  receipt?: Record<string, unknown>;
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
  /** The edge kind's name in the language asked (graph_views edge_kinds label), else its id. */
  label: string;
  /** The edge kind's meaning in the language asked; null when no kind describes it. */
  meaning: string | null;
  /** K1c-3a: sent_to_model = the tokens (estimate) of the element; left_out_of_model = 1 or the number of elements of the group. */
  weight?: number;
  /** sent_to_model: class, section, why, source_kind, ref, tokens_est; left_out_of_model: group, section, reason, reason_human, group_count. */
  metadata?: { reason?: string; reason_human?: string; group?: string; group_count?: number; tokens_est?: number; section?: string; [key: string]: unknown };
  derived?: boolean;
}

/** The summary of the record of a request in a graph.navigate answer (perspective sent_to_model): what was sent and left out, or why there is no record. */
export interface NavigateReceipt {
  present: boolean;
  msg_id?: string;
  request_msg_id?: string;
  request_address?: string;
  receipt?: { present?: boolean; tokens_est?: number; truncated?: boolean; sections?: number };
  counts?: { nodes?: number; edges?: number; derived_nodes?: number; resolved_nodes?: number; sent?: number; left_out?: number };
  dropped?: { ref?: string; label?: string; reason?: string; reason_human?: string; count?: number }[];
  /** The record is a record, not proof (the kernel's sentence). */
  note?: string;
  /** No record: why (before_m1 | focus_not_a_message ...) and the kernel's sentence for it. */
  missing_reason?: string;
  text?: string;
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
  /** Of the first level; by_source: the sources the budget collapsed (each keeps a share of the level). */
  counts: { total: number; shown: number; collapsed: number; by_source: Record<string, { label: string; total: number; shown: number }> };
  hidden_by_you: { count: number; items: { source: string; id: string; by?: unknown }[] };
  unknown: { source: string; reason: string; source_label: string; reason_human: string }[];
  pages: Record<string, { limit?: number; offset?: number; next_offset?: number; before?: string; next_before?: string; total?: number }>;
  limit: { value: number; from: "request" | "navigator_children_limit" };
  hops: number;
  handed_from?: string;
  perspectives?: string[];
  /** Only with the perspective sent_to_model. */
  receipt?: NavigateReceipt;
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
  /** M1-W0 r2:b4: the level's one-line meaning in the reader's language (graph/scopes.pack chain[].meaning); absent when the catalogue has none. */
  meaning?: string;
}

export interface FieldProvenance {
  scope: string;
  layer: string;
  source?: string;
  scope_id?: string;
  chip: GraphChip;
  /** The person level holds the same value as the preset (config.json carries the presets too): not "you said", not yours. */
  same_as_preset?: boolean;
  /** No level gives a value: value_human says what the reader does then (human.when_unset), chip code_preset. */
  when_unset?: boolean;
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
  /** The flows that read the field, by their human titles. */
  flows_reading: { id: string; title: string }[];
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

export interface RoleTodayStep {
  step: number;
  setting: string | null;
  value: unknown;
  decides: boolean;
  meaning: string;
}

/** A flow a class of model gets: `does`/`planned` are the data's own lines (English), `title`/`does_human`/`planned_human` the same in the language asked. */
export interface RoleFlow {
  id: string;
  status: string;
  does: string[];
  planned: string[];
  calls: string;
  title?: string;
  does_human?: string[];
  planned_human?: string[];
}

export interface RoleResolved {
  id: string;
  label: string;
  why: string;
  /** reason = the kernel's engineering note (files, keys); reason_human, when sent, is the same in words and is preferred. */
  status: { state: string; today?: string; reason?: string; reason_human?: string };
  policy: string | null;
  model: string | null;
  model_human: string;
  via: string;
  reason?: string;
  reason_human?: string;
  reason_detail?: string;
  /** No model: id "none" (no class); else cheap | mid | frontier | unknown with its name and reason in words. */
  tier: { id: string; label: string; reason: string };
  chain: RoleChainStep[];
  /** read: a reader reads the policy (its chain decides); described_not_read: the chain decides nothing today (see today_chain / status.today). */
  chain_status?: "read" | "described_not_read";
  /** What today's reader of a policy no reader reads yet reads, in order (guardian.pack policies.<id>.today_reads). */
  today_chain?: RoleTodayStep[];
  flow: RoleFlow | null;
  /** The flow of every class of model (the class decides which flow runs); the class the role has now is `tier.id`. Absent when no flow depends on the class. */
  flow_by_class?: { tier: { id: string; label: string }; flow: RoleFlow }[];
  mode?: { modes: unknown; batch_threshold: unknown; batch_key_set?: boolean | null };
  cost: {
    per_million?: { input: number; output: number | null; currency: string };
    per_million_unknown_reason?: string;
    per_million_unknown_human?: string;
    last_7d?: unknown;
    last_7d_unknown_reason?: string;
    last_7d_unknown_human?: string;
  };
  flows_reading: { id: string; title: string }[];
  /** The other flows that read one of the role's settings without declaring the role (cost estimate, Secretary, screens that show roles), with the key they read. */
  flows_reading_setting?: { id: string; title: string; key: string }[];
  settings: { key: string; label: string }[];
}

export interface RolesResolved {
  schema: "loom.roles_resolved/1";
  conv_id: string | null;
  roles: RoleResolved[];
  /** What could not be read (the roles then show less, and why). */
  issues?: { what: string; reason: string; reason_human: string }[];
}

export interface GraphCardResult extends GraphNodeRow {
  schema: "loom.graph_card/1";
  effect?: string;
  levels?: string[];
  edit_rule?: string;
  inheritance?: InheritanceLevel[];
  flows_reading?: { id: string; title: string }[];
  relations: Record<string, { label: string; meaning: string | null; inverse?: string | null; inverse_label?: string | null; out: { address: string; label: string }[]; in: { address: string; label: string }[] }>;
  relations_unknown?: { source: string; reason: string }[];
  /** decision: the rules' status (applicable | needs_confirmation | denied | default); label: the action's title in the language asked. */
  actions: { id: string; decision: string | null; label: string; implementation: string | null }[];
  actions_unknown_reason?: string;
  evidence_per_property: Record<string, GraphEvidence>;
  breadcrumbs: { address: string; label: string }[];
  children_count: number;
  role?: RoleResolved;
  item?: Record<string, unknown>;
  rom_item?: Record<string, unknown>;
  can_override?: { action: "rom.override"; file: string; pointer: string };
  /** The source's own attributes of the node (a settings field: key, type, edit, human ...); the card reads `key` and `type` from it. */
  attrs?: Record<string, unknown>;
}

export interface RomOverrideRequest {
  id: string;
  /** The fields of the ROM item your copy changes (label, why, hidden ...). */
  patch: Record<string, unknown>;
}
