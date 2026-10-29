// PH29 P1: the store plugin "Przeglądarka — baza SQLite" (OPFS + SQLite-WASM, VFS opfs-sahpool). The database lives
// in a dedicated Worker of ONE tab, the leader chosen with navigator.locks; other tabs forward their operations over a
// BroadcastChannel (tab-leader.ts). Without OPFS, Web Locks, BroadcastChannel or module Workers the plugin reports
// itself unavailable with the reason, and the selection falls back to the next store (advisory, not an error).
import { bundleFromText, bundleText, type GuestBundle } from "./guest-store";
import { observed, storageTraits, type StorePlugin, type Traits } from "./store-plugin";
import { createTabLeader, type ChannelLike, type Handler, type LocksLike, type TabLeader } from "./tab-leader";

export const OPFS_SQLITE_ID = "browser_opfs_sqlite";

export interface OpfsSqliteConfig {
  /** Lock and channel name (one database per name). */
  name: string;
  /** One connection held by a leader tab (preset true; §3.4 - verified for opfs-sahpool in this package). */
  exclusive: boolean;
  /** How long a non-leader waits for the leader's answer to the availability probe (preset). */
  probeTimeoutMs: number;
  vfsName: string;
  directory: string;
  dbName: string;
}

export const OPFS_SQLITE_PRESET: OpfsSqliteConfig = {
  name: "loom-store-opfs-sqlite", exclusive: true, probeTimeoutMs: 15000,
  vfsName: "loom-opfs-sahpool", directory: ".loom-store", dbName: "/working-copy.sqlite3",
};

export interface SaveReport { rows_written: number; rows_deleted: number; bytes_written: number; total_bytes?: number; canonical?: boolean; fallback: string | null; ms: number }

function workerHandler(cfg: OpfsSqliteConfig): Promise<Handler> {
  const w = new Worker(new URL("../workers/opfs-sqlite.worker.ts", import.meta.url), { type: "module" });
  let seq = 0;
  const waiting = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void }>();
  w.onmessage = (e: MessageEvent<{ id: number; ok: boolean; value?: unknown; error?: string; name?: string }>) => {
    const p = waiting.get(e.data.id);
    if (!p) return;
    waiting.delete(e.data.id);
    if (e.data.ok) p.resolve(e.data.value);
    else { const err = new Error(e.data.error ?? "worker error"); err.name = e.data.name ?? "Error"; p.reject(err); }
  };
  w.onerror = (e) => { for (const p of waiting.values()) p.reject(new Error(e.message || "worker failed")); waiting.clear(); };
  const call: Handler = (op, args) => new Promise((resolve, reject) => { const id = ++seq; waiting.set(id, { resolve, reject }); w.postMessage({ id, op, args }); });
  return call("open", { vfsName: cfg.vfsName, directory: cfg.directory, dbName: cfg.dbName }).then(() => call);
}

/** What this browser lacks for the plugin (empty = everything is there). */
export function missingPlatform(g: Record<string, unknown> = globalThis as unknown as Record<string, unknown>): string[] {
  const nav = g.navigator as { storage?: { getDirectory?: unknown }; locks?: unknown } | undefined;
  const out: string[] = [];
  if (typeof nav?.storage?.getDirectory !== "function") out.push("opfs");
  if (!nav?.locks) out.push("web_locks");
  if (typeof g.BroadcastChannel !== "function") out.push("broadcast_channel");
  if (typeof g.Worker !== "function") out.push("worker");
  return out;
}

export function createOpfsSqliteStore(cfg: OpfsSqliteConfig = OPFS_SQLITE_PRESET): StorePlugin & { lastSave: () => SaveReport | null; info: () => Promise<Record<string, unknown>>; leader: () => TabLeader | null } {
  let leader: TabLeader | null = null;
  let last: SaveReport | null = null;
  let obs: Record<string, unknown> | null = null;
  const tabId = `t_${Math.random().toString(16).slice(2, 10)}${Date.now().toString(16)}`;
  const start = () => {
    if (leader) return leader;
    const nav = navigator as unknown as { locks: LocksLike };
    leader = createTabLeader({ name: cfg.name, tabId, locks: nav.locks, channel: new BroadcastChannel(cfg.name) as unknown as ChannelLike,
      exclusive: cfg.exclusive, open: () => workerHandler(cfg) });
    return leader;
  };
  const call = (op: string, args?: unknown) => start().call(op, args);
  const withTimeout = <T,>(p: Promise<T>, ms: number) => Promise.race([p, new Promise<never>((_, rej) => setTimeout(() => rej(new Error(`no answer from the store in ${ms} ms`)), ms))]);

  return {
    id: OPFS_SQLITE_ID, kind: "opfs_sqlite", label: "Przeglądarka — baza SQLite", status: "implemented",
    async probe() {
      const miss = missingPlatform();
      if (miss.length) return { available: false, reason: `missing: ${miss.join(", ")}` };
      try { obs = await withTimeout(call("info") as Promise<Record<string, unknown>>, cfg.probeTimeoutMs); return { available: true }; }
      catch (e) { return { available: false, reason: String((e as Error).message ?? e) }; }
    },
    async traits(): Promise<Traits> {
      const t: Traits = {
        durable: observed(true, "plugin: OPFS outlives the tab and the session"),
        survives_site_data_clear: observed(false, "platform docs: OPFS belongs to the site's data"),
        offline: observed(true, "plugin"), shared_across_devices: observed(false, "plugin"),
        encrypted_at_rest: observed("none", "plugin (WebCrypto: B8)"), readers: observed("this browser", "plugin"),
        ops: observed(["incremental_write", "sql", "lock", "export_bundle"], "plugin"),
        max_object_bytes: observed("unknown", "origin quota applies, no per-value limit (to measure)"),
        available_on: observed(["web"], "plugin"),
        ...(await storageTraits()),
      };
      if (obs) {
        t.sqlite_version = observed(String(obs.sqlite_version ?? "unknown"), "sqlite_version()");
        t.vfs = observed(String(obs.vfs ?? "unknown"), "plugin");
        t.fts5 = observed(Boolean(obs.fts5), "sqlite_compileoption_used + CREATE VIRTUAL TABLE probe");
        t.integrity = observed(String(obs.integrity ?? "unknown"), "PRAGMA integrity_check");
        t.leader_tab = observed(leader?.isLeader() ?? false, "navigator.locks");
        if (obs.fallback) t.bundle_fallback = observed(String(obs.fallback), "D4: the server's original text is kept");
      }
      if (last) {
        t.last_write_bytes = observed(last.bytes_written, "last save (changed rows + header)", { unit: "B" });
        t.last_write_rows = observed(last.rows_written + last.rows_deleted, "last save");
        t.last_write_ms = observed(last.ms, "last save, in the worker", { unit: "ms" });
      }
      return t;
    },
    async save(bundle: GuestBundle) {
      last = await call("save", { text: bundleText(bundle) }) as SaveReport;
    },
    async load() {
      const r = await call("load") as { text: string | null };
      if (!r?.text) return null;
      return bundleFromText(r.text);
    },
    list: async () => await call("list") as string[],
    async wipe() {
      if (missingPlatform().length) return;
      try { await call("wipe"); } finally { obs = null; last = null; }
    },
    async close() { if (leader) { try { if (leader.isLeader()) await leader.call("close"); } catch { /* gone */ } await leader.close(); leader = null; } },
    lastSave: () => last,
    info: async () => (obs = await call("info") as Record<string, unknown>),
    leader: () => leader,
  };
}
