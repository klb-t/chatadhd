// The tool loop of use_model (include/loom/tool_loop.h).  Policy, formats and messages are the runtime profile
// `tool_loop`; this file is the mechanism: build the tools, run a model turn, read the calls, hand each to the host
// under the policy, put the results back, repeat.
#include "loom/tool_loop.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

#include "loom/config.h"
#include "loom/log.h"
#include "loom/runtime_profile.h"
#include "loom/util/utf8.h"

namespace loom::llm::tools {

namespace {

constexpr const char* kLog = "loom.tool_loop";

std::string lang_pick(const Json& localized, const std::string& lang) {
  if (localized.is_string()) return localized.get<std::string>();
  if (!localized.is_object()) return "";
  std::string v = json::get_string(localized, lang);
  if (v.empty()) v = json::get_string(localized, "en");
  return v;
}

std::string fill(std::string text, const std::map<std::string, std::string>& values) {
  for (const auto& [k, v] : values) {
    const std::string key = "{" + k + "}";
    for (std::size_t at = text.find(key); at != std::string::npos; at = text.find(key, at + v.size())) text.replace(at, key.size(), v);
  }
  return text;
}

std::filesystem::path root_of(const Services& s) {
  return s.data_root.empty() ? s.config.path().parent_path() : s.data_root;
}

bool is_effects_match(const Json& want, const Json& effects) {
  if (!want.is_object()) return true;
  for (auto it = want.begin(); it != want.end(); ++it) {
    const Json* have = json::find(effects, it.key());
    if (!have || !have->is_boolean() || !it.value().is_boolean() || *have != it.value()) return false;  // an unknown flag matches nothing
  }
  return true;
}

bool starts_with_any(const std::string& s, const Json& prefixes) {
  if (!prefixes.is_array()) return false;
  for (const auto& p : prefixes)
    if (p.is_string() && s.rfind(p.get<std::string>(), 0) == 0) return true;
  return false;
}

std::string jtype(const std::string& t) {
  static const std::set<std::string> known = {"string", "integer", "number", "boolean", "object", "array"};
  return known.count(t) ? t : "string";
}

std::string sanitize(const std::string& id, const Json& rule) {
  const std::string allowed = json::get_string(rule, "allowed", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
  const std::string repl = json::get_string(rule, "replacement", "_");
  std::string out;
  for (char c : id) out += allowed.find(c) != std::string::npos ? std::string(1, c) : repl;
  const long max = json::get_int(rule, "max_length", 64);
  if (max > 0 && out.size() > static_cast<std::size_t>(max)) out.resize(static_cast<std::size_t>(max));
  return out;
}

// ── Canonical transcript -> a dialect's wire messages ───────────────

Json call_json(const Json& c) { return c.value("arguments", Json::object()); }

Json wire_openai(const Json& canonical) {
  Json out = Json::array();
  for (const auto& m : canonical) {
    const std::string role = json::get_string(m, "role");
    if (role == "assistant" && m.contains("tool_calls") && !m["tool_calls"].empty()) {
      Json calls = Json::array();
      for (const auto& c : m["tool_calls"])
        calls.push_back(Json{{"id", c.value("id", "")}, {"type", "function"},
                             {"function", Json{{"name", c.value("name", "")}, {"arguments", json::dump(call_json(c))}}}});
      const std::string text = json::get_string(m, "content");
      out.push_back(Json{{"role", "assistant"}, {"content", text.empty() ? Json(nullptr) : Json(text)}, {"tool_calls", calls}});
    } else if (role == "tool") {
      out.push_back(Json{{"role", "tool"}, {"tool_call_id", m.value("tool_call_id", "")}, {"content", m.value("content", "")}});
    } else {
      out.push_back(m);
    }
  }
  return out;
}

Json wire_anthropic(const Json& canonical) {
  Json out = Json::array();
  for (const auto& m : canonical) {
    const std::string role = json::get_string(m, "role");
    if (role == "assistant" && m.contains("tool_calls") && !m["tool_calls"].empty()) {
      Json blocks = Json::array();
      const std::string text = json::get_string(m, "content");
      if (!text.empty()) blocks.push_back(Json{{"type", "text"}, {"text", text}});
      for (const auto& c : m["tool_calls"])
        blocks.push_back(Json{{"type", "tool_use"}, {"id", c.value("id", "")}, {"name", c.value("name", "")}, {"input", call_json(c)}});
      out.push_back(Json{{"role", "assistant"}, {"content", blocks}});
    } else if (role == "tool") {
      Json block{{"type", "tool_result"}, {"tool_use_id", m.value("tool_call_id", "")}, {"content", m.value("content", "")}};
      if (!out.empty() && out.back().value("role", "") == "user" && out.back()["content"].is_array() && !out.back()["content"].empty() &&
          out.back()["content"].back().value("type", "") == "tool_result")
        out.back()["content"].push_back(block);
      else
        out.push_back(Json{{"role", "user"}, {"content", Json::array({block})}});
    } else {
      out.push_back(m);
    }
  }
  return out;
}

Json wire_text(const Json& canonical, const Json& fmt) {
  const std::string open = json::get_string(fmt, "open", "<tool_call>"), close = json::get_string(fmt, "close", "</tool_call>");
  const std::string ropen = json::get_string(fmt, "result_open", "<tool_result>"), rclose = json::get_string(fmt, "result_close", "</tool_result>");
  Json out = Json::array();
  bool last_was_result = false;
  for (const auto& m : canonical) {
    const std::string role = json::get_string(m, "role");
    if (role == "assistant" && m.contains("tool_calls") && !m["tool_calls"].empty()) {
      std::string text = json::get_string(m, "content");
      for (const auto& c : m["tool_calls"])
        text += (text.empty() ? "" : "\n") + open + json::dump(Json{{"name", c.value("name", "")}, {"arguments", call_json(c)}}) + close;
      out.push_back(Json{{"role", "assistant"}, {"content", text}});
      last_was_result = false;
    } else if (role == "tool") {
      const std::string block = ropen + json::dump(Json{{"name", m.value("name", "")}, {"result", m.value("content", "")}}) + rclose;
      if (last_was_result) out.back()["content"] = out.back()["content"].get<std::string>() + "\n" + block;
      else out.push_back(Json{{"role", "user"}, {"content", block}});
      last_was_result = true;
    } else {
      out.push_back(m);
      last_was_result = false;
    }
  }
  return out;
}

// The calls in a text answer: marked JSON blocks; the text without them.
std::vector<ToolCall> parse_text_calls(std::string& text, const Json& fmt, std::size_t turn) {
  const std::string open = json::get_string(fmt, "open", "<tool_call>"), close = json::get_string(fmt, "close", "</tool_call>");
  std::vector<ToolCall> calls;
  std::string rest;
  std::size_t pos = 0;
  while (true) {
    const std::size_t a = text.find(open, pos);
    if (a == std::string::npos) { rest += text.substr(pos); break; }
    const std::size_t b = text.find(close, a + open.size());
    if (b == std::string::npos) { rest += text.substr(pos); break; }
    rest += text.substr(pos, a - pos);
    const std::string body = text.substr(a + open.size(), b - a - open.size());
    ToolCall tc;
    tc.id = "call_" + std::to_string(turn) + "_" + std::to_string(calls.size() + 1);
    tc.raw_arguments = body;
    auto parsed = json::parse(body);
    if (parsed && parsed->is_object() && parsed->contains("name") && (*parsed)["name"].is_string()) {
      tc.name = (*parsed)["name"].get<std::string>();
      const Json* args = json::find(*parsed, "arguments");
      if (!args || args->is_null()) tc.arguments = Json::object();
      else if (args->is_object()) tc.arguments = *args;
      else tc.valid = false;
    } else {
      tc.valid = false;
    }
    calls.push_back(std::move(tc));
    pos = b + close.size();
  }
  if (!calls.empty()) {
    // trim
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.back()))) rest.pop_back();
    std::size_t s = 0;
    while (s < rest.size() && std::isspace(static_cast<unsigned char>(rest[s]))) ++s;
    text = rest.substr(s);
  }
  return calls;
}

