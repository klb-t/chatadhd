#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>

#if defined(__linux__)
#include <sys/inotify.h>
#include <unistd.h>
#endif
#include <filesystem>
#include <map>
#include <set>

#include "loom/catalog.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/loom.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "util/json_value.h"
#include "loom/tasks.h"
#include "loom/util/fs.h"
#include "test_helpers.h"
#include "../third_party/miniz/miniz.h"

using namespace loom;
using test::unwrap;
namespace fs = std::filesystem;
namespace {
const std::string canary = "SYNTHETIC_P4_VIEW_PAYLOAD_80497";
Json fixture_source() {
  return Json::parse(R"([
    {"id":"safe-openai","title":"Public view fixture","current_node":"b","future":{"preserve":"unknown source field"},
     "mapping":{
       "root":{"parent":null,"children":["u"],"message":null},
       "u":{"parent":"root","children":["a","b"],"message":{"id":"u","author":{"role":"user"},"weight":0.75,"content":{"content_type":"text","parts":["SYNTHETIC_P4_VIEW_PAYLOAD_80497"]},"metadata":{"attachments":[{"id":"safe-unresolved","name":"synthetic.txt","future":17}]},"future":{"unknown":true}}},
       "a":{"parent":"u","children":[],"message":{"id":"a","author":{"role":"assistant"},"content":{"content_type":"text","parts":["Public alternative"]},"metadata":{"model_slug":"synthetic/alternative"}}},
       "b":{"parent":"u","children":[],"message":{"id":"b","author":{"role":"assistant"},"content":{"content_type":"text","parts":["Public active response"]},"metadata":{"model_slug":"synthetic/current"}}}
     }},
    {"uuid":"safe-anthropic","name":"Public reversed parent fixture","current_leaf_message_uuid":"a",
     "chat_messages":[{"uuid":"a","parent_message_uuid":"u","sender":"assistant","text":"Public child appears first","future":{"preserve":27}},
                      {"uuid":"u","sender":"human","text":"Public parent appears second"}]}
  ])");
}
void write_zip(const fs::path& path, const Json& document) {
  const auto bytes = document.dump();
  mz_zip_archive zip{};
  REQUIRE(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
  REQUIRE(mz_zip_writer_add_mem(&zip, "conversations.json", bytes.data(), bytes.size(), 0));
  REQUIRE(mz_zip_writer_finalize_archive(&zip));
  REQUIRE(mz_zip_writer_end(&zip));
}
struct Fixture {
  fsutil::TempDir workspace;
  fs::path source = workspace.path() / "public.zip";
  fs::path data = workspace.path() / "link-store";
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> runtime;
  std::map<std::string, std::string> conversations, units;
  explicit Fixture(const Json& document = fixture_source()) {
    write_zip(source, document);
    fs::create_directories(data / "profiles");
    LOOM_REQUIRE_OK(fsutil::write_file(data / "profiles/resource_read.pack",
      Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "resource_read"},
           {"overrides", {{"projection_storage", "transient"}, {"content_index", "none"}}}}.dump()));
    reopen();
    catalog::Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
    catalog::ScanConfig scan; scan.sources = {source.string()}; scan.retain_raw = "none";
    unwrap(catalog.scan(scan));
    catalog::ImportOptions import; import.mode = "full"; import.store_mode = "link";
    unwrap(catalog.import_selected(import));
    for (const auto& unit : unwrap(catalog.query(catalog::UnitQuery{}))) {
      units[unit.ext_id] = unit.unit.id;
      auto lock = runtime->db().lock();
      conversations[unit.ext_id] = unwrap(runtime->db().conn().query_text(
        "SELECT conv_id FROM loom_cat_imports WHERE unit_id=?", unit.unit.id)).value();
    }
    REQUIRE(conversations.size() == 2);
  }
  void reopen() {
    runtime.reset();
    RuntimeOptions options; options.data_dir = data.string(); options.start_workers = false; options.http = transport;
    runtime = unwrap(Runtime::open(options));
  }
  Json view(std::string_view name = "safe-openai", bool grant = true, const Json& options = Json::object()) {
    return unwrap(runtime->read_conversation_view(conversations.at(std::string(name)), options, grant));
  }
  Message placeholder(std::string_view name = "safe-openai") {
    const auto rows = unwrap(runtime->db().get_msgs(conversations.at(std::string(name)), true));
    REQUIRE(rows.size() == 1);
    return rows[0];
  }
};
Json normalized(const Json& messages) {
  std::map<std::string, int> indices;
  for (std::size_t index = 0; index < messages.size(); ++index) indices[messages[index]["id"].get<std::string>()] = static_cast<int>(index);
  Json result = Json::array();
  std::map<std::string, std::size_t> groups;
  for (const auto& row : messages) {
    const auto parent = row["parent_id"].is_null() ? -1 : indices.at(row["parent_id"].get<std::string>());
    // A source row without a version group is a singleton, matching the
    // native writer's generated independent group rather than all nulls
    // falsely forming one common version family.
    const auto group = row["version_group_id"].is_null()
      ? "singleton:" + row["id"].get<std::string>() : row["version_group_id"].get<std::string>();
    const auto first = groups.emplace(group, result.size()).first->second;
    Json normalized{{"parent", parent}, {"version_group_first_index", first}};
    for (const char* key : {"role", "text", "model", "status", "weight", "attachments", "version_num"}) normalized[key] = row[key];
    normalized["export"] = row["metadata"]["export"];
    result.push_back(normalized);
  }
  return result;
}
void check_no_payload(const fs::path& directory) {
  for (const auto& entry : fs::recursive_directory_iterator(directory)) {
    if (!entry.is_regular_file()) continue;
    INFO(entry.path().string());
    CHECK(unwrap(fsutil::read_file(entry.path())).find(canary) == std::string::npos);
  }
}
struct ReadEffects {
  std::optional<std::string> previous;
  std::vector<std::string> logs;
  int token;
#if defined(__linux__)
  int watch = -1;
#endif
  explicit ReadEffects(const fs::path& directory) : token(log::add_sink([this](const log::Record& row) { logs.push_back(row.message); })) {
    fs::create_directories(directory);
    if (const auto* value = std::getenv("TMPDIR")) previous = value;
#if defined(_WIN32)
    REQUIRE(_putenv_s("TMPDIR", directory.string().c_str()) == 0);
#else
    REQUIRE(setenv("TMPDIR", directory.string().c_str(), 1) == 0);
#endif
#if defined(__linux__)
    watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    REQUIRE(watch >= 0);
    REQUIRE(inotify_add_watch(watch, directory.string().c_str(), IN_CREATE | IN_MOVED_TO) >= 0);
#endif
  }
  ~ReadEffects() {
    log::remove_sink(token);
#if defined(__linux__)
    if (watch >= 0) close(watch);
#endif
#if defined(_WIN32)
    _putenv_s("TMPDIR", previous ? previous->c_str() : "");
#else
    if (previous) setenv("TMPDIR", previous->c_str(), 1); else unsetenv("TMPDIR");
#endif
  }
  void check() const {
    for (const auto& row : logs) CHECK(row.find(canary) == std::string::npos);
#if defined(__linux__)
    std::array<char, 8192> buffer{};
    const auto bytes = read(watch, buffer.data(), buffer.size());
    CHECK(bytes == -1);
    CHECK(errno == EAGAIN); // Includes files created then removed during the read.
#endif
  }
};
Json take(const char* raw) {
  REQUIRE(raw != nullptr);
  const auto parsed = json::parse(raw);
  loom_free_string(raw);
  REQUIRE(parsed);
  return *parsed;
}
}

