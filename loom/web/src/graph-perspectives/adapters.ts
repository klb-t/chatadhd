import { importedPointer } from "../content/imported-message";
import { parseApplicationProfileSource } from "../profiles/graph";
import { createProfileRegistry, registerProfile, type ApplicationProfile, type ProfileRegistry } from "../profiles/runtime";
import type { Availability, NeighborPage, NeighborRequest, ObjectRef, ReadContext, SourceAdapter, SourceDescriptor, SourceObject, SourceRelation } from "./types";

type Row = Record<string, unknown>;
export interface GraphPacketView extends Row {
  schema: "loom.graph_packet/1";
  entities: Row[];
  claims: Row[];
  sources: Row[];
}
export interface AdapterMetrics {
  resolveCalls: number;
  neighborCalls: number;
  materializedObjects: number;
  materializedRelations: number;
  indexedEntities: number;
  indexedClaims: number;
}
export interface PacketAdapter extends SourceAdapter { readonly metrics: AdapterMetrics; readonly packet: GraphPacketView }
export interface DemandLoader {
  /** A source/catalog/native loader supplied by the host. Never inferred from a URL. */
  resolve(ref: ObjectRef, context: ReadContext): Promise<SourceObject>;
  neighbors?(ref: ObjectRef, request: NeighborRequest, context: ReadContext): Promise<NeighborPage>;
}
const own = (value: object, key: string) => Object.prototype.hasOwnProperty.call(value, key);
const row = (value: unknown): Row => value !== null && typeof value === "object" && !Array.isArray(value) ? value as Row : {};
const text = (value: unknown): string | undefined => typeof value === "string" ? value : undefined;
const clone = <T,>(value: T): T => structuredClone(value);
const strings = (value: unknown): string[] => Array.isArray(value) ? value.filter((item): item is string => typeof item === "string") : [];
const statuses = new Set<Availability>(["available", "unloaded", "unavailable", "unknown", "denied"]);
function status(value: unknown, fallback: Availability): Availability { return statuses.has(value as Availability) ? value as Availability : fallback; }
function metadata(entity: Row): Row { return row(row(entity.attrs).perspective); }
function relationMetadata(claim: Row): Row { return row(row(row(claim.qualifiers).extra).perspective); }
function abort(context: ReadContext) { context.signal?.throwIfAborted(); }
function absent(ref: ObjectRef, state: Availability, reason: string): SourceObject {
  return { ref: clone(ref), label: ref.selector || ref.source, kind: "unknown", status: state, evidence: "unknown", reason };
}
function gate(ref: ObjectRef, descriptor: SourceDescriptor, context: ReadContext): SourceObject | undefined {
  abort(context);
  if (!context.permission.canRead(ref) || descriptor.status === "denied") return absent(ref, "denied", "permission_denied");
  if (ref.source !== descriptor.id) return absent(ref, "unknown", "source_mismatch");
  if (descriptor.status === "unavailable") return absent(ref, "unavailable", "source_unavailable");
  return undefined;
}
function pageGate(ref: ObjectRef, descriptor: SourceDescriptor, context: ReadContext): NeighborPage | undefined {
  const blocked = gate(ref, descriptor, context);
  return blocked ? { relations: [], status: blocked.status, reason: blocked.reason } : undefined;
}
function pagination<T>(values: T[], request: NeighborRequest): { items: T[]; nextCursor?: string; total: number } {
  if (!Number.isSafeInteger(request.limit) || request.limit < 1) throw new Error("invalid_neighbor_limit");
  if (request.cursor !== undefined && !/^(0|[1-9]\d*)$/.test(request.cursor)) throw new Error("invalid_neighbor_cursor");
  const offset = Number(request.cursor ?? 0);
  if (!Number.isSafeInteger(offset)) throw new Error("invalid_neighbor_cursor");
  const end = Math.min(values.length, offset + request.limit);
  return { items: values.slice(offset, end), ...(end < values.length ? { nextCursor: String(end) } : {}), total: values.length };
}
function supportedRelations(descriptor: SourceDescriptor, request: NeighborRequest): string[] | undefined {
  const structure = descriptor.structures.find(item => item.id === request.structure);
  if (!structure) return undefined;
  if (structure.directions && !structure.directions.includes(request.direction)) return undefined;
  return request.relations.length && !request.relations.includes("*") ? request.relations.filter(kind => structure.relations.includes("*") || structure.relations.includes(kind)) : structure.relations;
}

