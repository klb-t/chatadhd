import type { SourceOccurrence, SourceParts, TextSource, TextSourceRequest } from "./text-sources";
export function textSources(rows: unknown[]): TextSource[];
export function readSourceParts(value: unknown, source: TextSource): SourceParts;
export function readSourceOccurrence(value: unknown, source: TextSource, nodeId: string): SourceOccurrence;
export function sourceSelector(config: Record<string, unknown>): { enabled: boolean; ids: string[]; hops: number | null };
export function sourceSelectionPatch(config: Record<string, unknown>, nodeId: string, include: boolean, hops: number): Record<string, unknown>;
export function materializationRequest(source: TextSource, language: string): TextSourceRequest;
export function saveSourceSelection(host: {
  getConfig(): Promise<Record<string, unknown>>;
  mergeConfigKey?(key: string, patch: unknown): Promise<Record<string, unknown>>;
}, nodeId: string, include: boolean, hops: number): Promise<Record<string, unknown>>;
