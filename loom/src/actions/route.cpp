// input.route_intent: message or command (R54). Data: runtime profile intent_routing, `phrases` on actions.
#include "loom/chat_route.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "loom/action_element.h"
#include "loom/action_overlay.h"
#include "loom/action_result.h"
#include "loom/action_run.h"
#include "loom/action_runner.h"
#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/provenance.h"
#include "loom/re/regex.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/settings_fields.h"
#include "loom/use_model.h"
#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom::chat_route {

namespace {

using actions::ActionRunner;
using actions::ElementResolver;
using actions::ElementSources;
using actions::Engine;

constexpr std::string_view kProposalSchema = "loom.action_proposal/1";

std::vector<std::string> strings_of(const Json* v) {
  std::vector<std::string> out;
  if (v && v->is_array())
    for (const auto& x : *v)
      if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}

struct Data {
  Json cfg;
  Engine engine;
  ElementResolver resolver;
  std::set<std::string> setting_keys;
  std::filesystem::path root;
  Json results = Json::object();  // runtime profile action_results (link names, ...)
};

Result<Data> load_data(Runtime& rt) {
  const auto root = rt.paths().root;
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("intent_routing", root));
  LOOM_TRY_ASSIGN(auto engine, Engine::load(root));
  engine.set_user_values(rt.config().all());  // the user's settings are the person level of scoped values
  LOOM_TRY_ASSIGN(auto resolver, ElementResolver::load(root));
  std::set<std::string> keys;
  if (auto fields = settings_fields(root))
    if (const Json* list = json::find(*fields, "fields"); list && list->is_array())
      for (const auto& f : *list) keys.insert(json::get_string(f, "key"));
  Json results = Json::object();
  if (auto p = RuntimeProfile::load("action_results", root)) results = p->values();
  return Data{profile.values(), std::move(engine), std::move(resolver), std::move(keys), root, std::move(results)};
}

ElementSources sources_of(Runtime& rt) { return ElementSources{&rt.db(), &rt.blobs(), rt.paths().root}; }

// ── text handling ───────────────────────────────────────────────────

std::string fold(std::string_view text, const Json& map) {
  const std::string lowered = utf8::to_lower(text);
  std::string out;
  for (char32_t cp : utf8::decode(lowered)) {
    const std::string one = utf8::encode(cp);
    const Json* repl = json::find(map, one);
    out += repl && repl->is_string() ? repl->get<std::string>() : one;
  }
  return out;
}

std::vector<std::string> word_tokens(const std::string& folded) {
  std::vector<std::string> out;
  std::u32string cur;
  for (char32_t cp : utf8::decode(folded)) {
    if (unicode::is_word(cp)) {
      cur.push_back(cp);
    } else if (!cur.empty()) {
      out.push_back(utf8::encode(cur));
      cur.clear();
    }
  }
  if (!cur.empty()) out.push_back(utf8::encode(cur));
  return out;
}

std::string strip_punct(std::string tok) {
  static const std::string drop = ".,;:!?()[]{}\"'`<>";
  while (!tok.empty() && drop.find(tok.front()) != std::string::npos) tok.erase(tok.begin());
  while (!tok.empty() && drop.find(tok.back()) != std::string::npos) tok.pop_back();
  return tok;
}

std::vector<std::string> whitespace_tokens(std::string_view text) {
  std::vector<std::string> out;
  std::string cur;
  bool quoted = false, any = false;
  for (char c : text) {
    if (c == '"') {
      quoted = !quoted;
      any = true;
    } else if (!quoted && std::isspace(static_cast<unsigned char>(c))) {
      if (any) out.push_back(cur);
      cur.clear();
      any = false;
    } else {
      cur += c;
      any = true;
    }
  }
  if (any) out.push_back(cur);
  return out;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
  std::string out;
  for (const auto& p : parts) out += (out.empty() ? "" : sep) + p;
  return out;
}

bool word_matches(const std::string& token, const std::string& word) {
  if (!word.empty() && word.back() == '*') return token.rfind(word.substr(0, word.size() - 1), 0) == 0;
  return token == word;
}

struct Hit {
  std::size_t first = 0, words = 0;
};

std::optional<Hit> match_phrase(const std::vector<std::string>& tokens, const std::string& phrase, std::size_t window, std::size_t lead) {
  const auto words = whitespace_tokens(phrase);
  if (words.empty()) return std::nullopt;
  std::set<std::size_t> used;
  std::size_t first = tokens.size();
  const std::size_t limit = std::min(window, tokens.size());
  for (const auto& w : words) {
    bool found = false;
    for (std::size_t i = 0; i < limit && !found; ++i) {
      if (used.count(i) || !word_matches(tokens[i], w)) continue;
      used.insert(i);
      first = std::min(first, i);
      found = true;
    }
    if (!found) return std::nullopt;
  }
  if (first >= lead) return std::nullopt;
  return Hit{first, words.size()};
}

