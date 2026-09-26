// kb.h: data pack loading + validation (schema per file, closed sets,
// cross-file references). The validator IS the schema: pack files are data,
// what they may say is an invariant enforced here.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

#include "loom/kb.h"
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

void check_expected_property(V& v, const Json& o, const std::string& ptr) {
  if (!o.is_object()) {
    v.err(ptr, "expected an object");
    return;
  }
  if (const Json* e = v.member(o, ptr, "expr")) check_expr(v, *e, ptr + "/expr", kPredicates, "predicate");
  v.str(o, ptr, "rationale");
  v.strings(o, ptr, "confirm_if", false);
  v.strings(o, ptr, "refute_if", false);
}

// ── per-schema structural validation ────────────────────────────────
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
      v.str((*a)[i], p, "shape");
    }
  }
  if (const Json* e = v.object(d, "", "enums")) {
    for (const char* req : {"project_kind", "project_status", "component_status", "version_source", "item_type", "gt_evidence"}) {
      if (!e->contains(req)) v.err(std::string("/enums/") + req, "missing required enum");
    }
    for (auto it = e->begin(); it != e->end(); ++it) {
      if (!it.value().is_array() || it.value().empty()) v.err("/enums/" + it.key(), "expected a non-empty array");
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
    v.one_of(x, p, "kind", t.enum_values("project_kind"));
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
}

void v_principles(V& v, const Json& d) {
  const Json* a = v.array(d, "", "principles", true, true);
  if (!a) return;
  std::set<std::string> ids;
  for (std::size_t i = 0; i < a->size(); ++i) {
    const Json& x = (*a)[i];
    std::string p = idx("/principles", i);
    if (v.str(x, p, "id")) {
      std::string id = x["id"].get<std::string>();
      if (!ids.insert(id).second) v.err(p + "/id", "duplicate principle id");
      if (id.rfind("p.", 0) != 0) v.err(p + "/id", "principle ids start with 'p.'");
    }
    v.num(x, p, "prior", 0.01, 1);
    v.bilingual(x, p, "statement");
    v.strings(x, p, "phrasings", true, true);
    if (const Json* s = v.array(x, p, "sources", true, true)) {
      for (std::size_t k = 0; k < s->size(); ++k) {
        v.str((*s)[k], idx(p + "/sources", k), "doc");
        v.str((*s)[k], idx(p + "/sources", k), "section");
      }
    }
    if (const Json* ap = v.object(x, p, "applies_to")) v.strings(*ap, p + "/applies_to", "paradigms", true, true);
    v.object(x, p, "value_constraints");
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
    v.integer(x, p, "version", 1, 1000);
    v.integer(x, p, "stratum", 0, 2);
    v.closed(x, p, "produces", kRuleProduces);
    long stratum = json::get_int(x, "stratum", -1);
    std::string produces = json::get_string(x, "produces");
    static const char* kByStratum[] = {"derived", "inferred", "extrapolated"};
    if (stratum >= 0 && stratum <= 2 && produces != kByStratum[stratum]) {
      v.err(p + "/produces", "stratum " + std::to_string(stratum) + " must produce '" + kByStratum[stratum] + "'");
    }
    if (const Json* tg = v.object(x, p, "target")) {
      bool slot_target = tg->contains("paradigm");
      bool entity_target = tg->contains("entity_kind");
      if (slot_target == entity_target) v.err(p + "/target", "exactly one of paradigm+slot or entity_kind+attr");
      if (slot_target) v.str(*tg, p + "/target", "slot");
      if (entity_target) {
        v.one_of(*tg, p + "/target", "entity_kind", t.kinds);
        v.str(*tg, p + "/target", "attr");
      }
    }
    if (const Json* w = v.member(x, p, "when")) check_expr(v, *w, p + "/when", kConditionOps, "condition");
    if (const Json* w = v.member(x, p, "value")) check_expr(v, *w, p + "/value", kValueOps, "value");
    if (const Json* c = v.object(x, p, "confidence")) v.num(*c, p + "/confidence", "prior", 0.01, 1);
    if (const Json* e = v.member(x, p, "expected_property")) check_expected_property(v, *e, p + "/expected_property");
    v.object(x, p, "basis", false);
  }
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
  static const Evidence kAll[] = {Evidence::Observed, Evidence::Derived, Evidence::Inferred,
                                  Evidence::Extrapolated, Evidence::Absent, Evidence::User};
  if (const Json* ev = v.object(d, "", "evidence")) {
    for (Evidence e : kAll) {
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
  if (const Json* k = v.object(d, "", "kind")) {
    for (const auto& kind : t.kinds) {
      if (!k->contains(kind)) v.err("/kind/" + kind, "missing shape for entity kind");
    }
  }
  if (const Json* rc = v.object(d, "", "relation_category")) {
    for (const auto& cat : t.categories) {
      if (!rc->contains(cat)) v.err("/relation_category/" + cat, "missing encoding for relation category");
    }
  }
  if (const Json* cs = v.object(d, "", "check_state")) {
    for (const char* s : {"pending", "holds", "violated", "n/a"}) {
      if (!cs->contains(s)) v.err(std::string("/check_state/") + s, "missing check state");
    }
  }
  v.object(d, "", "conflict");
  if (v.strings(d, "", "legend_order", true, true)) {
    std::set<std::string> lo;
    for (const auto& x : d["legend_order"]) lo.insert(x.get<std::string>());
    if (lo.size() != 6) v.err("/legend_order", "must list the six evidence classes once each");
    for (const auto& x : lo) {
      if (!evidence_from_string(x)) v.err("/legend_order", "unknown evidence class " + x);
    }
  }
}

void v_binding(V& v, const Json& b, const std::string& p, const TypesVocab& t) {
  v.closed(b, p, "kind", kBindingKinds);
  std::string kind = json::get_string(b, "kind");
  if (kind == "subject") v.closed(b, p, "field", kSubjectFields);
  if (kind == "fact") {
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

void v_paradigm(V& v, const Json& d, const TypesVocab& t, std::string_view relpath) {
  std::string stem = fs::path(std::string(relpath)).stem().string();
  if (v.str(d, "", "id") && d["id"].get<std::string>() != stem) v.err("/id", "must equal the file name ('" + stem + "')");
  v.integer(d, "", "version", 1, 1000);
  v.str(d, "", "gt_paradigm");
  v.bilingual(d, "", "title");
  v.str(d, "", "description");
  if (v.str(d, "", "subject_kind")) {
    for (const auto& k : split(d["subject_kind"].get<std::string>(), '|')) {
      if (k != "owner" && !t.kinds.count(k)) v.err("/subject_kind", "unknown entity kind '" + k + "'");
    }
  }
  if (const Json* an = v.object(d, "", "anchors")) {
    v.num(*an, "/anchors", "min_score", 0, 1);
    bool any = false;
    for (const char* key : {"any", "all"}) {
      const Json* ops = json::find(*an, key);
      if (!ops) continue;
      any = true;
      if (!ops->is_array() || ops->empty()) {
        v.err(std::string("/anchors/") + key, "expected a non-empty array");
        continue;
      }
      for (std::size_t i = 0; i < ops->size(); ++i) {
        std::string p = idx(std::string("/anchors/") + key, i);
        const Json& o = (*ops)[i];
        v.closed(o, p, "op", kAnchorOps);
        std::string op = json::get_string(o, "op");
        if (op == "fact_count") {
          v.one_of(o, p, "rel", t.relations);
          v.integer(o, p, "min", 1, 1000);
        }
        if (op == "kind_hint") v.one_of(o, p, "value", t.enum_values("project_kind"));
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
    if (!any) v.err("/anchors", "needs 'any' and/or 'all'");
  }
  std::set<std::string> slot_names;
  if (const Json* slots = v.array(d, "", "slots", true, true)) {
    for (std::size_t i = 0; i < slots->size(); ++i) {
      const Json& s = (*slots)[i];
      std::string p = idx("/slots", i);
      if (v.str(s, p, "name") && !slot_names.insert(s["name"].get<std::string>()).second) {
        v.err(p + "/name", "duplicate slot");
      }
      if (v.str(s, p, "type") && !valid_slot_type(s["type"].get<std::string>(), t)) {
        v.err(p + "/type", "unknown slot type '" + s["type"].get<std::string>() + "'");
      }
      v.closed(s, p, "card", kCardinalities);
      if (const Json* r = json::find(s, "required"); r && !r->is_boolean()) v.err(p + "/required", "expected a boolean");
      v.num(s, p, "weight", 0.01, 100, false);
      if (json::get_string(s, "type") == "record[]") {
        if (const Json* f = v.object(s, p, "fields")) {
          for (auto it = f->begin(); it != f->end(); ++it) {
            std::string q = p + "/fields/" + it.key();
            if (v.str(it.value(), q, "type") && !valid_slot_type(it.value()["type"].get<std::string>(), t)) {
              v.err(q + "/type", "unknown field type");
            }
            if (it.value().contains("card")) v.closed(it.value(), q, "card", kCardinalities);
          }
        }
      }
      bool has_source = false;
      if (const Json* b = v.array(s, p, "bind", false)) {
        has_source = !b->empty();
        for (std::size_t k = 0; k < b->size(); ++k) v_binding(v, (*b)[k], idx(p + "/bind", k), t);
      }
      for (const char* key : {"infer", "extrapolate"}) {
        if (json::find(s, key)) {
          has_source = true;
          v.strings(s, p, key, true, true);
        }
      }
      if (!has_source) v.err(p, "slot has neither bindings nor rules");
    }
  }
  if (const Json* cs = v.array(d, "", "constraints")) {
    for (std::size_t i = 0; i < cs->size(); ++i) {
      std::string p = idx("/constraints", i);
      v.str((*cs)[i], p, "id");
      if (const Json* e = v.member((*cs)[i], p, "expr")) check_expr(v, *e, p + "/expr", kPredicates, "predicate");
      v.closed((*cs)[i], p, "on_violation", kViolationActions);
    }
  }
  if (const Json* r = v.object(d, "", "render")) {
    if (const Json* o = v.array(*r, "/render", "order")) {
      for (const auto& x : *o) {
        if (!x.is_string() || !slot_names.count(x.get<std::string>())) v.err("/render/order", "unknown slot " + json::dump(x));
      }
    }
    if (const Json* ts = json::find(*r, "title_slot"); ts && !ts->is_null()) {
      if (!ts->is_string() || !slot_names.count(ts->get<std::string>())) v.err("/render/title_slot", "unknown slot");
    }
  }
  if (json::find(d, "extrapolate")) v.strings(d, "", "extrapolate", true, true);
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
  else if (schema == "loom.kb.principles/1") v_principles(v, d);
  else if (schema == "loom.kb.rules/1") v_rules(v, d, t);
  else if (schema == "loom.kb.checks/1") v_checks(v, d);
  else if (schema == "loom.kb.thresholds/1") v_thresholds(v, d);
  else if (schema == "loom.kb.relevance/1") v_relevance(v, d);
  else if (schema == "loom.kb.selection_rules/1") v_selection_rules(v, d);
  else if (schema == "loom.kb.calibration/1") v_calibration(v, d);
  else if (schema == "loom.kb.evidence_encoding/1") v_evidence_encoding(v, d, t);
  else if (schema == "loom.kb.paradigm/1") v_paradigm(v, d, t, relpath);
  else v.err("/schema", "unknown schema id '" + std::string(schema) + "'");
  return issues.size() == before;
}

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

  // Collect ids.
  std::map<std::string, const Json*> paradigms;  // id -> doc
  for (const auto& [p, d] : docs) {
    if (json::get_string(d, "schema") == "loom.kb.paradigm/1") paradigms[json::get_string(d, "id")] = &d;
  }
  auto paradigm_slot = [&](const std::string& pid, const std::string& slot) -> const Json* {
    auto it = paradigms.find(pid);
    if (it == paradigms.end()) return nullptr;
    const Json* slots = json::find(*it->second, "slots");
    if (!slots || !slots->is_array()) return nullptr;
    for (const auto& s : *slots) {
      if (json::get_string(s, "name") == slot) return &s;
    }
    return nullptr;
  };
  std::map<std::string, const Json*> rules;
  if (const Json* r = json::find(doc(docs, "rules/inference_rules.json"), "rules"); r && r->is_array()) {
    for (const auto& x : *r) rules[json::get_string(x, "id")] = &x;
  }
  std::set<std::string> principles;
  if (const Json* r = json::find(doc(docs, "philosophy/seed_principles.json"), "principles"); r && r->is_array()) {
    principles = names_of(*r, "id");
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
  TypesVocab t(doc(docs, "schema/types.json").is_object() ? doc(docs, "schema/types.json") : Json::object());

  auto check_slot_ref = [&](const std::string& file, const std::string& ptr, const std::string& ref) {
    // "$slot:<paradigm>.<slot>[...]" -> the paradigm/slot must exist.
    if (ref.rfind("$slot:", 0) != 0) return;
    std::string body = ref.substr(6);
    std::size_t cut = body.find_first_of("[");
    if (cut != std::string::npos) body = body.substr(0, cut);
    std::size_t dot = body.find('.');
    if (dot == std::string::npos) return;
    std::string pid = body.substr(0, dot);
    std::string slot = body.substr(dot + 1);
    if (!paradigms.count(pid)) return;  // "$slot:x.y" with x not a paradigm is a same-instance path
    if (!paradigm_slot(pid, slot)) issues.push_back({file, ptr, "unknown slot reference " + ref});
  };
  std::function<void(const std::string&, const std::string&, const Json&)> walk_refs =
      [&](const std::string& file, const std::string& ptr, const Json& j) {
        if (j.is_string()) {
          check_slot_ref(file, ptr, j.get<std::string>());
        } else if (j.is_array()) {
          for (std::size_t i = 0; i < j.size(); ++i) walk_refs(file, idx(ptr, i), j[i]);
        } else if (j.is_object()) {
          for (auto it = j.begin(); it != j.end(); ++it) walk_refs(file, ptr + "/" + it.key(), it.value());
        }
      };

  // Paradigms: bindings, rules, cue classes.
  for (const auto& [pid, dp] : paradigms) {
    std::string file = "paradigms/" + pid + ".json";
    if (const Json* an = json::find(*dp, "anchors")) {
      for (const char* key : {"any", "all"}) {
        const Json* ops = json::find(*an, key);
        if (!ops || !ops->is_array()) continue;
        for (std::size_t i = 0; i < ops->size(); ++i) {
          if (json::get_string((*ops)[i], "op") == "cue" && !cue_classes.count(json::get_string((*ops)[i], "class"))) {
            issues.push_back({file, idx(std::string("/anchors/") + key, i) + "/class", "unknown cue class"});
          }
        }
      }
    }
    const Json* slots = json::find(*dp, "slots");
    if (!slots || !slots->is_array()) continue;
    for (std::size_t i = 0; i < slots->size(); ++i) {
      const Json& s = (*slots)[i];
      std::string p = idx("/slots", i);
      std::string sname = json::get_string(s, "name");
      if (const Json* b = json::find(s, "bind"); b && b->is_array()) {
        for (std::size_t k = 0; k < b->size(); ++k) {
          const Json& x = (*b)[k];
          std::string kind = json::get_string(x, "kind");
          if (kind == "lexicon") {
            std::string cls = json::get_string(x, "class");
            if (!lex_classes.count(cls) && !cue_classes.count(cls)) {
              issues.push_back({file, idx(p + "/bind", k) + "/class", "unknown lexicon class '" + cls + "'"});
            }
          }
          if (kind == "slot") {
            std::string op = json::get_string(x, "paradigm");
            if (!paradigm_slot(op, json::get_string(x, "slot"))) {
              issues.push_back({file, idx(p + "/bind", k), "unknown paradigm slot " + op + "." + json::get_string(x, "slot")});
            }
          }
        }
      }
      for (const char* key : {"infer", "extrapolate"}) {
        const Json* ids = json::find(s, key);
        if (!ids || !ids->is_array()) continue;
        for (const auto& rid : *ids) {
          std::string r = rid.is_string() ? rid.get<std::string>() : "";
          auto it = rules.find(r);
          if (it == rules.end()) {
            issues.push_back({file, p + "/" + key, "unknown rule '" + r + "'"});
            continue;
          }
          std::string produces = json::get_string(*it->second, "produces");
          bool extra = std::string(key) == "extrapolate";
          if (extra != (produces == "extrapolated")) {
            issues.push_back({file, p + "/" + key, "rule '" + r + "' produces " + produces + " (infer: derived|inferred, extrapolate: extrapolated)"});
          }
          const Json& tg = (*it->second)["target"];
          std::string tp = json::get_string(tg, "paradigm");
          auto tps = split(tp, '|');
          bool covers = tp == "*" || std::find(tps.begin(), tps.end(), pid) != tps.end();
          if (!covers || json::get_string(tg, "slot") != sname) {
            issues.push_back({file, p + "/" + key, "rule '" + r + "' targets " + tp + "." + json::get_string(tg, "slot")});
          }
        }
      }
    }
    if (const Json* ids = json::find(*dp, "extrapolate"); ids && ids->is_array()) {
      for (const auto& rid : *ids) {
        if (!rid.is_string() || !rules.count(rid.get<std::string>())) {
          issues.push_back({file, "/extrapolate", "unknown rule " + json::dump(rid)});
        }
      }
    }
    walk_refs(file, "", *dp);
  }

  // Rules: targets, principles, slot references.
  for (const auto& [rid, rp] : rules) {
    const std::string file = "rules/inference_rules.json";
    const Json& tg = (*rp)["target"];
    if (tg.contains("paradigm")) {
      std::string tp = json::get_string(tg, "paradigm");
      std::string slot = json::get_string(tg, "slot");
      bool found = false;
      for (const auto& pid : tp == "*" ? std::vector<std::string>{} : split(tp, '|')) {
        if (!paradigms.count(pid)) {
          issues.push_back({file, "/rules[" + rid + "]/target", "unknown paradigm '" + pid + "'"});
        } else if (!paradigm_slot(pid, slot)) {
          issues.push_back({file, "/rules[" + rid + "]/target", "paradigm " + pid + " has no slot '" + slot + "'"});
        } else {
          found = true;
        }
      }
      if (tp == "*") {
        for (const auto& [pid, _] : paradigms) found = found || paradigm_slot(pid, slot);
      }
      if (!found && tp == "*") issues.push_back({file, "/rules[" + rid + "]/target", "no paradigm has slot '" + slot + "'"});
      if (tg.contains("field") && tp != "*") {
        for (const auto& pid : split(tp, '|')) {
          const Json* s = paradigm_slot(pid, slot);
          if (s && !(json::find(*s, "fields") && (*s)["fields"].contains(json::get_string(tg, "field")))) {
            issues.push_back({file, "/rules[" + rid + "]/target/field", "unknown record field"});
          }
        }
      }
    }
    std::function<void(const Json&)> find_principles = [&](const Json& e) {
      if (e.is_object()) {
        if (json::get_string(e, "op") == "principle_active" || json::get_string(e, "op") == "consistent_with") {
          if (const Json* a = json::find(e, "args"); a && a->is_array() && !a->empty() && (*a)[0].is_string()) {
            std::string pid = (*a)[0].get<std::string>();
            if (!principles.count(pid)) issues.push_back({file, "/rules[" + rid + "]", "unknown principle '" + pid + "'"});
          }
        }
        for (auto it = e.begin(); it != e.end(); ++it) find_principles(it.value());
      } else if (e.is_array()) {
        for (const auto& x : e) find_principles(x);
      }
    };
    find_principles(*rp);
    if (const Json* b = json::find(*rp, "basis")) {
      if (const Json* ps = json::find(*b, "principles"); ps && ps->is_array()) {
        for (const auto& x : *ps) {
          if (!x.is_string() || !principles.count(x.get<std::string>())) {
            issues.push_back({file, "/rules[" + rid + "]/basis", "unknown principle " + json::dump(x)});
          }
        }
      }
    }
    walk_refs(file, "/rules[" + rid + "]", *rp);
    // Extrapolations stay below the cap.
    if (json::get_string(*rp, "produces") == "extrapolated") {
      double cap = json::get_number(doc(docs, "policy/thresholds.json")["paradigm"], "extrapolation_cap", 0.5);
      if (json::get_number((*rp)["confidence"], "prior", 1.0) > cap) {
        issues.push_back({file, "/rules[" + rid + "]/confidence", "extrapolation prior above thresholds.paradigm.extrapolation_cap"});
      }
    }
  }

  // Principles <-> checks <-> paradigms.
  if (const Json* r = json::find(doc(docs, "philosophy/seed_principles.json"), "principles"); r && r->is_array()) {
    for (const auto& x : *r) {
      std::string pid = json::get_string(x, "id");
      if (const Json* ap = json::find(x, "applies_to")) {
        if (const Json* ps = json::find(*ap, "paradigms"); ps && ps->is_array()) {
          for (const auto& y : *ps) {
            if (!y.is_string() || !paradigms.count(y.get<std::string>())) {
              issues.push_back({"philosophy/seed_principles.json", "/principles[" + pid + "]/applies_to", "unknown paradigm " + json::dump(y)});
            }
          }
        }
      }
      if (const Json* vc = json::find(x, "value_constraints")) {
        if (const Json* fc = json::find(*vc, "forbid_checks"); fc && fc->is_array()) {
          for (const auto& y : *fc) {
            if (!y.is_string() || !checks.count(y.get<std::string>())) {
              issues.push_back({"philosophy/seed_principles.json", "/principles[" + pid + "]/value_constraints", "unknown check " + json::dump(y)});
            }
          }
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
  // Profile projects referenced by selection rules exist ("owner" = philosophy probe).
  std::set<std::string> projects;
  if (const Json* pr = json::find(doc(docs, "profiles/self.json"), "projects"); pr && pr->is_array()) projects = names_of(*pr, "id");
  if (const Json* rs = json::find(doc(docs, "policy/selection_rules.json"), "rules"); rs && rs->is_array()) {
    for (const auto& x : *rs) {
      std::string pj = json::get_string(x["match"], "project");
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

Json Pack::paradigm_ids() const {
  Json out = Json::array();
  for (const auto& [p, d] : docs_) {
    if (json::get_string(d, "schema") == "loom.kb.paradigm/1") out.push_back(json::get_string(d, "id"));
  }
  return out;
}

const Json& Pack::paradigm(std::string_view id) const { return file("paradigms/" + std::string(id) + ".json"); }

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
  return find_by_id(file("philosophy/seed_principles.json"), "principles", id, empty_);
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
