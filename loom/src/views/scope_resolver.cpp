// One resolution chain for any setting. See include/loom/scope_resolver.h.
#include "loom/scope_resolver.h"

#include "loom/config.h"
#include "loom/db.h"
#include "loom/effective_view.h"
#include "loom/log.h"

#include <map>
#include <mutex>

namespace loom::scopes {
namespace {

constexpr const char* kLog = "scopes";

bool unset(const Json& v) { return v.is_null() || (v.is_string() && v.get<std::string>().empty()); }

// Perf (P19): Engine::load parses three runtime profiles and config.json; annotations::resolve_model opens a Resolver
// per message. The loaded engine is cached per data root and keyed by the stat of every file it reads (the three
// profile overlays and config.json), so any change to them reloads on the next open. A live Config is applied to
// each copy, never to the cached one.
struct EngineCache {
  std::mutex mu;
  std::map<std::string, std::pair<std::string, actions::Engine>> by_root;
};

EngineCache& engine_cache() {
  static EngineCache c;
  return c;
}

std::string fingerprint(const std::filesystem::path& root) {
  std::string out;
  const std::filesystem::path files[] = {root / "profiles" / "graph_entry.pack", root / "profiles" / "actions.pack",
                                         root / "profiles" / "action_rules.pack", root / "config.json"};
  for (const auto& f : files) {
    std::error_code ec;
    const auto st = std::filesystem::symlink_status(f, ec);
    if (ec || !std::filesystem::exists(st)) { out += "-;"; continue; }
    std::error_code e1, e2;
    const auto size = std::filesystem::is_regular_file(st) ? std::filesystem::file_size(f, e1) : 0;
    const auto mtime = std::filesystem::last_write_time(f, e2).time_since_epoch().count();
    out += std::to_string(size) + ":" + std::to_string(mtime) + ";";
  }
  return out;
}

Result<actions::Engine> cached_engine(const std::filesystem::path& root) {
  const std::string key = root.string();
  const std::string fp = fingerprint(root);
  auto& c = engine_cache();
  {
    std::lock_guard lk(c.mu);
    auto it = c.by_root.find(key);
    if (it != c.by_root.end() && it->second.first == fp) return it->second.second;
  }
  LOOM_TRY_ASSIGN(auto engine, actions::Engine::load(root));
  std::lock_guard lk(c.mu);
  c.by_root.insert_or_assign(key, std::make_pair(fp, engine));
  return engine;
}

}  // namespace

Json Resolution::to_json() const {
  Json o{{"schema", "loom.scope_resolution/1"}, {"defined", defined}, {"value", value}, {"units", units}, {"chain", chain}};
  if (defined) {
    o["scope"] = scope;
    if (!scope_id.empty()) o["scope_id"] = scope_id;
    o["source"] = source;
  }
  if (!ignored.empty()) o["ignored_units"] = ignored;
  return o;
}

Result<Resolver> Resolver::open(const std::filesystem::path& data_root, const Config* cfg, Database* db) {
  LOOM_TRY_ASSIGN(auto engine, cached_engine(data_root));
  if (cfg) engine.set_user_values(cfg->all());
  Resolver r;
  r.engine_ = std::make_shared<actions::Engine>(std::move(engine));
  r.cfg_ = cfg;
  r.db_ = db;
  r.root_ = data_root;
  return r;
}

Result<Resolution> Resolver::resolve(const Binding& b) const {
  if (b.key.empty()) return Error(Errc::InvalidArgument, "a scoped value needs a key");
  Json conv_meta = Json::object();
  if (!b.conv_id.empty() && db_) {
    auto conv = db_->get_conv(b.conv_id);
    if (!conv) return conv.error();
    if (*conv) conv_meta = (*conv)->metadata.is_object() ? (*conv)->metadata : Json::object();
  }
  Json units = b.units.is_object() ? b.units : conv_meta.value("units", Json::object());
  if (!units.is_object()) units = Json::object();

  // unit kinds are data (views.unit_kinds): kind -> chain level; a key that is already a level of the chain counts as that level
  LOOM_TRY_ASSIGN(auto kinds, views::effective(root_, "unit_kinds"));
  Resolution out;
  Json context = Json::object();
  for (auto it = units.begin(); it != units.end(); ++it) {
    if (!it.value().is_string() || it.value().get<std::string>().empty()) {
      out.ignored.push_back(Json{{"unit", it.key()}, {"why", "the unit id must be a non-empty string"}});
      continue;
    }
    std::string level;
    if (const auto* k = kinds.find(it.key()); k && !k->disabled) level = engine_->level_name(json::get_string(k->value, "level", it.key()));
    else level = engine_->level_name(it.key());
    if (level.empty()) {
      out.ignored.push_back(Json{{"unit", it.key()}, {"why", "neither a unit kind nor a level of the scope chain"}});
      log::write(log::Level::Debug, kLog, "unit '" + it.key() + "' of a conversation names no unit kind or scope level, ignored");
      continue;
    }
    context[level] = it.value();
  }
  out.units = context;
  if (!b.conv_id.empty()) context["conversation"] = b.conv_id;

  std::vector<actions::Baseline> baselines;
  if (!b.installation.is_null())
    baselines.emplace_back("installation", b.installation, b.installation_source.empty() ? "preset:" + b.key : b.installation_source);
  const std::string setting = b.person_setting.empty() ? b.key : b.person_setting;
  if (cfg_) {
    const Json mine = cfg_->get(setting);
    if (!unset(mine)) baselines.emplace_back("person", mine, "setting:" + setting);
  }
  if (b.unit_values.is_object()) {
    for (auto it = b.unit_values.begin(); it != b.unit_values.end(); ++it) {
      const std::string level = engine_->level_name(it.key());
      if (level.empty()) return Error(Errc::InvalidArgument, "unit_values: unknown scope level '" + it.key() + "'");
      baselines.emplace_back(level, it.value(), "unit:" + level, 0, context.value(level, ""));
    }
  }
  Json conv_value = b.conversation_value;
  if (conv_value.is_null() && conv_meta.is_object()) {
    const Json settings = conv_meta.value("settings", Json::object());
    if (settings.is_object() && settings.contains(b.key)) conv_value = settings[b.key];
  }
  if (!conv_value.is_null() && !b.conv_id.empty()) baselines.emplace_back("conversation", conv_value, "conversation:" + b.key, 0, b.conv_id);

  LOOM_TRY_ASSIGN(auto scope, engine_->parse_scope(context.empty() ? Json("conversation") : context));
  LOOM_TRY_ASSIGN(auto sv, engine_->resolve_value(b.key, scope, baselines));
  out.defined = sv.defined;
  out.value = sv.value;
  out.scope = sv.scope;
  out.scope_id = sv.scope_id;
  out.source = sv.source == "rule" ? "rule:" + sv.rule : sv.source;

  // the chain, general first: which level supplied the value, which were overridden
  const Json& chain = engine_->scope_chain();
  for (std::size_t i = 0; i < chain.size(); ++i) {
    const std::string id = json::get_string(chain[i], "id");
    Json level{{"level", id}, {"label", chain[i].value("label", Json(id))}, {"active", i <= scope.depth}};
    if (context.contains(id)) level["scope_id"] = context[id];
    Json cands = Json::array();
    std::string role = i <= scope.depth ? "none" : "inactive";
    for (const auto& row : sv.considered) {
      if (json::get_string(row, "scope") != id) continue;
      cands.push_back(row);
      const std::string r = json::get_string(row, "role");
      if (r == "decided") role = "supplied";
      else if (r == "conflict" && role != "supplied") role = "conflict";
      else if (r == "overridden" && role != "supplied" && role != "conflict") role = "overridden";
    }
    level["role"] = role;
    level["candidates"] = cands;
    out.chain.push_back(std::move(level));
  }
  out.engine = std::move(sv);
  return out;
}

Result<Json> Resolver::inheritance(const Binding& b) const {
  LOOM_TRY_ASSIGN(auto r, resolve(b));
  Json nodes = Json::array(), edges = Json::array(), overridden = Json::array();
  std::string prev;
  std::string supplier;
  for (const auto& l : r.chain) {
    const std::string id = json::get_string(l, "level");
    if (json::get_string(l, "role") == "supplied") supplier = id;
  }
  for (const auto& l : r.chain) {
    const std::string id = json::get_string(l, "level");
    const std::string role = json::get_string(l, "role");
    Json n{{"id", "scope:" + id}, {"kind", "scope_level"}, {"level", id}, {"label", l.value("label", Json(id))}, {"role", role}, {"active", l.value("active", false)}};
    if (l.contains("scope_id")) n["scope_id"] = l["scope_id"];
    for (const auto& c : l.value("candidates", Json::array()))
      if (c.contains("value") && (json::get_string(c, "role") == "decided" || json::get_string(c, "role") == "overridden")) { n["value"] = c["value"]; break; }
    nodes.push_back(std::move(n));
    if (!prev.empty()) edges.push_back(Json{{"src", "scope:" + id}, {"dst", "scope:" + prev}, {"type", "inherits_from"}});
    if (role == "overridden") {
      overridden.push_back(id);
      if (!supplier.empty()) edges.push_back(Json{{"src", "scope:" + supplier}, {"dst", "scope:" + id}, {"type", "overrides"}});
    }
    prev = id;
  }
  return Json{{"schema", "loom.perspective_inheritance/1"}, {"perspective", "dziedziczenie"}, {"key", b.key}, {"defined", r.defined},
              {"value", r.value}, {"supplied_by", supplier.empty() ? Json(nullptr) : Json(supplier)}, {"overridden", overridden},
              {"nodes", nodes}, {"edges", edges}, {"resolution", r.to_json()}};
}

Json value_or(const std::filesystem::path& data_root, const Config& cfg, Database* db, const Binding& binding, const Json& fallback) {
  auto resolver = Resolver::open(data_root, &cfg, db);
  if (resolver) {
    if (auto r = resolver->resolve(binding)) return r->defined ? r->value : fallback;
    else log::write(log::Level::Debug, kLog, "'" + binding.key + "' not resolved through the scope chain, the setting decides: " + r.error().message);
  } else {
    log::write(log::Level::Debug, kLog, "scope chain not readable, the setting decides: " + resolver.error().message);
  }
  const Json mine = cfg.get(binding.person_setting.empty() ? binding.key : binding.person_setting);
  return unset(mine) ? fallback : mine;
}

}  // namespace loom::scopes