void add_usage(Json& total, const Json& usage) {
  if (!usage.is_object()) return;
  for (auto it = usage.begin(); it != usage.end(); ++it) {
    if (!it.value().is_number()) continue;
    const double add = it.value().get<double>();
    const double have = total.contains(it.key()) && total[it.key()].is_number() ? total[it.key()].get<double>() : 0.0;
    const double sum = have + add;
    total[it.key()] = (it.value().is_number_integer() && (!total.contains(it.key()) || total[it.key()].is_number_integer())) ? Json(static_cast<long long>(sum)) : Json(sum);
  }
}

std::string cut(const std::string& s, long max_chars) {
  if (max_chars <= 0) return s;
  const std::string_view p = utf8::prefix(s, static_cast<std::size_t>(max_chars));
  if (p.size() >= s.size()) return s;
  return std::string(p) + "... [cut: " + std::to_string(s.size()) + " bytes in the run]";
}

const Json* named_policy(const Json& profile, const std::string& id) {
  const Json* policies = json::find(profile, "policies");
  return policies && policies->is_object() ? json::find(*policies, id) : nullptr;
}

}  // namespace

// ── Data ────────────────────────────────────────────────────────────

Json StepRecord::to_json() const {
  return Json{{"turn", turn}, {"tool_call_id", tool_call_id}, {"tool", tool}, {"action", action}, {"arguments", arguments},
              {"mode", mode}, {"status", status}, {"run_id", run_id}};
}

