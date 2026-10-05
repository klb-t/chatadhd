// Independent before/after native profile-policy probe. No evaluation corpus/providers.
// Compile twice against the corresponding OLD/NEW private catalog headers.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "catalog/catalog_internal.h"
#include "loom/catalog.h"
#include "loom/db.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/json.h"

using namespace loom;
using namespace loom::catalog;
using namespace loom::catalog::internal;
namespace fs = std::filesystem;

template <typename T>
T must(Result<T> result) {
  if (!result) throw std::runtime_error(result.error().to_string());
  return std::move(result).value();
}

Json failure(const Error& error) {
  return Json{{"ok", false}, {"code", std::string(errc_name(error.code))}, {"message", error.message}};
}

void write(const fs::path& path, const std::string& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output || !(output << bytes)) throw std::runtime_error("fixture write failed: " + path.string());
}

Json table_state(Database& db) {
  auto lock = db.lock();
  auto statement = must(db.conn().prepare(
      "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE 'loom_cat_%' ORDER BY name"));
  Json tables = Json::array();
  std::vector<std::string> names;
  while (must(statement.step())) names.push_back(statement.get_text(0));
  for (const auto& name : names) {
    auto count = must(db.conn().query_int("SELECT COUNT(*) FROM " + name));
    tables.push_back(Json{{"table", name}, {"rows", count ? Json(*count) : Json(nullptr)}});
  }
  return tables;
}

Json mentions(const AliasIndex& index, const kb::Normalizer& normalizer) {
  Json output = Json::array();
  for (const std::string text : {
           "ChatADHD and Loom kernel SDK design 2.3: abstrakcja policy as data.",
           "Loom weaving scarves and a wooden krosno.",
           "compiler Boreal 2.3; Boreal telescope; foliage boréalisme.",
           "Stróża i kaucji: Folio 4.7 is distinct from otherfolio.",
           "Policy lens: make every recipe and parameter inspectable."}) {
    Json hits = Json::array();
    const auto folded = normalizer.fold(text);
    const auto found = index.find(folded, 100);
    for (const auto& hit : found) hits.push_back(hit.to_json());
    Json versions = Json::array();
    for (const auto& version : find_version_mentions(folded, found, 80)) versions.push_back(version.to_json());
    output.push_back(Json{{"text", text}, {"hits", hits}, {"versions", versions}});
  }
  return output;
}

Json weight_independent_alias(const kb::Normalizer& normalizer, const kb::Pack& pack) {
  // The DIC-0464 field was never read by matching. Absent and malformed
  // weights must not create new numeric fallback semantics during its removal.
  Json cases = Json::array();
  for (const Json& weight : {Json(3.0), Json(0.0), Json(-8.0), Json("irrelevant-to-matching"), Json(nullptr)}) {
    SelfProfile profile;
    Json alias{{"term", "Boreal"}, {"key", normalizer.fold("Boreal")},
               {"class", "alias"}, {"project", "boreal"}, {"ambiguous", true},
               {"requires_context", Json{{"min", 1}, {"any", Json::array({"compiler"})}}},
               {"negative_context", Json::array({"telescope"})}};
    Json principle{{"term", "Policy lens"}, {"key", normalizer.fold("Policy lens")},
                   {"class", "principle"}, {"project", ""}, {"ambiguous", false}};
    if (!weight.is_null()) { alias["weight"] = weight; principle["weight"] = weight; }
    profile.terms = Json::array({alias, principle,
        Json{{"term", "stróż"}, {"key", normalizer.fold("stróż")}, {"class", "alias"}, {"project", "warden"}},
        Json{{"term", "kaucje"}, {"key", normalizer.fold("kaucje")}, {"class", "alias"}, {"project", "deposits"}},
        Json{{"term", "Folio"}, {"key", normalizer.fold("Folio")}, {"class", "alias"}, {"project", "folio"}}});
    auto index = AliasIndex::from_profile(profile, 2);
    index.enable_inflection(normalizer, pack);
    cases.push_back(Json{{"input_weight", weight}, {"mentions", mentions(index, normalizer)}});
  }
  return cases;
}

