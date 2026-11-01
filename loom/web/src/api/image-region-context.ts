import type { LoomApi } from "./loom-api";
import type { GraphData, GraphNode } from "./types";
import { saveSourceSelection, sourceSelector } from "./text-sources";

type ObjectValue = Record<string, unknown>;
type Pair = [number, number];
type Host = Pick<LoomApi, "getNodes" | "expandGraph" | "getConfig" | "mergeConfigKey">;
export interface RegionRequest { signal?: AbortSignal; isCurrent?: () => boolean }
export interface ImageRepresentation {
  nodeId: string; sourceId: string; originalHash: string; originalNode: string;
  representationSource: string; representationHash: string; regionNodes: string[];
  status: string; reason: string; method: ObjectValue; frames: unknown[];
  evidence: string;
}
export interface ImageRegion {
  nodeId: string; regionId: string; text: string; observationId: string; pointer: string;
  frame: { id: string; origin: Pair; extent: Pair; axes: [string, string]; units: [string, string]; orientation: string };
  bounds: { origin: Pair; size: Pair }; relation: string;
}

const object = (v: unknown): ObjectValue => {
  if (v === null || typeof v !== "object" || Array.isArray(v)) throw Error("image_region_metadata_invalid");
  return v as ObjectValue;
};
const string = (v: unknown): string => {
  if (typeof v !== "string" || !v) throw Error("image_region_metadata_invalid");
  return v;
};
const hash = (v: unknown): string => {
  const value = string(v);
  if (!/^[a-f0-9]{64}$/.test(value)) throw Error("image_region_hash_invalid");
  return value;
};
const pair = (v: unknown): Pair => {
  if (!Array.isArray(v) || v.length !== 2 || v.some(n => !Number.isSafeInteger(n))) throw Error("image_region_coordinates_invalid");
  return v as Pair;
};
const equal = (a: unknown, b: unknown): boolean => {
  if (Object.is(a, b)) return true;
  if (Array.isArray(a) && Array.isArray(b)) return a.length === b.length && a.every((v, i) => equal(v, b[i]));
  if (!a || !b || typeof a !== "object" || typeof b !== "object" || Array.isArray(a) || Array.isArray(b)) return false;
  const left = a as ObjectValue, right = b as ObjectValue, keys = Object.keys(left);
  return keys.length === Object.keys(right).length && keys.every(k => Object.prototype.hasOwnProperty.call(right, k) && equal(left[k], right[k]));
};
const current = (request: RegionRequest) => {
  if (request.signal?.aborted || request.isCurrent?.() === false) throw new DOMException("image_region_request_stale", "AbortError");
};

/** UI metadata checks only. The existing native candidate verifies blob bytes and identities when building context. */
export function readImageRepresentation(node: GraphNode): ImageRepresentation {
  const m = object(node.metadata), method = object(m.method);
  const originalHash = hash(m.source_sha256), representationHash = hash(m.representation_blob);
  if (m.schema !== "loom.image_representation/1" || m.representation_node !== node.id ||
      node.id !== "n_ocr_" + representationHash || m.source_node !== "n_image_" + originalHash ||
      m.representation_source !== "src_ocr_" + representationHash || typeof m.reason !== "string" ||
      !Array.isArray(m.frames) || !Array.isArray(m.region_nodes) || m.confidence !== null ||
      m.region_nodes.some(id => typeof id !== "string" || !id) || new Set(m.region_nodes).size !== m.region_nodes.length)
    throw Error("image_region_representation_invalid");
  string(method.id);
  return { nodeId: node.id, sourceId: string(m.source_id), originalHash, originalNode: string(m.source_node),
    representationSource: string(m.representation_source), representationHash, regionNodes: m.region_nodes as string[],
    status: string(m.status), reason: m.reason, method, frames: m.frames, evidence: string(m.evidence_class) };
}

