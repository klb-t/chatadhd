// Standalone thread-2 contract regression tests. Deliberately .cc: the kernel
// source glob must not pull a test main() into libloom_core.
#include "loom/usage_policy.h"
#include "loom/config.h"
#include "loom/sqlite.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"

#include <algorithm>
#include <barrier>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using loom::Errc;
using loom::Json;
using loom::UsagePolicy;

void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void near(double actual, double expected, const std::string& message) {
  check(std::abs(actual - expected) <= 1e-10 * std::max(1.0, std::abs(expected)),
        message + ": expected " + std::to_string(expected) + ", got " +
            std::to_string(actual));
}

template <class T>
T take(loom::Result<T> result, const std::string& where) {
  if (!result) throw std::runtime_error(where + ": " + result.error().to_string());
  return std::move(result).value();
}

template <class T>
void error(loom::Result<T> result, Errc expected, const std::string& where) {
  check(!result, where + ": expected error");
  check(result.error().code == expected, where + ": unexpected error " +
                                            result.error().to_string());
}

struct TemporaryDirectory {
  std::filesystem::path path;
  TemporaryDirectory() {
    auto root = std::filesystem::temp_directory_path();
    for (unsigned i = 0; i < 10000; ++i) {
      path = root / ("loom-usage-policy-contract-" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this)) + "-" +
                     std::to_string(i));
      std::error_code problem;
      if (std::filesystem::create_directory(path, problem)) return;
    }
    throw std::runtime_error("cannot allocate temporary directory");
  }
  ~TemporaryDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

struct Fixture {
  TemporaryDirectory directory;
  Json options;
  std::unique_ptr<UsagePolicy> policy;
  explicit Fixture(Json configured = loom::usage_policy_defaults())
      : options(std::move(configured)),
        policy(take(UsagePolicy::open(directory.path / "policy.sqlite", options), "open")) {}
  std::unique_ptr<UsagePolicy> another(const Json* configured = nullptr) {
    return take(UsagePolicy::open(directory.path / "policy.sqlite",
                                  configured ? *configured : options), "open another");
  }
  void reopen() {
    policy.reset();
    policy = another();
  }
  Json inspect(std::string cohort = "contract") {
    return take(policy->inspect(cohort), "inspect");
  }
};

Json estimate(std::string id, Json resources, std::string cohort = "contract") {
  return Json{{"operation_id", std::move(id)}, {"baseline_key", std::move(cohort)},
              {"resources", std::move(resources)},
              {"source_ref", "offline-contract-fixture"}};
}

Json actual(Json resources, std::string provenance = "instrument_measured") {
  return Json{{"resources", std::move(resources)}, {"provenance", std::move(provenance)},
              {"source_ref", "scripted-instrument-not-real-resource-measurement"}};
}

Json request(Fixture& f, const std::string& id, Json resources) {
  return take(f.policy->request(estimate(id, std::move(resources))), "request " + id);
}

void seed(Fixture& f, const std::string& id, Json resources,
          std::string provenance = "instrument_measured") {
  auto decision = request(f, id, resources);
  check(decision.at("status") == "allowed", "seed admission must be allowed");
  check(decision.at("authorized") == true, "seed must be authorized");
  auto completed = take(f.policy->complete(id, actual(resources, std::move(provenance))),
                        "seed complete");
  check(completed.at("status") == "completed", "seed must complete");
}

double baseline_value(Fixture& f, const std::string& resource) {
  return f.inspect().at("baseline").at(resource).at("value").get<double>();
}

Json owned_json(const char* value, const std::string& where) {
  check(value != nullptr, where + ": null C API response");
  auto parsed = loom::json::parse(value);
  loom_free_string(value);
  return take(std::move(parsed), where);
}

struct CApiFixture {
  TemporaryDirectory directory;
  LoomContext* context = nullptr;
  unsigned http_calls = 0;
  static int deny_http(const char*, LoomHttpResponse* response, void* user_data) {
    ++static_cast<CApiFixture*>(user_data)->http_calls;
    return loom_http_response_fail(response, LOOM_E_NETWORK,
                                  "offline policy fixture prohibits HTTP");
  }
  CApiFixture() { open(); }
  void open() {
    const char* problem = nullptr;
    auto options = Json{{"data_dir", directory.path.string()}, {"start_workers", false}}.dump();
    context = loom_init_ex(options.c_str(), &problem);
    std::string failure = problem ? problem : "no details";
    loom_free_string(problem);
    check(context != nullptr, "C API init: " + failure);
    check(loom_set_http_transport(context, deny_http, this) == LOOM_OK,
          "install offline HTTP sentinel");
  }
  ~CApiFixture() { loom_shutdown(context); }
  void reopen() {
    loom_shutdown(context);
    context = nullptr;
    open();
  }
  Json command(const Json& request) {
    auto encoded = request.dump();
    return owned_json(loom_usage_policy_json(context, encoded.c_str()), "policy C API");
  }
  Json config() { return owned_json(loom_get_config(context), "config C API"); }
  int patch(const Json& request) {
    auto encoded = request.dump();
    return loom_set_config_json(context, encoded.c_str());
  }
};

unsigned baseline_samples(Fixture& f, const std::string& resource) {
  return f.inspect().at("baseline").at(resource).at("samples").get<unsigned>();
}

void growth_boundary() {
  for (const auto& [amount, needs_confirmation] :
       std::vector<std::pair<double, bool>>{{99.99, false}, {100.0, true}, {100.01, true}}) {
    Fixture f;
    seed(f, "seed", {{"money_usd", 10.0}});
    auto decision = request(f, "boundary", {{"money_usd", amount}});
    check(decision.at("status") == (needs_confirmation ? "requires_confirmation" : "allowed"),
          "growth boundary status");
    check(decision.at("authorized") == !needs_confirmation, "growth boundary authorization");
    const auto& r = decision.at("resources").at("money_usd");
    near(r.at("baseline").get<double>(), 10.0, "growth baseline");
    near(r.at("ratio").get<double>(), amount / 10.0, "growth ratio");
    check(r.at("requires_confirmation") == needs_confirmation, "growth dimension reason");
    check(r.at("baseline_source") == "measured", "growth measured baseline");
    check(baseline_samples(f, "money_usd") == 1, "requests do not train baseline");
  }
}

