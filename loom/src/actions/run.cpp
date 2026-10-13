// The interpreter of composite actions: plan, gate, run, pause, resume, cancel (see loom/action_run.h).
#include "loom/action_run.h"

#include <algorithm>
#include <cmath>

#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom::actions {

namespace {

std::vector<std::string> strings_of(const Json* v) {
  std::vector<std::string> out;
  if (v && v->is_array())
    for (const auto& x : *v)
      if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}

bool contains(const std::vector<std::string>& list, const std::string& v) { return std::find(list.begin(), list.end(), v) != list.end(); }

std::string sha_of(const Json& value) { return Sha256::hex(json::canonical(value)); }

Json parts_json(const std::vector<llm::Part>& parts) {
  Json out = Json::array();
  for (const auto& p : parts) out.push_back(p.to_json());
  return out;
}

Result<std::vector<llm::Part>> parts_from(const Json& value) {
  std::vector<llm::Part> out;
  if (!value.is_array()) return out;
  for (const auto& j : value) {
    LOOM_TRY_ASSIGN(llm::Part p, llm::Part::from_json(j));
    out.push_back(std::move(p));
  }
  return out;
}

std::string text_of(const std::vector<llm::Part>& parts) {
  std::string out;
  for (const auto& p : parts) {
    if (p.kind != "text") continue;
    if (!out.empty()) out += "\n\n";
    out += p.text;
  }
  return out;
}

Json error_json(const Error& e) { return Json{{"code", std::string(errc_name(e.code))}, {"message", e.message}}; }

// ── Plan building ───────────────────────────────────────────────────

constexpr const char* kUserKinds[] = {"text", "image", "audio", "video", "embedding", "file_ref"};

bool takes_content(const Json& entry) {
  for (const auto& kind : strings_of(json::find(entry, "inputs")))
    for (const char* k : kUserKinds)
      if (kind == k) return true;
  return false;
}

// Replaces {"$arg": name} by the argument spec of the invoking step (the arguments of a composite are
// the `with` of the step that invokes it); without a spec the placeholders stay for the run's arguments.
Json substitute_args(const Json& node, const Json& spec) {
  if (spec.is_null()) return node;
  if (node.is_object()) {
    if (node.size() >= 1 && json::find(node, "$arg") && node.at("$arg").is_string()) {
      const std::string name = node.at("$arg").get<std::string>();
      if (const Json* given = json::find(spec, name)) return *given;
      return node;  // not given by the invoker: falls back to the run's argument of that name
    }
    Json out = Json::object();
    for (auto it = node.begin(); it != node.end(); ++it) out[it.key()] = substitute_args(it.value(), spec);
    return out;
  }
  if (node.is_array()) {
    Json out = Json::array();
    for (const auto& x : node) out.push_back(substitute_args(x, spec));
    return out;
  }
  return node;
}

using RefMap = std::function<std::string(const std::string&)>;

// Local step ids of a composite become the global ids of the plan (a composite sibling stands for its result step).
Json rewrite_when(const Json& cond, const RefMap& map) {
  if (!cond.is_object()) return cond;
  Json out = Json::object();
  for (auto it = cond.begin(); it != cond.end(); ++it) {
    const std::string& k = it.key();
    if ((k == "step" || k == "output") && it.value().is_string()) out[k] = map(it.value().get<std::string>());
    else if ((k == "not") && it.value().is_object()) out[k] = rewrite_when(it.value(), map);
    else if ((k == "all" || k == "any") && it.value().is_array()) {
      Json list = Json::array();
      for (const auto& c : it.value()) list.push_back(rewrite_when(c, map));
      out[k] = list;
    } else out[k] = it.value();
  }
  return out;
}

// {"$text": <local step id>} placeholders follow the same renaming ("input" stays the element's content).
Json rewrite_text_refs(const Json& node, const RefMap& map) {
  if (node.is_object()) {
    if (const Json* t = json::find(node, "$text"); t && t->is_string()) {
      Json out = node;
      if (t->get<std::string>() != "input") out["$text"] = map(t->get<std::string>());
      return out;
    }
    Json out = Json::object();
    for (auto it = node.begin(); it != node.end(); ++it) out[it.key()] = rewrite_text_refs(it.value(), map);
    return out;
  }
  if (node.is_array()) {
    Json out = Json::array();
    for (const auto& x : node) out.push_back(rewrite_text_refs(x, map));
    return out;
  }
  return node;
}

struct Builder {
  const Json& catalogue;
  std::int64_t max_depth = 8;
  Json steps = Json::array();

