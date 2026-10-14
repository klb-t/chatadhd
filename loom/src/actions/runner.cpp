// The interpreter bound to a Runtime: real executors (use_model, invoke ports, settings), element
// resolution, durable runs, cancel tokens. Data: runtime profile action_run.
#include "loom/action_runner.h"
#include "loom/action_emergent.h"
#include "loom/action_overlay.h"

#include <algorithm>
#include <thread>

#include "loom/action_element.h"
#include "loom/action_estimate.h"
#include "loom/action_result.h"
#include "loom/chat_route.h"
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
#include "loom/util/base64.h"
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

// A use_model step with `media: asr | ocr` (transcribe, ocr): the recording or the picture of the element goes to the media providers
// (profile media), which put it on the wire through the one use_model - the adapter of the provider's family, the usage guard, the
// provenance record with the operation id. The step's provenance is what those calls recorded on this thread. A call the guard holds
// comes back as the provider's error (the media providers have no way to resume it): the run fails with that reason.
StepOutcome run_media(Runtime& rt, const Json& settings, const StepCall& c, const std::string& family) {
  const Json media = settings.value("media", Json::object());
  const Json kind = media.value(family, Json::object());
  if (family == "formats" || !kind.is_object() || kind.empty()) return failed(Error(Errc::InvalidArgument, "unknown media kind '" + family + "' in a use_model step"));
  const std::string part_kind = json::get_string(kind, "part");
  const llm::Part* input = nullptr;
  for (const auto& p : c.inputs)
    if (p.kind == part_kind) { input = &p; break; }
  if (!input) return failed(Error(Errc::InvalidArgument, "nothing to read: the step takes a " + part_kind + " input"));
  std::string bytes = input->bytes;
  if (bytes.empty() && !input->blob_hash.empty()) {
    auto stored = rt.blobs().read(input->blob_hash);
    if (!stored) return failed(stored.error());
    bytes = std::move(*stored);
  }
  if (bytes.empty()) return failed(Error(Errc::InvalidArgument, "the " + part_kind + " has no bytes to read"));
  // The format name the providers know: from the mime type (listed, else its subtype), else the kind's default.
  std::string format = json::get_string(kind, "default_format");
  if (!input->mime.empty()) {
    const std::string mime = input->mime.substr(0, input->mime.find(';'));
    const std::string listed = json::get_string(media.value("formats", Json::object()), mime);
    if (!listed.empty()) format = listed;
    else if (const auto slash = mime.find('/'); slash != std::string::npos && slash + 1 < mime.size()) format = mime.substr(slash + 1);
  }
  auto optional_text = [&](const char* key) {
    const std::string v = json::get_string(c.with, key);
    return v.empty() ? std::optional<std::string>() : std::optional<std::string>(v);
  };
  const auto provider = optional_text("provider");
  const auto language = optional_text("language");

  Json provenance = Json::array();
  const std::thread::id caller = std::this_thread::get_id();
  rt.media().set_provenance_sink([&provenance, caller](const Json& record) {
    if (std::this_thread::get_id() == caller) provenance.push_back(record);  // another thread's call is not this step's
  });
  struct Release {
    Runtime& rt;
    ~Release() { rt.media().set_provenance_sink(nullptr); }
  } release{rt};
  std::string text;
  Json data = nullptr;
  if (family == "asr") {
    auto done = rt.media().transcribe_bytes(bytes, format, provider, language);
    if (!done) return failed(done.error());
    text = done->text;
    data = done->to_json();
  } else {
    auto done = rt.media().ocr_bytes(bytes, format, provider, language);
    if (!done) return failed(done.error());
    text = done->text;
    data = done->to_json();
  }
  StepOutcome out;
  out.state = StepOutcome::State::Ok;
  llm::Part part;
  part.kind = "text";
  part.text = std::move(text);
  out.parts.push_back(std::move(part));
  out.data = std::move(data);
  out.provenance = std::move(provenance);
  return out;
}

