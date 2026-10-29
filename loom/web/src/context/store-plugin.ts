// PH29 (design MAGAZYN_ABSTRAKCJA_PH29 §2): a store is an entry in data - {id, kind, label, traits, config, status} - and
// a plugin implements the closed minimum (save / load / list / wipe / probe; closed because it is the callers' contract,
// 2026-10-30, reopened by a new plugin contract version). Everything else a plugin can do is announced in
// `traits.ops` and used only when present. Trait names are NOT an enumeration: a new trait is a new key; a rule that
// does not know a key skips it; a trait without a value is "unknown", never false. A trait that depends on the device
// or the moment (quota, persistence) carries its source and the date it was observed.
//
// The registry has the shape of the kernel's BackendRegistry ({kind, status, factory}). Selection (§2.4, P2 extends it):
// an explicit ordered `targets` list always wins; "auto" takes the implemented, available plugins that satisfy
// `requires`, ordered by `prefers`. When nothing satisfies `requires`, nothing is blocked: the best available plugin
// is used and a NON-BLOCKING advisory names the missing traits with the evidence.

import type { GuestBundle } from "./guest-store";

export type TraitScalar = boolean | number | string | null;
export interface Trait { value: TraitScalar | TraitScalar[] | "unknown"; unit?: string; source?: string; observed_at?: string }
export type Traits = Record<string, Trait>;

export interface StorePlugin {
  readonly id: string;
  readonly kind: string;
  readonly label: string;
  /** Free text; "implemented" / "planned" are preset examples. */
  readonly status: string;
  /** Whether it can work here and now; never throws (a reason comes back instead). */
  probe(): Promise<{ available: boolean; reason?: string }>;
  traits(): Promise<Traits>;
  save(bundle: GuestBundle): Promise<void>;
  load(): Promise<GuestBundle | null>;
  list(): Promise<string[]>;
  /** Removes everything this store holds on the device (closing what it opened first). */
  wipe(): Promise<void>;
  /** Optional: releases resources (a worker, a database). */
  close?(): Promise<void>;
}

export interface StoreEntry { kind: string; status: string; factory: () => StorePlugin }

export interface StorePolicy {
  /** Explicit ordered list of plugin ids, or "auto" (preset). */
  targets?: string[] | "auto";
  /** Trait name -> required value (deep-equal); "unknown" never satisfies. */
  requires?: Record<string, unknown>;
  /** Trait names, best first; "-name" = lower is better. Booleans: true > false > unknown. */
  prefers?: string[];
}

export interface Advisory { kind: string; plugin?: string; missing?: string[]; reason?: string; evidence?: Record<string, unknown> }

export interface Candidate { id: string; status: string; available: boolean; reason?: string; traits: Traits }

export interface Choice { chosen: string | null; order: string[]; advisories: Advisory[] }

export function traitValue(t: Traits, name: string): unknown {
  const v = t[name];
  return v === undefined ? "unknown" : v.value;
}

function same(a: unknown, b: unknown): boolean {
  if (a === b) return true;
  if (Array.isArray(a) && Array.isArray(b)) return a.length === b.length && a.every((x, i) => same(x, b[i]));
  return false;
}

export function missingTraits(c: Candidate, requires: Record<string, unknown> = {}): string[] {
  const out: string[] = [];
  for (const [k, want] of Object.entries(requires)) {
    const have = traitValue(c.traits, k);
    if (have === "unknown" && want !== "unknown") { out.push(k); continue; }
    // A list trait (e.g. ops) satisfies a scalar requirement when it contains it.
    if (Array.isArray(have) && !Array.isArray(want)) { if (!have.some((x) => same(x, want))) out.push(k); continue; }
    if (!same(have, want)) out.push(k);
  }
  return out;
}

function rank(v: unknown): number {
  if (v === true) return 2;
  if (v === false) return 1;
  if (typeof v === "number" && Number.isFinite(v)) return v;
  if (Array.isArray(v)) return v.length;
  return Number.NEGATIVE_INFINITY;  // unknown and text rank last
}

