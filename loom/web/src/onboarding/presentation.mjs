import conversationViewPack from "./generated/conversation-view.json" with { type: "json" };
import embedded from "./generated/ui.json" with { type: "json" };

function canonical(value) {
  if (Array.isArray(value)) return value.map(canonical);
  if (value && typeof value === "object") return Object.fromEntries(Object.keys(value).sort().map(key => [key, canonical(value[key])]));
  return value;
}
/** Parser failures carry only mechanism IDs, never another catalog lookup. */
class TemplateFailure extends Error {
  constructor(code, parameters = {}) { super(code); this.code = code; this.parameters = parameters; }
}
function renderTemplate(template, parameters = {}) {
  if (!record(parameters) || ![Object.prototype, null].includes(Object.getPrototypeOf(parameters))) throw new TemplateFailure("error.template_parameters");
  if (typeof template !== "string") throw new TemplateFailure("error.template_syntax");
  let cursor = 0;
  let result = "";
  while (true) {
    const start = template.indexOf("{{", cursor);
    if (start < 0) return result + template.slice(cursor);
    const end = template.indexOf("}}", start + 2);
    if (end < 0 || end === start + 2) throw new TemplateFailure("error.template_syntax");
    const key = template.slice(start + 2, end);
    if (key.includes("{{")) throw new TemplateFailure("error.template_syntax");
    if (!Object.hasOwn(parameters, key)) throw new TemplateFailure("error.template_parameter", { key });
    const value = parameters[key];
    result += template.slice(cursor, start) + (typeof value === "string" ? value : JSON.stringify(canonical(value)));
    cursor = end + 2;
  }
}
/** Inert substitution only; a replacement is never reinterpreted as a template. */
export function formatTemplate(template, parameters = {}) {
  try { return renderTemplate(template, parameters); }
  catch (failure) {
    if (failure instanceof TemplateFailure) throw new PresentationError(failure.code, failure.parameters);
    throw failure;
  }
}
function diagnosticText(code, parameters, machineOnly) {
  if (machineOnly) return { text: code, machineOnly: true };
  const catalog = embedded?.locales?.[embedded?.default_locale];
  if (typeof catalog?.[code] !== "string") return { text: code, machineOnly: true };
  const renderedParameters = parameters?.expectedType ? { ...parameters, expected: catalog?.[`type.${parameters.expectedType}`] ?? parameters.expectedType } : parameters;
  try { return { text: renderTemplate(catalog[code], renderedParameters), machineOnly: false }; }
  catch { return { text: code, machineOnly: true }; }
}
export class PresentationError extends Error {
  constructor(code, parameters = {}, machineOnly = false) {
    const diagnostic = diagnosticText(code, parameters, machineOnly);
    super(diagnostic.text);
    this.name = "PresentationError";
    this.code = code;
    this.parameters = parameters;
    this.machineOnly = diagnostic.machineOnly;
  }
}
function record(value) { return Boolean(value && typeof value === "object" && !Array.isArray(value)); }
function invalid(path) { throw new PresentationError("error.presentation_invalid", { path }); }
function validateBootstrap() {
  const defaults = embedded?.defaults;
  const messages = embedded?.locales?.[embedded?.default_locale];
  if (!record(embedded) || embedded.schema !== "loom.onboarding.presentation/1" ||
      typeof embedded.default_locale !== "string" || !embedded.default_locale ||
      !record(embedded.locales) || !record(messages) || !record(defaults) ||
      !record(defaults.rows) || !record(defaults.control_order) ||
      Object.values(defaults.control_order).some(ids => !Array.isArray(ids) || ids.some(id => typeof id !== "string")))
    throw new PresentationError("error.presentation_invalid", { path: "bootstrap" }, true);
}
function validate(pack) {
  validateBootstrap();
  if (!record(pack) || pack.schema !== "loom.onboarding.presentation/1" || typeof pack.default_locale !== "string" || !record(pack.locales) || !record(pack.defaults)) invalid("schema");
  const requiredMessages = Object.keys(embedded.locales[embedded.default_locale]);
  if (!Object.hasOwn(pack.locales, pack.default_locale)) invalid("default_locale");
  for (const [locale, messages] of Object.entries(pack.locales)) {
    if (!record(messages) || requiredMessages.some(key => typeof messages[key] !== "string")) invalid(`locales/${locale}`);
  }
  const defaults = pack.defaults;
  if (Object.keys(embedded.defaults).some(key => !Object.hasOwn(defaults, key))) invalid("defaults");
  if (!["conversation", "form"].includes(defaults.mode) || !["text", "multiline", "json", "number", "boolean", "select"].includes(defaults.input)) invalid("defaults/mode,input");
  if (!Number.isInteger(defaults.json_indent) || defaults.json_indent < 0 || typeof defaults.provider_separator !== "string" || !defaults.provider_separator) invalid("defaults/json_indent,provider_separator");
  if (!record(defaults.rows) || Object.keys(embedded.defaults.rows).some(key => !Number.isInteger(defaults.rows[key]) || defaults.rows[key] <= 0)) invalid("defaults/rows");
  if (!record(defaults.presentation_tokens) || Object.entries(defaults.presentation_tokens).some(([key, value]) => !/^--[a-zA-Z_][\w-]*$/.test(key) || typeof value !== "string")) invalid("defaults/presentation_tokens");
  for (const group of ["modes", "preference_modes"]) {
    if (!Array.isArray(defaults[group]) || defaults[group].some(item => !record(item) || typeof item.id !== "string" || typeof item.label !== "string")) invalid(`defaults/${group}`);
  }
  if (defaults.modes.some(item => !["conversation", "form"].includes(item.id)) || defaults.preference_modes.some(item => !["ask", "candidate", "automatic"].includes(item.id))) invalid("defaults/modes");
  if (!Array.isArray(defaults.boolean_options) || defaults.boolean_options.some(item => !record(item) || typeof item.value !== "boolean" || typeof item.label !== "string")) invalid("defaults/boolean_options");
  if (!Array.isArray(defaults.privacy_controls) || defaults.privacy_controls.some(item => !record(item) || typeof item.key !== "string" || typeof item.label !== "string" || !["checkbox", "json", "provider-list"].includes(item.renderer))) invalid("defaults/privacy_controls");
  if (new Set(defaults.privacy_controls.map(item => item.key)).size !== defaults.privacy_controls.length) invalid("defaults/privacy_controls/keys");
  if (!record(defaults.control_order) || Object.keys(embedded.defaults.control_order).some(group => !Array.isArray(defaults.control_order[group]) || defaults.control_order[group].some(id => typeof id !== "string" || !embedded.defaults.control_order[group].includes(id)) || new Set(defaults.control_order[group]).size !== defaults.control_order[group].length)) invalid("defaults/control_order");
  for (const locale of Object.values(pack.locales)) {
    const refs = [...defaults.modes, ...defaults.preference_modes, ...defaults.boolean_options, ...defaults.privacy_controls];
    if (refs.some(item => typeof locale[item.label] !== "string")) invalid("defaults/message_refs");
  }
}
export function resolvePresentation(input, locale) {
  let pack = input;
  if (input === undefined) pack = embedded;
  else if (record(input) && Object.hasOwn(input, "available")) {
    if (!input.available || ["disabled", "excluded", "proposal", "missing"].includes(input.status)) throw new PresentationError("error.presentation", { reason: input.reason ?? input.status ?? "unavailable" }, true);
    pack = input.value;
  }
  try { validate(pack); }
  catch (failure) {
    if (pack === embedded && failure instanceof PresentationError) throw new PresentationError(failure.code, failure.parameters, true);
    throw failure;
  }
  const chosen = locale === undefined || locale === "" ? pack.default_locale : locale;
  if (!Object.hasOwn(pack.locales, chosen)) throw new PresentationError("error.presentation_locale", { locale: chosen });
  return { pack, locale: chosen, defaults: pack.defaults };
}
export function message(presentation, id, parameters = {}) {
  const template = presentation.pack.locales[presentation.locale][id];
  if (typeof template !== "string") throw new PresentationError("error.presentation_message", { id });
  return formatTemplate(template, parameters);
}
export function vocabulary(presentation, namespace, id) {
  const key = `${namespace}.${id}`;
  return Object.hasOwn(presentation.pack.locales[presentation.locale], key) ? message(presentation, key) : message(presentation, "value.raw_identifier", { id });
}
export function presentationStyle(presentation) { return presentation.defaults.presentation_tokens; }
export function controlOrder(presentation, group, registered) {
  const ids = presentation.defaults.control_order[group];
  if (!ids) throw new PresentationError("error.presentation_control", { group });
  for (const id of ids) if (!Object.hasOwn(registered, id)) throw new PresentationError("error.presentation_control", { group: `${group}/${id}` });
  return ids;
}
/** Ordinary UI uses translated diagnostics; original provider/parser details are
 * separately inspectable and do not become a hidden English message fallback. */
