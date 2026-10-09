// Reference reads use the same product Catalog/C ABI consumers as snapshot
// reads. Safe ZIP fixtures make payload retention observable in DB/WAL, blobs,
// logs and the operation's temporary directory.
#include <doctest/doctest.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#if defined(__linux__)
#include <sys/inotify.h>
#include <unistd.h>
#endif

#include "loom/catalog.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/log.h"
#include "loom/loom.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/tasks.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"
#include "../third_party/miniz/miniz.h"

using namespace loom;
using namespace loom::catalog;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

const std::array<std::string, 4> payload_markers{
    "qqtransienttitle7391", "qqtransientbody7391", "qqtransientattachment7391", "qqtransientfuture7391"};

Json conversations() {
  auto document = Json::parse(R"JSON([
    {"id":"safe-openai","title":"qqtransienttitle7391","current_node":"b",
     "future":{"unknown":"qqtransientfuture7391"},
     "mapping":{
       "root":{"parent":null,"children":["u"],"message":null},
       "u":{"parent":"root","children":["a","b"],"message":{"id":"u","author":{"role":"user"},"content":{"content_type":"text","parts":["qqtransientbody7391"]},"metadata":{"attachments":[{"id":"file-unresolved","name":"qqtransientattachment7391.bin"}]}}},
       "a":{"parent":"u","children":[],"message":{"id":"a","author":{"role":"assistant"},"content":{"content_type":"text","parts":["Safe alternative"]}}},
       "b":{"parent":"u","children":[],"message":{"id":"b","author":{"role":"assistant"},"content":{"content_type":"text","parts":["Safe current answer"]}}}
     }},
    {"uuid":"safe-anthropic","name":"Safe second conversation","current_leaf_message_uuid":"a",
     "chat_messages":[{"uuid":"u","sender":"human","text":"Safe question"},
                      {"uuid":"a","parent_message_uuid":"u","sender":"assistant","text":"Safe answer"}]}
  ])JSON");
  return document;
}

void write_zip(const fs::path& path, const Json& document) {
  const auto bytes = document.dump();
  mz_zip_archive zip{};
  REQUIRE(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
  // Stored bytes make an accidental retained ZIP detectable by marker search.
  REQUIRE(mz_zip_writer_add_mem(&zip, "conversations.json", bytes.data(), bytes.size(), 0));
  REQUIRE(mz_zip_writer_finalize_archive(&zip));
  REQUIRE(mz_zip_writer_end(&zip));
}

void set_policy(const fs::path& data, std::string_view storage, std::string_view index) {
  fs::create_directories(data / "profiles");
  const Json overlay{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "resource_read"},
                     {"overrides", {{"projection_storage", storage}, {"content_index", index}}}};
  LOOM_REQUIRE_OK(fsutil::write_file(data / "profiles/resource_read.pack", overlay.dump()));
}

std::unique_ptr<Runtime> open_runtime(const fs::path& data,
                                    const std::shared_ptr<net::ScriptedTransport>& transport) {
  RuntimeOptions options;
  options.data_dir = data.string();
  options.start_workers = false;
  options.http = transport;
  return unwrap(Runtime::open(options));
}

std::vector<CatalogUnit> scan_units(Catalog& catalog, const fs::path& source) {
  ScanConfig scan;
  scan.sources = {source.string()};
  scan.retain_raw = "none";
  unwrap(catalog.scan(scan));
  UnitQuery query;
  query.sort = "id";
  return unwrap(catalog.query(query));
}

void check_no_payload(const fs::path& directory) {
  REQUIRE(fs::exists(directory));
  for (const auto& entry : fs::recursive_directory_iterator(directory)) {
    if (!entry.is_regular_file()) continue;
    INFO(entry.path().string());
    const auto bytes = unwrap(fsutil::read_file(entry.path()));
    for (const auto& marker : payload_markers) CHECK(bytes.find(marker) == std::string::npos);
  }
}

