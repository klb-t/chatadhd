#include "loom/usage_policy.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <system_error>

#include "loom/sqlite.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"

namespace loom {
namespace {
Status quantities(const Json& values) {
  if (!values.is_object()) return Error(Errc::InvalidArgument, "resources must be an object");
  for (auto it = values.begin(); it != values.end(); ++it) {
    if (it.key().empty() || (!it->is_null() && (!it->is_number() ||
        !std::isfinite(it->get<double>()) || it->get<double>() < 0)))
      return Error(Errc::InvalidArgument, "resource quantities must be finite nonnegative numbers or null");
  }
  return {};
}
Status validate_estimate(const Json& estimate) {
  if (!estimate.is_object() || json::get_string(estimate, "operation_id").empty() ||
      json::get_string(estimate, "baseline_key").empty() || !estimate.contains("resources"))
    return Error(Errc::InvalidArgument, "estimate needs operation_id, baseline_key and resources");
  return quantities(estimate["resources"]);
}
Result<double> narrow(long double value) {
  if (!std::isfinite(value) || value > std::numeric_limits<double>::max())
    return Error(Errc::InvalidArgument, "resource aggregate exceeds finite JSON number representation");
  return static_cast<double>(value);
}
bool same(const Json& a, const Json& b) { return json::canonical(a) == json::canonical(b); }

// Corrupt ledgers fail visibly; never reinterpret them as an empty baseline.
Result<Json> decode_record(std::string_view raw) {
  LOOM_TRY_ASSIGN(auto record, json::parse(raw));
  try {
    if (!record.is_object() || record.value("schema", "") != "loom.usage_decision/1" ||
        !record.contains("estimate") || !validate_estimate(record["estimate"]) ||
        record["operation_id"] != record["estimate"]["operation_id"] ||
        record["baseline_key"] != record["estimate"]["baseline_key"] ||
        !record["authorized"].is_boolean() || !record["resources"].is_object() ||
        !record.contains("options") || !validate_usage_policy_options(record["options"]) ||
        !record.contains("remaining") || !quantities(record["remaining"]) || !record["actual"].is_object())
      return Error(Errc::Parse, "invalid usage operation record");
    const std::set<std::string> statuses{"allowed", "requires_confirmation", "denied", "cancelled", "unresolved", "completed"};
    if (!record["status"].is_string() || !statuses.contains(record["status"].get<std::string>()))
      return Error(Errc::Parse, "invalid usage operation status");
    const auto status = record["status"].get<std::string>();
    if (record["authorized"].get<bool>() != (status == "allowed" || status == "unresolved") ||
        ((status == "requires_confirmation" || status == "denied" || status == "completed" || status == "cancelled") &&
         !record["remaining"].empty()))
      return Error(Errc::Parse, "inconsistent usage operation lifecycle");
    bool growth = false;
    for (auto it = record["resources"].begin(); it != record["resources"].end(); ++it) {
      if (!it->is_object() || !(*it)["requires_confirmation"].is_boolean())
        return Error(Errc::Parse, "invalid usage comparison");
      growth = growth || (*it)["requires_confirmation"].get<bool>();
    }
    Json receipt{{"schema", record["schema"]}, {"operation_id", record["operation_id"]},
                 {"baseline_key", record["baseline_key"]}, {"estimate", record["estimate"]},
                 {"options", record["options"]}, {"status", growth ? "requires_confirmation" : "allowed"},
                 {"authorized", false}, {"resources", record["resources"]}};
    if (record["receipt_id"] != "usage_" + Sha256::hex(json::canonical(receipt)))
      return Error(Errc::Parse, "usage receipt integrity failure");
    for (auto it = record["actual"].begin(); it != record["actual"].end(); ++it) {
      if (!it->is_object() || !it->contains("value") ||
          !quantities(Json{{it.key(), (*it)["value"]}}))
        return Error(Errc::Parse, "invalid usage actual record");
      auto source = json::get_string(*it, "provenance");
      if (source != "instrument_measured" && source != "provider_reported" && source != "declared")
        return Error(Errc::Parse, "invalid usage actual provenance");
    }
    return record;
  } catch (const std::exception&) {
    return Error(Errc::Parse, "invalid usage operation record shape");
  }
}
}  // namespace

struct UsagePolicy::Impl {
  sql::Connection db;
  Json options;
  std::filesystem::path path;
  std::mutex mu;

  // The tombstone a compaction left for an operation id, if any.
  Result<std::optional<Json>> folded(std::string_view id) {
    LOOM_TRY_ASSIGN(auto st, db.prepare("SELECT baseline_key,status,estimate_sha256,folded_at FROM usage_folded_operations WHERE id=?"));
    st.bind_all(id);
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) return std::optional<Json>{};
    return std::optional<Json>(Json{{"operation_id", std::string(id)}, {"baseline_key", st.get_text(0)},
                                    {"status", st.get_text(1)}, {"estimate_sha256", st.get_text(2)},
                                    {"folded_at", st.get_text(3)}});
  }
  // An operation that is absent from the ledger: unknown, or folded by a compaction.
  Error missing(std::string_view id) {
    auto tomb = folded(id);
    if (tomb && *tomb) return Error(Errc::Conflict, "usage operation was folded by ledger compaction");
    return Error(Errc::NotFound, "usage operation not found");
  }

