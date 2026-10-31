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

/** The languages the catalogs have (the union of their `locales`): a name in languages.json alone does not make a language available. */
export function availableLanguages(catalogs: ReadonlyArray<{ locales?: Record<string, unknown> } | null | undefined>): string[] {
  const found = new Set<string>();
  for (const c of catalogs) for (const code of Object.keys(c?.locales ?? {})) found.add(code);
  return [...found];
}

export function languageName(code: string): string { return DATA.names[code] ?? code; }

/** The order in which a text is looked for: the language, its configured fallbacks, then the default language. */
export function languageChain(language: string): string[] {
  const chain = [language, ...(DATA.fallback[language] ?? []), DATA.default];
  return chain.filter((code, i) => code !== "" && chain.indexOf(code) === i);
}

/** Sorting follows the same language order as labels. An unsupported/custom catalog tag falls through
 * to the next configured language; it never makes arranging icons throw. */
export function languageCollator(language: string): Intl.Collator {
  for (const code of languageChain(language)) {
    try {
      if (Intl.Collator.supportedLocalesOf(code).length) return new Intl.Collator(code, { sensitivity: "base" });
    } catch { /* a custom catalog id need not be a BCP 47 locale */ }
  }
  return new Intl.Collator(undefined, { sensitivity: "base" });
}

export type LocalizedText = Record<string, string | undefined>;
/** Transform every translation without narrowing the catalog to particular languages. */
export function mapLocalizedText(value: unknown, transform: (text: string, language: string) => string): LocalizedText {
  if (!value || typeof value !== "object" || Array.isArray(value)) return {};
  return Object.fromEntries(Object.entries(value).filter((entry): entry is [string, string] => typeof entry[1] === "string")
    .map(([language, text]) => [language, transform(text, language)]));
}
/** Command aliases in data are lists per language; every catalog contributes. */
export function localizedPhrases(value: unknown): string[] {
  if (!value || typeof value !== "object" || Array.isArray(value)) return [];
  return Object.values(value).flatMap(list => Array.isArray(list) ? list.filter((word): word is string => typeof word === "string") : []);
}
/** Labels in data use the same configured language order as presentation catalogs. Sparse legacy
 * labels may have no default-language value; keep their first available translation as a last resort. */
export function localizedText(value: unknown, language: string): string | undefined {
  if (typeof value === "string") return value;
  if (!value || typeof value !== "object" || Array.isArray(value)) return undefined;
  const table = value as Record<string, unknown>;
  for (const code of [...languageChain(language), ...Object.keys(table)]) {
    if (Object.prototype.hasOwnProperty.call(table, code) && typeof table[code] === "string") return table[code] as string;
  }
  return undefined;
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

/** A feature whose catalog answers in `language` and, for a word that language lacks, in its fallback chain (one advisory per missing word).
 * For the screens that read `feature.catalog` directly (sign-in, guest layer) instead of the shell translator. */
export function withFallback<F extends { locale: string; catalog: Record<string, string>; locales?: Record<string, Record<string, string>> }>(feature: F, language: string): F {
  const merged: Record<string, string> = {};
  const chain = languageChain(language);
  for (const code of [...chain].reverse()) Object.assign(merged, code === feature.locale ? feature.catalog : feature.locales?.[code] ?? {});
  const own = feature.locales?.[language] ?? (language === feature.locale ? feature.catalog : {});
  for (const id of Object.keys(merged)) if (typeof own[id] !== "string") adviseMissingText(language, id, chain.find((c) => typeof feature.locales?.[c]?.[id] === "string") ?? null);
  return { ...feature, locale: language, catalog: merged };
}
