// Action/element matching: traits, groups, scoped rules and their conflict resolution.
// Data: graph_entry (ontology: classes, traits, groups; scopes), actions, action_rules profiles.
#include <algorithm>
#include <cmath>
#include <tuple>

#include "loom/action_rules.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"

namespace loom::actions {

namespace {

std::vector<std::string> strings_of(const Json* v) {
  std::vector<std::string> out;
  if (v && v->is_array())
    for (const auto& x : *v)
      if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}

bool reserved_kind(const std::string& k) {
  return k == "class" || k == "trait" || k == "bundle" || k == "node" || k == "any" || k == "action" || k == "action_group";
}

std::string effect_decision(const Json& effects, const std::string& effect) {
  const Json* e = json::find(effects, effect);
  return e ? json::get_string(*e, "decision", "allow") : std::string("allow");
}

}  // namespace

// ── Loading ───────────────────────────────────────────────────────

Result<Engine> Engine::load(const std::filesystem::path& data_dir) {
  LOOM_TRY_ASSIGN(auto entry, RuntimeProfile::load("graph_entry", data_dir));
  LOOM_TRY_ASSIGN(auto catalogue, RuntimeProfile::load("actions", data_dir));
  LOOM_TRY_ASSIGN(auto rules, RuntimeProfile::load("action_rules", data_dir));
  const Json* actions = json::find(catalogue.values(), "actions");
  LOOM_TRY_ASSIGN(Engine engine, from_values(entry.values(), actions ? *actions : Json::object(), rules.values()));
  // The user's settings are the `person` baselines of scoped values; a caller with a live Config replaces them.
  if (!data_dir.empty()) {
    if (auto raw = fsutil::read_file(data_dir / "config.json")) {
      auto parsed = json::parse(*raw);
      if (parsed && parsed->is_object()) engine.user_values_ = std::move(*parsed);
    }
  }
  return engine;
}

Result<Engine> Engine::from_values(const Json& graph_entry, const Json& actions, const Json& action_rules) {
  Engine e;
  auto object_at = [](const Json& parent, const char* key) {
    const Json* v = json::find(parent, key);
    return v && v->is_object() ? *v : Json::object();
  };
  const Json ontology = object_at(graph_entry, "ontology");
  e.classes_ = object_at(ontology, "classes");
  e.traits_ = object_at(ontology, "traits");
  e.groups_ = object_at(ontology, "groups");
  e.relations_ = object_at(ontology, "relations");
  const Json scopes = object_at(graph_entry, "scopes");
  if (const Json* c = json::find(scopes, "chain"); c && c->is_array()) e.scope_chain_ = *c;
  if (const Json* c = json::find(scopes, "conflict_rules"); c && c->is_array()) e.conflict_rules_ = *c;
  e.default_conflict_ = json::get_string(scopes, "default_rule");
  if (!actions.is_object()) return Error(Errc::InvalidArgument, "the action catalogue must be an object keyed by action id");
  e.actions_ = actions;
  e.axes_ = object_at(action_rules, "axes");
  e.decisions_ = object_at(action_rules, "decisions");
  e.effects_ = object_at(action_rules, "effects");
  e.aliases_ = object_at(action_rules, "scope_aliases");
  e.extrapolation_ = object_at(action_rules, "extrapolation");

  std::size_t order = 0;
  const Json rules = object_at(action_rules, "rules");
  for (auto it = rules.begin(); it != rules.end(); ++it) {
    const Json& r = it.value();
    if (!r.is_object() || json::get_bool(r, "disabled", false)) continue;
    Rule rule;
    rule.id = json::get_string(r, "id", it.key());
    rule.order = order++;
    const std::string wanted = json::get_string(r, "scope");
    const std::size_t rank = e.level_index(wanted, &rule.scope);
    if (rank == static_cast<std::size_t>(-1)) {
      e.rule_issues_.push_back(Json{{"code", "rule_scope_unknown"}, {"owner", rule.id}, {"scope", wanted}});
      continue;  // never matches: a rule outside the chain has no place in any scope
    }
    rule.rank = rank;
    rule.scope_id = json::get_string(r, "scope_id");
    if (const Json* t = json::find(r, "target"); t && t->is_object()) rule.target = *t;
    if (const Json* a = json::find(r, "action")) rule.action = *a;
    rule.effect = json::get_string(r, "effect");
    if (!e.effects_.contains(rule.effect))
      e.rule_issues_.push_back(Json{{"code", "rule_effect_unknown"}, {"owner", rule.id}, {"effect", rule.effect}});
    rule.decision = effect_decision(e.effects_, rule.effect);
    const Json* dec = json::find(e.decisions_, rule.decision);
    rule.dimension = dec ? json::get_string(*dec, "dimension", "gate") : std::string("gate");
    if (const Json* v = json::find(r, "reason")) rule.reason = *v;
    rule.initiator = json::get_string(r, "initiator");
    rule.locked = json::get_bool(r, "locked", false);
    rule.weight = json::get_number(r, "weight", 1.0);
    rule.conflict = json::get_string(r, "conflict");
    if (const Json* w = json::find(r, "when"); w && w->is_object()) rule.when = *w;
    if (const Json* v = json::find(r, "value")) {
      rule.value = *v;
      rule.has_value = true;
    }
    e.rules_.push_back(std::move(rule));
  }
  return e;
}

std::set<std::string> Engine::expand_traits(const std::vector<std::string>& names) const {
  std::map<std::string, std::string> expanded;
  for (const auto& n : names) add_trait(expanded, n, "produced");
  std::set<std::string> out;
  for (const auto& [t, from] : expanded) out.insert(t);
  return out;
}

int Engine::restrictiveness(std::string_view decision) const {
  const Json* d = json::find(decisions_, decision);
  return d ? static_cast<int>(json::get_int(*d, "restrictiveness", 0)) : 0;
}

std::size_t Engine::level_index(std::string_view name, std::string* resolved) const {
  std::string level(name);
  if (const Json* a = json::find(aliases_, level); a && a->is_string()) level = a->get<std::string>();
  for (std::size_t i = 0; i < scope_chain_.size(); ++i) {
    if (json::get_string(scope_chain_[i], "id") == level) {
      if (resolved) *resolved = level;
      return i;
    }
  }
  return static_cast<std::size_t>(-1);
}

std::string Engine::level_name(std::string_view name) const {
  std::string level;
  return level_index(name, &level) == static_cast<std::size_t>(-1) ? std::string() : level;
}

// ── Elements, traits, groups ─────────────────────────────────────

Result<Element> Engine::parse_element(const Json& j) const {
  Element e;
  if (j.is_string()) {
    e.cls = j.get<std::string>();
    return e;
  }
  if (!j.is_object()) return Error(Errc::InvalidArgument, "element must be an object {class, traits, attrs, id} or a class id");
  e.cls = json::get_string(j, "class");
  if (e.cls.empty()) e.cls = json::get_string(j, "kind");
  e.id = json::get_string(j, "id");
  if (const Json* t = json::find(j, "traits")) {
    if (!t->is_array()) return Error(Errc::InvalidArgument, "element.traits must be an array of trait ids");
    for (const auto& x : *t) {
      if (!x.is_string()) return Error(Errc::InvalidArgument, "element.traits must contain strings");
      e.own_traits.insert(x.get<std::string>());
    }
  }
  if (const Json* a = json::find(j, "attrs")) {
    if (!a->is_object()) return Error(Errc::InvalidArgument, "element.attrs must be an object");
    e.attrs = *a;
  }
  return e;
}

void Engine::add_trait(std::map<std::string, std::string>& out, const std::string& trait, const std::string& source) const {
  std::vector<std::pair<std::string, std::string>> stack{{trait, source}};
  while (!stack.empty()) {
    auto [t, from] = stack.back();
    stack.pop_back();
    if (t.empty() || !out.emplace(t, from).second) continue;
    const Json* klass = json::find(classes_, t);
    if (!klass) continue;  // a plain trait; a class id also stands for its parent and declared traits
    const std::string parent = json::get_string(*klass, "parent");
    if (!parent.empty()) stack.emplace_back(parent, "class:" + t);
    for (const auto& declared : strings_of(json::find(*klass, "traits"))) stack.emplace_back(declared, "class:" + t);
  }
}

Engine::Prepared Engine::prepare(const Element& element) const {
  Prepared p;
  p.element = element;
  std::set<std::string> seen;
  for (std::string c = element.cls; !c.empty() && seen.insert(c).second;) {
    p.lineage.push_back(c);
    const Json* klass = json::find(classes_, c);
    if (!klass) break;
    c = json::get_string(*klass, "parent");
  }
  p.class_known = !element.cls.empty() && json::find(classes_, element.cls) != nullptr;
  for (const auto& t : element.own_traits) add_trait(p.traits, t, "node");
  if (!element.cls.empty()) add_trait(p.traits, element.cls, "class:" + element.cls);
  return p;
}

int Engine::class_depth(const std::string& cls) const {
  int depth = 0;
  std::set<std::string> seen;
  for (std::string c = cls; seen.insert(c).second;) {
    const Json* klass = json::find(classes_, c);
    if (!klass) break;
    c = json::get_string(*klass, "parent");
    if (c.empty()) break;
    ++depth;
  }
  return depth;
}

bool Engine::in_group(const std::string& group, const std::map<std::string, std::string>& traits) const {
  const Json* g = json::find(groups_, group);
  if (!g) return false;
  const auto bundle = strings_of(json::find(*g, "traits"));
  if (bundle.empty()) return false;
  const bool any = json::get_string(*g, "element_match", "all") == "any";
  for (const auto& t : bundle) {
    const bool has = traits.count(t) > 0;
    if (any && has) return true;
    if (!any && !has) return false;
  }
  return !any;
}

bool Engine::action_in_group(const std::string& group, const Json& action) const {
  const Json* g = json::find(groups_, group);
  if (!g) return false;
  const auto bundle = strings_of(json::find(*g, "traits"));
  const auto flags = strings_of(json::find(*g, "effect_flags"));
  const auto content_ops = strings_of(json::find(*g, "content_ops"));
  const auto form_ops = strings_of(json::find(*g, "form_ops"));
  if (bundle.empty() && flags.empty() && content_ops.empty() && form_ops.empty()) return false;
  auto contains = [](const std::vector<std::string>& list, const std::string& v) {
    return std::find(list.begin(), list.end(), v) != list.end();
  };
  if (!bundle.empty()) {  // the action requires at least one trait of the bundle
    const auto requires_ = strings_of(json::find(action, "requires"));
    if (!std::any_of(requires_.begin(), requires_.end(), [&](const std::string& t) { return contains(bundle, t); })) return false;
  }
  if (!flags.empty()) {
    const Json* effects = json::find(action, "effects");
    if (!effects || !std::any_of(flags.begin(), flags.end(), [&](const std::string& f) { return json::get_bool(*effects, f, false); }))
      return false;
  }
  if (!content_ops.empty() && !contains(content_ops, json::get_string(action, "content_op", "none"))) return false;
  if (!form_ops.empty() && !contains(form_ops, json::get_string(action, "form_op", "none"))) return false;
  return true;
}

std::vector<std::string> Engine::groups_of_action(const Json& action) const {
  std::vector<std::string> out;
  for (auto it = groups_.begin(); it != groups_.end(); ++it)
    if (action_in_group(it.key(), action)) out.push_back(it.key());
  return out;
}

// ── Scopes ────────────────────────────────────────────────────────

Result<Scope> Engine::parse_scope(const Json& j) const {
  Scope s;
  if (scope_chain_.empty()) return s;
  s.depth = scope_chain_.size() - 1;  // default: the most specific level, every unbound rule applies
  s.level = json::get_string(scope_chain_[s.depth], "id");
  if (j.is_null() || (j.is_string() && j.get<std::string>().empty()) || (j.is_object() && j.empty())) return s;
  if (j.is_string()) {
    std::string level;
    const std::size_t i = level_index(j.get<std::string>(), &level);
    if (i == static_cast<std::size_t>(-1)) return Error(Errc::InvalidArgument, "unknown scope level '" + j.get<std::string>() + "'");
    s.depth = i;
    s.level = level;
    return s;
  }
  if (!j.is_object()) return Error(Errc::InvalidArgument, "scope must be a level id or an object {level: instance id}");
  std::size_t deepest = 0;
  bool any = false;
  for (auto it = j.begin(); it != j.end(); ++it) {
    std::string level;
    const std::size_t i = level_index(it.key(), &level);
    if (i == static_cast<std::size_t>(-1)) return Error(Errc::InvalidArgument, "unknown scope level '" + it.key() + "'");
    if (!it.value().is_string()) return Error(Errc::InvalidArgument, "scope instance of '" + it.key() + "' must be a string id");
    s.context[level] = it.value();
    if (!any || i > deepest) deepest = i;
    any = true;
  }
  s.depth = deepest;
  s.level = json::get_string(scope_chain_[deepest], "id");
  return s;
}

Json Engine::scope_json(const Scope& s) const {
  Json active = Json::array();
  for (std::size_t i = 0; i <= s.depth && i < scope_chain_.size(); ++i) active.push_back(json::get_string(scope_chain_[i], "id"));
  return Json{{"level", s.level}, {"context", s.context}, {"active", active}};
}

// ── Rule matching ─────────────────────────────────────────────────

int Engine::match_element(const Json& target, const Prepared& p) const {
  const std::string kind = json::get_string(target, "kind");
  const std::string id = json::get_string(target, "id");
  int spec = 0;
  bool shorthand = false;
  if (kind.empty() || kind == "any" || kind == "action" || kind == "action_group") {
    spec = 0;
  } else if (kind == "trait") {
    if (!p.traits.count(id)) return -1;
    spec = 5;
  } else if (kind == "bundle") {
    if (!in_group(id, p.traits)) return -1;
    spec = 6;
  } else if (kind == "node") {
    if (id.empty() || p.element.id != id) return -1;
    spec = 1000;
  } else {
    shorthand = !reserved_kind(kind);
    const std::string cls = shorthand ? kind : id;
    if (std::find(p.lineage.begin(), p.lineage.end(), cls) == p.lineage.end()) return -1;
    spec = 10 + class_depth(cls);
  }
  auto attr_ok = [&](const std::string& key, const Json& want) {
    const Json* have = json::find(p.element.attrs, key);
    return have && *have == want;
  };
  if (const Json* attrs = json::find(target, "attrs"); attrs && attrs->is_object()) {
    for (auto it = attrs->begin(); it != attrs->end(); ++it) {
      if (!attr_ok(it.key(), it.value())) return -1;
      spec += 20;
    }
  }
  if (shorthand) {  // {kind: <class>, <attr>: <value>, ...}: the remaining keys are attributes
    for (auto it = target.begin(); it != target.end(); ++it) {
      if (it.key() == "kind" || it.key() == "id" || it.key() == "attrs") continue;
      if (!attr_ok(it.key(), it.value())) return -1;
      spec += 20;
    }
  }
  return spec;
}

int Engine::match_action(const Rule& rule, std::string_view action, const Json* entry) const {
  int spec = 0;
  auto select = [&](const Json& sel) -> int {  // -1 = does not select this action
    if (sel.is_null()) return 0;
    if (sel.is_string()) {
      const std::string s = sel.get<std::string>();
      if (s.empty() || s == "*") return 0;
      return s == action ? 3 : -1;
    }
    if (sel.is_object()) {
      if (const std::string g = json::get_string(sel, "group"); !g.empty())
        return entry && action_in_group(g, *entry) ? 2 : -1;
      if (const std::string f = json::get_string(sel, "flag"); !f.empty()) {
        const Json* effects = entry ? json::find(*entry, "effects") : nullptr;
        return effects && json::get_bool(*effects, f, false) ? 1 : -1;
      }
    }
    return -1;
  };
  const std::string kind = json::get_string(rule.target, "kind");
  if (kind == "action") {
    const int s = select(Json(json::get_string(rule.target, "id")));
    if (s < 0) return -1;
    spec = std::max(spec, s);
  } else if (kind == "action_group") {
    const int s = select(Json{{"group", json::get_string(rule.target, "id")}});
    if (s < 0) return -1;
    spec = std::max(spec, s);
  }
  const int s = select(rule.action);
  if (s < 0) return -1;
  return std::max(spec, s);
}

bool Engine::active(const Rule& rule, const Scope& scope) const {
  if (rule.rank > scope.depth) return false;  // a level below the one asked about
  if (!rule.scope_id.empty()) {
    const Json* c = json::find(scope.context, rule.scope);
    if (!c || !c->is_string() || c->get<std::string>() != rule.scope_id) return false;
  }
  if (const Json* ctx = json::find(rule.when, "context"); ctx && ctx->is_object()) {
    for (auto it = ctx->begin(); it != ctx->end(); ++it) {
      std::string level;
      if (level_index(it.key(), &level) == static_cast<std::size_t>(-1)) return false;
      const Json* have = json::find(scope.context, level);
      if (!have || *have != it.value()) return false;
    }
  }
  return true;
}

// ── Conflict resolution (the conflict rules of graph/scopes.pack) ────

namespace {

struct Cand {
  const Engine::Rule* rule;
  int spec;            // specificity of target and action selectors
  std::string key;     // the competing outcome: a gate decision id, or a default action id
  int restrict_;       // restrictiveness of the outcome (higher = stricter)
};

struct Pick {
  std::size_t winner = 0;
  std::string conflict;     // conflict rule applied ("" = nothing competed)
  std::string unknown;      // a named conflict rule that has no implementation
  bool asked = false;       // the rule `ask`: the candidates disagree
  bool merged = false;      // the rule `merge`: reasons are combined
};

using Key4 = std::tuple<long, long, long, long>;

// General -> specific order key: more specific scope, then target/action selector, stricter, earlier defined.
Key4 specific(const Cand& c) {
  return {static_cast<long>(c.rule->rank), c.spec, c.restrict_, -static_cast<long>(c.rule->order)};
}

template <class F>
std::size_t best(const std::vector<Cand>& c, F key) {
  std::size_t b = 0;
  for (std::size_t i = 1; i < c.size(); ++i)
    if (key(c[i]) > key(c[b])) b = i;
  return b;
}

Pick resolve(const std::vector<Cand>& c, const std::string& chain_default, const std::set<std::string>& implemented) {
  Pick pk;
  if (c.size() <= 1) return pk;
  // The conflict rule that governs: the most general candidate that names one (higher scopes govern lower
  // ones, as in locked_by), else the chain default.
  std::string governing;
  std::size_t gi = 0;
  for (std::size_t i = 0; i < c.size(); ++i)
    if (!c[i].rule->conflict.empty() &&
        (governing.empty() || std::make_pair(c[i].rule->rank, c[i].rule->order) < std::make_pair(c[gi].rule->rank, c[gi].rule->order))) {
      governing = c[i].rule->conflict;
      gi = i;
    }
  if (governing.empty()) governing = chain_default;
  if (!implemented.count(governing)) {
    if (!governing.empty()) pk.unknown = governing;
    governing = "most_specific_wins";
  }
  pk.conflict = governing;

  if (governing == "most_specific_wins") {
    pk.winner = best(c, specific);
  } else if (governing == "locked_by") {
    std::vector<std::size_t> locked;
    for (std::size_t i = 0; i < c.size(); ++i)
      if (c[i].rule->locked) locked.push_back(i);
    if (locked.empty()) {
      pk.winner = best(c, specific);
    } else {  // the most general pin wins over every lower scope
      std::size_t b = locked[0];
      auto k = [&](std::size_t i) { return Key4{-static_cast<long>(c[i].rule->rank), c[i].spec, c[i].restrict_, -static_cast<long>(c[i].rule->order)}; };
      for (std::size_t i : locked)
        if (k(i) > k(b)) b = i;
      pk.winner = b;
    }
  } else if (governing == "first_defined") {
    pk.winner = best(c, [](const Cand& x) { return Key4{-static_cast<long>(x.rule->rank), -static_cast<long>(x.rule->order), 0, 0}; });
  } else if (governing == "merge") {
    pk.winner = best(c, specific);
    pk.merged = true;
  } else if (governing == "intersection" || governing == "min") {  // the strictest outcome
    pk.winner = best(c, [](const Cand& x) { return Key4{x.restrict_, x.rule->rank, x.spec, -static_cast<long>(x.rule->order)}; });
  } else if (governing == "max") {  // the most permissive outcome
    pk.winner = best(c, [](const Cand& x) { return Key4{-static_cast<long>(x.restrict_), x.rule->rank, x.spec, -static_cast<long>(x.rule->order)}; });
  } else if (governing == "weighted_vote") {
    std::map<std::string, double> votes;
    std::map<std::string, int> strict;
    for (const auto& x : c) {
      votes[x.key] += x.rule->weight;
      strict[x.key] = x.restrict_;
    }
    std::string win;
    for (const auto& [k, v] : votes) {
      if (win.empty() || v > votes[win] || (v == votes[win] && strict[k] > strict[win])) win = k;
    }
    std::vector<Cand> same;
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < c.size(); ++i)
      if (c[i].key == win) { same.push_back(c[i]); idx.push_back(i); }
    pk.winner = idx[best(same, specific)];
  } else if (governing == "ask") {
    pk.winner = best(c, specific);
    for (const auto& x : c)
      if (x.key != c[0].key) pk.asked = true;
  } else if (governing == "by_context") {  // rules bound to a context predicate outrank the unbound ones
    std::vector<Cand> bound;
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < c.size(); ++i)
      if (c[i].rule->when.is_object() && !c[i].rule->when.empty()) { bound.push_back(c[i]); idx.push_back(i); }
    pk.winner = bound.empty() ? best(c, specific) : idx[best(bound, specific)];
  }
  return pk;
}

