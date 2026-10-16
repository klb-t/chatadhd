// The glyphs of a disclosure (UI25): which sign a collapsed and an expanded section shows. Data, not code: the setting
// disclosure_glyphs names a set (disclosure_glyph_sets adds own ones); the result becomes CSS variables used by Disclosure and by every
// <details> summary, so the whole app speaks one notation.

export interface DisclosureGlyphs { collapsed: string; expanded: string }

/** Shipped sets. Preset: down_expands — ▼ on a collapsed section means "expand downwards" (the Windows 3.x drop-down
 *  notation the owner asked for), ▲ on an expanded one means "collapse upwards". U+FE0E keeps ▶ a text glyph on Android. */
export const GLYPH_SETS: Record<string, DisclosureGlyphs> = {
  down_expands: { collapsed: "▼", expanded: "▲" },
  tree: { collapsed: "▶︎", expanded: "▼" },
  plus_minus: { collapsed: "[+]", expanded: "[-]" },
};
export const GLYPHS_PRESET = "down_expands";

/** The settings: disclosure_glyphs names a set, disclosure_glyph_sets adds or replaces sets {name: {collapsed, expanded}}.
 *  An unknown name falls back to the preset; a missing or empty half keeps the preset sign. */
export function resolveGlyphs(name: unknown, ownSets?: unknown): DisclosureGlyphs {
  const preset = GLYPH_SETS[GLYPHS_PRESET];
  const own = ownSets && typeof ownSets === "object" ? (ownSets as Record<string, unknown>) : {};
  const key = typeof name === "string" && name ? name : GLYPHS_PRESET;
  const chosen = own[key] ?? GLYPH_SETS[key];
  if (!chosen || typeof chosen !== "object") return preset;
  const s = chosen as Record<string, unknown>;
  const pick = (v: unknown, fallback: string) => (typeof v === "string" && v.length > 0 ? v : fallback);
  return { collapsed: pick(s.collapsed, preset.collapsed), expanded: pick(s.expanded, preset.expanded) };
}

/** A CSS string literal for `content:`: quotes, backslashes and line breaks escaped. */
export function cssString(text: string): string {
  return `"${text.replace(/\\/g, "\\\\").replace(/"/g, '\\"').replace(/\r?\n/g, "\\A ")}"`;
}

/** --disclosure-glyph-collapsed / --disclosure-glyph-expanded for the document root. */
export function glyphVariables(name: unknown, ownSets?: unknown): Record<string, string> {
  const g = resolveGlyphs(name, ownSets);
  return { "--disclosure-glyph-collapsed": cssString(g.collapsed), "--disclosure-glyph-expanded": cssString(g.expanded) };
}
