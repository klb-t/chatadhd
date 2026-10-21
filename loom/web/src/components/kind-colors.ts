// The colour of a graph node by its kind, shared by the force-directed graph (GraphView.tsx), the perspectives panel and the
// navigator's map (M1-W0 P4), so one kind looks the same on every map. A kind not listed gets the last colour (a preset).
export const KIND_COLORS: Record<string, string> = {
  entity: "#4d8cf2",
  topic: "#f2a94d",
  concept: "#7ed957",
  code_ref: "#d957c8",
  file: "#57d9c8",
  person: "#f25d5d",
  org: "#c8d957",
  message: "#8c8c99",
};

export function colorFor(kind: string): string {
  return KIND_COLORS[kind] ?? "#9d9dff";
}
