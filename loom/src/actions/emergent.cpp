// The catalogue grows from conversations (R55): proposals only. See include/loom/action_emergent.h.
#include "loom/action_emergent.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

#include "loom/action_element.h"
#include "loom/action_index.h"
#include "loom/action_rules.h"
#include "loom/action_runner.h"
#include "loom/config.h"
#include "loom/db.h"
#include "loom/log.h"
#include "loom/runtime.h"
#include "loom/runtime_profile.h"
#include "loom/util/unicode.h"
#include "loom/util/sha256.h"
#include "loom/util/time.h"
#include "loom/util/utf8.h"

namespace loom::actions::emergent {

namespace {

std::vector<std::string> strings_of(const Json* v) {
  std::vector<std::string> out;
  if (v && v->is_array())
    for (const auto& x : *v)
      if (x.is_string()) out.push_back(x.get<std::string>());
  return out;
}

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

std::vector<std::string> word_tokens(const std::string& text) {
  std::vector<std::string> out;
  std::u32string cur;
  for (char32_t cp : utf8::decode(text)) {
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

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
  std::string out;
  for (const auto& p : parts) out += (out.empty() ? "" : sep) + p;
  return out;
}

std::string bare(std::string tok) {
  static const std::string drop = ".,;:!?()[]{}\"'`<>";
  while (!tok.empty() && drop.find(tok.front()) != std::string::npos) tok.erase(tok.begin());
  while (!tok.empty() && drop.find(tok.back()) != std::string::npos) tok.pop_back();
  return tok;
}

std::string ascii_slug(const std::string& folded) {
  std::string out;
  for (char c : folded) {
    if (std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c))) out += c;
    else if (c == '_' || c == '-' || c == '.') out += c;
  }
  return out;
}

const Json& emergent_cfg(const Json& routing) {
  static const Json none = Json::object();
  const Json* e = json::find(routing, "emergent");
  return e ? *e : none;
}

std::string class_label(const Engine& engine, const std::string& cls, const std::string& lang) {
  if (const Json* k = json::find(engine.classes(), cls))
    if (const Json* l = json::find(*k, "label"))
      if (l->is_object()) {
        const std::string text = json::get_string(*l, lang, json::get_string(*l, "en"));
        if (!text.empty()) return text;
      }
  return cls;
}

}  // namespace

std::string preset(Runtime& rt, const Json& routing) {
  const Json& cfg = emergent_cfg(routing);
  std::string name = json::get_string(cfg, "active", "propose");
  const Json v = rt.config().get(json::get_string(cfg, "setting", "emergent_actions"));
  if (v.is_string() && !v.get<std::string>().empty()) name = v.get<std::string>();
  const Json* presets = json::find(cfg, "presets");
  return presets && json::find(*presets, name) ? name : std::string("off");
}

// ── a command no action understands ─────────────────────────────────