// The conflict rules this engine implements (mechanism); which ones exist in the chain is data.
const std::set<std::string>& implemented_conflict_rules() {
  static const std::set<std::string> s{"most_specific_wins", "locked_by", "first_defined", "merge", "intersection",
                                       "min", "max", "weighted_vote", "ask", "by_context"};
  return s;
}

Json merged_reason(const std::vector<Cand>& c) {
  std::vector<const Cand*> order;
  for (const auto& x : c) order.push_back(&x);
  std::sort(order.begin(), order.end(), [](const Cand* a, const Cand* b) {
    return std::make_pair(a->rule->rank, a->rule->order) < std::make_pair(b->rule->rank, b->rule->order);
  });
  Json out;
  for (const Cand* x : order) {
    const Json& r = x->rule->reason;
    if (r.is_object()) {
      if (!out.is_object()) out = Json::object();
      for (auto it = r.begin(); it != r.end(); ++it) out[it.key()] = it.value();
    } else if (r.is_string() && !out.is_object()) {
      out = r;
    }
  }
  return out;
}

Json rule_summary(const Engine::Rule& r, const char* role) {
  Json o{{"id", r.id}, {"scope", r.scope}};
  if (!r.scope_id.empty()) o["scope_id"] = r.scope_id;
  o["effect"] = r.effect;
  o["decision"] = r.decision;
  o["target"] = r.target;
  if (!r.action.is_null()) o["action"] = r.action;
  if (!r.initiator.empty()) o["initiator"] = r.initiator;
  if (!r.reason.is_null()) o["reason"] = r.reason;
  o["role"] = role;
  return o;
}

}  // namespace