Json LoopOutcome::to_json() const {
  Json st = Json::array();
  for (const auto& s : steps) st.push_back(s.to_json());
  Json j{{"schema", std::string(kLoopSchema)}, {"status", status}, {"text", text}, {"turns", turns}, {"steps", st},
         {"proposals", proposals}, {"usage", usage}, {"messages", messages}};
  if (!hold.is_null()) j["hold"] = hold;
  if (error) j["error"] = Json{{"code", static_cast<int>(error->code)}, {"message", error->message}};
  return j;
}

Result<Json> load_profile(const Services& services) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("tool_loop", root_of(services)));
  return profile.values();
}

Result<std::string> resolve_format(const Services& services, const Json& profile, const Route& route, const ModelSpec& model) {
  const Json* formats = json::find(profile, "formats");
  auto known = [&](const std::string& id) { return formats && formats->is_object() && formats->contains(id); };
  // The data layers, each replacing the previous one: the provider manifest (metadata capabilities.tools), config
  // model_capabilities[model].tools, the request's ModelSpec::capabilities.tools.
  std::string named;
  auto layer = [&](const Json* data) {
    if (!data || !data->is_object()) return;
    if (const Json* t = json::find(*data, "tools"); t && t->is_string() && !t->get<std::string>().empty()) named = t->get<std::string>();
  };
  layer(json::find(route.metadata, "capabilities"));
  if (const Json per = services.config.get("model_capabilities"); per.is_object()) layer(json::find(per, model.model));
  layer(&model.capabilities);
  if (!named.empty()) {
    if (!known(named)) return Error(Errc::InvalidArgument, "unknown tool format '" + named + "' (profile tool_loop formats)");
    return named;
  }
  if (const Json* rules = json::find(profile, "model_rules"); rules && rules->is_array()) {
    for (const auto& r : *rules) {
      const Json match = r.value("match", Json::object());
      bool ok = true;
      if (match.contains("adapter") && json::get_string(match, "adapter") != route.adapter) ok = false;
      if (match.contains("provider") && json::get_string(match, "provider") != route.provider_id) ok = false;
      if (match.contains("model") && json::get_string(match, "model") != model.model) ok = false;
      if (match.contains("model_prefix") && model.model.rfind(json::get_string(match, "model_prefix"), 0) != 0) ok = false;
      if (ok) {
        const std::string f = json::get_string(r, "format");
        if (!known(f)) return Error(Errc::InvalidArgument, "unknown tool format '" + f + "' (profile tool_loop model_rules)");
        return f;
      }
    }
  }
  if (const Json* by = json::find(profile, "format_by_adapter"); by && by->is_object()) {
    const std::string f = json::get_string(*by, route.adapter);
    if (!f.empty() && known(f)) return f;
  }
  const std::string def = json::get_string(profile, "default_format", "text");
  if (!known(def)) return Error(Errc::InvalidArgument, "unknown default tool format '" + def + "'");
  return def;
}

