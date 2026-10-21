import { createContext, useContext } from "react";
import { featureInLanguage, featureMessage } from "../onboarding/presentation.mjs";

type Feature = Parameters<typeof featureMessage>[0];
export type ShellT = (id: string, params?: Record<string, string>) => string;

/** Words of the shell from a data catalog (presentation.ui_shell, graph_screen, settings_human, roles) in one language; without a catalog
 * the machine name is shown. The language's catalog is taken whole or not (partial): a word it lacks shows its id, never the English word
 * on a Polish screen — the key-parity unit tests keep the catalogs complete, this keeps a gap visible instead of switching the screen. */
export function shellTranslator(feature: Feature | null | undefined, language: string): ShellT {
  const f = feature ? featureInLanguage(feature, language, { partial: true }) : null;
  return (id, params = {}) => {
    if (!f) return id;
    try { return featureMessage(f, id, params); } catch { return id; }
  };
}
export const ShellText = createContext<ShellT>((id) => id);
export const useShellText = (): ShellT => useContext(ShellText);
/** The language of the shell words (ui_language or the browser's): for labels that come from data as {en, pl}. */
export const ShellLanguage = createContext<string>("en");
export const useShellLanguage = (): string => useContext(ShellLanguage);
/** A label from data: a string, or {en, pl} in the language of the shell, else en. */
export function dataLabel(label: unknown, language: string, fallback: string): string {
  if (typeof label === "string") return label;
  if (label && typeof label === "object") {
    const table = label as Record<string, unknown>;
    const text = table[language] ?? table.en;
    if (typeof text === "string") return text;
  }
  return fallback;
}

/** The first-run card is shown when the provider key is known to be missing; a failed lookup shows nothing. */
export function needsProviderKey(hasKey: boolean | null): boolean { return hasKey === false; }
