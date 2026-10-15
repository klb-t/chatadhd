// One disclosure for everything on the chat screen: its state, label and aria never wait for the animation (the animation is CSS only).
export interface DisclosureLabels { show: string; hide: string }
export interface DisclosureView { expanded: boolean; label: string; chevron: "collapsed" | "expanded"; ariaExpanded: "true" | "false" }

/** The header of a disclosure for a state: the label names the action ("Pokaż ..." / "Ukryj ..."), the chevron the state. */
export function disclosureView(open: boolean, labels: DisclosureLabels): DisclosureView {
  return { expanded: open, label: open ? labels.hide : labels.show, chevron: open ? "expanded" : "collapsed", ariaExpanded: open ? "true" : "false" };
}

/** Preset duration of the height animation (ms); the setting disclosure_ms replaces it, 0 turns it off. */
export const DISCLOSURE_MS_PRESET = 150;
export function disclosureMs(setting: unknown, reducedMotion: boolean): number {
  if (reducedMotion) return 0;
  return typeof setting === "number" && Number.isFinite(setting) && setting >= 0 ? Math.round(setting) : DISCLOSURE_MS_PRESET;
}