StepOutcome run_use_model(Runtime& rt, const Json& settings, const StepCall& c) {
  if (const std::string media = json::get_string(c.with, "media"); !media.empty()) return run_media(rt, settings, c, media);
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
  // A model call is declared under a composite that has a use_model step of its own: the root when it does, else the composite
  // whose step it is (a chain of composites, a sequence saved as one action).
  req.action = c.owner_action.empty() ? c.root_action : c.owner_action;
  if (!c.root_action.empty() && !c.owner_action.empty() && c.root_action != c.owner_action) {
    if (auto profile = RuntimeProfile::load("actions", rt.paths().root))
      if (const Json* actions = json::find(profile->values(), "actions"))
        if (const Json* root_entry = json::find(*actions, c.root_action))
          for (const auto& st : root_entry->value("steps", Json::array()))
            if (json::get_string(st, "action") == "use_model") req.action = c.root_action;
  }
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
  Json client;   // the declaration of the asking client (client_devices.h); null for none
  ExecutorTable executors;
};

ActionRunner::ActionRunner(Runtime& rt) : rt_(rt), store_(make_event_log_run_store(rt.event_log())), index_(*store_) {}
ActionRunner::~ActionRunner() = default;

RunStore& ActionRunner::store() { return *store_; }
SequenceIndex& ActionRunner::sequence_index() { return index_; }

void ActionRunner::install_executors(ExecutorTable& table) {
  Runtime& rt = rt_;
  // `settings` is read when a call happens, so an overlay change applies to the next call.
  table.add("use_model", [&rt](const StepCall& c) {
    auto profile = RuntimeProfile::load("action_run", rt.paths().root);
    return run_use_model(rt, profile ? profile->values() : Json::object(), c);
  });
  // The device actions a client declared are carried out by that client: the step pauses the run (client_devices.h).
  table.set_client_executor(make_client_step_executor());
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

Json ActionRunner::client_declaration(std::string_view client_id) { return clients_.find(client_id); }

Json ActionRunner::client_patches(std::string_view client_id) {
  const Json declaration = clients_.find(client_id);
  if (declaration.is_null()) return Json::object();
  auto profile = RuntimeProfile::load("client_devices", rt_.paths().root);
  auto catalogue = RuntimeProfile::load("actions", rt_.paths().root);
  if (!profile || !catalogue) return Json::object();
  const Json* actions = json::find(catalogue->values(), "actions");
  return actions::client_patches(actions ? *actions : Json::object(), declaration, profile->values());
}

Result<ActionRunner::Loaded> ActionRunner::load(std::string_view client_id) {
  const auto& root = rt_.paths().root;
  LOOM_TRY_ASSIGN(auto engine, Engine::load(root));
  engine.set_user_values(rt_.config().all());  // the user's settings are the person level of scoped values
  engine.apply_client(client_patches(client_id));  // the catalogue as the asking client sees it
  LOOM_TRY_ASSIGN(auto resolver, ElementResolver::load(root));
  Json settings = Json::object();
  if (auto profile = RuntimeProfile::load("action_run", root)) settings = profile->values();
  Json results = Json::object();
  if (auto profile = RuntimeProfile::load("action_results", root)) results = profile->values();
  Loaded loaded{std::move(engine), std::move(resolver), std::move(settings), std::move(results), clients_.find(client_id), {}};
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
  auto known_class = [&](std::string_view c) { return engine.has_class(c); };
  // Several elements as one input (combine): a list of ids, or {"members": [...]}.
  if (element.is_array()) element = Json{{"members", element}};
  Json resolved;
  if (element.is_object() && element.contains("members") && element["members"].is_array() && !element.contains("source")) {
    LOOM_TRY_ASSIGN(resolved, loaded.resolver.group(sources, element["members"], known_class,
                                                    [&](std::string_view cls) { return engine.expand_traits({std::string(cls)}); }));
  } else {
    LOOM_TRY_ASSIGN(resolved, loaded.resolver.resolve(sources, element, known_class));
  }
  if (resolved.is_string()) resolved = Json{{"class", resolved}};
  r.element = std::move(resolved);
  if (loaded.client.is_object()) {
    r.client = Json{{"client_id", loaded.client.value("client_id", "")}, {"client", loaded.client.value("client", Json::object())}};
  } else if (const std::string asked = json::get_string(request, "client_id"); !asked.empty()) {
    r.client = Json{{"client_id", asked}, {"known", false}};  // never declared, or the declaration expired: the device actions stay planned
  }
  return r;
}

Result<Json> ActionRunner::run(const Json& request, const CancelToken* external) {
  LOOM_TRY_ASSIGN(auto loaded, load(json::get_string(request, "client_id")));
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

// ── the clients ─────────────────────────────────────────────────────

Result<Json> ActionRunner::client_declare(const Json& request) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("client_devices", rt_.paths().root));
  LOOM_TRY_ASSIGN(auto catalogue, RuntimeProfile::load("actions", rt_.paths().root));
  const Json* actions = json::find(catalogue.values(), "actions");
  return clients_.declare(request, profile.values(), actions ? *actions : Json::object());
}

