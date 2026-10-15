// Environments, capabilities and accesses in the graph. See include/loom/environments.h.
#include "loom/environments.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <set>

#include "loom/action_rules.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/log.h"
#include "loom/media_providers.h"
#include "loom/port_graph.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/storage_backend.h"
#include "loom/batch_api.h"
#include "loom/path_policy.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace loom::environments {

namespace fs = std::filesystem;

namespace {

constexpr const char* kLog = "loom.environments";

const Json kNoObject = Json::object();

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string label_of(const Json& label, const std::string& lang, const std::string& fallback) {
  if (label.is_string()) return label.get<std::string>();
  if (!label.is_object() || label.empty()) return fallback;
  for (const std::string& key : {lang, std::string("en")}) {
    auto it = label.find(key);
    if (it != label.end() && it->is_string()) return it->get<std::string>();
  }
  for (const auto& item : label.items())
    if (item.value().is_string()) return item.value().get<std::string>();
  return fallback;
}

const Json& object_at(const Json& v, const char* key) {
  const Json* x = json::find(v, key);
  return x && x->is_object() ? *x : kNoObject;
}

std::vector<std::string> strings(const Json* v) {
  std::vector<std::string> out;
  if (v && v->is_array())
    for (const auto& x : *v)
      if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}

// The platform this binary is built for, as the token the pack's `current_when_built_for` uses.
const char* built_for() {
#if defined(__ANDROID__)
  return "android";
#else
  return "default";
#endif
}

// ── the data ───────────────────────────────────────────────────────────

struct Data {
  Json env;         // graph_entry /environments
  Json devices;     // graph_entry /devices
  Json platforms;   // graph_entry /device_platforms
  Json storages;    // storage_security /storages
  Json declared;    // keys of the credentials declared in settings_fields (scope secret)
};

Result<Data> load_data(Runtime& rt) {
  Data d;
  const auto& root = rt.paths().root;
  LOOM_TRY_ASSIGN(auto entry, RuntimeProfile::load("graph_entry", root));
  const Json& v = entry.values();
  d.env = object_at(v, "environments");
  d.devices = object_at(v, "devices");
  d.platforms = object_at(v, "device_platforms");
  if (d.env.empty()) return Error(Errc::NotFound, "no environments in the graph_entry profile (data/graph/environments.pack)");
  if (auto storage = RuntimeProfile::load("storage_security", root)) d.storages = object_at(storage->values(), "storages");
  else d.storages = Json::object();
  d.declared = Json::array();
  if (auto fields = RuntimeProfile::load("settings_fields", root)) {
    if (const Json* list = json::find(fields->values(), "fields"); list && list->is_array())
      for (const auto& f : *list)
        if (json::get_string(f, "scope") == "secret") d.declared.push_back(json::get_string(f, "key"));
  }
  return d;
}

bool setting_bool(Runtime& rt, const Data& d, const char* name, bool fallback) {
  const Json cfg = rt.config().get(std::string("environments_") + name, nullptr);
  if (cfg.is_boolean()) return cfg.get<bool>();
  const Json& s = object_at(object_at(d.env, "settings"), name);
  return json::get_bool(s, "value", fallback);
}

// ── node helpers (one graph write path: the nodes and links tables) ────────

Result<std::string> put_node(Database& db, const std::string& label, const std::string& kind, Json metadata) {
  metadata = port_graph::redact(metadata);
  LOOM_TRY_ASSIGN(auto existing, db.find_node(label, kind));
  if (existing) {
    if (existing->metadata != metadata) {
      NodePatch patch;
      patch.metadata = metadata;
      LOOM_TRY(db.update_node(existing->id, patch));
    }
    return existing->id;
  }
  NodeOptions o;
  o.tags = Json::array({kind, "environment_artifact"});
  o.metadata = metadata;
  return db.get_or_create_node(label, kind, o);
}

Status put_link(Database& db, const std::string& src, const std::string& dst, const char* type, const Json& meta = Json::object()) {
  auto made = db.create_link(src, dst, type, 1.0, port_graph::redact(meta));
  if (!made) return made.error();
  return Status{};
}

// What a sync of one environment wrote: ids of the capability and access nodes it should have.
struct Written {
  std::set<std::string> keep;
  int capabilities = 0, accesses = 0;
};

// Removes the capability and access nodes an environment had that its sources no longer describe.
Result<int> prune(Database& db, const std::string& env_id, const Written& w) {
  LOOM_TRY_ASSIGN(auto links, db.get_links(env_id));
  int removed = 0;
  for (const auto& l : links) {
    if (l.src != env_id || (l.link_type != "has_capability" && l.link_type != "accessed_via")) continue;
    if (w.keep.count(l.dst)) continue;
    LOOM_TRY(db.delete_node(l.dst));
    ++removed;
  }
  return removed;
}

// ── credentials: references only ───────────────────────────────────────

// {secret_ref, state} for a key name. Only Secrets::has() is asked; the value is never read.
Json reference_of(Runtime& rt, const std::string& key) {
  return Json{{"secret_ref", key}, {"state", rt.secrets().has(key) ? "set" : "missing"}};
}

std::string credential_kind(const Data& d, const std::string& key) {
  const Json& kinds = object_at(d.env, "credential_kinds");
  const Json& suffixes = object_at(kinds, "suffixes");
  std::string best, best_kind;
  for (const auto& it : suffixes.items()) {
    const std::string& suf = it.key();
    if (key.size() >= suf.size() && key.compare(key.size() - suf.size(), suf.size(), suf) == 0 && suf.size() > best.size()) {
      best = suf;
      best_kind = it.value().is_string() ? it.value().get<std::string>() : std::string();
    }
  }
  return best_kind.empty() ? json::get_string(kinds, "default", "secret") : best_kind;
}

std::string aggregate_state(const Json& refs) {
  int set = 0, total = 0;
  for (const auto& r : refs) {
    ++total;
    if (json::get_string(r, "state") == "set") ++set;
  }
  if (total == 0) return "none";
  return set == total ? "set" : (set == 0 ? "missing" : "partial");
}

// ── the sync ───────────────────────────────────────────────────────────

class Sync {
 public:
  Sync(Runtime& rt, Data data) : rt_(rt), db_(rt.db()), d_(std::move(data)) {}