std::string say(const Json& cfg, const std::string& lang, const std::string& id, const Json& params = Json::object()) {
  const Json& texts = cfg.at("texts");
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

std::string localized(const Json& v, const std::string& lang) {
  if (v.is_string()) return v.get<std::string>();
  if (v.is_object()) {
    for (const std::string& k : {lang, std::string("en")})
      if (const Json* s = json::find(v, k); s && s->is_string()) return s->get<std::string>();
  }
  return std::string();
}

// ── candidates ──────────────────────────────────────────────────────

struct Cand {
  std::string source, matched, lang, action;
  Json args = Json::object();
  Json element = nullptr;  // {"id": ...} | {"resolve": "current_conversation" | "last_message"} | null
  double score = 0;
};

struct Conv {
  std::string id, last_message_id;
};

const Json* entry_of(const Data& d, const std::string& action) { return json::find(d.engine.catalogue(), action); }

// ── arguments from loose phrases ────────────────────────────────────
// Data: heuristic.arguments (patterns) and heuristic.languages. A pattern fills a parameter the chosen action declares;
// what no pattern reads stays unextracted (the proposal says so), nothing is guessed.

struct ArgHit {
  std::string param, value, matched, pattern;
};

std::string trim_value(std::string v) {
  static const std::string drop = " \t\r\n.,;:!?\"'`";
  while (!v.empty() && drop.find(v.front()) != std::string::npos) v.erase(v.begin());
  while (!v.empty() && drop.find(v.back()) != std::string::npos) v.pop_back();
  return v;
}

// The label (English) of the language a folded word names; "" when the word names none that the data knows.
std::string language_label(const Data& d, const std::string& word) {
  const Json& h = d.cfg.at("heuristic");
  const Json* languages = json::find(h, "languages");
  if (!languages || !languages->is_array() || word.empty()) return std::string();
  for (const auto& l : *languages)
    for (const auto& name : strings_of(json::find(l, "names")))
      if (word_matches(word, fold(name, h.at("fold")))) return json::get_string(l.value("label", Json::object()), "en");
  return std::string();
}

std::vector<ArgHit> extract_args(const Data& d, const Json& entry, const std::string& text, const std::string& lang, const Json& have) {
  std::vector<ArgHit> hits;
  const Json* params = json::find(entry, "params");
  const Json& h = d.cfg.at("heuristic");
  const Json* patterns = json::find(h, "arguments");
  if (!params || !params->is_array() || !patterns || !patterns->is_array()) return hits;
  std::set<std::string> declared;
  for (const auto& p : *params) declared.insert(json::get_string(p, "name"));
  const std::u32string subject = utf8::decode(fold(text, h.at("fold")));
  std::u32string original = utf8::decode(std::string(text));
  if (original.size() != subject.size()) original = subject;  // the folding changed the length: read the folded words
  std::vector<const Json*> ordered;
  for (const auto& p : *patterns)
    if (json::get_string(p, "lang") == lang) ordered.push_back(&p);
  for (const auto& p : *patterns)
    if (json::get_string(p, "lang") != lang) ordered.push_back(&p);
  std::set<std::string> done;
  for (const Json* p : ordered) {
    const std::string param = json::get_string(*p, "param");
    if (!declared.count(param) || done.count(param)) continue;
    if (const Json* given = json::find(have, param); given && !given->is_null()) continue;  // the caller's word comes first
    auto rx = re::Regex::compile(json::get_string(*p, "regex"));
    if (!rx) continue;
    const std::string kind = json::get_string(*p, "kind", "text");
    for (const auto& m : rx->finditer(subject)) {
      auto original_group = [&](std::size_t g) {
        if (!m.matched(g)) return std::string();
        return utf8::encode(original.substr(static_cast<std::size_t>(m.start(g)), static_cast<std::size_t>(m.end(g) - m.start(g))));
      };
      std::string value;
      if (kind == "language") {
        value = language_label(d, m.group_utf8(1));
      } else if (const std::string tpl = json::get_string(*p, "template"); !tpl.empty()) {
        for (std::size_t i = 0; i < tpl.size(); ++i) {
          if (tpl[i] == '{' && i + 2 < tpl.size() && std::isdigit(static_cast<unsigned char>(tpl[i + 1])) && tpl[i + 2] == '}') {
            value += original_group(static_cast<std::size_t>(tpl[i + 1] - '0'));
            i += 2;
          } else {
            value += tpl[i];
          }
        }
      } else {
        value = trim_value(original_group(1));
      }
      if (value.empty()) continue;
      hits.push_back(ArgHit{param, value, trim_value(utf8::encode(original.substr(static_cast<std::size_t>(m.start(0)), static_cast<std::size_t>(m.end(0) - m.start(0))))),
                            json::get_string(*p, "id")});
      done.insert(param);
      break;
    }
  }
  return hits;
}

// What the action declares and what the words (or the settings) gave: the proposal shows it.
Json params_view(const Json& entry, const Json& said, const Json& bound, const std::vector<ArgHit>& hits, const std::string& lang) {
  Json out = Json::array();
  const Json* params = json::find(entry, "params");
  if (!params || !params->is_array()) return out;
  const std::string element_arg = json::get_string(entry, "element_from_arg");
  for (const auto& p : *params) {
    const std::string name = json::get_string(p, "name");
    if (name.empty() || name == element_arg) continue;
    Json row = Json::object();
    row["name"] = name;
    row["required"] = json::get_bool(p, "required", false);
    const Json* value = json::find(bound, name);
    row["given"] = value && !value->is_null();
    if (row["given"].get<bool>()) {
      row["value"] = *value;
      const bool from_phrase = std::any_of(hits.begin(), hits.end(), [&](const ArgHit& x) { return x.param == name; });
      const Json* spoken = json::find(said, name);
      row["from"] = from_phrase ? "phrase" : (spoken && !spoken->is_null()) ? "explicit" : "setting";
    }
    if (const Json* dflt = json::find(p, "default")) row["default"] = *dflt;
    if (const std::string setting = json::get_string(p, "setting"); !setting.empty()) row["setting"] = setting;
    if (const Json* desc = json::find(p, "description")) row["description"] = localized(*desc, lang);
    out.push_back(std::move(row));
  }
  return out;
}

std::string other_required_param(const Json& entry, const std::string& element_arg) {
  if (const Json* params = json::find(entry, "params"); params && params->is_array())
    for (const auto& p : *params)
      if (json::get_bool(p, "required", false) && json::get_string(p, "name") != element_arg) return json::get_string(p, "name");
  return std::string();
}

std::optional<Cand> parse_explicit(const Data& d, const std::string& text) {
  const Json& ex = d.cfg.at("heuristic").at("explicit");
  const std::string_view trimmed = utf8::strip(text);
  std::string rest;
  bool prefixed = false;
  for (const auto& prefix : strings_of(json::find(ex, "prefixes")))
    if (!prefix.empty() && trimmed.substr(0, prefix.size()) == prefix) {
      rest = std::string(trimmed.substr(prefix.size()));
      prefixed = true;
      break;
    }
  if (!prefixed) return std::nullopt;
  const auto tokens = whitespace_tokens(rest);
  if (tokens.empty()) return std::nullopt;
  const std::string verb = utf8::to_lower(tokens[0]);
  std::size_t idx = 1;
  std::string action;
  const auto run_verbs = strings_of(json::find(ex, "run_verbs"));
  if (std::find(run_verbs.begin(), run_verbs.end(), verb) != run_verbs.end()) {
    if (tokens.size() < 2) return std::nullopt;
    action = tokens[1];
    idx = 2;
  } else if (const Json* alias = json::find(ex.at("aliases"), verb); alias && alias->is_string()) {
    action = alias->get<std::string>();
  } else if (entry_of(d, verb)) {
    action = verb;
  } else {
    return std::nullopt;  // an unknown word after the marker (a path, a URL): not a command
  }
  const Json* entry = entry_of(d, action);
  Cand c;
  c.source = "explicit";
  c.matched = verb;
  c.action = action;
  c.score = 1.0;
  const std::string element_arg = entry ? json::get_string(*entry, "element_from_arg") : std::string();
  const std::string other = entry && !element_arg.empty() ? other_required_param(*entry, element_arg) : std::string();
  auto kv = re::Regex::compile(json::get_string(ex, "arg_pattern"));
  const auto markers = strings_of(json::find(ex, "element_markers"));
  std::vector<std::string> positional;
  std::string element_token;
  bool after_marker = false;
  for (std::size_t i = idx; i < tokens.size(); ++i) {
    const std::string& tok = tokens[i];
    if (std::find(markers.begin(), markers.end(), utf8::to_lower(tok)) != markers.end()) {
      after_marker = true;
      continue;
    }
    if (kv) {
      const std::u32string subject = utf8::decode(tok);
      if (auto m = kv->match(subject)) {
        const std::string k = m->group_utf8(1), v = m->group_utf8(2);
        if (!element_arg.empty() && !other.empty() && !c.args.contains(element_arg) && d.setting_keys.count(k)) {
          c.args[element_arg] = k;
          c.args[other] = v;
        } else {
          c.args[k] = v;
        }
        continue;
      }
    }
    const std::string bare = strip_punct(tok);
    if (element_token.empty() && (after_marker || d.resolver.is_id(bare) || d.engine.has_class(bare) || d.setting_keys.count(bare))) {
      element_token = bare;
      after_marker = false;
      continue;
    }
    positional.push_back(tok);
  }
  if (!element_arg.empty()) {
    if (!c.args.contains(element_arg) && !element_token.empty() && d.setting_keys.count(element_token)) {
      c.args[element_arg] = element_token;
      element_token.clear();
    }
    if (!other.empty() && !c.args.contains(other) && !positional.empty()) c.args[other] = join(positional, " ");
  }
  if (!element_token.empty()) {
    c.element = Json{{"id", element_token}};
  } else if (element_arg.empty() || !c.args.contains(element_arg)) {
    const std::string dflt = json::get_string(ex, "default_element");
    if (!dflt.empty()) c.element = Json{{"resolve", dflt}};
  }
  return c;
}

std::vector<Cand> parse_patterns(const Data& d, const std::string& text) {
  std::vector<Cand> out;
  const std::u32string subject = utf8::decode(std::string(utf8::strip(text)));
  for (const auto& p : d.cfg.at("heuristic").at("patterns")) {
    auto rx = re::Regex::compile(json::get_string(p, "regex"), re::kIgnoreCase);
    if (!rx) continue;
    auto m = rx->search(subject);
    if (!m) continue;
    Cand c;
    c.source = "pattern";
    c.matched = json::get_string(p, "id");
    c.action = json::get_string(p, "action");
    c.score = 1.0;
    const auto names = strings_of(json::find(p, "args"));
    for (std::size_t i = 0; i < names.size(); ++i) c.args[names[i]] = m->group_utf8(i + 1);
    const std::string element_arg = json::get_string(p, "element_arg");
    if (json::get_bool(p, "requires_element", false)) {
      if (element_arg.empty() || !c.args.contains(element_arg) || !d.setting_keys.count(c.args[element_arg].get<std::string>())) continue;
    }
    if (!element_arg.empty() && c.args.contains(element_arg)) c.element = Json{{"id", c.args[element_arg]}};
    out.push_back(std::move(c));
  }
  return out;
}

// Which element a free-text input talks about: an id anywhere in it, or a word that refers to the conversation or a message.
Json find_reference(const Data& d, const std::string& text, const std::vector<std::string>& folded_tokens) {
  for (const auto& raw : whitespace_tokens(text)) {
    const std::string bare = strip_punct(raw);
    if (!bare.empty() && d.resolver.is_id(bare)) return Json{{"id", bare}};
  }
  const Json& refs = d.cfg.at("heuristic").at("references");
  for (const auto& tok : folded_tokens)
    for (const auto& r : refs)
      for (const auto& w : strings_of(json::find(r, "words")))
        if (word_matches(tok, fold(w, d.cfg.at("heuristic").at("fold")))) return Json{{"resolve", json::get_string(r, "resolve")}};
  return Json(nullptr);
}

std::vector<Cand> parse_lexicon(const Data& d, const std::string& text) {
  std::vector<Cand> out;
  const Json& h = d.cfg.at("heuristic");
  const std::string folded = fold(text, h.at("fold"));
  const auto tokens = word_tokens(folded);
  const Json element = find_reference(d, text, tokens);
  if (element.is_null()) return out;  // naming an action is not enough: a command acts on an element
  const std::size_t window = static_cast<std::size_t>(json::get_int(h, "window_tokens", 12));
  const std::size_t lead = static_cast<std::size_t>(json::get_int(h, "lead_tokens", 4));
  const Json& scoring = h.at("scoring");
  for (auto it = d.engine.catalogue().begin(); it != d.engine.catalogue().end(); ++it) {
    const Json& entry = it.value();
    if (json::find(entry, "element_from_arg")) continue;  // settings are named by their key: the patterns handle them
    // The phrases of the action itself plus the lexicon of the routing profile (heuristic.phrases: action -> language -> phrases),
    // where the phrases the user's own conversations added live.
    Json merged = Json::object();
    if (const Json* own = json::find(entry, "phrases"); own && own->is_object()) merged = *own;
    if (const Json* extra = json::find(h, "phrases"); extra && extra->is_object())
      if (const Json* more = json::find(*extra, it.key()); more && more->is_object())
        for (auto l = more->begin(); l != more->end(); ++l) {
          if (!merged.contains(l.key()) || !merged[l.key()].is_array()) merged[l.key()] = Json::array();
          for (const auto& x : l.value())
            if (x.is_string()) merged[l.key()].push_back(x);
        }
    if (merged.empty()) continue;
    const Json* phrases = &merged;
    std::optional<Cand> best;
    for (auto lang = phrases->begin(); lang != phrases->end(); ++lang) {
      for (const auto& phrase : strings_of(&lang.value())) {
        auto hit = match_phrase(tokens, fold(phrase, h.at("fold")), window, lead);
        if (!hit) continue;
        const double score = std::min(1.0, json::get_number(scoring, "base", 0.6) + json::get_number(scoring, "per_word", 0.1) * static_cast<double>(hit->words) -
                                               json::get_number(scoring, "lead_penalty", 0.05) * static_cast<double>(hit->first));
        if (score < json::get_number(scoring, "min_score", 0.5)) continue;
        if (best && best->score >= score) continue;
        Cand c;
        c.source = "lexicon";
        c.matched = phrase;
        c.lang = lang.key();
        c.action = it.key();
        c.element = element;
        c.score = score;
        best = std::move(c);
      }
    }
    if (best) out.push_back(std::move(*best));
  }
  return out;
}

// ── scoped rules and values from what is said ───────────────────────
// Data: heuristic.rules (scope phrases, command patterns). "w tej rozmowie ustaw X=Y", "dla mnie zawsze pytaj przed depict",
// "for this conversation never run depict without asking" become a proposal to run rules.set (rules.disable, rules.list).

// The catalogue id a word stands for: an id, or a phrase of exactly one action. A word that names none, or several, names nothing.
std::string resolve_action_word(const Data& d, const std::string& word) {
  if (word.empty()) return std::string();
  if (entry_of(d, word)) return word;
  const Json& h = d.cfg.at("heuristic");
  std::vector<std::string> found;
  for (auto it = d.engine.catalogue().begin(); it != d.engine.catalogue().end(); ++it) {
    if (json::find(it.value(), "element_from_arg")) continue;
    std::vector<std::string> phrases;
    if (const Json* own = json::find(it.value(), "phrases"); own && own->is_object())
      for (auto l = own->begin(); l != own->end(); ++l)
        for (const auto& x : strings_of(&l.value())) phrases.push_back(x);
    if (const Json* extra = json::find(h, "phrases"); extra && extra->is_object())
      if (const Json* more = json::find(*extra, it.key()); more && more->is_object())
        for (auto l = more->begin(); l != more->end(); ++l)
          for (const auto& x : strings_of(&l.value())) phrases.push_back(x);
    for (const auto& phrase : phrases)
      if (word_matches(word, fold(phrase, h.at("fold")))) {
        found.push_back(it.key());
        break;
      }
  }
  return found.size() == 1 ? found[0] : std::string();
}

std::vector<Cand> parse_rule_commands(const Data& d, const std::string& text) {
  std::vector<Cand> out;
  const Json& h = d.cfg.at("heuristic");
  const Json* rules = json::find(h, "rules");
  if (!rules || !rules->is_object()) return out;
  std::u32string folded = utf8::decode(fold(text, h.at("fold")));
  std::u32string original = utf8::decode(std::string(text));
  if (original.size() != folded.size()) original = folded;
  // The scope phrase: the longest one found as whole words; it is blanked out (same length) so the command reads what is left.
  std::string scope;
  std::size_t best_len = 0;
  std::size_t best_at = 0;
  auto is_word = [](char32_t c) { return unicode::is_word(c); };
  for (const auto& entry : rules->value("scopes", Json::array()))
    for (auto l = entry.at("phrases").begin(); l != entry.at("phrases").end(); ++l)
      for (const auto& phrase : strings_of(&l.value())) {
        const std::u32string needle = utf8::decode(fold(phrase, h.at("fold")));
        if (needle.empty() || needle.size() <= best_len) continue;
        for (std::size_t at = folded.find(needle); at != std::u32string::npos; at = folded.find(needle, at + 1)) {
          const bool left = at == 0 || !is_word(folded[at - 1]);
          const bool right = at + needle.size() >= folded.size() || !is_word(folded[at + needle.size()]);
          if (left && right) {
            scope = json::get_string(entry, "scope");
            best_len = needle.size();
            best_at = at;
            break;
          }
        }
      }
  if (best_len > 0) {
    for (std::size_t i = best_at; i < best_at + best_len; ++i) folded[i] = original[i] = U' ';
  }
  for (const auto& command : rules->value("commands", Json::array())) {
    if (json::get_bool(command, "needs_scope", false) && scope.empty()) continue;
    auto rx = re::Regex::compile(json::get_string(command, "regex"));
    if (!rx) continue;
    auto m = rx->search(folded);
    if (!m) continue;
    const auto names = strings_of(json::find(command, "args"));
    const auto from_original = strings_of(json::find(command, "original"));
    const auto to_resolve = strings_of(json::find(command, "resolve"));
    Cand c;
    c.source = "rule_command";
    c.matched = json::get_string(command, "id");
    c.lang = json::get_string(command, "lang");
    c.action = json::get_string(command, "action");
    c.score = 1.05;  // a scoped statement outranks the plain settings patterns that would swallow its tail
    if (const std::string effect = json::get_string(command, "effect"); !effect.empty()) c.args["effect"] = effect;
    if (!scope.empty()) c.args["scope"] = scope;
    bool ok = true;
    for (std::size_t i = 0; i < names.size() && ok; ++i) {
      if (!m->matched(i + 1)) continue;
      std::string value = m->group_utf8(i + 1);
      if (std::find(from_original.begin(), from_original.end(), names[i]) != from_original.end())
        value = trim_value(utf8::encode(original.substr(static_cast<std::size_t>(m->start(i + 1)), static_cast<std::size_t>(m->end(i + 1) - m->start(i + 1)))));
      if (std::find(to_resolve.begin(), to_resolve.end(), names[i]) != to_resolve.end()) {
        value = resolve_action_word(d, value);
        if (value.empty()) ok = false;  // the word names no action: nothing is guessed
      }
      c.args[names[i]] = value;
    }
    if (!ok) continue;
    c.element = Json{{"id", "application"}};
    out.push_back(std::move(c));
    break;
  }
  return out;
}

// ── the model method ────────────────────────────────────────────────

struct ModelRoute {
  std::vector<Cand> cands;
  Json pending = nullptr;
  std::optional<Error> error;
};

ModelRoute model_route(Runtime& rt, const Data& d, const std::string& text, const std::string& lang, const Json& confirmation) {
  ModelRoute result;
  const Json& m = d.cfg.at("model");
  const Json& method = d.cfg.at("method");
  std::string model;
  for (const char* key : {"model_setting", "model_fallback_setting"}) {
    const Json v = rt.config().get(json::get_string(method, key));
    if (v.is_string() && !v.get<std::string>().empty()) {
      model = v.get<std::string>();
      break;
    }
  }
  if (model.empty()) {
    result.error = Error(Errc::InvalidArgument, "no model is configured for routing");
    return result;
  }
  std::string lines;
  for (auto it = d.engine.catalogue().begin(); it != d.engine.catalogue().end(); ++it) {
    if (it.key() == "use_model") continue;
    auto line = render_profile_template(json::get_string(m, "catalogue_line"),
        Json{{"id", it.key()}, {"title", json::get_string(it.value(), "title", it.key())}, {"requires", join(strings_of(json::find(it.value(), "requires")), ", ")}});
    if (line) lines += (lines.empty() ? "" : "\n") + *line;
  }
  auto user = render_profile_template(json::get_string(m, "user"), Json{{"actions", lines}, {"text", text}});
  if (!user) {
    result.error = user.error();
    return result;
  }
  llm::ModelRequest req;
  req.kind = "input.route_intent";
  req.action = "input.route_intent";
  req.models = {llm::ModelSpec{model, "", Json::object()}};
  req.messages = Json::array({Json{{"role", "system"}, {"content", json::get_string(m, "system")}}});
  req.prompt = *user;
  if (const Json* p = json::find(m, "parameters"); p && p->is_object()) req.parameters = *p;
  req.wants = {"json"};
  req.title = json::get_string(m, "title");
  req.timeout_ms = static_cast<int>(json::get_int(m, "timeout_ms", 30000));
  if (confirmation.is_object() && !confirmation.empty()) req.usage.confirmation = confirmation;
  llm::Services services(rt.config(), rt.secrets(), rt.http());
  services.data_root = rt.paths().root;
  auto outcome = llm::use_model(services, std::move(req));
  if (!outcome) {
    result.error = outcome.error();
    return result;
  }
  const llm::VariantResult& r = outcome->last();
  if (r.state == llm::State::Held) {
    const Json& admitted = r.hold["decision"];
    Json resources = Json::object();
    if (const Json* res = json::find(admitted, "resources")) resources = *res;
    result.pending = Json{{"schema", "loom.action_usage_pending/1"}, {"stage", "route_intent"}, {"status", json::get_string(r.hold, "status")},
                          {"operation_id", admitted.value("operation_id", Json(nullptr))}, {"receipt_id", admitted.value("receipt_id", Json(nullptr))},
                          {"baseline_key", admitted.value("baseline_key", Json(nullptr))}, {"projected", resources}, {"sent", false}};
    return result;
  }
  if (!r.ok()) {
    result.error = r.state == llm::State::HttpError ? Error(Errc::Http, "API error " + std::to_string(r.http_status))
                                                    : (r.error ? *r.error : Error(Errc::Unavailable, "model call failed"));
    return result;
  }
  Json answer = nullptr;
  for (const auto& part : r.outputs)
    if (part.kind == "json") answer = part.value;
  if (!answer.is_object()) {
    result.error = Error(Errc::Parse, "the model did not return a JSON object");
    return result;
  }
  if (json::get_string(answer, "kind") != "command") return result;
  const std::string action = json::get_string(answer, "action");
  const double confidence = json::get_number(answer, "confidence", 0.0);
  if (action.empty() || !entry_of(d, action) || confidence < json::get_number(m, "min_confidence", 0.5)) return result;
  Cand c;
  c.source = "model";
  c.matched = model;
  c.lang = lang;
  c.action = action;
  c.score = std::min(1.0, confidence);
  if (const Json* a = json::find(answer, "args"); a && a->is_object()) c.args = *a;
  const std::string ref = json::get_string(answer, "reference");
  if (ref == "conversation") c.element = Json{{"resolve", "current_conversation"}};
  else if (ref == "message") c.element = Json{{"resolve", "last_message"}};
  else if (!ref.empty()) c.element = Json{{"id", ref}};
  else if (const std::string dflt = json::get_string(d.cfg.at("heuristic").at("explicit"), "default_element"); !dflt.empty())
    c.element = Json{{"resolve", dflt}};
  const Json* entry = entry_of(d, action);
  const std::string element_arg = entry ? json::get_string(*entry, "element_from_arg") : std::string();
  if (!element_arg.empty() && c.args.contains(element_arg)) c.element = nullptr;
  result.cands.push_back(std::move(c));
  return result;
}

// ── evaluation of a candidate ───────────────────────────────────────

Result<Json> resolve_ref(Runtime& rt, Data& d, const Json& ref, const Conv& conv) {
  std::string id = json::get_string(ref, "id");
  const std::string kind = json::get_string(ref, "resolve");
  if (kind == "current_conversation") {
    if (conv.id.empty()) return Error(Errc::NotFound, "there is no conversation yet");
    id = conv.id;
  } else if (kind == "last_message") {
    if (conv.last_message_id.empty()) return Error(Errc::NotFound, "the conversation has no message yet");
    id = conv.last_message_id;
  }
  if (id.empty()) return Error(Errc::NotFound, "no element named");
  const Engine& engine = d.engine;
  LOOM_TRY_ASSIGN(Json el, d.resolver.resolve(sources_of(rt), Json(id), [&](std::string_view c) { return engine.has_class(c); }));
  if (el.is_string()) {
    if (!engine.has_class(el.get<std::string>())) return Error(Errc::NotFound, "'" + id + "' is not something I can act on");
    el = Json{{"class", el}};
  }
  return el;
}

Json effects_of(const Json& plan) { return plan.value("effects", Json::object()); }

Json element_summary(const Data& d, const Json& el, const std::string& lang) {
  const std::string cls = json::get_string(el, "class");
  std::string class_label = cls;
  if (const Json* k = json::find(d.engine.classes(), cls))
    if (const Json* l = json::find(*k, "label")) class_label = localized(*l, lang);
  Json out = Json::object();
  out["id"] = json::get_string(el, "id");
  out["class"] = cls;
  out["class_label"] = class_label;
  out["label"] = json::get_string(el, "label");
  out["source"] = json::get_string(el, "source");
  return out;
}

Json evaluate(Runtime& rt, Data& d, const Cand& c_in, const Conv& conv, const std::string& lang, const std::string& text) {
  Cand c = c_in;
  Json cmd = Json::object();
  cmd["source"] = c.source;
  cmd["matched"] = c.matched;
  cmd["score"] = std::round(c.score * 1000) / 1000;
  cmd["action"] = c.action;
  cmd["args"] = c.args;
  cmd["feasible"] = false;
  auto infeasible = [&](Json reason) {
    cmd["reason"] = std::move(reason);
    return cmd;
  };
  const Json* entry = entry_of(d, c.action);
  if (!entry) return infeasible(Json{{"code", "unknown_action"}, {"message", "no action '" + c.action + "'"}});
  cmd["title"] = localized(entry->value("labels", Json::object()), lang).empty() ? json::get_string(*entry, "title", c.action)
                                                                                 : localized(entry->value("labels", Json::object()), lang);
  // Arguments the words carry ("na niemiecki", "w stylu Szekspira"): read by the data's patterns, only for what the action declares.
  std::vector<ArgHit> hits;
  if (c.source == "lexicon" || c.source == "model") {
    hits = extract_args(d, *entry, text, c.lang.empty() ? lang : c.lang, c.args);
    for (const auto& hit : hits) c.args[hit.param] = hit.value;
    cmd["args"] = c.args;
  }
  cmd["args_read"] = Json::array();
  for (const auto& hit : hits) cmd["args_read"].push_back(Json{{"param", hit.param}, {"value", hit.value}, {"matched", hit.matched}, {"pattern", hit.pattern}});
  // The element.
  Json element_json = nullptr;
  if (!c.element.is_null()) {
    auto el = resolve_ref(rt, d, c.element, conv);
    if (!el) return infeasible(Json{{"code", "element_not_found"}, {"message", el.error().message}});
    element_json = *el;
  } else if (const std::string from = json::get_string(*entry, "element_from_arg"); !from.empty() && c.args.contains(from)) {
    auto el = resolve_ref(rt, d, Json{{"id", c.args[from]}}, conv);
    if (!el) return infeasible(Json{{"code", "element_not_found"}, {"message", el.error().message}});
    element_json = *el;
  } else if (json::find(*entry, "requires") && !entry->at("requires").empty()) {
    return infeasible(Json{{"code", "no_element"}, {"message", "name what to act on"}});
  } else {
    element_json = Json::object();
  }
  cmd["element"] = element_summary(d, element_json, lang);
  // Missing arguments.
  Json missing_args = Json::array();
  Json missing_settings = Json::object();  // argument -> the setting that would supply it
  const std::string element_arg = json::get_string(*entry, "element_from_arg");
  const Json bound = rt.actions().bind_params(*entry, c.args);  // what the settings already say counts as said
  cmd["params"] = params_view(*entry, c.args, bound, hits, lang);
  if (const Json* params = json::find(*entry, "params"); params && params->is_array())
    for (const auto& p : *params) {
      const std::string name = json::get_string(p, "name");
      if (json::get_bool(p, "required", false) && !bound.contains(name) && name != element_arg) {
        missing_args.push_back(name);
        if (const std::string setting = json::get_string(p, "setting"); !setting.empty()) missing_settings[name] = setting;
      }
    }
  if (!missing_args.empty()) return infeasible(Json{{"code", "missing_args"}, {"args", missing_args}, {"settings", missing_settings}});
  // A rule written from the conversation: "this conversation" is this one; a rule pinned by a higher scope is refused here,
  // with its reason, before the user is asked anything.
  if (c.action == "rules.set" || c.action == "rules.disable") {
    if (c.action == "rules.set") {
      const std::string level = d.engine.level_name(json::get_string(c.args, "scope", "person"));
      if (level == "conversation" && !c.args.contains("scope_id")) {
        if (conv.id.empty()) return infeasible(Json{{"code", "element_not_found"}, {"message", "there is no conversation yet"}});
        c.args["scope_id"] = conv.id;
      }
    }
    c.args["provenance"] = Json{{"origin", "chat"}, {"conv_id", conv.id}};
    cmd["args"] = c.args;
    auto draft = actions::rules_preview(d.root, c.action, c.args);
    if (!draft) return infeasible(Json{{"code", "error"}, {"message", draft.error().message}});
    if (!draft->value("ok", false)) return infeasible(Json{{"code", "refused"}, {"pin", draft->value("pin", Json::object())}});
    cmd["rule"] = draft->value("rule", Json(nullptr));
    cmd["rule_note"] = draft->value("note", Json(nullptr));
    cmd["replaces"] = draft->value("replaced", false);
  }
  // The scope of the question (R47): this conversation and the turn, so rules and thresholds bound to the conversation apply.
  const Json scope = conv.id.empty() ? Json(nullptr) : Json{{"conversation", conv.id}, {"turn", ""}};
  // Fit: the action fits when the element has the traits it requires; else a chain may supply them.
  auto view = d.engine.actions_for(element_json, scope, Json{{"include_unmatched", true}});
  if (!view) return infeasible(Json{{"code", "error"}, {"message", view.error().message}});
  Json row = nullptr;
  for (const auto& r : (*view)["actions"])
    if (json::get_string(r, "action") == c.action) row = r;
  const bool fits = !row.is_null() && json::get_string(row, "status") != "not_applicable";
  Json request = Json{{"element", element_json}, {"args", c.args}};
  if (!scope.is_null()) request["scope"] = scope;
  if (fits) {
    request["action"] = c.action;
  } else {
    auto chains = d.engine.propose_chain(element_json, Json{{"action", c.action}}, scope, Json::object());
    if (!chains || !(*chains)["chains"].is_array() || (*chains)["chains"].empty()) {
      Json reason{{"code", "unfit"}, {"missing", row.is_null() ? Json::array() : row.value("missing", Json::array())}};
      if (chains && chains->contains("beyond_bounds")) reason["beyond_bounds"] = (*chains)["beyond_bounds"];
      return infeasible(reason);
    }
    request["chain"] = (*chains)["chains"][0];
    Json steps = Json::array();
    for (const auto& s : (*chains)["chains"][0]["steps"]) steps.push_back(s.value("action", ""));
    cmd["chain"] = steps;
  }
  auto preview = rt.actions().preview(request);
  if (!preview) return infeasible(Json{{"code", "error"}, {"message", preview.error().message}});
  cmd["request"] = request;
  cmd["plan"] = preview->at("plan");
  cmd["gates"] = preview->at("gates");
  cmd["needs_confirmation"] = preview->at("needs_confirmation");
  cmd["denied"] = preview->at("denied");
  cmd["not_runnable"] = preview->at("not_runnable");
  cmd["runnable"] = preview->at("runnable");
  cmd["estimate"] = preview->value("estimate", Json(nullptr));
  cmd["feasible"] = true;
  // What would be sent: the exact size of the content the model steps take (money cost stays unknown).
  std::int64_t bytes = 0;
  if (preview->at("plan").at("cost").value("model_calls", 0) > 0 && element_json.is_object()) {
    if (auto parts = d.resolver.content_parts(sources_of(rt), element_json))
      for (const auto& p : *parts) bytes += static_cast<std::int64_t>(p.text.size());
  }
  cmd["input_bytes"] = bytes;
  return cmd;
}

Conv conv_of(Runtime& rt, const std::string& conv_id) {
  Conv c;
  c.id = conv_id;
  if (conv_id.empty()) return c;
  if (auto msgs = rt.db().get_msgs(conv_id)) {
    for (auto it = msgs->rbegin(); it != msgs->rend(); ++it) {
      const bool traffic = it->metadata.is_object() &&
                           (it->metadata.contains("routed") || it->metadata.contains("action_proposal") || it->metadata.contains("action_run"));
      if (traffic) continue;
      c.last_message_id = it->id;
      break;
    }
  }
  return c;
}

std::string effective_method(Runtime& rt, const Data& d, const Json& request) {
  const Json& m = d.cfg.at("method");
  std::string method = json::get_string(request, "method");
  if (method.empty()) {
    const Json v = rt.config().get(json::get_string(m, "setting"));
    if (v.is_string()) method = v.get<std::string>();
  }
  if (method.empty()) method = json::get_string(m, "active");
  return method;
}

std::string detect_language(const Data& d, const std::string& text, const std::string& hint) {
  if (!hint.empty()) return hint;
  const Json& heuristic = d.cfg.at("heuristic");
  // Words that belong to one language, else the letters of Polish, else the default.
  const auto tokens = word_tokens(fold(text, heuristic.at("fold")));
  std::string best;
  std::size_t best_hits = 0;
  for (auto it = heuristic.at("language_hints").begin(); it != heuristic.at("language_hints").end(); ++it) {
    std::size_t hits = 0;
    for (const auto& w : strings_of(&it.value()))
      hits += static_cast<std::size_t>(std::count(tokens.begin(), tokens.end(), w));
    if (hits > best_hits) {
      best_hits = hits;
      best = it.key();
    }
  }
  if (!best.empty()) return best;
  for (char32_t cp : utf8::decode(utf8::to_lower(text)))
    if (json::find(heuristic.at("fold"), utf8::encode(cp))) return "pl";
  return json::get_string(d.cfg, "default_language", "en");
}

}  // namespace

