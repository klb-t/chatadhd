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
  /** G3: a channel set by more than one encoding keeps every contribution (encoding id, its value, how it combines); one-encoding channels are absent. */
  channel_parts?: Record<string, ViewChannelPart[]>;
  sources: string[];
}
export interface ViewChannelPart { encoding: string; value: number | string; combine?: string }
/** A choice of an advisory (preset "keep" / "dont_ask"; an open list in data). */
export interface ViewAdvisoryChoice { id: string; label?: ViewText }
/** A non-blocking advisory (R58, loom/advisory.h, shape loom.advisory/1): what looks of little use and why, the question and the remembered answer. */
export interface ViewAdvisory {
  schema?: "loom.advisory/1";
  code: string;
  level: string;
  text: ViewText;
  params?: Record<string, unknown>;
  evidence?: Record<string, unknown>;
  subject?: unknown;
  ask?: {
    key?: string;
    question?: ViewText;
    fingerprint?: string;
    choices?: ViewAdvisoryChoice[];
    /** false = the person said "don't ask" for this configuration (acknowledged) or the policy never asks for this code (muted). */
    asking?: boolean;
    state?: "ask" | "acknowledged" | "muted" | string;
    policy?: { ask?: boolean; remember_days?: number | null };
    ack?: { code: string; fingerprint: string; choice: string; ack_at: string; context?: Record<string, unknown> };
    ack_expired?: boolean;
    /** the last answer was a transient one ("keep" this time): recorded, and the advisory asks again. */
    answered?: boolean;
  };
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
  combine?: string;
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
  /** G3: advisories about the configuration or a degenerate result; never blocking (absent in models built by a client, e.g. the navigator map). */
  advisories?: ViewAdvisory[];
}
export interface ViewDeclarationInfo { id: string; entry?: { label?: ViewText | string; meaning?: ViewText; elements?: unknown[] }; origin?: string }
/** A renderer of the view model (P28 step 3), data in the effective view `view_renderers`: which engine draws it, the modes (screen / WebXR session) and the settings. */
export interface ViewRendererMode { kind: "screen" | "xr" | string; label?: ViewText; meaning?: ViewText; session?: string; features?: { required?: string[]; optional?: string[] }; transparent_background?: boolean }
export interface ViewRendererInfo {
  id: string;
  origin?: string;
  entry?: { label?: ViewText | string; meaning?: ViewText; engine?: string; implemented?: boolean; default_mode?: string; modes?: Record<string, ViewRendererMode> } & Record<string, unknown>;
}
export interface ViewEngineList { declarations: ViewDeclarationInfo[]; renderers: ViewRendererInfo[] }
export interface ViewModelRequest {
  declaration: string | Record<string, unknown>;
  focus: string;
  budget?: number | null;
  sources?: Record<string, { weight?: number; depth?: number | null }>;
}

// Interaction as data (P28 step 2): the effective input bindings of a device (loom_view_interaction, GET|POST /api/view-interaction). The kernel resolves
// them (ROM presets + the user's overlays, with provenance; conflicts warned, never blocked; unknown things unsupported with the reason); the panel interprets.
export interface InteractionBinding {
  id: string;
  label: ViewText | string;
  device: string;
  /** key | button | gesture | phrase | gaze | lock + state, modifiers (held), locks (states that must be on). */
  input: Record<string, unknown>;
  action: { kind: string } & Record<string, unknown>;
  key: string;
  origin: string;
  provenance: unknown[];
  status: "active" | "recorded";
  shadowed?: boolean;
  shadowed_by?: string;
  reasons: ViewReason[];
  combine?: string;
}
export interface InteractionConflict { key: string; input: Record<string, unknown>; ids: string[]; winner: string; message: ViewText }
export interface InteractionActionInfo {
  id: string;
  label: ViewText;
  implemented: boolean;
  pairs?: Record<string, { label: ViewText; axes: Record<string, string> }>;
  targets?: Record<string, ViewText>;
}
export interface InteractionBindings {
  schema: "loom.view_interaction/1";
  device: string;
  devices: Array<{ id: string; label: ViewText; implemented: boolean; inputs: string[] }>;
  actions: InteractionActionInfo[];
  bindings: InteractionBinding[];
  conflicts: InteractionConflict[];
  disabled: string[];
  unsupported: Array<{ binding: string; device: string; reason: ViewReason }>;
  issues: Array<{ code: string; text: ViewText }>;
  /** G3: advisories about the configuration or a degenerate result; never blocking (absent in models built by a client, e.g. the navigator map). */
  advisories?: ViewAdvisory[];
}