Result<LoopSettings> resolve_settings(const Services& services, const Json& profile, const Route& route, const ModelSpec& model,
                                      const Json& request) {
  LoopSettings s;
  s.sources = Json::object();
  // policy: request (id or inline) > setting (id or inline) > profile preset
  Json chosen = nullptr;
  std::string source = "profile";
  if (const Json* p = json::find(request, "policy"); p && !p->is_null()) { chosen = *p; source = "request"; }
  else if (Json p2 = services.config.get("tool_loop_policy"); !p2.is_null() && !(p2.is_string() && p2.get<std::string>().empty())) { chosen = p2; source = "setting"; }
  if (chosen.is_null()) chosen = json::get_string(profile, "policy", "local_reversible");
  if (chosen.is_string()) {
    s.policy_id = chosen.get<std::string>();
    const Json* pol = named_policy(profile, s.policy_id);
    if (!pol) return Error(Errc::InvalidArgument, "unknown tool-loop policy '" + s.policy_id + "'");
    s.rules = pol->value("rules", Json::array());
  } else if (chosen.is_object() && chosen.contains("rules") && chosen["rules"].is_array()) {
    s.policy_id = json::get_string(chosen, "id", "custom");
    s.rules = chosen["rules"];
  } else if (chosen.is_array()) {
    s.policy_id = "custom";
    s.rules = chosen;
  } else {
    return Error(Errc::InvalidArgument, "tool-loop policy must be a preset id or {rules: [...]}");
  }
  s.sources["policy"] = source;

  auto number = [&](const char* key, const char* setting, long fallback, long& out) -> Status {
    Json v = nullptr;
    std::string src = "profile";
    if (const Json* r = json::find(request, key); r && !r->is_null()) { v = *r; src = "request"; }
    else if (Json c = services.config.get(setting); !c.is_null() && !(c.is_string() && c.get<std::string>().empty())) { v = c; src = "setting"; }
    out = fallback;
    if (!v.is_null()) {
      if (v.is_string()) v = json::parse_or(v.get<std::string>(), Json(nullptr));
      if (!v.is_number()) return Error(Errc::InvalidArgument, std::string(key) + " must be a number (0 = no limit)");
      out = static_cast<long>(std::llround(v.get<double>()));
      if (out < 0) return Error(Errc::InvalidArgument, std::string(key) + " must not be negative (0 = no limit)");
    }
    s.sources[key] = src;
    return Status{};
  };
  LOOM_TRY(number("max_steps", "tool_loop_max_steps", json::get_int(profile, "max_steps", 0), s.max_steps));
  LOOM_TRY(number("result_max_chars", "tool_loop_result_max_chars", json::get_int(profile, "result_max_chars", 0), s.result_max_chars));

  s.on_proposal = json::get_string(profile, "on_proposal", "stop");
  std::string src = "profile";
  if (const Json* r = json::find(request, "on_proposal"); r && r->is_string()) { s.on_proposal = r->get<std::string>(); src = "request"; }
  else if (Json c = services.config.get("tool_loop_on_proposal"); c.is_string() && !c.get<std::string>().empty()) { s.on_proposal = c.get<std::string>(); src = "setting"; }
  if (s.on_proposal != "stop" && s.on_proposal != "continue") return Error(Errc::InvalidArgument, "on_proposal must be stop or continue");
  s.sources["on_proposal"] = src;

  LOOM_TRY_ASSIGN(s.format, resolve_format(services, profile, route, model));
  const Json* fmt = json::find(*json::find(profile, "formats"), s.format);
  s.format_data = fmt ? *fmt : Json::object();
  s.format_kind = json::get_string(s.format_data, "kind");
  s.messages = profile.value("messages", Json::object());
  s.kind = json::get_string(profile, "kind", "agent.loop");
  s.lang = json::get_string(request, "lang", "en");
  return s;
}

std::string decide_mode(const Json& rules, const std::string& action, const Json& effects) {
  if (rules.is_array()) {
    for (const auto& r : rules) {
      const Json when = r.value("when", Json::object());
      if (!is_effects_match(when.value("effects", Json::object()), effects)) continue;
      if (when.contains("actions")) {
        bool hit = false;
        for (const auto& a : when["actions"]) hit = hit || (a.is_string() && a.get<std::string>() == action);
        if (!hit) continue;
      }
      if (when.contains("prefixes") && !starts_with_any(action, when["prefixes"])) continue;
      const std::string mode = json::get_string(r, "mode");
      if (mode == "run" || mode == "propose" || mode == "deny") return mode;
    }
  }
  return "propose";  // nothing decided: a call that no policy allows waits for the user
}

