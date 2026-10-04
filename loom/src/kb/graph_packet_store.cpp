#include "loom/graph_packet_store.h"

#include <cmath>
#include <map>
#include <set>

#include "loom/sqlite.h"
#include "loom/util/sha256.h"

namespace loom::kb {
namespace {

using Index = std::map<std::string, Json>;
using Indices = std::map<std::string, Index>;
const char* const kCollections[] = {"entities", "claims", "sources"};

Error invalid(std::string message) { return Error(Errc::InvalidArgument, std::move(message)); }
std::string hash(const Json& value) { return Sha256::hex(json::canonical(value)); }

Status keys(const Json& value, std::initializer_list<const char*> names) {
  if (!value.is_object() || value.size() != names.size()) return invalid("graph store: unexpected object fields");
  for (const char* name : names) if (!value.contains(name)) return invalid(std::string("graph store: missing ") + name);
  return {};
}

Result<std::string> identity(const Json& value, const char* field) {
  if (!value.contains(field) || !value[field].is_string() || value[field].get_ref<const std::string&>().empty())
    return invalid(std::string("graph store: nonempty ") + field + " required");
  return value[field].get<std::string>();
}

Result<Indices> index_packet(const Json& packet) {
  LOOM_TRY(keys(packet, {"schema", "packet_id", "definitions", "entities", "claims", "sources", "task", "provenance", "history"}));
  if (packet["schema"] != "loom.graph_packet/1" || !packet["history"].is_array() || !packet["task"].is_object())
    return invalid("graph store: incompatible packet");
  Json payload = packet;
  payload.erase("packet_id");
  if (packet["packet_id"] != hash(payload)) return invalid("graph store: packet identity drift");
  LOOM_TRY(keys(packet["provenance"], {"definitions", "entities", "claims", "sources"}));
  Indices result;
  std::set<std::string> global_ids;
  for (const char* name : {"definitions", "entities", "claims", "sources"}) {
    if (!packet[name].is_array() || !packet["provenance"][name].is_object())
      return invalid("graph store: collection or provenance is not an array/object");
    auto& index = result[name];
    for (const auto& record : packet[name]) {
      const Json* native = &record;
      if (std::string_view(name) == "sources") {
        LOOM_TRY(keys(record, {"observation", "known_at", "text_sha256"}));
        native = &record["observation"];
      }
      LOOM_TRY_ASSIGN(auto id, identity(*native, "id"));
      if (!global_ids.insert(id).second) return invalid("graph store: colliding record identity");
      if (!packet["provenance"][name].contains(id)) return invalid("graph store: missing record provenance");
      const auto& provenance = packet["provenance"][name][id];
      LOOM_TRY(keys(provenance, {"known_at", "origin", "record_sha256"}));
      if (provenance["record_sha256"] != hash(record)) return invalid("graph store: record provenance hash drift");
      index.emplace(id, record);
    }
    if (packet["provenance"][name].size() != index.size()) return invalid("graph store: extra record provenance");
  }
  return result;
}

Result<Indices> select_rows(const Json& request, const Indices& packet) {
  LOOM_TRY(keys(request["selection"], {"entities", "claims", "sources"}));
  LOOM_TRY(keys(request["expected_rows"], {"entities", "claims", "sources"}));
  Indices selected;
  std::size_t count = 0;
  for (const char* name : kCollections) {
    const auto& ids = request["selection"][name];
    const auto& expected = request["expected_rows"][name];
    if (!ids.is_array() || !expected.is_object()) return invalid("graph store: selection/CAS shape invalid");
    auto& rows = selected[name];
    for (const auto& id : ids) {
      if (!id.is_string()) return invalid("graph store: selection identity must be a string");
      const auto text = id.get<std::string>();
      auto found = packet.at(name).find(text);
      if (found == packet.at(name).end() || !rows.emplace(text, found->second).second)
        return invalid("graph store: absent or duplicate selected identity");
      if (!expected.contains(text) || !(expected[text].is_null() || expected[text].is_string()))
        return invalid("graph store: explicit row CAS required");
    }
    if (expected.size() != rows.size()) return invalid("graph store: extra CAS identity");
    count += rows.size();
  }
  if (!count) return invalid("graph store: empty selection");
  return selected;
}

struct Rows {
  std::vector<model::Observation> observations;
  std::vector<model::Entity> entities;
  std::vector<model::Claim> claims;
};

bool same_json_number(const Json& left, const Json& right) {
  if (left.is_number_float() && right.is_number_float())
    return left.get<double>() == right.get<double>();
  if (left.is_number_float() || right.is_number_float()) {
    const auto& integer = left.is_number_float() ? right : left;
    const double floating = (left.is_number_float() ? left : right).get<double>();
    if (!std::isfinite(floating) || std::trunc(floating) != floating) return false;
    // Half-open powers-of-two bounds are exactly representable in double.
    // Check before conversion: a rounded integer must not compare equal to
    // the original value, and out-of-range casts must never be attempted.
    if (integer.is_number_unsigned()) {
      if (floating < 0.0 || floating >= std::ldexp(1.0, 64)) return false;
      return static_cast<std::uint64_t>(floating) == integer.get<std::uint64_t>();
    }
    if (floating < -std::ldexp(1.0, 63) || floating >= std::ldexp(1.0, 63)) return false;
    return static_cast<std::int64_t>(floating) == integer.get<std::int64_t>();
  }
  if (left.is_number_unsigned() && right.is_number_unsigned())
    return left.get<std::uint64_t>() == right.get<std::uint64_t>();
  if (!left.is_number_unsigned() && !right.is_number_unsigned())
    return left.get<std::int64_t>() == right.get<std::int64_t>();
  const auto& signed_number = left.is_number_unsigned() ? right : left;
  const auto& unsigned_number = left.is_number_unsigned() ? left : right;
  const auto value = signed_number.get<std::int64_t>();
  return value >= 0 && static_cast<std::uint64_t>(value) == unsigned_number.get<std::uint64_t>();
}

bool same_json_value(const Json& left, const Json& right) {
  if (left.is_object() && right.is_object()) {
    if (left.size() != right.size()) return false;
    for (auto field = left.begin(); field != left.end(); ++field) {
      auto other = right.find(field.key());
      if (other == right.end() || !same_json_value(field.value(), other.value())) return false;
    }
    return true;
  }
  if (left.is_array() && right.is_array()) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index)
      if (!same_json_value(left[index], right[index])) return false;
    return true;
  }
  if (left.is_number() && right.is_number()) return same_json_number(left, right);
  // Array order and nonnumeric scalar values remain meaningful.
  return left == right;
}

