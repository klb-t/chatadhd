// Native source-only preparation uses the existing selector without a KB run.
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "context/context_execution.h"
#include "loom/context_engine.h"
#include "loom/db.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::context;
using loom::test::unwrap;

namespace {
struct NoSourceDispatch final : net::HttpTransport {
  int calls = 0;
  Result<net::HttpResponse> send(const net::HttpRequest&, const net::StreamSink*, const CancelToken*) override {
    ++calls;
    return Error(Errc::Auth, "source preparation test rejects network dispatch");
  }
  std::string name() const override { return "source_preparation_test"; }
};
struct CountingChannel final : CandidateChannel {
  int calls = 0;
  RetrievalBatch retrieve(std::string_view, const std::vector<RetrievalDocument>&,
      const CandidateChannelRequest&) override {
    ++calls;
    return {};
  }
};
struct Fixture {
  fsutil::TempDir directory;
  std::shared_ptr<NoSourceDispatch> http = std::make_shared<NoSourceDispatch>();
  std::unique_ptr<Runtime> runtime;
  std::shared_ptr<const kb::Pack> pack;
  std::unique_ptr<ContextEngine> engine;
  RuntimeProfile profile;
  ContextRequest request;
  SourceContextInput input;

  explicit Fixture(std::vector<std::string> texts = {"SOURCE_PAYLOAD_CANARY alpha preserved bytes",
                                                     "Another selected observation about beta"},
      std::vector<std::string> statuses = {}) {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    options.http = http;
    runtime = unwrap(Runtime::open(options));
    pack = unwrap(runtime->knowledge().pack());
    profile = unwrap(RuntimeProfile::builtin("context_source"));
    engine = std::make_unique<ContextEngine>(*runtime, runtime->knowledge().store(), pack);
    request.text = "PRIVATE_QUERY_CANARY alpha preserved bytes";
    request.goal_type = "verify_claim";
    request.relation_hops = 0;
    request.detail_resolution = model::Resolution::Full;
    request.budget_tokens = 4000;
    const auto original_source = "sha256:" + Sha256::hex(json::canonical(Json{{"messages", texts}}));
    const std::string version = Sha256::hex(json::canonical(Json{{"source", original_source}, {"statuses", statuses}}));
    model::Locator original;
    original.source = original_source;
    const auto original_unit = model::Unit::make_id(original_source, original);
    Json rows = Json::array();
    for (std::size_t index = 0; index < texts.size(); ++index)
      rows.push_back(Json{{"id", "refmsg:" + version + ":" + std::to_string(index)},
          {"text", texts[index]}, {"status", statuses.empty() ? "active" : statuses.at(index)}, {"role", "user"}});
    const Json projection{{"schema", "loom.source_context_projection/1"},
        {"source_identity", {{"unit_id", original_unit}, {"source", original_source}, {"version", version}}},
        {"messages", rows}};
    model::Locator root;
    root.source = "sha256:" + Sha256::hex(json::canonical(projection));
    const auto mapping_unit = model::Unit::make_id(root.source, root);
    for (std::size_t index = 0; index < texts.size(); ++index) {
      SourceContextObservation candidate;
      candidate.observation.unit = mapping_unit;
      candidate.observation.kind = model::ObservationKind::Utterance;
      candidate.observation.text = texts[index];
      candidate.observation.locator = root;
      candidate.observation.locator.json_pointer = "/messages/" + std::to_string(index) + "/text";
      candidate.observation.ordinal = static_cast<int>(index);
      candidate.observation.speaker = "user";
      candidate.observation.id = model::Observation::make_id(mapping_unit, candidate.observation.locator, texts[index]);
      candidate.message_ref = rows[index]["id"].get<std::string>();
      if (index) candidate.parent_ref = rows[index - 1]["id"].get<std::string>();
      candidate.source_version = version;
      candidate.message_status = rows[index]["status"].get<std::string>();
      candidate.source_ref = Json{{"unit_id", original_unit}, {"source", original_source},
          {"selector", original.to_json()}, {"version", version}, {"message_index", index},
          {"mapping_projection", {{"schema", "loom.source_context_projection/1"}, {"source", root.source},
              {"unit_id", mapping_unit}, {"storage", "transient"}, {"hash_scope", "canonical_mapped_projection"},
              {"locator", candidate.observation.locator.to_json()}}}};
      // This is a core fixture, not a Catalog receipt or a fake registered run.
      candidate.read_receipt_ref = Json{{"status", "native_fixture_without_source_io"}};
      input.observations.push_back(std::move(candidate));
    }
    input.acquisition = Json{{"schema", "loom.source_context_acquisition/1"},
        {"requested", Json::array()}, {"sources", Json::array()}, {"omissions", Json::array()}};
    bind();
  }
  void bind() { input.preflight_sha256 = unwrap(engine->preflight_source_context(request, profile)).binding_sha256; }
  model::ContextSet select() { return unwrap(engine->select(request, input, profile)); }
};
const model::ContextItem& item(const model::ContextSet& set, const std::string& ref) {
  const auto found = std::find_if(set.items.begin(), set.items.end(), [&](const auto& row) { return row.ref == ref; });
  REQUIRE(found != set.items.end());
  return *found;
}
} // namespace

