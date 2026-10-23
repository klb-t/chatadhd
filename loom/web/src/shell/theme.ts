// R67-fix: the setting `theme` has a reader in the web. The value is an open name (presets: dark, amoled); a name the style sheet has no palette for
// leaves the default tokens, it is never refused. Empty or non-text means "no opinion": the choice remembered on the device stays.
export function themeFromConfig(value: unknown): string | null {
  return typeof value === "string" && value.trim() !== "" ? value.trim() : null;
}