void decimal_money_boundary_has_consistent_report_and_decision() {
  Fixture f;
  seed(f, "seed", {{"money_usd", 0.1}});
  auto decision = request(f, "decimal-tenfold", {{"money_usd", 1.0}});
  near(decision.at("resources").at("money_usd").at("ratio").get<double>(),
       10.0, "reported binary64 tenfold ratio");
  check(decision.at("status") == "requires_confirmation",
        "reported tenfold growth at decimal money boundary requires confirmation");
}

void multiple_dimensions_and_open_names() {
  Fixture f;
  seed(f, "seed", {{"money_usd", 10.0}, {"input_tokens", 20}, {"future:units", 3}});
  auto decision = request(f, "multi", {{"money_usd", 90.0}, {"input_tokens", 200},
                                        {"future:units", 12}});
  check(decision.at("status") == "requires_confirmation", "one dimension must gate");
  check(!decision.at("resources").at("money_usd").at("requires_confirmation").get<bool>(),
        "money dimension below threshold");
  check(decision.at("resources").at("input_tokens").at("requires_confirmation") == true,
        "token dimension on threshold");
  near(decision.at("resources").at("future:units").at("ratio").get<double>(),
       4.0, "extension resource dimension survives");
  check(f.inspect().at("reservations").value("money_usd", 0.0) == 0.0,
        "pending confirmation must not reserve");
}

void cold_start_zero_and_unknown() {
  Fixture f;
  auto first = request(f, "no-baseline", {{"money_usd", 1e9}});
  check(first.at("status") == "allowed", "missing baseline introduces no restriction");
  const auto& absent = first.at("resources").at("money_usd");
  check(absent.at("baseline").is_null(), "missing baseline is not zero");
  check(absent.at("baseline_status") == "unavailable", "missing baseline status");
  check(absent.at("baseline_source") == "unavailable", "missing baseline source");
  take(f.policy->cancel("no-baseline", "fixture cleanup"), "cancel cold start");
  seed(f, "zero", {{"money_usd", 0.0}});
  auto still_zero = request(f, "still-zero", {{"money_usd", 0.0}});
  check(still_zero.at("status") == "allowed", "zero to zero requires no confirmation");
  auto positive = request(f, "positive", {{"money_usd", 0.001}});
  check(positive.at("status") == "requires_confirmation", "zero to positive explicit growth");
  check(positive.at("resources").at("money_usd").at("baseline_status") == "zero",
        "zero baseline identified");

  Fixture unknown;
  auto undecidable = request(unknown, "unknown", {{"gpu_seconds", nullptr}});
  check(undecidable.at("resources").at("gpu_seconds").at("estimate").is_null(),
        "unknown estimate remains null");
  check(undecidable.at("resources").at("gpu_seconds").at("projected").is_null(),
        "unknown projection remains null");
  auto snapshot = unknown.inspect();
  check(snapshot.at("reservations").at("gpu_seconds").is_null(),
        "unknown reservation must survive admission");

  Fixture masked;
  seed(masked, "seed", {{"money_usd", 10.0}});
  request(masked, "unknown-existing", {{"money_usd", nullptr}});
  auto known_large = request(masked, "known-large", {{"money_usd", 100.0}});
  check(known_large.at("status") == "requires_confirmation",
        "unknown held estimate cannot conceal known tenfold increase");
  const auto& dimension = known_large.at("resources").at("money_usd");
  check(dimension.at("projected").is_null(), "unknown total remains explicitly unknown");
  near(dimension.at("projected_lower_bound").get<double>(), 100.0,
       "known projected lower bound still triggers growth gate");
  check(dimension.at("ratio").is_null(), "unknown full growth ratio is not fabricated");
}

void sliding_window_and_declared_seed() {
  auto options = loom::usage_policy_defaults();
  options["baseline_window"] = 2;
  Fixture f(options);
  seed(f, "four", {{"cpu_seconds", 4.0}});
  seed(f, "six", {{"cpu_seconds", 6.0}});
  seed(f, "eight", {{"cpu_seconds", 8.0}});
  near(baseline_value(f, "cpu_seconds"), 7.0, "sliding last two observations");
  check(baseline_samples(f, "cpu_seconds") == 2, "window sample count");
  f.reopen();
  near(baseline_value(f, "cpu_seconds"), 7.0, "sliding baseline restart");

  options = loom::usage_policy_defaults();
  options["baseline_window"] = nullptr;
  options["initial_baselines"] = Json{{"contract", {{"money_usd", 30.0}}}};
  Fixture declared(options);
  auto preview = take(declared.policy->preview(estimate("preview", {{"money_usd", 30.0}})),
                      "preview declared seed");
  check(preview.at("resources").at("money_usd").at("baseline_source") == "declared",
        "initial baseline identifies declaration");
  check(preview.at("resources").at("money_usd").at("baseline_samples") == 0,
        "initial declaration is not an observation");
  seed(declared, "real", {{"money_usd", 20.0}}, "provider_reported");
  near(baseline_value(declared, "money_usd"), 20.0, "measured samples supersede preset");
  check(declared.inspect().at("baseline").at("money_usd").at("source") == "measured",
        "provider report creates measured baseline");
}

