// knowledge_store.h: KnowledgeStore — model objects in loom_kb_* tables
// (canonical JSON body + indexed columns), runs, judgements and replay.
#include "loom/knowledge_store.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <variant>

#include "loom/db.h"
#include "loom/sqlite.h"
#include "loom/util/time.h"

namespace loom::kb {

using namespace loom::model;

namespace {

using Val = std::variant<std::string, double, std::int64_t>;
using Cols = std::vector<std::pair<const char*, Val>>;

const char* const kRunTables[] = {"loom_kb_observations", "loom_kb_entities",     "loom_kb_aliases",
                                  "loom_kb_claims",       "loom_kb_claim_support", "loom_kb_principles",
                                  "loom_kb_operators",    "loom_kb_morphisms",    "loom_kb_instances",
                                  "loom_kb_slot_values",  "loom_kb_areas",        "loom_kb_decisions",
                                  "loom_kb_forks",        "loom_kb_status_records", "loom_kb_predictions",
                                  "loom_kb_models",       "loom_kb_products"};

void bind_val(sql::Stmt& st, int i, const Val& v) {
  if (const auto* s = std::get_if<std::string>(&v)) {
    st.bind(i, *s);
  } else if (const auto* d = std::get_if<double>(&v)) {
    st.bind(i, *d);
  } else {
    st.bind(i, std::get<std::int64_t>(v));
  }
}

Status upsert(sql::Connection& c, const char* table, const Cols& cols) {
  std::string names, qs;
  for (const auto& [n, _] : cols) {
    if (!names.empty()) {
      names += ", ";
      qs += ", ";
    }
    names += n;
    qs += "?";
  }
  LOOM_TRY_ASSIGN(sql::Stmt st, c.prepare(std::string("INSERT OR REPLACE INTO ") + table + " (" + names + ") VALUES (" + qs + ")"));
  int i = 1;
  for (const auto& [_, v] : cols) bind_val(st, i++, v);
  return st.run();
}

std::string body_of(const Json& j) { return json::dump(j); }

// SELECT body FROM table WHERE <where> ORDER BY <order> LIMIT ?
template <class T>
Result<std::vector<T>> select_bodies(sql::Connection& c, const std::string& table, const std::string& where,
                                     const std::vector<std::string>& binds, const std::string& order, int limit,
                                     const char* body_col = "body") {
  std::string q = "SELECT " + std::string(body_col) + " FROM " + table + (where.empty() ? "" : " WHERE " + where) +
                  " ORDER BY " + order + " LIMIT ?";
  LOOM_TRY_ASSIGN(sql::Stmt st, c.prepare(q));
  int i = 1;
  for (const auto& b : binds) st.bind(i++, b);
  st.bind(i, static_cast<std::int64_t>(limit <= 0 ? 1000000 : limit));
  std::vector<T> out;
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    auto j = json::parse(st.get_text(0));
    if (!j) return Error(Errc::Database, table + ": corrupt body: " + j.error().message);
    auto v = T::from_json(*j);
    if (!v) return Error(Errc::Database, table + ": invalid body: " + v.error().message);
    out.push_back(std::move(*v));
  }
  return out;
}

template <class T>
Result<std::optional<T>> select_one(sql::Connection& c, const std::string& table, std::string_view run,
                                    std::string_view id) {
  LOOM_TRY_ASSIGN(auto v, select_bodies<T>(c, table, "run_id = ? AND id = ?", {std::string(run), std::string(id)}, "id", 1));
  if (v.empty()) return std::optional<T>{};
  return std::optional<T>(std::move(v.front()));
}

// Where-clause builder.
struct Where {
  std::string sql;
  std::vector<std::string> binds;
  void add(const std::string& clause, const std::string& bind) {
    sql += (sql.empty() ? "" : " AND ") + clause;
    binds.push_back(bind);
  }
};

std::string en_s(auto e) { return std::string(to_string(e)); }

Status check_run(std::string_view run) {
  if (run.empty()) return Error(Errc::InvalidArgument, "run id is required");
  return {};
}

}  // namespace

// ── KnowledgeRun / rows ─────────────────────────────────────────────
std::string KnowledgeRun::make_id(std::string_view pack_hash, const Json& inputs) {
  return stable_id("kr_", std::string(pack_hash) + '\x1f' + json::canonical(inputs));
}
Json KnowledgeRun::to_json() const {
  return Json{{"id", id},       {"archive_run_id", archive_run_id}, {"pack_hash", pack_hash}, {"status", status},
              {"inputs", inputs}, {"summary", summary},           {"created", created}};
}
Json SlotRow::to_json() const {
  Json j{{"instance", instance}};
  Json v = value.to_json();
  for (auto it = v.begin(); it != v.end(); ++it) j[it.key()] = it.value();
  return j;
}
Json ReplayReport::to_json() const { return Json{{"applied", applied}, {"skipped", skipped}, {"details", details}}; }

KnowledgeStore::KnowledgeStore(Database& db) : db_(db) {}

Status KnowledgeStore::ensure_schema() { return kb::ensure_schema(db_); }
bool KnowledgeStore::has_schema() { return kb::has_schema(db_); }

// ── runs ────────────────────────────────────────────────────────────
Result<KnowledgeRun> KnowledgeStore::begin_run(std::string_view pack_hash, const Json& inputs,
                                               std::string_view archive_run_id) {
  LOOM_TRY(ensure_schema());
  KnowledgeRun r;
  r.id = KnowledgeRun::make_id(pack_hash, inputs);
  LOOM_TRY_ASSIGN(auto existing, get_run(r.id));
  if (existing) return *existing;
  r.pack_hash = pack_hash;
  r.inputs = inputs;
  r.archive_run_id = archive_run_id;
  r.created = timeutil::utc_now_iso();
  auto lk = db_.lock();
  LOOM_TRY(db_.conn().run(
      "INSERT OR IGNORE INTO loom_kb_runs (run_id, archive_run_id, pack_hash, status, inputs, summary, created) "
      "VALUES (?, ?, ?, ?, ?, ?, ?)",
      r.id, r.archive_run_id, r.pack_hash, r.status, json::dump(r.inputs), json::dump(r.summary), r.created));
  return r;
}

