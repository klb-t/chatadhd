/** Isolated host. Imports the production module and actual native transport. */
import { createRoot } from "react-dom/client";
import type { ComponentType } from "react";
import * as workbench from "../components/KnowledgeWorkbench";
import PerspectiveView, { type PerspectiveCatalog } from "./PerspectiveView";
import { createKnowledgeGraphBridge, type KnowledgeGraphRendererProps } from "./renderer-bridge";
import { createPacketAdapter } from "./adapters";
import { nativeResolutionFromResponse, type NativeLayerResponse } from "./native-layers";
import { createNativeResolutionAdapter } from "./native-projection";
import { selectPerspective } from "./select";
import { preparePerspectiveAnalysis, type AnalysisExportOptions } from "./workflow";
import type { ObjectRef, Perspective, SelectionResult, SourceDescriptor } from "./types";

const boot = await fetch("/api/graph-perspectives/bootstrap").then(response => {
  if (!response.ok) throw new Error(`graph_perspectives.bootstrap:${response.status}`);
  return response.json();
});
const catalog = boot.catalog as PerspectiveCatalog & { sourceDescriptor: SourceDescriptor; nativeSourceDescriptor: SourceDescriptor };
const preset = catalog.presets.find(item => item.id === catalog.controls.defaultPreset) ?? catalog.presets[0];
const initial: Perspective = {
  schema: "loom.graph_perspective/1", id: "graph-perspectives-demo", label: catalog.ui.title,
  focus: preset.focus ?? null,
  components: Object.fromEntries(preset.actions.filter(action => action.op === "override").map(action => [action.key, "value" in action ? action.value : null])),
  layerActions: preset.actions.filter(action => action.op !== "override"),
  analysis: { selected: [], linkedToPerspective: false },
};
const packet = boot.packet;
const adapter = createPacketAdapter(packet, catalog.sourceDescriptor);
let latestNativeResponse: NativeLayerResponse | null = null;
const nativeAdapter = createNativeResolutionAdapter(() => latestNativeResponse, catalog.nativeSourceDescriptor);
const adapters = [adapter, nativeAdapter];
const permission = { id: "demo-explicit-local-read", canRead: (ref: ObjectRef) => adapters.some(item => item.descriptor.id === ref.source) };

const diagnostics: {
  selection: SelectionResult | null; perspective: Perspective; packetBefore: string; packetAfter(): string;
  adapterMetrics: typeof adapter.metrics; renderMs: number; layoutMs: number; canonicalPacketHash: string; timings: { selectionMs: number; layoutMs: number; renderMs: number };
  reselect(): Promise<SelectionResult>;
} = {
  selection: null, perspective: initial, packetBefore: JSON.stringify(packet), packetAfter: () => JSON.stringify(packet),
  adapterMetrics: adapter.metrics, renderMs: 0, layoutMs: 0, canonicalPacketHash: Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(JSON.stringify(packet))))).map(byte => byte.toString(16).padStart(2, "0")).join(""), timings: { selectionMs: 0, layoutMs: 0, renderMs: 0 },
  reselect: async () => { if (!diagnostics.selection) throw new Error("graph_perspectives.not_ready"); return selectPerspective(diagnostics.selection.plan, adapters, permission); },
};
const Renderer = createKnowledgeGraphBridge((workbench as unknown as { KnowledgeGraph: ComponentType<KnowledgeGraphRendererProps> }).KnowledgeGraph, { onLayoutMeasured: duration => { diagnostics.layoutMs = duration; diagnostics.timings.layoutMs = duration; } });
Object.assign(window, { __graphPerspectives: diagnostics });
async function resolve(perspective: Perspective, signal?: AbortSignal) {
  const overrides = Object.entries(perspective.components).map(([key, value]) => ({ op: "override", key, value }));
  const response = await fetch("/api/native-layers", { method: "POST", headers: { "Content-Type": "application/json" }, signal,
    body: JSON.stringify({ pack: boot.pack, state: {}, actions: [...overrides, ...(Array.isArray(perspective.layerActions) ? perspective.layerActions : [])], keys: catalog.capabilities.map(item => item.id) }),
  });
  if (!response.ok) throw new Error(`graph_perspectives.native_transport:${response.status}`);
  const raw: unknown = await response.json();
  const result = nativeResolutionFromResponse(raw);
  latestNativeResponse = raw as NativeLayerResponse;
  return result;
}
document.title = catalog.ui.title;
createRoot(document.getElementById("root")!).render(<PerspectiveView initial={initial} catalog={catalog} adapters={adapters} permission={permission} resolve={resolve} Renderer={Renderer}
  prepareAnalysis={(perspective, selection, mode) => preparePerspectiveAnalysis(perspective, { ...(catalog.analysisExport as unknown as AnalysisExportOptions), selectionMode: mode, permission, resolveAnchor: () => undefined, queryPlan: selection.plan })}
  onResult={(selection, perspective) => { diagnostics.selection = selection; diagnostics.perspective = perspective; diagnostics.timings.selectionMs = selection.metrics.selectionMs; }}
  onTiming={duration => { diagnostics.renderMs = duration; diagnostics.timings.renderMs = duration; }} />);
