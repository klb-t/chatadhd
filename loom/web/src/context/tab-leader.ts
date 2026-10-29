// PH29 P1 §3.4: several tabs of the same origin, one database. One tab - the leader, chosen by the Web Locks API -
// holds the database (a dedicated Worker); the other tabs send their operations over a BroadcastChannel and get the
// answers back. When the leader goes away (tab closed, crashed) the browser releases its lock and the next waiting tab
// takes it; requests still waiting are sent again to the new leader (every store operation is idempotent: save writes
// the whole bundle's difference, load/list read, wipe removes). "One connection, one leader" is a setting
// (`exclusive`, preset true): with exclusive=false every tab opens its own handler and nothing is forwarded.
// Locks and channel are injected so the logic is tested without a browser.

export interface LockLike { name: string }
export interface LocksLike {
  request(name: string, opts: { mode?: "exclusive" | "shared"; signal?: AbortSignal }, cb: (lock: LockLike | null) => Promise<unknown>): Promise<unknown>;
}
export interface ChannelLike {
  postMessage(m: unknown): void;
  addEventListener(type: "message", fn: (e: { data: unknown }) => void): void;
  close(): void;
}

export type Handler = (op: string, args: unknown) => Promise<unknown>;

type Msg =
  | { t: "req"; id: string; from: string; op: string; args: unknown }
  | { t: "res"; id: string; to: string; ok: boolean; value?: unknown; error?: string }
  | { t: "leader"; tab: string }
  | { t: "hello"; tab: string };

export interface LeaderOptions {
  name: string;
  tabId: string;
  locks: LocksLike;
  channel: ChannelLike;
  /** Opens what the leader serves (e.g. starts the database worker). Called once, when this tab becomes the leader. */
  open: () => Promise<Handler>;
  exclusive?: boolean;
  onLeaderChange?: (leader: string | null, self: boolean) => void;
}

export interface TabLeader {
  call(op: string, args?: unknown): Promise<unknown>;
  isLeader(): boolean;
  leader(): string | null;
  /** Gives up the leadership (and the lock) and stops answering. */
  close(): Promise<void>;
}

export function createTabLeader(o: LeaderOptions): TabLeader {
  const exclusive = o.exclusive ?? true;
  let handler: Handler | null = null;
  let leaderId: string | null = null;
  let closed = false;
  let seq = 0;
  let release: (() => void) | null = null;
  let opening: Promise<Handler> | null = null;
  const pending = new Map<string, { op: string; args: unknown; resolve: (v: unknown) => void; reject: (e: Error) => void }>();
  const abort = typeof AbortController !== "undefined" ? new AbortController() : null;

  const setLeader = (id: string | null) => { leaderId = id; o.onLeaderChange?.(id, id === o.tabId); };
  const post = (m: Msg) => { if (!closed) o.channel.postMessage(m); };
  const send = (id: string) => { const p = pending.get(id); if (p) post({ t: "req", id, from: o.tabId, op: p.op, args: p.args }); };

  o.channel.addEventListener("message", (e) => {
    const m = e.data as Msg;
    if (!m || typeof m !== "object" || closed) return;
    if (m.t === "req" && handler) {
      const h = handler;
      void h(m.op, m.args).then(
        (value) => post({ t: "res", id: m.id, to: m.from, ok: true, value }),
        (err: unknown) => post({ t: "res", id: m.id, to: m.from, ok: false, error: String((err as Error)?.message ?? err) }));
    } else if (m.t === "res" && m.to === o.tabId) {
      const p = pending.get(m.id);
      if (!p) return;
      pending.delete(m.id);
      if (m.ok) p.resolve(m.value); else p.reject(new Error(m.error ?? "store error"));
    } else if (m.t === "leader") {
      // A leader ignores announcements (a late one from the tab it replaced); requests are broadcast and answered by
      // whichever tab holds the handler, so a stale id elsewhere is only information.
      if (handler) return;
      setLeader(m.tab);
      for (const id of pending.keys()) send(id);  // a new leader: whatever was waiting goes again
    } else if (m.t === "hello" && handler) {
      post({ t: "leader", tab: o.tabId });
    }
  });

  const become = async () => {
    opening = o.open();
    handler = await opening;
    setLeader(o.tabId);
    post({ t: "leader", tab: o.tabId });
    // Requests this tab queued before it won the lock are served locally now.
    for (const [id, p] of [...pending]) { pending.delete(id); handler(p.op, p.args).then(p.resolve, p.reject); }
  };

  if (exclusive) {
    void o.locks.request(o.name, { mode: "exclusive", signal: abort?.signal }, async () => {
      if (closed) return;
      await become();
      await new Promise<void>((r) => { release = r; });
    }).catch(() => { /* aborted on close */ });
    post({ t: "hello", tab: o.tabId });
  } else {
    void become();
  }

  return {
    call(op, args) {
      if (closed) return Promise.reject(new Error("store closed"));
      if (handler) return handler(op, args);
      if (opening) return opening.then((h) => h(op, args));
      return new Promise((resolve, reject) => {
        const id = `${o.tabId}:${++seq}`;
        pending.set(id, { op, args, resolve, reject });
        if (leaderId) send(id);
      });
    },
    isLeader: () => handler !== null,
    leader: () => leaderId,
    async close() {
      closed = true;
      abort?.abort();
      release?.();
      handler = null;
      for (const p of pending.values()) p.reject(new Error("store closed"));
      pending.clear();
      try { o.channel.close(); } catch { /* already closed */ }
    },
  };
}