/** A read-only, lazy view over the existing packet. Indexes hold references only;
 * no source rows are copied into another canonical graph or native store. */
export function createPacketAdapter(input: unknown, descriptor: SourceDescriptor): PacketAdapter {
  const value = row(input);
  if (value.schema !== "loom.graph_packet/1" || !Array.isArray(value.entities) || !Array.isArray(value.claims) || !Array.isArray(value.sources)) {
    throw new Error("invalid_graph_packet_view");
  }
  const packet = value as GraphPacketView;
  // This is the view boundary, not a replacement for native hash/provenance validation.
  if (packet.entities.some(entity => !entity || typeof entity !== "object" || Array.isArray(entity) || typeof entity.id !== "string") ||
      packet.claims.some(claim => !claim || typeof claim !== "object" || Array.isArray(claim) || typeof claim.id !== "string" || typeof claim.subject !== "string" || typeof claim.predicate !== "string" || typeof claim.object !== "string")) throw new Error("malformed_graph_packet_rows");
  const evidenceMap = row(descriptor.evidenceMap);
  const availabilityMap = row(descriptor.availabilityMap);
  const mapping = row(descriptor.objectMapping);
  const mapped = (entity: Row, field: string): unknown => typeof mapping[field] === "string" ? importedPointer(entity, mapping[field] as string).value : undefined;
  const entityStatus = (entity: Row): Availability => {
    const meta = metadata(entity); const value = meta.status ?? meta.availability ?? mapped(entity, "status");
    return status(typeof value === "string" && own(availabilityMap, value) ? availabilityMap[value] : value, value === undefined ? "available" : "unknown");
  };
  const evidence = (explicit: unknown, native: unknown) => text(explicit) ?? (typeof native === "string" ? text(evidenceMap[native]) : undefined) ?? "unknown";
  const metrics: AdapterMetrics = { resolveCalls: 0, neighborCalls: 0, materializedObjects: 0, materializedRelations: 0, indexedEntities: 0, indexedClaims: 0 };
  let byId: Map<string, Row> | undefined;
  let bySelector: Map<string, Row[]> | undefined;
  let byCanonical: Map<string, Row[]> | undefined;
  let adjacent: Map<string, Row[]> | undefined;
  function refFor(entity: Row): ObjectRef {
    const meta = metadata(entity); const supplied = row(meta.ref);
    const selector = text(supplied.selector) ?? text(meta.selector) ?? text(mapped(entity, "selector")) ?? text(entity.id) ?? "";
    const source = text(supplied.source) ?? text(meta.source_id) ?? text(mapped(entity, "source")) ?? descriptor.id;
    const canonicalId = text(supplied.canonicalId) ?? text(meta.object_id) ?? text(mapped(entity, "canonicalId")) ?? text(entity.canonical_key) ?? text(entity.id);
    const representation = text(supplied.representation) ?? text(meta.representation_id) ?? text(mapped(entity, "representation"));
    const snapshot = text(supplied.snapshot) ?? text(meta.snapshot) ?? text(mapped(entity, "snapshot"));
    return { ...clone(supplied), source, selector, ...(canonicalId ? { canonicalId } : {}),
      ...(representation ? { representation } : {}), ...(snapshot ? { snapshot } : {}) };
  }
  const selectorKey = (source: string, selector: string) => JSON.stringify([source, selector]);
  function index() {
    if (byId) return;
    const entityIds = new Map<string, Row>(), selectors = new Map<string, Row[]>(), identities = new Map<string, Row[]>(), edges = new Map<string, Row[]>();
    const append = (map: Map<string, Row[]>, key: string, entity: Row) => { const existing = map.get(key); if (existing) existing.push(entity); else map.set(key, [entity]); };
    for (const entity of packet.entities) {
      if (typeof entity.id !== "string" || entityIds.has(entity.id)) throw new Error("invalid_or_duplicate_packet_entity_id");
      const ref = refFor(entity); entityIds.set(entity.id, entity);
      append(selectors, selectorKey(ref.source, ref.selector), entity);
      if (ref.canonicalId) append(identities, selectorKey(ref.source, ref.canonicalId), entity);
    }
    const claimIds = new Set<string>();
    for (const claim of packet.claims) {
      if (claimIds.has(claim.id as string)) throw new Error("duplicate_packet_claim_id");
      claimIds.add(claim.id as string);
      const subject = text(claim.subject), object = text(claim.object);
      if (subject) append(edges, subject, claim);
      if (object && object !== subject) append(edges, object, claim);
    }
    byId = entityIds; bySelector = selectors; byCanonical = identities; adjacent = edges;
    metrics.indexedEntities = packet.entities.length; metrics.indexedClaims = packet.claims.length;
  }
  function find(ref: ObjectRef): { entity?: Row; reason?: string } {
    index();
    let matches = bySelector!.get(selectorKey(ref.source, ref.selector)) ?? [];
    if (!matches.length && ref.canonicalId) matches = byCanonical!.get(selectorKey(ref.source, ref.canonicalId)) ?? [];
    if (!matches.length) { const direct = byId!.get(ref.selector); if (direct && refFor(direct).source === ref.source) matches = [direct]; }
    const filter = (candidates: Row[]) => candidates.filter(entity => {
      const identity = refFor(entity);
      return (ref.snapshot === undefined || identity.snapshot === ref.snapshot) &&
        (ref.representation === undefined || identity.representation === ref.representation) &&
        (ref.canonicalId === undefined || identity.canonicalId === ref.canonicalId);
    });
    matches = filter(matches);
    if (!matches.length && ref.canonicalId) matches = filter(byCanonical!.get(selectorKey(ref.source, ref.canonicalId)) ?? []);
    return matches.length === 1 ? { entity: matches[0] } : { reason: matches.length ? "selector_ambiguous" : "selector_not_found" };
  }
  function objectFor(entity: Row): SourceObject {
    metrics.materializedObjects++;
    const meta = metadata(entity), ref = refFor(entity);
    const confidence = own(meta, "confidence") ? meta.confidence : mapped(entity, "confidence");
    // Native confidence stays in raw metadata. Parser presence does not certify calibration.
    return { ref, label: text(entity.label) ?? ref.selector, kind: text(entity.kind) ?? "unknown",
      status: entityStatus(entity), evidence: evidence(meta.evidence, mapped(entity, "evidence") ?? entity.evidence_class),
      ...(typeof confidence === "number" && Number.isFinite(confidence) && confidence >= 0 && confidence <= 1 ? { confidence } : {}),
      properties: { ...clone(row(meta.properties)), raw: clone(entity), recognition: clone(meta.recognition ?? mapped(entity, "recognition") ?? null) },
      ...(text(meta.reason) ? { reason: text(meta.reason) } : {}) };
  }
  return {
    descriptor: clone(descriptor), packet, metrics,
    async resolve(ref, context) {
      metrics.resolveCalls++;
      const blocked = gate(ref, descriptor, context); if (blocked) return blocked;
      const found = find(ref); if (!found.entity) return absent(ref, "unknown", found.reason!);
      const object = objectFor(found.entity);
      if (!context.permission.canRead(object.ref)) return absent(ref, "denied", "permission_denied");
      // Alias/canonical resolution retains the caller's address; the exact selected
      // representation remains explicit, without a renderer-dependent redirect.
      return { ...object, ref: { ...object.ref, selector: ref.selector }, properties: { ...object.properties, address: object.ref } };
    },
    async neighbors(ref, request, context) {
      metrics.neighborCalls++;
      const blocked = pageGate(ref, descriptor, context); if (blocked) return blocked;
      const allowed = supportedRelations(descriptor, request);
      if (!allowed) return { relations: [], status: "unknown", reason: "structure_unsupported" };
      const requested = request.snapshot === undefined ? ref : { ...ref, snapshot: request.snapshot };
      const found = find(requested); if (!found.entity) return { relations: [], status: "unknown", reason: found.reason };
      const foundStatus = entityStatus(found.entity);
      if (foundStatus !== "available") return { relations: [], status: foundStatus, reason: text(metadata(found.entity).reason) ?? "object_unavailable" };
      const entityId = found.entity.id;
      const candidates: { claim: Row; meta: Row; fromRef: ObjectRef; toRef: ObjectRef; kind: string }[] = [];
      let permissionFiltered = false;
      for (const claim of adjacent!.get(entityId as string) ?? []) {
        abort(context);
        const kind = text(claim.predicate); if (!kind || (!allowed.includes("*") && !allowed.includes(kind))) continue;
        const meta = relationMetadata(claim), structures = strings(meta.structures ?? meta.structure_ids);
        if (structures.length && !structures.includes(request.structure)) continue;
        if (request.direction === "outgoing" && claim.subject !== entityId) continue;
        if (request.direction === "incoming" && claim.object !== entityId) continue;
        const from = byId!.get(String(claim.subject)), to = byId!.get(String(claim.object));
        // Literal-valued claims remain on the raw row; they are not fabricated entities.
        if (!from || !to) continue;
        const fromRef = refFor(from), toRef = refFor(to);
        const explicitSnapshot = request.snapshot ?? ref.snapshot;
        if (explicitSnapshot !== undefined && text(meta.snapshot) && meta.snapshot !== explicitSnapshot) continue;
        if (!context.permission.canRead(fromRef) || !context.permission.canRead(toRef)) { permissionFiltered = true; continue; }
        candidates.push({ claim, meta, fromRef, toRef, kind });
      }
      const paged = pagination(candidates, request);
      const relations: SourceRelation[] = paged.items.map(({ claim, meta, fromRef, toRef, kind }) => {
        metrics.materializedRelations++;
        return { ...clone(meta), id: String(claim.id), from: fromRef, to: toRef, kind,
          evidence: evidence(meta.evidence, row(claim.assessment).evidence_class), basis: clone(row(claim.assessment).basis ?? null),
          properties: { raw: clone(claim) } };
      });
      return { relations, total: paged.total, ...(paged.nextCursor ? { nextCursor: paged.nextCursor } : {}),
        status: permissionFiltered ? "denied" : "available", ...(permissionFiltered ? { reason: "permission_filtered" } : {}) };
    },
  };
}
export const createGraphPacketAdapter = createPacketAdapter;

