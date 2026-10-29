// R103 §6: the history of a person without an account lives on HIS side. The browser store (preset) keeps one
// bundle - every conversation of the guest's working copy, exported in Loom's own JSON format - in IndexedDB, saved
// after each answer and imported back through POST /api/import when a new guest session starts with an empty
// working copy. The same bundle is what "Download archive" saves and "Load archive" reads. Other stores (file,
// folder, the person's own cloud) implement the same three calls; this file is the browser one.

// R103 B1b: the bundle is now the WHOLE working copy (`loom.guest_bundle/2`, GET /api/working-copy/bundle): database
// rows (conversations, messages with their analysis status, graph nodes and links incl. pins and trash marks), memory
// and settings - never a secret (allow-list in the kernel) - with a sha256 of its content. It is kept as the exact text
// the server produced and restored by POST /api/working-copy/bundle WITHOUT running analysis again. A v1 bundle (older
// browsers, older archive files) still restores conversation by conversation, as before.

export const ANSWER_DONE_EVENT = "loom:answer-done";
export const WORKING_COPY_CHANGED_EVENT = "loom:working-copy-changed";
export const BUNDLE_SCHEMA_V1 = "loom.guest_bundle/1";
export const BUNDLE_SCHEMA = "loom.guest_bundle/2";

export interface BundleConversation { id: string; title: string; content: string }
export interface GuestBundleV1 { schema: typeof BUNDLE_SCHEMA_V1; saved_at: string; conversations: BundleConversation[] }
/** v2 as stored: the server's exact text (its integrity hash covers the content) plus what the page needs to know. */
export interface GuestBundleV2 { schema: typeof BUNDLE_SCHEMA; saved_at: string; text: string }
export type GuestBundle = GuestBundleV1 | GuestBundleV2;

export interface GuestStore {
  save(bundle: GuestBundle): Promise<void>;
  load(): Promise<GuestBundle | null>;
  list(): Promise<string[]>;
  /** Removes everything this store holds on the device. */
  wipe(): Promise<void>;
}

const DB = "loom-guest-store", STORE = "bundles", KEY = "current";

function openDb(): Promise<IDBDatabase> {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB, 1);
    req.onupgradeneeded = () => { req.result.createObjectStore(STORE); };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

function tx<T>(mode: IDBTransactionMode, run: (s: IDBObjectStore) => IDBRequest<T>): Promise<T> {
  return openDb().then((db) => new Promise<T>((resolve, reject) => {
    const t = db.transaction(STORE, mode);
    const r = run(t.objectStore(STORE));
    t.oncomplete = () => { db.close(); resolve(r.result); };
    t.onerror = () => { db.close(); reject(t.error); };
  }));
}

export const browserStore: GuestStore = {
  save: async (bundle) => { await tx("readwrite", (s) => s.put(bundle, KEY)); },
  load: async () => {
    const v = await tx<unknown>("readonly", (s) => s.get(KEY));
    return isBundle(v) ? v : null;
  },
  list: async () => (await tx<IDBValidKey[]>("readonly", (s) => s.getAllKeys())).map(String),
  wipe: () => new Promise<void>((resolve) => {
    const req = indexedDB.deleteDatabase(DB);
    req.onsuccess = req.onerror = req.onblocked = () => resolve();
  }),
};

export function isBundle(v: unknown): v is GuestBundle {
  if (!v || typeof v !== "object") return false;
  const b = v as Record<string, unknown>;
  if (b.schema === BUNDLE_SCHEMA_V1) return Array.isArray(b.conversations);
  return b.schema === BUNDLE_SCHEMA && typeof b.text === "string";
}

/** A file or text from "Load archive": a v2 bundle as the server wrote it, a stored v2, or a v1 bundle. */
export function bundleFromText(text: string): GuestBundle | null {
  let v: unknown;
  try { v = JSON.parse(text); } catch { return null; }
  if (isBundle(v)) return v;
  if (v && typeof v === "object" && (v as Record<string, unknown>).schema === BUNDLE_SCHEMA && typeof (v as Record<string, unknown>).content === "object")
    return { schema: BUNDLE_SCHEMA, saved_at: String((v as Record<string, unknown>).saved_at ?? ""), text };
  return null;
}

/** How many things a bundle would bring back (rows of every table + memory entries); 0 = nothing to restore. */
export function bundleSize(b: GuestBundle): number {
  if (b.schema === BUNDLE_SCHEMA_V1) return b.conversations.length;
  try {
    const c = (JSON.parse(b.text) as { content?: { tables?: Record<string, { rows?: unknown[] }>; memory?: unknown[] } }).content ?? {};
    let n = Array.isArray(c.memory) ? c.memory.length : 0;
    for (const t of Object.values(c.tables ?? {})) n += Array.isArray(t?.rows) ? t.rows.length : 0;
    return n;
  } catch { return 0; }
}

/** The text a download writes: v2 exactly as the server produced it, v1 as JSON. */
export function bundleText(b: GuestBundle): string { return b.schema === BUNDLE_SCHEMA ? b.text : JSON.stringify(b); }

/** Asks the browser not to evict this site's data on its own; the answer feeds the notice's period. */
export async function requestPersistence(): Promise<{ persisted: boolean | null; freeBytes: number | null }> {
  const storage = typeof navigator !== "undefined" ? navigator.storage : undefined;
  let persisted: boolean | null = null, freeBytes: number | null = null;
  try { if (storage?.persist) persisted = await storage.persist(); } catch { persisted = null; }
  try {
    const e = storage?.estimate ? await storage.estimate() : undefined;
    if (e && typeof e.quota === "number" && typeof e.usage === "number") freeBytes = Math.max(0, e.quota - e.usage);
  } catch { freeBytes = null; }
  return { persisted, freeBytes };
}

