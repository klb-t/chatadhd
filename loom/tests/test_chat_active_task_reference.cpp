#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "chat/active_task_spec.h"
#include "loom/result.h"
#include "loom/util/json.h"

using namespace loom;

namespace {

Json statement(std::string id, std::string kind, std::string status, std::string value,
               std::vector<std::string> conditions = {},
               std::vector<std::string> supersedes = {}, std::string event = "event.0") {
  return Json{{"id", std::move(id)},
              {"kind", std::move(kind)},
              {"status", std::move(status)},
              {"text", std::move(value)},
              {"source_event_ids", Json::array({std::move(event)})},
              {"claim_ids", Json::array()},
              {"conditions", std::move(conditions)},
              {"supersedes", std::move(supersedes)}};
}

Json valid_spec() {
  return Json{
      {"schema", "loom.active_task_spec/1"},
      {"product_ref", {{"kind", "product"}, {"id", "reference-case-v1"}}},
      {"goal_id", "reference-goal"},
      {"knowledge_run", nullptr},
      {"scope", {{"conversation_id", "conv.reference"},
                 {"branch_id", "branch.reference"},
                 {"task_id", "task.reference"}}},
      {"version", 1},
      {"previous_product_ref", nullptr},
      {"known_at", "2026-09-30T23:59:59.123400+02:00"},
      {"representation", "derived_product"},
      {"materializer", {{"id", "independent-native-reference"}, {"version", "1"}}},
      {"history_event_ids", Json::array({"event.0", "event.1", "event.2"})},
      {"source_refs",
       Json::array({
           Json{{"event_id", "event.0"},
                {"locator", {{"source", "synthetic.reference"},
                             {"json_pointer", "/turns/0"}, {"byte_start", 0}, {"byte_len", 4}}},
                {"known_at", "2026-09-30T20:00:00Z"}, {"quote", "zero"}},
           Json{{"event_id", "event.1"},
                {"locator", {{"source", "synthetic.reference"}, {"json_pointer", "/turns/1"}}},
                {"known_at", "2026-09-30T20:00:01Z"}},
           Json{{"event_id", "event.2"},
                {"locator", {{"source", "synthetic.reference"}, {"json_pointer", "/turns/2"}}},
                {"known_at", "2026-09-30T20:00:02Z"}}})},
      {"statements", Json::array({
          statement("goal", "goal", "active", "Prepare the report."),
      })},
      // Valid but deliberately untrusted input. The compiler must replace both
      // fields with its deterministic projection.
      {"compiled_instruction",
       {{"text", "x"},
        {"source_map", Json::array({
             Json{{"span", {{"byte_start", 0}, {"byte_len", 1}}},
                  {"statement_ids", Json::array({"goal"})}}})}}},
      {"extensions", {{"opaque", Json::array({"preserve", 7})}}}};
}

void check_invalid(Json spec) {
  auto result = chat::compile_active_task_spec(spec);
  REQUIRE_FALSE(result);
  CHECK(result.error().code == Errc::InvalidArgument);
}

}  // namespace

