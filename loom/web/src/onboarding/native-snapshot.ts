/** Pure projection/envelopes for W10. This file calls no transport and persists
 * nothing; its input is the actual native OnboardingStore JSON response. */
import type { EffectiveDefault, HistoryRecord, JsonValue, LayerAction, ModelReply, OnboardingAction, OnboardingScenario, OnboardingSnapshot, PrivacyRule, ProfileCandidate, ProfileField, ScenarioField } from "./types";

type JsonObject = { [key: string]: JsonValue };
function object(value: unknown, label: string): JsonObject {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error(`Native onboarding ${label} must be an object.`);
  return value as JsonObject;
}
function array(value: unknown, label: string): JsonValue[] {
  if (!Array.isArray(value)) throw new Error(`Native onboarding ${label} must be an array.`);
  return value as JsonValue[];
}
function text(value: unknown, label: string): string {
  if (typeof value !== "string") throw new Error(`Native onboarding ${label} must be a string.`);
  return value;
}

export function normalizeNativeSnapshot(input: JsonValue): OnboardingSnapshot {
  const raw = object(input, "snapshot");
  const profile = object(raw.profile ?? raw, "profile");
  const scenario = object(raw.scenario_definition ?? raw.scenario, "scenario");
  const descriptors = array(scenario.fields, "scenario.fields").map((item) => object(item, "field descriptor"));
  const fieldMap = new Map(descriptors.map((item) => [text(item.id, "field.id"), item]));
  const sections = array(scenario.sections, "scenario.sections").map((item) => {
    const section = object(item, "section");
    const id = text(section.id, "section.id");
    const questions = array(section.questions, "section.questions").map((question) => object(question, "question"));
    const questionMap = new Map(questions.map((question) => [text(question.field, "question.field"), question]));
    const orderedIds = [...new Set([...questions.map((question) => text(question.field, "question.field")),
      ...descriptors.filter((descriptor) => descriptor.section === id).map((descriptor) => text(descriptor.id, "field.id"))])];
    const fields = orderedIds.map((fieldId): ScenarioField => {
      const descriptor = fieldMap.get(fieldId);
      if (!descriptor) throw new Error(`Native scenario question names missing field ${fieldId}.`);
      const question = questionMap.get(fieldId);
      return {
        id: fieldId, label: typeof descriptor.label === "string" ? descriptor.label : fieldId,
        question: question ? text(question.text, "question.text") : undefined,
        category: text(descriptor.category, "field.category"),
        input: typeof descriptor.input === "string" ? descriptor.input as ScenarioField["input"] : undefined,
        options: descriptor.options as unknown as ScenarioField["options"],
      };
    });
    return { id, title: text(section.title, "section.title"), description: typeof section.description === "string" ? section.description : undefined, fields };
  });
  const session = object(profile.session, "session");
  const sessionSections = object(session.sections, "session.sections");
  const activeId = text(session.section, "session.section");
  const active = sessionSections[activeId] ? object(sessionSections[activeId], "active section") : undefined;
  const privacy = object(profile.privacy, "privacy");
  const layers = raw.layers ? object(raw.layers, "layers") : undefined;
  const areas = layers?.areas ? object(layers.areas, "layer areas") : undefined;
  const layerHistory = layers?.history ? array(layers.history, "layer history") : [];
  const defaults = raw.effectiveDefaults ? array(raw.effectiveDefaults, "effectiveDefaults").map((item): EffectiveDefault => {
    const resolved = object(item, "default resolution");
    const key = text(resolved.key, "default key");
    const area = typeof resolved.area === "string" ? resolved.area : "";
    const areaConfig = areas?.[area] ? object(areas[area], "area config") : undefined;
    const entity = resolved.entity ? object(resolved.entity, "default entity") : undefined;
    return {
      id: typeof resolved.id === "string" ? resolved.id : key, key, area,
      label: typeof entity?.label === "string" ? entity.label : key,
      value: resolved.value, layer: text(resolved.layer, "default layer"), reason: text(resolved.explanation, "default explanation"),
      enabled: resolved.status === "effective", excluded: resolved.status === "excluded",
      status: text(resolved.status, "default status"), area_mode: areaConfig?.new_defaults_mode as EffectiveDefault["area_mode"],
      history: layerHistory.filter((event) => object(event, "layer history event").key === key || object(event, "layer history event").area === area) as HistoryRecord[],
    };
  }) : undefined;
  const uiScenario: OnboardingScenario = {
    id: text(scenario.id, "scenario.id"), title: typeof scenario.title === "string" ? scenario.title : text(scenario.id, "scenario.id"),
    method_ref: typeof scenario.method_ref === "string" ? scenario.method_ref : JSON.stringify(scenario.method_ref),
    sections, source: scenario,
  };
  const candidates = Object.fromEntries(Object.entries(object(profile.candidates, "candidates")).map(([id, value]) => {
    const candidate = object(value, "candidate");
    const descriptor = fieldMap.get(text(candidate.field, "candidate.field"));
    return [id, { ...candidate, section: candidate.section ?? descriptor?.section } as unknown as ProfileCandidate];
  }));
  return {
    scenario: uiScenario, fields: object(profile.fields, "fields") as unknown as Record<string, ProfileField>,
    session: { status: text(session.status, "session.status"), section: activeId,
      sections: Object.fromEntries(Object.entries(sessionSections).map(([id, value]) => [id, text(object(value, "section state").status, "section status")])),
      summary: typeof active?.summary === "string" ? active.summary : undefined },
    candidates, privacy: array(privacy.rules, "privacy.rules") as unknown as PrivacyRule[],
    history: array(profile.history, "history") as HistoryRecord[], settings: object(profile.settings, "settings") as unknown as OnboardingSnapshot["settings"],
    defaults, latest_reply: session.latest_reply as unknown as ModelReply | undefined,
  };
}

/** Reviews name their candidate separately from the unique action event id.
 * W10 applies its store CAS revision separately; this function never invents it. */
export function nativeDispatchAction(action: OnboardingAction | LayerAction, identity: { id: string; time: string }): JsonValue {
  if (action.op === "review") {
    const { id: candidate, ...rest } = action;
    return { ...rest, candidate, ...identity, source_refs: [], target: "profile" } as JsonValue;
  }
  const target = "key" in action || action.op === "set_area_mode" ? "layers" : "profile";
  return { ...identity, source_refs: [], ...action, target } as JsonValue;
}