TEST_SUITE("context_source_input") {
TEST_CASE("native source observations use common selection on a clean store with no implicit run") {
  Fixture f;
  auto legacy = f.engine->select(f.request);
  REQUIRE_FALSE(legacy);
  CHECK(legacy.error().code == Errc::NotFound);
  const auto before = unwrap(f.runtime->db().conn().query_int("SELECT total_changes()"));
  auto selected = f.select();
  LOOM_REQUIRE_OK(selected.validate());
  REQUIRE(selected.items.size() == 2);
  CHECK(unwrap(f.runtime->db().conn().query_int("SELECT total_changes()")) == before);
  CHECK(unwrap(f.runtime->knowledge().store().list_context_runs(10, "done")).empty());
  CHECK(selected.goal.params["retrieval_diagnostics"]["queries"].empty());
  CHECK(selected.goal.params["retrieval_diagnostics"]["scope"] == "request_scoped_observations");
  CHECK(selected.goal.params["source_context"]["knowledge_run"].is_null());
  for (const auto& value : selected.items) {
    CHECK(value.ref_kind == model::RefKind::Observation);
    CHECK(value.required_by.empty());
    CHECK(value.factors["confidence_basis"] == "quoted_bytes_not_assertion_truth");
    CHECK(value.factors["storage"] == "transient");
    CHECK(value.factors["source_ref"]["unit_id"] == f.input.observations[0].source_ref["unit_id"]);
  }
  CHECK(unwrap(f.engine->render(selected)).find("SOURCE_PAYLOAD_CANARY") != std::string::npos);
  CHECK(f.http->calls == 0);
}

TEST_CASE("source profile controls status relevance and exact prefix representations without fallback") {
  Fixture f({"SOURCE_PAYLOAD_CANARY alpha preserved bytes", "Another selected observation about beta"}, {"active", "excluded"});
  auto active = f.select();
  REQUIRE(active.items.size() == 1);
  REQUIRE(active.goal.params["source_context"]["omitted"].size() == 1);
  CHECK(active.goal.params["source_context"]["omitted"][0]["reason"] == "message_status");
  CHECK(active.goal.params["source_context"]["omitted"][0]["message_status"] == "excluded");
  f.profile = unwrap(f.profile.with_overrides(Json{{"admitted_statuses", Json::array({"active", "excluded"})},
      {"label_prefix_codepoints", 6}, {"summary_prefix_codepoints", 0}, {"base_relevance", 0.0}, {"lexical_weight", 0.0}}));
  f.request.detail_resolution = model::Resolution::Label;
  f.bind();
  auto labels = f.select();
  REQUIRE(labels.items.size() == 2);
  CHECK(item(labels, f.input.observations[0].observation.id).text.find("SOURCE") != std::string::npos);
  CHECK(item(labels, f.input.observations[0].observation.id).text.find("SOURCE_PAYLOAD") == std::string::npos);
  CHECK(item(labels, f.input.observations[0].observation.id).factors["relevance"] == 0.0);
  f.request.detail_resolution = model::Resolution::Summary;
  f.bind();
  auto summaries = f.select();
  for (const auto& value : summaries.items) {
    CHECK(value.text.find("SOURCE") == std::string::npos);
    CHECK(value.text.find("Another") == std::string::npos);
  }
  f.request.detail_resolution = model::Resolution::Raw;
  f.bind();
  CHECK(item(f.select(), f.input.observations[0].observation.id).text.find("SOURCE_PAYLOAD_CANARY") != std::string::npos);
  CHECK(f.http->calls == 0);
}

TEST_CASE("shared budget produces exact source omissions and never uses display detail for relevance") {
  Fixture f({"alpha " + std::string(240, 'x'), "beta " + std::string(240, 'y')});
  auto full = f.select();
  f.request.detail_resolution = model::Resolution::Label;
  f.bind();
  auto labels = f.select();
  for (const auto& value : full.items)
    CHECK(item(labels, value.ref).factors["relevance"] == value.factors["relevance"]);
  f.request.budget_tokens = 1;
  f.request.detail_resolution = model::Resolution::Full;
  f.bind();
  auto tiny = f.select();
  CHECK(tiny.items.empty());
  REQUIRE(tiny.dropped.size() == 2);
  REQUIRE(tiny.goal.params["source_context"]["omitted"].size() == 2);
  for (const auto& omission : tiny.goal.params["source_context"]["omitted"]) {
    CHECK(omission["reason"] == "budget");
    CHECK(omission["tokens"].get<int>() > 1);
    CHECK(omission["resolution"] == "full");
  }
  CHECK(tiny.used_tokens <= tiny.budget_tokens);
  LOOM_REQUIRE_OK(tiny.validate());
}

TEST_CASE("source budget rejects negative values and preserves explicit zero even for empty representations") {
  Fixture f;
  const auto before = unwrap(f.runtime->db().conn().query_int("SELECT total_changes()"));
  for (const int budget : {-1, std::numeric_limits<int>::min()}) {
    f.request.budget_tokens = budget;
    auto preflight = f.engine->preflight_source_context(f.request, f.profile);
    REQUIRE_FALSE(preflight);
    CHECK(preflight.error().code == Errc::InvalidArgument);
    auto selected = f.engine->select(f.request, f.input, f.profile);
    REQUIRE_FALSE(selected);
    CHECK(selected.error().code == Errc::InvalidArgument);
  }
  f.request.budget_tokens = 0;
  f.profile = unwrap(f.profile.with_overrides(Json{{"summary_prefix_codepoints", 0}}));
  for (const auto resolution : {model::Resolution::Full, model::Resolution::Summary}) {
    f.request.detail_resolution = resolution;
    f.bind();
    const auto selected = f.select();
    CHECK(selected.budget_tokens == 0);
    CHECK(selected.used_tokens == 0);
    CHECK(selected.items.empty());
    REQUIRE(selected.dropped.size() == f.input.observations.size());
    const auto& budget = selected.goal.params["retrieval_diagnostics"]["budget"];
    CHECK(budget["total_tokens"] == 0);
    CHECK(budget["used_tokens"] == 0);
    CHECK(budget["initial_band_tokens"] == Json::array({0, 0, 0}));
    CHECK(budget["final_band_tokens"] == Json::array({0, 0, 0}));
    CHECK(budget["used_band_tokens"] == Json::array({0, 0, 0}));
    const auto& selection = selected.goal.params["source_context"];
    CHECK(selection["selected"].empty());
    REQUIRE(selection["omitted"].size() == f.input.observations.size());
    for (const auto& omission : selection["omitted"])
      CHECK(omission["reason"] == "budget");
    LOOM_REQUIRE_OK(selected.validate());
    CHECK(unwrap(f.engine->render(selected)).find("SOURCE_PAYLOAD_CANARY") == std::string::npos);
  }
  // Empty extractive content can also render to zero tokens under a valid
  // data-defined encoding; zero still means no admitted source items.
  std::map<std::string, Json> documents;
  for (const auto& path : f.pack->files()) documents.emplace(path, f.pack->file(path));
  documents.at("policy/evidence_encoding.json")["evidence"]["observed"]["markdown"] = "{value}";
  f.pack = unwrap(kb::Pack::from_documents(std::move(documents)));
  f.engine = std::make_unique<ContextEngine>(*f.runtime, f.runtime->knowledge().store(), f.pack);
  f.bind();
  const auto empty_render = f.select();
  CHECK(empty_render.budget_tokens == 0);
  CHECK(empty_render.used_tokens == 0);
  CHECK(empty_render.items.empty());
  REQUIRE(empty_render.dropped.size() == f.input.observations.size());
  for (const auto& dropped : empty_render.dropped) {
    CHECK(dropped.tokens == 0);
    CHECK(dropped.factors["exclusion_reason"] == "budget");
  }
  CHECK(unwrap(f.runtime->db().conn().query_int("SELECT total_changes()")) == before);
  CHECK(f.http->calls == 0);
}

TEST_CASE("source content identity excludes fresh access receipts and retains metadata-only selection trace") {
  Fixture f;
  f.input.acquisition["sources"].push_back(Json{{"unit_id", f.input.observations[0].source_ref["unit_id"]},
      {"status", "native_fixture"}, {"read_receipt_ref", f.input.observations[0].read_receipt_ref}});
  auto first = f.select();
  const auto first_trace = f.engine->trace(first);
  CHECK(first_trace["source_context"] == first.goal.params["source_context"]);
  CHECK(first_trace["source_context"].dump().find("SOURCE_PAYLOAD_CANARY") == std::string::npos);
  CHECK(first_trace["source_context"].dump().find("PRIVATE_QUERY_CANARY") == std::string::npos);
  CHECK(first_trace["source_context"]["method_run_trace"]["status"] == "unavailable");
  CHECK(first_trace["goal_typing_capability"]["available"] == false);
  f.input.observations[0].read_receipt_ref["receipt_id"] = "another-real-access-receipt";
  f.input.acquisition["sources"][0]["read_receipt_ref"] = f.input.observations[0].read_receipt_ref;
  f.input.acquisition["arbitrary_native_field"] = "NATIVE_METADATA_CANARY";
  const auto second = f.select();
  CHECK(first.id == second.id);
  CHECK(first.goal.params["source_context"]["input_sha256"] == second.goal.params["source_context"]["input_sha256"]);
  CHECK(first.goal.params["source_context"]["acquisition_identity_sha256"] == second.goal.params["source_context"]["acquisition_identity_sha256"]);
  CHECK(first.goal.params["source_context"]["acquisition_sha256"] != second.goal.params["source_context"]["acquisition_sha256"]);
  CHECK(f.engine->trace(second)["source_context"].dump().find("NATIVE_METADATA_CANARY") == std::string::npos);
  Fixture changed({f.input.observations[0].observation.text + " changed bytes", f.input.observations[1].observation.text});
  CHECK(changed.select().id != second.id);
}

TEST_CASE("preflight rejects configured execution and every requested injected candidate channel without dispatch") {
  Fixture f;
  auto channel = std::make_shared<CountingChannel>();
  for (const auto* id : {"tfidf", "lexical", "native_external"}) {
    f.engine->set_candidate_channel(id, channel);
    auto request = f.request;
    request.candidate_channels = {CandidateChannelRequest{id, 10, 0.0}};
    auto result = f.engine->preflight_source_context(request, f.profile);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::Unavailable);
  }
  for (const auto* section : {"method_registry", "goal_typing", "embedding", "unified", "graph_reply"}) {
    f.runtime->config().set("context_execution", Json{{section, {{"enabled", true}}}});
    auto result = f.engine->preflight_source_context(f.request, f.profile);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == Errc::Unavailable);
  }
  f.runtime->config().set("context_execution", Json::object());
  {
    ContextExecutionScope scope(Json::object());
    CHECK_FALSE(f.engine->preflight_source_context(f.request, f.profile));
  }
  f.bind(); // unused installed capabilities do not prevent offline preparation
  CHECK(f.select().items.size() == 2);
  CHECK(channel->calls == 0);
  CHECK(f.http->calls == 0);
}