TEST_SUITE("chat_active_task_reference") {
  TEST_CASE("native compiler exactly matches the independent reference renderer") {
    auto spec = valid_spec();
    spec["statements"] = Json::array({
        statement("old", "style", "superseded", "Użyj dawnego stylu."),
        statement("goal", "goal", "active", "Napisz résumé po polsku."),
        statement("question", "open_issue", "contested", "Ustal tytuł „Żółć”.",
                  {"po akceptacji właściciela"}, {}, "event.1"),
        statement("exception", "exception", "active", "Zachowaj emoji 🧪.",
                  {"w aneksie α", "gdy wynik ≠ zero"}, {}, "event.2"),
        statement("private", "executor_context", "active", "Nie ujawniaj szkicu.",
                  {}, {}, "event.2"),
        statement("discarded", "alternative", "rejected", "Usuń źródła.",
                  {}, {}, "event.1"),
    });

    auto compiled = chat::compile_active_task_spec(spec);
    REQUIRE(compiled);
    const std::string expected =
        "[goal] Napisz résumé po polsku.\n"
        "[open_issue; contested — do not resolve without clarification] Ustal tytuł „Żółć”. "
        "[scope: po akceptacji właściciela]\n"
        "[exception] Zachowaj emoji 🧪. [scope: w aneksie α | gdy wynik ≠ zero]\n"
        "[executor_context; executor only — not product content] Nie ujawniaj szkicu.";
    CHECK((*compiled)["compiled_instruction"]["text"] == expected);
    CHECK((*compiled)["compiled_instruction"]["source_map"] == Json::array({
        Json{{"span", {{"byte_start", 0}, {"byte_len", 33}}},
             {"statement_ids", Json::array({"goal"})}},
        Json{{"span", {{"byte_start", 34}, {"byte_len", 130}}},
             {"statement_ids", Json::array({"question"})}},
        Json{{"span", {{"byte_start", 165}, {"byte_len", 75}}},
             {"statement_ids", Json::array({"exception"})}},
        Json{{"span", {{"byte_start", 241}, {"byte_len", 78}}},
             {"statement_ids", Json::array({"private"})}},
    }));
    CHECK((*compiled)["extensions"] == spec["extensions"]);
  }

  TEST_CASE("superseded and rejected clauses stay absent regardless of source order") {
    auto spec = valid_spec();
    spec["statements"] = Json::array({
        statement("old-format", "format", "superseded", "Return XML."),
        statement("rejected-plan", "alternative", "rejected", "Drop the citations.", {}, {}, "event.1"),
        statement("goal", "goal", "active", "Prepare the report."),
        statement("new-format", "format", "active", "Return JSON.", {}, {"old-format"}, "event.2"),
    });

    auto compiled = chat::compile_active_task_spec(spec);
    REQUIRE(compiled);
    CHECK((*compiled)["compiled_instruction"]["text"] ==
          "[goal] Prepare the report.\n[format] Return JSON.");
    CHECK((*compiled)["compiled_instruction"]["source_map"].size() == 2);
    CHECK((*compiled)["compiled_instruction"]["source_map"][0]["statement_ids"] ==
          Json::array({"goal"}));
    CHECK((*compiled)["compiled_instruction"]["source_map"][1]["statement_ids"] ==
          Json::array({"new-format"}));
  }

  TEST_CASE("compilation is deterministic idempotent and ignores forged compiled content") {
    auto spec = valid_spec();
    spec["compiled_instruction"]["text"] = "FORGED INSTRUCTION";
    spec["compiled_instruction"]["source_map"][0]["span"]["byte_len"] = 6;

    auto first = chat::compile_active_task_spec(spec);
    REQUIRE(first);
    auto second = chat::compile_active_task_spec(*first);
    REQUIRE(second);
    CHECK(*first == *second);
    CHECK((*first)["compiled_instruction"]["text"] == "[goal] Prepare the report.");
    CHECK((*first)["compiled_instruction"]["text"] != spec["compiled_instruction"]["text"]);
  }

  TEST_CASE("native integer boundary is explicit and malformed JSON types fail closed") {
    auto maximal = valid_spec();
    maximal["version"] = std::numeric_limits<std::uint64_t>::max();
    auto accepted = chat::compile_active_task_spec(maximal);
    REQUIRE(accepted);
    CHECK((*accepted)["version"] == std::numeric_limits<std::uint64_t>::max());

    std::vector<Json> invalid;
    invalid.push_back(valid_spec());
    invalid.back()["version"] = 0;
    invalid.push_back(valid_spec());
    invalid.back()["version"] = 1.0;  // Native JSON keeps this as float, unlike Python's numeric equality.
    invalid.push_back(valid_spec());
    invalid.back()["version"] = "1";
    invalid.push_back(valid_spec());
    invalid.back()["statements"][0]["conditions"] = Json::array({1});
    invalid.push_back(valid_spec());
    invalid.back()["statements"][0]["conditions"] = Json::array({""});
    invalid.push_back(valid_spec());
    invalid.back()["statements"][0]["supersedes"] = Json::object();
    invalid.push_back(valid_spec());
    invalid.back()["compiled_instruction"]["source_map"][0]["span"]["byte_start"] = 0.0;
    invalid.push_back(valid_spec());
    invalid.back()["compiled_instruction"]["source_map"][0]["statement_ids"] = Json::array({"missing"});
    invalid.push_back(valid_spec());
    invalid.back()["source_refs"][0]["locator"]["time_start"] = "0";
    invalid.back()["source_refs"][0]["locator"]["time_end"] = 1;

    for (const auto& candidate : invalid) check_invalid(candidate);
  }

  TEST_CASE("version values parsed beyond uint64 do not enter the native integer domain") {
    auto spec = valid_spec();
    spec["version"] = Json::parse("18446744073709551616");
    REQUIRE(spec["version"].is_number_float());
    check_invalid(std::move(spec));
  }

  TEST_CASE("large integer locator times retain exact ordering above binary64 precision") {
    auto reversed = valid_spec();
    reversed["source_refs"][0]["locator"]["time_start"] = std::uint64_t{9007199254740993ULL};
    reversed["source_refs"][0]["locator"]["time_end"] = std::uint64_t{9007199254740992ULL};
    check_invalid(std::move(reversed));

    auto ordered = valid_spec();
    ordered["source_refs"][0]["locator"]["time_start"] = std::uint64_t{9007199254740992ULL};
    ordered["source_refs"][0]["locator"]["time_end"] = std::uint64_t{9007199254740993ULL};
    CHECK(chat::compile_active_task_spec(ordered));

    auto maximal = valid_spec();
    maximal["source_refs"][0]["locator"]["time_start"] = std::numeric_limits<std::uint64_t>::max();
    maximal["source_refs"][0]["locator"]["time_end"] =
        std::numeric_limits<std::uint64_t>::max() - 1;
    check_invalid(std::move(maximal));
  }

  TEST_CASE("mixed floating and integer locator times preserve mathematical ordering") {
    const auto check_range = [](Json start, Json end, bool expected) {
      auto spec = valid_spec();
      spec["source_refs"][0]["locator"]["time_start"] = std::move(start);
      spec["source_refs"][0]["locator"]["time_end"] = std::move(end);
      CHECK(static_cast<bool>(chat::compile_active_task_spec(spec)) == expected);
    };
    check_range(std::uint64_t{9007199254740993ULL}, 9007199254740992.0, false);
    check_range(9007199254740992.0, std::uint64_t{9007199254740993ULL}, true);
    check_range(std::numeric_limits<std::uint64_t>::max(), 18446744073709551616.0, true);
    check_range(18446744073709551616.0, std::numeric_limits<std::uint64_t>::max(), false);
    check_range(1, 1.5, true);
    check_range(1.5, 1, false);
    check_range(-0.0, 0, true);
  }
}
