export interface ShellLanguageChoice {
  language: string;
  mode: "auto" | "explicit";
  source: "config" | "user_preference" | "device" | "catalog_fallback";
}

/** A saved UI choice is explicit. AUTO resets to the device, not a profile preset.
 * Legacy config values are preserved: without provenance we cannot call an old EN
 * value generated and silently replace it. Only an acknowledged user-layer profile
 * preference counts as an explicit fallback when the UI setting is absent. */
export function resolveShellLanguage(setting: unknown, preference: unknown, browser: readonly string[], available: readonly string[], preferenceLayer = "user"): ShellLanguageChoice {
  const pick = (v: unknown): string | null => {
    if (typeof v !== "string" || !v.trim() || v.trim().toLowerCase() === "auto") return null;
    const code = v.trim().toLowerCase().split(/[-_]/)[0];
    return available.includes(code) ? code : null;
  };
  const explicit = pick(setting);
  if (explicit) return { language: explicit, mode: "explicit", source: "config" };
  const reset = typeof setting === "string" && (!setting.trim() || setting.trim().toLowerCase() === "auto");
  const preferred = !reset && preferenceLayer === "user" ? pick(preference) : null;
  if (preferred) return { language: preferred, mode: "explicit", source: "user_preference" };
  for (const tag of browser) {
    const language = pick(tag);
    if (language) return { language, mode: "auto", source: "device" };
  }
  return { language: available.includes("en") ? "en" : available[0] ?? "en", mode: "auto", source: "catalog_fallback" };
}

export function shellLanguage(setting: unknown, preference: unknown, browser: readonly string[], available: readonly string[], preferenceLayer = "user"): string {
  return resolveShellLanguage(setting, preference, browser, available, preferenceLayer).language;
}
