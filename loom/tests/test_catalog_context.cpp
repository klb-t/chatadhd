// Independent, authored mechanism regressions for local alias evidence.
// No development/holdout relevance labels or fixture vocabulary are loaded.
#include <doctest/doctest.h>

#include <cmath>
#include <map>

#include "catalog/catalog_internal.h"
#include "loom/catalog.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::catalog;
using namespace loom::catalog::internal;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

Json ambiguous_term() {
  return Json{{"term", "Boreal"}, {"key", "boreal"}, {"class", "alias"}, {"project", "boreal"},
              {"ambiguous", true}, {"requires_context", Json{{"any", Json::array({"compiler"})}, {"min", 1}}},
              {"negative_context", Json::array({"telescope"})}};
}

SelfProfile local_profile() {
  SelfProfile p;
  p.terms = Json::array({ambiguous_term()});
  return p;
}

std::shared_ptr<const kb::Pack> pack_with_context_radius(int radius) {
  auto builtin = unwrap(kb::Pack::load_builtin());
  std::map<std::string, Json> documents;
  for (const auto& file : builtin->files()) documents[file] = builtin->file(file);
  documents["policy/thresholds.json"]["catalog"]["context_window_tokens"] = radius;
  documents["profiles/self.json"]["projects"][0]["aliases"] = Json::array({
      Json{{"t", "Boreal"}, {"ambiguous", true},
           {"requires_context", Json{{"any", Json::array({"compiler"})}, {"min", 1}}},
           {"negative_context", Json::array({"telescope"})}}});
  return unwrap(kb::Pack::from_documents(std::move(documents)));
}

std::unique_ptr<Runtime> open_rt(const fs::path& path) {
  RuntimeOptions options;
  options.data_dir = path.string();
  options.start_workers = false;
  return unwrap(Runtime::open(options));
}

std::string padding(int words) {
  std::string result;
  for (int i = 0; i < words; ++i) result += " filler";
  return result;
}

Json conversation(std::string id, std::string title, std::string text) {
  return Json{{"id", id}, {"title", title}, {"create_time", 1700000000}, {"current_node", "n"},
              {"mapping", Json{{"n", Json{{"id", "n"}, {"parent", nullptr}, {"children", Json::array()},
                  {"message", Json{{"id", "n"}, {"author", Json{{"role", "user"}}},
                      {"content", Json{{"content_type", "text"}, {"parts", Json::array({text})}}},
                      {"create_time", 1700000001}}}}}}}};
}

}  // namespace