void provenance_and_preview_are_not_training() {
  Fixture f;
  auto before = f.inspect();
  take(f.policy->preview(estimate("unrecorded", {{"money_usd", 3.0}})), "preview");
  check(f.inspect() == before, "preview must not write state");
  seed(f, "declared", {{"money_usd", 3.0}}, "declared");
  check(!f.inspect().at("baseline").contains("money_usd") ||
            f.inspect().at("baseline").at("money_usd").at("value").is_null(),
        "declared result cannot train measured baseline");
  seed(f, "measured", {{"money_usd", 4.0}}, "instrument_measured");
  seed(f, "reported", {{"money_usd", 8.0}}, "provider_reported");
  near(baseline_value(f, "money_usd"), 6.0, "only two measured observations");
  check(baseline_samples(f, "money_usd") == 2, "declared quantity excluded");
  request(f, "pending", {{"money_usd", 60.0}});
  check(baseline_samples(f, "money_usd") == 2, "pending request excluded");
  request(f, "invalid-provenance", {{"money_usd", 1.0}});
  error(f.policy->complete("invalid-provenance", actual({{"money_usd", 1.0}}, "estimated")),
        Errc::InvalidArgument, "unsupported measurement provenance");
  check(baseline_samples(f, "money_usd") == 2, "invalid completion cannot train");
}

void request_idempotency_conflict_restart() {
  Fixture f;
  seed(f, "seed", {{"money_usd", 10.0}});
  Json original = estimate("idempotent", {{"money_usd", 20.0}});
  auto first = take(f.policy->request(original), "initial idempotent request");
  auto second = take(f.policy->request(original), "repeat idempotent request");
  check(first == second, "same admitted request must replay receipt");
  near(f.inspect().at("reservations").at("money_usd").get<double>(),
       20.0, "duplicate request does not double reserve");
  error(f.policy->request(estimate("idempotent", {{"money_usd", 21.0}})),
        Errc::Conflict, "changed estimate conflicts");
  auto mutated_extension = original;
  mutated_extension["source_ref"] = "different-source";
  error(f.policy->request(mutated_extension), Errc::Conflict, "changed source conflicts");
  f.reopen();
  check(take(f.policy->request(original), "restart idempotent request") == first,
        "restart retains replay identity");
  near(f.inspect().at("reservations").at("money_usd").get<double>(),
       20.0, "restart retains held capacity");
  auto completed = take(f.policy->complete("idempotent", actual({{"money_usd", 15.0}})),
                        "complete idempotent");
  check(completed.at("status") == "completed", "completion state");
  auto sample_count = baseline_samples(f, "money_usd");
  take(f.policy->complete("idempotent", actual({{"money_usd", 15.0}})), "repeat complete");
  check(baseline_samples(f, "money_usd") == sample_count, "repeated complete trains once");
  error(f.policy->complete("idempotent", actual({{"money_usd", 16.0}})),
        Errc::Conflict, "conflicting known actual refused");
}

void two_instances_reservations_and_receipt_drift() {
  Fixture f;
  seed(f, "seed", {{"money_usd", 10.0}});
  auto other = f.another();
  auto large = request(f, "large", {{"money_usd", 100.0}});
  check(large.at("status") == "requires_confirmation", "large request pending");
  take(other->request(estimate("parallel", {{"money_usd", 40.0}})), "other reservation");
  near(f.inspect().at("reservations").at("money_usd").get<double>(),
       40.0, "other instance reservation visible");
  error(f.policy->confirm("large", large.at("receipt_id").get<std::string>(), true,
                          "fixture-owner-confirmation"),
        Errc::Conflict, "changed projection invalidates confirmation");
  auto refreshed = request(f, "large", {{"money_usd", 100.0}});
  check(refreshed.at("receipt_id") != large.at("receipt_id"), "drift refreshes pending receipt");
  near(refreshed.at("resources").at("money_usd").at("projected").get<double>(),
       140.0, "refreshed projected load");
  auto confirmed = take(f.policy->confirm("large", refreshed.at("receipt_id").get<std::string>(),
                                          true, "fixture-owner-confirmation"), "confirm refreshed");
  check(confirmed.at("authorized") == true, "bound confirmation authorizes");
  near(f.inspect().at("reservations").at("money_usd").get<double>(),
       140.0, "confirmed admission reserves atomically");

  Fixture exact;
  seed(exact, "seed", {{"money_usd", 10.0}});
  auto exact_other = exact.another();
  request(exact, "first", {{"money_usd", 40.0}});
  auto boundary = take(exact_other->request(estimate("second", {{"money_usd", 60.0}})),
                       "cross-instance threshold");
  check(boundary.at("status") == "requires_confirmation", "aggregate reserved boundary gates");
  near(boundary.at("resources").at("money_usd").at("reserved").get<double>(),
       40.0, "cross-instance held quantity");
}

void simultaneous_instances_serialize_admission() {
  Fixture f;
  seed(f, "seed", {{"money_usd", 10.0}});
  auto other = f.another();
  std::barrier start(3);
  Json first;
  Json second;
  std::exception_ptr first_error;
  std::exception_ptr second_error;
  std::thread a([&] {
    start.arrive_and_wait();
    try {
      first = take(f.policy->request(estimate("thread-one", {{"money_usd", 60.0}})),
                   "thread-one admission");
    } catch (...) {
      first_error = std::current_exception();
    }
  });
  std::thread b([&] {
    start.arrive_and_wait();
    try {
      second = take(other->request(estimate("thread-two", {{"money_usd", 60.0}})),
                    "thread-two admission");
    } catch (...) {
      second_error = std::current_exception();
    }
  });
  start.arrive_and_wait();
  a.join();
  b.join();
  if (first_error) std::rethrow_exception(first_error);
  if (second_error) std::rethrow_exception(second_error);
  unsigned allowed = (first.at("status") == "allowed" ? 1 : 0) +
                     (second.at("status") == "allowed" ? 1 : 0);
  unsigned pending = (first.at("status") == "requires_confirmation" ? 1 : 0) +
                     (second.at("status") == "requires_confirmation" ? 1 : 0);
  check(allowed == 1 && pending == 1, "atomic admissions serialize 60 + 60 against baseline 10");
  near(f.inspect().at("reservations").at("money_usd").get<double>(),
       60.0, "only one concurrent admission reserves");
}

