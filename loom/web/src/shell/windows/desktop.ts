// R101 P0 (PULPIT_R101_2026-10-28 §7, §8, §10, §12): the desktop layer under the windows, as pure data + total functions.
// Nothing here touches React or the DOM. The desktop is always there; it is SEEN where no window covers it (`desktopCover`), and from it
// a person can always open a window (the one guaranteed property P1, kept by the emergency path, which a person may switch off - B3).
// Every list and value below is a preset of shell/data/desktop.json or a setting (desktop_sections, desktop_emergency_path,
// last_window_close); unknown values are advisories, never errors.
import desktopFile from "../data/desktop.json";
import { apply, type Ctx, type WLayout, type WState, type Win } from "./model";
import { WINDOW_CTX, baseWindow, visibleOverlay } from "./session";
import { focusWindow, openWindow, type WindowData, WINDOW_DATA } from "./ops";
import { oneEntry } from "./steps";
import { KINDS, type Kind } from "./registry";

type Words = Record<string, { pl?: string; en?: string }>;
export interface DesktopData {
  sections: string[];
  sections_known: Record<string, { opens_window?: boolean; note?: string }>;
  sentence: { send?: boolean };
  emergency: { kind: string };
  last_window_close: { preset: string; aliases?: Record<string, string> };
  keys?: Record<string, string>;
  words: Words;
}
export const DESKTOP = desktopFile as unknown as DesktopData;

export const say = (words: Words, id: string, lang: string, vars: Record<string, string> = {}): string => {
  const w = words[id]; const s = (lang === "pl" ? w?.pl ?? w?.en : w?.en ?? w?.pl) ?? id;
  return s.replace(/\{(\w+)\}/g, (_, k: string) => vars[k] ?? "");
};

/** The windows a person sees as windows: a collapsed handle (the list handle of `czat`) is not one. */
export const liveWindows = (l: WLayout): Win[] => l.windows.filter((w) => !w.collapsed);

/** How much of the desktop the windows cover: full (a maximized window, a panel in front or a drawn dock tree), partial (only floating
 *  windows), none (no window shown: all closed or minimized). `shown` is the page's own rule of a drawn docked window. */
export function desktopCover(l: WLayout, shown: (w: Win) => boolean = (w) => !w.collapsed): "full" | "partial" | "none" {
  const base = baseWindow(l);   // a minimized window can be the "covered" one of a floating panel: it covers nothing
  if (visibleOverlay(l) || (base && base.mode !== "minimized")) return "full";
  if (l.windows.some((w) => w.dock && (w.mode === "docked" || w.mode === "tabbed") && shown(w))) return "full";
  if (l.windows.some((w) => w.mode === "floating" && w.rect && shown(w))) return "partial";
  return "none";
}

export interface DesktopSettings { sections: string[]; emergency: boolean; unknownSections: string[] }
/** desktop_sections (an array or a comma list; unset = the preset) and desktop_emergency_path (unset = on). */
export function desktopSettings(config: Record<string, unknown>, data: DesktopData = DESKTOP): DesktopSettings {
  let raw: unknown = config.desktop_sections;
  if (typeof raw === "string") { try { raw = raw.trim().startsWith("[") ? JSON.parse(raw) : raw.split(",").map((x) => x.trim()).filter(Boolean); } catch { raw = undefined; } }
  const list = Array.isArray(raw) ? raw.filter((x): x is string => typeof x === "string") : data.sections;
  const known = list.filter((x) => x in data.sections_known);
  const e = config.desktop_emergency_path;
  const emergency = !(e === false || e === "false" || e === 0 || e === "0");
  return { sections: known, emergency, unknownSections: list.filter((x) => !(x in data.sections_known)) };
}

export type KindTarget = { chat: true } | { panel: string };
export interface KindEntry { kind: string; label: string; status: "available" | "planned" | "cannot_open"; reason?: string; target?: KindTarget }
/** Every kind of the registry as an entry of "open a new window": chat is the layout's own window; a kind some panel opens as is
 *  available; a planned kind keeps its reason; a present kind no panel opens is shown with the reason `unavailable_cannot_open`. */
export function kindEntries(knownPanel: (id: string) => boolean, layoutId: string, lang: string, kinds: readonly Kind[] = KINDS, data: WindowData = WINDOW_DATA, words: Words = DESKTOP.words): KindEntry[] {
  const byKind = new Map<string, string>();
  const map = { ...(data.panel_kinds ?? {}), ...(data.panel_kinds_by_layout?.[layoutId] ?? {}) };
  for (const [panel, kind] of Object.entries(map)) if (knownPanel(panel) && !byKind.has(kind)) byKind.set(kind, panel);
  return kinds.map((k) => {
    const label = k.label?.pl ?? k.id;
    if (k.status !== "present") return { kind: k.id, label, status: "planned", reason: k.reason?.pl ?? k.status };
    // R101 D1: the background kind (the desktop itself) is listed, never hidden, with the reason that it is always there.
    if (k.mode === "background") return { kind: k.id, label, status: "cannot_open", reason: say(words, "background_kind", lang) };
    if (k.id === "chat") return { kind: k.id, label, status: "available", target: { chat: true } };
    const panel = byKind.get(k.id) ?? (knownPanel(k.id) ? k.id : undefined);
    return panel ? { kind: k.id, label, status: "available", target: { panel } } : { kind: k.id, label, status: "cannot_open", reason: say(words, "unavailable_cannot_open", lang) };
  });
}

