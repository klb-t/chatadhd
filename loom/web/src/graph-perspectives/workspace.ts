import { changeParameter, connect, parseWorkspace, type Parameter, type Parameters, type Workspace } from "../workspace/state";
import type { NativeResolution, ObjectRef, Perspective } from "./types";

/** Data mapping to the existing per-parameter binding graph, not another binding engine. */
export interface ComponentBinding {
  id: string;
  component: string;
  parameter: string;
  target: "component" | "focus";
  /** RFC6901 pointer inside a composite native value; empty/omitted selects all. */
  pointer?: string;
  selectionKind?: string;
  [key: string]: unknown;
}
export interface PerspectiveWorkspace {
  schema: "loom.graph_perspective_workspace/1";
  workspace: Workspace;
  perspectives: Record<string, Perspective>;
  mappings: ComponentBinding[];
  unsupported: { descriptor: ComponentBinding; reason: string }[];
  [key: string]: unknown;
}
export interface BindingOutcome { state: PerspectiveWorkspace; status: "applied" | "unsupported"; reason?: string; nativeResolutionRequired?: boolean }
export type NativePaneResolutions = Record<string, NativeResolution>;
const copy = <T,>(value: T): T => JSON.parse(JSON.stringify(value)) as T;
const object = (value: unknown): value is Record<string, unknown> => !!value && typeof value === "object" && !Array.isArray(value);