/** Host-owned demand loaders may parse/project a previously unopened source.
 * No renderer, URL guessing, automatic discovery dispatch or source mutation. */
export function createDemandAdapter(descriptor: SourceDescriptor, loader?: DemandLoader): SourceAdapter {
  return {
    descriptor: clone(descriptor),
    async resolve(ref, context) {
      const blocked = gate(ref, descriptor, context); if (blocked) return blocked;
      if (!loader) return absent(ref, descriptor.status === "unknown" ? "unknown" : "unloaded", "loader_not_available");
      try {
        const object = await loader.resolve(clone(ref), context); abort(context);
        if (object.ref.source !== ref.source || object.ref.selector !== ref.selector) return absent(ref, "unknown", "loader_identity_mismatch");
        if (!context.permission.canRead(object.ref)) return absent(ref, "denied", "permission_denied");
        return clone(object);
      } catch (error) {
        abort(context);
        return absent(ref, "unavailable", error instanceof Error ? error.message : "loader_failed");
      }
    },
    async neighbors(ref, request, context) {
      const blocked = pageGate(ref, descriptor, context); if (blocked) return blocked;
      if (!supportedRelations(descriptor, request)) return { relations: [], status: "unknown", reason: "structure_unsupported" };
      if (!loader?.neighbors) return { relations: [], status: "unloaded", reason: "neighbor_loader_not_available" };
      try {
        const page = await loader.neighbors(clone(ref), clone(request), context); abort(context);
        const permitted = page.relations.filter(relation => context.permission.canRead(relation.from) && context.permission.canRead(relation.to));
        const filtered = permitted.length !== page.relations.length;
        return { ...clone(page), relations: clone(permitted), ...(filtered ? { status: "denied", reason: "permission_filtered" } : {}) };
      } catch (error) {
        abort(context);
        return { relations: [], status: "unavailable", reason: error instanceof Error ? error.message : "loader_failed" };
      }
    },
  };
}