  // Returns the id of the step whose output is the result of this action.
  Result<std::string> expand(const std::string& action, const Json& with_spec, Json inputs, const std::string& id,
                             std::int64_t depth, std::vector<std::string>& path, const std::string& root,
                             const std::string& item, const Json& step_options) {
    const Json* entry = json::find(catalogue, action);
    if (!entry) return Error(Errc::NotFound, "unknown action '" + action + "'");
    if (std::find(path.begin(), path.end(), action) != path.end())
      return Error(Errc::InvalidArgument, "action '" + action + "' contains itself");
    if (max_depth >= 0 && depth > max_depth) return Error(Errc::InvalidArgument, "actions nest deeper than " + std::to_string(max_depth));
    const Json* children = json::find(*entry, "steps");
    const bool composite = json::get_string(*entry, "kind") == "composite";
    if (composite && (!children || !children->is_array() || children->empty()))
      return Error(Errc::Unsupported, "composite action '" + action + "' has no steps");
    if (composite && !json::get_string(*entry, "consumer").empty())
      return Error(Errc::Unsupported, "action '" + action + "' is carried out by its consumer (" + json::get_string(*entry, "consumer") +
                                          "), not by the interpreter");
    if (!composite) {
      Json leaf = Json::object();
      leaf["id"] = id;
      leaf["item"] = item;
      leaf["action"] = action;
      leaf["root"] = root;
      leaf["with"] = with_spec.is_object() ? with_spec : Json::object();
      leaf["inputs"] = inputs;
      for (const char* k : {"when", "variants", "variant_mode", "on_failure"})
        if (const Json* v = json::find(step_options, k)) leaf[k] = *v;
      steps.push_back(std::move(leaf));
      return id;
    }
    path.push_back(action);
    std::string previous;  // id of the previous sibling's result
    std::set<std::string> local_ids;
    std::map<std::string, std::string> result_of;  // local id of a sibling -> the step holding its result
    const RefMap map = [&](const std::string& local) {
      auto it = result_of.find(local);
      return it != result_of.end() ? it->second : id + "." + local;
    };
    for (std::size_t k = 0; k < children->size(); ++k) {
      const Json& child = (*children)[k];
      const std::string child_action = json::get_string(child, "action");
      if (child_action.empty()) return Error(Errc::InvalidArgument, "a step of '" + action + "' names no action");
      const std::string local = json::get_string(child, "id", "s" + std::to_string(k + 1));
      if (!local_ids.insert(local).second) return Error(Errc::InvalidArgument, "step id '" + local + "' repeats in '" + action + "'");
      const std::string global = id + "." + local;
      Json child_with = rewrite_text_refs(substitute_args(child.value("with", Json::object()), with_spec), map);
      Json child_inputs;
      if (const Json* declared = json::find(child_with, "inputs"); declared && declared->is_array()) {
        child_inputs = Json::array();
        for (const auto& ref : *declared) {
          Json r = ref;
          const std::string from = json::get_string(ref, "from");
          if (from == "input") {
            for (const auto& parent : inputs) child_inputs.push_back(parent);
            continue;
          }
          r["from"] = map(from);
          child_inputs.push_back(r);
        }
        child_with.erase("inputs");
      } else if (previous.empty()) {
        child_inputs = inputs;
      } else {
        child_inputs = Json::array({Json{{"from", previous}}});
      }
      Json options = Json::object();
      if (const Json* w = json::find(child, "when")) options["when"] = rewrite_when(*w, map);
      for (const char* key : {"variants", "variant_mode", "on_failure"})
        if (const Json* v = json::find(child, key)) options[key] = *v;
      LOOM_TRY_ASSIGN(previous, expand(child_action, child_with, child_inputs, global, depth + 1, path, root, item, options));
      result_of[local] = previous;
    }
    path.pop_back();
    // The result of a composite is the step it names (result_from), else the last one.
    const std::string named = json::get_string(*entry, "result_from");
    if (!named.empty()) {
      if (std::none_of(steps.begin(), steps.end(), [&](const Json& s) { return json::get_string(s, "id") == id + "." + named; }))
        return Error(Errc::InvalidArgument, "result_from '" + named + "' of '" + action + "' names no step");
      return id + "." + named;
    }
    return previous;
  }
};

Json add_effects(Json acc, const Json& entry) {
  if (const Json* eff = json::find(entry, "effects"))
    for (auto it = eff->begin(); it != eff->end(); ++it)
      if (it.value().is_boolean()) acc[it.key()] = json::get_bool(acc, it.key(), false) || it.value().get<bool>();
  return acc;
}

// ── Conditions ──────────────────────────────────────────────────────

struct RunView {
  const Json& run;
  const std::set<std::string>& traits;
};

const Json* step_record(const Json& run, const std::string& id) {
  for (const auto& s : run.at("steps"))
    if (json::get_string(s, "id") == id) return &s;
  return nullptr;
}

bool holds(const Json& cond, const RunView& v) {
  if (!cond.is_object() || cond.empty()) return true;
  if (const Json* c = json::find(cond, "not")) return !holds(*c, v);
  if (const Json* c = json::find(cond, "all")) {
    for (const auto& x : *c)
      if (!holds(x, v)) return false;
    return true;
  }
  if (const Json* c = json::find(cond, "any")) {
    for (const auto& x : *c)
      if (holds(x, v)) return true;
    return false;
  }
  if (const Json* t = json::find(cond, "trait")) return t->is_string() && v.traits.count(t->get<std::string>()) > 0;
  if (const Json* s = json::find(cond, "step")) {
    const Json* rec = s->is_string() ? step_record(v.run, s->get<std::string>()) : nullptr;
    const std::string status = rec ? json::get_string(*rec, "status") : std::string("pending");
    return status == json::get_string(cond, "is", "ok");
  }
  if (const Json* o = json::find(cond, "output")) {
    const Json* rec = o->is_string() ? step_record(v.run, o->get<std::string>()) : nullptr;
    if (!rec) return false;
    const std::string kind = json::get_string(cond, "kind");
    for (const auto& p : rec->value("outputs", Json::array()))
      if (kind.empty() || json::get_string(p, "kind") == kind) return true;
    return false;
  }
  if (const Json* a = json::find(cond, "arg")) {
    const Json* given = a->is_string() ? json::find(v.run.at("request").at("args"), a->get<std::string>()) : nullptr;
    if (const Json* eq = json::find(cond, "equals")) return given && *given == *eq;
    return given != nullptr && !given->is_null();
  }
  return true;  // an unknown condition does not block (R43)
}

// ── Placeholders ────────────────────────────────────────────────────

struct EvalCtx {
  const Json& run;
  const std::function<Result<std::string>(const std::string& ref)>& text_of_ref;
};

Result<Json> evaluate(const Json& node, const EvalCtx& ctx) {
  if (node.is_object()) {
    if (const Json* a = json::find(node, "$arg"); a && a->is_string()) {
      const std::string name = a->get<std::string>();
      if (const Json* given = json::find(ctx.run.at("request").at("args"), name); given && !given->is_null()) return *given;
      if (const Json* d = json::find(node, "default")) return *d;
      return Error(Errc::InvalidArgument, "argument '" + name + "' is required");
    }
    if (const Json* t = json::find(node, "$text"); t && t->is_string()) {
      LOOM_TRY_ASSIGN(std::string text, ctx.text_of_ref(t->get<std::string>()));
      return Json(text);
    }
    Json out = Json::object();
    for (auto it = node.begin(); it != node.end(); ++it) {
      LOOM_TRY_ASSIGN(Json v, evaluate(it.value(), ctx));
      out[it.key()] = std::move(v);
    }
    return out;
  }
  if (node.is_array()) {
    Json out = Json::array();
    for (const auto& x : node) {
      LOOM_TRY_ASSIGN(Json v, evaluate(x, ctx));
      out.push_back(std::move(v));
    }
    return out;
  }
  return node;
}

Json merged(Json base, const Json& over) {
  if (over.is_object())
    for (auto it = over.begin(); it != over.end(); ++it) base[it.key()] = it.value();
  return base;
}

std::string receipt_of(const Json& run, const std::string& kind, const Json& ids) {
  return "arr_" + Sha256::hex(json::get_string(run, "id") + "|" + kind + "|" + json::canonical(ids) + "|" +
                              sha_of(run.at("plan")))
                      .substr(0, 20);
}

std::set<std::string> trait_set(const Json& run) {
  std::set<std::string> out;
  for (const auto& t : strings_of(json::find(run, "element_traits"))) out.insert(t);
  for (const auto& t : strings_of(json::find(run, "traits"))) out.insert(t);
  return out;
}

}  // namespace