Status KnowledgeStore::finish_run(std::string_view run_id, std::string_view status, const Json& summary) {
  LOOM_TRY(ensure_schema());
  auto lk = db_.lock();
  LOOM_TRY(db_.conn().run("UPDATE loom_kb_runs SET status = ?, summary = ? WHERE run_id = ?", status,
                          json::dump(summary), run_id));
  if (db_.conn().changes() == 0) return Error(Errc::NotFound, "no knowledge run " + std::string(run_id));
  return {};
}

Result<std::optional<KnowledgeRun>> KnowledgeStore::get_run(std::string_view run_id) {
  if (!has_schema()) return std::optional<KnowledgeRun>{};
  auto lk = db_.lock();
  LOOM_TRY_ASSIGN(sql::Stmt st, db_.conn().prepare("SELECT run_id, archive_run_id, pack_hash, status, inputs, summary, "
                                                   "created FROM loom_kb_runs WHERE run_id = ?"));
  st.bind(1, run_id);
  LOOM_TRY_ASSIGN(bool row, st.step());
  if (!row) return std::optional<KnowledgeRun>{};
  KnowledgeRun r;
  r.id = st.get_text(0);
  r.archive_run_id = st.get_text(1);
  r.pack_hash = st.get_text(2);
  r.status = st.get_text(3);
  r.inputs = json::parse_or(st.get_text(4), Json::object());
  r.summary = json::parse_or(st.get_text(5), Json::object());
  r.created = st.get_text(6);
  return std::optional<KnowledgeRun>(std::move(r));
}

Result<std::vector<KnowledgeRun>> KnowledgeStore::list_runs(int limit, std::string_view status) {
  std::vector<KnowledgeRun> out;
  if (!has_schema()) return out;
  std::vector<std::string> ids;
  {
    auto lk = db_.lock();
    std::string query = "SELECT run_id FROM loom_kb_runs";
    if (!status.empty()) query += " WHERE status = ?";
    query += " ORDER BY created DESC, run_id LIMIT ?";
    LOOM_TRY_ASSIGN(sql::Stmt st, db_.conn().prepare(query));
    int index = 1;
    if (!status.empty()) st.bind(index++, status);
    st.bind(index, static_cast<std::int64_t>(limit <= 0 ? 50 : limit));
    while (true) {
      LOOM_TRY_ASSIGN(bool row, st.step());
      if (!row) break;
      ids.push_back(st.get_text(0));
    }
  }
  for (const auto& id : ids) {
    LOOM_TRY_ASSIGN(auto r, get_run(id));
    if (r) out.push_back(std::move(*r));
  }
  return out;
}

Status KnowledgeStore::clear_run(std::string_view run_id) {
  LOOM_TRY(check_run(run_id));
  if (!has_schema()) return {};
  auto lk = db_.lock();
  sql::Txn txn(db_.conn());
  LOOM_TRY(txn.begin_status());
  for (const char* t : kRunTables) LOOM_TRY(db_.conn().run(std::string("DELETE FROM ") + t + " WHERE run_id = ?", run_id));
  LOOM_TRY(db_.conn().run("UPDATE loom_kb_runs SET replayed_seq = 0 WHERE run_id = ?", run_id));
  return txn.commit();
}