TEST_CASE("conversation view shares lossless catalog mapping with full copy without changing storage getters") {
  Fixture fixture;
  const auto first = fixture.view();
  CHECK(first["schema"] == "loom.conversation_view/1"); CHECK(first["status"] == "complete");
  REQUIRE(first["messages"].size() == 3);
  CHECK(first["resources"][0]["current"] == true);
  CHECK(first["resources"][0]["produced_by"].is_object());
  CHECK(first["resources"][0]["method_manifest"]["trace"]["schema"] == "loom.method_run_trace/1");
  CHECK(first["resources"][0]["read_scope"]["container_io"] == "not_instrumented");
  CHECK(first["capabilities"]["source_history_send"]["available"] == false);
  for (const auto& row : first["messages"]) {
    CHECK(row["storage"] == "reference"); CHECK(row["created"].is_null());
    CHECK(row["capabilities"] == Json{{"edit", false}, {"set_status", false}, {"restore", false}, {"native_lookup", false}});
    CHECK_FALSE(unwrap(fixture.runtime->db().get_msg(row["id"].get<std::string>())).has_value());
  }
  CHECK(unwrap(fixture.runtime->db().get_msgs(fixture.conversations.at("safe-openai"), true)).size() == 1);
  CHECK(first.dump().find(canary) != std::string::npos);
  CHECK(first["resources"][0]["conversation"]["metadata"]["export"].dump().find("unknown source field") != std::string::npos);
  const auto repeated = fixture.view();
  CHECK(repeated["messages"] == first["messages"]); CHECK(repeated["view_id"] == first["view_id"]);
  CHECK(repeated["resources"][0]["method_receipt"] != first["resources"][0]["method_receipt"]);
  fixture.reopen();
  CHECK(fixture.view()["messages"] == first["messages"]);
  const auto reverse = fixture.view("safe-anthropic");
  REQUIRE(reverse["messages"].size() == 2);
  CHECK(reverse["messages"][0]["parent_id"] == reverse["messages"][1]["id"]);

  RuntimeOptions options; options.data_dir = (fixture.workspace.path() / "copy-store").string();
  options.start_workers = false; options.http = fixture.transport;
  auto copied = unwrap(Runtime::open(options));
  catalog::Catalog catalog(*copied, unwrap(copied->knowledge().pack()));
  catalog::ScanConfig scan; scan.sources = {fixture.source.string()}; scan.retain_raw = "none";
  unwrap(catalog.scan(scan));
  catalog::ImportOptions import; import.mode = "full"; import.store_mode = "copy";
  unwrap(catalog.import_selected(import));
  for (const auto& unit : unwrap(catalog.query(catalog::UnitQuery{}))) {
    auto lock = copied->db().lock();
    const auto conversation = unwrap(copied->db().conn().query_text("SELECT conv_id FROM loom_cat_imports WHERE unit_id=?", unit.unit.id)).value();
    const auto native = unwrap(copied->read_conversation_view(conversation, Json::object(), false));
    CHECK(native["status"] == "complete"); CHECK(native["resources"].empty());
    CHECK(json::equivalent(normalized(native["messages"]), normalized(fixture.view(unit.ext_id)["messages"])));
  }
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("conversation metadata view denies source dispatch and preserves stored ordinary and spoofed rows") {
  Fixture fixture;
  auto& db = fixture.runtime->db();
  std::int64_t before;
  { auto lock = db.lock(); before = unwrap(db.conn().query_int("SELECT total_changes()")).value(); }
  const auto denied = fixture.view("safe-openai", false);
  CHECK(denied["status"] == "unavailable"); CHECK(denied["resources"][0]["status"] == "read_denied");
  CHECK_FALSE(denied["resources"][0].contains("method_receipt"));
  CHECK(denied["read_configuration"]["values"]["projection_storage"] == "transient");
  CHECK(denied["read_configuration"]["value_schema"]["properties"]["projection_storage"]["enum"].size() == 2);
  CHECK(denied.dump().find(canary) == std::string::npos);
  { auto lock = db.lock(); CHECK(unwrap(db.conn().query_int("SELECT total_changes()")).value() == before); }
  const auto ordinary = unwrap(db.create_conv("ordinary"));
  NewMessage message; message.conv_id = ordinary.id; message.role = "user"; message.text = "Stored ordinary text";
  message.metadata = fixture.placeholder().metadata; // mutable metadata is not authority
  const auto id = unwrap(db.create_msg(message));
  const auto stored = unwrap(fixture.runtime->read_conversation_view(ordinary.id, Json::object(), false));
  CHECK(stored["status"] == "complete"); CHECK(stored["resources"].empty());
  CHECK(stored["messages"][0]["id"] == id); CHECK(stored["messages"][0]["capabilities"]["edit"] == true);
  CHECK_FALSE(fixture.runtime->read_conversation_view(ordinary.id, Json{{"read_authorized", true}}, true));
  CHECK_FALSE(fixture.runtime->read_conversation_view(ordinary.id, Json::array(), true));
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("conversation link authority rejects local edits exclusions missing proof and tampered unit source") {
  for (const std::string mutation : {"text", "excluded", "versions", "missing_conversation_proof", "unit_source"}) {
    CAPTURE(mutation);
    Fixture fixture;
    auto& db = fixture.runtime->db();
    const auto placeholder = fixture.placeholder();
    if (mutation == "versions") unwrap(db.edit_msg(placeholder.id, "Local user edit retained"));
    else if (mutation == "missing_conversation_proof") {
      auto lock = db.lock();
      LOOM_REQUIRE_OK(db.conn().run("DELETE FROM loom_provenance WHERE subject_id=? AND subject_kind='conversation'", placeholder.conv_id));
    } else if (mutation == "unit_source") {
      auto lock = db.lock();
      auto body = Json::parse(unwrap(db.conn().query_text("SELECT body FROM loom_cat_units WHERE id=?", fixture.units.at("safe-openai"))).value());
      body["unit"]["source"] = "sha256:forged-source";
      LOOM_REQUIRE_OK(db.conn().run("UPDATE loom_cat_units SET body=? WHERE id=?", body.dump(), fixture.units.at("safe-openai")));
    } else {
      MsgPatch patch;
      if (mutation == "text") patch.text = "Local direct edit retained";
      else patch.status = std::string(msg_status::kExcluded);
      LOOM_REQUIRE_OK(db.update_msg(placeholder.id, patch));
    }
    const auto before = unwrap(db.get_msgs(placeholder.conv_id, true));
    const auto view = fixture.view();
    CHECK(view["status"] != "complete"); CHECK(view["resources"][0]["status"] == "binding_unresolved");
    CHECK_FALSE(view["resources"][0].contains("method_receipt"));
    CHECK(view["messages"].size() == before.size());
    CHECK(view.dump().find(canary) == std::string::npos);
    for (const auto& row : view["messages"]) CHECK(row["storage"] == "native");
    CHECK(fixture.transport->requests().empty());
  }
}

TEST_CASE("conversation link migration honors legacy writer and new fingerprint across metadata and index changes") {
  Fixture fixture;
  auto& db = fixture.runtime->db();
  const auto original = fixture.placeholder();
  MsgPatch semantic; auto metadata = original.metadata;
  metadata["semantic_source"] = "regex"; metadata["local_note"] = "Keep this local metadata";
  semantic.metadata = metadata; LOOM_REQUIRE_OK(db.update_msg(original.id, semantic));
  CHECK(fixture.view()["resources"][0]["placeholder"]["metadata"]["local_note"] == "Keep this local metadata");
  // Simulate the actual old provenance shape, retaining both journal/proof rows.
  {
    auto lock = db.lock();
    auto rows = unwrap(db.conn().prepare("SELECT id, locator FROM loom_provenance WHERE source_id=?"));
    rows.bind(1, fixture.view()["resources"][0]["source_id"].get<std::string>());
    std::vector<std::pair<std::string, std::string>> updates;
    while (unwrap(rows.step())) {
      auto locator = Json::parse(rows.get_text(1)); locator.erase("catalog_link_binding");
      updates.emplace_back(rows.get_text(0), locator.dump());
    }
    for (const auto& [id, locator] : updates) LOOM_REQUIRE_OK(db.conn().run("UPDATE loom_provenance SET locator=? WHERE id=?", locator, id));
  }
  const auto legacy = fixture.view();
  CHECK(legacy["status"] == "complete");
  CHECK(legacy["resources"][0]["binding"]["evidence"] == "journal_provenance_legacy_writer");
  Fixture current;
  const auto before = current.placeholder();
  LOOM_REQUIRE_OK(fsutil::write_file(current.data / "profiles/resource_read.pack",
    Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "resource_read"},
         {"overrides", {{"projection_storage", "transient"}, {"content_index", "sketch"}}}}.dump()));
  catalog::Catalog catalog(*current.runtime, unwrap(current.runtime->knowledge().pack()));
  catalog::ScanConfig scan; scan.sources = {current.source.string()}; scan.retain_raw = "none";
  unwrap(catalog.scan(scan));
  CHECK(current.view()["status"] == "complete");
  CHECK(unwrap(current.runtime->db().get_msg(before.id))->text == before.text);
  // A copy-only upgrade adds raw retention but does not replace the link row.
  catalog::ImportOptions upgrade; upgrade.mode = "full"; upgrade.store_mode = "copy"; upgrade.import_messages = false;
  unwrap(catalog.import_selected(upgrade));
  CHECK(current.view()["status"] == "complete");
  CHECK(current.view()["resources"][0]["source_storage"] == "retained_copy");
}

