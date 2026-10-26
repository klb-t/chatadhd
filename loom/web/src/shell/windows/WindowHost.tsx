// W1 (OKNA §3.2): renders the active layout of the window model. A layout with a maximized window (the preset `czat`: the list collapsed,
// the chat maximized) shows that window's content full-screen — the plain chat app is this case, not a branch on the layout's name.
// Windows opened over the layout (panels, §3.3) are drawn after it, the top one visible. A layout without a maximized window is a grid of
// docks; until its dock tree is drawn (W3) the host hands it to `grid`, the former Workshop markup, with the overlay to place inside it.
import type { ReactNode } from "react";
import type { WState, Win } from "./model";
import { baseWindow, overlays } from "./session";

export interface WindowHostProps {
  state: WState;
  /** The content of a window of the layout shown full-screen (kind `chat` → ChatShell). */
  renderWindow: (w: Win) => ReactNode;
  /** The content of the top window opened over the layout. */
  renderOverlay: (w: Win) => ReactNode;
  /** A layout without a maximized window (W3 draws its tree); receives the overlay node. */
  grid: (overlay: ReactNode) => ReactNode;
}

export default function WindowHost({ state, renderWindow, renderOverlay, grid }: WindowHostProps) {
  const l = state.layout;
  const over = overlays(l);
  // one node, not a keyed list: the overlay element stays the same DOM node when one panel replaces another, as before W1
  const overlay = over.length ? renderOverlay(over[over.length - 1]) : null;
  const base = baseWindow(l);
  if (!base) return <>{grid(overlay)}</>;
  return <>{renderWindow(base)}{overlay}</>;
}