/** Bind one existing region to the selected representation and its actual graph relation; never use representation.content. */
export function readImageRegion(graph: GraphData, representation: ImageRepresentation, nodeId: string): ImageRegion {
  const index = representation.regionNodes.indexOf(nodeId);
  const nodes = graph.nodes.filter(n => n.id === nodeId), reps = graph.nodes.filter(n => n.id === representation.nodeId);
  if (index < 0 || nodes.length !== 1 || reps.length !== 1 || representation.status !== "recognized" || representation.evidence !== "inferred" ||
      !equal(readImageRepresentation(reps[0]), representation)) throw Error("image_region_binding_invalid");
  const node = nodes[0], metadata = object(node.metadata), unit = object(metadata.unit), observation = object(metadata.observation);
  const attrs = object(unit.attrs), obsAttrs = object(observation.attrs), loc = object(unit.locator), obsLoc = object(observation.locator);
  const region = object(attrs.spatial_region), method = object(attrs.method), pointer = "/regions/" + index;
  if (node.id !== "n_" + string(unit.id) || node.kind !== unit.kind || observation.unit !== unit.id || observation.kind !== "field" ||
      unit.source !== representation.representationSource || loc.source !== unit.source || obsLoc.source !== unit.source ||
      loc.json_pointer !== pointer || obsLoc.json_pointer !== pointer + "/text" || observation.ordinal !== index ||
      attrs.original_source !== representation.sourceId || attrs.original_sha256 !== representation.originalHash ||
      !equal(attrs.frames, representation.frames) || !equal(method, representation.method) || !equal(method, obsAttrs.method) ||
      !equal(obsAttrs.original, attrs) || obsAttrs.evidence_class !== "inferred" || obsAttrs.confidence !== null ||
      node.content !== region.text || observation.text !== region.text) throw Error("image_region_metadata_mismatch");
  for (const locator of [loc, obsLoc]) {
    if ((locator.member !== undefined && locator.member !== "") ||
        ["byte_start", "byte_len", "time_start", "time_end", "line"].some(k => locator[k] !== undefined && locator[k] !== null))
      throw Error("image_region_locator_invalid");
  }
  const frames = representation.frames.map(object).filter(f => f.id === region.frame);
  if (frames.length !== 1) throw Error("image_region_frame_invalid");
  const frame = frames[0], origin = pair(region.origin), size = pair(region.size), extent = pair(frame.extent);
  if (!equal(frame.axes, ["x", "y"]) || !equal(frame.units, ["px", "px"]) || !equal(pair(frame.origin), [0, 0]) || frame.orientation !== "top_left" ||
      origin.some((start, axis) => start < 0 || size[axis] <= 0 || extent[axis] <= 0 || size[axis] > extent[axis] || start > extent[axis] - size[axis]))
    throw Error("image_region_bounds_invalid");
  const links = graph.edges.filter(e => e.src === nodeId && e.dst === representation.nodeId);
  if (!links.length) throw Error("image_region_relation_missing");
  return { nodeId, regionId: string(region.id), text: string(region.text), observationId: string(observation.id), pointer: pointer + "/text",
    frame: { id: string(frame.id), origin: [0, 0], extent, axes: ["x", "y"], units: ["px", "px"], orientation: "top_left" },
    bounds: { origin, size }, relation: string(links[0].link_type ?? links[0].type) };
}

export async function loadImageRepresentations(host: Host, filter: { kind?: string; limit: number }, request: RegionRequest = {}) {
  current(request);
  const [nodes, config] = await Promise.all([host.getNodes(filter), host.getConfig()]);
  current(request);
  const selector = sourceSelector(config);
  const representations = nodes.filter(n => n.metadata?.schema === "loom.image_representation/1").map(readImageRepresentation);
  return { representations, config, selector, recordCount: nodes.length };
}

export async function loadImageRegion(host: Host, representation: ImageRepresentation, nodeId: string, request: RegionRequest = {}) {
  current(request);
  const graph = await host.expandGraph([nodeId], 1);
  current(request);
  return readImageRegion(graph, representation, nodeId);
}

/** Reuse the account selector's fresh read / merge / readback. There is no separate image selector. */
export async function saveImageRegionSelection(host: Host, region: ImageRegion, include: boolean, hops: number, request: RegionRequest = {}) {
  current(request);
  const config = await saveSourceSelection(host, region.nodeId, include, hops,
    () => !request.signal?.aborted && request.isCurrent?.() !== false);
  current(request);
  return config;
}
