// Actual consumer regressions, linked only by the standalone runner below.
// The core source glob sees an empty unit without this explicit macro.
#if defined(LOOM_GOAL_CUES_CONFIGURATION_MAIN)
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "../context_execution.h"
#include "loom/context_engine.h"
#include "loom/knowledge.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"

namespace {
using namespace loom;
template<class T> T checked(Result<T> result) {
  if (!result) throw std::runtime_error(result.error().to_string());
  return std::move(result).value();
}
struct Fixture {
  fsutil::TempDir directory;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> runtime;
  std::shared_ptr<const kb::Pack> pack;
  std::unique_ptr<context::ContextEngine> engine;
  context::ContextRequest request;
  Json rows = Json::array();
  std::size_t assertions = 0;

  Fixture() {
    transport->set_fallback(net::ScriptedTransport::Reply::fail(Errc::Network, "offline configuration fixture forbids transport"));
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    options.http = transport;
    runtime = checked(Runtime::open(options));
    runtime->config().set("semantic_analysis", false);
    runtime->config().set("semantic_model", "");
    pack = checked(kb::Pack::load_builtin());
    auto& store = runtime->knowledge().store();
    request.run = checked(store.begin_run(pack->hash(), Json{{"fixture", "synthetic_goal_cues_configuration"}})).id;
    if (!store.finish_run(request.run, "done", Json::object())) throw std::runtime_error("finish synthetic run failed");
    request.text = "qzxv";
    engine = std::make_unique<context::ContextEngine>(*runtime, store, pack);
  }
  void require(bool condition, const std::string& message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
  }
  void configure(const Json& values) {
    runtime->config().set("context_goal_cues", Json{{"effective_values", values}});
  }
  model::Goal goal(const std::string& name) {
    auto result = checked(engine->type_goal(request));
    rows.push_back(Json{{"case", name}, {"goal", result.to_json()}});
    return result;
  }
  void rejected(const std::string& name, Errc expected = Errc::InvalidArgument) {
    const auto result = engine->type_goal(request);
    require(!result, name + ": invalid consumed configuration was accepted");
    require(result.error().code == expected, name + ": wrong native error code");
    rows.push_back(Json{{"case", name}, {"error", Json{{"code", errc_name(result.error().code)},
        {"message", result.error().message}}}});
  }
};
}