  Result<std::optional<Json>> operation(std::string_view id) {
    LOOM_TRY_ASSIGN(auto raw, db.query_text("SELECT record FROM usage_operations WHERE id=?", id));
    if (!raw) return std::optional<Json>{};
    LOOM_TRY_ASSIGN(auto record, decode_record(*raw));
    return std::optional<Json>(std::move(record));
  }
  Status put(const Json& record) {
    return db.run("INSERT INTO usage_operations(id,baseline_key,record,active) VALUES(?,?,?,?) "
                  "ON CONFLICT(id) DO UPDATE SET record=excluded.record,active=excluded.active",
                  json::get_string(record, "operation_id"), json::get_string(record, "baseline_key"),
                  json::dump(record), record["authorized"].get<bool>() ? 1 : 0);
  }
  Status event(std::string_view id, std::string_view kind, const Json& payload) {
    return db.run("INSERT INTO usage_events(operation_id,kind,payload) VALUES(?,?,?)", id, kind, json::dump(payload));
  }
  Result<Json> baseline(std::string_view key, std::string_view resource) {
    std::int64_t window = -1;
    if (!options["baseline_window"].is_null()) window = options["baseline_window"].get<std::int64_t>();
    LOOM_TRY_ASSIGN(auto st, db.prepare("SELECT value FROM usage_samples WHERE baseline_key=? AND resource=? "
                                      "ORDER BY sequence DESC LIMIT ?"));
    st.bind_all(key, resource, window);
    long double sum = 0;
    std::int64_t count = 0;
    for (;;) {
      LOOM_TRY_ASSIGN(bool row, st.step());
      if (!row) break;
      const auto value = st.get_double(0);
      if (st.is_null(0) || !std::isfinite(value) || value < 0)
        return Error(Errc::Parse, "invalid stored usage measurement");
      sum += static_cast<long double>(value);
      ++count;
    }
    // Samples a compaction summed away: they stand in for the missing part of the
    // window (never while the window is full), or join the all-history mean.
    if (window < 0 || count < window) {
      LOOM_TRY_ASSIGN(auto folded_row, db.prepare("SELECT samples,sample_sum FROM usage_fold_samples WHERE baseline_key=? AND resource=?"));
      folded_row.bind_all(key, resource);
      LOOM_TRY_ASSIGN(bool has_fold, folded_row.step());
      if (has_fold && folded_row.get_int(0) > 0) {
        const std::int64_t folded_count = folded_row.get_int(0);
        const long double folded_sum = folded_row.get_double(1);
        if (window < 0) {
          sum += folded_sum;
          count += folded_count;
        } else {
          const std::int64_t fill = std::min(window - count, folded_count);
          sum += (folded_sum / static_cast<long double>(folded_count)) * static_cast<long double>(fill);
          count += fill;
        }
      }
    }
    if (count) {
      LOOM_TRY_ASSIGN(double mean, narrow(sum / count));
      return Json{{"value", mean}, {"samples", count}, {"source", "measured"}};
    }
    const Json& seeds = options["initial_baselines"];
    auto cohort = seeds.find(key);
    if (cohort != seeds.end() && cohort->contains(resource) && !(*cohort)[resource].is_null())
      return Json{{"value", (*cohort)[resource]}, {"samples", 0}, {"source", "declared"}};
    return Json{{"value", nullptr}, {"samples", 0}, {"source", "unavailable"}};
  }
  Result<Json> reservations(std::string_view key, std::string_view excluding = "", bool known_only = false) {
    Json totals = Json::object();
    LOOM_TRY_ASSIGN(auto st, db.prepare("SELECT record FROM usage_operations WHERE baseline_key=? AND id<>? AND active=1"));
    st.bind_all(key, excluding);
    for (;;) {
      LOOM_TRY_ASSIGN(bool row, st.step());
      if (!row) break;
      LOOM_TRY_ASSIGN(auto record, decode_record(st.get_text(0)));
      if (!record.value("authorized", false)) continue;
      const auto& reserved = record["remaining"];
      for (auto it = reserved.begin(); it != reserved.end(); ++it) {
        if (!totals.contains(it.key())) totals[it.key()] = 0.0;
        if (known_only && it->is_null()) continue;
        if (totals[it.key()].is_null() || it->is_null()) totals[it.key()] = nullptr;
        else {
          LOOM_TRY_ASSIGN(double total, narrow(static_cast<long double>(totals[it.key()].get<double>()) + it->get<double>()));
          totals[it.key()] = total;
        }
      }
    }
    return totals;
  }
  Result<Json> assess(const Json& estimate) {
    const auto key = json::get_string(estimate, "baseline_key");
    LOOM_TRY_ASSIGN(auto reserved, reservations(key, json::get_string(estimate, "operation_id")));
    LOOM_TRY_ASSIGN(auto known_reserved, reservations(key, json::get_string(estimate, "operation_id"), true));
    Json result{{"schema", "loom.usage_decision/1"}, {"operation_id", estimate["operation_id"]},
                {"baseline_key", key}, {"estimate", estimate}, {"options", options},
                {"status", "allowed"}, {"authorized", false}, {"resources", Json::object()}};
    // Include outstanding dimensions even if this operation does not mention them.
    std::set<std::string> dimensions;
    for (auto it = estimate["resources"].begin(); it != estimate["resources"].end(); ++it) dimensions.insert(it.key());
    if (options["include_reservations"].get<bool>())
      for (auto it = reserved.begin(); it != reserved.end(); ++it) dimensions.insert(it.key());
    for (const auto& dimension : dimensions) {
      LOOM_TRY_ASSIGN(auto base, baseline(key, dimension));
      Json amount = estimate["resources"].value(dimension, Json(0.0));
      Json outstanding = options["include_reservations"].get<bool>() ? reserved.value(dimension, Json(0.0)) : Json(0.0);
      double known_outstanding = options["include_reservations"].get<bool>() ? known_reserved.value(dimension, 0.0) : 0.0;
      LOOM_TRY_ASSIGN(double lower_bound, narrow(static_cast<long double>(known_outstanding) +
                                                  (amount.is_null() ? 0.0 : amount.get<double>())));
      Json projected = nullptr;
      if (!amount.is_null() && !outstanding.is_null()) {
        LOOM_TRY_ASSIGN(double total, narrow(static_cast<long double>(amount.get<double>()) + outstanding.get<double>()));
        projected = total;
      }
      bool needs = false;
      Json ratio = nullptr;
      std::string baseline_status = "unavailable";
      if (!base["value"].is_null()) {
        double b = base["value"].get<double>();
        baseline_status = b == 0 ? "zero" : "available";
        {
          long double p = lower_bound;
          if (b == 0) needs = p > 0;
          else {
            long double r = p / static_cast<long double>(b);
            if (!projected.is_null() && r <= std::numeric_limits<double>::max()) ratio = static_cast<double>(r);
            // Compare in the same binary64 representation returned to callers:
            // 0.1 -> 1.0 must agree with the displayed ratio of exactly ten.
            needs = r > std::numeric_limits<double>::max() ||
                    static_cast<double>(r) >= options["growth_factor"].get<double>();
          }
        }
      }
      result["resources"][dimension] = Json{{"estimate", amount}, {"baseline", base["value"]},
          {"baseline_source", base["source"]}, {"baseline_samples", base["samples"]},
          {"reserved", outstanding}, {"projected", projected}, {"ratio", ratio},
          {"reserved_lower_bound", known_outstanding}, {"projected_lower_bound", lower_bound},
          {"projection_status", projected.is_null() ? "partial_unknown" : "estimated"},
          {"baseline_status", baseline_status}, {"requires_confirmation", needs}};
      if (needs) result["status"] = "requires_confirmation";
    }
    result["receipt_id"] = "usage_" + Sha256::hex(json::canonical(result));
    return result;
  }
};

