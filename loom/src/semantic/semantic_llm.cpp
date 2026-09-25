// OWNER: wave 2 net/chat/worker. Port of engine/semantic_llm.py.
#include "loom/semantic_llm.h"

#include "loom/config.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/utf8.h"
#include "stub.h"

namespace loom {

namespace {

std::string cfg_string(const Config& cfg, std::string_view key, std::string fallback = "") {
  Json v = cfg.get(key);
  return v.is_string() ? v.get<std::string>() : std::move(fallback);
}

bool cfg_bool(const Config& cfg, std::string_view key, bool fallback) {
  return json::truthy(cfg.get(key, fallback));
}

std::string rstrip_slash(std::string s) {
  while (!s.empty() && s.back() == '/') s.pop_back();
  return s;
}

}  // namespace

// Exact copy of engine/semantic_llm.py _ANALYSIS_PROMPT (== batch_api's copy).
constexpr std::string_view kAnalysisPromptText = R"PROMPT(Analyse the following chat message. Return ONLY valid JSON (no markdown, no backticks).

{
  "entities": [{"name": "...", "kind": "person|org|place|code|file|concept|product|event", "relevance": 0.0-1.0}],
  "topics": [{"label": "...", "confidence": 0.0-1.0}],
  "relations": [{"subject": "...", "predicate": "mentions|depends_on|references|contradicts|implements|part_of", "object": "..."}],
  "summary": "one line summary",
  "sentiment": "positive|negative|neutral|mixed"
}

Rules:
- Extract ALL named entities (people, orgs, code modules, files, concepts).
- Topics should be domain labels (legal, finance, tech, ai, security, health, project, personal).
- Relations connect entities mentioned in the text.
- Be precise. Fewer high-confidence items > many low-confidence ones.
- If the message is trivial (greetings, "ok", etc.) return empty arrays.

Message:
)PROMPT";
const std::string_view kAnalysisPrompt = kAnalysisPromptText;

SemanticLLM::SemanticLLM(const Config& cfg, const Secrets& secrets, net::HttpTransport& http,
                         const SemanticAnalyzer& regex)
    : cfg_(cfg), secrets_(secrets), http_(http), regex_(regex) {}

bool SemanticLLM::enabled() const {
  if (disabled_.load()) return false;
  if (!cfg_bool(cfg_, "semantic_analysis", true)) return false;
  if (cfg_string(cfg_, "semantic_model").empty()) return false;
  if (!secrets_.has("api_key")) return false;
  return true;
}

std::optional<Json> SemanticLLM::parse_response_json(std::string_view raw) {
  std::string_view s = utf8::strip(raw);
  if (s.size() >= 3 && s.substr(0, 3) == "```") {
    if (auto nl = s.find('\n'); nl != std::string_view::npos) s = s.substr(nl + 1);
  }
  if (s.size() >= 3 && s.substr(s.size() - 3) == "```") {
    if (auto pos = s.rfind("```"); pos != std::string_view::npos) s = s.substr(0, pos);
  }
  s = utf8::strip(s);
  auto parsed = json::parse(s);
  if (!parsed) return std::nullopt;
  return *parsed;
}

Result<std::optional<Json>> SemanticLLM::call_llm(std::string_view text) {
  std::string key = secrets_.get_string("api_key");
  std::string base = rstrip_slash(cfg_string(cfg_, "base_url"));
  std::string model = cfg_string(cfg_, "semantic_model");
  if (key.empty() || base.empty() || model.empty()) return std::optional<Json>(std::nullopt);

  std::string analysis_text(utf8::prefix(text, kMaxAnalysisChars));

  Json body{{"model", model},
            {"messages", Json::array({Json{{"role", "user"}, {"content", std::string(kAnalysisPrompt) + analysis_text}}})},
            {"temperature", 0.1},
            {"max_tokens", 800}};

  net::HttpRequest req;
  req.method = "POST";
  req.url = base + "/chat/completions";
  req.headers = {
      {"Authorization", "Bearer " + key},
      {"Content-Type", "application/json"},
      {"HTTP-Referer", "https://github.com/chatadhd"},
      {"X-Title", "ChatADHD-Semantic"},
  };
  req.body = json::dump(body);
  req.timeout_ms = 30000;

  auto resp = http_.send(req);
  if (!resp) return resp.error();

  if (resp->status != 200) {
    std::string detail;
    if (auto j = resp->json()) {
      const Json* err = json::find(*j, "error");
      detail = json::dump(err ? *err : *j);
    }
    log::warn("loom.semantic_llm", "Semantic LLM error {}: {}", resp->status, std::string(utf8::prefix(detail, 120)));
    return std::optional<Json>(std::nullopt);
  }

  auto j = resp->json();
  if (!j) return std::optional<Json>(std::nullopt);
  std::string raw;
  try {
    raw = j->at("choices").at(0).at("message").at("content").get<std::string>();
  } catch (const std::exception&) {
    return std::optional<Json>(std::nullopt);
  }

  auto parsed = parse_response_json(raw);
  if (!parsed) {
    log::debug("loom.semantic_llm", "Semantic LLM returned invalid JSON: {}", std::string(utf8::prefix(raw, 100)));
    return std::optional<Json>(std::nullopt);
  }
  return std::optional<Json>(std::move(*parsed));
}

Json SemanticLLM::analyse(std::string_view text) {
  Analysis regex_result = regex_.analyse(text);

  if (!enabled() || utf8::length(text) < 20) return convert_regex(regex_result);

  auto result = call_llm(text);
  if (result && result->has_value()) {
    failures_.store(0);
    return merge(std::move(**result), regex_result);
  }

  if (!result) {
    log::debug("loom.semantic_llm", "LLM semantic analysis failed — using regex: {}", result.error().message);
  }

  int failures = failures_.fetch_add(1) + 1;
  if (failures >= kMaxConsecutiveFailures) {
    disabled_.store(true);
    log::warn("loom.semantic_llm",
              "Semantic LLM disabled after {} consecutive failures. Check model ID in Settings.", failures);
  }

  return convert_regex(regex_result);
}

Json SemanticLLM::convert_regex(const Analysis& regex) { return SemanticAnalyzer::to_unified(regex); }

Json SemanticLLM::merge(Json llm, const Analysis& regex) {
  if (!llm.is_object()) llm = Json::object();
  if (!llm.contains("entities") || !llm["entities"].is_array()) llm["entities"] = Json::array();
  if (!llm.contains("topics") || !llm["topics"].is_array()) llm["topics"] = Json::array();
  if (!llm.contains("relations") || !llm["relations"].is_array()) llm["relations"] = Json::array();
  llm["source"] = "llm";

  std::vector<std::string> llm_names;
  llm_names.reserve(llm["entities"].size());
  for (const auto& e : llm["entities"]) llm_names.push_back(utf8::to_lower(json::get_string(e, "name")));

  for (const auto& ent : regex.entities) {
    std::string lname = utf8::to_lower(ent.text);
    bool seen = false;
    for (const auto& n : llm_names) {
      if (n == lname) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      llm["entities"].push_back(Json{{"name", ent.text}, {"kind", ent.entity_type}, {"relevance", ent.confidence * 0.8}});
    }
  }
  return llm;
}

void SemanticLLM::reset_failures() noexcept {
  failures_.store(0);
  disabled_.store(false);
}

}  // namespace loom