void confirmation_binding_denial_and_restart() {
  Fixture f;
  seed(f, "seed", {{"money_usd", 10.0}});
  auto pending = request(f, "approve", {{"money_usd", 100.0}});
  std::string receipt = pending.at("receipt_id").get<std::string>();
  error(f.policy->confirm("approve", "wrong-receipt", true, "fixture-owner"),
        Errc::Conflict, "wrong receipt refused");
  f.reopen();
  auto confirmed = take(f.policy->confirm("approve", receipt, true, "fixture-owner"),
                        "confirm after restart");
  check(confirmed.at("authorized") == true, "restart confirmation authorizes");
  f.reopen();
  near(f.inspect().at("reservations").at("money_usd").get<double>(),
       100.0, "confirmation reservation survives restart");
  auto denied = request(f, "deny", {{"money_usd", 100.0}});
  auto rejection = take(f.policy->confirm("deny", denied.at("receipt_id").get<std::string>(),
                                          false, "fixture-owner"), "deny");
  check(rejection.at("status") == "denied", "denied lifecycle state");
  check(rejection.at("authorized") == false, "denial cannot authorize");
  near(f.inspect().at("reservations").at("money_usd").get<double>(),
       100.0, "denial adds no reservation");

  Fixture changed_options;
  seed(changed_options, "seed", {{"money_usd", 10.0}});
  auto old = request(changed_options, "options", {{"money_usd", 100.0}});
  Json changed = changed_options.options;
  changed["baseline_window"] = 1;
  auto new_policy = changed_options.another(&changed);
  error(new_policy->confirm("options", old.at("receipt_id").get<std::string>(), true,
                            "fixture-owner"),
        Errc::Conflict, "changed options invalidate receipt");
  auto fresh = take(new_policy->request(estimate("options", {{"money_usd", 100.0}})),
                    "refresh changed options");
  check(fresh.at("receipt_id") != old.at("receipt_id"), "options refresh binds new receipt");
}

void partial_completion_preserves_unknown_without_duplicate_training() {
  Fixture f;
  seed(f, "seed", {{"money_usd", 1.0}, {"cpu_seconds", 1.0}});
  request(f, "partial", {{"money_usd", 4.0}, {"cpu_seconds", 2.0}});
  Json partial = actual({{"money_usd", 5.0}, {"cpu_seconds", nullptr}});
  auto unresolved = take(f.policy->complete("partial", partial), "partial complete");
  check(unresolved.at("status") == "unresolved", "null resource keeps unresolved state");
  near(f.inspect().at("reservations").at("cpu_seconds").get<double>(),
       2.0, "unknown actual retains original reservation");
  near(f.inspect().at("reservations").value("money_usd", 0.0),
       0.0, "known actual releases its dimension reservation");
  check(baseline_samples(f, "money_usd") == 2, "known dimension trained once");
  check(baseline_samples(f, "cpu_seconds") == 1, "unknown dimension not trained");
  take(f.policy->complete("partial", partial), "repeat partial complete");
  check(baseline_samples(f, "money_usd") == 2, "partial retry cannot duplicate observation");
  f.reopen();
  auto resolved = take(f.policy->complete("partial", actual({{"cpu_seconds", 3.0}},
                                                            "provider_reported")),
                       "resolve remaining dimension");
  check(resolved.at("status") == "completed", "later known quantity resolves");
  check(baseline_samples(f, "money_usd") == 2, "resolution preserves earlier observation");
  check(baseline_samples(f, "cpu_seconds") == 2, "resolution trains once");
  near(baseline_value(f, "money_usd"), 3.0, "money measured mean");
  near(baseline_value(f, "cpu_seconds"), 2.0, "cpu measured mean");
  error(f.policy->complete("partial", actual({{"money_usd", 6.0}})),
        Errc::Conflict, "resolved quantity immutable");
}

void actual_overrun_is_recorded_and_cancellation_invents_no_measurement() {
  Fixture f;
  seed(f, "seed", {{"money_usd", 10.0}});
  auto admitted = request(f, "overrun", {{"money_usd", 20.0}});
  check(admitted.at("authorized") == true, "planned quantity admitted");
  auto completion = take(f.policy->complete("overrun", actual({{"money_usd", 200.0}})),
                         "record actual overrun");
  check(completion.at("status") == "completed", "overrun must remain recordable");
  near(baseline_value(f, "money_usd"), 105.0, "actual overrun enters measured baseline");
  request(f, "cancel", {{"money_usd", 10.0}});
  auto cancelled = take(f.policy->cancel("cancel", "caller cancelled before dispatch"), "cancel");
  check(cancelled.at("status") == "cancelled", "cancel state");
  check(cancelled.at("authorized") == false, "cancel is not authorization");
  check(baseline_samples(f, "money_usd") == 2, "cancel creates no fake zero observation");
  near(baseline_value(f, "money_usd"), 105.0, "cancel retains measured baseline");
}

void empty_resource_operations_complete_without_stale_authorization() {
  Fixture f;
  auto admitted = request(f, "metadata-only", Json::object());
  check(admitted.at("status") == "allowed", "resource-free operation is valid");
  auto completed = take(f.policy->complete("metadata-only", actual(Json::object())),
                        "resource-free complete");
  check(completed.at("status") == "completed", "empty completion finalizes empty reservation");
  check(completed.at("authorized") == false, "completed empty operation cannot remain authorized");
  auto replay = take(f.policy->request(estimate("metadata-only", Json::object())),
                     "resource-free replay");
  check(replay.at("status") == "completed", "empty terminal state replays");
  check(replay.at("authorized") == false, "empty replay cannot authorize new dispatch");
  check(f.inspect().at("baseline").empty(), "empty completion invents no measurement");
}

