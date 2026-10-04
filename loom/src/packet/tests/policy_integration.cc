// Standalone integration check against thread 2, compiled after its objects
// and packet-enabled core are available. Synthetic local calls only.
#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "loom/usage_policy.h"
#include "loom/util/json.h"
using namespace loom;
namespace {
void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
Json take(const char* bytes) {
  check(bytes != nullptr, "null C ABI result");
  auto value = json::parse(bytes);
  loom_free_string(bytes);
  check(bool(value), "invalid C ABI JSON");
  return *value;
}
Json call(LoomContext* ctx, Json command) {
  auto raw = json::dump(command);
  return take(loom_packet(ctx, raw.c_str()));
}
}  // namespace
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const auto root = std::filesystem::path(argv[1]);
  Json options = {{"data_dir", root.string()}, {"start_workers", false}};
  auto raw = json::dump(options);
  const char* error = nullptr;
  auto* ctx = loom_init_ex(raw.c_str(), &error);
  check(ctx != nullptr, "runtime init failed");
  // Thread 2 adds this preset to Config defaults; set it for the isolated link check.
  loom_set_config(ctx, "loom_usage_policy", "{}");
  try {
    auto make = [](const char* id, int calls) {
      return Json{{"operation", "capabilities"},
                  {"usage_estimate", Json{{"operation_id", id},
                                          {"baseline_key", "packet-integration"},
                                          {"resources", Json{{"calls", calls}, {"money_usd", 0}}}}}};
    };
    auto first = call(ctx, make("one", 1));

    check(first["executed"] == true, "initial execution missing");
    check(first["usage_settlement"]["status"] == "completed", "initial accounting missing");
    auto larger = make("ten", 10);
    auto held = call(ctx, larger);
    check(held["executed"] == false, "10x call executed without confirmation");
    check(held["usage_decision"]["status"] == "requires_confirmation", "10x decision missing");
    auto policy = UsagePolicy::open(root / "usage-policy.sqlite");
    check(bool(policy), "policy unavailable");
    auto receipt = held["usage_decision"]["receipt_id"].get<std::string>();
    auto confirm = (*policy)->confirm("ten", receipt, true, "synthetic-owner-confirmation");
    check(bool(confirm), "confirmation failed");
    auto resumed = call(ctx, larger);
    check(resumed["executed"] == true, "confirmed request not resumed");
    check(resumed["usage_settlement"]["status"] == "completed", "confirmed accounting missing");
    auto changed = make("ten", 10);
    changed["resource_limits"] = Json::object();
    auto conflict = call(ctx, changed);
    check(conflict.contains("error"), "reused operation ID changed its command");
    auto failed = make("invalid", 1);
    failed["operation"] = "not-a-packet-operation";
    auto rejected = call(ctx, failed);
    check(rejected.contains("error"), "invalid packet operation unexpectedly succeeded");
    check(rejected["usage_settlement"]["status"] == "cancelled",
          "failed operation reservation not cancelled");
    auto unknown = make("unknown", 1);
    unknown["usage_estimate"]["resources"]["gpu_seconds"] = nullptr;
    auto unresolved = call(ctx, unknown);
    check(unresolved["usage_settlement"]["status"] == "unresolved", "unknown resource fabricated as zero");
    std::cout << "policy integration: 6/6 scenarios passed (initial, 10x hold, confirmed resume, binding "
                 "conflict, failure cancel, unknown quantity)\n";
    loom_shutdown(ctx);
    return 0;
  } catch (...) {
    loom_shutdown(ctx);
    throw;
  }
}