// ── Executors, stores ───────────────────────────────────────────────

const Executor* ExecutorTable::find(std::string_view action, const Json& entry) const {
  if (auto it = by_action_.find(action); it != by_action_.end()) return &it->second;
  if (port_ && json::find(entry, "port") && !json::get_string(entry, "port").empty()) return &port_;
  return nullptr;
}

namespace {

class MemoryRunStore final : public RunStore {
 public:
  Status save(const Json& run) override {
    std::lock_guard lk(mu_);
    const std::string id = json::get_string(run, "id");
    if (!docs_.count(id)) order_.push_back(id);
    docs_[id] = run;
    return {};
  }
  Result<std::optional<Json>> load(std::string_view run_id) override {
    std::lock_guard lk(mu_);
    auto it = docs_.find(std::string(run_id));
    if (it == docs_.end()) return std::optional<Json>{};
    return std::optional<Json>(it->second);
  }
  Result<std::vector<Json>> list(const Json& filter) override {
    std::lock_guard lk(mu_);
    std::vector<Json> out;
    const std::string status = json::get_string(filter, "status");
    const std::string conv = json::get_string(filter, "conv_id");
    const std::int64_t limit = json::get_int(filter, "limit", 100);
    for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
      const Json& d = docs_.at(*it);
      if (!status.empty() && json::get_string(d, "status") != status) continue;
      if (!conv.empty() && json::get_string(d.value("request", Json::object()).value("context", Json::object()), "conv_id") != conv) continue;
      out.push_back(d);
      if (limit > 0 && static_cast<std::int64_t>(out.size()) >= limit) break;
    }
    return out;
  }

 private:
  std::mutex mu_;
  std::map<std::string, Json> docs_;
  std::vector<std::string> order_;
};

}  // namespace

std::unique_ptr<RunStore> make_memory_run_store() { return std::make_unique<MemoryRunStore>(); }

// ── RunRequest ──────────────────────────────────────────────────────

Result<RunRequest> RunRequest::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "request must be a JSON object");
  RunRequest r;
  r.action = json::get_string(j, "action");
  if (const Json* c = json::find(j, "chain"); c && !c->is_null()) r.chain = *c;
  if (r.action.empty() == r.chain.is_null()) return Error(Errc::InvalidArgument, "name exactly one of action and chain");
  if (const Json* e = json::find(j, "element")) r.element = *e;
  if (const Json* s = json::find(j, "scope")) r.scope = *s;
  if (const Json* a = json::find(j, "args")) {
    if (!a->is_object()) return Error(Errc::InvalidArgument, "args must be an object");
    r.args = *a;
  }
  if (const Json* c = json::find(j, "context")) {
    if (!c->is_object()) return Error(Errc::InvalidArgument, "context must be an object");
    r.context = *c;
  }
  r.propose = json::get_bool(j, "propose", false);
  r.confirmed = json::get_bool(j, "confirmed", false);
  r.ref = json::get_string(j, "ref");
  return r;
}

// ── Planning ────────────────────────────────────────────────────────

