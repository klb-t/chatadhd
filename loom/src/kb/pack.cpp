// kb.h: data pack loading + validation (schema per file, closed sets,
// cross-file references). The validator IS the schema: pack files are data,
// what they may say is an invariant enforced here.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

#include "loom/kb.h"
#include "loom/model.h"
#include "loom/re/regex.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"

namespace loom::kb {

namespace {

#include "kb/pack_embedded.inc"

namespace fs = std::filesystem;

using Docs = std::map<std::string, Json, std::less<>>;

// ── helpers ─────────────────────────────────────────────────────────
struct V {
  std::vector<PackIssue>& issues;
  std::string file;

  void err(const std::string& ptr, const std::string& msg) { issues.push_back(PackIssue{file, ptr, msg}); }

  const Json* member(const Json& o, const std::string& ptr, std::string_view key, bool required = true) {
    if (!o.is_object()) {
      err(ptr, "expected an object");
      return nullptr;
    }
    const Json* m = json::find(o, key);
    if (!m && required) err(ptr + "/" + std::string(key), "missing required key");
    return m;
  }
  bool str(const Json& o, const std::string& ptr, std::string_view key, bool required = true, bool nonempty = true) {
    const Json* m = member(o, ptr, key, required);
    if (!m) return !required;
    if (!m->is_string() || (nonempty && m->get<std::string>().empty())) {
      err(ptr + "/" + std::string(key), nonempty ? "expected a non-empty string" : "expected a string");
      return false;
    }
    return true;
  }
  bool num(const Json& o, const std::string& ptr, std::string_view key, double lo, double hi, bool required = true) {
    const Json* m = member(o, ptr, key, required);
    if (!m) return !required;
    if (!m->is_number()) {
      err(ptr + "/" + std::string(key), "expected a number");
      return false;
    }
    double v = m->get<double>();
    if (!(v >= lo && v <= hi)) {
      err(ptr + "/" + std::string(key), "out of range [" + json::format_float_py(lo) + ", " + json::format_float_py(hi) + "]");
      return false;
    }
    return true;
  }
  bool integer(const Json& o, const std::string& ptr, std::string_view key, long lo, long hi, bool required = true) {
    const Json* m = member(o, ptr, key, required);
    if (!m) return !required;
    if (!m->is_number_integer() || m->get<long>() < lo || m->get<long>() > hi) {
      err(ptr + "/" + std::string(key), "expected an integer in [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
      return false;
    }
    return true;
  }
  const Json* array(const Json& o, const std::string& ptr, std::string_view key, bool required = true,
                    bool nonempty = false) {
    const Json* m = member(o, ptr, key, required);
    if (!m) return nullptr;
    if (!m->is_array() || (nonempty && m->empty())) {
      err(ptr + "/" + std::string(key), nonempty ? "expected a non-empty array" : "expected an array");
      return nullptr;
    }
    return m;
  }
  const Json* object(const Json& o, const std::string& ptr, std::string_view key, bool required = true) {
    const Json* m = member(o, ptr, key, required);
    if (!m) return nullptr;
    if (!m->is_object()) {
      err(ptr + "/" + std::string(key), "expected an object");
      return nullptr;
    }
    return m;
  }
  bool strings(const Json& o, const std::string& ptr, std::string_view key, bool required = true,
               bool nonempty = false) {
    const Json* a = array(o, ptr, key, required, nonempty);
    if (!a) return !required && !json::find(o, key);
    bool ok = true;
    for (std::size_t i = 0; i < a->size(); ++i) {
      if (!(*a)[i].is_string() || (*a)[i].get<std::string>().empty()) {
        err(ptr + "/" + std::string(key) + "/" + std::to_string(i), "expected a non-empty string");
        ok = false;
      }
    }
    return ok;
  }
  void bilingual(const Json& o, const std::string& ptr, std::string_view key, bool required = true) {
    const Json* m = object(o, ptr, key, required);
    if (!m) return;
    str(*m, ptr + "/" + std::string(key), "en");
    str(*m, ptr + "/" + std::string(key), "pl");
  }
  void regex(const Json& o, const std::string& ptr, std::string_view key, bool required = true) {
    if (!str(o, ptr, key, required)) return;
    const Json* m = json::find(o, key);
    if (!m) return;
    auto r = re::Regex::compile(m->get<std::string>());
    if (!r) err(ptr + "/" + std::string(key), "invalid regex: " + r.error().message);
  }
  void one_of(const Json& o, const std::string& ptr, std::string_view key, const std::set<std::string>& allowed,
              bool required = true) {
    if (!str(o, ptr, key, required)) return;
    const Json* m = json::find(o, key);
    if (m && !allowed.count(m->get<std::string>())) {
      err(ptr + "/" + std::string(key), "unknown value '" + m->get<std::string>() + "'");
    }
  }
  template <std::size_t N>
  void closed(const Json& o, const std::string& ptr, std::string_view key, const std::string_view (&set)[N],
              bool required = true) {
    if (!str(o, ptr, key, required)) return;
    const Json* m = json::find(o, key);
    if (m && !in_closed_set(m->get<std::string>(), set)) {
      err(ptr + "/" + std::string(key), "'" + m->get<std::string>() + "' is not in the closed set implemented in code");
    }
  }
};

std::string idx(const std::string& ptr, std::size_t i) { return ptr + "/" + std::to_string(i); }

std::set<std::string> names_of(const Json& arr, std::string_view key) {
  std::set<std::string> out;
  if (!arr.is_array()) return out;
  for (const auto& x : arr) {
    std::string n = json::get_string(x, key);
    if (!n.empty()) out.insert(n);
  }
  return out;
}

std::vector<std::string> json_strings_of(const Json& o, std::string_view key) {
  std::vector<std::string> out;
  const Json* a = json::find(o, key);
  if (!a || !a->is_array()) return out;
  for (const auto& x : *a) {
    if (x.is_string()) out.push_back(x.get<std::string>());
  }
  return out;
}

// Member or null (never inserts, never asserts on const objects).
const Json& at(const Json& o, std::string_view key) {
  static const Json kNull;
  const Json* p = json::find(o, key);
  return p ? *p : kNull;
}

std::vector<std::string> split(std::string_view s, char sep) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (true) {
    std::size_t p = s.find(sep, start);
    out.emplace_back(s.substr(start, p == std::string_view::npos ? std::string_view::npos : p - start));
    if (p == std::string_view::npos) break;
    start = p + 1;
  }
  return out;
}

// Types vocabulary (schema/types.json) used by structural checks.
struct TypesVocab {
  std::set<std::string> kinds;
  std::set<std::string> relations;
  std::set<std::string> categories;
  std::map<std::string, std::set<std::string>> enums;
  std::map<std::string, std::set<std::string>> temporal;  // temporal slot -> record field names
  explicit TypesVocab(const Json& t) {
    kinds = names_of(json::find(t, "entity_kinds") ? t["entity_kinds"] : Json(), "kind");
    relations = names_of(json::find(t, "relations") ? t["relations"] : Json(), "name");
    if (const Json* r = json::find(t, "relations"); r && r->is_array()) {
      for (const auto& x : *r) {
        categories.insert(json::get_string(x, "category"));
        std::string inv = json::get_string(x, "inverse");
        if (!inv.empty()) relations.insert(inv);
      }
    }
    if (const Json* e = json::find(t, "enums"); e && e->is_object()) {
      for (auto it = e->begin(); it != e->end(); ++it) {
        std::set<std::string> vals;
        if (it.value().is_array()) {
          for (const auto& v : it.value()) {
            if (v.is_string()) vals.insert(v.get<std::string>());
          }
        }
        enums[it.key()] = vals;
      }
    }
    if (const Json* ts = json::find(t, "temporal_slots"); ts && ts->is_array()) {
      for (const auto& s : *ts) {
        std::set<std::string> fields;
        if (const Json* f = json::find(s, "fields"); f && f->is_object()) {
          for (auto it = f->begin(); it != f->end(); ++it) fields.insert(it.key());
        }
        temporal[json::get_string(s, "name")] = fields;
      }
    }
  }
  const std::set<std::string>& enum_values(const std::string& name) const {
    static const std::set<std::string> kEmpty;
    auto it = enums.find(name);
    return it == enums.end() ? kEmpty : it->second;
  }
};
// ── expressions (conditions, value ops, predicates) ─────────────────
template <std::size_t N>
void check_expr(V& v, const Json& e, const std::string& ptr, const std::string_view (&ops)[N], std::string_view what) {
  if (!e.is_object() || !e.contains("op")) {
    v.err(ptr, std::string("expected a ") + std::string(what) + " expression {\"op\",\"args\"}");
    return;
  }
  std::string op = json::get_string(e, "op");
  if (!in_closed_set(op, ops)) {
    v.err(ptr + "/op", "unknown " + std::string(what) + " op '" + op + "' (closed set in include/loom/kb.h)");
  }
  const Json* args = json::find(e, "args");
  if (!args) return;
  if (!args->is_array()) {
    v.err(ptr + "/args", "expected an array");
    return;
  }
  for (std::size_t i = 0; i < args->size(); ++i) {
    const Json& a = (*args)[i];
    std::string p = idx(ptr + "/args", i);
    if (a.is_object() && a.contains("op")) {
      check_expr(v, a, p, ops, what);
    } else if (a.is_string()) {
      const std::string s = a.get<std::string>();
      if (!s.empty() && s[0] == '$') {
        bool ok = false;
        for (auto pre : kRefPrefixes) ok = ok || s.rfind(pre, 0) == 0;
        if (!ok) v.err(p, "unknown reference '" + s + "'");
      }
    } else if (a.is_object()) {
      for (auto it = a.begin(); it != a.end(); ++it) {
        if (it.value().is_string()) {
          const std::string s = it.value().get<std::string>();
          if (!s.empty() && s[0] == '$') {
            bool ok = false;
            for (auto pre : kRefPrefixes) ok = ok || s.rfind(pre, 0) == 0;
            if (!ok) v.err(p + "/" + it.key(), "unknown reference '" + s + "'");
          }
        }
      }
    }
  }
}


// ── per-schema structural validation ────────────────────────────────
bool valid_slot_type(const std::string& type, const TypesVocab& t);
void v_binding(V& v, const Json& b, const std::string& p, const TypesVocab& t);

// Parses `d` with a model type; a parse error becomes an issue.
template <class T>
std::optional<T> parse_model(V& v, const Json& d, const std::string& ptr) {
  auto r = T::from_json(d);
  if (!r) {
    v.err(ptr, r.error().message);
    return std::nullopt;
  }
  return std::move(*r);
}

void v_types(V& v, const Json& d) {
  static const std::set<std::string> kCats = {"structural", "semantic", "temporal", "provenance", "custom"};
  std::set<std::string> seen;
  if (const Json* a = v.array(d, "", "entity_kinds", true, true)) {
    for (std::size_t i = 0; i < a->size(); ++i) {
      std::string p = idx("/entity_kinds", i);
      if (v.str((*a)[i], p, "kind") && !seen.insert((*a)[i]["kind"].get<std::string>()).second) {
        v.err(p + "/kind", "duplicate entity kind");
      }
      v.integer((*a)[i], p, "level", 0, 3);
      v.str((*a)[i], p, "shape", false);
    }
  }
  if (const Json* e = v.object(d, "", "enums")) {
    for (const char* req : {"project_status", "component_status", "version_source", "item_type", "gt_evidence"}) {
      if (!e->contains(req)) v.err(std::string("/enums/") + req, "missing required enum");
    }
    for (auto it = e->begin(); it != e->end(); ++it) {
      if (!it.value().is_array() || it.value().empty()) v.err("/enums/" + it.key(), "expected a non-empty array");
    }
    // component_status is the model's closed set of part statuses (§5).
    if (const Json* cs = json::find(*e, "component_status"); cs && cs->is_array()) {
      std::set<std::string> have;
      for (const auto& x : *cs) have.insert(x.is_string() ? x.get<std::string>() : "");
      std::set<std::string> want;
      for (auto s : model::all<model::StatusValue>()) want.insert(std::string(model::to_string(s)));
      if (have != want) v.err("/enums/component_status", "must list exactly the model statuses: " + model::names_list<model::StatusValue>());
    }
  }
  seen.clear();
  if (const Json* a = v.array(d, "", "relations", true, true)) {
    for (std::size_t i = 0; i < a->size(); ++i) {
      std::string p = idx("/relations", i);
      if (v.str((*a)[i], p, "name") && !seen.insert((*a)[i]["name"].get<std::string>()).second) {
        v.err(p + "/name", "duplicate relation");
      }
      v.str((*a)[i], p, "inverse", false);
      v.one_of((*a)[i], p, "category", kCats);
    }
  }
  TypesVocab t(d);
  seen.clear();
  if (const Json* a = v.array(d, "", "temporal_slots", true, true)) {
    for (std::size_t i = 0; i < a->size(); ++i) {
      std::string p = idx("/temporal_slots", i);
      auto s = parse_model<model::SlotSpec>(v, (*a)[i], p);
      if (!s) continue;
      if (!seen.insert(s->name).second) v.err(p + "/name", "duplicate temporal slot");
      if (!valid_slot_type(s->type, t)) v.err(p + "/type", "unknown slot type '" + s->type + "'");
      for (auto it = s->fields.begin(); it != s->fields.end(); ++it) {
        std::string ft = json::get_string(it.value(), "type");
        if (!valid_slot_type(ft, t)) v.err(p + "/fields/" + it.key(), "unknown field type '" + ft + "'");
      }
      for (std::size_t k = 0; k < s->bind.size(); ++k) v_binding(v, s->bind[k], idx(p + "/bind", k), t);
    }
  }
}
void v_stopwords(V& v, const Json& d) {
  v.strings(d, "", "en");
  v.strings(d, "", "pl");
  v.strings(d, "", "code", false);
  v.str(d, "", "extends", false);
}

void v_stemming(V& v, const Json& d) {
  if (const Json* f = v.object(d, "", "fold")) {
    for (auto it = f->begin(); it != f->end(); ++it) {
      if (utf8::length(it.key()) != 1 || !it.value().is_string()) v.err("/fold/" + it.key(), "expected one code point -> string");
    }
  }
  for (const char* lang : {"pl", "en"}) {
    const Json* s = v.object(d, "", lang);
    if (!s) continue;
    std::string p = std::string("/") + lang;
    v.integer(*s, p, "min_token", 1, 32);
    v.integer(*s, p, "min_stem", 1, 32);
    v.strings(*s, p, "suffixes", true, true);
    for (const char* key : {"markers", "keep_endings", "verbal", "restore_e"}) v.strings(*s, p, key, false);
    if (const Json* vb = json::find(*s, "verbal"); vb && vb->is_array()) {
      std::set<std::string> sufs;
      if (const Json* sf = json::find(*s, "suffixes"); sf && sf->is_array()) {
        for (const auto& x : *sf) sufs.insert(x.is_string() ? x.get<std::string>() : "");
      }
      for (const auto& x : *vb) {
        if (x.is_string() && !sufs.count(x.get<std::string>())) v.err(p + "/verbal", "verbal suffix is not in suffixes: " + x.get<std::string>());
      }
    }
    if (const Json* r = v.array(*s, p, "rewrite", false)) {
      for (std::size_t i = 0; i < r->size(); ++i) {
        v.str((*r)[i], idx(p + "/rewrite", i), "suffix");
        v.str((*r)[i], idx(p + "/rewrite", i), "to");
      }
    }
    if (const Json* e = v.object(*s, p, "exceptions", false)) {
      for (auto it = e->begin(); it != e->end(); ++it) {
        if (!it.value().is_string()) v.err(p + "/exceptions/" + it.key(), "expected a string");
      }
    }
  }
}

void v_glossary(V& v, const Json& d) {
  if (const Json* a = v.array(d, "", "pairs", true, true)) {
    for (std::size_t i = 0; i < a->size(); ++i) {
      const Json& x = (*a)[i];
      if (!x.is_array() || x.size() != 2 || !x[0].is_string() || !x[1].is_string()) {
        v.err(idx("/pairs", i), "expected [pl, en]");
      }
    }
  }
}

void v_gazetteer(V& v, const Json& d, const TypesVocab& t) {
  std::set<std::string> classes;
  if (const Json* c = v.object(d, "", "classes")) {
    for (auto it = c->begin(); it != c->end(); ++it) {
      classes.insert(it.key());
      std::string p = "/classes/" + it.key();
      v.one_of(it.value(), p, "entity_kind", t.kinds);
    }
    for (auto it = c->begin(); it != c->end(); ++it) {
      std::string parent = json::get_string(it.value(), "parent");
      if (!parent.empty() && !classes.count(parent)) v.err("/classes/" + it.key() + "/parent", "unknown class");
    }
  }
  std::set<std::string> ids;
  std::set<std::string> platform_ids;
  const Json* a = v.array(d, "", "entries", true, true);
  if (!a) return;
  for (const auto& e : *a) {
    if (json::get_string(e, "class") == "platform" || json::get_string(e, "class") == "environment") {
      platform_ids.insert(json::get_string(e, "id"));
    }
  }
  for (std::size_t i = 0; i < a->size(); ++i) {
    const Json& e = (*a)[i];
    std::string p = idx("/entries", i);
    if (v.str(e, p, "id") && !ids.insert(e["id"].get<std::string>()).second) v.err(p + "/id", "duplicate entry id");
    v.one_of(e, p, "class", classes);
    v.bilingual(e, p, "labels");
    v.strings(e, p, "forms", true, true);
    v.strings(e, p, "implies", false);
    if (v.strings(e, p, "platforms", false) && e.contains("platforms")) {
      for (const auto& pl : e["platforms"]) {
        if (pl.is_string() && !platform_ids.count(pl.get<std::string>())) {
          v.err(p + "/platforms", "unknown platform entry '" + pl.get<std::string>() + "'");
        }
      }
    }
    if (e.contains("ambiguous")) {
      v.strings(e, p, "ambiguous");
      v.strings(e, p, "requires_context", true, true);
      std::set<std::string> forms;
      for (const auto& f : e["forms"]) forms.insert(f.is_string() ? f.get<std::string>() : "");
      for (const auto& f : e["ambiguous"]) {
        if (f.is_string() && !forms.count(f.get<std::string>())) v.err(p + "/ambiguous", "not one of the forms: " + f.get<std::string>());
      }
    }
  }
}

void v_item_cues(V& v, const Json& d, const TypesVocab& t) {
  const auto& types = t.enum_values("item_type");
  if (v.strings(d, "", "type_order", true, true)) {
    for (const auto& x : d["type_order"]) {
      if (!types.count(x.get<std::string>())) v.err("/type_order", "unknown item type " + x.get<std::string>());
    }
  }
  for (const char* key : {"cues", "heading_hints"}) {
    const Json* a = v.array(d, "", key, true, true);
    if (!a) continue;
    for (std::size_t i = 0; i < a->size(); ++i) {
      std::string p = idx(std::string("/") + key, i);
      v.one_of((*a)[i], p, "type", types);
      if (v.str((*a)[i], p, "phrase")) {
        std::string ph = (*a)[i]["phrase"].get<std::string>();
        if (utf8::to_lower(ph) != ph) v.err(p + "/phrase", "phrases must be lowercase");
      }
      v.num((*a)[i], p, "weight", 0.01, 10);
    }
  }
  v.strings(d, "", "negators", true, true);
  v.strings(d, "", "generic_subject_words", true, true);
}

void v_cues(V& v, const Json& d) {
  const Json* c = v.object(d, "", "classes");
  if (!c) return;
  for (auto it = c->begin(); it != c->end(); ++it) {
    std::string p = "/classes/" + it.key();
    const Json* a = v.array(it.value(), p, "phrases", true, true);
    if (!a) continue;
    for (std::size_t i = 0; i < a->size(); ++i) {
      v.str((*a)[i], idx(p + "/phrases", i), "p");
      v.num((*a)[i], idx(p + "/phrases", i), "w", 0.01, 10);
    }
  }
}

void v_version_patterns(V& v, const Json& d) {
  v.regex(d, "", "regex");
  v.integer(d, "", "window_tokens", 1, 200);
  if (const Json* a = v.object(d, "", "anchors")) {
    v.strings(*a, "/anchors", "en", true, true);
    v.strings(*a, "/anchors", "pl", true, true);
  }
  if (const Json* a = v.array(d, "", "declarations", true, true)) {
    std::set<std::string> ids;
    for (std::size_t i = 0; i < a->size(); ++i) {
      std::string p = idx("/declarations", i);
      if (v.str((*a)[i], p, "id") && !ids.insert((*a)[i]["id"].get<std::string>()).second) v.err(p + "/id", "duplicate id");
      v.regex((*a)[i], p, "regex");
      v.num((*a)[i], p, "confidence", 0.01, 1);
    }
  }
  v.strings(d, "", "exclude_contexts", false);
}

void v_relation_patterns(V& v, const Json& d, const TypesVocab& t) {
  const Json* a = v.array(d, "", "patterns", true, true);
  if (!a) return;
  std::set<std::string> ids;
  for (std::size_t i = 0; i < a->size(); ++i) {
    const Json& x = (*a)[i];
    std::string p = idx("/patterns", i);
    if (v.str(x, p, "id") && !ids.insert(x["id"].get<std::string>()).second) v.err(p + "/id", "duplicate id");
    v.one_of(x, p, "lang", {"en", "pl", "any"});
    v.one_of(x, p, "rel", t.relations);
    v.num(x, p, "confidence", 0.01, 1);
    if (v.str(x, p, "template")) {
      const std::string tpl = x["template"].get<std::string>();
      bool has_obj = tpl.find("{obj:") != std::string::npos;
      bool has_subj = tpl.find("{subj:") != std::string::npos || x.contains("subject");
      if (!has_obj || !has_subj) v.err(p + "/template", "needs {subj:...} (or a 'subject') and {obj:...}");
    }
  }
}

void v_profile(V& v, const Json& d, const TypesVocab& t) {
  v.str(d, "", "id");
  const Json* a = v.array(d, "", "projects", true, true);
  if (!a) return;
  std::set<std::string> ids = names_of(*a, "id");
  std::set<std::string> seen;
  for (std::size_t i = 0; i < a->size(); ++i) {
    const Json& x = (*a)[i];
    std::string p = idx("/projects", i);
    if (v.str(x, p, "id") && !seen.insert(x["id"].get<std::string>()).second) v.err(p + "/id", "duplicate project id");
    v.str(x, p, "name");
    v.str(x, p, "project_kind");  // must name a project kind file (cross references)
    v.strings(x, p, "facets", false);
    if (const Json* m = json::find(x, "merged_into"); m && !m->is_null()) {
      if (!m->is_string() || !ids.count(m->get<std::string>())) v.err(p + "/merged_into", "must be null or a project id");
    }
    const Json* al = v.array(x, p, "aliases", true, true);
    if (!al) continue;
    for (std::size_t k = 0; k < al->size(); ++k) {
      const Json& y = (*al)[k];
      std::string q = idx(p + "/aliases", k);
      v.str(y, q, "t");
      if (json::get_bool(y, "ambiguous")) {
        if (const Json* rc = v.object(y, q, "requires_context")) {
          v.strings(*rc, q + "/requires_context", "any", true, true);
          v.integer(*rc, q + "/requires_context", "min", 1, 10);
        }
      }
      v.strings(y, q, "negative_context", false);
    }
  }
  (void)t;
}
void v_checks(V& v, const Json& d) {
  const Json* a = v.array(d, "", "checks", true, true);
  if (!a) return;
  std::set<std::string> ids;
  for (std::size_t i = 0; i < a->size(); ++i) {
    const Json& x = (*a)[i];
    std::string p = idx("/checks", i);
    if (v.str(x, p, "id") && !ids.insert(x["id"].get<std::string>()).second) v.err(p + "/id", "duplicate check id");
    v.str(x, p, "principle");
    v.closed(x, p, "detector", kCheckDetectors);
    if (const Json* pr = v.object(x, p, "params")) {
      v.strings(*pr, p + "/params", "languages", true, true);
      std::string det = json::get_string(x, "detector");
      if (det.rfind("regex_", 0) == 0) v.regex(*pr, p + "/params", "pattern");
      if (det == "string_array_literal") v.integer(*pr, p + "/params", "min_entries", 2, 100000);
    }
    v.bilingual(x, p, "message");
  }
}

void v_thresholds(V& v, const Json& d) {
  bool any = false;
  for (auto it = d.begin(); it != d.end(); ++it) {
    if (it.key() == "schema" || it.key() == "description") continue;
    any = true;
    if (!it.value().is_object()) {
      v.err("/" + it.key(), "expected an object of numbers");
      continue;
    }
    for (auto jt = it.value().begin(); jt != it.value().end(); ++jt) {
      if (!jt.value().is_number()) v.err("/" + it.key() + "/" + jt.key(), "expected a number");
    }
  }
  if (!any) v.err("", "no threshold groups");
  if (const Json* p = json::find(d, "paradigm")) {
    v.num(*p, "/paradigm", "extrapolation_cap", 0.01, 1);
    v.num(*p, "/paradigm", "tau_observed", 0, 1);
    v.num(*p, "/paradigm", "tau_inferred", 0, 1);
  } else {
    v.err("/paradigm", "missing required group");
  }
  for (const char* g : {"catalog", "concepts", "status", "principles", "atlas"}) {
    if (!d.contains(g)) v.err(std::string("/") + g, "missing required group");
  }
}

void v_relevance(V& v, const Json& d) {
  v.num(d, "", "bias", -100, 100);
  for (const char* key : {"weights", "term_class_weights"}) {
    if (const Json* w = v.object(d, "", key)) {
      for (auto it = w->begin(); it != w->end(); ++it) {
        if (!it.value().is_number()) v.err(std::string("/") + key + "/" + it.key(), "expected a number");
      }
    }
  }
  if (const Json* b = v.object(d, "", "bm25")) {
    v.num(*b, "/bm25", "k1", 0, 10);
    v.num(*b, "/bm25", "b", 0, 1);
  }
}

void v_selection_rules(V& v, const Json& d) {
  static const std::set<std::string> kKeys = {"label",     "project",   "title_regex", "date_from", "date_to", "file_glob",
                                              "score_min", "score_max", "lang",        "min_chars", "max_chars"};
  const Json* a = v.array(d, "", "rules");
  if (!a) return;
  std::set<std::string> ids;
  for (std::size_t i = 0; i < a->size(); ++i) {
    const Json& x = (*a)[i];
    std::string p = idx("/rules", i);
    if (v.str(x, p, "id") && !ids.insert(x["id"].get<std::string>()).second) v.err(p + "/id", "duplicate rule id");
    v.one_of(x, p, "action", {"include", "exclude", "review"});
    if (const Json* m = v.object(x, p, "match")) {
      for (auto it = m->begin(); it != m->end(); ++it) {
        if (!kKeys.count(it.key())) v.err(p + "/match/" + it.key(), "unknown match key");
      }
      if (m->contains("title_regex")) v.regex(*m, p + "/match", "title_regex");
    }
  }
}

void v_calibration(V& v, const Json& d) {
  v.integer(d, "", "min_labels", 1, 1000000);
  if (const Json* r = v.object(d, "", "reliability")) {
    for (auto it = r->begin(); it != r->end(); ++it) {
      v.num(it.value(), "/reliability/" + it.key(), "alpha", 0.001, 1e9);
      v.num(it.value(), "/reliability/" + it.key(), "beta", 0.001, 1e9);
    }
  }
  if (const Json* iso = v.object(d, "", "isotonic")) {
    for (auto it = iso->begin(); it != iso->end(); ++it) {
      std::string p = "/isotonic/" + it.key();
      if (!evidence_from_string(it.key())) v.err(p, "unknown evidence class");
      if (!it.value().is_array() || it.value().size() < 2) {
        v.err(p, "expected >= 2 [x, y] points");
        continue;
      }
      double px = -1, py = -1;
      for (const auto& pt : it.value()) {
        if (!pt.is_array() || pt.size() != 2 || !pt[0].is_number() || !pt[1].is_number()) {
          v.err(p, "points are [x, y] numbers");
          break;
        }
        double x = pt[0].get<double>(), y = pt[1].get<double>();
        if (x <= px || y < py || x < 0 || x > 1 || y < 0 || y > 1) {
          v.err(p, "points must have increasing x and non-decreasing y within [0, 1]");
          break;
        }
        px = x;
        py = y;
      }
    }
  }
  if (const Json* g = v.object(d, "", "gates")) v.num(*g, "/gates", "ece", 0, 1);
}

void v_evidence_encoding(V& v, const Json& d, const TypesVocab& t) {
  if (const Json* ev = v.object(d, "", "evidence")) {
    for (auto e : model::all<Evidence>()) {
      std::string name(to_string(e));
      std::string p = "/evidence/" + name;
      const Json* x = json::find(*ev, name);
      if (!x) {
        v.err(p, "missing evidence class");
        continue;
      }
      const Json* gt = v.member(*x, p, "gt");
      std::string want(gt_evidence(e));
      if (gt && !((want.empty() && gt->is_null()) || (gt->is_string() && gt->get<std::string>() == want))) {
        v.err(p + "/gt", "must be '" + (want.empty() ? std::string("null") : want) + "' (kb::gt_evidence)");
      }
      if (const Json* o = v.object(*x, p, "outline")) v.str(*o, p + "/outline", "style");
      v.str(*x, p, "glyph");
      v.str(*x, p, "cli");
      v.str(*x, p, "markdown");
      v.bilingual(*x, p, "label");
    }
    for (auto it = ev->begin(); it != ev->end(); ++it) {
      if (!evidence_from_string(it.key())) v.err("/evidence/" + it.key(), "unknown evidence class");
    }
  }
  // The origin channel: model_knowledge must be visibly distinct (§2.3, R7).
  if (const Json* og = v.object(d, "", "origin")) {
    for (auto o : model::all<model::Origin>()) {
      std::string name(model::to_string(o));
      std::string p = "/origin/" + name;
      const Json* x = json::find(*og, name);
      if (!x) {
        v.err(p, "missing origin");
        continue;
      }
      v.str(*x, p, "halo");
      v.str(*x, p, "cli", true, false);
      v.bilingual(*x, p, "label");
    }
    for (auto it = og->begin(); it != og->end(); ++it) {
      if (!model::from_string<model::Origin>(it.key())) v.err("/origin/" + it.key(), "unknown origin");
    }
    const Json& mk = at(*og, "model_knowledge");
    const Json& ar = at(*og, "archive");
    if (mk.is_object() && ar.is_object() && json::get_string(mk, "halo") == json::get_string(ar, "halo") &&
        json::get_string(mk, "badge") == json::get_string(ar, "badge")) {
      v.err("/origin/model_knowledge", "must be visibly distinct from archive (halo or badge)");
    }
  }
  if (const Json* c = v.object(d, "", "confidence")) {
    v.num(*c, "/confidence", "min_opacity", 0, 1);
    if (const Json* bins = v.array(*c, "/confidence", "bins", true, true)) {
      double prev = 0;
      for (std::size_t i = 0; i < bins->size(); ++i) {
        std::string p = idx("/confidence/bins", i);
        v.num((*bins)[i], p, "opacity", 0, 1);
        v.str((*bins)[i], p, "cli");
        double lt = json::get_number((*bins)[i], "lt", -1);
        if (lt <= prev) v.err(p + "/lt", "bin bounds must increase");
        prev = lt;
      }
      if (prev < 1.0) v.err("/confidence/bins", "the last bin must cover confidence 1.0");
    }
  }
  if (const Json* s = v.object(d, "", "status")) {
    for (const char* en : {"project_status", "component_status"}) {
      for (const auto& val : t.enum_values(en)) {
        if (!s->contains(val)) v.err("/status/" + val, "missing encoding for " + std::string(en) + " value");
      }
    }
    for (auto it = s->begin(); it != s->end(); ++it) {
      v.str(it.value(), "/status/" + it.key(), "hue");
      v.str(it.value(), "/status/" + it.key(), "glyph");
    }
  }
  if (const Json* r = v.object(d, "", "role")) {
    for (auto role : model::all<model::Role>()) {
      std::string name(model::to_string(role));
      if (!r->contains(name)) {
        v.err("/role/" + name, "missing shape for universal role");
      } else {
        v.str(at(*r, name), "/role/" + name, "shape");
      }
    }
    for (auto it = r->begin(); it != r->end(); ++it) {
      if (!model::from_string<model::Role>(it.key())) v.err("/role/" + it.key(), "unknown universal role");
    }
  }
  if (const Json* k = v.object(d, "", "kind", false)) {
    for (auto it = k->begin(); it != k->end(); ++it) {
      if (!t.kinds.count(it.key())) v.err("/kind/" + it.key(), "unknown entity kind");
    }
  }
  if (const Json* rc = v.object(d, "", "relation_category")) {
    for (const auto& cat : t.categories) {
      if (!rc->contains(cat)) v.err("/relation_category/" + cat, "missing encoding for relation category");
    }
  }
  if (const Json* cs = v.object(d, "", "check_state")) {
    for (auto s : model::all<CheckState>()) {
      if (!cs->contains(std::string(to_string(s)))) v.err("/check_state/" + std::string(to_string(s)), "missing check state");
    }
  }
  if (const Json* b = v.object(d, "", "badges")) {
    for (const char* req : {"oscillation", "lost_again", "transferred"}) {
      if (!b->contains(req)) v.err(std::string("/badges/") + req, "missing badge");
    }
  }
  v.object(d, "", "conflict");
  if (v.strings(d, "", "legend_order", true, true)) {
    std::set<std::string> lo;
    for (const auto& x : d["legend_order"]) lo.insert(x.get<std::string>());
    if (lo.size() != model::count<Evidence>()) v.err("/legend_order", "must list the six evidence classes once each");
    for (const auto& x : lo) {
      if (!evidence_from_string(x)) v.err("/legend_order", "unknown evidence class " + x);
    }
  }
}

void v_binding(V& v, const Json& b, const std::string& p, const TypesVocab& t) {
  v.closed(b, p, "kind", kBindingKinds);
  std::string kind = json::get_string(b, "kind");
  if (kind == "subject") v.closed(b, p, "field", kSubjectFields);
  if (kind == "claim") {
    v.one_of(b, p, "rel", t.relations);
    if (b.contains("via")) v.one_of(b, p, "via", t.relations);
    if (b.contains("dir")) v.one_of(b, p, "dir", {"in", "out"});
    if (const Json* f = json::find(b, "filter")) {
      if (f->contains("label_matches")) v.regex(*f, p + "/filter", "label_matches");
    }
  }
  if (kind == "items") {
    v.one_of(b, p, "type", t.enum_values("item_type"));
    v.one_of(b, p, "scope", {"subject", "topic"});
    if (b.contains("pick")) v.one_of(b, p, "pick", {"earliest", "latest", "all"});
  }
  if (kind == "lexicon") {
    v.str(b, p, "class");
    v.one_of(b, p, "scope", {"subject_sentences", "subject_units", "corpus"});
  }
  if (kind == "codebase") v.closed(b, p, "field", kCodebaseFields);
  if (kind == "slot") {
    v.str(b, p, "paradigm");
    v.str(b, p, "slot");
  }
  if (kind == "principles") v.one_of(b, p, "applies_to", {"subject", "any"});
  if (kind == "forks") v.closed(b, p, "field", kForkFields);
  if (kind == "enumeration") {
    v.one_of(b, p, "shape", {"chain", "options"});
    v.one_of(b, p, "scope", {"subject_sentences", "subject_units"});
  }
  if (kind == "versions") v.one_of(b, p, "source", {"git", "archive", "any"});
  if (kind == "checks") v.one_of(b, p, "field", {"violations"});
}

bool valid_slot_type(const std::string& type, const TypesVocab& t) {
  if (in_closed_set(type, kSlotTypes)) return true;
  if (type.rfind("entity:", 0) == 0) return t.kinds.count(type.substr(7)) > 0;
  if (type.rfind("enum:", 0) == 0) return t.enums.count(type.substr(5)) > 0;
  return false;
}

void v_slot_spec(V& v, const model::SlotSpec& s, const std::string& p, const TypesVocab& t) {
  if (!valid_slot_type(s.type, t)) v.err(p + "/type", "unknown slot type '" + s.type + "'");
  if (!s.relation.empty() && !t.relations.count(s.relation)) v.err(p + "/relation", "unknown relation '" + s.relation + "'");
  for (auto it = s.fields.begin(); it != s.fields.end(); ++it) {
    std::string ft = json::get_string(it.value(), "type");
    if (!valid_slot_type(ft, t)) v.err(p + "/fields/" + it.key(), "unknown field type '" + ft + "'");
    if (it.value().contains("card")) v.closed(it.value(), p + "/fields/" + it.key(), "card", kCardinalities);
  }
  for (std::size_t k = 0; k < s.bind.size(); ++k) v_binding(v, s.bind[k], idx(p + "/bind", k), t);
}

// Paradigm anchors: {"any"|"all": [anchor ops], "min_score"}.
void v_anchors(V& v, const Json& an, const std::string& p0, const TypesVocab& t) {
  if (!an.is_object() || an.empty()) return;
  v.num(an, p0, "min_score", 0, 1);
  bool any = false;
  for (const char* key : {"any", "all"}) {
    const Json* ops = json::find(an, key);
    if (!ops) continue;
    any = true;
    if (!ops->is_array() || ops->empty()) {
      v.err(p0 + "/" + key, "expected a non-empty array");
      continue;
    }
    for (std::size_t i = 0; i < ops->size(); ++i) {
      std::string p = idx(p0 + "/" + key, i);
      const Json& o = (*ops)[i];
      v.closed(o, p, "op", kAnchorOps);
      std::string op = json::get_string(o, "op");
      if (op == "claim_count") {
        v.one_of(o, p, "rel", t.relations);
        v.integer(o, p, "min", 1, 1000);
      }
      if (op == "kind_hint") v.str(o, p, "value");
      if (op == "cue") {
        v.str(o, p, "class");
        v.integer(o, p, "min", 1, 1000);
      }
      if (op == "has_items") {
        v.one_of(o, p, "type", t.enum_values("item_type"));
        v.integer(o, p, "min", 1, 1000);
      }
      if (op == "option_set") v.integer(o, p, "min_options", 2, 100);
      if (op == "principle_count") v.integer(o, p, "min", 1, 1000);
    }
  }
  if (!any) v.err(p0, "needs 'any' and/or 'all'");
}

void v_constraints(V& v, const Json& cs, const std::string& p0) {
  if (!cs.is_array()) return;
  for (std::size_t i = 0; i < cs.size(); ++i) {
    std::string p = idx(p0, i);
    v.str(cs[i], p, "id");
    if (const Json* e = v.member(cs[i], p, "expr")) check_expr(v, *e, p + "/expr", kPredicates, "predicate");
    v.closed(cs[i], p, "on_violation", kViolationActions);
  }
}

void v_domain_kinds(V& v, const std::vector<model::DomainKind>& kinds, const std::string& p0, const TypesVocab& t) {
  for (std::size_t i = 0; i < kinds.size(); ++i) {
    const auto& k = kinds[i];
    std::string p = idx(p0, i);
    if (!k.entity_kind.empty() && !t.kinds.count(k.entity_kind)) v.err(p + "/entity_kind", "unknown entity kind '" + k.entity_kind + "'");
    if (!valid_slot_type(k.value_type, t)) v.err(p + "/value_type", "unknown slot type '" + k.value_type + "'");
    if (!k.relation.empty() && !t.relations.count(k.relation)) v.err(p + "/relation", "unknown relation '" + k.relation + "'");
    for (std::size_t r = 0; r < k.relations.size(); ++r) {
      if (!t.relations.count(k.relations[r].rel)) v.err(idx(p + "/relations", r) + "/rel", "unknown relation '" + k.relations[r].rel + "'");
    }
    for (std::size_t s = 0; s < k.slots.size(); ++s) v_slot_spec(v, k.slots[s], idx(p + "/slots", s), t);
    for (std::size_t b = 0; b < k.bind.size(); ++b) v_binding(v, k.bind[b], idx(p + "/bind", b), t);
    if (!k.anchors.empty()) {
      if (const Json* terms = json::find(k.anchors, "terms")) {
        if (!terms->is_object()) {
          v.err(p + "/anchors/terms", "expected {\"en\":[...],\"pl\":[...]}");
        } else {
          v.strings(*terms, p + "/anchors/terms", "en", false);
          v.strings(*terms, p + "/anchors/terms", "pl", false);
        }
      }
      v.strings(k.anchors, p + "/anchors", "lexicon", false);
      v.strings(k.anchors, p + "/anchors", "cues", false);
    }
  }
}

std::string file_stem(std::string_view relpath) { return fs::path(std::string(relpath)).stem().string(); }

void v_project_kind(V& v, const Json& d, const TypesVocab& t, std::string_view relpath) {
  auto pk = parse_model<model::ProjectKind>(v, d, "");
  if (!pk) return;
  if (pk->header.id != file_stem(relpath)) v.err("/id", "must equal the file name ('" + file_stem(relpath) + "')");
  if (!t.kinds.count(pk->subject_kind)) v.err("/subject_kind", "unknown entity kind '" + pk->subject_kind + "'");
  v_anchors(v, pk->anchors, "/anchors", t);
  v_domain_kinds(v, pk->domain_kinds, "/domain_kinds", t);
  v_constraints(v, pk->constraints, "/constraints");
}

void v_facet(V& v, const Json& d, const TypesVocab& t, std::string_view relpath) {
  auto f = parse_model<model::Facet>(v, d, "");
  if (!f) return;
  if (f->header.id != file_stem(relpath)) v.err("/id", "must equal the file name ('" + file_stem(relpath) + "')");
  v_anchors(v, f->anchors, "/anchors", t);
  v_domain_kinds(v, f->domain_kinds, "/domain_kinds", t);
  v_constraints(v, f->constraints, "/constraints");
}

void v_artifact_type(V& v, const Json& d, const TypesVocab& t, std::string_view relpath) {
  auto a = parse_model<model::ArtifactType>(v, d, "");
  if (!a) return;
  if (a->header.id != file_stem(relpath)) v.err("/id", "must equal the file name ('" + file_stem(relpath) + "')");
  if (!a->detect.empty()) {
    v.num(a->detect, "/detect", "min_score", 0, 1);
    bool any = false;
    for (const char* key : {"any", "all"}) {
      const Json* ops = json::find(a->detect, key);
      if (!ops) continue;
      any = true;
      if (!ops->is_array() || ops->empty()) {
        v.err(std::string("/detect/") + key, "expected a non-empty array");
        continue;
      }
      for (std::size_t i = 0; i < ops->size(); ++i) {
        v.closed((*ops)[i], idx(std::string("/detect/") + key, i), "op", kDetectOps);
        v.str((*ops)[i], idx(std::string("/detect/") + key, i), "value");
      }
    }
    if (!any) v.err("/detect", "needs 'any' and/or 'all'");
  }
  if (const Json* seg = json::find(a->parse, "segment")) {
    if (!seg->is_array() || seg->empty()) {
      v.err("/parse/segment", "expected a non-empty array of segmenters");
    } else {
      for (std::size_t i = 0; i < seg->size(); ++i) {
        const Json& s = (*seg)[i];
        if (!s.is_string() || !in_closed_set(s.get<std::string>(), kSegmenters)) {
          v.err(idx("/parse/segment", i), "'" + json::dump(s) + "' is not a segmenter implemented in code (kb::kSegmenters)");
        }
      }
    }
  } else {
    v.err("/parse/segment", "missing required key");
  }
  if (const Json* ok = json::find(a->parse, "observation_kinds"); ok && ok->is_array()) {
    for (std::size_t i = 0; i < ok->size(); ++i) {
      std::string s = (*ok)[i].is_string() ? (*ok)[i].get<std::string>() : "";
      auto r = model::parse<model::ObservationKind>(s, "observation_kinds");
      if (!r) v.err(idx("/parse/observation_kinds", i), r.error().message);
    }
  }
  for (std::size_t i = 0; i < a->extract.size(); ++i) v.closed(a->extract[i], idx("/extract", i), "op", kExtractors);
  for (std::size_t i = 0; i < a->structure.size(); ++i) v_slot_spec(v, a->structure[i], idx("/structure", i), t);
}

void v_anchoring(V& v, const Json& d, const TypesVocab& t) {
  auto am = parse_model<model::AnchoringModel>(v, d, "");
  if (!am) return;
  for (const auto& [rel, rr] : am->relation_map) {
    if (!t.relations.count(rel)) v.err("/relation_map/" + rel, "relation is not declared in schema/types.json");
  }
}

void v_morphisms(V& v, const Json& d) {
  const Json* a = v.array(d, "", "morphisms", true, true);
  if (!a) return;
  std::set<std::string> ids;
  for (std::size_t i = 0; i < a->size(); ++i) {
    std::string p = idx("/morphisms", i);
    auto m = parse_model<model::Morphism>(v, (*a)[i], p);
    if (!m) continue;
    if (!ids.insert(m->id).second) v.err(p + "/id", "duplicate morphism id");
    if (m->id.rfind("m.", 0) != 0) v.err(p + "/id", "morphism ids start with 'm.'");
    if (m->use != model::MorphismUse::Transfer) {
      v.err(p + "/use", "anchoring morphisms are derived from the domain kinds' roles, not listed");
    }
    for (std::size_t k = 0; k < m->conditions.size(); ++k) check_expr(v, m->conditions[k], idx(p + "/conditions", k), kConditionOps, "condition");
    if (m->expected) check_expr(v, m->expected->expr, p + "/expected_property/expr", kPredicates, "predicate");
  }
}

void v_goal_types(V& v, const Json& d) {
  const Json* a = v.array(d, "", "goal_types", true, true);
  if (!a) return;
  std::set<std::string> ids;
  for (std::size_t i = 0; i < a->size(); ++i) {
    std::string p = idx("/goal_types", i);
    auto g = parse_model<model::GoalType>(v, (*a)[i], p);
    if (!g) continue;
    if (!ids.insert(g->id).second) v.err(p + "/id", "duplicate goal type");
    if (g->budget.size() != model::count<model::ContextBand>()) v.err(p + "/budget", "a goal type splits the budget over the three bands");
    if (!g->resolutions.count("*")) v.err(p + "/resolutions", "a goal type has a default resolution '*'");
  }
}

// Seed priors: every source is dated (temporal holdout) and names a document.
void v_prior_sources(V& v, const std::vector<model::Reference>& sources, const std::string& p) {
  if (sources.empty()) v.err(p + "/sources", "a prior names its dated sources");
  for (std::size_t k = 0; k < sources.size(); ++k) {
    if (sources[k].doc.empty()) v.err(idx(p + "/sources", k) + "/doc", "missing document");
    if (sources[k].date.size() != 10) v.err(idx(p + "/sources", k) + "/date", "priors need a YYYY-MM-DD source date (temporal holdout)");
  }
}

void v_principles(V& v, const Json& d) {
  const Json* a = v.array(d, "", "principles", true, true);
  if (!a) return;
  std::set<std::string> ids;
  for (std::size_t i = 0; i < a->size(); ++i) {
    std::string p = idx("/principles", i);
    auto x = parse_model<model::Principle>(v, (*a)[i], p);
    if (!x) continue;
    if (!ids.insert(x->id).second) v.err(p + "/id", "duplicate principle id");
    if (x->id.rfind("p.", 0) != 0) v.err(p + "/id", "principle ids start with 'p.'");
    if (!x->statement.count("en") || !x->statement.count("pl")) v.err(p + "/statement", "statements are bilingual {en, pl}");
    if (x->phrasings.empty()) v.err(p + "/phrasings", "a seed lists its verbatim phrasings");
    if (x->validation != model::ValidationStatus::Candidate) {
      v.err(p + "/validation_status", "seed principles are priors: validation_status must be 'candidate'");
    }
    if (!x->owner.empty() && x->owner != "user") v.err(p + "/owner", "owner is 'user' or empty");
    v_prior_sources(v, x->sources, p);
  }
}

void v_operators(V& v, const Json& d) {
  const Json* a = v.array(d, "", "operators", true, true);
  if (!a) return;
  std::set<std::string> ids;
  for (std::size_t i = 0; i < a->size(); ++i) {
    std::string p = idx("/operators", i);
    auto x = parse_model<model::Operator>(v, (*a)[i], p);
    if (!x) continue;
    if (!ids.insert(x->id).second) v.err(p + "/id", "duplicate operator id");
    if (x->id.rfind("op.", 0) != 0) v.err(p + "/id", "design operator ids start with 'op.'");
    if (x->is_rule()) v.err(p + "/produces", "design operators do not produce claims (rules live in rules/inference_rules.json)");
    if (!x->situation.count("en") || !x->situation.count("pl") || !x->solution.count("en") || !x->solution.count("pl")) {
      v.err(p, "situation and solution are bilingual {en, pl}");
    }
    if (x->principles.empty()) v.err(p + "/principles", "an operator names its justifying principles");
    if (x->validation != model::ValidationStatus::Candidate) {
      v.err(p + "/validation_status", "seed operators are priors: validation_status must be 'candidate'");
    }
    v_prior_sources(v, x->sources, p);
  }
}

void v_rules(V& v, const Json& d, const TypesVocab& t) {
  const Json* a = v.array(d, "", "rules", true, true);
  if (!a) return;
  std::set<std::string> ids;
  for (std::size_t i = 0; i < a->size(); ++i) {
    const Json& x = (*a)[i];
    std::string p = idx("/rules", i);
    if (v.str(x, p, "id") && !ids.insert(x["id"].get<std::string>()).second) v.err(p + "/id", "duplicate rule id");
    v.integer(x, p, "stratum", 0, 2);
    v.closed(x, p, "produces", kRuleProduces);
    if (const Json* w = v.member(x, p, "when")) check_expr(v, *w, p + "/when", kConditionOps, "condition");
    if (const Json* w = v.member(x, p, "value")) check_expr(v, *w, p + "/value", kValueOps, "value");
    if (const Json* e = json::find(x, "expected_property"); e && e->is_object()) {
      if (const Json* ex = v.member(*e, p + "/expected_property", "expr")) {
        check_expr(v, *ex, p + "/expected_property/expr", kPredicates, "predicate");
      }
      v.str(*e, p + "/expected_property", "rationale");
    }
    auto op = parse_model<model::Operator>(v, x, p);
    if (!op) continue;
    if (!op->is_rule()) {
      v.err(p + "/produces", "a rule produces derived, inferred or extrapolated claims");
      continue;
    }
    bool extra = *op->produces == Evidence::Extrapolated;
    if ((op->id.rfind("x.", 0) == 0) != extra) v.err(p + "/id", "extrapolation rules (and only they) are named 'x.*'");
    if (!extra && op->id.rfind("r.", 0) != 0) v.err(p + "/id", "rule ids start with 'r.'");
    const Json& tg = op->target;
    int shapes = (tg.contains("paradigm") ? 1 : 0) + (tg.contains("temporal") ? 1 : 0) + (tg.contains("entity_kind") ? 1 : 0);
    if (shapes != 1) v.err(p + "/target", "exactly one of {paradigm, slot[, field]}, {temporal[, field]}, {entity_kind, attr}");
    if (tg.contains("paradigm")) v.str(tg, p + "/target", "slot");
    if (tg.contains("temporal") && !t.temporal.count(json::get_string(tg, "temporal"))) {
      v.err(p + "/target/temporal", "unknown temporal slot (schema/types.json temporal_slots)");
    }
    if (tg.contains("temporal") && tg.contains("field")) {
      auto it = t.temporal.find(json::get_string(tg, "temporal"));
      if (it != t.temporal.end() && !it->second.count(json::get_string(tg, "field"))) v.err(p + "/target/field", "unknown record field");
    }
    if (tg.contains("entity_kind")) {
      v.one_of(tg, p + "/target", "entity_kind", t.kinds);
      v.str(tg, p + "/target", "attr");
    }
  }
}
void v_pack(V& v, const Json& d) {
  v.str(d, "", "id");
  v.integer(d, "", "version", 1, 1000000);
  if (const Json* f = v.array(d, "", "files", true, true)) {
    std::set<std::string> seen;
    for (std::size_t i = 0; i < f->size(); ++i) {
      std::string p = idx("/files", i);
      if (v.str((*f)[i], p, "path") && !seen.insert((*f)[i]["path"].get<std::string>()).second) {
        v.err(p + "/path", "duplicate path");
      }
      v.str((*f)[i], p, "schema");
    }
  }
}

// ── cross references ────────────────────────────────────────────────
const Json& doc(const Docs& docs, std::string_view path) {
  static const Json kNull;
  auto it = docs.find(path);
  return it == docs.end() ? kNull : it->second;
}

}  // namespace

Json PackIssue::to_json() const { return Json{{"file", file}, {"pointer", pointer}, {"message", message}}; }

bool validate_document(std::string_view relpath, std::string_view schema, const Json& d, const Json& types,
                       std::vector<PackIssue>& issues) {
  std::size_t before = issues.size();
  V v{issues, std::string(relpath)};
  if (!d.is_object()) {
    v.err("", "a pack document is a JSON object");
    return false;
  }
  if (json::get_string(d, "schema") != schema) {
    v.err("/schema", "expected '" + std::string(schema) + "', found '" + json::get_string(d, "schema") + "'");
    return false;
  }
  TypesVocab t(types.is_object() ? types : Json::object());
  if (schema == "loom.kb.pack/1") v_pack(v, d);
  else if (schema == "loom.kb.types/1") v_types(v, d);
  else if (schema == "loom.kb.stopwords/1") v_stopwords(v, d);
  else if (schema == "loom.kb.stemming/1") v_stemming(v, d);
  else if (schema == "loom.kb.glossary/1") v_glossary(v, d);
  else if (schema == "loom.kb.gazetteer/1") v_gazetteer(v, d, t);
  else if (schema == "loom.kb.item_cues/1") v_item_cues(v, d, t);
  else if (schema == "loom.kb.cues/1") v_cues(v, d);
  else if (schema == "loom.kb.version_patterns/1") v_version_patterns(v, d);
  else if (schema == "loom.kb.relation_patterns/1") v_relation_patterns(v, d, t);
  else if (schema == "loom.kb.profile/1") v_profile(v, d, t);
  else if (schema == "loom.kb.principles/2") v_principles(v, d);
  else if (schema == "loom.kb.operators/1") v_operators(v, d);
  else if (schema == "loom.kb.rules/2") v_rules(v, d, t);
  else if (schema == "loom.kb.checks/1") v_checks(v, d);
  else if (schema == "loom.kb.thresholds/1") v_thresholds(v, d);
  else if (schema == "loom.kb.relevance/1") v_relevance(v, d);
  else if (schema == "loom.kb.selection_rules/1") v_selection_rules(v, d);
  else if (schema == "loom.kb.calibration/1") v_calibration(v, d);
  else if (schema == "loom.kb.evidence_encoding/1") v_evidence_encoding(v, d, t);
  else if (schema == "loom.kb.goal_types/1") v_goal_types(v, d);
  else if (schema == "loom.kb.anchoring/1") v_anchoring(v, d, t);
  else if (schema == "loom.kb.morphisms/1") v_morphisms(v, d);
  else if (schema == "loom.kb.project_kind/1") v_project_kind(v, d, t, relpath);
  else if (schema == "loom.kb.facet/1") v_facet(v, d, t, relpath);
  else if (schema == "loom.kb.artifact_type/1") v_artifact_type(v, d, t, relpath);
  else v.err("/schema", "unknown schema id '" + std::string(schema) + "'");
  return issues.size() == before;
}

namespace {

// Everything a cross-reference check needs to know about one paradigm.
struct ParadigmInfo {
  std::string file;
  model::ParadigmKind kind = model::ParadigmKind::ProjectKind;
  std::vector<std::string> applies_to;  // facets: host project kinds
  std::vector<std::string> facets;      // project kinds: facets listed
  std::map<std::string, model::Role> roles;        // domain kind -> role (project kinds, facets)
  std::map<std::string, std::set<std::string>> fields;  // slot -> attribute slots / record fields
  std::map<std::string, std::vector<std::string>> slot_rules;
  std::vector<std::string> rules;       // paradigm-level rules
  std::vector<std::string> derived_from;
  std::vector<std::tuple<std::string, std::string, std::string, std::string>> relations;  // (kind, rel, target, pointer)
  std::vector<std::pair<std::string, std::string>> lexicon_refs;  // (class, pointer)
  std::vector<std::pair<std::string, std::string>> cue_refs;
  bool has_slot(const std::string& s) const { return fields.count(s) > 0; }
};

void collect_kinds(ParadigmInfo& pi, const std::vector<model::DomainKind>& kinds) {
  for (std::size_t i = 0; i < kinds.size(); ++i) {
    const auto& k = kinds[i];
    std::string p = idx("/domain_kinds", i);
    pi.roles[k.id] = k.role;
    auto& f = pi.fields[k.id];
    for (const auto& s : k.slots) f.insert(s.name);
    pi.slot_rules[k.id] = k.rules;
    for (std::size_t r = 0; r < k.relations.size(); ++r) {
      pi.relations.emplace_back(k.id, k.relations[r].rel, k.relations[r].target, idx(p + "/relations", r));
    }
    for (const auto& c : json_strings_of(k.anchors, "lexicon")) pi.lexicon_refs.emplace_back(c, p + "/anchors/lexicon");
    for (const auto& c : json_strings_of(k.anchors, "cues")) pi.cue_refs.emplace_back(c, p + "/anchors/cues");
    for (std::size_t b = 0; b < k.bind.size(); ++b) {
      if (json::get_string(k.bind[b], "kind") == "lexicon") {
        pi.lexicon_refs.emplace_back(json::get_string(k.bind[b], "class"), idx(p + "/bind", b));
      }
    }
  }
}

void collect_anchor_cues(ParadigmInfo& pi, const Json& anchors) {
  for (const char* key : {"any", "all"}) {
    const Json* ops = json::find(anchors, key);
    if (!ops || !ops->is_array()) continue;
    for (std::size_t i = 0; i < ops->size(); ++i) {
      if (json::get_string((*ops)[i], "op") == "cue") {
        pi.cue_refs.emplace_back(json::get_string((*ops)[i], "class"), idx(std::string("/anchors/") + key, i) + "/class");
      }
    }
  }
}

}  // namespace

void validate_cross_references(const Docs& docs, std::vector<PackIssue>& issues) {
  const Json& pack = doc(docs, "pack.json");
  // pack.json lists exactly the files present, with matching schema ids.
  std::set<std::string> listed;
  if (const Json* f = json::find(pack, "files"); f && f->is_array()) {
    for (const auto& x : *f) {
      std::string p = json::get_string(x, "path");
      listed.insert(p);
      auto it = docs.find(p);
      if (it == docs.end()) {
        issues.push_back({"pack.json", "/files", "listed file is missing: " + p});
      } else if (json::get_string(it->second, "schema") != json::get_string(x, "schema")) {
        issues.push_back({p, "/schema", "schema differs from pack.json ('" + json::get_string(x, "schema") + "')"});
      }
    }
  } else {
    issues.push_back({"pack.json", "", "missing or invalid pack.json"});
  }
  for (const auto& [p, _] : docs) {
    if (p != "pack.json" && !listed.count(p)) issues.push_back({p, "", "file is not listed in pack.json"});
  }

  TypesVocab t(doc(docs, "schema/types.json").is_object() ? doc(docs, "schema/types.json") : Json::object());

  // ── collect ───────────────────────────────────────────────────────
  std::map<std::string, ParadigmInfo> paradigms;  // id -> info
  for (const auto& [path, d] : docs) {
    std::string schema = json::get_string(d, "schema");
    ParadigmInfo pi;
    pi.file = path;
    if (schema == "loom.kb.project_kind/1") {
      auto pk = model::ProjectKind::from_json(d);
      if (!pk) continue;
      pi.kind = model::ParadigmKind::ProjectKind;
      pi.facets = pk->facets;
      pi.rules = pk->rules;
      pi.derived_from = pk->header.derived_from;
      collect_kinds(pi, pk->domain_kinds);
      collect_anchor_cues(pi, pk->anchors);
      paradigms[pk->header.id] = std::move(pi);
    } else if (schema == "loom.kb.facet/1") {
      auto fc = model::Facet::from_json(d);
      if (!fc) continue;
      pi.kind = model::ParadigmKind::Facet;
      pi.applies_to = fc->applies_to;
      pi.rules = fc->rules;
      pi.derived_from = fc->header.derived_from;
      collect_kinds(pi, fc->domain_kinds);
      collect_anchor_cues(pi, fc->anchors);
      paradigms[fc->header.id] = std::move(pi);
    } else if (schema == "loom.kb.artifact_type/1") {
      auto at = model::ArtifactType::from_json(d);
      if (!at) continue;
      pi.kind = model::ParadigmKind::ArtifactType;
      pi.derived_from = at->header.derived_from;
      for (const auto& s : at->structure) {
        auto& f = pi.fields[s.name];
        for (auto it = s.fields.begin(); it != s.fields.end(); ++it) f.insert(it.key());
        pi.slot_rules[s.name] = s.rules;
      }
      if (const Json* ops = json::find(at->detect, "any"); ops && ops->is_array()) {
        for (std::size_t i = 0; i < ops->size(); ++i) {
          if (json::get_string((*ops)[i], "op") == "cue") pi.cue_refs.emplace_back(json::get_string((*ops)[i], "value"), idx("/detect/any", i));
        }
      }
      paradigms[at->header.id] = std::move(pi);
    }
  }
  std::map<std::string, const Json*> rules;
  if (const Json* r = json::find(doc(docs, "rules/inference_rules.json"), "rules"); r && r->is_array()) {
    for (const auto& x : *r) rules[json::get_string(x, "id")] = &x;
  }
  std::map<std::string, const Json*> principles;
  std::set<std::string> value_principles;
  if (const Json* r = json::find(doc(docs, "philosophy/principles.json"), "principles"); r && r->is_array()) {
    for (const auto& x : *r) {
      principles[json::get_string(x, "id")] = &x;
      if (json::get_string(x, "level") == "value") value_principles.insert(json::get_string(x, "id"));
    }
  }
  std::set<std::string> checks;
  if (const Json* r = json::find(doc(docs, "rules/checks.json"), "checks"); r && r->is_array()) checks = names_of(*r, "id");
  std::set<std::string> lex_classes;
  if (const Json* c = json::find(doc(docs, "lexicons/gazetteer.json"), "classes"); c && c->is_object()) {
    for (auto it = c->begin(); it != c->end(); ++it) lex_classes.insert(it.key());
  }
  std::set<std::string> cue_classes;
  if (const Json* c = json::find(doc(docs, "lexicons/cues.json"), "classes"); c && c->is_object()) {
    for (auto it = c->begin(); it != c->end(); ++it) cue_classes.insert(it.key());
  }
  std::optional<model::AnchoringModel> anchoring;
  if (auto am = model::AnchoringModel::from_json(doc(docs, "morphisms/anchoring.json")); am) anchoring = std::move(*am);
  std::map<std::string, model::Morphism> morphs;
  for (const auto& [path, d] : docs) {
    if (json::get_string(d, "schema") != "loom.kb.morphisms/1") continue;
    if (const Json* a = json::find(d, "morphisms"); a && a->is_array()) {
      for (const auto& x : *a) {
        if (auto m = model::Morphism::from_json(x); m) morphs[m->id] = *m;
      }
    }
  }

  // A kind of a paradigm, or of a project kind a facet applies to.
  auto role_of = [&](const std::string& par, const std::string& kind) -> std::optional<model::Role> {
    auto it = paradigms.find(par);
    if (it == paradigms.end()) return std::nullopt;
    if (auto r = it->second.roles.find(kind); r != it->second.roles.end()) return r->second;
    for (const auto& host : it->second.applies_to) {
      auto h = paradigms.find(host);
      if (h == paradigms.end()) continue;
      if (auto r = h->second.roles.find(kind); r != h->second.roles.end()) return r->second;
    }
    return std::nullopt;
  };
  auto slot_exists = [&](const std::string& par, const std::string& slot) {
    auto it = paradigms.find(par);
    if (it == paradigms.end()) return false;
    if (it->second.has_slot(slot)) return true;
    for (const auto& host : it->second.applies_to) {
      auto h = paradigms.find(host);
      if (h != paradigms.end() && h->second.has_slot(slot)) return true;
    }
    return false;
  };
  // "$slot:<kind>" (same instance of `par`) / "$slot:<paradigm>.<kind>" / "$temporal:<slot>".
  auto check_ref = [&](const std::string& file, const std::string& ptr, const std::string& ref, const std::string& par) {
    std::string body;
    if (ref.rfind("$temporal:", 0) == 0) {
      body = ref.substr(10);
      body = body.substr(0, body.find_first_of("[."));
      if (!t.temporal.count(body)) issues.push_back({file, ptr, "unknown temporal slot in " + ref});
      return;
    }
    if (ref.rfind("$slot:", 0) != 0 && ref.rfind("$analog:", 0) != 0) return;
    body = ref.substr(ref.find(':') + 1);
    body = body.substr(0, body.find('['));
    std::size_t dot = body.find('.');
    if (dot != std::string::npos) {
      std::string p2 = body.substr(0, dot);
      std::string s2 = body.substr(dot + 1);
      if (!paradigms.count(p2)) {
        issues.push_back({file, ptr, "unknown paradigm in " + ref});
      } else if (!slot_exists(p2, s2)) {
        issues.push_back({file, ptr, "paradigm " + p2 + " has no slot '" + s2 + "' (" + ref + ")"});
      }
    } else if (!par.empty() && !slot_exists(par, body)) {
      issues.push_back({file, ptr, "paradigm " + par + " has no slot '" + body + "' (" + ref + ")"});
    }
  };
  std::function<void(const std::string&, const std::string&, const Json&, const std::string&)> walk_refs =
      [&](const std::string& file, const std::string& ptr, const Json& j, const std::string& par) {
        if (j.is_string()) {
          check_ref(file, ptr, j.get<std::string>(), par);
        } else if (j.is_array()) {
          for (std::size_t i = 0; i < j.size(); ++i) walk_refs(file, idx(ptr, i), j[i], par);
        } else if (j.is_object()) {
          for (auto it = j.begin(); it != j.end(); ++it) walk_refs(file, ptr + "/" + it.key(), it.value(), par);
        }
      };
  auto check_principle = [&](const std::string& file, const std::string& ptr, const std::string& pid) {
    if (!principles.count(pid)) issues.push_back({file, ptr, "unknown principle '" + pid + "'"});
  };

  // ── paradigms ─────────────────────────────────────────────────────
  for (const auto& [pid, pi] : paradigms) {
    const std::string& file = pi.file;
    for (const auto& [kind, rel, target, ptr] : pi.relations) {
      auto from = role_of(pid, kind);
      auto to = role_of(pid, target);
      if (!to) {
        issues.push_back({file, ptr + "/target", "unknown domain kind '" + target + "' (in " + pid + " or the kinds it applies to)"});
        continue;
      }
      if (!anchoring) {
        issues.push_back({file, ptr, "no anchoring model (morphisms/anchoring.json)"});
        continue;
      }
      if (auto why = anchoring->check(rel, *from, *to); !why.empty()) {
        issues.push_back({file, ptr, "relation does not anchor on the meta-model: " + why});
      }
    }
    for (const auto& [cls, ptr] : pi.lexicon_refs) {
      if (!lex_classes.count(cls) && !cue_classes.count(cls)) issues.push_back({file, ptr, "unknown lexicon class '" + cls + "'"});
    }
    for (const auto& [cls, ptr] : pi.cue_refs) {
      if (!cue_classes.count(cls)) issues.push_back({file, ptr, "unknown cue class '" + cls + "'"});
    }
    for (const auto& f : pi.facets) {
      auto it = paradigms.find(f);
      if (it == paradigms.end() || it->second.kind != model::ParadigmKind::Facet) {
        issues.push_back({file, "/facets", "unknown facet '" + f + "'"});
      } else if (std::find(it->second.applies_to.begin(), it->second.applies_to.end(), pid) == it->second.applies_to.end()) {
        issues.push_back({file, "/facets", "facet '" + f + "' does not apply to " + pid});
      }
    }
    for (const auto& host : pi.applies_to) {
      auto it = paradigms.find(host);
      if (it == paradigms.end() || it->second.kind != model::ParadigmKind::ProjectKind) {
        issues.push_back({file, "/applies_to", "unknown project kind '" + host + "'"});
      }
    }
    for (const auto& m : pi.derived_from) {
      if (!morphs.count(m)) issues.push_back({file, "/derived_from", "unknown morphism '" + m + "'"});
    }
    auto rule_targets = [&](const std::string& rid, const std::string& ptr, const std::string& slot) {
      auto it = rules.find(rid);
      if (it == rules.end()) {
        issues.push_back({file, ptr, "unknown rule '" + rid + "'"});
        return;
      }
      if (slot.empty()) return;  // paradigm-level rules may target temporal slots or entities
      const Json& tg = at(*it->second, "target");
      if (json::get_string(tg, "paradigm") != pid || json::get_string(tg, "slot") != slot) {
        issues.push_back({file, ptr, "rule '" + rid + "' targets " + json::get_string(tg, "paradigm") + "." + json::get_string(tg, "slot")});
      }
    };
    for (const auto& [slot, rs] : pi.slot_rules) {
      for (const auto& r : rs) rule_targets(r, "/slots[" + slot + "]/rules", slot);
    }
    for (const auto& r : pi.rules) rule_targets(r, "/rules", "");
    walk_refs(file, "", doc(docs, file), pid);
  }

  // ── rules ─────────────────────────────────────────────────────────
  double cap = json::get_number(at(doc(docs, "policy/thresholds.json"), "paradigm"), "extrapolation_cap", 0.5);
  for (const auto& [rid, rp] : rules) {
    const std::string file = "rules/inference_rules.json";
    const std::string ptr = "/rules[" + rid + "]";
    const Json& tg = at(*rp, "target");
    std::string par;
    if (tg.contains("paradigm")) {
      par = json::get_string(tg, "paradigm");
      std::string slot = json::get_string(tg, "slot");
      if (!paradigms.count(par)) {
        issues.push_back({file, ptr + "/target", "unknown paradigm '" + par + "'"});
        par.clear();
      } else if (!slot_exists(par, slot)) {
        issues.push_back({file, ptr + "/target", "paradigm " + par + " has no slot '" + slot + "'"});
      } else if (tg.contains("field")) {
        std::string field = json::get_string(tg, "field");
        const auto& pi = paradigms[par];
        auto f = pi.fields.find(slot);
        if (f != pi.fields.end() && !f->second.count(field)) issues.push_back({file, ptr + "/target/field", "unknown field '" + field + "'"});
      }
    }
    std::function<void(const Json&)> find_principles = [&](const Json& e) {
      if (e.is_object()) {
        std::string op = json::get_string(e, "op");
        if (op == "principle_active" || op == "consistent_with") {
          if (const Json* a = json::find(e, "args"); a && a->is_array() && !a->empty() && (*a)[0].is_string()) {
            check_principle(file, ptr, (*a)[0].get<std::string>());
          }
        }
        for (auto it = e.begin(); it != e.end(); ++it) find_principles(it.value());
      } else if (e.is_array()) {
        for (const auto& x : e) find_principles(x);
      }
    };
    find_principles(*rp);
    for (const char* key : {"principles"}) {
      if (const Json* ps = json::find(*rp, key); ps && ps->is_array()) {
        for (const auto& x : *ps) check_principle(file, ptr + "/" + key, x.is_string() ? x.get<std::string>() : "");
      }
    }
    if (const Json* b = json::find(*rp, "basis")) {
      if (const Json* ps = json::find(*b, "principles"); ps && ps->is_array()) {
        for (const auto& x : *ps) check_principle(file, ptr + "/basis", x.is_string() ? x.get<std::string>() : "");
      }
    }
    Json checked = *rp;
    checked.erase("basis");  // documentation only
    walk_refs(file, ptr, checked, par);
    if (json::get_string(*rp, "produces") == "extrapolated" && json::get_number(*rp, "confidence", 1.0) > cap) {
      issues.push_back({file, ptr + "/confidence", "extrapolation confidence above thresholds.paradigm.extrapolation_cap"});
    }
  }

  // ── morphisms: ends exist, transfers link kinds of the same role ──
  for (const auto& [mid, m] : morphs) {
    const std::string file = "morphisms/transfer.json";
    const std::string ptr = "/morphisms[" + mid + "]";
    auto end_role = [&](const model::MorphismEnd& e, const char* which) -> std::optional<model::Role> {
      auto r = role_of(e.paradigm, e.kind);
      if (!r) issues.push_back({file, ptr + "/" + which, "unknown domain kind " + e.paradigm + "." + e.kind});
      if (!e.slot.empty() && !slot_exists(e.paradigm, e.kind)) issues.push_back({file, ptr + "/" + which, "unknown slot"});
      return r;
    };
    auto a = end_role(m.from, "from");
    auto b = end_role(m.to, "to");
    if (a && b && *a != *b) {
      issues.push_back({file, ptr, "a transfer links kinds of the same universal role (" + std::string(model::to_string(*a)) +
                                       " != " + std::string(model::to_string(*b)) + ")"});
    }
    if (m.expected) walk_refs(file, ptr + "/expected_property", m.expected->expr, m.to.paradigm);
  }

  // ── principles, operators, checks ─────────────────────────────────
  for (const auto& [pid, pp] : principles) {
    const std::string file = "philosophy/principles.json";
    const std::string ptr = "/principles[" + pid + "]";
    for (const char* key : {"derived_from", "conflicts_with", "supersedes", "protects"}) {
      if (const Json* a = json::find(*pp, key); a && a->is_array()) {
        for (const auto& x : *a) {
          std::string id = x.is_string() ? x.get<std::string>() : "";
          check_principle(file, ptr + "/" + key, id);
          if (std::string(key) == "protects" && principles.count(id) && !value_principles.count(id)) {
            issues.push_back({file, ptr + "/protects", "'" + id + "' is not a principle of level value"});
          }
        }
      }
    }
    if (const Json* a = json::find(*pp, "checks"); a && a->is_array()) {
      for (const auto& x : *a) {
        if (!x.is_string() || !checks.count(x.get<std::string>())) issues.push_back({file, ptr + "/checks", "unknown check " + json::dump(x)});
      }
    }
    if (const Json* vc = json::find(*pp, "value_constraints")) {
      if (const Json* fc = json::find(*vc, "forbid_checks"); fc && fc->is_array()) {
        for (const auto& y : *fc) {
          if (!y.is_string() || !checks.count(y.get<std::string>())) issues.push_back({file, ptr + "/value_constraints", "unknown check " + json::dump(y)});
        }
      }
    }
    if (const Json* sc = json::find(*pp, "scope")) {
      for (const char* key : {"project_kinds", "facets", "artifact_types"}) {
        if (const Json* a = json::find(*sc, key); a && a->is_array()) {
          for (const auto& x : *a) {
            if (!x.is_string() || !paradigms.count(x.get<std::string>())) issues.push_back({file, ptr + "/scope/" + key, "unknown paradigm " + json::dump(x)});
          }
        }
      }
    }
  }
  if (const Json* r = json::find(doc(docs, "philosophy/operators.json"), "operators"); r && r->is_array()) {
    for (const auto& x : *r) {
      if (const Json* ps = json::find(x, "principles"); ps && ps->is_array()) {
        for (const auto& y : *ps) {
          check_principle("philosophy/operators.json", "/operators[" + json::get_string(x, "id") + "]/principles",
                          y.is_string() ? y.get<std::string>() : "");
        }
      }
    }
  }
  if (const Json* r = json::find(doc(docs, "rules/checks.json"), "checks"); r && r->is_array()) {
    for (const auto& x : *r) {
      if (!principles.count(json::get_string(x, "principle"))) {
        issues.push_back({"rules/checks.json", "/checks[" + json::get_string(x, "id") + "]/principle", "unknown principle"});
      }
    }
  }
  // Stop-word 'extends' points at a pack file.
  for (const auto& [p, d] : docs) {
    if (json::get_string(d, "schema") == "loom.kb.stopwords/1") {
      std::string ext = json::get_string(d, "extends");
      if (!ext.empty() && !docs.count(ext)) issues.push_back({p, "/extends", "unknown pack file " + ext});
    }
  }
  // Relation patterns: slot types resolve.
  if (const Json* pats = json::find(doc(docs, "lexicons/relation_patterns.json"), "patterns"); pats && pats->is_array()) {
    static const re::Regex kSlot = *re::Regex::compile(R"(\{(subj|obj):([^}]+)\})");
    for (const auto& x : *pats) {
      std::u32string tpl = utf8::decode(json::get_string(x, "template"));
      for (const auto& m : kSlot.finditer(tpl)) {
        for (auto part : split(m.group_utf8(2), '|')) {
          if (!part.empty() && part.back() == '+') part.pop_back();
          if (part != "any" && part != "text" && !t.kinds.count(part) && !lex_classes.count(part)) {
            issues.push_back({"lexicons/relation_patterns.json", "/patterns[" + json::get_string(x, "id") + "]/template",
                              "unknown slot type '" + part + "'"});
          }
        }
      }
    }
  }
  // Profile projects: project kinds and facets exist; selection rules name projects.
  std::set<std::string> projects;
  if (const Json* pr = json::find(doc(docs, "profiles/self.json"), "projects"); pr && pr->is_array()) {
    projects = names_of(*pr, "id");
    for (const auto& x : *pr) {
      std::string ptr = "/projects[" + json::get_string(x, "id") + "]";
      std::string pk = json::get_string(x, "project_kind");
      auto it = paradigms.find(pk);
      if (it == paradigms.end() || it->second.kind != model::ParadigmKind::ProjectKind) {
        issues.push_back({"profiles/self.json", ptr + "/project_kind", "unknown project kind '" + pk + "'"});
        continue;
      }
      for (const auto& f : json_strings_of(x, "facets")) {
        if (std::find(it->second.facets.begin(), it->second.facets.end(), f) == it->second.facets.end()) {
          issues.push_back({"profiles/self.json", ptr + "/facets", "facet '" + f + "' is not a facet of " + pk});
        }
      }
    }
  }
  if (const Json* rs = json::find(doc(docs, "policy/selection_rules.json"), "rules"); rs && rs->is_array()) {
    for (const auto& x : *rs) {
      std::string pj = json::get_string(at(x, "match"), "project");
      if (!pj.empty() && pj != "owner" && !projects.count(pj)) {
        issues.push_back({"policy/selection_rules.json", "/rules[" + json::get_string(x, "id") + "]/match/project", "unknown project"});
      }
    }
  }
}
// ── Pack ────────────────────────────────────────────────────────────
Result<std::shared_ptr<const Pack>> Pack::from_documents(std::map<std::string, Json> in) {
  auto pack = std::make_shared<Pack>();
  for (auto& [k, v] : in) pack->docs_.emplace(k, std::move(v));
  std::vector<PackIssue> issues;
  const Json& manifest = doc(pack->docs_, "pack.json");
  const Json& types = doc(pack->docs_, "schema/types.json");
  if (!manifest.is_object()) {
    issues.push_back({"pack.json", "", "missing pack.json"});
  } else {
    validate_document("pack.json", "loom.kb.pack/1", manifest, types, issues);
    if (const Json* f = json::find(manifest, "files"); f && f->is_array()) {
      for (const auto& x : *f) {
        std::string p = json::get_string(x, "path");
        auto it = pack->docs_.find(p);
        if (it != pack->docs_.end()) validate_document(p, json::get_string(x, "schema"), it->second, types, issues);
      }
    }
  }
  validate_cross_references(pack->docs_, issues);
  if (!issues.empty()) {
    std::string msg = "invalid kb pack (" + std::to_string(issues.size()) + " issue(s)):";
    std::size_t shown = 0;
    for (const auto& i : issues) {
      if (++shown > 20) {
        msg += "\n  ...";
        break;
      }
      msg += "\n  " + i.file + (i.pointer.empty() ? "" : "#" + i.pointer) + ": " + i.message;
    }
    return Error(Errc::InvalidArgument, msg);
  }
  Sha256 h;
  for (const auto& [p, d] : pack->docs_) {
    h.update(p);
    h.update(std::string_view("\0", 1));
    h.update(json::canonical(d));
    h.update(std::string_view("\0", 1));
  }
  pack->hash_ = h.finish_hex();
  return std::shared_ptr<const Pack>(std::move(pack));
}

Result<std::shared_ptr<const Pack>> Pack::load_builtin() {
  std::map<std::string, Json> docs;
  for (const auto& f : kEmbeddedPack) {
    std::string text;
    for (const char* const* c = f.chunks; *c; ++c) text += *c;
    auto j = json::parse(text);
    if (!j) return Error(Errc::Parse, std::string("embedded kb pack file ") + f.path + ": " + j.error().message);
    docs.emplace(f.path, std::move(*j));
  }
  return from_documents(std::move(docs));
}

namespace {
Result<std::map<std::string, Json>> read_dir_docs(const fs::path& dir, bool require_manifest) {
  std::map<std::string, Json> docs;
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return Error(Errc::NotFound, "kb pack directory not found: " + dir.string());
  std::vector<fs::path> files;
  for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
       it.increment(ec)) {
    if (it->is_regular_file() && it->path().extension() == ".json") files.push_back(it->path());
  }
  if (ec) return Error(Errc::Io, "cannot list " + dir.string() + ": " + ec.message());
  std::sort(files.begin(), files.end());
  for (const auto& f : files) {
    LOOM_TRY_ASSIGN(std::string text, fsutil::read_file(f));
    auto j = json::parse(text);
    std::string rel = fs::relative(f, dir).generic_string();
    if (!j) return Error(Errc::Parse, "kb pack file " + rel + ": " + j.error().message);
    docs.emplace(rel, std::move(*j));
  }
  if (require_manifest && !docs.count("pack.json")) return Error(Errc::InvalidArgument, "no pack.json in " + dir.string());
  return docs;
}
}  // namespace

