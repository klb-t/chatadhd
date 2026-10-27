// W1 (OKNA §3.2): renders the active layout of the window model. A layout with a maximized window (the preset `czat`: the list collapsed,
// the chat maximized) shows that window's content full-screen — the plain chat app is this case, not a branch on the layout's name.
// Windows opened over the layout (panels, §3.3) are drawn after it, the top one visible. A layout without a maximized window is a grid of
// docks: W3 draws its dock tree (DockTree) and hands it to `grid` - the frame around it (the switcher bar of the former Workshop) - with
// the overlay to place inside it.
import type { ReactNode } from "react";
import type { WState, Win } from "./model";
import { baseWindow, visibleOverlay } from "./session";
import DockTree from "./DockTree";
import { dockView } from "./dock";

export interface WindowHostProps {
  state: WState;
  /** The content of a window of the layout shown full-screen (kind `chat` → ChatShell). */
  renderWindow: (w: Win) => ReactNode;
  /** The content of the top window opened over the layout. */
  renderOverlay: (w: Win) => ReactNode;
  /** The frame of a layout without a maximized window: receives its drawn dock tree and the overlay node. */
  grid: (body: ReactNode, overlay: ReactNode) => ReactNode;
  /** W3: the content of a docked window (null = not drawn now: its leaf drops out and the sibling takes the space). */
  renderDocked?: (w: Win) => ReactNode;
  /** W3: whether a docked window is drawn now (e.g. the knowledge leaf only while it is open). */
  shown?: (w: Win) => boolean;
  /** W3: the title of a window on a tab of its leaf; activating a tab. */
  title?: (w: Win) => string;
  onActivate?: (w: Win) => void;
}

export default function WindowHost({ state, renderWindow, renderOverlay, grid, renderDocked, shown, title, onActivate }: WindowHostProps) {
  const l = state.layout;
  // W2: the overlay in front (maximized); others opened over the layout wait in the switcher (sheet „Okna”) and are not drawn.
  // One node, not a keyed list: the overlay element stays the same DOM node when one panel replaces another, as before W1
  const top = visibleOverlay(l);
  const overlay = top ? renderOverlay(top) : null;
  const base = baseWindow(l);
  if (!base) {
    const tree = dockView(l, shown);
    const body = tree ? <DockTree node={tree} renderWindow={renderDocked ?? renderWindow} title={title ?? ((w) => w.kind)} onActivate={onActivate} /> : null;
    return <>{grid(body, overlay)}</>;
  }
  return <>{renderWindow(base)}{overlay}</>;
}
