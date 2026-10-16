// The + menu next to the message field, as data (P33). The entries come from the layered default `chat.composer_menu`; the user's own
// changes (hide, add, move) from `chat.composer_menu_ops`. This file is the pure part: composing the entries and judging each one.
import { SHELL_PANELS } from "./panels";

export type MenuKind = "attach" | "camera" | "model" | "submenu" | "toggle" | "panel" | "unavailable";
export interface MenuItem {
  id: string;
  /** A word of presentation.ui_shell, or {en, pl}. */
  label: unknown;
  kind: MenuKind;
  /** toggle: a switch of this view (memory | knowledge). */
  flag?: string;
  /** toggle: the request field it sets while on, with `value` (web_search: true, reasoning_effort: "high", action: "agent.run" ...). */
  field?: string;
  value?: unknown;
  /** toggle: turning it on turns off the other field switches (an action takes only the words). */
  exclusive?: boolean;
  /** Settings that must not be empty, else the entry is unavailable with its `reason` (and `panel` where they are set). */
  needs?: string[];
  panel?: string;
  reason?: unknown;
  items?: MenuItem[];
}
export type MenuOp =
  | { op: "hide"; id: string }
  | { op: "add"; item: MenuItem; parent?: string; before?: string }
  | { op: "move"; id: string; before?: string; parent?: string };
/** A problem of the menu's data, said in the menu: `code` names a word of ui_shell (`cm_issue_<code>`); `name` is an entry's label. */
export interface MenuIssue { code: string; params: Record<string, unknown> }
export interface ComposedMenu { items: MenuItem[]; issues: MenuIssue[] }

const KINDS: readonly string[] = ["attach", "camera", "model", "submenu", "toggle", "panel", "unavailable"];
/** The switches of this view a toggle may name by `flag`; any other switch names the request field it sets (`field`). */
export const MENU_FLAGS = ["memory", "knowledge"] as const;
export type MenuFlag = typeof MENU_FLAGS[number];
export type MenuFlags = Record<MenuFlag, boolean>;
/** The setting a model choice is written to at the conversation level: the key the kernel reads (scoped_default_model), fixed. */
export const MODEL_SETTING = "default_model";

function isObject(v: unknown): v is Record<string, unknown> { return typeof v === "object" && v !== null && !Array.isArray(v); }
const issue = (code: string, params: Record<string, unknown> = {}): MenuIssue => ({ code, params });

/** Reads the entries of a menu value. A malformed entry is reported by the label it would show (never dropped silently). `where` is the
 *  label of the menu that holds them. */
