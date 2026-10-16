// The + menu next to the message field, as data (P33). The entries come from the layered default `chat.composer_menu`; the user's own
// changes (hide, add, move) from `chat.composer_menu_ops`. This file is the pure part: composing the entries and judging each one.

export type MenuKind = "attach" | "camera" | "model" | "submenu" | "toggle" | "panel" | "unavailable";
export interface MenuItem {
  id: string;
  /** A word of presentation.ui_shell, or {en, pl}. */
  label: unknown;
  kind: MenuKind;
  when?: string;
  flag?: string;
  panel?: string;
  /** model: the setting the choice is written to at the conversation level of the scope chain (preset default_model). */
  setting?: string;
  /** toggle: the value the flag sends when on (thinking: the reasoning effort, preset high). */
  value?: unknown;
  reason?: unknown;
  items?: MenuItem[];
}
export type MenuOp =
  | { op: "hide"; id: string }
  | { op: "add"; item: MenuItem; parent?: string; before?: string }
  | { op: "move"; id: string; before?: string; parent?: string };
export interface ComposedMenu { items: MenuItem[]; issues: string[] }

const KINDS: readonly string[] = ["attach", "camera", "model", "submenu", "toggle", "panel", "unavailable"];

function isObject(v: unknown): v is Record<string, unknown> { return typeof v === "object" && v !== null && !Array.isArray(v); }

/** Reads the entries of a menu value; a malformed entry is reported (not dropped silently). */
export function readItems(value: unknown, where: string, issues: string[]): MenuItem[] {
  if (!Array.isArray(value)) { issues.push(`${where}: items must be a list`); return []; }
  const out: MenuItem[] = [];
  for (const raw of value) {
    if (!isObject(raw) || typeof raw.id !== "string" || raw.id === "") { issues.push(`${where}: an entry without id`); continue; }
    if (typeof raw.kind !== "string" || !KINDS.includes(raw.kind)) { issues.push(`${where}/${raw.id}: unknown kind ${String(raw.kind)}`); continue; }
    const item: MenuItem = { id: raw.id, label: raw.label ?? raw.id, kind: raw.kind as MenuKind };
    for (const key of ["when", "flag", "panel", "setting"] as const) if (typeof raw[key] === "string") item[key] = raw[key] as string;
    if (raw.reason !== undefined) item.reason = raw.reason;
    if (raw.value !== undefined) item.value = raw.value;
    if (raw.kind === "submenu") item.items = readItems(raw.items ?? [], `${where}/${raw.id}`, issues);
    out.push(item);
  }
  return out;
}

function find(list: MenuItem[], id: string): { list: MenuItem[]; at: number } | null {
  for (let at = 0; at < list.length; at++) {
    if (list[at].id === id) return { list, at };
    const sub = list[at].items;
    if (sub) { const hit = find(sub, id); if (hit) return hit; }
  }
  return null;
}

/** The effective menu: the entries as shipped, then the user's operations in order. A bad operation is reported and skipped. */
export function composeMenu(baseValue: unknown, opsValue: unknown): ComposedMenu {
  const issues: string[] = [];
  const base = isObject(baseValue) ? baseValue.items : undefined;
  const items = readItems(base ?? [], "menu", issues);
  const ops = isObject(opsValue) && Array.isArray(opsValue.ops) ? opsValue.ops : [];
  if (opsValue !== undefined && opsValue !== null && !(isObject(opsValue) && Array.isArray(opsValue.ops))) issues.push("ops: expected {ops: [...]}");
  const target = (parent: unknown): MenuItem[] | null => {
    if (typeof parent !== "string" || parent === "") return items;
    const hit = find(items, parent);
    const holder = hit ? hit.list[hit.at] : null;
    return holder && holder.kind === "submenu" ? (holder.items ??= []) : null;
  };
  ops.forEach((raw: unknown, n: number) => {
    const where = `ops[${n}]`;
    if (!isObject(raw)) { issues.push(`${where}: not an object`); return; }
    if (raw.op === "hide") {
      const hit = typeof raw.id === "string" ? find(items, raw.id) : null;
      if (!hit) { issues.push(`${where}: no entry ${String(raw.id)} to hide`); return; }
      hit.list.splice(hit.at, 1);
    } else if (raw.op === "add") {
      const parsed = readItems([raw.item], where, issues);
      const list = target(raw.parent);
      if (!parsed.length) return;
      if (!list) { issues.push(`${where}: no submenu ${String(raw.parent)}`); return; }
      if (find(items, parsed[0].id)) { issues.push(`${where}: entry ${parsed[0].id} already exists`); return; }
      const before = typeof raw.before === "string" ? list.findIndex(i => i.id === raw.before) : -1;
      if (before >= 0) list.splice(before, 0, parsed[0]); else list.push(parsed[0]);
    } else if (raw.op === "move") {
      const hit = typeof raw.id === "string" ? find(items, raw.id) : null;
      const list = target(raw.parent);
      if (!hit || !list) { issues.push(`${where}: nothing to move (${String(raw.id)} → ${String(raw.parent ?? "top")})`); return; }
      const [moved] = hit.list.splice(hit.at, 1);
      const before = typeof raw.before === "string" ? list.findIndex(i => i.id === raw.before) : -1;
      if (before >= 0) list.splice(before, 0, moved); else list.push(moved);
    } else issues.push(`${where}: unknown operation ${String(raw.op)}`);
  });
  return { items, issues };
}