Result<Json> Interpreter::plan_of(const RunRequest& request) const {
  if (!deps_.engine) return Error(Errc::Internal, "the interpreter has no rule engine");
  const Json& catalogue = deps_.engine->catalogue();
  const Json* depth_setting = json::find(deps_.settings, "max_depth");
  Builder builder{catalogue, depth_setting && depth_setting->is_null() ? -1 : json::get_int(deps_.settings, "max_depth", 8)};
  Json items = Json::array();
  Json chain_items = Json::array();
  std::string root_label;
  if (!request.action.empty()) {
    chain_items.push_back(Json{{"action", request.action}});
    root_label = request.action;
  } else {
    const Json* list = &request.chain;
    if (request.chain.is_object()) list = json::find(request.chain, "steps");  // a propose_chain chain object
    if (!list || !list->is_array() || list->empty()) return Error(Errc::InvalidArgument, "chain must be a non-empty array of steps");
    for (const auto& s : *list) {
      const std::string a = s.is_string() ? s.get<std::string>() : json::get_string(s, "action");
      if (a.empty()) return Error(Errc::InvalidArgument, "a chain step names no action");
      Json item{{"action", a}};
      if (s.is_object())
        if (const Json* w = json::find(s, "with")) item["with"] = *w;
      chain_items.push_back(item);
    }
    root_label = "chain";
  }
  std::string previous_result;
  double cost = 0;
  std::int64_t model_calls = 0;
  Json effects = Json::object();
  for (std::size_t i = 0; i < chain_items.size(); ++i) {
    const Json& ci = chain_items[i];
    const std::string action = ci.at("action").get<std::string>();
    const Json* entry = json::find(catalogue, action);
    if (!entry) return Error(Errc::NotFound, "unknown action '" + action + "'");
    const std::string item_id = request.action.empty() ? "c" + std::to_string(i + 1) : "a1";
    Json inputs = previous_result.empty() ? Json::array({Json{{"from", "input"}}}) : Json::array({Json{{"from", previous_result}}});
    const Json with = ci.value("with", Json::object());
    if (const Json* declared = json::find(with, "inputs"); declared && declared->is_array()) {
      inputs = *declared;  // an explicit choice of the caller wins over the default flow
    }
    const std::size_t first_leaf = builder.steps.size();
    std::vector<std::string> path;
    Json options = Json::object();
    Json item_with = with;
    if (item_with.is_object()) item_with.erase("inputs");
    // An atomic action's request is its `with` overlaid on the run's arguments (a composite's steps use $arg).
    if (json::get_string(*entry, "kind") != "composite" && request.chain.is_null()) item_with = merged(request.args, item_with);
    // The entry decides whether the default content flows into the step (atomic leaf) or only into children.
    if (json::get_string(*entry, "kind") != "composite" && !takes_content(*entry)) inputs = Json::array();
    LOOM_TRY_ASSIGN(std::string result, builder.expand(action, item_with, inputs, item_id, 0, path, root_label == "chain" ? action : request.action, item_id, options));
    previous_result = result;
    Json leaves = Json::array();
    for (std::size_t k = first_leaf; k < builder.steps.size(); ++k) {
      Json& leaf = builder.steps[k];
      // Children of a composite take the content only when their own action reads content.
      const Json* le = json::find(catalogue, json::get_string(leaf, "action"));
      if (le && !takes_content(*le) && !leaf["inputs"].empty()) {
        bool only_input = true;
        for (const auto& r : leaf["inputs"])
          if (json::get_string(r, "from") != "input") only_input = false;
        if (only_input) leaf["inputs"] = Json::array();
      }
      leaves.push_back(json::get_string(leaf, "id"));
      if (json::get_string(leaf, "action") == "use_model") {
        const Json* variants = json::find(leaf, "variants");
        model_calls += variants && variants->is_array() && !variants->empty() ? static_cast<std::int64_t>(variants->size()) : 1;
      }
      effects = add_effects(effects, le ? *le : Json::object());
    }
    if (!leaves.empty()) builder.steps[builder.steps.size() - 1]["result"] = true;
    Json produced = entry->value("produces", Json::array());
    if (!leaves.empty()) builder.steps[builder.steps.size() - 1]["produces"] = produced;
    const double item_cost = json::get_number(*entry, "cost", 0.0);
    cost += item_cost;
    effects = add_effects(effects, *entry);
    Json item{{"id", item_id}, {"action", action}, {"title", json::get_string(*entry, "title", action)},
              {"kind", json::get_string(*entry, "kind")}, {"implementation", json::get_string(*entry, "status", "planned")},
              {"cost", item_cost}, {"confidence", json::get_number(*entry, "confidence", 1.0)},
              {"requires", entry->value("requires", Json::array())}, {"produces", produced}, {"leaves", leaves},
              {"result", result}, {"with", with}};
    if (const Json* l = json::find(*entry, "labels")) item["labels"] = *l;
    items.push_back(std::move(item));
  }
  Json plan = Json::object();
  plan["root"] = root_label;
  plan["items"] = items;
  plan["steps"] = builder.steps;
  plan["cost"] = Json{{"declared", std::round(cost * 1e6) / 1e6}, {"unit", "relative"}, {"model_calls", model_calls}};
  plan["effects"] = effects;
  return plan;
}

Result<std::set<std::string>> Interpreter::element_traits(const Json& element, const Json& scope) const {
  LOOM_TRY_ASSIGN(auto view, deps_.engine->actions_for(element, scope, Json{{"include_unmatched", true}}));
  std::set<std::string> traits;
  for (const auto& t : strings_of(json::find(view.at("element"), "traits"))) traits.insert(t);
  return traits;
}

// Gate of one action for the element: the rules of the action and of every name it lists in gate_as;
// the most restrictive decision wins (the action itself on a tie).
static Result<Json> gate_one(const Engine& engine, const Element& element, const std::string& action, const Json& entry,
                             const Scope& scope) {
  LOOM_TRY_ASSIGN(Decision best, engine.decide(element, action, scope));
  for (const auto& alias : strings_of(json::find(entry, "gate_as"))) {
    LOOM_TRY_ASSIGN(Decision d, engine.decide(element, alias, scope));
    if (engine.restrictiveness(d.decision) > engine.restrictiveness(best.decision)) best = std::move(d);
  }
  Json g = Json::object();
  g["decision"] = best.decision;
  g["status"] = best.status;
  g["rule"] = best.rule;
  g["effect"] = best.effect;
  if (!best.reason.is_null()) g["reason"] = best.reason;
  if (!best.initiator.empty()) g["initiator"] = best.initiator;
  return g;
}

Result<Json> Interpreter::gate_plan(const Json& plan, const Json& element_json, const Json& scope_json,
                                    const std::set<std::string>& base_traits) const {
  const Engine& engine = *deps_.engine;
  LOOM_TRY_ASSIGN(Element element, engine.parse_element(element_json));
  LOOM_TRY_ASSIGN(Scope scope, engine.parse_scope(scope_json));
  const Json& catalogue = engine.catalogue();
  Json gates = Json::array();
  std::set<std::string> traits = base_traits;
  for (const auto& item : plan.at("items")) {
    const std::string item_id = item.at("id").get<std::string>();
    Element current = element;
    for (const auto& t : traits) current.own_traits.insert(t);
    std::set<std::string> seen;
    auto add_gate = [&](const std::string& action, const std::string& owner) -> Status {
      if (!seen.insert(action).second) return {};
      const Json* entry = json::find(catalogue, action);
      LOOM_TRY_ASSIGN(Json g, gate_one(engine, current, action, entry ? *entry : Json::object(), scope));
      g["id"] = item_id + ":" + action;
      g["item"] = item_id;
      g["action"] = action;
      g["for"] = owner;
      g["title"] = entry ? json::get_string(*entry, "title", action) : action;
      gates.push_back(std::move(g));
      return {};
    };
    LOOM_TRY(add_gate(item.at("action").get<std::string>(), item_id));
    for (const auto& step : plan.at("steps"))
      if (json::get_string(step, "item") == item_id) LOOM_TRY(add_gate(json::get_string(step, "action"), json::get_string(step, "id")));
    for (const auto& t : engine.expand_traits(strings_of(json::find(item, "produces")))) traits.insert(t);
  }
  return gates;
}