int main(int argc, char** argv) {
  try {
    if (argc < 2 || argc > 3) throw std::runtime_error("usage: fixture canonical_descriptor [--mutated-builtin]");
    const bool mutated = argc == 3 && std::string(argv[2]) == "--mutated-builtin";
    if (argc == 3 && !mutated) throw std::runtime_error("unsupported fixture mode");
    const Json definition = checked(json::parse(checked(fsutil::read_file(argv[1]))));
    const Json defaults = definition.at("defaults");
    Fixture f;
    const auto baseline = f.goal("builtin");
    const auto baseline_context = checked(f.engine->select(f.request));
    f.rows.back()["context"] = baseline_context.to_json();
    f.require(baseline.confidence == defaults.at("no_cue_confidence").get<double>(), "actual builtin confidence does not consume descriptor");
    f.require(baseline.type == defaults.at("fallback_goal_type").get<std::string>(), "actual builtin fallback type does not consume descriptor");
    const auto legacy_id = model::Goal::make_id(baseline.type, baseline.text, baseline.targets);
    f.require(baseline.params.contains("goal_cues") == mutated, "builtin recipe drift must be traced; unchanged legacy recipe must retain parity");
    f.require((baseline.id != legacy_id) == mutated, "builtin recipe drift must invalidate legacy Goal identity");
    if (mutated) {
      f.require(baseline.params["goal_cues"]["source"] == "builtin", "mutated builtin was falsely attributed to caller");
      f.require(baseline.params["goal_cues"]["values"] == defaults, "mutated builtin trace differs from actual values");
      f.require(baseline.params["goal_cues"]["revision"] == definition["revision"], "mutated builtin revision was not captured");
    }
    f.require(checked(f.engine->type_goal(f.request)).to_json() == baseline.to_json(), "same builtin operation is not deterministic");
    f.require(checked(f.engine->select(f.request)).id == baseline_context.id, "same builtin Context identity is not deterministic");

    Json confidence_values = defaults;
    confidence_values["no_cue_confidence"] = 0.125;
    f.configure(confidence_values);
    const auto configured = f.goal("config_confidence");
    const auto configured_context = checked(f.engine->select(f.request));
    f.rows.back()["context"] = configured_context.to_json();
    f.require(configured.type == baseline.type, "confidence-only override changed the actual selected type");
    f.require(configured.confidence == 0.125, "actual no-cue confidence override was ignored");
    f.require(configured.id != baseline.id, "changed recipe retained stale Goal identity");
    f.require(configured_context.id != baseline_context.id, "changed recipe retained stale Context identity");
    f.require(configured.params["goal_cues"]["values"] == confidence_values, "actual configured values were not inspected");
    f.require(configured.params["goal_cues"]["source"] == "caller_effective_values", "actual config source was not inspected");
    f.require(checked(f.engine->type_goal(f.request)).id == configured.id, "same configured Goal identity is not deterministic");

    Json scoped_values = defaults;
    scoped_values["no_cue_confidence"] = 0.375;
    {
      context::ContextExecutionScope scope(Json{{"goal_typing", Json{{"goal_cues", Json{{"effective_values", scoped_values}}}}}});
      const auto scoped = f.goal("scope_confidence");
      const auto scoped_context = checked(f.engine->select(f.request));
      f.require(scoped.confidence == 0.375, "actual scope did not override config");
      f.require(scoped.params["goal_cues"]["values"] == scoped_values, "scope inspection is stale config data");
      f.require(scoped.params["goal_cues"]["hash"] != configured.params["goal_cues"]["hash"], "distinct consumed snapshots have the same hash");
      f.require(scoped.id != configured.id, "distinct scoped recipe retained configured Goal identity");
      f.require(scoped_context.id != configured_context.id, "distinct scoped recipe retained configured Context identity");
    }
    f.require(f.goal("scope_restored").to_json() == configured.to_json(), "scope mutated persistent config or leaked after destruction");

    f.configure(defaults);
    const auto explicit_default = f.goal("explicit_complete_defaults");
    f.require(explicit_default.params.contains("goal_cues"), "explicit complete settings lost their invocation provenance");
    f.require(explicit_default.params["goal_cues"]["values"] == defaults, "explicit complete defaults changed values");

    f.runtime->config().set("context_goal_cues", Json::object());
    f.request.text = "is it true implement implement";
    const auto original_winner = f.goal("original_ranking");
    Json phrase_values = defaults;
    phrase_values["cue_weight_per_additional_word"] = 1.0;
    f.configure(phrase_values);
    const auto phrase_winner = f.goal("configured_ranking");
    f.require(original_winner.type == "implement_part", "synthetic baseline ranking does not exercise the short-cue winner");
    f.require(phrase_winner.type == "verify_claim", "actual configurable multiword weight failed to reverse winner");
    f.require(phrase_winner.params["scores"]["verify_claim"] > phrase_winner.params["scores"]["implement_part"], "reported scores disagree with actual winner");
    f.request.text = "implement";
    Json denominator_values = defaults;
    denominator_values["confidence_denominator_offset"] = 0;
    f.configure(denominator_values);
    f.require(f.goal("zero_denominator_offset").confidence == 1, "zero offset was incorrectly clamped to a preset");
    Json signed_values = defaults;
    signed_values["cue_weight_base"] = -7;
    f.configure(signed_values);
    const auto signed_goal = f.goal("signed_weights");
    f.require(signed_goal.params["scores"]["implement_part"].get<double>() < 0, "signed cue weight was rejected or clamped");

    f.request.text = "qzxv";
    Json missing = defaults;
    missing.erase("cue_weight_base");
    f.configure(missing);
    f.rejected("missing_consumed_field");
    Json unknown = defaults;
    unknown["unconsumed_knob"] = 1;
    f.configure(unknown);
    f.rejected("unknown_consumed_field");
    Json wrong_type = defaults;
    wrong_type["no_cue_confidence"] = "0.25";
    f.configure(wrong_type);
    f.rejected("wrong_numeric_type");
    Json confidence_outside = defaults;
    confidence_outside["no_cue_confidence"] = 2;
    f.configure(confidence_outside);
    f.rejected("invalid_confidence_representation");
    Json invalid_denominator = defaults;
    invalid_denominator["confidence_denominator_offset"] = -1;
    f.configure(invalid_denominator);
    f.request.text = "implement";
    f.rejected("actual_zero_confidence_denominator");
    f.request.goal_type = "implement_part";
    f.configure(missing);
    const auto forced = f.goal("forced_goal_ignores_unused_invalid_cue_policy");
    f.require(forced.confidence == 1 && forced.type == "implement_part", "explicit owner goal did not remain authoritative");
    f.require(!forced.params.contains("goal_cues"), "forced goal falsely claimed execution of unused cue policy");
    f.request.goal_type.reset();
    f.request.text = "qzxv";
    Json unavailable_fallback = defaults;
    unavailable_fallback["fallback_goal_type"] = "synthetic_missing_native_type";
    unavailable_fallback["fallback_when_unavailable"] = "error";
    f.configure(unavailable_fallback);
    f.rejected("actual_missing_fallback", Errc::NotFound);
    unavailable_fallback["fallback_when_unavailable"] = "first_by_id";
    f.configure(unavailable_fallback);
    std::string first;
    for (const auto& type : f.pack->file("goals/goal_types.json")["goal_types"]) {
      const auto id = type["id"].get<std::string>();
      if (first.empty() || id < first) first = id;
    }
    f.require(f.goal("actual_first_by_id_fallback").type == first, "configured first_by_id operation was not executed");
    f.require(f.transport->requests().empty(), "configuration consumer made an unauthorized provider request");
    std::cout << json::canonical(Json{{"schema", "loom.goal_cues_configuration/1"},
        {"mutated_builtin", mutated}, {"assertions", f.assertions}, {"provider_calls", f.transport->requests().size()},
        {"rows", f.rows}}) << '\n';
    return std::cout ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
#endif
