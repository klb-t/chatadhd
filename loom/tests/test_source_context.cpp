#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <set>

#if defined(__linux__)
#include <sys/inotify.h>
#include <unistd.h>
#endif

#include "loom/catalog.h"
#include "loom/config.h"
#include "loom/context_engine.h"
#include "loom/knowledge.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"
#include "loom/runtime.h"
#include "loom/source_context.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"
#include "../third_party/miniz/miniz.h"

using namespace loom;
using test::unwrap;
namespace fs = std::filesystem;

namespace {
const std::array<std::string, 5> markers{
    "P4B1_PUBLIC_BODY_94261", "P4B1_PUBLIC_ALTERNATIVE_94261", "P4B1_PUBLIC_CURRENT_94261",
    "P4B1_PUBLIC_UNKNOWN_94261", "P4B1_PRIVATE_QUERY_94261"};
constexpr const char* user = "synthetic/source-context-user";

Json source_document() {
  return Json::parse(R"([
    {"id":"public-source-context","title":"Public source context fixture","current_node":"b",
     "future":{"preserve":"P4B1_PUBLIC_UNKNOWN_94261"},"mapping":{
       "root":{"parent":null,"children":["u"],"message":null},
       "u":{"parent":"root","children":["a","b"],"message":{"id":"u","author":{"role":"user"},"content":{"content_type":"text","parts":["P4B1_PUBLIC_BODY_94261: exact public observation with a sufficiently long suffix for representation tests."]}}},
       "a":{"parent":"u","children":[],"message":{"id":"a","author":{"role":"assistant"},"content":{"content_type":"text","parts":["P4B1_PUBLIC_ALTERNATIVE_94261: historical branch observation."]}}},
       "b":{"parent":"u","children":[],"message":{"id":"b","author":{"role":"assistant"},"content":{"content_type":"text","parts":["P4B1_PUBLIC_CURRENT_94261: current public branch observation."]}}}
    }}])");
}

