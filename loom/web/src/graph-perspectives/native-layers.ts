import type { EffectiveDefault, LayerAction, OnboardingAdapter, RequestOptions } from "../onboarding/types";
import type { NativeResolution } from "./types";

/** Consume existing normalized native rows. This module performs no merging,
 * fallback, exclusion handling or persistence: DefaultLayers owns those. */
export function nativeResolutionFromDefaults(defaults: readonly EffectiveDefault[]): NativeResolution {
  return {
    schema: "loom.graph_perspective_native_resolution/1",
    components: defaults.map(row => ({
      id: row.key, status: row.status ?? (row.excluded ? "excluded" : row.enabled ? "effective" : "disabled"),
      ...(row.value === undefined ? {} : { value: row.value }),
      origin: row.resolution?.source, reason: row.reason ?? undefined,
      resolution: row.resolution, layer: row.layer, history: row.history,
    })),
  };
}

/** Attach to B's existing profile adapter; no second settings engine. */
export class NativeLayerClient {
  constructor(private readonly adapter: Pick<OnboardingAdapter, "getSnapshot" | "dispatchLayer">) {}
  async defaults(options?: RequestOptions): Promise<EffectiveDefault[]> {
    const rows = (await this.adapter.getSnapshot(options)).defaults;
    if (!rows) throw new Error("graph_perspectives.native_defaults_unavailable");
    return rows;
  }
  async resolve(options?: RequestOptions): Promise<NativeResolution> {
    return nativeResolutionFromDefaults(await this.defaults(options));
  }
  async dispatch(action: LayerAction, options?: RequestOptions): Promise<NativeResolution> {
    if (!this.adapter.dispatchLayer) throw new Error("graph_perspectives.native_dispatch_unavailable");
    const rows = (await this.adapter.dispatchLayer(action, options)).defaults;
    if (!rows) throw new Error("graph_perspectives.native_defaults_unavailable");
    return nativeResolutionFromDefaults(rows);
  }
}

export interface NativeLayerResponse {
  schema: "loom.graph_perspective_native_resolution/1";
  resolver: string;
  layers: Record<string, unknown>;
  effectiveDefaults: Record<string, unknown>[];
}
/** Harness consumes the actual native JSON transport; never assumes missing
 * capability is enabled and retains unsupported native fields verbatim. */
export function nativeResolutionFromResponse(input: unknown): NativeResolution {
  if (!input || typeof input !== "object" || Array.isArray(input)) throw new Error("graph_perspectives.native_response_invalid");
  const response = input as Partial<NativeLayerResponse> & { error?: unknown };
  if (response.error) throw new Error(`graph_perspectives.native_error:${JSON.stringify(response.error)}`);
  if (response.schema !== "loom.graph_perspective_native_resolution/1" || !Array.isArray(response.effectiveDefaults))
    throw new Error("graph_perspectives.native_response_invalid");
  return {
    schema: response.schema,
    provenance: { resolver: response.resolver, layers: response.layers },
    components: response.effectiveDefaults.map(row => {
      if (typeof row.key !== "string" || typeof row.status !== "string") throw new Error("graph_perspectives.native_row_invalid");
      return { ...row, id: row.key, status: row.status, origin: row.source,
        reason: typeof row.explanation === "string" ? row.explanation : undefined };
    }),
  };
}
