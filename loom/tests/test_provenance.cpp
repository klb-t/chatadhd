#include <doctest/doctest.h>

#include <sys/stat.h>

#include "loom/db.h"
#include "loom/provenance.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

TEST_SUITE("provenance") {
  TEST_CASE("blob store: content addressing, dedup, immutability, streaming") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "p.db");
    BlobStore blobs(td.path() / "blobs", *db);
    auto r1 = unwrap(blobs.put("hello world", "text/plain"));
    CHECK(r1.hash == "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
    CHECK(r1.size == 11);
    CHECK(!r1.existed);
    auto path = blobs.path_for(r1.hash);
    CHECK(path == td.path() / "blobs" / "b9" / "4d" / r1.hash);
    CHECK(std::filesystem::exists(path));
    struct stat st{};
    REQUIRE(::stat(path.c_str(), &st) == 0);
    CHECK((st.st_mode & 0222) == 0);  // read-only
    auto r2 = unwrap(blobs.put("hello world"));
    CHECK(r2.existed);
    CHECK(r2.hash == r1.hash);
    CHECK(unwrap(blobs.read(r1.hash)) == "hello world");
    CHECK(blobs.has(r1.hash));
    CHECK(!blobs.has("00"));
    CHECK(!blobs.read(std::string(64, 'a')));
    LOOM_REQUIRE_OK(blobs.verify(r1.hash));
    CHECK(unwrap(db->conn().query_int("SELECT COUNT(*) FROM loom_blobs")) == std::optional<std::int64_t>(1));

    // Streaming put of a large file (> buffer size) hashes identically.
    std::string big;
    for (int i = 0; i < 300000; ++i) big.push_back(static_cast<char>('a' + i % 26));
    auto src = td.path() / "big.bin";
    LOOM_REQUIRE_OK(fsutil::write_file(src, big));
    auto r3 = unwrap(blobs.put_file(src, "application/octet-stream"));
    CHECK(r3.hash == Sha256::hex(big));
    CHECK(r3.size == 300000);
    CHECK(unwrap(blobs.put_file(src)).existed);
    CHECK(!blobs.put_file(td.path() / "missing"));

    // Tampering is detected.
    std::filesystem::permissions(path, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
    LOOM_REQUIRE_OK(fsutil::write_file(path, "tampered"));
    auto v = blobs.verify(r1.hash);
    REQUIRE(!v);
    CHECK(v.error().code == Errc::Conflict);
  }

  TEST_CASE("sources, provenance records and artifacts") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "p.db");
    ProvenanceStore prov(*db);
    SourceRecord s;
    s.kind = "file";
    s.uri = "/exports/conversations.json";
    s.blob_hash = std::string(64, 'b');
    s.size = 42;
    s.format = "json";
    s.parser = "loom.importer.json";
    s.parser_version = "1";
    s.metadata = Json{{"note", "test"}};
    std::string sid = unwrap(prov.add_source(s));
    CHECK(sid.rfind("src_", 0) == 0);
    auto got = unwrap(prov.get_source(sid));
    REQUIRE(got);
    CHECK(got->uri == s.uri);
    CHECK(got->metadata["note"] == "test");
    CHECK(!got->imported_at.empty());
    CHECK(unwrap(prov.find_sources_by_hash(std::string(64, 'b'))).size() == 1);
    CHECK(unwrap(prov.list_sources()).size() == 1);
    CHECK(unwrap(prov.list_sources(10, "url")).empty());
    SourceRecord bad;
    CHECK(!prov.add_source(bad));

    ProvenanceRecord p;
    p.subject_id = "c_000000000001";
    p.subject_kind = "conversation";
    p.source_id = sid;
    p.locator = Json{{"index", 0}};
    p.transform = "import.json@1";
    p.confidence = 0.9;
    std::string pid = unwrap(prov.add(p));
    CHECK(pid.rfind("pv_", 0) == 0);
    std::vector<ProvenanceRecord> many;
    for (int i = 0; i < 5; ++i) {
      ProvenanceRecord r;
      r.subject_id = "m_00000000000" + std::to_string(i);
      r.subject_kind = "message";
      r.source_id = sid;
      r.locator = Json{{"message_index", i}};
      many.push_back(r);
    }
    CHECK(unwrap(prov.add_many(many)) == 5);
    auto subj = unwrap(prov.for_subject("c_000000000001"));
    REQUIRE(subj.size() == 1);
    CHECK(subj[0].confidence == doctest::Approx(0.9));
    CHECK(subj[0].locator["index"] == 0);
    CHECK(unwrap(prov.for_source(sid)).size() == 6);

    ArtifactRecord a;
    a.kind = "report";
    a.title = "Mega Master";
    a.task_id = "t_000000000001";
    std::string aid = unwrap(prov.add_artifact(a));
    CHECK(unwrap(prov.get_artifact(aid))->title == "Mega Master");
    CHECK(unwrap(prov.list_artifacts(10, "report")).size() == 1);
    CHECK(!unwrap(prov.get_artifact("a_nope")));
  }

  TEST_CASE("event log is append-only with increasing seq and filters") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "p.db");
    EventLog log(*db);
    auto s1 = unwrap(log.append("task:pending", "t_1", Json{{"kind", "x"}}, "in"));
    auto s2 = unwrap(log.append("task:done", "t_1", Json::object(), "in", "out"));
    auto s3 = unwrap(log.append("import:done", "c_1"));
    CHECK(s1 < s2);
    CHECK(s2 < s3);
    CHECK(unwrap(log.last_seq()) == s3);
    EventQuery q;
    q.type = "task:*";
    auto tasks = unwrap(log.query(q));
    REQUIRE(tasks.size() == 2);
    CHECK(tasks[1].output_hash == "out");
    EventQuery q2;
    q2.after_seq = s1;
    CHECK(unwrap(log.query(q2)).size() == 2);
    EventQuery q3;
    q3.subject_id = "c_1";
    CHECK(unwrap(log.query(q3)).size() == 1);
    CHECK(!log.append(""));
  }
}