std::optional<Json> from_command(const Context& c, const std::string& text, const std::string& lang, const Json& element) {
  if (preset(c.rt, c.routing) != "propose") return std::nullopt;
  const Json& cfg = emergent_cfg(c.routing);
  const Json shape = cfg.value("command_shape", Json::object());
  if (json::get_bool(shape, "forbid_question_mark", true) && text.find('?') != std::string::npos) return std::nullopt;
  const Json& fold_map = c.routing.at("heuristic").at("fold");
  const auto tokens = word_tokens(fold(text, fold_map));
  const auto original_tokens = word_tokens(text);
  if (tokens.empty() || tokens.size() != original_tokens.size() || tokens.size() > static_cast<std::size_t>(json::get_int(shape, "max_tokens", 16))) return std::nullopt;
  const auto skip = strings_of(json::find(shape, "lead_skip"));
  const auto not_leads = strings_of(json::find(shape, "not_leads"));
  std::size_t idx = 0;
  while (idx < tokens.size() && std::find(skip.begin(), skip.end(), tokens[idx]) != skip.end()) ++idx;
  if (idx >= tokens.size() || idx > 2) return std::nullopt;
  const std::string verb = tokens[idx];
  if (utf8::length(verb) < static_cast<std::size_t>(json::get_int(shape, "min_lead_chars", 4))) return std::nullopt;
  if (std::find(not_leads.begin(), not_leads.end(), verb) != not_leads.end()) return std::nullopt;
  if (std::isdigit(static_cast<unsigned char>(verb[0])) || c.resolver.is_id(verb) || c.engine.has_class(verb)) return std::nullopt;
  if (!element.is_object()) return std::nullopt;
  // The element is the object of the command: named within a few words of the verb ("rozpisz tę wiadomość ..."), not somewhere in a request
  // for the model that merely mentions it ("napisz wiersz o tej rozmowie").
  {
    const auto raw_tokens = [&] {
      std::vector<std::string> out;
      std::istringstream split(text);
      for (std::string w; split >> w;) out.push_back(bare(w));
      return out;
    }();
    std::vector<std::string> reference_words;
    for (const auto& r : c.routing.at("heuristic").value("references", Json::array()))
      for (const auto& w : strings_of(json::find(r, "words"))) reference_words.push_back(fold(w, fold_map));
    const std::size_t within = static_cast<std::size_t>(json::get_int(shape, "element_within", 3));
    bool near = false;
    for (std::size_t k = idx + 1; k < tokens.size() && k <= idx + within && !near; ++k) {
      if (k < raw_tokens.size() && c.resolver.is_id(raw_tokens[k])) near = true;
      for (const auto& w : reference_words) {
        const bool prefix = !w.empty() && w.back() == '*';
        if (prefix ? tokens[k].rfind(w.substr(0, w.size() - 1), 0) == 0 : tokens[k] == w) near = true;
      }
    }
    // ids are separated by punctuation in some texts: the whitespace tokens are checked as well
    for (std::size_t k = 0; k < raw_tokens.size() && !near; ++k)
      if (k > idx && k <= idx + within && c.resolver.is_id(raw_tokens[k])) near = true;
    if (!near) return std::nullopt;
  }

  // The name comes from the user's word; a name that is taken gets a number.
  const Json naming = cfg.value("naming", Json::object());
  std::string base = json::get_string(naming, "prefix") + ascii_slug(verb);
  if (base.empty() || !std::islower(static_cast<unsigned char>(base[0])) ) base = "action_" + Sha256::hex(verb).substr(0, 6);
  base = std::string(utf8::prefix(base, static_cast<std::size_t>(json::get_int(naming, "max_chars", 24))));
  std::string id = base;
  for (int n = 2; json::find(c.engine.catalogue(), id); ++n) id = base + "_" + std::to_string(n);

  // The instruction: the user's own words, without the ids that named the element.
  std::vector<std::string> words;
  std::istringstream split(text);
  for (std::string w; split >> w;)
    if (!c.resolver.is_id(bare(w))) words.push_back(w);
  const std::string instruction = join(words, " ");

  // What the action will ask of an element: the content traits this one has.
  std::set<std::string> have = c.engine.expand_traits({json::get_string(element, "class")});
  for (const auto& t : strings_of(json::find(element, "traits"))) have.insert(t);
  Json needs = Json::array();
  for (const auto& t : strings_of(json::find(cfg, "requires_from")))
    if (have.count(t)) needs.push_back(t);
  if (needs.empty()) needs.push_back(json::get_string(element, "class").empty() ? "has_text" : json::get_string(element, "class"));

  std::string title = original_tokens[idx];
  if (!title.empty()) title = utf8::to_upper(std::string(utf8::prefix(title, 1))) + title.substr(std::string(utf8::prefix(title, 1)).size());

  Json args = Json::object();
  args["id"] = id;
  args["title"] = title;
  args["instruction"] = instruction;
  args["requires"] = needs;
  args["phrases"] = Json{{lang, Json::array({verb})}};
  args["lang"] = lang;
  Json sources = Json::array();
  if (const std::string eid = json::get_string(element, "id"); !eid.empty()) sources.push_back(eid);

  Json def = Json::object();
  def["kind"] = "define_action";
  def["writer"] = "actions.define";
  def["id"] = id;
  def["signature"] = "action:" + id;
  def["title"] = title;
  def["args"] = args;
  def["sources"] = sources;
  def["summary"] = Json{{"verb", original_tokens[idx]}, {"id", id}, {"instruction", instruction}, {"requires", join(strings_of(&needs), ", ")}};
  if (const std::string eid = json::get_string(element, "id"); !eid.empty()) def["then_run"] = Json{{"element", Json{{"id", eid}}}};
  return def;
}

