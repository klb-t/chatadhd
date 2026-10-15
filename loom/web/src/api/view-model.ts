// The visualization engine (P28): view declarations are graph data, the kernel evaluates one around a focus into a layout-independent view
// model (loom_view_model, POST /api/view-model). These are the shapes and the readers; nothing here draws.
export interface ViewText { en?: string; pl?: string }
export interface ViewReason { code: string; level: "info" | "warning" | string; text: ViewText; params?: Record<string, unknown> }
export interface ViewElement {
  id: string;
  label: string;
  kind: string;
  focus?: boolean;
  measures: Record<string, number | string>;
  sector?: string;
  /** coordinates element id -> its values: {angle_deg, radius, sector} | {x, y} | {depth} | {depth, angle_deg, branch, parent}. */
  coords: Record<string, Record<string, unknown>>;
  /** channel -> value: hue deg, saturation / lightness / alpha 0..1, size px, blur px, badge text, position/depth 0..1. */
  channels: Record<string, number | string>;
  sources: string[];
}
export interface ViewEdge { src: string; dst: string; type: string; kind?: string; source: string }
export interface ViewGroup {
  id: string;
  kind: "sector" | "layer" | "panel" | "lane" | "collapsed" | string;
  label?: ViewText | string;
  center_deg?: number;
  width_deg?: number;
  count?: number;
  members: string[];
}
export interface ViewEncoding {
  id: string;
  channel: string;
  measure: string;
  data_type: string;
  status: "promoted" | "allowed";
  rank?: number;
  position?: boolean;
  reasons: ViewReason[];
}
export interface ViewUnsupported { element: string; kind: string; reason: ViewReason }
export interface ViewCoordinateSystem { id: string; system: string; axes: string[]; bind: Record<string, string>; supported: boolean; settings?: Record<string, unknown> }
export interface ViewModel {
  schema: "loom.view_model/1";
  declaration: { id?: string; label?: ViewText | string; origin?: string; provenance?: unknown[] };
  focus: string;
  sources: Array<{ id: string; perspective: string; weight: number; depth: number | null; role: string; ok: boolean; reached: number }>;
  elements: ViewElement[];
  edges: ViewEdge[];
  groups: ViewGroup[];
  measure_stats: Record<string, { kind: string; data_type: string; unit?: string; min?: number; max?: number; count: number; categories?: string[] }>;
  coordinate_systems: ViewCoordinateSystem[];
  encodings: ViewEncoding[];
  unsupported: ViewUnsupported[];
  budget: { max_nodes: number | null; elements: number; collapsed: number; reason?: ViewReason };
  issues: Array<{ code: string; text: ViewText }>;
}
export interface ViewDeclarationInfo { id: string; entry?: { label?: ViewText | string; meaning?: ViewText; elements?: unknown[] }; origin?: string }
export interface ViewEngineList { declarations: ViewDeclarationInfo[] }
export interface ViewModelRequest {
  declaration: string | Record<string, unknown>;
  focus: string;
  budget?: number | null;
  sources?: Record<string, { weight?: number; depth?: number | null }>;
}