Result<std::shared_ptr<const Pack>> Pack::load_dir(const fs::path& dir) {
  LOOM_TRY_ASSIGN(auto docs, read_dir_docs(dir, true));
  return from_documents(std::move(docs));
}

Result<std::shared_ptr<const Pack>> Pack::load_with_overlay(const fs::path& overlay_dir) {
  LOOM_TRY_ASSIGN(auto base, load_builtin());
  std::error_code ec;
  if (overlay_dir.empty() || !fs::is_directory(overlay_dir, ec)) return base;
  LOOM_TRY_ASSIGN(auto over, read_dir_docs(overlay_dir, false));
  if (over.empty()) return base;
  std::map<std::string, Json> docs(base->docs_.begin(), base->docs_.end());
  Json overlay_manifest = over.count("pack.json") ? over["pack.json"] : Json();
  over.erase("pack.json");
  for (auto& [p, d] : over) {
    bool known = docs.count(p) > 0;
    docs[p] = std::move(d);
    if (!known) {
      // A new file must be declared by the overlay's pack.json.
      std::string schema;
      if (const Json* f = json::find(overlay_manifest, "files"); f && f->is_array()) {
        for (const auto& x : *f) {
          if (json::get_string(x, "path") == p) schema = json::get_string(x, "schema");
        }
      }
      if (schema.empty()) return Error(Errc::InvalidArgument, "overlay file " + p + " is not declared in the overlay pack.json");
      docs["pack.json"]["files"].push_back(Json{{"path", p}, {"schema", schema}});
    }
  }
  if (overlay_manifest.is_object()) {
    if (const Json* v = json::find(overlay_manifest, "version")) docs["pack.json"]["version"] = *v;
    if (const Json* id = json::find(overlay_manifest, "id")) docs["pack.json"]["id"] = *id;
  }
  return from_documents(std::move(docs));
}

