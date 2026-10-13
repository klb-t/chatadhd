#include "loom/action_table.h"

#include "loom/result.h"
#include "loom/runtime_profile.h"

namespace loom {
namespace {

// The one catalogue (data/runtime/actions.pack, keyed by id) listed as an array in key order.
Json list_of(const RuntimeProfile& profile) {
  Json out{{"catalogue_schema", "loom.action_catalogue/1"}, {"actions", Json::array()}};
  const Json& v = profile.values();
  if (v.contains("schema")) out["catalogue_schema"] = v["schema"];
  if (v.contains("actions") && v["actions"].is_object())
    for (const auto& [id, a] : v["actions"].items()) {
      if (a.is_object() && a.value("hidden", false)) continue;  // a user overlay hides an entry with "hidden": true
      out["actions"].push_back(a);
    }
  return out;
}

Json load_catalogue() {
  auto p = RuntimeProfile::builtin("actions");
  if (!p) return Json{{"catalogue_schema", "loom.action_catalogue/1"}, {"actions", Json::array()}};
  return list_of(*p);
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

Result<Json> action_catalogue(const std::filesystem::path& data_root) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("actions", data_root));
  return list_of(profile);
}

Json action_list(const std::filesystem::path& data_root) {
  auto cat = action_catalogue(data_root);
  if (cat) return *cat;
  return Json{{"error", Json{{"code", std::string(errc_name(cat.error().code))}, {"message", cat.error().message}}}};
}

const Json& action_table() {
  static const Json t = build_table();
  return t;
}

}  // namespace loom