TEST_CASE("conversation view retains a referenced native placeholder anchor without guessing source parent links") {
  Fixture fixture;
  const auto original = fixture.placeholder();
  NewMessage child; child.conv_id = original.conv_id; child.parent_id = original.id; child.role = "user"; child.text = "Stored continuation";
  const auto child_id = unwrap(fixture.runtime->db().create_msg(child));
  const auto view = fixture.view();
  CHECK(view["status"] == "complete"); REQUIRE(view["messages"].size() == 5);
  CHECK(view["resources"][0]["binding"]["placeholder_retained_as_anchor"] == true);
  std::set<std::string> ids;
  for (const auto& row : view["messages"]) ids.insert(row["id"].get<std::string>());
  for (const auto& row : view["messages"]) {
    if (!row["parent_id"].is_null()) CHECK(ids.contains(row["parent_id"].get<std::string>()));
    if (row["id"] == original.id) CHECK(row["reference_anchor"] == true);
    if (row["id"] == child_id) CHECK(row["parent_id"] == original.id);
  }
}

TEST_CASE("conversation view source failure is explicit and transient mapping does not persist through tasks or reopen") {
  Fixture fixture;
  ReadEffects effects(fixture.workspace.path() / "operation-temp");
  const auto first = fixture.view();
  const auto conv = fixture.conversations.at("safe-openai");
  catalog::Catalog catalog(*fixture.runtime, unwrap(fixture.runtime->knowledge().pack()));
  const auto read = unwrap(catalog.execute_resource(fixture.units.at("safe-openai"), Json::object(), true));
  CHECK(read["last_successful"]["mapping"]["schema"] == "loom.export_mapping/1");
  CHECK_FALSE(read.contains("mapping"));
  const auto task_id = unwrap(fixture.runtime->tasks().submit("catalog.read_resource", Json{{"unit_id", fixture.units.at("safe-openai")}}));
  const auto task = unwrap(fixture.runtime->tasks().run_sync(task_id));
  REQUIRE(task.status == "done"); REQUIRE(task.result.has_value());
  CHECK_FALSE(task.result->contains("last_successful")); CHECK(task.to_json().dump().find(canary) == std::string::npos);
  CHECK(unwrap(fixture.runtime->db().get_msgs(conv, true)).size() == 1);
  fixture.reopen();
  CHECK(unwrap(fixture.runtime->tasks().get(task_id))->to_json().dump().find(canary) == std::string::npos);
  CHECK(fixture.view()["messages"] == first["messages"]);
  fs::rename(fixture.source, fixture.source.string() + ".away");
  const auto absent = fixture.view();
  CHECK(absent["status"] == "unavailable"); CHECK(absent["resources"][0]["status"] == "unavailable");
  CHECK(absent["resources"][0]["current"] == false); CHECK(absent["messages"].size() == 1);
  CHECK(absent.dump().find(canary) == std::string::npos);
  fs::rename(fixture.source.string() + ".away", fixture.source);
  auto changed = fixture_source(); changed[0]["mapping"]["u"]["message"]["content"]["parts"][0] = "Changed public source bytes";
  write_zip(fixture.source, changed);
  const auto stale = fixture.view();
  CHECK(stale["status"] == "unavailable"); CHECK(stale["resources"][0]["status"] == "source_changed");
  CHECK(stale["resources"][0]["current"] == false);
  CHECK(fixture.transport->requests().empty());
  fixture.runtime.reset();
  check_no_payload(fixture.data);
  effects.check();
  check_no_payload(fixture.workspace.path() / "operation-temp");
}

