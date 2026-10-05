#include "runtime_adapter.h"

#include <algorithm>
#include <string>
#include <vector>

#if __has_include("loom/runtime_profile.h")
#include "loom/runtime_profile.h"
#endif

namespace loom::onboarding {

Result<Json> runtime_profile_values(const Json& definition, const DefaultLayers& layers,
                                    const Json& bindings) {
#if __has_include("loom/runtime_profile.h")
  try {
    if (!bindings.is_object()) return Error(Errc::InvalidArgument, "runtime layer bindings must be an object");
    LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::from_definition(definition));
    Json values = profile.values();
    std::vector<std::string> pointers;
    for (const auto& [pointer, key] : bindings.items()) {
      (void)key;
      for (const auto& existing : pointers) {
        if ((pointer.size() > existing.size() && pointer.starts_with(existing + "/")) ||
            (existing.size() > pointer.size() && existing.starts_with(pointer + "/")))
          return Error(Errc::Conflict, "runtime layer bindings overlap: " + pointer + " and " + existing);
      }
      pointers.push_back(pointer);
    }
    std::vector<std::string> removals;
    for (const auto& [pointer, key] : bindings.items()) {
      if (pointer.empty() || pointer.front() != '/' || !key.is_string())
        return Error(Errc::InvalidArgument, "runtime layer binding needs a non-root RFC6901 pointer and string key");
      LOOM_TRY_ASSIGN(auto effective, layers.resolve(key.get<std::string>()));
      Json::json_pointer path(pointer);
      if (effective["status"] == "effective") {
        values[path] = effective["value"];
      } else if (values.contains(path)) {
        removals.push_back(pointer);
      }
    }
    // All replacements precede removals. Deeper removals precede shallower
    // ones; sibling array indices descend numerically so indices never shift
    // under a later removal. RFC6901 pointers have already been validated.
    std::sort(removals.begin(), removals.end(), [](const std::string& a, const std::string& b) {
      const auto depth_a = std::count(a.begin(), a.end(), '/');
      const auto depth_b = std::count(b.begin(), b.end(), '/');
      if (depth_a != depth_b) return depth_a > depth_b;
      const auto split_a = a.rfind('/'), split_b = b.rfind('/');
      const auto parent_a = a.substr(0, split_a), parent_b = b.substr(0, split_b);
      if (parent_a != parent_b) return parent_a > parent_b;
      const auto tail_a = a.substr(split_a + 1), tail_b = b.substr(split_b + 1);
      const auto numeric = [](const std::string& tail) {
        return !tail.empty() && std::all_of(tail.begin(), tail.end(), [](char c) { return c >= '0' && c <= '9'; });
      };
      if (numeric(tail_a) && numeric(tail_b) && tail_a.size() != tail_b.size()) return tail_a.size() > tail_b.size();
      return tail_a > tail_b;
    });
    for (const auto& pointer : removals)
      values = values.patch(Json::array({Json{{"op", "remove"}, {"path", pointer}}}));
    LOOM_TRY_ASSIGN(auto checked, profile.with_values(values));
    return checked.inspection();
  } catch (const Json::exception& e) {
    return Error(Errc::InvalidArgument, std::string("runtime layer bindings: ") + e.what());
  }
#else
  (void)definition; (void)layers; (void)bindings;
  return Error(Errc::Unavailable, "RuntimeProfile from thread 11 is not integrated; runtime layer values were not applied");
#endif
}

}  // namespace loom::onboarding
