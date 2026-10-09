import { parseWorkspace } from "../workspace/state";
import type { Perspective } from "./types";
import { isObjectRef } from "./plan";

const record = (value: unknown): value is Record<string, unknown> => !!value && typeof value === "object" && !Array.isArray(value);
/** Persist intent, opaque options and native suppression actions verbatim, never source rows. */
export function exportPerspective(perspective: Perspective): string {
  const raw = JSON.stringify(perspective, null, 2);
  importPerspective(raw);
  return raw;
}
export function importPerspective(raw: string): { perspective: Perspective; warnings: string[] } {
  const value: unknown = JSON.parse(raw);
  if (!record(value)) throw new Error("perspective_object_required");
  if (value.schema === "loom.workbench/2") {
    const workspace = parseWorkspace(raw);
    const active = workspace.panes.find(pane => pane.id === workspace.active) ?? workspace.panes[0];
    const selection = active?.parameters.selection;
    return {
      perspective: { schema: "loom.graph_perspective/1", id: "legacy_workspace", components: {},
        focus: selection ? { source: selection.run || "workspace", selector: selection.id, representation: selection.kind } : null,
        workspace, legacyWorkspace: structuredClone(value), legacyParameters: structuredClone(active?.parameters ?? {}) },
      warnings: ["legacy_workspace_preserved", "legacy_parameters_require_native_resolution"],
    };
  }
  if (value.schema !== "loom.graph_perspective/1") throw new Error("perspective_schema_unsupported");
  if (typeof value.id !== "string" || !value.id || !record(value.components) || (value.focus !== null && !isObjectRef(value.focus))) throw new Error("perspective_invalid");
  if (value.label !== undefined && typeof value.label !== "string") throw new Error("perspective_label_invalid");
  if (value.workspace !== undefined) parseWorkspace(JSON.stringify(value.workspace));
  if (value.analysis !== undefined && (!record(value.analysis) || !Array.isArray(value.analysis.selected) || !value.analysis.selected.every(isObjectRef) ||
      (value.analysis.policyRef !== undefined && typeof value.analysis.policyRef !== "string") ||
      (value.analysis.linkedToPerspective !== undefined && typeof value.analysis.linkedToPerspective !== "boolean"))) throw new Error("perspective_analysis_invalid");
  const supported = new Set(["schema", "id", "label", "components", "focus", "workspace", "analysis", "layerActions"]);
  const warnings = Object.keys(value).filter(key => !supported.has(key)).map(key => `unknown_field_preserved:${key}`);
  return { perspective: structuredClone(value) as unknown as Perspective, warnings };
}