void empty_actual_keeps_nonempty_reservation_unresolved() {
  Fixture f;
  seed(f, "seed", {{"money_usd", 10.0}});
  request(f, "no-measurements", {{"money_usd", 2.0}});
  auto unresolved = take(f.policy->complete("no-measurements", actual(Json::object())),
                         "empty actual with outstanding reservation");
  check(unresolved.at("status") == "unresolved", "missing actual measurements are unresolved");
  near(f.inspect().at("reservations").at("money_usd").get<double>(),
       2.0, "empty actual retains reserved amount");
  check(baseline_samples(f, "money_usd") == 1, "missing observations train no baseline");
  f.reopen();
  near(f.inspect().at("reservations").at("money_usd").get<double>(),
       2.0, "unresolved missing measurements survive restart");
  auto resolved = take(f.policy->complete("no-measurements", actual({{"money_usd", 1.0}})),
                       "resolve absent actual measurement");
  check(resolved.at("status") == "completed", "later instrument measurement resolves absent quantity");
}

void finite_aggregate_overflow_rejects_without_mutation() {
  Fixture f;
  const double maximum = std::numeric_limits<double>::max();
  auto first = request(f, "held-max", {{"money_usd", maximum}});
  check(first.at("status") == "allowed", "largest finite resource can be represented");
  auto before = f.inspect();
  error(f.policy->request(estimate("would-overflow", {{"money_usd", maximum}})),
        Errc::InvalidArgument, "unrepresentable aggregate rejected");
  auto after = f.inspect();
  check(after == before, "aggregate overflow must not persist operation or event");
  check(after.at("operations").size() == 1, "original finite reservation survives overflow attempt");
}

void invalid_amounts_options_and_unbounded_presets() {
  for (auto amount : {-1.0, std::numeric_limits<double>::infinity(),
                      std::numeric_limits<double>::quiet_NaN()}) {
    Fixture f;
    error(f.policy->request(estimate("invalid", {{"money_usd", amount}})),
          Errc::InvalidArgument, "invalid estimate quantity");
    check(f.inspect().at("operations").empty(), "invalid estimate makes no operation");
  }
  for (auto window : {Json(0), Json(-1), Json(1.5)}) {
    auto options = loom::usage_policy_defaults();
    options["baseline_window"] = window;
    check(!loom::validate_usage_policy_options(options), "invalid baseline window refused");
  }
  for (auto timeout : {Json(-1), Json(std::uint64_t{2147483648}), Json(1.5)}) {
    auto options = loom::usage_policy_defaults();
    options["ledger_busy_timeout_ms"] = timeout;
    check(!loom::validate_usage_policy_options(options), "invalid SQLite timeout representation refused");
  }
  auto options = loom::usage_policy_defaults();
  options["baseline_window"] = nullptr;
  options["growth_factor"] = 1000.0;
  options["ledger_busy_timeout_ms"] = 0;
  options["custom_extension"] = Json{{"caller", "preserved"}};
  check(static_cast<bool>(loom::validate_usage_policy_options(options)),
        "large caller policy and extension allowed");
  Fixture f(options);
  seed(f, "seed", {{"money_usd", 1e9}});
  auto large = request(f, "large", {{"money_usd", 1e11}});
  check(large.at("status") == "allowed", "caller preset can allow arbitrary large amount");
  auto returned_options = f.inspect().at("options");
  check(returned_options.at("custom_extension") == options.at("custom_extension"),
        "unknown policy extension preserved");

  options = loom::usage_policy_defaults();
  options["include_reservations"] = false;
  Fixture individual(options);
  seed(individual, "seed", {{"money_usd", 10.0}});
  request(individual, "first", {{"money_usd", 90.0}});
  auto another = request(individual, "second", {{"money_usd", 90.0}});
  check(another.at("status") == "allowed", "caller can choose individual growth accounting");
}

void c_api_settings_preset_override_and_atomic_patch() {
  CApiFixture f;
  auto initial = f.command({{"action", "settings"}});
  check(!initial.contains("error"), "settings available through static C dispatcher");
  check(initial.at("source") == "preset", "fresh settings identify preset");
  check(initial.at("stored_override").is_null(), "fresh preset is not persisted user override");
  check(initial.at("effective") == initial.at("preset"), "fresh effective policy equals preset");
  check(initial.at("capabilities").at("resource_names") == "open", "open resources capability");
  check(initial.at("capabilities").at("number_representation") == "finite_binary64",
        "number capability explicit");

  auto before = f.config();
  Json invalid{{"theme", "must-not-be-written"},
               {"loom_usage_policy", {{"growth_factor", -1}}}};
  check(f.patch(invalid) == LOOM_E_INVALID_ARGUMENT, "invalid policy patch rejected");
  check(f.config() == before, "invalid mixed patch cannot mutate any other key");
  loom_set_config(f.context, "loom_usage_policy", R"({"baseline_window":0})");
  check(f.config() == before, "invalid scalar setter leaves config unchanged");
  for (auto timeout : {Json(-1), Json(std::uint64_t{2147483648})}) {
    auto invalid_timeout = Json{{"ledger_busy_timeout_ms", timeout}}.dump();
    loom_set_config(f.context, "loom_usage_policy", invalid_timeout.c_str());
    check(f.config() == before, "invalid scalar timeout setter leaves config unchanged");
  }

  Json override{{"growth_factor", 1000.0}, {"baseline_window", nullptr},
                {"ledger_busy_timeout_ms", 0},
                {"initial_baselines", {{"future/cohort", {{"future:resource", 1e9}}}}},
                {"caller_extension", {{"label", "retained"}}}};
  check(f.patch(Json{{"loom_usage_policy", override}, {"max_tokens", 1000000}}) == LOOM_OK,
        "large resource settings accepted without invented caps");
  auto configured = f.command({{"action", "settings"}});
  check(configured.at("source") == "configured", "override identifies configured source");
  check(configured.at("stored_override") == override, "stored override exact and open");
  check(configured.at("effective").at("include_reservations") == true,
        "omitted option inherits preset");
  check(configured.at("effective").at("growth_factor") == 1000.0,
        "caller can raise growth preset");
  check(configured.at("effective").at("caller_extension") == override.at("caller_extension"),
        "unknown setting extension survives effective merge");
  check(f.config().at("max_tokens") == 1000000, "large config value preserved");
  f.reopen();
  check(f.command({{"action", "settings"}}).at("stored_override") == override,
        "stored options survive full runtime restart");
  check(f.http_calls == 0, "settings checks remain entirely offline");
}