export interface ProfileSourceOptions {
  descriptor: SourceDescriptor;
  sourceText: string;
  /** Domain edge vocabulary remains in the supplied description. */
  childRelation: string;
  registry?: ProfileRegistry;
}
/** Existing native-compatible application profile loader + existing RFC6901
 * source selector. Fields are accessible before any UI or profile renderer opens. */
export function createApplicationProfileAdapter(options: ProfileSourceOptions): SourceAdapter {
  let profile: ApplicationProfile | undefined;
  const load = () => {
    if (!profile) {
      const parsed = parseApplicationProfileSource(options.sourceText);
      const document = row(parsed);
      const registry = options.registry ?? createProfileRegistry({ renderers: [String(row(document.presentation).renderer)],
        operations: Array.isArray(document.actions) ? document.actions.map(action => String(row(action).operation)) : [] });
      profile = registerProfile(registry, parsed);
    }
    return profile;
  };
  function reference(selector: string): ObjectRef {
    const current = load();
    return { source: options.descriptor.id, selector, canonicalId: `${current.id}${selector}`, snapshot: String(current.profile_revision) };
  }
  return createDemandAdapter(options.descriptor, {
    async resolve(ref) {
      const current = load();
      if (ref.snapshot !== undefined && ref.snapshot !== String(current.profile_revision)) return absent(ref, "unavailable", "snapshot_not_available");
      const selected = importedPointer(current, ref.selector);
      if (!selected.found) return absent(ref, "unknown", "selector_not_found");
      const value = selected.value;
      return { ref: { ...ref, ...reference(ref.selector) }, label: ref.selector || current.label,
        kind: value === null ? "null" : Array.isArray(value) ? "array" : typeof value,
        status: "available", evidence: "source", properties: { value: clone(value), loader: "registerProfile", sourceText: ref.selector === "" ? options.sourceText : undefined } };
    },
    async neighbors(ref, request) {
      const current = load();
      if ((request.snapshot ?? ref.snapshot) !== undefined && (request.snapshot ?? ref.snapshot) !== String(current.profile_revision)) return { relations: [], status: "unavailable", reason: "snapshot_not_available" };
      if (!supportedRelations(options.descriptor, request)?.includes(options.childRelation)) return { relations: [], total: 0, status: "available" };
      const selected = importedPointer(current, ref.selector);
      if (!selected.found) return { relations: [], status: "unknown", reason: "selector_not_found" };
      const relations: SourceRelation[] = [];
      const edge = (from: string, to: string): SourceRelation => ({ id: JSON.stringify([from, to, options.childRelation]),
        from: reference(from), to: reference(to), kind: options.childRelation, evidence: "source", basis: { selector: to } });
      if (request.direction !== "incoming" && selected.value && typeof selected.value === "object") {
        for (const key of Object.keys(selected.value)) relations.push(edge(ref.selector, `${ref.selector}/${key.replace(/~/g, "~0").replace(/\//g, "~1")}`));
      }
      if (request.direction !== "outgoing" && ref.selector !== "") relations.push(edge(ref.selector.slice(0, ref.selector.lastIndexOf("/")), ref.selector));
      const paged = pagination(relations, request);
      return { relations: paged.items, total: paged.total, ...(paged.nextCursor ? { nextCursor: paged.nextCursor } : {}), status: "available" };
    },
  });
}
