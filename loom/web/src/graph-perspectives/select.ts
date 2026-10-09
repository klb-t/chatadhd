import type { LocalResolutionRule, ObjectRef, PermissionContext, ProjectedObject, QueryPlan, SelectionResult, SourceAdapter, SourceObject, SourceRelation, VisualRule } from "./types";
import { objectKey, referenceKey } from "./navigation";

function matches(rule: LocalResolutionRule["match"] | VisualRule["match"], item: SourceObject, distance: number, focus: ObjectRef | null): boolean {
  for (const [key, value] of Object.entries(rule)) {
    if (key === "focus" && value !== (focus !== null && objectKey(item.ref) === objectKey(focus))) return false;
    if (key === "distance" && value !== distance) return false;
    if (key === "kind" && value !== item.kind) return false;
    if (key === "source" && value !== item.ref.source) return false;
    if (key === "selectors" && (!Array.isArray(value) || !value.includes(item.ref.selector))) return false;
    if (key === "snapshot" && value !== item.ref.snapshot) return false;
    if (key === "evidence" && value !== item.evidence) return false;
    if (key === "status" && value !== item.status) return false;
    if (!["focus", "distance", "kind", "source", "selectors", "snapshot", "evidence", "status"].includes(key)) return false;
  }
  return true;
}
function project(item: SourceObject, distance: number, plan: QueryPlan): { object: ProjectedObject; aggregate?: LocalResolutionRule["aggregate"] } {
  let resolution = plan.resolution, aggregate: LocalResolutionRule["aggregate"];
  for (const rule of plan.localResolution) if (matches(rule.match, item, distance, plan.focus)) { resolution = rule.resolution; aggregate = rule.aggregate; }
  const visual: ProjectedObject["visual"] = { opacity: 1, blur: 0 };
  const visualReasons: ProjectedObject["visualReasons"] = [];
  for (const rule of plan.visual) if (matches(rule.match, item, distance, plan.focus)) {
    // The neutral opacity is a rendering identity, never epistemic confidence.
    for (const key of ["opacity", "blur", "color", "group", "label"] as const) if (rule.style[key] !== undefined) Object.assign(visual, { [key]: rule.style[key] });
    visualReasons.push({ dimension: rule.dimension, rule: rule.id, explanation: rule.explanation });
  }
  return { object: { ...structuredClone(item), key: referenceKey(item.ref), canonicalKey: objectKey(item.ref), distance, resolution, visual, visualReasons }, aggregate };
}
function placeholder(ref: ObjectRef, status: SourceObject["status"], reason: string): SourceObject {
  return { ref: structuredClone(ref), label: ref.selector || ref.source, kind: "reference", status, evidence: "unknown", reason };
}
export interface SelectOptions { signal?: AbortSignal; start?: SelectionResult["continuation"] }