void settings_default_identity_and_canonical_hashes() {
  TemporaryDirectory directory;
  loom::Config config(directory.path / "config.json");
  const Json historical_preset{{"schema", "loom.usage_policy/1"},
                               {"growth_factor", 10.0},
                               {"baseline_window", 32},
                               {"ledger_busy_timeout_ms", 30000},
                               {"include_reservations", true},
                               {"initial_baselines", Json::object()}};
  auto settings = take(loom::usage_policy_settings(config), "C++ settings");
  check(settings.at("preset").size() == 6 && settings.at("preset") == historical_preset,
        "settings preserve exactly the six historical preset values");
  check(loom::usage_policy_defaults() == historical_preset,
        "historical default behavior remains identical");
  check(settings.at("effective") == historical_preset &&
            settings.at("stored_override").is_null() && settings.at("source") == "preset",
        "fresh C++ settings have no synthetic persistent override");
  check(settings.at("preset_source") == "legacy_code_pending_pack_migration",
        "preset source identifies pending data-pack migration");
  check(settings.at("override_semantics") == "replace_top_level_fields",
        "settings state replacement semantics");
  const auto& hashes = settings.at("hashes");
  const auto expected = loom::Sha256::hex(loom::json::canonical(historical_preset));
  check(hashes.at("algorithm") == "sha256" &&
            hashes.at("representation") == "loom.canonical_json",
        "settings identify canonical hash representation");
  check(hashes.at("preset") == expected && hashes.at("effective") == expected &&
            hashes.at("stored_override").is_null(),
        "settings hashes independently match canonical historical data");

  Json stored{{"growth_factor", 25.0},
              {"caller_extension", {{"z", 2}, {"a", Json{{"second", 4}, {"first", 3}}}}}};
  config.set("loom_usage_policy", stored);
  check(static_cast<bool>(config.save()), "save hash fixture override");
  const auto bytes = take(loom::fsutil::read_file(config.path()), "read hash fixture config");
  Json reordered{{"caller_extension", {{"a", Json{{"first", 3}, {"second", 4}}}, {"z", 2}}},
                 {"growth_factor", 25.0}};
  auto candidate = take(loom::usage_policy_settings(config, &reordered), "C++ reordered preview");
  check(candidate.at("hashes").at("stored_override") ==
            loom::Sha256::hex(loom::json::canonical(stored)),
        "stored override hash independently matches canonical data");
  check(candidate.at("preview").at("hashes").at("override") ==
            loom::Sha256::hex(loom::json::canonical(reordered)),
        "preview override hash independently matches canonical data");
  check(candidate.at("hashes").at("stored_override") ==
            candidate.at("preview").at("hashes").at("override"),
        "nested and top-level key order does not change canonical hash");
  check(candidate.at("preview").at("hashes").at("effective") ==
            loom::Sha256::hex(loom::json::canonical(candidate.at("preview").at("effective"))),
        "prospective effective hash independently matches canonical data");
  check(candidate.at("preview").at("effective_changed") == false,
        "equivalent key order does not report an effective change");
  check(candidate.at("preview").at("persisted") == false &&
            take(loom::fsutil::read_file(config.path()), "read unchanged hash fixture config") == bytes,
        "C++ settings preview does not persist its candidate");
  check(!std::filesystem::exists(directory.path / "usage-policy.sqlite"),
        "C++ settings do not create a ledger");
}

void c_api_settings_preview_reset_and_shallow_replacement() {
  CApiFixture f;
  Json stored{{"growth_factor", 40.0},
              {"initial_baselines", {{"old/cohort", {{"money_usd", 7.0}, {"old:units", 3}}}}},
              {"old_extension", {{"source", "saved"}}}};
  check(f.patch({{"loom_usage_policy", stored}}) == LOOM_OK, "save preview replacement fixture");
  const auto before = f.config();
  const auto bytes = take(loom::fsutil::read_file(f.directory.path / "config.json"),
                          "read preview replacement config");
  auto reset = f.command({{"action", "preview_settings"}, {"override", Json::object()}});
  check(!reset.contains("error"), "empty settings candidate is valid");
  check(reset.at("stored_override") == stored && reset.at("effective").at("growth_factor") == 40.0,
        "settings preview retains the active saved snapshot");
  check(reset.at("preview").at("override").empty() &&
            reset.at("preview").at("effective") == reset.at("preset"),
        "empty candidate prospectively resets all fields to the preset");
  check(reset.at("preview").at("effective_changed") == true &&
            reset.at("preview").at("persisted") == false,
        "prospective reset reports change without persistence");

  Json replacement{{"initial_baselines", {{"new/cohort", {{"future:units", 1e100}}}}}};
  auto shallow = f.command({{"action", "preview_settings"}, {"override", replacement}});
  check(!shallow.contains("error"), "shallow replacement candidate is valid");
  const auto& proposed = shallow.at("preview").at("effective");
  check(proposed.at("initial_baselines") == replacement.at("initial_baselines") &&
            !proposed.at("initial_baselines").contains("old/cohort"),
        "candidate replaces the complete initial_baselines top-level field");
  check(proposed.at("growth_factor") == 10.0 && !proposed.contains("old_extension"),
        "candidate is based on preset instead of merging the stored override");
  check(f.config() == before &&
            take(loom::fsutil::read_file(f.directory.path / "config.json"),
                 "read unchanged preview replacement config") == bytes,
        "reset and shallow preview leave both memory and config bytes unchanged");
  check(f.command({{"action", "settings"}}).at("stored_override") == stored,
        "empty preview does not delete the persistent override");
  check(!std::filesystem::exists(f.directory.path / "usage-policy.sqlite"),
        "settings previews do not create a ledger");
  check(f.http_calls == 0, "replacement previews remain entirely offline");
}

