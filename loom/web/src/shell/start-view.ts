// R56: what the app opens to. An ordered list of strategies, the first that yields a view wins; new_conversation always works.
export const START_VIEW_PRESET: readonly string[] = ["last_saved", "last_conversation", "new_conversation"];
export type Layout = "chat" | "workshop";
export interface SavedView { layout: Layout; conversationId: string | null }
export interface StartProbes {
  saved(): SavedView | null;
  conversationExists(id: string): Promise<boolean>;
  newest(): Promise<string | null>;
}
export interface StartView { source: string; layout: Layout | null; conversationId: string | null }

export function startViewOrder(setting: unknown): string[] {
  if (typeof setting === "string" && setting !== "") return [setting];
  if (Array.isArray(setting)) { const list = setting.filter((x): x is string => typeof x === "string"); if (list.length) return list; }
  return [...START_VIEW_PRESET];
}

/** layout_mode to a layout; "windows" is planned and opens the workshop until it exists. */
export function layoutOfMode(mode: unknown): Layout {
  return mode === "views_grid" || mode === "windows" ? "workshop" : "chat";
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

/** A deep link: #warsztat or #czat[/<conversation id>] states the view exactly; nothing else is restored. */
export function parseShellHash(hash: string): { layout: Layout; conversationId: string | null } | null {
  const m = /^#(warsztat|czat)(?:\/([A-Za-z0-9_-]+))?$/.exec(hash);
  return m ? { layout: m[1] === "warsztat" ? "workshop" : "chat", conversationId: m[2] ?? null } : null;
}

export const LAST_KEY = "loom.shell.last.v1";
export function readSaved(store: Pick<Storage, "getItem">): SavedView | null {
  try {
    const v = JSON.parse(store.getItem(LAST_KEY) ?? "null");
    if (v && v.schema === "loom.shell.last/1" && (v.layout === "chat" || v.layout === "workshop") && (v.conversation_id === null || typeof v.conversation_id === "string"))
      return { layout: v.layout, conversationId: v.conversation_id };
  } catch { /* unreadable: no saved view */ }
  return null;
}
export function writeSaved(store: Pick<Storage, "setItem">, view: SavedView): void {
  try { store.setItem(LAST_KEY, JSON.stringify({ schema: "loom.shell.last/1", layout: view.layout, conversation_id: view.conversationId })); } catch { /* storage blocked */ }
}