Result<Json> Interpreter::preview(const RunRequest& request) const {
  LOOM_TRY_ASSIGN(Json plan, plan_of(request));
  LOOM_TRY_ASSIGN(auto base_traits, element_traits(request.element, request.scope));
  LOOM_TRY_ASSIGN(Json gates, gate_plan(plan, request.element, request.scope, base_traits));
  Json confirm = Json::array(), denied = Json::array(), not_runnable = Json::array();
  for (const auto& g : gates) {
    if (g.at("decision") == "confirm") confirm.push_back(g);
    if (g.at("decision") == "deny") denied.push_back(g);
  }
  const Json& catalogue = deps_.engine->catalogue();
  for (const auto& item : plan.at("items")) {
    if (item.at("implementation") != "implemented") not_runnable.push_back(Json{{"action", item.at("action")}, {"why", "planned"}});
  }
  for (const auto& step : plan.at("steps")) {
    const Json* entry = json::find(catalogue, json::get_string(step, "action"));
    if (!entry) continue;
    if (json::get_string(*entry, "status") != "implemented")
      not_runnable.push_back(Json{{"action", step.at("action")}, {"why", "planned"}});
    else if (!deps_.executors || !deps_.executors->find(json::get_string(step, "action"), *entry))
      not_runnable.push_back(Json{{"action", step.at("action")}, {"why", "no_executor"}});
  }
  Json out = Json::object();
  out["schema"] = "loom.action_preview/1";
  out["plan"] = plan;
  out["gates"] = gates;
  out["needs_confirmation"] = confirm;
  out["denied"] = denied;
  out["not_runnable"] = not_runnable;
  out["runnable"] = denied.empty() && not_runnable.empty();
  return out;
}

// ── Running ─────────────────────────────────────────────────────────

Json run_summary(const Json& run) {
  Json s = Json::object();
  for (const char* k : {"id", "status", "created", "updated"})
    if (const Json* v = json::find(run, k)) s[k] = *v;
  if (const Json* p = json::find(run, "plan")) {
    Json actions = Json::array();
    for (const auto& item : p->value("items", Json::array())) actions.push_back(item.value("action", ""));
    s["actions"] = actions;
  }
  if (const Json* e = json::find(run, "error"); e && !e->is_null()) s["error"] = *e;
  return s;
}

namespace {

void stamp(Json& run, const std::string& status) {
  run["status"] = status;
  run["updated"] = timeutil::utc_now_iso();
}

}  // namespace

Result<Json> Interpreter::start(const RunRequest& request, const CancelToken* cancel) {
  if (!deps_.engine || !deps_.store) return Error(Errc::Internal, "the interpreter is not wired");
  LOOM_TRY_ASSIGN(Json plan, plan_of(request));
  const Engine& engine = *deps_.engine;
  const Json& catalogue = engine.catalogue();

  // Fit: every item needs the traits it requires from the element and from the items before it.
  LOOM_TRY_ASSIGN(auto base_traits, element_traits(request.element, request.scope));
  {
    std::set<std::string> traits = base_traits;
    for (const auto& item : plan.at("items")) {
      std::vector<std::string> missing;
      for (const auto& t : strings_of(json::find(item, "requires")))
        if (!traits.count(t)) missing.push_back(t);
      if (!missing.empty()) {
        std::string list;
        for (const auto& m : missing) list += (list.empty() ? "" : ", ") + m;
        return Error(Errc::InvalidArgument, "action '" + item.at("action").get<std::string>() + "' does not fit this element: missing " + list +
                                                "; actions.explain and actions.propose_chain say what would supply it");
      }
      for (const auto& t : engine.expand_traits(strings_of(json::find(item, "produces")))) traits.insert(t);
    }
  }
  // Runnable: implemented, with an executor for every leaf. Nothing starts when part of it cannot.
  for (const auto& item : plan.at("items"))
    if (item.at("implementation") != "implemented")
      return Error(Errc::Unavailable, "action '" + item.at("action").get<std::string>() + "' is planned, not implemented");
  for (const auto& step : plan.at("steps")) {
    const std::string action = step.at("action").get<std::string>();
    const Json* entry = json::find(catalogue, action);
    if (!entry || json::get_string(*entry, "status") != "implemented")
      return Error(Errc::Unavailable, "action '" + action + "' is planned, not implemented");
    if (!deps_.executors || !deps_.executors->find(action, *entry))
      return Error(Errc::Unavailable, "action '" + action + "' has no executor in this build");
  }

  LOOM_TRY_ASSIGN(Json gates, gate_plan(plan, request.element, request.scope, base_traits));

  Json run = Json::object();
  run["schema"] = std::string(kRunSchema);
  run["id"] = request.run_id.empty() ? gen_id("ar_") : request.run_id;
  run["created"] = timeutil::utc_now_iso();
  Json req = Json::object();
  if (!request.action.empty()) req["action"] = request.action;
  else req["chain"] = request.chain;
  req["element"] = request.element;
  req["scope"] = request.scope;
  req["args"] = request.args;
  req["context"] = request.context;
  run["request"] = req;
  run["element"] = request.element;
  run["element_traits"] = Json(std::vector<std::string>(base_traits.begin(), base_traits.end()));
  run["plan"] = plan;
  run["gates"] = gates;
  run["approved"] = Json::array();
  run["cursor"] = 0;
  run["traits"] = Json::array();
  run["steps"] = Json::array();
  run["pending"] = nullptr;
  run["result"] = nullptr;
  run["error"] = nullptr;
  run["confirmations"] = Json::array();

  Json denied = Json::array();
  Json to_confirm = Json::array();
  for (const auto& g : gates) {
    if (g.at("decision") == "deny") denied.push_back(g);
    else if (g.at("decision") == "confirm") to_confirm.push_back(g);
  }
  if (!denied.empty()) {
    const Json& first = denied[0];
    stamp(run, "denied");
    run["error"] = Json{{"code", "denied"}, {"message", "the rules do not allow '" + first.value("action", "") + "' here"},
                        {"gates", denied}};
    LOOM_TRY(deps_.store->save(run));
    return run;
  }
  if (request.confirmed) {
    for (const auto& g : to_confirm) run["approved"].push_back(g.at("id"));
    run["confirmations"].push_back(Json{{"at", timeutil::utc_now_iso()}, {"ref", request.ref}, {"approved", true},
                                        {"kind", "request"}, {"gates", run["approved"]}});
    to_confirm = Json::array();
  }
  auto pending_of = [&](const std::string& kind, const Json& list) {
    Json ids = Json::array();
    for (const auto& g : list) ids.push_back(g.at("id"));
    Json p = Json::object();
    p["schema"] = std::string(kPendingSchema);
    p["kind"] = kind;
    p["receipt_id"] = receipt_of(run, kind, ids);
    p["gates"] = list;
    p["resume"] = "actions.confirm {run_id, approved, receipt_id, ref}";
    return p;
  };
  if (request.propose) {
    // The user has not said yes to anything yet: whatever the rules would ask is asked together with this.
    Json all = to_confirm;
    stamp(run, "proposed");
    run["pending"] = pending_of("proposal", all);
    LOOM_TRY(deps_.store->save(run));
    return run;
  }
  if (!to_confirm.empty()) {
    stamp(run, "awaiting_confirmation");
    run["pending"] = pending_of("confirmation", to_confirm);
    LOOM_TRY(deps_.store->save(run));
    return run;
  }
  stamp(run, "running");
  LOOM_TRY(deps_.store->save(run));
  return drive(std::move(run), cancel, Json::object());
}

