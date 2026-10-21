// The ⋮ menu of the shell (M1-W0 / §3.13): entries come from data (shell/data/layouts.json, profile Czat). This file only reads them:
// which entries are shown in this host and what an action id means, so a unit test can check every entry of the data without a browser.
import layouts from "./data/layouts.json";

export interface MenuEntry { id: string; label: string; action: string; when?: string }
export interface MenuEnv { androidShell: boolean }
export type MenuTarget =
  | { kind: "panel"; panel: string }
  | { kind: "navigator"; focus: string }
  | { kind: "sheet"; sheet: "views" | "semantic" }
  | { kind: "workshop" }
  | { kind: "connection" }
  | { kind: "knowledge" }
  | { kind: "unknown"; action: string };

export const SHEETS = ["views", "semantic"] as const;

/** The entries of the menu in the layout `czat`, in the order of the data. */
export const CHAT_MENU: readonly MenuEntry[] = layouts.layouts.czat.menu as MenuEntry[];

/** The entries this host shows: an entry with `when: android_shell` only inside the Android shell. */
export function menuEntries(menu: readonly MenuEntry[], env: MenuEnv): MenuEntry[] {
  return menu.filter(entry => entry.when === undefined || (entry.when === "android_shell" && env.androidShell));
}

/** What an action id of the menu data means. An id nothing handles is `unknown` (the test of the data keeps the data free of them). */
export function parseMenuAction(action: string): MenuTarget {
  if (action.startsWith("panel:")) return { kind: "panel", panel: action.slice(6) };
  // navigator:<focus> opens the graph navigator at that node ("" = the root of the graph).
  if (action.startsWith("navigator:")) return { kind: "navigator", focus: action.slice(10) };
  if (action.startsWith("sheet:")) {
    const sheet = action.slice(6);
    if ((SHEETS as readonly string[]).includes(sheet)) return { kind: "sheet", sheet: sheet as "views" | "semantic" };
  }
  if (action === "layout:workshop") return { kind: "workshop" };
  if (action === "connection") return { kind: "connection" };
  if (action === "knowledge") return { kind: "knowledge" };
  return { kind: "unknown", action };
}
