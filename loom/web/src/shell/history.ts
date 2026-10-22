// M1-W0 / §3.14: one browser-history entry per open layer (sheet, menu, card, panel, path step of the navigator), so that the Android
// Back button and the browser's Back close the topmost layer instead of leaving the app. The contract with the Android shell (S2):
// window.__loomBack() is synchronous and returns true when it closed a layer; the shell then tries the WebView history and, at the
// root, asks before leaving (android_back_exit_confirm). Escape on a keyboard does the same as Back.
//
// Design: the number of history entries above the base always follows the number of open layers (reconcile). A layer closed by the page
// and another opened in the same moment (the ⋮ menu closes, a panel opens) therefore reuse one entry instead of a back + push pair that
// would race (history.back() is asynchronous: a push made before its popstate would be undone by it). The stack is pure logic over a
// small "host", so the tests drive it without a browser.
//
// M1-FLAKE1: a popstate is classified by WHAT it is, not by how long ago we asked. Every history.back() of our own is remembered (a
// count of backs in flight and the depth the history stands on) until a popstate answers it: the backs land one below the other
// (top-1, top-2, ...), so a popstate whose depth is one of those landings is ours however late it comes (a loaded phone can deliver it
// seconds later, after the app has already opened new layers) and only releases the wait. backTimeoutMs no longer turns a late own
// popstate into the person's Back: it only says how long the stack waits before it goes on (a further back, or the missing pushes)
// while the answer is still outstanding. Any other popstate is the person's (Back, a hash link, Forward).

export interface LayerEntry { id: string; close: () => void }
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
export interface LayerStackOptions {
  /** How long the stack waits for the popstate of a history.back() of its own before it goes on without the answer (a further back,
   *  or the pushes that are missing - the safety net for a back that never answers). The back stays remembered: its popstate, however
   *  late, is still recognised as ours. */
  backTimeoutMs?: number;
  /** Optional: after this long without any popstate an own back is forgotten for good, and a popstate that comes later is the
   *  person's. Preset: never (a lost back can then swallow one Back of the person that lands on exactly its depth). */
  forgetBackAfterMs?: number;
  now?: () => number;
}

/** Preset of LayerStackOptions.backTimeoutMs. */
export const BACK_TIMEOUT_PRESET_MS = 1000;

export class LayerStack {
  private entries: LayerEntry[] = [];
  private readonly host: HistoryHost;
  private readonly timeoutMs: number;
  private readonly now: () => number;
  private readonly forgetMs: number | null;
  private inflight = 0;   // backs of our own issued and not yet answered by a popstate
  private top = 0;        // while inflight > 0: the depth of the entry the history stands on (the backs land at top-1, top-2, ...)
  private since = 0;      // when the newest own back was issued, or the last one answered
  constructor(host: HistoryHost, options: LayerStackOptions = {}) {
    this.host = host;
    this.timeoutMs = options.backTimeoutMs ?? BACK_TIMEOUT_PRESET_MS;
    this.forgetMs = typeof options.forgetBackAfterMs === "number" ? options.forgetBackAfterMs : null;
    this.now = options.now ?? (() => Date.now());
  }

  /** The ids of the open layers, bottom first. */
  get ids(): string[] { return this.entries.map(e => e.id); }
  get size(): number { return this.entries.length; }
  has(id: string): boolean { return this.entries.some(e => e.id === id); }

  /** Opens a layer. Pushing an id that is already open replaces its close handler and adds nothing. */
  push(id: string, close: () => void): void {
    const existing = this.entries.find(e => e.id === id);
    if (existing) { existing.close = close; return; }
    this.entries.push({ id, close });
    this.reconcile();
  }

  /** The page closed a layer itself (the × button, a tap outside, the layer replaced by another): the stack forgets it and the
   *  history gives its entry back, so Back does not land on a closed layer. */
  remove(id: string): void {
    const index = this.entries.findIndex(e => e.id === id);
    if (index < 0) return;
    this.entries.splice(index, 1);
    this.reconcile();
  }