/** Opens a window of a kind from the desktop: a chat window that exists comes forward (restored if minimized), a closed one is opened
 *  again (under its preset id when free); a panel opens as the window ops open it. One journal entry. */
export function openFromDesktop(s: WState, target: KindTarget, deviceClass: string, presetChatId = "w_chat", ctx: Ctx = WINDOW_CTX, data: WindowData = WINDOW_DATA): WState {
  if ("chat" in target) {
    const chat = s.layout.windows.find((w) => w.kind === "chat" && typeof w.params?.panel !== "string");
    if (chat) return chat.mode === "minimized" ? restoreFromDesktop(s, chat.id, ctx) : focusWindow(s, chat.id, ctx);
    const taken = new Set(s.layout.windows.map((w) => w.id));
    const own: Ctx = taken.has(presetChatId) ? ctx : { ...ctx, newId: () => presetChatId };
    return oneEntry(s, apply(s, { op: "open", kind: "chat", node: "conv:current", placement: "maximize" }, own).state, "open");
  }
  return openWindow(s, target, "", deviceClass, ctx, data).state;
}

/** Restores a minimized window to the mode it had (one journal entry); a window shown nowhere else is brought to the front. */
export function restoreFromDesktop(s: WState, id: string, ctx: Ctx = WINDOW_CTX): WState {
  const r = apply(s, { op: "restore", id }, ctx).state;
  const w = r.layout.windows.find((x) => x.id === id);
  const front = w && w.mode !== "floating" && w.mode !== "maximized" && desktopCover(r.layout) === "none" ? apply(r, { op: "maximize", id }, ctx).state : r;
  return oneEntry(s, front, "restore");
}

/** "Show the desktop": every shown window minimized, one journal entry (layout.undo brings them back). */
export function showDesktop(s: WState, ctx: Ctx = WINDOW_CTX): WState {
  let r = s;
  for (const w of s.layout.windows) if (!w.collapsed && w.mode !== "minimized") r = apply(r, { op: "minimize", id: w.id }, ctx).state;
  return oneEntry(s, r, "minimize");
}

export const minimizedWindows = (l: WLayout): Win[] => l.windows.filter((w) => w.mode === "minimized" && !w.collapsed);

export type LastClose = { do: "desktop" } | { do: "close_app" } | { do: "reopen"; kind: string } | { do: "layout"; id: string } | { do: "desktop"; unknown: string };
/** last_window_close, an open value: desktop (preset) | close_app (alias exit) | reopen:<kind> | layout:<id>; anything else = desktop + advisory. */
export function lastWindowClose(value: unknown, data: DesktopData = DESKTOP): LastClose {
  const raw = typeof value === "string" && value.trim() ? value.trim() : data.last_window_close.preset;
  const v = data.last_window_close.aliases?.[raw] ?? raw;
  if (v === "desktop") return { do: "desktop" };
  if (v === "close_app") return { do: "close_app" };
  const m = /^(reopen|layout):(.+)$/.exec(v);
  if (m) return m[1] === "reopen" ? { do: "reopen", kind: m[2] } : { do: "layout", id: m[2] };
  return { do: "desktop", unknown: v };
}

/** True when this change of the state is the close of the last window: before it a window was live, after it none, by a close. */
export function closedLast(before: WLayout, after: WState): boolean {
  return liveWindows(before).length > 0 && liveWindows(after.layout).length === 0 && after.past[after.past.length - 1]?.op === "close";
}

export interface DesktopPath { section: string; kind?: string; window?: string; layout?: string; emergency?: boolean }
/** The ways the desktop offers to put a window on the screen, in this state with these settings (P1 is: non-empty unless the person
 *  switched the emergency path off). `layouts` = the ids the person can load. */
export function desktopPaths(l: WLayout, settings: DesktopSettings, entries: KindEntry[], layouts: string[], data: DesktopData = DESKTOP): DesktopPath[] {
  const out: DesktopPath[] = [];
  for (const sec of settings.sections) {
    if (sec === "sentence" && entries.some((e) => e.kind === "chat" && e.status === "available")) out.push({ section: sec, kind: "chat" });
    if (sec === "restore") for (const w of minimizedWindows(l)) out.push({ section: sec, window: w.id });
    if (sec === "open") for (const e of entries) if (e.status === "available") out.push({ section: sec, kind: e.kind });
    if (sec === "layouts") for (const id of layouts) out.push({ section: sec, layout: id });
  }
  if (!out.length && settings.emergency) out.push({ section: "emergency", kind: data.emergency.kind, emergency: true });
  return out;
}