TEST_CASE("source preflight binds actual cue policy and leaves forced goal independent of unused cues") {
  Fixture f;
  f.request.goal_type.reset();
  f.request.text = "qzxv_no_cue";
  f.bind();
  const auto before = f.select();
  f.runtime->config().set("context_goal_cues", Json{{"effective_values", {{"bad", true}}}});
  auto malformed = f.engine->preflight_source_context(f.request, f.profile);
  REQUIRE_FALSE(malformed);
  CHECK(malformed.error().code == Errc::InvalidArgument);
  f.request.goal_type = "verify_claim";
  f.bind();
  CHECK(f.select().goal.type == "verify_claim");
  f.runtime->config().set("context_goal_cues", Json::object());
  f.request.goal_type.reset();
  f.bind();
  auto policy = unwrap(RuntimeProfile::builtin("context_goal_cues"));
  auto values = policy.values();
  values["no_cue_confidence"] = 0.125;
  f.runtime->config().set("context_goal_cues", Json{{"effective_values", values}});
  auto stale = f.engine->select(f.request, f.input, f.profile);
  REQUIRE_FALSE(stale);
  CHECK(stale.error().code == Errc::Conflict);
  f.bind();
  const auto current = f.select();
  CHECK(current.goal.confidence == 0.125);
  CHECK(current.id != before.id);
  CHECK(f.http->calls == 0);
}

