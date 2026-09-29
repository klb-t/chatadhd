// Pack resources of the extractor (see lexicon.h). Deterministic.
#include "extract/lexicon.h"

#include <algorithm>

#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom::extract::detail {

namespace {

char32_t cp_before(std::string_view s, std::size_t pos) {
  if (pos == 0 || pos > s.size()) return 0;
  std::size_t i = pos - 1;
  while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
  auto d = utf8::decode(s.substr(i, pos - i));
  return d.empty() ? 0 : d.back();
}

char32_t cp_at(std::string_view s, std::size_t pos, std::size_t* n = nullptr) {
  if (pos >= s.size()) return 0;
  unsigned char c = static_cast<unsigned char>(s[pos]);
  std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
  len = std::min(len, s.size() - pos);
  if (n) *n = len;
  auto d = utf8::decode(s.substr(pos, len));
  return d.empty() ? 0 : d.front();
}

bool wordish(char32_t c) { return c != 0 && (unicode::is_alnum(c) || c == U'_'); }

std::vector<std::string> strings_of(const Json& j) {
  std::vector<std::string> out;
  if (!j.is_array()) return out;
  for (const auto& x : j) {
    if (x.is_string()) out.push_back(x.get<std::string>());
  }
  return out;
}

std::vector<std::string> split_ws(std::string_view s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == ' ' || c == '\t') {
      if (!cur.empty()) out.push_back(std::move(cur));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) out.push_back(std::move(cur));
  return out;
}

}  // namespace

Phrase make_phrase(const kb::Normalizer& norm, std::string_view p, double w) {
  Phrase ph;
  std::string s(utf8::strip(p));
  if (s.size() > 1 && s.back() == '*') {
    ph.prefix = true;
    s.pop_back();
  }
  ph.text = norm.fold(s);
  ph.w = w;
  return ph;
}

std::size_t find_phrase(std::string_view text, const Phrase& p, std::size_t from, std::size_t* len) {
  if (p.text.empty()) return std::string_view::npos;
  const bool lead_word = wordish(cp_at(p.text, 0));
  const bool tail_word = wordish(cp_before(p.text, p.text.size()));
  for (std::size_t pos = text.find(p.text, from); pos != std::string_view::npos; pos = text.find(p.text, pos + 1)) {
    if (lead_word && wordish(cp_before(text, pos))) continue;
    std::size_t end = pos + p.text.size();
    if (p.prefix) {
      std::size_t n = 0;
      while (end < text.size() && wordish(cp_at(text, end, &n))) end += n;
    } else if (tail_word && wordish(cp_at(text, end))) {
      continue;
    }
    if (len) *len = end - pos;
    return pos;
  }
  return std::string_view::npos;
}

std::vector<Token> tokenize(std::string_view text, bool keep_numbers) {
  std::vector<Token> out;
  std::size_t i = 0;
  std::size_t start = 0;
  bool in = false;
  bool has_alpha = false;
  auto flush = [&](std::size_t end) {
    if (in && (has_alpha || keep_numbers)) {
      Token t;
      t.surface = std::string(text.substr(start, end - start));
      t.lower = utf8::to_lower(t.surface);
      t.start = start;
      t.end = end;
      out.push_back(std::move(t));
    }
    in = false;
    has_alpha = false;
  };
  while (i < text.size()) {
    std::size_t n = 1;
    char32_t c = cp_at(text, i, &n);
    if (unicode::is_alnum(c)) {
      if (!in) {
        in = true;
        start = i;
      }
      if (unicode::is_alpha(c)) has_alpha = true;
    } else if ((c == U'+' || c == U'#') && in && has_alpha) {
      // "c++", "c#"
    } else {
      flush(i);
    }
    i += n;
  }
  flush(text.size());
  return out;
}

