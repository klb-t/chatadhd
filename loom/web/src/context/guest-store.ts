// R103 §6: the history of a person without an account lives on HIS side. The browser store (preset) keeps one
// bundle - every conversation of the guest's working copy, exported in Loom's own JSON format - in IndexedDB, saved
// after each answer and imported back through POST /api/import when a new guest session starts with an empty
// working copy. The same bundle is what "Download archive" saves and "Load archive" reads. Other stores (file,
// folder, the person's own cloud) implement the same three calls; this file is the browser one.

export const ANSWER_DONE_EVENT = "loom:answer-done";
export const BUNDLE_SCHEMA = "loom.guest_bundle/1";

export interface BundleConversation { id: string; title: string; content: string }
export interface GuestBundle { schema: string; saved_at: string; conversations: BundleConversation[] }

export interface GuestStore {
  save(bundle: GuestBundle): Promise<void>;
  load(): Promise<GuestBundle | null>;
  list(): Promise<string[]>;
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
};

export function isBundle(v: unknown): v is GuestBundle {
  if (!v || typeof v !== "object") return false;
  const b = v as Record<string, unknown>;
  return b.schema === BUNDLE_SCHEMA && Array.isArray(b.conversations);
}

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

/** Every conversation of the working copy, exported in Loom's JSON format. */
export async function collectBundle(fetcher: typeof fetch = fetch): Promise<GuestBundle> {
  const list = await (await fetcher("/api/conversations?limit=100000", { credentials: "same-origin" })).json() as Array<{ id: string; title?: string }>;
  const conversations: BundleConversation[] = [];
  for (const c of Array.isArray(list) ? list : []) {
    const r = await fetcher(`/api/conversations/${encodeURIComponent(c.id)}/export?format=json`, { credentials: "same-origin" });
    if (r.ok) conversations.push({ id: c.id, title: c.title ?? "", content: await r.text() });
  }
  return { schema: BUNDLE_SCHEMA, saved_at: new Date().toISOString(), conversations };
}

/** Imports a bundle into the working copy (POST /api/import, one file per conversation); returns how many came in. */
export async function restoreBundle(bundle: GuestBundle, fetcher: typeof fetch = fetch): Promise<number> {
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
