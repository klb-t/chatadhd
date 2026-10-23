// Advisories: the shared shape {code, level, text{pl,en}, params} (+ ask). See include/loom/advisory.h.
#include "loom/advisory.h"

namespace loom::advisory {

std::vector<std::string> Catalogue::codes() const {
  std::vector<std::string> out;
  for (auto it = entries_.begin(); it != entries_.end(); ++it) out.push_back(it.key());
  return out;
}

Json Catalogue::say(const std::string& code, const Json& params) const {
  const Json* entry = json::find(entries_, code);
  const Json* text = entry ? json::find(*entry, "text") : nullptr;
  Json out = Json::object();
  for (const char* lang : {"en", "pl"}) {
    std::string s = code;
    if (text && text->is_object()) s = json::get_string(*text, lang, json::get_string(*text, "en", code));
    if (params.is_object())
      for (auto p = params.begin(); p != params.end(); ++p) {
        const std::string needle = "{" + p.key() + "}";
        const std::string value = p.value().is_string() ? p.value().get<std::string>() : json::dump(p.value());
        for (std::size_t at = s.find(needle); at != std::string::npos; at = s.find(needle, at + value.size())) s.replace(at, needle.size(), value);
      }
    out[lang] = s;
  }
  return out;
}

Json Catalogue::reason(const std::string& code, const std::string& level, const Json& params) const {
  const Json* entry = json::find(entries_, code);
  std::string lvl = level;
  if (lvl.empty()) lvl = entry ? json::get_string(*entry, "level", "info") : "info";
  Json out{{"code", code}, {"level", lvl}, {"text", say(code, params)}, {"params", params}};
  if (entry) {
    if (const Json* q = json::find(*entry, "question"); q && q->is_object())
      out["ask"] = Json{{"key", "advisory." + code + ".ask_every"}, {"question", *q}};
  }
  return out;
}

void Collector::add(const std::string& code, const Json& params, const std::string& level) {
  Json r = catalogue_ ? catalogue_->reason(code, level, params)
                      : Json{{"code", code}, {"level", level.empty() ? "info" : level}, {"text", Json{{"en", code}, {"pl", code}}}, {"params", params}};
  for (const auto& have : items_)
    if (json::get_string(have, "code") == code && have.value("params", Json::object()) == params) return;
  items_.push_back(std::move(r));
}

void Collector::append(const std::vector<Json>& items) {
  for (const auto& i : items) {
    bool dup = false;
    for (const auto& have : items_)
      if (have == i) dup = true;
    if (!dup) items_.push_back(i);
  }
}

std::size_t Collector::count(const std::string& code) const {
  std::size_t n = 0;
  for (const auto& i : items_) n += json::get_string(i, "code") == code;
  return n;
}

}  // namespace loom::advisory