// ── a sequence repeated ─────────────────────────────────────────────

namespace {

// Interpreted composites are the actions that work on content; the writers and the settings are not part of a sequence.
bool content_action(const Engine& engine, const std::string& id) {
  const Json* e = json::find(engine.catalogue(), id);
  return e && json::get_string(*e, "kind") == "composite" && json::get_string(*e, "status") == "implemented" && json::get_string(*e, "consumer").empty() &&
         e->contains("steps") && !(*e)["steps"].empty();
}

// The index of completed runs, brought up to date: it folds in only the checkpoints written since the last detection (action_index.h).
IndexSettings settings_of(const Context& c) {
  return IndexSettings::from_routing(emergent_cfg(c.routing).value("sequences", Json::object()));
}

SequenceIndex* fresh_index(const Context& c) {
  SequenceIndex& index = c.rt.actions().sequence_index();
  const Engine& engine = c.engine;
  auto status = index.refresh([&engine](const std::string& action) { return content_action(engine, action); }, settings_of(c));
  if (!status) {
    log::write(log::Level::Debug, "loom.emergent", "the sequence index could not be brought up to date: " + status.error().message);
    return nullptr;
  }
  return &index;
}

}  // namespace

std::optional<Json> from_sequence(const Context& c, const std::string& conv_id) {
  if (preset(c.rt, c.routing) != "propose") return std::nullopt;
  const Json& cfg = emergent_cfg(c.routing);
  const Json seq = cfg.value("sequences", Json::object());
  const IndexSettings settings = settings_of(c);
  const std::size_t repeat = static_cast<std::size_t>(json::get_int(seq, "repeat", 3));
  const std::size_t max_len = settings.max_len;
  // The index holds the last runs of each conversation and how many times each chain was run: nothing is read from the log but what is new.
  SequenceIndex* index = fresh_index(c);
  if (!index) return std::nullopt;
  const std::vector<SequenceItem> list = index->tail(conv_id);
  if (list.empty()) return std::nullopt;
  auto pipeline = [&](const std::vector<SequenceItem>& l, std::size_t from, std::size_t len) { return is_pipeline(l, from, len, settings.gap_seconds); };
  auto signature = [&](const std::vector<SequenceItem>& l, std::size_t from, std::size_t len) { return chain_signature(l, from, len); };
  const std::string node_kind = cfg.value("node", Json::object()).value("kind", "proposal");
  for (std::size_t len = std::min(max_len, list.size()); len >= 2; --len) {  // the longest repeated one first
    const std::size_t from = list.size() - len;
    if (!pipeline(list, from, len)) continue;
    const std::string sig = signature(list, from, len);
    const std::size_t times = index->count(sig);
    if (times < repeat) continue;
    if (auto seen = c.rt.db().find_node("sequence:" + sig, node_kind); seen && *seen) return std::nullopt;  // offered before: not asked again
    std::vector<std::string> names;
    Json steps = Json::array();
    for (std::size_t k = from; k < from + len; ++k) {
      names.push_back(list[k].action);
      steps.push_back(Json{{"action", list[k].action}});
    }
    // Already saved by hand or from an earlier conversation: nothing to offer.
    bool exists = false;
    for (auto it = c.engine.catalogue().begin(); it != c.engine.catalogue().end(); ++it) {
      const Json* st = json::find(it.value(), "steps");
      if (!st || !st->is_array() || st->size() != names.size()) continue;
      bool same = true;
      for (std::size_t k = 0; k < names.size() && same; ++k) same = json::get_string((*st)[k], "action") == names[k];
      exists = exists || same;
    }
    if (exists) return std::nullopt;
    const std::string base = join(names, json::get_string(seq, "joiner", "_then_"));
    std::string id = base;
    for (int n = 2; json::find(c.engine.catalogue(), id); ++n) id = base + "_" + std::to_string(n);
    Json sources = Json::array();
    for (std::size_t k = from; k < from + len; ++k) {
      if (!list[k].message_id.empty()) sources.push_back(list[k].message_id);
      if (!list[k].element_id.empty()) sources.push_back(list[k].element_id);
    }
    std::string spaced = id;
    std::replace(spaced.begin(), spaced.end(), '_', ' ');
    Json args = Json::object();
    args["id"] = id;
    args["title"] = join(names, " + ");
    args["steps"] = steps;
    args["phrases"] = Json{{"en", Json::array({spaced})}};
    Json def = Json::object();
    def["kind"] = "define_sequence";
    def["writer"] = "actions.define";
    def["id"] = id;
    def["signature"] = "sequence:" + sig;
    def["title"] = join(names, " + ");
    def["args"] = args;
    def["sources"] = sources;
    def["summary"] = Json{{"sequence", join(names, " -> ")}, {"steps", join(names, " -> ")}, {"class", list[from].cls}, {"count", times}, {"id", id}};
    return def;
  }
  return std::nullopt;
}

