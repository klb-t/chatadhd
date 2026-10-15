// input.route_intent: message or command (R54). Data: runtime profile intent_routing, `phrases` on actions.
#include "loom/chat_route.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include "loom/action_element.h"
#include "loom/action_emergent.h"
#include "loom/guardian.h"
#include "loom/action_overlay.h"
#include "loom/action_result.h"
#include "loom/action_run.h"
#include "loom/action_runner.h"
#include "loom/chat_engine.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/log.h"
#include "loom/provenance.h"
#include "loom/re/regex.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/semantic_worker.h"
#include "loom/settings_fields.h"
#include "loom/usage_spend.h"
#include "loom/use_model.h"
#include "loom/util/time.h"
#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom::chat_route {

namespace {

using actions::ActionRunner;
using actions::ElementResolver;
using actions::ElementSources;
using actions::Engine;
namespace emergent = actions::emergent;

constexpr std::string_view kProposalSchema = "loom.action_proposal/1";

std::vector<std::string> strings_of(const Json* v) {
  std::vector<std::string> out;
  if (v && v->is_array())
    for (const auto& x : *v)
      if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}

// The expressions of the data are compiled once: the router reads a few hundred of them on every input. nullptr = the pattern does not compile.
std::shared_ptr<const re::Regex> compiled(const std::string& pattern, re::Flags flags = re::kNone) {
  static std::mutex mu;
  static std::map<std::pair<std::string, int>, std::shared_ptr<const re::Regex>> cache;
  std::lock_guard<std::mutex> lock(mu);
  auto key = std::make_pair(pattern, static_cast<int>(flags));
  if (auto it = cache.find(key); it != cache.end()) return it->second;
  auto made = re::Regex::compile(pattern, flags);
  std::shared_ptr<const re::Regex> out = made ? std::make_shared<const re::Regex>(std::move(*made)) : nullptr;
  cache.emplace(std::move(key), out);
  return out;
}

struct Data {
  Json cfg;
  Engine engine;
  ElementResolver resolver;
  std::set<std::string> setting_keys;
  std::filesystem::path root;
  Json results = Json::object();  // runtime profile action_results (link names, ...)
  std::string client_id;          // the client that asks (client_devices.h): the device actions it declared are runnable in what it is shown
  Json estimate_reasons = Json::object();  // profile action_run, estimate.reasons: why money is unknown, by code, per language
};

Result<Data> load_data(Runtime& rt, const std::string& client_id = std::string()) {
  const auto root = rt.paths().root;
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::load("intent_routing", root));
  LOOM_TRY_ASSIGN(auto engine, Engine::load(root));
  engine.set_user_values(rt.config().all());  // the user's settings are the person level of scoped values
  engine.apply_client(rt.actions().client_patches(client_id));
  LOOM_TRY_ASSIGN(auto resolver, ElementResolver::load(root));
  std::set<std::string> keys;
  if (auto fields = settings_fields(root))
    if (const Json* list = json::find(*fields, "fields"); list && list->is_array())
      for (const auto& f : *list) keys.insert(json::get_string(f, "key"));
  Json results = Json::object();
  if (auto p = RuntimeProfile::load("action_results", root)) results = p->values();
  Json reasons = Json::object();
  if (auto p = RuntimeProfile::load("action_run", root))
    if (const Json* est = json::find(p->values(), "estimate"))
      if (const Json* r = json::find(*est, "reasons"); r && r->is_object()) reasons = *r;
  return Data{profile.values(), std::move(engine), std::move(resolver), std::move(keys), root, std::move(results), client_id, std::move(reasons)};
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

// default_element of an action (data): the element a command acts on when the sentence names none. A name the router resolves
// (last_message, current_conversation) or an element id (device:microphone).
bool resolves_default(std::string_view name) { return name == "last_message" || name == "current_conversation"; }
Json default_element_ref(const std::string& name) { return resolves_default(name) ? Json{{"resolve", name}} : Json{{"id", name}}; }

struct Hit {
  std::size_t first = 0, words = 0, last = 0;  // the tokens the phrase occupies run from first to last (inclusive)
};

std::optional<Hit> match_phrase(const std::vector<std::string>& tokens, const std::string& phrase, std::size_t window, std::size_t lead) {
  const auto words = whitespace_tokens(phrase);
  if (words.empty()) return std::nullopt;
  std::set<std::size_t> used;
  std::size_t first = tokens.size(), last = 0;
  const std::size_t limit = std::min(window, tokens.size());
  for (const auto& w : words) {
    bool found = false;
    for (std::size_t i = 0; i < limit && !found; ++i) {
      if (used.count(i) || !word_matches(tokens[i], w)) continue;
      used.insert(i);
      first = std::min(first, i);
      last = std::max(last, i);
      found = true;
    }
    if (!found) return std::nullopt;
  }
  if (first >= lead) return std::nullopt;
  return Hit{first, words.size(), last};
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

// Why the money of an estimate is unknown, in the language of the proposal: the estimate returns a code (and parameters), the texts per
// language are data (action_run: estimate.reasons). A code the data does not know keeps the estimate's own (English) words.
void localize_money_reason(const Data& d, Json& money, const std::string& lang) {
  if (!money.is_object()) return;
  const std::string code = json::get_string(money, "reason_code");
  const Json* texts = code.empty() ? nullptr : json::find(d.estimate_reasons, code);
  if (!texts) return;
  std::string text = localized(*texts, lang);
  if (text.empty()) return;
  if (auto rendered = render_profile_template(text, money.value("reason_params", Json::object()))) text = *rendered;
  money["reason"] = text;
}

// ── candidates ──────────────────────────────────────────────────────

struct Cand {
  std::string source, matched, lang, action;
  std::string via;         // lexicon: an action said first whose product this one goes on from (the chain must contain it)
  Json args = Json::object();
  Json element = nullptr;  // {"id": ...} | {"resolve": "current_conversation" | "last_message"} | null
  double score = 0;
  Json emergent = nullptr;  // a definition the catalogue could grow by (action_emergent.h); the command writes it
  std::string check_arg;    // a command that is kept only when the text of this argument is itself a command (help.how_to: "how do I ...")
};

struct Conv {
  std::string id, last_message_id;
};

const Json* entry_of(const Data& d, const std::string& action) { return json::find(d.engine.catalogue(), action); }

// The title of an action in a language: its label there, else its title (English); the name given when the catalogue does not know the action.
std::string title_in(const Data& d, const std::string& action, const std::string& fallback, const std::string& lang) {
  const Json* entry = entry_of(d, action);
  if (!entry) return fallback.empty() ? action : fallback;
  const std::string label = localized(entry->value("labels", Json::object()), lang);
  return label.empty() ? json::get_string(*entry, "title", action) : label;
}

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
    auto rx = compiled(json::get_string(*p, "regex"));
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

// An action whose element is one of its arguments (a setting) may say, in its data, which element an argument it was given stands for
// (`element_arg_defaults`: usage.set_limit - an `amount` is about usage_spend_limits, a `growth_factor` about loom_usage_policy).
void default_element_arg(const Json& entry, Json& args) {
  const std::string from = json::get_string(entry, "element_from_arg");
  if (from.empty() || !args.is_object() || args.contains(from)) return;
  const Json* defaults = json::find(entry, "element_arg_defaults");
  if (!defaults || !defaults->is_object()) return;
  for (auto it = defaults->begin(); it != defaults->end(); ++it)
    if (it->is_string() && args.contains(it.key()) && !args[it.key()].is_null()) {
      args[from] = *it;
      return;
    }
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
  auto kv = compiled(json::get_string(ex, "arg_pattern"));
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
  // The words after the action that are not key=value fill the parameter the action names as its positional one: /search.find cats and dogs.
  if (entry) {
    const std::string positional_arg = json::get_string(*entry, "positional_arg");
    if (!positional_arg.empty() && !c.args.contains(positional_arg) && !positional.empty()) c.args[positional_arg] = join(positional, " ");
  }
  if (entry && element_token.empty()) default_element_arg(*entry, c.args);
  if (!element_token.empty()) {
    c.element = Json{{"id", element_token}};
  } else if (element_arg.empty() || !c.args.contains(element_arg)) {
    // An action of the application as a whole (it requires nothing but `application`) acts on the application, not on the conversation.
    const auto requires_ = entry ? strings_of(json::find(*entry, "requires")) : std::vector<std::string>();
    const std::string dflt = json::get_string(ex, "default_element");
    const std::string own = entry ? json::get_string(*entry, "default_element") : std::string();
    if (requires_.size() == 1 && requires_[0] == "application") c.element = Json{{"id", "application"}};
    else if (resolves_default(own)) c.element = default_element_ref(own);  // the action says what it acts on when nothing is named
    else if (!dflt.empty()) c.element = Json{{"resolve", dflt}};
  }
  return c;
}

std::vector<Cand> parse_patterns(const Data& d, const std::string& text) {
  std::vector<Cand> out;
  const std::u32string subject = utf8::decode(std::string(utf8::strip(text)));
  for (const auto& p : d.cfg.at("heuristic").at("patterns")) {
    auto rx = compiled(json::get_string(p, "regex"), re::kIgnoreCase);
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
        if (word_matches(tok, fold(w, d.cfg.at("heuristic").at("fold")))) {
          if (json::get_string(r, "resolve") == "element") return Json{{"id", json::get_string(r, "element")}};  // a device, an element by its id
          return Json{{"resolve", json::get_string(r, "resolve")}};
        }
  return Json(nullptr);
}

// Every id in the words, in the order said (for an action that takes several elements). One id or none: nothing to combine.
Json find_references_many(const Data& d, const std::string& text) {
  Json many = Json::array();
  for (const auto& raw : whitespace_tokens(text)) {
    const std::string bare = strip_punct(raw);
    if (!bare.empty() && d.resolver.is_id(bare) && std::none_of(many.begin(), many.end(), [&](const Json& x) { return x.value("id", "") == bare; }))
      many.push_back(Json{{"id", bare}});
  }
  return many;
}

// The best phrase of an action found in the tokens: a phrase of the action itself or of the lexicon of the routing profile
// (heuristic.phrases: action -> language -> phrases, where the phrases the user's own conversations added live).
struct Found {
  Cand cand;
  Hit hit;
};

// A sentence that says something ABOUT a result ("Ocena tej wiadomości jest niska", "The critique of this message is harsh") is not the command the
// word in it also names (P18). Data heuristic.statement_guard: `nouns` (the words of the lexicon's stems that are nouns as well as verbs: ocena,
// krytyka, podsumowanie ...) and `statement_words` (jest, była, wynosi, is, was ...). A phrase that hit on one of those nouns, in a sentence with a
// statement word, is dropped. A hit on a verb is never dropped ("Oceń tę wiadomość, bo nie jest jasna" is a command), nor a noun that asks for the act
// ("Zrób ocenę tej wiadomości": no statement word). Lists are languages -> words (folded as the text is); empty lists turn the guard off.
bool states_something(const Data& d, const std::vector<std::string>& tokens, const Hit& hit) {
  const Json& h = d.cfg.at("heuristic");
  const Json* guard = json::find(h, "statement_guard");
  if (!guard || !guard->is_object() || hit.first >= tokens.size()) return false;
  const Json& fold_map = h.at("fold");
  const auto listed = [&](const char* key, const std::string& word) {
    const Json* lists = json::find(*guard, key);
    if (!lists || !lists->is_object()) return false;
    for (auto lang = lists->begin(); lang != lists->end(); ++lang)
      for (const auto& w : strings_of(&lang.value()))
        if (fold(w, fold_map) == word) return true;
    return false;
  };
  if (!listed("nouns", tokens[hit.first])) return false;
  for (std::size_t i = 0; i < tokens.size(); ++i)
    if (i != hit.first && listed("statement_words", tokens[i])) return true;
  return false;
}

std::optional<Found> best_phrase(const Data& d, const std::string& key, const Json& entry, const std::vector<std::string>& tokens) {
  const Json& h = d.cfg.at("heuristic");
  const std::size_t window = static_cast<std::size_t>(json::get_int(h, "window_tokens", 12));
  const std::size_t lead = static_cast<std::size_t>(json::get_int(h, "lead_tokens", 4));
  const Json& scoring = h.at("scoring");
  Json merged = Json::object();
  if (const Json* own = json::find(entry, "phrases"); own && own->is_object()) merged = *own;
  if (const Json* extra = json::find(h, "phrases"); extra && extra->is_object())
    if (const Json* more = json::find(*extra, key); more && more->is_object())
      for (auto l = more->begin(); l != more->end(); ++l) {
        if (!merged.contains(l.key()) || !merged[l.key()].is_array()) merged[l.key()] = Json::array();
        for (const auto& x : l.value())
          if (x.is_string()) merged[l.key()].push_back(x);
      }
  std::optional<Found> best;
  for (auto lang = merged.begin(); lang != merged.end(); ++lang) {
    for (const auto& phrase : strings_of(&lang.value())) {
      auto hit = match_phrase(tokens, fold(phrase, h.at("fold")), window, lead);
      if (!hit) continue;
      if (states_something(d, tokens, *hit)) continue;
      const double score = std::min(1.0, json::get_number(scoring, "base", 0.6) + json::get_number(scoring, "per_word", 0.1) * static_cast<double>(hit->words) -
                                             json::get_number(scoring, "lead_penalty", 0.05) * static_cast<double>(hit->first));
      if (score < json::get_number(scoring, "min_score", 0.5)) continue;
      if (best && best->cand.score >= score) continue;
      Found f;
      f.cand.source = "lexicon";
      f.cand.matched = phrase;
      f.cand.lang = lang.key();
      f.cand.action = key;
      f.cand.score = score;
      f.hit = *hit;
      best = std::move(f);
    }
  }
  return best;
}

// A sentence that names no element but brings its own text to work on is an ordinary message: the model does the job inline
// ("przetłumacz na niemiecki: dzień dobry"). Data heuristic.payload: payload_patterns (a colon followed by text, a quoted text,
// a second line), max_words (content words allowed after the command), filler_words. The words the argument patterns read
// ("na niemiecki") and the filler words are not content.
bool carries_payload(const Data& d, const Json& entry, const std::string& text, const std::vector<std::string>& tokens, const Found& found) {
  const Json* cfg = json::find(d.cfg.at("heuristic"), "payload");
  if (!cfg || !cfg->is_object()) return false;
  const std::u32string subject = utf8::decode(text);
  for (const auto& pattern : strings_of(json::find(*cfg, "payload_patterns"))) {
    auto rx = compiled(pattern);
    if (rx && rx->search(subject)) return true;
  }
  const Json& fold_map = d.cfg.at("heuristic").at("fold");
  std::vector<std::string> tail(tokens.begin() + static_cast<std::ptrdiff_t>(std::min(found.hit.last + 1, tokens.size())), tokens.end());
  const auto drop_one = [&](const std::string& word) {
    auto at = std::find(tail.begin(), tail.end(), word);
    if (at != tail.end()) tail.erase(at);
  };
  for (const auto& hit : extract_args(d, entry, text, found.cand.lang, Json::object()))
    for (const auto& w : word_tokens(fold(hit.matched, fold_map))) drop_one(w);
  std::set<std::string> fillers;
  for (const auto& w : strings_of(json::find(*cfg, "filler_words"))) fillers.insert(fold(w, fold_map));
  std::size_t content = 0;
  for (const auto& w : tail)
    if (!fillers.count(w)) ++content;
  return content > static_cast<std::size_t>(std::max<std::int64_t>(0, json::get_int(*cfg, "max_words", 0)));
}

std::vector<Cand> parse_lexicon(const Data& d, const std::string& text) {
  std::vector<Cand> out;
  const Json& h = d.cfg.at("heuristic");
  const std::string folded = fold(text, h.at("fold"));
  const auto tokens = word_tokens(folded);
  const Json element = find_reference(d, text, tokens);
  // Naming an action is not enough: a command acts on an element. A device action implies its device ("nagraj dźwięk": the
  // microphone), which its catalogue entry names (default_element); any other action needs the element in the words.
  std::vector<Found> device_hits;
  for (auto it = d.engine.catalogue().begin(); it != d.engine.catalogue().end(); ++it) {
    const Json& entry = it.value();
    if (json::find(entry, "element_from_arg")) continue;  // settings are named by their key: the patterns handle them
    const std::string implied = json::get_string(entry, "default_element");
    if (element.is_null() && implied.empty()) continue;
    auto found = best_phrase(d, it.key(), entry, tokens);
    if (!found) continue;
    if (element.is_null() && resolves_default(implied) && carries_payload(d, entry, text, tokens, *found)) continue;
    Cand c = found->cand;
    c.element = element.is_null() ? default_element_ref(implied) : element;
    if (json::get_bool(entry, "element_many", false))
      if (Json many = find_references_many(d, text); many.size() > 1) c.element = Json{{"many", many}};
    if (!implied.empty() && json::get_string(c.element, "id").rfind("device:", 0) == 0) {  // a device action on a device: the second action may go on from it
      found->cand = c;
      device_hits.push_back(*found);
    }
    out.push_back(std::move(c));
  }
  // Two actions in one sentence ("nagraj notatkę głosową i ją streść"): the second acts on what the first brings. The device
  // action names the element; the action after the connector (heuristic.sequence.connectors) is the target of a chain from it.
  const Json* sequence = json::find(h, "sequence");
  if (sequence && !device_hits.empty()) {
    std::vector<std::string> connectors;
    for (const auto& w : strings_of(json::find(*sequence, "connectors"))) connectors.push_back(fold(w, h.at("fold")));
    const Found* first = &device_hits[0];
    for (const auto& f : device_hits)
      if (f.cand.score > first->cand.score) first = &f;
    std::size_t at = tokens.size();
    for (std::size_t i = first->hit.first + 1; i < tokens.size() && at == tokens.size(); ++i)
      if (std::find(connectors.begin(), connectors.end(), tokens[i]) != connectors.end()) at = i;
    if (at + 1 < tokens.size()) {
      const std::vector<std::string> tail(tokens.begin() + static_cast<std::ptrdiff_t>(at) + 1, tokens.end());
      std::optional<Found> then;
      for (auto it = d.engine.catalogue().begin(); it != d.engine.catalogue().end(); ++it) {
        const Json& entry = it.value();
        if (json::find(entry, "element_from_arg") || (!json::get_string(entry, "default_element").empty() && !resolves_default(json::get_string(entry, "default_element")))) continue;
        auto found = best_phrase(d, it.key(), entry, tail);
        if (found && (!then || found->cand.score > then->cand.score)) then = std::move(found);
      }
      if (then) {
        Cand c = then->cand;
        c.element = first->cand.element;
        c.via = first->cand.action;
        c.matched = first->cand.matched + " + " + then->cand.matched;
        c.score = std::min(1.0, first->cand.score + json::get_number(*sequence, "bonus", 0.05));  // two actions said beat the first alone
        out.insert(out.begin(), std::move(c));  // and win a tie with it: the sort is stable
      }
    }
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
  // Words that stand for one action ("obraz" is depict, not a media action that is only planned): heuristic.rules.action_words, word -> action.
  if (const Json* rules = json::find(h, "rules"); rules && rules->is_object())
    if (const Json* words = json::find(*rules, "action_words"); words && words->is_object())
      for (auto it = words->begin(); it != words->end(); ++it)
        if (it.value().is_string() && entry_of(d, it.value().get<std::string>()) && word_matches(word, fold(it.key(), h.at("fold")))) return it.value().get<std::string>();
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

// `inner`: the text is read as the topic of a how-to: commands that ask for another command's explanation, and the growth of the
// catalogue, are not part of that reading.
std::vector<Cand> parse_rule_commands(const Data& d, const std::string& text, bool inner = false) {
  std::vector<Cand> out;
  const Json& h = d.cfg.at("heuristic");
  const Json* rules = json::find(h, "rules");
  // The commands of the rules (with their scope phrases) and the commands of everything else the conversation can do (heuristic.commands).
  Json commands = Json::array();
  if (rules && rules->is_object())
    for (const auto& command : rules->value("commands", Json::array())) commands.push_back(command);
  if (const Json* more = json::find(h, "commands"); more && more->is_array())
    for (const auto& command : *more) commands.push_back(command);
  if (commands.empty()) return out;
  const std::u32string folded_raw = utf8::decode(fold(text, h.at("fold")));
  std::u32string original_raw = utf8::decode(std::string(text));
  if (original_raw.size() != folded_raw.size()) original_raw = folded_raw;
  std::u32string folded = folded_raw, original = original_raw;
  // The scope phrase: the longest one found as whole words; it is blanked out (same length) so the command reads what is left.
  std::string scope;
  std::size_t best_len = 0;
  std::size_t best_at = 0;
  auto is_word = [](char32_t c) { return unicode::is_word(c); };
  if (rules && rules->is_object())
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
  for (const auto& command : commands) {
    const bool takes_scope = command.value("takes_scope", true);  // the scope phrase is part of the sentence only for the commands that take one
    if (json::get_bool(command, "needs_scope", false) && scope.empty()) continue;
    if (inner && !json::get_string(command, "check_arg").empty()) continue;
    auto rx = compiled(json::get_string(command, "regex"));
    if (!rx) continue;
    const std::u32string& subject = takes_scope ? folded : folded_raw;
    const std::u32string& source = takes_scope ? original : original_raw;
    auto m = rx->search(subject);
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
    c.check_arg = json::get_string(command, "check_arg");
    if (const std::string effect = json::get_string(command, "effect"); !effect.empty()) c.args["effect"] = effect;
    if (const Json* fixed = json::find(command, "set"); fixed && fixed->is_object())
      for (auto it = fixed->begin(); it != fixed->end(); ++it) c.args[it.key()] = it.value();
    if (!scope.empty() && takes_scope) c.args["scope"] = scope;
    bool ok = true;
    for (std::size_t i = 0; i < names.size() && ok; ++i) {
      if (!m->matched(i + 1)) continue;
      std::string value = m->group_utf8(i + 1);
      if (std::find(from_original.begin(), from_original.end(), names[i]) != from_original.end())
        value = trim_value(utf8::encode(source.substr(static_cast<std::size_t>(m->start(i + 1)), static_cast<std::size_t>(m->end(i + 1) - m->start(i + 1)))));
      if (std::find(to_resolve.begin(), to_resolve.end(), names[i]) != to_resolve.end()) {
        value = resolve_action_word(d, value);
        if (value.empty()) ok = false;  // the word names no action: nothing is guessed
      }
      c.args[names[i]] = value;
    }
    if (!ok) continue;
    if (!c.check_arg.empty() && (!c.args.contains(c.check_arg) || json::get_string(c.args, c.check_arg).empty())) continue;
    if (json::get_bool(command, "element_from_arg", false)) c.element = nullptr;  // the action names its element by an argument (settings: element_from_arg)
    else if (const Json* el = json::find(command, "element"); el && el->is_object()) c.element = *el;
    else c.element = Json{{"id", "application"}};
    out.push_back(std::move(c));
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
    const Json limit = usage_limit_reasons(admitted);
    // P18: a spending limit that refuses (on_exceed deny) does not stop the message: the routing model is not asked, the local method decides
    // and the reason is said as a note (the same way as a failed model route). A limit that asks (confirm) holds with a receipt, like x10.
    if (json::get_string(r.hold, "status") == "denied" && !limit.empty()) {
      std::string why = "your spending limit does not allow the routing model call";
      const Json limits = limit.value("limits", Json::array());
      if (limits.is_array() && !limits.empty()) {
        const Json reason = limits[0].value("reason", Json::object());  // bound: json::find points into it
        for (const std::string& key : {lang, std::string("en")})
          if (const Json* t = json::find(reason, key); t && t->is_string() && !t->get<std::string>().empty()) { why = t->get<std::string>(); break; }
      }
      result.error = Error(Errc::Conflict, why);
      return result;
    }
    Json resources = Json::object();
    if (const Json* res = json::find(admitted, "resources")) resources = *res;
    result.pending = Json{{"schema", "loom.action_usage_pending/1"}, {"stage", "route_intent"}, {"status", json::get_string(r.hold, "status")},
                          {"operation_id", admitted.value("operation_id", Json(nullptr))}, {"receipt_id", admitted.value("receipt_id", Json(nullptr))},
                          {"baseline_key", admitted.value("baseline_key", Json(nullptr))}, {"projected", resources}, {"sent", false}};
    for (auto it = limit.begin(); it != limit.end(); ++it)
      if (it.key() != "status") result.pending[it.key()] = it.value();
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
  else if (const Json* own_entry = entry_of(d, action); own_entry && resolves_default(json::get_string(*own_entry, "default_element")))
    c.element = default_element_ref(json::get_string(*own_entry, "default_element"));  // what the action acts on when nothing is named
  else if (const std::string dflt = json::get_string(d.cfg.at("heuristic").at("explicit"), "default_element"); !dflt.empty())
    c.element = Json{{"resolve", dflt}};
  const Json* entry = entry_of(d, action);
  // The model named no element: an action that asks for one class (the application, a storage, a message) acts on that one.
  if (ref.empty() && entry) {
    const auto needs = strings_of(json::find(*entry, "requires"));
    if (needs.size() == 1 && needs[0] == "message") c.element = Json{{"resolve", "last_message"}};
    else if (needs.size() == 1 && needs[0] == "conversation") c.element = Json{{"resolve", "current_conversation"}};
    else if (needs.size() == 1 && d.engine.has_class(needs[0])) c.element = Json{{"id", needs[0]}};
  }
  const std::string element_arg = entry ? json::get_string(*entry, "element_from_arg") : std::string();
  if (!element_arg.empty() && c.args.contains(element_arg)) c.element = nullptr;
  result.cands.push_back(std::move(c));
  return result;
}

// ── evaluation of a candidate ───────────────────────────────────────

Result<Json> resolve_ref(Runtime& rt, Data& d, const Json& ref, const Conv& conv);

// Several elements as one: each reference is resolved, the group is what they have in common (ElementResolver::group).
Result<Json> resolve_many(Runtime& rt, Data& d, const Json& many, const Conv& conv) {
  Json members = Json::array();
  for (const auto& ref : many) {
    LOOM_TRY_ASSIGN(Json one, resolve_ref(rt, d, ref, conv));
    members.push_back(std::move(one));
  }
  const Engine& engine = d.engine;
  return d.resolver.group(sources_of(rt), members, [&](std::string_view c) { return engine.has_class(c); },
                          [&](std::string_view cls) { return engine.expand_traits({std::string(cls)}); });
}

Result<Json> resolve_ref(Runtime& rt, Data& d, const Json& ref, const Conv& conv) {
  if (const Json* many = json::find(ref, "many"); many && many->is_array()) return resolve_many(rt, d, *many, conv);
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
  if (json::get_string(el, "source") == "group") {  // several elements as one input
    Json ids = Json::array();
    for (const auto& m : el.value("members", Json::array())) ids.push_back(json::get_string(m, "id"));
    out["members"] = ids;
    out["label"] = say(d.cfg, lang, "group_label", Json{{"count", ids.size()}});
  }
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
  if (!c.emergent.is_null()) cmd["emergent"] = c.emergent;
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
  default_element_arg(*entry, c.args);
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
      bool required = json::get_bool(p, "required", false);
      for (const auto& other : strings_of(json::find(p, "required_unless")))
        if (bound.contains(other) && !bound[other].is_null()) required = false;
      if (required && !bound.contains(name) && name != element_arg) {
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
  if (!d.client_id.empty()) request["client_id"] = d.client_id;
  if (fits) {
    request["action"] = c.action;
  } else {
    auto chains = d.engine.propose_chain(element_json, Json{{"action", c.action}}, scope, Json::object());
    if (!chains || !(*chains)["chains"].is_array() || (*chains)["chains"].empty()) {
      Json reason{{"code", "unfit"}, {"missing", row.is_null() ? Json::array() : row.value("missing", Json::array())}};
      if (chains && chains->contains("beyond_bounds")) reason["beyond_bounds"] = (*chains)["beyond_bounds"];
      return infeasible(reason);
    }
    // The chain that goes through the action said first ("nagraj ... i streść": record -> transcribe -> summarise), else the best one.
    std::size_t pick = 0;
    if (!c.via.empty()) {
      bool through = false;
      for (std::size_t i = 0; i < (*chains)["chains"].size() && !through; ++i)
        for (const auto& s : (*chains)["chains"][i]["steps"])
          if (s.value("action", "") == c.via) {
            pick = i;
            through = true;
            break;
          }
    }
    request["chain"] = (*chains)["chains"][pick];
    Json steps = Json::array();
    for (const auto& s : (*chains)["chains"][pick]["steps"]) steps.push_back(s.value("action", ""));
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
  // The steps the asking client carries out itself (client_devices.h): said in the proposal, so the person knows what will happen on the device.
  {
    Json client_steps = Json::array();
    for (const auto& step : preview->at("plan").value("steps", Json::array())) {
      // A step the arguments already switch off (the picker of a file when the sentence asked for a folder) is not one the device will be asked.
      if (const Json* when = json::find(step, "when"); when && actions::condition_decided_by_args(*when, c.args) == 0) continue;
      const Json* e = entry_of(d, json::get_string(step, "action"));
      if (!e || json::get_string(*e, "runs_on") != "client") continue;
      const std::string label = localized(e->value("labels", Json::object()), lang);
      client_steps.push_back(Json{{"action", json::get_string(*e, "id")}, {"title", label.empty() ? json::get_string(*e, "title") : label},
                                  {"prompt", e->value("client", Json::object()).value("prompt", Json(nullptr))}});
    }
    cmd["client_steps"] = client_steps;
  }
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

// The language the words themselves show: words that belong to one language (heuristic.language_hints), else the letters of Polish.
// Empty when they show none ("undo", "ok", a bare id).
std::string language_of_words(const Data& d, const std::string& input) {
  const Json& heuristic = d.cfg.at("heuristic");
  // The name of an explicit command (/rules.list, /search.find) is syntax, not a word of a language.
  std::string text = std::string(utf8::strip(input));
  for (const auto& prefix : strings_of(json::find(heuristic.at("explicit"), "prefixes")))
    if (!prefix.empty() && text.rfind(prefix, 0) == 0) {
      const auto space = text.find_first_of(" \t\r\n");
      text = space == std::string::npos ? std::string() : text.substr(space);
      break;
    }
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
  return std::string();
}

// The language of the conversation: that of its latest proposal (the answers that were given in it), else of its latest message of the
// person that shows one (heuristic.conversation_language.messages looked at, newest first). Empty for a conversation that shows none.
std::string language_of_conversation(Runtime& rt, const Data& d, const std::string& conv_id) {
  if (conv_id.empty()) return std::string();
  auto msgs = rt.db().get_msgs(conv_id);
  if (!msgs) return std::string();
  const Json* cfg = json::find(d.cfg.at("heuristic"), "conversation_language");
  if (cfg && !json::get_bool(*cfg, "enabled", true)) return std::string();
  const std::int64_t window = cfg ? json::get_int(*cfg, "messages", 12) : 12;
  std::int64_t looked = 0;
  for (auto it = msgs->rbegin(); it != msgs->rend() && looked < window; ++it, ++looked) {
    if (it->metadata.is_object() && it->metadata.contains("action_proposal")) {
      const std::string lang = json::get_string(it->metadata["action_proposal"], "lang");
      if (!lang.empty()) return lang;
    }
    if (it->role == "user")
      if (const std::string lang = language_of_words(d, it->text); !lang.empty()) return lang;
  }
  return std::string();
}

// Words first; when they show no language, the conversation's; else the default. The caller's hint beats both.
std::string detect_language(Runtime& rt, const Data& d, const std::string& text, const std::string& hint, const std::string& conv_id) {
  if (!hint.empty()) return hint;
  if (const std::string words = language_of_words(d, text); !words.empty()) return words;
  if (const std::string conv = language_of_conversation(rt, d, conv_id); !conv.empty()) return conv;
  return json::get_string(d.cfg, "default_language", "en");
}

}  // namespace

// ── chat.route ──────────────────────────────────────────────────────

namespace {

Result<Json> route_with(Runtime& rt, Data& d, const Json& request) {
  const std::string text = json::get_string(request, "text");
  if (utf8::is_blank(text)) return Error(Errc::InvalidArgument, "text is required");
  const std::string method = effective_method(rt, d, request);
  if (!json::find(d.cfg.at("method").at("presets"), method)) {
    std::string known;
    for (auto it = d.cfg.at("method").at("presets").begin(); it != d.cfg.at("method").at("presets").end(); ++it) known += (known.empty() ? "" : ", ") + it.key();
    return Error(Errc::InvalidArgument, "unknown routing method '" + method + "' (" + known + ")");
  }
  const std::string lang = detect_language(rt, d, text, json::get_string(request, "lang"), json::get_string(request, "conv_id"));
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
  // Rules and scoped values from what is said, and the other commands of the conversation (the model method does not know them; they are read
  // locally either way). A command that explains another ("how do I ...") is kept only when what follows is a command itself.
  const bool inner = json::get_bool(request, "no_howto", false);
  auto rule_cands = parse_rule_commands(d, text, inner);
  rule_cands.erase(std::remove_if(rule_cands.begin(), rule_cands.end(),
                                  [&](const Cand& rc) {
                                    if (rc.check_arg.empty()) return false;
                                    Json nested{{"text", json::get_string(rc.args, rc.check_arg)}, {"method", "heuristic"}, {"no_howto", true}, {"conv_id", json::get_string(request, "conv_id")}};
                                    if (const Json* l = json::find(request, "lang")) nested["lang"] = *l;
                                    auto r = route_with(rt, d, nested);
                                    return !(r && r->value("kind", "") == "command");
                                  }),
                   rule_cands.end());
  if (!rule_cands.empty()) {
    cands.erase(std::remove_if(cands.begin(), cands.end(), [](const Cand& x) { return x.source == "pattern" && x.action == "settings.set"; }), cands.end());
    for (auto& r : rule_cands) cands.push_back(std::move(r));
  }
  // hybrid: the local reading came first. A command it is sure of (a known action and an element, score at or above confident_score) is never
  // overridden by a model's "message"; the model is asked only when the local reading is unsure (a candidate between min_score and
  // confident_score), or, when the preset says so (ask_when: no_candidate), when it found nothing. The model's verdict then replaces the unsure
  // candidates: "message" makes the input a message, a command is the model's command.
  if (method == "hybrid") {
    const Json& preset = d.cfg.at("method").at("presets").at("hybrid");
    const double confident_score = json::get_number(d.cfg.at("heuristic").at("scoring"), "confident_score", 0.7);
    const auto ask_when = strings_of(json::find(preset, "ask_when"));
    const auto asks = [&](const char* when) { return std::find(ask_when.begin(), ask_when.end(), when) != ask_when.end(); };
    const bool confident = std::any_of(cands.begin(), cands.end(), [&](const Cand& x) { return x.score >= confident_score; });
    const bool ask = !confident && !inner && (cands.empty() ? asks("no_candidate") : asks("unsure"));
    out["hybrid"] = Json{{"confident", confident}, {"asked_model", ask}};
    if (ask) {
      ModelRoute routed = model_route(rt, d, text, lang, request.value("route_usage_confirmation", Json::object()));
      if (!routed.pending.is_null()) {
        out["kind"] = "pending";
        out["usage_pending"] = routed.pending;
        return out;
      }
      if (routed.error) {
        out["issues"].push_back(Json{{"code", "model_route_failed"}, {"message", routed.error->message}, {"fallback", "heuristic"}});
      } else {
        cands = std::move(routed.cands);
      }
    }
  }
  // Nothing in the catalogue understands it, but it is shaped like a command about an element: the catalogue may grow (R55). This is a local
  // check of the words, whatever the method: a model that called the sentence a message, or whose call failed, does not take the offer away
  // (decision P14: the shape check runs after the model's verdict; the model's schema is unchanged).
  if (cands.empty() && !inner) {
    const auto ref = find_reference(d, text, word_tokens(fold(text, d.cfg.at("heuristic").at("fold"))));
    if (!ref.is_null()) {
      if (auto element = resolve_ref(rt, d, ref, conv)) {
        const emergent::Context ctx{rt, d.cfg, d.engine, d.resolver};
        if (auto def = emergent::from_command(ctx, text, lang, *element)) {
          Cand c;
          c.source = "emergent";
          c.matched = json::get_string(def->at("summary"), "verb");
          c.lang = lang;
          c.action = json::get_string(*def, "writer");
          c.args = def->at("args");
          c.element = Json{{"id", "application"}};
          c.score = 0.9;
          c.emergent = *def;
          if (const Json* then = json::find(*def, "then_run")) {
            c.emergent["then_run"]["element"]["class"] = json::get_string(*element, "class");
            c.emergent["then_run"]["element"]["label"] = json::get_string(*element, "label");
            (void)then;
          }
          cands.push_back(std::move(c));
        }
      }
    }
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
  LOOM_TRY_ASSIGN(Data d, load_data(rt, json::get_string(request, "client_id")));
  return route_with(rt, d, request);
}

// ── the chat window ─────────────────────────────────────────────────

namespace {

// A message that shows a question of a run paused on the client's step is a new step for every page that shows the conversation: the
// event bus says so (events::kActionClientStep), with ids only. `state` "closed": the question was answered or cancelled.
void announce_client_step(Runtime& rt, const std::string& conv_id, const std::string& message_id, const Json& meta, const std::string& state) {
  const Json* run = meta.is_object() ? json::find(meta, "action_run") : nullptr;
  if (!run || !run->is_object()) return;
  const Json pending = run->value("pending", Json(nullptr));
  if (state == "waiting" && (!pending.is_object() || json::get_string(pending, "kind") != "client_step")) return;
  Json payload{{"conv_id", conv_id}, {"message_id", message_id}, {"run_id", json::get_string(*run, "run_id")}, {"state", state}};
  if (pending.is_object()) {
    payload["receipt_id"] = pending.value("receipt_id", Json(nullptr));
    payload["step"] = pending.value("step", Json(nullptr));
    payload["action"] = pending.value("action", Json(nullptr));
  }
  if (state == "closed") payload["status"] = json::get_string(*run, "status");
  rt.bus().emit(events::kActionClientStep, payload);
}

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
    announce_client_step(rt, conv_id, id, nm.metadata, "waiting");
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
    std::string line = title_in(d, json::get_string(g, "action"), json::get_string(g, "title"), lang);
    if (!reason.empty()) line += " - " + reason;
    parts.push_back(line);
  }
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
    if (money.is_null()) money = Json{{"known", false}, {"currency", "USD"}, {"reason", "no estimate"}, {"reason_code", "no_estimate"}};
    localize_money_reason(d, money, lang);
    p["cost"] = Json{{"declared", cmd.at("plan").at("cost").at("declared")}, {"unit", "relative"}, {"model_calls", cmd.at("plan").at("cost").at("model_calls")},
                     {"pipeline_model_use", cmd.at("plan").at("cost").value("pipeline_model_use", false)}, {"input_bytes", cmd.value("input_bytes", 0)}, {"money", money},
                     {"tokens", estimate.is_object() ? estimate.value("tokens", Json(nullptr)) : Json(nullptr)}};
    p["effects"] = effects_of(cmd.at("plan"));
  }
  Json confirm = Json::array();
  for (const auto& g : cmd.value("needs_confirmation", Json::array()))
    confirm.push_back(Json{{"action", g.value("action", "")}, {"title", title_in(d, g.value("action", ""), g.value("title", ""), lang)}, {"reason", g.value("reason", Json(nullptr))},
                           {"initiator", g.value("initiator", "")}});
  p["needs_confirmation"] = confirm;
  if (cmd.contains("client_steps") && !cmd["client_steps"].empty()) p["client_steps"] = cmd["client_steps"];
  p["runnable"] = cmd.value("runnable", false) && cmd.value("feasible", false);
  if (cmd.contains("candidates")) p["alternatives"] = cmd["candidates"];
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
  if (element.value("source", "") == "group")
    where = element.value("label", "") + (element.value("class_label", "").empty() ? std::string() : " (" + element.value("class_label", "") + ")");
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
  if (const Json steps = proposal.value("client_steps", Json::array()); !steps.empty()) {
    std::vector<std::string> titles;
    for (const auto& st : steps) titles.push_back(st.value("title", ""));
    lines.push_back(say(d.cfg, lang, "proposal_client", Json{{"steps", join(titles, ", ")}}));
  }
  if (const Json* cost = json::find(proposal, "cost")) {
    lines.push_back(say(d.cfg, lang, "proposal_cost", Json{{"cost", cost->value("declared", 0.0)}, {"calls", cost->value("model_calls", 0)}, {"bytes", cost->value("input_bytes", 0)}}));
    if (cost->value("model_calls", 0) > 0 || cost->value("pipeline_model_use", false)) {
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
      const std::string why_code = n.value("why", "");
      std::string line = title_in(d, n.value("action", ""), n.value("action", ""), lang) + " (" + (why_code.empty() ? why_code : say(d.cfg, lang, "why_" + why_code));
      if (const Json* r = json::find(n, "reason")) line += ": " + localized(*r, lang);
      why.push_back(line + ")");
    }
    lines.push_back(say(d.cfg, lang, "proposal_not_runnable", Json{{"why", join(why, ", ")}}));
    return join(lines, "\n");
  }
  if (!proposal.value("needs_confirmation", Json::array()).empty())
    lines.push_back(say(d.cfg, lang, "proposal_confirm", Json{{"what", gate_reasons(d, cmd.value("needs_confirmation", Json::array()), lang)}}));
  // A limit of the user that only tells (spending limit with on_exceed notify): the run goes on and the proposal says so.
  for (const auto& g : cmd.value("gates", Json::array()))
    if (g.contains("notice")) lines.push_back(localized(g["notice"], lang));
  lines.push_back(say(d.cfg, lang, "proposal_ask"));
  return join(lines, "\n");
}

// What a result that is data and not words says in the language of the proposal: the action's result_text (data, per language; {{name}}
// is an argument of the run), then the data itself. Without a result_text, or when it names an argument the run did not have, the data alone.
std::string result_sentence(const Data& d, const Json& run, const std::string& lang) {
  const Json request = run.value("request", Json::object());
  const Json* entry = entry_of(d, json::get_string(request, "action"));
  if (!entry) return std::string();
  const std::string tpl = localized(entry->value("result_text", Json::object()), lang);
  if (tpl.empty()) return std::string();
  Json vars = request.value("args", Json::object());
  if (!vars.is_object()) vars = Json::object();
  vars["title"] = title_in(d, json::get_string(request, "action"), std::string(), lang);
  auto rendered = render_profile_template(tpl, vars);
  return rendered ? *rendered : std::string();
}

std::string body_of(Runtime& rt, const Data& d, const Json& run, const std::string& lang) {
  std::string out;
  const Json* result = json::find(run, "result");
  if (result && result->is_object()) {
    for (const auto& part : result->value("parts", Json::array()))
      if (json::get_string(part, "kind") == "text") {
        // A long text was moved to the blob store by the result sink; the reference is read back here.
        auto text = actions::part_text(&rt.blobs(), part);
        out += (out.empty() ? "" : "\n\n") + (text ? *text : json::get_string(part, "preview"));
      }
    if (out.empty()) {
      const std::string sentence = result_sentence(d, run, lang);
      if (const Json* data = json::find(*result, "data"); data && !data->is_null())
        out = (sentence.empty() ? std::string() : sentence + "\n\n") + "```json\n" + json::dump(*data, 2) + "\n```";
      else
        out = sentence;
    }
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
  r.meta = Json{{"run_id", json::get_string(run, "id")}, {"status", status}, {"lang", lang}};
  if (status == "completed") {
    if (Json produced = produced_of(run); !produced.is_null()) r.meta["produced"] = produced;
    // An action that needs a secret from the person (connecting a storage) asks them to open Settings: the message carries the keys, the page a button.
    if (const Json* result = json::find(run, "result"); result && result->is_object())
      if (const Json* data = json::find(*result, "data"); data && data->is_object() && data->contains("open_settings")) r.meta["open_settings"] = (*data)["open_settings"];
    r.text = say(d.cfg, lang, "result_completed", Json{{"title", title}});
    const std::string body = body_of(rt, d, run, lang);
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
  } else if (status == "awaiting_client") {
    // The device has a step to carry out: the question stays in this message (pending.client_step) until the client answers.
    const Json spec = run.value("pending", Json::object()).value("client_step", Json::object());
    std::string prompt = localized(spec.value("prompt", Json::object()), lang);
    if (prompt.empty()) prompt = spec.value("title", "");
    r.text = say(d.cfg, lang, "result_client", Json{{"prompt", prompt}});
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
    rep.text = body_of(rt, d, *run, rep.meta.value("lang", "en"));
    return posted;
  }
  LOOM_TRY_ASSIGN(std::string id, add_message(rt, conv_id, "assistant", rep.text, Json{{"action_run", rep.meta}, {"routed", "result"}}, "loom:actions"));
  if (auto linked = link_presented(rt, d, id, rep.meta.value("produced", Json(nullptr)), rep.meta.value("run_id", ""));
      !linked) {
    LOOM_TRY(set_metadata(rt, id, [&](Json& m) { m["action_run"]["link_error"] = linked.error().message; }));
  }
  return id;
}

// Is the whole input one of the words of this list of the confirmation lexicon (yes | no | send)?
bool lexicon_has(const Data& d, const char* which, const std::string& folded) {
  const Json* list = json::find(d.cfg.at("confirmation"), which);
  if (!list || !list->is_object()) return false;
  for (auto lang = list->begin(); lang != list->end(); ++lang)
    for (const auto& w : strings_of(&lang.value()))
      if (join(word_tokens(fold(w, d.cfg.at("heuristic").at("fold"))), " ") == folded) return true;
  return false;
}

std::string folded_input(const Data& d, const std::string& text) {
  return join(word_tokens(fold(text, d.cfg.at("heuristic").at("fold"))), " ");
}

std::string classify_answer(const Data& d, const std::string& text) {
  const std::string folded = folded_input(d, text);
  if (folded.empty()) return std::string();
  for (const char* which : {"yes", "no"})
    if (lexicon_has(d, which, folded)) return which;
  return std::string();
}

// The answer to the offer to send a rejected sentence as a message: "send" (its own words, and the yes words) or "decline" (the no words).
std::string classify_offer(const Data& d, const std::string& text) {
  const std::string folded = folded_input(d, text);
  if (folded.empty()) return std::string();
  if (lexicon_has(d, "send", folded) || lexicon_has(d, "yes", folded)) return "send";
  if (lexicon_has(d, "no", folded)) return "decline";
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

// ── the catalogue grows from conversations: proposals ───────────────

struct Reply {
  std::string assistant_id, text, type;
  Json payload = Json::object();
  Json meta = Json::object();
};

std::string overlay_file_of(const std::string& kind) {
  return kind == "define_group" ? "profiles/graph_entry.pack" : "profiles/actions.pack";
}

// The words of a proposal to define an action, a sequence or a group (texts are data).
std::string render_emergent(const Data& d, const Engine& engine, const Json& cmd, const Json& proposal, const std::string& lang) {
  const Json def = cmd.at("emergent");
  const std::string kind = def.value("kind", "");
  const Json sum = def.value("summary", Json::object());
  std::vector<std::string> lines;
  if (kind == "define_action") {
    lines.push_back(say(d.cfg, lang, "define_title", Json{{"verb", sum.value("verb", "")}}));
    lines.push_back(say(d.cfg, lang, "define_what", Json{{"id", sum.value("id", "")}, {"instruction", sum.value("instruction", "")}, {"requires", sum.value("requires", "")}}));
    if (const Json* then = json::find(def, "then_run")) {
      const Json el = then->value("element", Json::object());
      std::string cls = el.value("class", "");
      std::string where = cls;
      if (const Json* k = json::find(engine.classes(), cls))
        if (const Json* l = json::find(*k, "label")) where = localized(*l, lang);
      if (!el.value("label", "").empty()) where += " \"" + el.value("label", "") + "\"";
      lines.push_back(say(d.cfg, lang, "define_then", Json{{"element", where.empty() ? say(d.cfg, lang, "no_element") : where}}));
    }
    lines.push_back(say(d.cfg, lang, "define_where", Json{{"overlay", overlay_file_of(kind)}, {"id", sum.value("id", "")}}));
  } else if (kind == "define_sequence") {
    std::string cls = sum.value("class", "");
    if (const Json* k = json::find(engine.classes(), cls))
      if (const Json* l = json::find(*k, "label")) cls = localized(*l, lang);
    lines.push_back(say(d.cfg, lang, "sequence_title", Json{{"sequence", sum.value("sequence", "")}, {"class", cls}, {"count", sum.value("count", 0)}, {"id", sum.value("id", "")}}));
    lines.push_back(say(d.cfg, lang, "sequence_what", Json{{"steps", sum.value("steps", "")}}));
    lines.push_back(say(d.cfg, lang, "define_where", Json{{"overlay", overlay_file_of(kind)}, {"id", sum.value("id", "")}}));
  } else {
    lines.push_back(say(d.cfg, lang, "group_title", Json{{"action", sum.value("action", "")}, {"classes", sum.value("classes", "")}, {"traits", sum.value("traits", "")}, {"id", sum.value("id", "")}}));
    lines.push_back(say(d.cfg, lang, "group_what", Json{{"overlay", overlay_file_of(kind)}, {"id", sum.value("id", "")}}));
  }
  const Json denied = cmd.value("denied", Json::array());
  if (!denied.empty()) {
    lines.push_back(say(d.cfg, lang, "proposal_denied", Json{{"reason", gate_reasons(d, denied, lang)}, {"initiator", initiator_suffix(d, denied[0], lang)}}));
    return join(lines, "\n");
  }
  if (!proposal.value("needs_confirmation", Json::array()).empty())
    lines.push_back(say(d.cfg, lang, "proposal_confirm", Json{{"what", gate_reasons(d, cmd.value("needs_confirmation", Json::array()), lang)}}));
  // A limit of the user that only tells (spending limit with on_exceed notify): the run goes on and the proposal says so.
  for (const auto& g : cmd.value("gates", Json::array()))
    if (g.contains("notice")) lines.push_back(localized(g["notice"], lang));
  lines.push_back(say(d.cfg, lang, "define_ask"));
  return join(lines, "\n");
}

// The answer to a command in the conversation: the proposal, or - when the setting says so, or the user has said yes already - its result.
// `user_said_yes`: the plan is confirmed up front where the rules ask nothing (the follow-up of a definition the user has just accepted).
Result<Reply> reply_to_command(Runtime& rt, Data& d, const std::string& conv_id, const std::string& user_id, const std::string& text, const Json& cmd,
                               const std::string& method, const std::string& lang, bool user_said_yes) {
  Json route_doc{{"method", method}};
  Json proposal = build_proposal(d, cmd, route_doc, lang);
  // The sentence the proposal was read from (a growth offered after a run has none): a no does not lose it (intent_routing_on_reject).
  if (!text.empty() && !user_id.empty()) proposal["sentence"] = Json{{"message_id", user_id}};
  const bool grows = cmd.contains("emergent");  // a definition the catalogue could grow by: always waits for the user's yes
  const bool custom = cmd.contains("proposal_text");  // a proposal the system makes itself (the guardian): its own words, always waits for the yes
  std::string assistant_text = grows ? render_emergent(d, d.engine, cmd, proposal, lang) : custom ? localized(cmd["proposal_text"], lang) : render_proposal(d, cmd, proposal, lang);
  const bool usable = cmd.value("feasible", false) && cmd.value("runnable", false) && cmd.value("denied", Json::array()).empty();
  Reply reply;
  reply.type = "action_proposal";
  Json& meta = reply.meta;
  std::string early_id;  // the proposal message, when it had to be stored before the run (auto-run)
  std::string node;
  Json definition = nullptr;
  if (grows) {
    // Where it emerged from: a node with `derived_from` links to the messages (the user's own, the element, the runs it repeats).
    definition = cmd["emergent"];
    std::vector<std::string> from{user_id};
    for (const auto& x : definition.value("sources", Json::array()))
      if (x.is_string()) from.push_back(x.get<std::string>());
    if (auto made = emergent::record(rt, d.cfg, definition, conv_id, from)) node = *made;
    proposal["kind"] = definition.value("kind", "");
    Json shown{{"kind", definition.value("kind", "")}, {"id", definition.value("id", "")}, {"title", definition.value("title", "")}, {"summary", definition.value("summary", Json::object())},
               {"args", definition.value("args", Json::object())}, {"node", node}, {"overlay", overlay_file_of(definition.value("kind", ""))}};
    if (const Json* then = json::find(definition, "then_run")) shown["then_run"] = *then;
    proposal["definition"] = shown;
  }
  if (!usable) {
    proposal["status"] = !cmd.value("denied", Json::array()).empty() ? "denied" : "unavailable";
    if (grows) emergent::settle(rt, node, proposal.value("status", "unavailable"));
    meta["action_proposal"] = proposal;
    reply.payload["proposal"] = proposal;
  } else {
    Json run_request = cmd["request"];
    run_request["propose"] = true;
    if (run_request.contains("args") && run_request["args"].contains("provenance") && run_request["args"]["provenance"].is_object())
      run_request["args"]["provenance"]["message_id"] = user_id;  // a rule written from here remembers which message asked for it
    if (grows) {
      Json from = Json::array({user_id});
      for (const auto& x : definition.value("sources", Json::array())) from.push_back(x);
      run_request["args"]["provenance"] = Json{{"origin", "chat"}, {"conv_id", conv_id}, {"message_ids", from}, {"node", node}};
    }
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
      // An answer to a question (what can I do here, how do I do this) runs at once whatever the setting says: execution.direct lists the actions.
      const bool direct = [&] {
        for (const auto& a : strings_of(json::find(d.cfg.at("execution"), "direct")))
          if (a == cmd.value("action", "")) return true;
        return false;
      }();
      const std::string mode = [&] {
        if (user_said_yes || direct) return std::string("always");
        const Json v = rt.config().get(json::get_string(d.cfg.at("execution"), "setting"));
        return v.is_string() ? v.get<std::string>() : json::get_string(d.cfg.at("execution"), "active");
      }();
      const bool reads_only = json::get_bool(cmd["plan"].value("effects", Json::object()), "reads_only", false);
      const bool no_questions = cmd.value("needs_confirmation", Json::array()).empty();
      if (!grows && !custom && no_questions && (mode == "always" || (mode == "read_only" && reads_only))) {
        // The proposal is said first: the result (a message of its own when the target says so) comes after it, in the order it happened.
        proposal["status"] = "running";
        LOOM_TRY_ASSIGN(early_id, add_message(rt, conv_id, "assistant", assistant_text, Json{{"action_proposal", proposal}, {"routed", "proposal"}}, "loom:actions"));
        auto done = rt.actions().confirm(Json{{"run_id", (*run)["id"]}, {"approved", true}, {"receipt_id", proposal["receipt_id"]},
                                              {"ref", user_said_yes ? "chat:" + user_id : "auto:" + json::get_string(d.cfg.at("execution"), "setting") + "=" + mode}});
        if (done) {
          Report rep = report_of(rt, d, *done, proposal.value("title", ""), lang);
          const std::string st = json::get_string(*done, "status");
          proposal["status"] = st == "completed" ? "executed" : st == "awaiting_usage" || st == "awaiting_confirmation" || st == "awaiting_client" ? "waiting" : st;
          assistant_text = rep.text;
          if (!posted_message(*done).empty()) assistant_text = say(d.cfg, lang, "result_completed", Json{{"title", proposal.value("title", "")}});
          meta["action_run"] = rep.meta;
          reply.type = "action_result";
        } else {
          assistant_text = say(d.cfg, lang, "proposal_error", Json{{"message", done.error().message}});
          proposal["status"] = "stale";
        }
      }
    }
    meta["action_proposal"] = proposal;
    reply.payload["proposal"] = proposal;
    if (meta.contains("action_run")) reply.payload["run"] = meta["action_run"];
  }
  meta["routed"] = "proposal";
  if (cmd.contains("meta_extra") && cmd["meta_extra"].is_object())
    for (auto it = cmd["meta_extra"].begin(); it != cmd["meta_extra"].end(); ++it) meta[it.key()] = it.value();
  reply.assistant_id = early_id;
  if (early_id.empty()) {
    LOOM_TRY_ASSIGN(reply.assistant_id, add_message(rt, conv_id, "assistant", assistant_text, meta, "loom:actions"));
  } else {
    MsgPatch patch;  // the proposal message says how it ended
    patch.text = assistant_text;
    patch.metadata = meta;
    LOOM_TRY(rt.db().update_msg(early_id, patch));
    announce_client_step(rt, conv_id, early_id, meta, "waiting");
  }
  if (meta.contains("action_run")) {
    if (auto linked = link_presented(rt, d, reply.assistant_id, meta["action_run"].value("produced", Json(nullptr)), meta["action_run"].value("run_id", "")); !linked)
      LOOM_TRY(set_metadata(rt, reply.assistant_id, [&](Json& m) { m["action_run"]["link_error"] = linked.error().message; }));
  }
  reply.text = assistant_text;
  return reply;
}

// After a command has run: has the conversation just shown the catalogue a way to grow? A user-defined action used on several classes (a group),
// the same chain of actions repeated (a sequence). Offered as one more proposal message, never made.
Json offer_growth(Runtime& rt, Data& d, const std::string& conv_id, const std::string& user_id, const std::string& action_run, const std::string& lang) {
  if (emergent::preset(rt, d.cfg) != "propose") return Json(nullptr);
  const emergent::Context ctx{rt, d.cfg, d.engine, d.resolver};
  std::optional<Json> def = emergent::from_group(ctx, action_run);
  if (!def) def = emergent::from_sequence(ctx, conv_id);
  if (!def) return Json(nullptr);
  Cand c;
  c.source = "emergent";
  c.matched = def->value("id", "");
  c.lang = lang;
  c.action = json::get_string(*def, "writer");
  c.args = def->at("args");
  c.element = Json{{"id", "application"}};
  c.score = 0.9;
  c.emergent = *def;
  const Conv conv = conv_of(rt, conv_id);
  Json cmd = evaluate(rt, d, c, conv, lang, std::string());
  if (!cmd.value("feasible", false)) return Json(nullptr);
  auto reply = reply_to_command(rt, d, conv_id, user_id, std::string(), cmd, "emergent", lang, false);
  if (!reply) {
    log::write(log::Level::Debug, "loom.chat_route", "growth proposal failed: " + reply.error().message);
    return Json(nullptr);
  }
  return Json{{"message_id", reply->assistant_id}, {"proposal", reply->payload.value("proposal", Json(nullptr))}};
}

// ── the sentence behind a rejected proposal (setting intent_routing_on_reject) ───

// offer | send_as_message | drop; a name the profile does not list is its `unknown` preset, no setting is its `active` one.
std::string on_reject_mode(Runtime& rt, const Data& d) {
  const Json& cfg = d.cfg.at("on_reject");
  const Json v = rt.config().get(json::get_string(cfg, "setting"));
  if (v.is_string() && !v.get<std::string>().empty())
    return cfg.at("presets").contains(v.get<std::string>()) ? v.get<std::string>() : json::get_string(cfg, "unknown", json::get_string(cfg, "active"));
  return json::get_string(cfg, "active");
}

const Message* message_by_id(const std::vector<Message>& msgs, const std::string& id) {
  for (const auto& m : msgs)
    if (m.id == id) return &m;
  return nullptr;
}

Json offer_of(const Message& proposal) {
  if (!proposal.metadata.is_object() || !proposal.metadata.contains("action_proposal")) return Json(nullptr);
  return proposal.metadata["action_proposal"].value("offer", Json(nullptr));
}

// The messages of one proposal's exchange: the sentence it was read from, the proposal itself, the user's answers to it and
// the replies that carry its offer. A request that sends the sentence as a message leaves them out of the history.
std::vector<std::string> exchange_of(const std::vector<Message>& msgs, const std::string& proposal_id, const std::string& source_id) {
  std::vector<std::string> ids;
  for (const auto& m : msgs) {
    bool own = m.id == source_id || m.id == proposal_id;
    if (!own && m.metadata.is_object())
      own = json::get_string(m.metadata, "answers") == proposal_id ||
            json::get_string(m.metadata.value("action_offer", Json::object()), "proposal_message_id") == proposal_id;
    if (own) ids.push_back(m.id);
  }
  return ids;
}

// The user takes the offer: the sentence is to be sent as an ordinary message. The offer is consumed (status sent), so a second
// click cannot send it twice; the re-send of the same request after the usage guard held it (`again`) finds it consumed and goes on.
Result<Resend> take_offer(Runtime& rt, const Data& d, const std::string& conv_id, const std::string& proposal_id, bool again) {
  LOOM_TRY_ASSIGN(auto msgs, rt.db().get_msgs(conv_id));
  const Message* proposal = message_by_id(msgs, proposal_id);
  const Json offer = proposal ? offer_of(*proposal) : Json(nullptr);
  const std::string lang = proposal ? json::get_string(proposal->metadata.value("action_proposal", Json::object()), "lang", "en") : std::string("en");
  auto gone = [&](const std::string& why, Errc code) { return Error(code, say(d.cfg, lang, "offer_gone", Json{{"why", why}})); };
  if (!proposal) return gone("no proposal '" + proposal_id + "' in this conversation", Errc::NotFound);
  if (!offer.is_object()) return gone("that proposal offers nothing to send", Errc::NotFound);
  const std::string status = json::get_string(offer, "status");
  if (status != "open" && !(again && status == "sent")) return gone("the offer is " + status, Errc::AlreadyExists);
  const std::string source_id = json::get_string(offer, "message_id");
  const Message* source = message_by_id(msgs, source_id);
  if (!source || source->text.empty()) return gone("the message '" + source_id + "' is gone", Errc::NotFound);
  Resend r;
  r.text = source->text;
  r.history_skip = exchange_of(msgs, proposal_id, source_id);
  r.proposal_message_id = proposal_id;
  r.source_message_id = source_id;
  if (status == "open")
    LOOM_TRY(set_metadata(rt, proposal_id, [&](Json& m) { m["action_proposal"]["offer"]["status"] = "sent"; }));
  return r;
}

// ── answering a proposal ────────────────────────────────────────────

struct Settled {
  Json run = nullptr;
  std::string status;  // the proposal's new status
  std::string assistant_id, text, type = "action_result";
  Json payload = Json::object();
  Json offer = nullptr;
  bool send_now = false;  // on_reject = send_as_message: the sentence is to be sent as a message now
  std::string conv_id;    // set when the answer is said in a conversation other than the one asked in (the run deleted that one)
};

// Settles the proposal that waits in `proposal_msg` with the answer `run` came back with: its status, the reply (the result, the
// definition written, or the acknowledgement of a no with the offer to send the sentence as a message), the follow-ups. Shared by the
// typed yes/no (`user_id` = the user's answer message) and the clients' confirm call (`user_id` empty).
Result<Settled> settle_answer(Runtime& rt, Data& d, const std::string& conv_in, const Message& proposal_msg, bool yes, const Result<Json>& run,
                              const std::string& user_id) {
  // The run may have deleted the conversation it was asked in ("delete this conversation"): the answer is then said in a new one.
  std::string conv_id = conv_in;
  bool moved = false;
  if (auto cv = rt.db().get_conv(conv_in); cv && !*cv) {
    LOOM_TRY_ASSIGN(auto fresh, rt.db().create_conv("New Chat"));
    conv_id = fresh.id;
    moved = true;
  }
  Json proposal = proposal_msg.metadata.at("action_proposal");
  const std::string lang = json::get_string(proposal, "lang", "en");
  const std::string source_id = proposal.value("sentence", Json::object()).value("message_id", "");
  const std::string cause = user_id.empty() ? source_id : user_id;  // what a definition or a growth offer remembers it came from
  Report rep;
  std::string new_status;
  Json offer = nullptr;
  bool send_now = false;
  if (!run) {
    rep.text = say(d.cfg, lang, "proposal_error", Json{{"message", run.error().message}});
    rep.meta = Json{{"run_id", proposal.value("run_id", "")}, {"status", "error"}, {"error", run.error().message}};
    new_status = "stale";
  } else if (!yes) {
    rep.text = say(d.cfg, lang, "ack_no");
    rep.meta = Json{{"run_id", proposal.value("run_id", "")}, {"status", json::get_string(*run, "status")}};
    new_status = "rejected";
    rt.guardian().note_rejection(conv_id, proposal_msg.id, proposal, user_id);  // a signal for the guardian (the cycle of corrections)
    // A no drops the command, never the user's sentence: the setting says whether it is offered, sent or left.
    const std::string mode = on_reject_mode(rt, d);
    if (!source_id.empty() && mode != "drop") {
      LOOM_TRY_ASSIGN(auto source, rt.db().get_msg(source_id));
      if (source && !source->text.empty()) {
        send_now = mode == "send_as_message";
        offer = Json{{"action", "send_as_message"}, {"status", "open"}, {"message_id", source_id}, {"mode", mode}};
        rep.text = say(d.cfg, lang, send_now ? "ack_no_send" : "ack_no_offer");
      }
    }
  } else {
    rep = report_of(rt, d, *run, proposal.value("title", ""), lang);
    const std::string st = json::get_string(*run, "status");
    new_status = st == "completed" ? "executed" : st == "awaiting_usage" || st == "awaiting_confirmation" || st == "awaiting_client" ? "waiting" : st;
  }
  // A proposal to grow the catalogue: what was written is said, and the node that records its origin follows the answer.
  const Json definition = proposal.value("definition", Json(nullptr));
  const std::string grow_kind = definition.is_object() ? definition.value("kind", "") : std::string();
  const std::string node = definition.is_object() ? definition.value("node", "") : std::string();
  Json written_view = nullptr;  // what the accepted definition wrote, kept on the proposal so the block can show it and take it back
  if (!grow_kind.empty()) {
    if (new_status == "executed") {
      const Json written = run->value("result", Json::object()).value("data", Json::object());
      std::vector<std::string> overlays;
      for (const auto& o : written.value("overlays", Json::array())) overlays.push_back(o.get<std::string>());
      rep.text = say(d.cfg, lang, grow_kind == "define_group" ? "defined_group_done" : "defined_done",
                     Json{{"id", definition.value("id", "")}, {"overlay", overlays.empty() ? overlay_file_of(grow_kind) : join(overlays, ", ")}});
      written_view = Json{{"kind", grow_kind}, {"id", definition.value("id", "")}, {"overlays", overlays.empty() ? Json::array({overlay_file_of(grow_kind)}) : written.value("overlays", Json::array())},
                          {"entry", grow_kind == "define_group" ? written.value("group", Json(nullptr)) : written.value("entry", Json(nullptr))}};
    } else if (new_status == "rejected" || new_status == "stale") {
      emergent::settle(rt, node, new_status);
    }
  }
  // A rule written from the conversation is shown the same way and taken back with one click (rules.disable).
  if (grow_kind.empty() && new_status == "executed" && proposal.value("action", "") == "rules.set") {
    const Json written = run->value("result", Json::object()).value("data", Json::object());
    if (!written.value("id", "").empty())
      written_view = Json{{"kind", "rule"}, {"id", written.value("id", "")}, {"overlays", Json::array({written.value("overlay", "profiles/action_rules.pack")})},
                          {"entry", written.value("rule", Json(nullptr))}};
  }
  if (!moved) LOOM_TRY(set_metadata(rt, proposal_msg.id, [&](Json& m) {
    m["action_proposal"]["status"] = new_status;
    if (offer.is_object()) m["action_proposal"]["offer"] = offer;
    if (written_view.is_object()) m["action_proposal"]["written"] = written_view;
  }));
  LOOM_TRY_ASSIGN(std::string assistant_id, answer_message(rt, d, conv_id, run ? &*run : nullptr, rep));
  if (proposal_msg.metadata.contains("guardian"))
    LOOM_TRY(set_metadata(rt, assistant_id, [&](Json& m) { m["guardian"] = Json{{"traffic", true}, {"answers", proposal_msg.id}}; }));
  if (offer.is_object())  // the reply the offer is made in: the words that take it are read while it is the last thing said
    LOOM_TRY(set_metadata(rt, assistant_id, [&](Json& m) { m["action_offer"] = Json{{"proposal_message_id", proposal_msg.id}}; }));
  Json payload{{"run", rep.meta}, {"proposal_message_id", proposal_msg.id}, {"proposal_status", new_status}};
  if (offer.is_object()) payload["offer"] = offer;
  Settled out;
  out.run = run ? *run : Json(nullptr);
  out.status = new_status;
  out.text = rep.text;
  out.offer = offer;
  out.send_now = send_now;
  if (moved) out.conv_id = conv_id;

  if (grow_kind == "define_action" && new_status == "executed") {
    // The user said yes to the definition and to the command it came from: the new action runs on the element the words were about.
    payload["defined"] = Json{{"id", definition.value("id", "")}, {"message_id", assistant_id}, {"kind", grow_kind}};
    if (const Json* then = json::find(definition, "then_run")) {
      LOOM_TRY_ASSIGN(Data fresh, load_data(rt));  // the catalogue has changed
      Cand c;
      c.source = "emergent";
      c.matched = definition.value("id", "");
      c.lang = lang;
      c.action = definition.value("id", "");
      c.element = Json{{"id", then->value("element", Json::object()).value("id", "")}};
      c.score = 0.9;
      const Json cmd = evaluate(rt, fresh, c, conv_of(rt, conv_id), lang, std::string());
      LOOM_TRY_ASSIGN(Reply next, reply_to_command(rt, fresh, conv_id, cause, std::string(), cmd, "emergent", lang, true));
      assistant_id = next.assistant_id;
      out.text = next.text;
      out.type = next.type;
      for (auto it = next.payload.begin(); it != next.payload.end(); ++it) payload[it.key()] = it.value();
    }
  } else if (grow_kind.empty() && new_status == "executed") {
    const std::string action = run->value("request", Json::object()).value("action", "");
    if (Json follow = offer_growth(rt, d, conv_id, cause, action, lang); !follow.is_null()) payload["follow_up"] = follow;
  }
  out.assistant_id = assistant_id;
  out.payload = payload;
  return out;
}

// The user answered a pending proposal with a yes or a no.
Result<Outcome> answer_proposal(Runtime& rt, Data& d, const std::string& conv_id, const Message& proposal_msg, const std::string& answer,
                                const std::string& text, const std::function<void(const std::string&, const std::string&)>& emit_start) {
  const Json proposal = proposal_msg.metadata.at("action_proposal");
  Json answer_meta{{"routed", "confirmation"}, {"answers", proposal_msg.id}};
  if (proposal_msg.metadata.contains("guardian")) answer_meta["guardian"] = Json{{"traffic", true}};  // the guardian's own exchange is not conversation content
  LOOM_TRY_ASSIGN(std::string user_id, add_message(rt, conv_id, "user", text, answer_meta));
  if (emit_start) emit_start(conv_id, user_id);
  (void)rt.chat().load_conv(conv_id);
  const bool yes = answer == "yes";
  const auto run = rt.actions().confirm(Json{{"run_id", proposal.value("run_id", "")}, {"approved", yes}, {"receipt_id", proposal.value("receipt_id", "")}, {"ref", "chat:" + user_id}});
  LOOM_TRY_ASSIGN(Settled settled, settle_answer(rt, d, conv_id, proposal_msg, yes, run, user_id));
  Outcome out = settled.conv_id.empty() ? finish(conv_id, user_id, settled.assistant_id, settled.text, settled.type, settled.payload)
                                        : finish(settled.conv_id, std::string(), settled.assistant_id, settled.text, settled.type, settled.payload);
  if (settled.send_now) {
    // intent_routing_on_reject = send_as_message: the chat path sends the sentence now, as the message it would have been.
    auto taken = take_offer(rt, d, conv_id, proposal_msg.id, false);
    if (taken) {
      out.handled = false;
      out.resend = *taken;
    } else {
      log::write(log::Level::Debug, "loom.chat_route", "the sentence could not be sent as a message: " + taken.error().message);
    }
  }
  return out;
}

// The user answered the offer made in the last reply with the words that take it (or close it).
Result<std::optional<Outcome>> answer_offer(Runtime& rt, Data& d, const std::string& conv_id, const Message& proposal_msg, const std::string& answer,
                                            const std::string& text, const std::function<void(const std::string&, const std::string&)>& emit_start) {
  LOOM_TRY_ASSIGN(std::string user_id, add_message(rt, conv_id, "user", text, Json{{"routed", "confirmation"}, {"answers", proposal_msg.id}}));
  if (emit_start) emit_start(conv_id, user_id);
  (void)rt.chat().load_conv(conv_id);
  Outcome out;
  if (answer == "send") {
    LOOM_TRY_ASSIGN(Resend taken, take_offer(rt, d, conv_id, proposal_msg.id, false));
    out.conv_id = conv_id;
    out.user_message_id = user_id;
    out.resend = std::move(taken);  // handled stays false: the chat path sends it
    return std::optional<Outcome>(std::move(out));
  }
  const std::string lang = json::get_string(proposal_msg.metadata.at("action_proposal"), "lang", "en");
  LOOM_TRY(set_metadata(rt, proposal_msg.id, [&](Json& m) { m["action_proposal"]["offer"]["status"] = "declined"; }));
  const std::string said = say(d.cfg, lang, "offer_declined");
  LOOM_TRY_ASSIGN(std::string assistant_id, add_message(rt, conv_id, "assistant", said, Json{{"routed", "result"}, {"offer_closed", proposal_msg.id}}, "loom:actions"));
  return std::optional<Outcome>(finish(conv_id, user_id, assistant_id, said, "action_result",
                                       Json{{"proposal_message_id", proposal_msg.id}, {"proposal_status", "rejected"}, {"offer", Json{{"status", "declined"}}}}));
}

// ── the question of the background analysis (P17 3) ─────────────────

// The user answered the question of the background analysis (the last thing said in the conversation) with a yes or a no: the worker's own usage
// confirmation takes it, with the receipt the question carries.
Result<Outcome> answer_semantic_question(Runtime& rt, Data& d, const std::string& conv_id, const Message& question_msg, const std::string& answer,
                                         const std::string& text, const std::function<void(const std::string&, const std::string&)>& emit_start) {
  const Json question = question_msg.metadata.at("semantic_question");
  const std::string lang = json::get_string(question, "lang", "en");
  const std::string receipt = json::get_string(question, "receipt_id");
  const bool yes = answer == "yes";
  LOOM_TRY_ASSIGN(std::string user_id, add_message(rt, conv_id, "user", text, Json{{"routed", "confirmation"}, {"answers", question_msg.id}}));
  if (emit_start) emit_start(conv_id, user_id);
  (void)rt.chat().load_conv(conv_id);
  const std::string kind = json::get_string(question, "kind", "batch");
  auto done = rt.worker().confirm_usage(yes, receipt, "chat:" + user_id);
  std::string said, status;
  if (done) {
    status = yes ? "approved" : "declined";
    said = say(d.cfg, lang, yes ? "semantic_answer_yes" : (kind == "retry" ? "semantic_answer_no_retry" : "semantic_answer_no"));
  } else {
    status = "stale";  // the worker no longer asks it: answered elsewhere, or the batch changed
    said = say(d.cfg, lang, "semantic_answer_gone", Json{{"why", done.error().message}});
    note_semantic_answer(rt, Json{{"receipt_id", receipt}, {"approved", Json(nullptr)}, {"conv_ids", Json::array({conv_id})}});
  }
  LOOM_TRY_ASSIGN(std::string assistant_id, add_message(rt, conv_id, "assistant", said,
                                                        Json{{"routed", "result"}, {"semantic_answer", Json{{"receipt_id", receipt}, {"status", status}, {"question_message_id", question_msg.id}}}},
                                                        "loom:actions"));
  return finish(conv_id, user_id, assistant_id, said, "action_result",
                Json{{"semantic_question_message_id", question_msg.id}, {"semantic_status", status}, {"receipt_id", receipt}});
}

}  // namespace

Result<Json> offer(Runtime& rt, const OfferRequest& request) {
  if (request.conv_id.empty()) return Error(Errc::InvalidArgument, "a proposal needs a conversation");
  LOOM_TRY_ASSIGN(Data d, load_data(rt));
  Cand c;
  c.source = "guardian";
  c.matched = request.action;
  c.lang = request.lang;
  c.action = request.action;
  c.args = request.args;
  c.element = Json{{"id", "application"}};
  c.score = 1.0;
  Json cmd = evaluate(rt, d, c, conv_of(rt, request.conv_id), request.lang, std::string());
  if (!cmd.value("feasible", false) || !cmd.value("runnable", false) || !cmd.value("denied", Json::array()).empty())
    return Error(Errc::Conflict, "the proposal cannot be made: " + cmd.value("reason", Json::object()).value("message", std::string("denied by a rule or not runnable")));
  cmd["proposal_text"] = request.texts;
  cmd["meta_extra"] = request.meta;
  LOOM_TRY_ASSIGN(Reply reply, reply_to_command(rt, d, request.conv_id, std::string(), std::string(), cmd, "guardian", request.lang, false));
  return Json{{"message_id", reply.assistant_id}, {"proposal", reply.payload.value("proposal", Json(nullptr))}};
}

Result<Outcome> handle(Runtime& rt, const std::string& text, const ChatOptions& opts, const Json& request,
                       const std::function<void(const std::string&, const std::string&)>& emit_start) {
  Outcome none;
  LOOM_TRY_ASSIGN(Data d, load_data(rt, json::get_string(request, "client_id")));
  std::string conv_id = opts.conv_id.value_or(std::string());
  if (conv_id.empty())
    if (auto current = rt.chat().current_conv()) conv_id = current->id;

  // 0. The button of an offer: the sentence of a rejected proposal goes on the chat path as an ordinary message, whatever the routing method.
  if (!opts.send_as_message.empty()) {
    auto taken = conv_id.empty() ? Result<Resend>(Error(Errc::NotFound, "no conversation")) : take_offer(rt, d, conv_id, opts.send_as_message, opts.usage_confirmation.has_value());
    if (!taken) {
      Outcome failed;
      failed.handled = true;
      failed.conv_id = conv_id;
      const Json err{{"code", std::string(errc_name(taken.error().code))}, {"message", taken.error().message}};
      failed.chunk = Json{{"type", "error"}, {"code", err["code"]}, {"message", err["message"]}};
      failed.result = Json{{"error", err}};
      return failed;
    }
    none.conv_id = conv_id;
    none.resend = std::move(*taken);
    return none;
  }

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

  // 1. The answer to a proposal that waits as the last thing said in this conversation, or to the offer made in the last reply.
  if (!conv_id.empty()) {
    if (auto msgs = rt.db().get_msgs(conv_id); msgs && !msgs->empty()) {
      const Message& last = msgs->back();
      if (last.role == "assistant" && last.metadata.is_object() && last.metadata.contains("semantic_question") &&
          json::get_string(last.metadata["semantic_question"], "status") == "pending") {
        const std::string answer = classify_answer(d, text);
        if (!answer.empty()) return answer_semantic_question(rt, d, conv_id, last, answer, text, emit_start);
      } else if (last.role == "assistant" && last.metadata.is_object() && last.metadata.contains("action_proposal") &&
          json::get_string(last.metadata["action_proposal"], "status") == "pending" && !json::get_string(last.metadata["action_proposal"], "run_id").empty()) {
        const std::string answer = classify_answer(d, text);
        if (!answer.empty()) return answer_proposal(rt, d, conv_id, last, answer, text, emit_start);
      } else if (last.role == "assistant" && last.metadata.is_object() && last.metadata.contains("action_offer")) {
        const Message* proposal = message_by_id(*msgs, json::get_string(last.metadata["action_offer"], "proposal_message_id"));
        const Json offer = proposal ? offer_of(*proposal) : Json(nullptr);
        if (offer.is_object() && json::get_string(offer, "status") == "open") {
          const std::string answer = classify_offer(d, text);
          if (!answer.empty()) {
            LOOM_TRY_ASSIGN(std::optional<Outcome> done, answer_offer(rt, d, conv_id, *proposal, answer, text, emit_start));
            return std::move(*done);
          }
        }
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
  none.issues = routed.value("issues", Json::array());
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
    out.issues = none.issues;
    return out;
  }

  const Json cmd = routed["command"];
  const std::string lang = routed.value("lang", "en");
  // The user has refused this very proposal often enough in this conversation (the guardian's do-not-propose rule): the sentence is a message.
  if (!conv_id.empty() && !cmd.contains("proposal_text") && guardian::do_not_propose(rt.db(), rt.config(), conv_id, cmd.value("title", ""))) return none;
  if (conv_id.empty()) {
    LOOM_TRY_ASSIGN(auto created, rt.db().create_conv("New Chat"));
    conv_id = created.id;
  }
  (void)rt.chat().load_conv(conv_id);
  LOOM_TRY_ASSIGN(std::string user_id, add_message(rt, conv_id, "user", text, Json{{"routed", "command"}}));
  if (emit_start) emit_start(conv_id, user_id);

  LOOM_TRY_ASSIGN(Reply reply, reply_to_command(rt, d, conv_id, user_id, text, cmd, routed.value("method", ""), lang, false));
  Json payload = reply.payload;
  if (reply.meta.contains("action_run") && reply.meta["action_run"].value("status", "") == "completed") {
    if (Json follow = offer_growth(rt, d, conv_id, user_id, cmd.value("action", ""), lang); !follow.is_null()) payload["follow_up"] = follow;
  }
  title_first_exchange(rt, conv_id, text);
  Outcome handled = finish(conv_id, user_id, reply.assistant_id, reply.text, reply.type, payload);
  handled.issues = none.issues;
  return handled;
}

void note_removed(Runtime& rt, const Json& origin) {
  const std::string conv_id = json::get_string(origin, "conv_id");
  const std::string node = json::get_string(origin, "node");
  if (conv_id.empty() || node.empty()) return;
  auto msgs = rt.db().get_msgs(conv_id);
  if (!msgs) return;
  for (const auto& m : *msgs) {
    if (m.role != "assistant" || !m.metadata.is_object() || !m.metadata.contains("action_proposal")) continue;
    const Json& p = m.metadata["action_proposal"];
    if (json::get_string(p, "status") != "executed" || json::get_string(p.value("definition", Json::object()), "node") != node) continue;
    if (auto st = set_metadata(rt, m.id, [&](Json& meta) {
          meta["action_proposal"]["status"] = "undone";
          if (meta["action_proposal"].contains("written")) meta["action_proposal"]["written"]["undone"] = true;
        }); !st)
      log::write(log::Level::Debug, "loom.chat_route", "could not mark the proposal as taken back: " + st.error().message);
  }
}

namespace {
Result<Json> after_pause(Runtime& rt, Json run);  // below, with the client step's answer

// The growth the guard saw: the largest ratio among the resources that asked for the confirmation (null when the guard knows none).
Json hold_ratio(const Json& pending) {
  double best = 0;
  bool any = false;
  const Json projected = pending.value("projected", Json::object());
  if (projected.is_object())
    for (auto it = projected.begin(); it != projected.end(); ++it) {
      if (!it->is_object() || !json::get_bool(*it, "requires_confirmation", false)) continue;
      const Json ratio = it->value("ratio", Json(nullptr));
      if (ratio.is_number() && ratio.get<double>() > best) {
        best = ratio.get<double>();
        any = true;
      }
    }
  return any ? Json(best) : Json(nullptr);
}

// "12" for 12.4, "1.5" for 1.5: the growth as a person says it.
std::string ratio_words(double ratio) {
  char buf[32];
  if (ratio >= 10) std::snprintf(buf, sizeof(buf), "%.0f", ratio);
  else std::snprintf(buf, sizeof(buf), "%.1f", ratio);
  return buf;
}

// The conversation whose message was being analysed: the newest of those the hold names (conv_ids), else the one of the first message it lists (ids).
std::string conversation_of_hold(Runtime& rt, const Json& pending) {
  for (const auto& id : pending.value("conv_ids", Json::array()))
    if (id.is_string() && !id.get<std::string>().empty()) return id.get<std::string>();
  for (const auto& id : pending.value("ids", Json::array())) {
    if (!id.is_string()) continue;
    auto m = rt.db().get_msg(id.get<std::string>());
    if (m && *m && !(*m)->conv_id.empty()) return (*m)->conv_id;
  }
  return std::string();
}

// The question is told on the one event every question of a conversation uses (events::kActionClientStep): ids only.
void announce_question(Runtime& rt, const std::string& conv_id, const std::string& message_id, const std::string& receipt, const char* state, const std::string& status = std::string()) {
  Json payload{{"conv_id", conv_id}, {"message_id", message_id}, {"run_id", "semantic:" + receipt}, {"receipt_id", receipt}, {"question", "semantic"}, {"state", state}};
  if (!status.empty()) payload["status"] = status;
  rt.bus().emit(events::kActionClientStep, payload);
}
}  // namespace

void note_semantic_hold(Runtime& rt, const Json& pending) {
  const std::string receipt = json::get_string(pending, "receipt_id");
  const std::string conv_id = conversation_of_hold(rt, pending);
  if (receipt.empty() || conv_id.empty()) return;
  auto msgs = rt.db().get_msgs(conv_id);
  auto loaded = load_data(rt);
  if (!msgs || !loaded) return;
  Data d = std::move(*loaded);
  for (const auto& m : *msgs) {
    if (m.role != "assistant" || !m.metadata.is_object() || !m.metadata.contains("semantic_question")) continue;
    const Json q = m.metadata["semantic_question"];
    if (json::get_string(q, "receipt_id") == receipt) return;  // shown already
    if (json::get_string(q, "status") != "pending") continue;
    // an older question: the guard has a newer one, the old receipt cannot be answered any more
    if (set_metadata(rt, m.id, [&](Json& meta) { meta["semantic_question"]["status"] = "stale"; }))
      announce_question(rt, conv_id, m.id, json::get_string(q, "receipt_id"), "closed", "stale");
  }
  std::string lang = language_of_conversation(rt, d, conv_id);
  if (lang.empty()) lang = json::get_string(d.cfg, "default_language", "en");
  const std::string kind = json::get_string(pending, "kind", "batch");
  // P18: a hold or refusal by a spending limit says that, with the limit's own reason in the language of the conversation.
  std::string limit_reason;
  if (const Json limits = pending.value("limits", Json::array()); limits.is_array() && !limits.empty()) {
    const Json reason = limits[0].value("reason", Json::object());
    for (const std::string& key : {lang, std::string("en")})
      if (const Json* t = json::find(reason, key); t && t->is_string() && !t->get<std::string>().empty()) { limit_reason = t->get<std::string>(); break; }
  }
  if (json::get_string(pending, "status") == "denied" || json::get_bool(pending, "refused", false)) {
    // Nothing can be answered: the analysis is told as paused (once per receipt) and tries again later.
    for (const auto& m : *msgs)
      if (m.role == "assistant" && m.metadata.is_object() && m.metadata.contains("semantic_refusal") &&
          json::get_string(m.metadata["semantic_refusal"], "receipt_id") == receipt)
        return;
    const std::string note = say(d.cfg, lang, "semantic_refused", Json{{"reason", limit_reason}, {"count", pending.value("messages", 0)}});
    Json refusal{{"schema", "loom.semantic_usage_refusal/1"}, {"lang", lang}, {"kind", kind}, {"receipt_id", receipt},
                 {"limits", pending.value("limits", Json::array())}, {"reason_codes", pending.value("reason_codes", Json::array())}};
    auto posted = add_message(rt, conv_id, "assistant", note, Json{{"routed", "semantic_refusal"}, {"semantic_refusal", refusal}}, "loom:actions");
    if (posted) announce_question(rt, conv_id, *posted, receipt, "closed", "refused");
    return;
  }
  const Json ratio = hold_ratio(pending);
  const std::string text = !limit_reason.empty()
      ? say(d.cfg, lang, std::string("semantic_question_limit_") + (kind == "retry" ? "retry" : "batch"), Json{{"reason", limit_reason}, {"count", pending.value("messages", 0)}})
      : say(d.cfg, lang, std::string("semantic_question_") + (kind == "retry" ? "retry" : "batch") + (ratio.is_number() ? "" : "_unknown"),
            Json{{"ratio", ratio.is_number() ? ratio_words(ratio.get<double>()) : std::string()}, {"count", pending.value("messages", 0)}});
  Json question{{"schema", "loom.semantic_usage_question/1"}, {"status", "pending"}, {"lang", lang}, {"kind", kind}, {"receipt_id", receipt},
                {"operation_id", pending.value("operation_id", Json(nullptr))}, {"ratio", ratio}, {"receipt", pending},
                {"reason_codes", pending.value("reason_codes", Json::array())}};
  auto posted = add_message(rt, conv_id, "assistant", text, Json{{"routed", "semantic_question"}, {"semantic_question", question}}, "loom:actions");
  if (!posted) {
    log::write(log::Level::Debug, "loom.chat_route", "the question of the background analysis could not be posted: " + posted.error().message);
    return;
  }
  announce_question(rt, conv_id, *posted, receipt, "waiting");
}

void note_semantic_answer(Runtime& rt, const Json& answer) {
  const std::string receipt = json::get_string(answer, "receipt_id");
  if (receipt.empty()) return;
  const Json approved = answer.value("approved", Json(nullptr));
  const std::string status = approved.is_boolean() ? (approved.get<bool>() ? "approved" : "declined") : "stale";
  // The question is in the conversation of the message that was analysed: the answer carries the conversations the hold named.
  const std::string conv_id = conversation_of_hold(rt, answer);
  if (conv_id.empty()) return;
  auto msgs = rt.db().get_msgs(conv_id);
  if (!msgs) return;
  for (const auto& m : *msgs) {
    if (m.role != "assistant" || !m.metadata.is_object() || !m.metadata.contains("semantic_question")) continue;
    const Json q = m.metadata["semantic_question"];
    if (json::get_string(q, "receipt_id") != receipt || json::get_string(q, "status") != "pending") continue;
    if (set_metadata(rt, m.id, [&](Json& meta) {
          meta["semantic_question"]["status"] = status;
          meta["semantic_question"]["answered"] = Json{{"at", timeutil::utc_now_iso()}, {"ref", json::get_string(answer, "ref")}};
        }))
      announce_question(rt, conv_id, m.id, receipt, "closed", status);
  }
}

Result<Json> confirm(Runtime& rt, const Json& request) {
  const std::string run_id = json::get_string(request, "run_id");
  if (run_id.empty()) return rt.actions().confirm(request);
  // The proposal of a conversation that waits for exactly this run; without one the call is the plain action.
  auto status = rt.actions().status(Json{{"run_id", run_id}});
  const std::string conv_id = status ? json::get_string(status->value("request", Json::object()).value("context", Json::object()), "conv_id") : std::string();
  if (conv_id.empty()) return rt.actions().confirm(request);
  auto msgs = rt.db().get_msgs(conv_id);
  if (!msgs) return rt.actions().confirm(request);
  const Message* waiting = nullptr;
  for (auto it = msgs->rbegin(); it != msgs->rend() && !waiting; ++it)
    if (it->role == "assistant" && it->metadata.is_object() && it->metadata.contains("action_proposal") &&
        json::get_string(it->metadata["action_proposal"], "run_id") == run_id && json::get_string(it->metadata["action_proposal"], "status") == "pending")
      waiting = &*it;
  auto loaded = load_data(rt);
  if (!waiting || !loaded) {
    // No proposal waits for this run, but the run may be paused on a question the conversation showed (the usage guard's x10 hold, a rule's
    // confirmation): the answer ends the pause and the conversation hears how it went, as it does after the typed "yes".
    const std::string before = status ? json::get_string(*status, "status") : std::string();
    auto answered = rt.actions().confirm(request);
    if (!answered || (before != "awaiting_usage" && before != "awaiting_confirmation")) return answered;
    return after_pause(rt, std::move(*answered));
  }
  Data d = std::move(*loaded);
  const bool yes = json::get_bool(request, "approved", false);
  (void)rt.chat().load_conv(conv_id);
  auto run = rt.actions().confirm(request);
  if (!run) return run.error();  // a wrong receipt or a run that is gone: the proposal is not settled by a call that did not answer it
  LOOM_TRY_ASSIGN(Settled settled, settle_answer(rt, d, conv_id, *waiting, yes, run, std::string()));
  Json out = *run;
  out["proposal_message_id"] = waiting->id;
  out["proposal_status"] = settled.status;
  out["message_id"] = settled.assistant_id;
  if (!settled.conv_id.empty()) out["conv_id"] = settled.conv_id;
  if (settled.offer.is_object()) out["offer"] = settled.offer;
  if (settled.send_now) out["resend"] = Json{{"proposal_message_id", waiting->id}};
  if (settled.payload.contains("follow_up")) out["follow_up"] = settled.payload["follow_up"];
  if (settled.payload.contains("defined")) out["defined"] = settled.payload["defined"];
  return out;
}

namespace {

// A run that was paused on a question (the client's step) has moved on: the messages of the conversation that showed the question stop
// showing it - their pending question is closed and their status says how it went - and the proposal that waited for the run is settled.
void close_questions(Runtime& rt, const std::string& conv_id, const Json& run, const std::string& proposal_status) {
  auto msgs = rt.db().get_msgs(conv_id);
  if (!msgs) return;
  const std::string run_id = json::get_string(run, "id");
  const std::string status = json::get_string(run, "status");
  for (const auto& m : *msgs) {
    if (m.role != "assistant" || !m.metadata.is_object()) continue;
    if (m.metadata.contains("action_run") && json::get_string(m.metadata["action_run"], "run_id") == run_id &&
        m.metadata["action_run"].value("pending", Json(nullptr)).is_object()) {
      const bool was_client_step = json::get_string(m.metadata["action_run"]["pending"], "kind") == "client_step";
      Json closed;
      (void)set_metadata(rt, m.id, [&](Json& meta) {
        meta["action_run"]["pending"] = nullptr;
        meta["action_run"]["status"] = status;
        closed = meta;
      });
      if (was_client_step) announce_client_step(rt, conv_id, m.id, closed, "closed");
    }
    if (m.metadata.contains("action_proposal") && json::get_string(m.metadata["action_proposal"], "run_id") == run_id &&
        json::get_string(m.metadata["action_proposal"], "status") == "waiting" && !proposal_status.empty())
      (void)set_metadata(rt, m.id, [&](Json& meta) { meta["action_proposal"]["status"] = proposal_status; });
  }
}

// The conversation a run belongs to hears how it went once a pause is over: a message with the result (or the next question).
Result<Json> after_pause(Runtime& rt, Json run) {
  const std::string conv_id = json::get_string(run.value("request", Json::object()).value("context", Json::object()), "conv_id");
  if (conv_id.empty()) return run;
  const std::string status = json::get_string(run, "status");
  auto msgs = rt.db().get_msgs(conv_id);
  auto loaded = load_data(rt);
  if (!msgs || !loaded) return run;
  Data d = std::move(*loaded);
  Json proposal = nullptr;
  for (const auto& m : *msgs)
    if (m.role == "assistant" && m.metadata.is_object() && m.metadata.contains("action_proposal") &&
        json::get_string(m.metadata["action_proposal"], "run_id") == json::get_string(run, "id"))
      proposal = m.metadata["action_proposal"];
  const std::string lang = proposal.is_object() ? json::get_string(proposal, "lang", "en") : json::get_string(d.cfg, "default_language", "en");
  const std::string title = proposal.is_object() ? json::get_string(proposal, "title") : std::string();
  const std::string new_status = status == "completed" ? "executed" : status == "awaiting_client" || status == "awaiting_usage" || status == "awaiting_confirmation" ? "waiting"
                                 : status == "cancelled" ? "rejected" : status;
  close_questions(rt, conv_id, run, new_status);
  Report rep = report_of(rt, d, run, title, lang);
  auto posted = answer_message(rt, d, conv_id, &run, rep);
  if (!posted) return run;
  run["message_id"] = *posted;
  return run;
}

}  // namespace

void note_rule_switched(Runtime& rt, const std::string& rule_id, bool disabled) {
  auto profile = RuntimeProfile::load("action_rules", rt.paths().root);
  if (!profile || !profile->values().contains("rules")) return;
  const Json* rule = json::find(profile->values().at("rules"), rule_id);
  const std::string conv_id = rule ? json::get_string(rule->value("provenance", Json::object()), "conv_id") : std::string();
  if (conv_id.empty()) return;  // not written from a conversation: nothing there says so
  auto msgs = rt.db().get_msgs(conv_id);
  if (!msgs) return;
  for (const auto& m : *msgs) {
    if (m.role != "assistant" || !m.metadata.is_object() || !m.metadata.contains("action_proposal")) continue;
    const Json proposal = m.metadata["action_proposal"];
    const Json written = proposal.value("written", Json::object());
    if (json::get_string(written, "kind") != "rule" || json::get_string(written, "id") != rule_id) continue;
    (void)set_metadata(rt, m.id, [&](Json& meta) {
      meta["action_proposal"]["written"]["undone"] = disabled;
      meta["action_proposal"]["status"] = disabled ? "undone" : "executed";
    });
  }
}

Result<Json> client_result(Runtime& rt, const Json& request) {
  LOOM_TRY_ASSIGN(Json run, rt.actions().client_result(request));
  return after_pause(rt, std::move(run));
}

Result<Json> cancel(Runtime& rt, const Json& request) {
  // Only a run paused on the client's step has a question in the conversation to close; every other cancel is the plain action.
  auto before = rt.actions().status(Json{{"run_id", json::get_string(request, "run_id")}});
  const bool on_client = before && json::get_string(*before, "status") == "awaiting_client";
  LOOM_TRY_ASSIGN(Json answer, rt.actions().cancel(request));
  if (!on_client || json::get_string(answer, "status") == "cancelling") return answer;
  return after_pause(rt, std::move(answer));
}

}  // namespace loom::chat_route