Lexicons::Lexicons(const kb::Pack& pack, const Json& discovered) : norm(pack) {
  // cue classes
  if (const Json* c = json::find(pack.lexicon("cues"), "classes"); c && c->is_object()) {
    for (auto it = c->begin(); it != c->end(); ++it) {
      auto& v = cues_[it.key()];
      if (const Json* ps = json::find(it.value(), "phrases"); ps && ps->is_array()) {
        for (const auto& p : *ps) v.push_back(make_phrase(norm, json::get_string(p, "p"), json::get_number(p, "w", 1.0)));
      }
    }
  }
  for (const auto& p : phrases("negated_item")) neg_particles_.push_back(p);
  // item cues
  const Json& ic = pack.lexicon("item_cues");
  item_types = strings_of(json::find(ic, "type_order") ? ic["type_order"] : Json());
  if (const Json* a = json::find(ic, "cues"); a && a->is_array()) {
    for (const auto& x : *a) {
      item_cues[json::get_string(x, "type")].push_back(
          make_phrase(norm, json::get_string(x, "phrase"), json::get_number(x, "weight", 1.0)));
    }
  }
  if (const Json* a = json::find(ic, "heading_hints"); a && a->is_array()) {
    for (const auto& x : *a) {
      heading_hints.emplace_back(json::get_string(x, "type"),
                                 make_phrase(norm, json::get_string(x, "phrase"), json::get_number(x, "weight", 1.0)));
    }
  }
  for (const auto& s : strings_of(json::find(ic, "negators") ? ic["negators"] : Json())) negators.insert(norm.fold(s));
  for (const auto& s : strings_of(json::find(ic, "generic_subject_words") ? ic["generic_subject_words"] : Json())) {
    generic_words.insert(norm.fold(s));
  }

  // gazetteer
  const Json& gz = pack.lexicon("gazetteer");
  if (const Json* cl = json::find(gz, "classes"); cl && cl->is_object()) {
    for (auto it = cl->begin(); it != cl->end(); ++it) class_kind[it.key()] = json::get_string(it.value(), "entity_kind", "concept");
  }
  if (const Json* es = json::find(gz, "entries"); es && es->is_array()) {
    for (const auto& e : *es) {
      LexEntry le;
      le.id = json::get_string(e, "id");
      le.cls = json::get_string(e, "class");
      auto ck = class_kind.find(le.cls);
      le.kind = ck != class_kind.end() ? ck->second : "concept";
      const Json* labels = json::find(e, "labels");
      le.label = labels ? json::get_string(*labels, "en", le.id) : le.id;
      le.label_pl = labels ? json::get_string(*labels, "pl") : "";
      le.canonical = norm.phrase_key(le.label);
      if (le.canonical.empty()) le.canonical = norm.fold(le.id);
      le.source = "gazetteer";
      std::size_t idx = entries.size();
      entries.push_back(le);
      std::set<std::string> amb;
      for (const auto& a : strings_of(json::find(e, "ambiguous") ? e["ambiguous"] : Json())) amb.insert(norm.fold(a));
      std::vector<std::string> req;
      for (const auto& r : strings_of(json::find(e, "requires_context") ? e["requires_context"] : Json())) req.push_back(norm.fold(r));
      for (const auto& f : strings_of(json::find(e, "forms") ? e["forms"] : Json())) {
        LexForm lf;
        lf.ambiguous = amb.count(norm.fold(f)) > 0;
        if (lf.ambiguous) lf.requires_ctx = req;
        add_form(idx, f, lf);
      }
      add_form(idx, le.label, LexForm{});
    }
  }
  // profile projects (the self-profile; ambiguous aliases are context-gated)
  const Json& prof = pack.profile("self");
  if (const Json* ps = json::find(prof, "projects"); ps && ps->is_array()) {
    for (const auto& p : *ps) {
      LexEntry le;
      le.id = json::get_string(p, "id");
      le.cls = "project";
      le.kind = "project";
      le.label = json::get_string(p, "name", le.id);
      le.canonical = norm.phrase_key(le.label);
      le.source = "profile";
      if (const Json* m = json::find(p, "merged_into"); m && m->is_string()) le.merged_into = m->get<std::string>();
      std::size_t idx = entries.size();
      entries.push_back(le);
      if (const Json* as = json::find(p, "aliases"); as && as->is_array()) {
        for (const auto& a : *as) {
          LexForm lf;
          lf.ambiguous = json::get_bool(a, "ambiguous");
          if (const Json* rc = json::find(a, "requires_context"); rc && rc->is_object()) {
            for (const auto& t : strings_of(json::find(*rc, "any") ? (*rc)["any"] : Json())) lf.requires_ctx.push_back(norm.fold(t));
            lf.ctx_min = static_cast<int>(json::get_int(*rc, "min", 1));
          }
          for (const auto& t : strings_of(json::find(a, "negative_context") ? a["negative_context"] : Json())) {
            lf.negative_ctx.push_back(norm.fold(t));
          }
          add_form(idx, json::get_string(a, "t"), lf);
        }
      }
    }
  }
  // names discovered in the run's other units
  if (discovered.is_array()) {
    for (const auto& d : discovered) {
      LexEntry le;
      le.label = json::get_string(d, "label");
      if (le.label.empty()) continue;
      le.kind = json::get_string(d, "kind", "project");
      le.cls = le.kind;
      le.canonical = norm.phrase_key(le.label);
      if (le.canonical.empty()) continue;
      le.id = "discovered:" + le.kind + ":" + le.canonical;
      le.source = "discovered";
      std::size_t idx = entries.size();
      entries.push_back(le);
      LexForm lf;
      lf.inflect = true;
      add_form(idx, le.label, lf);
      for (const auto& a : strings_of(json::find(d, "aliases") ? d["aliases"] : Json())) add_form(idx, a, lf);
    }
  }

  // versions
  const Json& vp = pack.lexicon("version_patterns");
  if (auto r = re::Regex::compile(json::get_string(vp, "regex", "\\b[vV]?(\\d{1,2}\\.\\d{1,2}(?:\\.\\d{1,3})?)\\b"))) {
    version_re = std::move(*r);
  }
  version_window = static_cast<int>(json::get_int(vp, "window_tokens", 12));
  if (const Json* an = json::find(vp, "anchors"); an && an->is_object()) {
    for (auto it = an->begin(); it != an->end(); ++it) {
      for (const auto& s : strings_of(it.value())) version_anchors.push_back(norm.fold(s));
    }
  }
  if (const Json* ds = json::find(vp, "declarations"); ds && ds->is_array()) {
    for (const auto& d : *ds) {
      auto r = re::Regex::compile(json::get_string(d, "regex"), re::kMultiline);
      if (!r) continue;
      version_decls.push_back(VersionDecl{json::get_string(d, "id"), std::move(*r), json::get_number(d, "confidence", 0.8)});
    }
  }
  for (const auto& s : strings_of(json::find(vp, "exclude_contexts") ? vp["exclude_contexts"] : Json())) {
    version_excludes.push_back(norm.fold(s));
  }

  // name plausibility
  name_rules_ = pack.lexicon("name_rules");
  if (const Json* a = json::find(name_rules_, "all")) {
    if (const Json* rr = json::find(*a, "reject_regex"); rr && rr->is_array()) {
      for (const auto& x : *rr) {
        if (!x.is_string()) continue;
        if (auto r = re::Regex::compile(x.get<std::string>())) name_reject_.push_back(std::move(*r));
      }
    }
  }

  // relation patterns
  if (const Json* ps = json::find(pack.lexicon("relation_patterns"), "patterns"); ps && ps->is_array()) {
    for (const auto& p : *ps) {
      RelPattern rp;
      rp.id = json::get_string(p, "id");
      rp.rel = json::get_string(p, "rel");
      rp.confidence = json::get_number(p, "confidence", 0.5);
      rp.subject_ref = json::get_string(p, "subject");
      std::string t = json::get_string(p, "template");
      std::size_t i = 0;
      bool ok = true;
      while (i < t.size() && ok) {
        char c = t[i];
        if (c == ' ') {
          ++i;
        } else if (c == '{') {
          std::size_t e = t.find('}', i);
          if (e == std::string::npos) {
            ok = false;
            break;
          }
          std::string body = t.substr(i + 1, e - i - 1);
          PatElem el;
          el.kind = PatElem::Slot;
          std::size_t colon = body.find(':');
          el.slot = body.substr(0, colon);
          std::string types = colon == std::string::npos ? "any" : body.substr(colon + 1);
          if (!types.empty() && types.back() == '+') {
            el.plus = true;
            types.pop_back();
          }
          std::size_t s = 0;
          while (s <= types.size()) {
            std::size_t bar = types.find('|', s);
            if (bar == std::string::npos) bar = types.size();
            el.types.push_back(types.substr(s, bar - s));
            s = bar + 1;
          }
          rp.elems.push_back(std::move(el));
          i = e + 1;
        } else if (c == '(') {
          std::size_t e = t.find(')', i);
          if (e == std::string::npos) {
            ok = false;
            break;
          }
          std::string body = t.substr(i + 1, e - i - 1);
          PatElem el;
          el.kind = PatElem::Alt;
          std::size_t s = 0;
          while (s <= body.size()) {
            std::size_t bar = body.find('|', s);
            if (bar == std::string::npos) bar = body.size();
            std::vector<Phrase> words;
            for (const auto& w : split_ws(body.substr(s, bar - s))) words.push_back(make_phrase(norm, w, 1.0));
            if (!words.empty()) el.alts.push_back(std::move(words));
            s = bar + 1;
          }
          rp.elems.push_back(std::move(el));
          i = e + 1;
        } else {
          std::size_t e = t.find(' ', i);
          if (e == std::string::npos) e = t.size();
          std::string w = t.substr(i, e - i);
          PatElem el;
          if (w == "*") {
            el.kind = PatElem::Wild;
          } else {
            el.kind = PatElem::Literal;
            el.alts.push_back({make_phrase(norm, w, 1.0)});
          }
          rp.elems.push_back(std::move(el));
          i = e;
        }
      }
      if (ok && !rp.elems.empty()) patterns.push_back(std::move(rp));
    }
  }

  thresholds = pack.policy("thresholds");
  if (const Json* c = json::find(thresholds, "catalog")) context_window = static_cast<int>(json::get_int(*c, "context_window_tokens", 30));
  if (const Json* s = json::find(thresholds, "status")) status_min_weight = json::get_number(*s, "explicit_cue_min_weight", 1.5);
}