TEST_CASE("conversation view additive C ABI shares headless results and keeps legacy message getter storage only") {
  Fixture fixture;
  const auto expected = fixture.view();
  const auto conv = fixture.conversations.at("safe-openai");
  fixture.runtime.reset();
  const auto options = Json{{"data_dir", fixture.data.string()}, {"start_workers", false}}.dump();
  const char* error = nullptr;
  auto* ctx = loom_init_ex(options.c_str(), &error);
  if (error) { const auto failure = take(error); FAIL(failure.dump()); }
  REQUIRE(ctx != nullptr);
  int http_attempts = 0;
  REQUIRE(loom_set_http_transport(ctx, [](const char*, LoomHttpResponse* response, void* user) {
    ++*static_cast<int*>(user); return loom_http_response_fail(response, LOOM_E_NETWORK, "unexpected fixture HTTP");
  }, &http_attempts) == LOOM_OK);
  CHECK(take(loom_get_messages_ex(ctx, conv.c_str(), 1)).size() == 1);
  CHECK(take(loom_read_conversation_view(ctx, conv.c_str(), "{}", 0))["resources"][0]["status"] == "read_denied");
  CHECK(take(loom_read_conversation_view(ctx, conv.c_str(), "{}", 1))["messages"] == expected["messages"]);
  CHECK(take(loom_read_conversation_view(ctx, conv.c_str(), "{\"read_authorized\":true}", 1))["error"]["code"] == "invalid_argument");
  CHECK(take(loom_read_conversation_view(ctx, conv.c_str(), "[]", 1))["error"]["code"] == "invalid_argument");
  CHECK(take(loom_read_conversation_view(nullptr, conv.c_str(), "{}", 1))["error"]["code"] == "invalid_argument");
  loom_shutdown(ctx);
  CHECK(http_attempts == 0);
  check_no_payload(fixture.data);
}