std::vector<Tool> make_tools(const Json& profile, const Json& catalogue, const std::vector<std::string>& action_ids, const std::string& lang) {
  std::vector<Tool> out;
  std::set<std::string> used;
  const Json name_rule = profile.value("tool_name", Json::object());
  for (const auto& id : action_ids) {
    const Json* entry = json::find(catalogue, id);
    if (!entry || !entry->is_object()) continue;
    Tool t;
    t.action = id;
    std::string base = sanitize(id, name_rule);
    t.name = base;
    for (int n = 2; used.count(t.name); ++n) t.name = base + "_" + std::to_string(n);
    used.insert(t.name);
    std::string title = json::get_string(*entry, "title", id);
    if (const Json* labels = json::find(*entry, "labels"); labels && labels->is_object()) {
      const std::string l = json::get_string(*labels, lang);
      if (!l.empty()) title = l;
    }
    t.description = title;
    const std::string d = json::get_string(*entry, "description");
    if (!d.empty()) t.description += ". " + d;
    t.effects = entry->value("effects", Json::object());
    Json props = Json::object();
    Json required = Json::array();
    if (const Json* params = json::find(*entry, "params"); params && params->is_array()) {
      for (const auto& p : *params) {
        const std::string pn = json::get_string(p, "name");
        if (pn.empty()) continue;
        Json prop{{"type", jtype(json::get_string(p, "type", "string"))}};
        const std::string pd = lang_pick(p.value("description", Json(nullptr)), lang);
        if (!pd.empty()) prop["description"] = pd;
        if (p.contains("enum")) prop["enum"] = p["enum"];
        if (p.contains("default")) prop["default"] = p["default"];
        props[pn] = prop;
        if (json::get_bool(p, "required", false)) required.push_back(pn);
      }
    }
    const Json requires_ = entry->value("requires", Json::array());
    bool needs_element = false;
    for (const auto& r : requires_) needs_element = needs_element || (r.is_string() && r.get<std::string>() != "application");
    t.takes_element = needs_element;
    if (needs_element) props["element"] = Json{{"type", "string"}, {"description", "The id of the element this runs on (a conversation, message ...); the element of the loop when left out."}};
    t.parameters = Json{{"type", "object"}, {"properties", props}};
    if (!required.empty()) t.parameters["required"] = required;
    out.push_back(std::move(t));
  }
  return out;
}

Json wire_tools(const LoopSettings& settings, const std::vector<Tool>& tools) {
  Json out = Json::array();
  if (settings.format_kind == "openai_tools") {
    for (const auto& t : tools)
      out.push_back(Json{{"type", "function"}, {"function", Json{{"name", t.name}, {"description", t.description}, {"parameters", t.parameters}}}});
  } else if (settings.format_kind == "anthropic_tool_use") {
    for (const auto& t : tools) out.push_back(Json{{"name", t.name}, {"description", t.description}, {"input_schema", t.parameters}});
  }
  return out;
}

// ── The loop ────────────────────────────────────────────────────────

namespace {

Json result_message(const LoopSettings& s, const Tool* tool, const std::string& call_name, const std::string& status, bool executed,
                    const std::string& text, const std::string& note) {
  Json j{{"status", status}, {"executed", executed}};
  if (!text.empty()) j["result"] = cut(text, s.result_max_chars);
  if (!note.empty()) j["note"] = note;
  (void)tool;
  (void)call_name;
  return j;
}

std::string notice(const LoopSettings& s, const char* key, const std::map<std::string, std::string>& values) {
  return fill(lang_pick(s.messages.value(key, Json(nullptr)), s.lang), values);
}

}  // namespace