Result<std::vector<llm::Part>> Interpreter::inputs_of(const Json& run, const Json& step) const {
  std::vector<llm::Part> out;
  for (const auto& ref : step.value("inputs", Json::array())) {
    const std::string from = json::get_string(ref, "from");
    if (from == "input") {
      if (!deps_.element_content) continue;
      LOOM_TRY_ASSIGN(auto content, deps_.element_content(run.at("element")));
      for (auto& p : content) out.push_back(std::move(p));
      continue;
    }
    const Json* rec = step_record(run, from);
    if (!rec) return Error(Errc::InvalidArgument, "input of step '" + step.value("id", "") + "' refers to '" + from + "', which has not run");
    const std::string variant = json::get_string(ref, "variant");
    if (variant.empty()) {
      LOOM_TRY_ASSIGN(auto parts, parts_from(rec->value("outputs", Json::array())));
      for (auto& p : parts) out.push_back(std::move(p));
    } else {
      for (const auto& v : rec->value("variants", Json::array()))
        if (json::get_string(v, "label") == variant) {
          LOOM_TRY_ASSIGN(auto parts, parts_from(v.value("parts", Json::array())));
          for (auto& p : parts) out.push_back(std::move(p));
        }
    }
  }
  return out;
}

Result<Json> Interpreter::run_step(Json& run, std::size_t index, const CancelToken* cancel, const Json& usage_confirmation) {
  const Json step = run.at("plan").at("steps").at(index);
  const std::string id = step.at("id").get<std::string>();
  const std::string action = step.at("action").get<std::string>();
  const Json* entry = json::find(deps_.engine->catalogue(), action);
  const std::set<std::string> traits = trait_set(run);

  // A step held by the usage guard continues where it stopped.
  Json rec;
  std::size_t first_variant = 0;
  if (index < run.at("steps").size()) {
    rec = run["steps"][index];
    first_variant = static_cast<std::size_t>(json::get_int(rec, "next_variant", 0));
  } else {
    rec = Json::object();
    rec["schema"] = std::string(kStepSchema);
    rec["run_id"] = run.at("id");
    rec["id"] = id;
    rec["action"] = action;
    rec["item"] = step.value("item", "");
    rec["started"] = timeutil::utc_now_iso();
    rec["variants"] = Json::array();
    rec["outputs"] = Json::array();
    for (const auto& g : run.at("gates"))
      if (g.value("id", "") == step.value("item", "") + ":" + action)
        rec["decision"] = Json{{"gate", g.at("id")}, {"status", g.value("status", "")}, {"rule", g.value("rule", "")},
                               {"approved", std::find(run.at("approved").begin(), run.at("approved").end(), g.at("id")) != run.at("approved").end()}};
  }
  auto write = [&](const std::string& status) {
    rec["status"] = status;
    if (index < run["steps"].size()) run["steps"][index] = rec;
    else run["steps"].push_back(rec);
  };

  if (first_variant == 0 && rec.value("status", "") != "held") {
    if (const Json* when = json::find(step, "when"); when && !holds(*when, RunView{run, traits})) {
      rec["finished"] = timeutil::utc_now_iso();
      write("skipped");
      return rec;
    }
  }

  auto inputs = inputs_of(run, step);
  if (!inputs) {
    rec["finished"] = timeutil::utc_now_iso();
    rec["error"] = error_json(inputs.error());
    write("failed");
    return rec;
  }
  EvalCtx ctx{run, [&](const std::string& ref) -> Result<std::string> {
                if (ref == "input") {
                  LOOM_TRY_ASSIGN(auto content, deps_.element_content ? deps_.element_content(run.at("element")) : Result<std::vector<llm::Part>>(std::vector<llm::Part>{}));
                  return text_of(content);
                }
                const Json* r = step_record(run, ref);
                if (!r) return Error(Errc::InvalidArgument, "$text refers to '" + ref + "', which has not run");
                LOOM_TRY_ASSIGN(auto parts, parts_from(r->value("outputs", Json::array())));
                return text_of(parts);
              }};
  auto base_with = evaluate(step.value("with", Json::object()), ctx);
  if (!base_with) {
    rec["finished"] = timeutil::utc_now_iso();
    rec["error"] = error_json(base_with.error());
    write("failed");
    return rec;
  }

  Json variants = step.value("variants", Json::array());
  const bool multi = variants.is_array() && !variants.empty();
  if (!multi) variants = Json::array({Json::object()});
  const bool first_success = json::get_string(step, "variant_mode", "all") == "first_success";
  const Executor* executor = deps_.executors ? deps_.executors->find(action, entry ? *entry : Json::object()) : nullptr;
  if (!executor) {
    rec["finished"] = timeutil::utc_now_iso();
    rec["error"] = error_json(Error(Errc::Unavailable, "action '" + action + "' has no executor"));
    write("failed");
    return rec;
  }
  bool any_ok = false;
  for (const auto& v : rec["variants"])
    if (json::get_string(v, "state") == "ok") any_ok = true;
  for (std::size_t j = first_variant; j < variants.size(); ++j) {
    if (first_success && any_ok) break;
    if (cancel && cancel->cancelled()) {
      rec["finished"] = timeutil::utc_now_iso();
      write("cancelled");
      return rec;
    }
    const Json& variant = variants[j];
    const std::string label = multi ? json::get_string(variant, "label", "v" + std::to_string(j + 1)) : std::string();
    StepCall call;
    call.run_id = run.at("id").get<std::string>();
    call.step_id = id;
    call.action = action;
    call.root_action = step.value("root", "");
    call.variant = label;
    auto variant_with = evaluate(variant.value("with", Json::object()), ctx);
    if (!variant_with) {
      rec["finished"] = timeutil::utc_now_iso();
      rec["error"] = error_json(variant_with.error());
      write("failed");
      return rec;
    }
    call.with = merged(*base_with, *variant_with);
    call.inputs = *inputs;
    call.element = run.at("element");
    call.args = run.at("request").at("args");
    call.cancel = cancel;
    if (j == first_variant && rec.value("status", "") == "held") call.usage_confirmation = usage_confirmation;
    StepOutcome out = (*executor)(call);
    Json vrec = Json::object();
    vrec["label"] = label;
    switch (out.state) {
      case StepOutcome::State::Ok: vrec["state"] = "ok"; break;
      case StepOutcome::State::Failed: vrec["state"] = "failed"; break;
      case StepOutcome::State::Held: vrec["state"] = "held"; break;
      case StepOutcome::State::Cancelled: vrec["state"] = "cancelled"; break;
      case StepOutcome::State::Unavailable: vrec["state"] = "unavailable"; break;
    }
    vrec["parts"] = parts_json(out.parts);
    if (!out.data.is_null()) vrec["data"] = out.data;
    vrec["provenance"] = out.provenance;
    if (out.error) vrec["error"] = error_json(*out.error);
    if (out.state == StepOutcome::State::Held) {
      rec["next_variant"] = j;
      rec["hold"] = out.hold;
      rec["pending_variant"] = vrec;
      write("held");
      return rec;
    }
    rec.erase("hold");
    rec.erase("pending_variant");
    rec["variants"].push_back(vrec);
    rec["next_variant"] = j + 1;
    if (out.state == StepOutcome::State::Cancelled) {
      rec["finished"] = timeutil::utc_now_iso();
      write("cancelled");
      return rec;
    }
    if (out.state == StepOutcome::State::Ok) any_ok = true;
  }
  // The step's outputs: the parts of every variant that worked, in order.
  Json outputs = Json::array();
  Json provenance = Json::array();
  for (const auto& v : rec["variants"]) {
    if (json::get_string(v, "state") == "ok")
      for (const auto& p : v.value("parts", Json::array())) outputs.push_back(p);
    for (const auto& pr : v.value("provenance", Json::array())) provenance.push_back(pr);
    if (v.contains("error")) rec["error"] = v["error"];
  }
  rec["outputs"] = outputs;
  rec["provenance"] = provenance;
  rec["inputs_sha256"] = sha_of(parts_json(*inputs));
  rec["outputs_sha256"] = sha_of(outputs);
  rec["finished"] = timeutil::utc_now_iso();
  if (any_ok) rec.erase("error");
  write(any_ok ? "ok" : "failed");
  return rec;
}