// ── writes ──────────────────────────────────────────────────────────
namespace {

// Runs `fn(conn)` in one transaction after ensure_schema.
template <class F>
Status in_txn(Database& db, F&& fn) {
  LOOM_TRY(kb::ensure_schema(db));
  auto lk = db.lock();
  sql::Txn txn(db.conn());
  LOOM_TRY(txn.begin_status());
  LOOM_TRY(fn(db.conn()));
  return txn.commit();
}

Status write_claim(sql::Connection& c, std::string_view run, const Claim& x) {
  const Assessment& a = x.assessment;
  LOOM_TRY(upsert(c, "loom_kb_claims",
                  {{"run_id", std::string(run)},
                   {"id", x.id},
                   {"subject", x.subject},
                   {"predicate", x.predicate},
                   {"object", x.object},
                   {"evidence", en_s(a.evidence)},
                   {"origin", en_s(a.origin)},
                   {"status", en_s(a.status)},
                   {"confidence", a.confidence},
                   {"branch", x.qualifiers.branch},
                   {"version", x.qualifiers.version},
                   {"body", body_of(x.to_json())}}));
  LOOM_TRY(c.run("DELETE FROM loom_kb_claim_support WHERE run_id = ? AND claim_id = ?", run, x.id));
  std::set<std::string> obs;
  for (const auto& s : a.support) obs.insert(s.observation);
  for (const auto& o : obs) {
    LOOM_TRY(c.run("INSERT OR IGNORE INTO loom_kb_claim_support (run_id, claim_id, observation_id) VALUES (?, ?, ?)",
                   run, x.id, o));
  }
  return {};
}

Status write_entity(sql::Connection& c, std::string_view run, const Entity& x) {
  LOOM_TRY(upsert(c, "loom_kb_entities",
                  {{"run_id", std::string(run)},
                   {"id", x.id},
                   {"kind", x.kind},
                   {"canonical_key", x.canonical_key},
                   {"parent", x.parent},
                   {"status", en_s(x.status)},
                   {"body", body_of(x.to_json())}}));
  LOOM_TRY(c.run("DELETE FROM loom_kb_aliases WHERE run_id = ? AND entity_id = ?", run, x.id));
  for (const auto& al : x.aliases) {
    LOOM_TRY(upsert(c, "loom_kb_aliases",
                    {{"run_id", std::string(run)},
                     {"entity_id", x.id},
                     {"alias_key", al.key},
                     {"surface", al.surface},
                     {"lang", al.lang},
                     {"method", al.method},
                     {"count", static_cast<std::int64_t>(al.count)},
                     {"confidence", al.confidence}}));
  }
  return {};
}

Status write_instance(sql::Connection& c, std::string_view run, const Instance& x) {
  LOOM_TRY(upsert(c, "loom_kb_instances",
                  {{"run_id", std::string(run)},
                   {"id", x.id},
                   {"paradigm_kind", en_s(x.paradigm_kind)},
                   {"paradigm", x.paradigm},
                   {"subject", x.subject},
                   {"model", x.model},
                   {"body", body_of(x.to_json())}}));
  LOOM_TRY(c.run("DELETE FROM loom_kb_slot_values WHERE run_id = ? AND instance_id = ?", run, x.id));
  for (const auto& s : x.slots) {
    LOOM_TRY(upsert(c, "loom_kb_slot_values",
                    {{"run_id", std::string(run)},
                     {"instance_id", x.id},
                     {"slot", s.slot},
                     {"ord", static_cast<std::int64_t>(s.ord)},
                     {"claim_id", s.claim},
                     {"role", s.role ? en_s(*s.role) : std::string()},
                     {"conflict", static_cast<std::int64_t>(s.conflict ? 1 : 0)},
                     {"body", body_of(s.to_json())}}));
  }
  return {};
}

// A body-only row with extra indexed columns.
template <class T>
Status write_simple(sql::Connection& c, const char* table, std::string_view run, const T& x, Cols extra) {
  Cols cols = {{"run_id", std::string(run)}, {"id", x.id}};
  for (auto& e : extra) cols.push_back(std::move(e));
  cols.push_back({"body", body_of(x.to_json())});
  return upsert(c, table, cols);
}

Status require_id(const std::string& id, const char* what) {
  if (id.empty()) return Error(Errc::InvalidArgument, std::string(what) + ": id is required");
  return {};
}

}  // namespace

Status KnowledgeStore::put_observations(std::string_view run, const std::vector<Observation>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) LOOM_TRY(require_id(x.id, "observation"));
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) {
      LOOM_TRY(write_simple(c, "loom_kb_observations", run, x,
                            {{"unit", x.unit}, {"kind", en_s(x.kind)}, {"date", x.date}}));
    }
    return {};
  });
}

Status KnowledgeStore::put_entities(std::string_view run, const std::vector<Entity>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) {
    LOOM_TRY(require_id(x.id, "entity"));
    if (x.kind.empty() || x.canonical_key.empty()) {
      return Error(Errc::InvalidArgument, "entity " + x.id + ": kind and canonical_key are required");
    }
    if (!std::isfinite(x.confidence) || x.confidence < 0 || x.confidence > 1) {
      return Error(Errc::InvalidArgument, "entity " + x.id + ": confidence must be in [0, 1]");
    }
  }
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) LOOM_TRY(write_entity(c, run, x));
    return {};
  });
}

Status KnowledgeStore::put_claims(std::string_view run, const std::vector<Claim>& v) {
  LOOM_TRY(check_run(run));
  std::map<std::string, const Claim*> batch;
  for (const auto& x : v) {
    LOOM_TRY(require_id(x.id, "claim"));
    LOOM_TRY(x.validate());
    batch[x.id] = &x;
  }
  // I3 premise rules against the batch and the stored run.
  for (const auto& x : v) {
    for (const auto& pid : x.assessment.premises.claims) {
      std::optional<Claim> stored;
      const Claim* p = nullptr;
      if (auto it = batch.find(pid); it != batch.end()) {
        p = it->second;
      } else {
        LOOM_TRY_ASSIGN(stored, get_claim(run, pid));
        if (stored) p = &*stored;
      }
      if (!p) continue;  // premise outside the run (e.g. filled later): checked on the next write
      if (!may_be_premise(p->assessment.evidence)) {
        return Error(Errc::InvalidArgument, "claim " + x.id + ": premise " + pid + " is " +
                                                std::string(to_string(p->assessment.evidence)) +
                                                " and may never be a premise (I3)");
      }
      bool x_transfer = x.assessment.derivation && !x.assessment.derivation->morphism.empty();
      bool p_transfer = p->assessment.derivation && !p->assessment.derivation->morphism.empty();
      if (x_transfer && p_transfer) {
        return Error(Errc::InvalidArgument, "claim " + x.id + ": transferred claims never chain (transfer depth 1, I3); premise " +
                                                pid + " was transferred");
      }
    }
  }
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) LOOM_TRY(write_claim(c, run, x));
    return {};
  });
}

Status KnowledgeStore::put_principles(std::string_view run, const std::vector<Principle>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) {
    LOOM_TRY(require_id(x.id, "principle"));
    if (x.statement.empty()) return Error(Errc::InvalidArgument, "principle " + x.id + ": statement is required");
  }
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) {
      LOOM_TRY(write_simple(c, "loom_kb_principles", run, x,
                            {{"level", en_s(x.level)},
                             {"form", en_s(x.form)},
                             {"validation", en_s(x.validation)},
                             {"owner", x.owner}}));
    }
    return {};
  });
}

Status KnowledgeStore::put_operators(std::string_view run, const std::vector<Operator>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) LOOM_TRY(x.validate());
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) {
      LOOM_TRY(write_simple(c, "loom_kb_operators", run, x,
                            {{"produces", x.produces ? en_s(*x.produces) : std::string()},
                             {"validation", en_s(x.validation)}}));
    }
    return {};
  });
}