int main(int argc, char** argv) {
  try {
    if (argc != 4) throw std::runtime_error("usage: probe FIXTURE_ROOT FRESH_RUNTIME_ROOT MODE");
    const fs::path fixture = fs::absolute(argv[1]);
    const fs::path runtime_path = fs::absolute(argv[2]);
    const std::string mode = argv[3];
    if (fs::exists(runtime_path)) throw std::runtime_error("runtime root must be fresh; probe never deletes data");
    auto builtin = must(kb::Pack::load_builtin());
    write(fixture / "repo" / "QuartzEngine.cpp", "// authored fixture, not a repository scan\n");
    write(fixture / "repo" / "SilverBranch.md", "# Fixture\n");
    write(fixture / "repo" / "ignore.bin", "untouched generic bytes\n");
    write(fixture / "source" / "note.txt",
          "ChatADHD and Loom kernel SDK. Policy lens and abstraction.\n");
    Result<std::shared_ptr<const kb::Pack>> loaded(builtin);
    if (mode != "default") {
      Json relevance = builtin->policy("relevance");
      if (mode == "weights") {
        relevance["term_class_weights"]["alias"] = -4.25;
        relevance["term_class_weights"]["principle"] = 0.0;
        relevance["term_class_weights"]["path"] = 6.75;
      } else if (mode == "missing-alias" || mode == "missing-principle" || mode == "missing-path") {
        relevance["term_class_weights"].erase(mode.substr(8));
      } else if (mode == "empty-map") {
        relevance["term_class_weights"] = Json::object();
      } else if (mode == "missing-map") {
        relevance.erase("term_class_weights");
      } else if (mode == "invalid-map") {
        relevance["term_class_weights"] = Json::array();
      } else if (mode == "invalid-alias") {
        relevance["term_class_weights"]["alias"] = "not-a-number";
      } else throw std::runtime_error("unknown probe mode");
      const auto overlay = fixture / "overlays" / mode;
      write(overlay / "policy" / "relevance.json", json::canonical(relevance));
      loaded = kb::Pack::load_with_overlay(overlay);
    }
    Json output{{"fixture_path", fixture.string()}, {"mode", mode}};
    if (!loaded) {
      output["pack"] = failure(loaded.error());
      std::cout << json::canonical(output) << '\n';
      return 0;
    }
    auto pack = std::move(loaded).value();
    output["pack"] = Json{{"ok", true}, {"hash", pack->hash()},
                          {"effective_class_weights", pack->policy("relevance")["term_class_weights"]}};
    RuntimeOptions options;
    options.data_dir = runtime_path.string(); options.start_workers = false; options.enable_fts = false;
    auto runtime = must(Runtime::open(options));
    Catalog catalog(*runtime, pack);
    ProfileConfig config;
    config.repo = (fixture / "repo").string(); config.extra_terms = {"QuartzRoute"};
    output["tables_before"] = table_state(runtime->db());
    auto profile = catalog.build_profile(config);
    output["profile"] = profile ? Json{{"ok", true}, {"value", profile->to_json()}} : failure(profile.error());
    output["tables_after_profile"] = table_state(runtime->db());
    // Also compare the actual no-options default profile, independent of repo/extra terms.
    auto defaults = catalog.build_profile(ProfileConfig{});
    output["default_profile"] = defaults ? Json{{"ok", true}, {"value", defaults->to_json()}} : failure(defaults.error());
    kb::Normalizer normalizer(*pack);
    output["standalone_alias_weights"] = weight_independent_alias(normalizer, *pack);
    if (profile) {
      auto index = AliasIndex::from_profile(*profile);
      index.enable_inflection(normalizer, *pack);
      output["profile_mentions"] = mentions(index, normalizer);
    }
    ScanConfig scan; scan.sources = {(fixture / "source" / "note.txt").string()}; scan.threads = 1;
    auto scanned = catalog.scan(scan);
    output["scan"] = scanned ? Json{{"ok", true}, {"value", *scanned}} : failure(scanned.error());
    output["tables_after_scan"] = table_state(runtime->db());
    if (scanned) {
      UnitQuery query; query.sort = "id"; query.limit = 20;
      Json units = Json::array();
      for (const auto& unit : must(catalog.query(query))) units.push_back(unit.to_json());
      output["units"] = units;
    }
    runtime->shutdown();
    std::cout << json::canonical(output) << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "probe failure: " << error.what() << '\n';
    return 2;
  }
}
