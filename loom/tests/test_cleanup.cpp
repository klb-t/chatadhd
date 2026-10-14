// S2/R22: cleanup of abandoned upload parts and unreferenced blobs (loom/cleanup.h).
#include <doctest/doctest.h>

#include <fstream>

#include "loom/cleanup.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

void write(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << s;
}

void age(const fs::path& p, int hours) { fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::hours(hours)); }

const std::string kOld = "2020-01-01T00:00:00Z";

void backdate(Database& db, const std::string& hash) {
  auto lk = db.lock();
  REQUIRE(db.conn().run("UPDATE loom_blobs SET created = ? WHERE hash = ?", kOld, hash));
}

}  // namespace

TEST_SUITE("cleanup") {
  TEST_CASE("abandoned up_*.part files older than the setting are removed; others stay; 0 keeps everything") {
    fsutil::TempDir td;
    auto db = loom::test::open_db(td.path() / "c.db");
    BlobStore blobs(td.path() / "blobs", *db);
    const fs::path dir = td.path() / "tmp" / "uploads";
    write(dir / "up_0123456789ab.part", "old");
    write(dir / "up_aaaaaaaaaaaa.part", "young");
    write(dir / "up_0123456789ab.txt", "not a part");
    write(dir / "other.part", "not ours");
    write(dir / "up_zzzzzzzzzzzz.part", "not hex");
    age(dir / "up_0123456789ab.part", 30);
    age(dir / "up_0123456789ab.txt", 30);
    age(dir / "other.part", 30);
    age(dir / "up_zzzzzzzzzzzz.part", 30);

    CleanupOptions keep;
    keep.upload_part_max_age_hours = 0;
    auto r = unwrap(run_cleanup(*db, blobs, td.path(), keep));
    CHECK(r["upload_parts"]["removed"] == 0);
    CHECK(fs::exists(dir / "up_0123456789ab.part"));

    r = unwrap(run_cleanup(*db, blobs, td.path(), CleanupOptions{}));  // preset 24 h
    CHECK(r["upload_parts"]["removed"] == 1);
    CHECK(r["upload_parts"]["bytes"] == 3);
    CHECK_FALSE(fs::exists(dir / "up_0123456789ab.part"));
    CHECK(fs::exists(dir / "up_aaaaaaaaaaaa.part"));
    CHECK(fs::exists(dir / "up_0123456789ab.txt"));
    CHECK(fs::exists(dir / "other.part"));
    CHECK(fs::exists(dir / "up_zzzzzzzzzzzz.part"));
  }

  TEST_CASE("unreferenced old blobs: off by preset, dry run first, referenced blobs are never touched") {
    fsutil::TempDir td;
    auto db = loom::test::open_db(td.path() / "c.db");
    BlobStore blobs(td.path() / "blobs", *db);
    const auto orphan = unwrap(blobs.put("orphan bytes", "text/plain"));
    const auto by_node = unwrap(blobs.put("node bytes", "text/plain"));
    const auto by_json = unwrap(blobs.put("json bytes", "text/plain"));
    const auto young = unwrap(blobs.put("young bytes", "text/plain"));
    const auto by_source = unwrap(blobs.put("source bytes", "text/plain"));
    for (const auto& h : {orphan.hash, by_node.hash, by_json.hash, by_source.hash}) backdate(*db, h);
    NodeOptions o;
    o.metadata = Json{{"blob_hash", by_node.hash}};
    unwrap(db->create_node("blob:x", "blob", o));
    {
      auto lk = db->lock();
      REQUIRE(db->conn().run("INSERT INTO loom_sources (id, kind, blob_hash, size, imported_at) VALUES ('s1','file',?,1,'2020-01-01T00:00:00Z')", by_source.hash));
    }
    write(td.path() / "memory.json", "{\"note\":\"see " + by_json.hash + "\"}");

    CleanupOptions opts;  // preset: blobs off
    auto r = unwrap(run_cleanup(*db, blobs, td.path(), opts));
    CHECK(r["orphan_blobs"]["status"] == "off");
    CHECK(blobs.has(orphan.hash));

    opts.orphan_blob_max_age_days = 7;  // dry run is the preset
    r = unwrap(run_cleanup(*db, blobs, td.path(), opts));
    CHECK(r["orphan_blobs"]["status"] == "dry_run");
    CHECK(r["orphan_blobs"]["candidates"] == 1);
    CHECK(r["orphan_blobs"]["hashes"][0] == orphan.hash);
    CHECK(blobs.has(orphan.hash));

    opts.orphan_blob_dry_run = false;
    r = unwrap(run_cleanup(*db, blobs, td.path(), opts));
    CHECK(r["orphan_blobs"]["status"] == "deleted");
    CHECK(r["orphan_blobs"]["removed"] == 1);
    CHECK_FALSE(blobs.has(orphan.hash));
    for (const auto& h : {by_node.hash, by_json.hash, by_source.hash, young.hash}) CHECK(blobs.has(h));
    {
      auto lk = db->lock();
      CHECK(unwrap(db->conn().query_int("SELECT COUNT(*) FROM loom_blobs")).value() == 4);
    }
  }

  TEST_CASE("the cleanup config key: presets, overrides, wrong types refused") {
    fsutil::TempDir td;
    Config cfg(td.path() / "config.json");
    auto o = unwrap(cleanup_options(cfg));
    CHECK(o.upload_part_max_age_hours == 24);
    CHECK(o.orphan_blob_max_age_days == 0);
    CHECK(o.orphan_blob_dry_run);
    cfg.set("cleanup", Json{{"upload_part_max_age_hours", 0}, {"orphan_blob_max_age_days", 7}, {"orphan_blob_dry_run", false}});
    o = unwrap(cleanup_options(cfg));
    CHECK(o.upload_part_max_age_hours == 0);
    CHECK(o.orphan_blob_max_age_days == 7);
    CHECK_FALSE(o.orphan_blob_dry_run);
    cfg.set("cleanup", "x");
    CHECK_FALSE(cleanup_options(cfg));
    cfg.set("cleanup", Json{{"orphan_blob_max_age_days", -1}});
    CHECK_FALSE(cleanup_options(cfg));
  }
}