Status KnowledgeStore::put_morphisms(std::string_view run, const std::vector<Morphism>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) {
    LOOM_TRY(require_id(x.id, "morphism"));
    LOOM_TRY(Morphism::from_json(x.to_json()));  // the same structural rules as the pack
  }
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) {
      LOOM_TRY(write_simple(c, "loom_kb_morphisms", run, x, {{"use", en_s(x.use)}, {"validation", en_s(x.validation)}}));
    }
    return {};
  });
}

Status KnowledgeStore::put_instances(std::string_view run, const std::vector<Instance>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) {
    LOOM_TRY(require_id(x.id, "instance"));
    std::set<std::pair<std::string, int>> seen;
    for (const auto& s : x.slots) {
      if (s.claim.empty()) return Error(Errc::InvalidArgument, "instance " + x.id + ": slot " + s.slot + " has no claim");
      if (!seen.insert({s.slot, s.ord}).second) {
        return Error(Errc::InvalidArgument, "instance " + x.id + ": duplicate slot value " + s.slot + "#" + std::to_string(s.ord));
      }
    }
  }
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) LOOM_TRY(write_instance(c, run, x));
    return {};
  });
}

Status KnowledgeStore::put_areas(std::string_view run, const std::vector<Area>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) LOOM_TRY(require_id(x.id, "area"));
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) LOOM_TRY(write_simple(c, "loom_kb_areas", run, x, {{"subject", x.subject}}));
    return {};
  });
}

Status KnowledgeStore::put_decisions(std::string_view run, const std::vector<Decision>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) LOOM_TRY(Decision::from_json(x.to_json()));
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) {
      LOOM_TRY(write_simple(c, "loom_kb_decisions", run, x,
                            {{"subject", x.subject}, {"status", en_s(x.status)}, {"date", x.date}}));
    }
    return {};
  });
}

Status KnowledgeStore::put_forks(std::string_view run, const std::vector<Fork>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) LOOM_TRY(Fork::from_json(x.to_json()));
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) {
      LOOM_TRY(write_simple(c, "loom_kb_forks", run, x, {{"kind", en_s(x.kind)}, {"subject", x.subject}}));
    }
    return {};
  });
}

Status KnowledgeStore::put_status_records(std::string_view run, const std::vector<StatusRecord>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) {
    LOOM_TRY(require_id(x.id, "status record"));
    if (x.entity.empty()) return Error(Errc::InvalidArgument, "status record " + x.id + ": entity is required");
  }
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) {
      LOOM_TRY(write_simple(c, "loom_kb_status_records", run, x,
                            {{"entity", x.entity}, {"branch", x.branch}, {"version", x.version}, {"status", en_s(x.status)}}));
    }
    return {};
  });
}

Status KnowledgeStore::put_predictions(std::string_view run, const std::vector<Prediction>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) LOOM_TRY(require_id(x.id, "prediction"));
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) {
      LOOM_TRY(write_simple(c, "loom_kb_predictions", run, x,
                            {{"operator", x.op}, {"cut", x.cut}, {"outcome", en_s(x.outcome)}}));
    }
    return {};
  });
}

Status KnowledgeStore::put_models(std::string_view run, const std::vector<Model>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) LOOM_TRY(require_id(x.id, "model"));
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) {
      LOOM_TRY(write_simple(c, "loom_kb_models", run, x, {{"name", x.name}, {"validation", en_s(x.validation)}}));
    }
    return {};
  });
}

Status KnowledgeStore::put_products(std::string_view run, const std::vector<Product>& v) {
  LOOM_TRY(check_run(run));
  for (const auto& x : v) LOOM_TRY(require_id(x.id, "product"));
  return in_txn(db_, [&](sql::Connection& c) -> Status {
    for (const auto& x : v) {
      LOOM_TRY(write_simple(c, "loom_kb_products", run, x, {{"kind", x.kind}, {"instance", x.instance}}));
    }
    return {};
  });
}

// ── point reads ─────────────────────────────────────────────────────
#define LOOM_KB_GET(Type, fn, table)                                                             \
  Result<std::optional<Type>> KnowledgeStore::fn(std::string_view run, std::string_view id) {    \
    if (!has_schema()) return std::optional<Type>{};                                             \
    auto lk = db_.lock();                                                                        \
    return select_one<Type>(db_.conn(), table, run, id);                                         \
  }
LOOM_KB_GET(Observation, get_observation, "loom_kb_observations")
LOOM_KB_GET(Entity, get_entity, "loom_kb_entities")
LOOM_KB_GET(Claim, get_claim, "loom_kb_claims")
LOOM_KB_GET(Principle, get_principle, "loom_kb_principles")
LOOM_KB_GET(Operator, get_operator, "loom_kb_operators")
LOOM_KB_GET(Morphism, get_morphism, "loom_kb_morphisms")
LOOM_KB_GET(Instance, get_instance, "loom_kb_instances")
LOOM_KB_GET(Area, get_area, "loom_kb_areas")
LOOM_KB_GET(Decision, get_decision, "loom_kb_decisions")
LOOM_KB_GET(Fork, get_fork, "loom_kb_forks")
LOOM_KB_GET(Prediction, get_prediction, "loom_kb_predictions")
LOOM_KB_GET(Model, get_model, "loom_kb_models")
LOOM_KB_GET(Product, get_product, "loom_kb_products")
#undef LOOM_KB_GET

