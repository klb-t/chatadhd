#include "secret_filter.h"

#include <algorithm>
#include <cctype>
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

namespace {

std::string replace_all(std::string text, const std::string& what, const std::string& with, int& count) {
  if (what.empty()) return text;
  for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + with.size())) {
    text.replace(at, what.size(), with);
    ++count;
  }
  return text;
}

}  // namespace

std::string mask(const Json& recipe, const std::string& text_in, const Json& store_values, Json& marks, const std::string& where) {
  std::string text = text_in;
  if (!marks.is_array()) marks = Json::array();
  const Json store = recipe.is_object() ? recipe.value("store", Json::object()) : Json::object();
  const size_t min_len = static_cast<size_t>(store.value("min_length", 8));
  // Store values: longest first, so a value that contains another is masked whole.
  std::vector<std::pair<std::string, std::string>> values;  // key, value
  if (store_values.is_object())
    for (auto it = store_values.begin(); it != store_values.end(); ++it)
      if (it.value().is_string() && it.value().get<std::string>().size() >= min_len) values.emplace_back(it.key(), it.value().get<std::string>());
  std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.second.size() > b.second.size(); });
  for (const auto& [key, value] : values) {
    int n = 0;
    text = replace_all(std::move(text), value, "[redacted:secrets_store:" + key + "]", n);
    if (n) marks.push_back(Json{{"rule", "secrets_store"}, {"key", key}, {"where", where}, {"count", n}});
  }
  if (recipe.is_object() && recipe.contains("patterns"))
    for (const auto& p : recipe.at("patterns")) {
      auto flags = std::regex::ECMAScript;
      if (p.value("flags", "").find('i') != std::string::npos) flags |= std::regex::icase;
      std::regex rx;
      try {
        rx = std::regex(p.value("regex", ""), flags);
      } catch (const std::regex_error&) {
        continue;  // the gate says an unreadable pattern before anything leaves; the view masks with the rest
      }
      const std::string id = p.value("id", "pattern");
      int n = 0;
      for (auto it = std::sregex_iterator(text.begin(), text.end(), rx); it != std::sregex_iterator(); ++it) ++n;
      if (!n) continue;
      text = std::regex_replace(text, rx, "[redacted:" + id + "]");
      marks.push_back(Json{{"rule", id}, {"where", where}, {"count", n}});
    }
  return text;
}

Json mask_json(const Json& recipe, const Json& value, const Json& store_values, Json& marks, const std::string& where) {
  if (value.is_string()) return Json(mask(recipe, value.get<std::string>(), store_values, marks, where));
  if (value.is_array()) {
    Json out = Json::array();
    for (size_t i = 0; i < value.size(); ++i) out.push_back(mask_json(recipe, value[i], store_values, marks, where + "/" + std::to_string(i)));
    return out;
  }
  if (value.is_object()) {
    Json out = Json::object();
    for (auto it = value.begin(); it != value.end(); ++it) out[it.key()] = mask_json(recipe, it.value(), store_values, marks, where + "/" + it.key());
    return out;
  }
  return value;
}

bool credential_header(const std::string& name) {
  std::string n = name;
  std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  for (const char* w : {"authorization", "api-key", "api_key", "apikey", "token", "secret", "cookie", "password", "credential"})
    if (n.find(w) != std::string::npos) return true;
  return false;
}

}  // namespace loom::chat::secrets