Decision Engine::decide_prepared(const Prepared& p, std::string_view action, const Scope& scope) const {
  Decision d;
  const Json* entry = json::find(actions_, action);
  std::vector<Cand> cands;
  Json inactive = Json::array();
  for (const auto& rule : rules_) {
    if (rule.dimension != "gate") continue;
    const int ts = match_element(rule.target, p);
    if (ts < 0) continue;
    const int as = match_action(rule, action, entry);
    if (as < 0) continue;
    if (!active(rule, scope)) {
      inactive.push_back(rule_summary(rule, "inactive"));
      continue;
    }
    const Json* dec = json::find(decisions_, rule.decision);
    cands.push_back({&rule, ts * 10 + as, rule.decision, dec ? static_cast<int>(json::get_int(*dec, "restrictiveness", 0)) : 0});
  }
  if (const Json* dec = json::find(decisions_, d.decision)) d.status = json::get_string(*dec, "status", d.status);
  if (cands.empty()) {
    d.considered = inactive;
    return d;
  }
  const Pick pk = resolve(cands, default_conflict_, implemented_conflict_rules());
  const Cand& w = cands[pk.winner];
  d.conflict_rule = pk.conflict;
  if (pk.asked) {  // the scopes disagree and the rule says to ask
    d.decision = "confirm";
    d.effect = "confirm";
    d.rule.clear();
    if (const Json* dec = json::find(decisions_, "confirm")) d.status = json::get_string(*dec, "status", "needs_confirmation");
  } else {
    d.decision = w.key;
    d.effect = w.rule->effect;
    d.rule = w.rule->id;
    d.reason = pk.merged ? merged_reason(cands) : w.rule->reason;
    d.initiator = w.rule->initiator.empty()
                      ? w.rule->scope + (w.rule->scope_id.empty() ? std::string() : "/" + w.rule->scope_id)
                      : w.rule->initiator;
    if (const Json* dec = json::find(decisions_, d.decision)) d.status = json::get_string(*dec, "status", d.status);
  }
  Json considered = Json::array();
  for (std::size_t i = 0; i < cands.size(); ++i)
    considered.push_back(rule_summary(*cands[i].rule, pk.asked ? "conflict" : (i == pk.winner ? "decided" : "overridden")));
  for (const auto& x : inactive) considered.push_back(x);
  d.considered = considered;
  return d;
}