TEST_CASE("conversation view distinguishes recognized empty history from unavailable projection") {
  auto document = fixture_source();
  document[0]["mapping"] = Json::object();
  document[0].erase("current_node");
  Fixture fixture(document);
  const auto empty = fixture.view();
  CHECK(empty["status"] == "complete"); CHECK(empty["messages"].empty());
  CHECK(empty["resources"][0]["mapping_status"] == "recognized");
  CHECK(empty["omissions"].empty());
  fs::rename(fixture.source, fixture.source.string() + ".away");
  const auto missing = fixture.view();
  CHECK(missing["status"] == "unavailable"); CHECK_FALSE(missing["messages"].empty());
  CHECK_FALSE(missing["omissions"].empty());
}

TEST_CASE("conversation view keeps historical graph-only snapshots explicit and recomputes the full mapper when available") {
  Fixture fixture;
  auto& db = fixture.runtime->db();
  catalog::Catalog catalog(*fixture.runtime, unwrap(fixture.runtime->knowledge().pack()));
  const auto unit = fixture.units.at("safe-openai");
  const auto current = unwrap(catalog.execute_resource(unit, Json::object(), true));
  auto legacy = current["last_successful"];
  legacy.erase("mapping"); legacy.erase("schema"); // Actual pre-P4 snapshot shape.
  auto old_definition = unwrap(RuntimeProfile::builtin("resource_projection")).definition();
  old_definition["revision"] = 2;
  old_definition["description"] = "Published historical graph-only projection";
  legacy["projection_profile"] = unwrap(RuntimeProfile::from_definition(old_definition)).inspection();
  legacy["availability"] = Json{{"status", "available"}, {"current", true}};
  NodeOptions node; node.node_id = "resource:" + unit; node.metadata = legacy;
  unwrap(db.create_node(unit, "external:resource", node));
  // A previous result is immutable evidence independent of the latest root.
  NodeOptions evidence; evidence.node_id = "safe-historical-result"; evidence.metadata = legacy;
  unwrap(db.create_node("Historical fixture", "external:resource_read_result", evidence));
  fixture.reopen();
  fs::rename(fixture.source, fixture.source.string() + ".away");
  const auto unavailable = fixture.view();
  CHECK(unavailable["status"] == "unavailable"); CHECK(unavailable["messages"].size() == 1);
  CHECK(unavailable["resources"][0]["historical_snapshot"]["available"] == true);
  CHECK(unavailable["resources"][0]["historical_snapshot"]["mapping_available"] == false);
  CHECK(unavailable["resources"][0]["current"] == false);
  CHECK_FALSE(unavailable["omissions"].empty());
  CHECK(unwrap(fixture.runtime->db().get_node("resource:" + unit))->metadata == legacy);
  fs::rename(fixture.source.string() + ".away", fixture.source);
  const auto recomputed = fixture.view();
  CHECK(recomputed["status"] == "complete"); CHECK(recomputed["messages"].size() == 3);
  CHECK(recomputed["resources"][0]["projection_profile"]["definition"]["revision"] == 3);
  CHECK(unwrap(fixture.runtime->db().get_node("resource:" + unit))->metadata == legacy);
  CHECK(unwrap(fixture.runtime->db().get_node("safe-historical-result"))->metadata == legacy);
  CHECK(fixture.transport->requests().empty());
}