// ── queries ─────────────────────────────────────────────────────────
Result<std::vector<Claim>> KnowledgeStore::query_claims(std::string_view run, const ClaimQuery& q) {
  if (!has_schema()) return std::vector<Claim>{};
  Where w;
  w.add("c.run_id = ?", std::string(run));
  if (q.subject) w.add("c.subject = ?", *q.subject);
  if (q.predicate) w.add("c.predicate = ?", *q.predicate);
  if (q.object) w.add("c.object = ?", *q.object);
  if (q.evidence) w.add("c.evidence = ?", en_s(*q.evidence));
  if (q.origin) w.add("c.origin = ?", en_s(*q.origin));
  if (q.status) w.add("c.status = ?", en_s(*q.status));
  if (q.branch) w.add("c.branch = ?", *q.branch);
  std::string table = "loom_kb_claims c";
  if (q.observation) {
    w.add("EXISTS (SELECT 1 FROM loom_kb_claim_support s WHERE s.run_id = c.run_id AND s.claim_id = c.id AND "
          "s.observation_id = ?)",
          *q.observation);
  }
  auto lk = db_.lock();
  return select_bodies<Claim>(db_.conn(), table, w.sql, w.binds, "c.id", q.limit, "c.body");
}

Result<std::vector<Entity>> KnowledgeStore::query_entities(std::string_view run, const EntityQuery& q) {
  if (!has_schema()) return std::vector<Entity>{};
  Where w;
  w.add("e.run_id = ?", std::string(run));
  if (q.kind) w.add("e.kind = ?", *q.kind);
  if (q.canonical_key) w.add("e.canonical_key = ?", *q.canonical_key);
  if (q.parent) w.add("e.parent = ?", *q.parent);
  if (q.alias_key) {
    w.add("EXISTS (SELECT 1 FROM loom_kb_aliases a WHERE a.run_id = e.run_id AND a.entity_id = e.id AND a.alias_key = ?)",
          *q.alias_key);
  }
  auto lk = db_.lock();
  return select_bodies<Entity>(db_.conn(), "loom_kb_entities e", w.sql, w.binds, "e.id", q.limit, "e.body");
}

Result<std::vector<Instance>> KnowledgeStore::query_instances(std::string_view run, std::string_view paradigm,
                                                              std::string_view subject) {
  if (!has_schema()) return std::vector<Instance>{};
  Where w;
  w.add("run_id = ?", std::string(run));
  if (!paradigm.empty()) w.add("paradigm = ?", std::string(paradigm));
  if (!subject.empty()) w.add("subject = ?", std::string(subject));
  auto lk = db_.lock();
  return select_bodies<Instance>(db_.conn(), "loom_kb_instances", w.sql, w.binds, "id", 0);
}

Result<std::vector<SlotRow>> KnowledgeStore::query_slots(std::string_view run, const SlotQuery& q) {
  std::vector<SlotRow> out;
  if (!has_schema()) return out;
  Where w;
  w.add("run_id = ?", std::string(run));
  if (q.instance) w.add("instance_id = ?", *q.instance);
  if (q.role) w.add("role = ?", en_s(*q.role));
  if (q.slot) w.add("slot = ?", *q.slot);
  if (q.claim) w.add("claim_id = ?", *q.claim);
  auto lk = db_.lock();
  LOOM_TRY_ASSIGN(sql::Stmt st, db_.conn().prepare("SELECT instance_id, body FROM loom_kb_slot_values WHERE " + w.sql +
                                                   " ORDER BY instance_id, slot, ord LIMIT ?"));
  int i = 1;
  for (const auto& b : w.binds) st.bind(i++, b);
  st.bind(i, static_cast<std::int64_t>(q.limit <= 0 ? 1000000 : q.limit));
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    auto j = json::parse(st.get_text(1));
    if (!j) return Error(Errc::Database, "loom_kb_slot_values: corrupt body");
    LOOM_TRY_ASSIGN(SlotValue sv, SlotValue::from_json(*j));
    out.push_back(SlotRow{st.get_text(0), std::move(sv)});
  }
  return out;
}

namespace {
template <class T>
Result<std::vector<T>> list_run(Database& db, const char* table, std::string_view run, const char* col = nullptr,
                                std::string_view val = "") {
  if (!kb::has_schema(db)) return std::vector<T>{};
  Where w;
  w.add("run_id = ?", std::string(run));
  if (col && !val.empty()) w.add(std::string(col) + " = ?", std::string(val));
  auto lk = db.lock();
  return select_bodies<T>(db.conn(), table, w.sql, w.binds, "id", 0);
}
}  // namespace

Result<std::vector<Observation>> KnowledgeStore::observations_of_unit(std::string_view run, std::string_view unit) {
  LOOM_TRY_ASSIGN(auto v, list_run<Observation>(db_, "loom_kb_observations", run, "unit", unit));
  std::stable_sort(v.begin(), v.end(), [](const Observation& a, const Observation& b) {
    return a.ordinal != b.ordinal ? a.ordinal < b.ordinal : a.id < b.id;
  });
  return v;
}
Result<std::vector<Principle>> KnowledgeStore::list_principles(std::string_view run) {
  return list_run<Principle>(db_, "loom_kb_principles", run);
}
Result<std::vector<Operator>> KnowledgeStore::list_operators(std::string_view run) {
  return list_run<Operator>(db_, "loom_kb_operators", run);
}
Result<std::vector<Morphism>> KnowledgeStore::list_morphisms(std::string_view run) {
  return list_run<Morphism>(db_, "loom_kb_morphisms", run);
}
Result<std::vector<Area>> KnowledgeStore::list_areas(std::string_view run, std::string_view subject) {
  return list_run<Area>(db_, "loom_kb_areas", run, "subject", subject);
}
Result<std::vector<Decision>> KnowledgeStore::list_decisions(std::string_view run, std::string_view subject) {
  return list_run<Decision>(db_, "loom_kb_decisions", run, "subject", subject);
}
Result<std::vector<Fork>> KnowledgeStore::list_forks(std::string_view run, std::string_view subject) {
  return list_run<Fork>(db_, "loom_kb_forks", run, "subject", subject);
}
Result<std::vector<Prediction>> KnowledgeStore::list_predictions(std::string_view run) {
  return list_run<Prediction>(db_, "loom_kb_predictions", run);
}
Result<std::vector<Model>> KnowledgeStore::list_models(std::string_view run) {
  return list_run<Model>(db_, "loom_kb_models", run);
}
Result<std::vector<Product>> KnowledgeStore::list_products(std::string_view run) {
  return list_run<Product>(db_, "loom_kb_products", run);
}
Result<std::vector<StatusRecord>> KnowledgeStore::status_history(std::string_view run, std::string_view entity) {
  if (entity.empty()) return Error(Errc::InvalidArgument, "entity is required");
  LOOM_TRY_ASSIGN(auto v, list_run<StatusRecord>(db_, "loom_kb_status_records", run, "entity", entity));
  return order_status_history(std::move(v));
}

