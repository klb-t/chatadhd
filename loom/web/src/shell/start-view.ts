// R56: what the app opens to. An ordered list of strategies, the first that yields a view wins; new_conversation always works.
// W1 (OKNA §3.1): the view is a layout id of shell/data/layouts.json (`czat`, `siatka`, any saved one), read through windows/migrate.ts;
// the v1 values ("chat" | "workshop", key loom.shell.last.v1) are still read and mapped to `czat` / `siatka`.
import layoutsFile from "./data/layouts.json";
import { layoutIdOfMode, layoutIdOfSaved, parseLayoutHash, readLayoutsFile } from "./windows/migrate";
import type { WLayout } from "./windows/model";

export const START_VIEW_PRESET: readonly string[] = ["last_saved", "last_conversation", "new_conversation"];
/** A layout id (open set: a preset of layouts.json or a layout the person saved). */
export type Layout = string;
export interface SavedView { layout: Layout; conversationId: string | null }
export interface StartProbes {
  saved(): SavedView | null;
  conversationExists(id: string): Promise<boolean>;
  newest(): Promise<string | null>;
}
export interface StartView { source: string; layout: Layout | null; conversationId: string | null }

const FILE = readLayoutsFile(layoutsFile);
/** The layouts known to the shell (data), keyed by id. */
export const LAYOUTS: Readonly<Record<string, WLayout>> = FILE.layouts;
export const DEFAULT_LAYOUT: string = FILE.default;
export const JOURNAL_LIMIT: number = FILE.limit;
const DATA = layoutsFile as { aliases?: Record<string, string>; layout_modes?: Record<string, string>; fallbacks?: Record<string, string> };
const ALIASES: Record<string, string> = DATA.aliases ?? {};

/** Any name of a layout (id, alias, v1 value, the `last` of layout_mode) to an id that exists; unknown names give the default. */
export function resolveLayoutId(name: unknown, layouts: Readonly<Record<string, unknown>> = LAYOUTS): Layout {
  if (typeof name !== "string" || !name) return DEFAULT_LAYOUT;
  if (layouts[name]) return name;
  const alias = ALIASES[name] ?? DATA.fallbacks?.[name];
  if (alias && layouts[alias]) return alias;
  return DEFAULT_LAYOUT;
}

export function startViewOrder(setting: unknown): string[] {
  if (typeof setting === "string" && setting !== "") return [setting];
  if (Array.isArray(setting)) { const list = setting.filter((x): x is string => typeof x === "string"); if (list.length) return list; }
  return [...START_VIEW_PRESET];
}

/** layout_mode to a layout id (migrate.ts reads the value; `windows` = "the last own layout", which until W5 is the data's fallback: `siatka`). */
export function layoutOfMode(mode: unknown): Layout {
  return resolveLayoutId(layoutIdOfMode(mode, DATA.layout_modes).id);
}

export async function resolveStartView(order: string[], probes: StartProbes): Promise<StartView> {
  for (const name of order) {
    try {
      if (name === "last_saved") {
        const saved = probes.saved();
        if (saved && (saved.conversationId === null || await probes.conversationExists(saved.conversationId)))
          return { source: name, layout: saved.layout, conversationId: saved.conversationId };
      } else if (name === "last_conversation") {
        const id = await probes.newest();
        if (id) return { source: name, layout: null, conversationId: id };
      } else if (name === "new_conversation") return { source: name, layout: null, conversationId: null };
    } catch { /* a strategy that cannot answer is skipped */ }
  }
  return { source: "new_conversation", layout: null, conversationId: null };
}

/** A deep link: #warsztat, #czat[/<conversation id>] or #uklad/<id>[/<conversation id>] states the view exactly; nothing else is restored. */
export function parseShellHash(hash: string): { layout: Layout; conversationId: string | null } | null {
  const link = parseLayoutHash(hash);
  return link ? { layout: resolveLayoutId(link.layout), conversationId: link.conversationId } : null;
}

export const LAST_KEY_V1 = "loom.shell.last.v1";
export const LAST_KEY = "loom.shell.last.v2";
/** The saved "last view": v2 (layout id) first, else v1 ("chat" | "workshop" mapped to czat / siatka). */
export function readSaved(store: Pick<Storage, "getItem">): SavedView | null {
  const conv = (v: { conversation_id?: unknown }) => v.conversation_id === null || typeof v.conversation_id === "string";
  try {
    const v = JSON.parse(store.getItem(LAST_KEY) ?? "null");
    if (v && v.schema === "loom.shell.last/2" && typeof v.layout === "string" && v.layout && conv(v))
      return { layout: resolveLayoutId(v.layout), conversationId: v.conversation_id };
  } catch { /* unreadable: try v1 */ }
  try {
    const v = JSON.parse(store.getItem(LAST_KEY_V1) ?? "null");
    if (v && v.schema === "loom.shell.last/1" && (v.layout === "chat" || v.layout === "workshop") && conv(v))
      return { layout: resolveLayoutId(layoutIdOfSaved(v.layout)), conversationId: v.conversation_id };
  } catch { /* unreadable: no saved view */ }
  return null;
}
export function writeSaved(store: Pick<Storage, "setItem">, view: SavedView): void {
  try { store.setItem(LAST_KEY, JSON.stringify({ schema: "loom.shell.last/2", layout: view.layout, conversation_id: view.conversationId })); } catch { /* storage blocked */ }
}
