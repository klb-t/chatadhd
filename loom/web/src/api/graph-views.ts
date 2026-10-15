// Graph views as data (P24): perspectives (listed, rendered, side by side) and the scope resolver ("dziedziczenie").
import type { GraphEdge, GraphNode } from "./types";

export interface PerspectiveInfo {
  id: string;
  /** The data of the perspective (label {en, pl}, relations, direction, render ...). */
  entry?: { label?: { en?: string; pl?: string } | string; [key: string]: unknown };
  relations?: string[];
  edge_kinds?: string[];
  origin?: string;
}
export interface PerspectiveRender {
  perspective?: string;
  edge_kinds?: string[];
  nodes: GraphNode[];
  edges: GraphEdge[];
  unclassified?: unknown;
}
export interface PerspectivesList { perspectives: PerspectiveInfo[] }
export interface PerspectivesSideBySide { perspectives: Record<string, PerspectiveRender> }
export interface PerspectiveRequest {
  ids: string[];
  seeds?: string[];
  hops?: number;
  direction?: string;
  limit?: number;
  key?: string;
  conversation_id?: string;
}
export interface ScopeResolveRequest { key: string; conversation_id?: string; inheritance?: boolean }
export interface ScopeChainLevel {
  level: string;
  label?: { en?: string; pl?: string } | string;
  active?: boolean;
  scope_id?: string;
  role: "supplied" | "overridden" | "inactive" | "none" | string;
  candidates?: unknown[];
}
export interface ScopeResolution {
  key: string;
  defined: boolean;
  value?: unknown;
  scope?: string;
  scope_id?: string;
  source?: string;
  chain: ScopeChainLevel[];
}