  /** popstate: `depth` is the depth of the entry the history now stands on. A popstate that lands where one of our own backs lands
   *  (top-1 ... top-inflight) is the answer to it - however late - and only releases the wait; any other one is the person (Back, a
   *  hash link, Forward): every layer above the entry closes, topmost first. Returns how many closed. */
  popped(depth: number): number {
    this.forgetSilentBacks();
    const landed = this.inflight > 0 ? this.top - depth : 0;
    if (landed >= 1 && landed <= this.inflight) {
      this.inflight -= landed;       // the backs that landed (a browser may coalesce them) are answered
      this.top = depth;
      this.since = this.now();
      this.reconcile();
      return 0;
    }
    this.inflight = 0;               // the history moved by someone else: our older backs are done or lost
    let closed = 0;
    while (this.entries.length > depth) {
      const entry = this.entries.pop()!;
      closed += 1;
      try { entry.close(); } catch { /* a layer that is already gone */ }
    }
    this.reconcile();
    return closed;
  }

  /** Back from the shell or Escape: closes the topmost layer now (synchronously). True when a layer was closed; false at the root,
   *  so the caller may go back in the WebView history or ask before leaving. */
  back(): boolean {
    const entry = this.entries.pop();
    if (!entry) return false;
    try { entry.close(); } catch { /* a layer that is already gone */ }
    this.reconcile();
    return true;
  }

  /** Brings the history to the depth of the stack: pushes the missing entries, or goes back one entry and waits for its popstate.
   *  While a back of ours is unanswered the history is not pushed over (the push would be undone by it): the stack waits for the
   *  popstate, and only after backTimeoutMs goes on - with a further back counted from where the history will stand, or with the
   *  entries it lacks above the one it stands on. */
  private reconcile(): void {
    this.forgetSilentBacks();
    const want = this.entries.length;
    if (this.inflight > 0) {
      if (this.now() - this.since < this.timeoutMs) return;
      if (this.top - this.inflight > want) {              // more layers closed meanwhile: the next back lands one below the last
        this.inflight += 1;
        this.since = this.now();
        this.host.back();
      } else {
        // The history will end at top-inflight, at or above the stack unless the page grew: only entries above `top` are missing for
        // sure (below it the late back leaves the entries it needs, and its answer re-runs this).
        for (let depth = this.top + 1; depth <= want; depth += 1) { this.host.push(depth); this.top = depth; }
      }
      return;
    }
    const have = this.host.currentDepth();
    if (have < want) {
      for (let depth = have + 1; depth <= want; depth += 1) this.host.push(depth);
    } else if (have > want) {
      this.inflight = 1;
      this.top = have;
      this.since = this.now();
      this.host.back();
    }
  }

  /** forgetBackAfterMs: a back that has been silent that long is dropped (preset: never). */
  private forgetSilentBacks(): void {
    if (this.inflight > 0 && this.forgetMs !== null && this.now() - this.since >= this.forgetMs) this.inflight = 0;
  }
}

const STATE_KEY = "loomDepth";

/** The host over window.history. The base entry is given depth 0 so that a reload never starts in the middle of a stack. */
export function browserHost(win: Window = window): HistoryHost {
  const depthOf = (state: unknown): number => {
    const d = state && typeof state === "object" ? (state as Record<string, unknown>)[STATE_KEY] : undefined;
    return typeof d === "number" && Number.isFinite(d) && d >= 0 ? d : 0;
  };
  const stateWith = (depth: number) => ({ ...(win.history.state && typeof win.history.state === "object" ? win.history.state : {}), [STATE_KEY]: depth });
  return {
    push: depth => { try { win.history.pushState(stateWith(depth), ""); } catch { /* a page without history */ } },
    replace: depth => { try { win.history.replaceState(stateWith(depth), ""); } catch { /* same */ } },
    back: () => { try { win.history.back(); } catch { /* same */ } },
    currentDepth: () => depthOf(win.history.state),
  };
}

declare global {
  interface Window { __loomBack?: () => boolean; __loomLayers?: LayerStack }
}

/** Installs the one stack of the page: popstate settles it, Escape closes the topmost layer, window.__loomBack() answers the shell.
 *  Idempotent: a second call returns the stack already installed. */
export function installLayerStack(win: Window = window, options: LayerStackOptions = {}): LayerStack {
  if (win.__loomLayers) return win.__loomLayers;
  const host = browserHost(win);
  const stack = new LayerStack(host, options);
  // A page that comes back with a stale depth (reload inside a stack) starts at the base; the entries behind it are released by
  // the first Back (popped → reconcile).
  if (host.currentDepth() !== 0) host.replace(0);
  win.addEventListener("popstate", () => { stack.popped(host.currentDepth()); });
  win.addEventListener("keydown", event => {
    if (event.key !== "Escape" || event.defaultPrevented || event.isComposing) return;
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