Result<LoopOutcome> run_loop(const Services& services, ModelRequest base, const std::vector<Tool>& tools, ToolHost& host,
                             const LoopSettings& settings, const std::string& element, const LoopHooks& hooks) {
  if (base.models.empty()) return Error(Errc::InvalidArgument, "the tool loop needs a model");
  for (const auto& part : base.inputs)
    if (part.kind != "text" && part.kind != "json") return Error(Errc::InvalidArgument, "the tool loop takes text and json inputs only");

  // The conversation the loop continues: messages, context, inputs, prompt - as use_model would assemble them.
  Json canonical = base.messages.is_array() ? base.messages : Json::array();
  if (!base.context.is_null()) {
    if (!services.resolve_context) return Error(Errc::InvalidArgument, "a context reference needs Services::resolve_context");
    LOOM_TRY_ASSIGN(Json resolved, services.resolve_context(base.context));
    if (!resolved.is_array()) return Error(Errc::InvalidArgument, "context resolved to a non-array");
    for (const auto& m : resolved) canonical.push_back(m);
  }
  for (const auto& part : base.inputs)
    canonical.push_back(Json{{"role", "user"}, {"content", part.kind == "text" ? part.text : json::dump(part.value)}});
  if (!base.prompt.empty()) canonical.push_back(Json{{"role", "user"}, {"content", base.prompt}});

  std::map<std::string, const Tool*> by_name;
  for (const auto& t : tools) by_name[t.name] = &t;

  const bool native = settings.format_kind == "openai_tools" || settings.format_kind == "anthropic_tool_use";
  const bool textual = settings.format_kind == "text";
  Json tools_wire = native ? wire_tools(settings, tools) : Json(nullptr);

  // The tools described in the prompt of a model without native tools.
  std::string preface;
  if (textual && !tools.empty()) {
    preface = fill(lang_pick(settings.format_data.value("instruction", Json(nullptr)), settings.lang),
                   {{"open", json::get_string(settings.format_data, "open", "<tool_call>")},
                    {"close", json::get_string(settings.format_data, "close", "</tool_call>")},
                    {"result_open", json::get_string(settings.format_data, "result_open", "<tool_result>")},
                    {"result_close", json::get_string(settings.format_data, "result_close", "</tool_result>")}});
    for (const auto& t : tools) preface += "\n- " + t.name + ": " + t.description + " arguments: " + json::dump(t.parameters);
  }

  LoopOutcome out;
  out.status = "finished";
  out.usage = Json::object();
  const ModelSpec model = base.models.front();

  for (std::size_t turn = 1;; ++turn) {
    if (settings.max_steps > 0 && static_cast<long>(out.turns) >= settings.max_steps) {
      out.status = "step_limit";
      break;
    }
    ModelRequest req = base;
    req.kind = settings.kind;
    req.models = {model};
    req.mode = "single";
    req.context = nullptr;
    req.inputs.clear();
    req.prompt.clear();
    req.stream = false;
    req.capture = false;
    req.body = nullptr;
    req.body_bytes.clear();
    req.wire_headers.reset();
    req.wants = {"text"};
    req.parameters = base.parameters.is_object() ? base.parameters : Json::object();
    req.parameters.erase("stream");
    Json wire = settings.format_kind == "openai_tools" ? wire_openai(canonical)
                : settings.format_kind == "anthropic_tool_use" ? wire_anthropic(canonical)
                : textual ? wire_text(canonical, settings.format_data) : canonical;
    if (!preface.empty()) {
      Json with = Json::array();
      with.push_back(Json{{"role", "system"}, {"content", preface}});
      for (const auto& m : wire) with.push_back(m);
      wire = with;
    }
    req.messages = wire;
    if (native && !tools_wire.empty()) req.parameters["tools"] = tools_wire;

    auto outcome = use_model(services, std::move(req));
    if (!outcome) return outcome.error();
    if (outcome->results.empty()) {
      out.status = "failed";
      out.error = Error(Errc::Internal, "the model call returned no result");
      break;
    }
    const VariantResult& r = outcome->results.front();
    ++out.turns;
    add_usage(out.usage, r.usage);
    if (hooks.on_turn) hooks.on_turn(turn, r);
    if (r.state == State::Held) {
      out.status = "held";
      out.hold = r.hold;
      --out.turns;  // nothing was sent
      break;
    }
    if (r.state == State::Cancelled) {
      out.status = "cancelled";
      break;
    }
    if (!r.ok()) {
      out.status = "failed";
      if (r.state == State::HttpError) out.error = Error(Errc::Http, "API error " + std::to_string(r.http_status) + ": " + std::string(utf8::prefix(r.body, 500)));
      else out.error = r.error ? *r.error : Error(Errc::Unavailable, "model call " + std::string(to_string(r.state)));
      break;
    }

    std::string text = r.text;
    std::vector<ToolCall> calls = r.tool_calls;
    if (native && calls.empty()) {
      // the model answered in words
    } else if (textual) {
      calls = parse_text_calls(text, settings.format_data, turn);
    }
    out.text = text;
    if (calls.empty()) {  // finished: the answer closes the transcript
      canonical.push_back(Json{{"role", "assistant"}, {"content", text}});
      break;
    }

    Json assistant{{"role", "assistant"}, {"content", text}};
    Json call_list = Json::array();
    for (const auto& c : calls) call_list.push_back(Json{{"id", c.id}, {"name", c.name}, {"arguments", c.arguments}});
    assistant["tool_calls"] = call_list;
    canonical.push_back(assistant);

    bool stop_after = false;
    for (const auto& c : calls) {
      StepRecord rec;
      rec.turn = turn;
      rec.tool_call_id = c.id;
      rec.tool = c.name;
      rec.arguments = c.arguments;
      Json content;
      HostResult hr;
      bool have_hr = false;
      auto it = by_name.find(c.name);
      if (it == by_name.end()) {
        rec.mode = "invalid";
        rec.status = "unknown_tool";
        content = result_message(settings, nullptr, c.name, "error", false, "", notice(settings, "unknown_tool", {{"name", c.name}}));
      } else if (!c.valid) {
        rec.action = it->second->action;
        rec.mode = "invalid";
        rec.status = "bad_arguments";
        content = result_message(settings, it->second, c.name, "error", false, "", notice(settings, "bad_arguments", {{"name", c.name}}));
      } else {
        const Tool& tool = *it->second;
        rec.action = tool.action;
        rec.mode = decide_mode(settings.rules, tool.action, tool.effects);
        Json args = c.arguments;
        std::string el = element;
        if (tool.takes_element) {
          if (const Json* e = json::find(args, "element"); e && e->is_string() && !e->get<std::string>().empty()) el = e->get<std::string>();
          args.erase("element");
        }
        if (rec.mode == "deny") {
          rec.status = "denied";
          content = result_message(settings, &tool, c.name, "denied", false, "", notice(settings, "denied", {{"reason", "policy " + settings.policy_id}}));
        } else {
          auto res = host.call(tool, args, el, rec.mode == "propose");
          if (!res) {
            rec.status = "failed";
            content = result_message(settings, &tool, c.name, "failed", false, "", notice(settings, "failed", {{"reason", res.error().message}}));
          } else {
            hr = std::move(*res);
            have_hr = true;
            rec.status = hr.status;
            rec.run_id = hr.run_id;
            if (hr.status == "completed") {
              content = result_message(settings, &tool, c.name, "completed", true, hr.text, "");
            } else if (hr.status == "failed" || hr.status == "denied" || hr.status == "rejected" || hr.status == "cancelled") {
              content = result_message(settings, &tool, c.name, hr.status, false, "",
                                       notice(settings, hr.status == "denied" ? "denied" : "failed", {{"reason", hr.reason}}));
            } else {
              // proposed | awaiting_confirmation | awaiting_usage | awaiting_client: nothing ran, the user has to answer
              const bool held = hr.status == "awaiting_usage";
              content = result_message(settings, &tool, c.name, hr.status, false, "",
                                       held ? notice(settings, "held", {}) : notice(settings, "proposed", {{"run_id", hr.run_id}}));
              out.proposals.push_back(Json{{"action", tool.action}, {"arguments", args}, {"run_id", hr.run_id}, {"status", hr.status},
                                           {"tool_call_id", c.id}, {"element", el}});
              if (settings.on_proposal == "stop") stop_after = true;
            }
          }
        }
      }
      out.steps.push_back(rec);
      if (hooks.on_step) hooks.on_step(rec, have_hr ? &hr : nullptr);
      canonical.push_back(Json{{"role", "tool"}, {"tool_call_id", c.id}, {"name", c.name}, {"content", json::dump(content)}});
    }
    if (stop_after) {
      out.status = "needs_confirmation";
      break;
    }
  }
  out.messages = canonical;
  log::debug(kLog, "tool loop {} after {} turns, {} calls", out.status, out.turns, out.steps.size());
  return out;
}

}  // namespace loom::llm::tools
