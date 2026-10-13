// Field metadata of settings keys (Loom: loom_settings_fields / GET /api/settings/fields).
// `edit` is an open string; unknown rules are shown verbatim and never blocked.
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
}
export interface SettingsFieldsDoc {
  schema: string;
  edit_rules: { id: string; edit?: string; label_pl?: string; enforced?: boolean; meaning?: string }[];
  fields: SettingsField[];
}

export function parseSettingsFields(raw: unknown): SettingsFieldsDoc {
  const doc = raw as Partial<SettingsFieldsDoc> | null;
  if (!doc || !Array.isArray(doc.fields)) throw new Error("settings fields: malformed response");
  return { schema: String(doc.schema ?? ""), edit_rules: Array.isArray(doc.edit_rules) ? doc.edit_rules : [], fields: doc.fields };
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
