// Connected, directed relation joins over the existing evidence graph. Domain
// names, edges, admission classes and search budgets are validated pack data.
#include <algorithm>
#include <map>
#include <set>

#include "internal.h"

namespace loom::generalize::detail {

Result<Json> match_relation_pattern(const Index& ix, const model::Entity& subject, const Json& op) {
  const auto& nodes = op.at("nodes");
  const auto& edges = op.at("edges");
  const std::string root = op.at("root").get<std::string>();
  const auto max_steps = op.at("max_steps").get<std::uint64_t>();
  const auto max_matches = op.at("max_matches").get<std::uint64_t>();
  Json matches = Json::array();
  std::uint64_t steps = 0;
  auto result = [&] { return Json{{"matches", matches}, {"steps", steps}, {"complete", true}}; };
  if (nodes.at(root).get<std::string>() != subject.kind) return result();

  // A connected order means each join has an already bound endpoint. This
  // avoids a Cartesian scan of unconnected nodes; cycles check existing IDs.
  std::vector<std::size_t> order;
  std::set<std::string> connected{root};
  std::set<std::size_t> remaining;
  for (std::size_t i = 0; i < edges.size(); ++i) remaining.insert(i);
  while (!remaining.empty()) {
    auto next = std::find_if(remaining.begin(), remaining.end(), [&](std::size_t i) {
      return connected.count(edges[i].at("from").get<std::string>()) || connected.count(edges[i].at("to").get<std::string>());
    });
    if (next == remaining.end()) return Error(Errc::InvalidArgument, "generalize.relation_pattern: disconnected pattern");
    order.push_back(*next);
    connected.insert(edges[*next].at("from").get<std::string>());
    connected.insert(edges[*next].at("to").get<std::string>());
    remaining.erase(next);
  }

  // Reuse the run's directed indexes: do not rescan the full evidence graph
  // for each subject or pattern. Policy filters apply to each visited edge.
  auto admitted = [&](const char* key, std::string_view value) {
    const auto& values = op.at(key);
    return std::find(values.begin(), values.end(), Json(std::string(value))) != values.end();
  };

  struct State {
    std::map<std::string, std::string> bindings;
    std::map<std::size_t, std::string> claims;
    Json qualifiers = Json::object();
    const std::vector<const model::Claim*>* candidates = nullptr;
    std::size_t cursor = 0;
  };
  const std::vector<const model::Claim*> none;
  auto candidates = [&](State& state, std::size_t depth) {
    const auto& edge = edges[order[depth]];
    const auto from = state.bindings.find(edge.at("from").get<std::string>());
    const auto to = state.bindings.find(edge.at("to").get<std::string>());
    const auto& index = from != state.bindings.end() ? ix.by_subject : ix.by_object;
    const auto& bound = from != state.bindings.end() ? from->second : to->second;
    auto found = index.find(bound);
    state.candidates = found == index.end() ? &none : &found->second;
  };
  State first;
  first.bindings[root] = subject.id;
  candidates(first, 0);
  std::vector<State> stack{std::move(first)};
  while (!stack.empty()) {
    auto& state = stack.back();
    if (state.cursor == state.candidates->size()) { stack.pop_back(); continue; }
    if (max_steps && steps == max_steps)
      return Error(Errc::Unavailable, "generalize.relation_pattern: max_steps exhausted; recognition is incomplete");
    ++steps;
    const auto* claim = (*state.candidates)[state.cursor++];
    const auto& edge = edges[order[stack.size() - 1]];
    if (claim->predicate != edge.at("rel").get<std::string>() || claim->object.empty() || transferred(*claim) ||
        !admitted("evidence", model::to_string(claim->assessment.evidence)) ||
        !admitted("statuses", model::to_string(claim->assessment.status))) continue;
    bool admitted_qualifiers = true;
    for (auto it = op.at("claim_qualifiers").begin(); it != op.at("claim_qualifiers").end(); ++it) {
      const auto& extra = claim->qualifiers.extra;
      const auto& filter = it.value();
      if (!extra.contains(it.key())) {
        if (!filter.at("allow_missing").get<bool>()) admitted_qualifiers = false;
      } else {
        const auto& values = filter.at("values");
        if (std::find(values.begin(), values.end(), extra.at(it.key())) == values.end()) admitted_qualifiers = false;
      }
    }
    if (!admitted_qualifiers) continue;
    State next;
    next.bindings = state.bindings;
    next.claims = state.claims;
    next.qualifiers = state.qualifiers;
    auto bind_node = [&](const std::string& variable, const std::string& id) {
      const auto* entity = ix.find_entity(id);
      if (!entity || entity->status == model::ClaimStatus::Rejected || nodes.at(variable).get<std::string>() != entity->kind) return false;
      auto [it, added] = next.bindings.emplace(variable, id);
      return added || it->second == id;
    };
    if (!bind_node(edge.at("from").get<std::string>(), claim->subject) ||
        !bind_node(edge.at("to").get<std::string>(), claim->object)) continue;
    bool compatible = true;
    for (const auto& pair : op.at("distinct")) {
      auto a = next.bindings.find(pair[0].get<std::string>()), b = next.bindings.find(pair[1].get<std::string>());
      if (a != next.bindings.end() && b != next.bindings.end() && a->second == b->second) compatible = false;
    }
    const Json qualifiers = claim->qualifiers.to_json();
    for (const auto& key : op.at("same_qualifiers")) {
      const std::string name = key.get<std::string>();
      const Json value = qualifiers.value(name, Json(""));
      if (next.qualifiers.contains(name) && next.qualifiers[name] != value) compatible = false;
      next.qualifiers[name] = value;
    }
    if (!compatible) continue;
    next.claims[order[stack.size() - 1]] = claim->id;
    if (stack.size() < order.size()) {
      candidates(next, stack.size());
      stack.push_back(std::move(next));
      continue;
    }
    if (max_matches && matches.size() == max_matches)
      return Error(Errc::Unavailable, "generalize.relation_pattern: max_matches exhausted; recognition is incomplete");
    Json proof = Json::array();
    std::set<std::string> observations;
    for (const auto& [index, id] : next.claims) {
      proof.push_back(Json{{"edge", index}, {"claim", id}});
      const auto* c = ix.find_claim(id);
      for (const auto& support : c->assessment.support) observations.insert(support.observation);
    }
    matches.push_back(Json{{"bindings", next.bindings}, {"proof", proof}, {"observations", observations},
                           {"qualifiers", next.qualifiers}});
  }
  return result();
}

}  // namespace loom::generalize::detail
