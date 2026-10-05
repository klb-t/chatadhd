// Standalone source-private offline regression. Empty in the core source glob.
#if defined(LOOM_CHAT_REASONING_PROFILE_TEST_MAIN)
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/net/http.h"
#include "loom/runtime.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "chat/reasoning_profile.h"

namespace {
using namespace loom;

struct Checks {
  Json rows = Json::array();
  std::size_t failures = 0;
  std::size_t fake_http_requests = 0;
  void check(bool passed, std::string_view name, Json evidence = Json::object()) {
    rows.push_back(Json{{"name", name}, {"passed", passed}, {"evidence", std::move(evidence)}});
    if (!passed) ++failures;
  }
  void equal(const Json& actual, const Json& expected, std::string_view name) {
    check(actual == expected, name, Json{{"actual", actual}, {"expected", expected}});
  }
};

template<class T> T must(Result<T> value) {
  if (!value) throw std::runtime_error(value.error().to_string());
  return std::move(*value);
}

Json custom_values() {
  return Json{{"thinking_indicators", Json::array({"oddcore"})},
      {"budget_model_markers", Json::array({"BudgetX"})},
      {"budget_by_effort", Json{{"turbo", 500001}, {"", 700002}, {"LOW", 800003}}},
      {"unknown_effort_budget", 900004}, {"adaptive_effort", "automatic"},
      {"verbosity_effort", "verbose"}, {"verbosity_value", "caller-custom-verbosity"}};
}

Json exact(const Json& values) { return Json{{"effective_values", values}}; }

Json apply(const Json& values, std::string_view model, std::optional<std::string> effort,
           Json payload = Json{{"keep", true}}) {
  const auto recipe = must(chat::reasoning_recipe(exact(values)));
  chat::apply_reasoning_recipe(payload, model, effort, recipe);
  return payload;
}

struct RuntimeFixture {
  fsutil::TempDir directory;
  std::shared_ptr<net::ScriptedTransport> transport = std::make_shared<net::ScriptedTransport>();
  std::unique_ptr<Runtime> runtime;
  RuntimeFixture() {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    options.http = transport;
    runtime = must(Runtime::open(options));
    runtime->config().set("base_url", "https://reasoning-profile.fixture.test");
    runtime->config().set("default_model", "vendor/ODDCORE-BudgetX");
    runtime->config().set("context_execution", Json::object());
    runtime->config().set("semantic_analysis", false);
    runtime->secrets().set("api_key", "offline-synthetic-token");
  }
  void answer() {
    transport->expect("POST", "https://reasoning-profile.fixture.test/chat/completions",
        net::ScriptedTransport::Reply::json(200, Json{{"choices", Json::array({
            Json{{"message", Json{{"content", "Offline fixture answer"}}}}})}}));
  }
  ChatOptions options() const {
    ChatOptions options;
    options.stream = false;
    options.reasoning_effort = "turbo";
    options.trace_context = true;
    return options;
  }
};

void operations(Checks& checks) {
  const auto values = custom_values();
  const auto recipe = must(chat::reasoning_recipe(exact(values)));
  const auto inspection = recipe.snapshot.inspection();
  checks.equal(recipe.snapshot.values, values, "exact custom values have no preset merge");
  checks.equal(inspection.at("values"), values, "inspection contains exactly consumed values");
  checks.equal(inspection.at("hash"), recipe.snapshot.hash, "inspection identifies immutable snapshot");
  checks.check(!inspection.at("is_builtin").get<bool>(), "custom snapshot identified as nonbuiltin");
#if __has_include("loom/runtime_profile.h")
  checks.equal(inspection.at("validation_basis"), "runtime_profile_value_schema", "framework schema validation reported when linked capability exists");
#else
  checks.equal(inspection.at("validation_basis"), "native_consumed_fields", "native validation is explicit without framework schema capability");
#endif
  checks.equal(inspection.at("domain"), "chat_reasoning", "inspection names reasoning domain");
  checks.equal(apply(values, "vendor/ODDCORE-BudgetX", "turbo"),
      Json{{"keep", true}, {"reasoning", Json{{"enabled", true}, {"max_tokens", 500001}}}},
      "arbitrary model marker and custom budget above former 30000 preset");
  checks.equal(apply(values, "vendor/oddcore-BudgetX", ""),
      Json{{"keep", true}, {"reasoning", Json{{"enabled", true}, {"max_tokens", 700002}}}},
      "empty effort is an explicit configurable lookup key");
  checks.equal(apply(values, "vendor/oddcore-BudgetX", "LOW"),
      Json{{"keep", true}, {"reasoning", Json{{"enabled", true}, {"max_tokens", 800003}}}},
      "effort labels remain case-sensitive caller data");
  for (const auto& effort : {std::string("unknown"), std::string("low"), std::string("max"), std::string("adaptive")}) {
    checks.equal(apply(values, "vendor/oddcore-BudgetX", effort),
        Json{{"keep", true}, {"reasoning", Json{{"enabled", true}, {"max_tokens", 900004}}}},
        "caller unknown budget replaces historical effort behavior: " + effort);
  }
  checks.equal(apply(values, "vendor/oddcore-BudgetX", "automatic"),
      Json{{"keep", true}, {"reasoning", Json{{"enabled", true}}}}, "caller adaptive label suppresses token budget");
  checks.equal(apply(values, "vendor/oddcore-BudgetX", std::nullopt),
      Json{{"keep", true}, {"reasoning", Json{{"enabled", true}}}}, "null effort does not invent a budget");
  checks.equal(apply(values, "vendor/oddcore-BudgetX", "verbose"),
      Json{{"keep", true}, {"verbosity", "caller-custom-verbosity"}, {"reasoning", Json{{"enabled", true}}}},
      "caller verbosity label and value replace legacy max label");
  checks.equal(apply(values, "vendor/oddcore-budgetx", "turbo"),
      Json{{"keep", true}, {"reasoning", Json{{"enabled", true}, {"effort", "turbo"}}}},
      "budget marker matching retains native case-sensitive semantics");

  const Json populated{{"keep", true}, {"reasoning", Json{{"enabled", false}, {"caller_owned", true}}},
      {"verbosity", "caller-existing"}};
  checks.equal(apply(values, "claude-sonnet-4.6", "high", populated), populated,
      "custom marker list never silently adds built-in model names");
  auto disabled = values;
  disabled["thinking_indicators"] = Json::array();
  checks.equal(apply(disabled, "vendor/oddcore-BudgetX", "turbo", populated), populated,
      "empty thinking list disables reasoning and preserves existing payload");
  auto raw_effort = values;
  raw_effort["budget_model_markers"] = Json::array();
  checks.equal(apply(raw_effort, "vendor/oddcore-BudgetX", "turbo"),
      Json{{"keep", true}, {"reasoning", Json{{"enabled", true}, {"effort", "turbo"}}}},
      "empty budget marker list disables budget recipe without disabling reasoning");
  auto zero = values;
  zero["budget_by_effort"]["turbo"] = 0;
  checks.equal(apply(zero, "vendor/oddcore-BudgetX", "turbo").at("reasoning").at("max_tokens"), 0,
      "zero token budget is preserved rather than replaced by default");
  auto marker_all = values;
  marker_all["thinking_indicators"] = Json::array({""});
  checks.equal(apply(marker_all, "", "turbo").at("reasoning").at("effort"), "turbo",
      "empty substring is caller-authorized match-all data");

  for (const char* field : {"thinking_indicators", "budget_model_markers", "budget_by_effort", "unknown_effort_budget",
                            "adaptive_effort", "verbosity_effort", "verbosity_value"}) {
    auto missing = values;
    missing.erase(field);
    const auto rejected = chat::reasoning_recipe(exact(missing));
    checks.check(!rejected && rejected.error().code == Errc::InvalidArgument,
        "missing consumed field fails instead of resurrecting preset: " + std::string(field));
  }
  auto malformed = values;
  malformed["budget_by_effort"]["turbo"] = -1;
  const auto rejected = chat::reasoning_recipe(exact(malformed));
  checks.check(!rejected && rejected.error().code == Errc::InvalidArgument, "negative budget fails explicit consumed-field validation");
  const auto unavailable = chat::reasoning_recipe(Json{{"layer_snapshot", Json::object()}});
  checks.check(!unavailable, "invalid or unavailable layer snapshot cannot silently use builtin values");
}

void send_snapshot(Checks& checks) {
  RuntimeFixture fixture;
  const auto first_values = custom_values();
  auto second_values = first_values;
  second_values["budget_by_effort"]["turbo"] = 600002;
  const auto first = must(chat::reasoning_recipe(exact(first_values)));
  const auto second = must(chat::reasoning_recipe(exact(second_values)));
  fixture.runtime->config().set("chat_reasoning", exact(first_values));
  fixture.answer();
  ChatCallbacks callbacks;
  callbacks.on_start = [&](std::string_view, std::string_view) {
    fixture.runtime->config().set("chat_reasoning", exact(second_values));
  };
  const auto result = must(fixture.runtime->chat().send("Synthetic immutable snapshot request", fixture.options(), callbacks));
  const auto requests = fixture.transport->requests();
  checks.equal(requests.size(), 1, "actual send makes exactly one fake transport attempt");
  const auto payload = must(json::parse(requests.at(0).body));
  checks.equal(payload.at("reasoning").at("max_tokens"), 500001, "actual request uses snapshot frozen before callback mutation");
  checks.equal(payload.at("model"), "vendor/ODDCORE-BudgetX", "actual request keeps caller custom model");
  const auto assistant = must(fixture.runtime->db().get_msg(result.assistant_message_id));
  if (!assistant) throw std::runtime_error("assistant fixture row missing");
  checks.equal(assistant->metadata.at("reasoning_profile"), first.snapshot.inspection(),
      "persisted assistant inspection identifies exact snapshot consumed by first request");
  checks.equal(assistant->text, "Offline fixture answer", "actual fake response persisted");
  fixture.answer();
  const auto next = must(fixture.runtime->chat().send("Synthetic changed profile request", fixture.options()));
  const auto next_payload = must(json::parse(fixture.transport->requests().at(1).body));
  checks.equal(next_payload.at("reasoning").at("max_tokens"), 600002, "later request consumes changed configuration");
  const auto next_assistant = must(fixture.runtime->db().get_msg(next.assistant_message_id));
  if (!next_assistant) throw std::runtime_error("second assistant fixture row missing");
  checks.equal(next_assistant->metadata.at("reasoning_profile"), second.snapshot.inspection(),
      "later assistant retains changed snapshot inspection");
  checks.check(first.snapshot.hash != second.snapshot.hash, "changed consumed budget changes inspection identity");
  checks.fake_http_requests += fixture.transport->requests().size();
}

void invalid_before_mutation(Checks& checks) {
  RuntimeFixture fixture;
  auto values = custom_values();
  values.erase("unknown_effort_budget");
  fixture.runtime->config().set("chat_reasoning", exact(values));
  const auto result = fixture.runtime->chat().send("Synthetic rejected profile", fixture.options());
  checks.check(!result && result.error().code == Errc::InvalidArgument, "actual send rejects partial exact values");
  checks.check(fixture.transport->requests().empty(), "invalid profile dispatches no transport request");
  checks.check(!fixture.runtime->chat().current_conv(), "invalid profile is rejected before conversation mutation");
  checks.fake_http_requests += fixture.transport->requests().size();
}
}  // namespace

int main() {
  Checks checks;
  try {
    operations(checks);
    send_snapshot(checks);
    invalid_before_mutation(checks);
  } catch (const std::exception& error) {
    checks.check(false, "fixture/API setup completed", Json{{"error", error.what()}});
  }
  std::cout << loom::json::canonical(loom::Json{{"schema", "loom.chat_reasoning_profile_verification/1"},
      {"checks_count", checks.rows.size()}, {"passed_checks", checks.rows.size() - checks.failures},
      {"failed_checks", checks.failures}, {"fake_http_requests", checks.fake_http_requests},
      {"external_provider_calls", 0}, {"paid_provider_calls", 0}, {"checks", checks.rows}}) << '\n';
  return checks.failures || !std::cout ? 1 : 0;
}
#endif
