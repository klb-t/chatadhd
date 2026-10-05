import { useCallback, useEffect, useState } from "react";
import defaults from "../profiles/data/resource-controls.json";

export type ResourceControlPresets = typeof defaults;
export type ContextPreviewParameters = ResourceControlPresets["context_preview"]["defaults"];
export const RESOURCE_PRESET_KEY = "loom.resource-controls.presets.v1";
export const CONTEXT_PREVIEW_KEY = "loom.context-preview.parameters.v1";
export const SEMANTIC_BUDGET_KEY = "loom.semantic-analysis.budget.v1";
const PRESET_EVENT = "loom-resource-preset-change";
// Representation boundary of the current native int fields, not a usage policy.
export const NATIVE_INT_MAX = 2 ** 31 - 1;
export function nativeResourceInteger(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value) && Number.isInteger(value) && value >= 0 && value <= NATIVE_INT_MAX;
}
function record(value: unknown): value is Record<string, unknown> {
  return Boolean(value && typeof value === "object" && !Array.isArray(value));
}
// Object overlay; arrays and scalar values replace their default counterpart.
function merge(base: unknown, overlay: unknown): unknown {
  if (!record(base) || !record(overlay)) return overlay;
  return Object.fromEntries([...new Set([...Object.keys(base), ...Object.keys(overlay)])].map(key =>
    [key, Object.prototype.hasOwnProperty.call(overlay, key) ? merge(base[key], overlay[key]) : base[key]]));
}
export function validContextPreview(value: unknown): value is ContextPreviewParameters {
  return record(value) && typeof value.text === "string" && typeof value.auto_preview === "boolean" &&
    nativeResourceInteger(value.depth) && nativeResourceInteger(value.max_tokens) && nativeResourceInteger(value.debounce_ms);
}
export function validSemanticBudget(value: unknown): value is Record<string, number> {
  return record(value) && Object.keys(defaults.semantic_analysis.defaults).every(key => nativeResourceInteger(value[key])) && Object.values(value).every(nativeResourceInteger);
}
export function resolveResourcePresets(overlay: unknown): ResourceControlPresets {
  if (!record(overlay)) throw new Error("Resource preset override must be a JSON object.");
  const resolved = merge(defaults, overlay) as ResourceControlPresets;
  if (resolved.schema !== defaults.schema || !record(resolved.context_preview) || !validContextPreview(resolved.context_preview.defaults) ||
      !record(resolved.context_preview.sliders) || !record(resolved.semantic_analysis) || !validSemanticBudget(resolved.semantic_analysis.defaults) ||
      !Array.isArray(resolved.semantic_analysis.fields)) throw new Error("Invalid resource preset schema or native integer values.");
  for (const key of ["depth", "max_tokens"] as const) {
    const range = resolved.context_preview.sliders[key];
    if (!record(range) || !nativeResourceInteger(range.min) || !nativeResourceInteger(range.max) || !nativeResourceInteger(range.step) || range.max < range.min || range.step < 1)
      throw new Error(`Invalid ${key} suggested slider range.`);
  }
  const keys = new Set<string>();
  for (const field of resolved.semantic_analysis.fields) {
    if (!record(field) || typeof field.key !== "string" || !field.key || keys.has(field.key) || typeof field.label !== "string" ||
        !nativeResourceInteger(resolved.semantic_analysis.defaults[field.key as keyof typeof resolved.semantic_analysis.defaults]) ||
        !nativeResourceInteger(field.suggested_min) || !nativeResourceInteger(field.suggested_max) || field.suggested_max < field.suggested_min)
      throw new Error("Semantic fields need unique keys, labels, integer defaults and suggested ranges.");
    keys.add(field.key);
  }
  return resolved;
}
function errorText(error: unknown) { return error instanceof Error ? error.message : String(error); }
export function loadResourcePresets(): { presets: ResourceControlPresets; override: string; error: string } {
  try {
    const raw = localStorage.getItem(RESOURCE_PRESET_KEY);
    return { presets: resolveResourcePresets(raw === null ? {} : JSON.parse(raw)), override: raw ?? "{}", error: "" };
  } catch (error) { return { presets: defaults, override: "", error: `Resource preset override could not be read; its saved bytes were preserved. ${errorText(error)}` }; }
}
export function useResourcePresets() {
  const [loaded, setLoaded] = useState(loadResourcePresets);
  useEffect(() => {
    const refresh = () => setLoaded(loadResourcePresets());
    window.addEventListener(PRESET_EVENT, refresh);
    return () => window.removeEventListener(PRESET_EVENT, refresh);
  }, []);
  const saveOverride = useCallback((raw: string) => {
    const parsed: unknown = JSON.parse(raw);
    resolveResourcePresets(parsed);
    localStorage.setItem(RESOURCE_PRESET_KEY, raw);
    window.dispatchEvent(new Event(PRESET_EVENT));
  }, []);
  return { ...loaded, saveOverride };
}

/** Persist valid chosen parameters only; invalid/corrupt stored bytes are retained. */
export function useResourceParameters<T>(key: string, fallback: T, valid: (value: unknown) => value is T) {
  const [loaded] = useState(() => {
    try {
      const raw = localStorage.getItem(key);
      if (raw === null) return { value: fallback, error: "" };
      const parsed: unknown = JSON.parse(raw);
      if (!valid(parsed)) throw new Error("Invalid saved parameters.");
      return { value: parsed, error: "" };
    } catch (error) { return { value: fallback, error: `Chosen parameters could not be restored; autosave is paused and the saved bytes were preserved. ${errorText(error)}` }; }
  });
  const [value, setValue] = useState(loaded.value);
  const [storageError, setStorageError] = useState(loaded.error);
  useEffect(() => {
    if (storageError || !valid(value)) return;
    try { localStorage.setItem(key, JSON.stringify(value)); }
    catch (error) { setStorageError(`Chosen parameters could not be saved: ${errorText(error)}`); }
  }, [key, value, storageError, valid]);
  return { value, setValue, storageError };
}