TEST_CASE("source preflight rejects unsupported controls and representation operators before payload") {
  Fixture f;
  for (const auto& request : {Json{{"relation_hops", 1}}, Json{{"run", "kr_unrelated"}},
      Json{{"targets", Json::array({"e_unrelated"})}}, Json{{"project", "e_unrelated"}},
      Json{{"claim_targets", Json::array({"cl_unrelated"})}}, Json{{"include_counter_evidence", true}},
      Json{{"lexical_shadow", true}}, Json{{"candidate_scan_limit", 2}},
      Json{{"plan", {{"id", "plan"}, {"theses", Json::array({Json{{"id", "one"}, {"text", "goal"}}})}}}}}) {
    auto value = f.request.to_json(); value.update(request);
    const auto parsed = unwrap(ContextRequest::from_json(value));
    auto rejected = f.engine->preflight_source_context(parsed, f.profile);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::Unavailable);
  }
  for (const char* field : {"label_operator", "summary_operator", "full_operator", "raw_operator"}) {
    const auto profile = unwrap(f.profile.with_overrides(Json{{field, "unimplemented_model_summary"}}));
    auto rejected = f.engine->preflight_source_context(f.request, profile);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::Unavailable);
  }
  auto changed = f.request; changed.budget_tokens += 1;
  auto stale = f.engine->select(changed, f.input, f.profile);
  REQUIRE_FALSE(stale);
  CHECK(stale.error().code == Errc::Conflict);
  CHECK(f.http->calls == 0);
}

