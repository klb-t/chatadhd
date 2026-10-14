#include <utility>
#include <algorithm>
#include <set>

#include "loom/config.h"
#include "loom/db.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/port_compose.h"
#include "loom/runtime_profile.h"
#include "loom/storage_backend.h"
#include "loom/util/fs.h"
#include "loom/util/ids.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"
#include "storage_internal.h"

namespace loom::storage {
namespace {
constexpr std::string_view kLog = "loom.storage";
const Json kEmptyObj = Json::object();
const Json kEmptyArr = Json::array();

const Json& obj_at(const Json& v, std::string_view k) {
  const Json* p = json::find(v, k);
  return p && p->is_object() ? *p : kEmptyObj;
}
bool is_hash(std::string_view s) {
  if (s.size() != 64) return false;
  return std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
std::string last_segment(std::string_view key) {
  auto p = key.rfind('/');
  return std::string(p == std::string_view::npos ? key : key.substr(p + 1));
}
}  // namespace

StorageService::StorageService(Json values, BackendEnv env, const BackendRegistry* registry)
    : values_(std::move(values)), env_(std::move(env)), registry_(registry ? registry : &BackendRegistry::builtin()) {}

Result<std::unique_ptr<StorageService>> StorageService::load(const BackendEnv& env, Database* db, const Json& overrides) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("storage_security", env.data_dir, overrides));
  auto s = std::make_unique<StorageService>(profile.values(), env);
  s->set_database(db);
  return s;
}

void StorageService::set_values(Json v) {
  std::lock_guard lk(mu_);
  values_ = std::move(v);
  cache_.clear();
}

std::vector<Target> StorageService::placement(std::string_view category) const {
  std::vector<Target> out;
  const Json* arr = json::find(obj_at(values_, "placement"), category);
  if (!arr || !arr->is_array()) return out;
  for (const auto& e : *arr) {
    if (!e.is_object() || json::get_bool(e, "hidden", false)) continue;
    out.push_back({json::get_string(e, "storage"), json::get_string(e, "role", "primary")});
  }
  return out;
}

bool StorageService::is_default_local(std::string_view category) const {
  auto t = placement(category);
  return t.size() == 1 && t[0].storage == "local_dir";
}

Result<std::shared_ptr<StorageBackend>> StorageService::backend(const std::string& sid) const {
  std::lock_guard lk(mu_);
  if (auto it = cache_.find(sid); it != cache_.end()) return it->second;
  const Json* sj = json::find(obj_at(values_, "storages"), sid);
  if (!sj || !sj->is_object()) return Error(Errc::NotFound, "unknown storage '" + sid + "'");
  Json cfg;
  if (const Json* b = json::find(*sj, "backend"); b && b->is_object()) {
    cfg = *b;
  } else if (json::get_string(*sj, "class") == "local") {
    cfg = Json{{"kind", "local"}, {"path", json::get_string(*sj, "path", "{dir}")}};
  } else {
    return Error(Errc::Unsupported, "storage '" + sid + "' (class " + json::get_string(*sj, "class") +
                                        ") is not an object store; add a \"backend\" descriptor to use it as one");
  }
  LOOM_TRY_ASSIGN(auto be, registry_->create(sid, cfg, env_));
  std::shared_ptr<StorageBackend> sp(std::move(be));
  cache_[sid] = sp;
  return sp;
}

bool StorageService::placed_on(std::string_view category, std::string_view sid) const {
  for (const auto& t : placement(category)) if (t.storage == sid) return true;
  return false;
}

Status StorageService::put(std::string_view category, std::string_view key, std::string_view bytes, std::string_view mime,
                           std::string_view skip) {
  auto targets = placement(category);
  if (targets.empty()) return Error(Errc::NotFound, "no placement for category '" + std::string(category) + "'");
  const std::string full = std::string(category) + "/" + std::string(key);
  int quorum = static_cast<int>(json::get_int(obj_at(obj_at(values_, "sync_rules"), "write_quorum"), "preset", 1));
  return compose::mirror(
      targets,
      [&](const Target& t) -> std::optional<Status> {
        if (t.role == "egress" || t.storage == skip) return std::nullopt;
        auto be = backend(t.storage);
        return be ? (*be)->put(full, bytes, mime) : Status(be.error());
      },
      quorum, Error(Errc::Unavailable, "no target accepted the write"),
      [](const Target& t, const Error& e) {
        log::warn(kLog, "write to {} degraded: {}", t.storage, e.message);  // unreachable target degrades, never blocks
      });
}