void Lexicons::add_form(std::size_t entry, std::string_view surface, LexForm f) {
  if (utf8::is_blank(surface)) return;
  f.entry = entry;
  f.surface = std::string(surface);
  std::set<std::string> keys;
  for (kb::Lang l : {kb::Lang::Unknown, kb::Lang::En, kb::Lang::Pl}) {
    std::string k = norm.phrase_key(surface, false, l);
    if (!k.empty()) keys.insert(k);
  }
  const std::size_t words = norm.tokens(surface).size();
  for (const auto& k : keys) {
    std::size_t n = static_cast<std::size_t>(std::count(k.begin(), k.end(), ' ')) + 1;
    // a multi-word form whose key collapses to one word ("projekt
    // projektów" -> "projekt") would match every mention of that word
    if (words >= 2 && n < 2) continue;
    f.tokens = n;
    max_form_tokens = std::max(max_form_tokens, n);
    first_keys.insert(k.substr(0, k.find(' ')));
    auto& v = forms[k];
    bool dup = false;
    for (const auto& x : v) dup = dup || (x.entry == entry && x.surface == f.surface);
    if (!dup) v.push_back(f);
    if (f.inflect && n == 1 && utf8::length(k) >= 4) inflecting.emplace_back(k, f);
  }
}

const std::vector<Phrase>& Lexicons::phrases(std::string_view cls) const {
  auto it = cues_.find(std::string(cls));
  return it == cues_.end() ? empty_ : it->second;
}