// ── chat.route ──────────────────────────────────────────────────────

namespace {

Result<Json> route_with(Runtime& rt, Data& d, const Json& request) {
  const std::string text = json::get_string(request, "text");
  if (utf8::is_blank(text)) return Error(Errc::InvalidArgument, "text is required");
  const std::string method = effective_method(rt, d, request);
  if (!json::find(d.cfg.at("method").at("presets"), method))
    return Error(Errc::InvalidArgument, "unknown routing method '" + method + "' (off, heuristic or model)");
  const std::string lang = detect_language(d, text, json::get_string(request, "lang"));
  Json out = Json::object();
  out["schema"] = "loom.chat_route/1";
  out["kind"] = "message";
  out["method"] = method;
  out["lang"] = lang;
  out["issues"] = Json::array();
  if (method == "off") return out;

  const Conv conv = conv_of(rt, json::get_string(request, "conv_id"));
  std::vector<Cand> cands;
  if (method == "model") {
    ModelRoute routed = model_route(rt, d, text, lang, request.value("route_usage_confirmation", Json::object()));
    if (!routed.pending.is_null()) {
      out["kind"] = "pending";
      out["usage_pending"] = routed.pending;
      return out;
    }
    if (routed.error) {
      // Not silent: the failure is reported, and the local method decides instead.
      out["issues"].push_back(Json{{"code", "model_route_failed"}, {"message", routed.error->message}, {"fallback", "heuristic"}});
      if (auto e = parse_explicit(d, text)) cands.push_back(*e);
      for (auto& p : parse_patterns(d, text)) cands.push_back(std::move(p));
      for (auto& l : parse_lexicon(d, text)) cands.push_back(std::move(l));
    } else {
      cands = std::move(routed.cands);
    }
  } else {
    if (auto e = parse_explicit(d, text)) cands.push_back(*e);
    for (auto& p : parse_patterns(d, text)) cands.push_back(std::move(p));
    for (auto& l : parse_lexicon(d, text)) cands.push_back(std::move(l));
  }
  // Rules and scoped values from what is said (the model method does not know them; they are read locally either way).
  if (auto rule_cands = parse_rule_commands(d, text); !rule_cands.empty()) {
    cands.erase(std::remove_if(cands.begin(), cands.end(), [](const Cand& x) { return x.source == "pattern" && x.action == "settings.set"; }), cands.end());
    for (auto& r : rule_cands) cands.push_back(std::move(r));
  }
  if (cands.empty()) return out;
  std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
  for (auto& c : cands)
    if (c.lang.empty()) c.lang = lang;

  Json summaries = Json::array();
  Json chosen = nullptr;
  Json first = nullptr;
  for (const auto& c : cands) {
    Json cmd = evaluate(rt, d, c, conv, lang, text);
    // "this conversation" with no conversation, or an id that is nothing: words that merely sound like a command.
    if (c.source != "explicit" && c.source != "pattern" && !cmd.value("feasible", false) &&
        cmd.value("reason", Json::object()).value("code", "") == "element_not_found")
      continue;
    summaries.push_back(Json{{"action", c.action}, {"source", c.source}, {"score", cmd["score"]}, {"feasible", cmd["feasible"]}});
    if (first.is_null()) first = cmd;
    if (cmd.value("feasible", false) && chosen.is_null()) chosen = cmd;
  }
  if (first.is_null()) return out;  // every reading pointed at nothing: it is a message
  Json command = chosen.is_null() ? first : chosen;
  command["candidates"] = summaries;
  out["kind"] = "command";
  out["confidence"] = command["score"];
  out["command"] = command;
  return out;
}

}  // namespace

