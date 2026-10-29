// PH29 P1: the web store registry ({kind, status, factory}, same shape as the kernel's BackendRegistry) and the choice
// of the store the page uses. Preset policy (data, not a rule): "auto" - implemented, available stores that are
// durable, the one with more operations first (OPFS+SQLite before the IndexedDB bundle). The person's own choice
// (`loom.store.policy` in this browser, set from the store panel) wins: an explicit `targets` list. A store that is not
// available here is shown with its reason and the next one is used - an advisory, never an error.
import { browserStore } from "./guest-store";
import { createOpfsSqliteStore, OPFS_SQLITE_ID } from "./opfs-sqlite-store";
import { choose, observed, storageTraits, type Advisory, type Candidate, type StoreEntry, type StorePlugin, type StorePolicy, type Traits } from "./store-plugin";

export const INDEXEDDB_ID = "browser_indexeddb";
export const POLICY_KEY = "loom.store.policy";
export const PRESET_POLICY: StorePolicy = { targets: "auto", requires: { durable: true }, prefers: ["ops"] };

export function indexedDbStore(): StorePlugin {
  return {
    id: INDEXEDDB_ID, kind: "indexeddb_bundle", label: "Przeglądarka — paczka (IndexedDB)", status: "implemented",
    probe: async () => (typeof indexedDB !== "undefined" ? { available: true } : { available: false, reason: "missing: indexeddb" }),
    traits: async (): Promise<Traits> => ({
      durable: observed(true, "plugin: IndexedDB outlives the tab and the session"),
      survives_site_data_clear: observed(false, "platform docs"),
      offline: observed(true, "plugin"), shared_across_devices: observed(false, "plugin"),
      encrypted_at_rest: observed("none", "plugin"), readers: observed("this browser", "plugin"),
      ops: observed(["whole_bundle_write"], "plugin: the whole bundle is written on every save"),
      available_on: observed(["web"], "plugin"),
      ...(await storageTraits()),
    }),
    save: browserStore.save, load: browserStore.load, list: browserStore.list, wipe: browserStore.wipe,
  };
}

export const WEB_STORES: StoreEntry[] = [
  { kind: OPFS_SQLITE_ID, status: "implemented", factory: () => createOpfsSqliteStore() },
  { kind: INDEXEDDB_ID, status: "implemented", factory: indexedDbStore },
];

export function readPolicy(): StorePolicy {
  try {
    const raw = localStorage.getItem(POLICY_KEY);
    if (raw) { const p = JSON.parse(raw) as StorePolicy; if (p && typeof p === "object") return { ...PRESET_POLICY, ...p }; }
  } catch { /* the preset */ }
  return PRESET_POLICY;
}

export function writePolicy(p: StorePolicy | null): void {
  try { if (p) localStorage.setItem(POLICY_KEY, JSON.stringify(p)); else localStorage.removeItem(POLICY_KEY); } catch { /* refused: the preset stays */ }
}

export interface StoreSelection { plugin: StorePlugin | null; plugins: StorePlugin[]; candidates: Candidate[]; advisories: Advisory[]; policy: StorePolicy }

export async function selectStore(entries: StoreEntry[] = WEB_STORES, policy: StorePolicy = readPolicy()): Promise<StoreSelection> {
  const plugins = entries.map((e) => e.factory());
  const candidates: Candidate[] = [];
  // An explicit list: probe in its order and stop at the first usable one (the others are not started at all).
  const ordered = Array.isArray(policy.targets)
    ? [...policy.targets.map((id) => plugins.find((p) => p.id === id)).filter((p): p is StorePlugin => !!p), ...plugins.filter((p) => !(policy.targets as string[]).includes(p.id))]
    : plugins;
  let found = false;
  for (const p of ordered) {
    if (found && Array.isArray(policy.targets)) { candidates.push({ id: p.id, status: p.status, available: false, reason: "not probed (an earlier target is usable)", traits: {} }); continue; }
    const pr = await p.probe();
    if (pr.available && Array.isArray(policy.targets) && policy.targets.includes(p.id)) found = true;
    candidates.push({ id: p.id, status: p.status, available: pr.available, reason: pr.reason, traits: await p.traits() });
  }
  const c = choose(candidates, policy);
  for (const p of plugins) if (p.id !== c.chosen) void p.close?.();
  return { plugin: plugins.find((p) => p.id === c.chosen) ?? null, plugins, candidates, advisories: c.advisories, policy };
}