TEST_SUITE("catalog_context") {
  TEST_CASE("ambiguous aliases use occurrence-local context on both sides") {
    auto idx = AliasIndex::from_profile(local_profile(), 2);
    for (const std::string text : {"boreal compiler", "compiler boreal", "boreal žółć compiler"}) {
      auto hits = idx.find(text, 20);
      REQUIRE(hits.size() == 1);
      CHECK_FALSE(hits[0].trap);
    }
    CHECK(idx.find("boreal" + padding(2) + " compiler", 20).empty());
    CHECK(idx.find("compiler" + padding(2) + " boreal", 20).empty());

    // The late positive cue authorizes only its own occurrence, not the
    // earlier ambiguous word separated by another topic's vocabulary.
    std::string text = "boreal" + padding(5) + " boreal compiler";
    auto hits = idx.find(text, 20);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].offset == static_cast<std::int64_t>(text.rfind("boreal")));
  }

  TEST_CASE("negative context is local too and still blocks a nearby trap") {
    auto idx = AliasIndex::from_profile(local_profile(), 2);
    auto local_trap = idx.find("boreal telescope" + padding(5) + " compiler", 20);
    REQUIRE(local_trap.size() == 1);
    CHECK(local_trap[0].trap);
    auto local_signal = idx.find("boreal compiler" + padding(5) + " telescope", 20);
    REQUIRE(local_signal.size() == 1);
    CHECK_FALSE(local_signal[0].trap);
  }

  TEST_CASE("default radius is thirty tokens and policy controls pack and profile paths") {
    auto idx = AliasIndex::from_profile(local_profile());
    REQUIRE(idx.find("boreal" + padding(29) + " compiler", 20).size() == 1);
    CHECK(idx.find("boreal" + padding(30) + " compiler", 20).empty());

    auto pack = pack_with_context_radius(2);
    CHECK(alias_context_window_tokens(*pack) == 2);
    auto scan_index = AliasIndex::from_pack(*pack);
    auto score_index = AliasIndex::from_profile(local_profile(), alias_context_window_tokens(*pack));
    for (auto* index : {&scan_index, &score_index}) {
      REQUIRE(index->find("boreal filler compiler", 20).size() == 1);
      CHECK(index->find("boreal filler filler compiler", 20).empty());
    }
  }

  TEST_CASE("version binding requires a nearby accepted identity alias") {
    Mention principle;
    principle.kind = "principle";
    principle.offset = 0;
    Mention identity = principle;
    identity.kind = "alias";
    Mention trap = identity;
    trap.trap = true;
    std::string text = "boreal 2.3";
    CHECK(find_version_mentions(text, {principle}, 80).empty());
    CHECK(find_version_mentions(text, {trap}, 80).empty());
    CHECK(find_version_mentions(text, {}, 80).empty());
    CHECK(find_version_mentions(text, {identity}, 80).size() == 1);
    identity.offset = 500;
    CHECK(find_version_mentions(text, {principle, identity}, 80).empty());
  }

  TEST_CASE("scoring exposes typed counts while rejecting trap titles and principle-bound versions") {
    fsutil::TempDir data_dir;
    fsutil::TempDir source_dir;
    Json conversations = Json::array({
        conversation("trap-title", "Boreal telescope", "Weather remains calm."),
        conversation("accepted-title", "Folio", "Weather remains calm."),
        conversation("principle", "note", "policy lens 2.3"),
        conversation("identity", "note", "Folio 2.3"),
        conversation("combined", "note", "Folio policy lens 2.3"),
        conversation("distant", "note", "boreal" + padding(31) + " compiler"),
        conversation("nearby", "note", "boreal compiler")});
    LOOM_REQUIRE_OK(fsutil::write_file(source_dir.path() / "conversations.json", json::dump(conversations)));
    RuntimeOptions options;
    options.data_dir = data_dir.path().string();
    options.start_workers = false;
    auto rt = unwrap(Runtime::open(options));
    Catalog cat(*rt, unwrap(rt->knowledge().pack()));
    ScanConfig scan;
    scan.sources = {source_dir.path().string()};
    unwrap(cat.scan(scan));
    auto profile = unwrap(cat.build_profile(ProfileConfig{}));
    profile.terms = local_profile().terms;
    profile.terms.push_back(Json{{"term", "Folio"}, {"key", "folio"}, {"class", "alias"}, {"project", "folio"}});
    profile.terms.push_back(Json{{"term", "policy lens"}, {"key", "policy lens"}, {"class", "principle"}});
    {
      auto lk = rt->db().lock();
      unwrap(rt->db().conn().run("UPDATE loom_cat_profiles SET body = ? WHERE id = ?", json::dump(profile.to_json()), profile.id));
    }
    ScoreConfig config;
    config.profile_id = profile.id;
    config.max_passes = 1;
    auto stats = unwrap(cat.score(config));
    CHECK(json::get_int(stats, "scoring_evidence_version") == 3);
    CHECK(stats["legacy_combined_features"] == Json::array({"id_hits", "class_diversity", "code_evidence"}));
    auto units = unwrap(cat.query(UnitQuery{}));
    REQUIRE(units.size() == 7);
    for (const auto& unit : units) {
      auto preview = unwrap(cat.preview(unit.unit.id));
      const auto& features = preview["score"]["features"];
      CAPTURE(unit.ext_id);
      CAPTURE(features.dump());
      if (unit.ext_id == "trap-title") CHECK(json::get_number(features, "title") == 0.0);
      if (unit.ext_id == "accepted-title") CHECK(json::get_number(features, "title") == 1.0);
      if (unit.ext_id == "principle") {
        CHECK(json::get_number(features, "identity_alias_hits") == 0.0);
        CHECK(json::get_number(features, "principle_hits") == 1.0);
        CHECK(json::get_number(features, "id_hits") == 1.0);  // unchanged legacy transform
        CHECK(json::get_number(features, "version_mention") == 0.0);
      }
      if (unit.ext_id == "identity") {
        CHECK(json::get_number(features, "identity_alias_hits") == 1.0);
        CHECK(json::get_number(features, "principle_hits") == 0.0);
        CHECK(json::get_number(features, "version_mention") == 1.0);
      }
      if (unit.ext_id == "combined") {
        CHECK(json::get_number(features, "identity_alias_hits") == 1.0);
        CHECK(json::get_number(features, "principle_hits") == 1.0);
        CHECK(json::get_number(features, "id_hits") == doctest::Approx(1.0 + std::log(2.0)));
      }
      if (unit.ext_id == "distant") CHECK(json::get_number(features, "identity_alias_hits") == 0.0);
      if (unit.ext_id == "nearby") CHECK(json::get_number(features, "identity_alias_hits") == 1.0);
    }
  }

  TEST_CASE("legacy checkpoints migrate and changed scan inputs refresh only derived evidence") {
    fsutil::TempDir data_dir;
    fsutil::TempDir source_dir;
    const auto path = source_dir.path() / "notes.txt";
    const std::string source = "Boreal filler filler compiler quartz marble granite amber.";
    LOOM_REQUIRE_OK(fsutil::write_file(path, source));
    auto rt = open_rt(data_dir.path());
    auto narrow_pack = pack_with_context_radius(2);
    Catalog cat(*rt, narrow_pack);
    ScanConfig scan;
    scan.sources = {path.string()};
    scan.sketch.top_k = 1;
    auto initial = unwrap(cat.scan(scan));
    REQUIRE(json::get_int(initial, "new") == 1);
    auto units = unwrap(cat.query(UnitQuery{}));
    REQUIRE(units.size() == 1);
    const auto original = units[0];
    REQUIRE(original.mentions.empty());
    const auto first_hash = json::get_string(initial, "input_hash");
    REQUIRE_FALSE(first_hash.empty());
    std::string creation_time;
    {
      auto lk = rt->db().lock();
      creation_time = *unwrap(rt->db().conn().query_text("SELECT created FROM loom_cat_units WHERE id = ?", original.unit.id));
      // Simulate a pre-fingerprint database with stale retrieval evidence and
      // a completed checkpoint; the source and original locator remain intact.
      auto legacy = original;
      legacy.unit.attrs.erase("catalog_scan_fingerprint");
      legacy.mentions = Json::array({Json{{"kind", "alias"}, {"key", "stale"}, {"offset", 0}}});
      unwrap(rt->db().conn().run("UPDATE loom_cat_units SET body = ? WHERE id = ?", json::dump(legacy.to_json()), original.unit.id));
      unwrap(rt->db().conn().exec("DROP TABLE loom_cat_checkpoint"));
      unwrap(rt->db().conn().exec(
          "CREATE TABLE loom_cat_checkpoint (source_id TEXT NOT NULL, member TEXT NOT NULL DEFAULT '', "
          "element_ordinal INTEGER NOT NULL DEFAULT -1, byte_offset INTEGER NOT NULL DEFAULT 0, "
          "done INTEGER NOT NULL DEFAULT 0, updated TEXT NOT NULL DEFAULT '', PRIMARY KEY(source_id, member))"));
      unwrap(rt->db().conn().run("INSERT INTO loom_cat_checkpoint (source_id, member, done) VALUES (?, '', 1)", original.unit.source));
    }
    auto migrated = unwrap(cat.scan(scan));
    CHECK(json::get_int(migrated, "refreshed") == 1);
    units = unwrap(cat.query(UnitQuery{}));
    REQUIRE(units.size() == 1);
    CHECK(units[0].mentions.empty());
    CHECK(units[0].unit.id == original.unit.id);
    CHECK(units[0].unit.locator.to_json() == original.unit.locator.to_json());
    CHECK(units[0].content_hash == original.content_hash);
    CHECK(unwrap(cat.read_unit(original.unit.id)) == source);
    CHECK(json::get_int(unwrap(cat.scan(scan)), "units") == 0);

    // A changed pack reaches both checkpoint and content dedup layers.
    auto wide_pack = pack_with_context_radius(4);
    Catalog wide(*rt, wide_pack);
    auto rescored_input = unwrap(wide.scan(scan));
    CHECK(json::get_int(rescored_input, "refreshed") == 1);
    CHECK(json::get_string(rescored_input, "input_hash") != first_hash);
    units = unwrap(wide.query(UnitQuery{}));
    REQUIRE(units.size() == 1);
    REQUIRE(units[0].mentions.size() == 1);
    CHECK(json::get_string(units[0].mentions[0], "kind") == "alias");

    // Sketch parameters also invalidate an otherwise identical source.
    scan.sketch.top_k = 8;
    auto new_sketch = unwrap(wide.scan(scan));
    CHECK(json::get_int(new_sketch, "refreshed") == 1);
    CHECK(json::get_string(new_sketch, "input_hash") != json::get_string(rescored_input, "input_hash"));
    {
      auto lk = rt->db().lock();
      CHECK(rt->db().conn().has_column("loom_cat_checkpoint", "input_hash"));
      CHECK(*unwrap(rt->db().conn().query_text("SELECT created FROM loom_cat_units WHERE id = ?", original.unit.id)) == creation_time);
      auto stored = *unwrap(rt->db().conn().query_text("SELECT sketch FROM loom_cat_units WHERE id = ?", original.unit.id));
      CHECK(unwrap(json::parse(stored))["top_terms"].size() > 1);
      CHECK(*unwrap(rt->db().conn().query_text("SELECT input_hash FROM loom_cat_checkpoint WHERE source_id = ?", original.unit.source)) ==
            json::get_string(new_sketch, "input_hash"));
    }
    CHECK(unwrap(wide.read_unit(original.unit.id)) == source);
    CHECK(json::get_int(unwrap(wide.scan(scan)), "units") == 0);
  }

  TEST_CASE("missing original bytes cannot reuse stale identity or principle context") {
    fsutil::TempDir data_dir;
    fsutil::TempDir source_dir;
    const auto path = source_dir.path() / "notes.txt";
    LOOM_REQUIRE_OK(fsutil::write_file(path, "Boreal compiler 2.3"));
    auto rt = open_rt(data_dir.path());
    auto pack = pack_with_context_radius(2);
    Catalog cat(*rt, pack);
    ScanConfig scan;
    scan.sources = {path.string()};
    unwrap(cat.scan(scan));
    auto profile = unwrap(cat.build_profile(ProfileConfig{}));
    ScoreConfig score;
    score.profile_id = profile.id;
    auto first = unwrap(cat.score(score));
    CHECK(json::get_int(first, "identity_unavailable") == 0);
    auto units = unwrap(cat.query(UnitQuery{}));
    REQUIRE(units.size() == 1);
    auto available = unwrap(cat.preview(units[0].unit.id));
    CHECK(json::get_number(available["score"]["features"], "identity_alias_hits") == 1.0);
    CHECK(json::get_number(available["score"]["features"], "identity_source_available") == 1.0);
    REQUIRE(fs::remove(path));
    auto missing = unwrap(cat.score(score));
    CHECK(json::get_int(missing, "identity_unavailable") == 1);
    auto unavailable = unwrap(cat.preview(units[0].unit.id));
    const auto& features = unavailable["score"]["features"];
    CHECK(json::get_number(features, "identity_alias_hits") == 0.0);
    CHECK(json::get_number(features, "principle_hits") == 0.0);
    CHECK(json::get_number(features, "version_mention") == 0.0);
    CHECK(json::get_number(features, "identity_source_available") == 0.0);
  }
}
