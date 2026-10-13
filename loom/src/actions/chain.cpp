// Extrapolation (GRAF_HUB_KOD_SILNIK §5, wave 2b): when the element lacks a trait, look for chains of
// actions that supply it. A chain is a proposal with its declared cost and confidence; nothing here
// executes an action. Bounds come from the extrapolation presets in data/runtime/action_rules.pack
// (max chain length, max cost, min confidence, max results; null = no bound; `off` disables).
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

#include "loom/action_rules.h"

namespace loom::actions {

namespace {

constexpr double kEps = 1e-9;

std::vector<std::string> strings_of(const Json* v) {
  std::vector<std::string> out;
  if (v && v->is_array())
    for (const auto& x : *v)
      if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}

double rounded(double v) { return std::round(v * 1e6) / 1e6; }

struct Bounds {
  std::optional<long> max_length;
  std::optional<double> max_cost;
  std::optional<double> min_confidence;
  std::optional<long> max_results;
};

Json bounds_json(const Bounds& b) {
  return Json{{"max_length", b.max_length ? Json(*b.max_length) : Json(nullptr)},
              {"max_cost", b.max_cost ? Json(*b.max_cost) : Json(nullptr)},
              {"min_confidence", b.min_confidence ? Json(*b.min_confidence) : Json(nullptr)},
              {"max_results", b.max_results ? Json(*b.max_results) : Json(nullptr)}};
}

// A threshold from a preset or a call: absent = keep, null = no bound, number = bound.
Status read_bound(const Json& source, const char* key, bool integer, std::optional<double>& out) {
  const Json* v = json::find(source, key);
  if (!v) return {};
  if (v->is_null()) {
    out.reset();
    return {};
  }
  if (!v->is_number() || (integer && !v->is_number_integer()))
    return Error(Errc::InvalidArgument, std::string(key) + " must be " + (integer ? "an integer" : "a number") + " or null");
  out = v->get<double>();
  return {};
}

struct Cand {
  std::string id;
  const Json* entry = nullptr;
  std::set<std::string> requires_;
  std::set<std::string> produces;  // expanded
  double cost = 0;
  double confidence = 1;
};

struct Step {
  const Cand* cand = nullptr;
  bool is_target = false;
  std::string status;
};

struct Found {
  std::vector<Step> steps;
  double cost = 0;
  double confidence = 1;
};

bool subset(const std::set<std::string>& need, const std::set<std::string>& have) {
  return std::all_of(need.begin(), need.end(), [&](const std::string& t) { return have.count(t) > 0; });
}

}  // namespace

Result<Json> Engine::chains(const Prepared& p, const Json& target, const Scope& scope, const Json& options) const {
  const std::string goal_trait = json::get_string(target, "trait");
  const std::string goal_action = json::get_string(target, "action");
  if (goal_trait.empty() == goal_action.empty())
    return Error(Errc::InvalidArgument, "target must name exactly one of trait and action");
  const Json* goal_entry = nullptr;
  if (!goal_action.empty()) {
    goal_entry = json::find(actions_, goal_action);
    if (!goal_entry) return Error(Errc::NotFound, "unknown action '" + goal_action + "'");
  }

  // The preset and every bound come through the scope chain (R47, the same one the rules use): the installation's
  // preset (data), the user's settings, then rules of any level; the most specific scope wins and the answer
  // says which scope set each value. A present key in the call overrides all of it.
  Json resolved = Json::object();
  const std::string preset_setting = json::get_string(extrapolation_, "setting");
  const std::string bounds_setting = json::get_string(extrapolation_, "bounds_setting");
  const bool has_person = !level_name("person").empty();
  std::string preset_name;
  std::string preset_level = "installation";
  std::string preset_scope_id;
  const bool call_preset = options.is_object() && json::find(options, "preset") != nullptr;
  if (call_preset) {
    preset_name = json::get_string(options, "preset");
    preset_level = scope.level;  // the call speaks at the level of the question: its preset's bounds always apply
    resolved["preset"] = Json{{"value", preset_name}, {"defined", true}, {"scope", preset_level}, {"source", "call"}};
  } else {
    std::vector<Baseline> bl;
    if (const std::string active = json::get_string(extrapolation_, "active"); !active.empty() && !level_name("installation").empty())
      bl.push_back({"installation", active, "profile:action_rules.extrapolation.active", 0});
    if (has_person && !preset_setting.empty())
      if (const Json* u = json::find(user_values_, preset_setting); u && u->is_string() && !u->get<std::string>().empty())
        bl.push_back({"person", *u, "setting:" + preset_setting, 0});
    LOOM_TRY_ASSIGN(ScopedValue sv, resolve_value("extrapolation.preset", scope, bl));
    if (sv.defined && sv.value.is_string()) preset_name = sv.value.get<std::string>();
    if (sv.defined) {
      preset_level = sv.scope;
      preset_scope_id = sv.scope_id;
      resolved["preset"] = sv.to_json();
    } else {
      // A question asked above the installation (the engine, the application preset) sees no baseline: the profile's
      // active preset still answers it, as it did before values were scoped.
      preset_name = json::get_string(extrapolation_, "active");
      preset_level = scope.level;
      resolved["preset"] = Json{{"value", preset_name}, {"defined", !preset_name.empty()}, {"scope", scope.level},
                                {"source", "profile:action_rules.extrapolation.active"}, {"fallback", true}};
    }
  }
  const Json* presets = json::find(extrapolation_, "presets");
  const Json* preset = presets ? json::find(*presets, preset_name) : nullptr;
  if (!preset) {
    std::string by = " (no preset is set)";
    if (const Json* r = json::find(resolved, "preset"); r && json::get_string(*r, "source") != "")
      by = " (set by " + json::get_string(*r, "source") + (json::get_string(*r, "scope").empty() ? "" : " at " + json::get_string(*r, "scope")) + ")";
    return Error(Errc::InvalidArgument, "unknown extrapolation preset '" + preset_name + "'" + by);
  }
  Json out = Json::object();
  out["schema"] = "loom.actions_chain/1";
  out["proposal_only"] = true;  // chains are proposals; nothing is executed
  out["preset"] = preset_name;
  out["target"] = target;
  out["scope"] = scope_json(scope);
  out["resolved"] = resolved;
  if (!json::get_bool(*preset, "enabled", true)) {
    out["enabled"] = false;
    out["chains"] = Json::array();
    return out;
  }
  out["enabled"] = true;
  Bounds bounds;
  {
    struct BoundSpec {
      const char* name;
      bool integer;
    };
    for (const BoundSpec spec : {BoundSpec{"max_length", true}, BoundSpec{"max_cost", false}, BoundSpec{"min_confidence", false},
                                 BoundSpec{"max_results", true}}) {
      std::vector<Baseline> bl;
      if (const Json* pv = json::find(*preset, spec.name); !level_name(preset_level).empty() && pv && !call_preset)
        bl.push_back({preset_level, *pv, "preset:" + preset_name, 0, preset_scope_id});
      if (has_person && !bounds_setting.empty() && !call_preset)  // a preset named in the call is taken as it is
        if (const Json* u = json::find(user_values_, bounds_setting); u && u->is_object())
          if (const Json* uv = json::find(*u, spec.name)) bl.push_back({"person", *uv, "setting:" + bounds_setting, 1});
      ScopedValue sv;
      if (!call_preset) {
        LOOM_TRY_ASSIGN(sv, resolve_value(std::string("extrapolation.") + spec.name, scope, bl));
      }
      std::optional<double> v;
      if (sv.defined) {
        LOOM_TRY(read_bound(Json{{spec.name, sv.value}}, spec.name, spec.integer, v));
        Json r = sv.to_json();
        if (sv.source.rfind("preset:", 0) == 0) r["preset"] = preset_name;
        resolved[spec.name] = r;
      } else if (const Json* pv = json::find(*preset, spec.name)) {  // the preset's own bound, whatever level the question is at
        LOOM_TRY(read_bound(*preset, spec.name, spec.integer, v));
        resolved[spec.name] = Json{{"value", *pv}, {"defined", true}, {"scope", scope.level}, {"source", "preset:" + preset_name}, {"preset", preset_name}};
      }
      if (options.is_object())
        if (const Json* c = json::find(options, spec.name)) {
          LOOM_TRY(read_bound(options, spec.name, spec.integer, v));
          resolved[spec.name] = Json{{"value", *c}, {"defined", true}, {"scope", scope.level}, {"source", "call"}};
        }
      if (std::string(spec.name) == "max_length") { if (v) bounds.max_length = static_cast<long>(*v); }
      else if (std::string(spec.name) == "max_cost") bounds.max_cost = v;
      else if (std::string(spec.name) == "min_confidence") bounds.min_confidence = v;
      else if (v) bounds.max_results = static_cast<long>(*v);
    }
  }
  out["resolved"] = resolved;
  out["bounds"] = bounds_json(bounds);

  std::set<std::string> start;
  for (const auto& [t, from] : p.traits) start.insert(t);

  // Candidates: every action that produces something. The target action is only ever the last step.
  std::vector<Cand> cands;
  for (auto it = actions_.begin(); it != actions_.end(); ++it) {
    if (it.key() == goal_action) continue;
    const auto produces = strings_of(json::find(it.value(), "produces"));
    if (produces.empty()) continue;
    Cand c;
    c.id = it.key();
    c.entry = &it.value();
    for (const auto& t : strings_of(json::find(it.value(), "requires"))) c.requires_.insert(t);
    std::map<std::string, std::string> expanded;
    for (const auto& t : produces) add_trait(expanded, t, "produced");
    for (const auto& [t, from] : expanded) c.produces.insert(t);
    c.cost = json::get_number(it.value(), "cost", 0.0);
    c.confidence = json::get_number(it.value(), "confidence", 1.0);
    cands.push_back(std::move(c));
  }
  Cand goal;
  if (goal_entry) {
    goal.id = goal_action;
    goal.entry = goal_entry;
    for (const auto& t : strings_of(json::find(*goal_entry, "requires"))) goal.requires_.insert(t);
    goal.cost = json::get_number(*goal_entry, "cost", 0.0);
    goal.confidence = json::get_number(*goal_entry, "confidence", 1.0);
  }

  // Traits that can lead to the goal: the goal itself and, backwards, whatever the actions producing
  // them require. A step is only worth proposing if it adds one of these that the element lacks.
  std::set<std::string> needed;
  if (goal_entry) needed = goal.requires_;
  else needed.insert(goal_trait);
  for (bool grown = true; grown;) {
    grown = false;
    for (const auto& c : cands)
      if (std::any_of(c.produces.begin(), c.produces.end(), [&](const std::string& t) { return needed.count(t) > 0; }))
        for (const auto& t : c.requires_) grown = needed.insert(t).second || grown;
  }
  auto gains = [&](const Cand& c, const std::set<std::string>& state) {
    return std::any_of(c.produces.begin(), c.produces.end(),
                       [&](const std::string& t) { return needed.count(t) > 0 && state.count(t) == 0; });
  };

  auto step_json = [&](const Step& s) {
    const Json& a = *s.cand->entry;
    Json o{{"action", s.cand->id}, {"title", json::get_string(a, "title", s.cand->id)},
           {"implementation", json::get_string(a, "status", "planned")}, {"status", s.status},
           {"content_op", json::get_string(a, "content_op", "none")}, {"form_op", json::get_string(a, "form_op", "none")},
           {"cost", s.cand->cost}, {"confidence", s.cand->confidence}};
    if (const Json* v = json::find(a, "produces")) o["produces"] = *v;
    return o;
  };

  // Direct answers first.
  Json issues = Json::array();
  if (goal_entry) {
    if (subset(goal.requires_, start)) {
      Found f;
      const Decision d = decide_prepared(p, goal_action, scope);
      f.steps.push_back({&goal, true, d.status});
      f.cost = goal.cost;
      f.confidence = goal.confidence;
      out["direct"] = true;
      out["already_present"] = false;
      Json chain{{"steps", Json::array({step_json(f.steps[0])})}, {"length", 1}, {"cost", rounded(f.cost)}, {"confidence", rounded(f.confidence)}};
      out["chains"] = Json::array({chain});
      out["rejected"] = Json::array();
      return out;
    }
  } else {
    if (!traits_.contains(goal_trait) && !classes_.contains(goal_trait))
      issues.push_back(Json{{"code", "trait_unknown"}, {"trait", goal_trait}});
    if (start.count(goal_trait)) {
      out["direct"] = false;
      out["already_present"] = true;
      out["chains"] = Json::array();
      out["issues"] = issues;
      return out;
    }
  }

  Json rejected = Json::array();
  std::set<std::string> rejected_seen;
  auto synthetic = [&](const std::set<std::string>& state) {
    Prepared sp;
    for (const auto& t : state) sp.traits.emplace(t, "produced");
    sp.element.own_traits = state;
    return sp;
  };
  // Gate of one step under the rules of the scope; a denied step is not proposed.
  auto gate = [&](const Prepared& pr, const std::string& id, std::string* status) {
    const Decision d = decide_prepared(pr, id, scope);
    if (status) *status = d.status;
    if (d.decision == "deny") {
      if (rejected_seen.insert(id + "|" + d.rule).second) {
        Json r{{"action", id}, {"status", d.status}, {"rule", d.rule}};
        if (!d.reason.is_null()) r["reason"] = d.reason;
        if (!d.initiator.empty()) r["initiator"] = d.initiator;
        rejected.push_back(r);
      }
      return false;
    }
    return true;
  };

  auto search = [&](const Bounds& b, bool dominance) {
    std::vector<Found> found;
    std::map<std::set<std::string>, std::vector<std::pair<double, double>>> seen_states;
    std::vector<Step> path;
    std::set<std::string> used;
    std::vector<std::set<std::string>> on_path{start};
    const long reserve = goal_entry ? 1 : 0;  // room for the target action as the last step
    std::function<void(const Prepared&, const std::set<std::string>&, double, double)> dfs =
        [&](const Prepared& pr, const std::set<std::string>& state, double cost, double conf) {
          if (goal_entry && !path.empty() && subset(goal.requires_, state)) {
            std::string status;
            const double nc = cost + goal.cost, nf = conf * goal.confidence;
            if (b.max_length && static_cast<long>(path.size()) + 1 > *b.max_length) return;
            if (b.max_cost && nc > *b.max_cost + kEps) return;
            if (b.min_confidence && nf < *b.min_confidence - kEps) return;
            if (!gate(pr, goal.id, &status)) return;
            Found f{path, nc, nf};
            f.steps.push_back({&goal, true, status});
            found.push_back(std::move(f));
            return;  // longer chains through more intermediate steps would only add cost
          }
          if (b.max_length && static_cast<long>(path.size()) + 1 + reserve > *b.max_length) return;
          for (const auto& c : cands) {
            if (used.count(c.id) || !subset(c.requires_, state)) continue;
            const double nc = cost + c.cost, nf = conf * c.confidence;
            if (b.max_cost && nc > *b.max_cost + kEps) continue;
            if (b.min_confidence && nf < *b.min_confidence - kEps) continue;
            if (!gains(c, state)) continue;  // adds nothing the goal needs
            if (std::find(on_path.begin(), on_path.end(), c.produces) != on_path.end()) continue;
            std::string status;
            if (!gate(pr, c.id, &status)) continue;
            if (dominance) {  // unbounded length: keep the best route to each reachable state
              auto& visits = seen_states[c.produces];
              bool dominated = std::any_of(visits.begin(), visits.end(), [&](const std::pair<double, double>& v) {
                return v.first <= nc + kEps && v.second >= nf - kEps;
              });
              if (dominated) continue;
              visits.emplace_back(nc, nf);
            }
            path.push_back({&c, false, status});
            used.insert(c.id);
            on_path.push_back(c.produces);
            if (!goal_entry && c.produces.count(goal_trait)) {
              found.push_back(Found{path, nc, nf});
            } else {
              const Prepared next = synthetic(c.produces);
              dfs(next, c.produces, nc, nf);
            }
            on_path.pop_back();
            used.erase(c.id);
            path.pop_back();
          }
        };
    dfs(p, start, 0, 1);
    return found;
  };

  auto rank = [&](std::vector<Found>& found) {
    std::vector<std::pair<std::string, bool>> keys;  // (by, descending)
    if (const Json* r = json::find(extrapolation_, "rank_by"); r && r->is_array())
      for (const auto& k : *r) keys.emplace_back(json::get_string(k, "by"), json::get_string(k, "order") == "desc");
    auto ids = [](const Found& f) {
      std::string s;
      for (const auto& st : f.steps) s += st.cand->id + "\n";
      return s;
    };
    std::stable_sort(found.begin(), found.end(), [&](const Found& a, const Found& b) {
      for (const auto& [by, desc] : keys) {
        double x = 0, y = 0;
        if (by == "confidence") { x = a.confidence; y = b.confidence; }
        else if (by == "cost") { x = a.cost; y = b.cost; }
        else if (by == "length") { x = static_cast<double>(a.steps.size()); y = static_cast<double>(b.steps.size()); }
        else continue;
        if (std::fabs(x - y) < kEps) continue;
        return desc ? x > y : x < y;
      }
      return ids(a) < ids(b);  // deterministic
    });
  };

  auto chain_json = [&](const Found& f) {
    Json steps = Json::array();
    Json effects = Json::object();  // the flags of the steps, OR-ed: what the whole chain would do
    Json confirm = Json::array();
    bool executable = true;
    for (const auto& s : f.steps) {
      steps.push_back(step_json(s));
      if (s.status == "needs_confirmation") confirm.push_back(s.cand->id);
      if (json::get_string(*s.cand->entry, "status") != "implemented") executable = false;
      if (const Json* eff = json::find(*s.cand->entry, "effects"))
        for (auto it = eff->begin(); it != eff->end(); ++it)
          if (it.value().is_boolean()) effects[it.key()] = json::get_bool(effects, it.key(), false) || it.value().get<bool>();
    }
    return Json{{"steps", steps}, {"length", f.steps.size()}, {"cost", rounded(f.cost)}, {"confidence", rounded(f.confidence)},
                {"effects", effects}, {"needs_confirmation", confirm}, {"executable", executable}};
  };

  std::vector<Found> found = search(bounds, !bounds.max_length);
  rank(found);
  if (bounds.max_results && static_cast<long>(found.size()) > *bounds.max_results)
    found.resize(static_cast<std::size_t>(std::max(0L, *bounds.max_results)));
  Json chains = Json::array();
  for (const auto& f : found) chains.push_back(chain_json(f));
  out["direct"] = false;
  out["already_present"] = false;
  out["chains"] = chains;
  if (found.empty() && (bounds.max_length || bounds.max_cost || bounds.min_confidence)) {
    // Say what it would take: the best chain without bounds, and which bound it exceeds.
    std::vector<Found> beyond = search(Bounds{}, true);
    rank(beyond);
    if (!beyond.empty()) {
      const Found& f = beyond.front();
      Json exceeds = Json::array();
      if (bounds.max_length && static_cast<long>(f.steps.size()) > *bounds.max_length) exceeds.push_back("max_length");
      if (bounds.max_cost && f.cost > *bounds.max_cost + kEps) exceeds.push_back("max_cost");
      if (bounds.min_confidence && f.confidence < *bounds.min_confidence - kEps) exceeds.push_back("min_confidence");
      Json c = chain_json(f);
      c["exceeds"] = exceeds;
      out["beyond_bounds"] = c;
    }
  }
  out["rejected"] = rejected;
  out["issues"] = issues;
  return out;
}

}  // namespace loom::actions
