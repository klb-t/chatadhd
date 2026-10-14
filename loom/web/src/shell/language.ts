// R56: the language of the shell follows the setting ui_language, then the user's preference.language, then the browser, then en.
export function shellLanguage(setting: unknown, preference: unknown, browser: readonly string[], available: readonly string[]): string {
  const pick = (v: unknown): string | null => {
    if (typeof v !== "string" || v === "" || v === "auto") return null;
    const code = v.toLowerCase().split(/[-_]/)[0];
    return available.includes(code) ? code : null;
  };
  const chosen = pick(setting) ?? pick(preference);
  if (chosen) return chosen;
  for (const tag of browser) { const b = pick(tag); if (b) return b; }
  return available.includes("en") ? "en" : available[0] ?? "en";
}