std::vector<std::string> Lexicons::classes_with_prefix(std::string_view prefix) const {
  std::vector<std::string> out;
  for (const auto& [k, v] : cues_) {
    if (k.rfind(prefix, 0) == 0) out.push_back(k);
  }
  return out;
}

bool Lexicons::negated_at(std::string_view folded, std::size_t pos) const {
  // The two words before `pos`, not crossing a clause delimiter.
  std::size_t from = pos > 48 ? pos - 48 : 0;
  std::string_view window = folded.substr(from, pos - from);
  std::size_t cut = window.find_last_of(",.;:!?()-");
  if (cut != std::string_view::npos) window.remove_prefix(cut + 1);
  std::vector<Token> toks = tokenize(window);
  std::size_t n = toks.size();
  for (std::size_t k = n >= 2 ? n - 2 : 0; k < n; ++k) {
    for (const auto& p : neg_particles_) {
      if (toks[k].lower == p.text) return true;
    }
  }
  return false;
}

std::vector<CueHit> Lexicons::match(std::string_view cls, std::string_view folded) const {
  std::vector<CueHit> out;
  for (const auto& p : phrases(cls)) {
    std::size_t from = 0;
    std::size_t len = 0;
    for (std::size_t pos = find_phrase(folded, p, from, &len); pos != std::string_view::npos;
         pos = find_phrase(folded, p, from, &len)) {
      CueHit h;
      h.phrase = p.text;
      h.w = p.w;
      h.pos = pos;
      h.len = len;
      // A negation phrase ("nie działa") is its own cue; otherwise look back.
      bool self_negative = false;
      for (const auto& n : neg_particles_) self_negative = self_negative || p.text.rfind(n.text + " ", 0) == 0 || p.text == n.text;
      h.negated = !self_negative && negated_at(folded, pos);
      out.push_back(std::move(h));
      from = pos + std::max<std::size_t>(len, 1);
    }
  }
  // Longer phrases win overlaps ("must not" over "must").
  std::sort(out.begin(), out.end(), [](const CueHit& a, const CueHit& b) {
    if (a.pos != b.pos) return a.pos < b.pos;
    return a.len > b.len;
  });
  std::vector<CueHit> kept;
  std::size_t covered = 0;
  for (auto& h : out) {
    if (!kept.empty() && h.pos < covered) {
      if (h.pos == kept.back().pos && h.len > kept.back().len) kept.back() = h;
      continue;
    }
    covered = h.pos + h.len;
    kept.push_back(std::move(h));
  }
  return kept;
}