Result<Json> ActionRunner::client_capabilities(const Json& request) {
  const std::string id = json::get_string(request, "client_id");
  if (id.empty()) return Json{{"clients", clients_.list()}};
  const Json declaration = clients_.find(id);
  if (declaration.is_null()) {
    auto profile = RuntimeProfile::load("client_devices", rt_.paths().root);
    Json out{{"client_id", id}, {"known", false}};
    if (profile)
      if (const Json* t = json::find(profile->values().value("texts", Json::object()), "unknown_client")) out["message"] = *t;
    return out;
  }
  Json out = declaration;
  out["known"] = true;
  return out;
}

Result<Json> ActionRunner::client_blob(const Json& request) {
  const std::string hash = json::get_string(request, "hash");
  if (!BlobStore::is_valid_hash(hash)) return Error(Errc::InvalidArgument, "hash must be a blob hash (64 hex)");
  if (!rt_.blobs().has(hash)) return Error(Errc::NotFound, "no blob " + hash);
  LOOM_TRY_ASSIGN(std::string bytes, rt_.blobs().read(hash));
  std::string mime;
  if (auto row = rt_.db().find_node("blob:" + hash, "blob"); row && *row) mime = json::get_string((*row)->metadata, "mime");
  return Json{{"hash", hash}, {"mime", mime}, {"size", static_cast<std::int64_t>(bytes.size())}, {"data_base64", base64::encode(bytes)}};
}

namespace {

// The parts a client brought back, as typed parts of the run: media given as data_base64 go to the blob store (with a graph node
// and an artifact record that say where they came from), a part naming a stored blob is checked, text stays text.
Result<Json> stored_client_parts(Runtime& rt, const Json& parts, const Json& origin) {
  Json out = Json::array();
  if (!parts.is_array()) return out;
  for (const auto& p : parts) {
    if (!p.is_object()) return Error(Errc::InvalidArgument, "a part must be an object");
    const std::string kind = json::get_string(p, "kind");
    if (!llm::known_part_kind(kind)) return Error(Errc::InvalidArgument, "unknown part kind '" + kind + "'");
    Json part = Json{{"kind", kind}};
    if (const std::string mime = json::get_string(p, "mime"); !mime.empty()) part["mime"] = mime;
    if (kind == "text" || kind == "json" || kind == "embedding") {
      if (const std::string text = json::get_string(p, "text"); !text.empty()) part["text"] = text;
      if (const Json* v = json::find(p, "value")) part["value"] = *v;
      out.push_back(std::move(part));
      continue;
    }
    std::string hash = json::get_string(p, "blob_hash");
    if (const std::string b64 = json::get_string(p, "data_base64"); !b64.empty()) {
      LOOM_TRY_ASSIGN(std::string bytes, base64::decode(b64, true));
      if (bytes.empty()) return Error(Errc::InvalidArgument, "a " + kind + " part has no bytes");
      LOOM_TRY_ASSIGN(BlobRef ref, rt.blobs().put(bytes, json::get_string(p, "mime")));
      hash = ref.hash;
      part["size"] = ref.size;
      NodeOptions o;
      o.tags = Json::array({"blob", "client_capture", kind});
      Json meta{{"blob_hash", hash}, {"size", ref.size}, {"mime", json::get_string(p, "mime")}, {"output_kind", kind}, {"produced_by", origin}};
      if (const std::string name = json::get_string(p, "name"); !name.empty()) meta["name"] = name;
      o.metadata = std::move(meta);
      LOOM_TRY_ASSIGN(std::string node, rt.db().get_or_create_node("blob:" + hash, "blob", o));
      part["node_id"] = node;
      ArtifactRecord artifact;
      artifact.kind = "client_capture";
      artifact.title = kind + " from " + json::get_string(origin, "action");
      artifact.blob_hash = hash;
      artifact.mime = json::get_string(p, "mime");
      artifact.metadata = origin;
      if (auto made = rt.provenance().add_artifact(std::move(artifact))) part["artifact_id"] = *made;
    } else if (!hash.empty()) {
      if (!BlobStore::is_valid_hash(hash) || !rt.blobs().has(hash)) return Error(Errc::NotFound, "the blob " + hash + " is not in the blob store");
    } else if (json::get_string(p, "url").empty()) {
      return Error(Errc::InvalidArgument, "a " + kind + " part needs data_base64, blob_hash or url");
    }
    if (!hash.empty()) part["blob_hash"] = hash;
    if (const std::string url = json::get_string(p, "url"); !url.empty()) part["url"] = url;
    out.push_back(std::move(part));
  }
  return out;
}

}  // namespace