Result<std::string> StorageService::get(std::string_view category, std::string_view key, std::string_view skip) const {
  auto targets = placement(category);
  const std::string full = std::string(category) + "/" + std::string(key);
  bool backend_unavailable = false;  // a target whose backend cannot be built always counts as the last error
  return compose::fallback<std::string>(
      targets,
      [&](const Target& t) -> std::optional<Result<std::string>> {
        if (t.storage == skip) return std::nullopt;
        auto be = backend(t.storage);
        if (!be) { backend_unavailable = true; return Result<std::string>(be.error()); }
        return (*be)->get(full);
      },
      Error(Errc::NotFound, "object not found: " + full),
      [&](const Target& t, const Error& e, Error& last) {
        if (std::exchange(backend_unavailable, false)) { last = e; return; }
        if (e.code == Errc::NotFound) return;
        log::warn(kLog, "read from {} failed, trying next: {}", t.storage, e.message);
        last = e;
      });
}

bool StorageService::has(std::string_view category, std::string_view key, std::string_view skip) const {
  const std::string full = std::string(category) + "/" + std::string(key);
  for (const auto& t : placement(category)) {
    if (t.storage == skip) continue;
    auto be = backend(t.storage);
    if (be && (*be)->stat(full)) return true;
  }
  return false;
}

Status StorageService::record_location(std::string_view category, std::string_view key, const std::string& sid,
                                       std::string_view sha, std::int64_t size) {
  if (!db_) return {};
  auto lk = db_->lock();
  NodeOptions no;
  no.metadata = Json{{"role", "storage"}};
  LOOM_TRY_ASSIGN(std::string node, db_->get_or_create_node(sid, "storage", no));
  LOOM_TRY(db_->create_link("blob:" + std::string(sha), node, "stored_in", 1.0,
                            Json{{"category", category}, {"key", key}, {"size", size}, {"verified", timeutil::utc_now_iso()}}));
  return {};
}

Result<std::string> StorageService::put_cas(std::string_view category, std::string_view bytes, std::string_view mime) {
  const std::string hash = Sha256::hex(bytes);
  const std::string key = cas_key(hash);
  LOOM_TRY(put(category, key, bytes, mime));
  note_stored(category, hash, static_cast<std::int64_t>(bytes.size()));
  return hash;
}

void StorageService::note_stored(std::string_view category, std::string_view sha, std::int64_t size) {
  if (!db_) return;
  const std::string key = cas_key(sha);
  for (const auto& t : placement(category)) {
    auto be = backend(t.storage);
    if (!be || !(*be)->stat(std::string(category) + "/" + key)) continue;
    if (auto st = record_location(category, key, t.storage, sha, size); !st)
      log::debug(kLog, "location record failed: {}", st.error().message);
  }
}

Result<std::vector<std::string>> StorageService::list_category_keys(std::string_view category, const std::string& sid) const {
  LOOM_TRY_ASSIGN(auto be, backend(sid));
  const std::string prefix = std::string(category) + "/";
  LOOM_TRY_ASSIGN(auto objs, be->list(prefix));
  std::vector<std::string> keys;
  for (auto& o : objs) keys.push_back(o.key.substr(prefix.size()));
  return keys;
}