UsagePolicy::UsagePolicy(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
UsagePolicy::~UsagePolicy() = default;

Result<std::unique_ptr<UsagePolicy>> UsagePolicy::open(const std::filesystem::path& path, const Json& options) {
  LOOM_TRY(validate_usage_policy_options(options));
  LOOM_TRY_ASSIGN(auto preset, usage_policy_preset());
  if (path.empty()) return Error(Errc::InvalidArgument, "ledger path is required");
  if (path.has_parent_path()) LOOM_TRY(fsutil::ensure_dir(path.parent_path()));
  auto impl = std::make_unique<Impl>();
  impl->path = path;
  impl->options = std::move(preset);
  for (auto it = options.begin(); it != options.end(); ++it) impl->options[it.key()] = it.value();
  sql::OpenOptions sql_options;
  sql_options.busy_timeout_ms = impl->options["ledger_busy_timeout_ms"].get<int>();
  LOOM_TRY_ASSIGN(auto connection, sql::Connection::open(path, sql_options));
  impl->db = std::move(connection);
  // Several callers may create the ledger at once; the WAL switch needs a retry
  // (see Connection::enter_wal_mode). The busy timeout is the budget.
  LOOM_TRY(impl->db.enter_wal_mode(sql_options.busy_timeout_ms));
  LOOM_TRY(impl->db.exec("PRAGMA synchronous=FULL;"));
  sql::Txn txn(impl->db);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY(impl->db.exec(
      "CREATE TABLE IF NOT EXISTS usage_metadata(version INTEGER NOT NULL);"
      "INSERT INTO usage_metadata SELECT 1 WHERE NOT EXISTS(SELECT 1 FROM usage_metadata);"));
  LOOM_TRY_ASSIGN(auto version, impl->db.query_int("SELECT version FROM usage_metadata"));
  if (!version || *version != 1) return Error(Errc::Unsupported, "unsupported usage ledger schema");
  LOOM_TRY(impl->db.exec(
      "CREATE TABLE IF NOT EXISTS usage_operations(id TEXT PRIMARY KEY,baseline_key TEXT NOT NULL,record TEXT NOT NULL,active INTEGER NOT NULL);"
      "CREATE INDEX IF NOT EXISTS usage_operation_cohort ON usage_operations(baseline_key,active);"
      "CREATE TABLE IF NOT EXISTS usage_samples(sequence INTEGER PRIMARY KEY,operation_id TEXT NOT NULL,baseline_key TEXT NOT NULL,"
      "resource TEXT NOT NULL,value REAL NOT NULL,provenance TEXT NOT NULL,UNIQUE(operation_id,resource));"
      "CREATE INDEX IF NOT EXISTS usage_sample_cohort ON usage_samples(baseline_key,resource,sequence);"
      "CREATE TABLE IF NOT EXISTS usage_events(sequence INTEGER PRIMARY KEY,operation_id TEXT NOT NULL,kind TEXT NOT NULL,payload TEXT NOT NULL,"
      "recorded_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
      "CREATE INDEX IF NOT EXISTS usage_event_operation ON usage_events(operation_id,sequence);"
      // Compaction (additive; a ledger that was never compacted keeps these empty).
      "CREATE TABLE IF NOT EXISTS usage_folded_operations(id TEXT PRIMARY KEY,baseline_key TEXT NOT NULL,status TEXT NOT NULL,"
      "estimate_sha256 TEXT NOT NULL,folded_at TEXT NOT NULL);"
      "CREATE INDEX IF NOT EXISTS usage_folded_cohort ON usage_folded_operations(baseline_key);"
      "CREATE TABLE IF NOT EXISTS usage_fold_samples(baseline_key TEXT NOT NULL,resource TEXT NOT NULL,samples INTEGER NOT NULL,"
      "sample_sum REAL NOT NULL,sample_min REAL NOT NULL,sample_max REAL NOT NULL,PRIMARY KEY(baseline_key,resource));"
      "CREATE TABLE IF NOT EXISTS usage_compaction_state(id INTEGER PRIMARY KEY CHECK(id=1),last_run_at TEXT NOT NULL);"));
  LOOM_TRY(txn.commit());
  return std::unique_ptr<UsagePolicy>(new UsagePolicy(std::move(impl)));
}

Result<Json> UsagePolicy::preview(const Json& estimate) {
  LOOM_TRY(validate_estimate(estimate));
  std::lock_guard lock(impl_->mu);
  sql::Txn txn(impl_->db, false);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto result, impl_->assess(estimate));
  LOOM_TRY(txn.commit());
  return result;
}

