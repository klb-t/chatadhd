// M1-W0 (3), package P5: the state of the human settings screen (SettingsHuman.tsx) as pure logic, so a unit test drives it without a
// browser. The screen draws what settings.resolved answers: the groups (their order, label and why come from the data), the fields of a
// group (label, why, effect, value in words, provenance chip, the flows that read them). Nothing here lists a group, a field or a
// word of its own; the words come from presentation.settings_human (a missing word shows its id), and the card of a field is the
// node card of the navigator (NodeCard.tsx), so there is one place that writes a value.
import type { ResolvedField, SettingsResolved } from "../api/graph-projection";
import { dataLabel, type ShellT } from "../shell/text";
import { evidenceText } from "./navigator-state";
import { BASELINE_LEVELS } from "./node-card-state";

/** The group that carries the former settings screen as "Wszystkie pola" (spec §3.5: Silnik › Wszystkie pola). A preset: when no
 *  group has this id (an overlay hid or renamed it) the former screen is drawn under the group list, so nothing disappears. */
export const ALL_FIELDS_GROUP_PRESET = "silnik";

/** "Yours": a value somebody gave on a level of a person (person, conversation, project ...) that is not the preset. A level that is
 *  a baseline file (installation, app preset, engine), no level at all (code, secret) and "the preset kept in your settings"
 *  (config.json carries the presets too) are not yours. */
export function isYours(field: Pick<ResolvedField, "provenance">): boolean {
  const p = field.provenance;
  if (!p || p.same_as_preset || p.chip?.same_as_preset) return false;
  const scope = p.scope ?? "none";
  return scope !== "none" && scope !== "rom" && !BASELINE_LEVELS.has(scope);
}

export interface GroupSummary { id: string; label: string; why: string; order: number; fields: number; yours: number }

/** The groups in the order of the data (order, then as sent), each with its field count and how many are yours. */
export function summaries(doc: Pick<SettingsResolved, "groups">): GroupSummary[] {
  return doc.groups
    .map((g, position) => ({ g, position }))
    .sort((a, b) => (a.g.order - b.g.order) || (a.position - b.position))
    .map(({ g }) => ({ id: g.id, label: g.label, why: g.why, order: g.order, fields: g.fields.length, yours: g.fields.filter(isYours).length }));
}

export function totals(doc: Pick<SettingsResolved, "groups">): { fields: number; yours: number } {
  return summaries(doc).reduce((sum, g) => ({ fields: sum.fields + g.fields, yours: sum.yours + g.yours }), { fields: 0, yours: 0 });
}

/** "5 pól": the plural form comes from the language (Intl.PluralRules), the word of the form from the catalogue. */
export function countWord(kind: "fields" | "yours", n: number, language: string, t: ShellT): string {
  if (kind === "yours") return t("count.yours", { n: String(n) });
  let category = "other";
  try { category = new Intl.PluralRules(language).select(n); } catch { /* an unknown language: the word for "other" */ }
  return t(`count.fields.${category}`, { n: String(n) });
}

/** "N pól · M Twoje". */
export function countLine(counts: { fields: number; yours: number }, language: string, t: ShellT): string {
  return t("count.line", { fields: countWord("fields", counts.fields, language, t), yours: countWord("yours", counts.yours, language, t) });
}

export const fieldsToShow = (fields: ResolvedField[], onlyChanged: boolean): ResolvedField[] => onlyChanged ? fields.filter(isYours) : fields;

export interface SubgroupBlock { id: string | null; label: string; why: string; fields: ResolvedField[] }

/** The subgroups of a group in the order they first appear, with the labels of the pack (settings_fields groups[].subgroups[]) in the
 *  language; a subgroup the pack does not label shows its id, a field with no subgroup goes to the block "Ogólne" (subgroup.none).
 *  A group none of whose fields has a subgroup is one block with no heading (label ""). */
export function subgroupBlocks(fields: ResolvedField[], labels: Record<string, { label: string; why: string }>, t: ShellT): SubgroupBlock[] {
  const blocks: SubgroupBlock[] = [];
  const index = new Map<string, SubgroupBlock>();
  const anySub = fields.some(f => f.human.subgroup);
  for (const field of fields) {
    const id = field.human.subgroup ?? null;
    const key = id ?? "";
    let block = index.get(key);
    if (!block) {
      block = { id, label: !anySub ? "" : id === null ? t("subgroup.none") : (labels[id]?.label ?? id), why: id === null ? "" : (labels[id]?.why ?? ""), fields: [] };
      index.set(key, block);
      blocks.push(block);
    }
    block.fields.push(field);
  }
  return blocks;
}

/** The subgroup labels of one group out of the settings_fields document (settings.fields answer), in the language asked. */
export function subgroupLabels(raw: unknown, groupId: string, language: string): Record<string, { label: string; why: string }> {
  const out: Record<string, { label: string; why: string }> = {};
  const groups = (raw as { groups?: Record<string, { subgroups?: { id?: unknown; label?: unknown; why?: unknown }[] }> } | null)?.groups;
  for (const sub of groups?.[groupId]?.subgroups ?? []) {
    if (typeof sub?.id !== "string") continue;
    out[sub.id] = { label: dataLabel(sub.label, language, sub.id), why: dataLabel(sub.why, language, "") };
  }
  return out;
}

export interface FieldChip { id: string; text: string; kind: "provenance" | "evidence" | "status" | "profile" }

/** The chips of a field row: where the value comes from (the kernel's own words), the evidence only when it is not a plain preset,
 *  "planowane" for a field no reader reads yet, "profil: …" for a value that lives in a profile. */
export function fieldChips(field: ResolvedField, t: ShellT, graphT: ShellT): FieldChip[] {
  const chips: FieldChip[] = [];
  const chip = field.provenance?.chip;
  chips.push({ id: "provenance", kind: "provenance", text: chip?.label ?? t(`chip.${chip?.id ?? "none"}`) });
  const evidence = evidenceText(field, graphT);
  if (evidence) chips.push({ id: "evidence", kind: "evidence", text: evidence });
  if (field.status) chips.push({ id: "status", kind: "status", text: t(`status.${field.status}`) });
  if (field.profile_value) chips.push({ id: "profile", kind: "profile", text: t("chip.profile", { profile: field.profile_value.profile }) });
  return chips;
}

/** A field no reader reads yet says so, and the reason is the kernel's own text of what it does today (human.effect). */
export const isPlanned = (field: Pick<ResolvedField, "status">): boolean => field.status === "planned";

export const flowTitles = (field: Pick<ResolvedField, "flows_reading">): string[] => (field.flows_reading ?? []).map(f => f.title || f.id);

export const editText = (field: Pick<ResolvedField, "edit_rule">, t: ShellT): string => t(`edit.${field.edit_rule}`);
