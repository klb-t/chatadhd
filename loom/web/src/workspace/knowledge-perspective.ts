import productSource from "../../../data/profiles/graph_perspective.pack?raw";
import catalog from "../../../data/graph_perspectives/catalog.json";
import { createDemandAdapter } from "../graph-perspectives/adapters";
import { compilePerspective } from "../graph-perspectives/plan";
import { selectPerspective } from "../graph-perspectives/select";
import type { CapabilityDescriptor, NativeResolution, ObjectRef, PermissionContext, Perspective, SelectionResult, SourceAdapter, SourceObject, SourceRelation } from "../graph-perspectives/types";
import type { KnowledgeApi, KnowledgeRecord } from "../api/knowledge";

export const perspectivePack = JSON.parse(productSource) as {
  schema: string; pack_id: string; revision: number; entries: { id: string; key: string; area: string; revision: number; label: string; value: unknown }[];
  presentation: { locale: string; locales: Record<string, Record<string, string>> };
  capability_ids: string[]; source: { structure: string; relations: string[]; directions: ("incoming" | "outgoing" | "both")[] };
  supported_resolutions: string[];
};
export const perspectiveText = perspectivePack.presentation.locales[perspectivePack.presentation.locale];
if (!perspectiveText || perspectivePack.schema !== "loom.default_layers_pack/1" || !Array.isArray(perspectivePack.entries)) throw new Error("product_perspective_pack_invalid");
export const perspectiveCapabilities = catalog.capabilities.filter(row => perspectivePack.capability_ids.includes(row.id)) as CapabilityDescriptor[];
export const perspectiveEnabledKey = "graph.perspective.enabled";
export type KnowledgePerspectiveData = { entities: KnowledgeRecord[]; claims: KnowledgeRecord[]; partial: boolean; errors: string[] };
export interface KnowledgePerspectiveSource {
  adapter: SourceAdapter;
  ref(id: string): ObjectRef;
  load(): Promise<KnowledgePerspectiveData>;
}
const row = (value: unknown): KnowledgeRecord => value !== null && typeof value === "object" && !Array.isArray(value) ? value as KnowledgeRecord : {};
const text = (value: unknown): string => typeof value === "string" ? value : "";

/** Host boundary for the existing authenticated local KnowledgeApi. This never
 * authorizes model disclosure, writes source data, or invents a graph packet. */
export function createKnowledgePerspectiveSource(api: Pick<KnowledgeApi, "query">, run: string, limit: number,
  initial?: KnowledgePerspectiveData): KnowledgePerspectiveSource {
  if (!run || !Number.isSafeInteger(limit) || limit < 1) throw new Error("knowledge_perspective_source_invalid");
  const source = `knowledge:${run}`;
  const ref = (id: string): ObjectRef => ({ source, selector: id, canonicalId: id, snapshot: run });
  let pending: Promise<KnowledgePerspectiveData> | undefined;
  const load = () => {
    if (!pending) pending = initial ? Promise.resolve(initial) : Promise.all([
      api.query("entities", { run, limit }), api.query("claims", { run, limit }),
    ]).then(([entities, claims]) => {
      if (entities.run !== run || claims.run !== run) throw new Error("knowledge_perspective_run_mismatch");
      return { entities: entities.items, claims: claims.items,
        partial: entities.has_more === true || claims.has_more === true || entities.items.length >= limit || claims.items.length >= limit, errors: [] };
    });
    return pending;
  };
  const descriptor = { id: source, label: perspectiveText.source, structures: [{ id: perspectivePack.source.structure,
    label: perspectiveText.source, relations: perspectivePack.source.relations, directions: perspectivePack.source.directions }] };
  function unavailable(address: ObjectRef, reason: string): SourceObject {
    return { ref: address, label: address.selector, kind: "reference", status: "unavailable", evidence: "unknown", reason };
  }
  const versionMatches = (address: ObjectRef) => address.snapshot === undefined || address.snapshot === run;
  const adapter = createDemandAdapter(descriptor, {
    async resolve(address, context) {
      if (!versionMatches(address)) return unavailable(address, "knowledge_run_version_unavailable");
      context.signal?.throwIfAborted();
      const data = await load(); context.signal?.throwIfAborted();
      const entity = data.entities.find(item => item.id === address.selector);
      if (!entity) return unavailable(address, data.partial ? "entity_outside_loaded_prefix" : "entity_unavailable");
      return { ref: { ...address, ...ref(address.selector) }, label: text(entity.label) || address.selector,
        kind: text(entity.kind), evidence: text(entity.evidence_class) || "unknown", status: "available",
        properties: { raw: structuredClone(entity), source_run: run, coverage: data.partial ? "prefix" : "complete", errors: [...data.errors] } };
    },
    async neighbors(address, request, context) {
      if (!versionMatches(address) || (request.snapshot !== undefined && request.snapshot !== run))
        return { relations: [], status: "unavailable", reason: "knowledge_run_version_unavailable" };
      if (request.structure !== perspectivePack.source.structure) return { relations: [], status: "unknown", reason: "structure_unsupported" };
      if (!Number.isSafeInteger(request.limit) || request.limit < 1 || (request.cursor !== undefined && !/^(0|[1-9][0-9]*)$/.test(request.cursor))) throw new Error("knowledge_neighbor_page_invalid");
      const offset = Number(request.cursor ?? 0);
      if (!Number.isSafeInteger(offset)) throw new Error("knowledge_neighbor_page_invalid");
      const data = await load(); context.signal?.throwIfAborted();
      const relations: SourceRelation[] = [];
      const entityIds = new Set(data.entities.map(entity => entity.id));
      for (const claim of data.claims) {
        const from = text(claim.subject), to = text(claim.object), kind = text(claim.predicate);
        if (!from || !to || !kind || (!request.relations.includes("*") && !request.relations.includes(kind))) continue;
        if (!(request.direction !== "incoming" && from === address.selector) && !(request.direction !== "outgoing" && to === address.selector)) continue;
        // Native literal-valued claims remain literal records, not fabricated nodes.
        if (!entityIds.has(to) || !entityIds.has(from)) continue;
        relations.push({ id: text(claim.id), from: ref(from), to: ref(to), kind,
          evidence: text(row(claim.assessment).evidence_class) || "unknown", properties: { raw: structuredClone(claim), source_run: run } });
      }
      const end = offset + request.limit;
      return { relations: relations.slice(offset, end), total: relations.length,
        ...(end < relations.length ? { nextCursor: String(end) } : {}),
        status: data.partial || data.errors.length ? "unavailable" : "available",
        ...(data.partial || data.errors.length ? { reason: data.errors.join("; ") || "knowledge_prefix_limited" } : {}) };
    },
  });
  return { adapter, ref, load };
}

