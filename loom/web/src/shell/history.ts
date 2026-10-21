// M1-W0 / §3.14: one browser-history entry per layer (sheet, menu, card, path step of the navigator), so that the Android Back
// button and the browser's Back close the topmost layer instead of leaving the app. The contract with the Android shell (S2):
// window.__loomBack() is synchronous and returns true when it closed a layer; the shell then tries the WebView history and, at the
// root, asks before leaving (android_back_exit_confirm). Escape on a keyboard does the same as Back.
//
// The stack is pure logic over a small "host" (pushState / back / the current depth), so the tests drive it without a browser.

export interface LayerEntry { id: string; depth: number; close: () => void }
export interface HistoryHost {
  /** Records one entry whose state carries the depth. */
  push(depth: number): void;
  /** Rewrites the current entry's depth (the base entry at install). */
  replace(depth: number): void;
  /** Goes one entry back (asynchronous in a browser: popstate follows). */
  back(): void;
  /** The depth the current history entry carries (0 for the base or an entry without one). */
  currentDepth(): number;
}

export class LayerStack {
  private entries: LayerEntry[] = [];
  private readonly host: HistoryHost;
  constructor(host: HistoryHost) { this.host = host; }

  /** The ids of the open layers, bottom first. */
  get ids(): string[] { return this.entries.map(e => e.id); }
  get size(): number { return this.entries.length; }
  has(id: string): boolean { return this.entries.some(e => e.id === id); }

  /** Opens a layer: one history entry, whose state carries the depth of the stack after the push. Pushing an id that is already
   *  open replaces its close handler and adds nothing. */
  push(id: string, close: () => void): void {
    const existing = this.entries.find(e => e.id === id);
    if (existing) { existing.close = close; return; }
    const depth = (this.entries.length ? this.entries[this.entries.length - 1].depth : this.host.currentDepth()) + 1;
    this.entries.push({ id, depth, close });
    this.host.push(depth);
  }

  /** The page closed a layer itself (the × button, a tap outside): the stack forgets it and, when it was the topmost one and the
   *  history still stands on its entry, the history goes back one entry so Back does not land on a closed layer. */
  remove(id: string): void {
    const index = this.entries.findIndex(e => e.id === id);
    if (index < 0) return;
    const [entry] = this.entries.splice(index, 1);
    const wasTop = index === this.entries.length;
    if (wasTop && this.host.currentDepth() === entry.depth) this.host.back();
  }

  /** The history moved (popstate) to an entry of depth `depth`: every layer above it closes, topmost first. Returns how many closed. */
  settle(depth: number): number {
    let closed = 0;
    while (this.entries.length && this.entries[this.entries.length - 1].depth > depth) {
      const entry = this.entries.pop()!;
      closed += 1;
      try { entry.close(); } catch { /* a layer that is already gone */ }
    }
    return closed;
  }

  /** Back from the shell or Escape: closes the topmost layer now (synchronously) and takes its history entry back. True when a
   *  layer was closed; false at the root, so the caller may leave or ask. */
  back(): boolean {
    const entry = this.entries.pop();
    if (!entry) return false;
    const onEntry = this.host.currentDepth() === entry.depth;
    try { entry.close(); } catch { /* a layer that is already gone */ }
    if (onEntry) this.host.back();
    return true;
  }
}

const STATE_KEY = "loomDepth";

/** The host over window.history. The base entry is given depth 0 so that a reload never starts in the middle of a stack. */
export function browserHost(win: Window = window): HistoryHost {
  const depthOf = (state: unknown): number => {
    const d = state && typeof state === "object" ? (state as Record<string, unknown>)[STATE_KEY] : undefined;
    return typeof d === "number" && Number.isFinite(d) && d >= 0 ? d : 0;
  };
  return {
    push: depth => { try { win.history.pushState({ ...(win.history.state ?? {}), [STATE_KEY]: depth }, ""); } catch { /* a page without history */ } },
    replace: depth => { try { win.history.replaceState({ ...(win.history.state ?? {}), [STATE_KEY]: depth }, ""); } catch { /* same */ } },
    back: () => { try { win.history.back(); } catch { /* same */ } },
    currentDepth: () => depthOf(win.history.state),
  };
}

declare global {
  interface Window { __loomBack?: () => boolean; __loomLayers?: LayerStack }
}

/** Installs the one stack of the page: popstate settles it, Escape closes the topmost layer, window.__loomBack() answers the shell.
 *  Idempotent: a second call returns the stack already installed. */
export function installLayerStack(win: Window = window): LayerStack {
  if (win.__loomLayers) return win.__loomLayers;
  const host = browserHost(win);
  const stack = new LayerStack(host);
  // A page that comes back with a stale depth (reload inside a stack) starts at the base.
  if (host.currentDepth() !== 0) host.replace(0);
  win.addEventListener("popstate", () => { stack.settle(host.currentDepth()); });
  win.addEventListener("keydown", event => {
    if (event.key !== "Escape" || event.defaultPrevented) return;
    if (stack.back()) event.preventDefault();
  });
  win.__loomBack = () => stack.back();
  win.__loomLayers = stack;
  return stack;
}

/** The stack of the page, installing it on first use (components call this in effects). */
export function layerStack(): LayerStack | null {
  if (typeof window === "undefined") return null;
  return installLayerStack(window);
}
