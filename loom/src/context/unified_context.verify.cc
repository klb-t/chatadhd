// Offline synthetic regressions. Compiled manually with the chat selector's
// source-local doctest main; this is not linked into the production library.
#include <doctest/doctest.h>

#include "unified_context.h"

#include <limits>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/graph_memory.h"
#include "loom/memory_engine.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace {
using namespace loom;

template<class T> T required(Result<T>&& result) {
  if (!result) FAIL("fixture/API failure: " << result.error().to_string());
  return std::move(result).value();
}

struct Fixture {
  fsutil::TempDir dir;
  std::unique_ptr<Database> db = required(Database::open(dir.path() / "db.sqlite"));
  Config cfg{dir.path() / "config.json"};
  std::unique_ptr<SemanticAnalyzer> analyzer = required(SemanticAnalyzer::create());
  GraphMemorySelector graph{*db, cfg, *analyzer};
  MemoryEngine memory{dir.path() / "memory.json"};
  context::ContextRequest request;

  Json build(Json knowledge = nullptr, Json options = Json::object(), bool mem = false, bool graph_enabled = false) {
    return required(context::compile_unified_context(*db, cfg, &graph, &memory, request,
        std::move(knowledge), "", mem, graph_enabled, options));
  }

  std::string message(std::string_view text) {
    const auto conv = required(db->create_conv("Synthetic archive"));
    NewMessage m;
    m.conv_id = conv.id;
    m.text = text;
    m.role = "assistant";
    m.metadata = Json{{"semantic", Json{{"summary", "WRONG_SUMMARY_MUST_NOT_BE_SOURCE"}}}};
    return required(db->create_msg(m));
  }
};

Json knowledge_item(std::string ref, std::string text, std::string band = "goal", std::string kind = "claim") {
  return Json{{"ref", std::move(ref)}, {"ref_kind", std::move(kind)}, {"band", std::move(band)},
      {"text", std::move(text)}, {"resolution", "raw"}, {"required_by", Json::array()},
      {"factors", Json{{"candidate_channels", Json::array({Json{{"id", "synthetic"}}})}}},
      {"provenance", Json{{"origin", "recorded"}, {"source_ref", "synthetic_source"}}},
      {"tokens", 1}, {"why", "synthetic regression input"}};
}

Json knowledge_fixture(Json items, Json dropped = Json::array()) {
  return Json{{"prompt", "upstream diagnostic prompt is not reused as original"},
      {"context_set", Json{{"id", "synthetic_context"}, {"items", std::move(items)}, {"dropped", std::move(dropped)}}}};
}

bool dropped_reason(const Json& result, const std::string& reason) {
  for (const auto& item : result["dropped"]) if (item.value("drop_reason", "") == reason) return true;
  return false;
}
}

TEST_CASE("unified selector counts full Unicode prompt with exact inclusive budget") {
  Fixture f;
  const auto source = knowledge_fixture(Json::array({knowledge_item("claim_alpha", "α🙂猫\nexact raw source")}));
  const auto full = f.build(source);
  const auto used = full["used_tokens"].get<std::size_t>();
  REQUIRE(used > 1);
  CHECK(used == (utf8::length(full["prompt"].get<std::string>()) + 3) / 4);
  CHECK(full["selected"].size() == 1);
  CHECK(full["selected"][0]["prompt_tokens"] == used);
  CHECK(full["knowledge_result"] == source);
  const auto exact = f.build(source, Json{{"budget_tokens", used}});
  CHECK(exact["prompt"] == full["prompt"]);
  const auto short_budget = f.build(source, Json{{"budget_tokens", used - 1}});
  CHECK(short_budget["selected"].empty());
  CHECK(short_budget["used_tokens"] == 0);
  CHECK(dropped_reason(short_budget, "rendered_prompt_budget"));
  const auto zero = f.build(source, Json{{"budget_tokens", 0}});
  CHECK(zero["prompt"] == "");
  CHECK(zero["budget_tokens"] == 0);
}

