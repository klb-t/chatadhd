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
#include "loom/util/unicode.h"
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

// The value of a setting node, resolved when it leaves the catalogue (defined with the settings reader).
void resolve_setting_node(Catalogue::Impl& c, Json& n);
// The nodes of the parts of You (M1-K2c-2): which part of party:self a node of a source hangs under, and the actions it offers (profile.subgraphs).
void regroup_self(Catalogue::Impl& c, const Json& source, Read& r);

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
  std::vector<Json> looked_up_edges;   // edges of the nodes of paged sources read one by one (node())

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
  std::map<std::string, Json> settings_cache;           // setting(): key -> value for this render
  std::map<std::string, std::pair<const Json*, bool>> field_index;  // settings key -> (its entry in fields(), a profile value?)
  std::optional<std::set<std::string>> unit_levels_cache;  // unit_levels()
  std::optional<views::EdgeKinds> kinds;
  bool kinds_tried = false;
  std::optional<Json> graph_entry_values;
  std::optional<Json> profile_snapshot;
  std::string profile_error;
  bool profile_tried = false;
  std::optional<onboarding::DefaultLayers> layers_cache;
  std::string layers_error;
  std::optional<Json> screen_cache;
  std::map<std::string, Json> receipt_cache;                 // receipt_of(): request or answer id -> the render of its record (M1-K1c3a)
  std::optional<std::set<std::string>> receipt_kinds_cache;  // the edge kinds of the perspective sent_to_model
  std::string profile_user;                  // the stored onboarding profile that profile() read ("" = none)
  std::optional<Json> self_table_cache;      // subgraph_table()
  std::string self_table_layer;              // the layer that supplied it: builtin | user
  std::optional<Json> slot_plan_cache;       // slot_plan()
  std::optional<Json> level_labels_cache;    // level_label(): detail level id -> label {pl, en, ...}
  std::optional<Json> action_table_cache;    // the actions of the class application: action id -> {label, decision, implementation}

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
          if (snap) {
            profile_snapshot = std::move(*snap);
            profile_user = user;
          }
          else if (snap.error().code != Errc::NotFound) profile_error = "onboarding_unavailable";
        }
      }
      if (!profile_error.empty()) log::write(log::Level::Debug, kLog, "onboarding profile not shown: " + profile_error);
    }
    if (!profile_error.empty()) return Error(Errc::Unavailable, profile_error);
    return profile_snapshot ? &*profile_snapshot : nullptr;
  }

  // The shipped onboarding layers: embedded, so parsed once per process (a failure is deterministic and logged).
  static const Result<onboarding::DefaultLayers>& shipped_layers() {
    static const Result<onboarding::DefaultLayers> shipped = []() -> Result<onboarding::DefaultLayers> {
      auto pack = onboarding::builtin_pack();
      if (!pack) {
        log::write(log::Level::Debug, kLog, "shipped onboarding pack not readable: " + pack.error().message);
        return pack.error();
      }
      return onboarding::DefaultLayers::create(*pack);
    }();
    return shipped;
  }

  // The onboarding layers (the stored profile's pack, else the shipped one): preferences, privacy rules and the words of the screen.
  Result<const onboarding::DefaultLayers*> layers() {
    if (!layers_cache && layers_error.empty()) {
      auto snap = profile();
      if (snap && *snap && (*snap)->contains("pack") && (*snap)->contains("layers")) {
        auto made = onboarding::DefaultLayers::create((**snap)["pack"], (**snap)["layers"]);
        if (made) layers_cache = std::move(*made);
        else layers_error = made.error().message;
      } else if (const auto& shipped = shipped_layers(); shipped) {
        return &*shipped;
      } else {
        layers_error = shipped.error().message;
      }
      if (!layers_error.empty()) log::write(log::Level::Debug, kLog, "onboarding layers not readable: " + layers_error);
    }
    if (!layers_cache) return Error(Errc::Unavailable, layers_error.empty() ? std::string("onboarding_unavailable") : layers_error);
    return &*layers_cache;
  }

  // The words of the graph screen (presentation.graph_screen) in the language of the render, English for a word that language lacks;
  // a stored profile older than these words reads the shipped ones. Read once per render.
  const Json& screen_words() {
    if (screen_cache) return *screen_cache;
    screen_cache = Json::object();
    Json locales = nullptr;
    if (auto l = layers(); l)
      if (auto r = (*l)->resolve("presentation.graph_screen"); r && json::get_string(*r, "status") == "effective") locales = r->value("value", Json::object()).value("locales", Json());
    if (!locales.is_object()) {
      const auto& shipped = shipped_layers();
      if (auto r = shipped ? shipped->resolve("presentation.graph_screen") : Result<Json>(shipped.error()); r)
        locales = r->value("value", Json::object()).value("locales", Json());
      else log::write(log::Level::Debug, kLog, "graph screen words not readable: " + r.error().message);
    }
    if (locales.is_object()) {
      for (const std::string& l : {std::string("en"), lang()})
        if (const Json* words_here = json::find(locales, l); words_here && words_here->is_object())
          for (const auto& w : words_here->items()) (*screen_cache)[w.key()] = w.value();
    }
    return *screen_cache;
  }

  // One word of the screen ("" when the screen has none), with its {{variables}}.
  std::string word(const std::string& id, const Json& vars = Json::object()) {
    const Json* w = json::find(screen_words(), id);
    if (!w || !w->is_string()) {
      // The words of the graph sources (value_words) are the fallback: a word the screen's pack does not have yet.
      const Json* own = json::find(words, id);
      return own && own->is_object() ? say(*own) : std::string();
    }
    if (vars.empty()) return w->get<std::string>();
    auto text = render_profile_template(w->get<std::string>(), vars);
    return text ? *text : w->get<std::string>();
  }

  // Why something is unknown, in words: reason.<code> (the code before ": detail"); the code itself when no word says it.
  std::string reason_words(const std::string& reason) {
    const std::string code = reason.substr(0, reason.find(':'));
    const std::string w = word("reason." + code);
    return w.empty() ? reason : w;
  }

  // The evidence in words: what its method says (evidence.method.<method>: "you said it", "a model's reply", "imported (Claude.ai, 2025-03)"),
  // else what its class says (evidence.<class>; an inference with its confidence). Inference is never worded as something you said.
  std::string evidence_words(const Json& ev) {
    const Json prov = ev.value("provenance", Json::object());
    const std::string method = json::get_string(prov, "method");
    const std::string cls = json::get_string(ev, "class");
    if (cls == "inferred") {
      const Json conf = ev.value("confidence", Json(nullptr));
      if (method != "model_inferred" && !method.empty())
        if (const std::string w = word("evidence.method." + method); !w.empty()) return w;
      return conf.is_number() ? word("evidence.inferred", Json{{"confidence", json::format_float_py(conf.get<double>())}}) : word("evidence.inferred_unknown");
    }
    if (starts_with(method, "import:")) {
      const std::string time = json::get_string(prov, "time");
      const std::string w = word("evidence.method.import", Json{{"source", method.substr(7)}, {"date", time.substr(0, std::min<std::size_t>(7, time.size()))}});
      if (!w.empty()) return w;
    }
    if (!method.empty())
      if (const std::string w = word("evidence.method." + method); !w.empty()) return w;
    return word("evidence." + cls);
  }

  // A node as it leaves the catalogue: its evidence (and each property's) in words.
  void finish(Json& n) {
    if (json::get_bool(n, "value_pending", false)) resolve_setting_node(*this, n);
    // A node that already says its evidence in words (a derived node of the record of a request, receipt_node) keeps its own words.
    if (Json* ev = n.contains("evidence") ? &n["evidence"] : nullptr; ev && ev->is_object() && !ev->contains("human")) (*ev)["human"] = evidence_words(*ev);
    if (n.contains("evidence_overrides") && n["evidence_overrides"].is_object())
      for (auto& item : n["evidence_overrides"].items())
        if (item.value().is_object() && !item.value().contains("human")) item.value()["human"] = evidence_words(item.value());
  }

  // One area of the entry profile as a node (label, why, order, expand; no counters): nullopt for an area the profile does not name (a
  // generated cell of the axes, which only the whole entry view knows).
  std::optional<Json> area_alone(const Json& src, const std::string& address) {
    const std::string id = address.substr(5);
    const Json areas = graph_entry().value("areas", Json::object());
    const Json* own = json::find(areas, id);
    if (!own || !own->is_object() || json::get_bool(*own, "hidden", false)) return std::nullopt;
    auto shipped = RuntimeProfile::builtin("graph_entry");
    const Json shipped_area = shipped ? shipped->values().value("areas", Json::object()).value(id, Json()) : Json();
    const std::string layer = *own == shipped_area ? std::string("rom") : std::string("overlay:graph_entry");
    Json n = make(src, address, say(own->value("label", Json(id))), std::string(), layer, "graph/entry.pack#/areas/" + id, layer == "rom" ? "preset" : "override");
    n["order"] = own->value("order", 1000);
    if (const Json* why = json::find(*own, "why"); why) n["why"] = say(*why);
    n["attrs"] = Json{{"axes", own->value("axes", Json::object())}, {"origin", "area"}, {"expand", own->value("expand", Json(nullptr))}, {"counts_with_the_root", true}};
    return n;
  }

  // ── nodes ──────────────────────────────────────────────────────────

  Json make(const Json& src, const std::string& address, const std::string& label, const std::string& parent, const std::string& layer,
            const std::string& source_ref, const std::string& method, const Json& time = nullptr) {
    Json layers = Json::array();
    layers.push_back(layer);
    // What your overlay defines or changes is yours (observed, defined_by_you / override), never the ROM's preset.
    const bool yours = starts_with(layer, "overlay:");
    const Json cls = yours ? Json{{"class", "observed"}, {"confidence", 1.0}} : src.value("epistemic_default", Json::object());
    const std::string how = yours && method == "preset" ? std::string("override") : method;
    Json n{{"address", address},
                {"label", label},
                {"kind", json::get_string(src, "class")},
                {"source", json::get_string(src, "id")},
                {"parent", parent},
                {"evidence", evidence_of(cls, layer, source_ref, how, time)},
                {"provenance", Json{{"layer", layer}, {"layers", layers}}},
                {"has_children", false},
                {"attrs", Json::object()}};
    if (const Json* w = json::find(src, "node_why"); w && w->is_object()) n["why"] = say(*w);
    return n;
  }

  // The parent of a source's nodes: {area: <id>} -> area:<id>, {node: <address>} -> that node ({of: <source>} is set per node by its reader).
  std::string parent_of(const Json& src) const {
    const Json p = src.value("parent", Json::object());
    if (const std::string area = json::get_string(p, "area"); !area.empty()) return "area:" + area;
    if (const std::string node = json::get_string(p, "node"); !node.empty()) return node;
    return std::string();
  }

  // The node at an address, from the source that knows it (a paged source reads only that node); nullopt when none does.
  Result<std::optional<Json>> node_at(const std::string& address) {
    const Json* src = nullptr;
    std::size_t best_len = 0;
    for (const auto& s : sources) {
      const std::string p = json::get_string(s, "prefix");
      if (!p.empty() && p.size() > best_len && starts_with(address, p) && address.size() > p.size()) {
        src = &s;
        best_len = p.size();
      }
    }
    if (!src) return std::optional<Json>();
    // An area looked up alone (a breadcrumb, a focus) is read from the entry profile, without computing every counter of the entry view;
    // the counters come with the root's listing.
    if (json::get_string(*src, "reader") == "graph_entry.areas" && !eager.count(json::get_string(*src, "id")))
      if (auto alone = area_alone(*src, address); alone) {
        finish(*alone);
        return alone;
      }
    if (paged(*src)) {
      LOOM_TRY_ASSIGN(auto r, read_under(*src, std::string(), Json{{"address", address}}));
      for (auto& e : r.edges) looked_up_edges.push_back(std::move(e));
      for (auto& n : r.nodes)
        if (json::get_string(n, "address") == address) {
          finish(n);
          return std::optional<Json>(std::move(n));
        }
      return std::optional<Json>();
    }
    LOOM_TRY_ASSIGN(const Read* r, read_whole(*src));
    for (const auto& n : r->nodes)
      if (json::get_string(n, "address") == address) {
        Json one = n;
        finish(one);
        return std::optional<Json>(std::move(one));
      }
    return std::optional<Json>();
  }

  // A number of bytes in words (64 GB): the units of value_words.byte_units, 1024 apart.
  std::string bytes_human(std::int64_t n) const {
    const Json units = words.value("byte_units", Json::array({"B", "KB", "MB", "GB", "TB", "PB"}));
    double v = static_cast<double>(n);
    std::size_t u = 0;
    while (v >= 1024.0 && u + 1 < units.size()) {
      v /= 1024.0;
      ++u;
    }
    std::string num;
    if (v == static_cast<double>(static_cast<std::int64_t>(v))) {
      num = std::to_string(static_cast<std::int64_t>(v));
    } else {
      num = json::format_float_py(static_cast<double>(static_cast<std::int64_t>(v * 10.0 + 0.5)) / 10.0);
      const std::string sep = say(words.value("decimal_separator", Json(".")));
      if (!sep.empty() && sep != ".")
        if (auto dot = num.find('.'); dot != std::string::npos) num.replace(dot, 1, sep);
    }
    const std::string unit = u < units.size() && units[u].is_string() ? units[u].get<std::string>() : std::string();
    return unit.empty() ? num : num + " " + unit;
  }

  std::string human_value(const Json& v, const Json& field) const {
    for (const auto& e : field.value("values", Json::array()))
      if (e.is_object() && e.contains("value") && e["value"] == v && e.contains("meaning")) {
        const std::string m = say(e["meaning"]);
        if (!m.empty()) return m;
      }
    const Json w = words;
    const Json human = field.value("human", Json::object());
    // An object said by one of its members (human.summary: the ×10 of the usage fuse).
    if (const Json* summary = json::find(human, "summary"); summary && summary->is_object() && v.is_object())
      if (const Json* member = json::find_pointer(v, json::get_string(*summary, "pointer")); member) {
        auto text = render_profile_template(say(summary->value("text", Json())), Json{{"v", human_value(*member, Json::object())}});
        if (text && !text->empty()) return *text;
      }
    if (json::get_string(field, "unit") == "bytes" && (v.is_number_integer() || v.is_number_unsigned())) return bytes_human(v.get<std::int64_t>());
    if (v.is_null()) return say(w.value("null", Json("not set")));
    if (v.is_boolean()) return say(w.value(v.get<bool>() ? "true" : "false", Json(v.get<bool>() ? "on" : "off")));
    if (v.is_string()) return v.get<std::string>().empty() ? say(w.value("empty", Json("empty"))) : v.get<std::string>();
    if (v.is_number_integer() || v.is_number_unsigned()) return json::dump(v);
    if (v.is_number_float()) {
      std::string s = json::format_float_py(v.get<double>());
      if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
      const std::string sep = say(w.value("decimal_separator", Json(".")));
      if (!sep.empty() && sep != ".")
        if (auto dot = s.find('.'); dot != std::string::npos) s.replace(dot, 1, sep);
      return s;
    }
    if (v.empty()) return say(w.value("empty", Json("empty")));
    auto text = render_profile_template(say(w.value("items", Json("{{n}} items"))), Json{{"n", v.size()}});
    return text ? *text : std::to_string(v.size());
  }

  // The chip of the level that supplied a value: {id, label}, the label the screen's word (chip.<id>), else the chain's own label.
  Json chip_of(const std::string& scope, const Json& chain) {
    const std::string id = scope.empty() ? std::string("none") : scope;
    std::string label = word("chip." + id);
    if (label.empty())
      for (const auto& l : chain)
        if (json::get_string(l, "level") == scope) label = say(l.value("label", Json(scope)));
    return Json{{"id", id}, {"label", label.empty() ? id : label}};
  }

  // No level gives a value: what the reader does then (human.when_unset, the preset in code), else "not set anywhere".
  void unset_words(Json& out, const Json& field, const std::string& key) {
    const std::string when = say(field.value("human", Json::object()).value("when_unset", Json()));
    if (when.empty()) {
      out["provenance"]["chip"] = Json{{"id", "none"}, {"label", word("chip.none").empty() ? std::string("none") : word("chip.none")}};
      return;
    }
    out["value_human"] = when;
    out["provenance"]["chip"] = Json{{"id", "code_preset"}, {"label", word("chip.code_preset").empty() ? std::string("code_preset") : word("chip.code_preset")}};
    out["provenance"]["when_unset"] = true;
    out["evidence"] = evidence_of(Json{{"class", "declared"}, {"confidence", 1.0}}, "code", key, "code_preset");
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
      out["provenance"] = Json{{"scope", set ? "person" : "none"}, {"layer", "secrets.json"}, {"chip", Json{{"id", "secret"}, {"label", word("chip.secret").empty() ? std::string("secret") : word("chip.secret")}}}};
      out["inheritance"] = Json::array();
      out["evidence"] = evidence_of(set ? Json{{"class", "observed"}, {"confidence", 1.0}} : src.value("epistemic_default", Json::object()),
                                    "secrets.json", key, set ? "setting" : "preset");
      return out;
    }
    // The preset: the config preset (default_value), else the runtime profile the reader takes it from (preset_from -> preset_value).
    const Json preset = field.contains("default_value") ? field["default_value"] : field.value("preset_value", Json(nullptr));
    const std::string preset_source = field.contains("default_value") ? std::string("preset:settings") : json::get_string(field, "preset_source", "preset:settings");
    const scopes::Resolver* r = get_resolver();
    scopes::Binding b;
    b.key = key;
    b.conv_id = env.conv_id;
    if (!preset.is_null()) {
      b.installation = preset;
      b.installation_source = preset_source;
    }
    // A value of the person that lives in another layer than config.json (a preference of the profile's user layer): it is the `person` level.
    const bool person_elsewhere = field.contains("person_value") && !field["person_value"].is_null();
    if (person_elsewhere) {
      b.person_value = field["person_value"];
      b.person_source = json::get_string(field, "person_source", "profile:" + key);
    }
    const std::string person_layer = json::get_string(field, "person_layer", "config.json");
    std::optional<scopes::Resolution> res;
    if (r) {
      auto got = r->resolve(b);
      if (got) res = std::move(*got);
      else log::write(log::Level::Debug, kLog, "'" + key + "' not resolved through the scope chain: " + got.error().message);
    }
    const std::string preset_layer = starts_with(preset_source, "profile:") ? preset_source : std::string("rom");
    if (!res) {
      // No chain: what the settings say, else the preset - and the provenance says the chain could not be read.
      const Json mine = person_elsewhere ? field["person_value"] : env.cfg ? env.cfg->get(key) : Json(nullptr);
      const bool set = !mine.is_null();
      out["value"] = set ? mine : preset;
      out["value_human"] = human_value(out["value"], field);
      const std::string scope = set ? "person" : preset.is_null() ? "none" : "installation";
      out["provenance"] = Json{{"scope", scope}, {"layer", set ? person_layer : preset.is_null() ? "none" : preset_layer}, {"chip", chip_of(scope == "none" ? std::string() : scope, Json::array())},
                               {"chain_unavailable", resolver_error.empty() ? std::string("scope_chain_unavailable") : resolver_error}};
      out["inheritance"] = Json::array();
      out["evidence"] = evidence_of(set ? Json{{"class", "observed"}, {"confidence", 1.0}} : src.value("epistemic_default", Json::object()),
                                    set ? person_layer : preset_layer, key, set ? "setting" : "preset");
      if (!set && preset.is_null()) unset_words(out, field, key);
      return out;
    }
    out["value"] = res->defined ? res->value : Json(nullptr);
    out["value_human"] = human_value(out["value"], field);
    std::string layer = preset_layer;
    if (res->scope == "person") layer = person_layer;
    else if (res->scope == "conversation") layer = "conversation.metadata";
    else if (starts_with(res->source, "rule:")) layer = "rules:" + res->source.substr(5);
    else if (res->scope != "installation" && !res->scope.empty()) layer = "unit:" + res->scope;
    if (!res->defined) layer = "none";
    Json prov{{"scope", res->defined ? res->scope : std::string("none")}, {"layer", layer}, {"source", res->source}, {"chip", chip_of(res->defined ? res->scope : std::string(), res->chain)}};
    if (!res->scope_id.empty()) prov["scope_id"] = res->scope_id;
    // config.json holds the presets too (the auto-upgrade writes them): a person value equal to the preset may be the preset itself, so it is
    // said (chip: the preset kept in your settings), its evidence stays the preset's (never "you said"), and it is not counted as yours.
    const bool preset_copy = res->defined && res->scope == "person" && !person_elsewhere && !preset.is_null() && preset == res->value;
    if (preset_copy) {
      prov["same_as_preset"] = true;
      if (const std::string w = word("chip.same_as_preset"); !w.empty()) prov["chip"]["label"] = w;
      prov["chip"]["same_as_preset"] = true;
    }
    out["provenance"] = prov;
    out["inheritance"] = inheritance_of(*res);
    const Json by_scope = src.value("evidence_by_scope", Json::object());
    Json cls = src.value("epistemic_default", Json::object());
    if (const Json* e = json::find(by_scope, res->scope); e && e->is_object()) cls = *e;
    else if (const Json* any = json::find(by_scope, "*"); any && any->is_object() && res->scope != "installation" && res->defined) cls = *any;
    if (preset_copy) cls = src.value("epistemic_default", Json::object());
    out["evidence"] = evidence_of(cls, layer, key, res->scope == "installation" ? "preset" : preset_copy ? "preset_in_config" : "setting");
    if (!res->defined) unset_words(out, field, key);
    return out;
  }

  // The whole chain, general first: [{level, label, role, active, scope_id?, value?, unit_level?, no_unit?}]. A unit level of a chain that
  // has no unit of that kind says so (no_unit: the conversation is "bez projektu"), never left out.
  Json inheritance_of(const scopes::Resolution& res) {
    const std::set<std::string>& levels = unit_levels();
    Json out = Json::array();
    for (const auto& l : res.chain) {
      const std::string id = json::get_string(l, "level");
      Json row{{"level", id}, {"label", say(l.value("label", Json(id)))}, {"role", json::get_string(l, "role")}, {"active", l.value("active", false)}};
      // M1-W0 r2:b4: the level's one-line meaning in the reader's language (graph/scopes.pack chain[].meaning); a level without one has no key.
      if (const Json* m = json::find(l, "meaning"); m) if (const std::string s = say(*m); !s.empty()) row["meaning"] = s;
      if (l.contains("scope_id")) row["scope_id"] = l["scope_id"];
      for (const auto& c : l.value("candidates", Json::array()))
        if (c.contains("value") && (json::get_string(c, "role") == "decided" || json::get_string(c, "role") == "overridden")) {
          row["value"] = c["value"];
          break;
        }
      if (levels.count(id)) {
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
    if (r.unknown.empty()) regroup_self(*this, src, r);
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

  // A setting's value through the scope chain of this render (a rule, the conversation or its units may set it), else the person level.
  Json setting(const std::string& key) {
    if (auto it = settings_cache.find(key); it != settings_cache.end()) return it->second;
    Json v = nullptr;
    if (const scopes::Resolver* r = get_resolver()) {
      scopes::Binding b;
      b.key = key;
      b.conv_id = env.conv_id;
      if (auto got = r->resolve(b); !got) log::write(log::Level::Debug, kLog, "'" + key + "' not resolved through the scope chain: " + got.error().message);
      else if (got->defined) v = got->value;
    } else if (env.cfg) {
      v = env.cfg->get(key);
    }
    settings_cache[key] = v;
    return v;
  }

  // The levels of the scope chain that are unit kinds (views unit_kinds), read once per render.
  const std::set<std::string>& unit_levels() {
    if (!unit_levels_cache) {
      unit_levels_cache.emplace();
      if (auto uk = views::effective(env.root, "unit_kinds"))
        for (const auto& e : uk->entries) unit_levels_cache->insert(json::get_string(e.value, "level", e.id));
      else log::write(log::Level::Debug, kLog, "unit kinds not readable: " + uk.error().message);
    }
    return *unit_levels_cache;
  }

  // ── the parts of You (profiles/user.pack profile.subgraphs, M1-K2c-2) ─────────────────────────────

  static constexpr const char* kSelf = "party:self";
  static constexpr const char* kPartPrefix = "subgraph:self/";

  // The table as the onboarding layers resolve it (your table replaces the shipped one). A stored profile older than `members` (its pack has
  // none) reads the shipped table, so a profile made before this change still shows its parts.
  const Json& subgraph_table() {
    if (self_table_cache) return *self_table_cache;
    self_table_cache = Json::array();
    self_table_layer = "builtin";
    const auto take = [&](const onboarding::DefaultLayers& layers) {
      auto r = layers.resolve("profile.subgraphs");
      if (!r || json::get_string(*r, "status") != "effective") return false;
      const Json table = r->value("value", Json::object()).value("subgraphs", Json());
      bool has_members = false;
      if (table.is_array())
        for (const auto& e : table) has_members = has_members || (e.is_object() && e.contains("members"));
      if (!has_members) return false;
      self_table_cache = table;
      self_table_layer = json::get_string(*r, "layer", "builtin");
      return true;
    };
    bool got = false;
    if (auto l = layers(); l) got = take(**l);
    if (!got)
      if (const auto& shipped = shipped_layers(); shipped) got = take(*shipped);
    if (!got) log::write(log::Level::Debug, kLog, "the parts of You (profile.subgraphs) are not readable: You has no parts");
    return *self_table_cache;
  }

  // True when the part at `parent` (subgraph:self/<id>) takes its members from the source: the sources to read for its children.
  bool member_reads(const std::string& parent, const std::string& source_id) {
    if (!starts_with(parent, kPartPrefix)) return false;
    const std::string id = parent.substr(std::string(kPartPrefix).size());
    for (const auto& sg : subgraph_table())
      if (json::get_string(sg, "id") == id)
        for (const auto& m : sg.value("members", Json::array()))
          if (json::get_string(m, "source") == source_id) return true;
    return false;
  }

  // The label of a detail level of the interview scenario in the language asked, by its id (what the person chose: "Minimalnie", not "minimal");
  // "" when the scenario names no such level, so the caller keeps the id.
  std::string level_label(const std::string& id) {
    if (!level_labels_cache) {
      level_labels_cache = Json::object();
      auto scenario = onboarding::builtin_scenario();
      if (!scenario) log::write(log::Level::Debug, kLog, "detail level labels: the scenario is not readable: " + scenario.error().message);
      else
        for (const auto& l : scenario->value("detail_levels", Json::array()))
          if (l.is_object() && l.contains("label")) (*level_labels_cache)[json::get_string(l, "id")] = l["label"];
    }
    const Json* l = json::find(*level_labels_cache, id);
    return l ? say(*l) : std::string();
  }

  // Which slots the interview asks at the chosen level and why it will not ask the others (the same plan profile.show answers from):
  // {level, asked[field], skipped{field: {reason, reason_text}}} or {unknown_reason}.
  const Json& slot_plan() {
    if (slot_plan_cache) return *slot_plan_cache;
    slot_plan_cache = Json::object();
    Json& out = *slot_plan_cache;
    auto snap = profile();
    if (!snap || !*snap || !env.db || profile_user.empty()) {
      out["unknown_reason"] = "not_interviewed";
      return out;
    }
    const Json level = setting("profile_detail_level");
    if (!level.is_string() || level.get<std::string>().empty()) {
      out["unknown_reason"] = "no_level_chosen";
      return out;
    }
    Json request{{"what", "plan"}, {"level", level}, {"locale", lang()}};
    if (const Json by = setting("profile_detail_by_category"); by.is_object() && !by.empty()) request["by_category"] = by;
    auto view = onboarding::OnboardingStore(*env.db).interview_view(profile_user, request);
    if (!view || !view->contains("view")) {
      out["unknown_reason"] = view ? std::string("plan_unreadable") : view.error().message;
      return out;
    }
    const Json plan = view->at("view");
    out["level"] = level;
    out["asked"] = Json::array();
    for (const auto& a : plan.value("asks", Json::array())) out["asked"].push_back(json::get_string(a, "field"));
    out["skipped"] = Json::object();
    for (const auto& s : plan.value("skipped", Json::array())) {
      const std::string raw = json::get_string(s, "reason");
      const bool above = raw == "detail_above_level" || raw == "sensitivity_above_level";
      out["skipped"][json::get_string(s, "field")] = Json{{"reason", above ? std::string("above_level") : raw}, {"reason_detail", raw}, {"reason_text", say(s.value("reason_text", Json()))}};
    }
    return out;
  }

  std::int64_t page_limit(const Json& src, const Json& paging) {
    if (paging.contains("page_limit") && paging["page_limit"].is_number_integer()) return paging["page_limit"].get<std::int64_t>();
    std::int64_t limit = 0;  // 0 = no limit (the setting's own meaning)
    if (const std::string key = json::get_string(src, "page_setting"); !key.empty())
      if (const Json v = setting(key); v.is_number_integer()) limit = v.get<std::int64_t>();
    // A level shows at most the navigator's budget: a page is never read further than what can be shown (the total still counts all).
    const std::int64_t cap = json::get_int(paging, "page_cap", 0);
    if (cap > 0 && (limit <= 0 || cap < limit)) limit = cap;
    return limit;
  }
};

// ── readers (mechanism ids of runtime/graph_sources.pack) ────────────────────────────

namespace {

using Impl = Catalogue::Impl;

std::string layer_vs_builtin(const Json& effective, const Json& builtin, const std::string& overlay) {
  return effective == builtin ? std::string("rom") : overlay;
}

// The model roles, each resolved once per render (the policy chain through the render's Resolver and ports profile).
const Json& role_rows(Impl& c);

// A count of the entry view in words: its name (count.name.<counter>) and, when unknown, why (reason words).
void count_words(Impl& c, Json& counts) {
  if (!counts.is_object()) return;
  for (auto& item : counts.items()) {
    Json& v = item.value();
    if (!v.is_object()) continue;
    if (const std::string name = c.word("count.name." + item.key()); !name.empty()) v["name"] = name;
    if (!json::get_bool(v, "known", true))
      if (const std::string why = json::get_string(v, "reason"); !why.empty()) v["reason_human"] = c.reason_words(why);
  }
}

Result<Read> read_areas(Impl& c, const Json& src, const std::string&, const Json&) {
  graph_entry::Env ge;
  ge.db = c.env.db;
  ge.cfg = c.env.cfg;
  ge.data_dir = c.env.root;
  // The counter of the roles says what the role rows of this render say (one resolution, one Resolver).
  ge.roles = [&c]() -> Result<Json> {
    const Json& rows = role_rows(c);
    if (rows.contains("unknown")) return Error(Errc::Unavailable, json::get_string(rows, "unknown"));
    return rows;
  };
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
    // A generated cell of the axes (an area no entry names) says so in its label; it stays in the list with its counts.
    std::string label = json::get_string(a, "label");
    if (json::get_string(a, "origin") == "cell")
      if (const std::string w = c.word("area.cell", Json{{"label", label}}); !w.empty()) label = w;
    Json n = c.make(src, "area:" + id, label, std::string(), layer, "graph/entry.pack#/areas/" + id, layer == "rom" ? "preset" : "override");
    n["order"] = a.value("order", 1000);
    if (const Json* why = json::find(own, "why"); why) n["why"] = c.say(*why);
    Json counts = a.value("counts", Json::object());
    count_words(c, counts);
    n["attrs"] = Json{{"counts", counts}, {"axes", a.value("axes", Json::object())}, {"origin", a.value("origin", "area")},
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
    out["provenance"] = Json{{"scope", "none"}, {"layer", "none"}, {"chip", Json{{"id", "none"}, {"label", c.word("chip.none").empty() ? std::string("none") : c.word("chip.none")}}}};
    out["evidence"] = evidence_of(src.value("epistemic_default", Json::object()), "none", profile + pointer, "profile_value");
    out["inheritance"] = Json::array();
    return out;
  }
  out["value"] = p.value("value", Json(nullptr));
  out["value_human"] = c.human_value(out["value"], p);
  auto shipped = RuntimeProfile::builtin(profile);
  if (!shipped) log::write(log::Level::Debug, kLog, "shipped profile " + profile + " not readable: " + shipped.error().message);
  const Json* builtin_value = shipped ? json::find_pointer(shipped->values(), pointer) : nullptr;
  const bool yours = !builtin_value || *builtin_value != out["value"];
  const std::string layer = yours ? "overlay:profiles/" + profile + ".pack" : std::string("rom");
  const std::string scope = yours ? "person" : "installation";
  const std::string chip_word = c.word(yours ? "chip.overlay" : "chip.installation");
  out["provenance"] = Json{{"scope", scope}, {"layer", layer}, {"source", "profile:" + profile + pointer},
                           {"chip", Json{{"id", scope}, {"label", chip_word.empty() ? scope : chip_word}}}};
  out["evidence"] = evidence_of(yours ? Json{{"class", "observed"}, {"confidence", 1.0}} : src.value("epistemic_default", Json::object()), layer,
                                profile + pointer, yours ? "override" : "preset");
  // Two levels: the shipped preset of the profile, and your overlay of it (profiles/<profile>.pack), which you may change or clear.
  Json shipped_level{{"level", "installation"}, {"label", c.word("chip.installation")}, {"role", yours ? "overridden" : "supplied"}, {"active", true}};
  if (builtin_value) shipped_level["value"] = *builtin_value;
  Json overlay_level{{"level", "overlay"}, {"label", c.word("chip.overlay")}, {"role", yours ? "supplied" : "none"}, {"active", true},
                     {"file", "profiles/" + profile + ".pack"}};
  if (yours) overlay_level["value"] = out["value"];
  out["inheritance"] = Json::array({shipped_level, overlay_level});
  out["edit_rule"] = "optional";
  return out;
}

Json field_human(Impl& c, const Json& field) {
  const Json h = field.value("human", Json::object());
  Json out{{"label", c.say(h.value("label", Json()))}, {"why", c.say(h.value("why", Json()))}, {"effect", c.say(h.value("effect", Json()))},
           {"levels", h.value("levels", Json::array())}, {"group", h.value("group", "")}};
  for (const char* k : {"subgroup", "role", "role_status", "placed_from", "technical_only", "flows"})
    if (h.contains(k)) out[k] = h[k];
  return out;
}

// The settings and profile values, each in its group. Their values are resolved only when a node leaves the catalogue (a group, a card,
// settings.resolved: resolve_setting_node), never for the fields a render does not show.
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
    Json n = c.make(src, address, h.value("label", "").empty() ? f.key : h.value("label", ""), "group:" + group, "rom", f.key, "setting");
    n["why"] = h.value("why", "");
    if (field.contains("status")) n["status"] = field["status"].value("state", "");
    n["attrs"] = Json{{"key", f.key}, {"profile_value", f.profile_value}, {"type", field.value("type", "")}, {"edit", field.value("edit", Json(""))}, {"human", h}};
    if (f.profile_value) n["attrs"]["profile"] = Json{{"profile", field.value("profile", "")}, {"pointer", field.value("pointer", "")}};
    n["value_pending"] = true;
    c.field_index[f.key] = std::make_pair(f.field, f.profile_value);
    if (field.value("default", "") == "present" || field.contains("default_value"))
      for (const auto& e : src.value("edges", Json::array()))
        if (json::get_string(e, "kind") == "has_default") r.edges.push_back(Json{{"src", address}, {"dst", json::get_string(e, "to")}, {"kind", "has_default"}});
    r.nodes.push_back(std::move(n));
  }
  return r;
}

}  // namespace