void write_zip(const fs::path& path, const Json& document) {
  const auto bytes = document.dump();
  mz_zip_archive zip{};
  REQUIRE(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
  // Store rather than compress to make a retained container detectable too.
  REQUIRE(mz_zip_writer_add_mem(&zip, "conversations.json", bytes.data(), bytes.size(), 0));
  REQUIRE(mz_zip_writer_finalize_archive(&zip));
  REQUIRE(mz_zip_writer_end(&zip));
}

void no_markers(std::string_view bytes) {
  for (const auto& marker : markers) CHECK(bytes.find(marker) == std::string_view::npos);
}

void no_payload_files(const fs::path& directory) {
  for (const auto& entry : fs::recursive_directory_iterator(directory)) {
    if (!entry.is_regular_file()) continue;
    INFO(entry.path().string());
    no_markers(unwrap(fsutil::read_file(entry.path())));
  }
}

struct ReadEffects {
  std::optional<std::string> previous;
  log::Level level = log::level();
  std::vector<std::string> logs;
  int sink;
#if defined(__linux__)
  int watch = -1;
#endif
  explicit ReadEffects(const fs::path& directory)
      : sink(log::add_sink([this](const log::Record& record) { logs.push_back(record.message); })) {
    fs::create_directories(directory);
    log::set_level(log::Level::Debug);
    if (const auto* old = std::getenv("TMPDIR")) previous = old;
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
    log::remove_sink(sink);
    log::set_level(level);
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
    for (const auto& line : logs) no_markers(line);
#if defined(__linux__)
    // Includes staging that was removed before the operation returned.
    alignas(inotify_event) std::array<char, 8192> events{};
    CHECK(read(watch, events.data(), events.size()) == -1);
    CHECK(errno == EAGAIN);
#endif
  }
};

struct Fixture {
  fsutil::TempDir directory;
  fs::path source = directory.path() / "public.zip";
  fs::path data = directory.path() / "store";
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> runtime;
  std::string conversation, unit;
  Json selector;

  Fixture() {
    write_zip(source, source_document());
    fs::create_directories(data / "profiles");
    LOOM_REQUIRE_OK(fsutil::write_file(data / "profiles/resource_read.pack",
        Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "resource_read"},
             {"overrides", {{"projection_storage", "transient"}, {"content_index", "none"}}}}.dump()));
    reopen();
    catalog::Catalog catalog(*runtime, unwrap(runtime->knowledge().pack()));
    catalog::ScanConfig scan; scan.sources = {source.string()}; scan.retain_raw = "none";
    unwrap(catalog.scan(scan));
    const auto units = unwrap(catalog.query(catalog::UnitQuery{}));
    REQUIRE(units.size() == 1);
    unit = units.front().unit.id;
    selector = units.front().unit.locator.to_json();
    catalog::ImportOptions options; options.mode = "full"; options.store_mode = "link";
    unwrap(catalog.import_selected(options));
    conversation = unwrap(runtime->db().conn().query_text(
        "SELECT conv_id FROM loom_cat_imports WHERE unit_id=?", unit)).value();
  }

  void reopen() {
    runtime.reset();
    RuntimeOptions options; options.data_dir = data.string(); options.start_workers = false; options.http = transport;
    runtime = unwrap(Runtime::open(options));
  }

  Json profile(std::string_view identity = user) {
    onboarding::OnboardingStore store(runtime->db());
    return unwrap(store.read(identity));
  }

  Json install(std::string_view identity = user) {
    onboarding::OnboardingStore store(runtime->db());
    const auto opened = unwrap(store.open(identity));
    return unwrap(store.install_entries(identity, opened.at("revision").get<std::int64_t>(),
                                        unwrap(Runtime::source_context_defaults())));
  }

  Json action(const Json& operation, std::string_view identity = user) {
    onboarding::OnboardingStore store(runtime->db());
    return unwrap(store.apply(identity, profile(identity).at("revision").get<std::int64_t>(), operation));
  }

  Json resolution(std::string_view identity = user) {
    const auto state = profile(identity);
    const auto layers = unwrap(onboarding::DefaultLayers::create(state.at("pack"), state.at("layers")));
    return unwrap(layers.resolve("context.source"));
  }

  std::int64_t receipts() {
    auto& connection = runtime->db().conn();
    if (!connection.has_table("loom_kb_graph_receipts")) return 0;
    return unwrap(connection.query_int("SELECT COUNT(*) FROM loom_kb_graph_receipts")).value();
  }

  SourceContextRequest request(std::vector<std::size_t> indices = {0, 1, 2}) {
    SourceContextRequest request;
    request.context.text = markers.back();
    request.context.goal_type = "verify_claim"; // Existing goal data admit observed evidence.
    request.context.relation_hops = 0;
    request.context.detail_resolution = model::Resolution::Full;
    request.context.budget_tokens = 4000;
    request.profile_user = user;
    request.fragments.push_back({conversation, unit, std::move(indices), std::nullopt});
    return request;
  }
};

bool omitted(const PreparedSourceContext& result, std::string_view reason) {
  for (const auto& omission : result.receipt.at("acquisition").at("omissions"))
    if (omission.at("reason") == reason) return true;
  return false;
}

void rejected_without_read(Fixture& fixture, const SourceContextRequest& request,
                           bool grant = true) {
  const auto before = fixture.receipts();
  const auto result = fixture.runtime->prepare_source_context(request, grant);
  REQUIRE_FALSE(result);
  CHECK(fixture.receipts() == before);
  CHECK(fixture.transport->requests().empty());
}
} // namespace