Result<Decision> Engine::decide(const Element& element, std::string_view action, const Scope& scope) const {
  return decide_prepared(prepare(element), action, scope);
}

std::string Engine::default_action(const Prepared& p, const Scope& scope) const {
  std::vector<Cand> cands;
  for (const auto& rule : rules_) {
    if (rule.dimension != "default" || !rule.action.is_string()) continue;
    const int ts = match_element(rule.target, p);
    if (ts < 0 || !active(rule, scope)) continue;
    cands.push_back({&rule, ts * 10, rule.action.get<std::string>(), 0});
  }
  if (cands.empty()) return "";
  const Pick pk = resolve(cands, default_conflict_, implemented_conflict_rules());
  return cands[pk.winner].key;
}

// ── Scoped values (R47) ───────────────────────────────────────────

Json ScopedValue::to_json() const {
  Json o = Json::object();
  o["value"] = value;
  o["defined"] = defined;
  o["scope"] = scope;
  if (!scope_id.empty()) o["scope_id"] = scope_id;
  o["source"] = source;
  o["rule"] = rule;
  if (!initiator.empty()) o["initiator"] = initiator;
  if (!conflict_rule.empty()) o["conflict_rule"] = conflict_rule;
  if (asked) o["asked"] = true;
  return o;
}