// ── a user-defined action used on several classes ───────────────────

std::optional<Json> from_group(const Context& c, const std::string& action) {
  if (preset(c.rt, c.routing) != "propose") return std::nullopt;
  const Json* entry = json::find(c.engine.catalogue(), action);
  if (!entry || json::get_string(entry->value("origin", Json::object()), "kind") != "emergent") return std::nullopt;
  const Json& cfg = emergent_cfg(c.routing);
  const Json groups_cfg = cfg.value("groups", Json::object());
  const std::size_t min_classes = static_cast<std::size_t>(json::get_int(groups_cfg, "min_classes", 2));
  SequenceIndex* index = fresh_index(c);
  if (!index) return std::nullopt;
  const std::vector<std::string> classes = index->classes_of(action);
  if (classes.size() < min_classes) return std::nullopt;
  // What all of those classes have in common (a class id is a trait of its own; they are named only when nothing else is shared).
  std::set<std::string> shared;
  bool first = true;
  for (const auto& cls : classes) {
    std::set<std::string> own = c.engine.expand_traits({cls});
    if (first) shared = std::move(own);
    else {
      std::set<std::string> both;
      std::set_intersection(shared.begin(), shared.end(), own.begin(), own.end(), std::inserter(both, both.begin()));
      shared = std::move(both);
    }
    first = false;
  }
  std::vector<std::string> traits;
  for (const auto& t : shared)
    if (!c.engine.has_class(t)) traits.push_back(t);
  if (traits.empty()) traits.assign(shared.begin(), shared.end());
  if (traits.empty()) return std::nullopt;
  const std::string sig = "group:" + action + ":" + join(traits, ",");
  const std::string node_kind = cfg.value("node", Json::object()).value("kind", "proposal");
  if (auto seen = c.rt.db().find_node(sig, node_kind); seen && *seen) return std::nullopt;
  // A group of exactly these traits already exists (shipped or yours): nothing to offer.
  if (auto profile = RuntimeProfile::load("graph_entry", c.rt.paths().root)) {
    const Json groups = profile->values().value("ontology", Json::object()).value("groups", Json::object());
    for (auto it = groups.begin(); it != groups.end(); ++it) {
      auto have = strings_of(json::find(it.value(), "traits"));
      std::set<std::string> a(have.begin(), have.end()), b(traits.begin(), traits.end());
      if (a == b) return std::nullopt;
    }
  }
  const std::string id = action + json::get_string(groups_cfg, "suffix", "_targets");
  Json label = Json::object();
  const Json texts = c.routing.value("texts", Json::object());
  for (auto it = texts.begin(); it != texts.end(); ++it) {
    const std::string tpl = json::get_string(it.value(), "defined_group_label");
    if (tpl.empty()) continue;
    if (auto rendered = render_profile_template(tpl, Json{{"action", action}})) label[it.key()] = *rendered;
  }
  std::vector<std::string> class_names;
  for (const auto& cls : classes) class_names.push_back(class_label(c.engine, cls, "en"));
  Json args = Json::object();
  args["id"] = id;
  args["traits"] = Json(traits);
  args["label"] = label;
  args["element_match"] = "all";
  Json def = Json::object();
  def["kind"] = "define_group";
  def["writer"] = "groups.define";
  def["id"] = id;
  def["signature"] = sig;
  def["title"] = id;
  def["args"] = args;
  def["sources"] = Json::array();
  for (const auto& message_id : index->messages_of(action)) def["sources"].push_back(message_id);
  def["summary"] = Json{{"action", action}, {"classes", join(class_names, ", ")}, {"traits", join(traits, ", ")}, {"id", id}};
  return def;
}