Status exact_projection(const Json& input, const Json& output) {
  // Native serializers may normalize numeric representation (1 -> 1.0),
  // but silently discarded fields or changes to semantic content are rejected.
  if (!same_json_value(input, output)) return invalid("graph store: native row projection would discard or change fields");
  return {};
}

Status closed(const Index& index, const std::string& id, const char* kind) {
  if (!id.empty() && !index.count(id)) return invalid(std::string("graph store: selection omits referenced ") + kind + " " + id);
  return {};
}

Status required_reference(const Index& index, const std::string& id, const char* kind) {
  if (id.empty()) return invalid(std::string("graph store: empty ") + kind + " reference");
  return closed(index, id, kind);
}

Result<Rows> validate_rows(const Indices& selected) {
  Rows rows;
  const auto& sources = selected.at("sources");
  const auto& entities = selected.at("entities");
  const auto& claims = selected.at("claims");
  for (const auto& [id, source] : sources) {
    LOOM_TRY_ASSIGN(auto observation, model::Observation::from_json(source["observation"]));
    LOOM_TRY(exact_projection(source["observation"], observation.to_json()));
    if (observation.unit.empty() || observation.locator.source.empty() ||
        (observation.locator.line && *observation.locator.line < 0) ||
        source["text_sha256"] != Sha256::hex(observation.text)) return invalid("graph store: source identity/hash invalid");
    rows.observations.push_back(std::move(observation));
  }
  for (const auto& [id, source] : entities) {
    LOOM_TRY_ASSIGN(auto entity, model::Entity::from_json(source));
    LOOM_TRY(exact_projection(source, entity.to_json()));
    LOOM_TRY(closed(entities, entity.parent, "entity parent"));
    rows.entities.push_back(std::move(entity));
  }
  for (const auto& [id, source] : claims) {
    LOOM_TRY_ASSIGN(auto claim, model::Claim::from_json(source));
    LOOM_TRY(exact_projection(source, claim.to_json()));
    LOOM_TRY(claim.validate());
    LOOM_TRY(closed(entities, claim.subject, "claim subject"));
    LOOM_TRY(closed(entities, claim.object, "claim object"));
    for (const auto& ref : claim.assessment.premises.claims) LOOM_TRY(required_reference(claims, ref, "premise"));
    for (const auto& ref : claim.assessment.counter.claims) LOOM_TRY(required_reference(claims, ref, "counter claim"));
    for (const auto& ref : claim.assessment.consequences.claims) LOOM_TRY(required_reference(claims, ref, "consequence"));
    for (const auto& ref : claim.assessment.counter.observations) LOOM_TRY(required_reference(sources, ref, "counter observation"));
    for (const auto& support : claim.assessment.support) {
      LOOM_TRY(closed(sources, support.observation, "support observation"));
      const auto& original = sources.at(support.observation)["observation"];
      const std::string text = original["text"].get<std::string>();
      if (support.quote.empty() || text.find(support.quote) == std::string::npos)
        return invalid("graph store: support quote mismatch");
      const Json located = support.locator.to_json();
      const auto& locator = original["locator"];
      if (!same_json_value(located, locator)) {
        for (const char* field : {"source", "member", "json_pointer", "time_start", "time_end"})
          if (!same_json_value(located[field], locator[field])) return invalid("graph store: support locator mismatch");
        if (!locator["byte_start"].is_number_integer() || !support.locator.byte_start || !support.locator.byte_len)
          return invalid("graph store: support subspan unverifiable");
        const auto base = locator["byte_start"].get<std::int64_t>();
        const auto start = *support.locator.byte_start;
        const auto length = *support.locator.byte_len;
        if (start < base || length < 0 || static_cast<std::uint64_t>(start - base) > text.size() ||
            static_cast<std::uint64_t>(length) > text.size() - static_cast<std::size_t>(start - base) ||
            text.substr(static_cast<std::size_t>(start - base), static_cast<std::size_t>(length)) != support.quote)
          return invalid("graph store: UTF-8 support subspan quote mismatch");
      }
    }
    rows.claims.push_back(std::move(claim));
  }
  return rows;
}