Result<Json> UsagePolicy::request(const Json& estimate) {
  LOOM_TRY(validate_estimate(estimate));
  std::lock_guard lock(impl_->mu);
  sql::Txn txn(impl_->db);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto previous, impl_->operation(json::get_string(estimate, "operation_id")));
  if (!previous) {
    // An operation a compaction folded keeps its identity: the same estimate
    // answers as the completed operation it was, a different one conflicts.
    LOOM_TRY_ASSIGN(auto tomb, impl_->folded(json::get_string(estimate, "operation_id")));
    if (tomb) {
      if ((*tomb)["estimate_sha256"] != Sha256::hex(json::canonical(estimate)))
        return Error(Errc::Conflict, "operation ID already has a different estimate");
      LOOM_TRY(txn.commit());
      return Json{{"schema", "loom.usage_decision/1"}, {"operation_id", estimate["operation_id"]},
                  {"baseline_key", (*tomb)["baseline_key"]}, {"estimate", estimate}, {"status", (*tomb)["status"]},
                  {"authorized", false}, {"compacted", true}, {"folded_at", (*tomb)["folded_at"]},
                  {"remaining", Json::object()}, {"actual", Json::object()}, {"resources", Json::object()}};
    }
  }
  if (previous) {
    if (!same((*previous)["estimate"], estimate)) return Error(Errc::Conflict, "operation ID already has a different estimate");
    if (json::get_string(*previous, "status") != "requires_confirmation") {
      LOOM_TRY(txn.commit());
      return *previous;
    }
  }
  LOOM_TRY_ASSIGN(auto result, impl_->assess(estimate));
  if (previous && result["receipt_id"] == (*previous)["receipt_id"]) {
    LOOM_TRY(txn.commit());
    return *previous;
  }
  bool admitted = result["status"] == "allowed";
  result["authorized"] = admitted;
  result["remaining"] = admitted ? estimate["resources"] : Json::object();
  result["actual"] = Json::object();
  LOOM_TRY(impl_->put(result));
  LOOM_TRY(impl_->event(json::get_string(result, "operation_id"), "request", result));
  LOOM_TRY(txn.commit());
  return result;
}

