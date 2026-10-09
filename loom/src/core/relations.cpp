#include "loom/relations.h"

#include "loom/db.h"
#include "loom/log.h"

namespace loom {
namespace {
constexpr std::string_view kLog = "loom.relations";

// Policy data: the built-in relation vocabulary. Python's graph code uses the
// names; everything else (inverse, symmetry, category) is Loom metadata.
constexpr std::string_view kBuiltinJson = R"JSON([
  {"name": "mentions",       "inverse": "mentioned_in",   "category": "structural", "description": "message/text mentions an entity"},
  {"name": "tagged_with",    "inverse": "tags",           "category": "structural", "description": "item is tagged with a topic"},
  {"name": "part_of",        "inverse": "has_part",       "category": "structural", "transitive": true, "description": "item belongs to a container (message -> conversation)"},
  {"name": "reply_to",       "inverse": "replied_by",     "category": "dialogue",   "description": "message replies to another message"},
  {"name": "answers",        "inverse": "answered_by",    "category": "dialogue",   "description": "item answers a question"},
  {"name": "asks",           "inverse": "asked_in",       "category": "dialogue",   "description": "item raises a question"},
  {"name": "depends_on",     "inverse": "dependency_of",  "category": "semantic",   "transitive": true, "description": "subject requires object"},
  {"name": "references",     "inverse": "referenced_by",  "category": "semantic",   "description": "subject refers to object"},
  {"name": "implements",     "inverse": "implemented_by", "category": "semantic",   "description": "subject implements object (spec, interface, idea)"},
  {"name": "contradicts",    "inverse": "contradicts",    "category": "semantic",   "symmetric": true, "description": "statements are in conflict"},
  {"name": "alternative_to", "inverse": "alternative_to", "category": "semantic",   "symmetric": true, "description": "options for the same decision"},
  {"name": "related",        "inverse": "related",        "category": "semantic",   "symmetric": true, "description": "generic association (Python default link_type)"},
  {"name": "related_to",     "inverse": "related_to",     "category": "semantic",   "symmetric": true, "description": "generic association (core/semantic.py RelationType)"},
  {"name": "generated_by",   "inverse": "generated",      "category": "provenance", "description": "artifact produced by a model/tool/task"},
  {"name": "derived_from",   "inverse": "derivation_of",  "category": "provenance", "transitive": true, "description": "derived state computed from a source"},
  {"name": "created_by",     "inverse": "created",        "category": "provenance", "description": "item authored by a person/agent"},
  {"name": "forked_from",    "inverse": "fork_of",        "category": "provenance", "description": "idea/branch forked from another"},
  {"name": "supersedes",     "inverse": "superseded_by",  "category": "temporal",   "transitive": true, "description": "newer decision replaces an older one (history kept)"},
  {"name": "superseded_by",  "inverse": "supersedes",     "category": "temporal",   "transitive": true, "description": "older decision replaced by a newer one"},
  {"name": "decided_in",     "inverse": "decision_of",    "category": "temporal",   "description": "decision was made in a conversation/message"}
])JSON";
}  // namespace

const Json& builtin_relation_types() {
  static const Json kBuiltin = json::parse_or(kBuiltinJson, Json::array());
  return kBuiltin;
}

Json RelationType::to_json() const {
  return Json{{"name", name},
              {"inverse", inverse.empty() ? Json(nullptr) : Json(inverse)},
              {"symmetric", symmetric},
              {"transitive", transitive},
              {"category", category},
              {"description", description},
              {"metadata", metadata}};
}

Result<RelationType> RelationType::from_json(const Json& j) {
  RelationType t;
  t.name = json::get_string(j, "name");
  if (t.name.empty()) return Error(Errc::InvalidArgument, "relation type needs a name");
  t.inverse = json::get_string(j, "inverse");
  t.symmetric = json::get_bool(j, "symmetric", false);
  t.transitive = json::get_bool(j, "transitive", false);
  t.category = json::get_string(j, "category", "custom");
  t.description = json::get_string(j, "description");
  if (const Json* m = json::find(j, "metadata"); m && m->is_object()) t.metadata = *m;
  return t;
}

RelationRegistry::RelationRegistry(Database& db) : db_(db) {}

