// Independent mixed-source evidence runner. Production default values come
// exclusively from actual RuntimeProfile descriptors; identities below are
// synthetic fixture identities, not a production graph registry or database.
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "loom/onboarding_layers.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"

using loom::Errc;
using loom::Json;
using loom::RuntimeProfile;
using loom::onboarding::DefaultLayers;
using loom::onboarding::runtime_profile_values;

namespace {
std::size_t assertions = 0;
void check(bool condition, const std::string& label) {
  ++assertions;
  if (!condition) throw std::runtime_error(label);
}
template <class T> T take(loom::Result<T> result) {
  if (!result) throw std::runtime_error(result.error().to_string());
  return std::move(result).value();
}
std::string canonical_hash(const Json& value) {
  return loom::Sha256::hex(loom::json::canonical(value));
}
bool equal(const Json& a, const Json& b) {
  return loom::json::canonical(a) == loom::json::canonical(b);
}
std::string token(std::string_view text) {
  std::string out;
  for (char c : text) out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
  return out;
}
std::string layer_key(const Json& fixture, const std::string& domain, const std::string& pointer) {
  return fixture.at("key_prefix").get<std::string>() + domain + pointer;
}
Json make_pack(const RuntimeProfile& profile, const Json& fixture) {
  Json entries = Json::array();
  for (const auto& [name, value] : profile.values().items()) {
    const auto pointer = "/" + token(name);
    const auto key = layer_key(fixture, profile.domain(), pointer);
    entries.push_back(Json{{"id", key}, {"key", key}, {"area", profile.domain()},
      {"revision", profile.definition().at("revision")}, {"value", value},
      {"evidence_only", true}, {"source_domain", profile.domain()},
      {"source_pointer", pointer}, {"definition_sha256", canonical_hash(profile.definition())}});
  }
  return Json{{"schema", "loom.default_layers_pack/1"},
    {"pack_id", fixture.at("pack_prefix").get<std::string>() + profile.domain()},
    {"revision", profile.definition().at("revision")}, {"policy", fixture.at("policy")},
    {"entries", std::move(entries)}};
}
Json make_bindings(const RuntimeProfile& profile, const Json& fixture) {
  Json result = Json::object();
  for (const auto& [name, value] : profile.values().items()) {
    (void)value;
    const auto pointer = "/" + token(name);
    result[pointer] = layer_key(fixture, profile.domain(), pointer);
  }
  return result;
}
DefaultLayers act(const DefaultLayers& original, const Json& action) {
  const auto before = original.snapshot();
  auto state = take(original.dispatch(action));
  check(equal(original.snapshot(), before), "dispatch changed the original immutable state");
  return take(DefaultLayers::create(original.pack(), state));
}
Json resolution(const DefaultLayers& layers, const std::string& key, std::string_view status) {
  auto result = take(layers.resolve(key));
  check(result.at("status") == status, "unexpected layer resolution status for " + key);
  check(result.contains("explanation") && !result.at("explanation").get<std::string>().empty(),
        "layer resolution has no explanation");
  return result;
}
void collect_paths(const Json& value, const std::string& pointer, std::vector<std::string>& paths) {
  if (value.is_object()) {
    for (const auto& [key, child] : value.items()) collect_paths(child, pointer + "/" + token(key), paths);
  } else if (value.is_array()) {
    paths.push_back(pointer);
    for (std::size_t i = 0; i < value.size(); ++i) collect_paths(value[i], pointer + "/" + std::to_string(i), paths);
  } else paths.push_back(pointer);
}
// Candidate changes are universal fixture operations. The actual native value
// schema accepts/rejects them; no substitute schema validator decides validity.
Json changed_values(const RuntimeProfile& profile, std::string& selected_path) {
  std::vector<std::string> paths;
  collect_paths(profile.values(), "", paths);
  for (const auto& path : paths) {
    const auto& old = profile.values().at(Json::json_pointer(path));
    std::vector<Json> candidates;
    if (old.is_boolean()) candidates.emplace_back(!old.get<bool>());
    else if (old.is_string()) candidates.emplace_back(old.get<std::string>() + ".mixed-evidence");
    else if (old.is_number_unsigned()) {
      const auto value = old.get<std::uint64_t>();
      if (value != std::numeric_limits<std::uint64_t>::max()) candidates.emplace_back(value + 1);
      if (value > 0) candidates.emplace_back(value - 1);
    } else if (old.is_number_integer()) {
      const auto value = old.get<std::int64_t>();
      if (value != std::numeric_limits<std::int64_t>::max()) candidates.emplace_back(value + 1);
      if (value != std::numeric_limits<std::int64_t>::min()) candidates.emplace_back(value - 1);
    } else if (old.is_number()) {
      candidates.emplace_back(old.get<double>() + 1.0);
      candidates.emplace_back(old.get<double>() - 1.0);
      candidates.emplace_back(old.get<double>() + 0.01);
      candidates.emplace_back(old.get<double>() - 0.01);
    } else if (old.is_array() && !old.empty()) {
      Json changed = old;
      if (changed.size() > 1) std::swap(changed[0], changed[1]);
      else changed.push_back(changed[0]);
      candidates.emplace_back(std::move(changed));
    }
    for (const auto& candidate : candidates) {
      Json changed = profile.values();
      changed[Json::json_pointer(path)] = candidate;
      auto validated = profile.with_values(changed);
      if (validated && !equal(changed, profile.values())) {
        selected_path = path;
        return changed;
      }
    }
  }
  throw std::runtime_error("fixture found no native-schema-valid changed setting in " + profile.domain());
}
Json error_receipt(const loom::Result<Json>& result, const std::string& label) {
  check(!result, label + " unexpectedly succeeded");
  check(result.error().code == Errc::InvalidArgument, label + " did not preserve schema error");
  return Json{{"code", loom::errc_name(result.error().code)}, {"message", result.error().message}};
}
void check_effective(const Json& effective, const RuntimeProfile& expected) {
  check(equal(effective.at("values"), expected.values()), "effective values differ from native direct profile");
  check(effective.at("hash") == expected.hash(), "effective hash differs from native direct profile");
  check(effective.at("is_builtin") == expected.is_builtin(), "effective builtin flag differs");
}

Json probe(const RuntimeProfile& profile, const Json& fixture) {
  const auto assertions_before = assertions;
  const Json definition_before = profile.definition(), values_before = profile.values();
  const std::string hash_before = profile.hash();
  const auto pack = make_pack(profile, fixture), bindings = make_bindings(profile, fixture);
  auto layers = take(DefaultLayers::create(pack, fixture.at("opaque_state")));
  const auto untouched_state = layers.snapshot();
  const auto untouched_pack = layers.pack();
  const auto effective = take(runtime_profile_values(profile.definition(), layers, bindings));
  check_effective(effective, profile);
  for (const auto& [pointer, key] : bindings.items()) {
    auto value = resolution(layers, key.get<std::string>(), "effective");
    check(value.at("layer") == "builtin", "untouched value is not builtin");
    check(equal(value.at("value"), profile.values().at(Json::json_pointer(pointer))), "layer value differs from exact descriptor default");
  }
  std::string changed_path;
  const auto candidate_values = changed_values(profile, changed_path);
  auto candidate_profile = take(profile.with_values(candidate_values));
  const auto slash = changed_path.find('/', 1);
  const auto pointer = changed_path.substr(0, slash);
  const auto key = bindings.at(pointer).get<std::string>();
  const auto group = candidate_values.at(Json::json_pointer(pointer));
  const Json action{{"op", "override"}, {"key", key}, {"value", group},
                    {"provenance", fixture.at("provenance")}, {"time", fixture.at("known_at")}};
  auto overridden = act(layers, action);
  auto overridden_resolution = resolution(overridden, key, "effective");
  check(overridden_resolution.at("layer") == "user", "user override lost precedence");
  check(overridden_resolution.at("source").at("provenance") == fixture.at("provenance"), "override provenance lost");
  check_effective(take(runtime_profile_values(profile.definition(), overridden, bindings)), candidate_profile);
  auto cleared = act(overridden, Json{{"op", "clear_override"}, {"key", key}});
  check_effective(take(runtime_profile_values(profile.definition(), cleared, bindings)), profile);

  auto disabled = act(layers, Json{{"op", "disable"}, {"key", key}});
  check(!resolution(disabled, key, "disabled").contains("value"), "disabled default retains effective value");
  const auto disabled_result = runtime_profile_values(profile.definition(), disabled, bindings);
  auto disabled_error = error_receipt(disabled_result, "suppressed required setting");
  auto excluded = act(layers, Json{{"op", "exclude"}, {"key", key}});
  check(!resolution(excluded, key, "excluded").contains("value"), "excluded default retains effective value");
  const auto excluded_result = runtime_profile_values(profile.definition(), excluded, bindings);
  auto excluded_error = error_receipt(excluded_result, "excluded required setting");
  auto rejected_override = excluded.dispatch(action);
  check(!rejected_override && rejected_override.error().code == Errc::Conflict, "override bypassed permanent exclusion");

  Json newer_pack = pack;
  newer_pack["revision"] = pack.at("revision").get<std::uint64_t>() + 1;
  for (auto& entry : newer_pack["entries"]) if (entry.at("key") == key) {
    entry["revision"] = entry.at("revision").get<std::uint64_t>() + 1;
    entry["value"] = group;
  }
  const auto new_key = key + fixture.at("new_key_suffix").get<std::string>();
  newer_pack["entries"].push_back(Json{{"id", new_key}, {"key", new_key}, {"area", profile.domain()},
                                      {"revision", 1}, {"value", group}, {"evidence_only", true}});
  auto updated = take(DefaultLayers::create(newer_pack, take(excluded.update_pack(newer_pack))));
  check(!resolution(updated, key, "excluded").contains("value"), "pack update resurrected permanent exclusion");
  check(!resolution(updated, new_key, "proposal").contains("value"), "new excluded-area proposal applied implicitly");
  check(updated.snapshot().at("opaque_fixture_state") == fixture.at("opaque_state").at("opaque_fixture_state"), "unknown persistent state lost");
  auto proposed_bindings = bindings;
  proposed_bindings[pointer] = new_key;
  auto proposal_error = error_receipt(runtime_profile_values(profile.definition(), updated, proposed_bindings), "unaccepted proposal required setting");
  auto direct = act(updated, Json{{"op", "set_area_mode"}, {"area", profile.domain()}, {"mode", "direct"}});
  check(!resolution(direct, key, "excluded").contains("value"), "direct area mode cleared old permanent exclusion");
  resolution(direct, new_key, "effective");
  check_effective(take(runtime_profile_values(profile.definition(), direct, proposed_bindings)), candidate_profile);

  Json removed_pack = newer_pack;
  removed_pack["revision"] = newer_pack.at("revision").get<std::uint64_t>() + 1;
  for (auto it = removed_pack["entries"].begin(); it != removed_pack["entries"].end();) {
    if (it->at("key") == key) it = removed_pack["entries"].erase(it); else ++it;
  }
  auto disappeared = take(DefaultLayers::create(removed_pack, updated.snapshot()));
  check(!resolution(disappeared, key, "excluded").contains("value"), "disappearance discarded the exclusion marker");
  Json returned_pack = newer_pack;
  returned_pack["revision"] = removed_pack.at("revision").get<std::uint64_t>() + 1;
  auto returned = take(DefaultLayers::create(returned_pack, disappeared.snapshot()));
  check(!resolution(returned, key, "excluded").contains("value"), "reappearance resurrected an excluded default");
  error_receipt(runtime_profile_values(profile.definition(), returned, bindings), "reappeared excluded required setting");
  auto reenabled = act(returned, Json{{"op", "reenable"}, {"key", key}});
  check_effective(take(runtime_profile_values(profile.definition(), reenabled, bindings)), candidate_profile);

  Json recycled_pack = returned_pack;
  recycled_pack["revision"] = returned_pack.at("revision").get<std::uint64_t>() + 1;
  for (auto& entry : recycled_pack["entries"]) if (entry.at("key") == key) entry["id"] = new_key + fixture.at("new_key_suffix").get<std::string>();
  auto recycled = returned.update_pack(recycled_pack);
  check(!recycled && recycled.error().code == Errc::Conflict, "recycled identity bypassed exclusion history");
  check(equal(layers.snapshot(), untouched_state) && equal(layers.pack(), untouched_pack), "original layers changed");
  check(equal(profile.definition(), definition_before) && equal(profile.values(), values_before) && profile.hash() == hash_before, "original profile changed");
  auto reloaded = take(DefaultLayers::create(returned.pack(), take(loom::json::parse(loom::json::canonical(returned.snapshot())))));
  check(equal(reloaded.snapshot(), returned.snapshot()), "persistent layer-state roundtrip changed bytes");

  Json row{{"domain", profile.domain()}, {"bindings", bindings}, {"binding_groups", bindings.size()},
    {"definition", profile.definition()}, {"defaults", profile.values()}, {"default_hash", profile.hash()},
    {"definition_sha256", canonical_hash(profile.definition())}, {"defaults_sha256", canonical_hash(profile.values())},
    {"changed_leaf", changed_path}, {"changed_group_pointer", pointer}, {"changed_group", group},
    {"changed_hash", candidate_profile.hash()}, {"disabled_error", disabled_error}, {"excluded_error", excluded_error},
    {"proposal_error", proposal_error}, {"final_exclusion_resolution", take(returned.resolve(key))},
    {"assertions", assertions - assertions_before}, {"status", "passed"}};
  const auto inspection = profile.inspection();
  if (inspection.contains("definition")) {
    check(equal(inspection.at("definition"), profile.definition()), "inspection definition drift");
    check(equal(inspection.at("defaults"), profile.definition().at("defaults")), "inspection defaults drift");
    check(inspection.at("source_provenance").is_array() && !inspection.at("source_provenance").empty(),
          "builtin inspection has no authoritative source provenance");
    row["inspection_source_provenance"] = inspection.at("source_provenance");
    row["inspection_descriptor_verified"] = true;
  } else row["inspection_descriptor_verified"] = false;
  row["assertions"] = assertions - assertions_before;
  return row;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("usage: runner fixture.json");
    const auto fixture = take(loom::json::parse(take(loom::fsutil::read_file(argv[1]))));
    check(fixture.at("schema") == "loom.runtime_layers_mixed_fixture/1", "wrong fixture schema");
    check(fixture.at("policy").at("excluded_area_new_defaults") == "proposal", "fixture must exercise the actual W12 proposal preset");
    Json rows = Json::array();
    std::size_t groups = 0;
    for (const auto& domain : RuntimeProfile::domains()) {
      auto profile = take(RuntimeProfile::builtin(domain));
      auto row = probe(profile, fixture);
      groups += row.at("binding_groups").get<std::size_t>();
      rows.push_back(std::move(row));
    }
    Json result{{"schema", "loom.runtime_layers_mixed_result/1"}, {"status", "passed"},
      {"domains", rows.size()}, {"binding_groups", groups}, {"assertions", assertions},
      {"provider_calls", 0}, {"database_created", false}, {"production_bindings_claimed", false},
      {"scope", "Actual W12 DefaultLayers/runtime_profile_values paired with actual W11 RuntimeProfile; synthetic evidence identities only. No OnboardingStore, registry execution, live consumer wiring or full CTest claim."},
      {"results", std::move(rows)}};
    std::cout << loom::json::dump(result) << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "mixed runtime-layer proof failed after " << assertions << " assertions: " << error.what() << '\n';
    return 1;
  }
}