Result<ScopedValue> Engine::resolve_value(std::string_view key, const Scope& scope, const std::vector<Baseline>& baselines) const {
  constexpr std::size_t kBaselineOrder = 1'000'000;  // explicit rules win ties against baselines of the same level
  std::vector<Rule> synthetic;
  synthetic.reserve(baselines.size());
  std::vector<Cand> cands;
  Json inactive = Json::array();
  for (const auto& b : baselines) {
    Rule r;
    r.id = "baseline:" + b.level;
    r.rank = level_index(b.level, &r.scope);
    if (r.rank == static_cast<std::size_t>(-1)) return Error(Errc::InvalidArgument, "unknown scope level '" + b.level + "' in a baseline of '" + std::string(key) + "'");
    r.scope_id = b.scope_id;
    r.effect = "set_value";
    r.decision = "value";
    r.dimension = "value";
    r.initiator = b.source;
    r.value = b.value;
    r.has_value = true;
    r.order = kBaselineOrder - static_cast<std::size_t>(std::max(0, b.order));
    if (r.rank > scope.depth) {
      inactive.push_back(rule_summary(r, "inactive"));
      continue;
    }
    synthetic.push_back(std::move(r));
  }
  for (const auto& r : synthetic) cands.push_back({&r, 0, json::canonical(r.value), 0});
  for (const auto& rule : rules_) {
    if (rule.dimension != "value" || !rule.has_value) continue;
    if (json::get_string(rule.target, "kind") != "setting" || json::get_string(rule.target, "key") != key) continue;
    if (!active(rule, scope)) {
      inactive.push_back(rule_summary(rule, "inactive"));
      continue;
    }
    cands.push_back({&rule, 0, json::canonical(rule.value), 0});
  }
  ScopedValue out;
  if (cands.empty()) {
    out.considered = inactive;
    return out;
  }
  const Pick pk = resolve(cands, default_conflict_, implemented_conflict_rules());
  const Cand& w = cands[pk.winner];
  out.defined = true;
  out.value = w.rule->value;
  out.scope = w.rule->scope;
  out.scope_id = w.rule->scope_id;
  out.rule = w.rule->id;
  const bool baseline = w.rule->id.rfind("baseline:", 0) == 0;
  out.source = baseline ? w.rule->initiator : "rule";
  out.initiator = baseline ? std::string() : (w.rule->initiator.empty() ? w.rule->scope + (w.rule->scope_id.empty() ? std::string() : "/" + w.rule->scope_id) : w.rule->initiator);
  out.conflict_rule = pk.conflict;
  out.asked = pk.asked;
  for (std::size_t i = 0; i < cands.size(); ++i) {
    Json s = rule_summary(*cands[i].rule, pk.asked ? "conflict" : (i == pk.winner ? "decided" : "overridden"));
    s["value"] = cands[i].rule->value;
    out.considered.push_back(std::move(s));
  }
  for (const auto& x : inactive) out.considered.push_back(x);
  return out;
}