Result<Json> Interpreter::drive(Json run, const CancelToken* cancel, const Json& usage_confirmation) {
  const std::string run_id = run.at("id").get<std::string>();
  const Json steps = run.at("plan").at("steps");
  auto checkpoint = [&](const std::string& status) -> Status {
    stamp(run, status);
    return deps_.store->save(run);
  };
  bool resumed = false;
  while (static_cast<std::size_t>(json::get_int(run, "cursor", 0)) < steps.size()) {
    if (cancel && cancel->cancelled()) {
      LOOM_TRY(checkpoint("cancelled"));
      return run;
    }
    const std::size_t index = static_cast<std::size_t>(json::get_int(run, "cursor", 0));
    Json confirmation = resumed ? Json::object() : usage_confirmation;
    resumed = true;
    LOOM_TRY_ASSIGN(Json rec, run_step(run, index, cancel, confirmation));
    const std::string status = rec.value("status", "");
    if (deps_.on_step && status != "held") deps_.on_step(rec);
    if (status == "held") {
      const Json hold = rec.value("hold", Json::object());
      Json ids = Json::array({steps[index].at("id")});
      Json p = Json::object();
      p["schema"] = std::string(kPendingSchema);
      p["kind"] = "usage";
      p["step"] = steps[index].at("id");
      p["action"] = steps[index].at("action");
      p["receipt_id"] = hold.value("receipt_id", Json(nullptr));
      p["operation_id"] = hold.value("operation_id", Json(nullptr));
      p["usage_pending"] = hold;
      p["resume"] = "actions.confirm {run_id, approved, receipt_id, ref}";
      run["pending"] = p;
      LOOM_TRY(checkpoint("awaiting_usage"));
      return run;
    }
    if (status == "cancelled") {
      LOOM_TRY(checkpoint("cancelled"));
      return run;
    }
    if (status == "ok") {
      for (const auto& t : deps_.engine->expand_traits(strings_of(json::find(steps[index], "produces"))))
        if (!contains(strings_of(json::find(run, "traits")), t)) run["traits"].push_back(t);
    }
    if (status == "failed" && json::get_string(steps[index], "on_failure", "stop") != "continue") {
      run["error"] = rec.value("error", Json{{"code", "failed"}, {"message", "step '" + steps[index].value("id", "") + "' failed"}});
      run["error"]["step"] = steps[index].at("id");
      LOOM_TRY(checkpoint("failed"));
      return run;
    }
    run["cursor"] = static_cast<std::int64_t>(index) + 1;
    LOOM_TRY(checkpoint("running"));
  }
  // The result: the output of the last item's result step (or of the last step that produced anything).
  Json result = Json::object();
  const Json items = run.at("plan").at("items");
  if (!items.empty()) {
    const std::string wanted = items[items.size() - 1].value("result", "");
    const Json* rec = step_record(run, wanted);
    if (!rec || rec->value("outputs", Json::array()).empty()) {
      rec = nullptr;
      for (auto it = run.at("steps").rbegin(); it != run.at("steps").rend(); ++it)
        if (!it->value("outputs", Json::array()).empty()) {
          rec = &*it;
          break;
        }
    }
    if (rec) {
      result["from"] = rec->value("id", "");
      result["parts"] = rec->value("outputs", Json::array());
    } else {
      result["parts"] = Json::array();
    }
    // The structured data an atomic action returned (storage.status, ...) travels in the variants.
    if (rec)
      for (const auto& v : rec->value("variants", Json::array()))
        if (v.contains("data") && !result.contains("data")) result["data"] = v["data"];
  }
  run["result"] = result;
  LOOM_TRY(checkpoint("completed"));
  return run;
}

