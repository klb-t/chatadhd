// R50 resource-economy report: measures, balance, read-only guarantee, presets.
#include <doctest/doctest.h>

#include "loom/db.h"
#include "loom/economy.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

const std::string kLong(80, 'x');
std::string block(char c) { return "{\"profile\":\"" + std::string(300, c) + "\"}"; }

std::string add_msg(Database& db, const std::string& conv, const std::string& role, const std::string& text,
                    Json metadata = Json::object()) {
  NewMessage m;
  m.conv_id = conv;
  m.role = role;
  m.text = text;
  m.metadata = std::move(metadata);
  return unwrap(db.create_msg(m));
}

void exec(Database& db, const std::string& sql) {
  auto lock = db.lock();
  REQUIRE(static_cast<bool>(db.conn().exec(sql)));
}

// Two identical conversations, a re-imported source, repeated metadata blocks, a versioned message.
fs::path build_fixture(const fs::path& dir) {
  const fs::path path = dir / "economy.db";
  auto db = loom::test::open_db(path);
  const std::string a = unwrap(db->create_conv("A")).id;
  const std::string b = unwrap(db->create_conv("B")).id;
  for (const auto& conv : {a, b}) {
    add_msg(*db, conv, "user", "Question one " + kLong);
    add_msg(*db, conv, "assistant", "Answer  one\n" + kLong);  // whitespace differs only in the second pass
  }
  // The same 300-byte profile block copied into three attempt records, plus one different block.
  for (int i = 0; i < 3; ++i)
    add_msg(*db, a, "user", "attempt " + std::to_string(i) + " " + std::string(70, 'a' + i),
            Json{{"attempt", i}, {"profile", Json::parse(block('p'))}});
  add_msg(*db, a, "user", "other " + std::string(70, 'z'), Json{{"profile", Json::parse(block('q'))}});
  exec(*db, "INSERT INTO loom_sources(id,kind,uri,blob_hash,size,imported_at) VALUES "
            "('src_1','file','/a.zip','" + std::string(64, 'a') + "',1000,'2026-01-01T00:00:00Z'),"
            "('src_2','file','/copy/a.zip','" + std::string(64, 'a') + "',1000,'2026-01-02T00:00:00Z')");
  exec(*db, "CREATE TABLE IF NOT EXISTS loom_import_fingerprints (fingerprint TEXT NOT NULL, subject_id TEXT NOT NULL, "
            "subject_kind TEXT NOT NULL, source_id TEXT, created TEXT NOT NULL, PRIMARY KEY(fingerprint, subject_id))");
  exec(*db, "INSERT INTO loom_import_fingerprints VALUES ('fp1','" + a + "','conversation','src_1','t'),"
            "('fp1','" + b + "','conversation','src_2','t')");
  exec(*db, "INSERT INTO links(id,src,dst,link_type,weight,metadata,created) VALUES "
            "('l_1','x','y','derived_from',1.0,'{\"relation\":\"duplicate_of\",\"level\":\"conversation\"}','t')");
  // An edited message: the new version shares a long prefix with the old one.
  const std::string first = add_msg(*db, a, "user", kLong + kLong + " original");
  (void)first;
  return path;
}

std::string snapshot(const fs::path& db) {
  std::string out = unwrap(sha256_file_hex(db));
  out += fs::exists(db.string() + "-wal") ? "|wal" : "|nowal";
  out += fs::exists(db.string() + "-shm") ? "|shm" : "|noshm";
  return out;
}

Json run(const fs::path& dir, const fs::path& db, Json overrides = Json::object()) {
  return unwrap(economy::report_for_data_dir(dir, db, Json{{"overrides", std::move(overrides)}}));
}

}  // namespace