/** What this client can do now; an entry that needs a thing the client lacks stays in the menu with a reason. */
export interface MenuContext {
  camera: boolean;
  /** Whether the transport can take a file (clientBlobUpload). */
  upload: boolean;
}
export type ItemState = { state: "ok" } | { state: "hidden" } | { state: "unavailable"; reason: unknown };

export function itemState(item: MenuItem, ctx: MenuContext): ItemState {
  if (item.when === "camera" && !ctx.camera) return { state: "hidden" };
  if (item.kind === "unavailable") return { state: "unavailable", reason: item.reason ?? "cm_reason_unknown_action" };
  if ((item.kind === "attach" || item.kind === "camera") && !ctx.upload) return { state: "unavailable", reason: "cm_reason_upload" };
  return { state: "ok" };
}

/** The request fields a set of on-flags adds to one message. Only the flags that differ from the preset are sent. */
export interface MenuFlags { web_search: boolean; thinking: boolean; memory: boolean; knowledge: boolean }
/** Used only when a toggle entry of the data carries no value of its own (the shipped entry says high). */
export const THINKING_EFFORT = "high";
/** Used only when the model entry of the data names no setting (the shipped entry says default_model). */
export const MODEL_SETTING = "default_model";

/** Every entry, submenus included, in menu order. */
export function flatItems(items: MenuItem[]): MenuItem[] {
  return items.flatMap(i => [i, ...(i.items ? flatItems(i.items) : [])]);
}
/** The value a toggle sends when on: the entry's `value` (data), else the fallback. */
export function toggleValue(items: MenuItem[], flag: string, fallback: string): string {
  const hit = flatItems(items).find(i => i.kind === "toggle" && i.flag === flag);
  return typeof hit?.value === "string" && hit.value !== "" ? hit.value : fallback;
}
/** The setting a model choice is written to: the model entry's `setting` (data), else default_model. */
export function modelSetting(items: MenuItem[]): string {
  const hit = flatItems(items).find(i => i.kind === "model");
  return hit?.setting || MODEL_SETTING;
}

/** The answer of the scope chain (P24) for the model setting of one conversation, as the transport reads it. */
export interface ModelResolution { defined?: boolean; value?: unknown; scope?: string; chain?: { level: string }[] }
/** The model a conversation has of its own: one a level more specific than the person supplied (the conversation, its project, ...).
 *  null = it has none, and the picker keeps the person's choice. The order of the levels is the chain's (data), general first. */
export function conversationModel(resolution: ModelResolution | null | undefined): string | null {
  if (!resolution?.defined || typeof resolution.value !== "string" || resolution.value === "") return null;
  const levels = (resolution.chain ?? []).map(l => l.level);
  const at = levels.indexOf(resolution.scope ?? "");
  const person = levels.indexOf("person");
  return at >= 0 && person >= 0 && at > person ? resolution.value : null;
}
/** The conversation's metadata with the model chosen at its level of the chain ("" = none of its own: the chain decides). Other keys stay. */
export function withConversationModel(metadata: unknown, setting: string, model: string): Record<string, unknown> {
  const meta: Record<string, unknown> = isObject(metadata) ? { ...metadata } : {};
  const settings: Record<string, unknown> = isObject(meta.settings) ? { ...meta.settings } : {};
  if (model) settings[setting] = model; else delete settings[setting];
  meta.settings = settings;
  return meta;
}

/** The reference the kernel resolves to the uploaded file (see ChatEngine::set_attachment_resolver). */
export function attachmentRef(hash: string, name: string): string { return `blob:${hash}/${name}`; }
/** The name to show for a stored attachment: a blob reference carries it after the slash, a path ends with it. */
export function attachmentName(ref: string): string {
  const slash = ref.startsWith("blob:") ? ref.indexOf("/") : ref.lastIndexOf("/");
  const tail = slash >= 0 ? ref.slice(slash + 1) : ref;
  return tail || ref;
}