  Result<Json> run(const Json& request);

 private:
  struct Platform { std::string id; Json def; std::string env_id; };

  Result<std::string> vault();
  Status ports();
  Result<std::string> runtime_env(const std::string& platform, const Json& def);
  Result<std::string> directory_env(const fs::path& path, bool probe);
  Result<std::string> storage_env(const std::string& id, const Json& def);
  Result<std::string> service_env(const std::string& id, const Json& def);
  Status orphan_credentials();
  Result<std::string> capability(const std::string& env_id, const std::string& label, Json meta, Written& w);
  Result<std::string> access(const std::string& env_id, const std::string& label, Json meta, Written& w, bool in_vault);
  void summary(const std::string& node, const std::string& id, const std::string& kind, const std::string& cls, const std::string& label,
               const Written& w);

  Runtime& rt_;
  Database& db_;
  Data d_;
  std::string vault_id_;
  std::map<std::string, std::string> port_node_;               // port name -> node id
  std::map<std::string, std::vector<std::string>> adapters_;   // port name -> implemented adapter names
  std::map<std::string, std::string> runtime_nodes_;           // platform -> environment node id
  std::string data_dir_node_;
  std::set<std::string> credential_keys_seen_;
  Json out_ = Json::array();
  int removed_ = 0;
};

Result<std::string> Sync::vault() {
  if (!vault_id_.empty()) return vault_id_;
  const Json& v = object_at(d_.env, "vault");
  Json meta{{"schema", kEnvironmentSchema}, {"env_kind", "vault"}, {"file", json::get_string(v, "file", "secrets.json")},
            {"label", label_of(v.value("label", Json()), "en", "Secrets vault")}, {"holds_values", false}};
  LOOM_TRY_ASSIGN(vault_id_, put_node(db_, "vault:secrets", "secret_vault", std::move(meta)));
  return vault_id_;
}

Status Sync::ports() {
  auto add = [&](const char* name, std::vector<port_graph::AdapterInfo> infos) -> Status {
    LOOM_TRY(port_graph::record_adapters(db_, name, infos));
    LOOM_TRY_ASSIGN(port_node_[name], port_graph::port_node(db_, name));
    for (const auto& i : infos)
      if (i.status == "implemented") adapters_[name].push_back(i.name);
    return Status{};
  };
  LOOM_TRY(add("asr", port_graph::adapters_of(asr_adapters())));
  LOOM_TRY(add("ocr", port_graph::adapters_of(ocr_adapters())));
  LOOM_TRY(add("storage", port_graph::adapters_of(storage::BackendRegistry::builtin())));
  LOOM_TRY(add("batch", port_graph::adapters_of(SemanticBatchAPI::backends())));
  return Status{};
}

Result<std::string> Sync::capability(const std::string& env_id, const std::string& label, Json meta, Written& w) {
  meta["schema"] = kCapabilitySchema;
  LOOM_TRY_ASSIGN(const std::string id, put_node(db_, label, "capability", std::move(meta)));
  LOOM_TRY(put_link(db_, env_id, id, "has_capability"));
  w.keep.insert(id);
  ++w.capabilities;
  return id;
}

Result<std::string> Sync::access(const std::string& env_id, const std::string& label, Json meta, Written& w, bool in_vault) {
  meta["schema"] = kAccessSchema;
  LOOM_TRY_ASSIGN(const std::string id, put_node(db_, label, "access", std::move(meta)));
  LOOM_TRY(put_link(db_, env_id, id, "accessed_via"));
  if (in_vault) {
    LOOM_TRY_ASSIGN(const std::string v, vault());
    LOOM_TRY(put_link(db_, id, v, "stored_in"));
  }
  w.keep.insert(id);
  ++w.accesses;
  return id;
}

void Sync::summary(const std::string& node, const std::string& id, const std::string& kind, const std::string& cls,
                   const std::string& label, const Written& w) {
  out_.push_back(Json{{"id", id}, {"node", node}, {"kind", kind}, {"class", cls}, {"label", label},
                      {"capabilities", w.capabilities}, {"accesses", w.accesses}});
}

// The device availability of a platform: "available" | "permission" | "unavailable".
std::string availability(const Json& device, const std::string& platform) {
  return json::get_string(object_at(device, "availability"), platform, "unavailable");
}

Result<std::string> Sync::runtime_env(const std::string& platform, const Json& def) {
  const std::string cls = json::get_string(def, "class", "runtime_environment");
  const std::string label = label_of(object_at(d_.platforms, platform.c_str()).value("en", Json()), "en", platform);
  const std::string label_pl = label_of(object_at(d_.platforms, platform.c_str()).value("pl", Json()), "pl", label);
  const std::string cur = json::get_string(def, "current_when_built_for");
  const std::string eid = platform;
  Json meta{{"schema", kEnvironmentSchema}, {"env_kind", "runtime"}, {"platform", platform}, {"class", cls},
            {"label", Json{{"en", label}, {"pl", label_pl}}}, {"aliases", object_at(def, "aliases")},
            {"current", !cur.empty() && cur == built_for()}, {"hosts_ports", json::get_bool(def, "hosts_ports", false)}};
  LOOM_TRY_ASSIGN(const std::string env_id, put_node(db_, "environment:runtime:" + platform, "environment", std::move(meta)));
  runtime_nodes_[platform] = env_id;
  Written w;
  bool needs_permission = false;
  std::string permission_access;
  auto permission = [&]() -> Result<std::string> {
    if (!permission_access.empty()) return permission_access;
    Json m{{"kind", "os_permission"}, {"label", object_at(object_at(d_.env, "access_kinds"), "os_permission").value("label", Json())},
           {"secret_ref", nullptr}, {"state", "granted_by_user"}, {"env", "runtime:" + platform}};
    LOOM_TRY_ASSIGN(permission_access, access(env_id, "access:runtime:" + platform + ":os_permission", std::move(m), w, false));
    return permission_access;
  };
  for (const auto& it : d_.devices.items()) {
    const Json& dev = it.value();
    if (!dev.is_object() || json::get_bool(dev, "hidden", false)) continue;
    const std::string av = availability(dev, platform);
    if (av != "available" && av != "permission") continue;
    Json cm{{"source", "devices"}, {"device", it.key()}, {"element_class", json::get_string(dev, "class", it.key())},
            {"availability", av}, {"label", dev.value("label", Json())}, {"env", "runtime:" + platform}};
    LOOM_TRY_ASSIGN(const std::string cap, capability(env_id, "capability:runtime:" + platform + ":" + it.key(), std::move(cm), w));
    if (av == "permission") {
      needs_permission = true;
      LOOM_TRY_ASSIGN(const std::string acc, permission());
      LOOM_TRY(put_link(db_, cap, acc, "requires_access"));
    }
  }
  (void)needs_permission;
  if (json::get_bool(def, "hosts_ports", false)) {
    for (const auto& it : object_at(d_.env, "ports").items()) {
      const std::string port = it.key();
      auto node = port_node_.find(port);
      if (node == port_node_.end()) continue;
      Json cm{{"source", "ports"}, {"port", port}, {"element_class", json::get_string(it.value(), "element_class", "port")},
              {"availability", "available"}, {"adapters", adapters_[port]}, {"env", "runtime:" + platform},
              {"label", Json{{"en", "Port " + port}, {"pl", "Port " + port}}}};
      LOOM_TRY_ASSIGN(const std::string cap, capability(env_id, "capability:runtime:" + platform + ":port:" + port, std::move(cm), w));
      LOOM_TRY(put_link(db_, cap, node->second, "provided_by"));
    }
  }
  const Json& acc = object_at(def, "access");
  if (!acc.empty()) {
    const std::string kind = json::get_string(acc, "kind", "physical");
    Json am{{"kind", kind}, {"label", acc.value("label", Json())}, {"secret_ref", nullptr}, {"state", "implied"}, {"env", "runtime:" + platform}};
    LOOM_TRY(access(env_id, "access:runtime:" + platform + ":" + kind, std::move(am), w, false));
  }
  LOOM_TRY_ASSIGN(const int gone, prune(db_, env_id, w));
  removed_ += gone;
  summary(env_id, "runtime:" + platform, "runtime", cls, label, w);
  return env_id;
}

// A directory is described by what the operating system says about it: nothing in it is read.
Result<std::string> Sync::directory_env(const fs::path& path_in, bool probe) {
  std::error_code ec;
  fs::path path = fs::weakly_canonical(path_in, ec);
  if (ec) path = path_in;
  const fs::path data_root = [&] {
    std::error_code e2;
    auto c = fs::weakly_canonical(rt_.paths().root, e2);
    return e2 ? rt_.paths().root : c;
  }();
  const bool is_data = path == data_root;
  bool exists = false, readable = false, writable = false, is_dir = false;
  if (probe || is_data) {
    exists = fs::exists(path, ec);
    is_dir = exists && fs::is_directory(path, ec);
#if !defined(_WIN32)
    readable = exists && ::access(path.c_str(), R_OK) == 0;
    writable = exists && ::access(path.c_str(), W_OK) == 0;
#else
    const auto perms = exists ? fs::status(path, ec).permissions() : fs::perms::none;
    readable = exists;
    writable = exists && (perms & fs::perms::owner_write) != fs::perms::none;
#endif
  }
  const Json& dir = object_at(d_.env, "directory");
  const std::string cls = json::get_string(dir, "class", "directory_environment");
  const std::string key = path.generic_string();
  const std::string label = is_data ? "Data directory" : key;
  Json meta{{"schema", kEnvironmentSchema}, {"env_kind", "directory"}, {"class", cls}, {"path", key},
            {"label", Json{{"en", is_data ? "Data directory" : key}, {"pl", is_data ? "Katalog danych" : key}}},
            {"aliases", is_data ? object_at(dir, "aliases") : Json::object()},
            {"exists", exists}, {"is_directory", is_dir}, {"readable", readable}, {"writable", writable},
            {"data_directory", is_data}, {"probed", probe || is_data}};
  LOOM_TRY_ASSIGN(const std::string env_id, put_node(db_, "environment:directory:" + key, "environment", std::move(meta)));
  if (is_data) data_dir_node_ = env_id;
  Written w;
  const std::map<std::string, bool> facts{{"exists", exists && is_dir}, {"readable", readable && is_dir}, {"writable", writable && is_dir},
                                           {"data_directory", is_data}};
  for (const auto& it : object_at(dir, "capabilities").items()) {
    bool ok = true;
    for (const auto& cond : strings(json::find(it.value(), "when"))) {
      auto f = facts.find(cond);
      ok = ok && f != facts.end() && f->second;
    }
    if (!ok) continue;
    Json cm{{"source", "directory"}, {"what", it.key()}, {"element_class", json::get_string(it.value(), "element_class", "file_access")},
            {"availability", "available"}, {"label", it.value().value("label", Json())}, {"env", "directory:" + key}};
    LOOM_TRY(capability(env_id, "capability:directory:" + key + ":" + it.key(), std::move(cm), w));
  }
  const Json& acc = object_at(dir, "access");
  if (!acc.empty()) {
    const std::string kind = json::get_string(acc, "kind", "file_permission");
    Json am{{"kind", kind}, {"label", acc.value("label", Json())}, {"secret_ref", nullptr}, {"state", "implied"}, {"env", "directory:" + key}};
    LOOM_TRY(access(env_id, "access:directory:" + key + ":" + kind, std::move(am), w, false));
  }
  if (is_data)
    for (const auto& r : runtime_nodes_) LOOM_TRY(put_link(db_, env_id, r.second, "hosted_on"));
  LOOM_TRY_ASSIGN(const int gone, prune(db_, env_id, w));
  removed_ += gone;
  summary(env_id, "directory:" + key, "directory", cls, label, w);
  return env_id;
}

Result<std::string> Sync::storage_env(const std::string& id, const Json& def) {
  const Json& conf = object_at(d_.env, "storage");
  const std::string cls = json::get_string(conf, "class", "storage_environment");
  const std::string label_en = label_of(def.value("label", Json()), "en", id);
  const std::string label_pl = label_of(def.value("label", Json()), "pl", label_en);
  std::vector<std::string> platforms;
  const auto available_on = strings(json::find(def, "available_on"));
  for (const auto& p : object_at(conf, "platform_map").items()) {
    const auto os = strings(&p.value());
    if (std::any_of(os.begin(), os.end(), [&](const std::string& o) { return std::find(available_on.begin(), available_on.end(), o) != available_on.end(); }))
      platforms.push_back(p.key());
  }
  const Json* backend = json::find(def, "backend");
  Json meta{{"schema", kEnvironmentSchema}, {"env_kind", "storage"}, {"class", cls}, {"storage_id", id},
            {"storage_class", json::get_string(def, "class")}, {"label", Json{{"en", label_en}, {"pl", label_pl}}},
            {"platforms", platforms}, {"backend_kind", backend ? json::get_string(*backend, "kind") : std::string()},
            {"path", json::get_string(def, "path")}};
  LOOM_TRY_ASSIGN(const std::string env_id, put_node(db_, "environment:storage:" + id, "environment", std::move(meta)));
  Written w;
  Json cm{{"source", "storage"}, {"storage", id}, {"element_class", json::get_string(conf, "capability_class", "storage_backend")},
          {"operations", def.value("capabilities", Json::array())}, {"availability", "available"},
          {"label", Json{{"en", "Keep data here"}, {"pl", "Trzymaj tu dane"}}}, {"env", "storage:" + id}};
  LOOM_TRY(capability(env_id, "capability:storage:" + id + ":backend", std::move(cm), w));
  // Credentials: the key names this storage refers to. Nothing but names and whether they are set.
  Json refs = Json::array();
  std::string prefix;
  if (backend) {
    prefix = json::get_string(*backend, "secret_ref");
    if (!prefix.empty()) {
      std::vector<std::string> keys;
      for (const auto& k : rt_.secrets().keys())
        if (k.rfind(prefix + "_", 0) == 0) keys.push_back(k);
      std::sort(keys.begin(), keys.end());
      for (const auto& k : keys) refs.push_back(reference_of(rt_, k));
    }
  }
  const std::string custody = json::get_string(object_at(def, "encryption"), "key_custody");
  const std::string cprefix = json::get_string(conf, "key_custody_prefix", "secrets.json:");
  if (custody.rfind(cprefix, 0) == 0) refs.push_back(reference_of(rt_, custody.substr(cprefix.size())));
  if (!refs.empty() || !prefix.empty()) {
    const std::string bkind = backend ? json::get_string(*backend, "kind") : std::string();
    std::string kind = json::get_string(object_at(conf, "access_kinds"), bkind);
    if (kind.empty() && !refs.empty()) kind = credential_kind(d_, json::get_string(refs[0], "secret_ref"));
    if (kind.empty()) kind = "secret";
    Json am{{"kind", kind}, {"label", object_at(object_at(d_.env, "access_kinds"), kind.c_str()).value("label", Json())},
            {"refs", refs}, {"ref_prefix", prefix.empty() ? Json(nullptr) : Json(prefix)},
            {"state", prefix.empty() || !refs.empty() ? aggregate_state(refs) : "missing"}, {"env", "storage:" + id}};
    for (const auto& r : refs) credential_keys_seen_.insert(json::get_string(r, "secret_ref"));
    LOOM_TRY(access(env_id, "access:storage:" + id + ":" + kind, std::move(am), w, true));
  }
  for (const auto& p : platforms) {
    auto r = runtime_nodes_.find(p);
    if (r != runtime_nodes_.end()) LOOM_TRY(put_link(db_, env_id, r->second, "hosted_on"));
  }
  if (json::get_string(def, "path") == "{dir}" && !data_dir_node_.empty()) LOOM_TRY(put_link(db_, env_id, data_dir_node_, "hosted_on"));
  LOOM_TRY_ASSIGN(const int gone, prune(db_, env_id, w));
  removed_ += gone;
  summary(env_id, "storage:" + id, "storage", cls, label_en, w);
  return env_id;
}

Result<std::string> Sync::service_env(const std::string& id, const Json& def) {
  const std::string cls = json::get_string(d_.env, "service_class", "service_environment");
  const std::string label_en = label_of(def.value("label", Json()), "en", id);
  const std::string label_pl = label_of(def.value("label", Json()), "pl", label_en);
  Json meta{{"schema", kEnvironmentSchema}, {"env_kind", "service"}, {"class", cls}, {"service", id},
            {"label", Json{{"en", label_en}, {"pl", label_pl}}}};
  LOOM_TRY_ASSIGN(const std::string env_id, put_node(db_, "environment:service:" + id, "environment", std::move(meta)));
  Written w;
  for (const auto& port : strings(json::find(def, "ports"))) {
    LOOM_TRY_ASSIGN(const std::string pn, [&]() -> Result<std::string> {
      auto f = port_node_.find(port);
      if (f != port_node_.end()) return f->second;
      return port_graph::port_node(db_, port);
    }());
    const Json* pc = json::find(object_at(d_.env, "ports"), port);
    Json cm{{"source", "service"}, {"port", port}, {"element_class", pc ? json::get_string(*pc, "element_class", "port") : std::string("port")},
            {"availability", "available"}, {"label", Json{{"en", "Port " + port}, {"pl", "Port " + port}}}, {"env", "service:" + id}};
    LOOM_TRY_ASSIGN(const std::string cap, capability(env_id, "capability:service:" + id + ":port:" + port, std::move(cm), w));
    LOOM_TRY(put_link(db_, cap, pn, "provided_by"));
  }
  Json refs = Json::array();
  for (const auto& k : strings(json::find(def, "keys"))) {
    refs.push_back(reference_of(rt_, k));
    credential_keys_seen_.insert(k);
  }
  if (!refs.empty()) {
    const std::string kind = credential_kind(d_, json::get_string(refs[0], "secret_ref"));
    Json am{{"kind", kind}, {"label", object_at(object_at(d_.env, "access_kinds"), kind.c_str()).value("label", Json())},
            {"refs", refs}, {"state", aggregate_state(refs)}, {"env", "service:" + id}};
    LOOM_TRY(access(env_id, "access:service:" + id + ":" + kind, std::move(am), w, true));
  }
  LOOM_TRY_ASSIGN(const int gone, prune(db_, env_id, w));
  removed_ += gone;
  summary(env_id, "service:" + id, "service", cls, label_en, w);
  return env_id;
}

// Declared credentials no environment above claimed: they still exist, so they have an access node in the vault.
Status Sync::orphan_credentials() {
  LOOM_TRY_ASSIGN(const std::string v, vault());
  Json refs = Json::array();
  for (const auto& k : d_.declared) {
    const std::string key = k.get<std::string>();
    if (credential_keys_seen_.count(key)) continue;
    refs.push_back(reference_of(rt_, key));
  }
  // One access node per vault-level credential, attached to the vault itself.
  std::set<std::string> keep;
  for (const auto& r : refs) {
    const std::string key = json::get_string(r, "secret_ref");
    Json am{{"schema", kAccessSchema}, {"kind", credential_kind(d_, key)}, {"refs", Json::array({r})}, {"state", json::get_string(r, "state")},
            {"env", "vault"}};
    LOOM_TRY_ASSIGN(const std::string id, put_node(db_, "access:vault:" + key, "access", std::move(am)));
    LOOM_TRY(put_link(db_, v, id, "accessed_via"));
    LOOM_TRY(put_link(db_, id, v, "stored_in"));
    keep.insert(id);
  }
  LOOM_TRY_ASSIGN(auto links, db_.get_links(v, "accessed_via"));  // a credential some environment claims now is not vault-level any more
  for (const auto& l : links)
    if (l.src == v && !keep.count(l.dst)) LOOM_TRY(db_.delete_node(l.dst));
  return Status{};
}

Result<Json> Sync::run(const Json& request) {
  LOOM_TRY(ports());
  LOOM_TRY(vault());
  for (const auto& it : object_at(d_.env, "runtimes").items()) {
    if (!it.value().is_object() || json::get_bool(it.value(), "hidden", false)) continue;
    LOOM_TRY(runtime_env(it.key(), it.value()));
  }
  const bool probe = setting_bool(rt_, d_, "probe_paths", true);
  LOOM_TRY(directory_env(rt_.paths().root, true));
  const std::string extra = json::get_string(request, "path");
  if (!extra.empty()) {
    LOOM_TRY(rt_.path_policy().check(extra, "path"));
    LOOM_TRY(directory_env(fs::path(extra), probe));
  }
  const Json& conf = object_at(d_.env, "storage");
  const Json& skip = object_at(conf, "skip_when");
  for (const auto& it : d_.storages.items()) {
    const Json& def = it.value();
    if (!def.is_object() || json::get_bool(def, "hidden", false)) continue;
    bool skipped = false;
    for (const auto& s : skip.items()) skipped = skipped || (def.contains(s.key()) && def[s.key()] == s.value());
    if (skipped) continue;
    LOOM_TRY(storage_env(it.key(), def));
  }
  // Services first claim their keys, so only the rest are vault-level.
  for (const auto& it : object_at(d_.env, "services").items()) {
    if (!it.value().is_object() || json::get_bool(it.value(), "hidden", false)) continue;
    LOOM_TRY(service_env(it.key(), it.value()));
  }
  LOOM_TRY(orphan_credentials());
  return Json{{"schema", kSyncSchema}, {"environments", out_}, {"removed", removed_}};
}

// ── reading the graph ──────────────────────────────────────────────────

struct EnvView {
  Node node;
  std::vector<Node> capabilities, accesses;
};

std::string lang_of(const Json& request) { return json::get_string(request, "lang", "en"); }

Result<std::vector<Node>> neighbours(Database& db, const std::string& env_id, const char* link_type) {
  LOOM_TRY_ASSIGN(auto links, db.get_links(env_id, link_type));
  std::vector<Node> out;
  for (const auto& l : links) {
    if (l.src != env_id) continue;
    LOOM_TRY_ASSIGN(auto n, db.get_node(l.dst));
    if (n) out.push_back(std::move(*n));
  }
  std::sort(out.begin(), out.end(), [](const Node& a, const Node& b) { return a.label < b.label; });
  return out;
}

Result<EnvView> view_of(Database& db, Node node) {
  EnvView v;
  LOOM_TRY_ASSIGN(v.capabilities, neighbours(db, node.id, "has_capability"));
  LOOM_TRY_ASSIGN(v.accesses, neighbours(db, node.id, "accessed_via"));
  v.node = std::move(node);
  return v;
}

Json access_json(const Node& n, const std::string& lang) {
  const Json& m = n.metadata;
  Json out{{"node", n.id}, {"id", n.label}, {"kind", json::get_string(m, "kind")}, {"state", json::get_string(m, "state")},
           {"label", label_of(m.value("label", Json()), lang, json::get_string(m, "kind"))}};
  if (const Json* refs = json::find(m, "refs")) out["refs"] = *refs;
  if (const Json* p = json::find(m, "ref_prefix"); p && !p->is_null()) out["ref_prefix"] = *p;
  return out;
}

Json capability_json(const Node& n, const std::string& lang) {
  const Json& m = n.metadata;
  Json out{{"node", n.id}, {"id", n.label}, {"element_class", json::get_string(m, "element_class")},
           {"availability", json::get_string(m, "availability", "available")}, {"source", json::get_string(m, "source")},
           {"label", label_of(m.value("label", Json()), lang, n.label)}};
  for (const char* k : {"adapters", "operations"})
    if (const Json* v = json::find(m, k)) out[k] = *v;
  return out;
}

std::string env_label(const Node& n, const std::string& lang) { return label_of(n.metadata.value("label", Json()), lang, n.label); }

// The id a user sees ("runtime:android"): the node label without its "environment:" prefix.
std::string short_id(const Node& n) { return n.label.rfind("environment:", 0) == 0 ? n.label.substr(12) : n.label; }

Json env_summary(const EnvView& v, const std::string& lang) {
  Json caps = Json::array(), accs = Json::array();
  for (const auto& c : v.capabilities) caps.push_back(capability_json(c, lang));
  for (const auto& a : v.accesses) accs.push_back(access_json(a, lang));
  return Json{{"id", short_id(v.node)}, {"node", v.node.id}, {"kind", json::get_string(v.node.metadata, "env_kind")},
              {"class", json::get_string(v.node.metadata, "class")}, {"label", env_label(v.node, lang)},
              {"current", json::get_bool(v.node.metadata, "current", false)}, {"capabilities", caps}, {"accesses", accs}};
}

Result<std::vector<Node>> all_environments(Database& db) {
  LOOM_TRY_ASSIGN(auto nodes, db.list_nodes("environment", 100000));
  std::sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) { return a.label < b.label; });
  return nodes;
}

