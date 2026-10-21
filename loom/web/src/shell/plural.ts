// M1-W0 r2:b1: one plural helper for every screen that counts ("1 węzeł / 2 węzły / 5 węzłów", "1 pole / 2 pola / 5 pól").
// The form comes from the language (Intl.PluralRules: pl one/few/many/other, en one/other), the word of the form from the
// catalogue (count.<kind>.<form>); a form the catalogue lacks shows its id, never a wrong word.
import type { ShellT } from "./text";

export function pluralCategory(n: number, language: string): string {
  try { return new Intl.PluralRules(language).select(n); } catch { return "other"; /* an unknown language: the word for "other" */ }
}

/** "{{n}} <word in the right form>" for count.<kind>.<form> of the catalogue the translator holds. */
export function countPhrase(t: ShellT, kind: string, n: number, language: string): string {
  return t(`count.${kind}.${pluralCategory(n, language)}`, { n: String(n) });
}
