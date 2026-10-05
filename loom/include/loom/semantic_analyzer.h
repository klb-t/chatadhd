// loom/semantic_analyzer.h — port of core/semantic.py.   [OWNER: wave 2 semantic]
//
// Level-1 (regex NER + keyword topics + heuristic relations), zero external
// dependencies. The rules (patterns, topic keywords, relation patterns) are
// policy data: AnalyzerRules::builtin() reproduces the Python module exactly
// and can be replaced/extended from JSON without code changes.
//
// Python parity requirements (differential tests in tests/compat):
//   extract_entities: run every entity pattern in order with finditer; value =
//     group(1) if the match has a capturing group that participated
//     (m.lastindex) else group(0); strip(); dedupe on (type, value.lower())
//     across all patterns; start/end are the WHOLE match's code-point offsets
//     (m.start()/m.end()), confidence 1.0.
//   extract_topics(text, threshold=2): text.lower(); for each topic in rule
//     order count keywords whose kw.lower() is a substring; include when
//     hits >= threshold.
//   extract_relations: the depends_on pattern (confidence 0.6) then the
//     references pattern (0.5); subject=group(1).strip(), obj=group(2).strip(),
//     source_text=group(0).
//   analyse(): {entities, topics, relations, timestamp=utcnow iso + "Z"}.
//
// Entity/relation types are strings (capability-based, not enum-based); the
// constants below are the Python enum values.
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {

class RuntimeProfile;

namespace entity_type {  // core/semantic.py EntityType values
inline constexpr std::string_view kEmail = "email";
inline constexpr std::string_view kUrl = "url";
inline constexpr std::string_view kDate = "date";
inline constexpr std::string_view kTime = "time";
inline constexpr std::string_view kMoney = "money";
inline constexpr std::string_view kPhone = "phone";
inline constexpr std::string_view kHashtag = "hashtag";
inline constexpr std::string_view kMention = "mention";
inline constexpr std::string_view kFilePath = "file_path";
inline constexpr std::string_view kIpAddress = "ip_address";
inline constexpr std::string_view kCodeRef = "code_ref";
inline constexpr std::string_view kPerson = "person";
inline constexpr std::string_view kOrganisation = "organisation";
inline constexpr std::string_view kUnknown = "unknown";
}  // namespace entity_type

namespace relation_type {  // core/semantic.py RelationType values
inline constexpr std::string_view kMentions = "mentions";
inline constexpr std::string_view kDependsOn = "depends_on";
inline constexpr std::string_view kRelatedTo = "related_to";
inline constexpr std::string_view kCreatedBy = "created_by";
inline constexpr std::string_view kPartOf = "part_of";
inline constexpr std::string_view kReferences = "references";
}  // namespace relation_type

struct ExtractedEntity {
  std::string text;
  std::string entity_type;
  double confidence = 1.0;
  std::size_t start = 0;  // code points
  std::size_t end = 0;
  Json metadata = Json::object();
  Json to_json() const;  // {"text","entity_type","confidence","start","end","metadata"}
};

struct ExtractedRelation {
  std::string subject;
  std::string predicate;
  std::string obj;
  double confidence = 0.5;
  std::string source_text;
  Json to_json() const;  // {"subject","predicate","obj","confidence","source_text"}
};

struct Analysis {
  std::vector<ExtractedEntity> entities;
  std::vector<std::string> topics;
  std::vector<ExtractedRelation> relations;
  std::string timestamp;
  Json to_json() const;
};

// Rule set (policy data). Flags use loom::re::Flag bits.
struct EntityPattern {
  std::string entity_type;
  std::string pattern;
  unsigned flags = 0;
  std::optional<double> confidence;
};
struct RelationPattern {
  std::string predicate;
  std::string pattern;
  unsigned flags = 0;
  double confidence = 0.5;
};
struct AnalyzerRules {
  std::vector<EntityPattern> entity_patterns;                             // in evaluation order
  std::vector<std::pair<std::string, std::vector<std::string>>> topics;  // in evaluation order
  std::vector<RelationPattern> relation_patterns;                        // in evaluation order
  // Resolved profile parameters are separate from the legacy rules JSON.
  Json profile_parameters = Json::object();
  std::string profile_hash;

  // Exact copy of _PATTERNS, _TOPIC_KEYWORDS and the two relation patterns.
  static const AnalyzerRules& builtin();
  static Result<AnalyzerRules> from_json(const Json& j);
  static Result<AnalyzerRules> from_profile(const RuntimeProfile& profile);
  Json to_json() const;
};

class SemanticAnalyzer {
 public:
  // Compiles every pattern; Errc::Parse if one does not compile.
  static Result<std::unique_ptr<SemanticAnalyzer>> create(const AnalyzerRules& rules = AnalyzerRules::builtin());
  static Result<std::unique_ptr<SemanticAnalyzer>> create_with_profile(const RuntimeProfile& profile);
  static Result<std::unique_ptr<SemanticAnalyzer>> create_from_data_dir(const std::filesystem::path& data_dir,
                                                                      const Json& overrides = Json::object());
  ~SemanticAnalyzer();
  SemanticAnalyzer(const SemanticAnalyzer&) = delete;
  SemanticAnalyzer& operator=(const SemanticAnalyzer&) = delete;

  // All methods are const and thread-safe.
  std::vector<ExtractedEntity> extract_entities(std::string_view text) const;
  std::vector<std::string> extract_topics(std::string_view text) const;
  std::vector<std::string> extract_topics(std::string_view text, int threshold) const;
  std::vector<ExtractedRelation> extract_relations(std::string_view text) const;
  Analysis analyse(std::string_view text) const;

  // The "unified" analysis dict used by SemanticLLM._convert_regex,
  // GraphEngine and SemanticWorker._regex_analyse:
  //   {"entities":[{"name","kind","relevance"}], "topics":[{"label","confidence":0.5}],
  //    "relations":[{"subject","predicate","object"}], "summary":"",
  //    "sentiment":"neutral", "source":"regex"}
  static Json to_unified(const Analysis& a);
  Json to_unified_profile(const Analysis& a) const;
  const std::string& profile_hash() const noexcept;

  const AnalyzerRules& rules() const noexcept;

  struct Impl;  // defined in src/semantic/ only
  // Use create(); public only so std::make_unique can reach it.
  explicit SemanticAnalyzer(std::unique_ptr<Impl> impl);

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace loom