std::string norm(const std::string& s) {
  std::string out;
  for (char c : lower(s)) out.push_back(c == '_' ? ' ' : c);
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

// A word the user said, a platform, a node id or a label -> the environment node. Aliases are data of the node.
Result<std::optional<Node>> resolve(Database& db, const std::string& word, const Json& request, std::string& how) {
  LOOM_TRY_ASSIGN(auto nodes, all_environments(db));
  const std::string w = norm(word);
  auto matches = [&](const Node& n) -> bool {
    if (norm(n.label) == w || norm(short_id(n)) == w || n.id == word) return true;
    const Json& m = n.metadata;
    if (norm(json::get_string(m, "platform")) == w && !w.empty()) return true;
    if (norm(json::get_string(m, "storage_id")) == w && !w.empty()) return true;
    if (norm(json::get_string(m, "service")) == w && !w.empty()) return true;
    if (const Json* lab = json::find(m, "label"); lab && lab->is_object())
      for (const auto& l : lab->items())
        if (l.value().is_string() && norm(l.value().get<std::string>()) == w) return true;
    if (const Json* al = json::find(m, "aliases"); al && al->is_object())
      for (const auto& lang : al->items())
        for (const auto& a : strings(&lang.value()))
          if (norm(a) == w) return true;
    return false;
  };
  for (const auto& n : nodes)
    if (matches(n)) {
      how = "named";
      return std::optional<Node>(n);
    }
  // A sentence ("my phone", "in this directory") contains the alias: the longest alias found wins.
  const Node* best = nullptr;
  std::size_t best_len = 0;
  for (const auto& n : nodes) {
    const Json* al = json::find(n.metadata, "aliases");
    if (!al || !al->is_object()) continue;
    for (const auto& lang : al->items())
      for (const auto& a : strings(&lang.value())) {
        const std::string na = norm(a);
        if (na.size() > best_len && (" " + w + " ").find(" " + na + " ") != std::string::npos) {
          best = &n;
          best_len = na.size();
        }
      }
  }
  (void)request;
  if (best) {
    how = "alias";
    return std::optional<Node>(*best);
  }
  return std::optional<Node>();
}

int status_rank(const std::string& s) {
  if (s == "default" || s == "applicable") return 3;
  if (s == "needs_confirmation") return 2;
  if (s == "denied") return 1;
  return 0;
}

}  // namespace