export function errorPresentation(error, presentation) {
  const details = error instanceof Error ? error.message : String(error);
  if (error instanceof PresentationError && error.machineOnly) return { text: error.code, details: error.code };
  presentation ??= resolvePresentation();
  if (error instanceof PresentationError) {
    const parameters = error.parameters.expectedType ? { ...error.parameters, expected: message(presentation, `type.${error.parameters.expectedType}`) } : error.parameters;
    return { text: message(presentation, error.code, parameters), details };
  }
  const base = embedded.locales[embedded.default_locale];
  const current = presentation.pack.locales[presentation.locale];
  const id = Object.keys(base).find(key => key.startsWith("error.") && (base[key] === details || current[key] === details));
  return { text: message(presentation, id ?? "error.failure"), details };
}

export function layerExplanation(presentation, entry) {
  if (entry.reason === null) return null;
  const id = entry.status === "excluded" ? "layer.excluded" : entry.status === "disabled" ? "layer.disabled" :
    entry.status === "proposal" ? "layer.proposal" : entry.status === "missing" ? "layer.missing" :
    entry.layer === "user" ? "layer.user" : entry.layer === "builtin" ? "layer.builtin" : null;
  const parameters = { ...(entry.resolution ?? Object.fromEntries(Object.entries({ key: entry.key, id: entry.id, area: entry.area, status: entry.status, layer: entry.layer, value: entry.value }).filter(([, value]) => value !== undefined))), explanation: null };
  return id ? message(presentation, id, parameters) : entry.reason;
}