std::string Pack::id() const { return json::get_string(file("pack.json"), "id"); }
int Pack::version() const { return static_cast<int>(json::get_int(file("pack.json"), "version")); }

std::vector<std::string> Pack::files() const {
  std::vector<std::string> out;
  for (const auto& [p, _] : docs_) out.push_back(p);
  return out;
}

const Json& Pack::file(std::string_view relpath) const {
  auto it = docs_.find(relpath);
  return it == docs_.end() ? empty_ : it->second;
}

const Json& Pack::types() const { return file("schema/types.json"); }

std::vector<std::string> Pack::ids(std::string_view dir) const {
  std::vector<std::string> out;
  std::string prefix = std::string(dir) + "/";
  for (const auto& [p, _] : docs_) {
    if (p.rfind(prefix, 0) == 0 && p.find('/', prefix.size()) == std::string::npos && p.size() > prefix.size() + 5) {
      out.push_back(p.substr(prefix.size(), p.size() - prefix.size() - 5));  // strip ".json"
    }
  }
  return out;
}

const Json& Pack::project_kind(std::string_view id) const { return file("project_kinds/" + std::string(id) + ".json"); }
const Json& Pack::facet(std::string_view id) const { return file("facets/" + std::string(id) + ".json"); }
const Json& Pack::artifact_type(std::string_view id) const { return file("artifact_types/" + std::string(id) + ".json"); }