TEST_CASE("unified selector deduplicates identity and preserves distinct equal-text evidence") {
  Fixture f;
  auto a = knowledge_item("same_ref", "same wording", "stable");
  auto duplicate = a;
  duplicate["required_by"] = Json::array({"parent_claim"});
  duplicate["provenance"]["source_ref"] = "other_recorded_source";
  auto other = knowledge_item("observation_ref", "same wording", "project", "observation");
  const auto result = f.build(knowledge_fixture(Json::array({a, duplicate, other})));
  REQUIRE(result["selected"].size() == 2);
  CHECK(result["selected"][0]["required_by"] == Json::array({"parent_claim"}));
  CHECK(result["selected"][0]["duplicate_paths"][0]["provenance"]["source_ref"] == "other_recorded_source");
  CHECK(result["selected"][1]["ref"] == "observation_ref");
  CHECK(dropped_reason(result, "duplicate_identity"));
  std::size_t sum = 0;
  for (const auto& row : result["selected"]) sum += row["prompt_tokens"].get<std::size_t>();
  CHECK(sum == result["used_tokens"].get<std::size_t>());
}

TEST_CASE("unified selector preserves and renders omitted required premise diagnostics") {
  Fixture f;
  auto parent = knowledge_item("parent_claim", "conclusion with a premise", "project");
  auto premise = knowledge_item("premise_claim", std::string(2000, 'x'));
  premise["required_by"] = Json::array({"parent_claim"});
  const auto original = knowledge_fixture(Json::array({parent, premise}));
  const auto result = f.build(original, Json{{"budget_tokens", 150}});
  REQUIRE(result["selected"].size() == 1);
  CHECK(result["selected"][0]["ref"] == "parent_claim");
  CHECK(result["selected"][0]["missing_premises"] == Json::array({"premise_claim"}));
  CHECK(result["prompt"].get<std::string>().find("INCOMPLETE: premises not included: premise_claim") != std::string::npos);
  CHECK(result["used_tokens"].get<std::size_t>() <= 150);
  CHECK(result["knowledge_result"] == original);
  // Upstream omissions keep their existing fields and also mark selected parents.
  const auto upstream = f.build(knowledge_fixture(Json::array({parent}), Json::array({premise})));
  CHECK(upstream["selected"][0]["missing_premises"] == Json::array({"premise_claim"}));
  CHECK(dropped_reason(upstream, "upstream_selector"));
}

TEST_CASE("unified selector uses raw message bytes and independent graph reach and detail") {
  Fixture f;
  const std::string raw = "α🙂猫\noriginal source, not semantic summary";
  const auto mid = f.message(raw);
  const auto seed = required(f.db->create_node("SyntheticRoot"));
  const auto middle = required(f.db->create_node("SyntheticMiddle"));
  required(f.db->create_link(seed, middle));
  required(f.db->create_link(middle, mid));
  const Json shallow{{"legacy_seed_ids", Json::array({seed})}, {"legacy_relation_hops", 1}};
  const auto before = required(f.db->get_msg(mid))->to_json();
  CHECK(f.build(nullptr, shallow, false, true)["selected"].empty());
  auto deep = shallow;
  deep["legacy_relation_hops"] = 2;
  deep["legacy_detail_chars"] = 2;
  const auto result = f.build(nullptr, deep, false, true);
  REQUIRE(result["selected"].size() == 1);
  const auto& row = result["selected"][0];
  CHECK(row["text"] == "α🙂");
  CHECK(row["source_text"] == raw);
  CHECK(row["provenance"]["text_sha256"] == Sha256::hex(raw));
  CHECK(row["provenance"]["byte_span"] == Json::array({0, std::string("α🙂").size()}));
  CHECK(row["provenance"]["codepoint_span"] == Json::array({0, 2}));
  CHECK(row["provenance"]["truth_status"] == "not_assessed");
  CHECK(row["resolution"] == "raw_excerpt");
  CHECK(result["prompt"].get<std::string>().find("WRONG_SUMMARY") == std::string::npos);
  CHECK(result["prompt"].get<std::string>().find("not a truth verdict") != std::string::npos);
  CHECK(required(f.db->get_msg(mid))->to_json() == before);
  deep["legacy_detail_chars"] = 0;
  const auto whole = f.build(nullptr, deep, false, true);
  CHECK(whole["selected"][0]["text"] == raw);
  CHECK(whole["selector_channels"][2]["traversal"]["visited"] == result["selector_channels"][2]["traversal"]["visited"]);
}

