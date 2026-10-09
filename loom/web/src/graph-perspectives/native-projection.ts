import { importedPointer } from "../content/imported-message";
import type { NativeLayerResponse } from "./native-layers";
import type { NeighborPage, ObjectRef, SourceAdapter, SourceDescriptor, SourceObject, SourceRelation } from "./types";

type Row = Record<string, unknown>;
interface ProjectionDescription {
  childRelation: string;
  relationByField: Record<string, string>;
  layerPaths: Record<string, string>;
  kinds: Record<string, string>;
  labels: Record<string, string>;
  links: { from: string; to: string; relation: string }[];
}
const object = (value: unknown): value is Row => !!value && typeof value === "object" && !Array.isArray(value);
const escape = (value: string) => value.replace(/~/g, "~0").replace(/\//g, "~1");
const unescape = (value: string) => value.replace(/~1/g, "/").replace(/~0/g, "~");
const own = (value: object, key: string) => Object.prototype.hasOwnProperty.call(value, key);

/** Read-only view over the exact native response already fetched by the host.
 * This aliases rows by stable key; it neither stores a second graph nor computes
 * effective settings. Access uses the existing source JSON-pointer selector. */
export function createNativeResolutionAdapter(
  response: NativeLayerResponse | (() => NativeLayerResponse | null),
  descriptor: SourceDescriptor,
): SourceAdapter {
  const config = descriptor.nativeProjection as ProjectionDescription | undefined;
  if (!config || typeof config.childRelation !== "string" || !object(config.layerPaths) || !object(config.kinds) || !object(config.labels) || !Array.isArray(config.links))
    throw new Error("graph_perspectives.native_projection_descriptor_invalid");
  const shape = config;
  function snapshot() {
    const raw = typeof response === "function" ? response() : response;
    if (!raw || raw.schema !== "loom.graph_perspective_native_resolution/1" || !Array.isArray(raw.effectiveDefaults)) return null;
    const components: Row = Object.create(null);
    const indexes = new Map<string, number>();
    raw.effectiveDefaults.forEach((row, index) => {
      if (typeof row.key !== "string" || own(components, row.key)) throw new Error("graph_perspectives.native_projection_duplicate_key");
      components[row.key] = row; indexes.set(row.key, index);
    });
    return { raw, tree: { profile: raw.layers, components }, indexes };
  }
  const reference = (selector: string): ObjectRef => ({ source: descriptor.id, selector, canonicalId: `${descriptor.id}:${selector}` });
  const absent = (ref: ObjectRef, status: SourceObject["status"], reason: string): SourceObject => ({ ref: structuredClone(ref), label: ref.selector || descriptor.label,
    kind: shape.kinds.reference, status, evidence: "unknown", reason });
  function nodeRole(selector: string): string {
    if (!selector) return "root";
    const parts = selector.split("/").slice(1).map(unescape);
    if (parts.length === 1) return "collection";
    if (parts[0] === "components" && parts.length === 2) return "component";
    if (parts[0] === "components" && parts.length === 3 && ["value", "source"].includes(parts[2])) return parts[2];
    return "field";
  }
  function rowFor(selector: string, current: NonNullable<ReturnType<typeof snapshot>>): Row | undefined {
    const parts = selector.split("/").slice(1).map(unescape);
    const candidate = current.tree.components[parts[1]];
    return parts[0] === "components" && typeof parts[1] === "string" && object(candidate) ? candidate : undefined;
  }
  function sourcePointer(selector: string, current: NonNullable<ReturnType<typeof snapshot>>): string {
    if (selector === "") return "";
    if (selector === "/profile" || selector.startsWith("/profile/")) return `/layers${selector.slice("/profile".length)}`;
    const parts = selector.split("/").slice(1).map(unescape);
    if (parts[0] === "components" && parts.length >= 2) {
      const index = current.indexes.get(parts[1]);
      if (index !== undefined) return `/effectiveDefaults/${index}${parts.slice(2).map(part => `/${escape(part)}`).join("")}`;
    }
    return "/effectiveDefaults";
  }
  function kindOf(selector: string): string { return shape.kinds[nodeRole(selector)]; }
  function title(selector: string, value: unknown): string {
    if (!selector) return descriptor.label;
    if (nodeRole(selector) === "component" && object(value)) {
      const entity = object(value.entity) ? value.entity : {};
      if (typeof entity.label === "string") return entity.label;
      if (typeof value.key === "string") return value.key;
    }
    const last = unescape(selector.slice(selector.lastIndexOf("/") + 1));
    return shape.labels[last] ?? last;
  }
  function relation(from: string, to: string, kind: string, current: NonNullable<ReturnType<typeof snapshot>>): SourceRelation {
    return { id: JSON.stringify([descriptor.id, from, kind, to]), from: reference(from), to: reference(to), kind, evidence: "computed",
      basis: { resolver: current.raw.resolver, source: descriptor.id, from: sourcePointer(from, current), to: sourcePointer(to, current), operation: "native_response_projection" } };
  }
  function parentEdge(selector: string, current: NonNullable<ReturnType<typeof snapshot>>): SourceRelation | null {
    if (!selector) return null;
    const parent = selector.slice(0, selector.lastIndexOf("/"));
    const name = unescape(selector.slice(selector.lastIndexOf("/") + 1));
    const kind = nodeRole(parent) === "component" ? shape.relationByField[name] ?? shape.childRelation : shape.childRelation;
    return relation(parent, selector, kind, current);
  }
  function crossLinks(selector: string, current: NonNullable<ReturnType<typeof snapshot>>): SourceRelation[] {
    const row = rowFor(selector, current);
    if (!row || typeof row.key !== "string") return [];
    const base = `/components/${escape(row.key)}`;
    const paths: Record<string, string | undefined> = { component: base, value: own(row, "value") ? `${base}/value` : undefined,
      source: own(row, "source") ? `${base}/source` : undefined };
    const template = typeof row.layer === "string" ? shape.layerPaths[row.layer] : undefined;
    if (template) {
      const path = template.replace(/\{([^}]+)\}/g, (_match, key: string) => typeof row[key] === "string" ? escape(row[key] as string) : "");
      if (importedPointer(current.tree, path).found) paths.layer = path;
    }
    const edges: SourceRelation[] = [];
    for (const link of shape.links) {
      const from = paths[link.from], to = paths[link.to];
      if (from && to && (from === selector || to === selector)) edges.push(relation(from, to, link.relation, current));
    }
    return edges;
  }
  return {
    descriptor: structuredClone(descriptor),
    async resolve(ref, context) {
      if (context.signal?.aborted) throw new DOMException("Aborted", "AbortError");
      if (ref.source !== descriptor.id) return absent(ref, "unknown", "source_mismatch");
      if (!context.permission.canRead(ref)) return absent(ref, "denied", "permission_denied");
      if (ref.snapshot !== undefined) return absent(ref, "unavailable", "native_projection_snapshot_reader_unavailable");
      const current = snapshot(); if (!current) return absent(ref, "unloaded", "native_response_unavailable");
      const selected = importedPointer(current.tree, ref.selector);
      if (!selected.found) return absent(ref, "unknown", "selector_not_found");
      const canonical = reference(ref.selector);
      if (!context.permission.canRead(canonical)) return absent(ref, "denied", "permission_denied");
      const row = rowFor(ref.selector, current);
      return { ref: canonical, label: title(ref.selector, selected.value), kind: kindOf(ref.selector), status: "available", evidence: "computed",
        properties: { value: structuredClone(selected.value), nativeStatus: row?.status, resolver: current.raw.resolver,
          sourcePointer: sourcePointer(ref.selector, current), sourceRef: { source: descriptor.id, selector: sourcePointer(ref.selector, current) },
          confidenceStatus: "not_calibrated", projection: "read_only_native_response" } };
    },
    async neighbors(ref, request, context): Promise<NeighborPage> {
      if (context.signal?.aborted) throw new DOMException("Aborted", "AbortError");
      if (!context.permission.canRead(ref)) return { relations: [], status: "denied", reason: "permission_denied" };
      if (ref.source !== descriptor.id) return { relations: [], status: "unknown", reason: "source_mismatch" };
      if (ref.snapshot !== undefined || request.snapshot !== undefined) return { relations: [], status: "unavailable", reason: "native_projection_snapshot_reader_unavailable" };
      const structure = descriptor.structures.find(item => item.id === request.structure);
      if (!structure) return { relations: [], status: "unknown", reason: "structure_unsupported" };
      if (!Number.isSafeInteger(request.limit) || request.limit < 1) throw new Error("invalid_neighbor_limit");
      const offset = request.cursor === undefined ? 0 : Number(request.cursor);
      if (!Number.isSafeInteger(offset) || offset < 0) throw new Error("invalid_neighbor_cursor");
      const current = snapshot(); if (!current) return { relations: [], status: "unloaded", reason: "native_response_unavailable" };
      const selected = importedPointer(current.tree, ref.selector);
      if (!selected.found) return { relations: [], status: "unknown", reason: "selector_not_found" };
      const edges: SourceRelation[] = [];
      const parent = parentEdge(ref.selector, current); if (parent) edges.push(parent);
      if (object(selected.value) || Array.isArray(selected.value)) {
        for (const key of Object.keys(selected.value)) {
          const edge = parentEdge(`${ref.selector}/${escape(key)}`, current); if (edge) edges.push(edge);
        }
      }
      edges.push(...crossLinks(ref.selector, current));
      // Cross-links are indexed only across already resolved native rows, not a
      // separate persistent graph. This also supports reverse provenance walks.
      if (ref.selector.startsWith("/profile/")) for (const key of current.indexes.keys()) edges.push(...crossLinks(`/components/${escape(key)}/source`, current).filter(edge => edge.to.selector === ref.selector));
      const unique = new Map(edges.map(edge => [edge.id, edge]));
      const selectedRelations = [...unique.values()].filter(edge => structure.relations.includes(edge.kind)
        && (!request.relations.length || request.relations.includes(edge.kind))
        && (request.direction === "both" || request.direction === "outgoing" && edge.from.selector === ref.selector || request.direction === "incoming" && edge.to.selector === ref.selector)
        && context.permission.canRead(edge.from) && context.permission.canRead(edge.to));
      return { relations: selectedRelations.slice(offset, offset + request.limit), total: selectedRelations.length,
        ...(offset + request.limit < selectedRelations.length ? { nextCursor: String(offset + request.limit) } : {}) };
    },
  };
}