namespace {
const Json& find_by_id(const Json& doc_, std::string_view list, std::string_view id, const Json& none) {
  const Json* a = json::find(doc_, list);
  if (!a || !a->is_array()) return none;
  for (const auto& x : *a) {
    if (json::get_string(x, "id") == id) return x;
  }
  return none;
}
}  // namespace

const Json& Pack::rule(std::string_view id) const {
  return find_by_id(file("rules/inference_rules.json"), "rules", id, empty_);
}
const Json& Pack::principle(std::string_view id) const {
  return find_by_id(file("philosophy/principles.json"), "principles", id, empty_);
}
const Json& Pack::op(std::string_view id) const {
  return find_by_id(file("philosophy/operators.json"), "operators", id, empty_);
}
const Json& Pack::policy(std::string_view name) const { return file("policy/" + std::string(name) + ".json"); }
const Json& Pack::lexicon(std::string_view name) const { return file("lexicons/" + std::string(name) + ".json"); }
const Json& Pack::profile(std::string_view id) const { return file("profiles/" + std::string(id) + ".json"); }
Json Pack::manifest() const {
  Json files = Json::array();
  for (const auto& [p, d] : docs_) {
    files.push_back(Json{{"path", p}, {"schema", json::get_string(d, "schema")}, {"sha256", Sha256::hex(json::canonical(d))}});
  }
  return Json{{"id", id()}, {"version", version()}, {"hash", hash_}, {"files", files}};
}

}  // namespace loom::kb