namespace {

// A resolved value (resolve_field / profile_value_row) on its node: value, value_human, evidence, provenance {layer, layers, scope, chip}
// (layers = the levels of the chain that supplied the value or were overridden) and attrs.resolved (the whole resolution, with the inheritance).
void apply_resolved(Json& n, const Json& resolved) {
  const Json prov = resolved.value("provenance", Json::object());
  n["value"] = resolved.value("value", Json(nullptr));
  n["value_human"] = resolved.value("value_human", "");
  n["evidence"] = resolved.value("evidence", n.value("evidence", Json::object()));
  n["provenance"] = Json{{"layer", json::get_string(prov, "layer")}, {"layers", Json::array()}, {"scope", prov.value("scope", "")}, {"chip", prov.value("chip", Json::object())}};
  for (const auto& l : resolved.value("inheritance", Json::array()))
    if (json::get_string(l, "role") == "supplied" || json::get_string(l, "role") == "overridden") n["provenance"]["layers"].push_back(json::get_string(l, "level"));
  if (!n.contains("attrs") || !n["attrs"].is_object()) n["attrs"] = Json::object();
  n["attrs"]["resolved"] = resolved;
}

}  // namespace

// The value of one setting node through the render's one Resolver: value, value_human, evidence, provenance {layer, layers, scope, chip},
// the edit rule of a profile value, and attrs.resolved (the whole resolution, with the inheritance).
void resolve_setting_node(Catalogue::Impl& c, Json& n) {
  n.erase("value_pending");
  const std::string key = json::get_string(n.value("attrs", Json::object()), "key");
  const auto it = c.field_index.find(key);
  const Json* src = c.source("setting");
  if (it == c.field_index.end() || !src) {
    n["value_unknown_reason"] = "settings_fields_unavailable";
    return;
  }
  const Json& field = *it->second.first;
  const Json resolved = it->second.second ? profile_value_row(c, field, *src) : c.resolve_field(field, *src);
  apply_resolved(n, resolved);
  if (!field.contains("edit")) n["attrs"]["edit"] = resolved.value("edit_rule", Json(""));
}