void c_api_invalid_settings_preview_is_read_only() {
  CApiFixture f;
  check(f.patch({{"loom_usage_policy", {{"growth_factor", 25.0}}}}) == LOOM_OK,
        "save invalid preview fixture");
  const auto before = f.config();
  const auto bytes = take(loom::fsutil::read_file(f.directory.path / "config.json"),
                          "read invalid preview config");
  std::vector<Json> invalid{
      Json{{"action", "preview_settings"}},
      Json{{"action", "preview_settings"}, {"override", nullptr}},
      Json{{"action", "preview_settings"}, {"override", Json::array()}},
      Json{{"action", "preview_settings"}, {"override", "not-an-object"}},
      Json{{"action", "preview_settings"}, {"override", {{"growth_factor", 1.0}}}},
      Json{{"action", "preview_settings"}, {"override", {{"baseline_window", 0}}}},
      Json{{"action", "preview_settings"},
           {"override", {{"initial_baselines", {{"cohort", {{"money_usd", -1}}}}}}}},
  };
  for (const auto& command : invalid) {
    auto refused = f.command(command);
    check(refused.contains("error") && refused.at("error").at("code") == "invalid_argument",
          "malformed settings preview returns an argument error");
    check(f.config() == before &&
              take(loom::fsutil::read_file(f.directory.path / "config.json"),
                   "read refused preview config") == bytes,
          "malformed settings preview cannot mutate any config value or file byte");
    check(!std::filesystem::exists(f.directory.path / "usage-policy.sqlite"),
          "malformed settings preview does not open a ledger");
  }
  check(f.http_calls == 0, "malformed previews remain entirely offline");
}

void c_api_settings_preview_matches_save_and_restart() {
  CApiFixture f;
  Json override{{"growth_factor", 1e100}, {"baseline_window", nullptr},
                {"ledger_busy_timeout_ms", 0}, {"include_reservations", false},
                {"initial_baselines", {{"future/cohort", {{"future:units", 1e200},
                                                            {"unknown:units", nullptr}}}}},
                {"future_extension", {{"huge_valid_value", 1e250},
                                       {"arbitrary_variants", Json::array({"caller-a", "caller-b"})}}}};
  const auto before = f.config();
  auto preview = f.command({{"action", "preview_settings"}, {"override", override}});
  check(!preview.contains("error"), "large open settings candidate is valid");
  check(preview.at("preview").at("override") == override &&
            preview.at("preview").at("effective").at("future_extension") == override.at("future_extension"),
        "arbitrary extension data and large finite values survive preview exactly");
  check(preview.at("preview").at("hashes").at("override") ==
            loom::Sha256::hex(loom::json::canonical(override)),
        "large extension candidate hash independently matches canonical data");
  check(f.config() == before && !std::filesystem::exists(f.directory.path / "usage-policy.sqlite"),
        "large settings candidate remains read-only");
  check(f.patch({{"loom_usage_policy", override}}) == LOOM_OK, "save the previewed candidate");
  auto saved = f.command({{"action", "settings"}});
  check(saved.at("stored_override") == override &&
            saved.at("effective") == preview.at("preview").at("effective"),
        "preview computes exactly the subsequently saved effective options");
  check(saved.at("hashes").at("effective") == preview.at("preview").at("hashes").at("effective") &&
            saved.at("hashes").at("stored_override") == preview.at("preview").at("hashes").at("override"),
        "save preserves both prospective canonical hashes");
  f.reopen();
  auto restarted = f.command({{"action", "settings"}});
  check(restarted.at("effective") == saved.at("effective") &&
            restarted.at("hashes") == saved.at("hashes"),
        "previewed effective options and hashes survive a fresh runtime");
  auto consumed = f.command({{"action", "inspect"}, {"baseline_key", "future/cohort"}});
  check(!consumed.contains("error") && consumed.at("options") == preview.at("preview").at("effective"),
        "fresh policy consumes exactly the previewed and saved effective options");
  check(f.http_calls == 0, "preview, save, restart and local consumption remain entirely offline");
}