/** Optional feature catalogs consume native effective values, never re-resolve layers.
 * undefined is reserved for bootstrap without a selected user. null means missing. */
export function resolvePresentationFeature(presentation, feature, effective) {
  let value;
  if (effective === undefined) {
    value = conversationViewPack.entries.find(entry => entry.key === `presentation.${feature}`)?.value;
  } else {
    if (!record(effective) || effective.status !== "effective" || effective.enabled !== true || effective.excluded !== false)
      throw new PresentationError("error.presentation", { reason: effective?.status ?? "missing" }, true);
    value = effective.value;
  }
  if (!record(value) || value.schema !== "loom.presentation_feature/1" || value.feature !== feature || !record(value.locales) ||
      !record(value.locales[presentation.locale])) throw new PresentationError("error.presentation", { reason: "feature_catalog_unavailable" }, true);
  const declared = conversationViewPack.entries.find(entry => entry.key === `presentation.${feature}`)?.value;
  if (!declared || Object.keys(declared.locales[Object.keys(declared.locales)[0]]).some(key =>
      typeof value.locales[presentation.locale][key] !== "string"))
    throw new PresentationError("error.presentation", { reason: "feature_catalog_incomplete" }, true);
  return { feature, locale: presentation.locale, catalog: value.locales[presentation.locale] };
}
export function featureMessage(feature, id, parameters = {}) {
  const template = feature.catalog[id];
  if (typeof template !== "string") throw new PresentationError("error.presentation_message", { id });
  return formatTemplate(template, parameters);
}
