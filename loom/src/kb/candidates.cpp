#include "loom/knowledge_store.h"

#include "loom/db.h"
#include "loom/sqlite.h"

namespace loom::kb {

Result<Json> KnowledgeStore::query_candidates(std::string_view run, std::string_view kind,
                                             int limit, std::int64_t offset) {
  if (run.empty() || limit < 1 || limit > 1000 || offset < 0)
    return Error(Errc::InvalidArgument, "candidate query requires run, limit 1..1000 and nonnegative offset");
  Json out{{"items", Json::array()}, {"total", 0}, {"limit", limit}, {"offset", offset},
           {"has_more", false}, {"interpretation", "unpromoted_candidates_not_canonical_claims"}};
  if (!has_schema()) return out;
  auto lock = db_.lock();
  sql::Txn tx(db_.conn(), false);
  LOOM_TRY(tx.begin_status());
  // Rows are global. Never let a query for one run leak proposals from another.
  const std::string where =
      " WHERE json_extract(CASE WHEN json_valid(payload) THEN payload ELSE '{}' END,'$.run_id')=?"
      " AND (?='' OR kind=?)";
  LOOM_TRY_ASSIGN(auto count, db_.conn().query_int("SELECT COUNT(*) FROM loom_kb_candidates" + where,
                                                 run, kind, kind));
  const auto total = count.value_or(0);
  out["total"] = total;
  LOOM_TRY_ASSIGN(auto rows, db_.conn().prepare(
      "SELECT id,kind,payload,support,eval,status,created FROM loom_kb_candidates" + where +
      " ORDER BY id LIMIT ? OFFSET ?"));
  rows.bind_all(run, kind, kind, limit, offset);
  while (true) {
    LOOM_TRY_ASSIGN(auto row, rows.step());
    if (!row) break;
    LOOM_TRY_ASSIGN(auto payload, json::parse(rows.get_text(2)));
    LOOM_TRY_ASSIGN(auto support, json::parse(rows.get_text(3)));
    LOOM_TRY_ASSIGN(auto evaluation, json::parse(rows.get_text(4)));
    out["items"].push_back(Json{{"id", rows.get_text(0)}, {"kind", rows.get_text(1)},
                                 {"payload", payload}, {"support", support}, {"eval", evaluation},
                                 {"status", rows.get_text(5)}, {"created", rows.get_text(6)}});
  }
  out["has_more"] = offset < total && static_cast<std::int64_t>(out["items"].size()) < total - offset;
  LOOM_TRY(tx.commit());
  return out;
}

}  // namespace loom::kb