/** Shared by the workspace and headless callers; no renderer-dependent state. */
export async function selectKnowledgePerspective(perspective: Perspective, resolution: NativeResolution,
  source: KnowledgePerspectiveSource, permission: PermissionContext, signal?: AbortSignal): Promise<SelectionResult> {
  const relevant = { ...resolution, components: resolution.components.filter(component => perspectivePack.capability_ids.includes(component.id)) };
  const plan = compilePerspective(perspective, relevant, perspectiveCapabilities);
  if (plan.structure !== perspectivePack.source.structure) plan.errors.push("knowledge_structure_unavailable");
  if (!perspectivePack.supported_resolutions.includes(plan.resolution) || plan.localResolution.some(rule => !perspectivePack.supported_resolutions.includes(rule.resolution))) plan.errors.push("knowledge_resolution_unavailable");
  if (plan.localResolution.some(rule => rule.aggregate !== undefined)) plan.errors.push("knowledge_aggregation_navigation_unavailable");
  if (plan.visual.some(rule => rule.style.group !== undefined)) plan.errors.push("knowledge_visual_group_unavailable");
  const selected = await selectPerspective(plan, [source.adapter], permission, { signal });
  for (const item of selected.objects) {
    const errors = item.properties?.errors;
    if (item.properties?.coverage === "prefix" || (Array.isArray(errors) && errors.length)) {
      selected.omissions.push({ ref: item.ref, reason: "source_status", detail: "knowledge_prefix_limited" });
      selected.complete = false;
    }
  }
  return selected;
}

/** Feed selected rows to the existing SVG; keep native records for linked views. */
export function perspectiveDataset(selection: SelectionResult): { entities: KnowledgeRecord[]; claims: KnowledgeRecord[] } {
  const entities = selection.objects.map(item => {
    const native = structuredClone(row(item.properties?.raw));
    // Source confidence remains inspectable in raw provenance. A view does not
    // promote it to a measured/calibrated presentation metric.
    delete native.confidence;
    return { ...native, id: item.key, label: item.visual.label ?? item.label, kind: item.kind, evidence_class: item.evidence,
      perspective: item, ...(item.confidence === undefined ? {} : { confidence: item.confidence }),
      ...(item.resolution === "details" ? { label: `${item.visual.label ?? item.label}${perspectiveText.detail_separator}${item.kind}` } : {}) };
  });
  const claims = selection.graph.edges.map(edge => ({ ...row(row(edge.metadata?.relation).properties).raw as KnowledgeRecord,
    native_record: structuredClone(row(row(edge.metadata?.relation).properties).raw),
    id: edge.id, subject: edge.src, object: edge.dst, predicate: edge.type }));
  return { entities, claims };
}