Result<Json> KnowledgeStore::stats(std::string_view run) {
  Json out = Json::object();
  if (!has_schema()) return out;
  auto lk = db_.lock();
  for (const char* t : kRunTables) {
    LOOM_TRY_ASSIGN(auto n, db_.conn().query_int(std::string("SELECT COUNT(*) FROM ") + t + " WHERE run_id = ?", run));
    out[std::string(t).substr(8)] = n.value_or(0);
  }
  return out;
}

// ── judgements ──────────────────────────────────────────────────────
Result<Judgement> KnowledgeStore::add_judgement(Judgement j) {
  LOOM_TRY(j.validate());
  if (j.created.empty()) j.created = timeutil::utc_now_iso();
  if (j.id.empty()) j.id = Judgement::make_id(j.created, j.target, j.verdict, j.payload);
  LOOM_TRY(ensure_schema());
  auto lk = db_.lock();
  sql::Connection& c = db_.conn();
  sql::Txn txn(c);
  LOOM_TRY(txn.begin_status());
  LOOM_TRY_ASSIGN(auto existing, c.query_text("SELECT body FROM loom_kb_judgements WHERE id = ?", j.id));
  if (existing) {
    LOOM_TRY(txn.commit());
    auto pj = json::parse(*existing);
    if (!pj) return Error(Errc::Database, "loom_kb_judgements: corrupt body");
    return Judgement::from_json(*pj);
  }
  LOOM_TRY_ASSIGN(auto mx, c.query_int("SELECT COALESCE(MAX(seq), 0) FROM loom_kb_judgements"));
  j.seq = mx.value_or(0) + 1;
  LOOM_TRY(c.run("INSERT INTO loom_kb_judgements (id, seq, target_kind, target, verdict, created, body) "
                 "VALUES (?, ?, ?, ?, ?, ?, ?)",
                 j.id, j.seq, en_s(j.target_kind), j.target, en_s(j.verdict), j.created, json::dump(j.to_json())));
  LOOM_TRY(txn.commit());
  return j;
}

Result<std::vector<Judgement>> KnowledgeStore::judgements(std::int64_t after_seq, std::string_view target) {
  std::vector<Judgement> out;
  if (!has_schema()) return out;
  auto lk = db_.lock();
  std::string q = "SELECT body FROM loom_kb_judgements WHERE seq > ?";
  if (!target.empty()) q += " AND target = ?";
  q += " ORDER BY seq";
  LOOM_TRY_ASSIGN(sql::Stmt st, db_.conn().prepare(q));
  st.bind(1, after_seq);
  if (!target.empty()) st.bind(2, target);
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    auto j = json::parse(st.get_text(0));
    if (!j) return Error(Errc::Database, "loom_kb_judgements: corrupt body");
    LOOM_TRY_ASSIGN(Judgement x, Judgement::from_json(*j));
    out.push_back(std::move(x));
  }
  return out;
}

// ── replay ──────────────────────────────────────────────────────────
namespace {

std::string judgement_note(const Judgement& j) { return "judgement:" + j.id; }

void add_unique(std::vector<std::string>& v, const std::string& s) {
  if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

// Merges `patch` keys into the object's JSON and parses it back.
template <class T>
Result<T> patched(const T& x, const Json& patch, std::initializer_list<const char*> frozen) {
  Json j = x.to_json();
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    for (const char* f : frozen) {
      if (it.key() == f) return Error(Errc::InvalidArgument, std::string("field '") + f + "' cannot be edited");
    }
    j[it.key()] = it.value();
  }
  return T::from_json(j);
}

}  // namespace