Result<Json> ActionRunner::client_result(const Json& request, const CancelToken* external) {
  const std::string run_id = json::get_string(request, "run_id");
  if (run_id.empty()) return Error(Errc::InvalidArgument, "run_id is required");
  LOOM_TRY_ASSIGN(auto loaded, load());
  auto interpreter = make_interpreter(loaded);
  Json answer = request;
  answer.erase("run_id");
  // Nothing is stored for an answer the run is not waiting for (a wrong receipt, a run that was cancelled meanwhile).
  LOOM_TRY_ASSIGN(Json current, interpreter.get(run_id));
  const Json pending = current.value("pending", Json(nullptr));
  if (json::get_string(current, "status") == "awaiting_client" && pending.is_object() &&
      json::get_string(request, "receipt_id") == json::get_string(pending, "receipt_id") && json::get_string(request, "state") == "ok") {
    const Json spec = pending.value("client_step", Json::object());
    Json origin{{"schema", "loom.client_capture/1"}, {"run_id", run_id}, {"step", json::get_string(pending, "step")},
                {"action", json::get_string(pending, "action")}, {"client_id", current.value("client", Json::object()).value("client_id", "")},
                {"ref", json::get_string(request, "ref")}, {"output_kind", spec.value("expects", Json::array()).empty() ? "media" : spec["expects"][0]}};
    LOOM_TRY_ASSIGN(answer["parts"], stored_client_parts(rt_, request.value("parts", Json::array()), origin));
  }
  CancelToken own;
  const CancelToken* token = external ? external : &own;
  {
    std::lock_guard lk(mu_);
    if (live_.count(run_id)) return Error(Errc::Busy, "run '" + run_id + "' is executing right now");
    live_.emplace(run_id, *token);
  }
  auto result = interpreter.client_result(run_id, answer, token);
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
  LOOM_TRY_ASSIGN(auto loaded, load(json::get_string(request, "client_id")));
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
  if (action == "rules.disable") {
    auto done = rules_disable(root, request);
    if (done && json::get_bool(*done, "written", true) && json::get_bool(*done, "changed", true))
      chat_route::note_rule_switched(rt_, json::get_string(*done, "id"), json::get_bool(*done, "disabled", true));
    return done;
  }
  if (action == "rules.list") return rules_list(root, request);
  const bool define = action == "actions.define" || action == "groups.define";
  const bool remove = action == "actions.remove" || action == "groups.remove";
  if (!define && !remove) return Error(Errc::NotFound, "unknown action '" + std::string(action) + "'");
  Result<Json> done = action == "actions.define"   ? actions_define(root, request)
                      : action == "groups.define"  ? groups_define(root, request)
                      : action == "actions.remove" ? actions_remove(root, request)
                                                   : groups_remove(root, request);
  // The node that recorded where the proposal came from says what became of it.
  if (done && json::get_bool(*done, "written", true)) {
    const std::string node = define ? json::get_string(request.value("provenance", Json::object()), "node")
                                    : json::get_string(done->value("origin", Json::object()), "node");
    emergent::settle(rt_, node, define ? "accepted" : "undone", *done);
    if (!define) chat_route::note_removed(rt_, done->value("origin", Json::object()));
  }
  return done;
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
  if (action == "client_result") return client_result(request);
  if (action == "client.declare") return client_declare(request);
  if (action == "client.capabilities") return client_capabilities(request);
  if (action == "client.blob") return client_blob(request);
  if (action == "cancel") return cancel(request);
  if (action == "status") return status(request);
  if (action == "preview") return preview(request);
  if (action == "rules.list") return write_overlay(action, request);
  if (is_overlay_action(action)) return run_gated(action, request);
  const ElementSources sources{&rt_.db(), &rt_.blobs(), rt_.paths().root};
  const Json patches = client_patches(json::get_string(request, "client_id"));
  return actions::invoke(rt_.paths().root, action, request, &sources, &rt_.config(), &patches);
}

}  // namespace loom::actions