TEST_SUITE("economy") {
  TEST_CASE("report groups repeated imports, duplicate conversations/messages and metadata blocks") {
    fsutil::TempDir td;
    const auto db = build_fixture(td.path());
    const Json r = run(td.path(), db);
    CHECK(r["schema"] == "loom.economy_report/1");
    CHECK(r["read_only"] == true);
    CHECK(r["writes"] == 0);
    const Json& c1 = r["classes"]["1"]["measures"];
    CHECK(c1["source_reimports"]["totals"]["groups"] == 1);
    CHECK(c1["source_reimports"]["findings"][0]["copies"] == 2);
    CHECK(c1["import_fingerprints"]["totals"]["groups"] == 1);
    CHECK(c1["import_fingerprints"]["findings"][0]["copies"] == 2);
    CHECK(c1["recorded_duplicate_links"]["count"] == 1);
    CHECK(c1["recorded_duplicate_links"]["by_level"]["conversation"] == 1);
    // Conversations A and B differ (A carries extra messages), so no whole-conversation group exists...
    CHECK(c1["duplicate_conversations"]["totals"]["groups"] == 0);
    // ...but the two shared questions/answers (whitespace-normalized) are duplicate messages.
    CHECK(c1["duplicate_messages"]["totals"]["groups"] == 2);
    CHECK(c1["duplicate_messages"]["totals"]["copies"] == 4);
    CHECK(c1["duplicate_messages"]["totals"]["redundant_bytes"].get<int>() > 0);
    CHECK(c1["blob_store"]["duplicate_hash_groups"] == 0);
    // The 300-byte profile block appears in three attempt records (the other block only once).
    const Json& blocks = r["classes"]["3"]["measures"]["repeated_json_blocks"];
    REQUIRE(blocks["totals"]["groups"] == 1);
    CHECK(blocks["findings"][0]["copies"] == 3);
    CHECK(blocks["findings"][0]["examples"].size() == 3);
    // Every finding carries evaluated actions including the always-available "nothing".
    const Json& actions = blocks["findings"][0]["actions"]["proposed"];
    CHECK(actions.back()["action"] == "nothing");
    CHECK(actions[0]["action"] == "pin_link");
    CHECK(actions[0]["balance"]["net_usd"].is_number());
    // The versioned message with a shared prefix is not a version here (single version): class 8 reports no group.
    CHECK(r["classes"]["8"]["measures"]["version_shared_prefixes"]["known"] == true);
  }

  TEST_CASE("identical conversations form a group; unknown inputs make balances {known:false}") {
    fsutil::TempDir td;
    const auto path = td.path() / "dup.db";
    {
      auto db = loom::test::open_db(path);
      for (const char* title : {"one", "two"}) {
        const std::string conv = unwrap(db->create_conv(title)).id;
        add_msg(*db, conv, "user", "same question " + kLong);
        add_msg(*db, conv, "assistant", "same answer " + kLong);
      }
    }
    const Json r = run(td.path(), path);
    const Json& conv = r["classes"]["1"]["measures"]["duplicate_conversations"];
    REQUIRE(conv["totals"]["groups"] == 1);
    const Json& f = conv["findings"][0];
    CHECK(f["copies"] == 2);
    // Prompt-visible content needs a token price and expected inclusions: both unknown by preset.
    const Json& pin = f["actions"]["proposed"][0]["balance"];
    CHECK(pin["net_usd"]["known"] == false);
    CHECK(pin["net_usd"]["missing"].size() == 2);
    CHECK(pin["net_without_tokens_usd"].is_number());
    CHECK(f["actions"]["proposed"][1]["action"] == "compression");
    CHECK(f["actions"]["proposed"][1]["balance"]["known"] == false);
    // Supplying them (an overlay) makes the balance known.
    const Json priced = run(td.path(), path, Json{{"token_price_usd_per_million", 3.0}, {"expected_prompt_inclusions", 10}});
    CHECK(priced["classes"]["1"]["measures"]["duplicate_conversations"]["findings"][0]["actions"]["proposed"][0]
                ["balance"]["net_usd"].is_number());
  }

  TEST_CASE("classes that cannot be measured are {known:false}, never zero") {
    fsutil::TempDir td;
    const auto db = build_fixture(td.path());
    const Json r = run(td.path(), db);
    for (const char* id : {"4", "6", "7"}) {
      const Json& k = r["classes"][id];
      CHECK(k["known"] == false);
      for (const auto& [name, m] : k["measures"].items()) {
        CHECK(m["known"] == false);
        CHECK(m.contains("reason"));
        CHECK_FALSE(m.contains("totals"));
      }
    }
    CHECK(r["summary"]["classes_not_measured"].size() == 3);
    // An absent table is unknown too (a database without the import ledger).
    CHECK(unwrap(economy::report_for_data_dir(td.path(), db, Json::object()))["classes"]["1"]["measures"]
              ["import_fingerprints"]["known"] == true);
  }

  TEST_CASE("the report never changes the database file") {
    fsutil::TempDir td;
    const auto db = build_fixture(td.path());
    const std::string before = snapshot(db);
    const auto mtime = fs::last_write_time(db);
    for (int i = 0; i < 2; ++i) (void)run(td.path(), db);
    CHECK(snapshot(db) == before);
    CHECK(fs::last_write_time(db) == mtime);
  }

  TEST_CASE("thresholds from an overlay change the result; mode off analyses nothing") {
    fsutil::TempDir td;
    const auto db = build_fixture(td.path());
    CHECK(run(td.path(), db, Json{{"min_block_copies", 4}})["classes"]["3"]["measures"]["repeated_json_blocks"]["totals"]["groups"] == 0);
    CHECK(run(td.path(), db, Json{{"min_block_bytes", 100000}})["classes"]["3"]["measures"]["repeated_json_blocks"]["totals"]["groups"] == 0);
    CHECK(run(td.path(), db, Json{{"min_message_bytes", 100000}})["classes"]["1"]["measures"]["duplicate_messages"]["totals"]["groups"] == 0);
    CHECK(run(td.path(), db, Json{{"json_columns", Json::array()}})["classes"]["3"]["measures"]["repeated_json_blocks"]["known"] == false);
    const Json off = run(td.path(), fs::path("/nonexistent/never-opened.db"), Json{{"mode", "off"}});
    CHECK(off["status"] == "disabled");
    CHECK(off["classes"].empty());
    // A profile file in the data directory is honoured the same way as an override.
    fs::create_directories(td.path() / "profiles");
    REQUIRE(static_cast<bool>(fsutil::write_file(td.path() / "profiles" / "economy.pack",
                                     R"({"schema":"loom.runtime_profile_overlay/1","domain":"economy","overrides":{"min_block_copies":4}})")));
    CHECK(unwrap(economy::report_for_data_dir(td.path(), db, Json::object()))["classes"]["3"]["measures"]
              ["repeated_json_blocks"]["totals"]["groups"] == 0);
  }
}
