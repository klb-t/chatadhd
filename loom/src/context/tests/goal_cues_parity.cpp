// Compile only this fixture with LOOM_GOAL_CUES_PARITY_MAIN against either
// actual core. The production source glob sees an empty translation unit.
// This fixture does not implement an expected classifier or copy its formula.
#if defined(LOOM_GOAL_CUES_PARITY_MAIN)
#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "loom/context_engine.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"

namespace {
using loom::Json;
template<class T> T checked(loom::Result<T> result) {
  if (!result) throw std::runtime_error(result.error().to_string());
  return std::move(result).value();
}
struct Probe {
  std::string id;
  std::string text;
  std::optional<std::string> forced_type;
};

std::shared_ptr<const loom::kb::Pack> without_general_type(const loom::kb::Pack& builtin) {
  std::map<std::string, Json> documents;
  for (const auto& file : builtin.files()) documents[file] = builtin.file(file);
  auto& types = documents.at("goals/goal_types.json").at("goal_types");
  types.erase(std::remove_if(types.begin(), types.end(), [](const Json& type) {
    return type.at("id") == "answer_question";
  }), types.end());
  return checked(loom::kb::Pack::from_documents(std::move(documents)));
}
} // namespace

int main() {
  try {
    loom::fsutil::TempDir directory("loom_goal_cues_parity_");
    auto transport = std::make_shared<loom::net::ScriptedTransport>();
    transport->set_fallback(loom::net::ScriptedTransport::Reply::fail(
        loom::Errc::Network, "offline parity fixture forbids transport calls"));
    loom::RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    options.http = transport;
    auto runtime = checked(loom::Runtime::open(options));
    runtime->config().set("semantic_analysis", false);
    runtime->config().set("semantic_model", "");
    const auto builtin = checked(loom::kb::Pack::load_builtin());
    const auto custom = without_general_type(*builtin);
    Json rows = Json::array();
    std::size_t goals = 0, errors = 0;
    const auto execute = [&](const std::string& variation,
        std::shared_ptr<const loom::kb::Pack> pack, const Probe& probe) {
      loom::context::ContextEngine engine(*runtime, runtime->knowledge().store(), std::move(pack));
      loom::context::ContextRequest request;
      request.text = probe.text;
      request.goal_type = probe.forced_type;
      request.targets = {"synthetic_goal_target_alpha", "synthetic_goal_target_beta"};
      request.project = "synthetic_goal_project";
      const auto actual = engine.type_goal(request);
      Json result;
      if (actual) {
        ++goals;
        result = Json{{"ok", true}, {"goal", actual->to_json()}};
      } else {
        ++errors;
        result = Json{{"ok", false}, {"error", Json{{"code", loom::errc_name(actual.error().code)},
            {"message", actual.error().message}}}};
      }
      rows.push_back(Json{{"pack_variation", variation}, {"case", probe.id},
          {"request", request.to_json()}, {"forced_type", probe.forced_type ? Json(*probe.forced_type) : Json(nullptr)},
          {"result", result}, {"result_canonical", loom::json::canonical(result)}});
    };
    const std::vector<Probe> probes{
        {"empty", "", std::nullopt}, {"no_cue", "qzxv qzxv", std::nullopt},
        {"single_cue", "implement", std::nullopt},
        {"repeated_single_cue", "implement implement", std::nullopt},
        {"multiword", "is it true", std::nullopt},
        {"repeated_multiword", "is it true is it true", std::nullopt},
        {"tie", "implement write", std::nullopt},
        {"mixed_phrase_and_single_cues", "is it true implement implement", std::nullopt},
        {"bilingual_implementation", "zaimplementuj checklisty i napraw błąd", std::nullopt},
        {"bilingual_verification", "czy to prawda? sprawdź to", std::nullopt},
        {"case_unicode_and_newline", "CZY TO PRAWDA?\nSprawdź coś.", std::nullopt},
        {"historical_substring_matches", "unfixed reimplement", std::nullopt},
        {"all_zero_scores_with_empty_forced_type", "qzxv", std::string("")},
        {"forced_unknown", "implement", std::string("synthetic_unknown_goal_type")}};
    for (const auto& probe : probes) execute("builtin", builtin, probe);
    for (const auto& type : builtin->file("goals/goal_types.json").at("goal_types")) {
      const auto id = type.at("id").get<std::string>();
      execute("builtin", builtin, Probe{"forced_" + id, "qzxv", id});
    }
    execute("without_answer_question", custom, Probe{"empty_fallback", "", std::nullopt});
    execute("without_answer_question", custom, Probe{"no_cue_fallback", "qzxv", std::nullopt});
    execute("without_answer_question", custom, Probe{"forced_removed_type", "", std::string("answer_question")});
    execute("without_answer_question", custom, Probe{"retained_multiword_type", "czy to prawda", std::nullopt});
    const auto calls = transport->requests().size();
    if (calls != 0) throw std::runtime_error("offline parity fixture observed transport calls");
    std::cout << loom::json::canonical(Json{{"schema", "loom.goal_cues_parity/1"},
        {"native_pack_hashes", Json{{"builtin", builtin->hash()}, {"without_answer_question", custom->hash()}}},
        {"builtin_goal_type_count", builtin->file("goals/goal_types.json").at("goal_types").size()},
        {"row_count", rows.size()}, {"goal_count", goals}, {"error_count", errors},
        {"provider_calls", calls}, {"rows", rows}}) << '\n';
    return std::cout ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
#endif