Status RelationRegistry::load_cache_locked() {
  // Callers hold Database's recursive lock before the registry mutex.
  // A transaction-local view must never survive a later outer rollback.
  cache_loaded_ = false;
  cache_.clear();
  LOOM_TRY_ASSIGN(auto st, db_.conn().prepare(
                               "SELECT name, inverse, symmetric, transitive, category, description, metadata FROM "
                               "loom_relation_types"));
  while (true) {
    LOOM_TRY_ASSIGN(bool row, st.step());
    if (!row) break;
    RelationType t;
    t.name = st.get_text(0);
    t.inverse = st.get_opt_text(1).value_or("");
    t.symmetric = st.get_int(2) != 0;
    t.transitive = st.get_int(3) != 0;
    t.category = st.get_text(4);
    t.description = st.get_text(5);
    t.metadata = json::parse_or(st.get_text(6), Json::object());
    cache_[t.name] = std::move(t);
  }
  cache_loaded_ = !db_.conn().in_transaction();
  return {};
}

Status RelationRegistry::seed_builtin() {
  auto dl = db_.lock();
  std::lock_guard lk(mu_);
  cache_loaded_ = false;
  sql::Txn txn(db_.conn());
  LOOM_TRY(txn.begin_status());
  for (const auto& j : builtin_relation_types()) {
    LOOM_TRY_ASSIGN(RelationType t, RelationType::from_json(j));
    LOOM_TRY(db_.conn().run(
        "INSERT OR IGNORE INTO loom_relation_types (name, inverse, symmetric, transitive, category, description, "
        "metadata) VALUES (?, ?, ?, ?, ?, ?, ?)",
        t.name, t.inverse.empty() ? std::optional<std::string>() : std::optional<std::string>(t.inverse),
        t.symmetric ? 1 : 0, t.transitive ? 1 : 0, t.category, t.description, json::py_dumps(t.metadata)));
  }
  LOOM_TRY(txn.commit());
  return load_cache_locked();
}

Status RelationRegistry::upsert(const RelationType& t) {
  if (t.name.empty()) return Error(Errc::InvalidArgument, "relation type needs a name");
  auto dl = db_.lock();
  std::lock_guard lk(mu_);
  const bool was_loaded = cache_loaded_;
  cache_loaded_ = false;  // Even a failed statement may have trigger side effects.
  LOOM_TRY(db_.conn().run(
        "INSERT OR REPLACE INTO loom_relation_types (name, inverse, symmetric, transitive, category, description, "
        "metadata) VALUES (?, ?, ?, ?, ?, ?, ?)",
        t.name, t.inverse.empty() ? std::optional<std::string>() : std::optional<std::string>(t.inverse),
        t.symmetric ? 1 : 0, t.transitive ? 1 : 0, t.category, t.description, json::py_dumps(t.metadata)));
  cache_[t.name] = t;
  cache_loaded_ = was_loaded && !db_.conn().in_transaction();
  return {};
}

Status RelationRegistry::load(const Json& definitions) {
  if (!definitions.is_array()) return Error(Errc::InvalidArgument, "relation definitions must be a JSON array");
  for (const auto& j : definitions) {
    LOOM_TRY_ASSIGN(RelationType t, RelationType::from_json(j));
    LOOM_TRY(upsert(t));
  }
  return {};
}

Result<std::optional<RelationType>> RelationRegistry::get(std::string_view name) {
  auto dl = db_.lock();
  std::lock_guard lk(mu_);
  if (!cache_loaded_ || db_.conn().in_transaction()) LOOM_TRY(load_cache_locked());
  auto it = cache_.find(name);
  if (it == cache_.end()) return std::optional<RelationType>{};
  return std::optional<RelationType>(it->second);
}

Result<std::vector<RelationType>> RelationRegistry::list(std::optional<std::string_view> category) {
  auto dl = db_.lock();
  std::lock_guard lk(mu_);
  if (!cache_loaded_ || db_.conn().in_transaction()) LOOM_TRY(load_cache_locked());
  std::vector<RelationType> out;
  for (const auto& [n, t] : cache_) {
    if (!category || t.category == *category) out.push_back(t);
  }
  return out;
}

Result<RelationType> RelationRegistry::ensure(std::string_view name) {
  // Serialize the miss/upsert pair without taking the nonrecursive registry
  // mutex around get()/upsert(), which already acquire it in DB-first order.
  auto dl = db_.lock();
  LOOM_TRY_ASSIGN(auto existing, get(name));
  if (existing) return *existing;
  RelationType t;
  t.name = std::string(name);
  t.category = "custom";
  t.description = "auto-registered";
  LOOM_TRY(upsert(t));
  log::debug(kLog, "auto-registered relation type {}", name);
  return t;
}

std::string RelationRegistry::inverse_of(std::string_view name) {
  auto r = get(name);
  if (r && r->has_value()) return (*r)->inverse;
  return {};
}

bool RelationRegistry::is_symmetric(std::string_view name) {
  auto r = get(name);
  return r && r->has_value() && (*r)->symmetric;
}

}  // namespace loom
