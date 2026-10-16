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
  role?: string;
  flows?: string[];
  technical_only?: boolean;
}
export interface SettingsSubgroup {
  id: string;
  label: LocalizedText;
  why?: LocalizedText;
}
export interface SettingsGroup {
  label: LocalizedText;
  why: LocalizedText;
  order: number;
  subgroups: SettingsSubgroup[];
  hidden?: boolean;
  empty_reason?: LocalizedText;
}
/** A value of a runtime profile shown as a field with the chip "runtime profile: <profile>"; the value lives in the profile.
 * `problem` (instead of `value`) says why it could not be read; it is shown, never dropped. */
export interface SettingsProfileValue {
  profile: string;
  pointer: string;
  human: SettingsHuman;
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
  values?: { value: unknown; meaning?: LocalizedText; status?: string; reason?: LocalizedText | string }[];
}
export interface SettingsFieldsDoc {
  schema: string;
  edit_rules: { id: string; edit?: string; label_pl?: string; enforced?: boolean; meaning?: string }[];
  fields: SettingsField[];
  groups: Record<string, SettingsGroup>;
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
