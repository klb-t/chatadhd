import { featureMessage, type PresentationFeature } from "../onboarding/presentation.mjs";
import type { JsonValue } from "../onboarding/types";

/** Presentation feature holding the texts (`presentation.open_settings`). */
export const OPEN_SETTINGS_FEATURE = "open_settings";
/** Event on the window: a part of the page asks the shell to open one of its panels (the shell listens, the part does not know the shell). */
export const OPEN_PANEL_EVENT = "loom:open-panel";
/** The panel that holds the secrets (keys, tokens): they are entered there and never in the conversation. */
export const SETTINGS_PANEL = "settings";

/** Index signature keeps the module lexically scannable by the literal guard. */
interface Row { [key: string]: JsonValue }
interface Meta { [key: string]: unknown }
/** What a finished action asks the person to do in Settings: the secrets to enter (an action that connects a storage names them). */
export interface OpenSettingsHint { section: string; keys: string[] }

function text(value: unknown): string {
  return typeof value === "string" ? value : "";
}

/** The request a message carries (metadata.action_run.open_settings), or null when it asks nothing. */
export function openSettingsHint(metadata: Meta | undefined): OpenSettingsHint | null {
  const run = metadata ? (metadata.action_run as { open_settings?: unknown } | undefined) : undefined;
  const raw = run && run.open_settings !== null && typeof run.open_settings === "object" ? (run.open_settings as { [key: string]: unknown }) : null;
  if (!raw) return null;
  const keys = Array.isArray(raw.keys) ? raw.keys.filter((key): key is string => typeof key === "string" && key !== "") : [];
  if (keys.length === 0) return null;
  return { section: text(raw.section) === "" ? "secrets" : text(raw.section), keys };
}

/** The button and the line above it, in the page's language (words from the catalog; without one the machine names are shown). */
export function openSettingsTexts(feature: PresentationFeature | null, hint: OpenSettingsHint): { button: string; note: string } {
  const parameters: Row = { keys: hint.keys.join(", "), count: hint.keys.length };
  if (feature) {
    try { return { button: featureMessage(feature, "button", parameters), note: featureMessage(feature, "note", parameters) }; } catch { /* the catalog lacks the words: the names below */ }
  }
  return { button: SETTINGS_PANEL, note: hint.keys.join(", ") };
}

/** Asks the shell to open the panel. Returns whether the event could be sent (a page without a window cannot). */
export function requestOpenPanel(panel: string, target?: { dispatchEvent(event: Event): boolean }): boolean {
  const host = target ?? (typeof window === "undefined" ? undefined : window);
  if (!host || typeof CustomEvent === "undefined") return false;
  host.dispatchEvent(new CustomEvent(OPEN_PANEL_EVENT, { detail: { panel } }));
  return true;
}

/** The panel a request names, or null when the event is not a request or names none. */
export function panelOfRequest(event: unknown): string | null {
  const detail = event !== null && typeof event === "object" ? (event as { detail?: unknown }).detail : undefined;
  const panel = detail !== null && typeof detail === "object" ? text((detail as { panel?: unknown }).panel) : "";
  return panel === "" ? null : panel;
}
