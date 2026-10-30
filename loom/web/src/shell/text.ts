import { createContext, useContext } from "react";
import { featureMessage, setProductName, productName, resolvePresentation, resolvePresentationFeature } from "../onboarding/presentation.mjs";
import { adviseMissingText, languageChain } from "./language-pack";

type Feature = Parameters<typeof featureMessage>[0];
export type ShellT = (id: string, params?: Record<string, string>) => string;

/** Words of the shell from a data catalog (presentation.ui_shell, graph_screen, settings_human, roles) in one language. A word the language
 * lacks comes from its fallback chain (language-pack.ts: configured fallbacks, then the default language) with one developer advisory per
 * word; only when no language has it is the id returned. The person sees a text in another language rather than a key. */
export function shellTranslator(feature: Feature | null | undefined, language: string): ShellT {
  const locales = (feature as { locales?: Record<string, Record<string, unknown>> } | null | undefined)?.locales;
  const own = locales?.[language];
  if (feature && typeof (own ?? feature.catalog).product_name === "string") setProductName((own ?? feature.catalog).product_name as string);
  const chain = languageChain(language);
  return (id, params = {}) => {
    if (!feature) return id;
    for (const code of chain) {
      const catalog = code === feature.locale ? feature.catalog : locales?.[code];
      if (!catalog || typeof catalog[id] !== "string") continue;
      if (code !== language) adviseMissingText(language, id, code);
      try { return featureMessage({ ...feature, locale: code, catalog: catalog as Record<string, string> }, id, params); } catch { return id; }
    }
    adviseMissingText(language, id, null);
    return id;
  };
}
/** The product name on screen (see presentation.mjs): for places outside the catalogs (tab title, placeholders). */
export { productName };
/** Without a provider (a screen or a test rendered on its own) the words come from the built-in ui_shell catalog in the default language of
 * languages.json, built on first use, so the person never sees a raw key; the app's provider still sets the language and its catalog. */
let builtin: ShellT | null = null;
const builtinWords: ShellT = (id, params) => {
  builtin ??= shellTranslator(resolvePresentationFeature(resolvePresentation(), "ui_shell") as Feature, languageChain("")[0]);
  return builtin(id, params);
};
export const ShellText = createContext<ShellT>(builtinWords);
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