const char* table(const std::string& collection) {
  if (collection == "sources") return "loom_kb_observations";
  if (collection == "entities") return "loom_kb_entities";
  return "loom_kb_claims";
}

Result<Json> sql_rows(sql::Connection& connection, const std::string& query, const std::string& run,
                      const std::string& id) {
  LOOM_TRY_ASSIGN(auto statement, connection.prepare(query));
  statement.bind_all(run, id);
  Json output = Json::array();
  while (true) {
    LOOM_TRY_ASSIGN(bool present, statement.step());
    if (!present) break;
    Json row = Json::object();
    for (int i = 0; i < statement.column_count(); ++i) {
      const auto name = statement.column_name(i);
      switch (statement.column_type(i)) {
        case sql::Type::Null: row[name] = nullptr; break;
        case sql::Type::Integer: row[name] = statement.get_int(i); break;
        case sql::Type::Float: row[name] = statement.get_double(i); break;
        case sql::Type::Blob: return Error(Errc::Database, "graph store: unexpected binary knowledge column");
        case sql::Type::Text: row[name] = statement.get_text(i); break;
      }
    }
    output.push_back(std::move(row));
  }
  return output;
}

Result<Json> snapshot(Database& db, const std::string& run, const std::string& collection, const std::string& id) {
  Json result;
  LOOM_TRY_ASSIGN(result["row"], sql_rows(db.conn(), std::string("SELECT * FROM ") + table(collection) +
      " WHERE run_id = ? AND id = ?", run, id));
  result["related"] = Json::array();
  if (collection == "entities") {
    LOOM_TRY_ASSIGN(result["related"], sql_rows(db.conn(), "SELECT * FROM loom_kb_aliases WHERE run_id = ? "
        "AND entity_id = ? ORDER BY alias_key", run, id));
  } else if (collection == "claims") {
    LOOM_TRY_ASSIGN(result["related"], sql_rows(db.conn(), "SELECT * FROM loom_kb_claim_support WHERE run_id = ? "
        "AND claim_id = ? ORDER BY observation_id", run, id));
  }
  return result;
}

Result<Json> stored_body(Database& db, const std::string& run, const std::string& collection, const std::string& id) {
  LOOM_TRY_ASSIGN(auto text, db.conn().query_text(std::string("SELECT body FROM ") + table(collection) +
      " WHERE run_id = ? AND id = ?", run, id));
  if (!text) return Json();
  LOOM_TRY_ASSIGN(auto body, json::parse(*text));
  return body;
}