Result<Json> sync(Runtime& rt, const Json& request) {
  LOOM_TRY_ASSIGN(auto data, load_data(rt));
  Sync s(rt, std::move(data));
  return s.run(request.is_object() ? request : kNoObject);
}

Result<Json> list(Runtime& rt, const Json& request) {
  LOOM_TRY_ASSIGN(auto data, load_data(rt));
  const bool want_sync = request.contains("sync") ? json::get_bool(request, "sync") : setting_bool(rt, data, "auto_sync", true);
  if (want_sync) LOOM_TRY(sync(rt, request));
  const std::string lang = lang_of(request);
  LOOM_TRY_ASSIGN(auto nodes, all_environments(rt.db()));
  Json envs = Json::array();
  for (auto& n : nodes) {
    LOOM_TRY_ASSIGN(auto v, view_of(rt.db(), std::move(n)));
    envs.push_back(env_summary(v, lang));
  }
  return port_graph::redact(Json{{"schema", kListSchema}, {"environments", envs}});
}

Result<Json> can_do(Runtime& rt, const Json& request) {
  const Json& req = request.is_object() ? request : kNoObject;
  LOOM_TRY_ASSIGN(auto data, load_data(rt));
  Database& db = rt.db();
  const std::string lang = lang_of(req);
  const bool want_sync = req.contains("sync") ? json::get_bool(req, "sync") : setting_bool(rt, data, "auto_sync", true);
  const std::string path = json::get_string(req, "path");
  // A directory the graph does not know yet is added by the sync; the answer is read from the graph either way.
  if (want_sync || !path.empty()) LOOM_TRY(sync(rt, path.empty() ? kNoObject : Json{{"path", path}}));
  std::string how = "current";
  std::optional<Node> found;
  if (!path.empty()) {
    std::error_code ec;
    fs::path canon = fs::weakly_canonical(path, ec);
    LOOM_TRY_ASSIGN(found, db.find_node("environment:directory:" + (ec ? path : canon.generic_string()), "environment"));
    how = "path";
  } else if (const std::string word = json::get_string(req, "environment"); !word.empty()) {
    LOOM_TRY_ASSIGN(found, resolve(db, word, req, how));
    if (!found) {
      LOOM_TRY_ASSIGN(auto all, all_environments(db));
      Json known = Json::array();
      for (const auto& n : all) known.push_back(short_id(n));
      return Error(Errc::NotFound, "no environment '" + word + "' in the graph (known: " + json::dump(known) + ")");
    }
  } else {
    LOOM_TRY_ASSIGN(auto all, all_environments(db));
    for (auto& n : all)
      if (json::get_bool(n.metadata, "current", false)) { found = n; break; }
  }
  if (!found) return Error(Errc::NotFound, "the environment is not in the graph (run environments.sync)");
  LOOM_TRY_ASSIGN(auto view, view_of(db, std::move(*found)));

  // The actions that fit: for the environment as such and for the class each capability points at.
  LOOM_TRY_ASSIGN(auto engine, actions::Engine::load(rt.paths().root));
  engine.set_user_values(rt.config().all());
  const bool include_unmatched = json::get_bool(req, "include_unmatched", false);
  Json options = Json{{"include_unmatched", false}};
  std::map<std::string, Json> rows;
  std::map<std::string, std::pair<int, int>> ways;  // action -> {ways to do it, ways that need an access first}
  std::map<std::string, std::set<std::string>> gates;
  std::vector<std::string> order;
  Json issues = Json::array();
  auto absorb = [&](const std::string& cls, const Json& via, const Json& needs) -> Status {
    auto answer = engine.actions_for(Json{{"class", cls}}, Json(), options);
    if (!answer) return answer.error();
    for (const auto& i : (*answer)["issues"]) issues.push_back(i);
    for (const auto& row : (*answer)["actions"]) {
      const std::string id = json::get_string(row, "action");
      auto it = rows.find(id);
      if (it == rows.end()) {
        Json r{{"action", id}, {"title", json::get_string(row, "title", id)}, {"implementation", json::get_string(row, "implementation")},
               {"status", json::get_string(row, "status")}, {"effects", row.value("effects", Json::object())},
               {"default", json::get_bool(row, "default", false)}, {"via", Json::array()}};
        if (const Json* c = json::find(row, "callable")) r["callable"] = *c;
        it = rows.emplace(id, std::move(r)).first;
        order.push_back(id);
      } else if (status_rank(json::get_string(row, "status")) > status_rank(json::get_string(it->second, "status"))) {
        it->second["status"] = row.value("status", Json());
      }
      it->second["via"].push_back(via);
      ++ways[id].first;
      if (needs.is_array() && !needs.empty()) {
        ++ways[id].second;
        for (const auto& n : needs) gates[id].insert(n.get<std::string>());
      }
    }
    return Status{};
  };
  const std::string env_class = json::get_string(view.node.metadata, "class");
  LOOM_TRY(absorb(env_class, Json{{"kind", "environment"}, {"id", short_id(view.node)}, {"class", env_class}}, Json::array()));
  Json caps = Json::array();
  std::set<std::string> classes_done;
  for (const auto& c : view.capabilities) {
    Json cj = capability_json(c, lang);
    const std::string cls = json::get_string(c.metadata, "element_class");
    // The accesses this capability requires (an operating-system permission).
    Json needs = Json::array();
    LOOM_TRY_ASSIGN(auto required, neighbours(db, c.id, "requires_access"));
    for (const auto& a : required) needs.push_back(a.label);
    if (!needs.empty()) cj["requires_access"] = needs;
    caps.push_back(cj);
    if (cls.empty()) continue;
    LOOM_TRY(absorb(cls, Json{{"kind", "capability"}, {"id", c.label}, {"class", cls}}, needs));
  }
  Json actions = Json::array();
  std::map<std::string, int> by_status;
  for (const auto& id : order) {
    Json r = rows[id];
    // Doable only after an access (every way to do it needs one): say which.
    if (ways[id].second == ways[id].first && !gates[id].empty()) r["needs_access"] = Json(std::vector<std::string>(gates[id].begin(), gates[id].end()));
    ++by_status[json::get_string(r, "status")];
    actions.push_back(std::move(r));
  }
  Json accs = Json::array();
  for (const auto& a : view.accesses) accs.push_back(access_json(a, lang));
  Json counts{{"actions", actions.size()}, {"capabilities", caps.size()}, {"accesses", accs.size()}};
  Json statuses = Json::object();
  for (const auto& s : by_status) statuses[s.first] = s.second;
  counts["by_status"] = statuses;
  (void)include_unmatched;
  Json env = env_summary(view, lang);
  env.erase("capabilities");
  env.erase("accesses");
  env["resolved"] = how;
  return port_graph::redact(Json{{"schema", kCanDoSchema}, {"environment", env}, {"capabilities", caps}, {"accesses", accs},
                                  {"actions", actions}, {"counts", counts}, {"issues", issues}, {"from_graph", true}});
}

}  // namespace loom::environments