export function createPerspectiveWorkspace(workspace: Workspace, perspectives: Record<string, Perspective>): PerspectiveWorkspace {
  return parsePerspectiveWorkspace(JSON.stringify({ schema: "loom.graph_perspective_workspace/1", workspace, perspectives, mappings: [], unsupported: [] }));
}
/** Unknown perspective/envelope fields survive. Existing workspace validation stays authoritative. */
export function parsePerspectiveWorkspace(raw: string): PerspectiveWorkspace {
  const parsed: unknown = JSON.parse(raw);
  if (!object(parsed) || parsed.schema !== "loom.graph_perspective_workspace/1" || !object(parsed.perspectives) || !Array.isArray(parsed.mappings) || !Array.isArray(parsed.unsupported)) throw new Error("invalid_perspective_workspace");
  const workspace = parseWorkspace(JSON.stringify(parsed.workspace));
  for (const [id, perspective] of Object.entries(parsed.perspectives)) {
    if (!workspace.panes.some(pane => pane.id === id) || !object(perspective) || perspective.schema !== "loom.graph_perspective/1" || typeof perspective.id !== "string" || !object(perspective.components) || (perspective.focus !== null && !validRef(perspective.focus))) throw new Error("invalid_perspective_workspace_reference");
  }
  for (const mapping of parsed.mappings) if (!validMapping(mapping)) throw new Error("invalid_perspective_workspace_mapping");
  const meanings = new Map<string, string>();
  for (const mapping of parsed.mappings as ComponentBinding[]) {
    const meaning = JSON.stringify([mapping.component, mapping.target, mapping.pointer ?? ""]);
    if (meanings.has(mapping.parameter) && meanings.get(mapping.parameter) !== meaning) throw new Error("conflicting_perspective_workspace_mapping");
    meanings.set(mapping.parameter, meaning);
  }
  for (const unsupported of parsed.unsupported) if (!object(unsupported) || !validMapping(unsupported.descriptor) || typeof unsupported.reason !== "string") throw new Error("invalid_perspective_workspace_diagnostic");
  return { ...parsed, workspace } as unknown as PerspectiveWorkspace;
}
export function serializePerspectiveWorkspace(state: PerspectiveWorkspace): string { return JSON.stringify(state); }
function validRef(value: unknown): value is ObjectRef { return object(value) && typeof value.source === "string" && typeof value.selector === "string"; }
function validMapping(value: unknown): value is ComponentBinding {
  return object(value) && typeof value.id === "string" && typeof value.component === "string" && typeof value.parameter === "string" && ["component", "focus"].includes(String(value.target)) &&
    (value.pointer === undefined || (typeof value.pointer === "string" && (value.pointer === "" || value.pointer.startsWith("/")) && !/~(?![01])/.test(value.pointer)));
}
function unsupported(state: PerspectiveWorkspace, descriptor: ComponentBinding, reason: string): BindingOutcome {
  return { state: { ...state, unsupported: [...state.unsupported, { descriptor: copy(descriptor), reason }] }, status: "unsupported", reason };
}
function check(state: PerspectiveWorkspace, descriptor: ComponentBinding): string | undefined {
  if (!validMapping(descriptor)) return "invalid_binding_descriptor";
  if (!state.workspace.panes.length || !Object.prototype.hasOwnProperty.call(state.workspace.panes[0].parameters, descriptor.parameter)) return "unsupported_workspace_parameter";
  if (descriptor.target === "focus" && descriptor.parameter !== "selection") return "focus_requires_selection_parameter";
  if (descriptor.target !== "focus" && descriptor.parameter === "selection") return "selection_requires_focus_target";
  if (descriptor.target === "focus" && descriptor.pointer) return "focus_pointer_unsupported";
  if (state.mappings.some(item => item.parameter === descriptor.parameter && (item.component !== descriptor.component || item.target !== descriptor.target || (item.pointer ?? "") !== (descriptor.pointer ?? "")))) return "parameter_mapping_conflict";
  return undefined;
}
function pointerKeys(pointer: string | undefined): string[] { return pointer ? pointer.slice(1).split("/").map(key => key.replace(/~1/g, "/").replace(/~0/g, "~")) : []; }
function member(value: unknown, key: string): unknown {
  if (!value || typeof value !== "object" || !Object.prototype.hasOwnProperty.call(value, key) || ["__proto__", "prototype", "constructor"].includes(key)) throw new Error("binding_pointer_unavailable");
  return (value as Record<string, unknown>)[key];
}
function nativeComponent(descriptor: ComponentBinding, resolution?: NativeResolution): unknown {
  if (!resolution) throw new Error("native_resolution_required");
  const rows = resolution.components.filter(row => row.id === descriptor.component);
  if (rows.length !== 1) throw new Error("native_component_resolution_ambiguous");
  if (rows[0].status !== "effective") throw new Error("native_component_not_effective");
  return rows[0].value;
}
function readComponent(perspective: Perspective, descriptor: ComponentBinding, resolution?: NativeResolution): unknown {
  let value = descriptor.pointer ? nativeComponent(descriptor, resolution) : perspective.components[descriptor.component];
  for (const key of pointerKeys(descriptor.pointer)) value = member(value, key);
  return value;
}
function updatedComponent(descriptor: ComponentBinding, value: unknown, resolution?: NativeResolution): unknown {
  const keys = pointerKeys(descriptor.pointer);
  if (!keys.length) return copy(value);
  const root = copy(nativeComponent(descriptor, resolution));
  let parent = root;
  for (const key of keys.slice(0, -1)) parent = member(parent, key);
  const key = keys[keys.length - 1];
  member(parent, key);
  (parent as Record<string, unknown>)[key] = copy(value);
  return root;
}
function parameterValue(state: PerspectiveWorkspace, pane: string, descriptor: ComponentBinding, value: unknown): Parameters[Parameter] {
  if (descriptor.target !== "focus") return copy(value) as Parameters[Parameter];
  if (value === null) return null;
  if (!validRef(value)) throw new Error("invalid_focus_reference");
  return { kind: descriptor.selectionKind ?? "source", id: value.canonicalId ?? value.selector, focus: value.selector, run: value.snapshot ?? state.workspace.panes.find(item => item.id === pane)!.parameters.run };
}
function projectChanged(state: PerspectiveWorkspace, workspace: Workspace, descriptor: ComponentBinding, value: unknown, nativeByPane?: NativePaneResolutions): PerspectiveWorkspace {
  const perspectives = { ...state.perspectives };
  for (const pane of workspace.panes) {
    const before = state.workspace.panes.find(item => item.id === pane.id);
    if (before === pane || !perspectives[pane.id]) continue;
    const perspective = perspectives[pane.id];
    if (descriptor.target === "focus") perspectives[pane.id] = { ...perspective, focus: copy(value) as ObjectRef | null };
    else {
      if (perspective.layerActions !== undefined && !Array.isArray(perspective.layerActions)) throw new Error("invalid_native_layer_actions");
      const wholeValue = updatedComponent(descriptor, value, nativeByPane?.[pane.id]);
      perspectives[pane.id] = { ...perspective, components: { ...perspective.components, [descriptor.component]: wholeValue },
        layerActions: [...(perspective.layerActions as unknown[] | undefined ?? []), { op: "override", key: descriptor.component, value: copy(wholeValue) }] };
    }
  }
  return { ...state, workspace, perspectives, mappings: state.mappings.some(item => item.parameter === descriptor.parameter) ? state.mappings : [...state.mappings, copy(descriptor)] };
}
function failureReason(error: unknown, fallback: string): string {
  return error instanceof Error && ["native_resolution_required", "native_component_resolution_ambiguous", "native_component_not_effective", "binding_pointer_unavailable", "invalid_native_layer_actions"].includes(error.message) ? error.message : fallback;
}
export function updatePerspectiveComponent(state: PerspectiveWorkspace, pane: string, descriptor: ComponentBinding, value: unknown, nativeByPane?: NativePaneResolutions): BindingOutcome {
  const problem = check(state, descriptor);
  if (problem) return unsupported(state, descriptor, problem);
  if (!state.perspectives[pane]) return unsupported(state, descriptor, "missing_perspective_pane");
  try {
    const workspace = changeParameter(state.workspace, pane, descriptor.parameter as Parameter, parameterValue(state, pane, descriptor, value));
    parseWorkspace(JSON.stringify(workspace));
    return { state: projectChanged(state, workspace, descriptor, value, nativeByPane), status: "applied", nativeResolutionRequired: descriptor.target !== "focus" };
  } catch (error) { return unsupported(state, descriptor, failureReason(error, "invalid_workspace_parameter_value")); }
}
export function bindPerspectiveComponent(state: PerspectiveWorkspace, source: string, target: string, descriptor: ComponentBinding, nativeByPane?: NativePaneResolutions): BindingOutcome {
  const problem = check(state, descriptor);
  if (problem) return unsupported(state, descriptor, problem);
  if (source === target || !state.perspectives[source] || !state.perspectives[target]) return unsupported(state, descriptor, "invalid_perspective_binding_endpoints");
  let value: unknown;
  try { value = descriptor.target === "focus" ? state.perspectives[source].focus : readComponent(state.perspectives[source], descriptor, nativeByPane?.[source]); }
  catch (error) { return unsupported(state, descriptor, failureReason(error, "binding_pointer_unavailable")); }
  if (value === undefined) return unsupported(state, descriptor, "missing_perspective_component");
  const updated = updatePerspectiveComponent(state, source, descriptor, value, nativeByPane);
  if (updated.status !== "applied") return updated;
  const workspace = connect(updated.state.workspace, source, target, descriptor.parameter as Parameter);
  try { return { state: projectChanged(updated.state, workspace, descriptor, value, nativeByPane), status: "applied", nativeResolutionRequired: descriptor.target !== "focus" }; }
  catch (error) { return unsupported(state, descriptor, failureReason(error, "binding_pointer_unavailable")); }
}
