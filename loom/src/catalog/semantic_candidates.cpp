#include "semantic_candidates.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

#include "loom/util/sha256.h"

namespace loom::catalog::internal {
namespace {

Error invalid(std::string detail) {
  return Error(Errc::InvalidArgument, "catalog_semantic_candidates: " + std::move(detail));
}

Error stale(std::string detail) {
  return Error(Errc::Conflict, "catalog_semantic_candidates: " + std::move(detail));
}

Result<std::string> string_field(const Json& object, const char* key) {
  const auto* value = json::find(object, key);
  if (!value || !value->is_string() || value->get_ref<const std::string&>().empty())
    return invalid(std::string(key) + " must be a nonempty string");
  return value->get<std::string>();
}

Result<double> number_field(const Json& object, const char* key) {
  const auto* value = json::find(object, key);
  if (!value || !value->is_number()) return invalid(std::string(key) + " must be a finite number");
  const double number = value->get<double>();
  if (!std::isfinite(number)) return invalid(std::string(key) + " must be a finite number");
  return number;
}

// Scale each vector by its largest absolute component before forming norms.
// This retains even subnormal direction vectors and prevents squaring a large
// finite double from overflowing. Long-double accumulators reduce dot-product
// cancellation without imposing an arbitrary dimension or value ceiling.
Result<std::vector<long double>> scaled_vector(const Json& value, const std::string& name) {
  if (!value.is_array() || value.empty()) return invalid(name + " must be a nonempty numeric vector");
  double scale = 0.0;
  for (const auto& component : value) {
    if (!component.is_number()) return invalid(name + " components must be finite numbers");
    const double number = component.get<double>();
    if (!std::isfinite(number)) return invalid(name + " components must be finite numbers");
    scale = std::max(scale, std::abs(number));
  }
  if (scale == 0.0) return invalid(name + " must have a nonzero norm");
  std::vector<long double> result;
  result.reserve(value.size());
  for (const auto& component : value)
    result.push_back(static_cast<long double>(component.get<double>()) / static_cast<long double>(scale));
  return result;
}

double cosine_of(const std::vector<long double>& query, const std::vector<long double>& record) {
  long double dot = 0.0L, query_norm2 = 0.0L, record_norm2 = 0.0L;
  for (std::size_t i = 0; i < query.size(); ++i) {
    dot += query[i] * record[i];
    query_norm2 += query[i] * query[i];
    record_norm2 += record[i] * record[i];
  }
  const long double cosine = dot / (std::sqrt(query_norm2) * std::sqrt(record_norm2));
  return static_cast<double>(std::clamp(cosine, -1.0L, 1.0L));
}

double retrieval_rank(double cosine, double bias, double weight) {
  const long double linear = static_cast<long double>(bias) + static_cast<long double>(weight) * cosine;
  if (linear >= 0.0L) return static_cast<double>(1.0L / (1.0L + std::exp(-linear)));
  const long double exponential = std::exp(linear);
  return static_cast<double>(exponential / (1.0L + exponential));
}

}  // namespace

Result<SemanticCandidates> evaluate_semantic_candidates(
    const Json& config, const std::string& profile_input_hash,
    const std::vector<CatalogUnit>& units) {
  SemanticCandidates result;
  if (config.is_null()) return result;
  if (!config.is_object()) return invalid("expected an object or null");
  const auto* enabled = json::find(config, "enabled");
  if (!enabled) return result;
  if (!enabled->is_boolean()) return invalid("enabled must be a boolean");
  if (!enabled->get<bool>()) return result;

  LOOM_TRY_ASSIGN(auto schema, string_field(config, "schema"));
  if (schema != "loom.catalog_semantic_candidates/1") return invalid("unsupported schema");
  LOOM_TRY_ASSIGN(auto profile_hash, string_field(config, "profile_input_hash"));
  if (profile_hash != profile_input_hash) return stale("profile_input_hash does not match the active profile");
  LOOM_TRY_ASSIGN(result.channel, string_field(config, "channel"));
  LOOM_TRY_ASSIGN(result.model, string_field(config, "model"));
  LOOM_TRY_ASSIGN(result.method, string_field(config, "method"));
  const auto* policy = json::find(config, "policy");
  if (!policy || !policy->is_object()) return invalid("policy must be an object");
  LOOM_TRY_ASSIGN(result.bias, number_field(*policy, "bias"));
  LOOM_TRY_ASSIGN(result.weight, number_field(*policy, "weight"));
  LOOM_TRY_ASSIGN(result.tau_relevant, number_field(*policy, "tau_relevant"));
  if (result.tau_relevant < 0.0 || result.tau_relevant > 1.0)
    return invalid("tau_relevant must be between zero and one");
  LOOM_TRY_ASSIGN(result.fusion, string_field(*policy, "fusion"));
  if (result.fusion != "additive" && result.fusion != "union")
    return invalid("fusion must be additive or union");
  result.policy = *policy;

  const auto* query_json = json::find(config, "query");
  if (!query_json) return invalid("query is required");
  LOOM_TRY_ASSIGN(auto query, scaled_vector(*query_json, "query"));
  const auto* records = json::find(config, "records");
  if (!records || !records->is_array()) return invalid("records must be an array");

  std::map<std::string, const CatalogUnit*> units_by_id;
  for (const auto& unit : units) {
    if (unit.unit.id.empty()) return invalid("catalog unit ID must be nonempty");
    if (!units_by_id.emplace(unit.unit.id, &unit).second) return invalid("duplicate catalog unit ID");
  }
  result.configuration_hash = Sha256::hex(json::canonical(config));
  const std::string query_hash = Sha256::hex(json::canonical(*query_json));
  std::set<std::string> supplied_ids;
  for (const auto& record : *records) {
    if (!record.is_object()) return invalid("each record must be an object");
    LOOM_TRY_ASSIGN(auto unit_id, string_field(record, "unit_id"));
    if (!supplied_ids.insert(unit_id).second) return invalid("duplicate record unit_id: " + unit_id);
    const auto unit = units_by_id.find(unit_id);
    if (unit == units_by_id.end()) return stale("record references an unknown catalog unit: " + unit_id);
    LOOM_TRY_ASSIGN(auto content_hash, string_field(record, "content_hash"));
    if (content_hash != unit->second->content_hash)
      return stale("content_hash does not match catalog unit: " + unit_id);
    const auto* vector_json = json::find(record, "vector");
    if (!vector_json) return invalid("record vector is required");
    LOOM_TRY_ASSIGN(auto vector, scaled_vector(*vector_json, "record vector"));
    if (vector.size() != query.size()) return invalid("record and query vector dimensions differ");

    SemanticCandidateHit hit;
    hit.cosine = cosine_of(query, vector);
    hit.score = retrieval_rank(hit.cosine, result.bias, result.weight);
    hit.evidence = Json{{"channel", result.channel}, {"model", result.model}, {"method", result.method},
                        {"origin", "supplied_vector"}, {"profile_input_hash", profile_hash},
                        {"unit_id", unit_id}, {"content_hash", content_hash},
                        {"configuration_hash", result.configuration_hash}, {"query_hash", query_hash},
                        {"vector_hash", Sha256::hex(json::canonical(*vector_json))},
                        {"score_kind", "retrieval_rank"}, {"cosine", hit.cosine}, {"score", hit.score}};
    result.hits.emplace(std::move(unit_id), std::move(hit));
  }
  result.enabled = true;
  return result;
}

}  // namespace loom::catalog::internal