TEST_CASE("source context explicitly installs R40 defaults and cannot resurrect missing disabled or excluded policy") {
  Fixture fixture;
  auto request = fixture.request();
  rejected_without_read(fixture, request); // Does not implicitly create the requested user.
  onboarding::OnboardingStore store(fixture.runtime->db());
  unwrap(store.open(user));
  rejected_without_read(fixture, request); // User exists, optional context.source does not.
  fixture.install();
  CHECK(fixture.resolution().at("status") == "effective");
  CHECK(fixture.resolution().at("layer") == "builtin");
  REQUIRE_FALSE(fixture.resolution().at("explanation").get<std::string>().empty());
  const auto installed = fixture.profile();
  bool found_graph_policy = false;
  for (const auto& entity : unwrap(fixture.runtime->knowledge().store().query_entities(
      installed.at("graph_run_id").get<std::string>(), kb::EntityQuery{}))) {
    if (entity.attrs.value("key", std::string()) != "context.source") continue;
    found_graph_policy = true;
    CHECK(entity.attrs.at("status") == "effective");
    CHECK(entity.attrs.at("value") == fixture.resolution().at("value"));
  }
  CHECK(found_graph_policy); // The same effective data live in the actual graph.
  fixture.action(Json{{"target", "layers"}, {"op", "disable"}, {"key", "context.source"}});
  request.source_profile_overrides = Json{{"admitted_statuses", Json::array({"active"})}};
  rejected_without_read(fixture, request);
  fixture.action(Json{{"target", "layers"}, {"op", "exclude"}, {"key", "context.source"}});
  auto state = fixture.profile();
  auto upgraded = state.at("pack");
  upgraded["revision"] = upgraded.at("revision").get<std::int64_t>() + 1;
  for (auto& entry : upgraded["entries"]) if (entry.at("key") == "context.source") {
    entry["revision"] = entry.at("revision").get<std::int64_t>() + 1;
    entry["value"]["label_prefix_codepoints"] = 7;
  }
  unwrap(store.update_pack(user, state.at("revision").get<std::int64_t>(), upgraded,
                          state.at("scenario_definition")));
  fixture.reopen();
  CHECK(fixture.resolution().at("status") == "excluded");
  fixture.install(); // Reinstalling defaults is not reenable.
  CHECK(fixture.resolution().at("status") == "excluded");
  rejected_without_read(fixture, request);
  fixture.action(Json{{"target", "layers"}, {"op", "reenable"}, {"key", "context.source"}});
  request.source_profile_overrides = Json::object();
  const auto prepared = unwrap(fixture.runtime->prepare_source_context(request, true));
  CHECK(prepared.receipt.at("effective_configuration").at("values").at("label_prefix_codepoints") == 7);
  CHECK_FALSE(prepared.context_set.items.empty());
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("source context reads the real linked ZIP headlessly and retains only metadata across reopen") {
  Fixture fixture;
  fixture.install();
  const auto stored = unwrap(fixture.runtime->db().get_msgs(fixture.conversation, true));
  REQUIRE(stored.size() == 1);
  no_markers(stored.front().to_json().dump());
  const auto before_receipts = fixture.receipts();
  // Installing the actual R40 profile creates its real onboarding graph. Do
  // not manufacture a knowledge run or relabel that existing run's lifecycle.
  std::set<std::string> eligible_before;
  for (const auto& run : unwrap(fixture.runtime->knowledge().store().list_context_runs(100, "done")))
    eligible_before.insert(run.id);
  auto request = fixture.request();
  ReadEffects effects(fixture.directory.path() / "operation-tmp");
  const auto prepared = unwrap(fixture.runtime->prepare_source_context(request, true));
  CHECK(prepared.receipt.at("schema") == "loom.source_context_preparation/1");
  CHECK(prepared.receipt.at("context_id") == prepared.context_set.id);
  CHECK(prepared.receipt.at("authorization") == Json{{"local_read", true}, {"egress", false}});
  CHECK(prepared.receipt.at("retention") == Json{{"context", "memory"}, {"prompt", "memory"}, {"source_projection", "transient"}});
  REQUIRE(prepared.context_set.items.size() == 2); // Active user and current branch, not historical alternative.
  for (const auto& item : prepared.context_set.items) {
    CHECK(item.ref_kind == model::RefKind::Observation);
    CHECK(item.band == model::ContextBand::Goal);
    CHECK(item.required_by.empty());
    CHECK(item.missing_premises.empty());
  }
  CHECK(prepared.prompt.find(markers[0]) != std::string::npos);
  CHECK(prepared.prompt.find(markers[2]) != std::string::npos);
  CHECK(prepared.prompt.find(markers[1]) == std::string::npos);
  no_markers(prepared.receipt.dump());
  CHECK(fixture.receipts() == before_receipts + 1);
  const auto& acquisition = prepared.receipt.at("acquisition");
  REQUIRE(acquisition.at("sources").size() == 1);
  CHECK(acquisition.at("sources")[0].at("selector") == fixture.selector);
  std::set<std::string> eligible_after;
  for (const auto& run : unwrap(fixture.runtime->knowledge().store().list_context_runs(100, "done")))
    eligible_after.insert(run.id);
  CHECK(eligible_after == eligible_before);
  // The accepted storage run and the method-run graph entity have distinct
  // identities in the existing GraphPacketStore contract.
  const auto& read_ref = acquisition.at("sources")[0].at("read_receipt_ref");
  const auto read_run_id = read_ref.at("method_receipt").at("run_id").get<std::string>();
  const auto read_run = unwrap(fixture.runtime->knowledge().store().get_run(read_run_id));
  REQUIRE(read_run.has_value());
  CHECK(read_run->summary.at("implicit_context_eligible") == false);
  CHECK(read_run->summary.at("graph_packet_receipt") == read_ref.at("method_receipt").at("receipt_id"));
  const auto produced = unwrap(fixture.runtime->knowledge().store().get_entity(
      read_run_id, read_ref.at("produced_by").at("result_entity_id").get<std::string>()));
  REQUIRE(produced.has_value());
  CHECK(produced->attrs.at("current") == true);
  CHECK(unwrap(fixture.runtime->db().get_msgs(fixture.conversation, true)).front().to_json() == stored.front().to_json());
  CHECK(fixture.transport->requests().empty());
  effects.check();
  no_payload_files(fixture.data);
  no_payload_files(fixture.directory.path() / "operation-tmp");
  // A consumer may persist the declared receipt, never ContextSet/prompt/trace.
  LOOM_REQUIRE_OK(fsutil::write_file(fixture.data / "public-preparation-receipt.json", prepared.receipt.dump()));
  const auto receipt_bytes = unwrap(fsutil::read_file(fixture.data / "public-preparation-receipt.json"));
  fixture.reopen();
  no_payload_files(fixture.data);
  CHECK(unwrap(fsutil::read_file(fixture.data / "public-preparation-receipt.json")) == receipt_bytes);
  CHECK(fixture.receipts() == before_receipts + 1); // Restart alone does not reprepare.
  rejected_without_read(fixture, request, false);
  const auto repeated = unwrap(fixture.runtime->prepare_source_context(request, true));
  CHECK(repeated.prompt == prepared.prompt);
  CHECK(repeated.context_set.id == prepared.context_set.id);
  CHECK(fixture.receipts() == before_receipts + 2);
  CHECK(fixture.transport->requests().empty());
  effects.check();
}

TEST_CASE("source context profiles and one-call data control status admission and representation without rewriting source") {
  Fixture fixture;
  fixture.install();
  auto request = fixture.request();
  request.context.detail_resolution = model::Resolution::Label;
  request.source_profile_overrides = Json{{"label_prefix_codepoints", 6}};
  const auto short_labels = unwrap(fixture.runtime->prepare_source_context(request, true));
  REQUIRE(short_labels.context_set.items.size() == 2);
  // Existing evidence_encoding data wrap observed source bytes in Markdown;
  // prefix selection precedes that shared rendering step.
  for (const auto& item : short_labels.context_set.items) CHECK(item.text == "**P4B1_P**");
  CHECK(short_labels.receipt.at("effective_configuration").at("values").at("label_prefix_codepoints") == 6);
  auto values = fixture.resolution().at("value");
  values["admitted_statuses"] = Json::array({"version"});
  values["summary_prefix_codepoints"] = 9;
  fixture.action(Json{{"target", "layers"}, {"op", "override"}, {"key", "context.source"}, {"value", values}});
  CHECK(fixture.resolution().at("layer") == "user");
  request.source_profile_overrides = Json::object();
  request.context.detail_resolution = model::Resolution::Summary;
  const auto alternative = unwrap(fixture.runtime->prepare_source_context(request, true));
  REQUIRE(alternative.context_set.items.size() == 1);
  CHECK(alternative.context_set.items[0].text == "**P4B1_PUBL**");
  CHECK(alternative.receipt.at("effective_configuration").at("values").at("admitted_statuses") == Json::array({"version"}));
  request.context.detail_resolution = model::Resolution::Full;
  const auto full = unwrap(fixture.runtime->prepare_source_context(request, true));
  CHECK(full.prompt.find(markers[1]) != std::string::npos);
  CHECK(full.prompt.find(markers[0]) == std::string::npos);
  CHECK(full.context_set.id != short_labels.context_set.id);
  fixture.install("synthetic/source-context-second-user");
  request.profile_user = "synthetic/source-context-second-user";
  const auto second = unwrap(fixture.runtime->prepare_source_context(request, true));
  REQUIRE(second.context_set.items.size() == 2);
  CHECK(second.prompt.find(markers[0]) != std::string::npos);
  CHECK(second.prompt.find(markers[1]) == std::string::npos);
  fixture.reopen();
  request.profile_user = user;
  const auto restored = unwrap(fixture.runtime->prepare_source_context(request, true));
  CHECK(restored.prompt == full.prompt);
  CHECK(fixture.resolution().at("value") == values);
  CHECK(fixture.transport->requests().empty());
  no_payload_files(fixture.data);
}

TEST_CASE("source context unsupported strategies invalid selections and missing host grant reject before source dispatch") {
  Fixture fixture;
  fixture.install();
  const auto base = fixture.request();
  rejected_without_read(fixture, base, false);
  for (const char* control : {"run", "targets", "project", "claim_targets", "counter", "plan", "radius", "channel"}) {
    INFO(control);
    auto request = base;
    const std::string which = control;
    if (which == "run") request.context.run = "synthetic/unsupported-run";
    if (which == "targets") request.context.targets = {"synthetic/entity"};
    if (which == "project") request.context.project = "synthetic/project";
    if (which == "claim_targets") request.context.claim_targets = {"synthetic/claim"};
    if (which == "counter") request.context.include_counter_evidence = true;
    if (which == "plan") request.context.plan = Json::object();
    if (which == "radius") request.context.relation_hops = 1;
    if (which == "channel") request.context.candidate_channels.push_back({"tfidf", 50, 0});
    rejected_without_read(fixture, request);
  }
  for (const char* mode : {"method_registry", "embedding", "goal_typing", "unified", "graph_reply"}) {
    INFO(mode);
    fixture.runtime->config().set("context_execution", Json{{mode, {{"enabled", true}}}});
    rejected_without_read(fixture, base);
  }
  fixture.runtime->config().set("context_execution", Json::object());
  auto request = base;
  request.read_options = Json{{"projection_storage", "snapshot"}};
  rejected_without_read(fixture, request);
  request = base; request.fragments[0].message_indices = {0, 0};
  rejected_without_read(fixture, request);
  request = base; request.fragments[0].expected_version = "synthetic/version-one";
  request.fragments.push_back({fixture.conversation, fixture.unit, {1}, "synthetic/version-two"});
  rejected_without_read(fixture, request);
  request = base; request.source_profile_overrides = Json{{"label_operator", "unsupported/operator"}};
  rejected_without_read(fixture, request);
  // Exercise Runtime's real pre-I/O path: inferred goal typing consumes cue
  // settings, so malformed policy must fail before the Catalog reads this ZIP.
  request = base;
  request.context.goal_type.reset();
  fixture.runtime->config().set("context_goal_cues", Json{{"effective_values", {{"bad", true}}}});
  const auto before_cue_failure = fixture.receipts();
  const auto malformed_cues = fixture.runtime->prepare_source_context(request, true);
  REQUIRE_FALSE(malformed_cues);
  CHECK(malformed_cues.error().code == Errc::InvalidArgument);
  CHECK(fixture.receipts() == before_cue_failure);
  CHECK(fixture.receipts() == 0);
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("source context observation locators resolve actual mapped bytes and retain identity across fragment subsets") {
  Fixture fixture;
  fixture.install();
  auto request = fixture.request();
  request.source_profile_overrides = Json{{"admitted_statuses", Json::array({"active", "version"})}};
  const auto prepared = unwrap(fixture.runtime->prepare_source_context(request, true));
  REQUIRE(prepared.context_set.items.size() == 3);
  // The native view is an independent consumer of the same full export mapper;
  // this read follows preparation, so prior rendering cannot seed the selector.
  const auto view = unwrap(fixture.runtime->read_conversation_view(fixture.conversation, Json::object(), true));
  REQUIRE(view.at("resources").size() == 1);
  const auto& resource = view.at("resources")[0];
  Json identity = Json::object();
  for (const auto* field : {"unit_id", "source", "selector", "content_hash", "version", "mapping_version", "source_index", "mapping_index_scope"})
    identity[field] = resource.at(field);
  identity["mapping_schema"] = resource.at("mapping").at("schema");
  Json rows = Json::array();
  for (const auto& message : view.at("messages")) {
    REQUIRE(message.at("storage") == "reference");
    Json row = Json::object();
    for (const auto* field : {"id", "parent_id", "role", "status", "text"}) row[field] = message.at(field);
    rows.push_back(std::move(row));
  }
  const Json projection{{"schema", "loom.source_context_projection/1"}, {"source_identity", identity}, {"messages", rows}};
  const auto projection_source = "sha256:" + Sha256::hex(json::canonical(projection));
  model::Locator root; root.source = projection_source;
  const auto mapping_unit = model::Unit::make_id(projection_source, root);
  std::set<std::string> references;
  for (const auto& item : prepared.context_set.items) {
    const auto& source_ref = item.factors.at("source_ref");
    CHECK(source_ref.at("selector") == fixture.selector);
    CHECK(source_ref.at("unit_id") == fixture.unit);
    CHECK(source_ref.at("mapping_projection").at("source") == projection_source);
    CHECK(source_ref.at("mapping_projection").at("unit_id") == mapping_unit);
    CHECK(source_ref.at("mapping_projection").at("storage") == "transient");
    const auto locator = unwrap(model::Locator::from_json(item.factors.at("observation_locator")));
    CHECK(locator.source == projection_source);
    CHECK(locator.member.empty());
    const auto text = projection.at(Json::json_pointer(locator.json_pointer)).get<std::string>();
    // The locator/hash bind the unmodified mapped bytes; ContextItem.text is
    // the existing evidence-marked representation used for prompt budgeting.
    CHECK(item.text == "**" + text + "**");
    CHECK(item.ref == model::Observation::make_id(mapping_unit, locator, text));
    CHECK(item.factors.at("text_sha256") == Sha256::hex(text));
    CHECK(item.factors.at("confidence_basis") == "quoted_bytes_not_assertion_truth");
    references.insert(item.ref);
  }
  REQUIRE(references.size() == 3);
  request.fragments[0].message_indices = {0};
  request.fragments[0].expected_version = resource.at("version").get<std::string>();
  const auto subset = unwrap(fixture.runtime->prepare_source_context(request, true));
  REQUIRE(subset.context_set.items.size() == 1);
  CHECK(references.contains(subset.context_set.items[0].ref));
  CHECK(subset.context_set.items[0].factors.at("source_ref").at("mapping_projection").at("source") == projection_source);
  // Two selectors for one conversation share one actual access/receipt.
  request.fragments.push_back({fixture.conversation, fixture.unit, {1}, resource.at("version").get<std::string>()});
  const auto before_reads = fixture.receipts();
  const auto grouped = unwrap(fixture.runtime->prepare_source_context(request, true));
  REQUIRE(grouped.context_set.items.size() == 2);
  CHECK(fixture.receipts() == before_reads + 1);
  CHECK(grouped.receipt.at("acquisition").at("sources").size() == 1);
  for (const auto& item : grouped.context_set.items) CHECK(references.contains(item.ref));
  no_markers(grouped.receipt.dump());
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("source context empty missing and stale fragment selections are explicit rather than select all") {
  Fixture fixture;
  fixture.install();
  auto request = fixture.request({});
  auto empty = unwrap(fixture.runtime->prepare_source_context(request, true));
  CHECK(empty.context_set.items.empty());
  CHECK(empty.receipt.at("status") == "empty");
  request = fixture.request({999});
  const auto missing = unwrap(fixture.runtime->prepare_source_context(request, true));
  CHECK(missing.context_set.items.empty());
  CHECK(omitted(missing, "fragment_not_found"));
  CHECK(missing.receipt.at("status") == "unavailable");
  request = fixture.request({0});
  request.fragments[0].expected_version = "synthetic/stale-version";
  const auto stale = unwrap(fixture.runtime->prepare_source_context(request, true));
  CHECK(stale.context_set.items.empty());
  CHECK(omitted(stale, "stale_selector"));
  CHECK(stale.receipt.at("status") == "unavailable");
  request = fixture.request({0});
  request.fragments[0].unit_id = "synthetic/not-bound-unit";
  const auto before_unbound = fixture.receipts();
  const auto unbound = unwrap(fixture.runtime->prepare_source_context(request, true));
  CHECK(unbound.context_set.items.empty());
  CHECK(omitted(unbound, "unit_not_bound"));
  CHECK(fixture.receipts() == before_unbound);
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("source context honors zero selection budget and rejects negative budget before access") {
  Fixture fixture;
  fixture.install();
  auto request = fixture.request({0});
  request.context.budget_tokens = -1;
  rejected_without_read(fixture, request);
  CHECK(fixture.receipts() == 0);
  request.context.budget_tokens = 0;
  const auto prepared = unwrap(fixture.runtime->prepare_source_context(request, true));
  CHECK(prepared.context_set.budget_tokens == 0);
  CHECK(prepared.context_set.used_tokens == 0);
  CHECK(prepared.context_set.items.empty());
  REQUIRE(prepared.receipt.at("selection").at("omitted").size() == 1);
  CHECK(prepared.receipt.at("selection").at("omitted")[0].at("reason") == "budget");
  CHECK(prepared.receipt.at("status") == "complete"); // Acquisition succeeded; selection is explicitly budgeted out.
  CHECK(fixture.receipts() == 1);
  no_markers(prepared.receipt.dump());
  no_payload_files(fixture.data);
  CHECK(fixture.transport->requests().empty());
}

TEST_CASE("source context source mutation or unavailability never replaces earlier prepared evidence with empty success") {
  Fixture fixture;
  fixture.install();
  auto request = fixture.request({0});
  const auto before = unwrap(fixture.runtime->prepare_source_context(request, true));
  REQUIRE(before.context_set.items.size() == 1);
  const auto original_prompt = before.prompt;
  const auto original_receipt = before.receipt.dump();
  const auto& sources = before.receipt.at("acquisition").at("sources");
  REQUIRE(sources.size() == 1);
  request.fragments[0].expected_version = sources[0].at("version").get<std::string>();
  auto changed = source_document();
  changed[0]["mapping"]["u"]["message"]["content"]["parts"][0] = "Synthetic changed public observation";
  write_zip(fixture.source, changed);
  const auto mutation = unwrap(fixture.runtime->prepare_source_context(request, true));
  CHECK(mutation.context_set.items.empty());
  CHECK(mutation.receipt.at("status") == "unavailable");
  CHECK_FALSE(mutation.receipt.at("acquisition").at("omissions").empty());
  CHECK(before.prompt == original_prompt);
  CHECK(before.receipt.dump() == original_receipt);
  fs::rename(fixture.source, fixture.source.string() + ".away");
  fixture.reopen();
  const auto unavailable = unwrap(fixture.runtime->prepare_source_context(request, true));
  CHECK(unavailable.context_set.items.empty());
  CHECK(unavailable.receipt.at("status") == "unavailable");
  CHECK_FALSE(unavailable.receipt.at("acquisition").at("omissions").empty());
  CHECK(unwrap(fixture.runtime->db().get_msgs(fixture.conversation, true)).size() == 1);
  no_markers(mutation.receipt.dump());
  no_markers(unavailable.receipt.dump());
  no_payload_files(fixture.data);
  CHECK(fixture.transport->requests().empty());
}
