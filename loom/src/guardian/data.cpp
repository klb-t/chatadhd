#include "internal.h"

#include <set>

#include "loom/runtime_profile.h"
#include "loom/util/sha256.h"
#include "loom/util/unicode.h"
#include "loom/util/utf8.h"
#include "text.h"

namespace loom::guardian::detail {
namespace {

void merge(Json& into, const Json& over) {
  if (!over.is_object() || !into.is_object()) {
    into = over;
    return;
  }
  for (auto it = over.begin(); it != over.end(); ++it) {
    if (into.contains(it.key()) && into[it.key()].is_object() && it.value().is_object()) merge(into[it.key()], it.value());
    else into[it.key()] = it.value();
  }
}

}  // namespace

Result<Data> load_data(const Config& cfg, const std::filesystem::path& root) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("guardian", root));
  Data d;
  d.p = profile.values();
  d.root = root;
  const Json mine = cfg.get("guardian_levels");
  if (mine.is_object() && !mine.empty()) {
    if (!d.p["levels"].contains("defs")) d.p["levels"]["defs"] = Json::object();
    merge(d.p["levels"]["defs"], mine);
  }
  d.fold = d.p.value("tokens", Json::object()).value("fold", Json::object());
  return d;
}

std::string say(const Data& d, const std::string& lang, const std::string& id, const Json& params) {
  const Json& texts = d.p.at("texts");
  const Json* table = json::find(texts, lang);
  const Json* t = table ? json::find(*table, id) : nullptr;
  if (!t || !t->is_string()) {
    table = json::find(texts, "en");
    t = table ? json::find(*table, id) : nullptr;
  }
  if (!t || !t->is_string()) return id;
  auto rendered = render_profile_template(t->get<std::string>(), params);
  return rendered ? *rendered : t->get<std::string>();
}

std::string scope_name(const Data& d, const std::string& lang, const std::string& scope) {
  const Json& names = d.p.value("scope_names", Json::object());
  for (const std::string& l : {lang, std::string("en")})
    if (const Json* t = json::find(names, l))
      if (const Json* s = json::find(*t, scope); s && s->is_string()) return s->get<std::string>();
  return scope;
}

std::string choose(const Config& cfg, const Json& section) {
  const Json v = cfg.get(json::get_string(section, "setting"));
  const Json& presets = section.at("presets");
  if (v.is_string() && !v.get<std::string>().empty())
    return presets.contains(v.get<std::string>()) ? v.get<std::string>() : json::get_string(section, "unknown", json::get_string(section, "active"));
  return json::get_string(section, "active");
}

bool is_traffic(const Data& d, const Message& m) {
  if (!m.metadata.is_object()) return false;
  for (const auto& key : text::strings_of(json::find(d.p.value("traffic", Json::object()), "ignore_metadata")))
    if (m.metadata.contains(key)) return true;
  return false;
}

std::string guess_lang(const Data& d, const std::string& txt) {
  for (char32_t c : utf8::decode(txt))
    if (c == 0x105 || c == 0x107 || c == 0x119 || c == 0x142 || c == 0x144 || c == 0xF3 || c == 0x15B || c == 0x17A || c == 0x17C) return "pl";
  const auto toks = text::tokenize(utf8::decode(txt), d.fold);
  std::size_t pl = 0, en = 0;
  const Json& kinds = d.p.at("kinds");
  for (auto it = kinds.begin(); it != kinds.end(); ++it) {
    const Json detect = it.value().value("detect", Json::object());  // value() copies: keep it alive while h points into it
    const Json* h = json::find(detect, "heuristic");
    const Json* markers = h ? json::find(*h, "markers") : nullptr;
    if (!markers) continue;
    pl += text::find(toks, text::compile(text::strings_of(json::find(*markers, "pl")), d.fold)).size();
    en += text::find(toks, text::compile(text::strings_of(json::find(*markers, "en")), d.fold)).size();
  }
  return pl > en ? "pl" : "en";
}

std::string clip_to(const std::string& s, std::size_t n) {
  std::string t(utf8::strip(s));
  return std::string(utf8::prefix(t, n));
}

std::string sha256_hex(const std::string& s) { return Sha256::hex(s); }

std::vector<std::string> subject_tokens(const Data& d, const std::string& txt) {
  std::set<std::string> stop;
  for (const std::string lang : {"pl", "en"})
    for (const auto& w : text::strings_of(json::find(d.p.value("tokens", Json::object()).value("stopwords", Json::object()), lang))) stop.insert(w);
  const std::size_t min_len = static_cast<std::size_t>(json::get_int(d.p.value("weighting", Json::object()), "min_token_len", 3));
  std::set<std::string> out;
  for (const auto& t : text::tokenize(utf8::decode(txt), d.fold))
    if (utf8::length(t.folded) >= min_len && !stop.count(t.folded)) out.insert(t.folded);
  return {out.begin(), out.end()};
}

bool negative(const Data& d, const std::string& txt) {
  std::vector<std::string> words;
  const Json& neg = d.p.value("polarity", Json::object()).value("negation", Json::object());
  for (auto it = neg.begin(); it != neg.end(); ++it)
    for (const auto& w : text::strings_of(&it.value())) words.push_back(w);
  return !text::find(text::tokenize(utf8::decode(txt), d.fold), text::compile(words, d.fold)).empty();
}

double overlap(const std::vector<std::string>& a, const std::vector<std::string>& b) {
  if (a.empty() || b.empty()) return 0;
  std::size_t common = 0;
  const std::set<std::string> sb(b.begin(), b.end());
  for (const auto& x : a) common += sb.count(x);
  return static_cast<double>(common) / static_cast<double>(std::min(a.size(), b.size()));
}

}  // namespace loom::guardian::detail
