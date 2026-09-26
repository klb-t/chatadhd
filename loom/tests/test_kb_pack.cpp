// kb.h: the paradigm-engine data pack loads and validates; the embedded copy
// equals loom/data; the validator rejects what the closed sets forbid; the
// normalizer, version helpers and lazy schema behave as documented.
#include <doctest/doctest.h>

#include <filesystem>

#include "loom/db.h"
#include "loom/kb.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {

std::filesystem::path data_dir() { return std::filesystem::path(LOOM_TEST_FIXTURES).parent_path().parent_path() / "data"; }

std::map<std::string, Json> dir_docs() {
  std::map<std::string, Json> docs;
  auto pack = unwrap(kb::Pack::load_dir(data_dir()));
  for (const auto& f : pack->files()) docs[f] = pack->file(f);
  return docs;
}

std::string error_of(std::map<std::string, Json> docs) {
  auto r = kb::Pack::from_documents(std::move(docs));
  return r ? std::string() : r.error().message;
}

}  // namespace

TEST_SUITE("kb_pack") {
  TEST_CASE("every file under loom/data loads and validates (schema + cross references)") {
    auto r = kb::Pack::load_dir(data_dir());
    INFO((r ? std::string("ok") : r.error().message));
    REQUIRE(static_cast<bool>(r));
    const auto& pack = **r;
    CHECK(pack.id() == "builtin");
    CHECK(pack.hash().size() == 64);
    // Every JSON file on disk is listed (loaded) and every listed file exists.
    std::size_t on_disk = 0;
    for (const auto& e : std::filesystem::recursive_directory_iterator(data_dir())) {
      on_disk += e.is_regular_file() && e.path().extension() == ".json";
    }
    CHECK(pack.files().size() == on_disk);
    // The eight canonical paradigms with the ground-truth slot names.
    const std::map<std::string, std::vector<std::string>> canonical = {
        {"multiplatform_app", {"name", "purpose", "platforms", "core_language", "ui_framework", "storage", "sync",
                               "architecture_principles", "modules", "current_version", "status"}},
        {"pipeline", {"name", "purpose", "stages", "inputs", "outputs", "tools", "storage", "status"}},
        {"brainstorm", {"topic", "options", "chosen", "rejected", "open_questions"}},
        {"specification", {"subject", "requirements", "invariants", "interfaces", "open_questions"}},
        {"coding_philosophy", {"principles"}},
        {"codebase", {"repo", "languages", "modules", "entry_points", "tests"}},
        {"version_history", {"versions"}},
        {"agent_system", {"name", "purpose", "roles", "tools", "status"}}};
    for (const auto& [id, slots] : canonical) {
      INFO(id);
      const Json& p = pack.paradigm(id);
      REQUIRE(p.is_object());
      std::set<std::string> have;
      for (const auto& s : p["slots"]) have.insert(s["name"].get<std::string>());
      for (const auto& s : slots) {
        INFO(s);
        CHECK(have.count(s) == 1);
      }
    }
    CHECK(pack.paradigm_ids().size() == 8);
    CHECK(pack.rule("r.storage_local_first").is_object());
    CHECK(pack.principle("p.kod_ne_dane").is_object());
    CHECK(pack.policy("evidence_encoding").is_object());
    CHECK(pack.lexicon("gazetteer").is_object());
    CHECK(pack.profile("self").is_object());
    CHECK(pack.manifest()["files"].size() == pack.files().size());
  }

  TEST_CASE("the embedded pack is byte-for-byte the loom/data directory (run tools/gen_kb_pack.py)") {
    auto builtin = unwrap(kb::Pack::load_builtin());
    auto dir = unwrap(kb::Pack::load_dir(data_dir()));
    CHECK(builtin->files() == dir->files());
    CHECK(builtin->hash() == dir->hash());
  }

  TEST_CASE("item cue lexicon mirrors the archive classifier (run tools/gen_kb_lexicons.py)") {
    auto pack = unwrap(kb::Pack::load_dir(data_dir()));
    const Json& cues = pack->lexicon("item_cues");
    CHECK(cues["cues"].size() > 150);
    CHECK(cues["type_order"].size() == 9);
    CHECK(pack->lexicon("stopwords_base")["pl"].size() > 100);
  }

  TEST_CASE("validator rejects names outside the closed sets and dangling references") {
    auto base = dir_docs();
    {
      auto d = base;
      d["paradigms/pipeline.json"]["slots"][0]["bind"][0]["kind"] = "sql_query";
      CHECK(error_of(d).find("not in the closed set") != std::string::npos);
    }
    {
      auto d = base;
      d["paradigms/multiplatform_app.json"]["slots"][2]["infer"] = Json::array({"r.does_not_exist"});
      CHECK(error_of(d).find("unknown rule 'r.does_not_exist'") != std::string::npos);
    }
    {
      auto d = base;
      d["rules/inference_rules.json"]["rules"][0]["expected_property"]["expr"]["op"] = "looks_right";
      CHECK(error_of(d).find("unknown predicate op 'looks_right'") != std::string::npos);
    }
    {
      auto d = base;
      d["rules/inference_rules.json"]["rules"][0]["stratum"] = 2;  // derived rule in the extrapolation stratum
      CHECK(error_of(d).find("must produce 'extrapolated'") != std::string::npos);
    }
    {
      auto d = base;
      d["rules/checks.json"]["checks"][0]["principle"] = "p.nope";
      CHECK(error_of(d).find("unknown principle") != std::string::npos);
    }
    {
      auto d = base;
      d["policy/evidence_encoding.json"]["evidence"].erase("inferred");
      CHECK(error_of(d).find("missing evidence class") != std::string::npos);
    }
    {
      auto d = base;
      d["lexicons/extra.json"] = Json{{"schema", "loom.kb.cues/1"}, {"classes", Json::object()}};
      CHECK(error_of(d).find("not listed in pack.json") != std::string::npos);
    }
    {
      auto d = base;
      d["paradigms/codebase.json"]["id"] = "code_base";
      CHECK(error_of(d).find("must equal the file name") != std::string::npos);
    }
  }

  TEST_CASE("overlay files replace built-in ones and change the hash; an invalid overlay is rejected") {
    fsutil::TempDir td;
    auto base = unwrap(kb::Pack::load_builtin());
    CHECK(unwrap(kb::Pack::load_with_overlay(td.path() / "missing"))->hash() == base->hash());
    Json th = base->policy("thresholds");
    th["paradigm"]["tau_inferred"] = 0.6;
    LOOM_REQUIRE_OK(fsutil::ensure_dir(td.path() / "policy"));
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "policy/thresholds.json", json::dump(th)));
    auto over = unwrap(kb::Pack::load_with_overlay(td.path()));
    CHECK(over->hash() != base->hash());
    CHECK(over->policy("thresholds")["paradigm"]["tau_inferred"].get<double>() == doctest::Approx(0.6));
    th["paradigm"]["extrapolation_cap"] = 7;
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "policy/thresholds.json", json::dump(th)));
    CHECK(!kb::Pack::load_with_overlay(td.path()));
  }

  TEST_CASE("normalizer: PL/EN match keys, stop words, glossary, language guess") {
    auto pack = unwrap(kb::Pack::load_builtin());
    kb::Normalizer n(*pack);
    CHECK(n.fold("Zażółć GĘŚLĄ") == "zazolc gesla");
    CHECK(n.match_key("grafu") == "graf");
    CHECK(n.match_key("Grafie") == "graf");
    CHECK(n.match_key("czatu") == "czat");
    CHECK(n.match_key("serializacji") == n.match_key("serializacja"));
    CHECK(n.match_key("modułów") == n.match_key("moduły"));
    CHECK(n.match_key("pipelines") == "pipeline");
    CHECK(n.match_key("stores") == "store");
    CHECK(n.phrase_key("grafu wiedzy") == n.phrase_key("graf wiedzy"));
    CHECK(n.phrase_key("graf wiedzy") == "knowledge graph");  // glossary -> English key
    CHECK(n.phrase_key("czat ADHD") == n.phrase_key("chat adhd"));
    CHECK(n.is_stopword("się"));
    CHECK(n.is_stopword("sie"));
    CHECK(n.is_stopword("nowa"));
    CHECK(!n.is_stopword("loom"));
    auto toks = n.tokens("Port to C++ and C# (v0.9)");
    CHECK(std::find(toks.begin(), toks.end(), "c++") != toks.end());
    CHECK(std::find(toks.begin(), toks.end(), "c#") != toks.end());
    CHECK(n.guess_lang("To jest aplikacja, która działa na Androidzie i desktopie.") == kb::Lang::Pl);
    CHECK(n.guess_lang("This is an application that runs on Android and desktop.") == kb::Lang::En);
  }

  TEST_CASE("normalizer golden cases: Polish -cji/-cja, English plurals and verb forms, glossary both ways") {
    auto pack = unwrap(kb::Pack::load_builtin());
    kb::Normalizer n(*pack);
    // Polish: -cji/-cja and case endings meet in one key; diacritics fold.
    for (const char* w : {"serializacja", "serializacji", "serializację", "serializacją", "serializacjach"}) {
      INFO(w);
      CHECK(n.match_key(w) == "serializacj");
    }
    CHECK(n.match_key("specyfikacji") == n.match_key("specyfikacja"));
    CHECK(n.match_key("wersji") == n.match_key("wersjach"));
    CHECK(n.match_key("modułów") == "modul");
    CHECK(n.match_key("pamięci") == n.match_key("pamięć"));
    CHECK(n.token_lang("serializacji") == kb::Lang::Pl);  // marker suffix, no diacritic
    CHECK(n.token_lang("pipeline") == kb::Lang::En);
    // English: plural -s/-es/-ies, keep endings, verbal forms.
    CHECK(n.match_key("modules") == "module");
    CHECK(n.match_key("boxes") == "box");
    CHECK(n.match_key("matches") == "match");
    CHECK(n.match_key("classes") == "class");
    CHECK(n.match_key("policies") == "policy");
    CHECK(n.match_key("status") == "status");
    CHECK(n.match_key("analysis") == "analysis");
    CHECK(n.match_key("stored") == "store");
    CHECK(n.match_key("storing") == "store");
    CHECK(n.match_key("created") == "create");
    CHECK(n.match_key("running") == "run");
    CHECK(n.match_key("string") == "string");
    CHECK(n.match_key("serializing") == n.match_key("serialization"));
    // Glossary: one English key for both languages, inflected Polish included.
    CHECK(n.phrase_key("czat ADHD") == "chat adhd");
    CHECK(n.phrase_key("Czatu ADHD") == "chat adhd");
    CHECK(n.phrase_key("chat ADHD") == "chat adhd");
    CHECK(n.phrase_key("grafu wiedzy") == "knowledge graph");
    CHECK(n.phrase_key("knowledge graphs") == "knowledge graph");
    CHECK(n.phrase_key("potoku") == n.phrase_key("pipelines"));
    CHECK(n.phrase_key("modułów") == n.phrase_key("modules"));
    CHECK(n.phrase_key("zasady") == "principle");
    CHECK(n.phrase_key("specyfikacji") == "specification");
    // Without the glossary the keys stay in their own language.
    CHECK(n.phrase_key("czat ADHD", false) == "czat adhd");
    CHECK(n.phrase_key("grafu wiedzy", false) == n.phrase_key("graf wiedzy", false));
    // An explicit language hint wins for undiacritised Polish words.
    CHECK(n.phrase_key("zasady projektu", false, kb::Lang::Pl) == n.phrase_key("zasadach", false, kb::Lang::Pl));
    CHECK(n.phrase_key("ADHD, czat!") == n.phrase_key("adhd chat"));
  }

  TEST_CASE("versions and stable ids") {
    CHECK(kb::normalize_version("v0.07.09") == "0.7.9");
    CHECK(kb::normalize_version("0.10") == "0.10");
    CHECK(kb::normalize_version("3") == "");
    CHECK(kb::normalize_version("1.2.3.4") == "");
    CHECK(kb::normalize_version("0.9-beta") == "");
    CHECK(kb::compare_versions("0.9", "0.9.0") == 0);
    CHECK(kb::compare_versions("0.10", "0.9.5") > 0);
    CHECK(kb::compare_versions("0.7.10", "0.7.9") > 0);
    CHECK(kb::stable_id("e_", "project|chatadhd") == kb::stable_id("e_", "project|chatadhd"));
    CHECK(kb::stable_id("e_", "project|chatadhd").size() == 18);  // 16 hex
    CHECK(kb::stable_id("n_", "project|chatadhd", 12).size() == 14);  // Python id format
    CHECK(kb::stable_id("e_", "project|chatadhd") != kb::stable_id("e_", "project|loom"));
  }

  TEST_CASE("evidence vocabulary maps onto the ground-truth classes") {
    CHECK(kb::gt_evidence(kb::Evidence::Observed) == "observed");
    CHECK(kb::gt_evidence(kb::Evidence::Derived) == "observed");
    CHECK(kb::gt_evidence(kb::Evidence::User) == "observed");
    CHECK(kb::gt_evidence(kb::Evidence::Inferred) == "inferable");
    CHECK(kb::gt_evidence(kb::Evidence::Absent) == "absent");
    CHECK(kb::gt_evidence(kb::Evidence::Extrapolated).empty());
    for (auto e : {kb::Evidence::Observed, kb::Evidence::Derived, kb::Evidence::Inferred, kb::Evidence::Extrapolated,
                   kb::Evidence::Absent, kb::Evidence::User}) {
      CHECK(kb::evidence_from_string(kb::to_string(e)) == e);
    }
    auto pack = unwrap(kb::Pack::load_builtin());
    auto ep = unwrap(kb::ExpectedProperty::from_json(pack->rule("r.storage_local_first")["expected_property"]));
    CHECK(ep.render().rfind("all(in_class(storage.embedded), consistent_with(p.local_first)", 0) == 0);
    CHECK(!kb::ExpectedProperty::from_json(Json{{"expr", Json{{"op", "vibes"}}}}));
  }

  TEST_CASE("kb tables are created lazily and idempotently, core schema untouched") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "kb.db");
    CHECK(!kb::has_schema(*db));
    {
      auto lk = db->lock();
      CHECK(!db->conn().has_table("loom_kb_claims"));
    }
    LOOM_REQUIRE_OK(kb::ensure_schema(*db));
    LOOM_REQUIRE_OK(kb::ensure_schema(*db));
    CHECK(kb::has_schema(*db));
    auto lk = db->lock();
    for (const char* t : {"loom_kb_entities", "loom_kb_claims", "loom_kb_instances", "loom_kb_slot_values",
                          "loom_kb_judgements", "loom_kb_principles", "loom_kb_status_records"}) {
      INFO(t);
      CHECK(db->conn().has_column(t, "body"));
    }
    CHECK(!db->conn().has_table("loom_kb_facts"));
    CHECK(unwrap(db->get_meta("loom_schema_version")).value_or("") == "1");  // wave-1 tables unchanged
    CHECK(unwrap(db->get_meta("loom_kb_schema_version")).value_or("") == std::to_string(kb::kKbSchemaVersion));
    CHECK(unwrap(db->schema_version()) == 4);
  }

  TEST_CASE("the v1 draft layout is upgraded in place (empty tables recreated, facts dropped)") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "kb.db");
    {
      auto lk = db->lock();
      LOOM_REQUIRE_OK(db->conn().exec(
          "CREATE TABLE loom_kb_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
          "INSERT INTO loom_kb_meta VALUES ('schema_version', '1');"
          "CREATE TABLE loom_kb_facts (run_id TEXT NOT NULL, id TEXT NOT NULL, PRIMARY KEY (run_id, id));"
          "CREATE TABLE loom_kb_entities (run_id TEXT NOT NULL, id TEXT NOT NULL, kind TEXT NOT NULL, "
          "canonical_key TEXT NOT NULL, label TEXT NOT NULL, PRIMARY KEY (run_id, id));"
          "CREATE TABLE loom_kb_judgements (id TEXT PRIMARY KEY, semantic_key TEXT NOT NULL, target_kind TEXT NOT NULL,"
          " verdict TEXT NOT NULL, created TEXT NOT NULL);"));
    }
    LOOM_REQUIRE_OK(kb::ensure_schema(*db));
    auto lk = db->lock();
    CHECK(!db->conn().has_table("loom_kb_facts"));
    CHECK(db->conn().has_column("loom_kb_entities", "body"));
    CHECK(!db->conn().has_column("loom_kb_entities", "label"));  // recreated (it was empty)
    CHECK(db->conn().has_column("loom_kb_judgements", "seq"));
    CHECK(!db->conn().has_column("loom_kb_judgements", "semantic_key"));
  }
}