/** Orders candidates by `prefers` (stable: ties keep the registry order). */
export function orderByPreference(cands: Candidate[], prefers: string[] = []): Candidate[] {
  const idx = new Map(cands.map((c, i) => [c.id, i]));
  return [...cands].sort((a, b) => {
    for (const p of prefers) {
      const asc = p.startsWith("-");
      const name = asc ? p.slice(1) : p;
      const ra = rank(traitValue(a.traits, name)), rb = rank(traitValue(b.traits, name));
      if (ra === rb) continue;
      if (ra === Number.NEGATIVE_INFINITY) return 1;
      if (rb === Number.NEGATIVE_INFINITY) return -1;
      return asc ? ra - rb : rb - ra;
    }
    return (idx.get(a.id) ?? 0) - (idx.get(b.id) ?? 0);
  });
}

/** Pure selection; the caller probes the plugins and passes what they reported. */
export function choose(cands: Candidate[], policy: StorePolicy = {}): Choice {
  const advisories: Advisory[] = [];
  const usable = cands.filter((c) => c.available && c.status === "implemented");
  for (const c of cands) if (!c.available || c.status !== "implemented")
    advisories.push({ kind: "store_unavailable", plugin: c.id, reason: c.reason ?? c.status });
  if (Array.isArray(policy.targets)) {
    const order = policy.targets.filter((id) => usable.some((c) => c.id === id));
    for (const id of policy.targets) if (!cands.some((c) => c.id === id)) advisories.push({ kind: "store_unknown", plugin: id });
    if (order.length && order[0] !== policy.targets[0])
      advisories.push({ kind: "store_fallback", plugin: order[0], reason: `first target ${policy.targets[0]} is not usable here` });
    return { chosen: order[0] ?? null, order, advisories };
  }
  const requires = policy.requires ?? {};
  const fit = usable.filter((c) => missingTraits(c, requires).length === 0);
  const ordered = orderByPreference(fit, policy.prefers);
  if (ordered.length) return { chosen: ordered[0].id, order: ordered.map((c) => c.id), advisories };
  // Nothing satisfies the requirements: not blocked - the best usable store, with the evidence.
  const rest = orderByPreference(usable, policy.prefers);
  if (rest.length) {
    const missing = missingTraits(rest[0], requires);
    advisories.push({ kind: "store_requirements_unmet", plugin: rest[0].id, missing,
      evidence: Object.fromEntries(missing.map((k) => [k, traitValue(rest[0].traits, k)])) });
  }
  return { chosen: rest[0]?.id ?? null, order: rest.map((c) => c.id), advisories };
}

/** Today's date-stamped observation. */
export function observed(value: Trait["value"], source: string, extra: Partial<Trait> = {}): Trait {
  return { value, source, observed_at: new Date().toISOString(), ...extra };
}

/** persist() / estimate() as traits (platform-defined values, Storage standard, closed list 2026-10-30). */
export async function storageTraits(): Promise<Traits> {
  const storage = typeof navigator !== "undefined" ? navigator.storage : undefined;
  const t: Traits = {};
  try { t.persisted = observed(storage?.persisted ? await storage.persisted() : "unknown", "navigator.storage.persisted()"); }
  catch { t.persisted = observed("unknown", "navigator.storage.persisted() failed"); }
  try {
    const e = storage?.estimate ? await storage.estimate() : undefined;
    if (e && typeof e.quota === "number") t.quota_bytes = observed(e.quota, "navigator.storage.estimate()", { unit: "B" });
    if (e && typeof e.quota === "number" && typeof e.usage === "number")
      t.free_bytes = observed(Math.max(0, e.quota - e.usage), "navigator.storage.estimate()", { unit: "B" });
  } catch { /* unknown */ }
  return t;
}