Result<ReplayReport> KnowledgeStore::replay_judgements(std::string_view run) {
  LOOM_TRY(check_run(run));
  ReplayReport rep;
  LOOM_TRY(ensure_schema());
  std::int64_t done_seq = 0;
  {
    auto lk = db_.lock();
    LOOM_TRY_ASSIGN(auto v, db_.conn().query_int("SELECT replayed_seq FROM loom_kb_runs WHERE run_id = ?", run));
    if (!v) return Error(Errc::NotFound, "no knowledge run " + std::string(run));
    done_seq = *v;
  }
  LOOM_TRY_ASSIGN(auto all, judgements(done_seq));
  auto note = [&](const Judgement& j, bool applied, const std::string& reason) {
    (applied ? rep.applied : rep.skipped) += 1;
    rep.details.push_back(Json{{"judgement", j.id},
                               {"target", j.target},
                               {"verdict", std::string(to_string(j.verdict))},
                               {"result", applied ? "applied" : "skipped"},
                               {"reason", reason}});
  };
  // Re-keys a claim onto new subject/object values (merge) or a new value (edit).
  auto user_assess = [](Assessment& a, const Judgement& j) {
    a.evidence = EvidenceClass::User;
    a.confidence = 1.0;
    a.status = ClaimStatus::Active;
    if (a.expected) a.check = CheckState::Holds;
    add_unique(a.premises.assumptions, judgement_note(j));
  };
  auto repoint_slots = [&](const std::string& from, const std::string& to) -> Status {
    LOOM_TRY_ASSIGN(auto rows, query_slots(run, SlotQuery{std::nullopt, std::nullopt, std::nullopt, from, 0}));
    std::set<std::string> insts;
    for (const auto& r : rows) insts.insert(r.instance);
    for (const auto& iid : insts) {
      LOOM_TRY_ASSIGN(auto inst, get_instance(run, iid));
      if (!inst) continue;
      for (auto& s : inst->slots) {
        if (s.claim == from) s.claim = to;
      }
      LOOM_TRY(put_instances(run, {*inst}));
    }
    return {};
  };

  for (const auto& j : all) {
    switch (j.target_kind) {
      case RefKind::Claim: {
        LOOM_TRY_ASSIGN(auto c, get_claim(run, j.target));
        if (!c) {
          note(j, false, "target not in run");
          break;
        }
        if (j.verdict == Verdict::Confirm) {
          user_assess(c->assessment, j);
          LOOM_TRY(put_claims(run, {*c}));
          note(j, true, "");
        } else if (j.verdict == Verdict::Reject) {
          c->assessment.status = ClaimStatus::Rejected;
          add_unique(c->assessment.premises.assumptions, judgement_note(j));
          LOOM_TRY(put_claims(run, {*c}));
          note(j, true, "");
        } else if (j.verdict == Verdict::Edit) {
          Claim n = *c;
          n.object = json::get_string(j.payload, "object");
          if (n.object.empty()) {
            const Json* v = json::find(j.payload, "value");
            n.value = v ? *v : Json(nullptr);
          } else {
            n.value = nullptr;
          }
          n.assessment.origin = Origin::User;
          n.assessment.alternatives.clear();
          user_assess(n.assessment, j);
          n.assessment.counter.claims.clear();
          n.id = Claim::make_id(n.subject, n.predicate, n.object, n.value, n.qualifiers);
          if (n.id == c->id) {
            LOOM_TRY(put_claims(run, {n}));
            note(j, true, "value unchanged; confirmed");
            break;
          }
          c->assessment.status = ClaimStatus::Rejected;
          add_unique(c->assessment.counter.claims, n.id);
          add_unique(c->assessment.premises.assumptions, judgement_note(j));
          LOOM_TRY(put_claims(run, {n, *c}));
          LOOM_TRY(repoint_slots(c->id, n.id));
          note(j, true, "replaced by " + n.id);
        } else {
          note(j, false, "verdict not applicable to a claim");
        }
        break;
      }
      case RefKind::Entity: {
        LOOM_TRY_ASSIGN(auto e, get_entity(run, j.target));
        if (!e) {
          note(j, false, "target not in run");
          break;
        }
        if (j.verdict == Verdict::Confirm || j.verdict == Verdict::Reject) {
          e->status = j.verdict == Verdict::Confirm ? ClaimStatus::Active : ClaimStatus::Rejected;
          if (j.verdict == Verdict::Confirm) {
            e->evidence = EvidenceClass::User;
            e->confidence = 1.0;
          }
          LOOM_TRY(put_entities(run, {*e}));
          note(j, true, "");
        } else if (j.verdict == Verdict::Edit) {
          auto ne = patched(*e, j.payload, {"id", "kind", "canonical_key"});
          if (!ne) {
            note(j, false, ne.error().message);
            break;
          }
          LOOM_TRY(put_entities(run, {*ne}));
          note(j, true, "");
        } else if (j.verdict == Verdict::Merge) {
          std::string into = json::get_string(j.payload, "into");
          LOOM_TRY_ASSIGN(auto keep, get_entity(run, into));
          if (!keep) {
            note(j, false, "merge target not in run");
            break;
          }
          for (auto a : e->aliases) {
            bool have = std::any_of(keep->aliases.begin(), keep->aliases.end(), [&](const Alias& x) { return x.key == a.key; });
            if (!have) {
              a.method = "merge";
              keep->aliases.push_back(a);
            }
          }
          if (std::none_of(keep->aliases.begin(), keep->aliases.end(),
                           [&](const Alias& x) { return x.key == e->canonical_key; })) {
            keep->aliases.push_back(Alias{e->canonical_key, e->label, "", "merge", 0, 1.0});
          }
          std::sort(keep->aliases.begin(), keep->aliases.end(), [](const Alias& a, const Alias& b) { return a.key < b.key; });
          e->status = ClaimStatus::Rejected;
          e->attrs["merged_into"] = into;
          LOOM_TRY(put_entities(run, {*keep, *e}));
          // Re-key the claims that mention the merged entity.
          std::vector<Claim> moved;
          for (bool as_subject : {true, false}) {
            ClaimQuery q;
            (as_subject ? q.subject : q.object) = e->id;
            LOOM_TRY_ASSIGN(auto cs, query_claims(run, q));
            for (auto& c : cs) {
              if (c.assessment.status == ClaimStatus::Superseded) continue;
              Claim n = c;
              if (n.subject == e->id) n.subject = into;
              if (n.object == e->id) n.object = into;
              n.id = Claim::make_id(n.subject, n.predicate, n.object, n.value, n.qualifiers);
              add_unique(n.assessment.premises.assumptions, judgement_note(j));
              // The surviving entity may already carry the same claim: keep that
              // claim's assessment (an owner verdict on it wins) and add support.
              LOOM_TRY_ASSIGN(auto same, get_claim(run, n.id));
              if (same && same->id != c.id) {
                Claim merged = *same;
                for (const auto& sp : n.assessment.support) {
                  bool have = std::any_of(merged.assessment.support.begin(), merged.assessment.support.end(),
                                          [&](const Support& x) { return x.observation == sp.observation; });
                  if (!have) merged.assessment.support.push_back(sp);
                }
                std::sort(merged.assessment.support.begin(), merged.assessment.support.end(),
                          [](const Support& a, const Support& b) { return a.observation < b.observation; });
                add_unique(merged.assessment.premises.assumptions, judgement_note(j));
                n = merged;
              }
              c.assessment.status = ClaimStatus::Superseded;
              add_unique(c.assessment.counter.claims, n.id);
              moved.push_back(n);
              moved.push_back(c);
            }
          }
          std::sort(moved.begin(), moved.end(), [](const Claim& a, const Claim& b) { return a.id < b.id; });
          moved.erase(std::unique(moved.begin(), moved.end(), [](const Claim& a, const Claim& b) { return a.id == b.id; }),
                      moved.end());
          if (!moved.empty()) LOOM_TRY(put_claims(run, moved));
          for (const auto& c : moved) {
            if (c.assessment.status == ClaimStatus::Superseded && !c.assessment.counter.claims.empty()) {
              LOOM_TRY(repoint_slots(c.id, c.assessment.counter.claims.back()));
            }
          }
          note(j, true, "merged into " + into);
        } else {  // split
          std::set<std::string> keys;
          if (const Json* al = json::find(j.payload, "aliases"); al && al->is_array()) {
            for (const auto& k : *al) {
              if (k.is_string()) keys.insert(k.get<std::string>());
            }
          }
          Entity n;
          n.kind = e->kind;
          n.canonical_key = json::get_string(j.payload, "key");
          n.id = Entity::make_id(n.kind, n.canonical_key);
          n.label = json::get_string(j.payload, "label", n.canonical_key);
          n.evidence = EvidenceClass::User;
          n.origin = Origin::User;
          n.confidence = 1.0;
          n.attrs["split_from"] = e->id;
          std::vector<Alias> rest;
          for (const auto& a : e->aliases) (keys.count(a.key) ? n.aliases : rest).push_back(a);
          e->aliases = rest;
          e->attrs["split_into"] = n.id;
          LOOM_TRY(put_entities(run, {*e, n}));
          note(j, true, "split into " + n.id);
        }
        break;
      }
      case RefKind::Principle: {
        LOOM_TRY_ASSIGN(auto p, get_principle(run, j.target));
        if (!p) {
          note(j, false, "target not in run");
          break;
        }
        if (j.verdict == Verdict::Edit) {
          auto np = patched(*p, j.payload, {"id"});
          if (!np) {
            note(j, false, np.error().message);
            break;
          }
          *p = *np;
        } else if (j.verdict == Verdict::Confirm || j.verdict == Verdict::Reject) {
          p->validation = j.verdict == Verdict::Confirm ? ValidationStatus::Confirmed : ValidationStatus::Rejected;
        } else {
          note(j, false, "verdict not applicable to a principle");
          break;
        }
        LOOM_TRY(put_principles(run, {*p}));
        note(j, true, "");
        break;
      }
      case RefKind::Operator:
      case RefKind::Morphism:
      case RefKind::Model: {
        auto apply = [&](auto getter, auto putter, auto& obj) -> Result<bool> {
          LOOM_TRY_ASSIGN(auto o, getter());
          if (!o) {
            note(j, false, "target not in run");
            return false;
          }
          obj = *o;
          if (j.verdict == Verdict::Edit) {
            auto n = patched(obj, j.payload, {"id"});
            if (!n) {
              note(j, false, n.error().message);
              return false;
            }
            obj = *n;
          } else if (j.verdict == Verdict::Confirm || j.verdict == Verdict::Reject) {
            obj.validation = j.verdict == Verdict::Confirm ? ValidationStatus::Confirmed : ValidationStatus::Rejected;
          } else {
            note(j, false, "verdict not applicable");
            return false;
          }
          LOOM_TRY(putter(obj));
          note(j, true, "");
          return true;
        };
        if (j.target_kind == RefKind::Operator) {
          Operator o;
          LOOM_TRY(apply([&] { return get_operator(run, j.target); }, [&](const Operator& x) { return put_operators(run, {x}); }, o));
        } else if (j.target_kind == RefKind::Morphism) {
          Morphism o;
          LOOM_TRY(apply([&] { return get_morphism(run, j.target); }, [&](const Morphism& x) { return put_morphisms(run, {x}); }, o));
        } else {
          Model o;
          LOOM_TRY(apply([&] { return get_model(run, j.target); }, [&](const Model& x) { return put_models(run, {x}); }, o));
        }
        break;
      }
      case RefKind::Decision: {
        LOOM_TRY_ASSIGN(auto d, get_decision(run, j.target));
        if (!d) {
          note(j, false, "target not in run");
          break;
        }
        if (j.verdict == Verdict::Edit) {
          auto nd = patched(*d, j.payload, {"id"});
          if (!nd) {
            note(j, false, nd.error().message);
            break;
          }
          LOOM_TRY(put_decisions(run, {*nd}));
          note(j, true, "");
        } else if (j.verdict == Verdict::Reject) {
          d->status = DecisionStatus::Reverted;
          LOOM_TRY(put_decisions(run, {*d}));
          note(j, true, "");
        } else if (j.verdict == Verdict::Confirm) {
          note(j, true, "confirmed (the decides claim carries the evidence)");
        } else {
          note(j, false, "verdict not applicable to a decision");
        }
        break;
      }
      default:
        note(j, false, "no replay rule for target kind " + std::string(to_string(j.target_kind)));
        break;
    }
    auto lk = db_.lock();
    LOOM_TRY(db_.conn().run("UPDATE loom_kb_runs SET replayed_seq = ? WHERE run_id = ?", j.seq, run));
  }
  return rep;
}

}  // namespace loom::kb