Result<Json> route(Runtime& rt, const Json& request) {
  LOOM_TRY_ASSIGN(Data d, load_data(rt));
  return route_with(rt, d, request);
}

// ── the chat window ─────────────────────────────────────────────────

namespace {

Result<std::string> add_message(Runtime& rt, const std::string& conv_id, const std::string& role, const std::string& text, Json metadata,
                                const std::string& model = std::string()) {
  NewMessage nm;
  nm.conv_id = conv_id;
  nm.text = text;
  nm.role = role;
  nm.metadata = std::move(metadata);
  if (!model.empty()) nm.model = model;
  LOOM_TRY_ASSIGN(std::string id, rt.db().create_msg(nm));
  if (role == "user") {
    rt.bus().emit(events::kMsgCreated, Json{{"id", id}, {"text", text}, {"conv_id", conv_id}, {"role", "user"}});
  } else {
    // System-made answers are not analysed by the semantic worker: they are not the user's content.
    MsgPatch patch;
    patch.semantic_status = "done";
    LOOM_TRY(rt.db().update_msg(id, patch));
  }
  return id;
}

Status set_metadata(Runtime& rt, const std::string& message_id, const std::function<void(Json&)>& edit) {
  LOOM_TRY_ASSIGN(auto m, rt.db().get_msg(message_id));
  if (!m) return Error(Errc::NotFound, "no message '" + message_id + "'");
  Json metadata = m->metadata.is_object() ? m->metadata : Json::object();
  edit(metadata);
  MsgPatch patch;
  patch.metadata = metadata;
  return rt.db().update_msg(message_id, patch);
}

void title_first_exchange(Runtime& rt, const std::string& conv_id, const std::string& text) {
  auto all = rt.db().get_msgs(conv_id);
  const Json auto_title = rt.config().get("auto_title");
  if (all && all->size() <= 2 && (auto_title.is_null() || (auto_title.is_boolean() && auto_title.get<bool>()))) {
    ConvPatch patch;
    patch.title = std::string(utf8::prefix(text, 30)) + (utf8::length(text) > 30 ? "..." : "");
    (void)rt.db().update_conv(conv_id, patch);
  }
}

std::string effects_sentence(const Data& d, const Json& effects, const std::string& lang) {
  std::vector<std::string> parts;
  auto add = [&](const char* flag, const char* text_id) {
    if (json::get_bool(effects, flag, false)) parts.push_back(say(d.cfg, lang, text_id));
  };
  if (json::get_bool(effects, "reads_only", false)) parts.push_back(say(d.cfg, lang, "effect_reads"));
  else add("writes", "effect_writes");
  add("sends_off_device", "effect_sends");
  add("costs_money", "effect_money");
  add("irreversible", "effect_irreversible");
  add("touches_private_data", "effect_private");
  if (parts.empty()) parts.push_back(say(d.cfg, lang, "effect_none"));
  return join(parts, ", ");
}

std::string gate_reasons(const Data& d, const Json& gates, const std::string& lang) {
  std::vector<std::string> parts;
  for (const auto& g : gates) {
    std::string reason = g.contains("reason") ? localized(g["reason"], lang) : std::string();
    std::string line = json::get_string(g, "title", json::get_string(g, "action"));
    if (!reason.empty()) line += " - " + reason;
    parts.push_back(line);
  }
  (void)d;
  return join(parts, "; ");
}

std::string initiator_suffix(const Data& d, const Json& gate, const std::string& lang) {
  const std::string who = json::get_string(gate, "initiator");
  return who.empty() ? std::string() : say(d.cfg, lang, "initiator_by", Json{{"who", who}});
}

// The proposal as stored in the message and shown by the clients (texts in the language of the input).
Json build_proposal(const Data& d, const Json& cmd, const Json& route_doc, const std::string& lang) {
  Json p = Json::object();
  p["schema"] = std::string(kProposalSchema);
  p["status"] = "pending";
  p["lang"] = lang;
  p["method"] = route_doc.value("method", "");
  p["source"] = cmd.value("source", "");
  p["matched"] = cmd.value("matched", "");
  p["score"] = cmd.value("score", 0.0);
  p["title"] = cmd.value("title", "");
  p["action"] = cmd.value("action", "");
  if (cmd.contains("chain")) p["chain"] = cmd["chain"];
  p["args"] = cmd.value("args", Json::object());
  if (cmd.contains("element")) p["element"] = cmd["element"];
  p["args_read"] = cmd.value("args_read", Json::array());
  if (cmd.contains("rule")) p["rule"] = cmd["rule"];
  if (cmd.contains("replaces")) p["replaces"] = cmd["replaces"];
  p["params"] = cmd.value("params", Json::array());
  Json items = Json::array();
  if (cmd.contains("plan"))
    for (const auto& item : cmd.at("plan").value("items", Json::array()))
      items.push_back(Json{{"action", item.value("action", "")}, {"title", localized(item.value("labels", Json::object()), lang).empty() ? item.value("title", "") : localized(item.at("labels"), lang)},
                           {"cost", item.value("cost", 0.0)}, {"implementation", item.value("implementation", "")}});
  p["items"] = items;
  if (cmd.contains("plan")) {
    const Json estimate = cmd.value("estimate", Json(nullptr));
    // Money is a range where the model registry knows the price, else unknown with the reason: never a zero.
    Json money = estimate.is_object() ? estimate.value("money", Json(nullptr)) : Json(nullptr);
    if (money.is_null()) money = Json{{"known", false}, {"currency", "USD"}, {"reason", "no estimate"}};
    p["cost"] = Json{{"declared", cmd.at("plan").at("cost").at("declared")}, {"unit", "relative"}, {"model_calls", cmd.at("plan").at("cost").at("model_calls")},
                     {"input_bytes", cmd.value("input_bytes", 0)}, {"money", money},
                     {"tokens", estimate.is_object() ? estimate.value("tokens", Json(nullptr)) : Json(nullptr)}};
    p["effects"] = effects_of(cmd.at("plan"));
  }
  Json confirm = Json::array();
  for (const auto& g : cmd.value("needs_confirmation", Json::array()))
    confirm.push_back(Json{{"action", g.value("action", "")}, {"title", g.value("title", "")}, {"reason", g.value("reason", Json(nullptr))},
                           {"initiator", g.value("initiator", "")}});
  p["needs_confirmation"] = confirm;
  p["runnable"] = cmd.value("runnable", false) && cmd.value("feasible", false);
  if (cmd.contains("candidates")) p["alternatives"] = cmd["candidates"];
  (void)d;
  return p;
}

// A sum of money for people: enough digits to tell a hundredth of a cent from nothing.
std::string money_text(double v) {
  char buf[48];
  std::snprintf(buf, sizeof buf, v >= 0.01 ? "%.2f" : "%.4f", v);
  return buf;
}

std::string render_proposal(const Data& d, const Json& cmd, const Json& proposal, const std::string& lang) {
  std::vector<std::string> lines;
  lines.push_back(say(d.cfg, lang, "proposal_title", Json{{"title", proposal.value("title", "")}}));
  const Json element = proposal.value("element", Json::object());
  std::string where = element.value("label", "").empty() ? element.value("class_label", "") : element.value("class_label", "") + " \"" + element.value("label", "") + "\"";
  if (where.empty()) where = say(d.cfg, lang, "no_element");
  lines.push_back(say(d.cfg, lang, "proposal_element", Json{{"element", where}}));
  // What the words gave to the declared parameters, and which optional ones keep their defaults.
  {
    std::vector<std::string> given, open;
    for (const auto& row : cmd.value("params", Json::array())) {
      const std::string name = row.value("name", "");
      if (row.value("given", false)) {
        const Json& v = row.at("value");
        given.push_back(name + "=" + (v.is_string() ? v.get<std::string>() : json::dump(v)));
      } else if (!row.value("required", false) && row.contains("default") && !row.at("default").is_null()) {
        const Json& v = row.at("default");
        open.push_back(name + " (" + (v.is_string() ? v.get<std::string>() : json::dump(v)) + ")");
      }
    }
    if (!given.empty()) lines.push_back(say(d.cfg, lang, "proposal_args", Json{{"args", join(given, ", ")}}));
    if (const Json note = cmd.value("rule_note", Json(nullptr)); note.is_object() && note.contains("consumer"))
      lines.push_back(say(d.cfg, lang, "proposal_note", Json{{"note", say(d.cfg, lang, "rule_note_consumer")}}));
    if (cmd.value("replaces", false)) lines.push_back(say(d.cfg, lang, "rule_replaces"));
    if (!open.empty()) lines.push_back(say(d.cfg, lang, "proposal_args_open", Json{{"params", join(open, ", ")}}));
  }
  const Json reason = cmd.value("reason", Json(nullptr));
  if (!cmd.value("feasible", false)) {
    const std::string code = reason.is_object() ? json::get_string(reason, "code") : std::string();
    if (code == "refused") {
      const Json pin = reason.value("pin", Json::object());
      lines.push_back(say(d.cfg, lang, "proposal_refused", Json{{"reason", localized(pin.value("reason", Json("")), lang)}, {"initiator", initiator_suffix(d, pin, lang)}}));
    } else if (code == "unfit") {
      lines.push_back(say(d.cfg, lang, "proposal_unfit", Json{{"missing", join(strings_of(json::find(reason, "missing")), ", ")}}));
    } else if (code == "missing_args") {
      std::string hint;
      if (const Json* settings = json::find(reason, "settings"); settings && settings->is_object() && !settings->empty()) {
        std::vector<std::string> keys;
        for (auto it = settings->begin(); it != settings->end(); ++it) keys.push_back(it.value().get<std::string>());
        hint = say(d.cfg, lang, "missing_args_settings", Json{{"settings", join(keys, ", ")}});
      }
      lines.push_back(say(d.cfg, lang, "proposal_missing_args", Json{{"args", join(strings_of(json::find(reason, "args")), ", ")}, {"action", cmd.value("action", "")}}) + hint);
    } else {
      lines.push_back(say(d.cfg, lang, "proposal_error", Json{{"message", reason.is_object() ? json::get_string(reason, "message") : std::string("?")}}));
    }
    return join(lines, "\n");
  }
  std::vector<std::string> plan;
  for (const auto& item : proposal.value("items", Json::array())) plan.push_back(item.value("title", ""));
  lines.push_back(say(d.cfg, lang, "proposal_plan", Json{{"plan", join(plan, " -> ")}}));
  if (const Json* cost = json::find(proposal, "cost")) {
    lines.push_back(say(d.cfg, lang, "proposal_cost", Json{{"cost", cost->value("declared", 0.0)}, {"calls", cost->value("model_calls", 0)}, {"bytes", cost->value("input_bytes", 0)}}));
    if (cost->value("model_calls", 0) > 0) {
      const Json money = cost->value("money", Json(nullptr));
      if (money.is_object() && json::get_bool(money, "known", false))
        lines.push_back(say(d.cfg, lang, "proposal_money_known", Json{{"low", money_text(money.value("low", 0.0))}, {"high", money_text(money.value("high", 0.0))}, {"currency", money.value("currency", "USD")}}));
      else
        lines.push_back(say(d.cfg, lang, "proposal_money_unknown", Json{{"why", money.is_object() ? money.value("reason", "") : std::string()}}));
    }
  }
  if (const Json* effects = json::find(proposal, "effects"))
    lines.push_back(say(d.cfg, lang, "proposal_effects", Json{{"effects", effects_sentence(d, *effects, lang)}}));
  const Json denied = cmd.value("denied", Json::array());
  if (!denied.empty()) {
    lines.push_back(say(d.cfg, lang, "proposal_denied", Json{{"reason", gate_reasons(d, denied, lang)}, {"initiator", initiator_suffix(d, denied[0], lang)}}));
    return join(lines, "\n");
  }
  const Json not_runnable = cmd.value("not_runnable", Json::array());
  if (!not_runnable.empty()) {
    std::vector<std::string> why;
    for (const auto& n : not_runnable) {
      std::string line = n.value("action", "") + " (" + n.value("why", "");
      if (const Json* r = json::find(n, "reason")) line += ": " + localized(*r, lang);
      why.push_back(line + ")");
    }
    lines.push_back(say(d.cfg, lang, "proposal_not_runnable", Json{{"why", join(why, ", ")}}));
    return join(lines, "\n");
  }
  if (!proposal.value("needs_confirmation", Json::array()).empty())
    lines.push_back(say(d.cfg, lang, "proposal_confirm", Json{{"what", gate_reasons(d, cmd.value("needs_confirmation", Json::array()), lang)}}));
  lines.push_back(say(d.cfg, lang, "proposal_ask"));
  return join(lines, "\n");
}

std::string body_of(Runtime& rt, const Json& run) {
  std::string out;
  const Json* result = json::find(run, "result");
  if (result && result->is_object()) {
    for (const auto& part : result->value("parts", Json::array()))
      if (json::get_string(part, "kind") == "text") {
        // A long text was moved to the blob store by the result sink; the reference is read back here.
        auto text = actions::part_text(&rt.blobs(), part);
        out += (out.empty() ? "" : "\n\n") + (text ? *text : json::get_string(part, "preview"));
      }
    if (out.empty())
      if (const Json* data = json::find(*result, "data")) out = "```json\n" + json::dump(*data, 2) + "\n```";
  }
  return out;
}

struct Report {
  std::string text;
  Json meta;
};

// What the result sink produced for a completed run: the form nodes, the content node, the run node.
Json produced_of(const Json& run) {
  const Json* result = json::find(run, "result");
  const Json* produced = result ? json::find(*result, "produced") : nullptr;
  if (!produced || !produced->is_object()) return Json(nullptr);
  Json forms = Json::array();
  for (const auto& item : produced->value("items", Json::array()))
    if (!json::get_string(item, "form").empty()) forms.push_back(item.at("form"));
  if (forms.empty()) return Json(nullptr);
  return Json{{"target", produced->value("target", "")}, {"forms", forms}, {"content", produced->value("content", Json(nullptr))},
              {"run_node", produced->value("run_node", Json(nullptr))}};
}

// The message the sink posted (target conversation_message): it is the result, no second message says it again.
std::string posted_message(const Json& run) {
  const Json* result = json::find(run, "result");
  const Json* produced = result ? json::find(*result, "produced") : nullptr;
  if (!produced || !produced->is_object()) return std::string();
  for (const auto& item : produced->value("items", Json::array()))
    if (!json::get_string(item, "message_id").empty()) return json::get_string(item, "message_id");
  return std::string();
}

Report report_of(Runtime& rt, const Data& d, const Json& run, const std::string& title, const std::string& lang) {
  Report r;
  const std::string status = json::get_string(run, "status");
  r.meta = Json{{"run_id", json::get_string(run, "id")}, {"status", status}};
  if (status == "completed") {
    if (Json produced = produced_of(run); !produced.is_null()) r.meta["produced"] = produced;
    r.text = say(d.cfg, lang, "result_completed", Json{{"title", title}});
    const std::string body = body_of(rt, run);
    r.text += "\n\n" + (body.empty() ? say(d.cfg, lang, "result_no_text") : body);
  } else if (status == "failed") {
    const Json err = run.value("error", Json::object());
    r.text = say(d.cfg, lang, "result_failed", Json{{"step", err.value("step", "")}, {"message", err.value("message", "")}});
  } else if (status == "denied") {
    r.text = say(d.cfg, lang, "result_denied", Json{{"message", gate_reasons(d, run.value("error", Json::object()).value("gates", Json::array()), lang)}});
  } else if (status == "rejected") {
    r.text = say(d.cfg, lang, "result_rejected");
  } else if (status == "cancelled") {
    r.text = say(d.cfg, lang, "result_cancelled");
  } else if (status == "awaiting_usage") {
    r.text = say(d.cfg, lang, "result_usage");
    r.meta["pending"] = run.value("pending", Json(nullptr));
  } else {
    r.text = say(d.cfg, lang, "result_waiting", Json{{"kind", run.value("pending", Json::object()).value("kind", status)}});
    r.meta["pending"] = run.value("pending", Json(nullptr));
  }
  return r;
}

// The chat message that says a result links to the nodes it presents (graph targets), so the node and the message find each other.
Status link_presented(Runtime& rt, const Data& d, const std::string& message_id, const Json& produced, const std::string& run_id) {
  if (!produced.is_object()) return {};
  const std::string type = json::get_string(d.results.value("links", Json::object()), "presented_as", "references");
  for (const auto& f : produced.value("forms", Json::array())) {
    const std::string form = f.get<std::string>();
    if (form == message_id) continue;
    auto made = rt.db().create_link(message_id, form, type, 1.0, Json{{"run_id", run_id}, {"role", "result_message"}});
    if (!made) return made.error();
  }
  return {};
}

// The router's message about a run. When the target posted the result as a message of its own, that message is the
// answer (and no second one repeats it); otherwise a report message that links to what the run produced.
Result<std::string> answer_message(Runtime& rt, const Data& d, const std::string& conv_id, const Json* run, Report& rep) {
  const std::string posted = run ? posted_message(*run) : std::string();
  if (!posted.empty()) {
    rep.text = body_of(rt, *run);
    return posted;
  }
  LOOM_TRY_ASSIGN(std::string id, add_message(rt, conv_id, "assistant", rep.text, Json{{"action_run", rep.meta}, {"routed", "result"}}, "loom:actions"));
  if (auto linked = link_presented(rt, d, id, rep.meta.value("produced", Json(nullptr)), rep.meta.value("run_id", ""));
      !linked) {
    LOOM_TRY(set_metadata(rt, id, [&](Json& m) { m["action_run"]["link_error"] = linked.error().message; }));
  }
  return id;
}

std::string classify_answer(const Data& d, const std::string& text) {
  const std::string folded = join(word_tokens(fold(text, d.cfg.at("heuristic").at("fold"))), " ");
  if (folded.empty()) return std::string();
  const Json& conf = d.cfg.at("confirmation");
  for (const char* which : {"yes", "no"})
    for (auto lang = conf.at(which).begin(); lang != conf.at(which).end(); ++lang)
      for (const auto& w : strings_of(&lang.value()))
        if (join(word_tokens(fold(w, d.cfg.at("heuristic").at("fold"))), " ") == folded) return which;
  return std::string();
}

Json chunk_for(const std::string& type, const std::string& conv_id, const std::string& user_id, const std::string& assistant_id,
               const std::string& text, const Json& payload_key_value) {
  Json chunk{{"type", type}, {"conv_id", conv_id}, {"user_message_id", user_id}, {"message_id", assistant_id}, {"text", text}};
  for (auto it = payload_key_value.begin(); it != payload_key_value.end(); ++it) chunk[it.key()] = it.value();
  return chunk;
}

Outcome finish(const std::string& conv_id, const std::string& user_id, const std::string& assistant_id, const std::string& text,
               const std::string& type, Json payload) {
  Outcome out;
  out.handled = true;
  out.conv_id = conv_id;
  out.user_message_id = user_id;
  out.assistant_message_id = assistant_id;
  out.chunk = chunk_for(type, conv_id, user_id, assistant_id, text, payload);
  Json result{{"conv_id", conv_id}, {"user_message_id", user_id}, {"assistant_message_id", assistant_id}, {"text", text},
              {"model", "loom:actions"}, {"usage", Json::object()}, {"cancelled", false}, {"routed", Json{{"kind", "command"}, {"outcome", type}}}};
  for (auto it = payload.begin(); it != payload.end(); ++it) result["routed"][it.key()] = it.value();
  out.result = result;
  return out;
}

// The user answered a pending proposal with a yes or a no.
Result<Outcome> answer_proposal(Runtime& rt, Data& d, const std::string& conv_id, const Message& proposal_msg, const std::string& answer,
                                const std::string& text, const std::function<void(const std::string&, const std::string&)>& emit_start) {
  Json proposal = proposal_msg.metadata.at("action_proposal");
  const std::string lang = json::get_string(proposal, "lang", "en");
  LOOM_TRY_ASSIGN(std::string user_id, add_message(rt, conv_id, "user", text, Json{{"routed", "confirmation"}, {"answers", proposal_msg.id}}));
  if (emit_start) emit_start(conv_id, user_id);
  (void)rt.chat().load_conv(conv_id);
  const bool yes = answer == "yes";
  Json confirm{{"run_id", proposal.value("run_id", "")}, {"approved", yes}, {"receipt_id", proposal.value("receipt_id", "")}, {"ref", "chat:" + user_id}};
  auto run = rt.actions().confirm(confirm);
  Report rep;
  std::string new_status;
  if (!run) {
    rep.text = say(d.cfg, lang, "proposal_error", Json{{"message", run.error().message}});
    rep.meta = Json{{"run_id", proposal.value("run_id", "")}, {"status", "error"}, {"error", run.error().message}};
    new_status = "stale";
  } else if (!yes) {
    rep.text = say(d.cfg, lang, "ack_no");
    rep.meta = Json{{"run_id", proposal.value("run_id", "")}, {"status", json::get_string(*run, "status")}};
    new_status = "rejected";
  } else {
    rep = report_of(rt, d, *run, proposal.value("title", ""), lang);
    const std::string st = json::get_string(*run, "status");
    new_status = st == "completed" ? "executed" : st == "awaiting_usage" || st == "awaiting_confirmation" ? "waiting" : st;
  }
  LOOM_TRY(set_metadata(rt, proposal_msg.id, [&](Json& m) { m["action_proposal"]["status"] = new_status; }));
  LOOM_TRY_ASSIGN(std::string assistant_id, answer_message(rt, d, conv_id, run ? &*run : nullptr, rep));
  Json payload{{"run", rep.meta}, {"proposal_message_id", proposal_msg.id}, {"proposal_status", new_status}};
  return finish(conv_id, user_id, assistant_id, rep.text, "action_result", payload);
}

}  // namespace