Result<Json> drift(Database& db, const Json& receipt) {
  Json changed = Json::array();
  Json snapshots = Json::object();
  const auto run = receipt["run_id"].get<std::string>();
  for (const char* name : kCollections) {
    snapshots[name] = Json::object();
    for (auto it = receipt["row_snapshots"][name].begin(); it != receipt["row_snapshots"][name].end(); ++it) {
      LOOM_TRY_ASSIGN(auto current, snapshot(db, run, name, it.key()));
      if (current != it.value()) changed.push_back(Json{{"collection", name}, {"id", it.key()},
          {"reason", current["row"].empty() ? "missing" : "changed"}});
      snapshots[name][it.key()] = std::move(current);
    }
  }
  return Json{{"matches", changed.empty()}, {"rows", changed}, {"current_row_snapshots", snapshots}};
}

Status stable_record_identity(const std::string& collection, const Json& stored, const Json& selected) {
  if (stored.is_null()) return {};
  const Json& incoming = collection == "sources" ? selected["observation"] : selected;
  if (collection == "sources" && !same_json_value(stored, incoming))
    return invalid("graph store: immutable observation changes require a new record identity");
  const std::vector<const char*> fields = collection == "entities"
      ? std::vector<const char*>{"kind", "canonical_key"}
      : std::vector<const char*>{"subject", "predicate", "object", "value", "qualifiers"};
  if (collection != "sources") {
    for (const char* field : fields) {
      if (!same_json_value(stored[field], incoming[field])) return invalid("graph store: content changes require a new record identity");
    }
  }
  return {};
}

Json opaque_references(const Rows& rows) {
  Json references = Json::array();
  for (const auto& claim : rows.claims) {
    for (const auto& id : claim.assessment.premises.principles)
      references.push_back(Json{{"claim", claim.id}, {"kind", "principle"}, {"id", id}});
    for (const auto& id : claim.assessment.consequences.predictions)
      references.push_back(Json{{"claim", claim.id}, {"kind", "prediction"}, {"id", id}});
    for (const auto& id : claim.assessment.consequences.checks)
      references.push_back(Json{{"claim", claim.id}, {"kind", "pack_check"}, {"id", id}});
  }
  return references;
}

Result<Json> load_receipt(Database& db, const std::string& id) {
  if (!db.conn().has_table("loom_kb_graph_receipts")) return Error(Errc::NotFound, "graph receipt not found");
  LOOM_TRY_ASSIGN(auto text, db.conn().query_text("SELECT body FROM loom_kb_graph_receipts WHERE id = ?", id));
  if (!text) return Error(Errc::NotFound, "graph receipt not found");
  LOOM_TRY_ASSIGN(auto receipt, json::parse(*text));
  Json payload = receipt;
  payload.erase("receipt_sha256");
  if (receipt.value("receipt_sha256", Json()) != hash(payload)) return Error(Errc::Database, "graph receipt hash drift");
  return receipt;
}

}  // namespace

