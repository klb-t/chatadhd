#include "loom/action_table.h"

#include "loom/runtime_profile.h"

namespace loom {
namespace {

Json load_catalogue() {
  auto p = RuntimeProfile::builtin("actions_storage");
  if (!p) return Json{{"catalogue_schema", "loom.action_catalogue/1"}, {"actions", Json::array()}};
  return p->values();
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