TEST_CASE("unified selector reports legacy traversal truncation and configurable inactive inclusion") {
  Fixture f;
  const auto seed = required(f.db->create_node("SyntheticRoot"));
  const auto a = f.message("first synthetic source");
  const auto b = f.message("second synthetic source");
  required(f.db->create_link(seed, a));
  required(f.db->create_link(seed, b));
  const Json options{{"legacy_seed_ids", Json::array({seed})}, {"legacy_relation_hops", 1}, {"legacy_max_messages", 1}};
  const auto limited = f.build(nullptr, options, false, true);
  CHECK(limited["selected"].size() == 1);
  CHECK(limited["selector_channels"][2]["traversal"]["truncated"] == true);
  CHECK(dropped_reason(limited, "message_limit"));
  auto all = options;
  all["legacy_max_messages"] = 0;
  CHECK(f.build(nullptr, all, false, true)["selected"].size() == 2);
  all["legacy_max_visited"] = 1;
  const auto cap = f.build(nullptr, all, false, true);
  CHECK(cap["selected"].empty());
  CHECK(dropped_reason(cap, "visited_limit"));
  REQUIRE(f.db->set_msg_status(a, msg_status::kDeleted));
  all["legacy_max_visited"] = 0;
  const auto active = f.build(nullptr, all, false, true);
  CHECK(active["selected"].size() == 1);
  CHECK(dropped_reason(active, "inactive_message"));
  all["legacy_include_inactive"] = true;
  CHECK(f.build(nullptr, all, false, true)["selected"].size() == 2);
}

TEST_CASE("unified selector memory is derived with exact source references and no preview mutation") {
  Fixture f;
  const std::string raw = "  α🙂\nowner text preserved  ";
  const auto id = required(f.memory.add_node(raw));
  REQUIRE(f.cfg.save());
  const auto before = required(fsutil::read_file(f.memory.path()));
  const auto config_before = required(fsutil::read_file(f.cfg.path()));
  const auto result = f.build(nullptr, Json{{"memory_max_chars", 0}}, true);
  REQUIRE(result["selected"].size() == 1);
  const auto& row = result["selected"][0];
  CHECK(row["text"] == raw);
  CHECK(row["resolution"] == "derived");
  CHECK(row["provenance"]["origin"] == "derived");
  CHECK(row["provenance"]["raw_node_mapping_verified"] == true);
  CHECK(row["provenance"]["source"]["source_ref"] == id);
  CHECK(row["provenance"]["source"]["source_text"] == raw);
  CHECK(row["provenance"]["source"]["byte_span"] == Json::array({0, raw.size()}));
  CHECK(result["memory_input_sha256"] == Sha256::hex(f.memory.get_active_context(std::numeric_limits<std::size_t>::max())));
  CHECK(result["memory_input_mapping_verified"] == true);
  CHECK(required(fsutil::read_file(f.memory.path())) == before);
  CHECK(required(fsutil::read_file(f.cfg.path())) == config_before);
}

TEST_CASE("unified selector source priority and inclusion are caller policy") {
  Fixture f;
  required(f.memory.add_node("Synthetic memory"));
  const auto kb = knowledge_fixture(Json::array({knowledge_item("stable_claim", "synthetic knowledge", "stable")}));
  const auto base = f.build(kb, Json::object(), true);
  REQUIRE(base["selected"].size() == 2);
  CHECK(base["selected"][0]["channel"] == "memory");
  const auto reversed = f.build(kb, Json{{"source_priority", Json::array({"knowledge", "memory"})}}, true);
  CHECK(reversed["selected"][0]["channel"] == "knowledge");
  const auto memory_only = f.build(kb, Json{{"include_knowledge", false}}, true);
  CHECK(memory_only["selected"].size() == 1);
  CHECK(memory_only["selected"][0]["channel"] == "memory");
  CHECK(dropped_reason(memory_only, "channel_disabled"));
  const auto invalid = context::compile_unified_context(*f.db, f.cfg, &f.graph, &f.memory, f.request,
      kb, "", false, false, Json{{"legacy_relation_hops", -1}});
  CHECK_FALSE(invalid);
}

TEST_CASE("unified selector unavailable channels are reported without creating capabilities") {
  Fixture f;
  const auto result = required(context::compile_unified_context(*f.db, f.cfg, nullptr, nullptr,
      f.request, nullptr, "", true, true));
  CHECK(result["prompt"] == "");
  CHECK(result["selected"].empty());
  CHECK(result["selector_channels"].size() == 3);
  for (const auto& channel : result["selector_channels"]) CHECK(channel["available"] == false);
}

