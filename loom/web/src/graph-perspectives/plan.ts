import type { CapabilityDescriptor, EffectiveComponent, LocalResolutionRule, NativeResolution, ObjectRef, Perspective, QueryPlan, ResolutionPresentation, VisualRule } from "./types";

const object = (value: unknown): value is Record<string, unknown> => !!value && typeof value === "object" && !Array.isArray(value);
const strings = (value: unknown): value is string[] => Array.isArray(value) && value.every(item => typeof item === "string");
const integer = (value: unknown, min: number): value is number => Number.isSafeInteger(value) && Number(value) >= min;
/** Restrict a color value to self-contained CSS color syntax; never a paint-server URL or variable. */
export function isSupportedColor(value: unknown): value is string {
  if (typeof value !== "string") return false;
  const color = value.trim();
  if (/^#[0-9a-f]{3}(?:[0-9a-f]|[0-9a-f]{3}|[0-9a-f]{5})?$/i.test(color) || /^[a-z]+$/i.test(color)) return true;
  const number = "[-+]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:e[-+]?[0-9]+)?";
  const component = `${number}%?`, percent = `${number}%`, hue = `${number}(?:deg|grad|rad|turn)?`;
  const comma = (first: string, rest: string) => `${first}\\s*,\\s*${rest}\\s*,\\s*${rest}(?:\\s*,\\s*${component})?`;
  const space = (first: string, rest: string) => `${first}\\s+${rest}\\s+${rest}(?:\\s*/\\s*${component})?`;
  return new RegExp(`^(?:rgba?\\(\\s*(?:${comma(component, component)}|${space(component, component)})\\s*\\)|hsla?\\(\\s*(?:${comma(hue, percent)}|${space(hue, percent)})\\s*\\))$`, "i").test(color);
}
export function isObjectRef(value: unknown): value is ObjectRef {
  return object(value) && typeof value.source === "string" && !!value.source && typeof value.selector === "string" &&
    ["canonicalId", "representation", "snapshot"].every(key => value[key] === undefined || typeof value[key] === "string");
}
/** Compile resolved native values. This is deliberately not a layer/fallback engine. */
export function compilePerspective(perspective: Perspective, resolution: NativeResolution, capabilities: CapabilityDescriptor[]): QueryPlan {
  const values: Record<string, unknown> = Object.create(null), errors: string[] = [], unsupported: QueryPlan["unsupported"] = [];
  const descriptors = new Map(capabilities.map(item => [item.id, item]));
  const engineTargets = new Set(["structure", "resolution", "traversal", "temporal", "evidence", "visual", "budget", "localResolution", "goal", "focus", "presentation"]);
  const seen = new Set<string>();
  const explanation: EffectiveComponent[] = structuredClone(resolution.components);
  for (const row of resolution.components) {
    if (seen.has(row.id)) { errors.push(`duplicate_effective_component:${row.id}`); continue; }
    seen.add(row.id);
    const descriptor = descriptors.get(row.id);
    if (!descriptor) { unsupported.push({ id: row.id, reason: "capability_descriptor_missing", value: row.value }); continue; }
    if (descriptor.status === "unsupported") { unsupported.push({ id: row.id, reason: descriptor.reason ?? "capability_unsupported", value: row.value }); continue; }
    if (row.status !== "effective") continue;
    if (descriptor.status === "limited") unsupported.push({ id: row.id, reason: descriptor.reason ?? "capability_limited", value: row.value });
    if (Object.prototype.hasOwnProperty.call(values, descriptor.target)) { errors.push(`duplicate_target:${descriptor.target}`); continue; }
    values[descriptor.target] = structuredClone(row.value);
    if (!engineTargets.has(descriptor.target)) unsupported.push({ id: row.id, reason: "adapter_capability_pending", value: structuredClone(row.value) });
  }
  for (const [id, value] of Object.entries(perspective.components)) if (!descriptors.has(id) && !unsupported.some(item => item.id === id)) {
    unsupported.push({ id, reason: "capability_descriptor_missing", value: structuredClone(value) });
  }
  const traversal = object(values.traversal) ? values.traversal : {};
  const temporal = object(values.temporal) ? values.temporal : {};
  const budget = object(values.budget) ? values.budget : {};
  if (typeof values.structure !== "string") errors.push("structure_not_effective");
  if (typeof values.resolution !== "string") errors.push("resolution_not_effective");
  if (!strings(traversal.relations)) errors.push("traversal_relations_invalid");
  if (!["incoming", "outgoing", "both"].includes(String(traversal.direction))) errors.push("traversal_direction_invalid");
  if (!integer(traversal.hops, 0)) errors.push("traversal_hops_invalid");
  for (const field of ["query", "render", "page"]) if (!integer(budget[field], 1)) errors.push(`budget_${field}_invalid`);
  if (budget.neighborPages !== undefined && !integer(budget.neighborPages, 1)) errors.push("budget_neighbor_pages_invalid");
  if (budget.neighborPages === undefined && integer(budget.query, 1)) explanation.push({ id: "budget.neighborPages", value: budget.query, status: "effective", origin: { kind: "plan_alias", target: "budget.query" }, reason: "legacy_neighbor_budget_equals_query_budget" });
  if (temporal.snapshot !== undefined && typeof temporal.snapshot !== "string") errors.push("snapshot_invalid");
  if (temporal.compareSnapshots !== undefined && !strings(temporal.compareSnapshots)) errors.push("compare_snapshots_invalid");
  if (values.evidence !== undefined && !strings(values.evidence)) errors.push("evidence_invalid");
  const local = values.localResolution ?? [];
  if (!Array.isArray(local) || local.some(rule => !object(rule) || typeof rule.id !== "string" || !object(rule.match) || typeof rule.resolution !== "string" ||
      (rule.aggregate !== undefined && (!object(rule.aggregate) || typeof rule.aggregate.id !== "string" || typeof rule.aggregate.label !== "string")))) errors.push("local_resolution_invalid");
  const visual = values.visual ?? [];
  if (!Array.isArray(visual) || visual.some(rule => !object(rule) || typeof rule.id !== "string" || typeof rule.dimension !== "string" || typeof rule.explanation !== "string" || !object(rule.match) || !object(rule.style) ||
      (rule.style.opacity !== undefined && (typeof rule.style.opacity !== "number" || !Number.isFinite(rule.style.opacity) || rule.style.opacity < 0 || rule.style.opacity > 1)) ||
      (rule.style.blur !== undefined && (typeof rule.style.blur !== "number" || !Number.isFinite(rule.style.blur) || rule.style.blur < 0)))) errors.push("visual_mapping_invalid");
  const presentation = values.presentation ?? [];
  if (!Array.isArray(presentation) || presentation.some(descriptor => !object(descriptor) || typeof descriptor.id !== "string" || !Array.isArray(descriptor.fields) ||
    descriptor.fields.some(field => !object(field) || typeof field.id !== "string" || typeof field.label !== "string" || typeof field.path !== "string" ||
      (field.path !== "" && !field.path.startsWith("/")) || /~(?:[^01]|$)/.test(field.path)))) errors.push("presentation_fields_invalid");
  if (Array.isArray(presentation)) {
    const ids = presentation.filter(object).map(item => item.id);
    if (new Set(ids).size !== ids.length) errors.push("presentation_resolution_duplicate");
    for (const descriptor of presentation) if (object(descriptor) && Array.isArray(descriptor.fields)) {
      const fields = descriptor.fields.filter(object).map(item => item.id);
      if (new Set(fields).size !== fields.length) errors.push("presentation_field_duplicate");
    }
  }
  const localMatchKeys = new Set(["focus", "distance", "kind", "source", "selectors"]);
  const visualMatchKeys = new Set(["focus", "distance", "kind", "snapshot", "evidence", "status"]);
  for (const [group, rules, keys] of [["localResolution", local, localMatchKeys], ["visual", visual, visualMatchKeys]] as const) {
    if (!Array.isArray(rules)) continue;
    for (const rule of rules) {
      if (!object(rule) || !object(rule.match)) continue;
      for (const [key, value] of Object.entries(rule.match)) {
        if (!keys.has(key)) unsupported.push({ id: `${group}.${String(rule.id)}.match.${key}`, reason: "match_field_unsupported", value });
        else if ((key === "distance" && !integer(value, 0)) || (key === "focus" && typeof value !== "boolean") || (key === "selectors" && !strings(value)) ||
          (!["distance", "focus", "selectors"].includes(key) && typeof value !== "string")) errors.push(`${group}_match_invalid:${String(rule.id)}:${key}`);
      }
      if (group === "visual" && object(rule.style)) for (const [key, value] of Object.entries(rule.style)) {
        if (!["opacity", "blur", "color", "group", "label"].includes(key)) unsupported.push({ id: `${group}.${String(rule.id)}.style.${key}`, reason: "style_field_unsupported", value });
        else if (key === "color" && !isSupportedColor(value)) errors.push(`visual_color_unsupported:${String(rule.id)}`);
        else if (["color", "group", "label"].includes(key) && typeof value !== "string") errors.push(`visual_style_invalid:${String(rule.id)}:${key}`);
      }
    }
  }
  // Navigation is an explicit transient reference, independent of R40 profile composition.
  const focus = perspective.focus ?? values.focus ?? null;
  if (perspective.focus !== null) explanation.push({ id: "navigation.focus", value: structuredClone(perspective.focus), status: "effective", origin: { kind: "navigation" }, reason: "explicit_navigation_focus" });
  if (focus !== null && !isObjectRef(focus)) errors.push("focus_invalid");
  return {
    schema: "loom.graph_perspective_plan/1", perspectiveId: perspective.id, focus: isObjectRef(focus) ? structuredClone(focus) : null,
    structure: typeof values.structure === "string" ? values.structure : "", relations: strings(traversal.relations) ? [...traversal.relations] : [],
    direction: traversal.direction as QueryPlan["direction"], hops: integer(traversal.hops, 0) ? traversal.hops : 0,
    resolution: typeof values.resolution === "string" ? values.resolution : "", localResolution: Array.isArray(local) ? structuredClone(local) as LocalResolutionRule[] : [],
    ...(typeof temporal.snapshot === "string" ? { snapshot: temporal.snapshot } : {}),
    compareSnapshots: strings(temporal.compareSnapshots) ? [...temporal.compareSnapshots] : [], evidence: strings(values.evidence) ? [...values.evidence] : [],
    queryBudget: integer(budget.query, 1) ? budget.query : 0, neighborBudget: integer(budget.neighborPages, 1) ? budget.neighborPages : integer(budget.query, 1) ? budget.query : 0, renderBudget: integer(budget.render, 1) ? budget.render : 0,
    pageSize: integer(budget.page, 1) ? budget.page : 0, visual: Array.isArray(visual) ? structuredClone(visual) as VisualRule[] : [],
    presentation: Array.isArray(presentation) ? structuredClone(presentation) as ResolutionPresentation[] : [],
    values, explanation, unsupported, errors, sourcePerspective: structuredClone(perspective),
  };
}