Result<Json> UsagePolicy::confirm(std::string_view id, std::string_view receipt_id, bool approved,
                                std::string_view confirmation_ref) {
  if (confirmation_ref.empty()) return Error(Errc::InvalidArgument, "confirmation_ref is required");
  std::lock_guard lock(impl_->mu);
  sql::Txn txn(impl_->db);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto stored, impl_->operation(id));
  if (!stored) return impl_->missing(id);
  Json result = *stored;
  if (result["receipt_id"] != receipt_id) return Error(Errc::Conflict, "confirmation receipt does not match");
  if (result.contains("confirmation")) {
    const auto& c = result["confirmation"];
    if (c["approved"] != approved || c["ref"] != confirmation_ref)
      return Error(Errc::Conflict, "operation already has a different confirmation");
    LOOM_TRY(txn.commit());
    return result;
  }
  if (result["status"] != "requires_confirmation") return Error(Errc::Conflict, "operation is not waiting for confirmation");
  if (approved) {
    LOOM_TRY_ASSIGN(auto fresh, impl_->assess(result["estimate"]));
    if (fresh["receipt_id"] != result["receipt_id"])
      return Error(Errc::Conflict, "usage projection changed; request a fresh receipt");
  }
  result["confirmation"] = Json{{"approved", approved}, {"ref", confirmation_ref}};
  result["authorized"] = approved;
  result["status"] = approved ? "allowed" : "denied";
  result["remaining"] = approved ? result["estimate"]["resources"] : Json::object();
  LOOM_TRY(impl_->put(result));
  LOOM_TRY(impl_->event(id, "confirmation", result["confirmation"]));
  LOOM_TRY(txn.commit());
  return result;
}

Result<Json> UsagePolicy::complete(std::string_view id, const Json& actual) {
  if (!actual.is_object() || !actual.contains("resources"))
    return Error(Errc::InvalidArgument, "actual needs resources and provenance");
  LOOM_TRY(quantities(actual["resources"]));
  auto provenance = json::get_string(actual, "provenance");
  bool measured = provenance == "instrument_measured" || provenance == "provider_reported";
  if (!measured && provenance != "declared") return Error(Errc::InvalidArgument, "invalid actual provenance");
  std::lock_guard lock(impl_->mu);
  sql::Txn txn(impl_->db);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto stored, impl_->operation(id));
  if (!stored) return impl_->missing(id);
  Json result = *stored;
  auto status = json::get_string(result, "status");
  if (status != "allowed" && status != "unresolved" && status != "completed")
    return Error(Errc::Conflict, "operation was not admitted or was cancelled");
  bool changed = false;
  bool overrun = false;
  for (auto it = actual["resources"].begin(); it != actual["resources"].end(); ++it) {
    auto previous = result["actual"].find(it.key());
    if (previous != result["actual"].end() && !(*previous)["value"].is_null()) {
      if (!it->is_null() && (!same((*previous)["value"], it.value()) || (*previous)["provenance"] != provenance))
        return Error(Errc::Conflict, "recorded measurement is immutable");
      continue;
    }
    Json entry{{"value", it.value()}, {"provenance", provenance}};
    if (previous != result["actual"].end() && same(*previous, entry)) continue;
    result["actual"][it.key()] = entry;
    changed = true;
    if (!it->is_null()) {
      result["remaining"].erase(it.key());
      if (measured)
        LOOM_TRY(impl_->db.run("INSERT INTO usage_samples(operation_id,baseline_key,resource,value,provenance) VALUES(?,?,?,?,?)",
            id, json::get_string(result, "baseline_key"), it.key(), it->get<double>(), provenance));
      auto original = result["estimate"]["resources"].find(it.key());
      overrun = overrun || original == result["estimate"]["resources"].end() ||
                original->is_null() || it->get<double>() > original->get<double>();
    } else if (!result["remaining"].contains(it.key())) result["remaining"][it.key()] = nullptr;
  }
  const auto final_status = result["remaining"].empty() ? "completed" : "unresolved";
  if (changed || status != final_status) {
    result["status"] = result["remaining"].empty() ? "completed" : "unresolved";
    result["authorized"] = !result["remaining"].empty();
    result["overrun"] = result.value("overrun", false) || overrun;
    LOOM_TRY(impl_->put(result));
    LOOM_TRY(impl_->event(id, "actual", actual));
  }
  LOOM_TRY(txn.commit());
  return result;
}

