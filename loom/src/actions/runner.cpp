// The interpreter bound to a Runtime: real executors (use_model, invoke ports, settings), element
// resolution, durable runs, cancel tokens. Data: runtime profile action_run.
#include "loom/action_runner.h"
#include "loom/action_overlay.h"

#include <algorithm>

#include "loom/action_element.h"
#include "loom/action_estimate.h"
#include "loom/action_result.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/providers.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/semantic_worker.h"
#include "loom/settings_fields.h"
#include "loom/storage_command.h"
#include "loom/usage_policy.h"
#include "loom/util/ids.h"
#include "loom/util/utf8.h"
#include "loom/media_providers.h"
#include "loom/use_model.h"

namespace loom::actions {

namespace {

llm::Part json_part(Json value) {
  llm::Part p;
  p.kind = "json";
  p.value = std::move(value);
  return p;
}

StepOutcome failed(Error e) {
  StepOutcome o;
  o.state = StepOutcome::State::Failed;
  o.error = std::move(e);
  return o;
}

// ── use_model ───────────────────────────────────────────────────────

Json usage_pending_of(const llm::VariantResult& r) {
  const Json& admitted = r.hold["decision"];
  Json resources = Json::object();
  if (const Json* res = json::find(admitted, "resources")) resources = *res;
  return Json{{"schema", "loom.action_usage_pending/1"}, {"status", json::get_string(r.hold, "status")},
              {"operation_id", admitted.value("operation_id", Json(nullptr))},
              {"receipt_id", admitted.value("receipt_id", Json(nullptr))},
              {"baseline_key", admitted.value("baseline_key", Json(nullptr))},
              {"projected", resources}, {"decisions", r.hold.value("decisions", Json(nullptr))}, {"sent", false}};
}

StepOutcome run_use_model(Runtime& rt, const Json& settings, const StepCall& c) {
  const Json um = settings.value("use_model", Json::object());
  llm::ModelRequest req;
  const Json& with = c.with;
  if (const Json* list = json::find(with, "models"); list && list->is_array()) {
    for (const auto& m : *list) {
      llm::ModelSpec spec;
      if (m.is_string()) spec.model = m.get<std::string>();
      else if (m.is_object()) {
        spec.model = json::get_string(m, "model");
        spec.provider = json::get_string(m, "provider");
        if (const Json* p = json::find(m, "parameters"); p && p->is_object()) spec.parameters = *p;
        if (const Json* cap = json::find(m, "capabilities")) spec.capabilities = *cap;
      }
      if (spec.model.empty()) return failed(Error(Errc::InvalidArgument, "a model in `models` names no model"));
      req.models.push_back(std::move(spec));
    }
  }
  if (req.models.empty()) {
    std::string model = json::get_string(with, "model");
    if (model.empty()) {
      const Json configured = rt.config().get(json::get_string(um, "model_setting", "default_model"));
      if (configured.is_string()) model = configured.get<std::string>();
    }
    if (model.empty()) return failed(Error(Errc::InvalidArgument, "no model: name one in the step or set a default model"));
    req.models.push_back(llm::ModelSpec{model, json::get_string(with, "provider"), Json::object()});
  }
  req.mode = json::get_string(with, "mode", req.models.size() > 1 ? "sequential" : "single");
  if (const Json* p = json::find(with, "parameters"); p && p->is_object()) req.parameters = *p;
  const std::string system = json::get_string(with, "system");
  if (!system.empty()) req.messages.push_back(Json{{"role", "system"}, {"content", system}});
  req.prompt = json::get_string(with, "prompt");
  req.inputs = c.inputs;
  if (const Json* w = json::find(with, "wants"); w && w->is_array()) {
    req.wants.clear();
    for (const auto& k : *w)
      if (k.is_string()) req.wants.push_back(k.get<std::string>());
  }
  if (req.prompt.empty() && req.inputs.empty() && req.messages.empty())
    return failed(Error(Errc::InvalidArgument, "nothing to send: the step has no prompt, messages or input"));
  req.kind = c.root_action;
  req.action = c.root_action;
  req.title = json::get_string(with, "title", json::get_string(um, "title", "ChatADHD"));
  req.timeout_ms = static_cast<int>(json::get_int(with, "timeout_ms", json::get_int(um, "timeout_ms", 180000)));
  req.cancel = c.cancel;
  if (c.usage_confirmation.is_object() && !c.usage_confirmation.empty()) req.usage.confirmation = c.usage_confirmation;

  llm::Services services(rt.config(), rt.secrets(), rt.http());
  services.data_root = rt.paths().root;
  auto outputs = llm::make_output_store(rt.blobs(), &rt.provenance(), &rt.db());
  services.outputs = outputs.get();
  Json provenance = Json::array();
  services.on_provenance = [&](const Json& p) { provenance.push_back(p); };

  auto outcome = llm::use_model(services, std::move(req));
  if (!outcome) return failed(outcome.error());
  StepOutcome out;
  out.provenance = provenance;
  bool any_ok = false;
  for (const auto& r : outcome->results) {
    if (r.state == llm::State::Held) {
      const std::string status = json::get_string(r.hold, "status");
      if (status != "requires_confirmation" && status != "denied" && status != "cancelled")
        return failed(Error(Errc::Paused, "model usage is not admitted: " + status));
      out.state = StepOutcome::State::Held;
      out.hold = usage_pending_of(r);
      out.parts.clear();
      return out;
    }
    if (r.state == llm::State::Cancelled) {
      out.state = StepOutcome::State::Cancelled;
      out.error = Error(Errc::Cancelled, "cancelled");
      return out;
    }
    if (r.ok()) {
      any_ok = true;
      for (const auto& p : r.outputs) out.parts.push_back(p);
      continue;
    }
    // A failed variant: the last failure is the error if none worked.
    if (r.state == llm::State::HttpError)
      out.error = Error(Errc::Http, "API error " + std::to_string(r.http_status) + ": " + std::string(utf8::prefix(r.body, 500)));
    else
      out.error = r.error ? *r.error : Error(Errc::Unavailable, "model call " + std::string(llm::to_string(r.state)));
  }
  if (any_ok) {
    out.state = StepOutcome::State::Ok;
    out.error.reset();
    return out;
  }
  out.state = StepOutcome::State::Failed;
  if (!out.error) out.error = Error(Errc::Unavailable, "the model call produced no result");
  return out;
}

bool is_overlay_action(std::string_view action) {
  return action == "rules.set" || action == "rules.disable" || action == "rules.list" || action == "actions.define" || action == "actions.remove" ||
         action == "groups.define" || action == "groups.remove";
}

// ── settings ────────────────────────────────────────────────────────

const Json* find_field(const Json& doc, const std::string& key) {
  const Json* fields = json::find(doc, "fields");
  if (!fields || !fields->is_array()) return nullptr;
  for (const auto& f : *fields)
    if (json::get_string(f, "key") == key) return &f;
  return nullptr;
}

Json take(const StepCall& c, const char* name) {
  if (const Json* v = json::find(c.with, name)) return *v;
  if (const Json* v = json::find(c.args, name)) return *v;
  return Json(nullptr);
}

// The value as the field's type wants it: text given to a string field stays text; other types are parsed.
Result<Json> typed_value(const Json& given, const std::string& type) {
  if (type == "string") {
    if (given.is_string()) return given;
    return Json(json::dump(given));
  }
  Json v = given;
  if (given.is_string()) v = json::parse_or(given.get<std::string>(), given);
  const bool ok = (type == "integer" && v.is_number_integer()) || (type == "number" && v.is_number()) ||
                  (type == "boolean" && v.is_boolean()) || (type == "object" && v.is_object()) ||
                  (type == "array" && v.is_array()) || type.empty();
  if (!ok) return Error(Errc::InvalidArgument, "the value does not fit the setting's type (" + type + ")");
  return v;
}

StepOutcome run_settings_set(Runtime& rt, const StepCall& c) {
  const Json key_json = take(c, "key");
  if (!key_json.is_string() || key_json.get<std::string>().empty()) return failed(Error(Errc::InvalidArgument, "key is required"));
  const std::string key = key_json.get<std::string>();
  Json given = take(c, "value");
  if (!json::find(c.with, "value") && !json::find(c.args, "value")) return failed(Error(Errc::InvalidArgument, "value is required"));
  auto doc = settings_fields(rt.paths().root);
  if (!doc) return failed(doc.error());
  const Json* field = find_field(*doc, key);
  if (!field) return failed(Error(Errc::NotFound, "no such setting '" + key + "'"));
  const std::string scope = json::get_string(*field, "scope", "config");
  auto value = typed_value(given, json::get_string(*field, "type"));
  if (!value) return failed(value.error());
  StepOutcome out;
  Json report = Json{{"key", key}, {"scope", scope}};
  if (scope == "secret") {
    if (!value->is_string()) return failed(Error(Errc::InvalidArgument, "a secret is text"));
    rt.secrets().set(key, value->get<std::string>());
    if (auto st = rt.secrets().save(); !st) return failed(st.error());
    rt.media().refresh();
    report["set"] = true;  // the value is never echoed
  } else {
    if (auto ok = check_config_write(key, rt.paths().root); !ok) return failed(ok.error());
    if (key == "loom_usage_policy") {
      if (auto st = validate_usage_policy_options(*value); !st) return failed(st.error());
    }
    report["previous"] = rt.config().get(key);
    if (key == "semantic_retry_mode") {
      if (!value->is_string()) return failed(Error(Errc::InvalidArgument, "semantic_retry_mode is text"));
      if (auto st = SemanticWorker::persist_retry_mode(rt.config(), value->get<std::string>()); !st) return failed(st.error());
      rt.worker().wake();
    } else {
      rt.config().set(key, *value);
      if (auto st = rt.config().save(); !st) return failed(st.error());
    }
    report["value"] = *value;
  }
  out.state = StepOutcome::State::Ok;
  out.data = report;
  out.parts.push_back(json_part(report));
  return out;
}

StepOutcome run_settings_get(Runtime& rt, const StepCall& c) {
  const Json key_json = take(c, "key");
  if (!key_json.is_string() || key_json.get<std::string>().empty()) return failed(Error(Errc::InvalidArgument, "key is required"));
  const std::string key = key_json.get<std::string>();
  auto doc = settings_fields(rt.paths().root);
  if (!doc) return failed(doc.error());
  const Json* field = find_field(*doc, key);
  if (!field) return failed(Error(Errc::NotFound, "no such setting '" + key + "'"));
  Json report = Json{{"key", key}, {"scope", json::get_string(*field, "scope", "config")}, {"default", json::get_string(*field, "default")},
                     {"edit", json::get_string(*field, "edit")}, {"description", json::get_string(*field, "description")}};
  if (json::get_string(*field, "scope") == "secret") report["set"] = rt.secrets().has(key);  // never the value
  else {
    report["value"] = rt.config().get(key);
    if (const Json* d = json::find(*field, "default_value")) report["default_value"] = *d;
  }
  StepOutcome out;
  out.state = StepOutcome::State::Ok;
  out.data = report;
  out.parts.push_back(json_part(report));
  return out;
}

}  // namespace

// ── ActionRunner ────────────────────────────────────────────────────

struct ActionRunner::Loaded {
  Engine engine;
  ElementResolver resolver;
  Json settings;
  Json results;  // runtime profile action_results: where the results of a run go
  ExecutorTable executors;
};

ActionRunner::ActionRunner(Runtime& rt) : rt_(rt), store_(make_event_log_run_store(rt.event_log())) {}
ActionRunner::~ActionRunner() = default;

RunStore& ActionRunner::store() { return *store_; }

void ActionRunner::install_executors(ExecutorTable& table) {
  Runtime& rt = rt_;
  // `settings` is read when a call happens, so an overlay change applies to the next call.
  table.add("use_model", [&rt](const StepCall& c) {
    auto profile = RuntimeProfile::load("action_run", rt.paths().root);
    return run_use_model(rt, profile ? profile->values() : Json::object(), c);
  });
  table.add("settings.set", [&rt](const StepCall& c) { return run_settings_set(rt, c); });
  table.add("settings.get", [&rt](const StepCall& c) { return run_settings_get(rt, c); });
  table.set_port_executor([this](const StepCall& c) {
    auto profile = RuntimeProfile::load("actions", rt_.paths().root);
    const Json* actions = profile ? json::find(profile->values(), "actions") : nullptr;
    const Json* entry = actions ? json::find(*actions, c.action) : nullptr;
    if (!entry) return failed(Error(Errc::NotFound, "unknown action '" + c.action + "'"));
    Json request = c.args;
    for (auto it = c.with.begin(); it != c.with.end(); ++it) request[it.key()] = it.value();
    auto result = invoke_port(json::get_string(*entry, "port"), json::get_string(*entry, "action"), request);
    if (!result) return failed(result.error());
    StepOutcome out;
    out.state = StepOutcome::State::Ok;
    out.data = *result;
    out.parts.push_back(json_part(*result));
    return out;
  });
}

Result<ActionRunner::Loaded> ActionRunner::load() {
  const auto& root = rt_.paths().root;
  LOOM_TRY_ASSIGN(auto engine, Engine::load(root));
  engine.set_user_values(rt_.config().all());  // the user's settings are the person level of scoped values
  LOOM_TRY_ASSIGN(auto resolver, ElementResolver::load(root));
  Json settings = Json::object();
  if (auto profile = RuntimeProfile::load("action_run", root)) settings = profile->values();
  Json results = Json::object();
  if (auto profile = RuntimeProfile::load("action_results", root)) results = profile->values();
  Loaded loaded{std::move(engine), std::move(resolver), std::move(settings), std::move(results), {}};
  install_executors(loaded.executors);
  return loaded;
}

Interpreter ActionRunner::make_interpreter(const Loaded& loaded) const {
  InterpreterDeps deps;
  deps.engine = &loaded.engine;
  deps.executors = &loaded.executors;
  deps.store = store_.get();
  deps.settings = loaded.settings;
  const ElementResolver* resolver = &loaded.resolver;
  Runtime* rt = &rt_;
  deps.element_content = [resolver, rt](const Json& element) {
    return resolver->content_parts(ElementSources{&rt->db(), &rt->blobs(), rt->paths().root}, element);
  };
  deps.on_step = [rt](const Json& record) {
    (void)rt->event_log().append("action.step", json::get_string(record, "run_id"), record);
  };
  // The results of a completed run land in the graph (or stay in the run, as the target says).
  ResultServices services;
  services.db = &rt_.db();
  services.blobs = &rt_.blobs();
  services.config = &rt_.config();
  services.engine = &loaded.engine;
  services.bus = &rt_.bus();
  services.profile = loaded.results;
  deps.on_result = [services](Json& run) { return store_run_results(services, run); };
  return Interpreter(deps);
}

Json ActionRunner::bind_params(const Json& entry, Json args) const {
  const Json* params = json::find(entry, "params");
  if (!params || !params->is_array()) return args;
  if (!args.is_object()) args = Json::object();
  for (const auto& p : *params) {
    const std::string name = json::get_string(p, "name"), setting = json::get_string(p, "setting");
    if (name.empty() || setting.empty()) continue;
    if (const Json* have = json::find(args, name); have && !have->is_null()) continue;  // the caller's word comes first
    const Json value = rt_.config().get(setting);
    if (value.is_null() || (value.is_string() && value.get<std::string>().empty())) continue;
    args[name] = value;
  }
  return args;
}

Result<RunRequest> ActionRunner::prepare(const Loaded& loaded, const Json& request) const {
  LOOM_TRY_ASSIGN(RunRequest r, RunRequest::from_json(request));
  Json element = request.value("element", Json(nullptr));
  const Json* entry = r.action.empty() ? nullptr : json::find(loaded.engine.catalogue(), r.action);
  if (r.action.empty() == false && !entry) return Error(Errc::NotFound, "unknown action '" + r.action + "'");
  if (element.is_null() && entry) {
    // An action may name the argument that is its element (a setting is the element of "set it").
    const std::string from = json::get_string(*entry, "element_from_arg");
    if (!from.empty()) {
      if (const Json* v = json::find(r.args, from)) element = *v;
    }
  }
  if (element.is_null()) element = Json::object();
  // Arguments the caller left out come from the settings the catalogue names (a model, a provider ...). What a composite
  // action needs and still lacks is said before anything starts: its early steps must not be paid for in vain.
  std::vector<const Json*> named;
  if (entry) named.push_back(entry);
  {
    const Json* steps = r.chain.is_array() ? &r.chain : (r.chain.is_object() ? json::find(r.chain, "steps") : nullptr);
    if (steps && steps->is_array())
      for (const auto& step : *steps) {
        const std::string name = step.is_string() ? step.get<std::string>() : json::get_string(step, "action");
        if (const Json* e = json::find(loaded.engine.catalogue(), name)) named.push_back(e);
      }
  }
  for (const Json* e : named) r.args = bind_params(*e, r.args);
  for (const Json* e : named) {
    if (json::get_string(*e, "kind") != "composite") continue;
    const Json* params = json::find(*e, "params");
    if (!params || !params->is_array()) continue;
    for (const auto& p : *params) {
      const std::string name = json::get_string(p, "name");
      const Json* have = json::find(r.args, name);
      if (!json::get_bool(p, "required", false) || (have && !have->is_null())) continue;
      const std::string setting = json::get_string(p, "setting");
      return Error(Errc::InvalidArgument, "action '" + json::get_string(*e, "id") + "' needs the argument '" + name + "'" +
                                              (setting.empty() ? std::string() : " (or the setting " + setting + ")"));
    }
  }
  const ElementSources sources{&rt_.db(), &rt_.blobs(), rt_.paths().root};
  const Engine& engine = loaded.engine;
  LOOM_TRY_ASSIGN(Json resolved, loaded.resolver.resolve(sources, element, [&](std::string_view c) { return engine.has_class(c); }));
  if (resolved.is_string()) resolved = Json{{"class", resolved}};
  r.element = std::move(resolved);
  return r;
}

Result<Json> ActionRunner::run(const Json& request, const CancelToken* external) {
  LOOM_TRY_ASSIGN(auto loaded, load());
  LOOM_TRY_ASSIGN(RunRequest r, prepare(loaded, request));
  r.run_id = gen_id("ar_");
  CancelToken own;
  const CancelToken* token = external ? external : &own;
  {
    std::lock_guard lk(mu_);
    live_.emplace(r.run_id, *token);
  }
  auto interpreter = make_interpreter(loaded);
  auto result = interpreter.start(r, token);
  std::lock_guard lk(mu_);
  live_.erase(r.run_id);
  if (result) return public_view(*result);
  return result;
}

Result<Json> ActionRunner::confirm(const Json& request, const CancelToken* external) {
  const std::string run_id = json::get_string(request, "run_id");
  if (run_id.empty()) return Error(Errc::InvalidArgument, "run_id is required");
  LOOM_TRY_ASSIGN(auto loaded, load());
  CancelToken own;
  const CancelToken* token = external ? external : &own;
  {
    std::lock_guard lk(mu_);
    if (live_.count(run_id)) return Error(Errc::Busy, "run '" + run_id + "' is executing right now");
    live_.emplace(run_id, *token);
  }
  auto interpreter = make_interpreter(loaded);
  auto result = interpreter.confirm(run_id, request, token);
  std::lock_guard lk(mu_);
  live_.erase(run_id);
  if (result) return public_view(*result);
  return result;
}

Result<Json> ActionRunner::cancel(const Json& request) {
  const std::string run_id = json::get_string(request, "run_id");
  if (run_id.empty()) return Error(Errc::InvalidArgument, "run_id is required");
  {
    std::lock_guard lk(mu_);
    if (auto it = live_.find(run_id); it != live_.end()) {
      it->second.cancel();
      return Json{{"run_id", run_id}, {"status", "cancelling"}, {"message", "the run stops at the next step boundary or inside its model call"}};
    }
  }
  LOOM_TRY_ASSIGN(auto loaded, load());
  auto interpreter = make_interpreter(loaded);
  return interpreter.cancel(run_id, json::get_string(request, "ref"));
}

Result<Json> ActionRunner::status(const Json& request) {
  if (json::get_bool(request, "list", false)) {
    LOOM_TRY_ASSIGN(auto runs, store_->list(request));
    Json out = Json::array();
    for (const auto& r : runs) out.push_back(run_summary(r));
    return Json{{"runs", out}};
  }
  const std::string run_id = json::get_string(request, "run_id");
  if (run_id.empty()) return Error(Errc::InvalidArgument, "run_id is required (or list: true)");
  LOOM_TRY_ASSIGN(auto doc, store_->load(run_id));
  if (!doc) return Error(Errc::NotFound, "no run '" + run_id + "'");
  Json run = *doc;
  std::lock_guard lk(mu_);
  run["live"] = live_.count(run_id) > 0;
  return run;
}

Result<Json> ActionRunner::preview(const Json& request) {
  LOOM_TRY_ASSIGN(auto loaded, load());
  LOOM_TRY_ASSIGN(RunRequest r, prepare(loaded, request));
  auto interpreter = make_interpreter(loaded);
  LOOM_TRY_ASSIGN(Json preview, interpreter.preview(r));
  // What the plan would cost: tokens, and money where the registry knows the price (never zero for what it does not know).
  std::int64_t chars = 0;
  const ElementSources sources{&rt_.db(), &rt_.blobs(), rt_.paths().root};
  if (preview.at("plan").at("cost").value("model_calls", 0) > 0)
    if (auto parts = loaded.resolver.content_parts(sources, r.element))
      for (const auto& p : *parts) chars += static_cast<std::int64_t>(utf8::length(p.text));
  EstimateServices services;
  services.models = &rt_.models();
  services.config = &rt_.config();
  services.settings = loaded.settings;
  services.catalogue = loaded.engine.catalogue();
  preview["estimate"] = estimate_plan(services, preview.at("plan"), r.args, chars);
  return preview;
}

// The writes of rules and of the user's own actions: raw (a step of a run has passed the rules already) or gated (a call from
// loom_invoke, HTTP or the CLI becomes a run, so the rules of the scope and the confirmation apply as for any command).
Result<Json> ActionRunner::write_overlay(std::string_view action, const Json& request) {
  const auto& root = rt_.paths().root;
  if (action == "rules.set") return rules_set(root, request);
  if (action == "rules.disable") return rules_disable(root, request);
  if (action == "rules.list") return rules_list(root, request);
  if (action == "actions.define") return actions_define(root, request);
  if (action == "actions.remove") return actions_remove(root, request);
  if (action == "groups.define") return groups_define(root, request);
  if (action == "groups.remove") return groups_remove(root, request);
  return Error(Errc::NotFound, "unknown action '" + std::string(action) + "'");
}

Result<Json> ActionRunner::run_gated(std::string_view action, const Json& request) {
  Json args = request;
  for (const char* control : {"confirmed", "ref", "context", "propose"}) args.erase(control);
  Json run_request{{"action", std::string(action)}, {"element", Json{{"class", "application"}}}, {"args", args}, {"ref", json::get_string(request, "ref", "api")}};
  Json context = request.value("context", Json::object());
  if (!context.is_object()) context = Json::object();
  if (!context.contains("origin")) context["origin"] = json::get_string(request, "ref", "api");
  run_request["context"] = context;
  if (json::get_bool(request, "confirmed", false)) run_request["confirmed"] = true;
  if (json::get_bool(request, "propose", false)) run_request["propose"] = true;
  std::string conv = json::get_string(context, "conv_id");
  if (conv.empty() && json::get_string(request, "scope") == "conversation") conv = json::get_string(request, "scope_id");
  if (!conv.empty()) run_request["scope"] = Json{{"conversation", conv}, {"turn", ""}};
  return run(run_request);
}

Result<Json> ActionRunner::invoke_port(std::string_view port, std::string_view action, const Json& request) {
  if (port == "actions" && is_overlay_action(action)) return write_overlay(action, request);
  if (port == "actions") return invoke(action, request);
  if (port == "storage") {
    Json r = request;
    r["action"] = action.rfind("storage.", 0) == 0 ? std::string(action) : "storage." + std::string(action);
    return storage::run_command(rt_, r);
  }
  return Error(Errc::Unsupported, "port '" + std::string(port) + "' cannot be run as a step");
}

Result<Json> ActionRunner::invoke(std::string_view action, const Json& request) {
  if (action == "run") return run(request);
  if (action == "confirm") return confirm(request);
  if (action == "cancel") return cancel(request);
  if (action == "status") return status(request);
  if (action == "preview") return preview(request);
  if (action == "rules.list") return write_overlay(action, request);
  if (is_overlay_action(action)) return run_gated(action, request);
  const ElementSources sources{&rt_.db(), &rt_.blobs(), rt_.paths().root};
  return actions::invoke(rt_.paths().root, action, request, &sources, &rt_.config());
}

}  // namespace loom::actions
