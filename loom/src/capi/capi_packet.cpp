#include <chrono>

#include "context.h"
#include "packet/packet.h"
#if __has_include("loom/usage_policy.h")
#include "loom/sqlite.h"
#include "loom/usage_policy.h"
#endif

using namespace loom;
using namespace loom::capi;

#if __has_include("loom/usage_policy.h")
namespace {
// Admission and accounting belong to W2. This receipt records only whether
// this packet command was dispatched, and its original output/measurements.
// A unique, durable claim protects duplicate IDs across contexts and processes.
struct PacketDispatch {
  sql::Connection db;

  static Result<PacketDispatch> open(const std::filesystem::path& path, const Json& options) {
    sql::OpenOptions sqlite;
    sqlite.busy_timeout_ms = options.at("ledger_busy_timeout_ms").get<int>();
    LOOM_TRY_ASSIGN(auto connection, sql::Connection::open(path, sqlite));
    LOOM_TRY(connection.exec("PRAGMA synchronous=FULL;"));
    LOOM_TRY(connection.exec(
        "CREATE TABLE IF NOT EXISTS loom_packet_dispatch_v1("
        "operation_id TEXT PRIMARY KEY,record TEXT NOT NULL);"));
    return PacketDispatch{std::move(connection)};
  }

  static Json seal(Json record) {
    record.erase("dispatch_sha256");
    record["dispatch_sha256"] = packet::digest(record);
    return record;
  }

  Result<std::pair<Json, bool>> claim(const Json& decision, const Json& estimate,
                                    const std::string& request_hash) {
    const auto id = json::get_string(estimate, "operation_id");
    const auto estimate_hash = packet::digest(estimate);
    sql::Txn txn(db);
    LOOM_TRY(txn.begin_status());
    LOOM_TRY_ASSIGN(auto previous, db.query_text(
                                      "SELECT record FROM loom_packet_dispatch_v1 WHERE operation_id=?", id));
    if (previous) {
      Json record;
      try {
        record = packet::parse_strict(*previous);
        packet::shape(record, {"schema", "operation_id", "request_sha256", "estimate_sha256",
                               "usage_receipt_id", "status", "result", "actual", "response",
                               "dispatch_sha256"});
      } catch (const std::exception&) {
        return Error(Errc::Parse, "invalid packet dispatch receipt");
      }
      if (record["schema"] != "loom.packet_dispatch/1" || seal(record) != record ||
          record["operation_id"] != id || record["request_sha256"] != request_hash ||
          record["estimate_sha256"] != estimate_hash ||
          record["usage_receipt_id"] != decision["receipt_id"])
        return Error(Errc::Conflict, "packet dispatch receipt binding or integrity mismatch");
      const auto status = json::get_string(record, "status");
      if ((status != "claimed" && status != "result_ready" && status != "finished") ||
          (status == "claimed" && (!record["result"].is_null() || !record["actual"].is_null() ||
                                    !record["response"].is_null())) ||
          (status != "claimed" && record["result"].is_null()) ||
          (status == "finished" && !record["response"].is_object()))
        return Error(Errc::Parse, "inconsistent packet dispatch receipt lifecycle");
      LOOM_TRY(txn.commit());
      return std::pair<Json, bool>{std::move(record), false};
    }
    // An old unresolved reservation may already represent a real dispatch.
    // Unknown measurements are not permission to perform the command again.
    if (!json::get_bool(decision, "authorized") || decision["status"] != "allowed" ||
        !decision["actual"].empty()) {
      LOOM_TRY(txn.commit());
      return std::pair<Json, bool>{nullptr, false};
    }
    Json record = seal(Json{{"schema", "loom.packet_dispatch/1"}, {"operation_id", id},
                            {"request_sha256", request_hash}, {"estimate_sha256", estimate_hash},
                            {"usage_receipt_id", decision["receipt_id"]}, {"status", "claimed"},
                            {"result", nullptr}, {"actual", nullptr}, {"response", nullptr}});
    LOOM_TRY(db.run("INSERT INTO loom_packet_dispatch_v1(operation_id,record) VALUES(?,?)",
                    id, json::dump(record)));
    LOOM_TRY(txn.commit());
    return std::pair<Json, bool>{std::move(record), true};
  }

  Status save(Json& current, Json next) {
    next = seal(std::move(next));
    sql::Txn txn(db);
    LOOM_TRY(txn.begin_status());
    LOOM_TRY(db.run("UPDATE loom_packet_dispatch_v1 SET record=? WHERE operation_id=? AND record=?",
                    json::dump(next), json::get_string(current, "operation_id"), json::dump(current)));
    if (db.changes() != 1) return Error(Errc::Conflict, "packet dispatch receipt changed concurrently");
    LOOM_TRY(txn.commit());
    current = std::move(next);
    return {};
  }
};
}  // namespace
#endif