struct CapturedLogs {
  log::Level previous = log::level();
  std::vector<std::string> messages;
  int token;
  CapturedLogs() : token(log::add_sink([this](const log::Record& record) { messages.push_back(record.message); })) {
    log::set_level(log::Level::Debug);
  }
  ~CapturedLogs() { log::remove_sink(token); log::set_level(previous); }
  void check() const {
    for (const auto& line : messages)
      for (const auto& marker : payload_markers) CHECK(line.find(marker) == std::string::npos);
    for (const auto& line : log::recent())
      for (const auto& marker : payload_markers) CHECK(line.find(marker) == std::string::npos);
  }
};

struct OperationTemp {
  std::optional<std::string> previous;
#if defined(__linux__)
  int watch_fd = -1;
#endif
  explicit OperationTemp(const fs::path& directory) {
    if (const auto* old = std::getenv("TMPDIR")) previous = old;
#if defined(_WIN32)
    REQUIRE(_putenv_s("TMPDIR", directory.string().c_str()) == 0);
#else
    REQUIRE(setenv("TMPDIR", directory.string().c_str(), 1) == 0);
#endif
#if defined(__linux__)
    watch_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    REQUIRE(watch_fd >= 0);
    REQUIRE(inotify_add_watch(watch_fd, directory.string().c_str(), IN_CREATE | IN_MOVED_TO) >= 0);
#endif
  }
  ~OperationTemp() {
#if defined(__linux__)
    if (watch_fd >= 0) close(watch_fd);
#endif
#if defined(_WIN32)
    _putenv_s("TMPDIR", previous ? previous->c_str() : "");
#else
    if (previous) setenv("TMPDIR", previous->c_str(), 1);
    else unsetenv("TMPDIR");
#endif
  }
  void check_no_staging() const {
#if defined(__linux__)
    // The watcher also catches files/directories created and then removed.
    alignas(inotify_event) std::array<char, 4096> events{};
    const auto count = read(watch_fd, events.data(), events.size());
    CHECK(count == -1);
    CHECK(errno == EAGAIN);
#endif
  }
};

Json view_resource(const fs::path& data, const std::string& unit_id) {
  const auto options = Json{{"data_dir", data.string()}, {"start_workers", false}}.dump();
  const char* error = nullptr;
  auto* context = loom_init_ex(options.c_str(), &error);
  REQUIRE(context != nullptr);
  auto* raw = loom_catalog_preview(context, unit_id.c_str());
  REQUIRE(raw != nullptr);
  const auto parsed = json::parse(raw);
  loom_free_string(raw);
  loom_shutdown(context);
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->contains("resource"));
  return parsed->at("resource");
}

void check_same_graph(const Json& left, const Json& right) {
  for (const auto* field : {"source", "selector", "content_hash", "projection_profile", "mapping_version",
                            "mapping_status", "coverage", "raw", "nodes", "edges"})
    CHECK(left.at(field) == right.at(field));
}

}  // namespace

