#include "loom/port_graph.h"

#include <algorithm>
#include <cctype>

#include "loom/log.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"

namespace loom::port_graph {

namespace {

constexpr const char* kLog = "loom.port_graph";
thread_local std::string t_run_id;

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool looks_secret_key(const std::string& key) {
  const std::string k = lower(key);
  for (const char* w : {"secret", "token", "password", "passwd", "apikey", "api_key", "authorization", "bearer", "credential", "private_key"})
    if (k.find(w) != std::string::npos) return true;
  return false;
}
bool is_reference_key(const std::string& key) { return key == "secret_key" || key == "secret_ref" || key == "credential_ref"; }

bool looks_secret_value(const std::string& v) {
  if (v.size() < 20) return false;
  for (const char* p : {"sk-", "gsk_", "ghp_", "xoxb-", "AIza", "Bearer "})
    if (v.rfind(p, 0) == 0) return true;
  return false;
}

Json redact_impl(const Json& v, const std::string& key) {
  if (v.is_object()) {
    Json o = Json::object();
    for (auto it = v.begin(); it != v.end(); ++it) o[it.key()] = redact_impl(it.value(), it.key());
    return o;
  }
  if (v.is_array()) {
    Json a = Json::array();
    for (const auto& x : v) a.push_back(redact_impl(x, key));
    return a;
  }
  if (!key.empty() && looks_secret_key(key) && !is_reference_key(key) && !v.is_null() && !v.is_number() && !v.is_boolean()) return Json("[redacted]");  // counts such as "tokens": 5 are not secrets
  if (v.is_string() && looks_secret_value(v.get<std::string>())) return "[redacted]";
  return v;
}

Result<std::string> node(Database& db, const std::string& label, const std::string& kind, const std::string& content, Json metadata) {
  NodeOptions o;
  o.content = content;
  o.tags = Json::array({kind, "port_artifact"});
  o.metadata = redact(metadata);
  return db.get_or_create_node(label, kind, o);
}

Status link(Database& db, const std::string& src, const std::string& dst, const char* type, const Json& meta = Json::object()) {
  auto made = db.create_link(src, dst, type, 1.0, redact(meta));
  if (!made) return made.error();
  return Status{};
}

std::string role_port(const std::string& role) {
  const auto dot = role.find('.');
  return dot == std::string::npos ? role : role.substr(0, dot);
}

// Adapter names a role entry (adapter or composition, nested) refers to.
void member_names(const Json& entry, std::vector<std::string>& out) {
  if (entry.is_string()) { out.push_back(entry.get<std::string>()); return; }
  if (!entry.is_object()) return;
  if (const std::string a = json::get_string(entry, "adapter"); !a.empty()) out.push_back(a);
  if (const Json* m = json::find(entry, "members"); m && m->is_array())
    for (const auto& x : *m) member_names(x, out);
}

Result<std::string> named_adapter(Database& db, const std::string& port, const std::string& name) {
  auto found = db.find_node("adapter:" + port + ":" + name, "adapter");
  if (!found) return found.error();
  if (*found) return (*found)->id;
  // Named by a profile but not in the registry (a descriptor id such as "groq"): honest status, no invention.
  return adapter_node(db, port, AdapterInfo{name, "unregistered", Json::object()});
}

}  // namespace

Json redact(const Json& value) { return redact_impl(value, ""); }

namespace {
thread_local ActionRunScope* t_scope = nullptr;
}
ActionRunScope::ActionRunScope(std::string run_id) : previous_(t_run_id), run_id_(run_id), outer_(t_scope) {
  t_run_id = std::move(run_id);
  t_scope = this;
}
ActionRunScope::~ActionRunScope() {
  t_run_id = previous_;
  t_scope = outer_;
}
std::string ActionRunScope::current() { return t_run_id; }

Result<std::string> port_node(Database& db, const std::string& port) {
  return node(db, "port:" + port, "port", "", Json{{"schema", kAdapterSchema}, {"port", port}});
}

Result<std::string> adapter_node(Database& db, const std::string& port, const AdapterInfo& a) {
  LOOM_TRY_ASSIGN(const std::string pnode, port_node(db, port));
  Json meta{{"schema", kAdapterSchema}, {"port", port}, {"name", a.name}, {"status", a.status}, {"meta", a.meta}};
  LOOM_TRY_ASSIGN(const std::string id, node(db, "adapter:" + port + ":" + a.name, "adapter", "", std::move(meta)));
  LOOM_TRY(link(db, id, pnode, "implements"));
  return id;
}

Result<std::vector<std::string>> record_adapters(Database& db, const std::string& port, const std::vector<AdapterInfo>& adapters) {
  std::vector<std::string> ids;
  for (const auto& a : adapters) {
    LOOM_TRY_ASSIGN(auto id, adapter_node(db, port, a));
    ids.push_back(std::move(id));
  }
  return ids;
}

Result<std::string> record_role_choice(Database& db, const Json& resolved, const std::string& port_in) {
  const std::string role = json::get_string(resolved, "role");
  if (role.empty() || !json::get_bool(resolved, "defined", true)) return std::string();
  const std::string port = port_in.empty() ? role_port(role) : port_in;
  const std::string scope = json::get_string(resolved, "scope");
  const std::string scope_id = json::get_string(resolved, "scope_id");
  // PortProfile::resolve() carries the whole role entry under "entry" (members, quorum, fuse ...).
  Json entry = Json::object();
  if (const Json* e = json::find(resolved, "entry"); e && e->is_object()) entry = *e;
  for (const char* k : {"adapter", "compose", "members", "quorum", "fuse", "params", "setting"})
    if (const Json* x = json::find(resolved, k); x && !entry.contains(k)) entry[k] = *x;
  // The human texts of a role (ports.pack label, why, status) describe the job; they are not part of the choice.
  for (const char* k : {"label", "why", "status"}) entry.erase(k);
  entry = redact(entry);
  // The same choice is the same node; a changed choice (another adapter, params, scope) is a new one.
  const std::string digest = Sha256::hex(json::canonical(entry)).substr(0, 12);
  const std::string label = "role_choice:" + role + ":" + scope + (scope_id.empty() ? "" : ":" + scope_id) + ":" + digest;
  Json meta{{"schema", kRoleChoiceSchema}, {"role", role}, {"port", port}, {"scope", scope},
            {"scope_id", scope_id.empty() ? Json(nullptr) : Json(scope_id)},
            {"source", json::get_string(resolved, "source")}, {"entry", entry}};
  if (const std::string f = json::get_string(resolved, "file"); !f.empty()) meta["file"] = f;
  LOOM_TRY_ASSIGN(const std::string id, node(db, label, "role_choice", "", std::move(meta)));
  std::vector<std::string> names;
  member_names(entry, names);
  for (const auto& n : names) {
    LOOM_TRY_ASSIGN(const std::string a, named_adapter(db, port, n));
    LOOM_TRY(link(db, id, a, "chose", Json{{"role", role}, {"scope", scope}}));
  }
  return id;
}

Result<std::vector<std::string>> record_role_choices(Database& db, const PortProfile& profile, const Json& conversation,
                                                     const std::string& conversation_id, const Json& settings) {
  std::vector<std::string> ids;
  for (const auto& role : profile.roles()) {
    auto r = profile.resolve(role.get<std::string>(), conversation, conversation_id, settings);
    if (!r) { log::debug(kLog, "role {} not resolved: {}", role.get<std::string>(), r.error().message); continue; }
    LOOM_TRY_ASSIGN(auto id, record_role_choice(db, *r));
    if (!id.empty()) ids.push_back(std::move(id));
  }
  return ids;
}

PortRun port_run_from_model_use(const Json& rec) {
  PortRun r;
  r.kind = json::get_string(rec, "kind", "model_use");
  const auto dot = r.kind.find('.');
  r.port = dot == std::string::npos ? r.kind : r.kind.substr(dot + 1);  // media.asr -> asr
  r.adapter = json::get_string(rec, "adapter");
  r.operation_id = json::get_string(rec, "operation_id");
  r.request_sha256 = json::get_string(rec, "request_sha256");
  r.status = json::get_string(rec, "status", "ok");
  r.model = json::get_string(rec, "requested_model");
  r.provider = json::get_string(rec, "provider");
  if (const Json* u = json::find(rec, "usage"); u && !u->is_null()) r.usage = *u;
  r.role = r.port;
  return r;
}

Result<std::string> record_port_run(Database& db, const PortRun& run) {
  const std::string ident = !run.operation_id.empty() ? run.operation_id : run.request_sha256;
  std::string run_id = run.action_run_id.empty() ? ActionRunScope::current() : run.action_run_id;

  Json meta{{"schema", kPortRunSchema}, {"port", run.port}, {"role", run.role}, {"kind", run.kind},
            {"adapter", run.adapter}, {"status", run.status}, {"model", run.model}, {"provider", run.provider},
            {"operation_id", run.operation_id.empty() ? Json(nullptr) : Json(run.operation_id)},
            {"request_sha256", run.request_sha256.empty() ? Json(nullptr) : Json(run.request_sha256)},
            {"provenance", Json{{"subject_kind", "model_use"}, {"subject_id", ident}}},
            {"recorded_at", timeutil::utc_now_iso()}};
  if (!run.error.empty()) meta["error"] = run.error;
  if (!run.usage.is_null()) meta["usage"] = run.usage;
  if (run.ensemble.is_object()) meta["ensemble"] = run.ensemble;
  if (!run.role_choice_node.empty()) meta["role_choice_node"] = run.role_choice_node;
  if (!run_id.empty()) meta["action_run_id"] = run_id;
  for (auto it = run.extra.begin(); it != run.extra.end(); ++it)
    if (!meta.contains(it.key())) meta[it.key()] = it.value();
  const std::string label = ident.empty() ? std::string() : "port_run:" + ident;
  std::string id;
  if (label.empty()) {
    NodeOptions o;
    o.tags = Json::array({"port_run", "port_artifact"});
    o.metadata = redact(meta);
    LOOM_TRY_ASSIGN(id, db.create_node("port_run:" + run.port + ":" + run.adapter, "port_run", o));
  } else {
    LOOM_TRY_ASSIGN(id, node(db, label, "port_run", "", meta));
  }

  std::vector<std::string> producers;
  if (!run.adapter.empty()) producers.push_back(run.adapter);
  if (run.ensemble.is_object())
    for (const auto& p : run.ensemble.value("produced_by", Json::array()))
      if (p.is_string() && std::find(producers.begin(), producers.end(), p.get<std::string>()) == producers.end()) producers.push_back(p.get<std::string>());
  for (const auto& name : producers) {
    LOOM_TRY_ASSIGN(const std::string a, named_adapter(db, run.port, name));
    LOOM_TRY(link(db, id, a, "produced_by", Json{{"operation_id", run.operation_id}}));
  }
  for (const auto& in : run.inputs) LOOM_TRY(link(db, id, in, "derived_from"));
  if (!run.role_choice_node.empty()) LOOM_TRY(link(db, run.role_choice_node, id, "used_by"));
  if (!run_id.empty()) {
    for (ActionRunScope* s = t_scope; s; s = s->outer_)
      if (s->run_id_ == run_id) {
        s->recorded_.push_back(id);
        break;
      }
    LOOM_TRY_ASSIGN(auto run_node, db.find_node("run:" + run_id, "action_run"));
    if (run_node) LOOM_TRY(link(db, id, run_node->id, "part_of", Json{{"run_id", run_id}}));
  }
  return id;
}

Result<int> reconcile_action_run(Database& db, const std::string& run_id, const std::vector<std::string>& node_ids) {
  if (run_id.empty()) return 0;
  LOOM_TRY_ASSIGN(auto run_node, db.find_node("run:" + run_id, "action_run"));
  if (!run_node) return 0;
  std::vector<Node> candidates;
  if (node_ids.empty()) {
    LOOM_TRY_ASSIGN(candidates, db.list_nodes("port_run", 1000000));
  } else {
    for (const auto& id : node_ids) {
      LOOM_TRY_ASSIGN(auto n, db.get_node(id));
      if (n) candidates.push_back(*n);
    }
  }
  int made = 0;
  for (const auto& n : candidates) {
    if (json::get_string(n.metadata, "action_run_id") != run_id) continue;
    LOOM_TRY_ASSIGN(auto links, db.get_links(n.id, "part_of"));
    bool have = false;
    for (const auto& l : links) have = have || (l.src == n.id && l.dst == run_node->id);
    if (have) continue;
    LOOM_TRY(link(db, n.id, run_node->id, "part_of", Json{{"run_id", run_id}}));
    ++made;
  }
  return made;
}

Result<int> reconcile_action_runs(Database& db) {
  int made = 0;
  LOOM_TRY_ASSIGN(auto nodes, db.list_nodes("port_run", 1000000));
  for (const auto& n : nodes) {
    const std::string run_id = json::get_string(n.metadata, "action_run_id");
    if (run_id.empty()) continue;
    LOOM_TRY_ASSIGN(auto run_node, db.find_node("run:" + run_id, "action_run"));
    if (!run_node) continue;
    LOOM_TRY_ASSIGN(auto links, db.get_links(n.id, "part_of"));
    bool have = false;
    for (const auto& l : links) have = have || (l.src == n.id && l.dst == run_node->id);
    if (have) continue;
    LOOM_TRY(link(db, n.id, run_node->id, "part_of", Json{{"run_id", run_id}}));
    ++made;
  }
  return made;
}

}  // namespace loom::port_graph
