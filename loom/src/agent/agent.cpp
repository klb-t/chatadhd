// The agent: the runtime-bound side of the tool loop (include/loom/agent.h).  Everything an action does goes through
// ActionRunner's public API; this file only chooses which actions are offered, reads the settings, and writes the trace.
#include "loom/agent.h"

#include <set>

#include "loom/action_runner.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/log.h"
#include "loom/runtime.h"
#include "loom/tool_loop.h"
#include "loom/util/ids.h"
#include "loom/util/time.h"

namespace loom::agent {

namespace {

constexpr const char* kLog = "loom.agent";
using llm::tools::HostResult;
using llm::tools::LoopOutcome;
using llm::tools::StepRecord;
using llm::tools::Tool;

std::string cfg_string(const Config& c, const char* key) {
  Json v = c.get(key);
  return v.is_string() ? v.get<std::string>() : std::string();
}

// The text of a completed run document: its result parts (inline, or the preview of text that moved to the blob store).
std::string result_text(const Json& run) {
  std::string out;
  const Json* result = json::find(run, "result");
  if (result && result->is_object()) {
    if (const Json* parts = json::find(*result, "parts"); parts && parts->is_array()) {
      for (const auto& p : *parts) {
        if (json::get_string(p, "kind") != "text") continue;
        std::string t = json::get_string(p, "text");
        if (t.empty()) t = json::get_string(p, "preview");
        if (!out.empty() && !t.empty()) out += "\n";
        out += t;
      }
    }
    if (out.empty()) {
      if (const Json* data = json::find(*result, "data"); data && !data->is_null()) out = json::dump(*data);
    }
  }
  return out;
}

std::string reason_of(const Json& run) {
  for (const char* k : {"reason", "error"}) {
    if (const Json* v = json::find(run, k)) {
      if (v->is_string()) return v->get<std::string>();
      if (v->is_object()) {
        std::string m = json::get_string(*v, "message");
        if (!m.empty()) return m;
        m = json::get_string(*v, "reason");
        if (!m.empty()) return m;
      }
    }
  }
  if (const Json* g = json::find(run, "gates"); g && g->is_array())
    for (const auto& gate : *g) {
      std::string r = json::get_string(gate, "reason");
      if (!r.empty()) return r;
    }
  return "";
}

// The door to the interpreter: ActionRunner::run, nothing else.
class RunnerHost final : public llm::tools::ToolHost {
 public:
  RunnerHost(Runtime& rt, std::string loop_id, Json scope, Json context, const CancelToken* cancel)
      : rt_(rt), loop_id_(std::move(loop_id)), scope_(std::move(scope)), context_(std::move(context)), cancel_(cancel) {}

  Result<HostResult> call(const Tool& tool, const Json& arguments, const std::string& element, bool propose) override {
    Json request{{"action", tool.action}, {"args", arguments}, {"ref", "agent:" + loop_id_}, {"context", context_}};
    request["element"] = element.empty() ? Json{{"class", "application"}} : Json(element);
    if (!scope_.is_null()) request["scope"] = scope_;
    if (propose) request["propose"] = true;
    LOOM_TRY_ASSIGN(Json run, rt_.actions().run(request, cancel_));
    HostResult hr;
    hr.status = json::get_string(run, "status");
    hr.run_id = json::get_string(run, "id");
    hr.text = result_text(run);
    hr.reason = reason_of(run);
    hr.run = std::move(run);
    return hr;
  }

