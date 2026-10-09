// OWNER: wave 2 net/chat/worker. Port of engine/semantic_llm.py.
#include "loom/semantic_llm.h"

#include "loom/config.h"
#include "loom/log.h"
#include "loom/net/http.h"
#include "loom/semantic_analyzer.h"
#include "loom/util/sha256.h"
#include "loom/util/utf8.h"
#include "stub.h"

namespace loom {

namespace {

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
  std::lock_guard lk(state_mu_);
  auto settings = settings_locked();
  refresh_identity_locked(settings);
  return !disabled_.load() && settings.active && !settings.model.empty() && !settings.key.empty();
}

SemanticLLM::Settings SemanticLLM::settings_locked() const {
  Json config = cfg_.all();
  const Json* active = json::find(config, "semantic_analysis");
  return Settings{active ? json::truthy(*active) : true,
                  json::get_string(config, "semantic_model"),
                  rstrip_slash(json::get_string(config, "base_url")), secrets_.get_string("api_key")};
}

void SemanticLLM::refresh_identity_locked(const Settings& settings) const {
  Json identity = Json::array({settings.active, settings.model, settings.base, Sha256::hex(settings.key)});
  if (!config_identity_ || *config_identity_ != identity) {
    config_identity_ = std::move(identity);
    ++config_generation_;
    failures_.store(0);
    disabled_.store(false);
  }
}

void SemanticLLM::record_result(std::uint64_t generation, bool success) {
  std::lock_guard lk(state_mu_);
  refresh_identity_locked(settings_locked());
  if (generation != config_generation_ || disabled_.load()) return;
  if (success) {
    failures_.store(0);
  } else {
    int failures = failures_.fetch_add(1) + 1;
    if (failures >= kMaxConsecutiveFailures) {
      disabled_.store(true);
      log::warn("loom.semantic_llm",
                "Semantic LLM disabled after {} consecutive failures. "
                "Check semantic model, endpoint and credentials in Settings.", failures);
    }
  }
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
  Settings settings;
  {
    std::lock_guard lk(state_mu_);
    settings = settings_locked();
  }
  return call_llm(text, settings);
}

Result<std::optional<Json>> SemanticLLM::call_llm(std::string_view text, const Settings& settings) {
  if (settings.key.empty() || settings.base.empty() || settings.model.empty()) return std::optional<Json>(std::nullopt);

  std::string analysis_text(utf8::prefix(text, kMaxAnalysisChars));

  Json body{{"model", settings.model},
            {"messages", Json::array({Json{{"role", "user"}, {"content", std::string(kAnalysisPrompt) + analysis_text}}})},
            {"temperature", 0.1},
            {"max_tokens", 800}};

  net::HttpRequest req;
  req.method = "POST";
  req.url = settings.base + "/chat/completions";
  req.headers = {
      {"Authorization", "Bearer " + settings.key},
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
  return analyse(text, regex_);
}

Json SemanticLLM::analyse(std::string_view text, const SemanticAnalyzer& analyzer) {
  Analysis regex_result = analyzer.analyse(text);

  Settings settings;
  std::uint64_t generation;
  {
    std::lock_guard lk(state_mu_);
    settings = settings_locked();
    refresh_identity_locked(settings);
    if (disabled_.load() || !settings.active || settings.model.empty() || settings.key.empty() || utf8::length(text) < 20)
      return analyzer.to_unified_profile(regex_result);
    generation = config_generation_;
  }

  auto result = call_llm(text, settings);
  if (result && result->has_value()) {
    record_result(generation, true);
    return merge(std::move(**result), regex_result);
  }

  if (!result) {
    log::debug("loom.semantic_llm", "LLM semantic analysis failed — using regex: {}", result.error().message);
  }

  record_result(generation, false);

  return analyzer.to_unified_profile(regex_result);
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
  std::lock_guard lk(state_mu_);
  ++config_generation_;
  failures_.store(0);
  disabled_.store(false);
}

}  // namespace loom
