// R101 D4 (PULPIT_R101 §5): the page's side of the trash, pure. "To the trash" is reversible; "empty" is today's irreversible step.
// The kernel keeps conversations (metadata.trash), memory entries (memory.trash) and pins (link metadata.trash) and answers trash.list;
// the page adds its own layouts (trashed_at in the layout document). What an icon dropped on the trash moves is the setting drop_on_trash:
// target (preset) = the thing the icon points to, pin = only the icon [examples]; an address with no trash of its own falls back to the pin.

export interface TrashItem { kind: string; id: string; title: string; at: string; from?: string; desktop?: string; due?: boolean }
export interface TrashStep { action: string; args: Record<string, unknown> }

/** The action that moves an icon's thing to the trash. conv:<id> -> conversation.trash, memory:<id> -> memory.trash (mode target);
 *  every other address, and every address in mode pin, -> pin.trash of the icon. An unknown mode is the preset target. */
export function trashStepOf(address: string, mode: unknown, desktop?: string): TrashStep {
  const m = mode === "pin" ? "pin" : "target";
  if (m === "target" && address.startsWith("conv:") && address.length > 5) return { action: "conversation.trash", args: { query: address.slice(5), from: "desktop" } };
  if (m === "target" && address.startsWith("memory:") && address.length > 7) return { action: "memory.trash", args: { query: address.slice(7) } };
  return { action: "pin.trash", args: { target: address, ...(desktop ? { desktop } : {}) } };
}

/** Whether an icon can go to the trash at all: the trash itself and the desktop cannot (dropping the trash on itself means nothing). */
export function trashable(address: string): boolean { return !address.startsWith("trash:") && !address.startsWith("desktop:"); }

/** The action that brings one thing of the trash back (the page's own layouts come back by the page, null here). */
export function restoreStepOf(item: TrashItem): TrashStep | null {
  if (item.kind === "conversation") return { action: "conversation.untrash", args: { query: item.id } };
  if (item.kind === "memory") return { action: "memory.untrash", args: { query: item.id } };
  if (item.kind === "pin") return { action: "trash.restore", args: { query: item.id } };
  return null;
}

/** The kernel's items plus the page's layouts in the trash, newest first; a missing date sorts last. */
export function mergeTrash(kernel: unknown, layouts: { id: string; label: string; trashed_at?: string }[]): TrashItem[] {
  const out: TrashItem[] = [];
  for (const it of Array.isArray(kernel) ? kernel : []) {
    if (!it || typeof it !== "object") continue;
    const o = it as Record<string, unknown>;
    if (typeof o.kind !== "string" || typeof o.id !== "string") continue;
    out.push({ kind: o.kind, id: o.id, title: typeof o.title === "string" ? o.title : o.id, at: typeof o.at === "string" ? o.at : "",
      ...(typeof o.from === "string" ? { from: o.from } : {}), ...(typeof o.desktop === "string" ? { desktop: o.desktop } : {}), ...(o.due === true ? { due: true } : {}) });
  }
  for (const l of layouts) if (l.trashed_at) out.push({ kind: "layout", id: l.id, title: l.label, at: l.trashed_at, from: "layouts" });
  return out.sort((a, b) => (b.at || "").localeCompare(a.at || ""));
}

/** The badge of the trash icon: the count, empty when nothing is in it (the channel badge <- count of §5.2). */
export function trashBadge(count: number): string { return count > 0 ? (count > 99 ? "99+" : String(count)) : ""; }

/** Grouped by kind in the order of first appearance (the window lists by kind, each group newest first). */
export function byKind(items: TrashItem[]): [string, TrashItem[]][] {
  const m = new Map<string, TrashItem[]>();
  for (const it of items) { if (!m.has(it.kind)) m.set(it.kind, []); m.get(it.kind)!.push(it); }
  return [...m.entries()];
}

/** The advisory trash_retention_short when the setting is a number under the threshold (preset 7 days). */
export function retentionShort(days: unknown, threshold = 7): boolean { return typeof days === "number" && days < threshold; }

export const TRASH_CHANGED = "loom:trash-changed";
