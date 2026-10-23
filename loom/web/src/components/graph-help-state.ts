// M1-G1b (4): the help texts of the graph screens (presentation.graph_screen help.1..N for the navigator, help_old.1..N for the old force-directed map). The words
// were in the catalogue and no screen drew them. The help sits under the path row in a disclosure, closed by default, and whether it is open is remembered for
// the person in the settings (config.json, the same path as every setting), not in the browser. This file is the pure part: which setting, how a setting
// is read, which lines exist. Nothing here knows a sentence.

/** Per screen: the setting that remembers whether its help is open and the prefix of its words. A new screen is a new row. */
export const HELP_SCREENS = {
  navigator: { setting: "navigator_help_open", prefix: "help" },
  old_graph: { setting: "graph_view_help_open", prefix: "help_old" },
} as const;
export type HelpScreen = keyof typeof HELP_SCREENS;

/** The setting as loaded: open only when it says so (true, "true", 1); anything else, a missing value included, is the preset: closed. */
export function helpOpenOf(setting: unknown): boolean {
  return setting === true || setting === 1 || (typeof setting === "string" && setting.trim().toLowerCase() === "true");
}

/** The lines of a help: <prefix>.1, <prefix>.2, ... for as long as the catalogue has them (a missing word shows its id, which ends the list). No fixed count. */
export function helpLines(t: (id: string) => string, prefix: string): string[] {
  const lines: string[] = [];
  for (let n = 1; ; n++) {
    const id = `${prefix}.${n}`, text = t(id);
    if (text === id || text === "") break;
    lines.push(text);
  }
  return lines;
}
