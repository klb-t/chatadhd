import languages from "./data/languages.json";

/** The one place where "which language, and what when a text is missing" is decided. All of it is data (shell/data/languages.json and the
 * `locales` of the catalogs): a new language is added by data only. */
interface LanguageData { default: string; fallback: Record<string, string[]>; names: Record<string, string> }
const DATA = languages as unknown as LanguageData;

/** The language tags of the device, most wanted first (the whole list with its order, not just the first tag). */
export function deviceLanguages(): readonly string[] {
  if (typeof navigator === "undefined") return [];
  const list = navigator.languages && navigator.languages.length > 0 ? navigator.languages : navigator.language ? [navigator.language] : [];
  return list.filter((tag): tag is string => typeof tag === "string" && tag !== "");
}

/** The languages the catalogs have (the union of their `locales`) plus those the data file names. */
export function availableLanguages(catalogs: ReadonlyArray<{ locales?: Record<string, unknown> } | null | undefined>): string[] {
  const found = new Set<string>(Object.keys(DATA.names));
  for (const c of catalogs) for (const code of Object.keys(c?.locales ?? {})) found.add(code);
  return [...found];
}

export function languageName(code: string): string { return DATA.names[code] ?? code; }

/** The order in which a text is looked for: the language, its configured fallbacks, then the default language. */
export function languageChain(language: string): string[] {
  const chain = [language, ...(DATA.fallback[language] ?? []), DATA.default];
  return chain.filter((code, i) => code !== "" && chain.indexOf(code) === i);
}

const seen = new Set<string>();
/** A text missing in the wanted language: one advisory per (language, id) for the developer (console and `window.__loomMissingText`);
 * the person sees the fallback language's text, never the key. */
export function adviseMissingText(language: string, id: string, usedLanguage: string | null): void {
  const key = `${language}:${id}`;
  if (seen.has(key)) return;
  seen.add(key);
  try {
    const w = window as unknown as { __loomMissingText?: Record<string, string | null> };
    (w.__loomMissingText ??= {})[key] = usedLanguage;
    console.warn(`[i18n] missing text "${id}" in "${language}"; ${usedLanguage ? `shown from "${usedLanguage}"` : "no language has it"}`);
  } catch { /* no window (unit tests): the advisory is only for a developer */ }
}