 private:
  Runtime& rt_;
  std::string loop_id_;
  Json scope_, context_;
  const CancelToken* cancel_;
};

bool excluded(const Json& expose, const std::string& id) {
  const Json* ex = json::find(expose, "exclude");
  if (!ex || !ex->is_array()) return false;
  for (const auto& e : *ex) {
    if (!e.is_string()) continue;
    const std::string p = e.get<std::string>();
    if (!p.empty() && (p.back() == '.' ? id.rfind(p, 0) == 0 : id == p)) return true;
  }
  return false;
}

// Everything the request, the profile and the interpreter's rules say about the loop before a model is asked.
struct Plan {
  Json profile;
  Json catalogue;
  llm::Route route;
  llm::ModelRequest base;
  llm::tools::LoopSettings settings;
  std::vector<Tool> tools;
  Json element = Json::object();
  std::string element_id;
  Json scope = nullptr;
  Json context = Json::object();
  std::string loop_id;
  Json denied = Json::array();   // actions the rules deny for this element (not offered)
};

Result<llm::ModelRequest> base_request(Runtime& rt, const Json& request, const Json& profile) {
  llm::ModelRequest base;
  llm::ModelSpec spec;
  spec.model = json::get_string(request, "model");
  if (spec.model.empty()) spec.model = cfg_string(rt.config(), "default_model");
  if (spec.model.empty()) return Error(Errc::InvalidArgument, "no model: name one in the request or set default_model");
  spec.provider = json::get_string(request, "provider");
  if (const Json* c = json::find(request, "capabilities"); c && c->is_object()) spec.capabilities = *c;
  base.models.push_back(spec);
  if (const Json* p = json::find(request, "parameters"); p && p->is_object()) base.parameters = *p;
  else base.parameters = Json::object();
  if (!base.parameters.contains("max_tokens"))
    if (Json mt = rt.config().get("max_tokens"); mt.is_number()) base.parameters["max_tokens"] = mt;
  if (!base.parameters.contains("temperature"))
    if (Json t = rt.config().get("temperature"); t.is_number()) base.parameters["temperature"] = t;
  if (const Json* m = json::find(request, "messages"); m && m->is_array()) base.messages = *m;
  base.prompt = json::get_string(request, "prompt");
  if (const Json* r = json::find(request, "route"); r && r->is_object()) {
    base.route.provider = json::get_string(*r, "provider");
    base.route.base_url = json::get_string(*r, "base_url");
    base.route.adapter = json::get_string(*r, "adapter");
  }
  if (base.route.provider.empty()) base.route.provider = json::get_string(request, "provider");
  base.title = json::get_string(request, "title", "ChatADHD");
  base.timeout_ms = static_cast<int>(json::get_int(request, "timeout_ms", 180000));
  base.action = "";
  base.kind = json::get_string(profile, "kind", "agent.loop");
  if (const Json* uc = json::find(request, "usage_confirmation"); uc && uc->is_object()) base.usage.confirmation = *uc;
  // The conversation as the context of the loop.
  const std::string conv = json::get_string(request, "conversation_id");
  if (!conv.empty()) {
    LOOM_TRY_ASSIGN(auto msgs, rt.db().get_msgs(conv));
    Json history = Json::array();
    for (const auto& m : msgs) {
      if (m.status != "active" || (m.role != "user" && m.role != "assistant" && m.role != "system")) continue;
      history.push_back(Json{{"role", m.role}, {"content", m.text}});
    }
    Json merged = history;
    for (const auto& m : base.messages) merged.push_back(m);
    base.messages = merged;
  }
  if (base.prompt.empty() && base.messages.empty()) return Error(Errc::InvalidArgument, "nothing to ask: give a prompt, messages or a conversation_id");
  return base;
}

Result<Plan> make_plan(Runtime& rt, llm::Services& services, const Json& request, bool need_credential) {
  Plan p;
  p.loop_id = gen_id("ag_");
  LOOM_TRY_ASSIGN(p.profile, llm::tools::load_profile(services));
  LOOM_TRY_ASSIGN(p.base, base_request(rt, request, p.profile));
  llm::RouteSelector sel = p.base.route;
  sel.require_credential = need_credential;
  LOOM_TRY_ASSIGN(p.route, llm::resolve_route(services, sel));
  LOOM_TRY_ASSIGN(p.settings, llm::tools::resolve_settings(services, p.profile, p.route, p.base.models.front(), request));
  LOOM_TRY_ASSIGN(p.catalogue, llm::action_catalogue(services));

  // The element in context: a bare id (the interpreter resolves it), an element object, or the application.
  const Json* el = json::find(request, "element");
  if (el && el->is_string() && !el->get<std::string>().empty()) { p.element_id = el->get<std::string>(); p.element = *el; }
  else if (el && el->is_object()) { p.element = *el; p.element_id = json::get_string(*el, "id"); }
  else if (std::string conv = json::get_string(request, "conversation_id"); !conv.empty()) { p.element_id = conv; p.element = conv; }
  else p.element = p.profile.value("expose", Json::object()).value("default_element", Json{{"class", "application"}});
  if (const Json* s = json::find(request, "scope"); s && !s->is_null()) p.scope = *s;
  else if (std::string conv = json::get_string(request, "conversation_id"); !conv.empty()) p.scope = Json{{"conversation", conv}, {"turn", ""}};
  p.context = Json{{"origin", "agent"}, {"agent_loop", p.loop_id}};
  if (std::string conv = json::get_string(request, "conversation_id"); !conv.empty()) p.context["conv_id"] = conv;
  if (std::string lang = json::get_string(request, "lang"); !lang.empty()) p.context["lang"] = lang;

  // Which actions fit: the engine's own matching (`actions.for`), minus what the profile leaves out, or the caller's list.
  const Json expose = p.profile.value("expose", Json::object());
  std::vector<std::string> ids;
  if (const Json* wanted = json::find(request, "actions"); wanted && wanted->is_array()) {
    for (const auto& a : *wanted)
      if (a.is_string() && json::find(p.catalogue, a.get<std::string>())) ids.push_back(a.get<std::string>());
  } else {
    Json q{{"element", p.element}, {"implemented_only", true}};
    if (!p.scope.is_null()) q["scope"] = p.scope;
    if (json::get_bool(expose, "include_unmatched", false)) q["include_unmatched"] = true;
    std::set<std::string> statuses;
    if (const Json* st = json::find(expose, "statuses"); st && st->is_array())
      for (const auto& s : *st) if (s.is_string()) statuses.insert(s.get<std::string>());
    std::vector<Json> elements{p.element};
    if (const Json* also = json::find(expose, "also_elements"); also && also->is_array())
      for (const auto& e : *also) elements.push_back(e);
    std::set<std::string> seen;
    for (const auto& element : elements) {
      q["element"] = element;
      LOOM_TRY_ASSIGN(Json rows, rt.actions().invoke("for", q));
      for (const auto& row : rows.value("actions", Json::array())) {
        const std::string id = json::get_string(row, "action");
        if (id.empty() || excluded(expose, id) || !seen.insert(id).second) continue;
        if (!statuses.empty() && !statuses.count(json::get_string(row, "implementation", "planned"))) continue;
        const std::string status = json::get_string(row, "status");
        if (status == "denied" || status == "not_applicable") { p.denied.push_back(Json{{"action", id}, {"status", status}}); continue; }
        if (row.contains("runs_on")) continue;  // a device step of a client: not something a text loop can carry out
        ids.push_back(id);
      }
    }
  }
  p.tools = llm::tools::make_tools(p.profile, p.catalogue, ids, p.settings.lang);
  return p;
}

Json settings_json(const llm::tools::LoopSettings& s) {
  return Json{{"policy", s.policy_id}, {"rules", s.rules}, {"max_steps", s.max_steps}, {"on_proposal", s.on_proposal},
              {"result_max_chars", s.result_max_chars}, {"format", s.format}, {"format_kind", s.format_kind}, {"sources", s.sources}};
}

}  // namespace

Result<Json> tools(Runtime& rt, const Json& request) {
  llm::Services services(rt.config(), rt.secrets(), rt.http());
  services.data_root = rt.paths().root;
  LOOM_TRY_ASSIGN(Plan p, make_plan(rt, services, request, false));
  Json list = Json::array();
  for (const auto& t : p.tools)
    list.push_back(Json{{"name", t.name}, {"action", t.action}, {"description", t.description}, {"parameters", t.parameters},
                        {"effects", t.effects}, {"mode", llm::tools::decide_mode(p.settings.rules, t.action, t.effects)}});
  return Json{{"schema", "loom.agent_tools/1"}, {"tools", list}, {"not_offered", p.denied}, {"settings", settings_json(p.settings)},
              {"wire", llm::tools::wire_tools(p.settings, p.tools)}};
}

Result<Json> formats(Runtime& rt, const Json& request) {
  llm::Services services(rt.config(), rt.secrets(), rt.http());
  services.data_root = rt.paths().root;
  LOOM_TRY_ASSIGN(Json profile, llm::tools::load_profile(services));
  llm::ModelSpec spec;
  spec.model = json::get_string(request, "model");
  if (spec.model.empty()) spec.model = cfg_string(rt.config(), "default_model");
  if (const Json* c = json::find(request, "capabilities"); c && c->is_object()) spec.capabilities = *c;
  llm::RouteSelector sel;
  sel.provider = json::get_string(request, "provider");
  sel.require_credential = false;
  LOOM_TRY_ASSIGN(auto route, llm::resolve_route(services, sel));
  LOOM_TRY_ASSIGN(std::string chosen, llm::tools::resolve_format(services, profile, route, spec));
  return Json{{"schema", "loom.agent_formats/1"}, {"model", spec.model}, {"provider", route.provider_id}, {"adapter", route.adapter},
              {"format", chosen}, {"formats", profile.value("formats", Json::object())},
              {"format_by_adapter", profile.value("format_by_adapter", Json::object())},
              {"model_rules", profile.value("model_rules", Json::array())}};
}

Result<Json> run(Runtime& rt, const Json& request, const CancelToken* cancel) {
  llm::Services services(rt.config(), rt.secrets(), rt.http());
  services.data_root = rt.paths().root;
  auto outputs = llm::make_output_store(rt.blobs(), &rt.provenance(), &rt.db());
  services.outputs = outputs.get();
  Json provenance = Json::array();
  services.on_provenance = [&](const Json& p) { provenance.push_back(p); };

  LOOM_TRY_ASSIGN(Plan p, make_plan(rt, services, request, true));
  p.base.cancel = cancel;
  Database& db = rt.db();
  const bool trace = json::get_bool(request, "trace", true);

  // The trace: one node for the loop; every call hangs on it (part_of).
  std::string loop_node;
  const std::string kind = p.profile.value("use_loop_node", std::string("action_run"));
  auto loop_meta = [&](const char* status, const LoopOutcome* o) {
    Json m{{"schema", std::string(llm::tools::kLoopSchema)}, {"loop_id", p.loop_id}, {"status", status}, {"created", timeutil::utc_now_iso()},
           {"model", p.base.models.front().model}, {"policy", p.settings.policy_id}, {"format", p.settings.format},
           {"element", p.element_id}, {"tools", static_cast<int>(p.tools.size())}};
    if (o) { m["turns"] = o->turns; m["steps"] = o->steps.size(); m["usage"] = o->usage; }
    return m;
  };
  if (trace) {
    NodeOptions no;
    no.tags = Json::array({"agent_loop"});
    no.metadata = loop_meta("running", nullptr);
    no.content = p.base.prompt;
    auto made = db.create_node("agent_loop:" + p.loop_id, kind, no);
    if (made) loop_node = *made;
    else log::warn(kLog, "loop node: {}", made.error().message);
  }

  RunnerHost host(rt, p.loop_id, p.scope, p.context, cancel);
  llm::tools::LoopHooks hooks;
  std::size_t n = 0;
  hooks.on_step = [&](const StepRecord& rec, const HostResult* hr) {
    if (loop_node.empty()) return;
    ++n;
    NodeOptions so;
    so.tags = Json::array({"agent_step"});
    so.metadata = rec.to_json();
    so.metadata["schema"] = std::string(kStepNodeSchema);
    so.metadata["loop_id"] = p.loop_id;
    so.metadata["n"] = n;
    auto step = db.create_node("agent_step:" + p.loop_id + ":" + std::to_string(n), kind, so);
    if (!step) { log::warn(kLog, "step node: {}", step.error().message); return; }
    Json lm{{"loop_id", p.loop_id}, {"n", n}};
    if (auto l = db.create_link(*step, loop_node, "part_of", 1.0, lm); !l) log::warn(kLog, "step link: {}", l.error().message);
    if (hr && !hr->run_id.empty()) {
      // the node the interpreter keeps for the run (the same label as its result trace uses)
      NodeOptions ro;
      ro.tags = Json::array({"action_run"});
      auto run_node = db.get_or_create_node("run:" + hr->run_id, kind, ro);
      if (run_node) {
        if (auto l = db.create_link(*run_node, loop_node, "part_of", 1.0, lm); !l) log::warn(kLog, "run link: {}", l.error().message);
        if (auto l = db.create_link(*step, *run_node, "derived_from", 1.0, lm); !l) log::warn(kLog, "step link: {}", l.error().message);
      }
    }
  };

  auto outcome = llm::tools::run_loop(services, p.base, p.tools, host, p.settings, p.element_id, hooks);
  if (!outcome) {
    if (!loop_node.empty()) {
      NodePatch patch;
      patch.metadata = loop_meta("error", nullptr);
      if (auto st = db.update_node(loop_node, patch); !st) log::warn(kLog, "loop node update: {}", st.error().message);
    }
    return outcome.error();
  }
  if (!loop_node.empty()) {
    NodePatch patch;
    patch.metadata = loop_meta(outcome->status.c_str(), &*outcome);
    patch.content = outcome->text.empty() ? p.base.prompt : outcome->text;
    if (auto st = db.update_node(loop_node, patch); !st) log::warn(kLog, "loop node update: {}", st.error().message);
  }
  Json answer = outcome->to_json();
  answer["loop_id"] = p.loop_id;
  answer["loop_node"] = loop_node.empty() ? Json(nullptr) : Json(loop_node);
  answer["settings"] = settings_json(p.settings);
  answer["provenance"] = provenance;
  return answer;
}

Result<Json> invoke(Runtime& rt, std::string_view action, const Json& request) {
  if (action == "run") return run(rt, request);
  if (action == "tools") return tools(rt, request);
  if (action == "formats") return formats(rt, request);
  return Error(Errc::InvalidArgument, "unknown agent action '" + std::string(action) + "' (run | tools | formats)");
}

}  // namespace loom::agent