namespace {

Result<Read> read_scopes(Impl& c, const Json& src, const std::string&, const Json&) {
  const Json* chain = json::find_pointer(c.graph_entry(), "/scopes/chain");
  if (!chain || !chain->is_array()) return Error(Errc::Unavailable, "scope_chain_absent");
  const std::set<std::string>& unit_levels = c.unit_levels();
  Read r;
  std::string prev;
  int order = 0;
  for (const auto& l : *chain) {
    const std::string id = json::get_string(l, "id");
    Json n = c.make(src, "scope:" + id, c.say(l.value("label", Json(id))), c.parent_of(src), "rom", "graph/scopes.pack#/chain/" + id, "preset");
    n["order"] = order++;
    // M1-W0 r2:b4: the navigator's "po co" line of a scope level is its catalogue meaning (same words as the card's chain).
    if (const Json* m = json::find(l, "meaning"); m) if (const std::string s = c.say(*m); !s.empty()) n["why"] = s;
    if (unit_levels.count(id)) n["attrs"]["unit_level"] = true;
    if (!prev.empty()) r.edges.push_back(Json{{"src", "scope:" + id}, {"dst", "scope:" + prev}, {"kind", "inherits_from"}});
    prev = id;
    r.nodes.push_back(std::move(n));
  }
  return r;
}

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
    n["value_human"] = json::get_string(models[m]["tier"], "label", json::get_string(models[m]["tier"], "id"));
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
  const Json* items = json::find_pointer(profile.values(), pointer);
  if (!items) return Error(Errc::Unavailable, "pointer_absent: " + domain + pointer);
  auto shipped = RuntimeProfile::builtin(domain);
  const Json* shipped_items = shipped ? json::find_pointer(shipped->values(), pointer) : nullptr;
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
    if (label.empty()) label = c.say(e.value.value("meaning", Json()));  // an entry without a label is named by what it means, never its id
    if (label.empty()) label = e.id;
    Json n = c.make(src, prefix + e.id, label, c.parent_of(src), e.origin, "runtime/graph_views.pack#/" + kind + "/" + e.id, "preset");
    n["provenance"]["layers"] = Json::array();
    for (const auto& p : e.provenance) n["provenance"]["layers"].push_back(json::get_string(p, "layer"));
    n["order"] = static_cast<std::int64_t>(e.order);
    if (e.value.contains("meaning")) n["why"] = c.say(e.value["meaning"]);
    if (e.disabled) n["status"] = "disabled";
    for (const char* k : {"device", "source", "parent", "direction"})
      if (e.value.contains(k)) n["attrs"][k] = e.value[k];
    n["attrs"]["id"] = e.id;
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
  const Json history = snap ? snap->value("history", Json::array()) : Json::array();
  // The value a field held before its present one (the last event of the field that replaced a known value with another): what a correction
  // overwrote. The history is bounded by the person's retention rules; when it no longer holds the event there is no previous value to show.
  auto previous_of = [&](const std::string& id, const Json& now) -> Json {
    if (!history.is_array()) return Json(nullptr);
    for (auto e = history.rbegin(); e != history.rend(); ++e) {
      if (!e->is_object() || json::get_string(*e, "field") != id) continue;
      const Json before = e->value("before", Json(nullptr));
      if (!before.is_object() || json::get_string(before, "status") != "known") continue;
      const Json value = before.value("value", Json(nullptr));
      if (value.is_null() || value == now) continue;
      return Json{{"value", value}, {"time", before.value("time", Json(nullptr))}, {"provenance", before.value("provenance", Json(nullptr))}};
    }
    return Json(nullptr);
  };
  std::vector<Json> out;
  for (const auto& it : fields.items()) {
    const Json& f = it.value();
    const Json prov = f.value("provenance", Json(nullptr));
    out.push_back(Json{{"id", it.key()},
                       {"previous", json::get_string(f, "status") == "known" ? previous_of(it.key(), f.value("value", Json(nullptr))) : Json(nullptr)},
                       {"section", f.value("section", "")},
                       {"category", f.value("category", "")},
                       {"state", f.value("status", "unknown")},
                       {"value", f.value("value", Json(nullptr))},
                       {"provenance", prov},
                       {"time", f.value("time", Json(nullptr))},
                       {"setting_key", f.value("setting_key", Json(nullptr))},
                       {"interviewed", snap != nullptr}});
  }
  return out;
}

namespace {

Json catalogue_title(const Json& a, const std::string& lang);

// The choices of an action parameter (graph_sources `param_choices`, key <action>.<parameter>): the person picks from words, never types an id.
// `from` names a source the kernel can read; today "conversations" (the list of conversations: value = id, label = title). A source the kernel
// cannot read, or an unreadable database, leaves the parameter as a typed one (logged), nothing is refused. The param then has `kind: "choice"` and
// `choices: [{value, label}]`; `none` (words) is the empty choice of an optional parameter.
void add_choices(Impl& c, const std::string& action, const std::string& name, Json& param) {
  const Json table = c.profile_values.value("param_choices", Json::object());
  const Json* spec = json::find(table, action + "." + name);
  if (!spec || !spec->is_object()) return;
  const std::string from = json::get_string(*spec, "from");
  const std::string vf = json::get_string(*spec, "value", "id");
  const std::string lf = json::get_string(*spec, "label", "title");
  Json choices = Json::array();
  // `for_levels` (data): per source, the values of the parameter named by `depends_on` for which its choices apply ({"conversations": ["conversation"],
  // "units": ["project"]}); a source not listed applies to any value. The choices carry them as `levels`, so a screen offers only the fitting ones.
  const auto for_levels = [&](const char* source) -> Json {
    const Json per_source = spec->value("for_levels", Json::object());
    const Json* lv = json::find(per_source, source);
    return lv && lv->is_array() ? *lv : Json(nullptr);
  };
  if (from == "conversations" && c.env.db) {
    auto convs = c.env.db->list_convs(-1);
    if (!convs) {
      log::write(log::Level::Debug, kLog, "choices of " + action + "." + name + ": conversations not readable: " + convs.error().message);
      return;
    }
    for (const auto& cv : *convs) {
      const Json row = cv.to_json();
      std::string label = json::get_string(row, lf);
      if (label.empty()) label = c.say(spec->value("untitled", Json("")));
      Json choice{{"value", json::get_string(row, vf)}, {"label", label}};
      if (const Json lv = for_levels("conversations"); lv.is_array()) choice["levels"] = lv;
      choices.push_back(std::move(choice));
    }
    // `also: ["units"]` adds the units (nodes of kind unit: value = the unit's id, label = its words and kind): a rule at a unit's level belongs to
    // one unit, one at the conversation's level to one conversation, and the same parameter names the instance of either.
    for (const auto& a : spec->value("also", Json::array())) {
      if (!a.is_string() || a.get<std::string>() != "units") continue;
      auto units = c.env.db->list_nodes(std::string("unit"), 500);
      if (!units) {
        log::write(log::Level::Debug, kLog, "choices of " + action + "." + name + ": units not readable: " + units.error().message);
        continue;
      }
      for (const auto& u : *units) {
        const Json row = u.to_json();
        const Json meta = row.value("metadata", Json::object());
        const std::string uid = json::get_string(meta, "unit_id");
        if (uid.empty()) continue;
        const std::string words = json::get_string(row, "label", uid);
        const std::string kind = json::get_string(meta, "unit_kind");
        Json choice{{"value", uid}, {"label", kind.empty() ? words : words + " (" + kind + ")"}};
        if (const Json lv = for_levels("units"); lv.is_array()) choice["levels"] = lv;
        choices.push_back(std::move(choice));
      }
    }
  } else if (from == "scope_levels") {
    // The levels of the one scope chain (the same chain a row's inheritance shows), limited to the levels the data names: a person picks the level a value
    // is written at. The words are the screen's chip words (chip.<level>), else the chain's own label. No chain readable: the parameter stays typed.
    const scopes::Resolver* r = c.get_resolver();
    if (!r) {
      log::write(log::Level::Debug, kLog, "choices of " + action + "." + name + ": scope chain not readable");
      return;
    }
    scopes::Binding b;
    b.key = "preference.probe";
    b.conv_id = c.env.conv_id;
    auto got = r->resolve(b);
    if (!got) {
      log::write(log::Level::Debug, kLog, "choices of " + action + "." + name + ": chain not resolved: " + got.error().message);
      return;
    }
    // `levels` (data): the levels worth offering when a value is written by hand (the preset leaves out the ones no person writes by hand, as the
    // engine's and the installation's); absent = every level of the chain. Either way the order is the chain's.
    std::set<std::string> offered;
    for (const auto& e : spec->value("levels", Json::array()))
      if (e.is_string()) offered.insert(e.get<std::string>());
    const Json chain = got->chain;
    for (const auto& l : chain) {
      const std::string level = json::get_string(l, "level");
      if (level.empty() || (!offered.empty() && !offered.count(level))) continue;
      std::string label = c.word("chip." + level);
      if (label.empty()) label = c.say(l.value("label", Json(level)));
      choices.push_back(Json{{"value", level}, {"label", label}});
    }
  } else {
    log::write(log::Level::Debug, kLog, "choices of " + action + "." + name + ": no reader for source '" + from + "'");
    return;
  }
  param["kind"] = "choice";
  param["choices"] = std::move(choices);
  if (const std::string dep = json::get_string(*spec, "depends_on"); !dep.empty()) param["depends_on"] = dep;
  if (const Json* none = json::find(*spec, "none"); none && !json::get_bool(param, "required", false)) param["none"] = c.say(*none);
}

// The actions of the catalogue as the class application sees them (the class the person's own actions are for): id -> {label, decision,
// implementation}, once per render. The label is the catalogue's title in the language asked; the decision is the rules' status for it.
const Json& application_actions(Impl& c) {
  if (c.action_table_cache) return *c.action_table_cache;
  c.action_table_cache = Json::object();
  Json& table = *c.action_table_cache;
  auto catalogue = action_catalogue(c.env.root);
  if (!catalogue) {
    log::write(log::Level::Debug, kLog, "the parts of You: action catalogue not readable: " + catalogue.error().message);
    return table;
  }
  Json decisions = Json::object();
  const actions::ElementSources sources{c.env.db, nullptr, c.env.root};
  if (auto fits = actions::invoke(c.env.root, "for", Json{{"element", "application"}}, &sources, c.env.cfg); fits) {
    for (const auto& a : fits->value("actions", Json::array()))
      decisions[json::get_string(a, "action", json::get_string(a, "id"))] = Json{{"decision", a.value("status", Json(nullptr))}, {"implementation", a.value("implementation", Json(nullptr))}};
  } else {
    log::write(log::Level::Debug, kLog, "the parts of You: decisions of the rules not readable: " + fits.error().message);
  }
  for (const auto& a : catalogue->value("actions", Json::array())) {
    const std::string id = json::get_string(a, "id");
    Json row{{"label", catalogue_title(a, c.lang())}, {"decision", nullptr}, {"implementation", a.value("status", Json(nullptr))}};
    // The parameters the action takes (the catalogue's `params`): name, how it is entered (kind), the words that ask for it, whether it is required.
    // The label is the pack's word for this parameter (param.<action>.<name>, data of graph_sources) else the catalogue's own description.
    Json params = Json::array();
    for (const auto& p : a.value("params", Json::array())) {
      const std::string name = json::get_string(p, "name");
      if (name.empty()) continue;
      Json label = c.words.value("param." + id + "." + name, Json(nullptr));
      const std::string words = label.is_object() ? c.say(label) : std::string();
      Json param{{"name", name},
                 {"kind", json::get_string(p, "edit", json::get_string(p, "type", "free_text"))},
                 {"type", json::get_string(p, "type", "string")},
                 {"required", json::get_bool(p, "required", false)},
                 {"label", words.empty() ? c.say(p.value("description", Json())) : words}};
      add_choices(c, id, name, param);
      params.push_back(std::move(param));
    }
    row["params"] = std::move(params);
    if (const Json* d = json::find(decisions, id)) {
      row["decision"] = d->value("decision", Json(nullptr));
      if (!d->value("implementation", Json(nullptr)).is_null()) row["implementation"] = (*d)["implementation"];
    }
    table[id] = std::move(row);
  }
  return table;
}

// A dotted path ("attrs.section") of a node; nullptr when a step is missing.
const Json* path_in(const Json& n, const std::string& path) {
  const Json* cur = &n;
  std::size_t pos = 0;
  while (cur) {
    const std::size_t dot = path.find('.', pos);
    cur = cur->is_object() ? json::find(*cur, path.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos)) : nullptr;
    if (dot == std::string::npos) break;
    pos = dot + 1;
  }
  return cur;
}

// The fields a part's member must have (profile.subgraphs members[].where: a path -> a value, or a list of values one of which it has).
bool fits_where(const Json& n, const Json& where) {
  if (!where.is_object()) return true;
  for (const auto& w : where.items()) {
    const Json* v = path_in(n, w.key());
    if (!v) return false;
    if (w.value().is_array()) {
      bool any = false;
      for (const auto& x : w.value()) any = any || x == *v;
      if (!any) return false;
    } else if (!(w.value() == *v)) {
      return false;
    }
  }
  return true;
}

// The actions a part of You offers on a node (profile.subgraphs actions / actions_here): the catalogue's own rows (title, decision, implementation)
// with the arguments filled from the node ("attrs.slot" = that field of the node, {"const": x} = x). An action the catalogue does not have is not
// offered (and logged); one whose argument the node lacks is not offered on that node.
Json self_actions(Impl& c, const Json& specs, const Json& node) {
  Json out = Json::array();
  if (!specs.is_array() || specs.empty()) return out;
  const Json table = application_actions(c);
  for (const auto& s : specs) {
    const std::string id = json::get_string(s, "action");
    const Json* known = json::find(table, id);
    if (id.empty() || !known) {
      log::write(log::Level::Debug, kLog, "the parts of You offer the action '" + id + "', which the catalogue does not have");
      continue;
    }
    // `where` (the same form as the members' where of a part): the action is offered only on a node that has these fields (a state, an evidence class).
    if (s.contains("where") && !fits_where(node, s["where"])) continue;
    const Json arg_specs = s.value("args", Json::object());
    Json args = Json::object();
    bool complete = true;
    for (const auto& a : arg_specs.items()) {
      if (a.value().is_object() && a.value().contains("const")) {
        args[a.key()] = a.value()["const"];
      } else if (a.value().is_string()) {
        const Json* v = path_in(node, a.value().get<std::string>());
        if (v && !v->is_null()) args[a.key()] = *v;
        else complete = false;
      } else {
        complete = false;
      }
    }
    if (!complete) continue;
    Json row = *known;
    row["id"] = id;
    row["args"] = args;
    // What the person still has to give: the required parameters the node did not fill, and those the spec names in `ask` (optional ones). The web
    // builds its form from this list; the arguments of `args` are never asked for.
    Json ask = Json::array();
    std::set<std::string> named;
    for (const auto& n : s.value("ask", Json::array()))
      if (n.is_string()) named.insert(n.get<std::string>());
    for (const auto& p : known->value("params", Json::array())) {
      const std::string name = json::get_string(p, "name");
      if (args.contains(name)) continue;
      if (json::get_bool(p, "required", false) || named.count(name)) ask.push_back(p);
    }
    row["params"] = std::move(ask);
    out.push_back(std::move(row));
  }
  return out;
}

void apply_review(Impl& c, Json& n, const Json& src, const Json& meta);