extern "C" LOOM_API const char* loom_packet(LoomContext* ctx, const char* request_json) {
  return guard_json("loom_packet", [&]() -> const char* {
    LOOM_CAPI_REQUIRE_CTX_JSON(ctx);
    if (!request_json) return out_error(Errc::InvalidArgument, "packet request required");
    Json request;
    try {
      request = packet::parse_strict(request_json);
    } catch (const std::invalid_argument& e) {
      if (std::string_view(e.what()) == "graph_packet_native_integer_representation_unavailable")
        return out_error(Errc::Unavailable, e.what());
      return out_error(Errc::Parse, "packet request: invalid strict JSON");
    } catch (const std::exception&) {
      return out_error(Errc::Parse, "packet request: invalid strict JSON");
    }
    // Thread 2 supplies the durable policy implementation. Preserve the exact
    // request binding in its estimate: reusing an operation ID for changed
    // packet data must conflict even when the declared quantities are equal.
    if (request.contains("usage_estimate")) {
#if __has_include("loom/usage_policy.h")
      auto options = effective_usage_policy_options(ctx->rt->config());
      if (!options) return out_error(options.error());
      auto policy = UsagePolicy::open(ctx->rt->paths().root / "usage-policy.sqlite", *options);
      if (!policy) return out_error(policy.error());
      Json estimate = request["usage_estimate"];
      if (!estimate.is_object()) return out_error(Errc::InvalidArgument, "usage_estimate must be an object");
      Json bound = request;
      bound.erase("usage_estimate");
      estimate["packet_request_sha256"] = packet::digest(bound);
      auto decision = (*policy)->request(estimate);
      if (!decision) return out_error(decision.error());
      auto dispatch = PacketDispatch::open(ctx->rt->paths().root / "usage-policy.sqlite", *options);
      if (!dispatch) return out_error(dispatch.error());
      auto claim = dispatch->claim(*decision, estimate, packet::digest(bound));
      if (!claim) return out_error(claim.error());
      auto [record, owns_claim] = std::move(*claim);
      if (record.is_null())
        return out(Json{{"usage_decision", *decision}, {"executed", false}, {"replayed", false},
                        {"dispatch_status", decision->at("status") == "unresolved"
                                                ? "legacy_unresolved_indeterminate" : "not_admitted"}});
      if (!owns_claim && record["status"] == "claimed")
        return out(Json{{"usage_decision", *decision}, {"executed", false}, {"replayed", false},
                        {"dispatch_status", "pending_or_interrupted"},
                        {"dispatch_receipt", record}});
      if (!owns_claim && record["status"] == "finished") {
        Json response = record["response"];
        response["executed"] = false;
        response["replayed"] = true;
        response["usage_current_decision"] = *decision;
        return out(response);
      }
      if (owns_claim) {
        const auto started = std::chrono::steady_clock::now();
        auto result = packet::execute(bound);
        Json next = record;
        next["status"] = "result_ready";
        next["result"] = result ? *result : error_json(result.error());
        if (result && !result->contains("error")) {
          // Replay settlement uses these original measurements, never timings
          // or quantities from a second dispatch. Unknowns remain reserved.
          const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
          Json measured = Json::object();
          const Json available{{"input_bytes", std::strlen(request_json)},
                               {"output_bytes", json::dump(*result).size()},
                               {"wall_seconds", elapsed}, {"calls", 1}, {"money_usd", 0}};
          for (auto it = estimate["resources"].begin(); it != estimate["resources"].end(); ++it)
            measured[it.key()] = available.contains(it.key()) ? available[it.key()] : Json(nullptr);
          next["actual"] = Json{{"resources", measured}, {"provenance", "instrument_measured"}};
        }
        auto saved = dispatch->save(record, std::move(next));
        if (!saved) {
          Json response{{"result", result ? *result : error_json(result.error())},
                        {"usage_decision", *decision}, {"executed", true}, {"replayed", false},
                        {"dispatch_status", "outcome_persistence_failed"},
                        {"dispatch_error", error_json(saved.error())}};
          return out(response);
        }
      }
      const bool failed = record["result"].contains("error");
      const auto id = json::get_string(estimate, "operation_id");
      auto settled = failed ? (*policy)->cancel(id, "packet operation failed")
                            : (*policy)->complete(id, record["actual"]);
      Json response = failed ? record["result"] : Json{{"result", record["result"]}};
      response["usage_decision"] = *decision;
      response["usage_settlement"] = settled ? *settled : error_json(settled.error());
      response["executed"] = owns_claim;
      response["replayed"] = !owns_claim;
      Json next = record;
      next["status"] = settled ? "finished" : "result_ready";
      next["response"] = response;
      auto saved = dispatch->save(record, std::move(next));
      if (!saved) response["dispatch_error"] = error_json(saved.error());
      return out(response);
#else
      return out_error(Errc::Unavailable, "usage policy dependency (thread 2) is not integrated");
#endif
    }
    return out_result(packet::execute(request));
  });
}
