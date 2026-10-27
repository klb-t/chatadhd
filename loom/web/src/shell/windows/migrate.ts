// Readers v1 -> v2 (W0, OKNA §3.1, §2.5): the file shell/data/layouts.json, the saved "last view" and the layout_mode setting.
// Total: unreadable input gives the default preset and an advisory (the fallback id is data of the file: `default`), never an exception.
import type { Advisory, WLayout } from "./model";

export interface LayoutsFile { schema?: string; default?: string; aliases?: Record<string, string>; layout_modes?: Record<string, string>; journal_limit?: number; layouts?: Record<string, Record<string, unknown>> }
const adv = (id: string, evidence: Record<string, unknown>, text: string): Advisory => ({ id, evidence, text });
const clone = <T,>(x: T): T => JSON.parse(JSON.stringify(x));

/** The two dock trees of today's views, as a v1 file has none: czat = list (collapsed) + maximized chat; siatka = list | chat | knowledge. */
function synthesize(id: string, v1: Record<string, unknown>): Record<string, unknown> {
  const w = (wid: string, kind: string, node: string, mode: string, leaf: string, extra: object = {}) =>
    ({ id: wid, kind, node, mode, dock: { leaf }, z: 0, params: {}, profile: null, title_override: null, collapsed: false, pinned: false, ...extra });
  const grid = v1.layout_mode === "views_grid" || id === "warsztat" || id === "siatka";
  const base = grid
    ? { tree: { split: "h", ratio: 0.22, a: { leaf: "L0" }, b: { split: "h", ratio: 0.5, a: { leaf: "L1" }, b: { leaf: "L2" } } },
        leaves: { L0: { windows: ["w_list"], active: "w_list" }, L1: { windows: ["w_chat"], active: "w_chat" }, L2: { windows: ["w_knowledge"], active: "w_knowledge" } },
        windows: [w("w_list", "conversation_list", "conversations:", "docked", "L0"), w("w_chat", "chat", "conv:current", "docked", "L1"), w("w_knowledge", "knowledge", "knowledge:", "docked", "L2")],
        shelf: { handles: [] } }
    : { tree: { split: "h", ratio: 0.28, a: { leaf: "L0" }, b: { leaf: "L1" } },
        leaves: { L0: { windows: ["w_list"], active: "w_list" }, L1: { windows: ["w_chat"], active: "w_chat" } },
        windows: [w("w_list", "conversation_list", "conversations:", "docked", "L0", { collapsed: true }), w("w_chat", "chat", "conv:current", "maximized", "L1")],
        shelf: { handles: ["w_list"] } };
  return { origin: "preset", device_class: "any", focus: "w_chat", channels: { conversation: null }, ...base };
}

/** Reads a layouts file of any version into v2 layouts keyed by id. v1 entries get synthesized dock trees; v2 entries pass through. */
export function readLayoutsFile(doc: unknown): { layouts: Record<string, WLayout>; default: string; limit: number; advisories: Advisory[] } {
  const out: Advisory[] = []; const d = (doc && typeof doc === "object" ? doc : {}) as LayoutsFile;
  const layouts: Record<string, WLayout> = {};
  const raw = d.layouts && typeof d.layouts === "object" ? d.layouts : {};
  const v1 = d.schema === "loom.shell_layouts/1";
  if (!v1 && d.schema !== "loom.shell_layouts/2") out.push(adv("layouts_schema_unknown", { schema: d.schema }, "Nieznana wersja pliku układów, odczytano jak v1."));
  for (const [id0, e] of Object.entries(raw)) {
    if (!e || typeof e !== "object") { out.push(adv("layout_unreadable", { id: id0 }, "Układ nie jest obiektem, pominięto.")); continue; }
    const id = id0 === "warsztat" && !(("tree" in e)) ? "siatka" : id0;   // v1 called the grid "warsztat"
    const full = ("tree" in e && "windows" in e) ? clone(e) : { ...clone(e), ...synthesize(id0, e) };
    layouts[id] = { ...full, id, schema: "loom.window_layout/2" } as unknown as WLayout;
  }
  if (!Object.keys(layouts).length) { layouts.czat = { ...synthesize("czat", {}), id: "czat", schema: "loom.window_layout/2" } as unknown as WLayout; out.push(adv("layouts_empty", {}, "Brak układów w pliku, użyto presetu „czat”.")); }
  const def = typeof d.default === "string" ? (layouts[d.default] ? d.default : layoutIdOf(d.default, d, layouts)) : "czat";
  const limit = typeof d.journal_limit === "number" && d.journal_limit >= 0 ? d.journal_limit : 50;
  return { layouts, default: layouts[def] ? def : Object.keys(layouts)[0], limit, advisories: out };
}

function layoutIdOf(name: string, d: LayoutsFile, layouts: Record<string, unknown>): string {
  const a = d.aliases?.[name] ?? name; return layouts[a] ? a : "czat";
}

/** Saved view v1 stored `"chat" | "workshop"`; v2 stores a layout id. Anything else maps to the default id. */
export function layoutIdOfSaved(layout: unknown, aliases: Record<string, string> = { chat: "czat", workshop: "siatka", warsztat: "siatka" }): string {
  return typeof layout === "string" && aliases[layout] ? aliases[layout] : typeof layout === "string" && layout ? layout : "czat";
}

/** The `layout_mode` setting: full_screen_chat | views_grid | windows (planned: "the last own layout") | `layout:<id>` | junk (-> czat with an advisory). */
export function layoutIdOfMode(mode: unknown, modes: Record<string, string> = { full_screen_chat: "czat", views_grid: "siatka", windows: "last" }): { id: string; advisory?: Advisory } {
  if (typeof mode === "string") {
    if (mode.startsWith("layout:") && mode.length > 7) return { id: mode.slice(7) };
    if (Object.prototype.hasOwnProperty.call(modes, mode)) return { id: modes[mode] };
  }
  return { id: "czat", advisory: adv("layout_mode_unknown", { mode }, "Nieznana wartość layout_mode, otwarto układ „czat”.") };
}

/** A hash `#czat[/id]`, `#warsztat`, `#uklad/<id>[/<conv>]`: the layout id and the conversation (W1 wires it; the pure part is here). */
export function parseLayoutHash(hash: string): { layout: string; conversationId: string | null } | null {
  const m = /^#(warsztat|siatka|czat)(?:\/([A-Za-z0-9_-]+))?$/.exec(hash) ?? /^#(uklad)\/([A-Za-z0-9_-]+)(?:\/([A-Za-z0-9_-]+))?$/.exec(hash);
  if (!m) return null;
  if (m[1] === "uklad") return { layout: m[2], conversationId: m[3] ?? null };
  return { layout: m[1] === "warsztat" || m[1] === "siatka" ? "siatka" : "czat", conversationId: m[2] ?? null };   // W3: #siatka too; #warsztat = siatka until W4 (§11 pt 3)
}