TEST_CASE("native source identity rejects duplicate rows and false original-source mapping pointers") {
  Fixture f;
  auto duplicate = f.input;
  duplicate.observations.push_back(duplicate.observations[0]);
  auto result = f.engine->select(f.request, duplicate, f.profile);
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Errc::InvalidArgument);
  auto false_pointer = f.input;
  auto& row = false_pointer.observations[0];
  row.observation.locator.source = row.source_ref["source"].get<std::string>();
  row.observation.id = model::Observation::make_id(row.observation.unit, row.observation.locator, row.observation.text);
  result = f.engine->select(f.request, false_pointer, f.profile);
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Errc::InvalidArgument);
  auto mismatched = f.input;
  mismatched.observations[0].source_version = "another-version";
  result = f.engine->select(f.request, mismatched, f.profile);
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Errc::InvalidArgument);
}

TEST_CASE("prefix lengths preserve the host integer range without narrowing or allocating the requested length") {
  Fixture f;
  f.request.detail_resolution = model::Resolution::Label;
  for (const Json& length : {Json(std::int64_t{0}), Json(std::uint64_t{0}),
                            Json(std::int64_t{6}), Json(std::uint64_t{6})}) {
    f.profile = unwrap(f.profile.with_overrides(Json{{"label_prefix_codepoints", length}}));
    auto preflight = f.engine->preflight_source_context(f.request, f.profile);
    REQUIRE(preflight);
    f.input.preflight_sha256 = preflight->binding_sha256;
    const auto selected = f.select();
    const auto& rendered = item(selected, f.input.observations[0].observation.id);
    CHECK(rendered.text.find("SOURCE_PAYLOAD_CANARY") == std::string::npos);
    CHECK((rendered.text.find("SOURCE") != std::string::npos) == (length.get<std::uint64_t>() == 6));
    CHECK(rendered.factors["representations"]["label"]["truncated"] == true);
  }
  for (const Json& length : {Json(std::numeric_limits<std::int64_t>::max()),
                            Json(std::numeric_limits<std::uint64_t>::max())}) {
    f.profile = unwrap(f.profile.with_overrides(Json{{"label_prefix_codepoints", length}}));
    auto preflight = f.engine->preflight_source_context(f.request, f.profile);
    if (length.get<std::uint64_t>() > std::numeric_limits<std::size_t>::max()) {
      REQUIRE_FALSE(preflight);
      CHECK(preflight.error().code == Errc::InvalidArgument);
    } else {
      REQUIRE(preflight);
      f.input.preflight_sha256 = preflight->binding_sha256;
      const auto selected = f.select();
      CHECK(item(selected, f.input.observations[0].observation.id).text.find("SOURCE_PAYLOAD_CANARY") != std::string::npos);
      CHECK(item(selected, f.input.observations[0].observation.id).factors["representations"]["label"]["truncated"] == false);
    }
  }
  for (const Json& invalid : {Json(std::int64_t{-1}), Json(0.0), Json(6.0), Json(6.5)}) {
    const auto profile = f.profile.with_overrides(Json{{"label_prefix_codepoints", invalid}});
    REQUIRE_FALSE(profile);
    CHECK(profile.error().code == Errc::InvalidArgument);
  }
}