/** Which notice text and period follow from what the store reported (§3.5: honest, from the actual store). */
export function noticePeriod(store: { persisted: boolean | null; freeBytes: number | null; available: boolean }):
  { body: "guest.notice_body_browser" | "guest.notice_body_session"; period: "guest.persisted" | "guest.not_persisted" | "guest.period_unknown" | null } {
  if (!store.available) return { body: "guest.notice_body_session", period: null };
  if (store.persisted === true) return { body: "guest.notice_body_browser", period: "guest.persisted" };
  if (store.persisted === false) return { body: "guest.notice_body_browser", period: "guest.not_persisted" };
  return { body: "guest.notice_body_browser", period: "guest.period_unknown" };
}

export function humanBytes(n: number | null): string {
  if (n === null) return "?";
  const units = ["B", "KB", "MB", "GB", "TB"];
  let i = 0, v = n;
  while (v >= 1024 && i < units.length - 1) { v /= 1024; i += 1; }
  return `${v >= 10 || i === 0 ? Math.round(v) : v.toFixed(1)} ${units[i]}`;
}

export function humanSeconds(s: number): string {
  if (s <= 0) return "∞";
  if (s % 86400 === 0) return `${s / 86400} d`;
  if (s % 3600 === 0) return `${s / 3600} h`;
  if (s >= 60) return `${Math.round(s / 60)} min`;
  return `${s} s`;
}

/** The whole working copy as a v2 bundle (the server's exact text). */
export async function collectBundle(fetcher: typeof fetch = fetch): Promise<GuestBundleV2> {
  const r = await fetcher("/api/working-copy/bundle", { credentials: "same-origin", cache: "no-store" });
  if (!r.ok) throw new Error(`bundle: HTTP ${r.status}`);
  const text = await r.text();
  const head = JSON.parse(text) as { schema?: string; saved_at?: string };
  if (head.schema !== BUNDLE_SCHEMA) throw new Error("bundle: unexpected schema");
  return { schema: BUNDLE_SCHEMA, saved_at: String(head.saved_at ?? new Date().toISOString()), text };
}

/** Restores a bundle into the working copy; returns how many conversations came in. v2: one request, no analysis. */
export async function restoreBundle(bundle: GuestBundle, fetcher: typeof fetch = fetch, mode: "merge" | "replace" = "merge"): Promise<number> {
  if (bundle.schema === BUNDLE_SCHEMA) {
    // "replace" (a new session's working copy): exactly the bundle, also over what the page wrote before the restore
    // (e.g. the desktop's first pins); "merge" (Load archive into a working copy in use): the bundle over what is there.
    const r = await fetcher(`/api/working-copy/bundle?mode=${mode}`, { method: "POST", body: bundle.text, credentials: "same-origin",
      headers: { "Content-Type": "application/json" } });
    const out = await r.json() as { restored?: { tables?: Record<string, number> }; error?: { message?: string } };
    if (!r.ok || out.error) throw new Error(out.error?.message ?? `restore: HTTP ${r.status}`);
    return Number(out.restored?.tables?.conversations ?? 0);
  }
  return restoreBundleV1(bundle, fetcher);
}

/** "Clear everything from this browser": the store, the app's local/session storage and OPFS, then the guest session ends. */
export async function wipeEverything(store: GuestStore = browserStore, fetcher: typeof fetch = fetch): Promise<void> {
  try { await store.wipe(); } catch { /* nothing stored */ }
  try {
    const root = await (navigator.storage as unknown as { getDirectory?: () => Promise<FileSystemDirectoryHandle> })?.getDirectory?.();
    const dir = root as unknown as { keys?: () => AsyncIterable<string>; removeEntry: (n: string, o: { recursive: boolean }) => Promise<void> } | undefined;
    if (dir?.keys) for await (const name of dir.keys()) await dir.removeEntry(name, { recursive: true });
  } catch { /* no OPFS */ }
  try { localStorage.clear(); } catch { /* refused */ }
  try { sessionStorage.clear(); } catch { /* refused */ }
  try { await fetcher("/api/auth/logout", { method: "POST", credentials: "same-origin" }); } catch { /* the session ends with its TTL */ }
}

/** v1: every conversation of the working copy, exported in Loom's JSON format (kept for older readers). */
export async function collectBundleV1(fetcher: typeof fetch = fetch): Promise<GuestBundleV1> {
  const list = await (await fetcher("/api/conversations?limit=100000", { credentials: "same-origin" })).json() as Array<{ id: string; title?: string }>;
  const conversations: BundleConversation[] = [];
  for (const c of Array.isArray(list) ? list : []) {
    const r = await fetcher(`/api/conversations/${encodeURIComponent(c.id)}/export?format=json`, { credentials: "same-origin" });
    if (r.ok) conversations.push({ id: c.id, title: c.title ?? "", content: await r.text() });
  }
  return { schema: BUNDLE_SCHEMA_V1, saved_at: new Date().toISOString(), conversations };
}

/** v1: imports each conversation (POST /api/import; analysis runs again); returns how many came in. */
export async function restoreBundleV1(bundle: GuestBundleV1, fetcher: typeof fetch = fetch): Promise<number> {
  let count = 0;
  for (const c of bundle.conversations) {
    const form = new FormData();
    form.append("file", new Blob([c.content], { type: "application/json" }), `${c.id || "conversation"}.json`);
    const q = c.title ? `?title=${encodeURIComponent(c.title)}` : "";
    const r = await fetcher(`/api/import${q}`, { method: "POST", body: form, credentials: "same-origin" });
    await r.text();  // the import answers as a stream; reading it to the end waits for the import
    if (r.ok) count += 1;
  }
  return count;
}
