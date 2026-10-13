#include "loom/action_table.h"

#include "loom/runtime_profile.h"

namespace loom {
namespace {

// The one catalogue (data/runtime/actions.pack, keyed by id) listed as an array in key order.
Json load_catalogue() {
  Json out{{"catalogue_schema", "loom.action_catalogue/1"}, {"actions", Json::array()}};
  auto p = RuntimeProfile::builtin("actions");
  if (!p) return out;
  const Json& v = p->values();
  if (v.contains("schema")) out["catalogue_schema"] = v["schema"];
  if (v.contains("actions") && v["actions"].is_object())
    for (const auto& [id, a] : v["actions"].items()) out["actions"].push_back(a);
  return out;
}

Json build_table() {
  Json t = Json::array();
  for (const auto& a : action_catalogue().value("actions", Json::array())) {
    if (!a.contains("http")) continue;
    const Json& h = a["http"];
    t.push_back(Json{{"port", a.value("port", "")}, {"action", a.value("action", "")},
                     {"verb", h.value("verb", "GET")}, {"path", h.value("path", "")},
                     {"query", h.value("query", Json::array())}});
  }
  return t;
}

}  // namespace

const Json& action_catalogue() {
  static const Json c = load_catalogue();
  return c;
}

const Json& action_table() {
  static const Json t = build_table();
  return t;
}

}  // namespace loom