TEST_SUITE("catalog_resource_transient") {
  TEST_CASE("ZIP reference view and headless reads survive reopen without payload persistence or staging") {
    fsutil::TempDir workspace;
    const auto data = workspace.path() / "data";
    const auto temporary = workspace.path() / "temporary";
    fs::create_directories(temporary);
    const auto source = workspace.path() / "safe.zip";
    const auto original = conversations();
    write_zip(source, original);
    set_policy(data, "transient", "none");
    auto transport = std::make_shared<net::ScriptedTransport>();
    CapturedLogs logs;
    auto runtime = open_runtime(data, transport);
    OperationTemp operation_temp(temporary);
    Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
    const auto units = scan_units(catalog, source);
    REQUIRE(units.size() == 2);
    std::string openai_id;
    for (const auto& unit : units) {
      CHECK(unit.unit.title.empty());
      CHECK(unit.head.empty());
      CHECK(unit.attachments.empty());
      CHECK(unit.unit.attrs.at("content_index") == "none");
      CHECK(unit.n_msgs > 0);
      CHECK(unit.n_chars > 0);
      CHECK(unit.unit.bytes > 0);
      const auto headless = unwrap(catalog.read_resource(unit.unit.id));
      CHECK(headless.at("status") == "available");
      CHECK(headless.at("current") == true);
      const auto& graph = headless.at("last_successful");
      CHECK(graph.at("coverage") == "provider_conversation");
      CHECK(graph.at("mapping_status") == "recognized");
      CHECK(graph.at("nodes").size() == static_cast<std::size_t>(unit.n_msgs + 1));
      CHECK(view_resource(data, unit.unit.id) == headless);
      CHECK_FALSE(unwrap(runtime->db().get_node("resource:" + unit.unit.id)).has_value());
      if (unit.ext_id == "safe-openai") {
        openai_id = unit.unit.id;
        CHECK(graph.at("raw") == original[0]);
        CHECK(graph.at("edges").size() == 5);
      } else {
        CHECK(graph.at("raw") == original[1]);
      }
    }
    REQUIRE_FALSE(openai_id.empty());
    CHECK(unwrap(runtime->db().list_nodes()).empty());
    CHECK(unwrap(runtime->db().list_convs()).empty());
    check_no_payload(data); // includes currently open SQLite WAL and any logs/blobs
    runtime.reset();
    check_no_payload(data);
    runtime = open_runtime(data, transport);
    Catalog reopened(*runtime, unwrap(runtime->knowledge().pack()));
    const auto restored = unwrap(reopened.read_resource(openai_id));
    CHECK(restored.at("last_successful").at("raw") == original[0]);
    CHECK_FALSE(unwrap(runtime->db().get_node("resource:" + openai_id)).has_value());

    const auto offline = workspace.path() / "source-offline.zip";
    fs::rename(source, offline);
    const auto unavailable = unwrap(reopened.read_resource(openai_id));
    CHECK(unavailable.at("status") == "unavailable");
    CHECK(unavailable.at("current") == false);
    CHECK(unavailable.at("last_successful").is_null());
    CHECK(unavailable.contains("error"));
    CHECK(unwrap(reopened.query(UnitQuery{})).size() == units.size());
    fs::rename(offline, source);
    auto changed = original;
    changed[0]["mapping"]["u"]["message"]["content"]["parts"][0] = "qqtransientbody7392";
    write_zip(source, changed);
    const auto mismatch = unwrap(reopened.read_resource(openai_id));
    CHECK(mismatch.at("status") == "source_changed");
    CHECK(mismatch.at("current") == false);
    CHECK(mismatch.at("last_successful").is_null());
    write_zip(source, original);
    CHECK(unwrap(reopened.read_resource(openai_id)).at("last_successful").at("raw") == original[0]);
    runtime.reset();
    check_no_payload(data);
    CHECK(fs::is_empty(temporary));
    operation_temp.check_no_staging();
    logs.check();
    CHECK(transport->requests().empty());
  }

  TEST_CASE("snapshot defaults preserve graph identity and history when transient reads are selected later") {
    fsutil::TempDir workspace;
    const auto source = workspace.path() / "safe.zip";
    const auto data = workspace.path() / "snapshot-data";
    write_zip(source, conversations());
    auto transport = std::make_shared<net::ScriptedTransport>();
    auto runtime = open_runtime(data, transport);
    Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
    const auto units = scan_units(catalog, source);
    REQUIRE(units.size() == 2);
    const auto id = units.front().unit.id;
    const auto snapshot = unwrap(catalog.read_resource(id)).at("last_successful");
    const auto retained = unwrap(runtime->db().get_node("resource:" + id));
    REQUIRE(retained.has_value());
    const auto transient = unwrap(catalog.read_resource(id, Json{{"projection_storage", "transient"}}));
    check_same_graph(transient.at("last_successful"), snapshot);
    CHECK(unwrap(runtime->db().get_node("resource:" + id))->metadata == retained->metadata);
    CHECK_FALSE(catalog.read_resource(id, Json{{"projection_storage", "unknown"}}).has_value());
    set_policy(data, "transient", "sketch");
    runtime.reset();
    runtime = open_runtime(data, transport);
    Catalog reopened(*runtime, unwrap(runtime->knowledge().pack()));
    check_same_graph(unwrap(reopened.read_resource(id)).at("last_successful"), snapshot);
    fs::rename(source, workspace.path() / "offline.zip");
    const auto stale = unwrap(reopened.read_resource(id));
    CHECK(stale.at("current") == false);
    CHECK(stale.at("status") == "unavailable");
    CHECK(stale.at("last_successful") == snapshot);
    CHECK(unwrap(runtime->db().get_node("resource:" + id))->metadata == retained->metadata);
    CHECK(transport->requests().empty());
  }

  TEST_CASE("index retention and projection retention are independently selected through the same profile") {
    fsutil::TempDir workspace;
    const auto source = workspace.path() / "safe.zip";
    write_zip(source, conversations());
    auto transport = std::make_shared<net::ScriptedTransport>();
    auto indexed = open_runtime(workspace.path() / "indexed", transport);
    Catalog indexed_catalog(*indexed, unwrap(indexed->knowledge().pack()));
    const auto indexed_units = scan_units(indexed_catalog, source);
    const auto unindexed_data = workspace.path() / "unindexed";
    set_policy(unindexed_data, "snapshot", "none");
    auto unindexed = open_runtime(unindexed_data, transport);
    Catalog unindexed_catalog(*unindexed, unwrap(unindexed->knowledge().pack()));
    const auto unindexed_units = scan_units(unindexed_catalog, source);
    REQUIRE(indexed_units.size() == unindexed_units.size());
    for (std::size_t i = 0; i < indexed_units.size(); ++i) {
      const auto& old = indexed_units[i];
      const auto& none = unindexed_units[i];
      CHECK(old.unit.id == none.unit.id);
      CHECK(old.n_chars == none.n_chars);
      CHECK(old.n_code_chars == none.n_code_chars);
      CHECK(old.n_msgs == none.n_msgs);
      CHECK(none.unit.title.empty());
      CHECK_FALSE(old.unit.title.empty());
      const auto transient = unwrap(indexed_catalog.read_resource(old.unit.id, Json{{"projection_storage", "transient"}}));
      CHECK_FALSE(unwrap(indexed->db().get_node("resource:" + old.unit.id)).has_value());
      const auto persisted = unwrap(unindexed_catalog.read_resource(none.unit.id));
      REQUIRE(unwrap(unindexed->db().get_node("resource:" + none.unit.id)).has_value());
      check_same_graph(transient.at("last_successful"), persisted.at("last_successful"));
    }
    CHECK(transport->requests().empty());
  }

  TEST_CASE("index policy transitions preserve truthful metadata and reject removal of retained history") {
    fsutil::TempDir workspace;
    const auto source = workspace.path() / "safe.zip";
    const auto data = workspace.path() / "data";
    write_zip(source, conversations());
    auto transport = std::make_shared<net::ScriptedTransport>();
    auto reference = open_runtime(workspace.path() / "reference", transport);
    Catalog reference_catalog(*reference, unwrap(reference->knowledge().pack()));
    const auto expected = scan_units(reference_catalog, source);
    set_policy(data, "transient", "none");
    auto runtime = open_runtime(data, transport);
    ScanConfig scan;
    scan.sources = {source.string()};
    scan.retain_raw = "none";
    UnitQuery query;
    query.sort = "id";
    std::vector<std::string> retained_bodies, retained_sketches;
    {
      Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
      const auto original = scan_units(catalog, source);
      REQUIRE(original.size() == expected.size());
      check_no_payload(data);
      set_policy(data, "transient", "sketch");
      const auto refreshed = unwrap(catalog.scan(scan));
      CHECK(refreshed.at("warnings").empty());
      CHECK(refreshed.at("refreshed") == original.size());
      const auto indexed = unwrap(catalog.query(query));
      REQUIRE(indexed.size() == original.size());
      for (std::size_t i = 0; i < indexed.size(); ++i) {
        CHECK(indexed[i].unit.id == original[i].unit.id);
        CHECK(indexed[i].unit.source == original[i].unit.source);
        CHECK(indexed[i].unit.locator.to_json() == original[i].unit.locator.to_json());
        CHECK(indexed[i].content_hash == original[i].content_hash);
        CHECK(indexed[i].prev_version == original[i].prev_version);
        CHECK(indexed[i].unit.attrs.value("content_index", "") == "sketch");
        CHECK(indexed[i].unit.title == expected[i].unit.title);
        CHECK(indexed[i].head == expected[i].head);
        CHECK(indexed[i].attachments == expected[i].attachments);
        CHECK(indexed[i].n_msgs == expected[i].n_msgs);
        CHECK(indexed[i].n_chars == expected[i].n_chars);
        CHECK(indexed[i].mentions == expected[i].mentions);
        auto& db = runtime->db().conn();
        CHECK(unwrap(db.query_text("SELECT title FROM loom_cat_units WHERE id = ?", indexed[i].unit.id)) ==
              std::optional<std::string>(expected[i].unit.title));
        retained_bodies.push_back(*unwrap(db.query_text("SELECT body FROM loom_cat_units WHERE id = ?", indexed[i].unit.id)));
        retained_sketches.push_back(*unwrap(db.query_text("SELECT sketch FROM loom_cat_units WHERE id = ?", indexed[i].unit.id)));
        CHECK(retained_sketches.back() == *unwrap(reference->db().conn().query_text(
            "SELECT sketch FROM loom_cat_units WHERE id = ?", indexed[i].unit.id)));
      }
    }
    // Turning indexing off cannot erase the previous DB/WAL snapshot. Reject
    // this transition explicitly, including after a retry and store reopen.
    set_policy(data, "transient", "none");
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (attempt == 2) {
        runtime.reset();
        runtime = open_runtime(data, transport);
      }
      Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
      const auto rejected = unwrap(catalog.scan(scan));
      INFO("transition attempt ", attempt);
      CHECK_FALSE(rejected.at("warnings").empty());
      CHECK(rejected.at("warnings").dump().find("transient policy does not purge history") != std::string::npos);
      CHECK(rejected.at("refreshed") == 0);
      const auto retained = unwrap(catalog.query(query));
      REQUIRE(retained.size() == expected.size());
      for (std::size_t i = 0; i < retained.size(); ++i) {
        CHECK(retained[i].unit.attrs.value("content_index", "") == "sketch");
        CHECK(unwrap(runtime->db().conn().query_text("SELECT body FROM loom_cat_units WHERE id = ?", retained[i].unit.id)) ==
              std::optional<std::string>(retained_bodies[i]));
        CHECK(unwrap(runtime->db().conn().query_text("SELECT sketch FROM loom_cat_units WHERE id = ?", retained[i].unit.id)) ==
              std::optional<std::string>(retained_sketches[i]));
      }
    }
    CHECK(transport->requests().empty());
  }

  TEST_CASE("unindexed malformed JSON diagnostics do not retain source payload in results or task storage") {
    fsutil::TempDir workspace;
    const auto source = workspace.path() / "malformed.json";
    const auto data = workspace.path() / "data";
    const std::string malformed = R"({"text":"qqtransientbody7391" : 0})";
    const auto raw_error = json::parse(malformed);
    REQUIRE_FALSE(raw_error);
    // The shared parser already suppresses third-party source excerpts. Keep
    // that contract through catalog warnings and durable task results.
    CHECK(raw_error.error().code == Errc::Parse);
    CHECK(raw_error.error().message == "invalid JSON");
    CHECK(raw_error.error().message.find(payload_markers[1]) == std::string::npos);
    LOOM_REQUIRE_OK(fsutil::write_file(source, "[" + malformed + "]"));
    set_policy(data, "transient", "none");
    auto transport = std::make_shared<net::ScriptedTransport>();
    CapturedLogs logs;
    auto runtime = open_runtime(data, transport);
    ScanConfig scan;
    scan.sources = {source.string()};
    scan.retain_raw = "none";
    scan.force = true;
    {
      Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
      const auto result = unwrap(catalog.scan(scan));
      CHECK(result.at("units") == 0);
      CHECK_FALSE(result.at("warnings").empty());
      CHECK(result.at("warnings").dump().find("malformed JSON element skipped") != std::string::npos);
      CHECK(result.at("warnings").dump().find("invalid JSON") != std::string::npos);
      CHECK(result.dump().find(payload_markers[1]) == std::string::npos);
      CHECK(unwrap(catalog.query(UnitQuery{})).empty());
    }
    // Exercise the real queue/result persistence boundary with the actual
    // Catalog scanner as a test-owned handler, without a model transport.
    runtime->tasks().register_handler("fixture.catalog.scan", [&](TaskContext& task) -> Status {
      Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
      LOOM_TRY_ASSIGN(auto result, catalog.scan(scan));
      task.set_result(result);
      return {};
    });
    const auto task_id = unwrap(runtime->tasks().submit("fixture.catalog.scan", Json::object()));
    const auto completed = unwrap(runtime->tasks().run_sync(task_id));
    REQUIRE(completed.status == "done");
    REQUIRE(completed.result.has_value());
    CHECK_FALSE(completed.result->at("warnings").empty());
    CHECK(completed.to_json().dump().find(payload_markers[1]) == std::string::npos);
    runtime.reset();
    check_no_payload(data);
    runtime = open_runtime(data, transport);
    const auto restored = unwrap(runtime->tasks().get(task_id));
    REQUIRE(restored.has_value());
    CHECK(restored->to_json() == completed.to_json());
    CHECK(restored->to_json().dump().find(payload_markers[1]) == std::string::npos);
    check_no_payload(data);
    logs.check();
    CHECK(transport->requests().empty());
  }

  TEST_CASE("legacy no-index labels cannot certify retention history or bypass checks on resume") {
    fsutil::TempDir workspace;
    const auto source = workspace.path() / "safe.zip";
    const auto data = workspace.path() / "data";
    write_zip(source, conversations());
    set_policy(data, "transient", "none");
    auto transport = std::make_shared<net::ScriptedTransport>();
    auto runtime = open_runtime(data, transport);
    const auto pack = unwrap(runtime->knowledge().pack());
    ScanConfig scan;
    scan.sources = {source.string()};
    scan.retain_raw = "none";
    std::vector<CatalogUnit> original;
    {
      Catalog catalog(*runtime, pack);
      original = scan_units(catalog, source);
    }
    REQUIRE(original.size() == 2);
    std::string fingerprint;
    SUBCASE("legacy completed checkpoint is invalidated by the retention contract") {
      const auto base = Sha256::hex(json::canonical(Json{{"scanner_version", std::string(kScannerVersion)},
          {"pack_hash", pack->hash()}, {"sketch", scan.sketch.to_json()},
          {"source_index_contract", "catalog.source_index/1"}}));
      fingerprint = Sha256::hex(json::canonical(Json{{"base", base}, {"content_index", "none"}}));
    }
    SUBCASE("forced identical-input scan validates provenance before its unchanged shortcut") {
      fingerprint = original[0].unit.attrs.at("catalog_scan_fingerprint").get<std::string>();
      scan.force = true;
    }
    std::vector<std::string> legacy_bodies, legacy_sketches;
    for (auto unit : original) {
      CHECK(unit.unit.attrs.at("index_retention_contract") == "catalog.index_retention/1");
      // A legacy 'none' label may follow a formerly indexed snapshot. Even
      // empty current fields cannot prove what old WAL pages retained.
      unit.unit.attrs.erase("index_retention_contract");
      unit.unit.attrs["catalog_scan_fingerprint"] = fingerprint;
      legacy_bodies.push_back(json::dump(unit.to_json()));
      auto& db = runtime->db().conn();
      legacy_sketches.push_back(*unwrap(db.query_text("SELECT sketch FROM loom_cat_units WHERE id = ?", unit.unit.id)));
      LOOM_REQUIRE_OK(db.run("UPDATE loom_cat_units SET body = ? WHERE id = ?", legacy_bodies.back(), unit.unit.id));
    }
    LOOM_REQUIRE_OK(runtime->db().conn().run("UPDATE loom_cat_checkpoint SET input_hash = ?, done = 1", fingerprint));
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (attempt == 2) {
        runtime.reset();
        runtime = open_runtime(data, transport);
      }
      Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
      const auto result = unwrap(catalog.scan(scan));
      INFO("legacy retention attempt ", attempt);
      CHECK(result.at("warnings").dump().find("retention history is unknown") != std::string::npos);
      CHECK(result.at("warnings").dump().find("fresh catalog store") != std::string::npos);
      CHECK(result.at("refreshed") == 0);
      CHECK(result.at("unchanged") == 0);
      CHECK(unwrap(runtime->db().conn().query_int("SELECT COUNT(*) FROM loom_cat_checkpoint WHERE done != 0")) ==
            std::optional<std::int64_t>(0));
      for (std::size_t i = 0; i < original.size(); ++i) {
        CHECK(unwrap(runtime->db().conn().query_text("SELECT body FROM loom_cat_units WHERE id = ?", original[i].unit.id)) ==
              std::optional<std::string>(legacy_bodies[i]));
        CHECK(unwrap(runtime->db().conn().query_text("SELECT sketch FROM loom_cat_units WHERE id = ?", original[i].unit.id)) ==
              std::optional<std::string>(legacy_sketches[i]));
      }
      scan.force = false;
    }
    // Explicitly enabling retained indexing is still available for legacy
    // rows; the system does not silently adopt it on the owner's behalf.
    set_policy(data, "transient", "sketch");
    Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
    const auto enabled = unwrap(catalog.scan(scan));
    CHECK(enabled.at("warnings").empty());
    CHECK(enabled.at("refreshed") == original.size());
    const auto indexed = unwrap(catalog.query(UnitQuery{}));
    REQUIRE(indexed.size() == original.size());
    for (const auto& unit : indexed) {
      CHECK(unit.unit.attrs.at("content_index") == "sketch");
      CHECK(unit.unit.attrs.at("index_retention_contract") == "catalog.index_retention/1");
      CHECK_FALSE(unit.unit.title.empty());
    }
    CHECK(transport->requests().empty());
  }

  TEST_CASE("whole-source retention refusal invalidates completed checkpoints for plain and ZIP sources") {
    bool archived = false;
    SUBCASE("plain text source") {}
    SUBCASE("text member in ZIP source") { archived = true; }
    fsutil::TempDir workspace;
    const auto source = workspace.path() / (archived ? "safe.zip" : "notes.txt");
    const auto data = workspace.path() / "data";
    const std::string contents = "Safe synthetic text qqtransientbody7391";
    if (archived) {
      mz_zip_archive zip{};
      REQUIRE(mz_zip_writer_init_file(&zip, source.string().c_str(), 0));
      REQUIRE(mz_zip_writer_add_mem(&zip, "notes.txt", contents.data(), contents.size(), 0));
      REQUIRE(mz_zip_writer_finalize_archive(&zip));
      REQUIRE(mz_zip_writer_end(&zip));
    } else {
      LOOM_REQUIRE_OK(fsutil::write_file(source, contents));
    }
    set_policy(data, "transient", "none");
    auto transport = std::make_shared<net::ScriptedTransport>();
    auto runtime = open_runtime(data, transport);
    CatalogUnit legacy;
    {
      Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
      const auto units = scan_units(catalog, source);
      REQUIRE(units.size() == 1);
      legacy = units[0];
    }
    // Same current input fingerprint and completed checkpoint, but absent
    // provenance: a forced scan must invalidate the old completion on refusal.
    legacy.unit.attrs.erase("index_retention_contract");
    const auto retained_body = json::dump(legacy.to_json());
    const auto retained_sketch = unwrap(runtime->db().conn().query_text(
        "SELECT sketch FROM loom_cat_units WHERE id = ?", legacy.unit.id));
    LOOM_REQUIRE_OK(runtime->db().conn().run("UPDATE loom_cat_units SET body = ? WHERE id = ?", retained_body, legacy.unit.id));
    CHECK(unwrap(runtime->db().conn().query_int("SELECT done FROM loom_cat_checkpoint")) == std::optional<std::int64_t>(1));
    ScanConfig scan;
    scan.sources = {source.string()};
    scan.retain_raw = "none";
    scan.force = true;
    for (int attempt = 0; attempt < 3; ++attempt) {
      if (attempt == 2) {
        runtime.reset();
        runtime = open_runtime(data, transport);
      }
      Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
      const auto result = unwrap(catalog.scan(scan));
      INFO("whole-source retention attempt ", attempt);
      CHECK(result.at("warnings").dump().find("retention history is unknown") != std::string::npos);
      CHECK(result.at("refreshed") == 0);
      CHECK(unwrap(runtime->db().conn().query_int("SELECT done FROM loom_cat_checkpoint")) == std::optional<std::int64_t>(0));
      CHECK(unwrap(runtime->db().conn().query_text("SELECT body FROM loom_cat_units WHERE id = ?", legacy.unit.id)) ==
            std::optional<std::string>(retained_body));
      CHECK(unwrap(runtime->db().conn().query_text("SELECT sketch FROM loom_cat_units WHERE id = ?", legacy.unit.id)) == retained_sketch);
      scan.force = false;
    }
    check_no_payload(data);
    CHECK(transport->requests().empty());
  }
}