double Lexicons::score(std::string_view cls, std::string_view folded) const {
  double s = 0.0;
  for (const auto& h : match(cls, folded)) {
    if (!h.negated) s += h.w;
  }
  return s;
}

bool Lexicons::name_ok(std::string_view kind, std::string_view label, std::string_view artifact_type, bool strict) const {
  std::string l(utf8::strip(label));
  if (l.empty()) return false;
  const std::u32string u = utf8::decode(l);
  if (const Json* all = json::find(name_rules_, "all")) {
    const std::u32string cs = utf8::decode(json::get_string(*all, "reject_chars"));
    for (char32_t ch : u) {
      if (cs.find(ch) != std::u32string::npos) return false;
    }
    for (const auto& s : strings_of(json::find(*all, "reject_substrings") ? (*all)["reject_substrings"] : Json())) {
      if (l.find(s) != std::string::npos) return false;
    }
    std::string low = fold(l);
    for (const auto& s : strings_of(json::find(*all, "reject_suffixes") ? (*all)["reject_suffixes"] : Json())) {
      if (low.size() > s.size() && low.compare(low.size() - s.size(), s.size(), s) == 0) return false;
    }
    auto toks = norm.tokens(l);
    if (!toks.empty()) {
      for (const auto& s : strings_of(json::find(*all, "reject_leading_tokens") ? (*all)["reject_leading_tokens"] : Json())) {
        if (toks.front() == s) return false;
      }
    }
    for (const auto& r : name_reject_) {
      if (r.search(u)) return false;
    }
  }
  if (kind == "project" && strict) {
    if (const Json* p = json::find(name_rules_, "project")) {
      // Casual media (chat, mail, transcripts) name projects by lowercase
      // descriptions ("generator reelsow"); the strict rules are for
      // technical text where a bare lowercase noun is a term, not a name.
      for (const auto& t : strings_of(json::find(*p, "lenient_types") ? (*p)["lenient_types"] : Json())) {
        if (t == artifact_type) return true;
      }
      auto toks = norm.tokens(l);
      if (toks.empty()) return false;
      if (u.size() < static_cast<std::size_t>(json::get_int(*p, "min_chars", 0))) return false;
      if (toks.size() > static_cast<std::size_t>(json::get_int(*p, "max_tokens", 99))) return false;
      if (json::get_bool(*p, "require_upper")) {
        bool up = false;
        for (unsigned char ch : l) up = up || std::isupper(ch);
        if (!up) return false;
      }
      if (json::get_bool(*p, "reject_stopword_edges") && (norm.is_stopword(toks.front()) || norm.is_stopword(toks.back()))) return false;
    }
  }
  return true;
}

}  // namespace loom::extract::detail