Result<Read> read_party(Impl& c, const Json& src, const std::string&, const Json&) {
  Read r;
  Json self = c.make(src, "party:self", c.say(src.value("label", Json("You"))), c.parent_of(src), "rom", "graph/entry.pack#/seed/elements/party", "preset");
  self["kind"] = json::get_string(src, "class", "person");
  self["has_children"] = true;
  if (const Json* w = json::find(src, "self_why"); w && w->is_object()) self["why"] = c.say(*w);
  self["actions"] = self_actions(c, src.value("self_actions", Json::array()), self);
  Env pe = c.env;
  auto slots = profile_slots(pe);
  if (!slots) {
    self["count_unknown_reason"] = slots.error().message;
    r.nodes.push_back(std::move(self));
    return r;
  }
  Json by = Json::object();
  const Json labels = src.value("slot_labels", Json::object());
  const Json plan = c.slot_plan();  // a copy: nothing below may leave a reference into the cache
  std::set<std::string> asked;
  for (const auto& f : plan.value("asked", Json::array())) asked.insert(f.get<std::string>());
  const Json skipped = plan.value("skipped", Json::object());
  for (const auto& s : *slots) {
    const std::string id = json::get_string(s, "id");
    const std::string state = json::get_string(s, "state", "unknown");
    by[state] = by.value(state, static_cast<std::int64_t>(0)) + 1;
    std::string label = c.say(labels.value(id, Json()));
    if (label.empty()) label = id;
    const Json prov = s.value("provenance", Json(nullptr));
    // The store keeps how an answer came as a word (user_stated | form | user_corrected | model_inferred); an older or foreign snapshot may carry an object.
    // The evidence class is chosen by that word (evidence_by_method: your own words are said, a model's inference is inferred); the method is what
    // the screen says about it (the interview, a correction, a model's inference).
    const std::string how = prov.is_string() ? prov.get<std::string>() : std::string();
    const std::string method = prov.is_object() ? json::get_string(prov, "method", json::get_string(prov, "kind", "interview"))
                               : how == "model_inferred" ? std::string("model_inferred") : how == "user_corrected" ? std::string("corrected") : std::string("interview");
    const Json when = prov.is_object() ? prov.value("time", prov.value("created", Json(nullptr))) : s.value("time", Json(nullptr));
    const bool known = state == "known";
    Json n = c.make(src, "party:self/" + id, label, "party:self", known ? "onboarding" : "rom", known ? "onboarding.profile.fields." + id : "onboarding/scenario.pack#/fields/" + id,
                    known ? method : "scenario", known ? when : Json(nullptr));
    n["kind"] = "profile_slot";
    n["status"] = state;
    // Only a known slot has a value: unknown, declined and never say nothing about the person.
    n["value"] = known ? s.value("value", Json(nullptr)) : Json(nullptr);
    n["value_human"] = known ? c.human_value(s.value("value", Json(nullptr)), Json::object()) : c.say(c.words.value("slot_" + state, Json(state)));
    // A slot that holds a choice among the levels of the scenario (the question that sets profile_detail_level) says the level's label in the language
    // asked; the id stays in attrs.value_id (it is what the setting holds).
    Json value_id = Json(nullptr);
    if (known && s.value("setting_key", Json(nullptr)).is_string() && s.value("value", Json(nullptr)).is_string()) {
      if (const std::string lab = c.level_label(s["value"].get<std::string>()); !lab.empty()) {
        value_id = s["value"];
        n["value_human"] = lab;
      }
    }
    // An answer is what you said, an inference is never shown as said. A slot with no value has no evidence at all (an empty slot holds nothing to
    // be evidence of): evidence is null and `state` says why - unanswered (unknown), declined or never - in the words of the screen.
    if (known) {
      Json cls = evidence_by_method(src, how.empty() ? method : how, src.value("epistemic_default", Json::object()));
      n["evidence"] = evidence_of(cls, "onboarding", json::get_string(n["evidence"]["provenance"], "source_ref"), method, json::get_string(n["evidence"]["provenance"], "time").empty() ? Json(nullptr) : n["evidence"]["provenance"]["time"]);
    } else {
      n["evidence"] = nullptr;
      n["state"] = state == "unknown" ? std::string("unanswered") : state;
    }
    n["attrs"] = Json{{"slot", id}, {"section", s.value("section", "")}, {"category", s.value("category", "")}};
    if (!value_id.is_null()) n["attrs"]["value_id"] = value_id;
    // What a correction overwrote (the card says it under the details): the earlier value in words, and when it was given.
    if (known)
      if (const Json prev = s.value("previous", Json(nullptr)); prev.is_object())
        n["attrs"]["previous"] = Json{{"value_human", c.human_value(prev.value("value", Json(nullptr)), Json::object())}, {"time", prev.value("time", Json(nullptr))}};
    // Whether the interview asks this slot at your level, and when it will not, why (profile.show says the same): null = no level or no interview yet.
    if (plan.contains("asked")) {
      const bool is_asked = asked.count(id) > 0;
      n["attrs"]["asked_at_level"] = is_asked;
      if (!is_asked)
        if (const Json* why = json::find(skipped, id); why && why->is_object()) {
          n["attrs"]["reason"] = why->value("reason", Json(nullptr));
          n["attrs"]["reason_text"] = why->value("reason_text", "");
          if (state == "unknown" && json::get_string(*why, "reason") == "above_level") {
            n["value_human"] = c.say(c.words.value("slot_above_level", Json("not asked at your level")));
            if (const std::string text = json::get_string(*why, "reason_text"); !text.empty()) n["why"] = text;
          }
        }
    } else {
      n["attrs"]["asked_at_level"] = nullptr;
      n["attrs"]["asked_at_level_unknown_reason"] = plan.value("unknown_reason", "not_interviewed");
    }
    r.nodes.push_back(std::move(n));
  }
  // The people and organisations the analysis found (nodes of the kinds the source names): an inference, never something you said.
  const Json contacts = src.value("contacts", Json::object());
  if (c.env.db && contacts.is_object() && contacts.contains("node_kinds")) {
    const std::int64_t limit = json::get_int(contacts, "limit", 200);
    for (const auto& k : contacts["node_kinds"]) {
      if (!k.is_string()) continue;
      auto nodes = c.env.db->list_nodes(std::string_view(k.get<std::string>()), limit > 0 ? static_cast<int>(limit) : -1);
      if (!nodes) {
        self["attrs"]["contacts_unknown_reason"] = nodes.error().message;
        log::write(log::Level::Debug, kLog, "contacts of kind " + k.get<std::string>() + " not readable: " + nodes.error().message);
        continue;
      }
      for (const auto& t : *nodes) {
        const std::string bare = starts_with(t.id, "n_") ? t.id.substr(2) : t.id;
        Json n = c.make(src, "party:" + bare, t.label.empty() ? bare : t.label, "party:self", "db", t.id, "analysis", t.created);
        n["kind"] = k.get<std::string>() == "person" ? "person" : "organization";
        n["same_as"] = Json::array({t.id});
        n["evidence"] = evidence_of(Json{{"class", "inferred"}, {"confidence", t.metadata.is_object() && t.metadata.contains("confidence") ? t.metadata["confidence"] : Json(nullptr)}},
                                    "db", t.id, "analysis", t.created);
        n["attrs"] = Json{{"contact", true}, {"node_kind", k.get<std::string>()}, {"node_id", t.id}};
        apply_review(c, n, src, t.metadata);
        r.edges.push_back(Json{{"src", "party:self"}, {"dst", "party:" + bare}, {"kind", "knows"}});
        r.nodes.push_back(std::move(n));
      }
    }
  }
  self["attrs"]["slots"] = by;
  r.nodes.insert(r.nodes.begin(), std::move(self));
  return r;
}


// The parts of You: one node per entry of profile.subgraphs, with how many members it has by state and by evidence class (an inference is counted
// as an inference), and the actions offered on the part itself.
Result<Read> read_subgraphs(Impl& c, const Json& src, const std::string&, const Json&) {
  Read r;
  const Json table = c.subgraph_table();
  const std::string layer = c.self_table_layer == "user" ? "onboarding" : "rom";
  std::int64_t order = 0;
  for (const auto& sg : table) {
    const std::string id = json::get_string(sg, "id");
    if (id.empty()) continue;
    const std::string address = std::string(Impl::kPartPrefix) + id;
    if (json::get_bool(sg, "hidden", false)) {
      r.hidden.push_back(Json{{"source", json::get_string(src, "id")}, {"id", address}, {"by", c.self_table_layer == "user" ? "overlay" : "rom"}});
      continue;
    }
    Json n = c.make(src, address, c.say(sg.value("label", Json(id))), Impl::kSelf, layer, "profiles/user.pack#profile.subgraphs/" + id, "preset");
    if (const Json w = sg.value("why", Json()); !w.is_null()) n["why"] = c.say(w);
    n["order"] = ++order;
    n["has_children"] = true;
    Json states = Json::object(), classes = Json::object(), unknown = Json::array();
    std::int64_t members = 0;
    for (const auto& m : sg.value("members", Json::array())) {
      const std::string sid = json::get_string(m, "source");
      const Json* member_source = c.source(sid);
      if (!member_source) {
        unknown.push_back(Json{{"source", sid}, {"reason", "source_missing"}});
        continue;
      }
      auto got = c.read_whole(*member_source);
      if (!got) {
        unknown.push_back(Json{{"source", sid}, {"reason", got.error().message}});
        continue;
      }
      if (!(*got)->unknown.empty()) {
        unknown.push_back(Json{{"source", sid}, {"reason", (*got)->unknown}});
        continue;
      }
      for (const auto& nd : (*got)->nodes) {
        if (json::get_string(nd, "parent") != address) continue;
        ++members;
        if (const std::string st = json::get_string(nd, "status"); !st.empty()) states[st] = states.value(st, static_cast<std::int64_t>(0)) + 1;
        if (const std::string cls = json::get_string(nd.value("evidence", Json::object()), "class"); !cls.empty())
          classes[cls] = classes.value(cls, static_cast<std::int64_t>(0)) + 1;
      }
    }
    n["attrs"] = Json{{"id", id}, {"counts", states}, {"evidence_classes", classes}, {"members", members}, {"per_flow", json::get_bool(sg, "per_flow", false)}};
    if (!unknown.empty()) n["attrs"]["members_unknown"] = unknown;
    n["actions"] = self_actions(c, sg.value("actions_here", Json::array()), n);
    r.nodes.push_back(std::move(n));
  }
  return r;
}

Json layer_evidence(const Json& src, const std::string& layer, const std::string& ref) {
  const bool yours = layer == "user";
  return evidence_of(yours ? Json{{"class", "observed"}, {"confidence", 1.0}} : src.value("epistemic_default", Json::object()), yours ? "onboarding" : "rom", ref,
                     yours ? "override" : "preset");
}

// A preference of the user profile pack through the one scope chain (M1-K2c-2): the pack's value is the installation level, your override in the
// profile is the person level, a rule or the conversation's own value (settings[preference.<name>]) or a unit's rule are the levels below; the
// answer says which level set the value (provenance.scope, the chip) and carries the whole chain (attrs.resolved.inheritance). A preference
// that is disabled, excluded, only proposed or missing is not resolved at all: it has no value and says its status.
Result<Read> read_preferences(Impl& c, const Json& src, const std::string&, const Json&) {
  LOOM_TRY_ASSIGN(const onboarding::DefaultLayers* layers, c.layers());
  Read r;
  for (const auto& e : layers->pack().value("entries", Json::array())) {
    const std::string key = json::get_string(e, "key");
    if (!starts_with(key, "preference.")) continue;
    LOOM_TRY_ASSIGN(auto res, layers->resolve(key));
    const std::string layer = json::get_string(res, "layer");
    const std::string status = json::get_string(res, "status");
    const std::string name = key.substr(std::string("preference.").size());
    std::string label = c.say(src.value("labels", Json::object()).value(name, Json()));
    Json n = c.make(src, "preference:" + name, label.empty() ? name : label, "party:self", layer == "user" ? "onboarding" : "rom", "profiles/user.pack#" + key, "preset");
    // The value in words (value_meanings of the source); a value no meaning names is shown as it is.
    Json meanings = Json::array();
    const Json named = src.value("value_meanings", Json::object()).value(name, Json::object());
    if (named.is_object())
      for (const auto& m : named.items()) meanings.push_back(Json{{"value", m.key()}, {"meaning", m.value()}});
    // The interview's slot that asks about the same thing is the same preference seen from the interview (same_as); changing the preference is
    // changing that slot, so the node carries it (attrs.slot: the argument of the actions the parts table offers on a preference).
    const std::string slot = json::get_string(src.value("slot_of", Json::object()), name);
    if (!slot.empty()) r.edges.push_back(Json{{"src", "preference:" + name}, {"dst", "party:self/" + slot}, {"kind", "same_as"}});
    n["attrs"] = Json{{"key", key}, {"layer", layer}};
    if (!slot.empty()) n["attrs"]["slot"] = slot;
    if (status != "effective") {
      n["value"] = res.value("value", Json(nullptr));
      n["value_human"] = c.human_value(n["value"], Json{{"values", meanings}});
      n["status"] = status;
      n["evidence"] = layer_evidence(src, layer, "profiles/user.pack#" + key);
      r.nodes.push_back(std::move(n));
      continue;
    }
    // The pack entry is what ships; a "user" layer is the person's override of it.
    Json field{{"key", key}, {"values", meanings}, {"preset_value", e.value("value", Json(nullptr))}, {"preset_source", "preset:profiles/user.pack"},
               {"person_layer", "profiles/user.pack"}, {"person_source", "profile:profiles/user.pack#" + key}};
    if (layer == "user") field["person_value"] = res.value("value", Json(nullptr));
    const Json resolved = c.resolve_field(field, src);
    apply_resolved(n, resolved);
    n["status"] = status;
    n["attrs"]["scope"] = resolved.value("provenance", Json::object()).value("scope", "");
    r.nodes.push_back(std::move(n));
  }
  return r;
}

Result<Read> read_privacy(Impl& c, const Json& src, const std::string&, const Json&) {
  LOOM_TRY_ASSIGN(const onboarding::DefaultLayers* layers, c.layers());
  LOOM_TRY_ASSIGN(auto res, layers->resolve("onboarding.privacy"));
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

// The person's verdict on a conclusion of the analysis (metadata.review of the database node, written by inference.confirm / inference.reject):
// confirmed = something the person said now (class from evidence_by_method.user_confirmed, said unless the pack says otherwise; the time is the
// verdict's); rejected = the entry stays, holds no evidence, says why (state rejected) and keeps what it was (attrs.review.was). Nothing is dropped.
void apply_review(Impl& c, Json& n, const Json& src, const Json& meta) {
  if (!meta.is_object()) return;
  const Json review = meta.value("review", Json(nullptr));
  if (!review.is_object()) return;
  const std::string decision = json::get_string(review, "decision");
  const std::string method = json::get_string(review, "method", decision == "confirmed" ? "user_confirmed" : "user_rejected");
  Json info{{"decision", decision}, {"time", review.value("time", Json(nullptr))}, {"method", method}, {"was", n["evidence"]}};
  const Json history = meta.value("review_history", Json(nullptr));
  if (history.is_array() && !history.empty()) info["history"] = history;
  n["attrs"]["review"] = info;
  if (decision == "confirmed") {
    Json cls = Json{{"class", "said"}, {"confidence", 1.0}};
    const Json by_method = src.value("evidence_by_method", Json::object());
    if (const Json* by = json::find(by_method, method); by && by->is_object()) cls = *by;
    n["evidence"] = evidence_of(cls, "db", json::get_string(n["evidence"]["provenance"], "source_ref"), method, review.value("time", Json(nullptr)));
  } else if (decision == "rejected") {
    n["evidence"] = nullptr;
    n["state"] = "rejected";
    n["status"] = "rejected";
    const std::string why = c.word("review.rejected");
    if (!why.empty()) n["why"] = why;
  }
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
    n["attrs"]["node_id"] = t.id;
    apply_review(c, n, src, t.metadata);
    r.edges.push_back(Json{{"src", "party:self"}, {"dst", address}, {"kind", "interested_in"}});
    r.nodes.push_back(std::move(n));
  }
  return r;
}

