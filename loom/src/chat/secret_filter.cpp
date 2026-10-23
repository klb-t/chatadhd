#include "secret_filter.h"

#include <regex>
#include <set>

namespace loom::chat::secrets {
namespace {

Json label_of(const Json& v) { return v.is_object() ? v : Json::object(); }

}  // namespace

Result<Json> gate(const Json& recipe, const std::string& mode, const std::vector<Source>& sources, const Json& store_values, bool confirmed) {
  const Json& modes = recipe.at("modes");
  // A value of the setting the recipe does not know is read as "ask": nothing leaves unseen, nothing is refused for good.
  const std::string action = modes.contains(mode) ? modes.at(mode).value("action", "hold") : (modes.contains("ask") ? modes.at("ask").value("action", "hold") : "hold");
  if (action == "pass") return Json(nullptr);
  if (action == "hold" && confirmed) return Json(nullptr);

  const Json& texts = recipe.at("texts");
  std::vector<std::pair<Json, std::regex>> compiled;
  for (const auto& p : recipe.at("patterns")) {
    auto flags = std::regex::ECMAScript;
    if (p.value("flags", "").find('i') != std::string::npos) flags |= std::regex::icase;
    try {
      compiled.emplace_back(p, std::regex(p.at("regex").get<std::string>(), flags));
    } catch (const std::regex_error& e) {
      // Said, not skipped: a pattern that cannot be read would otherwise let the secret it was written for go out unseen.
      return Error(Errc::InvalidArgument, "chat_secrets: the pattern '" + p.value("id", "?") + "' is not a valid regular expression: " + e.what());
    }
  }
  const Json& store = recipe.at("store");
  const size_t min_len = static_cast<size_t>(store.value("min_length", 8));
  std::set<std::string> skip;
  for (const auto& k : store.value("skip_keys", Json::array())) skip.insert(k.get<std::string>());

  Json hits = Json::array();
  for (const auto& s : sources) {
    const std::string place_key = s.where == "file" ? "where_file" : "where_text";
    auto add = [&](Json h) {
      h["where"] = s.where;
      h["place"] = label_of(texts.value(place_key, Json::object()));
      if (!s.file.empty()) h["file"] = s.file;
      hits.push_back(std::move(h));
    };
    for (const auto& [p, rx] : compiled)
      if (std::regex_search(s.text, rx)) add(Json{{"rule", p.at("id")}, {"label", label_of(p.value("label", Json::object()))}});
    if (store.value("scan", true) && store_values.is_object()) {
      for (auto it = store_values.begin(); it != store_values.end(); ++it) {
        if (!it.value().is_string() || skip.count(it.key())) continue;
        const std::string v = it.value().get<std::string>();
        if (v.size() >= min_len && s.text.find(v) != std::string::npos)
          add(Json{{"rule", "secrets_store"}, {"label", label_of(texts.value("where_store", Json::object()))}, {"key", it.key()}});
      }
    }
  }
  if (hits.empty()) return Json(nullptr);

  Json out{{"schema", "loom.chat_secret_pending/1"}, {"mode", mode}, {"action", action}, {"hits", hits}};
  if (action == "refuse") {
    out["status"] = "refused";
    out["text"] = label_of(texts.value("refused", Json::object()));
  } else {
    out["status"] = "needs_confirmation";
    out["text"] = label_of(texts.value("ask", Json::object()));
    out["actions"] = Json{{"send_anyway", label_of(texts.value("send_anyway", Json::object()))}, {"dont_send", label_of(texts.value("dont_send", Json::object()))}};
    out["resend"] = "repeat the same request with secret_confirmation {approved: true}";
  }
  return out;
}

}  // namespace loom::chat::secrets
