import type { EffectiveDefault, JsonValue } from "./types";
export interface PresentationPack {
  schema: "loom.onboarding.presentation/1";
  default_locale: string;
  locales: Record<string, Record<string, string>>;
  defaults: {
    mode: "conversation" | "form";
    input: "text" | "multiline" | "json" | "number" | "boolean" | "select";
    json_indent: number;
    provider_separator: string;
    modes: { id: "conversation" | "form"; label: string }[];
    preference_modes: { id: "ask" | "candidate" | "automatic"; label: string }[];
    boolean_options: { value: boolean; label: string }[];
    rows: Record<string, number>;
    privacy_controls: { key: string; renderer: "checkbox" | "json" | "provider-list"; label: string }[];
    control_order: Record<string, string[]>;
    presentation_tokens: Record<string, string>;
  };
}
export interface PresentationAvailability { available: boolean; value?: PresentationPack; reason?: string; status?: string }
export interface Presentation { pack: PresentationPack; locale: string; defaults: PresentationPack["defaults"] }
export class PresentationError extends Error { code: string; parameters: Record<string, JsonValue>; machineOnly: boolean; constructor(code: string, parameters?: Record<string, JsonValue>, machineOnly?: boolean) }
export function formatTemplate(template: string, parameters?: Record<string, JsonValue>): string;
export function resolvePresentation(input?: unknown, locale?: string): Presentation;
export function message(presentation: Presentation, id: string, parameters?: Record<string, JsonValue>): string;
export function vocabulary(presentation: Presentation, namespace: string, id: string): string;
export function presentationStyle(presentation: Presentation): Record<string, string>;
export function controlOrder(presentation: Presentation, group: string, registered: Record<string, unknown>): string[];
export function errorPresentation(error: unknown, presentation?: Presentation): { text: string; details: string };

export function layerExplanation(presentation: Presentation, entry: EffectiveDefault): string | null;

export interface PresentationFeature { feature: string; locale: string; catalog: Record<string, string> }
export function resolvePresentationFeature(presentation: Presentation, feature: string, effective?: EffectiveDefault | null): PresentationFeature;
export function featureMessage(feature: PresentationFeature, id: string, parameters?: Record<string, JsonValue>): string;
