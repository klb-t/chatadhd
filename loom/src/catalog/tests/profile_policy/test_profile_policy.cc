// Independent owned regressions for W6 DIC-0462 / DIC-0464.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "catalog/catalog_internal.h"
#include "loom/catalog.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "loom/sqlite.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"
#include "loom/util/sha256.h"

using namespace loom;
using namespace loom::catalog;
using namespace loom::catalog::internal;
namespace fs = std::filesystem;

namespace {
template <typename T>
T must(Result<T> result) {
  if (!result) throw std::runtime_error(result.error().to_string());
  return std::move(result).value();
}

void write(const fs::path& path, std::string_view bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out || !(out << bytes)) throw std::runtime_error("fixture write failed");
}

std::map<std::string, Json> docs() {
  auto builtin = must(kb::Pack::load_builtin());
  std::map<std::string, Json> result;
  for (const auto& path : builtin->files()) result[path] = builtin->file(path);
  return result;
}

std::shared_ptr<const kb::Pack> pack_with(const std::string& cls, Json weight) {
  auto input = docs();
  input["policy/relevance.json"]["term_class_weights"][cls] = std::move(weight);
  return must(kb::Pack::from_documents(std::move(input)));
}

std::shared_ptr<const kb::Pack> missing(const std::string& cls) {
  auto input = docs();
  input["policy/relevance.json"]["term_class_weights"].erase(cls);
  return must(kb::Pack::from_documents(std::move(input)));
}

std::unique_ptr<Runtime> runtime(const fs::path& root) {
  RuntimeOptions options;
  options.data_dir = root.string(); options.start_workers = false; options.enable_fts = false;
  return must(Runtime::open(options));
}

std::int64_t catalog_tables(Database& db) {
  auto lock = db.lock();
  const auto count = must(db.conn().query_int(
      "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name LIKE 'loom_cat_%'"));
  if (!count) throw std::runtime_error("catalog table count missing");
  return *count;
}

Json rows(Database& db) {
  auto lock = db.lock();
  auto query = must(db.conn().prepare("SELECT id,input_hash,body,created FROM loom_cat_profiles ORDER BY id"));
  Json result = Json::array();
  while (must(query.step())) {
    result.push_back(Json{{"id", query.get_text(0)}, {"input_hash", query.get_text(1)},
                          {"body", query.get_text(2)}, {"created", query.get_text(3)}});
  }
  return result;
}

void check_pointer(const Error& error, std::string_view pointer) {
  CHECK(error.code == Errc::InvalidArgument);
  CHECK(error.message.find(pointer) != std::string::npos);
}

Json hits(const AliasIndex& index, std::string_view folded) {
  Json result = Json::array();
  for (const auto& hit : index.find(folded, 30)) result.push_back(hit.to_json());
  return result;
}
}  // namespace