// "Learned by the model": analysis nodes (any kind named in the source's `kinds`, an open list in data) whose provenance.method (written by the graph
// engine when it created the node) is one of the source's `methods` (a preset list in data; a method counts when it equals an entry or starts with it,
// e.g. "model:" covers "model:<id>"). Evidence class and the way to confirm/reject are the same as for the interests; a node with no provenance
// (older, imported, regex) is simply not here - it was never claimed to come from a model.
Result<Read> read_learned(Impl& c, const Json& src, const std::string&, const Json& paging) {
  if (!c.env.db) return Error(Errc::Unavailable, "database_unavailable");
  const std::int64_t limit = c.page_limit(src, paging);
  std::vector<std::string> kinds, methods;
  for (const auto& k : src.value("kinds", Json::array())) if (k.is_string()) kinds.push_back(k.get<std::string>());
  for (const auto& m : src.value("methods", Json::array())) if (m.is_string()) methods.push_back(m.get<std::string>());
  Read r;
  for (const auto& kind : kinds) {
    LOOM_TRY_ASSIGN(auto nodes, c.env.db->list_nodes(std::string_view(kind), limit > 0 ? static_cast<int>(limit) : -1));
    for (const auto& t : nodes) {
      const Json prov = t.metadata.is_object() ? t.metadata.value("provenance", Json(nullptr)) : Json(nullptr);
      const std::string method = prov.is_object() ? json::get_string(prov, "method") : std::string();
      bool counts = false;
      for (const auto& m : methods)
        if (!method.empty() && !m.empty() && starts_with(method, m)) counts = true;
      if (!counts) continue;
      const std::string address = "learned:" + kind + ":" + t.label;
      Json n = c.make(src, address, t.label, "party:self", "db", t.id, method, prov.value("at", Json(t.created)));
      n["same_as"] = Json::array({t.id});
      if (t.metadata.contains("confidence")) n["evidence"]["confidence"] = t.metadata["confidence"];
      n["attrs"]["node_id"] = t.id;
      n["attrs"]["node_kind"] = kind;
      n["attrs"]["method"] = method;
      apply_review(c, n, src, t.metadata);
      r.nodes.push_back(std::move(n));
    }
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
    // The method is what the note says it is (provenance.method), else where it came from (provenance.source: user, import, folder, guardian, yours);
    // a note written before the origin was recorded says that, in words, and is not claimed to be anything.
    std::string method = prov.is_object() ? json::get_string(prov, "method") : std::string();
    const std::string source_kind = prov.is_object() ? json::get_string(prov, "source") : std::string();
    if (method.empty()) method = source_kind.empty() ? std::string("not_recorded_before_m1") : "note_" + source_kind;
    Json n = c.make(src, "memory:" + m.id, clip(m.content, 60), m.parent_id ? "memory:" + *m.parent_id : std::string("party:self"), "memory.json", "memory.json#" + m.id,
                    method, m.created);
    n["evidence"] = evidence_of(evidence_by_method(src, method, src.value("epistemic_default", Json::object())), "memory.json", "memory.json#" + m.id, method,
                                prov.is_object() ? prov.value("time", Json(m.created)) : Json(m.created));
    n["evidence"]["human"] = c.evidence_words(n["evidence"]);
    n["count"] = kids[m.id];
    n["has_children"] = kids[m.id] > 0;
    n["status"] = m.active ? "active" : "inactive";
    n["attrs"] = Json{{"note_id", m.id}, {"node_type", m.node_type}, {"tags", m.tags},
                      {"provenance", Json{{"known", !source_kind.empty() || !prov.is_null()}, {"source", source_kind.empty() ? Json(nullptr) : Json(source_kind)},
                                          {"time", prov.is_object() ? prov.value("time", Json(nullptr)) : Json(nullptr)}, {"ref", prov.is_object() ? prov.value("ref", Json(nullptr)) : Json(nullptr)},
                                          {"words", n["evidence"]["human"]}}}};
    // A forgotten entry (memory.forget) stays on the list with the record of forgetting: it is not in any conversation, and it says so, when, and
    // whether its words were kept. It is its own status, so that a part of the screen (data) can fold it away without hiding it.
    const Json forgotten = m.metadata.is_object() ? m.metadata.value("forgotten", Json(nullptr)) : Json(nullptr);
    if (forgotten.is_object()) {
      const bool kept = json::get_bool(forgotten, "kept", true);
      const std::string when = json::get_string(forgotten, "time");
      n["status"] = "forgotten";
      n["state"] = "forgotten";
      if (m.content.empty()) n["label"] = c.word("forgotten.label_dropped");
      std::string why = c.word(kept ? "forgotten.why" : "forgotten.why_dropped");
      if (when.size() >= 10) why += " (" + when.substr(0, 10) + ")";
      n["why"] = why;
      n["attrs"]["forgotten"] = forgotten;
    }
    // The earlier forgettings of an entry that was brought back (metadata.forget_history: when, by whom, and the restoring): the record is kept
    // whatever the entry is now, and the screen can say it in words.
    const Json history = m.metadata.is_object() ? m.metadata.value("forget_history", Json(nullptr)) : Json(nullptr);
    if (history.is_array() && !history.empty()) n["attrs"]["forget_history"] = history;
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
    std::string sql = "SELECT c.id, c.title, c.source, c.created, c.updated, c.metadata, (SELECT COUNT(*) FROM messages m WHERE m.conv_id = c.id AND m.status = 'active') FROM conversations c";
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
  // An imported conversation's messages came by import (its source); otherwise yours are what you said, the model's are its replies.
  std::string imported;
  if (!conv_id.empty()) {
    auto conv = c.env.db->get_conv(conv_id);
    if (!conv) log::write(log::Level::Debug, kLog, "conversation " + conv_id + " not readable: " + conv.error().message);
    else if (*conv && !(*conv)->source.empty() && (*conv)->source != "user") imported = (*conv)->source;
  }
  for (const auto& m : msgs) {
    const std::string address = address_of_db(m.id, db_prefix, prefix);
    const std::string method = !imported.empty() ? "import:" + imported : m.role == "user" ? "said" : m.role == "assistant" ? "model_reply" : "message";
    std::string who = c.word("message.role." + m.role);
    if (who.empty()) who = m.role;
    Json n = c.make(src, address, who + ": " + clip(m.text, 60), address_of_db(m.conv_id, conv_db, conv_prefix), "db", m.id, method, m.created);
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
    n["attrs"] = Json{{"unit_kind", id.substr(0, id.find('/'))}, {"unit_id", id.substr(id.find('/') + 1)}, {"conversations", n_members}};
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
    // What it is for, in the language asked (why.<lang>); English is the description. A language without its words shows the
    // description and says so (no_translation), never silently another language.
    if (const std::string mine = json::get_string(a.value("why", Json::object()), c.lang()); !mine.empty()) {
      n["why"] = mine;
    } else {
      n["why"] = json::get_string(a, "description");
      if (c.lang() != "en")
        n["no_translation"] = Json{{"lang", c.lang()}, {"shown_lang", "en"}, {"words", c.word("no_translation", Json{{"lang", c.lang()}, {"shown_lang", "en"}})}};
    }
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
  const Json all_actions = cat.value("actions", Json::array());
  std::map<std::string, const Json*> by_id;  // pointers into all_actions, which outlives them
  for (const auto& a : all_actions) by_id[json::get_string(a, "id")] = &a;
  Read r;
  for (const auto& a : all_actions) {
    if (json::get_string(a, "id") != id) continue;
    const Json human_steps = a.value("human", Json::object()).value("steps", Json::array());
    const bool described = human_steps.is_array() && !human_steps.empty();
    const Json steps = described ? human_steps : a.value("steps", Json::array());
    for (std::size_t i = 0; i < steps.size(); ++i) {
      if (only_step && only_step != i + 1) continue;
      const Json& s = steps[i];
      // A step without words of its own is named by its action's title ("Użyj modelu"); the action id stays in attrs.
      const std::string step_action = json::get_string(s, "action", json::get_string(s, "id"));
      std::string label = described ? c.say(s.value("label", Json())) : std::string();
      if (label.empty() && !step_action.empty())
        if (const auto it = by_id.find(step_action); it != by_id.end()) label = catalogue_title(*it->second, c.lang()).get<std::string>();
      if (label.empty()) label = step_action;
      if (label.empty()) label = std::to_string(i + 1);
      Json n = c.make(src, "step:" + id + "/" + std::to_string(i + 1), label, flow, "rom", "runtime/actions.pack#/actions/" + id + "/" + (described ? "human/steps" : "steps"), "description");
      n["order"] = static_cast<std::int64_t>(i + 1);
      if (!step_action.empty()) n["attrs"]["action"] = step_action;
      if (!described && s.contains("action")) r.edges.push_back(Json{{"src", n["address"]}, {"dst", "flow:" + json::get_string(s, "action")}, {"kind", "uses"}});
      r.nodes.push_back(std::move(n));
    }
  }
  return r;
}

// The layer of a source's own data: rom when the effective row is the shipped one, else the user's overlay of graph_sources.
std::string source_layer(const Json& src) {
  static const Json shipped = [] {
    auto p = RuntimeProfile::builtin("graph_sources");
    if (!p) log::write(log::Level::Debug, kLog, "shipped graph_sources not readable: " + p.error().message);
    return p ? p->values().value("sources", Json::array()) : Json::array();
  }();
  for (const auto& s : shipped)
    if (json::get_string(s, "id") == json::get_string(src, "id")) return s == src ? std::string("rom") : std::string("overlay:graph_sources");
  return "overlay:graph_sources";
}

// Nodes written in the source's own data (params.nodes [{id, label, why?}]): a node that holds other sources ({parent: {node}}).
Result<Read> read_static(Impl& c, const Json& src, const std::string&, const Json&) {
  Read r;
  const std::string prefix = json::get_string(src, "prefix");
  const std::string layer = source_layer(src);
  std::int64_t order = 0;
  for (const auto& e : src.value("params", Json::object()).value("nodes", Json::array())) {
    const std::string id = json::get_string(e, "id");
    if (id.empty()) continue;
    Json n = c.make(src, prefix + id, c.say(e.value("label", Json(id))), c.parent_of(src), layer,
                    "runtime/graph_sources.pack#/sources/" + json::get_string(src, "id") + "/params/nodes/" + id, "preset");
    n["order"] = order++;
    n["why"] = c.say(e.value("why", Json()));
    n["has_children"] = true;  // what hangs under it is counted from the sources parented to it
    r.nodes.push_back(std::move(n));
  }
  return r;
}

// What the app does without being asked (params.items [{id, label, why, setting?, flow?}]): each with the setting that switches it (its
// value, through the scope chain, is the node's value) and the flow it runs.
Result<Read> read_automations(Impl& c, const Json& src, const std::string&, const Json&) {
  std::set<std::string> flows;
  if (auto cat = action_catalogue(c.env.root))
    for (const auto& a : cat->value("actions", Json::array())) flows.insert(json::get_string(a, "id"));
  else log::write(log::Level::Debug, kLog, "automations: action catalogue not readable: " + cat.error().message);
  Read r;
  const std::string prefix = json::get_string(src, "prefix");
  const std::string layer = source_layer(src);
  std::int64_t order = 0;
  for (const auto& item : src.value("params", Json::object()).value("items", Json::array())) {
    const std::string id = json::get_string(item, "id");
    if (id.empty()) continue;
    const std::string address = prefix + id;
    Json n = c.make(src, address, c.say(item.value("label", Json(id))), c.parent_of(src), layer,
                    "runtime/graph_sources.pack#/sources/" + json::get_string(src, "id") + "/params/items/" + id, "preset");
    n["order"] = order++;
    n["why"] = c.say(item.value("why", Json()));
    Json attrs = Json::object();
    if (const std::string key = json::get_string(item, "setting"); !key.empty()) {
      attrs["setting"] = key;
      LOOM_TRY_ASSIGN(auto field, c.node_at("setting:" + key));
      if (field) {
        n["value"] = field->value("value", Json(nullptr));
        n["value_human"] = field->value("value_human", Json(nullptr));
        n["provenance"]["chip"] = field->value("provenance", Json::object()).value("chip", Json(nullptr));
        n["evidence_overrides"] = Json{{"value", field->value("evidence", Json::object())}};
        r.edges.push_back(Json{{"src", address}, {"dst", "setting:" + key}, {"kind", "switched_by"}});
      } else {
        n["value_unknown_reason"] = "setting_absent";
      }
    }
    if (const std::string flow = json::get_string(item, "flow"); !flow.empty()) {
      attrs["flow"] = flow;
      if (flows.count(flow)) r.edges.push_back(Json{{"src", address}, {"dst", "flow:" + flow}, {"kind", "runs"}});
      else attrs["flow_missing"] = true;
    }
    n["attrs"] = attrs;
    r.nodes.push_back(std::move(n));
  }
  return r;
}

// The background tasks (loom_tasks), newest first, paged by the source's page_setting; the total counts all of them.
Result<Read> read_tasks(Impl& c, const Json& src, const std::string&, const Json& paging) {
  if (!c.env.db) return Error(Errc::Unavailable, "database_unavailable");
  const std::string prefix = json::get_string(src, "prefix");
  const std::int64_t limit = c.page_limit(src, paging);
  const std::int64_t offset = std::max<std::int64_t>(0, json::get_int(paging, "offset", 0));
  const std::string only = json::get_string(paging, "address");
  Read r;
  struct Row {
    std::string id, kind, status, created, updated, error;
    std::int64_t attempts = 0;
  };
  std::vector<Row> rows;
  std::int64_t total = 0;
  {
    auto lock = c.env.db->lock();
    auto& conn = c.env.db->conn();
    if (!conn.has_table("loom_tasks")) return Error(Errc::Unavailable, "tasks_table_absent");
    std::string sql = "SELECT id, kind, status, created, updated, COALESCE(error, ''), attempts FROM loom_tasks";
    if (!only.empty()) sql += " WHERE id = ?";
    sql += " ORDER BY updated DESC, rowid DESC";
    if (only.empty() && limit > 0) sql += " LIMIT ? OFFSET ?";
    LOOM_TRY_ASSIGN(auto st, conn.prepare(sql));
    if (!only.empty()) st.bind(1, only.substr(prefix.size()));
    else if (limit > 0) st.bind(1, limit).bind(2, offset);
    while (true) {
      LOOM_TRY_ASSIGN(bool more, st.step());
      if (!more) break;
      rows.push_back(Row{st.get_text(0), st.get_text(1), st.get_text(2), st.get_text(3), st.get_text(4), st.get_text(5), st.get_int(6)});
    }
    if (only.empty()) {
      LOOM_TRY_ASSIGN(auto n, conn.query_int("SELECT COUNT(*) FROM loom_tasks"));
      total = n.value_or(0);
    } else {
      total = static_cast<std::int64_t>(rows.size());
    }
  }
  for (const auto& row : rows) {
    Json n = c.make(src, prefix + row.id, row.kind + " — " + row.status, c.parent_of(src), "db", "loom_tasks#" + row.id, "task", row.updated);
    n["status"] = row.status;
    n["value"] = row.status;
    n["attrs"] = Json{{"kind", row.kind}, {"created", row.created}, {"updated", row.updated}, {"attempts", row.attempts}};
    if (!row.error.empty()) n["attrs"]["error"] = row.error;
    r.nodes.push_back(std::move(n));
  }
  r.total = total;
  if (only.empty()) {
    r.page = Json{{"limit", limit}, {"offset", offset}, {"total", total}};
    if (limit > 0 && offset + limit < total) r.page["next_offset"] = offset + limit;
  }
  return r;
}

// Every entry in calibration (graph_entry::calibration_entries, the same walk as the counter of the area).
Result<Read> read_calibration(Impl& c, const Json& src, const std::string&, const Json&) {
  std::map<std::string, Json> values;
  Json unreadable = Json::array();
  const Json entries = graph_entry::calibration_entries(
      [&](const std::string& domain) -> const Json* {
        auto it = values.find(domain);
        if (it == values.end()) {
          auto p = RuntimeProfile::load(domain, c.env.root);
          if (!p) log::write(log::Level::Debug, kLog, "calibration: profile " + domain + " not readable: " + p.error().message);
          it = values.emplace(domain, p ? p->values() : Json(nullptr)).first;
        }
        return it->second.is_null() ? nullptr : &it->second;
      },
      &unreadable);
  Read r;
  const std::string prefix = json::get_string(src, "prefix");
  std::set<std::string> seen;
  for (const auto& e : entries) {
    const std::string where = json::get_string(e, "where"), pointer = json::get_string(e, "pointer");
    std::string address = prefix + where + (pointer.empty() ? std::string() : "#" + pointer);
    if (!seen.insert(address).second) continue;
    std::string label = c.say(e.value("label", Json()));
    if (label.empty() && pointer.empty()) label = c.word("calibration.whole_pack", Json{{"pack", where}});
    if (label.empty()) label = pointer.substr(pointer.rfind('/') + 1);
    if (label.empty()) label = where;
    Json n = c.make(src, address, label, c.parent_of(src), "rom", where + (pointer.empty() ? std::string() : "#" + pointer), "preset");
    n["value"] = "calibration";
    n["attrs"] = Json{{"where", where}, {"pointer", pointer}};
    if (e.contains("target")) n["attrs"]["target"] = e["target"];
    r.nodes.push_back(std::move(n));
  }
  if (!unreadable.empty()) r.page = Json{{"unreadable", unreadable}};
  return r;
}

// Nodes that live elsewhere, shown here too (params.addresses, and params.settings {subgroup}): the same address and node; its own
// place stays its parent for the breadcrumbs (attrs.home).
Result<Read> read_also(Impl& c, const Json& src, const std::string&, const Json&) {
  Read r;
  const std::string here = c.parent_of(src);
  const Json params = src.value("params", Json::object());
  std::vector<Json> found;
  for (const auto& a : strings_in(params.value("addresses", Json::array()))) {
    LOOM_TRY_ASSIGN(auto n, c.node_at(a));
    if (n) found.push_back(std::move(*n));
    else log::write(log::Level::Debug, kLog, "also: no node at " + a);
  }
  if (const Json where = params.value("settings", Json::object()); where.is_object() && !where.empty())
    if (const Json* fields_src = c.source("setting")) {
      LOOM_TRY_ASSIGN(const Read* all, c.read_whole(*fields_src));
      for (const auto& n : all->nodes) {
        const Json human = n.value("attrs", Json::object()).value("human", Json::object());
        bool fits = true;
        for (const auto& w : where.items()) fits = fits && human.value(w.key(), Json()) == w.value();
        if (fits) found.push_back(n);
      }
    }
  for (auto& n : found) {
    n["attrs"]["home"] = n.value("parent", Json(nullptr));
    n["parent"] = here;
    n["shown_also_in"] = here;
    r.nodes.push_back(std::move(n));
  }
  return r;
}

// The nodes the importer wrote (IMPORT_JAKO_PLIKI_W_GRAFIE P1, src/import/import_graph.h): rows of the nodes table whose metadata carries
// params.metadata_key. Each node keeps its own class (metadata.<key>.class); the tree follows metadata.<key>.nav_parent (empty = a root
// under the area); the links between these nodes are the edges; a conversation node is the same node as the conversation it describes.
Result<Read> read_imports(Impl& c, const Json& src, const std::string&, const Json&) {
  if (!c.env.db) return Error(Errc::Unavailable, "database_unavailable");
  const Json params = src.value("params", Json::object());
  const std::string mkey = json::get_string(params, "metadata_key", "import_graph");
  const std::string db_prefix = json::get_string(params, "same_as_prefix", "n_");
  const std::string prefix = json::get_string(src, "prefix");
  const Json* conv_src = c.source("conv");
  const std::string conv_prefix = conv_src ? json::get_string(*conv_src, "prefix") : std::string("conv:");
  const std::string conv_db = conv_src ? json::get_string(*conv_src, "same_as_prefix", "c_") : std::string("c_");
  struct Row {
    std::string id, label, created;
    Json block;
  };
  std::vector<Row> rows;
  struct LinkRow {
    std::string src, dst, type;
  };
  std::vector<LinkRow> links;
  {
    auto lock = c.env.db->lock();
    auto& conn = c.env.db->conn();
    const std::string path = "$." + mkey;
    LOOM_TRY_ASSIGN(auto st, conn.prepare("SELECT id, label, created, json_extract(metadata, ?) FROM nodes WHERE json_valid(metadata) AND "
                                          "json_type(metadata, ?) = 'object' ORDER BY rowid"));
    st.bind(1, path).bind(2, path);
    while (true) {
      LOOM_TRY_ASSIGN(bool more, st.step());
      if (!more) break;
      rows.push_back(Row{st.get_text(0), st.get_text(1), st.get_text(2), json::parse_or(st.get_text(3), Json::object())});
    }
    LOOM_TRY_ASSIGN(auto ls, conn.prepare("SELECT src, dst, link_type FROM links WHERE json_valid(metadata) AND json_extract(metadata, '$.by') = 'import_graph'"));
    while (true) {
      LOOM_TRY_ASSIGN(bool more, ls.step());
      if (!more) break;
      links.push_back(LinkRow{ls.get_text(0), ls.get_text(1), ls.get_text(2)});
    }
  }
  std::set<std::string> ids;
  std::map<std::string, std::int64_t> kids;
  for (const auto& row : rows) ids.insert(row.id);
  for (const auto& row : rows) {
    const std::string up = json::get_string(row.block, "nav_parent");
    if (!up.empty() && ids.count(up)) ++kids[up];
  }
  const std::string area = c.parent_of(src);
  Read r;
  for (const auto& row : rows) {
    const std::string address = address_of_db(row.id, db_prefix, prefix);
    const std::string up = json::get_string(row.block, "nav_parent");
    const std::string parent = !up.empty() && ids.count(up) ? address_of_db(up, db_prefix, prefix) : area;
    Json n = c.make(src, address, row.label.empty() ? row.id : row.label, parent, "db", row.id, "import:graph", row.created);
    const std::string cls = json::get_string(row.block, "class");
    if (!cls.empty()) n["kind"] = cls;
    n["same_as"] = Json::array({row.id});
    // the fields whose values are the person's own content (file names, titles, e-mail): the web shows them as content
    if (const Json* pc = json::find(row.block, "person_content"); pc && pc->is_array() && !pc->empty()) n["person_content"] = *pc;
    if (const Json* pv = json::find(row.block, "person_values"); pv && pv->is_array() && !pv->empty()) n["person_values"] = *pv;
    const auto k = kids.find(row.id);
    n["has_children"] = k != kids.end();
    if (k != kids.end()) n["count"] = k->second;
    Json attrs = row.block.value("attrs", Json::object());
    if (!attrs.is_object()) attrs = Json::object();
    attrs["id"] = row.id;
    attrs["class"] = cls;
    if (const Json* f = json::find(row.block, "facets"); f && f->is_array() && !f->empty()) attrs["facets"] = *f;
    const Json conv_ids = attrs.value("conv_ids", Json::array());
    if (conv_ids.is_array())
      for (const auto& cid : conv_ids)
        if (cid.is_string() && starts_with(cid.get<std::string>(), conv_db)) {
          const std::string conv_address = address_of_db(cid.get<std::string>(), conv_db, conv_prefix);
          n["same_as"].push_back(conv_address);
          r.edges.push_back(Json{{"src", address}, {"dst", conv_address}, {"kind", "same_as"}});
        }
    n["attrs"] = std::move(attrs);
    r.nodes.push_back(std::move(n));
  }
  for (const auto& l : links)
    if (ids.count(l.src) && ids.count(l.dst))
      r.edges.push_back(Json{{"src", address_of_db(l.src, db_prefix, prefix)}, {"dst", address_of_db(l.dst, db_prefix, prefix)}, {"kind", l.type}});
  r.total = static_cast<std::int64_t>(r.nodes.size());
  return r;
}

}  // namespace

const std::map<std::string, Impl::Reader>& Impl::readers() {
  static const std::map<std::string, Reader> r = {
      {"graph_entry.areas", read_areas},  {"rom.items", read_rom},          {"settings.groups", read_groups},  {"settings.fields", read_settings},
      {"scopes.chain", read_scopes},      {"ports.roles", read_roles},      {"models.bound", read_models},     {"profile.items", read_profile_items},
      {"config.items", read_config_items}, {"views.effective", read_views}, {"party.self", read_party},         {"user.preferences", read_preferences},
      {"user.privacy", read_privacy},     {"db.topics", read_topics}, {"db.learned", read_learned},       {"memory.notes", read_memory},     {"db.conversations", read_conversations},
      {"db.messages", read_messages},     {"db.units", read_units},         {"actions.catalogue", read_flows}, {"actions.steps", read_steps},
      {"static.nodes", read_static},      {"automations.declared", read_automations}, {"db.tasks", read_tasks}, {"calibration.items", read_calibration},
      {"nodes.also", read_also}, {"party.subgraphs", read_subgraphs}, {"db.imports", read_imports}};
  return r;
}

// What hangs under You moves into the part that claims it (profile.subgraphs members): a node of a source whose parent is You gets the part as its
// parent; a member marked `also` keeps its home and is listed in the part too (shown_also_in). Every claimed node carries the actions of the part.
void regroup_self(Catalogue::Impl& c, const Json& source, Read& r) {
  const std::string sid = json::get_string(source, "id");
  if (sid == "subgraph") return;
  const Json table = c.subgraph_table();  // a copy: nothing below may leave a reference into the cache
  if (!table.is_array() || table.empty()) return;
  std::vector<Json> shown_too;
  for (auto& n : r.nodes) {
    const std::string was = json::get_string(n, "parent");
    for (const auto& sg : table) {
      bool claimed = false, also = false;
      const Json members = sg.value("members", Json::array());
      for (const auto& m : members) {
        if (json::get_string(m, "source") != sid || !fits_where(n, m.value("where", Json::object()))) continue;
        claimed = true;
        also = json::get_bool(m, "also", false);
        break;
      }
      if (!claimed || (!also && was != Impl::kSelf)) continue;
      const std::string part = json::get_string(sg, "id");
      const std::string address = std::string(Impl::kPartPrefix) + part;
      const Json actions = sg.value("actions", Json::array());
      if (also) {
        Json copy = n;
        copy["attrs"]["home"] = was;
        copy["attrs"]["subgraph"] = part;
        copy["parent"] = address;
        copy["shown_also_in"] = address;
        copy["actions"] = self_actions(c, actions, copy);
        n["actions"] = self_actions(c, actions, n);
        shown_too.push_back(std::move(copy));
      } else {
        n["parent"] = address;
        n["attrs"]["subgraph"] = part;
        n["actions"] = self_actions(c, actions, n);
      }
      break;
    }
  }
  for (auto& n : shown_too) r.nodes.push_back(std::move(n));
}

// ── roles ─────────────────────────────────────────────────────────────────────────

namespace {

// The class of a model in words: the pattern or threshold that put it there, else the unknown class; the flow a class gets is said
// only for a policy whose readers act on it (with_flow).
std::string tier_reason(const Json& models, const std::string& model, const std::string& tier, const std::string& lang, bool with_flow) {
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
  const Json unknown = models.value("unknown_tier", Json::object());
  std::string text = pick(unknown.value("reason", Json()), lang);
  if (with_flow)
    if (const std::string flow = pick(unknown.value("flow_note", Json()), lang); !flow.empty()) text += " " + flow;
  return text;
}

Json cost_of(Impl& c, const std::string& model) {
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
  for (const char* k : {"per_million_unknown_reason", "last_7d_unknown_reason"})
    if (cost.contains(k)) cost[std::string(k).substr(0, std::string(k).size() - 6) + "human"] = c.reason_words(cost[k].get<std::string>());
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
  // What could not be read is said (issues), never dropped: a role then shows less, and why.
  Json issues = Json::array();
  const auto note = [&](const std::string& what, const std::string& reason) {
    log::write(log::Level::Debug, kLog, "roles: " + what + " not readable: " + reason);
    issues.push_back(Json{{"what", what}, {"reason", reason}, {"reason_human", c.reason_words(reason)}});
  };
  auto ports_values = RuntimeProfile::load("ports", c.env.root);
  if (!ports_values) note("ports_profile_values", ports_values.error().message);
  const Json roles_data = ports_values ? ports_values->values().value("roles", Json::object()) : Json::object();
  auto shipped = RuntimeProfile::builtin("ports");
  if (!shipped) note("ports_profile_shipped", shipped.error().message);
  const Json shipped_roles = shipped ? shipped->values().value("roles", Json::object()) : Json::object();
  auto policy = annotations::Policy::load_cached(c.env.root);
  if (!policy) note("policy_unavailable", policy.error().message);
  const Json models = policy ? policy->models : Json::object();
  const Json policies = models.value("policies", Json::object());
  const Json src = c.source("role") ? *c.source("role") : Json::object();
  const Json modes = src.value("role_modes", Json::object());
  auto catalogue = action_catalogue(c.env.root);
  if (!catalogue) note("action_catalogue_unavailable", catalogue.error().message);
  const Json actions = catalogue ? catalogue->value("actions", Json::array()) : Json::array();
  auto fields = c.fields();
  if (!fields) note("settings_fields_unavailable", fields.error().message);
  const Json settings = c.env.cfg ? c.env.cfg->all() : Json::object();
  const Json conv_ports = c.conversation_meta().value("ports", Json::object());
  const scopes::Resolver* resolver = c.get_resolver();
  // A step that names a setting (directly, or through a pointer of a runtime profile) reads it as its reader does: at the person level.
  const auto setting_of_step = [&](const Json& step) -> std::string {
    if (const std::string key = json::get_string(step, "setting"); !key.empty()) return key;
    if (const std::string domain = json::get_string(step, "profile"); !domain.empty()) {
      auto prof = RuntimeProfile::load(domain, c.env.root);
      if (!prof) {
        note("today_reads:" + domain, prof.error().message);
        return std::string();
      }
      const Json* at = json::find_pointer(prof->values(), json::get_string(step, "pointer"));
      return at && at->is_string() ? at->get<std::string>() : std::string();
    }
    return std::string();
  };
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
      if (st.contains("reason_human")) shown["reason_human"] = c.say(st["reason_human"]);  // the same in words, for the screen
      row["status"] = shown;
    } else {
      row["status"] = Json{{"state", "implemented"}};
    }
    std::string policy_id;
    for (const auto& p : policies.items())
      if (json::get_string(p.value(), "role") == id) policy_id = p.key();
    row["policy"] = policy_id.empty() ? Json(nullptr) : Json(policy_id);
    const Json pol = policy_id.empty() ? Json::object() : policies[policy_id];
    // A policy is what decides only when a reader reads it (readers); a policy without one describes a chain no job reads yet, and the
    // model is what today's reader reads (today_reads: settings in order, at the person level), else the ports resolution.
    const bool read_by_a_reader = pol.contains("readers") && !pol["readers"].is_null();
    std::string model, via, reason, tier, flow;
    Json today_chain = nullptr;
    if (read_by_a_reader && c.env.cfg) {
      const auto choice = annotations::resolve_model(*c.env.cfg, c.env.root, policy_id, c.env.conv_id, std::string(), -1, c.env.db, ports, resolver);
      model = choice.model;
      via = choice.via;
      if (model.empty()) {
        reason = "no_model";  // the reason code; the policy's own sentence stays as the detail
        if (!choice.reason.empty()) row["reason_detail"] = choice.reason;
      }
      tier = choice.tier;
      flow = choice.flow;
    } else if (const Json reads = pol.value("today_reads", Json()); reads.is_array() && !reads.empty()) {
      today_chain = Json::array();
      for (std::size_t i = 0; i < reads.size(); ++i) {
        const std::string key = setting_of_step(reads[i]);
        const std::string read_role = json::get_string(reads[i], "role");
        // level chain: through the scope chain as the reader does (a conversation or a unit may set it); person: config.json; a role: your
        // choice for it or this conversation's (the ports, never the preset's own model).
        Json v = nullptr;
        if (!read_role.empty()) {
          if (auto res = ports->resolve(read_role, conv_ports, c.env.conv_id, settings); res && json::get_string(*res, "scope") != "installation")
            v = res->value("params", Json::object()).value("model", Json(nullptr));
          else if (!res) log::write(log::Level::Debug, kLog, "today's read of role " + read_role + " not resolved: " + res.error().message);
        } else if (!key.empty() && json::get_string(reads[i], "level", "person") == "chain" && resolver) {
          scopes::Binding b;
          b.key = key;
          b.conv_id = c.env.conv_id;
          if (auto got = resolver->resolve(b); got && got->defined) v = got->value;
          else if (!got) log::write(log::Level::Debug, kLog, "today's read of " + key + " not resolved: " + got.error().message);
        } else if (!key.empty() && c.env.cfg) {
          v = c.env.cfg->get(key);
        }
        const bool gives = model.empty() && v.is_string() && !v.get<std::string>().empty();
        if (gives) {
          model = v.get<std::string>();
          via = read_role.empty() ? "today:" + key : "today:role:" + read_role;
        }
        Json step{{"step", i + 1}, {"setting", key.empty() ? Json(nullptr) : Json(key)}, {"value", v}, {"decides", gives}, {"meaning", c.say(reads[i].value("meaning", Json()))}};
        if (!read_role.empty()) step["role"] = read_role;
        today_chain.push_back(std::move(step));
      }
      if (model.empty()) reason = "no_model";
    } else if (auto res = ports->resolve(id, conv_ports, c.env.conv_id, settings)) {
      const Json m = res->value("params", Json::object()).value("model", Json());
      if (m.is_string()) model = m.get<std::string>();
      via = "ports:" + json::get_string(*res, "scope");
      if (model.empty()) reason = res->contains("compose") ? std::string("composed") : std::string("no_model");
    } else {
      reason = res.error().message;
    }
    row["model"] = model.empty() ? Json(nullptr) : Json(model);
    row["model_human"] = model.empty() ? c.word(reason == "composed" ? "role.composed" : "role.no_model") : (c.env.models ? c.env.models->name(model) : model);
    if (json::get_string(row, "model_human").empty()) row["model_human"] = model.empty() ? reason : model;
    row["via"] = via;
    if (!reason.empty()) {
      row["reason"] = reason;
      row["reason_human"] = c.reason_words(reason);
    }
    // No model: no class (never the unknown class of a model that is not there).
    if (model.empty()) {
      row["tier"] = Json{{"id", "none"}, {"label", c.word("tier.none")}, {"reason", c.word(reason == "composed" ? "tier.composed" : "tier.none_reason")}};
    } else {
      if (tier.empty()) tier = annotations::model_tier(models, model);
      if (tier.empty()) tier = "unknown";
      row["tier"] = Json{{"id", tier}, {"label", c.word("tier." + tier)}, {"reason", tier_reason(models, model, tier, c.lang(), read_by_a_reader)}};
    }
    // The chain of the policy, step by step, with what each step reads today. A chain no reader reads decides nothing.
    Json chain = Json::array();
    const Json steps = pol.value("chain", Json::array());
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
          else if (!r) log::write(log::Level::Debug, kLog, "step setting not resolved: " + r.error().message);
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
      step["decides"] = read_by_a_reader && !via.empty() && (via == mine || (type == "relative_to" && starts_with(via, "relative_to")));
      chain.push_back(std::move(step));
    }
    row["chain"] = chain;
    if (!policy_id.empty()) row["chain_status"] = read_by_a_reader ? "read" : "described_not_read";
    if (!today_chain.is_null()) row["today_chain"] = today_chain;
    // The flow a class of model gets, for the policies whose readers act on it (policies with readers).
    if (!flow.empty() && read_by_a_reader) {
      // The flow of the class the model has now, and (flow_by_class) the flow every class gets: the class of the model decides which
      // flow runs, so the whole map answers "why can I choose this model and what changes with it" without trying models one by one.
      const Json defs = models.value("flow_defs", Json::object());
      const auto flow_row = [&](const std::string& flow_id) {
        const Json def = defs.value(flow_id, Json::object());
        Json fr{{"id", flow_id}, {"status", def.value("status", "")}, {"does", def.value("does", Json::array())}, {"planned", def.value("planned", Json::array())},
                {"calls", def.value("calls", "")}};
        if (const Json* human = json::find(def, "human"); human && human->is_object()) {  // the words of the flow in the language asked
          if (human->contains("title")) fr["title"] = c.say((*human)["title"]);
          for (const char* list : {"does", "planned"}) {
            Json words = Json::array();
            for (const auto& item : human->value(list, Json::array())) words.push_back(c.say(item));
            fr[std::string(list) + "_human"] = words;
          }
        }
        return fr;
      };
      row["flow"] = flow_row(flow);
      Json by_class = Json::array();
      const Json class_flows = models.value("flows", Json::object());  // a named value: items() of a temporary would dangle
      for (const auto& entry : class_flows.items()) {
        if (!entry.value().is_string()) continue;
        by_class.push_back(Json{{"tier", Json{{"id", entry.key()}, {"label", c.word("tier." + entry.key())}}}, {"flow", flow_row(entry.value().get<std::string>())}});
      }
      row["flow_by_class"] = by_class;
    } else {
      row["flow"] = nullptr;
    }
    if (const Json* mode = json::find(modes, id); mode && mode->is_object()) {
      auto worker = RuntimeProfile::load(json::get_string(*mode, "profile", "worker"), c.env.root);
      Json m = Json::object();
      if (worker) {
        const Json* list = json::find_pointer(worker->values(), json::get_string(*mode, "pointer"));
        const Json* threshold = json::find_pointer(worker->values(), json::get_string(*mode, "threshold_pointer"));
        m["modes"] = list ? *list : Json(nullptr);
        m["batch_threshold"] = threshold ? *threshold : Json(nullptr);
      } else {
        note("role_mode:" + id, worker.error().message);
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
    // The other flows that read one of the role's settings without declaring the role (the cost estimate, the Secretary reading
    // semantic_model through its own policy, the screens that show the roles): the owner asked for EVERY flow that reads the key.
    // The list is the actions' own `reads`, audited against their executors by tools/check_action_reads.py.
    Json also = Json::array();
    for (const auto& a : actions) {
      const std::string aid = json::get_string(a, "id");
      bool listed = false;
      for (const auto& r : readers) listed = listed || json::get_string(r, "id") == aid;
      if (listed) continue;
      std::string via_key;
      for (const auto& read : strings_in(a.value("reads", Json())))
        for (const auto& k : keys)
          if (via_key.empty() && json::get_string(k, "key") == read) via_key = read;
      if (!via_key.empty()) also.push_back(Json{{"id", aid}, {"title", catalogue_title(a, c.lang())}, {"key", via_key}});
    }
    row["flows_reading_setting"] = also;
    rows.push_back(std::move(row));
  }
  if (!issues.empty()) out["issues"] = issues;
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
bool Catalogue::subgraph_reads(const std::string& parent, const std::string& source_id) { return impl_->member_reads(parent, source_id); }

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

std::string Catalogue::db_id(std::string_view address) const {
  const std::string a(address);
  for (const auto& f : impl_->identity.value("db_forms", Json::array())) {
    const std::string from = json::get_string(f, "address_prefix");
    if (!from.empty() && starts_with(a, from) && a.size() > from.size()) return json::get_string(f, "db_prefix") + a.substr(from.size());
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
  // A derived node of the perspective sent_to_model: a view of the record stored with the request, not a node of any source.
  if (starts_with(address, "sent:")) {
    LOOM_TRY_ASSIGN(auto row, receipt_node(*this, address));
    if (row) c.finish(*row);
    return row;
  }
  return c.node_at(address);
}

Result<const Json*> Catalogue::receipt_of(const std::string& msg_id) {
  Impl& c = *impl_;
  if (const auto it = c.receipt_cache.find(msg_id); it != c.receipt_cache.end()) return &it->second;
  if (!c.receipt_kinds_cache) {
    LOOM_TRY_ASSIGN(auto persp, views::Perspectives::open(c.env.root));
    LOOM_TRY_ASSIGN(auto ks, persp.edge_kinds_of("sent_to_model"));
    c.receipt_kinds_cache = std::move(ks);
  }
  LOOM_TRY_ASSIGN(auto rendered, render_receipt(*this, *c.receipt_kinds_cache, Json{{"msg_id", msg_id}, {"lang", c.lang()}}));
  // The record belongs to the request: asked by its answer or by itself, one render serves both.
  if (const std::string req = json::get_string(rendered, "request_msg_id"); !req.empty() && req != msg_id) c.receipt_cache.emplace(req, rendered);
  const auto at = c.receipt_cache.emplace(msg_id, std::move(rendered)).first;
  return &at->second;
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
    const std::string under = json::get_string(p, "node");
    // The sources parented to this area, to this node or to the focus's source, and the focus's own source (nodes nested in their own
    // kind: the slots of You, the notes under a note).
    const bool parented = parent.empty() ? json::get_bool(p, "root", false)
                                         : (!area.empty() && parent == "area:" + area) || (!under.empty() && parent == under) || (!of.empty() && of == parent_source) ||
                                               c.member_reads(parent, json::get_string(s, "id"));
    const bool own = !parent.empty() && !parent_source.empty() && json::get_string(s, "id") == parent_source && !Impl::paged(s);
    const bool here = parented || own;
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
      out.nodes.back()["listed_by"] = sid;  // the source that lists it here (a node shown also elsewhere keeps its own source)
      c.finish(out.nodes.back());
      ++mine;
    }
    // What an overlay hid among the children of this parent: a source placed in this area (or at the root) hides its own entries here;
    // the entries of a source read under its parent kind (the fields of a hidden group) are not children of any shown node.
    if (parented && of.empty())
      for (const auto& h : r->hidden) out.hidden.push_back(h);
    const std::int64_t known_here = Impl::paged(s) && r->total >= 0 ? r->total : mine;
    out.total += known_here;
    out.totals[sid] += known_here;
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

Result<std::vector<Json>> Catalogue::edges_touching(const std::string& address, Json* unknown) {
  const Json* own = source_of(address);
  std::set<std::string> ids;
  if (own) {
    const std::string own_id = json::get_string(*own, "id"), prefix = json::get_string(*own, "prefix");
    ids.insert(own_id);
    for (const auto& s : impl_->sources)
      for (const auto& e : s.value("edges", Json::array())) {
        const std::string to = json::get_string(e, "to"), from = json::get_string(e, "from");
        if (to == own_id || (!prefix.empty() && (starts_with(to, prefix) || starts_with(from, prefix)))) ids.insert(json::get_string(s, "id"));
      }
  }
  return edges_of(ids.empty() ? nullptr : &ids, unknown);
}

Result<std::vector<Json>> Catalogue::edges(Json* unknown) { return edges_of(nullptr, unknown); }

Result<std::vector<Json>> Catalogue::edges_of(const std::set<std::string>* only, Json* unknown) {
  Impl& c = *impl_;
  std::vector<Json> out = c.looked_up_edges;
  for (const auto& s : c.sources) {
    if (Impl::paged(s)) continue;
    if (only && !only->count(json::get_string(s, "id"))) continue;
    auto got = c.read_whole(s);
    // A source that cannot be read gives no edges, and says why (never silently: the perspectives show it as unknown).
    const std::string reason = !got ? got.error().message : (*got)->unknown;
    if (!reason.empty()) {
      log::write(log::Level::Debug, kLog, "source " + json::get_string(s, "id") + " gives no edges: " + reason);
      if (unknown && reason != "read_under_parent") unknown->push_back(Json{{"source", json::get_string(s, "id")}, {"reason", reason}});
      if (!got) continue;
    }
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

Json Catalogue::setting(const std::string& key) { return impl_->setting(key); }
std::string Catalogue::reason_words(const std::string& reason) { return impl_->reason_words(reason); }
std::string Catalogue::source_label(const std::string& id) {
  const Json* s = impl_->source(id);
  const std::string label = s ? impl_->say(s->value("label", Json())) : std::string();
  return label.empty() ? id : label;
}

Result<Json> Catalogue::roles() {
  const Json& rows = role_rows(*impl_);
  if (rows.contains("unknown")) return Error(Errc::Unavailable, json::get_string(rows, "unknown"));
  return rows;
}

Json Catalogue::edge_label(const std::string& kind) {
  views::EdgeKinds* k = impl_->edge_kinds();
  const views::EffectiveEntry* e = k ? k->view().find(kind) : nullptr;
  if (!e) return kind;
  std::string label = e->value.contains("label") ? impl_->say(e->value["label"]) : std::string();
  return label.empty() ? Json(kind) : Json(label);
}

Json Catalogue::edge_meaning(const std::string& kind) {
  views::EdgeKinds* k = impl_->edge_kinds();
  if (!k) return nullptr;
  const views::EffectiveEntry* e = k->view().find(kind);
  if (!e || !e->value.contains("meaning")) return nullptr;
  return impl_->say(e->value["meaning"]);
}

std::string Catalogue::edge_inverse(const std::string& kind) {
  views::EdgeKinds* k = impl_->edge_kinds();
  const views::EffectiveEntry* e = k ? k->view().find(kind) : nullptr;
  return e ? json::get_string(e->value, "inverse") : std::string();
}

// ── actions ───────────────────────────────────────────────────────────────────────

namespace {

const std::set<std::string> kRowKeys = {"address", "label", "kind", "value", "value_human", "count", "count_unknown_reason", "provenance", "evidence",
                                        "evidence_overrides", "has_children", "status", "why", "order", "same_as", "source", "empty_reason", "no_translation",
                                        "shown_also_in", "count_unknown_human", "value_unknown_reason", "derived", "derived_note", "receipt", "actions", "state",
                                        "person_content", "person_values"};

Json row_of(const Json& n) {
  Json r = Json::object();
  for (const auto& k : kRowKeys)
    if (n.contains(k)) r[k] = n[k];
  return r;
}

// The budget of one level: the request's, else navigator_children_limit through the scope chain (a rule or the conversation may set it).
std::int64_t children_limit(Catalogue& cat, const Json& request) {
  if (request.contains("limit") && request["limit"].is_number_integer()) return request["limit"].get<std::int64_t>();
  if (const Json v = cat.setting("navigator_children_limit"); v.is_number_integer()) return v.get<std::int64_t>();
  return 50;  // the preset of navigator_children_limit when no settings are known (config.cpp DEFAULTS)
}

// The rows a level shows within its budget: every source keeps a share (one row per source in turn, in source order), so a large source
// never crowds the others out; the rows keep their order. limit <= 0 = no budget.
std::vector<const Json*> within_budget(const std::vector<Json>& nodes, std::int64_t limit) {
  std::vector<std::string> order;
  std::map<std::string, std::vector<const Json*>> by;
  for (const auto& n : nodes) {
    const std::string s = json::get_string(n, "listed_by", json::get_string(n, "source"));
    if (!by.count(s)) order.push_back(s);
    by[s].push_back(&n);
  }
  std::map<std::string, std::size_t> take;
  if (limit <= 0 || static_cast<std::int64_t>(nodes.size()) <= limit) {
    for (const auto& s : order) take[s] = by[s].size();
  } else {
    std::int64_t left = limit;
    for (bool more = true; more && left > 0;) {
      more = false;
      for (const auto& s : order) {
        if (left == 0) break;
        if (take[s] < by[s].size()) {
          ++take[s];
          --left;
          more = true;
        }
      }
    }
  }
  std::vector<const Json*> out;
  for (const auto& s : order)
    for (std::size_t i = 0; i < take[s]; ++i) out.push_back(by[s][i]);
  return out;
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
    if ((starts_with(target, "area:") && "area:" + json::get_string(p, "area") == target) || (!ps_id.empty() && json::get_string(p, "of") == ps_id) ||
        json::get_string(p, "node") == target || cat.subgraph_reads(target, json::get_string(s, "id")))
      any_source = true;
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

namespace {

// How a name in a sentence is compared with the names of the graph (graph_sources.pack match): folded (lower case, the letters of `fold`
// replaced: ą -> a), word by word; two words are the same when equal, or when they share a stem of at least min_prefix letters and
// neither goes on for more than max_suffix letters after it (ustawień = ustawienia, przepływów = przepływy; rzeczy is not przeczyta).
struct NameMatch {
  Json fold_map = Json::object();
  std::size_t min_prefix = 4;
  std::size_t max_suffix = 3;

  std::u32string fold(const std::string& s) const {
    std::u32string out;
    for (char32_t cp : utf8::decode(utf8::to_lower(s))) {
      const Json* r = json::find(fold_map, utf8::encode(cp));
      if (r && r->is_string()) {
        for (char32_t x : utf8::decode(r->get<std::string>())) out.push_back(x);
      } else {
        out.push_back(cp);
      }
    }
    return out;
  }
  std::vector<std::u32string> words(const std::string& s) const {
    std::vector<std::u32string> out;
    std::u32string cur;
    for (char32_t cp : fold(s)) {
      if (unicode::is_word(cp)) {
        cur.push_back(cp);
      } else if (!cur.empty()) {
        out.push_back(cur);
        cur.clear();
      }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
  }
  bool same_word(const std::u32string& a, const std::u32string& b) const {
    if (a == b) return true;
    std::size_t p = 0;
    while (p < a.size() && p < b.size() && a[p] == b[p]) ++p;
    return p >= min_prefix && a.size() - p <= max_suffix && b.size() - p <= max_suffix;
  }
  bool same_words(const std::vector<std::u32string>& a, const std::vector<std::u32string>& b) const {
    if (a.empty() || a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
      if (!same_word(a[i], b[i])) return false;
    return true;
  }
  // Every word of the query is a word of the name (whole words or their stems, never a part of another word).
  bool has_words(const std::vector<std::u32string>& name, const std::vector<std::u32string>& query) const {
    if (query.empty()) return false;
    for (const auto& q : query) {
      bool found = false;
      for (const auto& w : name) found = found || same_word(w, q);
      if (!found) return false;
    }
    return true;
  }
};

}  // namespace

Result<std::string> find_address(const Env& env, const std::string& raw, bool exact_only) {
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
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("graph_sources", env.root));
  const Json match = profile.values().value("match", Json::object());
  NameMatch m;
  m.fold_map = match.value("fold", Json::object());
  m.min_prefix = static_cast<std::size_t>(std::max<std::int64_t>(1, json::get_int(match, "min_prefix", 4)));
  m.max_suffix = static_cast<std::size_t>(std::max<std::int64_t>(0, json::get_int(match, "max_suffix", 3)));
  const auto want = m.words(words);
  const std::string lang = env.lang.empty() ? std::string("en") : env.lang;
  // The words the data gives a node besides its name (aliases: rozmowy -> the conversations, role -> the model roles), in this language.
  // An alias said exactly wins over one that only matches by its ending (importy -> Importy, not import -> Dane i korpusy).
  const Json aliases = profile.values().value("aliases", Json::array());
  for (const bool exact : {true, false})
    for (const auto& a : aliases) {
      const auto alias_words = strings_in(a.value("words", Json::object()).value(lang, Json::array()));
      for (const auto& w : alias_words)
        if (exact ? m.words(w) == want : m.same_words(m.words(w), want)) return json::get_string(a, "address");
    }
  // The pool: the areas (read from the entry profile, no counters), then every node of the sources read whole.
  std::vector<Json> pool;
  if (auto entry = RuntimeProfile::load("graph_entry", env.root)) {
    const Json areas = entry->values().value("areas", Json::object());
    for (const auto& a : areas.items()) {
      LOOM_TRY_ASSIGN(auto n, cat.node("area:" + a.key()));
      if (n) pool.push_back(std::move(*n));
    }
  } else {
    log::write(log::Level::Debug, kLog, "find_address: graph_entry not readable: " + entry.error().message);
  }
  for (const auto& s : cat.sources()) {
    if (Impl::paged(s) || json::get_bool(s.value("parent", Json::object()), "root", false)) continue;
    LOOM_TRY_ASSIGN(auto all, cat.children_of_source(json::get_string(s, "id")));
    for (auto& n : all) pool.push_back(std::move(n));
  }
  auto id_of = [](const std::string& address) { return address.substr(address.find(':') + 1); };
  for (const auto& n : pool)
    if (m.same_words(m.words(json::get_string(n, "label")), want) || m.same_words(m.words(id_of(json::get_string(n, "address"))), want))
      return json::get_string(n, "address");
  if (exact_only) return std::string();
  for (const auto& n : pool)
    if (m.has_words(m.words(json::get_string(n, "label")), want)) return json::get_string(n, "address");
  return std::string();
}

Result<Json> known_names(const Env& env, const std::string& raw) {
  LOOM_TRY_ASSIGN(auto cat, Catalogue::open(env));
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("graph_sources", env.root));
  const Json values = profile.values();
  const Json match = values.value("match", Json::object());
  NameMatch m;
  m.fold_map = match.value("fold", Json::object());
  const Json cfg = values.value("unknown_names", Json::object());
  const std::size_t show = static_cast<std::size_t>(std::max<std::int64_t>(0, json::get_int(cfg, "show", 6)));
  const std::size_t near_prefix = static_cast<std::size_t>(std::max<std::int64_t>(1, json::get_int(cfg, "near_prefix", 3)));
  const auto said = m.words(raw);
  const std::string lang = env.lang.empty() ? std::string("en") : env.lang;
  struct Row {
    Json node;
    std::vector<std::string> words;
    std::size_t near = 0;
    std::int64_t order = 1000;
  };
  std::vector<Row> rows;
  if (auto entry = RuntimeProfile::load("graph_entry", env.root)) {
    const Json areas = entry->values().value("areas", Json::object());
    for (const auto& a : areas.items()) {
      LOOM_TRY_ASSIGN(auto n, cat.node("area:" + a.key()));
      if (!n) continue;
      Row r;
      r.order = json::get_int(*n, "order", 1000);
      r.node = std::move(*n);
      rows.push_back(std::move(r));
    }
  } else {
    log::write(log::Level::Debug, kLog, "known_names: graph_entry not readable: " + entry.error().message);
  }
  const Json aliases = values.value("aliases", Json::array());
  for (const auto& a : aliases) {
    const std::string address = json::get_string(a, "address");
    const Json by_lang = a.value("words", Json::object());
    const Json in_lang = by_lang.value(lang, Json::array());
    for (auto& r : rows)
      if (json::get_string(r.node, "address") == address)
        for (const auto& w : strings_in(in_lang)) r.words.push_back(w);
  }
  for (auto& r : rows) {
    std::vector<std::string> spoken = r.words;
    spoken.push_back(json::get_string(r.node, "label"));
    for (const auto& name : spoken)
      for (const auto& nw : m.words(name))
        for (const auto& qw : said) {
          std::size_t p = 0;
          while (p < nw.size() && p < qw.size() && nw[p] == qw[p]) ++p;
          r.near = std::max(r.near, p);
        }
  }
  std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.near != b.near ? a.near > b.near : a.order < b.order; });
  const std::size_t total = rows.size();
  const std::size_t shown = show == 0 ? total : std::min(show, total);
  Json names = Json::array();
  for (std::size_t i = 0; i < shown; ++i)
    names.push_back(Json{{"address", json::get_string(rows[i].node, "address")}, {"label", json::get_string(rows[i].node, "label")}, {"words", rows[i].words}});
  return Json{{"names", names}, {"total", total}, {"shown", shown}, {"nearest", !rows.empty() && rows.front().near >= near_prefix}};
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
  const std::int64_t limit = children_limit(cat, request);
  const std::int64_t hops = std::max<std::int64_t>(1, json::get_int(request, "hops", 1));
  const std::string query = folded(json::get_string(request, "query"));
  const std::vector<std::string> perspectives = strings_in(request.value("perspectives", Json::array()));
  // The perspectives asked for: the edge kinds they select; those whose source is the record of a request ("receipt", sent_to_model) are
  // drawn from that record (render_receipt), not from the catalogue's edges (M1-K1c3a).
  std::set<std::string> kinds, receipt_kinds;
  Json perspective_info = Json::array();
  if (!perspectives.empty()) {
    LOOM_TRY_ASSIGN(auto persp, views::Perspectives::open(env.root));
    for (const auto& p : perspectives) {
      LOOM_TRY_ASSIGN(auto ks, persp.edge_kinds_of(p));
      kinds.insert(ks.begin(), ks.end());
      perspective_info.push_back(p);
      const auto* entry = persp.view().find(p);
      if (entry && json::get_string(entry->value, "source") == "receipt") receipt_kinds.insert(ks.begin(), ks.end());
    }
  }
  // The record of the request in view: an answer hands over to the request that the record belongs to (said in handed_from).
  const Json* receipt_render = nullptr;
  Json receipt_info = nullptr;
  if (!receipt_kinds.empty()) {
    if (!starts_with(focus_address, "msg:")) {
      receipt_info = Json{{"present", false}, {"missing_reason", "focus_not_a_message"}, {"text", receipt_words(env, "sent_focus_not_message")}};
    } else {
      LOOM_TRY_ASSIGN(receipt_render, cat.receipt_of(cat.db_id(focus_address)));
      const std::string request_address = json::get_string(receipt_render->value("focus", Json::object()), "address");
      if (json::get_bool(receipt_render->value("receipt", Json::object()), "present", false) && !request_address.empty() && request_address != focus_address) {
        if (handed_from.empty()) handed_from = focus_address;
        focus_address = request_address;
        LOOM_TRY_ASSIGN(focus, cat.node(focus_address));
        if (!focus) return Error(Errc::NotFound, "the answer " + handed_from + " belongs to the request '" + focus_address + "', which no source knows");
      }
    }
  }
  Json paging = Json::object();
  if (request.contains("offset")) paging["offset"] = request["offset"];
  if (request.contains("before")) paging["before"] = request["before"];
  if (limit > 0 && query.empty()) paging["page_cap"] = limit;  // a query filters the whole page

  // The first level within its budget (and the query, which looks at this level); each deeper level only under a parent shown, within
  // the budget of that parent (the budget is per level and parent).
  std::vector<Json> nodes;  // with depth
  std::vector<Json> edges;
  Json unknown = Json::array(), hidden = Json::array(), pages = Json::object();
  std::int64_t total = 0;        // the first level: every child the sources know, or every match of the query
  std::int64_t shown_first = 0;  // the first level shown
  Json by_source = Json::object();  // the first level of the sources the budget collapsed: source -> {label, total, shown}
  std::set<std::string> seen{focus_address};
  const auto matches = [&](const Json& n) {
    return query.empty() || folded(json::get_string(n, "label")).find(query) != std::string::npos ||
           folded(json::get_string(n, "address")).find(query) != std::string::npos;
  };
  // The first level's rows per source: what each source has here and what the budget shows of it.
  const auto collapsed_by_source = [&](const std::map<std::string, std::int64_t>& totals, const std::vector<const Json*>& chosen) {
    std::map<std::string, std::int64_t> shown_by;
    for (const Json* n : chosen) shown_by[json::get_string(*n, "listed_by", json::get_string(*n, "source"))] += 1;
    for (const auto& [sid, t] : totals) {
      if (t <= shown_by[sid]) continue;
      const Json* src = nullptr;
      for (const auto& s : cat.sources())
        if (json::get_string(s, "id") == sid) src = &s;
      by_source[sid] = Json{{"label", src ? pick(src->value("label", Json(sid)), env.lang.empty() ? std::string("en") : env.lang) : sid}, {"total", t},
                            {"shown", shown_by[sid]}};
    }
  };
  if (!perspectives.empty()) {
    // The perspectives choose the edges: the nodes they reach from the focus, `hops` far, in both directions.
    LOOM_TRY_ASSIGN(auto all, cat.edges(&unknown));
    LOOM_TRY_ASSIGN(auto here, cat.children(focus_address, paging));
    for (const auto& e : here.edges) all.push_back(e);
    // The links of the record of the request (sent_to_model, left_out_of_model): weight = the tokens, metadata = class, why, reason.
    if (receipt_render)
      for (auto& e : receipt_edges(*receipt_render, receipt_kinds)) all.push_back(std::move(e));
    std::vector<Json> reached;  // breadth first, each with its depth and the node it was reached from (via)
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
        one["via"] = cur;
        reached.push_back(std::move(one));
        todo.push_back({next, depth + 1});
      }
    }
    std::vector<Json> first;
    std::map<std::string, std::int64_t> totals;
    for (const auto& n : reached)
      if (json::get_int(n, "depth", 1) == 1 && matches(n)) {
        totals[json::get_string(n, "source")] += 1;
        first.push_back(n);
      }
    total = static_cast<std::int64_t>(first.size());
    const auto chosen = within_budget(first, limit);
    shown_first = static_cast<std::int64_t>(chosen.size());
    collapsed_by_source(totals, chosen);
    std::set<std::string> kept{focus_address};
    for (const Json* n : chosen) {
      kept.insert(json::get_string(*n, "address"));
      nodes.push_back(*n);
    }
    std::map<std::string, std::int64_t> per_parent;
    for (const auto& n : reached) {
      const std::string via = json::get_string(n, "via");
      if (json::get_int(n, "depth", 1) == 1 || !kept.count(via) || (limit > 0 && per_parent[via] >= limit)) continue;
      ++per_parent[via];
      kept.insert(json::get_string(n, "address"));
      nodes.push_back(n);
    }
  } else {
    // Containment: the children of the focus, and theirs up to `hops`, each level read for the parents shown.
    std::vector<std::string> level{focus_address};
    for (std::int64_t depth = 0; depth < hops && !level.empty(); ++depth) {
      std::vector<std::string> next;
      for (const auto& at : level) {
        Json level_paging = depth == 0 ? paging : Json::object();
        if (depth > 0 && limit > 0) level_paging["page_cap"] = limit;
        LOOM_TRY_ASSIGN(auto kids, cat.children(at, level_paging));
        std::vector<Json> fresh;
        std::map<std::string, std::int64_t> matched;
        for (auto& n : kids.nodes) {
          if (seen.count(json::get_string(n, "address")) || (depth == 0 && !matches(n))) continue;
          matched[json::get_string(n, "listed_by", json::get_string(n, "source"))] += 1;
          fresh.push_back(std::move(n));
        }
        const auto chosen = within_budget(fresh, limit);
        if (depth == 0) {
          total = query.empty() ? kids.total : static_cast<std::int64_t>(fresh.size());
          unknown = kids.unknown;
          hidden = kids.hidden;
          pages = kids.pages;
          shown_first = static_cast<std::int64_t>(chosen.size());
          collapsed_by_source(query.empty() ? kids.totals : matched, chosen);
        }
        for (const auto& e : kids.edges) edges.push_back(e);
        for (const Json* p : chosen) {
          const std::string a = json::get_string(*p, "address");
          if (!seen.insert(a).second) continue;
          const Json* src = cat.source_of(a);
          edges.push_back(Json{{"src", a}, {"dst", at}, {"kind", src ? json::get_string(*src, "contain_edge", "part_of") : std::string("part_of")}});
          Json one = *p;
          one["depth"] = depth + 1;
          if (json::get_bool(*p, "has_children", false) || starts_with(a, "area:")) next.push_back(a);
          nodes.push_back(std::move(one));
        }
      }
      level = std::move(next);
    }
  }
  Json children = Json::array();
  std::set<std::string> shown{focus_address};
  for (const auto& n : nodes) {
    Json row = row_of(n);
    row["depth"] = n.value("depth", 1);
    if (const Json attrs = n.value("attrs", Json::object()); attrs.contains("counts")) {
      row["counts"] = attrs["counts"];
      if (attrs.contains("evidence_classes")) row["evidence_classes"] = attrs["evidence_classes"];
    }
    LOOM_TRY(fill_count(cat, row, n));
    if (const std::string why = json::get_string(row, "count_unknown_reason"); !why.empty()) row["count_unknown_human"] = cat.reason_words(why);
    shown.insert(json::get_string(n, "address"));
    children.push_back(std::move(row));
  }
  Json edge_rows = Json::array();
  std::set<std::string> edge_seen;
  for (const auto& e : edges) {
    const std::string s = json::get_string(e, "src"), d = json::get_string(e, "dst"), k = json::get_string(e, "kind");
    if (!shown.count(s) || !shown.count(d)) continue;  // both ends drawn; the card lists every relation
    // a link of the record is one per item (the same element may be listed twice): told apart by its id
    if (!edge_seen.insert(s + "\x1f" + d + "\x1f" + k + "\x1f" + json::get_string(e, "id")).second) continue;
    Json row{{"src", s}, {"dst", d}, {"kind", k}, {"label", cat.edge_label(k)}, {"meaning", cat.edge_meaning(k)}};
    for (const char* extra : {"weight", "metadata", "derived"})
      if (e.contains(extra)) row[extra] = e[extra];
    edge_rows.push_back(std::move(row));
  }
  LOOM_TRY_ASSIGN(auto crumbs, breadcrumbs(cat, *focus));
  // What could not be said, in words: the source by its name, the reason by its word.
  for (auto& u : unknown)
    if (u.is_object()) {
      u["source_label"] = cat.source_label(json::get_string(u, "source"));
      u["reason_human"] = cat.reason_words(json::get_string(u, "reason"));
    }
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
           {"counts", Json{{"total", total}, {"shown", shown_first}, {"collapsed", std::max<std::int64_t>(0, total - shown_first)}, {"by_source", by_source}}},
           {"hidden_by_you", Json{{"count", hidden.size()}, {"items", hidden}}},
           {"unknown", unknown},
           {"pages", pages},
           {"limit", Json{{"value", limit}, {"from", request.contains("limit") ? "request" : "navigator_children_limit"}}},
           {"hops", hops},
           {"resolver_opens", cat.resolver_opens()}};
  // the person's own values inside the focus label (a file name ...): the web shows them as content (import_graph person_content)
  if (const Json* pv = json::find(*focus, "person_values"); pv && pv->is_array() && !pv->empty()) out["focus"]["person_values"] = *pv;
  if (!handed_from.empty()) out["handed_from"] = handed_from;
  if (!perspective_info.empty()) out["perspectives"] = perspective_info;
  if (receipt_render) receipt_info = receipt_summary(*receipt_render);
  if (!receipt_info.is_null()) out["receipt"] = receipt_info;
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
  // Relations grouped by edge kind, both directions: from the node's own source, the sources whose edges reach it, and its children.
  Json relations_unknown = Json::array();
  LOOM_TRY_ASSIGN(auto all, cat.edges_touching(address, &relations_unknown));
  // A derived node of the record of a request (sent:<request id>:<ref>): its relations are the links of that record.
  if (source == "receipt") {
    const std::string rest = address.substr(5);
    if (auto drawn = cat.receipt_of(rest.substr(0, rest.find(':'))); drawn)
      for (auto& e : receipt_edges(**drawn, {})) all.push_back(std::move(e));
    else relations_unknown.push_back(Json{{"source", "receipt"}, {"reason", drawn.error().message}});
  }
  LOOM_TRY_ASSIGN(auto kids, cat.children(address, Json{{"page_limit", 50}}));
  for (const auto& e : kids.edges) all.push_back(e);
  // (not for a derived node of the record of a request: it is not "part of" the message, its relation is the link of the record, with its weight)
  if (const std::string parent = json::get_string(*node, "parent"); !parent.empty() && source != "receipt") {
    const Json* own = cat.source_of(address);
    all.push_back(Json{{"src", address}, {"dst", parent}, {"kind", own ? json::get_string(*own, "contain_edge", "part_of") : std::string("part_of")}});
  }
  Json relations = Json::object();
  std::set<std::string> rel_seen;
  for (const auto& e : all) {
    const std::string s = json::get_string(e, "src"), d = json::get_string(e, "dst"), k = json::get_string(e, "kind");
    if (s != address && d != address) continue;
    if (!rel_seen.insert(s + "\x1f" + d + "\x1f" + k).second) continue;
    if (!relations.contains(k)) {
      // M1-W0 r1:b5: the card heads the in-rows with the inverse kind's name ("zawiera" for part_of), so a group names the concrete node
      const std::string inv = cat.edge_inverse(k);
      relations[k] = Json{{"label", cat.edge_label(k)}, {"meaning", cat.edge_meaning(k)}, {"inverse", inv.empty() ? Json(nullptr) : Json(inv)},
                          {"inverse_label", inv.empty() ? Json(nullptr) : cat.edge_label(inv)}, {"out", Json::array()}, {"in", Json::array()}};
    }
    const std::string other = s == address ? d : s;
    std::string label = other;
    if (auto n = cat.node(other); n && *n) label = json::get_string(**n, "label", other);
    Json related{{"address", other}, {"label", label}};
    if (e.contains("weight")) related["weight"] = e["weight"];  // a link of the record: the tokens (sent_to_model) or the count (left_out_of_model)
    relations[k][s == address ? "out" : "in"].push_back(std::move(related));
  }
  out["relations"] = relations;
  if (!relations_unknown.empty()) out["relations_unknown"] = relations_unknown;
  out["children_count"] = kids.total;
  // What can be done with it: actions.for on its element.
  // What can be done with it: actions.for on its element; each action with its decision (the rules' status: applicable, needs_confirmation,
  // denied, default), its title in the language asked and whether it is implemented. A ROM item also offers your copy (rom.override),
  // decided by the rules (confirm:rom.override).
  const std::string lang = env.lang.empty() ? std::string("en") : env.lang;
  std::map<std::string, const Json*> by_id;  // pointers into all_actions, which outlives them
  auto catalogue = action_catalogue(env.root);
  const Json all_actions = catalogue ? catalogue->value("actions", Json::array()) : Json::array();
  if (catalogue)
    for (const auto& a : all_actions) by_id[json::get_string(a, "id")] = &a;
  else log::write(log::Level::Debug, kLog, "card: action catalogue not readable: " + catalogue.error().message);
  const auto action_row = [&](const std::string& id, const Json& decision, const Json& implementation) {
    const auto it = by_id.find(id);
    std::string label = it != by_id.end() ? catalogue_title(*it->second, lang).get<std::string>() : std::string();
    if (label.empty()) label = id;
    return Json{{"id", id}, {"decision", decision}, {"label", label},
                {"implementation", implementation.is_null() && it != by_id.end() ? it->second->value("status", Json(nullptr)) : implementation}};
  };
  const actions::ElementSources sources{env.db, nullptr, env.root};
  auto fits = actions::invoke(env.root, "for", Json{{"element", address}}, &sources, env.cfg);
  Json acts = Json::array();
  if (fits) {
    for (const auto& a : fits->value("actions", Json::array()))
      acts.push_back(action_row(json::get_string(a, "action", json::get_string(a, "id")), a.value("status", Json(nullptr)), a.value("implementation", Json(nullptr))));
  } else {
    out["actions_unknown_reason"] = fits.error().message;
  }
  if (source == "rom") {
    bool listed = false;
    for (const auto& a : acts) listed = listed || json::get_string(a, "id") == "rom.override";
    if (!listed) {
      Json decision = "needs_confirmation";
      if (auto engine = actions::Engine::load(env.root)) {
        actions::Element element;
        element.id = address;
        element.cls = json::get_string(*node, "kind");
        if (auto scope = engine->parse_scope(Json(nullptr)); scope)
          if (auto d = engine->decide(element, "rom.override", *scope); d) decision = d->status;
      } else {
        log::write(log::Level::Debug, kLog, "card: rules not readable: " + engine.error().message);
      }
      acts.push_back(action_row("rom.override", decision, Json(nullptr)));
    }
  }
  // The actions the parts of You offer on this node (profile.subgraphs, with their arguments) come first; the class's own follow, none twice.
  {
    Json merged = Json::array();
    std::set<std::string> listed;
    for (const auto& a : node->value("actions", Json::array())) {
      listed.insert(json::get_string(a, "id"));
      merged.push_back(a);
    }
    for (const auto& a : acts)
      if (listed.insert(json::get_string(a, "id")).second) merged.push_back(a);
    acts = std::move(merged);
  }
  out["actions"] = acts;
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
  // Every field through the one Resolver of this render; the flows that read a field named by their titles (the catalogue read once).
  std::map<std::string, Json> titles;
  if (auto catalogue = action_catalogue(e.root))
    for (const auto& a : catalogue->value("actions", Json::array())) titles[json::get_string(a, "id")] = catalogue_title(a, e.lang.empty() ? std::string("en") : e.lang);
  else log::write(log::Level::Debug, kLog, "settings.resolved: action catalogue not readable: " + catalogue.error().message);
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
      for (const auto& id : strings_in(human.value("flows", Json::array()))) {
        const auto it = titles.find(id);
        flows.push_back(Json{{"id", id}, {"title", it != titles.end() ? it->second : Json(id)}});
      }
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
  Json out{{"schema", "loom.roles_resolved/1"},
           {"conv_id", e.conv_id.empty() ? Json(nullptr) : Json(e.conv_id)},
           {"roles", rows.value("roles", Json::array())},
           {"resolver_opens", cat.resolver_opens()}};
  if (rows.contains("issues")) out["issues"] = rows["issues"];  // what could not be read, said (the roles then show less, and why)
  return out;
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
  Json unknown = Json::array();
  LOOM_TRY_ASSIGN(auto all, cat.edges(&unknown));
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
    edges.push_back(Json{{"src", s}, {"dst", d}, {"type", k}, {"kind", k}, {"label", cat.edge_label(k)}, {"meaning", cat.edge_meaning(k)}});
  }
  Json ks = Json::array();
  for (const auto& k : kinds) ks.push_back(k);
  Json out{{"schema", "loom.perspective_render/1"}, {"source", "catalogue"}, {"edge_kinds", ks}, {"direction", direction}, {"hops", hops_j}, {"nodes", nodes}, {"edges", edges}};
  if (truncated) out["truncated"] = true;
  if (!unknown.empty()) out["unknown"] = unknown;
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