Result<Json> StorageService::transfer(const MoveOptions& o) {
  if (o.delete_source && !o.move) return Error(Errc::InvalidArgument, "delete_source requires move");
  auto current = placement(o.category);
  if (current.empty()) return Error(Errc::NotFound, "no placement for category '" + o.category + "'");
  // allowed_storages: any | only[...] | none
  const Json& allow = obj_at(obj_at(values_, "allowed_storages"), o.category);
  const std::string state = json::get_string(allow, "state", "any");
  if (state == "none") return Error(Errc::Conflict, "category '" + o.category + "' allows no storage");
  if (state == "only") {
    bool ok = false;
    if (const Json* l = json::find(allow, "storages"); l && l->is_array())
      for (const auto& s : *l) ok = ok || (s.is_string() && s.get<std::string>() == o.to);
    if (!ok) return Error(Errc::Conflict, "storage '" + o.to + "' is not allowed for category '" + o.category + "'");
  }
  LOOM_TRY_ASSIGN(auto target, backend(o.to));
  std::vector<Target> sources;
  for (const auto& t : current) if (t.storage != o.to) sources.push_back(t);

  // 1. enumerate the union of keys on the sources
  std::set<std::string> keys;
  for (const auto& s : sources) {
    LOOM_TRY_ASSIGN(auto ks, list_category_keys(o.category, s.storage));
    keys.insert(ks.begin(), ks.end());
  }
  Json report{{"category", o.category}, {"to", o.to}, {"mode", o.move ? "move" : "copy"},
              {"objects", static_cast<std::int64_t>(keys.size())}};
  if (o.dry_run) { report["dry_run"] = true; return report; }

  // 2. copy + verify sha256 on the target; roll back what this run created on any failure
  std::vector<std::string> created, verified;
  std::int64_t bytes_total = 0, skipped = 0;
  auto rollback = [&] { for (const auto& k : created) (void)target->remove(o.category + "/" + k); };
  for (const auto& k : keys) {
    const std::string full = o.category + "/" + k;
    std::string data;
    bool got = false;
    Error last(Errc::NotFound, "object vanished from sources: " + full);
    for (const auto& s : sources) {  // read preference order
      auto be = backend(s.storage);
      if (!be) continue;
      auto r = (*be)->get(full);
      if (r) { data = std::move(*r); got = true; break; }
      last = r.error();
    }
    if (!got) { rollback(); return last; }
    const std::string sha = Sha256::hex(data);
    const std::string leaf = last_segment(k);
    if (is_hash(leaf) && leaf != sha) {
      rollback();
      return Error(Errc::Conflict, "source object does not match its hash (not copied): " + full);
    }
    if (auto have = target->stat(full); have) {
      if (verify_object(*target, full, sha)) { ++skipped; verified.push_back(k); bytes_total += static_cast<std::int64_t>(data.size()); continue; }
    }
    if (auto st = target->put(full, data); !st) { rollback(); return st.error(); }
    created.push_back(k);
    if (auto st = verify_object(*target, full, sha); !st) { rollback(); return st.error(); }
    verified.push_back(k);
    bytes_total += static_cast<std::int64_t>(data.size());
    if (is_hash(leaf)) (void)record_location(o.category, k, o.to, sha, static_cast<std::int64_t>(data.size()));
  }

  // 3. switch placement (data: profile overlay) — only now
  Json placement_json = Json::array();
  if (o.move) {
    placement_json.push_back(Json{{"storage", o.to}, {"role", "primary"}, {"encrypt_at_rest", "none"}});
  } else {
    for (const auto& t : current) placement_json.push_back(Json{{"storage", t.storage}, {"role", t.role}, {"encrypt_at_rest", "none"}});
    bool present = std::any_of(current.begin(), current.end(), [&](const Target& t) { return t.storage == o.to; });
    if (!present) placement_json.push_back(Json{{"storage", o.to}, {"role", "replica"}, {"encrypt_at_rest", "none"}});
  }
  if (o.persist) {
    const std::filesystem::path file = env_.data_dir / "profiles" / "storage_security.pack";
    Json doc = Json{{"schema", "loom.runtime_profile_overlay/1"}, {"domain", "storage_security"}, {"overrides", Json::object()}};
    std::error_code ec;
    std::string before;
    const bool existed = std::filesystem::exists(file, ec);
    if (existed) {
      LOOM_TRY_ASSIGN(before, fsutil::read_file(file));
      auto parsed = json::parse(before);
      if (!parsed) { rollback(); return Error(Errc::Parse, "existing storage_security overlay is not valid JSON; not modified"); }
      doc = *parsed;
    }
    if (!doc.contains("overrides") || !doc["overrides"].is_object()) doc["overrides"] = Json::object();
    doc["overrides"]["placement"][o.category] = placement_json;
    LOOM_TRY(fsutil::ensure_dir(file.parent_path()));
    LOOM_TRY(fsutil::atomic_write(file, json::dump(doc)));
    auto check = RuntimeProfile::load("storage_security", env_.data_dir);
    if (!check) {  // never leave an invalid overlay behind
      if (existed) (void)fsutil::atomic_write(file, before); else std::filesystem::remove(file, ec);
      rollback();
      return Error(Errc::Conflict, "new placement is rejected by the profile: " + check.error().message);
    }
    set_values(check->values());
  } else {
    Json v = values_;
    v["placement"][o.category] = placement_json;
    set_values(std::move(v));
  }

  // 4. optional source removal, only for objects verified on the target
  std::int64_t removed = 0;
  if (o.delete_source) {
    for (const auto& s : sources) {
      auto be = backend(s.storage);
      if (!be) continue;
      for (const auto& k : verified)
        if ((*be)->remove(o.category + "/" + k)) ++removed;
    }
  }
  report["copied"] = static_cast<std::int64_t>(created.size());
  report["already_present"] = skipped;
  report["bytes"] = bytes_total;
  report["removed_from_source"] = removed;
  report["placement"] = placement_json;
  return report;
}

