import type { NavigationEntry, NavigationState, ObjectRef } from "./types";

/** Snapshot/representation address, not semantic identity. */
export function referenceKey(ref: ObjectRef): string {
  return JSON.stringify([ref.source, ref.selector, ref.representation ?? null, ref.snapshot ?? null]);
}
export const refKey = referenceKey;
/** canonicalId is asserted by an adapter; this mechanism never infers equivalence. */
export function objectKey(ref: ObjectRef): string {
  return ref.canonicalId === undefined ? JSON.stringify([ref.source, ref.selector]) : JSON.stringify([ref.source, ref.canonicalId]);
}
export function sameObject(a: ObjectRef, b: ObjectRef): boolean { return objectKey(a) === objectKey(b); }
export function createNavigation(ref: ObjectRef, structure: string, snapshot = ref.snapshot): NavigationState {
  return { current: { ref: structuredClone(ref), structure, ...(snapshot === undefined ? {} : { snapshot }) }, back: [], forward: [] };
}
export function navigate(state: NavigationState, entry: NavigationEntry): NavigationState {
  if (referenceKey(state.current.ref) === referenceKey(entry.ref) && state.current.structure === entry.structure && state.current.snapshot === entry.snapshot) return state;
  return { current: structuredClone(entry), back: [...state.back, structuredClone(state.current)], forward: [] };
}
export function goBack(state: NavigationState): NavigationState {
  if (!state.back.length) return state;
  return { current: state.back[state.back.length - 1], back: state.back.slice(0, -1), forward: [state.current, ...state.forward] };
}
export function goForward(state: NavigationState): NavigationState {
  if (!state.forward.length) return state;
  return { current: state.forward[0], back: [...state.back, state.current], forward: state.forward.slice(1) };
}
export function changeNavigationStructure(state: NavigationState, structure: string): NavigationState {
  return navigate(state, { ...state.current, structure });
}