// ── Data consistency ──────────────────────────────────────────────

Json Engine::issues() const {
  Json out = Json::array();
  auto add = [&](Json issue) { out.push_back(std::move(issue)); };
  auto known_trait = [&](const std::string& t) { return traits_.contains(t) || classes_.contains(t); };
  for (auto it = classes_.begin(); it != classes_.end(); ++it) {
    const std::string parent = json::get_string(it.value(), "parent");
    if (!parent.empty() && !classes_.contains(parent)) add(Json{{"code", "class_parent_unknown"}, {"owner", it.key()}, {"parent", parent}});
    for (const auto& t : strings_of(json::find(it.value(), "traits")))
      if (!known_trait(t)) add(Json{{"code", "trait_unknown"}, {"owner", "class:" + it.key()}, {"trait", t}});
  }
  for (auto it = groups_.begin(); it != groups_.end(); ++it)
    for (const auto& t : strings_of(json::find(it.value(), "traits")))
      if (!known_trait(t)) add(Json{{"code", "trait_unknown"}, {"owner", "group:" + it.key()}, {"trait", t}});
  auto op_known = [&](const char* axis, const std::string& v) {
    const Json* list = json::find(axes_, axis);
    if (!list || !list->is_array()) return true;  // no vocabulary declared: open
    for (const auto& x : *list)
      if (json::get_string(x, "id") == v) return true;
    return false;
  };
  for (auto it = actions_.begin(); it != actions_.end(); ++it) {
    for (const char* member : {"requires", "produces"})
      for (const auto& t : strings_of(json::find(it.value(), member)))
        if (!known_trait(t)) add(Json{{"code", "trait_unknown"}, {"owner", "action:" + it.key()}, {"member", member}, {"trait", t}});
    for (const char* axis : {"content_op", "form_op"})
      if (const std::string v = json::get_string(it.value(), axis); !v.empty() && !op_known(axis, v))
        add(Json{{"code", "axis_value_unknown"}, {"owner", "action:" + it.key()}, {"axis", axis}, {"value", v}});
  }
  for (const auto& i : rule_issues_) add(i);
  std::set<std::string> conflicts;
  for (const auto& c : conflict_rules_) conflicts.insert(json::get_string(c, "id"));
  for (const auto& r : rules_) {
    const std::string kind = json::get_string(r.target, "kind"), id = json::get_string(r.target, "id");
    if (kind == "trait" && !known_trait(id)) add(Json{{"code", "trait_unknown"}, {"owner", "rule:" + r.id}, {"trait", id}});
    if (kind == "bundle" && !groups_.contains(id)) add(Json{{"code", "group_unknown"}, {"owner", "rule:" + r.id}, {"group", id}});
    if (kind == "class" && !classes_.contains(id)) add(Json{{"code", "class_unknown"}, {"owner", "rule:" + r.id}, {"class", id}});
    if (r.decision == "deny" && (r.reason.is_null() || (r.reason.is_string() && r.reason.get<std::string>().empty())))
      add(Json{{"code", "deny_without_reason"}, {"owner", "rule:" + r.id}});
    if (r.dimension == "default" && (!r.action.is_string() || !actions_.contains(r.action.get<std::string>())))
      add(Json{{"code", "default_action_unknown"}, {"owner", "rule:" + r.id}});
    if (r.dimension == "value") {
      if (kind != "setting" || json::get_string(r.target, "key").empty()) add(Json{{"code", "value_rule_without_key"}, {"owner", "rule:" + r.id}});
      if (!r.has_value) add(Json{{"code", "value_rule_without_value"}, {"owner", "rule:" + r.id}});
    }
    if (!r.conflict.empty() && !conflicts.count(r.conflict)) add(Json{{"code", "conflict_rule_unknown"}, {"owner", "rule:" + r.id}, {"conflict", r.conflict}});
    if (!r.conflict.empty() && conflicts.count(r.conflict) && !implemented_conflict_rules().count(r.conflict))
      add(Json{{"code", "conflict_rule_not_implemented"}, {"owner", "rule:" + r.id}, {"conflict", r.conflict}});
  }
  if (const Json* presets = json::find(extrapolation_, "presets")) {
    const std::string active_preset = json::get_string(extrapolation_, "active");
    if (!presets->contains(active_preset)) add(Json{{"code", "extrapolation_preset_unknown"}, {"preset", active_preset}});
  }
  return out;
}

}  // namespace loom::actions