// ── confirm / cancel / get ──────────────────────────────────────────

Result<Json> Interpreter::get(std::string_view run_id) const {
  if (!deps_.store) return Error(Errc::Internal, "the interpreter is not wired");
  LOOM_TRY_ASSIGN(auto run, deps_.store->load(run_id));
  if (!run) return Error(Errc::NotFound, "no run '" + std::string(run_id) + "'");
  return *run;
}

struct Interpreter::Claim {
  Interpreter& self;
  std::string id;
  bool held = false;
  Claim(Interpreter& s, std::string_view run_id) : self(s), id(run_id) {
    std::lock_guard lk(self.mu_);
    held = self.claimed_.insert(id).second;
  }
  ~Claim() {
    if (!held) return;
    std::lock_guard lk(self.mu_);
    self.claimed_.erase(id);
  }
};

Result<Json> Interpreter::confirm(std::string_view run_id, const Json& answer, const CancelToken* cancel) {
  if (!answer.is_object()) return Error(Errc::InvalidArgument, "the answer must be an object {approved, receipt_id, ref}");
  Claim claim(*this, run_id);
  if (!claim.held) return Error(Errc::Busy, "run '" + std::string(run_id) + "' is executing right now");
  LOOM_TRY_ASSIGN(Json run, get(run_id));
  const std::string status = json::get_string(run, "status");
  if (status != "proposed" && status != "awaiting_confirmation" && status != "awaiting_usage")
    return Error(Errc::Conflict, "run '" + std::string(run_id) + "' is " + status + "; nothing is waiting for an answer");
  const Json pending = run.value("pending", Json(nullptr));
  if (!pending.is_object()) return Error(Errc::Conflict, "run has no pending question");
  const std::string receipt = json::get_string(answer, "receipt_id");
  if (receipt.empty()) return Error(Errc::InvalidArgument, "receipt_id is required (see pending.receipt_id)");
  if (receipt != json::get_string(pending, "receipt_id"))
    return Error(Errc::Conflict, "receipt_id does not match the pending question of this run");
  const bool approved = json::get_bool(answer, "approved", false);
  const std::string ref = json::get_string(answer, "ref");
  const std::string kind = json::get_string(pending, "kind");
  run["confirmations"].push_back(Json{{"at", timeutil::utc_now_iso()}, {"ref", ref}, {"approved", approved}, {"kind", kind},
                                      {"receipt_id", receipt}});
  if (kind == "usage") {
    const Json confirmation = Json{{"approved", approved}, {"receipt_id", receipt}, {"ref", ref},
                                   {"operation_id", pending.value("operation_id", Json(nullptr))}};
    if (!approved) {
      // The guard records the denial; nothing is sent.
      const std::size_t index = static_cast<std::size_t>(json::get_int(run, "cursor", 0));
      (void)run_step(run, index, cancel, confirmation);
      stamp(run, "rejected");
      run["pending"] = nullptr;
      run["error"] = Json{{"code", "rejected"}, {"message", "the usage confirmation was declined; nothing was sent"}};
      LOOM_TRY(deps_.store->save(run));
      return run;
    }
    run["pending"] = nullptr;
    stamp(run, "running");
    LOOM_TRY(deps_.store->save(run));
    return drive(std::move(run), cancel, confirmation);
  }
  if (!approved) {
    stamp(run, "rejected");
    run["pending"] = nullptr;
    run["error"] = Json{{"code", "rejected"}, {"message", "declined; nothing was executed"}};
    LOOM_TRY(deps_.store->save(run));
    return run;
  }
  // Rule confirmations (and the user's go on a proposal) cover the gates that were shown.
  for (const auto& g : pending.value("gates", Json::array()))
    if (std::find(run["approved"].begin(), run["approved"].end(), g.at("id")) == run["approved"].end()) run["approved"].push_back(g.at("id"));
  // A proposal also covers every gate that asks for confirmation, even ones the pending list did not carry.
  for (const auto& g : run.at("gates"))
    if (g.value("decision", "") == "confirm" && std::find(run["approved"].begin(), run["approved"].end(), g.at("id")) == run["approved"].end())
      run["approved"].push_back(g.at("id"));
  run["pending"] = nullptr;
  stamp(run, "running");
  LOOM_TRY(deps_.store->save(run));
  return drive(std::move(run), cancel, Json::object());
}

Result<Json> Interpreter::cancel(std::string_view run_id, std::string_view ref) {
  Claim claim(*this, run_id);
  if (!claim.held) return Error(Errc::Busy, "run '" + std::string(run_id) + "' is executing; cancel its token");
  LOOM_TRY_ASSIGN(Json run, get(run_id));
  const std::string status = json::get_string(run, "status");
  if (status == "completed" || status == "failed" || status == "denied" || status == "rejected" || status == "cancelled")
    return Error(Errc::Conflict, "run '" + std::string(run_id) + "' is already " + status);
  run["confirmations"].push_back(Json{{"at", timeutil::utc_now_iso()}, {"ref", std::string(ref)}, {"approved", false}, {"kind", "cancel"}});
  run["pending"] = nullptr;
  run["error"] = Json{{"code", "cancelled"}, {"message", "cancelled; later steps did not run"}};
  stamp(run, "cancelled");
  LOOM_TRY(deps_.store->save(run));
  return run;
}

}  // namespace loom::actions