Result<Outcome> handle(Runtime& rt, const std::string& text, const ChatOptions& opts, const Json& request,
                       const std::function<void(const std::string&, const std::string&)>& emit_start) {
  Outcome none;
  LOOM_TRY_ASSIGN(Data d, load_data(rt));
  const std::string method = effective_method(rt, d, request);
  if (method == "off") return none;
  // An explicit chat feature (attachments, a re-send with the usage confirmation, ...) is a message.
  for (const auto& field : strings_of(json::find(d.cfg, "message_request_fields"))) {
    const Json* v = json::find(request, field);
    if (v && !v->is_null() && !(v->is_boolean() && !v->get<bool>()) && !(v->is_array() && v->empty()) && !(v->is_object() && v->empty()) &&
        !(v->is_string() && v->get<std::string>().empty()))
      return none;
  }
  if (opts.attachments.size() || opts.active_task_spec || opts.knowledge_context || opts.web_search || opts.deep_research || opts.usage_confirmation) return none;

  std::string conv_id = opts.conv_id.value_or(std::string());
  if (conv_id.empty())
    if (auto current = rt.chat().current_conv()) conv_id = current->id;

  // 1. The answer to a proposal that waits as the last thing said in this conversation.
  if (!conv_id.empty()) {
    if (auto msgs = rt.db().get_msgs(conv_id); msgs && !msgs->empty()) {
      const Message& last = msgs->back();
      if (last.role == "assistant" && last.metadata.is_object() && last.metadata.contains("action_proposal") &&
          json::get_string(last.metadata["action_proposal"], "status") == "pending" && !json::get_string(last.metadata["action_proposal"], "run_id").empty()) {
        const std::string answer = classify_answer(d, text);
        if (!answer.empty()) return answer_proposal(rt, d, conv_id, last, answer, text, emit_start);
      }
    }
  }

  // 2. Message or command.
  Json route_request{{"text", text}, {"conv_id", conv_id}};
  if (const Json* m = json::find(request, "intent_routing_method")) route_request["method"] = *m;
  if (const Json* l = json::find(request, "lang")) route_request["lang"] = *l;
  if (const Json* c = json::find(request, "route_usage_confirmation")) route_request["route_usage_confirmation"] = *c;
  LOOM_TRY_ASSIGN(Json routed, route_with(rt, d, route_request));
  const std::string kind = routed.value("kind", "message");
  if (kind == "message") return none;
  if (kind == "pending") {
    // The re-send after the user's answer goes to this conversation, as a held chat send does.
    if (conv_id.empty()) {
      LOOM_TRY_ASSIGN(auto created, rt.db().create_conv("New Chat"));
      conv_id = created.id;
      (void)rt.chat().load_conv(conv_id);
    }
    Outcome out;
    out.handled = true;
    out.conv_id = conv_id;
    out.chunk = Json{{"type", "usage_pending"}, {"conv_id", conv_id}, {"user_message_id", ""}, {"usage_pending", routed["usage_pending"]}};
    out.result = Json{{"conv_id", conv_id}, {"usage_pending", routed["usage_pending"]}, {"routed", Json{{"kind", "pending"}}}};
    return out;
  }

  const Json cmd = routed["command"];
  const std::string lang = routed.value("lang", "en");
  if (conv_id.empty()) {
    LOOM_TRY_ASSIGN(auto created, rt.db().create_conv("New Chat"));
    conv_id = created.id;
  }
  (void)rt.chat().load_conv(conv_id);
  LOOM_TRY_ASSIGN(std::string user_id, add_message(rt, conv_id, "user", text, Json{{"routed", "command"}}));
  if (emit_start) emit_start(conv_id, user_id);

  Json proposal = build_proposal(d, cmd, routed, lang);
  std::string assistant_text = render_proposal(d, cmd, proposal, lang);
  const bool usable = cmd.value("feasible", false) && cmd.value("runnable", false) && cmd.value("denied", Json::array()).empty();
  Json meta = Json::object();
  std::string type = "action_proposal";
  Json payload = Json::object();
  if (!usable) {
    proposal["status"] = !cmd.value("denied", Json::array()).empty() ? "denied" : "unavailable";
    meta["action_proposal"] = proposal;
    payload["proposal"] = proposal;
  } else {
    Json run_request = cmd["request"];
    run_request["propose"] = true;
    if (run_request.contains("args") && run_request["args"].contains("provenance") && run_request["args"]["provenance"].is_object())
      run_request["args"]["provenance"]["message_id"] = user_id;  // a rule written from here remembers which message asked for it
    run_request["ref"] = "chat:" + user_id;
    run_request["context"] = Json{{"origin", "chat"}, {"conv_id", conv_id}, {"message_id", user_id}, {"text", text}, {"source", cmd.value("source", "")}, {"lang", lang}};
    auto run = rt.actions().run(run_request);
    if (!run) {
      proposal["status"] = "unavailable";
      assistant_text = say(d.cfg, lang, "proposal_error", Json{{"message", run.error().message}});
    } else if (run->value("status", "") == "denied") {
      proposal["status"] = "denied";
      assistant_text = say(d.cfg, lang, "result_denied", Json{{"message", gate_reasons(d, run->value("error", Json::object()).value("gates", Json::array()), lang)}});
    } else {
      proposal["run_id"] = (*run)["id"];
      proposal["receipt_id"] = run->value("pending", Json::object()).value("receipt_id", "");
      // The setting decides whether the user's yes is needed: a plan that only reads (or any plan) may run at once;
      // whatever the rules ask to confirm is still asked.
      const std::string mode = [&] {
        const Json v = rt.config().get(json::get_string(d.cfg.at("execution"), "setting"));
        return v.is_string() ? v.get<std::string>() : json::get_string(d.cfg.at("execution"), "active");
      }();
      const bool reads_only = json::get_bool(cmd["plan"].value("effects", Json::object()), "reads_only", false);
      const bool no_questions = cmd.value("needs_confirmation", Json::array()).empty();
      if (no_questions && (mode == "always" || (mode == "read_only" && reads_only))) {
        auto done = rt.actions().confirm(Json{{"run_id", (*run)["id"]}, {"approved", true}, {"receipt_id", proposal["receipt_id"]},
                                              {"ref", "auto:" + json::get_string(d.cfg.at("execution"), "setting") + "=" + mode}});
        if (done) {
          Report rep = report_of(rt, d, *done, proposal.value("title", ""), lang);
          const std::string st = json::get_string(*done, "status");
          proposal["status"] = st == "completed" ? "executed" : st == "awaiting_usage" || st == "awaiting_confirmation" ? "waiting" : st;
          assistant_text = rep.text;
          if (!posted_message(*done).empty()) assistant_text = say(d.cfg, lang, "result_completed", Json{{"title", proposal.value("title", "")}});
          meta["action_run"] = rep.meta;
          type = "action_result";
        } else {
          assistant_text = say(d.cfg, lang, "proposal_error", Json{{"message", done.error().message}});
          proposal["status"] = "stale";
        }
      }
    }
    meta["action_proposal"] = proposal;
    payload["proposal"] = proposal;
    if (meta.contains("action_run")) payload["run"] = meta["action_run"];
  }
  meta["routed"] = "proposal";
  LOOM_TRY_ASSIGN(std::string assistant_id, add_message(rt, conv_id, "assistant", assistant_text, meta, "loom:actions"));
  if (meta.contains("action_run")) {
    if (auto linked = link_presented(rt, d, assistant_id, meta["action_run"].value("produced", Json(nullptr)), meta["action_run"].value("run_id", "")); !linked)
      LOOM_TRY(set_metadata(rt, assistant_id, [&](Json& m) { m["action_run"]["link_error"] = linked.error().message; }));
  }
  title_first_exchange(rt, conv_id, text);
  return finish(conv_id, user_id, assistant_id, assistant_text, type, payload);
}

}  // namespace loom::chat_route
