// M1-G1b: a chat sentence that asks for the graph ("pokaż katalog główny grafu", "pokaż import w grafie") is answered in the conversation with the kernel's text
// (action graph.navigate, G1a). The sentence also means "show me": once the run completed, the app opens the navigator where the run stood - the address the KERNEL
// resolved (a name such as "import" becomes area:data_corpora there; the web never maps names). This is the pure part: the target out of a stored run record.
export interface GraphTarget { focus: string; perspectives: string[] }

const record = (v: unknown): Record<string, unknown> | null => v && typeof v === "object" && !Array.isArray(v) ? v as Record<string, unknown> : null;

/** Where a completed graph.navigate run stood ("" = the root of the graph), with the perspectives it was asked for; null for any other run or a run that did not complete. */
export function graphTargetOfRun(run: unknown): GraphTarget | null {
  const r = record(run);
  const request = record(r?.request);
  if (!r || request?.action !== "graph.navigate") return null;
  if (typeof r.status === "string" && r.status !== "completed") return null;
  const steps = Array.isArray(r.steps) ? r.steps : [];
  for (const step of steps) {
    const s = record(step);
    if (s?.action !== "graph.navigate" || !Array.isArray(s.variants)) continue;
    for (const variant of s.variants) {
      const data = record(record(variant)?.data);
      const focus = record(data?.focus);
      if (data?.schema === "loom.graph_navigate/1" && typeof focus?.address === "string") {
        const asked = record(request.args)?.perspectives;
        return { focus: focus.address, perspectives: Array.isArray(asked) ? asked.filter((x): x is string => typeof x === "string") : [] };
      }
    }
  }
  return null;
}
