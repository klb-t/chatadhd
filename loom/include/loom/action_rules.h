// loom/action_rules.h — which actions fit which element (GRAF_HUB_KOD_SILNIK §5-§7, §9).
//
// Four pieces of data, none of them a list of allowed actions:
//   traits      on classes (data/graph/classes.pack, inherited along `parent`; a class id is a trait
//               of its own; a node adds traits of its own),
//   requirements on actions (data/runtime/actions.pack: requires / produces, the content_op and
//               form_op axes of §6, declared cost and confidence, effect flags),
//   rules       scoped {scope, target, action, effect} (data/runtime/action_rules.pack; the same
//               store and shape hold the settings field edit rules of S2/R2),
//   presets     of extrapolation (max chain length, max cost, min confidence; off allowed).
// An action fits an element when the element has every trait the action requires; a new action
// therefore fits all existing classes that meet its requirements without editing them. The rules then
// say which of the fitting actions is the default, needs confirmation or is denied. No rule = allow
// (R43). Chain proposals are proposals: nothing here executes an action.
//
// Mechanism vs data: the engine knows how to match, resolve conflicts (the conflict rules named in
// graph/scopes.pack) and search chains; which traits, groups, rules and thresholds exist is data.
#pragma once

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::actions {

// The element an action is applied to, as far as the engine needs to know it.
struct Element {
  std::string id;                  // optional node id (for rules that target one node)
  std::string cls;                 // class id; "" or unknown = only the own traits count
  std::set<std::string> own_traits;  // traits the node adds to its class
  Json attrs = Json::object();     // attributes rules may match (settings field: field_scope, key)
};

// The scope a question is asked in: the chain of graph/scopes.pack is active from its first level down
// to `level`; `context` names the instance of a level (a project, a conversation) for rules bound to one.
struct Scope {
  std::size_t depth = 0;
  std::string level;
  Json context = Json::object();
};

// Outcome of resolving the rules for one (element, action) pair.
struct Decision {
  std::string effect = "allow";     // effect id of the deciding rule ("allow" when no rule matches)
  std::string decision = "allow";   // gate decision: allow | confirm | deny (open: the decisions table)
  std::string status = "applicable";  // the word the decisions table gives that decision
  std::string rule;                 // id of the deciding rule; empty = no rule matched
  std::string conflict_rule;        // the conflict rule that resolved competing rules ("" = none competed)
  Json reason;                      // null, a string or {en, pl}
  std::string initiator;            // who stands behind the deciding rule
  Json considered = Json::array();  // every rule that targets the pair, with its role
};

class Engine {
 public:
  // Internal shapes (public so the implementation files can share them).
  // A rule of the store, normalised: the scope resolved to a rank in the chain.
  struct Rule {
    std::string id;
    std::string scope;                // resolved level id ("" when the rule names an unknown scope)
    std::size_t rank = 0;             // index in the chain
    std::string scope_id;             // instance of the level, "" = every instance
    Json target = Json::object();
    Json action;                      // string | object | null
    std::string effect;
    std::string decision;             // from the effects table; an unknown effect is an allow
    std::string dimension;            // gate | default
    Json reason;
    std::string initiator;
    bool locked = false;
    double weight = 1;
    std::string conflict;
    Json when;
    std::size_t order = 0;            // definition order
  };
  // One element with its traits expanded (trait -> where it came from) and class lineage.
  struct Prepared {
    Element element;
    std::map<std::string, std::string> traits;
    std::vector<std::string> lineage;  // class, parent, ...
    bool class_known = false;
  };

  // The effective profiles graph_entry (ontology, scopes), actions and action_rules, with the user
  // overlays of `data_dir` (profiles/<domain>.pack); empty = shipped data only.
  static Result<Engine> load(const std::filesystem::path& data_dir = {});
  // Explicit values (tools, tests): graph_entry profile values, the action catalogue (id -> entry,
  // the `actions` member of the actions profile) and action_rules profile values.
  static Result<Engine> from_values(const Json& graph_entry, const Json& actions, const Json& action_rules);

  // Element: {"class"|"kind": id, "traits": [...], "attrs": {...}, "id": "..."} or a bare class id.
  Result<Element> parse_element(const Json& element) const;
  // Scope: null (the most specific level), a level id, or {level: instance id, ...}.
  Result<Scope> parse_scope(const Json& scope) const;

  // Gate decision of the rules for (element, action). The action need not be in the catalogue
  // (settings fields use the action "edit").
  Result<Decision> decide(const Element& element, std::string_view action, const Scope& scope) const;

  // {"schema":"loom.actions_for/1","element","scope","default_action","actions":[row...],"issues":[]}
  // options: {"include_unmatched"?: false, "implemented_only"?: false}
  // row: {action,title,kind,implementation,callable,status,default,reason?,initiator?,effects,content_op,
  //       form_op,cost,confidence,groups,explanation{requires,matched[{trait,from}],decided_by}}
  // status: applicable | needs_confirmation | denied | default (| not_applicable with include_unmatched).
  Result<Json> actions_for(const Json& element, const Json& scope = Json(), const Json& options = Json::object()) const;

  // {"schema":"loom.actions_explain/1",...}: traits met or missing and where each came from, every rule
  // that targets the pair with its role (decided | overridden | inactive), and when traits are missing
  // the chain proposals that would supply them.
  Result<Json> explain(const Json& element, std::string_view action, const Json& scope = Json()) const;

  // Candidate chains of actions that make `target` ({"trait": id} or {"action": id}) available for the
  // element, ranked by declared cost/confidence, bounded by the extrapolation preset. Proposals only.
  // options: {"preset"?, "max_length"?, "max_cost"?, "min_confidence"?, "max_results"?}; a present key
  // overrides the preset, null = no bound.
  Result<Json> propose_chain(const Json& element, const Json& target, const Json& scope = Json(),
                             const Json& options = Json::object()) const;

  // Problems in the data this engine was built from (unknown traits, scopes, effects; deny without a
  // reason; ...), as [{code, owner, ...}]. Empty = consistent.
  Json issues() const;

  const Json& catalogue() const { return actions_; }

 private:
  Prepared prepare(const Element& element) const;
  void add_trait(std::map<std::string, std::string>& out, const std::string& trait, const std::string& source) const;
  std::size_t level_index(std::string_view name, std::string* resolved = nullptr) const;
  int class_depth(const std::string& cls) const;
  int match_element(const Json& target, const Prepared& p) const;
  int match_action(const Rule& rule, std::string_view action, const Json* entry) const;
  bool active(const Rule& rule, const Scope& scope) const;
  Decision decide_prepared(const Prepared& p, std::string_view action, const Scope& scope) const;
  std::string default_action(const Prepared& p, const Scope& scope) const;
  bool in_group(const std::string& group, const std::map<std::string, std::string>& traits) const;
  bool action_in_group(const std::string& group, const Json& action) const;
  std::vector<std::string> groups_of_action(const Json& action) const;
  Json element_json(const Element& element, const Prepared& p) const;
  Json scope_json(const Scope& scope) const;
  Json row_for(const std::string& id, const Json& action, const Prepared& p, const Scope& scope,
               const std::string& default_id) const;
  Result<Json> chains(const Prepared& p, const Json& target, const Scope& scope, const Json& options) const;

  Json classes_ = Json::object();
  Json traits_ = Json::object();
  Json groups_ = Json::object();
  Json relations_ = Json::object();
  Json scope_chain_ = Json::array();
  Json conflict_rules_ = Json::array();
  std::string default_conflict_;
  Json actions_ = Json::object();
  Json axes_ = Json::object();
  Json decisions_ = Json::object();
  Json effects_ = Json::object();
  Json aliases_ = Json::object();
  Json extrapolation_ = Json::object();
  std::vector<Rule> rules_;
  std::vector<Json> rule_issues_;
};

// loom_invoke port "actions": action "for" | "explain" | "propose_chain" with the request objects
// described at actions.for / actions.explain / actions.propose_chain in the action catalogue.
Result<Json> invoke(const std::filesystem::path& data_dir, std::string_view action, const Json& request);

}  // namespace loom::actions