Result<Json> UsagePolicy::cancel(std::string_view id, std::string_view reason) {
  if (reason.empty()) return Error(Errc::InvalidArgument, "cancellation reason is required");
  std::lock_guard lock(impl_->mu);
  sql::Txn txn(impl_->db);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto stored, impl_->operation(id));
  if (!stored) return impl_->missing(id);
  Json result = *stored;
  if (result["status"] == "cancelled") {
    if (result["cancellation_reason"] != reason) return Error(Errc::Conflict, "different cancellation already recorded");
    LOOM_TRY(txn.commit());
    return result;
  }
  if (result["status"] == "completed" || result["status"] == "denied")
    return Error(Errc::Conflict, "operation already terminal");
  result["status"] = "cancelled";
  result["authorized"] = false;
  result["remaining"] = Json::object();
  result["cancellation_reason"] = reason;
  LOOM_TRY(impl_->put(result));
  LOOM_TRY(impl_->event(id, "cancel", Json{{"reason", reason}}));
  LOOM_TRY(txn.commit());
  return result;
}

Result<Json> UsagePolicy::inspect(std::string_view key) {
  if (key.empty()) return Error(Errc::InvalidArgument, "baseline_key is required");
  std::lock_guard lock(impl_->mu);
  sql::Txn txn(impl_->db, false);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto reserved, impl_->reservations(key));
  Json result{{"baseline_key", key}, {"options", impl_->options}, {"baseline", Json::object()},
              {"reservations", reserved}, {"operations", Json::array()}, {"events", Json::array()}};
  std::set<std::string> dimensions;
  LOOM_TRY_ASSIGN(auto operations, impl_->db.prepare("SELECT record FROM usage_operations WHERE baseline_key=? ORDER BY id"));
  operations.bind_all(key);
  for (;;) {
    LOOM_TRY_ASSIGN(bool row, operations.step());
    if (!row) break;
    LOOM_TRY_ASSIGN(auto record, decode_record(operations.get_text(0)));
    for (auto it = record["estimate"]["resources"].begin(); it != record["estimate"]["resources"].end(); ++it) dimensions.insert(it.key());
    for (auto it = record["actual"].begin(); it != record["actual"].end(); ++it) dimensions.insert(it.key());
    result["operations"].push_back(record);
  }
  {
    // Measured resources stay visible when the operations that measured them were folded.
    LOOM_TRY_ASSIGN(auto measured_rows, impl_->db.prepare("SELECT DISTINCT resource FROM usage_samples WHERE baseline_key=? "
                                                           "UNION SELECT resource FROM usage_fold_samples WHERE baseline_key=?"));
    measured_rows.bind_all(key, key);
    for (;;) {
      LOOM_TRY_ASSIGN(bool row, measured_rows.step());
      if (!row) break;
      dimensions.insert(measured_rows.get_text(0));
    }
  }
  if (impl_->options["initial_baselines"].contains(key))
    for (auto it = impl_->options["initial_baselines"][key].begin(); it != impl_->options["initial_baselines"][key].end(); ++it) dimensions.insert(it.key());
  for (const auto& dimension : dimensions) {
    LOOM_TRY_ASSIGN(auto base, impl_->baseline(key, dimension));
    result["baseline"][dimension] = base;
  }
  LOOM_TRY_ASSIGN(auto events, impl_->db.prepare("SELECT e.sequence,e.operation_id,e.kind,e.payload,e.recorded_at FROM usage_events e "
      "JOIN usage_operations o ON o.id=e.operation_id WHERE o.baseline_key=? ORDER BY e.sequence"));
  events.bind_all(key);
  for (;;) {
    LOOM_TRY_ASSIGN(bool row, events.step());
    if (!row) break;
    LOOM_TRY_ASSIGN(auto payload, json::parse(events.get_text(3)));
    result["events"].push_back(Json{{"sequence", events.get_int(0)}, {"operation_id", events.get_text(1)},
                                      {"kind", events.get_text(2)}, {"payload", payload}, {"recorded_at", events.get_text(4)}});
  }
  // What compaction folded away from this cohort (absent when nothing was).
  LOOM_TRY_ASSIGN(auto folded_operations, impl_->db.query_int("SELECT COUNT(*) FROM usage_folded_operations WHERE baseline_key=?", key));
  LOOM_TRY_ASSIGN(auto folded_rows, impl_->db.prepare("SELECT resource,samples,sample_sum,sample_min,sample_max FROM usage_fold_samples "
                                                       "WHERE baseline_key=? ORDER BY resource"));
  folded_rows.bind_all(key);
  Json folded_samples = Json::object();
  for (;;) {
    LOOM_TRY_ASSIGN(bool row, folded_rows.step());
    if (!row) break;
    folded_samples[folded_rows.get_text(0)] = Json{{"samples", folded_rows.get_int(1)}, {"sum", folded_rows.get_double(2)},
                                                   {"min", folded_rows.get_double(3)}, {"max", folded_rows.get_double(4)}};
  }
  if ((folded_operations && *folded_operations) || !folded_samples.empty())
    result["folded"] = Json{{"operations", folded_operations.value_or(0)}, {"samples", folded_samples}};
  LOOM_TRY(txn.commit());
  return result;
}