export function readItems(value: unknown, where: unknown, issues: MenuIssue[]): MenuItem[] {
  if (!Array.isArray(value)) { issues.push(issue("items_not_list", { name: where })); return []; }
  const out: MenuItem[] = [];
  for (const raw of value) {
    if (!isObject(raw) || typeof raw.id !== "string" || raw.id === "") { issues.push(issue("no_id", { name: where })); continue; }
    const name = raw.label ?? raw.id;
    if (typeof raw.kind !== "string" || !KINDS.includes(raw.kind)) { issues.push(issue("unknown_kind", { name, kind: String(raw.kind) })); continue; }
    const item: MenuItem = { id: raw.id, label: name, kind: raw.kind as MenuKind };
    for (const key of ["flag", "field", "panel"] as const) if (typeof raw[key] === "string" && raw[key] !== "") item[key] = raw[key] as string;
    if (raw.reason !== undefined) item.reason = raw.reason;
    if (raw.value !== undefined) item.value = raw.value;
    if (raw.exclusive === true) item.exclusive = true;
    if (Array.isArray(raw.needs)) item.needs = raw.needs.filter((k): k is string => typeof k === "string" && k !== "");
    if (item.kind === "toggle" && !item.field && !(MENU_FLAGS as readonly string[]).includes(item.flag ?? "")) {
      issues.push(issue("toggle_target", { name })); continue;
    }
    if (item.kind === "panel" && !item.panel) { issues.push(issue("no_panel", { name })); continue; }
    if (item.panel && !(SHELL_PANELS as readonly string[]).includes(item.panel)) {
      issues.push(issue("unknown_panel", { name, panel: item.panel }));
      if (item.kind === "panel") continue;
      delete item.panel;  // an unavailable entry still says its reason; it only cannot point anywhere
    }
    if (item.kind === "submenu") item.items = readItems(raw.items ?? [], name, issues);
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
  const issues: MenuIssue[] = [];
  let items: MenuItem[] = [];
  if (isObject(baseValue) && baseValue.items !== undefined) items = readItems(baseValue.items, "cm_title", issues);
  else issues.push(issue("no_items"));
  const ops = isObject(opsValue) && Array.isArray(opsValue.ops) ? opsValue.ops : [];
  if (opsValue !== undefined && opsValue !== null && !(isObject(opsValue) && Array.isArray(opsValue.ops))) issues.push(issue("ops_shape"));
  const target = (parent: unknown): MenuItem[] | null => {
    if (typeof parent !== "string" || parent === "") return items;
    const hit = find(items, parent);
    const holder = hit ? hit.list[hit.at] : null;
    return holder && holder.kind === "submenu" ? (holder.items ??= []) : null;
  };
  const labelOf = (id: unknown): unknown => { const hit = typeof id === "string" ? find(items, id) : null; return hit ? hit.list[hit.at].label : String(id); };
  ops.forEach((raw: unknown, index: number) => {
    const n = index + 1;
    if (!isObject(raw)) { issues.push(issue("op_shape", { n })); return; }
    if (raw.op === "hide") {
      const hit = typeof raw.id === "string" ? find(items, raw.id) : null;
      if (!hit) { issues.push(issue("hide_missing", { n, name: String(raw.id) })); return; }
      hit.list.splice(hit.at, 1);
    } else if (raw.op === "add") {
      const parsed = readItems([raw.item], "cm_title", issues);
      if (!parsed.length) return;
      const list = target(raw.parent);
      if (!list) { issues.push(issue("no_submenu", { n, name: labelOf(raw.parent) })); return; }
      if (find(items, parsed[0].id)) { issues.push(issue("exists", { n, name: parsed[0].label })); return; }
      const before = typeof raw.before === "string" ? list.findIndex(i => i.id === raw.before) : -1;
      if (before >= 0) list.splice(before, 0, parsed[0]); else list.push(parsed[0]);
    } else if (raw.op === "move") {
      const hit = typeof raw.id === "string" ? find(items, raw.id) : null;
      if (!hit) { issues.push(issue("move_missing", { n, name: String(raw.id) })); return; }
      const moving = hit.list[hit.at];
      // A submenu cannot go into itself or into one of its own submenus: it would leave the menu.
      if (typeof raw.parent === "string" && (raw.parent === moving.id || (moving.items && find(moving.items, raw.parent)))) {
        issues.push(issue("move_into_itself", { n, name: moving.label })); return;
      }
      const list = target(raw.parent);
      if (!list) { issues.push(issue("no_submenu", { n, name: labelOf(raw.parent) })); return; }
      hit.list.splice(hit.at, 1);
      const before = typeof raw.before === "string" ? list.findIndex(i => i.id === raw.before) : -1;
      if (before >= 0) list.splice(before, 0, moving); else list.push(moving);
    } else issues.push(issue("unknown_op", { n, op: String(raw.op) }));
  });
  return { items, issues };
}

/** A layered default as the page reads it: its status (effective | disabled | excluded | missing) and value. */
export interface LayeredValue { status: string; value?: unknown }
/** The menu the page shows. The user's changes apply only while their default is effective (a disabled or excluded one is off by choice).
 *  A menu default that is not effective falls back to the shipped entries, and the menu says so. */
export function effectiveMenu(base: LayeredValue, ops: LayeredValue, shipped: unknown): ComposedMenu {
  const own = base.status === "effective";
  const composed = composeMenu(own ? base.value : shipped, ops.status === "effective" ? ops.value : undefined);
  if (!own) composed.issues.unshift(issue("menu_not_effective"));
  return composed;
}

/** The words of a label: a word of ui_shell, else the label itself (a plain text someone wrote), {en, pl} by language, else the fallback
 *  (the id, only when there is no label at all). `t` gives back the id it does not know. */
export function labelText(label: unknown, t: (id: string, params?: Record<string, string>) => string, lang: string, fallback: string): string {
  if (typeof label === "string") return label === "" ? fallback : t(label);
  if (isObject(label)) { const text = label[lang] ?? label.en; if (typeof text === "string") return text; }
  return fallback;
}
/** One problem of the menu in plain words of the page's language (`cm_issue_<code>`), entries named by the label they show. */
export function issueText(item: MenuIssue, t: (id: string, params?: Record<string, string>) => string, lang: string): string {
  const params: Record<string, string> = {};
  for (const [key, value] of Object.entries(item.params)) params[key] = key === "name" ? labelText(value, t, lang, String(value)) : String(value);
  return t(`cm_issue_${item.code}`, params);
}

/** What this client can do now; an entry that needs a thing the client lacks stays in the menu with a reason. */
export interface MenuContext {
  /** Whether the transport can take a file (clientBlobUpload). */
  upload: boolean;
  /** The settings, for entries that need some (null: not read, the entries stay usable and the kernel says what is missing). */
  settings?: Record<string, unknown> | null;
}
export type ItemState = { state: "ok" } | { state: "unavailable"; reason: unknown };

export function itemState(item: MenuItem, ctx: MenuContext): ItemState {
  if (item.kind === "unavailable") return { state: "unavailable", reason: item.reason ?? "cm_reason_unknown_action" };
  if ((item.kind === "attach" || item.kind === "camera") && !ctx.upload) return { state: "unavailable", reason: "cm_reason_upload" };
  if (item.needs?.length && ctx.settings) {
    const settings = ctx.settings;
    const empty = item.needs.some(key => settings[key] === undefined || settings[key] === null || settings[key] === "");
    if (empty) return { state: "unavailable", reason: item.reason ?? "cm_reason_needs" };
  }
  return { state: "ok" };
}

/** Every entry, submenus included, in menu order. */
export function flatItems(items: MenuItem[]): MenuItem[] {
  return items.flatMap(i => [i, ...(i.items ? flatItems(i.items) : [])]);
}
/** The field switches of the menu (toggles that set a request field), in menu order. */
export function fieldSwitches(items: MenuItem[]): MenuItem[] {
  return flatItems(items).filter(i => i.kind === "toggle" && typeof i.field === "string" && i.field !== "");
}
/** The field switches after turning `id` on or off. Switches that set the same field are one choice; an exclusive one stands alone. */
export function switchToggle(items: MenuItem[], on: Record<string, boolean>, id: string): Record<string, boolean> {
  const all = fieldSwitches(items);
  const item = all.find(i => i.id === id);
  if (!item) return on;
  if (on[id]) { const next = { ...on }; delete next[id]; return next; }
  const next: Record<string, boolean> = {};
  for (const other of all) {
    if (!on[other.id] || other.id === id) continue;
    if (other.field === item.field || item.exclusive || other.exclusive) continue;  // turned off by this choice
    next[other.id] = true;
  }
  next[id] = true;
  return next;
}
/** The switches that are on, in menu order (an id the menu no longer has is not on). */
export function onSwitches(items: MenuItem[], on: Record<string, boolean>): MenuItem[] {
  return fieldSwitches(items).filter(i => on[i.id]);
}
/** The request fields the switches that are on set. */
export function requestFields(items: MenuItem[], on: Record<string, boolean>): Record<string, unknown> {
  const out: Record<string, unknown> = {};
  for (const item of onSwitches(items, on)) out[item.field as string] = item.value === undefined ? true : item.value;
  return out;
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
/** Which attachment a refused send names (ChatEngine::check_attachments ends its message with the reference as given), and why:
 *  "missing" (not found) or "unreadable" (a kind of file that is not read). null: the refusal is about none of them. */
export function refusedAttachment(code: unknown, message: unknown, refs: string[]): { ref: string; why: "missing" | "unreadable" } | null {
  if (typeof message !== "string") return null;
  const ref = refs.find(r => message.endsWith(r));
  if (!ref) return null;
  return { ref, why: code === "unsupported" ? "unreadable" : "missing" };
}