void c_api_receipt_lifecycle_dispatcher() {
  CApiFixture f;
  Json options{{"initial_baselines", {{"contract", {{"money_usd", 10.0}}}}}};
  check(f.patch({{"loom_usage_policy", options}}) == LOOM_OK, "set initial baseline");
  Json operation = estimate("api-growth", {{"money_usd", 100.0}});
  auto preview = f.command({{"action", "preview"}, {"estimate", operation}});
  check(preview.at("status") == "requires_confirmation", "dispatcher preview returns normal receipt");
  check(f.command({{"action", "inspect"}, {"baseline_key", "contract"}})
            .at("operations").empty(), "dispatcher preview is read-only");
  auto pending = f.command({{"action", "request"}, {"estimate", operation}});
  check(!pending.contains("error"), "requires_confirmation is a value, not an error envelope");
  check(pending.at("status") == "requires_confirmation", "dispatcher growth gates");
  auto bad = f.command({{"action", "confirm"}, {"operation_id", "api-growth"},
                        {"receipt_id", "wrong"}, {"approved", true},
                        {"confirmation_ref", "fixture-owner"}});
  check(bad.at("error").at("code") == "conflict", "dispatcher stale receipt error envelope");
  auto approved = f.command({{"action", "confirm"}, {"operation_id", "api-growth"},
                             {"receipt_id", pending.at("receipt_id")}, {"approved", true},
                             {"confirmation_ref", "fixture-owner"}});
  check(approved.at("authorized") == true, "dispatcher confirmation authorizes");
  f.reopen();
  auto snapshot = f.command({{"action", "inspect"}, {"baseline_key", "contract"}});
  near(snapshot.at("reservations").at("money_usd").get<double>(),
       100.0, "C dispatcher reservation survives runtime restart");
  auto complete = f.command({{"action", "complete"}, {"operation_id", "api-growth"},
                             {"actual", actual({{"money_usd", 80.0}}, "provider_reported")}});
  check(complete.at("status") == "completed", "dispatcher completion state");
  check(complete.at("authorized") == false, "dispatcher completion revokes execution authorization");
  auto replay = f.command({{"action", "request"}, {"estimate", operation}});
  check(replay.at("status") == "completed" && replay.at("authorized") == false,
        "dispatcher terminal replay does not dispatch again");
  auto unknown_action = f.command({{"action", "unknown"}});
  check(unknown_action.at("error").at("code") == "invalid_argument", "unknown command rejected");
  check(f.http_calls == 0, "dispatcher lifecycle remains entirely offline");
}

void corrupt_record_returns_result_error_without_exception() {
  for (const auto& field : {"authorized", "remaining", "all"}) {
    Fixture f;
    seed(f, "seed", {{"money_usd", 10.0}});
    request(f, "held", {{"money_usd", 2.0}});
    auto external = take(loom::sql::Connection::open(f.directory.path / "policy.sqlite"),
                         "fixture corruption connection");
    auto raw = take(external.query_text("SELECT record FROM usage_operations WHERE id='held'"),
                    "read fixture record");
    check(raw.has_value(), "fixture record exists");
    auto malformed = take(loom::json::parse(*raw), "parse fixture record");
    if (std::string(field) == "all") malformed = Json::object();
    else malformed[field] = "invalid-shape";
    auto write = external.run("UPDATE usage_operations SET record=? WHERE id='held'", malformed.dump());
    check(static_cast<bool>(write), "inject fixture record corruption");
    auto assessed = f.policy->preview(estimate("new", {{"money_usd", 2.0}}));
    check(!assessed, "corrupt persisted record must fail closed");
    check(assessed.error().code == Errc::Parse || assessed.error().code == Errc::Database,
          "corrupt persisted record must return parse/database error");
    auto inspected = f.policy->inspect("contract");
    check(!inspected, "inspection must not return malformed persisted record as valid");
    auto repeated = f.policy->request(estimate("held", {{"money_usd", 2.0}}));
    check(!repeated && repeated.error().code == Errc::Parse,
          "replaying corrupt operation returns Parse error");
  }
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string, std::function<void()>>> cases = {
      {"growth boundary 9.999/10/10.001", growth_boundary},
      {"decimal money tenfold boundary", decimal_money_boundary_has_consistent_report_and_decision},
      {"multiple dimensions and open names", multiple_dimensions_and_open_names},
      {"cold start, zero and unknown", cold_start_zero_and_unknown},
      {"sliding window and declared seed", sliding_window_and_declared_seed},
      {"provenance and read-only preview", provenance_and_preview_are_not_training},
      {"idempotency, conflict and restart", request_idempotency_conflict_restart},
      {"two instances, reservation drift", two_instances_reservations_and_receipt_drift},
      {"simultaneous instances serialize admission", simultaneous_instances_serialize_admission},
      {"confirmation binding, denial, options", confirmation_binding_denial_and_restart},
      {"partial completion without duplicate training", partial_completion_preserves_unknown_without_duplicate_training},
      {"actual overrun and cancellation", actual_overrun_is_recorded_and_cancellation_invents_no_measurement},
      {"empty resource completion", empty_resource_operations_complete_without_stale_authorization},
      {"empty actual retains unresolved reservation", empty_actual_keeps_nonempty_reservation_unresolved},
      {"finite aggregate overflow rollback", finite_aggregate_overflow_rejects_without_mutation},
      {"invalid quantities and configurable presets", invalid_amounts_options_and_unbounded_presets},
      {"C API settings and atomic policy patch", c_api_settings_preset_override_and_atomic_patch},
      {"settings default identity and canonical hashes", settings_default_identity_and_canonical_hashes},
      {"C API settings reset and shallow replacement preview", c_api_settings_preview_reset_and_shallow_replacement},
      {"C API invalid settings preview is read-only", c_api_invalid_settings_preview_is_read_only},
      {"C API settings preview matches save and restart", c_api_settings_preview_matches_save_and_restart},
      {"C API receipt lifecycle dispatcher", c_api_receipt_lifecycle_dispatcher},
      {"corrupt record returns Result error", corrupt_record_returns_result_error_without_exception},
  };
  unsigned passed = 0;
  for (const auto& [name, run] : cases) {
    try {
      run();
      ++passed;
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception& problem) {
      std::cerr << "FAIL " << name << ": " << problem.what() << '\n';
    }
  }
  std::cout << "UsagePolicy contract: " << passed << "/" << cases.size() << " passed\n";
  return passed == cases.size() ? 0 : 1;
}
