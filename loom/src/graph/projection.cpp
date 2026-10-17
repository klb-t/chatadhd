// The graph projection. See src/graph/projection.h.
#include "graph/projection.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <set>

#include "loom/action_element.h"
#include "loom/action_rules.h"
#include "loom/action_table.h"
#include "loom/annotation_layer.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/effective_view.h"
#include "loom/graph_entry.h"
#include "loom/log.h"
#include "loom/memory_engine.h"
#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"
#include "loom/perspectives.h"
#include "loom/port_profile.h"
#include "loom/providers.h"
#include "loom/rom.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/scope_resolver.h"
#include "loom/settings_fields.h"
#include "loom/util/utf8.h"

namespace loom::projection {
namespace {

constexpr const char* kLog = "projection";

std::string pick(const Json& v, const std::string& lang) {
  if (v.is_string()) return v.get<std::string>();
  if (!v.is_object()) return std::string();
  for (const std::string& k : {lang, std::string("en"), std::string("pl")})
    if (const Json* s = json::find(v, k); s && s->is_string()) return s->get<std::string>();
  return std::string();
}

bool starts_with(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }

std::string clip(const std::string& s, std::size_t n) {
  const std::string_view head = utf8::prefix(s, n);
  return head.size() < s.size() ? std::string(head) + "…" : s;
}

const Json* at_pointer(const Json& root, const std::string& pointer) {
  if (pointer.empty()) return &root;
  try {
    const Json::json_pointer p(pointer);
    return root.contains(p) ? &root.at(p) : nullptr;
  } catch (const std::exception& e) {
    log::write(log::Level::Debug, kLog, "pointer '" + pointer + "' is not a JSON pointer: " + e.what());
    return nullptr;
  }
}

std::vector<std::string> strings_in(const Json& v) {
  std::vector<std::string> out;
  if (v.is_string() && !v.get<std::string>().empty()) out.push_back(v.get<std::string>());
  if (v.is_array())
    for (const auto& x : v)
      if (x.is_string() && !x.get<std::string>().empty()) out.push_back(x.get<std::string>());
  return out;
}

// What one reader returns for one source (and one parent, for the sources read under a parent).
struct Read {
  std::vector<Json> nodes;
  std::vector<Json> edges;      // {src, dst, kind}
  std::int64_t total = -1;      // -1 = nodes.size()
  std::string unknown;          // why the source could not be read ("" = read)
  Json hidden = Json::array();  // [{source, id, by?}]
  Json page = nullptr;          // {limit, offset|before, next_offset|next_before, total} of a paged source
};

Json evidence_of(const Json& cls, const std::string& layer, const std::string& source_ref, const std::string& method, const Json& time = nullptr) {
  return Json{{"class", json::get_string(cls, "class", "declared")},
              {"confidence", cls.is_object() && cls.contains("confidence") ? cls["confidence"] : Json(nullptr)},
              {"provenance", Json{{"layer", layer}, {"source_ref", source_ref}, {"method", method}, {"time", time}}}};
}

// A property whose provenance says a model inferred it is inferred, whatever the source's default says.
Json evidence_by_method(const Json& source, const std::string& method, const Json& fallback) {
  const Json by = source.value("evidence_by_method", Json::object());
  if (by.is_object()) {
    if (const Json* e = json::find(by, method); e && e->is_object()) return *e;
    if (method == "model_inferred") return Json{{"class", "inferred"}, {"confidence", nullptr}};
    if (const Json* any = json::find(by, "*"); any && any->is_object()) return *any;
  }
  if (method == "model_inferred") return Json{{"class", "inferred"}, {"confidence", nullptr}};
  return fallback;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────

struct Catalogue::Impl {
  Env env;
  Json sources = Json::array();
  Json hidden_sources = Json::array();
  Json identity = Json::object();
  Json root_row = Json::object();
  Json words = Json::object();
  Json profile_values = Json::object();  // graph_sources values

  std::map<std::string, Read> eager;  // source id -> whole read

  std::size_t opens = 0;
  std::optional<scopes::Resolver> resolver;
  bool resolver_tried = false;
  std::string resolver_error;
  std::optional<PortProfile> ports;
  bool ports_tried = false;
  std::optional<Json> fields_doc;
  std::string fields_error;
  std::optional<Json> conv_meta;
  std::optional<Json> roles_cache;  // role id -> resolution row
  std::optional<views::EdgeKinds> kinds;
  bool kinds_tried = false;
  std::optional<Json> graph_entry_values;
  std::optional<Json> profile_snapshot;
  std::string profile_error;
  bool profile_tried = false;

  using Reader = std::function<Result<Read>(Impl&, const Json& source, const std::string& parent, const Json& paging)>;
  static const std::map<std::string, Reader>& readers();
  static bool needs_parent(const std::string& reader) { return reader == "db.messages" || reader == "actions.steps"; }
  static bool paged(const Json& source) { return source.contains("page_setting") || needs_parent(json::get_string(source, "reader")); }

  std::string lang() const { return env.lang.empty() ? std::string("en") : env.lang; }
  std::string say(const Json& v) const { return pick(v, lang()); }

  const Json* source(const std::string& id) const {
    for (const auto& s : sources)
      if (json::get_string(s, "id") == id) return &s;
    return nullptr;
  }

  // ── shared state of one render ─────────────────────────────────────

  const scopes::Resolver* get_resolver() {
    if (!resolver_tried) {
      resolver_tried = true;
      ++opens;
      if (auto r = scopes::Resolver::open(env.root, env.cfg, env.db)) resolver = std::move(*r);
      else {
        resolver_error = r.error().message;
        log::write(log::Level::Debug, kLog, "scope chain not readable, values are the settings: " + resolver_error);
      }
    }
    return resolver ? &*resolver : nullptr;
  }

  const PortProfile* get_ports() {
    if (!ports_tried) {
      ports_tried = true;
      if (auto p = PortProfile::load(env.root)) ports = std::move(*p);
      else log::write(log::Level::Debug, kLog, "ports profile not readable: " + p.error().message);
    }
    return ports ? &*ports : nullptr;
  }

  Result<const Json*> fields() {
    if (!fields_doc && fields_error.empty()) {
      auto doc = settings_fields(env.root);
      if (doc) fields_doc = std::move(*doc);
      else fields_error = doc.error().message;
    }
    if (!fields_doc) return Error(Errc::Unavailable, "settings_fields_unavailable: " + fields_error);
    return &*fields_doc;
  }

  const Json& graph_entry() {
    if (!graph_entry_values) {
      auto p = RuntimeProfile::load("graph_entry", env.root);
      if (p) graph_entry_values = p->values();
      else {
        log::write(log::Level::Debug, kLog, "graph_entry profile not readable: " + p.error().message);
        graph_entry_values = Json::object();
      }
    }
    return *graph_entry_values;
  }

  // The conversation in view: its metadata (units, settings, ports), read once.
  const Json& conversation_meta() {
    if (!conv_meta) {
      conv_meta = Json::object();
      if (!env.conv_id.empty() && env.db) {
        auto c = env.db->get_conv(env.conv_id);
        if (!c) log::write(log::Level::Debug, kLog, "conversation " + env.conv_id + " not readable: " + c.error().message);
        else if (*c && (*c)->metadata.is_object()) conv_meta = (*c)->metadata;
      }
    }
    return *conv_meta;
  }

  views::EdgeKinds* edge_kinds() {
    if (!kinds_tried) {
      kinds_tried = true;
      if (auto k = views::EdgeKinds::open(env.root)) kinds = std::move(*k);
      else log::write(log::Level::Debug, kLog, "edge kinds not readable: " + k.error().message);
    }
    return kinds ? &*kinds : nullptr;
  }

  // The onboarding snapshot shown as You: the one given, else the stored one (named, or the only one).
  Result<const Json*> profile() {
    if (!profile_tried) {
      profile_tried = true;
      if (!env.profile.is_null()) {
        profile_snapshot = env.profile;
      } else if (!env.db) {
        profile_error = "database_unavailable";
      } else {
        std::string user = env.user_id;
        bool any = false;
        {
          auto lock = env.db->lock();
          auto& conn = env.db->conn();
          if (conn.has_table("loom_onboarding_profiles")) {
            auto st = conn.prepare("SELECT user_id FROM loom_onboarding_profiles ORDER BY user_id LIMIT 2");
            if (!st) profile_error = "onboarding_unavailable";
            else {
              std::vector<std::string> ids;
              while (true) {
                auto more = st->step();
                if (!more) { profile_error = "onboarding_unavailable"; break; }
                if (!*more) break;
                ids.push_back(st->get_text(0));
              }
              any = !ids.empty();
              if (user.empty() && ids.size() > 1) profile_error = "several_profiles_name_one";
              if (user.empty() && ids.size() == 1) user = ids[0];
            }
          }
        }
        if (profile_error.empty() && any && !user.empty()) {
          onboarding::OnboardingStore store(*env.db);
          auto snap = store.read(user);
          if (snap) profile_snapshot = std::move(*snap);
          else if (snap.error().code != Errc::NotFound) profile_error = "onboarding_unavailable";
        }
      }
      if (!profile_error.empty()) log::write(log::Level::Debug, kLog, "onboarding profile not shown: " + profile_error);
    }
    if (!profile_error.empty()) return Error(Errc::Unavailable, profile_error);
    return profile_snapshot ? &*profile_snapshot : nullptr;
  }

  // ── nodes ──────────────────────────────────────────────────────────

  Json make(const Json& src, const std::string& address, const std::string& label, const std::string& parent, const std::string& layer,
            const std::string& source_ref, const std::string& method, const Json& time = nullptr) {
    Json layers = Json::array();
    layers.push_back(layer);
    return Json{{"address", address},
                {"label", label},
                {"kind", json::get_string(src, "class")},
                {"source", json::get_string(src, "id")},
                {"parent", parent},
                {"evidence", evidence_of(src.value("epistemic_default", Json::object()), layer, source_ref, method, time)},
                {"provenance", Json{{"layer", layer}, {"layers", layers}}},
                {"has_children", false},
                {"attrs", Json::object()}};
  }

  std::string parent_of(const Json& src) const {
    const Json p = src.value("parent", Json::object());
    if (const std::string area = json::get_string(p, "area"); !area.empty()) return "area:" + area;
    return std::string();
  }

  std::string human_value(const Json& v, const Json& field) const {
    for (const auto& e : field.value("values", Json::array()))
      if (e.is_object() && e.contains("value") && e["value"] == v && e.contains("meaning")) {
        const std::string m = say(e["meaning"]);
        if (!m.empty()) return m;
      }
    const Json w = words;
    if (v.is_null()) return say(w.value("null", Json("not set")));
    if (v.is_boolean()) return say(w.value(v.get<bool>() ? "true" : "false", Json(v.get<bool>() ? "on" : "off")));
    if (v.is_string()) return v.get<std::string>().empty() ? say(w.value("empty", Json("empty"))) : v.get<std::string>();
    if (v.is_number_integer() || v.is_number_unsigned()) return json::dump(v);
    if (v.is_number_float()) {
      std::string s = json::format_float_py(v.get<double>());
      const std::string sep = say(w.value("decimal_separator", Json(".")));
      if (!sep.empty() && sep != ".")
        if (auto dot = s.find('.'); dot != std::string::npos) s.replace(dot, 1, sep);
      return s;
    }
    if (v.empty()) return say(w.value("empty", Json("empty")));
    auto text = render_profile_template(say(w.value("items", Json("{{n}} items"))), Json{{"n", v.size()}});
    return text ? *text : std::to_string(v.size());
  }

  // The chip of the level that supplied a value: {id, label} with the chain's own label.
  Json chip_of(const std::string& scope, const Json& chain) const {
    for (const auto& l : chain)
      if (json::get_string(l, "level") == scope) return Json{{"id", scope}, {"label", say(l.value("label", Json(scope)))}};
    return Json{{"id", scope.empty() ? std::string("none") : scope}, {"label", scope}};
  }

  // One field (config / secret) or profile value through the one scope chain: {value, value_human, provenance, inheritance, evidence}.
  Json resolve_field(const Json& field, const Json& src) {
    const std::string key = json::get_string(field, "key");
    Json out = Json::object();
    if (json::get_string(field, "scope") == "secret") {
      const bool set = env.secrets && env.secrets->has(key);
      out["value"] = nullptr;
      out["secret"] = true;
      out["is_set"] = env.secrets ? Json(set) : Json(nullptr);
      out["value_human"] = say(words.value(set ? "secret_set" : "secret_unset", Json(set ? "set" : "not set")));
      out["provenance"] = Json{{"scope", set ? "person" : "none"}, {"layer", "secrets.json"}, {"chip", Json{{"id", "secret"}, {"label", say(words.value("secret_chip", Json("secret")))}}}};
      out["inheritance"] = Json::array();
      out["evidence"] = evidence_of(set ? Json{{"class", "observed"}, {"confidence", 1.0}} : src.value("epistemic_default", Json::object()),
                                    "secrets.json", key, set ? "setting" : "preset");
      return out;
    }
    const scopes::Resolver* r = get_resolver();
    scopes::Binding b;
    b.key = key;
    b.conv_id = env.conv_id;
    if (field.contains("default_value")) {
      b.installation = field["default_value"];
      b.installation_source = "preset:settings";
    }
    std::optional<scopes::Resolution> res;
    if (r) {
      auto got = r->resolve(b);
      if (got) res = std::move(*got);
      else log::write(log::Level::Debug, kLog, "'" + key + "' not resolved through the scope chain: " + got.error().message);
    }
    if (!res) {
      // No chain: what the settings say, else the preset - and the provenance says the chain could not be read.
      const Json mine = env.cfg ? env.cfg->get(key) : Json(nullptr);
      const bool set = !mine.is_null();
      out["value"] = set ? mine : field.value("default_value", Json(nullptr));
      out["value_human"] = human_value(out["value"], field);
      out["provenance"] = Json{{"scope", set ? "person" : "installation"}, {"layer", set ? "config.json" : "rom"},
                               {"chip", Json{{"id", set ? "person" : "installation"}, {"label", set ? "person" : "installation"}}},
                               {"chain_unavailable", resolver_error.empty() ? std::string("scope_chain_unavailable") : resolver_error}};
      out["inheritance"] = Json::array();
      out["evidence"] = evidence_of(set ? Json{{"class", "observed"}, {"confidence", 1.0}} : src.value("epistemic_default", Json::object()),
                                    set ? "config.json" : "rom", key, set ? "setting" : "preset");
      return out;
    }
    out["value"] = res->defined ? res->value : Json(nullptr);
    out["value_human"] = human_value(out["value"], field);
    std::string layer = "rom";
    if (res->scope == "person") layer = "config.json";
    else if (res->scope == "conversation") layer = "conversation.metadata";
    else if (starts_with(res->source, "rule:")) layer = "rules:" + res->source.substr(5);
    else if (res->scope != "installation" && !res->scope.empty()) layer = "unit:" + res->scope;
    if (!res->defined) layer = "none";
    Json prov{{"scope", res->defined ? res->scope : std::string("none")}, {"layer", layer}, {"source", res->source}, {"chip", chip_of(res->defined ? res->scope : std::string(), res->chain)}};
    if (!res->scope_id.empty()) prov["scope_id"] = res->scope_id;
    // config.json holds the presets too (the auto-upgrade writes them): a person value equal to the preset may be the preset itself, so it is
    // said, and its evidence stays the preset's (never "you said").
    const bool preset_copy = res->defined && res->scope == "person" && field.contains("default_value") && field["default_value"] == res->value;
    if (preset_copy) prov["same_as_preset"] = true;
    out["provenance"] = prov;
    out["inheritance"] = inheritance_of(*res);
    const Json by_scope = src.value("evidence_by_scope", Json::object());
    Json cls = src.value("epistemic_default", Json::object());
    if (const Json* e = json::find(by_scope, res->scope); e && e->is_object()) cls = *e;
    else if (const Json* any = json::find(by_scope, "*"); any && any->is_object() && res->scope != "installation" && res->defined) cls = *any;
    if (preset_copy) cls = src.value("epistemic_default", Json::object());
    out["evidence"] = evidence_of(cls, layer, key, res->scope == "installation" ? "preset" : preset_copy ? "preset_in_config" : "setting");
    return out;
  }

  // The whole chain, general first: [{level, label, role, active, scope_id?, value?, unit_level?, no_unit?}]. A unit level of a chain that
  // has no unit of that kind says so (no_unit: the conversation is "bez projektu"), never left out.
  Json inheritance_of(const scopes::Resolution& res) {
    std::set<std::string> unit_levels;
    if (auto uk = views::effective(env.root, "unit_kinds"))
      for (const auto& e : uk->entries) unit_levels.insert(json::get_string(e.value, "level", e.id));
    Json out = Json::array();
    for (const auto& l : res.chain) {
      const std::string id = json::get_string(l, "level");
      Json row{{"level", id}, {"label", say(l.value("label", Json(id)))}, {"role", json::get_string(l, "role")}, {"active", l.value("active", false)}};
      if (l.contains("scope_id")) row["scope_id"] = l["scope_id"];
      for (const auto& c : l.value("candidates", Json::array()))
        if (c.contains("value") && (json::get_string(c, "role") == "decided" || json::get_string(c, "role") == "overridden")) {
          row["value"] = c["value"];
          break;
        }
      if (unit_levels.count(id)) {
        row["unit_level"] = true;
        if (!l.contains("scope_id")) row["no_unit"] = true;
      }
      out.push_back(std::move(row));
    }
    return out;
  }

  // ── reading sources ────────────────────────────────────────────────

  Result<const Read*> read_whole(const Json& src) {
    const std::string id = json::get_string(src, "id");
    if (auto it = eager.find(id); it != eager.end()) return &it->second;
    const std::string reader = json::get_string(src, "reader");
    Read r;
    if (auto f = readers().find(reader); f == readers().end()) {
      r.unknown = "reader_missing: " + reader;
    } else if (needs_parent(reader)) {
      r.unknown = "read_under_parent";
    } else {
      auto got = f->second(*this, src, std::string(), Json::object());
      if (got) r = std::move(*got);
      else r.unknown = got.error().message;
    }
    if (r.unknown.empty())
      if (const Json st = src.value("status", Json::object()); st.is_object() && json::get_string(st, "state") == "planned" && r.nodes.empty())
        r.unknown = "planned";
    return &eager.emplace(id, std::move(r)).first->second;
  }

  Result<Read> read_under(const Json& src, const std::string& parent, const Json& paging) {
    const std::string reader = json::get_string(src, "reader");
    auto f = readers().find(reader);
    if (f == readers().end()) {
      Read r;
      r.unknown = "reader_missing: " + reader;
      return r;
    }
    return f->second(*this, src, parent, paging);
  }

  std::int64_t page_limit(const Json& src, const Json& paging) const {
    if (paging.contains("page_limit") && paging["page_limit"].is_number_integer()) return paging["page_limit"].get<std::int64_t>();
    const std::string key = json::get_string(src, "page_setting");
    if (!key.empty() && env.cfg) {
      const Json v = env.cfg->get(key);
      if (v.is_number_integer()) return v.get<std::int64_t>();
    }
    return 0;  // 0 = no limit (the setting's own meaning)
  }
};

// ── readers (mechanism ids of runtime/graph_sources.pack) ────────────────────────────

namespace {

using Impl = Catalogue::Impl;

std::string layer_vs_builtin(const Json& effective, const Json& builtin, const std::string& overlay) {
  return effective == builtin ? std::string("rom") : overlay;
}

Result<Read> read_areas(Impl& c, const Json& src, const std::string&, const Json&) {
  graph_entry::Env ge;
  ge.db = c.env.db;
  ge.cfg = c.env.cfg;
  ge.data_dir = c.env.root;
  LOOM_TRY_ASSIGN(auto view, graph_entry::compute(ge, Json{{"lang", c.lang()}, {"include_hidden", true}}));
  auto shipped = RuntimeProfile::builtin("graph_entry");
  const Json shipped_areas = shipped ? shipped->values().value("areas", Json::object()) : Json::object();
  const Json effective_areas = c.graph_entry().value("areas", Json::object());
  Read r;
  for (const auto& a : view.value("areas", Json::array())) {
    const std::string id = json::get_string(a, "id");
    if (json::get_bool(a, "hidden", false)) {
      r.hidden.push_back(Json{{"source", "area"}, {"id", "area:" + id}, {"by", "overlay:graph_entry"}});
      continue;
    }
    const Json own = effective_areas.value(id, Json());
    const std::string layer = json::get_string(a, "origin") == "cell" ? std::string("rom")
                              : layer_vs_builtin(own, shipped_areas.value(id, Json()), "overlay:graph_entry");
    Json n = c.make(src, "area:" + id, json::get_string(a, "label"), std::string(), layer, "graph/entry.pack#/areas/" + id, "preset");
    n["order"] = a.value("order", 1000);
    n["attrs"] = Json{{"counts", a.value("counts", Json::object())}, {"axes", a.value("axes", Json::object())}, {"origin", a.value("origin", "area")},
                      {"expand", a.value("expand", Json(nullptr))}};
    r.nodes.push_back(std::move(n));
  }
  if (!view.value("issues", Json::array()).empty()) r.page = Json{{"issues", view["issues"]}};
  return r;
}

Result<Read> read_rom(Impl& c, const Json& src, const std::string&, const Json&) {
  LOOM_TRY_ASSIGN(auto rom, rom::Rom::open());
  const auto mounts = rom::MountRegistry::standard(c.env.root);
  Read r;
  for (const auto& id : rom.ids()) {
    LOOM_TRY_ASSIGN(auto layered, mounts.read(std::string(rom::kAddressPrefix) + id));
    const Json item = layered.value("item", Json::object());
    const bool overlaid = json::get_string(layered, "source") == "overlay";
    if (json::get_bool(item, "hidden", false)) {
      r.hidden.push_back(Json{{"source", "rom"}, {"id", "rom:" + id}, {"by", "overlay:graph_entry"}});
      continue;
    }
    Json n = c.make(src, "rom:" + id, c.say(item.value("label", Json(id))), c.parent_of(src), overlaid ? "overlay:graph_entry" : "rom",
                    "graph/rom.pack#/items/" + id, "preset");
    n["order"] = item.value("number", 0);
    n["value"] = item.value("status", "unknown");
    n["status"] = item.value("status", "unknown");
    n["why"] = c.say(item.value("why", Json()));
    n["attrs"] = Json{{"number", item.value("number", 0)}, {"files", item.value("files", Json::array()).size()}, {"gaps", item.value("gaps", Json::array())}};
    if (overlaid) {
      n["provenance"]["layers"] = Json::array({"rom", "overlay:graph_entry"});
      n["evidence_overrides"] = Json{{"item", evidence_of(Json{{"class", "observed"}, {"confidence", 1.0}}, "overlay:graph_entry",
                                                          "profiles/graph_entry.pack#/overrides/rom/items/" + id, "override")}};
    }
    r.nodes.push_back(std::move(n));
  }
  return r;
}

// Fields and profile values in the settings document, with their group.
struct FieldRef {
  const Json* field;
  std::string key;
  bool profile_value;
};

std::vector<FieldRef> all_fields(const Json& doc) {
  std::vector<FieldRef> out;
  if (const Json* fields = json::find(doc, "fields"); fields && fields->is_array())
    for (const auto& f : *fields) out.push_back({&f, json::get_string(f, "key"), false});
  if (const Json* pv = json::find(doc, "profile_values"); pv && pv->is_array())
    for (const auto& p : *pv) {
      std::string pointer = json::get_string(p, "pointer");
      if (starts_with(pointer, "/")) pointer = pointer.substr(1);
      out.push_back({&p, json::get_string(p, "profile") + "/" + pointer, true});
    }
  return out;
}

std::string group_of(const Json& field) { return json::get_string(field.value("human", Json::object()), "group"); }

Result<Read> read_groups(Impl& c, const Json& src, const std::string&, const Json&) {
  LOOM_TRY_ASSIGN(const Json* doc, c.fields());
  std::map<std::string, std::int64_t> members;
  for (const auto& f : all_fields(*doc)) members[group_of(*f.field)] += 1;
  Read r;
  const Json groups = doc->value("groups", Json::object());
  for (const auto& g : groups.items()) {
    const Json& v = g.value();
    if (json::get_bool(v, "hidden", false)) {
      r.hidden.push_back(Json{{"source", "group"}, {"id", "group:" + g.key()}, {"by", v.value("hidden_by", Json("overlay"))}});
      continue;
    }
    const std::string origin = json::get_string(v, "origin", "rom");
    Json n = c.make(src, "group:" + g.key(), c.say(v.value("label", Json(g.key()))), c.parent_of(src), origin, "runtime/settings_fields.pack#/groups/" + g.key(), "preset");
    n["order"] = v.value("order", 1000);
    n["why"] = c.say(v.value("why", Json()));
    n["count"] = members[g.key()];
    n["has_children"] = members[g.key()] > 0;
    n["provenance"]["layers"] = v.value("provenance", Json::array());
    n["attrs"] = Json{{"subgroups", v.value("subgroups", Json::array())}};
    if (members[g.key()] == 0 && v.contains("empty_reason")) n["empty_reason"] = c.say(v["empty_reason"]);
    r.nodes.push_back(std::move(n));
  }
  std::sort(r.nodes.begin(), r.nodes.end(), [](const Json& a, const Json& b) { return json::get_int(a, "order", 1000) < json::get_int(b, "order", 1000); });
  return r;
}

Json profile_value_row(Impl& c, const Json& p, const Json& src) {
  const std::string profile = json::get_string(p, "profile");
  const std::string pointer = json::get_string(p, "pointer");
  Json out = Json::object();
  if (p.contains("problem")) {
    out["value"] = nullptr;
    out["value_human"] = c.say(c.words.value("problem", Json("cannot be read")));
    out["problem"] = p["problem"];
    out["provenance"] = Json{{"scope", "none"}, {"layer", "none"}, {"chip", Json{{"id", "none"}, {"label", ""}}}};
    out["evidence"] = evidence_of(src.value("epistemic_default", Json::object()), "none", profile + pointer, "profile_value");
    out["inheritance"] = Json::array();
    return out;
  }
  out["value"] = p.value("value", Json(nullptr));
  out["value_human"] = c.human_value(out["value"], p);
  auto shipped = RuntimeProfile::builtin(profile);
  const Json* builtin_value = shipped ? at_pointer(shipped->values(), pointer) : nullptr;
  const bool yours = !builtin_value || *builtin_value != out["value"];
  const std::string layer = yours ? "overlay:profiles/" + profile + ".pack" : std::string("rom");
  const std::string scope = yours ? "person" : "installation";
  out["provenance"] = Json{{"scope", scope}, {"layer", layer}, {"source", "profile:" + profile + pointer},
                           {"chip", Json{{"id", scope}, {"label", c.say(c.words.value("chip_" + scope, Json(scope)))}}}};
  out["evidence"] = evidence_of(yours ? Json{{"class", "observed"}, {"confidence", 1.0}} : src.value("epistemic_default", Json::object()), layer,
                                profile + pointer, yours ? "override" : "preset");
  out["inheritance"] = Json::array();
  return out;
}

Json field_human(Impl& c, const Json& field) {
  const Json h = field.value("human", Json::object());
  Json out{{"label", c.say(h.value("label", Json()))}, {"why", c.say(h.value("why", Json()))}, {"effect", c.say(h.value("effect", Json()))},
           {"levels", h.value("levels", Json::array())}, {"group", h.value("group", "")}};
  for (const char* k : {"subgroup", "role", "role_status", "placed_from", "technical_only"})
    if (h.contains(k)) out[k] = h[k];
  return out;
}

Result<Read> read_settings(Impl& c, const Json& src, const std::string&, const Json&) {
  LOOM_TRY_ASSIGN(const Json* doc, c.fields());
  Read r;
  std::set<std::string> hidden_groups;
  const Json all_groups = doc->value("groups", Json::object());
  for (const auto& g : all_groups.items())
    if (json::get_bool(g.value(), "hidden", false)) hidden_groups.insert(g.key());
  for (const auto& f : all_fields(*doc)) {
    const Json& field = *f.field;
    const std::string address = "setting:" + f.key;
    const std::string group = group_of(field);
    if (hidden_groups.count(group)) {
      r.hidden.push_back(Json{{"source", "setting"}, {"id", address}, {"by", "group:" + group}});
      continue;
    }
    const Json h = field_human(c, field);
    const Json resolved = f.profile_value ? profile_value_row(c, field, src) : c.resolve_field(field, src);
    Json n = c.make(src, address, h.value("label", "").empty() ? f.key : h.value("label", ""), "group:" + group,
                    json::get_string(resolved.value("provenance", Json::object()), "layer"), f.key, "setting");
    n["value"] = resolved.value("value", Json(nullptr));
    n["value_human"] = resolved.value("value_human", "");
    n["why"] = h.value("why", "");
    n["evidence"] = resolved.value("evidence", n["evidence"]);
    n["provenance"] = Json{{"layer", json::get_string(resolved.value("provenance", Json::object()), "layer")}, {"layers", Json::array()},
                           {"scope", resolved.value("provenance", Json::object()).value("scope", "")},
                           {"chip", resolved.value("provenance", Json::object()).value("chip", Json::object())}};
    for (const auto& l : resolved.value("inheritance", Json::array()))
      if (json::get_string(l, "role") == "supplied" || json::get_string(l, "role") == "overridden") n["provenance"]["layers"].push_back(json::get_string(l, "level"));
    if (field.contains("status")) n["status"] = field["status"].value("state", "");
    n["attrs"] = Json{{"key", f.key}, {"profile_value", f.profile_value}, {"type", field.value("type", "")}, {"edit", field.value("edit", "")},
                      {"human", h}, {"resolved", resolved}};
    if (f.profile_value) n["attrs"]["profile"] = Json{{"profile", field.value("profile", "")}, {"pointer", field.value("pointer", "")}};
    if (field.value("default", "") == "present" || field.contains("default_value"))
      for (const auto& e : src.value("edges", Json::array()))
        if (json::get_string(e, "kind") == "has_default") r.edges.push_back(Json{{"src", address}, {"dst", json::get_string(e, "to")}, {"kind", "has_default"}});
    r.nodes.push_back(std::move(n));
  }
  return r;
}

Result<Read> read_scopes(Impl& c, const Json& src, const std::string&, const Json&) {
  const Json* chain = at_pointer(c.graph_entry(), "/scopes/chain");
  if (!chain || !chain->is_array()) return Error(Errc::Unavailable, "scope_chain_absent");
  std::set<std::string> unit_levels;
  if (auto uk = views::effective(c.env.root, "unit_kinds"))
    for (const auto& e : uk->entries) unit_levels.insert(json::get_string(e.value, "level", e.id));
  else log::write(log::Level::Debug, kLog, "unit kinds not readable: " + uk.error().message);
  Read r;
  std::string prev;
  int order = 0;
  for (const auto& l : *chain) {
    const std::string id = json::get_string(l, "id");
    Json n = c.make(src, "scope:" + id, c.say(l.value("label", Json(id))), c.parent_of(src), "rom", "graph/scopes.pack#/chain/" + id, "preset");
    n["order"] = order++;
    if (unit_levels.count(id)) n["attrs"]["unit_level"] = true;
    if (!prev.empty()) r.edges.push_back(Json{{"src", "scope:" + id}, {"dst", "scope:" + prev}, {"kind", "inherits_from"}});
    prev = id;
    r.nodes.push_back(std::move(n));
  }
  return r;
}

// The model roles, each resolved once per render (the policy chain through the render's Resolver and ports profile).
const Json& role_rows(Impl& c);

Result<Read> read_roles(Impl& c, const Json& src, const std::string&, const Json&) {
  const Json& rows = role_rows(c);
  if (rows.contains("unknown")) return Error(Errc::Unavailable, json::get_string(rows, "unknown"));
  Read r;
  int order = 0;
  for (const auto& row : rows.value("roles", Json::array())) {
    const std::string id = json::get_string(row, "id");
    Json n = c.make(src, "role:" + id, json::get_string(row, "label", id), c.parent_of(src), json::get_string(row, "layer", "rom"), "runtime/ports.pack#/roles/" + id,
                    "preset");
    n["order"] = order++;
    n["value"] = row.value("model", Json(nullptr));
    n["value_human"] = row.value("model_human", "");
    n["why"] = row.value("why", "");
    n["status"] = json::get_string(row.value("status", Json::object()), "state", "implemented");
    n["attrs"] = Json{{"policy", row.value("policy", Json(nullptr))}, {"via", row.value("via", "")}};
    if (const std::string m = json::get_string(row, "model"); !m.empty()) {
      r.edges.push_back(Json{{"src", "role:" + id}, {"dst", "model:" + m}, {"kind", "bound_to"}});
      n["has_children"] = false;
    }
    r.nodes.push_back(std::move(n));
  }
  return r;
}

Result<Read> read_models(Impl& c, const Json& src, const std::string&, const Json&) {
  const Json& rows = role_rows(c);
  if (rows.contains("unknown")) return Error(Errc::Unavailable, json::get_string(rows, "unknown"));
  std::map<std::string, Json> models;
  std::vector<std::string> order;
  for (const auto& row : rows.value("roles", Json::array())) {
    const std::string m = json::get_string(row, "model");
    if (m.empty()) continue;
    if (!models.count(m)) {
      order.push_back(m);
      models[m] = Json{{"tier", row.value("tier", Json::object())}, {"cost", row.value("cost", Json::object())}, {"roles", Json::array()}};
    }
    models[m]["roles"].push_back(json::get_string(row, "id"));
  }
  Read r;
  for (const auto& m : order) {
    const std::string label = c.env.models ? c.env.models->name(m) : m;
    Json n = c.make(src, "model:" + m, label.empty() ? m : label, c.parent_of(src), "config.json", m, "role_resolution");
    n["value"] = json::get_string(models[m]["tier"], "id");
    n["attrs"] = models[m];
    r.nodes.push_back(std::move(n));
  }
  return r;
}

// Entries of a runtime profile at a pointer (object keyed by id, or array of {id}).
Result<Read> read_profile_items(Impl& c, const Json& src, const std::string&, const Json&) {
  const Json params = src.value("params", Json::object());
  const std::string domain = json::get_string(params, "domain");
  const std::string pointer = json::get_string(params, "pointer");
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load(domain, c.env.root));
  const Json* items = at_pointer(profile.values(), pointer);
  if (!items) return Error(Errc::Unavailable, "pointer_absent: " + domain + pointer);
  auto shipped = RuntimeProfile::builtin(domain);
  const Json* shipped_items = shipped ? at_pointer(shipped->values(), pointer) : nullptr;
  const std::string prefix = json::get_string(src, "prefix");
  Read r;
  auto add = [&](const std::string& id, const Json& v) {
    if (id.empty()) return;
    if (v.is_object() && json::get_bool(v, "hidden", false)) {
      r.hidden.push_back(Json{{"source", json::get_string(src, "id")}, {"id", prefix + id}, {"by", "overlay:" + domain}});
      return;
    }
    const Json* own = shipped_items && shipped_items->is_object() ? json::find(*shipped_items, id) : nullptr;
    const std::string layer = own && *own == v ? std::string("rom") : (shipped_items ? "overlay:" + domain : std::string("rom"));
    std::string label = v.is_object() ? c.say(v.value("label", Json())) : std::string();
    if (label.empty() && v.is_object() && v.contains("access")) label = c.say(v["access"].value("label", Json()));
    Json n = c.make(src, prefix + id, label.empty() ? id : label, c.parent_of(src), layer, "runtime/" + domain + ".pack#" + pointer + "/" + id, "preset");
    if (v.is_object())
      for (const char* k : {"class", "kind", "keys", "ports", "availability", "device", "status"})
        if (v.contains(k)) n["attrs"][k] = v[k];
    r.nodes.push_back(std::move(n));
  };
  if (items->is_object())
    for (const auto& it : items->items()) add(it.key(), it.value());
  else if (items->is_array())
    for (const auto& it : *items) add(it.is_object() ? json::get_string(it, "id") : (it.is_string() ? it.get<std::string>() : std::string()), it);
  return r;
}

Result<Read> read_config_items(Impl& c, const Json& src, const std::string&, const Json&) {
  if (!c.env.cfg) return Error(Errc::Unavailable, "config_unavailable");
  const std::string key = json::get_string(src.value("params", Json::object()), "key");
  const Json items = c.env.cfg->get(key, Json::object());
  if (!items.is_object()) return Error(Errc::Unavailable, "config_" + key + "_not_an_object");
  Read r;
  const std::string prefix = json::get_string(src, "prefix");
  for (const auto& it : items.items()) {
    const Json& v = it.value();
    std::string label = v.is_object() ? c.say(v.value("label", Json())) : std::string();
    Json n = c.make(src, prefix + it.key(), label.empty() ? it.key() : label, c.parent_of(src), "config.json", "config.json#/" + key + "/" + it.key(), "setting");
    if (v.is_object())
      for (const char* k : {"kind", "transport", "trust", "enabled"})
        if (v.contains(k)) n["attrs"][k] = v[k];
    r.nodes.push_back(std::move(n));
  }
  return r;
}

Result<Read> read_views(Impl& c, const Json& src, const std::string&, const Json&) {
  const std::string kind = json::get_string(src.value("params", Json::object()), "kind");
  auto eff = views::effective(c.env.root, kind);
  if (!eff) return Error(Errc::Unavailable, "view_kind_unavailable: " + eff.error().message);
  Read r;
  const std::string prefix = json::get_string(src, "prefix");
  for (const auto& e : eff->entries) {
    std::string label = c.say(e.value.value("label", Json()));
    if (label.empty()) label = e.id;
    Json n = c.make(src, prefix + e.id, label, c.parent_of(src), e.origin, "runtime/graph_views.pack#/" + kind + "/" + e.id, "preset");
    n["provenance"]["layers"] = Json::array();
    for (const auto& p : e.provenance) n["provenance"]["layers"].push_back(json::get_string(p, "layer"));
    n["order"] = static_cast<std::int64_t>(e.order);
    if (e.value.contains("meaning")) n["why"] = c.say(e.value["meaning"]);
    if (e.disabled) n["status"] = "disabled";
    for (const char* k : {"device", "source", "parent", "direction"})
      if (e.value.contains(k)) n["attrs"][k] = e.value[k];
    r.nodes.push_back(std::move(n));
  }
  for (const auto& h : eff->hidden) r.hidden.push_back(Json{{"source", json::get_string(src, "id")}, {"id", prefix + json::get_string(h, "id")}, {"by", h.value("by", Json(nullptr))}});
  return r;
}

}  // namespace

// The slots of the profile interview. See projection.h.
Result<std::vector<Json>> profile_slots(const Env& env) {
  Catalogue::Impl c;
  c.env = env;
  LOOM_TRY_ASSIGN(const Json* snap, c.profile());
  Json fields = snap ? snap->value("fields", Json::object()) : Json::object();
  if (!fields.is_object() || fields.empty()) {
    LOOM_TRY_ASSIGN(auto scenario, onboarding::builtin_scenario());
    fields = Json::object();
    for (const auto& f : scenario.value("fields", Json::array())) {
      Json one = f;
      one["status"] = "unknown";
      fields[json::get_string(f, "id")] = std::move(one);
    }
  }
  std::vector<Json> out;
  for (const auto& it : fields.items()) {
    const Json& f = it.value();
    const Json prov = f.value("provenance", Json(nullptr));
    out.push_back(Json{{"id", it.key()},
                       {"section", f.value("section", "")},
                       {"category", f.value("category", "")},
                       {"state", f.value("status", "unknown")},
                       {"value", f.value("value", Json(nullptr))},
                       {"provenance", prov},
                       {"interviewed", snap != nullptr}});
  }
  return out;
}

namespace {

Result<Read> read_party(Impl& c, const Json& src, const std::string&, const Json&) {
  Read r;
  Json self = c.make(src, "party:self", c.say(src.value("label", Json("You"))), c.parent_of(src), "rom", "graph/entry.pack#/seed/elements/party", "preset");
  self["kind"] = json::get_string(src, "class", "person");
  self["has_children"] = true;
  Env pe = c.env;
  auto slots = profile_slots(pe);
  if (!slots) {
    self["count_unknown_reason"] = slots.error().message;
    r.nodes.push_back(std::move(self));
    return r;
  }
  Json by = Json::object();
  const Json labels = src.value("slot_labels", Json::object());
  for (const auto& s : *slots) {
    const std::string id = json::get_string(s, "id");
    const std::string state = json::get_string(s, "state", "unknown");
    by[state] = by.value(state, static_cast<std::int64_t>(0)) + 1;
    std::string label = c.say(labels.value(id, Json()));
    if (label.empty()) label = id;
    const Json prov = s.value("provenance", Json(nullptr));
    const std::string method = prov.is_object() ? json::get_string(prov, "method", json::get_string(prov, "kind", "interview")) : std::string("interview");
    const bool known = state == "known";
    Json n = c.make(src, "party:self/" + id, label, "party:self", known ? "onboarding" : "rom", known ? "onboarding.profile.fields." + id : "onboarding/scenario.pack#/fields/" + id,
                    known ? method : "scenario", prov.is_object() ? prov.value("time", prov.value("created", Json(nullptr))) : Json(nullptr));
    n["kind"] = "profile_slot";
    n["status"] = state;
    n["value"] = known ? s.value("value", Json(nullptr)) : Json(nullptr);
    n["value_human"] = known ? c.human_value(s.value("value", Json(nullptr)), Json::object()) : c.say(c.words.value("slot_" + state, Json(state)));
    // A slot nobody answered carries the scenario's declaration; an answer is what you said, an inference is never shown as said.
    Json cls = known ? evidence_by_method(src, method, src.value("epistemic_default", Json::object())) : Json{{"class", "declared"}, {"confidence", nullptr}};
    n["evidence"] = evidence_of(cls, known ? "onboarding" : "rom", json::get_string(n["evidence"]["provenance"], "source_ref"), known ? method : "scenario",
                                json::get_string(n["evidence"]["provenance"], "time").empty() ? Json(nullptr) : n["evidence"]["provenance"]["time"]);
    n["attrs"] = Json{{"section", s.value("section", "")}, {"category", s.value("category", "")}};
    r.nodes.push_back(std::move(n));
  }
  self["attrs"] = Json{{"slots", by}};
  r.nodes.insert(r.nodes.begin(), std::move(self));
  return r;
}

// The onboarding layers (stored profile, else the shipped pack), for preferences and privacy rules.
Result<onboarding::DefaultLayers> layers_of(Impl& c) {
  auto snap = c.profile();
  if (snap && *snap && (*snap)->contains("pack") && (*snap)->contains("layers")) return onboarding::DefaultLayers::create((**snap)["pack"], (**snap)["layers"]);
  LOOM_TRY_ASSIGN(auto pack, onboarding::builtin_pack());
  return onboarding::DefaultLayers::create(pack);
}

Json layer_evidence(const Json& src, const std::string& layer, const std::string& ref) {
  const bool yours = layer == "user";
  return evidence_of(yours ? Json{{"class", "observed"}, {"confidence", 1.0}} : src.value("epistemic_default", Json::object()), yours ? "onboarding" : "rom", ref,
                     yours ? "override" : "preset");
}

Result<Read> read_preferences(Impl& c, const Json& src, const std::string&, const Json&) {
  LOOM_TRY_ASSIGN(auto layers, layers_of(c));
  Read r;
  for (const auto& e : layers.pack().value("entries", Json::array())) {
    const std::string key = json::get_string(e, "key");
    if (!starts_with(key, "preference.")) continue;
    LOOM_TRY_ASSIGN(auto res, layers.resolve(key));
    const std::string layer = json::get_string(res, "layer");
    const std::string name = key.substr(std::string("preference.").size());
    std::string label = c.say(src.value("labels", Json::object()).value(name, Json()));
    Json n = c.make(src, "preference:" + name, label.empty() ? name : label, "party:self", layer == "user" ? "onboarding" : "rom", "profiles/user.pack#" + key, "preset");
    n["value"] = res.value("value", Json(nullptr));
    n["value_human"] = c.human_value(n["value"], Json::object());
    n["status"] = json::get_string(res, "status");
    n["evidence"] = layer_evidence(src, layer, "profiles/user.pack#" + key);
    n["attrs"] = Json{{"key", key}, {"layer", layer}};
    r.nodes.push_back(std::move(n));
  }
  return r;
}

Result<Read> read_privacy(Impl& c, const Json& src, const std::string&, const Json&) {
  LOOM_TRY_ASSIGN(auto layers, layers_of(c));
  LOOM_TRY_ASSIGN(auto res, layers.resolve("onboarding.privacy"));
  const std::string layer = json::get_string(res, "layer");
  Read r;
  for (const auto& rule : res.value("value", Json::object()).value("rules", Json::array())) {
    const std::string id = json::get_string(rule, "id");
    if (id.empty()) continue;
    std::string label = c.say(src.value("labels", Json::object()).value(id, Json()));
    Json n = c.make(src, "policy:" + id, label.empty() ? id : label, "party:self", layer == "user" ? "onboarding" : "rom", "profiles/user.pack#onboarding.privacy", "preset");
    n["evidence"] = layer_evidence(src, layer, "profiles/user.pack#onboarding.privacy/" + id);
    for (const char* k : {"category", "store", "infer", "explicit_only", "providers", "retention"})
      if (rule.contains(k)) n["attrs"][k] = rule[k];
    r.nodes.push_back(std::move(n));
  }
  return r;
}

Result<Read> read_topics(Impl& c, const Json& src, const std::string&, const Json& paging) {
  if (!c.env.db) return Error(Errc::Unavailable, "database_unavailable");
  const std::int64_t limit = c.page_limit(src, paging);
  LOOM_TRY_ASSIGN(auto nodes, c.env.db->list_nodes(std::string_view("topic"), limit > 0 ? static_cast<int>(limit) : -1));
  Read r;
  for (const auto& t : nodes) {
    const std::string address = "interest:" + t.label;
    Json n = c.make(src, address, t.label, "party:self", "db", t.id, "analysis", t.created);
    n["same_as"] = Json::array({t.id});
    if (t.metadata.is_object() && t.metadata.contains("confidence")) n["evidence"]["confidence"] = t.metadata["confidence"];
    r.edges.push_back(Json{{"src", "party:self"}, {"dst", address}, {"kind", "interested_in"}});
    r.nodes.push_back(std::move(n));
  }
  return r;
}

Result<Read> read_memory(Impl& c, const Json& src, const std::string&, const Json&) {
  if (!c.env.memory) return Error(Errc::Unavailable, "memory_unavailable");
  Read r;
  std::map<std::string, std::int64_t> kids;
  const auto all = c.env.memory->get_all();
  for (const auto& m : all)
    if (m.parent_id) kids[*m.parent_id] += 1;
  for (const auto& m : all) {
    const Json prov = m.metadata.is_object() ? m.metadata.value("provenance", Json(nullptr)) : Json(nullptr);
    const std::string method = prov.is_object() ? json::get_string(prov, "method", "not_recorded") : std::string("not_recorded");
    Json n = c.make(src, "memory:" + m.id, clip(m.content, 60), m.parent_id ? "memory:" + *m.parent_id : std::string("party:self"), "memory.json", "memory.json#" + m.id,
                    method, m.created);
    n["evidence"] = evidence_of(evidence_by_method(src, method, src.value("epistemic_default", Json::object())), "memory.json", "memory.json#" + m.id, method,
                                prov.is_object() ? prov.value("time", Json(m.created)) : Json(m.created));
    n["count"] = kids[m.id];
    n["has_children"] = kids[m.id] > 0;
    n["status"] = m.active ? "active" : "inactive";
    n["attrs"] = Json{{"node_type", m.node_type}, {"tags", m.tags}};
    r.nodes.push_back(std::move(n));
  }
  return r;
}

std::string address_of_db(const std::string& id, const std::string& db_prefix, const std::string& address_prefix) {
  return address_prefix + (starts_with(id, db_prefix) ? id.substr(db_prefix.size()) : id);
}

Result<Read> read_conversations(Impl& c, const Json& src, const std::string&, const Json& paging) {
  if (!c.env.db) return Error(Errc::Unavailable, "database_unavailable");
  const std::string db_prefix = json::get_string(src, "same_as_prefix", "c_");
  const std::string prefix = json::get_string(src, "prefix");
  const std::int64_t limit = c.page_limit(src, paging);
  const std::int64_t offset = std::max<std::int64_t>(0, json::get_int(paging, "offset", 0));
  const std::string only = json::get_string(paging, "address");
  Read r;
  struct Row {
    std::string id, title, source, created, updated, metadata;
    std::int64_t messages = 0;
  };
  std::vector<Row> rows;
  std::int64_t total = 0;
  {
    auto lock = c.env.db->lock();
    auto& conn = c.env.db->conn();
    std::string sql = "SELECT c.id, c.title, c.source, c.created, c.updated, c.metadata, (SELECT COUNT(*) FROM messages m WHERE m.conv_id = c.id) FROM conversations c";
    if (!only.empty()) sql += " WHERE c.id = ?";
    sql += " ORDER BY c.updated DESC, c.rowid DESC";
    if (only.empty() && limit > 0) sql += " LIMIT ? OFFSET ?";
    LOOM_TRY_ASSIGN(auto st, conn.prepare(sql));
    if (!only.empty()) st.bind(1, db_prefix + only.substr(prefix.size()));
    else if (limit > 0) st.bind(1, limit).bind(2, offset);
    while (true) {
      LOOM_TRY_ASSIGN(bool more, st.step());
      if (!more) break;
      rows.push_back(Row{st.get_text(0), st.get_text(1), st.get_text(2), st.get_text(3), st.get_text(4), st.get_text(5), st.get_int(6)});
    }
    if (only.empty()) {
      LOOM_TRY_ASSIGN(auto n, conn.query_int("SELECT COUNT(*) FROM conversations"));
      total = n.value_or(0);
    } else {
      total = static_cast<std::int64_t>(rows.size());
    }
  }
  for (const auto& row : rows) {
    const std::string address = address_of_db(row.id, db_prefix, prefix);
    const std::string method = row.source == "user" || row.source.empty() ? std::string("conversation") : "import:" + row.source;
    Json n = c.make(src, address, row.title.empty() ? row.id : row.title, c.parent_of(src), "db", row.id, method, row.updated);
    n["same_as"] = Json::array({row.id});
    n["count"] = row.messages;
    n["has_children"] = row.messages > 0;
    n["attrs"] = Json{{"id", row.id}, {"source", row.source}, {"created", row.created}, {"updated", row.updated}};
    const Json meta = json::parse_or(row.metadata, Json::object());
    const Json units = meta.is_object() ? meta.value("units", Json::object()) : Json::object();
    if (units.is_object())
      for (const auto& u : units.items())
        if (u.value().is_string() && !u.value().get<std::string>().empty())
          r.edges.push_back(Json{{"src", address}, {"dst", "unit:" + u.key() + "/" + u.value().get<std::string>()}, {"kind", "member_of"}});
    r.nodes.push_back(std::move(n));
  }
  r.total = total;
  if (only.empty()) {
    r.page = Json{{"limit", limit}, {"offset", offset}, {"total", total}};
    if (limit > 0 && offset + limit < total) r.page["next_offset"] = offset + limit;
  }
  return r;
}

// The nodes rows of kind message that point at a message: message id -> node ids (identity rule of graph_sources.pack).
Result<std::map<std::string, std::vector<std::string>>> message_rows(Impl& c) {
  std::map<std::string, std::vector<std::string>> out;
  if (!c.env.db) return out;
  const Json rules = c.identity.value("node_rows", Json::array());
  for (const auto& rule : rules) {
    LOOM_TRY_ASSIGN(auto rows, c.env.db->list_nodes(std::string_view(json::get_string(rule, "kind")), -1));
    const std::string db_prefix = json::get_string(rule, "db_prefix");
    for (const auto& n : rows) {
      std::string target;
      for (const auto& where : strings_in(rule.value("points_with", Json::array()))) {
        if (where == "label") target = n.label;
        else if (starts_with(where, "metadata.") && n.metadata.is_object()) target = json::get_string(n.metadata, where.substr(9));
        if (starts_with(target, db_prefix)) break;
        target.clear();
      }
      if (!target.empty()) out[target].push_back(n.id);
    }
  }
  return out;
}

Result<Read> read_messages(Impl& c, const Json& src, const std::string& parent, const Json& paging) {
  if (!c.env.db) return Error(Errc::Unavailable, "database_unavailable");
  const std::string db_prefix = json::get_string(src, "same_as_prefix", "m_");
  const std::string prefix = json::get_string(src, "prefix");
  const Json* conv_src = c.source("conv");
  const std::string conv_prefix = conv_src ? json::get_string(*conv_src, "prefix") : std::string("conv:");
  const std::string conv_db = conv_src ? json::get_string(*conv_src, "same_as_prefix", "c_") : std::string("c_");
  Read r;
  std::vector<Message> msgs;
  std::string conv_id;
  if (const std::string only = json::get_string(paging, "address"); !only.empty()) {
    LOOM_TRY_ASSIGN(auto m, c.env.db->get_msg(db_prefix + only.substr(prefix.size())));
    if (m) {
      conv_id = m->conv_id;
      msgs.push_back(std::move(*m));
    }
    r.total = static_cast<std::int64_t>(msgs.size());
  } else {
    if (!starts_with(parent, conv_prefix)) return Error(Errc::InvalidArgument, "read_under_conversation");
    conv_id = conv_db + parent.substr(conv_prefix.size());
    const std::int64_t limit = c.page_limit(src, paging);
    LOOM_TRY_ASSIGN(auto page, c.env.db->get_msgs_page(conv_id, limit, json::get_string(paging, "before")));
    r.total = page.total;
    r.page = Json{{"limit", limit}, {"total", page.total}};
    if (!json::get_string(paging, "before").empty()) r.page["before"] = json::get_string(paging, "before");
    if (page.truncated) r.page["next_before"] = page.next_before;
    msgs = std::move(page.messages);
  }
  LOOM_TRY_ASSIGN(auto rows, message_rows(c));
  for (const auto& m : msgs) {
    const std::string address = address_of_db(m.id, db_prefix, prefix);
    Json n = c.make(src, address, m.role + ": " + clip(m.text, 60), address_of_db(m.conv_id, conv_db, conv_prefix), "db", m.id, m.role == "user" ? "said" : "message", m.created);
    n["same_as"] = Json::array({m.id});
    if (auto it = rows.find(m.id); it != rows.end())
      for (const auto& nid : it->second) {
        n["same_as"].push_back(nid);
        r.edges.push_back(Json{{"src", nid}, {"dst", address}, {"kind", "same_as"}});
      }
    n["status"] = m.status;
    n["attrs"] = Json{{"id", m.id}, {"role", m.role}, {"created", m.created}, {"semantic_status", m.semantic_status}};
    if (m.model) n["attrs"]["model"] = *m.model;
    if (m.parent_id && !m.parent_id->empty()) r.edges.push_back(Json{{"src", address}, {"dst", address_of_db(*m.parent_id, db_prefix, prefix)}, {"kind", "reply_to"}});
    r.nodes.push_back(std::move(n));
  }
  return r;
}

Result<Read> read_units(Impl& c, const Json& src, const std::string&, const Json&) {
  if (!c.env.db) return Error(Errc::Unavailable, "database_unavailable");
  std::map<std::string, std::int64_t> members;
  {
    auto lock = c.env.db->lock();
    LOOM_TRY_ASSIGN(auto st, c.env.db->conn().prepare("SELECT metadata FROM conversations WHERE json_extract(metadata, '$.units') IS NOT NULL"));
    while (true) {
      LOOM_TRY_ASSIGN(bool more, st.step());
      if (!more) break;
      const Json meta = json::parse_or(st.get_text(0), Json::object());
      const Json units = meta.is_object() ? meta.value("units", Json::object()) : Json::object();
      if (units.is_object())
        for (const auto& u : units.items())
          if (u.value().is_string() && !u.value().get<std::string>().empty()) members[u.key() + "/" + u.value().get<std::string>()] += 1;
    }
  }
  Read r;
  for (const auto& [id, n_members] : members) {
    Json n = c.make(src, "unit:" + id, id.substr(id.find('/') + 1), c.parent_of(src), "db", "conversations.metadata.units", "conversation");
    n["count"] = n_members;
    n["attrs"] = Json{{"unit_kind", id.substr(0, id.find('/'))}, {"conversations", n_members}};
    r.nodes.push_back(std::move(n));
  }
  return r;
}

Json catalogue_title(const Json& a, const std::string& lang) {
  std::string t = pick(a.value("human", Json::object()).value("title", Json()), lang);
  if (t.empty() && lang != "en") t = pick(a.value("labels", Json::object()), lang);
  if (t.empty()) t = json::get_string(a, "title", json::get_string(a, "id"));
  return t;
}

Result<Read> read_flows(Impl& c, const Json& src, const std::string&, const Json&) {
  LOOM_TRY_ASSIGN(auto cat, action_catalogue(c.env.root));
  auto shipped_profile = RuntimeProfile::builtin("actions");
  const Json shipped = shipped_profile ? shipped_profile->values().value("actions", Json::object()) : Json::object();
  std::vector<Json> composite, atomic;
  Read r;
  for (const auto& a : cat.value("actions", Json::array())) {
    const std::string id = json::get_string(a, "id");
    const Json* own = json::find(shipped, id);
    const std::string layer = own && *own == a ? std::string("rom") : std::string("overlay:actions");
    Json n = c.make(src, "flow:" + id, catalogue_title(a, c.lang()), c.parent_of(src), layer, "runtime/actions.pack#/actions/" + id, layer == "rom" ? "preset" : "defined_by_you");
    n["why"] = json::get_string(a, "description");
    n["status"] = json::get_string(a, "status");
    const Json human_steps = a.value("human", Json::object()).value("steps", Json::array());
    const Json steps = a.value("steps", Json::array());
    const std::size_t n_steps = human_steps.is_array() && !human_steps.empty() ? human_steps.size() : (steps.is_array() ? steps.size() : 0);
    n["count"] = static_cast<std::int64_t>(n_steps);
    n["has_children"] = n_steps > 0;
    n["attrs"] = Json{{"id", id}, {"action_kind", a.value("kind", "")}, {"uses_role", a.value("uses_role", Json(nullptr))}, {"reads", a.value("reads", Json::array())}};
    for (const auto& role : strings_in(a.value("uses_role", Json())))
      r.edges.push_back(Json{{"src", "flow:" + id}, {"dst", "role:" + role}, {"kind", "uses_role"}});
    for (const auto& key : strings_in(a.value("reads", Json::array())))
      r.edges.push_back(Json{{"src", "flow:" + id}, {"dst", "setting:" + key}, {"kind", "reads_setting"}});
    (json::get_string(a, "kind") == "composite" ? composite : atomic).push_back(std::move(n));
  }
  for (auto& n : composite) r.nodes.push_back(std::move(n));
  for (auto& n : atomic) r.nodes.push_back(std::move(n));
  return r;
}

Result<Read> read_steps(Impl& c, const Json& src, const std::string& parent, const Json& paging) {
  const std::string flow_prefix = "flow:";
  std::string flow = parent;
  std::size_t only_step = 0;
  if (const std::string only = json::get_string(paging, "address"); !only.empty()) {
    const std::string rest = only.substr(json::get_string(src, "prefix").size());
    const auto slash = rest.rfind('/');
    if (slash == std::string::npos) return Read{};
    flow = flow_prefix + rest.substr(0, slash);
    only_step = static_cast<std::size_t>(std::max<long long>(0, std::atoll(rest.substr(slash + 1).c_str())));
  }
  if (!starts_with(flow, flow_prefix)) return Error(Errc::InvalidArgument, "read_under_flow");
  const std::string id = flow.substr(flow_prefix.size());
  LOOM_TRY_ASSIGN(auto cat, action_catalogue(c.env.root));
  Read r;
  for (const auto& a : cat.value("actions", Json::array())) {
    if (json::get_string(a, "id") != id) continue;
    const Json human_steps = a.value("human", Json::object()).value("steps", Json::array());
    const bool described = human_steps.is_array() && !human_steps.empty();
    const Json steps = described ? human_steps : a.value("steps", Json::array());
    for (std::size_t i = 0; i < steps.size(); ++i) {
      if (only_step && only_step != i + 1) continue;
      const Json& s = steps[i];
      std::string label = described ? c.say(s.value("label", Json())) : json::get_string(s, "action", json::get_string(s, "id"));
      if (label.empty()) label = std::to_string(i + 1);
      Json n = c.make(src, "step:" + id + "/" + std::to_string(i + 1), label, flow, "rom", "runtime/actions.pack#/actions/" + id + "/" + (described ? "human/steps" : "steps"), "description");
      n["order"] = static_cast<std::int64_t>(i + 1);
      if (!described && s.contains("action")) r.edges.push_back(Json{{"src", n["address"]}, {"dst", "flow:" + json::get_string(s, "action")}, {"kind", "uses"}});
      r.nodes.push_back(std::move(n));
    }
  }
  return r;
}

}  // namespace

const std::map<std::string, Impl::Reader>& Impl::readers() {
  static const std::map<std::string, Reader> r = {
      {"graph_entry.areas", read_areas},  {"rom.items", read_rom},          {"settings.groups", read_groups},  {"settings.fields", read_settings},
      {"scopes.chain", read_scopes},      {"ports.roles", read_roles},      {"models.bound", read_models},     {"profile.items", read_profile_items},
      {"config.items", read_config_items}, {"views.effective", read_views}, {"party.self", read_party},         {"user.preferences", read_preferences},
      {"user.privacy", read_privacy},     {"db.topics", read_topics},       {"memory.notes", read_memory},     {"db.conversations", read_conversations},
      {"db.messages", read_messages},     {"db.units", read_units},         {"actions.catalogue", read_flows}, {"actions.steps", read_steps}};
  return r;
}

// ── roles ─────────────────────────────────────────────────────────────────────────

namespace {

std::string tier_reason(const Json& models, const std::string& model, const std::string& tier, const std::string& lang) {
  const std::string m = utf8::to_lower(model);
  for (const auto& t : models.value("tiers", Json::array())) {
    if (json::get_string(t, "id") != tier) continue;
    for (const auto& p : t.value("patterns", Json::array()))
      if (p.is_string() && !model.empty() && m.find(utf8::to_lower(p.get<std::string>())) != std::string::npos) {
        std::string text = pick(t.value("reason", Json::object()).value("pattern", Json()), lang);
        if (auto at = text.find("{pattern}"); at != std::string::npos) text.replace(at, 9, p.get<std::string>());
        return text;
      }
  }
  return pick(models.value("unknown_tier", Json::object()).value("reason", Json()), lang);
}

Json cost_of(const Impl& c, const std::string& model) {
  Json cost = Json::object();
  if (model.empty()) cost["per_million_unknown_reason"] = "no_model";
  else if (!c.env.models) cost["per_million_unknown_reason"] = "no_model_registry";
  else if (c.env.models->size() == 0) cost["per_million_unknown_reason"] = "model_registry_empty";
  else {
    const Json pricing = c.env.models->get_pricing(model);
    auto num = [](const Json& v) -> double {
      if (v.is_number()) return v.get<double>();
      if (v.is_string()) {
        try {
          return std::stod(v.get<std::string>());
        } catch (const std::exception& e) {
          log::write(log::Level::Debug, kLog, std::string("price is not a number: ") + e.what());
        }
      }
      return -1;
    };
    const double prompt = pricing.is_object() ? num(pricing.value("prompt", Json())) : -1;
    const double completion = pricing.is_object() ? num(pricing.value("completion", Json())) : -1;
    if (prompt < 0) cost["per_million_unknown_reason"] = "model_not_in_registry";
    else {
      cost["per_million"] = Json{{"input", prompt * 1e6}, {"output", completion < 0 ? Json(nullptr) : Json(completion * 1e6)}, {"currency", "USD"}};
    }
  }
  // The usage ledger records the action of an operation, not the role (runner.cpp); a per-role week needs that mapping.
  cost["last_7d_unknown_reason"] = "ledger_records_actions_not_roles";
  return cost;
}

const Json& role_rows(Impl& c) {
  if (c.roles_cache) return *c.roles_cache;
  c.roles_cache = Json::object();
  Json& out = *c.roles_cache;
  const PortProfile* ports = c.get_ports();
  if (!ports) {
    out["unknown"] = "ports_profile_unavailable";
    return out;
  }
  auto ports_values = RuntimeProfile::load("ports", c.env.root);
  const Json roles_data = ports_values ? ports_values->values().value("roles", Json::object()) : Json::object();
  auto shipped = RuntimeProfile::builtin("ports");
  const Json shipped_roles = shipped ? shipped->values().value("roles", Json::object()) : Json::object();
  auto policy = annotations::Policy::load_cached(c.env.root);
  const Json models = policy ? policy->models : Json::object();
  const Json policies = models.value("policies", Json::object());
  const Json src = c.source("role") ? *c.source("role") : Json::object();
  const Json modes = src.value("role_modes", Json::object());
  auto catalogue = action_catalogue(c.env.root);
  const Json actions = catalogue ? catalogue->value("actions", Json::array()) : Json::array();
  auto fields = c.fields();
  const Json settings = c.env.cfg ? c.env.cfg->all() : Json::object();
  const Json conv_ports = c.conversation_meta().value("ports", Json::object());
  const scopes::Resolver* resolver = c.get_resolver();
  Json rows = Json::array();
  for (const auto& name_j : ports->roles()) {
    const std::string id = name_j.get<std::string>();
    const Json data = roles_data.value(id, Json::object());
    Json row{{"id", id}, {"label", c.say(data.value("label", Json(id)))}, {"why", c.say(data.value("why", Json()))},
             {"layer", data == shipped_roles.value(id, Json()) ? "rom" : "overlay:ports"}};
    if (data.contains("status")) {
      Json st = data["status"];
      Json shown{{"state", json::get_string(st, "state")}};
      if (st.contains("today")) shown["today"] = c.say(st["today"]);
      if (st.contains("reason")) shown["reason"] = c.say(st["reason"]);
      row["status"] = shown;
    } else {
      row["status"] = Json{{"state", "implemented"}};
    }
    std::string policy_id;
    for (const auto& p : policies.items())
      if (json::get_string(p.value(), "role") == id) policy_id = p.key();
    row["policy"] = policy_id.empty() ? Json(nullptr) : Json(policy_id);
    // The model: the policy's answer (the same function every reader calls), else the ports resolution.
    std::string model, via, reason, tier, flow;
    if (!policy_id.empty() && c.env.cfg) {
      const auto choice = annotations::resolve_model(*c.env.cfg, c.env.root, policy_id, c.env.conv_id, std::string(), -1, c.env.db, ports, resolver);
      model = choice.model;
      via = choice.via;
      reason = choice.reason;
      tier = choice.tier;
      flow = choice.flow;
    } else if (auto res = ports->resolve(id, conv_ports, c.env.conv_id, settings)) {
      const Json m = res->value("params", Json::object()).value("model", Json());
      if (m.is_string()) model = m.get<std::string>();
      via = "ports:" + json::get_string(*res, "scope");
      if (model.empty()) reason = res->contains("compose") ? std::string("composed") : std::string("no_model");
      if (!model.empty()) tier = annotations::model_tier(models, model);
    } else {
      reason = res.error().message;
    }
    row["model"] = model.empty() ? Json(nullptr) : Json(model);
    row["model_human"] = model.empty() ? c.say(c.words.value("no_model", Json("no model"))) : (c.env.models ? c.env.models->name(model) : model);
    row["via"] = via;
    if (!reason.empty()) row["reason"] = reason;
    if (tier.empty() && !model.empty()) tier = annotations::model_tier(models, model);
    row["tier"] = Json{{"id", tier.empty() ? std::string("unknown") : tier}, {"reason", tier_reason(models, model, tier, c.lang())}};
    // The chain of the policy, step by step, with what each step reads today.
    Json chain = Json::array();
    const Json steps = policy_id.empty() ? Json::array() : policies[policy_id].value("chain", Json::array());
    for (std::size_t i = 0; i < steps.size(); ++i) {
      const Json& s = steps[i];
      const std::string type = json::get_string(s, "type");
      Json step{{"step", i + 1}, {"type", type}, {"meaning", c.say(s.value("meaning", Json()))}};
      Json value = nullptr;
      if (type == "explicit" && s.contains("setting")) {
        step["setting"] = s["setting"];
        if (resolver) {
          scopes::Binding b;
          b.key = json::get_string(s, "setting");
          b.conv_id = c.env.conv_id;
          if (auto r = resolver->resolve(b); r && r->defined) value = r->value;
        }
      } else if (type == "explicit" && s.contains("model")) {
        value = s["model"];
      } else if (type == "role") {
        step["role"] = s.value("role", "");
        if (auto res = ports->resolve(json::get_string(s, "role"), Json::object(), c.env.conv_id, settings))
          value = res->value("params", Json::object()).value("model", Json(nullptr));
      }
      step["value"] = value;
      const std::string mine = type == "explicit" ? "explicit:" + json::get_string(s, "setting", "model") : type == "role" ? "role:" + json::get_string(s, "role") : type;
      step["decides"] = !via.empty() && (via == mine || (type == "relative_to" && starts_with(via, "relative_to")));
      chain.push_back(std::move(step));
    }
    row["chain"] = chain;
    // The flow a class of model gets, for the policies whose readers act on it (policies with readers).
    const Json pol = policy_id.empty() ? Json::object() : policies[policy_id];
    if (!flow.empty() && pol.contains("readers")) {
      const Json def = models.value("flow_defs", Json::object()).value(flow, Json::object());
      row["flow"] = Json{{"id", flow}, {"status", def.value("status", "")}, {"does", def.value("does", Json::array())}, {"planned", def.value("planned", Json::array())},
                         {"calls", def.value("calls", "")}};
    } else {
      row["flow"] = nullptr;
    }
    if (const Json* mode = json::find(modes, id); mode && mode->is_object()) {
      auto worker = RuntimeProfile::load(json::get_string(*mode, "profile", "worker"), c.env.root);
      Json m = Json::object();
      if (worker) {
        const Json* list = at_pointer(worker->values(), json::get_string(*mode, "pointer"));
        const Json* threshold = at_pointer(worker->values(), json::get_string(*mode, "threshold_pointer"));
        m["modes"] = list ? *list : Json(nullptr);
        m["batch_threshold"] = threshold ? *threshold : Json(nullptr);
      }
      if (const std::string key = json::get_string(*mode, "batch_key"); !key.empty()) m["batch_key_set"] = c.env.secrets ? Json(c.env.secrets->has(key)) : Json(nullptr);
      row["mode"] = m;
    }
    row["cost"] = cost_of(c, model);
    Json readers = Json::array();
    for (const auto& a : actions)
      for (const auto& r : strings_in(a.value("uses_role", Json())))
        if (r == id) readers.push_back(Json{{"id", json::get_string(a, "id")}, {"title", catalogue_title(a, c.lang())}});
    row["flows_reading"] = readers;
    Json keys = Json::array();
    if (fields)
      for (const auto& f : (*fields)->value("fields", Json::array()))
        if (json::get_string(f.value("human", Json::object()), "role") == id)
          keys.push_back(Json{{"key", json::get_string(f, "key")}, {"label", c.say(f["human"].value("label", Json()))}});
    row["settings"] = keys;
    rows.push_back(std::move(row));
  }
  out["roles"] = rows;
  return out;
}

}  // namespace

// ── Catalogue ─────────────────────────────────────────────────────────────────────

Result<Catalogue> Catalogue::open(Env env) {
  Catalogue cat;
  cat.impl_ = std::make_shared<Impl>();
  Impl& c = *cat.impl_;
  c.env = std::move(env);
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("graph_sources", c.env.root));
  const Json& v = profile.values();
  c.profile_values = v;
  for (const auto& s : v.value("sources", Json::array())) {
    if (json::get_bool(s, "hidden", false)) c.hidden_sources.push_back(Json{{"source", json::get_string(s, "id")}, {"id", json::get_string(s, "prefix")}, {"by", "overlay:graph_sources"}});
    else c.sources.push_back(s);
  }
  c.identity = v.value("identity", Json::object());
  c.root_row = v.value("root", Json::object());
  c.words = v.value("value_words", Json::object());
  return cat;
}

const Env& Catalogue::env() const { return impl_->env; }
const Json& Catalogue::sources() const { return impl_->sources; }
const Json& Catalogue::hidden_sources() const { return impl_->hidden_sources; }
std::size_t Catalogue::resolver_opens() const { return impl_->opens; }

const Json* Catalogue::source_of(std::string_view address) const {
  const Json* best = nullptr;
  std::size_t best_len = 0;
  for (const auto& s : impl_->sources) {
    const std::string p = json::get_string(s, "prefix");
    if (!p.empty() && p.size() > best_len && starts_with(address, p) && address.size() > p.size()) {
      best = &s;
      best_len = p.size();
    }
  }
  return best;
}

std::string Catalogue::canonical(std::string_view address) {
  const std::string a(address);
  for (const auto& f : impl_->identity.value("db_forms", Json::array())) {
    const std::string db = json::get_string(f, "db_prefix");
    if (!db.empty() && starts_with(a, db) && a.size() > db.size()) return json::get_string(f, "address_prefix") + a.substr(db.size());
  }
  // A nodes row of kind message that points at a message is that message.
  if (impl_->env.db && starts_with(a, "n_")) {
    auto rows = message_rows(*impl_);
    if (!rows) log::write(log::Level::Debug, kLog, "message rows not readable: " + rows.error().message);
    else
      for (const auto& [msg, ids] : *rows)
        if (std::find(ids.begin(), ids.end(), a) != ids.end()) return canonical(msg);
  }
  return a;
}

Result<std::optional<Json>> Catalogue::node(const std::string& raw) {
  const std::string address = canonical(raw);
  Impl& c = *impl_;
  if (address.empty()) {
    Json root{{"address", ""}, {"label", c.say(c.root_row.value("label", Json("Graph")))}, {"kind", json::get_string(c.root_row, "kind", "root")}, {"source", "root"},
              {"parent", nullptr}, {"has_children", true}, {"evidence", evidence_of(Json{{"class", "declared"}, {"confidence", 1.0}}, "rom", "runtime/graph_sources.pack", "preset", nullptr)},
              {"provenance", Json{{"layer", "rom"}, {"layers", Json::array({"rom"})}}}, {"attrs", Json::object()}};
    return std::optional<Json>(std::move(root));
  }
  const Json* src = source_of(address);
  if (!src) return std::optional<Json>();
  if (Impl::paged(*src)) {
    LOOM_TRY_ASSIGN(auto r, c.read_under(*src, std::string(), Json{{"address", address}}));
    for (auto& n : r.nodes)
      if (json::get_string(n, "address") == address) return std::optional<Json>(std::move(n));
    return std::optional<Json>();
  }
  LOOM_TRY_ASSIGN(const Read* r, c.read_whole(*src));
  for (const auto& n : r->nodes)
    if (json::get_string(n, "address") == address) return std::optional<Json>(n);
  return std::optional<Json>();
}

Result<Catalogue::Children> Catalogue::children(const std::string& raw_parent, const Json& paging) {
  Impl& c = *impl_;
  const std::string parent = canonical(raw_parent);
  Children out;
  std::string parent_source;
  if (const Json* ps = parent.empty() ? nullptr : source_of(parent)) parent_source = json::get_string(*ps, "id");
  std::set<std::string> seen;
  for (const auto& s : c.sources) {
    const Json p = s.value("parent", Json::object());
    const std::string area = json::get_string(p, "area");
    const std::string of = json::get_string(p, "of");
    // The sources parented to this area or to the focus's source, and the focus's own source (nodes nested in their own kind: the
    // slots of You, the notes under a note).
    const bool here = parent.empty() ? json::get_bool(p, "root", false)
                                     : (!area.empty() && parent == "area:" + area) || (!of.empty() && of == parent_source) ||
                                           (!parent_source.empty() && json::get_string(s, "id") == parent_source);
    if (!here) continue;
    const std::string sid = json::get_string(s, "id");
    Read paged_read;
    const Read* r = nullptr;
    if (Impl::paged(s)) {
      auto got = c.read_under(s, parent, paging);
      if (!got) {
        out.unknown.push_back(Json{{"source", sid}, {"reason", got.error().message}});
        continue;
      }
      paged_read = std::move(*got);
      r = &paged_read;
      if (!r->page.is_null()) out.pages[sid] = r->page;
    } else {
      auto got = c.read_whole(s);
      if (!got) {
        out.unknown.push_back(Json{{"source", sid}, {"reason", got.error().message}});
        continue;
      }
      r = *got;
    }
    if (!r->unknown.empty()) {
      out.unknown.push_back(Json{{"source", sid}, {"reason", r->unknown}});
      continue;
    }
    ++out.sources_read;
    std::int64_t mine = 0;
    std::set<std::string> addresses;
    for (const auto& n : r->nodes) {
      if (json::get_string(n, "parent") != parent) continue;
      const std::string a = json::get_string(n, "address");
      if (!seen.insert(a).second) continue;  // counted by unique address
      addresses.insert(a);
      out.nodes.push_back(n);
      ++mine;
    }
    for (const auto& h : r->hidden) {
      // Hidden entries of this parent only (a hidden group hides its fields from that group).
      if (json::get_string(s.value("parent", Json::object()), "of").empty() || json::get_string(h, "by") == parent || !parent.empty())
        out.hidden.push_back(h);
    }
    out.total += Impl::paged(s) && r->total >= 0 ? r->total : mine;
    for (const auto& e : r->edges)
      if (addresses.count(json::get_string(e, "src")) || addresses.count(json::get_string(e, "dst"))) out.edges.push_back(e);
  }
  return out;
}

Result<std::vector<Json>> Catalogue::children_of_source(const std::string& id) {
  const Json* s = impl_->source(id);
  if (!s || Impl::paged(*s)) return std::vector<Json>();
  LOOM_TRY_ASSIGN(const Read* r, impl_->read_whole(*s));
  return r->nodes;
}

Result<std::vector<Json>> Catalogue::edges() {
  Impl& c = *impl_;
  std::vector<Json> out;
  for (const auto& s : c.sources) {
    if (Impl::paged(s)) continue;
    auto got = c.read_whole(s);
    if (!got) continue;
    const Read* r = *got;
    for (const auto& e : r->edges) out.push_back(e);
    const std::string contain = json::get_string(s, "contain_edge", "part_of");
    for (const auto& n : r->nodes) {
      const std::string p = json::get_string(n, "parent");
      if (!p.empty()) out.push_back(Json{{"src", json::get_string(n, "address")}, {"dst", p}, {"kind", contain}});
    }
  }
  return out;
}

Result<Json> Catalogue::roles() {
  const Json& rows = role_rows(*impl_);
  if (rows.contains("unknown")) return Error(Errc::Unavailable, json::get_string(rows, "unknown"));
  return rows;
}

Json Catalogue::edge_meaning(const std::string& kind) {
  views::EdgeKinds* k = impl_->edge_kinds();
  if (!k) return nullptr;
  const views::EffectiveEntry* e = k->view().find(kind);
  if (!e || !e->value.contains("meaning")) return nullptr;
  return impl_->say(e->value["meaning"]);
}

// ── actions ───────────────────────────────────────────────────────────────────────

namespace {

const std::set<std::string> kRowKeys = {"address", "label", "kind", "value", "value_human", "count", "count_unknown_reason", "provenance", "evidence",
                                        "evidence_overrides", "has_children", "status", "why", "order", "same_as", "source", "empty_reason"};

Json row_of(const Json& n) {
  Json r = Json::object();
  for (const auto& k : kRowKeys)
    if (n.contains(k)) r[k] = n[k];
  return r;
}

std::int64_t children_limit(const Env& env, const Json& request) {
  if (request.contains("limit") && request["limit"].is_number_integer()) return request["limit"].get<std::int64_t>();
  if (env.cfg) {
    const Json v = env.cfg->get("navigator_children_limit");
    if (v.is_number_integer()) return v.get<std::int64_t>();
  }
  return 50;  // the preset of navigator_children_limit when no settings are known (config.cpp DEFAULTS)
}

std::string folded(const std::string& s) { return utf8::to_lower(s); }

// The focus an area hands over to (expand: graph.navigate {focus}), or "".
std::string redirect_of(const Json& node) {
  const Json expand = node.value("attrs", Json::object()).value("expand", Json(nullptr));
  if (!expand.is_object() || json::get_string(expand, "view") != "graph.navigate") return std::string();
  const std::string focus = json::get_string(expand.value("params", Json::object()), "focus");
  return focus == json::get_string(node, "address") ? std::string() : focus;
}

Result<Json> breadcrumbs(Catalogue& cat, const Json& focus) {
  Json out = Json::array();
  std::string at = json::get_string(focus, "parent");
  std::set<std::string> guard;
  std::vector<Json> chain;
  while (!focus.value("parent", Json(nullptr)).is_null() && guard.insert(at).second) {
    LOOM_TRY_ASSIGN(auto n, cat.node(at));
    if (!n) break;
    chain.push_back(Json{{"address", json::get_string(*n, "address")}, {"label", json::get_string(*n, "label")}});
    if (n->value("parent", Json(nullptr)).is_null()) break;
    at = json::get_string(*n, "parent");
  }
  std::reverse(chain.begin(), chain.end());
  // An area that hands its focus over to the node in view is that node: it is not shown twice.
  for (const auto& c : chain) {
    if (starts_with(json::get_string(c, "address"), "area:")) {
      LOOM_TRY_ASSIGN(auto n, cat.node(json::get_string(c, "address")));
      if (n && redirect_of(*n) == json::get_string(focus, "address")) continue;
    }
    out.push_back(c);
  }
  return out;
}

// The count of a child row: its own (sources that know it) or its children's total; unknown says why.
Result<void> fill_count(Catalogue& cat, Json& row, const Json& node) {
  if (node.contains("count") || node.contains("count_unknown_reason")) return {};
  std::string target = json::get_string(node, "address");
  // A node its reader knows to be a leaf has no count to give; an area is always counted (or says why it cannot be).
  if (!starts_with(target, "area:") && !json::get_bool(node, "has_children", false)) return {};
  if (const std::string to = redirect_of(node); !to.empty()) target = to;
  bool any_source = !redirect_of(node).empty();
  const Json* ps = cat.source_of(target);
  const std::string ps_id = ps ? json::get_string(*ps, "id") : std::string();
  for (const auto& s : cat.sources()) {
    const Json p = s.value("parent", Json::object());
    if ((starts_with(target, "area:") && "area:" + json::get_string(p, "area") == target) || (!ps_id.empty() && json::get_string(p, "of") == ps_id)) any_source = true;
  }
  if (!any_source && !ps_id.empty()) any_source = json::get_bool(node, "has_children", false);  // nested in its own source
  if (!any_source) {
    row["has_children"] = false;
    // An area no source lists yet says so; any other node without children is a leaf (no count to give).
    if (starts_with(target, "area:")) row["count_unknown_reason"] = "no_projection_source";
    return {};
  }
  LOOM_TRY_ASSIGN(auto kids, cat.children(target, Json{{"page_limit", 1}}));
  if (kids.sources_read == 0 && !kids.unknown.empty()) {
    row["count_unknown_reason"] = json::get_string(kids.unknown[0], "reason");
    row["has_children"] = false;
    return {};
  }
  row["count"] = kids.total;
  row["has_children"] = kids.total > 0;
  if (!kids.unknown.empty()) row["count_partial"] = kids.unknown;
  return {};
}

}  // namespace

Result<std::string> find_address(const Env& env, const std::string& raw) {
  std::string words = raw;
  while (!words.empty() && (words.back() == ' ' || words.back() == '.')) words.pop_back();
  while (!words.empty() && words.front() == ' ') words.erase(0, 1);
  if (words.empty()) return std::string();
  LOOM_TRY_ASSIGN(auto cat, Catalogue::open(env));
  const std::string direct = cat.canonical(words);
  if (cat.source_of(direct)) {
    LOOM_TRY_ASSIGN(auto n, cat.node(direct));
    return n ? direct : std::string();
  }
  const std::string want = folded(words);
  std::vector<Json> pool;
  LOOM_TRY_ASSIGN(auto areas, cat.children(""));
  for (auto& n : areas.nodes) pool.push_back(std::move(n));
  for (const auto& s : cat.sources()) {
    if (Impl::paged(s) || json::get_bool(s.value("parent", Json::object()), "root", false)) continue;
    LOOM_TRY_ASSIGN(auto all, cat.children_of_source(json::get_string(s, "id")));
    for (auto& n : all) pool.push_back(std::move(n));
  }
  auto id_of = [](const std::string& address) { return address.substr(address.find(':') + 1); };
  for (const auto& n : pool)
    if (folded(json::get_string(n, "label")) == want || folded(id_of(json::get_string(n, "address"))) == want) return json::get_string(n, "address");
  for (const auto& n : pool)
    if (folded(json::get_string(n, "label")).find(want) != std::string::npos) return json::get_string(n, "address");
  return std::string();
}

Result<Json> navigate(const Env& env, const Json& request) {
  LOOM_TRY_ASSIGN(auto cat, Catalogue::open(env));
  std::string focus_address = cat.canonical(json::get_string(request, "focus"));
  std::string handed_from;
  LOOM_TRY_ASSIGN(auto focus, cat.node(focus_address));
  if (!focus) return Error(Errc::NotFound, "no node at '" + focus_address + "' in the graph projection");
  if (const std::string to = redirect_of(*focus); !to.empty()) {
    handed_from = focus_address;
    focus_address = cat.canonical(to);
    LOOM_TRY_ASSIGN(focus, cat.node(focus_address));
    if (!focus) return Error(Errc::NotFound, "the area " + handed_from + " opens '" + focus_address + "', which no source knows");
  }
  const std::int64_t limit = children_limit(env, request);
  const std::int64_t hops = std::max<std::int64_t>(1, json::get_int(request, "hops", 1));
  const std::string query = folded(json::get_string(request, "query"));
  const std::vector<std::string> perspectives = strings_in(request.value("perspectives", Json::array()));
  Json paging = Json::object();
  if (request.contains("offset")) paging["offset"] = request["offset"];
  if (request.contains("before")) paging["before"] = request["before"];

  std::vector<Json> nodes;  // with depth
  std::vector<Json> edges;
  Json unknown = Json::array(), hidden = Json::array(), pages = Json::object();
  std::int64_t total = 0;
  std::set<std::string> seen{focus_address};
  Json perspective_info = Json::array();
  if (!perspectives.empty()) {
    // The perspectives choose the edges: the nodes they reach from the focus, `hops` far, in both directions.
    LOOM_TRY_ASSIGN(auto persp, views::Perspectives::open(env.root));
    std::set<std::string> kinds;
    for (const auto& p : perspectives) {
      LOOM_TRY_ASSIGN(auto ks, persp.edge_kinds_of(p));
      kinds.insert(ks.begin(), ks.end());
      perspective_info.push_back(p);
    }
    LOOM_TRY_ASSIGN(auto all, cat.edges());
    LOOM_TRY_ASSIGN(auto here, cat.children(focus_address, paging));
    for (const auto& e : here.edges) all.push_back(e);
    std::deque<std::pair<std::string, std::int64_t>> todo{{focus_address, 0}};
    while (!todo.empty()) {
      auto [cur, depth] = todo.front();
      todo.pop_front();
      if (depth >= hops) continue;
      for (const auto& e : all) {
        if (!kinds.count(json::get_string(e, "kind"))) continue;
        const std::string s = json::get_string(e, "src"), d = json::get_string(e, "dst");
        if (s != cur && d != cur) continue;
        edges.push_back(e);
        const std::string next = s == cur ? d : s;
        if (!seen.insert(next).second) continue;
        LOOM_TRY_ASSIGN(auto n, cat.node(next));
        if (!n) continue;
        Json one = *n;
        one["depth"] = depth + 1;
        nodes.push_back(std::move(one));
        ++total;
        todo.push_back({next, depth + 1});
      }
    }
  } else {
    // Containment: the children of the focus, and theirs up to `hops`.
    std::vector<std::pair<std::string, std::int64_t>> level{{focus_address, 0}};
    for (std::int64_t depth = 0; depth < hops && !level.empty(); ++depth) {
      std::vector<std::pair<std::string, std::int64_t>> next;
      for (const auto& [at, d] : level) {
        LOOM_TRY_ASSIGN(auto kids, cat.children(at, depth == 0 ? paging : Json::object()));
        if (depth == 0) {
          total = kids.total;
          unknown = kids.unknown;
          hidden = kids.hidden;
          pages = kids.pages;
        }
        for (const auto& e : kids.edges) edges.push_back(e);
        const Json* src = nullptr;
        for (auto& n : kids.nodes) {
          const std::string a = json::get_string(n, "address");
          if (!seen.insert(a).second) continue;
          src = cat.source_of(a);
          edges.push_back(Json{{"src", a}, {"dst", at}, {"kind", src ? json::get_string(*src, "contain_edge", "part_of") : std::string("part_of")}});
          Json one = n;
          one["depth"] = d + 1;
          if (json::get_bool(n, "has_children", false) || starts_with(a, "area:")) next.push_back({a, d + 1});
          nodes.push_back(std::move(one));
        }
      }
      level = std::move(next);
    }
  }
  if (!query.empty()) {
    std::vector<Json> kept;
    for (auto& n : nodes)
      if (folded(json::get_string(n, "label")).find(query) != std::string::npos || folded(json::get_string(n, "address")).find(query) != std::string::npos)
        kept.push_back(std::move(n));
    total = static_cast<std::int64_t>(kept.size());
    nodes = std::move(kept);
  }
  Json children = Json::array();
  std::set<std::string> shown{focus_address};
  std::int64_t shown_first = 0;
  for (const auto& n : nodes) {
    const bool first_level = json::get_int(n, "depth", 1) == 1;
    if (first_level && limit > 0 && shown_first >= limit) continue;
    if (first_level) ++shown_first;
    Json row = row_of(n);
    row["depth"] = n.value("depth", 1);
    if (const Json attrs = n.value("attrs", Json::object()); attrs.contains("counts")) row["counts"] = attrs["counts"];
    LOOM_TRY(fill_count(cat, row, n));
    shown.insert(json::get_string(n, "address"));
    children.push_back(std::move(row));
  }
  Json edge_rows = Json::array();
  std::set<std::string> edge_seen;
  for (const auto& e : edges) {
    const std::string s = json::get_string(e, "src"), d = json::get_string(e, "dst"), k = json::get_string(e, "kind");
    if (!shown.count(s) || !shown.count(d)) continue;  // both ends drawn; the card lists every relation
    if (!edge_seen.insert(s + "\x1f" + d + "\x1f" + k).second) continue;
    edge_rows.push_back(Json{{"src", s}, {"dst", d}, {"kind", k}, {"meaning", cat.edge_meaning(k)}});
  }
  LOOM_TRY_ASSIGN(auto crumbs, breadcrumbs(cat, *focus));
  const std::int64_t first_total = perspectives.empty() ? total : total;
  Json out{{"schema", "loom.graph_navigate/1"},
           {"focus", Json{{"address", focus_address},
                          {"label", focus->value("label", "")},
                          {"kind", focus->value("kind", "")},
                          {"evidence", focus->value("evidence", Json::object())},
                          {"provenance", focus->value("provenance", Json::object())},
                          {"why", focus->value("why", Json(nullptr))},
                          {"value", focus->value("value", Json(nullptr))},
                          {"value_human", focus->value("value_human", Json(nullptr))}}},
           {"children", children},
           {"edges", edge_rows},
           {"breadcrumbs", crumbs},
           {"counts", Json{{"total", first_total}, {"shown", shown_first}, {"collapsed", std::max<std::int64_t>(0, first_total - shown_first)}}},
           {"hidden_by_you", Json{{"count", hidden.size()}, {"items", hidden}}},
           {"unknown", unknown},
           {"pages", pages},
           {"limit", Json{{"value", limit}, {"from", request.contains("limit") ? "request" : "navigator_children_limit"}}},
           {"hops", hops},
           {"resolver_opens", cat.resolver_opens()}};
  if (!handed_from.empty()) out["handed_from"] = handed_from;
  if (!perspective_info.empty()) out["perspectives"] = perspective_info;
  if (!cat.hidden_sources().empty()) out["hidden_sources"] = cat.hidden_sources();
  return out;
}

Result<Json> card(const Env& env, const Json& request) {
  const std::string raw = json::get_string(request, "address", json::get_string(request, "focus"));
  if (raw.empty()) return Error(Errc::InvalidArgument, "graph.card needs an address");
  LOOM_TRY_ASSIGN(auto cat, Catalogue::open(env));
  const std::string address = cat.canonical(raw);
  LOOM_TRY_ASSIGN(auto node, cat.node(address));
  if (!node) return Error(Errc::NotFound, "no node at '" + address + "' in the graph projection");
  Json out = *node;
  out["schema"] = "loom.graph_card/1";
  out["address"] = address;
  const std::string source = json::get_string(*node, "source");
  out["evidence_per_property"] = Json{{"*", node->value("evidence", Json::object())}};
  if (node->contains("evidence_overrides"))
    for (const auto& o : (*node)["evidence_overrides"].items()) out["evidence_per_property"][o.key()] = o.value();
  if (source == "setting") {
    const Json attrs = node->value("attrs", Json::object());
    const Json human = attrs.value("human", Json::object());
    out["why"] = human.value("why", "");
    out["effect"] = human.value("effect", "");
    out["levels"] = human.value("levels", Json::array());
    out["edit_rule"] = attrs.value("edit", "");
    const Json resolved = attrs.value("resolved", Json::object());
    out["inheritance"] = resolved.value("inheritance", Json::array());
    out["evidence_per_property"]["value"] = resolved.value("evidence", Json::object());
    Json flows = Json::array();
    auto catalogue = action_catalogue(env.root);
    for (const auto& f : strings_in(human.value("flows", Json::array()))) {
      Json title = f;
      if (catalogue)
        for (const auto& a : catalogue->value("actions", Json::array()))
          if (json::get_string(a, "id") == f) title = catalogue_title(a, env.lang.empty() ? std::string("en") : env.lang);
      flows.push_back(Json{{"id", f}, {"title", title}});
    }
    out["flows_reading"] = flows;
  } else if (source == "role") {
    LOOM_TRY_ASSIGN(auto roles, cat.roles());
    for (const auto& r : roles.value("roles", Json::array()))
      if ("role:" + json::get_string(r, "id") == address) out["role"] = r;
  } else if (source == "rom") {
    LOOM_TRY_ASSIGN(auto layered, rom::MountRegistry::standard(env.root).read(address));
    out["item"] = layered.value("item", Json::object());
    out["rom_item"] = layered.value("rom_item", Json::object());
    out["can_override"] = Json{{"action", "rom.override"}, {"file", (env.root / "profiles" / "graph_entry.pack").string()},
                               {"pointer", "/overrides/rom/items/" + address.substr(4)}};
  }
  // Relations grouped by edge kind, both directions.
  LOOM_TRY_ASSIGN(auto all, cat.edges());
  LOOM_TRY_ASSIGN(auto kids, cat.children(address, Json{{"page_limit", 50}}));
  for (const auto& e : kids.edges) all.push_back(e);
  Json relations = Json::object();
  std::set<std::string> rel_seen;
  for (const auto& e : all) {
    const std::string s = json::get_string(e, "src"), d = json::get_string(e, "dst"), k = json::get_string(e, "kind");
    if (s != address && d != address) continue;
    if (!rel_seen.insert(s + "\x1f" + d + "\x1f" + k).second) continue;
    if (!relations.contains(k)) relations[k] = Json{{"meaning", cat.edge_meaning(k)}, {"out", Json::array()}, {"in", Json::array()}};
    const std::string other = s == address ? d : s;
    std::string label = other;
    if (auto n = cat.node(other); n && *n) label = json::get_string(**n, "label", other);
    relations[k][s == address ? "out" : "in"].push_back(Json{{"address", other}, {"label", label}});
  }
  out["relations"] = relations;
  out["children_count"] = kids.total;
  // What can be done with it: actions.for on its element.
  const actions::ElementSources sources{env.db, nullptr, env.root};
  auto fits = actions::invoke(env.root, "for", Json{{"element", address}}, &sources, env.cfg);
  if (fits) {
    Json acts = Json::array();
    for (const auto& a : fits->value("actions", Json::array()))
      acts.push_back(Json{{"id", json::get_string(a, "id", json::get_string(a, "action"))}, {"decision", a.value("decision", Json(nullptr))}, {"label", a.value("label", Json(nullptr))}});
    out["actions"] = acts;
  } else {
    out["actions"] = Json::array();
    out["actions_unknown_reason"] = fits.error().message;
  }
  LOOM_TRY_ASSIGN(auto crumbs, breadcrumbs(cat, *node));
  out["breadcrumbs"] = crumbs;
  return out;
}

Result<Json> settings_resolved(const Env& env, const Json& request) {
  Env e = env;
  if (const std::string conv = json::get_string(request, "conv_id"); !conv.empty()) e.conv_id = conv;
  LOOM_TRY_ASSIGN(auto cat, Catalogue::open(e));
  const Json* src = nullptr;
  std::string groups_parent;
  for (const auto& s : cat.sources()) {
    if (json::get_string(s, "reader") == "settings.fields") src = &s;
    if (json::get_string(s, "reader") == "settings.groups") groups_parent = "area:" + json::get_string(s.value("parent", Json::object()), "area");
  }
  if (!src || groups_parent == "area:") return Error(Errc::NotFound, "graph_sources has no settings or settings groups source");
  // Every field through the one Resolver of this render.
  LOOM_TRY_ASSIGN(auto groups_read, cat.children(groups_parent));
  Json groups = Json::array();
  std::int64_t field_count = 0;
  Json profile_values = Json::array();
  for (const auto& g : groups_read.nodes) {
    if (json::get_string(g, "source") != "group") continue;
    LOOM_TRY_ASSIGN(auto fields, cat.children(json::get_string(g, "address")));
    Json rows = Json::array();
    for (const auto& f : fields.nodes) {
      const Json attrs = f.value("attrs", Json::object());
      const Json resolved = attrs.value("resolved", Json::object());
      const Json human = attrs.value("human", Json::object());
      Json flows = Json::array();
      for (const auto& id : strings_in(human.value("flows", Json::array()))) flows.push_back(id);
      Json row{{"key", attrs.value("key", "")},
               {"address", f.value("address", "")},
               {"human", human},
               {"value", f.value("value", Json(nullptr))},
               {"value_human", f.value("value_human", "")},
               {"provenance", resolved.value("provenance", Json::object())},
               {"inheritance", resolved.value("inheritance", Json::array())},
               {"edit_rule", attrs.value("edit", "")},
               {"evidence", f.value("evidence", Json::object())},
               {"flows_reading", flows},
               {"proposals", Json::array()}};
      if (f.contains("status")) row["status"] = f["status"];
      if (attrs.value("profile_value", false)) {
        row["profile_value"] = attrs.value("profile", Json::object());
        profile_values.push_back(row["key"]);
      }
      if (resolved.contains("secret")) row["secret"] = true;
      ++field_count;
      rows.push_back(std::move(row));
    }
    groups.push_back(Json{{"id", json::get_string(g, "address").substr(6)}, {"label", g.value("label", "")}, {"why", g.value("why", "")}, {"order", g.value("order", 0)},
                          {"provenance", g.value("provenance", Json::object())}, {"fields", rows}});
  }
  // Fields of a hidden group are listed by key, never dropped.
  Json hidden_keys = Json::array();
  auto doc = settings_fields(e.root);
  if (doc) {
    std::set<std::string> hidden_groups;
    const Json all_groups = doc->value("groups", Json::object());
    for (const auto& g : all_groups.items())
      if (json::get_bool(g.value(), "hidden", false)) hidden_groups.insert(g.key());
    for (const auto& f : all_fields(*doc))
      if (hidden_groups.count(group_of(*f.field))) hidden_keys.push_back(f.key);
  }
  const Json status = src->value("proposals_status", Json::object());
  Json proposals_status = Json::object();
  if (status.is_object() && !status.empty())
    proposals_status = Json{{"state", json::get_string(status, "state")}, {"today", pick(status.value("today", Json()), e.lang)}, {"reason", pick(status.value("reason", Json()), e.lang)}};
  return Json{{"schema", "loom.settings_resolved/1"},
              {"conv_id", e.conv_id.empty() ? Json(nullptr) : Json(e.conv_id)},
              {"groups", groups},
              {"hidden_keys", hidden_keys},
              {"profile_values", profile_values},
              {"proposals_status", proposals_status},
              {"counts", Json{{"fields", field_count}, {"hidden", hidden_keys.size()}, {"profile_values", profile_values.size()}}},
              {"resolver_opens", cat.resolver_opens()}};
}

Result<Json> roles_resolved(const Env& env, const Json& request) {
  Env e = env;
  if (const std::string conv = json::get_string(request, "conv_id"); !conv.empty()) e.conv_id = conv;
  LOOM_TRY_ASSIGN(auto cat, Catalogue::open(e));
  LOOM_TRY_ASSIGN(auto rows, cat.roles());
  return Json{{"schema", "loom.roles_resolved/1"},
              {"conv_id", e.conv_id.empty() ? Json(nullptr) : Json(e.conv_id)},
              {"roles", rows.value("roles", Json::array())},
              {"resolver_opens", cat.resolver_opens()}};
}

Result<Json> rom_override(const std::filesystem::path& root, const Json& request) {
  std::string id = json::get_string(request, "id", json::get_string(request, "address"));
  if (starts_with(id, rom::kAddressPrefix)) id = id.substr(rom::kAddressPrefix.size());
  if (id.empty()) return Error(Errc::InvalidArgument, "rom.override needs the id of a ROM item");
  Json patch = request.value("patch", Json(nullptr));
  if (patch.is_string()) {
    LOOM_TRY_ASSIGN(patch, json::parse(patch.get<std::string>()));
  }
  if (!patch.is_object() || patch.empty()) return Error(Errc::InvalidArgument, "rom.override needs a patch object (the fields of the item your copy changes)");
  const auto mounts = rom::MountRegistry::standard(root);
  LOOM_TRY(mounts.write("user", std::string(rom::kAddressPrefix) + id, patch));
  LOOM_TRY_ASSIGN(auto layered, mounts.read(std::string(rom::kAddressPrefix) + id));
  return Json{{"schema", "loom.rom_override/1"}, {"written", true}, {"address", std::string(rom::kAddressPrefix) + id},
              {"file", (root / "profiles" / "graph_entry.pack").string()}, {"pointer", "/overrides/rom/items/" + id}, {"patch", patch},
              {"source", layered.value("source", "")}, {"item", layered.value("item", Json::object())}};
}

Result<Json> render_catalogue(const Env& env, const std::set<std::string>& kinds, const Json& request) {
  LOOM_TRY_ASSIGN(auto cat, Catalogue::open(env));
  LOOM_TRY_ASSIGN(auto all, cat.edges());
  const std::string direction = json::get_string(request, "direction", "both");
  const Json hops_j = request.value("hops", Json(nullptr));
  const bool bounded = hops_j.is_number_integer();
  const std::int64_t hops = bounded ? hops_j.get<std::int64_t>() : 0;
  std::vector<std::string> seeds;
  for (const auto& s : strings_in(request.value("seeds", Json::array()))) seeds.push_back(cat.canonical(s));
  std::vector<Json> chosen;
  for (const auto& e : all)
    if (kinds.count(json::get_string(e, "kind"))) chosen.push_back(e);
  std::set<std::string> reached(seeds.begin(), seeds.end());
  std::vector<const Json*> kept;
  if (seeds.empty()) {
    for (const auto& e : chosen) {
      reached.insert(json::get_string(e, "src"));
      reached.insert(json::get_string(e, "dst"));
      kept.push_back(&e);
    }
  } else {
    std::deque<std::pair<std::string, std::int64_t>> todo;
    for (const auto& s : seeds) todo.push_back({s, 0});
    std::set<std::string> seen(seeds.begin(), seeds.end());
    std::set<const Json*> in_kept;
    while (!todo.empty()) {
      auto [cur, depth] = todo.front();
      todo.pop_front();
      if (bounded && depth >= hops) continue;
      for (const auto& e : chosen) {
        const std::string s = json::get_string(e, "src"), d = json::get_string(e, "dst");
        const bool out_edge = s == cur && (direction == "out" || direction == "both");
        const bool in_edge = d == cur && (direction == "in" || direction == "both");
        if (!out_edge && !in_edge) continue;
        if (in_kept.insert(&e).second) kept.push_back(&e);
        const std::string next = out_edge ? d : s;
        reached.insert(next);
        if (seen.insert(next).second) todo.push_back({next, depth + 1});
      }
    }
  }
  const std::int64_t limit = request.contains("limit") && request["limit"].is_number_integer() ? request["limit"].get<std::int64_t>() : -1;
  Json nodes = Json::array(), edges = Json::array();
  std::set<std::string> shown;
  bool truncated = false;
  for (const auto& a : reached) {
    LOOM_TRY_ASSIGN(auto n, cat.node(a));
    if (!n) continue;
    if (limit >= 0 && static_cast<std::int64_t>(nodes.size()) >= limit) {
      truncated = true;
      break;
    }
    nodes.push_back(Json{{"id", a}, {"address", a}, {"kind", n->value("kind", "")}, {"label", n->value("label", "")}, {"evidence", n->value("evidence", Json::object())}});
    shown.insert(a);
  }
  for (const auto* e : kept) {
    const std::string s = json::get_string(*e, "src"), d = json::get_string(*e, "dst"), k = json::get_string(*e, "kind");
    if (!shown.count(s) || !shown.count(d)) continue;
    edges.push_back(Json{{"src", s}, {"dst", d}, {"type", k}, {"kind", k}, {"meaning", cat.edge_meaning(k)}});
  }
  Json ks = Json::array();
  for (const auto& k : kinds) ks.push_back(k);
  Json out{{"schema", "loom.perspective_render/1"}, {"source", "catalogue"}, {"edge_kinds", ks}, {"direction", direction}, {"hops", hops_j}, {"nodes", nodes}, {"edges", edges}};
  if (truncated) out["truncated"] = true;
  return out;
}

bool is_projection_action(std::string_view action) {
  return action == "graph.navigate" || action == "graph.card" || action == "settings.resolved" || action == "roles.resolved" || action == "rom.override";
}

Env env_of(Runtime& rt, const Json& request) {
  Env e;
  e.root = rt.paths().root;
  e.cfg = &rt.config();
  e.db = &rt.db();
  e.memory = &rt.memory();
  e.secrets = &rt.secrets();
  e.models = &rt.models();
  e.lang = json::get_string(request, "lang", "en");
  e.conv_id = json::get_string(request, "conv_id", json::get_string(request, "conversation_id"));
  e.user_id = json::get_string(request, "user_id");
  return e;
}

Result<Json> invoke(Runtime& rt, std::string_view action, const Json& request) {
  const Env e = env_of(rt, request);
  if (action == "graph.navigate") return navigate(e, request);
  if (action == "graph.card") return card(e, request);
  if (action == "settings.resolved") return settings_resolved(e, request);
  if (action == "roles.resolved") return roles_resolved(e, request);
  if (action == "rom.override") return rom_override(e.root, request);
  return Error(Errc::NotFound, "the graph projection has no action '" + std::string(action) + "'");
}

}  // namespace loom::projection
