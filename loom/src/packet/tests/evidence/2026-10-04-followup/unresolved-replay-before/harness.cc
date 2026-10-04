#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "loom/usage_policy.h"
#include "loom/util/json.h"

using namespace loom;

Json take(const char* raw) {
  if (!raw) throw std::runtime_error("null C ABI result");
  auto result = json::parse(raw);
  loom_free_string(raw);
  if (!result) throw std::runtime_error("invalid C ABI JSON");
  return *result;
}

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const std::filesystem::path root = argv[1];
  Json options{{"data_dir", root.string()}, {"start_workers", false}};
  auto options_json = json::dump(options);
  const char* error = nullptr;
  auto* context = loom_init_ex(options_json.c_str(), &error);
  if (!context) {
    std::cerr << (error ? error : "runtime initialization failed") << '\n';
    if (error) loom_free_string(error);
    return 3;
  }
  // The isolated static library is W4; install W2's default Config preset
  // explicitly, exactly as W4's checked-in policy integration harness does.
  loom_set_config(context, "loom_usage_policy", "{}");
  try {
    Json request{{"operation", "capabilities"},
                 {"usage_estimate", Json{{"operation_id", "synthetic-unresolved-replay"},
                                         {"baseline_key", "synthetic-packet-capabilities"},
                                         {"resources", Json{{"calls", 1}, {"gpu_seconds", nullptr}}}}}};
    const auto command = json::dump(request);
    auto first = take(loom_packet(context, command.c_str()));
    auto second = take(loom_packet(context, command.c_str()));
    auto policy = UsagePolicy::open(root / "usage-policy.sqlite");
    require(bool(policy), "policy inspection could not open ledger");
    auto state = (*policy)->inspect("synthetic-packet-capabilities");
    require(bool(state), "policy inspection failed");
    std::cout << json::dump(Json{{"request", request}, {"first", first},
                                {"second", second}, {"ledger", *state}}) << '\n';
    require(first.contains("executed") && first["executed"] == true, "first execution missing");
    require(first["usage_settlement"]["status"] == "unresolved", "first settlement not unresolved");
    require(second.contains("executed") && second["executed"] == true, "second execution blocked: bug not reproduced");
    require(second["usage_settlement"]["status"] == "unresolved", "second settlement not unresolved");
    require((*state)["baseline"]["calls"]["samples"] == 1, "calls measurement recorded more than once");
    require((*state)["operations"][0]["actual"]["calls"]["value"] == 1, "recorded calls changed");
    std::cerr << "REPRODUCED: identical unresolved request executed twice, ledger records calls=1 and one sample\n";
    loom_shutdown(context);
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n';
    loom_shutdown(context);
    return 1;
  }
}