TEST_SUITE("catalog_profile_policy") {
TEST_CASE("default class weights materialize all profile sources and deterministic identity") {
  fsutil::TempDir data;
  fsutil::TempDir repo;
  write(repo.path() / "QuartzPath.cpp", "// authored fixture\n");
  auto pack = must(kb::Pack::load_builtin());
  auto rt = runtime(data.path());
  Catalog catalog(*rt, pack);
  ProfileConfig config;
  config.repo = repo.path().string(); config.extra_terms = {"UserQuartz"};
  auto profile = must(catalog.build_profile(config));
  REQUIRE(!profile.projects.empty());
  bool path = false, user = false, principle = false, alias = false;
  const auto& weights = pack->policy("relevance")["term_class_weights"];
  for (const auto& term : profile.terms) {
    const auto cls = term["class"].get<std::string>();
    CHECK(term["weight"] == weights[cls]);
    path |= cls == "path";
    user |= term["provenance"] == "user";
    principle |= cls == "principle";
    alias |= cls == "alias" && term["provenance"] == "profiles/self.json";
  }
  CHECK(path); CHECK(user); CHECK(principle); CHECK(alias);
  Json identity{{"pack_hash", pack->hash()}, {"cfg", config.to_json()},
                {"terms", profile.terms}, {"projects", profile.projects}};
  CHECK(profile.input_hash == Sha256::hex(json::canonical(identity)));
  CHECK(profile.id == "cp_" + profile.input_hash.substr(0, 16));
  const auto repeated = must(catalog.build_profile(config));
  CHECK(json::canonical(repeated.to_json()) == json::canonical(profile.to_json()));
  const auto plain = must(catalog.build_profile(ProfileConfig{}));
  CHECK(std::none_of(plain.terms.begin(), plain.terms.end(), [](const Json& t) { return t["class"] == "path"; }));
  CHECK(json::canonical(plain.projects) == json::canonical(profile.projects));
}

TEST_CASE("native file overlay reaches alias principle and eligible path weights without bounds") {
  fsutil::TempDir overlay;
  fsutil::TempDir data;
  fsutil::TempDir repo;
  auto builtin = must(kb::Pack::load_builtin());
  Json relevance = builtin->policy("relevance");
  relevance["term_class_weights"]["alias"] = -4.25;
  relevance["term_class_weights"]["principle"] = 0.0;
  relevance["term_class_weights"]["path"] = 6.75;
  write(overlay.path() / "policy" / "relevance.json", json::canonical(relevance));
  write(repo.path() / "QuartzPath.cpp", "// fixture\n");
  auto pack = must(kb::Pack::load_with_overlay(overlay.path()));
  CHECK(pack->hash() != builtin->hash());
  auto rt = runtime(data.path());
  Catalog catalog(*rt, pack);
  ProfileConfig config;
  config.repo = repo.path().string(); config.extra_terms = {"UserQuartz"};
  auto profile = must(catalog.build_profile(config));
  int aliases = 0, principles = 0, paths = 0;
  for (const auto& term : profile.terms) {
    if (term["class"] == "alias") { CHECK(term["weight"] == Json(-4.25)); ++aliases; }
    if (term["class"] == "principle") { CHECK(term["weight"] == Json(0.0)); ++principles; }
    if (term["class"] == "path") { CHECK(term["weight"] == Json(6.75)); ++paths; }
  }
  CHECK(aliases > 0); CHECK(principles > 0); CHECK(paths == 1);
}

TEST_CASE("existing pack validation rejects missing wrong-type maps and nonnumeric values") {
  for (int variant = 0; variant < 4; ++variant) {
    auto input = docs();
    auto& relevance = input["policy/relevance.json"];
    if (variant == 0) relevance.erase("term_class_weights");
    if (variant == 1) relevance["term_class_weights"] = Json::array();
    if (variant == 2) relevance["term_class_weights"] = nullptr;
    if (variant == 3) relevance["term_class_weights"]["alias"] = "not-a-number";
    auto pack = kb::Pack::from_documents(std::move(input));
    REQUIRE_FALSE(pack);
    check_pointer(pack.error(), "/term_class_weights");
  }
}

TEST_CASE("missing consumed alias and principle weights reject profile and scan before lazy catalog writes") {
  for (const std::string cls : {"alias", "principle"}) {
    fsutil::TempDir data;
    fsutil::TempDir source;
    write(source.path() / "note.txt", "ChatADHD kernel policy as data\n");
    auto rt = runtime(data.path());
    auto pack = missing(cls);
    Catalog catalog(*rt, pack);
    const auto before = catalog_tables(rt->db());
    auto profile = catalog.build_profile(ProfileConfig{});
    REQUIRE_FALSE(profile);
    check_pointer(profile.error(), "/term_class_weights/" + cls);
    CHECK(catalog_tables(rt->db()) == before);
    ScanConfig scan; scan.sources = {(source.path() / "note.txt").string()}; scan.threads = 1;
    auto result = catalog.scan(scan);
    REQUIRE_FALSE(result);
    check_pointer(result.error(), "/term_class_weights/" + cls);
    CHECK(catalog_tables(rt->db()) == before);
    auto index = AliasIndex::from_pack_checked(*pack);
    REQUIRE_FALSE(index);
    check_pointer(index.error(), "/term_class_weights/" + cls);
  }
}

TEST_CASE("empty map is explicit missing-class error rather than hidden profile defaults") {
  fsutil::TempDir data;
  auto input = docs(); input["policy/relevance.json"]["term_class_weights"] = Json::object();
  auto pack = must(kb::Pack::from_documents(std::move(input)));
  auto rt = runtime(data.path()); Catalog catalog(*rt, pack);
  auto profile = catalog.build_profile(ProfileConfig{});
  REQUIRE_FALSE(profile); check_pointer(profile.error(), "/term_class_weights/");
  CHECK(catalog_tables(rt->db()) == 0);
}

TEST_CASE("knowledge stage delegates schema creation until checked scan policy succeeds") {
  fsutil::TempDir data;
  fsutil::TempDir source;
  auto pack = missing("alias");
  write(data.path() / "kb/policy/relevance.json", json::canonical(pack->policy("relevance")));
  write(source.path() / "note.txt", "ChatADHD kernel\n");
  auto rt = runtime(data.path());
  knowledge::KnowledgeConfig config;
  config.sources = {(source.path() / "note.txt").string()};
  config.stages = {"catalog"};
  config.llm = "off";
  config.stage_params = Json{{"catalog", Json{{"import", Json{{"dry_run", true}}}}}};
  auto result = must(rt->knowledge().run(config));
  CHECK(result.status == "failed");
  CHECK(result.error.find("/term_class_weights/alias") != std::string::npos);
  CHECK(catalog_tables(rt->db()) == 0);
}

TEST_CASE("legacy private adapters expose policy failure rather than substitute defaults") {
  auto pack = missing("alias");
  kb::Normalizer normalizer(*pack);
  CHECK_THROWS_AS(AliasIndex::from_pack(*pack), std::invalid_argument);
  CHECK_THROWS_AS(flatten_self_profile(*pack, normalizer, ProfileConfig{}), std::invalid_argument);
}

TEST_CASE("nonfinite numeric class values accepted upstream are rejected by profile and scan") {
  for (double weight : {std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::infinity(),
                        -std::numeric_limits<double>::infinity()}) {
    fsutil::TempDir data;
    fsutil::TempDir source;
    write(source.path() / "note.txt", "ChatADHD kernel\n");
    // from_documents receives a native Json number. Serializing nonfinite
    // numbers to a JSON file would change the input and test a different path.
    auto pack = pack_with("alias", Json(weight));
    auto rt = runtime(data.path()); Catalog catalog(*rt, pack);
    auto profile = catalog.build_profile(ProfileConfig{});
    REQUIRE_FALSE(profile); check_pointer(profile.error(), "/term_class_weights/alias");
    CHECK(catalog_tables(rt->db()) == 0);
    ScanConfig scan; scan.sources = {(source.path() / "note.txt").string()}; scan.threads = 1;
    auto result = catalog.scan(scan);
    REQUIRE_FALSE(result); check_pointer(result.error(), "/term_class_weights/alias");
    CHECK(catalog_tables(rt->db()) == 0);
  }
}

TEST_CASE("finite numeric class values have no arbitrary weight ceiling") {
  fsutil::TempDir data;
  auto pack = pack_with("alias", Json(std::numeric_limits<double>::max()));
  auto rt = runtime(data.path()); Catalog catalog(*rt, pack);
  auto profile = must(catalog.build_profile(ProfileConfig{}));
  int aliases = 0;
  for (const auto& term : profile.terms) if (term["class"] == "alias") {
    CHECK(term["weight"] == Json(std::numeric_limits<double>::max())); ++aliases;
  }
  CHECK(aliases > 0);
}

TEST_CASE("missing path weight is required only when an eligible path term is emitted") {
  fsutil::TempDir data;
  fsutil::TempDir repo;
  auto pack = missing("path"); auto rt = runtime(data.path()); Catalog catalog(*rt, pack);
  auto plain = catalog.build_profile(ProfileConfig{});
  REQUIRE(plain);
  ProfileConfig config; config.repo = repo.path().string();
  write(repo.path() / "Excluded.bin", "fixture\n");
  auto no_eligible = catalog.build_profile(config); REQUIRE(no_eligible);
  const auto before = rows(rt->db());
  write(repo.path() / "EligiblePath.cpp", "// fixture\n");
  auto rejected = catalog.build_profile(config); REQUIRE_FALSE(rejected);
  check_pointer(rejected.error(), "/term_class_weights/path");
  CHECK(rows(rt->db()) == before);
  ScanConfig scan; scan.sources = {(repo.path() / "EligiblePath.cpp").string()}; scan.threads = 1;
  REQUIRE(catalog.scan(scan));
}

TEST_CASE("invalid rebuild and scan preserve existing valid profile rows") {
  fsutil::TempDir data;
  fsutil::TempDir source;
  auto builtin = must(kb::Pack::load_builtin()); auto rt = runtime(data.path());
  Catalog valid(*rt, builtin); const auto profile = must(valid.build_profile(ProfileConfig{}));
  (void)profile;
  const auto before = rows(rt->db());
  write(source.path() / "note.txt", "ChatADHD kernel\n");
  Catalog broken(*rt, missing("alias"));
  CHECK_FALSE(broken.build_profile(ProfileConfig{}));
  ScanConfig scan; scan.sources = {(source.path() / "note.txt").string()}; scan.threads = 1;
  CHECK_FALSE(broken.scan(scan));
  CHECK(rows(rt->db()) == before);
  auto lock = rt->db().lock();
  CHECK(must(rt->db().conn().query_int("SELECT COUNT(*) FROM loom_cat_units")).value_or(-1) == 0);
  CHECK(must(rt->db().conn().query_int("SELECT COUNT(*) FROM loom_cat_sources")).value_or(-1) == 0);
}

TEST_CASE("private alias matching is independent of removed unused numeric weight") {
  auto pack = must(kb::Pack::load_builtin()); kb::Normalizer normalizer(*pack);
  Json expected;
  for (const Json& weight : {Json(3.0), Json(0.0), Json(-9.0), Json("not-consumed"), Json(nullptr)}) {
    SelfProfile profile;
    Json term{{"term", "Boreal"}, {"key", "boreal"}, {"class", "alias"}, {"project", "boreal"},
              {"ambiguous", true}, {"requires_context", Json{{"min", 1}, {"any", Json::array({"compiler"})}}},
              {"negative_context", Json::array({"telescope"})}};
    if (!weight.is_null()) term["weight"] = weight;
    profile.terms = Json::array({term}); auto index = AliasIndex::from_profile(profile, 2);
    Json actual = Json::array({hits(index, "compiler boreal"), hits(index, "boreal telescope"),
                               hits(index, "boréal borealisme otherboreal")});
    REQUIRE(actual[0].size() == 1); CHECK_FALSE(actual[0][0]["trap"].get<bool>());
    REQUIRE(actual[1].size() == 1); CHECK(actual[1][0]["trap"].get<bool>());
    CHECK(actual[2].empty());
    if (expected.is_null()) expected = actual;
    CHECK(json::canonical(actual) == json::canonical(expected));
  }
}
}