Result<Json> GraphPacketStore::execute(const Json& request) {
  LOOM_TRY_ASSIGN(auto operation, identity(request, "operation"));
  auto lock = db_.lock();
  if (operation == "read" || operation == "replay") {
    LOOM_TRY(keys(request, {"operation", "receipt_id"}));
    LOOM_TRY_ASSIGN(auto id, identity(request, "receipt_id"));
    // One read snapshot makes a drift verdict coherent across all row tables.
    sql::Txn transaction(db_.conn());
    LOOM_TRY(transaction.begin_status());
    LOOM_TRY_ASSIGN(auto receipt, load_receipt(db_, id));
    LOOM_TRY_ASSIGN(auto status, drift(db_, receipt));
    if (operation == "replay" && !status["matches"].get<bool>())
      return invalid("graph store: replay row drift: " + json::dump(status["rows"]));
    LOOM_TRY(transaction.commit());
    return Json{{"receipt", receipt}, {"row_drift", status}, {"replayed", operation == "replay"}};
  }
  if (operation != "accept") return invalid("graph store: unknown operation");
  LOOM_TRY(keys(request, {"operation", "target", "packet", "selection", "expected_rows", "explicitly_accepted"}));
  if (request["explicitly_accepted"] != Json(true)) return invalid("graph store: explicit acceptance required");
  LOOM_TRY_ASSIGN(auto target, identity(request, "target"));
  LOOM_TRY_ASSIGN(auto index, index_packet(request["packet"]));
  LOOM_TRY_ASSIGN(auto selected, select_rows(request, index));
  LOOM_TRY_ASSIGN(auto rows, validate_rows(selected));
  Json input = request;
  input.erase("operation");
  const std::string request_hash = hash(input);
  const std::string receipt_id = "gpr_" + request_hash;
  sql::Txn transaction(db_.conn());
  LOOM_TRY(transaction.begin_status());
  LOOM_TRY(store_.ensure_schema());
  LOOM_TRY_ASSIGN(auto existing, db_.conn().query_text("SELECT body FROM loom_kb_graph_receipts WHERE id = ?", receipt_id));
  if (existing) {
    LOOM_TRY_ASSIGN(auto receipt, load_receipt(db_, receipt_id));
    LOOM_TRY_ASSIGN(auto status, drift(db_, receipt));
    if (!status["matches"].get<bool>()) return invalid("graph store: accepted request retry has row drift");
    LOOM_TRY(transaction.commit());
    return Json{{"receipt", receipt}, {"row_drift", status}, {"replayed", true}};
  }
  LOOM_TRY_ASSIGN(auto run, store_.begin_run("loom.graph_packet_store/1", Json{{"graph_packet_target", target}}));
  for (const char* name : kCollections) {
    for (const auto& [id, selected_record] : selected[name]) {
      LOOM_TRY_ASSIGN(auto current, stored_body(db_, run.id, name, id));
      const Json actual = current.is_null() ? Json() : Json(hash(current));
      if (request["expected_rows"][name][id] != actual) return invalid("graph store: row compare-and-swap failed for " + id);
      LOOM_TRY(stable_record_identity(name, current, selected_record));
    }
  }
  LOOM_TRY(store_.put_observations(run.id, rows.observations));
  LOOM_TRY(store_.put_entities(run.id, rows.entities));
  LOOM_TRY(store_.put_claims(run.id, rows.claims));
  // The existing owner's append-only judgement log is authoritative over an
  // accepted exchange packet. Reapply even previously replayed judgements so
  // a later CAS acceptance cannot overwrite an already confirmed/rejected row.
  LOOM_TRY(db_.conn().run("UPDATE loom_kb_runs SET replayed_seq = 0 WHERE run_id = ?", run.id));
  LOOM_TRY_ASSIGN(auto judgement_report, store_.replay_judgements(run.id));
  Json receipt{{"schema", "loom.graph_packet_store_receipt/1"}, {"id", receipt_id},
      {"request_sha256", request_hash}, {"target", target}, {"run_id", run.id},
      {"packet", request["packet"]}, {"selection", request["selection"]},
      {"expected_rows", request["expected_rows"]}, {"stored_row_sha256", Json::object()},
      {"row_snapshots", Json::object()}, {"explicitly_accepted", true},
      {"acceptance_establishes_content_truth", false},
      {"native_validation_scope", "packet_head_and_provenance_hashes_selected_native_rows_entity_claim_observation_reference_closure_source_hashes_quotes_utf8_subspans"},
      {"opaque_native_references", opaque_references(rows)},
      {"opaque_native_reference_validation", "principle_prediction_and_pack_check_ids_preserved_but_not_resolved_by_this_adapter"},
      {"judgement_replay", judgement_report.to_json()},
      {"reversible_history_validation", "not_performed_by_native_adapter_use_graph_packet_codec"},
      {"source_authenticity", "not_established_by_internal_content_hashes"}};
  for (const char* name : kCollections) {
    receipt["row_snapshots"][name] = Json::object();
    receipt["stored_row_sha256"][name] = Json::object();
    for (const auto& [id, unused] : selected[name]) {
      LOOM_TRY_ASSIGN(receipt["row_snapshots"][name][id], snapshot(db_, run.id, name, id));
      LOOM_TRY_ASSIGN(auto body, stored_body(db_, run.id, name, id));
      receipt["stored_row_sha256"][name][id] = hash(body);
    }
  }
  receipt["receipt_sha256"] = hash(receipt);
  LOOM_TRY(db_.conn().run("INSERT INTO loom_kb_graph_receipts (id, run_id, body) VALUES (?, ?, ?)",
      receipt_id, run.id, json::dump(receipt)));
  LOOM_TRY(store_.finish_run(run.id, "done", Json{{"graph_packet_receipt", receipt_id}}));
  LOOM_TRY(transaction.commit());
  return Json{{"receipt", receipt}, {"row_drift", Json{{"matches", true}, {"rows", Json::array()},
      {"current_row_snapshots", receipt["row_snapshots"]}}}, {"replayed", false}};
}

}  // namespace loom::kb