// ── provenance ──────────────────────────────────────────────────────

Result<std::string> record(Runtime& rt, const Json& routing, const Json& definition, const std::string& conv_id, const std::vector<std::string>& message_ids) {
  const Json node_cfg = emergent_cfg(routing).value("node", Json::object());
  const std::string kind = json::get_string(node_cfg, "kind", "proposal");
  const std::string link = json::get_string(node_cfg, "link", "derived_from");
  NodeOptions o;
  o.tags = Json::array({"proposal", json::get_string(definition, "kind")});
  o.metadata = Json{{"schema", "loom.emergent_proposal/1"},
                    {"kind", json::get_string(definition, "kind")},
                    {"status", "proposed"},
                    {"id", json::get_string(definition, "id")},
                    {"conv_id", conv_id},
                    {"messages", Json(message_ids)},
                    {"definition", definition.value("args", Json::object())},
                    {"proposed", timeutil::utc_now_iso()}};
  o.content = json::get_string(definition, "title");
  const std::string label = json::get_string(definition, "signature", "define:" + json::get_string(definition, "id"));
  std::string node;
  LOOM_TRY_ASSIGN(auto existing, rt.db().find_node(label, kind));
  if (existing) {  // proposed again (an earlier one was left unanswered or dropped): the same node, a new status
    node = existing->id;
    NodePatch patch;
    patch.metadata = o.metadata;
    LOOM_TRY(rt.db().update_node(node, patch));
  } else {
    LOOM_TRY_ASSIGN(node, rt.db().create_node(label, kind, o));
  }
  std::set<std::string> linked;
  for (const auto& id : message_ids) {
    if (id.empty() || !linked.insert(id).second) continue;
    LOOM_TRY_ASSIGN(auto made, rt.db().create_link(node, id, link, 1.0, Json{{"role", "emerged_from"}, {"conv_id", conv_id}}));
    (void)made;
  }
  return node;
}

void settle(Runtime& rt, const std::string& node_id, const std::string& status, const Json& detail) {
  if (node_id.empty()) return;
  auto node = rt.db().get_node(node_id);
  if (!node || !*node) return;
  Json meta = (*node)->metadata.is_object() ? (*node)->metadata : Json::object();
  meta["status"] = status;
  meta[status] = timeutil::utc_now_iso();
  if (!detail.empty()) meta["outcome"] = detail;
  NodePatch patch;
  patch.metadata = meta;
  (void)rt.db().update_node(node_id, patch);
}

}  // namespace loom::actions::emergent