TEST_CASE("empty or wholly excluded source input remains distinguishable from unavailable acquisition") {
  Fixture f;
  auto empty = f.input;
  empty.observations.clear();
  const auto selected = unwrap(f.engine->select(f.request, empty, f.profile));
  CHECK(selected.items.empty());
  CHECK(selected.goal.params["source_context"]["considered"].empty());
  CHECK(selected.goal.params["source_context"]["omitted"].empty());
  f.profile = unwrap(f.profile.with_overrides(Json{{"admitted_statuses", Json::array()}}));
  f.bind();
  const auto excluded = f.select();
  CHECK(excluded.items.empty());
  CHECK(excluded.goal.params["source_context"]["considered"].size() == 2);
  CHECK(excluded.goal.params["source_context"]["omitted"].size() == 2);
  for (const auto& row : excluded.goal.params["source_context"]["omitted"])
    CHECK(row["reason"] == "message_status");
  CHECK(f.http->calls == 0);
}

TEST_CASE("empty source results retain stable acquisition identity without volatile read receipts or arbitrary metadata") {
  Fixture f;
  auto input = f.input;
  input.observations.clear();
  const Json requested{{"conversation_id", "public-conversation"}, {"unit_id", "public-unit"},
      {"message_indices", Json::array()}, {"expected_version", nullptr}};
  input.acquisition["requested"] = Json::array({requested});
  const auto empty_selection = unwrap(f.engine->select(f.request, input, f.profile));
  input.acquisition["requested"][0]["message_indices"] = Json::array({0});
  input.acquisition["sources"] = Json::array({Json{{"conversation_id", "public-conversation"},
      {"unit_id", "public-unit"}, {"source", "public-source"}, {"status", "unavailable"}, {"current", false},
      {"content_hash", "public-content-hash"}, {"mapping_version", "public-mapping-v1"},
      {"source_index", 0}, {"mapping_index_scope", "source_array"},
      {"read_receipt_ref", {{"method_receipt", {{"receipt_id", "first-read"}}}}}}});
  input.acquisition["omissions"] = Json::array({Json{{"conversation_id", "public-conversation"},
      {"unit_id", "public-unit"}, {"reason", "unavailable"}}});
  const auto unavailable = unwrap(f.engine->select(f.request, input, f.profile));
  CHECK(unavailable.id != empty_selection.id);
  CHECK(unavailable.goal.params["source_context"]["input_sha256"] == empty_selection.goal.params["source_context"]["input_sha256"]);
  input.acquisition["sources"][0]["read_receipt_ref"]["method_receipt"]["receipt_id"] = "second-read";
  input.acquisition["sources"][0]["arbitrary_private_metadata"] = "ACQUISITION_METADATA_CANARY";
  input.acquisition["omissions"][0]["message"] = "PRIVATE_ERROR_BODY_CANARY";
  const auto repeated = unwrap(f.engine->select(f.request, input, f.profile));
  CHECK(repeated.id == unavailable.id);
  CHECK(repeated.goal.params["source_context"]["acquisition_sha256"] != unavailable.goal.params["source_context"]["acquisition_sha256"]);
  CHECK(repeated.goal.params["source_context"].dump().find("ACQUISITION_METADATA_CANARY") == std::string::npos);
  CHECK(repeated.goal.params["source_context"].dump().find("PRIVATE_ERROR_BODY_CANARY") == std::string::npos);
  for (const char* reason : {"stale_selector", "fragment_not_found", "mapping_unavailable"}) {
    auto variant = input;
    variant.acquisition["omissions"][0]["reason"] = reason;
    CHECK(unwrap(f.engine->select(f.request, variant, f.profile)).id != unavailable.id);
  }
  for (const auto& change : {Json{{"content_hash", "changed-content"}}, Json{{"mapping_version", "v2"}},
      Json{{"source_index", 1}}, Json{{"status", "partial"}}, Json{{"current", true}}}) {
    auto variant = input;
    variant.acquisition["sources"][0].update(change);
    CHECK(unwrap(f.engine->select(f.request, variant, f.profile)).id != unavailable.id);
  }
  auto different_request = input;
  different_request.acquisition["requested"][0]["expected_version"] = "pinned-other-version";
  CHECK(unwrap(f.engine->select(f.request, different_request, f.profile)).id != unavailable.id);
  auto reordered = input;
  reordered.acquisition = Json{{"omissions", input.acquisition["omissions"]},
      {"sources", input.acquisition["sources"]}, {"requested", input.acquisition["requested"]},
      {"schema", input.acquisition["schema"]}};
  CHECK(unwrap(f.engine->select(f.request, reordered, f.profile)).id == repeated.id);
  for (const char* collection : {"requested", "sources", "omissions"}) {
    auto malformed = input;
    malformed.acquisition[collection] = Json::object();
    auto rejected = f.engine->select(f.request, malformed, f.profile);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::InvalidArgument);
    malformed.acquisition[collection] = Json::array({"not-an-object"});
    rejected = f.engine->select(f.request, malformed, f.profile);
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == Errc::InvalidArgument);
  }
}
}
