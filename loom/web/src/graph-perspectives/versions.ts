import type { ObjectRef, ProjectedObject } from "./types";
export interface VersionPropertyChange { path: string; kind: "added" | "removed" | "changed"; left?: unknown; right?: unknown }
export interface ObjectVersionComparison {
  canonicalKey: string;
  left: ObjectRef;
  right: ObjectRef;
  status: "compared" | "unavailable";
  representationChanged: boolean;
  changes: VersionPropertyChange[];
  /** Structural differences are observations about supplied values, not semantic history. */
  method: "structural_properties";
}
const object = (value: unknown): value is Record<string, unknown> => !!value && typeof value === "object";
const escape = (value: string) => value.replace(/~/g, "~0").replace(/\//g, "~1");
function diff(left: unknown, right: unknown, path: string): VersionPropertyChange[] {
  if (Object.is(left, right)) return [];
  if (object(left) && object(right) && Array.isArray(left) === Array.isArray(right)) {
    return [...new Set([...Object.keys(left), ...Object.keys(right)])].sort().flatMap(key => {
      const child = `${path}/${escape(key)}`;
      if (!Object.prototype.hasOwnProperty.call(left, key)) return [{ path: child, kind: "added", right: structuredClone(right[key]) } as VersionPropertyChange];
      if (!Object.prototype.hasOwnProperty.call(right, key)) return [{ path: child, kind: "removed", left: structuredClone(left[key]) } as VersionPropertyChange];
      return diff(left[key], right[key], child);
    });
  }
  return [{ path, kind: "changed", left: structuredClone(left), right: structuredClone(right) }];
}
/** Order is selection order; left/right does not assert a temporal ordering of opaque snapshot ids. */
export function compareObjectVersions(objects: ProjectedObject[]): ObjectVersionComparison[] {
  const groups = new Map<string, ProjectedObject[]>();
  for (const item of objects) if (item.ref.snapshot !== undefined) {
    const group = groups.get(item.canonicalKey) ?? [];
    group.push(item); groups.set(item.canonicalKey, group);
  }
  const comparisons: ObjectVersionComparison[] = [];
  for (const [canonicalKey, items] of groups) for (let left = 0; left < items.length; left++) for (let right = left + 1; right < items.length; right++) {
    const a = items[left], b = items[right];
    if (a.ref.snapshot === b.ref.snapshot) continue;
    const available = a.status === "available" && b.status === "available";
    comparisons.push({ canonicalKey, left: structuredClone(a.ref), right: structuredClone(b.ref), status: available ? "compared" : "unavailable",
      representationChanged: a.ref.representation !== b.ref.representation, method: "structural_properties",
      changes: available ? diff(a.properties ?? {}, b.properties ?? {}, "/properties") : [] });
  }
  return comparisons;
}