TEST_CASE("unified selector budgets memory per source node instead of one opaque block") {
  Fixture f;
  const auto small = required(f.memory.add_node("small usable memory node"));
  const auto large = required(f.memory.add_node(std::string(3000, 'x')));
  const auto result = f.build(nullptr, Json{{"memory_max_chars", 0}, {"budget_tokens", 100}}, true);
  REQUIRE(result["selected"].size() == 1);
  CHECK(result["selected"][0]["ref"] == small);
  bool large_dropped = false;
  for (const auto& row : result["dropped"]) if (row.value("ref", "") == large) large_dropped = true;
  CHECK(large_dropped);
  CHECK(result["used_tokens"].get<std::size_t>() <= 100);
  CHECK(result["memory_input_sha256"] == Sha256::hex(f.memory.get_active_context(std::numeric_limits<std::size_t>::max())));
}

TEST_CASE("unified selector preserves per-thesis alternative knowledge representations") {
  Fixture f;
  auto full = knowledge_item("same_ref", "same wording");
  auto label = full;
  label["resolution"] = "label";
  auto incomplete = full;
  incomplete["missing_premises"] = Json::array({"missing_for_this_projection"});
  auto project = full;
  project["band"] = "project";
  auto duplicate = full;
  full["factors"]["thesis_ids"] = Json::array({"thesis_a"});
  duplicate["factors"]["thesis_ids"] = Json::array({"thesis_b"});
  const auto result = f.build(knowledge_fixture(Json::array({full, label, incomplete, project, duplicate})));
  REQUIRE(result["selected"].size() == 4);
  CHECK(dropped_reason(result, "duplicate_identity"));
  bool combined_membership = false;
  for (const auto& row : result["selected"]) {
    if (row["band"] == "goal" && row["resolution"] == "raw" && !row.contains("missing_premises")) {
      CHECK(row["factors"]["thesis_ids"] == Json::array({"thesis_a", "thesis_b"}));
      combined_membership = true;
    }
  }
  CHECK(combined_membership);
}

TEST_CASE("unified selector preserves knowledge coverage warnings once inside its prompt budget") {
  Fixture f;
  model::ContextSet set;
  set.id = "cx_synthetic_diagnostics";
  set.goal.id = "goal_synthetic_diagnostics";
  set.goal.type = "repair";
  set.goal.text = "synthetic diagnostic request";
  set.goal.params = Json{{"retrieval_diagnostics", Json{{"status", "incomplete"}}},
      {"candidate_retrieval", Json{{"corpus_status", "ok"}, {"channels", Json::array({
          Json{{"id", "vector"}, {"status", "unavailable"}},
          Json{{"id", "synthetic_offline"}, {"status", "ok"}}})}}}};
  set.budget_tokens = 4000;
  set.items.push_back(required(model::ContextItem::from_json(knowledge_item("diagnostic_claim_a", "first knowledge statement"))));
  set.items.push_back(required(model::ContextItem::from_json(knowledge_item("diagnostic_claim_b", "second knowledge statement"))));
  set.used_tokens = 2;
  const Json original{{"goal", set.goal.to_json()}, {"context_set", set.to_json()}, {"prompt", "source build retained"}};
  const auto full = f.build(original);
  REQUIRE(full["selected"].size() == 2);
  const auto text = full["prompt"].get<std::string>();
  const std::string vector_marker = "[INCOMPLETE: candidate instrument vector: unavailable]";
  const auto vector_position = text.find(vector_marker);
  REQUIRE(vector_position != std::string::npos);
  CHECK(text.find(vector_marker, vector_position + 1) == std::string::npos);
  CHECK(text.find("retrieval encountered a store query error or reached a query cap") != std::string::npos);
  CHECK(full["knowledge_result"] == original);
  CHECK(full["selector_channels"][1]["diagnostics_mapping_status"] == "model_context_set_rendered");
  CHECK(full["used_tokens"].get<std::size_t>() == (utf8::length(text) + 3) / 4);
  const auto exact = f.build(original, Json{{"budget_tokens", full["used_tokens"]}});
  CHECK(exact["prompt"] == full["prompt"]);
  std::size_t sum = 0;
  for (const auto& row : full["selected"]) sum += row["prompt_tokens"].get<std::size_t>();
  CHECK(sum == full["used_tokens"].get<std::size_t>());
  // A budget that could fit item bytes but cannot fit its warnings excludes the
  // evidence, rather than silently removing the warning tail.
  const auto tiny = f.build(original, Json{{"budget_tokens", 30}});
  CHECK(tiny["selected"].empty());
  CHECK(tiny["prompt"] == "");
  CHECK(tiny["used_tokens"] == 0);
  CHECK(f.build(original, Json{{"include_knowledge", false}})["prompt"] == "");
  const auto custom = f.build(knowledge_fixture(Json::array({knowledge_item("custom_claim", "minimal custom context")})));
  CHECK(custom["selector_channels"][1]["diagnostics_mapping_status"] == "context_set_model_parse_unsupported");
}