Result<bool> UsagePolicy::compaction_due(double hours) {
  if (!std::isfinite(hours) || hours < 0) return Error(Errc::InvalidArgument, "interval must be a finite nonnegative number of hours");
  char modifier[64];
  std::snprintf(modifier, sizeof modifier, "-%.3f seconds", hours * 3600.0);
  std::lock_guard lock(impl_->mu);
  LOOM_TRY_ASSIGN(auto due, impl_->db.query_int(
      "SELECT CASE WHEN last_run_at <= datetime('now', ?) THEN 1 ELSE 0 END FROM usage_compaction_state WHERE id=1", std::string(modifier)));
  return !due || *due != 0;
}

Result<Json> UsagePolicy::compact(const Json& settings, bool dry_run, std::string_view as_of) {
  LOOM_TRY(validate_usage_compaction_settings(settings));
  std::lock_guard lock(impl_->mu);
  auto& db = impl_->db;
  const double days = settings["older_than_days"].get<double>();
  std::set<std::string> foldable;
  for (const auto& status : settings["statuses"]) foldable.insert(status.get<std::string>());
  const bool reclaim = settings["reclaim_space"].get<bool>();
  std::int64_t window = -1;  // -1: all history is the baseline, so no sample may be summed away
  if (!impl_->options["baseline_window"].is_null()) window = impl_->options["baseline_window"].get<std::int64_t>();
  const bool fold_samples = settings["fold_samples_beyond_window"].get<bool>() && window >= 0;

  char modifier[64];
  std::snprintf(modifier, sizeof modifier, "-%.3f seconds", days * 86400.0);
  std::optional<std::string> reference;
  if (!as_of.empty()) reference = std::string(as_of);
  LOOM_TRY_ASSIGN(auto cutoff, db.query_text("SELECT datetime(COALESCE(?, 'now'), ?)", reference, std::string(modifier)));
  if (!cutoff) return Error(Errc::InvalidArgument, "as_of or older_than_days is not a representable time");

  const auto ledger_bytes = [&] {
    std::uintmax_t total = 0;
    for (const char* suffix : {"", "-wal"}) {
      std::error_code ec;
      const auto size = std::filesystem::file_size(std::filesystem::path(impl_->path.string() + suffix), ec);
      if (!ec) total += size;
    }
    return total;
  };
  const auto bytes_before = ledger_bytes();

  // Selection and removal share one write transaction, so an operation cannot
  // change between being chosen and being folded.
  sql::Txn txn(db, !dry_run);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto operations_before, db.query_int("SELECT COUNT(*) FROM usage_operations"));
  struct Fold { std::string id, key, status, estimate_sha; };
  std::vector<Fold> folds;
  std::int64_t owner_confirmed = 0, status_not_foldable = 0, unreadable = 0;
  {
    LOOM_TRY_ASSIGN(auto st, db.prepare(
        "SELECT o.id,o.baseline_key,o.record FROM usage_operations o WHERE o.active=0 AND "
        "(SELECT MAX(e.recorded_at) FROM usage_events e WHERE e.operation_id=o.id)<? ORDER BY o.id"));
    st.bind_all(*cutoff);
    for (;;) {
      LOOM_TRY_ASSIGN(bool row, st.step());
      if (!row) break;
      auto record = decode_record(st.get_text(2));
      if (!record) { ++unreadable; continue; }  // a damaged record is left exactly as found
      const auto status = json::get_string(*record, "status");
      if (!foldable.contains(status)) { ++status_not_foldable; continue; }
      if (record->contains("confirmation")) { ++owner_confirmed; continue; }  // the owner's decision stays
      folds.push_back({st.get_text(0), st.get_text(1), status, Sha256::hex(json::canonical((*record)["estimate"]))});
    }
  }
  std::set<std::string> cohorts;
  for (const auto& fold : folds) cohorts.insert(fold.key);

  struct Aggregate { std::int64_t samples = 0; long double sum = 0; double min = 0, max = 0; };
  std::map<std::pair<std::string, std::string>, Aggregate> summed;
  std::vector<std::int64_t> sample_rows;
  if (!dry_run) {
    for (const auto& fold : folds) {
      LOOM_TRY(db.run("INSERT OR IGNORE INTO usage_folded_operations(id,baseline_key,status,estimate_sha256,folded_at) "
                      "VALUES(?,?,?,?,datetime('now'))", fold.id, fold.key, fold.status, fold.estimate_sha));
      LOOM_TRY(db.run("DELETE FROM usage_events WHERE operation_id=?", fold.id));
      LOOM_TRY(db.run("DELETE FROM usage_operations WHERE id=?", fold.id));
    }
  }
  if (fold_samples) {
    // The newest `window` measurements of a cohort and resource are what the
    // baseline reads; older ones of folded operations are summed into one row.
    // In a dry run the operations are not yet tombstoned, so the folds count.
    LOOM_TRY_ASSIGN(auto st, db.prepare(
        "SELECT sequence,operation_id,baseline_key,resource,value FROM ("
        " SELECT s.sequence,s.operation_id,s.baseline_key,s.resource,s.value,"
        "  ROW_NUMBER() OVER (PARTITION BY s.baseline_key,s.resource ORDER BY s.sequence DESC) AS position"
        " FROM usage_samples s) WHERE position>?"));
    st.bind_all(window);
    std::set<std::string> folding;
    if (dry_run) for (const auto& fold : folds) folding.insert(fold.id);
    LOOM_TRY_ASSIGN(auto tombstone, db.prepare("SELECT 1 FROM usage_folded_operations WHERE id=?"));
    for (;;) {
      LOOM_TRY_ASSIGN(bool row, st.step());
      if (!row) break;
      const std::string operation = st.get_text(1);
      bool folded_operation = folding.contains(operation);
      if (!folded_operation) {
        tombstone.reset();
        tombstone.bind_all(operation);
        LOOM_TRY_ASSIGN(folded_operation, tombstone.step());
      }
      if (!folded_operation) continue;
      auto& aggregate = summed[{st.get_text(2), st.get_text(3)}];
      const double value = st.get_double(4);
      if (aggregate.samples == 0) aggregate.min = aggregate.max = value;
      aggregate.min = std::min(aggregate.min, value);
      aggregate.max = std::max(aggregate.max, value);
      aggregate.sum += static_cast<long double>(value);
      ++aggregate.samples;
      sample_rows.push_back(st.get_int(0));
    }
  }
  std::int64_t samples_folded = static_cast<std::int64_t>(sample_rows.size());
  if (!dry_run) {
    for (const auto& [group, aggregate] : summed) {
      std::int64_t samples = aggregate.samples;
      long double total = aggregate.sum;
      double low = aggregate.min, high = aggregate.max;
      LOOM_TRY_ASSIGN(auto existing, db.prepare("SELECT samples,sample_sum,sample_min,sample_max FROM usage_fold_samples WHERE baseline_key=? AND resource=?"));
      existing.bind_all(group.first, group.second);
      LOOM_TRY_ASSIGN(bool had, existing.step());
      if (had) {
        samples += existing.get_int(0);
        total += static_cast<long double>(existing.get_double(1));
        low = std::min(low, existing.get_double(2));
        high = std::max(high, existing.get_double(3));
      }
      LOOM_TRY_ASSIGN(double narrowed, narrow(total));
      LOOM_TRY(db.run("INSERT OR REPLACE INTO usage_fold_samples(baseline_key,resource,samples,sample_sum,sample_min,sample_max) VALUES(?,?,?,?,?,?)",
                      group.first, group.second, samples, narrowed, low, high));
    }
    for (const auto sequence : sample_rows) LOOM_TRY(db.run("DELETE FROM usage_samples WHERE sequence=?", sequence));
    LOOM_TRY(db.run("INSERT INTO usage_compaction_state(id,last_run_at) VALUES(1,datetime('now')) "
                    "ON CONFLICT(id) DO UPDATE SET last_run_at=excluded.last_run_at"));
  }
  const std::int64_t before_count = operations_before.value_or(0);
  LOOM_TRY(txn.commit());

  bool reclaimed = false;
  Json reclaim_error = nullptr;
  if (!dry_run && reclaim && (!folds.empty() || samples_folded)) {
    // The freed pages only leave the file with a VACUUM, and the WAL with a truncating checkpoint.
    for (const char* statement : {"PRAGMA wal_checkpoint(TRUNCATE);", "VACUUM;", "PRAGMA wal_checkpoint(TRUNCATE);"}) {
      if (auto done = db.exec(statement); !done) {
        reclaim_error = done.error().message;
        break;
      }
    }
    reclaimed = reclaim_error.is_null();
  }
  return Json{{"schema", "loom.usage_compaction/1"},
              {"dry_run", dry_run},
              {"as_of", reference ? Json(*reference) : Json(nullptr)},
              {"cutoff", *cutoff},
              {"settings", settings},
              {"operations_before", before_count},
              {"operations_after", before_count - (dry_run ? 0 : static_cast<std::int64_t>(folds.size()))},
              {"folded_operations", folds.size()},
              {"folded_samples", samples_folded},
              {"cohorts", cohorts.size()},
              {"retained", Json{{"owner_confirmed", owner_confirmed}, {"status_not_foldable", status_not_foldable},
                                {"unreadable", unreadable}}},
              {"bytes_before", bytes_before},
              {"bytes_after", ledger_bytes()},
              {"space_reclaimed", reclaimed},
              {"reclaim_error", reclaim_error}};
}
}  // namespace loom
