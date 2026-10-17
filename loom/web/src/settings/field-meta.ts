// Field metadata of settings keys (Loom: loom_settings_fields / GET /api/settings/fields).
// `edit` is an open string; unknown rules are shown verbatim and never blocked.
/** A sentence in both languages (M1-D0: every human text of the settings is data in pl and en). */
export interface LocalizedText {
  pl: string;
  en: string;
}
/** How a non-engineer reads a field: label, why it exists, what changing it does, where it may be set (scope chain levels),
 * which human group holds it. Data of settings_fields.pack; an overlay may regroup, rename or hide. */
export interface SettingsHuman {
  label: LocalizedText;
  why: LocalizedText;
  effect: LocalizedText;
  levels: string[];
  group: string;
  subgroup?: string;
  /** Set when the pack's `placement` (a user overlay, per key) moved the entry: the preset group/subgroup it came from. */
  placed_from?: { group: string; subgroup?: string };
  role?: string;
  /** Status of `role` only when ports.pack does not know the role (an overlay's own role): the chip says so instead of dangling.
   *  A role ports.pack knows carries its label, why and status there (roles.<id>.status: partial | planned, M1-D1). */
  role_status?: string;
  flows?: string[];
  technical_only?: boolean;
}
/** A field whose key has a preset but no reader yet: shown as the chip "planned (reason)"; dropped by the position that implements the reader. */
export interface SettingsFieldStatus {
  state: string;
  by?: string;
  /** Technical name of the code that will read the key (shown under "nazwa techniczna"). */
  reader?: string;
  reason: LocalizedText;
  /** What the app does today, while the reader is missing; shown next to the planned chip. */
  today?: LocalizedText;
}
export interface SettingsSubgroup {
  id: string;
  label: LocalizedText;
  why?: LocalizedText;
}
/** A group as the view kind `settings_groups` composes it: the pack's group (ROM) with the views overlays of the data dir on top;
 * origin/provenance say who renamed or regrouped. A hidden group stays listed (hidden + hidden_by) so its fields are never orphaned. */
export interface SettingsGroup {
  label: LocalizedText;
  why: LocalizedText;
  order: number;
  subgroups: SettingsSubgroup[];
  origin?: string;
  provenance?: unknown[];
  hidden?: boolean;
  hidden_by?: string;
  disabled?: boolean;
  empty_reason?: LocalizedText;
}
/** Evidence for human.levels: keys whose reader resolves the value through the scope chain (project/conversation), keys with a
 * per-request option (turn) and keys or profile values (<profile><pointer>) whose installation layer has a writer (installation).
 * A level a field declares without evidence is a problem in the kernel's check, never a promise shown. */
export interface SettingsLevelEvidence {
  chain: string[];
  turn: string[];
  installation?: string[];
}
/** A value a field's reader understands. `on_set` says what a write of a value with a `status` does on every write path:
 * "refuse" (its reader rejects it) or "warn" (stored with a warning; absent = warn). */
export interface SettingsFieldValue {
  value: unknown;
  meaning?: LocalizedText;
  preset?: boolean;
  status?: string;
  /** The work item that adds the reader of a planned value (technical). */
  by?: string;
  reason?: LocalizedText | string;
  on_set?: "refuse" | "warn" | string;
  /** The value is in the reader's runtime profile (the field's `values_from`), with the user's profile overlay. */
  from_profile?: string;
  /** A status the data gave (e.g. "planned") that no longer holds: the reader's profile now has the value. */
  status_superseded?: string;
}
/** Where a field's values come from: the runtime profile's value at `pointer` (its keys when `keys`), as the reader reads it. */
export interface SettingsValuesFrom {
  profile: string;
  pointer: string;
  keys?: boolean;
}
/** A value of a runtime profile shown as a field with the chip "runtime profile: <profile>"; the value lives in the profile.
 * `problem` (instead of `value`) says why it could not be read; it is shown, never dropped. */
export interface SettingsProfileValue {
  profile: string;
  pointer: string;
  /** Absent only in a hand-written overlay; the kernel's check lists it as a problem. */
  human?: SettingsHuman;
  value?: unknown;
  problem?: string;
}
export interface SettingsField {
  key: string;
  scope: string;
  group?: string;
  type?: string;
  default: "present" | "absent" | string;
  default_value?: unknown;
  edit: string;
  description?: string;
  sensitive?: boolean;
  human?: SettingsHuman;
  values?: SettingsFieldValue[];
  /** The values are the reader's data (resolved by the kernel); the pack's own entries annotate them. */
  values_from?: SettingsValuesFrom;
  /** Why `values_from` could not be read (the annotations are listed alone). */
  values_from_problem?: string;
  /** The values list is examples, not the whole set (every write path accepts other values). */
  values_open?: boolean;
  /** What a write of a value outside a closed `values` list does: "refuse" or "warn" (stored with a warning; absent = warn). */
  on_unlisted?: "refuse" | "warn" | string;
  status?: SettingsFieldStatus;
}
export interface SettingsFieldsDoc {
  schema: string;
  edit_rules: { id: string; edit?: string; label_pl?: string; enforced?: boolean; meaning?: string }[];
  fields: SettingsField[];
  groups: Record<string, SettingsGroup>;
  /** Bad overlay operations on settings_groups as the view engine reports them, an unreadable views directory and bad
   * `placement` entries; shown, never swallowed. */
  groups_issues?: unknown[];
  level_evidence?: SettingsLevelEvidence;
  profile_values: SettingsProfileValue[];
}

export function parseSettingsFields(raw: unknown): SettingsFieldsDoc {
  const doc = raw as Partial<SettingsFieldsDoc> | null;
  if (!doc || !Array.isArray(doc.fields)) throw new Error("settings fields: malformed response");
  const groups = doc.groups && typeof doc.groups === "object" && !Array.isArray(doc.groups) ? doc.groups : {};
  return {
    schema: String(doc.schema ?? ""),
    edit_rules: Array.isArray(doc.edit_rules) ? doc.edit_rules : [],
    fields: doc.fields,
    groups,
    groups_issues: Array.isArray(doc.groups_issues) ? doc.groups_issues : undefined,
    level_evidence: doc.level_evidence && typeof doc.level_evidence === "object" ? doc.level_evidence : undefined,
    profile_values: Array.isArray(doc.profile_values) ? doc.profile_values : [],
  };
}

/** Text of the default column. Sensitive fields never show a value. */
export function defaultLabel(f: SettingsField): string {
  if (f.default !== "present") return "no default";
  if (f.sensitive || f.default_value === undefined) return "default";
  return `default: ${typeof f.default_value === "string" ? JSON.stringify(f.default_value) : JSON.stringify(f.default_value)}`;
}

/** Whether the generic write path rejects edits (rule marked enforced and read_only). */
export function isReadOnly(f: SettingsField): boolean {
  return f.edit === "read_only";
}

export function editLabel(f: SettingsField, doc: SettingsFieldsDoc): string {
  const rule = doc.edit_rules.find((r) => (r.edit ?? r.id) === f.edit);
  return rule ? (rule.edit ?? rule.id) : f.edit;
}