TEST_CASE("unified selector dedup preserves native keyed channel scores and ranks") {
  Fixture f;
  auto first = knowledge_item("scored_claim", "identical representation");
  const Json vector_signal{{"method", "synthetic_embed"}, {"score", 0.87}, {"rank", 2},
      {"selection_relevance", 0.5}, {"ranking_signal", "reciprocal_channel_rank"}};
  const Json tfidf_signal{{"method", "tfidf_cosine"}, {"score", 0.42}, {"rank", 3},
      {"selection_relevance", 1.0 / 3}, {"ranking_signal", "reciprocal_channel_rank"}};
  first["factors"]["candidate_channels"] = Json{{"vector", vector_signal}};
  first["factors"]["thesis_ids"] = Json::array({"first_thesis"});
  first["factors"]["counter_for"] = Json::array({"first_counter_target"});
  auto duplicate = first;
  duplicate["factors"]["candidate_channels"] = Json{{"tfidf", tfidf_signal},
      {"vector", Json{{"method", "synthetic_embed"}, {"score", 0.71}, {"rank", 7}}}};
  duplicate["factors"]["thesis_ids"] = Json::array({"second_thesis"});
  duplicate["factors"]["counter_for"] = Json::array({"second_counter_target"});
  const auto original = knowledge_fixture(Json::array({first, duplicate}));
  const auto result = f.build(original);
  REQUIRE(result["selected"].size() == 1);
  const auto& row = result["selected"][0];
  REQUIRE(row["factors"]["candidate_channels"].is_object());
  CHECK(row["factors"]["candidate_channels"].size() == 2);
  CHECK(row["factors"]["candidate_channels"]["vector"] == vector_signal);
  CHECK(row["factors"]["candidate_channels"]["tfidf"] == tfidf_signal);
  CHECK(row["factors"]["thesis_ids"] == Json::array({"first_thesis", "second_thesis"}));
  CHECK(row["factors"]["counter_for"] == Json::array({"first_counter_target", "second_counter_target"}));
  CHECK(row["duplicate_paths"][0]["factors"]["candidate_channels"]["vector"]["rank"] == 7);
  CHECK(result["knowledge_result"] == original);
}

TEST_CASE("unified selector current-conversation exclusion is a configurable preset") {
  Fixture f;
  const std::string raw = "same conversation exact α🙂 source";
  const auto mid = f.message(raw);
  const auto stored = required(f.db->get_msg(mid));
  REQUIRE(stored.has_value());
  Json options{{"legacy_seed_ids", Json::array({mid})}, {"legacy_relation_hops", 0}};
  const auto excluded = required(context::compile_unified_context(*f.db, f.cfg, &f.graph, &f.memory,
      f.request, nullptr, stored->conv_id, false, true, options));
  CHECK(excluded["selected"].empty());
  CHECK(dropped_reason(excluded, "current_conversation"));
  options["legacy_include_current_conversation"] = true;
  const auto included = required(context::compile_unified_context(*f.db, f.cfg, &f.graph, &f.memory,
      f.request, nullptr, stored->conv_id, false, true, options));
  REQUIRE(included["selected"].size() == 1);
  CHECK(included["selected"][0]["ref"] == mid);
  CHECK(included["selected"][0]["source_text"] == raw);
  CHECK(included["selected"][0]["provenance"]["conversation_ref"] == stored->conv_id);
  CHECK(included["selected"][0]["provenance"]["source_ref"] == mid);
  CHECK(included["selected"][0]["provenance"]["text_sha256"] == Sha256::hex(raw));
  options["legacy_include_current_conversation"] = "invalid boolean";
  CHECK_FALSE(context::compile_unified_context(*f.db, f.cfg, &f.graph, &f.memory,
      f.request, nullptr, stored->conv_id, false, true, options));
}