Result<Json> StorageService::backup(const std::filesystem::path& file, const std::string& sid, std::string_view name) {
  LOOM_TRY_ASSIGN(auto be, backend(sid));
  LOOM_TRY_ASSIGN(std::string data, fsutil::read_file(file));
  const std::string key = "backups/" + std::string(name);
  if (!valid_key(key)) return Error(Errc::InvalidArgument, "invalid backup name");
  const std::string sha = Sha256::hex(data);
  LOOM_TRY(be->put(key, data, "application/octet-stream"));
  LOOM_TRY(verify_object(*be, key, sha));
  return Json{{"storage", sid}, {"key", key}, {"sha256", sha}, {"size", static_cast<std::int64_t>(data.size())}};
}

Result<Json> StorageService::backup_database(const std::string& sid) {
  if (!db_) return Error(Errc::Unavailable, "no database attached");
  const std::string name = "chatadhd-" + timeutil::utc_now_iso() + ".db";
  std::string safe = name;
  for (char& c : safe) if (c == ':') c = '-';
  std::filesystem::path tmp = env_.data_dir / ("backup-" + random_hex(8) + ".tmp.db");
  {
    auto lk = db_->lock();
    LOOM_TRY(db_->conn().run("VACUUM INTO ?", tmp.string()));  // consistent snapshot, WAL folded in
  }
  auto r = backup(tmp, sid, safe);
  std::error_code ec;
  std::filesystem::remove(tmp, ec);
  return r;
}

Json StorageService::describe() const {
  Json backends = Json::array();
  for (const auto& s : obj_at(values_, "storages").items()) {
    const Json& v = s.value();
    if (!v.is_object() || json::get_bool(v, "hidden", false)) continue;
    const Json* b = json::find(v, "backend");
    std::string kind = b && b->is_object() ? json::get_string(*b, "kind") : (json::get_string(v, "class") == "local" ? "local" : "");
    const KindDescriptor* d = kind.empty() ? nullptr : registry_->find(kind);
    Json e{{"id", s.key()}, {"class", json::get_string(v, "class")}, {"kind", kind.empty() ? Json(nullptr) : Json(kind)},
           {"adapter", d ? d->status : std::string("not_object_store")}};
    if (b && b->is_object()) {  // endpoint-level facts only; credentials are referenced, never shown
      for (const char* k : {"endpoint", "bucket", "region", "prefix", "url"}) if (b->contains(k)) e[k] = (*b)[k];
      const std::string ref = json::get_string(*b, "secret_ref", "storage_" + s.key());
      bool creds = false;
      if (env_.secrets) creds = env_.secrets->has(ref + "_access_key") || env_.secrets->has(ref + "_refresh_token") || env_.secrets->has(ref + "_user") || env_.secrets->has(ref + "_token");
      e["credentials_present"] = creds;
    }
    e["configured"] = json::get_bool(v, "configured", true);
    backends.push_back(std::move(e));
  }
  Json cats = Json::array();
  for (const auto& c : obj_at(values_, "categories").items()) {
    Json pl = Json::array();
    for (const auto& t : placement(c.key())) pl.push_back(Json{{"storage", t.storage}, {"role", t.role}});
    cats.push_back(Json{{"id", c.key()}, {"sensitivity", json::get_string(c.value(), "sensitivity")},
                        {"placement", pl}, {"default_placement", is_default_local(c.key())}});
  }
  Json kinds = Json::array();
  for (const auto& k : registry_->kinds()) kinds.push_back(Json{{"kind", k}, {"status", registry_->find(k)->status}});
  return Json{{"backends", backends}, {"categories", cats}, {"kinds", kinds}};
}

Json StorageService::probe_all() const {
  Json out = describe();
  for (auto& b : out["backends"]) {
    const std::string sid = b["id"].get<std::string>();
    if (!json::get_bool(b, "configured", true)) {
      b["reachable"] = Json{{"known", false}, {"reason", "template, not configured"}};
      continue;
    }
    auto be = backend(sid);
    if (!be) {
      b["reachable"] = Json{{"known", false}, {"reason", be.error().message}};
      continue;
    }
    auto st = (*be)->probe();
    b["reachable"] = st ? Json{{"known", true}, {"ok", true}} : Json{{"known", true}, {"ok", false}, {"error", st.error().message}};
  }
  return out;
}

Json StorageService::locate(std::string_view hash) const {
  Json out = Json::array();
  if (!db_) return out;
  auto lk = db_->lock();
  auto links = db_->get_links("blob:" + std::string(hash), "stored_in");
  if (!links) return out;
  for (const auto& l : *links) {
    auto n = db_->get_node(l.dst);
    std::string label = n && *n ? (*n)->label : l.dst;
    out.push_back(Json{{"storage", label}, {"metadata", l.metadata}});
  }
  return out;
}

}  // namespace loom::storage
