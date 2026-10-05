// Standalone integration check against thread 2, compiled after its objects
// and packet-enabled core are available. Synthetic local calls only.
#include <barrier>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "loom/sqlite.h"
#include "loom/usage_policy.h"
#include "loom/util/json.h"
#include "loom/util/sha256.h"
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
Json seal(Json record) {
  record.erase("dispatch_sha256");
  record["dispatch_sha256"] = Sha256::hex(json::canonical(record));
  return record;
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
    auto receipts = sql::Connection::open(root / "usage-policy.sqlite");
    check(bool(receipts), "dispatch receipt database unavailable");
    auto held_claim = receipts->query_int(
        "SELECT COUNT(*) FROM loom_packet_dispatch_v1 WHERE operation_id='ten'");
    check(bool(held_claim) && *held_claim && **held_claim == 0, "unconfirmed operation claimed execution");
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

    // Neither completed nor unresolved IDs authorize a second dispatch.
    auto before = (*policy)->inspect("packet-integration");
    check(bool(before), "baseline inspection failed");
    auto completed_replay = call(ctx, make("one", 1));
    check(completed_replay["executed"] == false && completed_replay["replayed"] == true,
          "completed operation dispatched twice");
    check(completed_replay["result"] == first["result"], "completed result was not replayed");
    auto unresolved_replay = call(ctx, unknown);
    check(unresolved_replay["executed"] == false && unresolved_replay["replayed"] == true,
          "unresolved operation dispatched twice");
    check(unresolved_replay["result"] == unresolved["result"], "unresolved result changed on replay");
    auto after = (*policy)->inspect("packet-integration");
    check(bool(after) && *after == *before, "replay changed samples, events or reservations");
    check((*after)["reservations"]["gpu_seconds"].is_null(), "unknown reservation hidden by replay");

    auto failed_replay = call(ctx, failed);
    check(failed_replay.contains("error") && failed_replay["executed"] == false &&
              failed_replay["replayed"] == true,
          "failed operation was dispatched again");
    check(failed_replay["error"] == rejected["error"], "first failure was not preserved");

    // The unique SQL claim, rather than an in-process flag, arbitrates callers.
    auto concurrent = make("concurrent", 1);
    concurrent["usage_estimate"]["baseline_key"] = "packet-concurrent";
    concurrent["usage_estimate"]["resources"]["gpu_seconds"] = nullptr;
    std::barrier start(2);
    Json replies[2];
    std::exception_ptr failures[2];
    auto run = [&](int i) {
      start.arrive_and_wait();
      try { replies[i] = call(ctx, concurrent); }
      catch (...) { failures[i] = std::current_exception(); }
    };
    std::thread a(run, 0), b(run, 1);
    a.join(); b.join();
    for (const auto& failure : failures) if (failure) std::rethrow_exception(failure);
    check(replies[0].value("executed", false) + replies[1].value("executed", false) == 1,
          "concurrent duplicate ID dispatched twice or not at all");
    auto concurrent_replay = call(ctx, concurrent);
    check(concurrent_replay["executed"] == false && concurrent_replay["replayed"] == true,
          "concurrent operation not durably replayable");
    auto concurrent_state = (*policy)->inspect("packet-concurrent");
    check(bool(concurrent_state) && (*concurrent_state)["baseline"]["calls"]["samples"] == 1 &&
              (*concurrent_state)["baseline"]["calls"]["value"] == 1,
          "concurrent accounting does not have exactly one calls sample");
    check((*concurrent_state)["reservations"]["gpu_seconds"].is_null(),
          "concurrent replay erased unknown quantity");

    // Induce a real transactional settlement failure after actual execution.
    // The stored result/measurements survive and only accounting is retried.
    auto settlement_error = make("settlement-error", 1);
    settlement_error["usage_estimate"]["baseline_key"] = "packet-settlement-error";
    settlement_error["usage_estimate"]["resources"]["gpu_seconds"] = nullptr;
    settlement_error["usage_estimate"]["resources"]["wall_seconds"] = nullptr;
    check(bool(receipts->exec(
        "CREATE TRIGGER packet_test_settlement_failure BEFORE INSERT ON usage_samples "
        "WHEN NEW.operation_id='settlement-error' BEGIN "
        "SELECT RAISE(ABORT,'synthetic packet accounting failure'); END;")),
        "settlement failure setup failed");
    auto accounting_failed = call(ctx, settlement_error);
    check(accounting_failed["executed"] == true &&
              accounting_failed["usage_settlement"].contains("error"),
          "induced accounting failure was not exposed");
    auto ready_bytes = receipts->query_text(
        "SELECT record FROM loom_packet_dispatch_v1 WHERE operation_id='settlement-error'");
    check(bool(ready_bytes) && *ready_bytes, "original result lost after accounting failure");
    auto ready_record = json::parse(**ready_bytes);
    check(bool(ready_record) && (*ready_record)["status"] == "result_ready",
          "failed settlement did not leave a recoverable result");
    const auto measured_wall = (*ready_record)["actual"]["resources"]["wall_seconds"];
    check(bool(receipts->exec("DROP TRIGGER packet_test_settlement_failure")),
          "settlement failure teardown failed");
    auto accounting_recovered = call(ctx, settlement_error);
    check(accounting_recovered["executed"] == false && accounting_recovered["replayed"] == true &&
              accounting_recovered["result"] == accounting_failed["result"] &&
              accounting_recovered["usage_settlement"]["status"] == "unresolved",
          "failed accounting retried packet execution or lost the original result");
    check(accounting_recovered["usage_settlement"]["actual"]["wall_seconds"]["value"] == measured_wall,
          "accounting retry replaced original wall measurement");
    auto accounting_state = (*policy)->inspect("packet-settlement-error");
    check(bool(accounting_state) && (*accounting_state)["baseline"]["calls"]["samples"] == 1 &&
              (*accounting_state)["reservations"]["gpu_seconds"].is_null(),
          "failed accounting recovery duplicated calls or erased unknown reservation");

    // Pre-upgrade accounting can be unresolved without a dispatch receipt.
    auto legacy = make("legacy", 1);
    legacy["usage_estimate"]["baseline_key"] = "packet-legacy";
    legacy["usage_estimate"]["resources"]["gpu_seconds"] = nullptr;
    Json bound = legacy;
    bound.erase("usage_estimate");
    Json estimate = legacy["usage_estimate"];
    estimate["packet_request_sha256"] = Sha256::hex(json::canonical(bound));
    auto admitted = (*policy)->request(estimate);
    check(bool(admitted) && (*admitted)["authorized"] == true, "legacy request setup failed");
    auto legacy_actual = (*policy)->complete("legacy", Json{{"resources", Json{{"calls", 1},
                                             {"money_usd", 0}, {"gpu_seconds", nullptr}}},
                                             {"provenance", "instrument_measured"}});
    check(bool(legacy_actual), "legacy accounting setup failed");
    auto legacy_before = (*policy)->inspect("packet-legacy");
    auto legacy_replay = call(ctx, legacy);
    check(legacy_replay["executed"] == false &&
              legacy_replay["dispatch_status"] == "legacy_unresolved_indeterminate",
          "legacy unresolved operation was redispatched");
    auto legacy_after = (*policy)->inspect("packet-legacy");
    check(bool(legacy_before) && bool(legacy_after) && *legacy_before == *legacy_after,
          "legacy replay changed accounting");

    // Seed the durable boundaries reached by interrupted dispatch/settlement.
    auto seed = [&](const char* id, const char* status) {
      auto command = make(id, 1);
      command["usage_estimate"]["baseline_key"] = std::string("packet-") + id;
      command["usage_estimate"]["resources"]["gpu_seconds"] = nullptr;
      Json command_bound = command;
      command_bound.erase("usage_estimate");
      const auto request_hash = Sha256::hex(json::canonical(command_bound));
      Json command_estimate = command["usage_estimate"];
      command_estimate["packet_request_sha256"] = request_hash;
      auto authorization = (*policy)->request(command_estimate);
      check(bool(authorization), "interruption admission setup failed");
      Json record = {{"schema", "loom.packet_dispatch/1"}, {"operation_id", id},
                     {"request_sha256", request_hash},
                     {"estimate_sha256", Sha256::hex(json::canonical(command_estimate))},
                     {"usage_receipt_id", (*authorization)["receipt_id"]}, {"status", status},
                     {"result", nullptr}, {"actual", nullptr}, {"response", nullptr}};
      if (std::string_view(status) == "result_ready") {
        record["result"] = first["result"];
        record["actual"] = Json{{"resources", Json{{"calls", 1}, {"money_usd", 0},
                                                   {"gpu_seconds", nullptr}}},
                               {"provenance", "instrument_measured"}};
      }
      auto db = sql::Connection::open(root / "usage-policy.sqlite");
      check(bool(db), "interruption receipt database unavailable");
      check(bool(db->run("INSERT INTO loom_packet_dispatch_v1(operation_id,record) VALUES(?,?)",
                         id, json::dump(seal(std::move(record))))), "interruption receipt setup failed");
      return command;
    };
    auto pending = seed("interrupted-claim", "claimed");
    auto accounting = seed("interrupted-settlement", "result_ready");
    loom_shutdown(ctx);
    ctx = loom_init_ex(raw.c_str(), &error);
    check(ctx != nullptr, "runtime restart failed");
    loom_set_config(ctx, "loom_usage_policy", "{}");
    auto restarted_unknown = call(ctx, unknown);
    check(restarted_unknown["executed"] == false && restarted_unknown["replayed"] == true &&
              restarted_unknown["result"] == unresolved["result"],
          "restart lost completed dispatch receipt");
    auto interrupted = call(ctx, pending);
    check(interrupted["executed"] == false && interrupted["replayed"] == false &&
              interrupted["dispatch_status"] == "pending_or_interrupted",
          "interrupted claim was redispatched");
    auto recovered = call(ctx, accounting);
    check(recovered["executed"] == false && recovered["replayed"] == true &&
              recovered["usage_settlement"]["status"] == "unresolved",
          "accounting-only restart recovery failed");
    auto recovered_replay = call(ctx, accounting);
    check(recovered_replay["executed"] == false && recovered_replay["replayed"] == true,
          "recovered output was dispatched again");
    auto recovered_state = (*policy)->inspect("packet-interrupted-settlement");
    check(bool(recovered_state) && (*recovered_state)["baseline"]["calls"]["samples"] == 1 &&
              (*recovered_state)["reservations"]["gpu_seconds"].is_null(),
          "recovery duplicated measurement or erased unknown reservation");
    std::cout << "policy integration: 14/14 scenarios passed (initial, 10x hold, confirmed resume, binding "
                 "conflict, failure cancel, unknown quantity, completed replay, unresolved replay, "
                 "failure replay, concurrent duplicate, failed settlement recovery, legacy unresolved, interrupted claim, "
                 "accounting-only restart recovery)\n";
    loom_shutdown(ctx);
    return 0;
  } catch (...) {
    loom_shutdown(ctx);
    throw;
  }
}
