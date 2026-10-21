#include "profile_prefix.h"

#include <algorithm>
#include <tuple>

#include "loom/db.h"
#include "loom/graph_memory.h"
#include "loom/log.h"
#include "loom/onboarding_layers.h"
#include "loom/onboarding_store.h"

namespace loom::chat::prefix {

namespace {

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
  for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
  return s;
}

// A value as a line of text; "" = nothing to say (null, empty text, empty list).
std::string value_text(const Json& v) {
  if (v.is_null()) return "";
  if (v.is_string()) return v.get<std::string>();
  if (v.is_array()) {
    std::string out;
    for (const auto& e : v) {
      const std::string one = value_text(e);
      if (one.empty()) continue;
      if (!out.empty()) out += ", ";
      out += one;
    }
    return out;
  }
  return json::dump(v);
}

const Json& shipped_subgraphs() {
  static const Json table = []() -> Json {
    auto pack = onboarding::builtin_pack();
    if (!pack) return Json::array();
    for (const auto& e : pack->value("entries", Json::array()))
      if (e.value("key", "") == "profile.subgraphs" && e.contains("value")) return e["value"].value("subgraphs", Json::array());
    return Json::array();
  }();
  return table;
}

}  // namespace

Rendered render(const Json& recipe, const Json& subgraphs, const std::string& level, const std::vector<Fact>& facts) {
  Rendered out;
  const Json regions = recipe.value("regions", Json::object());
  out.level = regions.contains(level) ? level : json::get_string(recipe, "default_level", "normal");
  if (!regions.contains(out.level)) return out;
  const Json& region = regions.at(out.level);
  // Which parts and which keys this level carries (null = all).
  const Json& want_sg = region.contains("subgraphs") ? region.at("subgraphs") : Json();
  const Json& want_keys = region.contains("keys") ? region.at("keys") : Json();
  auto in_list = [](const Json& list, const std::string& id) {
    if (!list.is_array()) return true;
    for (const auto& e : list) if (e.is_string() && e.get<std::string>() == id) return true;
    return false;
  };
  struct Part { long long order; long long rank; std::string id; std::string label; };
  std::vector<Part> parts;
  if (subgraphs.is_array())
    for (const auto& s : subgraphs) {
      if (!s.is_object() || !s.contains("order_for_prefix") || !s["order_for_prefix"].is_number_integer()) continue;  // null = not part of the prefix
      const std::string id = json::get_string(s, "id");
      if (id.empty() || !in_list(want_sg, id)) continue;
      const Json label = s.value("label", Json::object());
      parts.push_back({s["order_for_prefix"].get<long long>(), s.value("stability_rank", 0LL), id, json::get_string(label, "en", id)});
    }
  std::sort(parts.begin(), parts.end(), [](const Part& a, const Part& b) {
    return std::tie(a.order, a.rank, a.id) < std::tie(b.order, b.rank, b.id);
  });
  const std::string sg_line = json::get_string(recipe, "subgraph_line", "[{label}]");
  const std::string item_line = json::get_string(recipe, "item_line", "- {key}: {value}");
  const Json keys_of = recipe.value("subgraph_keys", Json::object());
  std::string body;
  for (const auto& part : parts) {
    if (!keys_of.contains(part.id) || !keys_of.at(part.id).is_array()) continue;
    std::string lines;
    for (const auto& k : keys_of.at(part.id)) {
      if (!k.is_string()) continue;
      const std::string key = k.get<std::string>();
      if (!in_list(want_keys, key)) continue;
      for (const auto& f : facts) {
        if (f.subgraph != part.id || f.key != key) continue;
        const std::string text = value_text(f.value);
        if (text.empty()) break;
        const std::string line = replace_all(replace_all(item_line, "{key}", key), "{value}", text);
        lines += line + "\n";
        out.items.push_back(Json{{"ref", key}, {"label", key}, {"subgraph", part.id}, {"tokens_est", std::max(1, ContextSelector::estimate_tokens(line))}});
        break;
      }
    }
    if (!lines.empty()) body += replace_all(sg_line, "{label}", part.label) + "\n" + lines;
  }
  if (body.empty()) return out;
  if (body.back() == '\n') body.pop_back();
  out.text = json::get_string(recipe, "header") + "\n" + body;
  return out;
}

Gathered gather(Database& db, const Json& recipe) {
  Gathered g;
  g.subgraphs = shipped_subgraphs();
  std::string user;
  {
    auto lock = db.lock();
    auto& conn = db.conn();
    if (!conn.has_table("loom_onboarding_profiles")) { g.unavailable = "no_profile"; return g; }
    auto st = conn.prepare("SELECT user_id FROM loom_onboarding_profiles ORDER BY user_id LIMIT 2");
    if (!st) { g.unavailable = "unreadable"; return g; }
    std::vector<std::string> ids;
    while (true) {
      auto more = st->step();
      if (!more) { g.unavailable = "unreadable"; return g; }
      if (!*more) break;
      ids.push_back(st->get_text(0));
    }
    if (ids.empty()) { g.unavailable = "no_profile"; return g; }
    if (ids.size() > 1) { g.unavailable = "several_profiles"; return g; }
    user = ids[0];
  }
  onboarding::OnboardingStore store(db);
  auto snap = store.read(user);
  if (!snap || !snap->contains("pack") || !snap->contains("layers")) {
    log::debug("loom.chat_engine", "profile prefix: the stored profile is not readable");
    g.unavailable = "unreadable";
    return g;
  }
  auto layers = onboarding::DefaultLayers::create(snap->at("pack"), snap->at("layers"));
  if (!layers) {
    log::debug("loom.chat_engine", "profile prefix: the onboarding layers are not readable: {}", layers.error().message);
    g.unavailable = "unreadable";
    return g;
  }
  if (auto r = layers->resolve("profile.subgraphs"); r && r->value("status", "") == "effective" && r->value("value", Json()).is_object())
    if (const Json table = r->at("value").value("subgraphs", Json()); table.is_array()) g.subgraphs = table;
  const Json keys_of = recipe.value("subgraph_keys", Json::object());
  for (auto it = keys_of.begin(); it != keys_of.end(); ++it) {
    if (!it.value().is_array()) continue;
    for (const auto& k : it.value()) {
      if (!k.is_string()) continue;
      auto r = layers->resolve(k.get<std::string>());
      // Only what the person set themselves: the layer "user". A preset of the pack or a proposal is not their word.
      if (r && r->value("status", "") == "effective" && r->value("layer", "") == "user" && r->contains("value"))
        g.facts.push_back({it.key(), k.get<std::string>(), r->at("value")});
    }
  }
  return g;
}

}  // namespace loom::chat::prefix
