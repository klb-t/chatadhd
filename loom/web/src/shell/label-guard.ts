// M1-G1b (3): one label, one screen. The owner met two controls with the same words that opened different screens ("Ta rozmowa w grafie":
// the ⋮ menu opened the navigator, the Workshop button the old force-directed map). This file lists every control of the shell that names a
// screen - the entries of the ⋮ menu (shell/data/layouts.json) and the buttons of the Workshop (shell/panels.ts) - with the words it shows in a
// language and the screen it opens, and finds the labels that lead to more than one screen. Nothing here knows a word: the labels are read from
// the catalogue passed in (presentation.ui_shell of conversation_view.pack), the screens from the data, so a new entry or a renamed word is
// checked by the unit test (tests/shell/label-guard.test.mjs) without a line changed here.
import { CHAT_MENU, menuEntries, parseMenuAction, type MenuEntry, type MenuTarget } from "./menu";
import { PANEL_LABELS, PANEL_TEXT_IDS, SHELL_PANELS, type PanelId } from "./panels";

export interface ShellControl {
  /** Where the control is: "menu" (⋮ in the chat) or "workshop" (the navigation of the Workshop). */
  where: "menu" | "workshop";
  id: string;
  /** The words as the screen shows them in the language of the catalogue. */
  label: string;
  /** The screen the control opens, as one string: two controls that open the same screen have the same value. */
  opens: string;
}

/** The screen a menu action opens. The Workshop button of the navigator opens it at the root, as the menu entry "Graf" does. */
export function opensOfTarget(target: MenuTarget): string {
  switch (target.kind) {
    case "panel": return target.panel === "navigator" ? "navigator:" : `panel:${target.panel}`;
    case "navigator": return `navigator:${target.focus}`;
    case "sheet": return `sheet:${target.sheet}`;
    case "workshop": return "layout:workshop";
    case "connection": return "connection";
    case "knowledge": return "knowledge";
    default: return `unknown:${target.action}`;
  }
}

/** The word of a catalogue id; a missing word shows its id (that is what the screen does), so the guard sees what the person sees. */
const say = (words: Record<string, string>, id: string): string => words[id] ?? id;

/** Every control of the shell that names a screen, with its words in the language of `words` (the ui_shell catalogue of one language).
 *  Workshop: the button "Wróć do czatu" and one button per panel (App.tsx draws Object.keys(PANEL_LABELS)); a panel with no word of its own keeps its English label in every language. */
export function shellControls(words: Record<string, string>, menu: readonly MenuEntry[] = CHAT_MENU, panels: readonly PanelId[] = SHELL_PANELS): ShellControl[] {
  const out: ShellControl[] = [];
  for (const entry of menuEntries(menu, { androidShell: true })) out.push({ where: "menu", id: entry.id, label: say(words, entry.label), opens: opensOfTarget(parseMenuAction(entry.action)) });
  out.push({ where: "workshop", id: "chat", label: say(words, "workshop_back"), opens: "layout:chat" });
  for (const panel of panels) {
    const wordId = PANEL_TEXT_IDS[panel];
    out.push({ where: "workshop", id: panel, label: say(words, wordId ?? PANEL_LABELS[panel]), opens: opensOfTarget({ kind: "panel", panel }) });
  }
  return out;
}

/** Labels compare as a person reads them: no case, no extra spaces. */
export const normalLabel = (label: string): string => label.trim().replace(/\s+/g, " ").toLocaleLowerCase();

export interface LabelCollision { label: string; controls: ShellControl[] }

/** The labels that lead to more than one screen. The same label on two controls that open the SAME screen (the menu's "Ustawienia" and the Workshop's) is fine. */
export function labelCollisions(controls: readonly ShellControl[]): LabelCollision[] {
  const groups = new Map<string, ShellControl[]>();
  for (const control of controls) {
    const key = normalLabel(control.label);
    groups.set(key, [...(groups.get(key) ?? []), control]);
  }
  return [...groups.entries()]
    .filter(([, list]) => new Set(list.map(control => control.opens)).size > 1)
    .map(([label, list]) => ({ label, controls: list }));
}