/** Demand-driven traversal. No graph writes, renderer, network policy or native-store import is required. */
export async function selectPerspective(plan: QueryPlan, adapters: SourceAdapter[], permission: PermissionContext, options: SelectOptions = {}): Promise<SelectionResult> {
  if (plan.errors.length) throw new Error(`invalid_perspective_plan:${plan.errors.join(",")}`);
  const begin = performance.now();
  const bySource = new Map<string, SourceAdapter>();
  for (const adapter of adapters) {
    if (bySource.has(adapter.descriptor.id)) throw new Error(`duplicate_source_adapter:${adapter.descriptor.id}`);
    bySource.set(adapter.descriptor.id, adapter);
  }
  const resolvedPlan = { ...plan, unsupported: plan.unsupported.filter(item => {
    if (item.reason !== "adapter_capability_pending") return true;
    return !adapters.some(adapter => adapter.descriptor.capabilities?.some(capability => capability.id === item.id && capability.status === "supported"));
  }) };
  const omissions: SelectionResult["omissions"] = [], continuation: SelectionResult["continuation"] = [];
  const queue: { ref: ObjectRef; distance: number; cursor?: string }[] = [], scheduled = new Set<string>();
  const enqueue = (ref: ObjectRef, distance: number, cursor?: string) => {
    const key = referenceKey(ref);
    if (!scheduled.has(key)) { scheduled.add(key); queue.push({ ref: structuredClone(ref), distance, cursor }); }
  };
  const focus = plan.focus ? { ...plan.focus, ...(plan.snapshot === undefined ? {} : { snapshot: plan.snapshot }) } : null;
  if (options.start) options.start.forEach(item => enqueue(item.ref, item.distance, item.cursor));
  else if (focus) {
    enqueue(focus, 0);
    plan.compareSnapshots.forEach(snapshot => {
      const other = { ...focus, snapshot };
      if (snapshot !== focus.snapshot) delete other.representation;
      enqueue(other, 0);
    });
  }
  const raw: { item: SourceObject; distance: number; requested: string }[] = [];
  const relations = new Map<string, SourceRelation>();
  let resolvedObjects = 0, neighborCalls = 0;
  for (let index = 0; index < queue.length; index++) {
    options.signal?.throwIfAborted();
    const current = queue[index], key = referenceKey(current.ref);
    if (raw.length >= plan.queryBudget) {
      for (const waiting of queue.slice(index)) { continuation.push(waiting); omissions.push({ ref: waiting.ref, reason: "query_budget" }); }
      break;
    }
    const adapter = bySource.get(current.ref.source);
    let item: SourceObject;
    if (!permission.canRead(current.ref)) {
      item = placeholder(current.ref, "denied", "permission_denied"); omissions.push({ ref: current.ref, reason: "permission" });
    } else if (!adapter) {
      item = placeholder(current.ref, "unloaded", "source_adapter_missing"); omissions.push({ ref: current.ref, reason: "adapter_missing" });
    } else {
      try {
        resolvedObjects++;
        item = await adapter.resolve(current.ref, { signal: options.signal, permission });
        // A resolver may assert canonical identity but may not silently redirect the address.
        if (item.ref.source !== current.ref.source || item.ref.selector !== current.ref.selector) throw new Error("adapter_changed_address");
        if (current.ref.snapshot !== undefined && item.ref.snapshot !== undefined && item.ref.snapshot !== current.ref.snapshot) throw new Error("adapter_changed_snapshot");
        item = { ...item, ref: { ...current.ref, ...item.ref } };
      } catch (error) {
        options.signal?.throwIfAborted();
        item = placeholder(current.ref, "unavailable", String(error));
      }
    }
    raw.push({ item, distance: current.distance, requested: key });
    if (item.status !== "available") {
      omissions.push({ ref: item.ref, reason: "source_status", detail: item.reason ?? item.status });
      continue;
    }
    if (!adapter || current.distance >= plan.hops || !plan.relations.length) continue;
    let cursor = current.cursor;
    const cursors = new Set<string>();
    while (true) {
      options.signal?.throwIfAborted();
      const limit = Math.min(plan.pageSize, Math.max(1, plan.queryBudget - queue.length));
      let page;
      try {
        neighborCalls++;
        page = await adapter.neighbors(item.ref, { structure: plan.structure, relations: [...plan.relations], direction: plan.direction,
          snapshot: item.ref.snapshot, cursor, limit, parameters: plan.values }, { signal: options.signal, permission });
      } catch (error) {
        options.signal?.throwIfAborted();
        omissions.push({ ref: item.ref, reason: "source_status", detail: String(error) });
        break;
      }
      if (page.status && page.status !== "available") omissions.push({ ref: item.ref, reason: "source_status", detail: page.reason ?? page.status });
      for (const edge of page.relations) {
        if (!plan.relations.includes("*") && !plan.relations.includes(edge.kind)) continue;
        const outgoing = referenceKey(edge.from) === referenceKey(item.ref) || (objectKey(edge.from) === objectKey(item.ref) && edge.from.snapshot === item.ref.snapshot);
        const incoming = referenceKey(edge.to) === referenceKey(item.ref) || (objectKey(edge.to) === objectKey(item.ref) && edge.to.snapshot === item.ref.snapshot);
        if (!(outgoing && plan.direction !== "incoming") && !(incoming && plan.direction !== "outgoing")) continue;
        relations.set(JSON.stringify([edge.id, referenceKey(edge.from), referenceKey(edge.to)]), structuredClone(edge));
        if (outgoing && plan.direction !== "incoming") enqueue(edge.to, current.distance + 1);
        if (incoming && plan.direction !== "outgoing") enqueue(edge.from, current.distance + 1);
      }
      if (!page.nextCursor) break;
      if (queue.length >= plan.queryBudget) { continuation.push({ ref: current.ref, cursor: page.nextCursor, distance: current.distance }); omissions.push({ ref: current.ref, reason: "query_budget", detail: "neighbor_page_pending" }); break; }
      if (cursors.has(page.nextCursor) || page.nextCursor === cursor) {
        omissions.push({ ref: current.ref, reason: "source_status", detail: "adapter_cursor_did_not_advance" }); break;
      }
      cursors.add(page.nextCursor); cursor = page.nextCursor;
    }
  }
  const objects: ProjectedObject[] = [], remap = new Map<string, string>(), aggregateMap = new Map<string, ProjectedObject>();
  for (const { item, distance, requested } of raw) {
    // Status placeholders and the selected address remain visible even under an evidence filter.
    if (plan.evidence.length && !plan.evidence.includes(item.evidence) && item.status === "available" && (!focus || objectKey(item.ref) !== objectKey(focus))) {
      omissions.push({ ref: item.ref, reason: "evidence_filter" }); continue;
    }
    const projected = project(item, distance, { ...plan, focus });
    if (projected.aggregate && (!focus || objectKey(item.ref) !== objectKey(focus))) {
      const groupKey = JSON.stringify([item.ref.source, projected.aggregate.id, item.ref.snapshot ?? null]);
      let group = aggregateMap.get(groupKey);
      if (!group) {
        const ref = { source: item.ref.source, selector: `aggregate:${projected.aggregate.id}`, representation: "aggregate", ...(item.ref.snapshot === undefined ? {} : { snapshot: item.ref.snapshot }) };
        group = { ...projected.object, ref, key: referenceKey(ref), canonicalKey: objectKey(ref), label: projected.aggregate.label,
          kind: "aggregate", evidence: "computed", properties: { aggregateRule: projected.aggregate.id }, aggregateMembers: [] };
        delete group.confidence;
        aggregateMap.set(groupKey, group); objects.push(group);
      }
      group.aggregateMembers!.push(structuredClone(item.ref)); group.distance = Math.min(group.distance, distance);
      remap.set(requested, group.key); remap.set(referenceKey(item.ref), group.key);
      omissions.push({ ref: item.ref, reason: "aggregate", detail: group.key });
    } else {
      objects.push(projected.object); remap.set(requested, projected.object.key); remap.set(referenceKey(item.ref), projected.object.key);
    }
  }
  const focusKey = focus === null ? null : remap.get(referenceKey(focus));
  objects.sort((a, b) => Number(b.key === focusKey) - Number(a.key === focusKey) || a.distance - b.distance || a.key.localeCompare(b.key));
  const visible = objects.slice(0, plan.renderBudget);
  for (const item of objects.slice(plan.renderBudget)) omissions.push({ ref: item.ref, reason: "render_budget", count: item.aggregateMembers?.length ?? 1 });
  const visibleKeys = new Set(visible.map(item => item.key));
  const canonicalAddresses = new Map<string, Set<string>>();
  for (const { item } of raw) {
    const key = JSON.stringify([objectKey(item.ref), item.ref.snapshot ?? null]);
    const address = remap.get(referenceKey(item.ref));
    if (address) { const matches = canonicalAddresses.get(key) ?? new Set<string>(); matches.add(address); canonicalAddresses.set(key, matches); }
  }
  const endpoint = (ref: ObjectRef) => {
    const exact = remap.get(referenceKey(ref));
    if (exact) return exact;
    const candidates = canonicalAddresses.get(JSON.stringify([objectKey(ref), ref.snapshot ?? null]));
    return candidates?.size === 1 ? [...candidates][0] : undefined;
  };
  const selectedRelations = [...relations.values()];
  const graph = {
    nodes: visible.map(item => ({ id: item.key, kind: item.kind, label: item.visual.label ?? item.label,
      ...(typeof item.properties?.content === "string" ? { content: item.properties.content } : {}),
      metadata: { perspective: item, canonicalKey: item.canonicalKey } })),
    edges: selectedRelations.flatMap(edge => {
      const src = endpoint(edge.from), dst = endpoint(edge.to);
      return src && dst && src !== dst && visibleKeys.has(src) && visibleKeys.has(dst) ? [{ id: edge.id, src, dst, type: edge.kind, metadata: { relation: edge } }] : [];
    }),
  };
  return {
    plan: resolvedPlan, objects: visible, relations: selectedRelations, graph, analysis: structuredClone(plan.sourcePerspective.analysis?.selected ?? []), permissionId: permission.id,
    omissions, continuation, complete: !omissions.some(item => ["query_budget", "render_budget", "source_status", "adapter_missing", "permission"].includes(item.reason)),
    metrics: { selectionMs: performance.now() - begin, resolvedObjects, neighborCalls, discoveredObjects: scheduled.size, renderedObjects: visible.length },
  };
}
