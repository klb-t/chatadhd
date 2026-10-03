#include "loom/context_plan.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>

#include "ctx_common.h"

namespace loom::context {
namespace {

Status invalid(const std::string& field, const std::string& reason) {
  return Error(Errc::InvalidArgument, "context plan " + field + ": " + reason);
}

bool nonblank(const Json& value) {
  return value.is_string() && value.get_ref<const std::string&>().find_first_not_of(" \t\r\n") != std::string::npos;
}

Status keys(const Json& object, const std::set<std::string>& allowed, const std::string& field) {
  for (const auto& [key, value] : object.items()) {
    (void)value;
    if (!allowed.count(key)) return invalid(field + "." + key, "unknown field");
  }
  return {};
}

Status ids(const Json& object, const std::string& key, const std::string& field) {
  if (!object.contains(key)) return {};
  const auto& value = object[key];
  if (!value.is_array()) return invalid(field + "." + key, "must be an array of nonempty ids");
  std::set<std::string> seen;
  for (const auto& id : value) {
    if (!nonblank(id)) return invalid(field + "." + key, "ids must be nonempty strings");
    if (!seen.insert(id.get<std::string>()).second) return invalid(field + "." + key, "duplicate id");
  }
  return {};
}

Json gap(const std::string& reason, const std::string& ref = "") {
  Json value{{"reason", reason}};
  if (!ref.empty()) value["ref"] = ref;
  return value;
}

void selection_gaps(const Json& params, Json& gaps) {
  if (const auto* diagnostics = json::find(params, "retrieval_diagnostics"); diagnostics && diagnostics->is_object()) {
    if (diagnostics->value("status", "") == "incomplete") {
      gaps.push_back(Json{{"reason", "retrieval_query_or_cap_incomplete"},
          {"query_errors", diagnostics->value("query_errors", 0)}, {"cap_hits", diagnostics->value("cap_hits", 0)}});
    }
  }
  const auto* retrieval = json::find(params, "candidate_retrieval");
  if (!retrieval || !retrieval->is_object()) return;
  if (retrieval->value("corpus_status", "ok") != "ok") gaps.push_back(gap("candidate_corpus_query_error"));
  if (retrieval->value("possibly_truncated", false)) gaps.push_back(gap("candidate_corpus_may_be_truncated"));
  if (retrieval->value("entity_label_query_errors", 0) > 0) gaps.push_back(gap("candidate_label_query_errors"));
  if (const auto* channels = json::find(*retrieval, "channels"); channels && channels->is_array()) {
    for (const auto& channel : *channels) {
      const auto id = json::get_string(channel, "id");
      const auto status = json::get_string(channel, "status");
      if (status != "ok") {
        gaps.push_back(Json{{"reason", "candidate_channel_" + status}, {"channel", id},
            {"detail", json::get_string(channel, "reason")}});
      }
      if (channel.value("truncated", false)) gaps.push_back(Json{{"reason", "candidate_result_limit"}, {"channel", id}});
      if (const auto* invalid = json::find(channel, "invalid_hits"); invalid && !invalid->empty()) {
        gaps.push_back(Json{{"reason", "candidate_invalid_results"}, {"channel", id}, {"count", invalid->size()}});
      }
    }
  }
  if (const auto* shadow = json::find(*retrieval, "lexical_shadow"); shadow && shadow->is_object()) {
    const auto status = json::get_string(*shadow, "status");
    if (status != "ok") gaps.push_back(Json{{"reason", "lexical_shadow_" + status}, {"detail", json::get_string(*shadow, "reason")}});
  }
}

std::string representation_key(const model::ContextItem& item) {
  // Different resolutions and incomplete dependency projections must survive
  // independently even if they happen to render the same bytes.
  return json::dump(Json{{"ref_kind", std::string(model::to_string(item.ref_kind))},
      {"ref", item.ref}, {"band", std::string(model::to_string(item.band))},
      {"resolution", std::string(model::to_string(item.resolution))},
      {"text", item.text}, {"tokens", item.tokens}, {"missing_premises", item.missing_premises}});
}

void membership(model::ContextItem& item, const std::string& thesis_id) {
  const Json original = item.factors;
  if (!item.factors.is_object()) item.factors = Json::object();
  item.factors["thesis_ids"] = Json::array({thesis_id});
  item.factors["thesis_selection"] = Json{{thesis_id, Json{{"factors", original}, {"score", item.score},
      {"why", item.why}, {"required_by", item.required_by}, {"missing_premises", item.missing_premises}}}};
}

void merge_membership(model::ContextItem& existing, const model::ContextItem& item, const std::string& thesis_id) {
  existing.factors["thesis_ids"].push_back(thesis_id);
  existing.factors["thesis_selection"][thesis_id] = item.factors["thesis_selection"][thesis_id];
  existing.score = std::max(existing.score, item.score);
  for (const auto& ref : item.required_by) {
    if (std::find(existing.required_by.begin(), existing.required_by.end(), ref) == existing.required_by.end()) {
      existing.required_by.push_back(ref);
    }
  }
  std::sort(existing.required_by.begin(), existing.required_by.end());
}

bool item_order(const model::ContextItem& a, const model::ContextItem& b) {
  if (a.band != b.band) return a.band < b.band;
  if (a.score != b.score) return a.score > b.score;
  if (a.ref != b.ref) return a.ref < b.ref;
  if (a.resolution != b.resolution) return a.resolution < b.resolution;
  return representation_key(a) < representation_key(b);
}

}  // namespace

Status validate_context_plan(const Json& plan) {
  if (plan.is_null()) return {};
  if (!plan.is_object()) return invalid("", "must be an object or null");
  LOOM_TRY(keys(plan, {"id", "source_ref", "theses"}, ""));
  if (!plan.contains("id") || !nonblank(plan["id"])) return invalid("id", "must be a nonempty string");
  if (!plan.contains("theses") || !plan["theses"].is_array() || plan["theses"].empty()) {
    return invalid("theses", "must be a nonempty array");
  }
  std::set<std::string> seen;
  for (const auto& thesis : plan["theses"]) {
    if (!thesis.is_object()) return invalid("thesis", "must be an object");
    LOOM_TRY(keys(thesis, {"id", "text", "targets", "claims", "relation_hops", "detail_resolution",
                          "require_counter_evidence", "budget_weight"}, "thesis"));
    for (const auto& key : {"id", "text"}) {
      if (!thesis.contains(key) || !nonblank(thesis[key])) return invalid(std::string("thesis.") + key, "must be a nonempty string");
    }
    const auto id = thesis["id"].get<std::string>();
    if (!seen.insert(id).second) return invalid("thesis.id", "duplicate id " + id);
    LOOM_TRY(ids(thesis, "targets", id));
    LOOM_TRY(ids(thesis, "claims", id));
    if (thesis.contains("relation_hops")) {
      const auto& hops = thesis["relation_hops"];
      if (!hops.is_number_integer() || (hops.is_number_unsigned()
              ? hops.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
              : hops.get<std::int64_t>() < 0 || hops.get<std::int64_t>() > std::numeric_limits<int>::max())) {
        return invalid(id + ".relation_hops", "must be a non-negative supported integer");
      }
    }
    if (thesis.contains("detail_resolution") && !thesis["detail_resolution"].is_null()) {
      const auto& detail = thesis["detail_resolution"];
      if (!detail.is_string() || !model::from_string<model::Resolution>(detail.get<std::string>())) {
        return invalid(id + ".detail_resolution", "must be label, summary, full, raw or null");
      }
    }
    if (thesis.contains("require_counter_evidence") && !thesis["require_counter_evidence"].is_boolean()) {
      return invalid(id + ".require_counter_evidence", "must be boolean");
    }
    if (thesis.contains("budget_weight")) {
      const auto& weight = thesis["budget_weight"];
      if (!weight.is_number() || !std::isfinite(weight.get<double>()) || weight.get<double>() <= 0) {
        return invalid(id + ".budget_weight", "must be a finite positive number");
      }
    }
  }
  return {};
}

Result<model::ContextSet> select_context_plan(ContextEngine& engine, kb::KnowledgeStore& store,
                                            const ContextRequest& req) {
  LOOM_TRY(validate_context_plan(req.plan));
  if (req.plan.is_null()) return Error(Errc::InvalidArgument, "plan selection requires a non-null plan");
  LOOM_TRY_ASSIGN(auto run, ctx::resolve_run(store, req.run));
  ContextRequest base = req;
  base.plan = nullptr;
  base.run = run;  // All thesis projections use the same resolved snapshot.
  LOOM_TRY_ASSIGN(auto goal, engine.type_goal(base));
  model::ContextSet result;
  result.goal = std::move(goal);
  result.budget_tokens = req.budget_tokens > 0 ? req.budget_tokens : 4000;
  const auto& theses = req.plan["theses"];
  long double max_weight = 1;
  for (const auto& thesis : theses) max_weight = std::max(max_weight, static_cast<long double>(thesis.value("budget_weight", 1.0)));
  long double total_weight = 0;
  for (const auto& thesis : theses) total_weight += static_cast<long double>(thesis.value("budget_weight", 1.0)) / max_weight;
  long double cumulative_weight = 0;
  int previous_boundary = 0, carry = 0;
  std::map<std::string, std::size_t> representations;
  Json traces = Json::array();
  std::size_t position = 0;

  for (const auto& thesis : theses) {
    const auto thesis_id = thesis["id"].get<std::string>();
    ContextRequest sub = base;
    sub.text = thesis["text"].get<std::string>();
    if (thesis.contains("targets")) sub.targets = thesis["targets"].get<std::vector<std::string>>();
    if (thesis.contains("claims")) sub.claim_targets = thesis["claims"].get<std::vector<std::string>>();
    if (thesis.contains("relation_hops")) sub.relation_hops = thesis["relation_hops"].get<int>();
    if (thesis.contains("detail_resolution") && !thesis["detail_resolution"].is_null()) {
      sub.detail_resolution = model::from_string<model::Resolution>(thesis["detail_resolution"].get<std::string>());
    }
    sub.include_counter_evidence = thesis.value("require_counter_evidence", true);
    cumulative_weight += static_cast<long double>(thesis.value("budget_weight", 1.0)) / max_weight;
    const int boundary = ++position == theses.size() ? result.budget_tokens :
        std::clamp(static_cast<int>(std::floor(result.budget_tokens * cumulative_weight / total_weight)),
                   previous_boundary, result.budget_tokens);
    const int base_allocation = boundary - previous_boundary;
    const int allocation = base_allocation + carry;
    previous_boundary = boundary;
    Json gaps = Json::array();
    Json trace{{"id", thesis_id}, {"text", sub.text}, {"targets", sub.targets}, {"claims", sub.claim_targets},
        {"relation_hops", sub.relation_hops},
        {"detail_resolution", sub.detail_resolution ? Json(std::string(model::to_string(*sub.detail_resolution))) : Json(nullptr)},
        {"require_counter_evidence", sub.include_counter_evidence}, {"budget_weight", thesis.value("budget_weight", 1.0)},
        {"base_budget_tokens", base_allocation}, {"carried_budget_tokens", carry}, {"allocated_budget_tokens", allocation},
        {"selected_refs", Json::array()}, {"dropped", Json::array()}, {"selection_params", Json::object()}};
    int added_tokens = 0;
    int direct_candidates = 0;
    if (allocation == 0) {
      gaps.push_back(gap("zero_budget_allocation"));
      for (const auto& ref : sub.claim_targets) gaps.push_back(gap("explicit_claim_not_evaluated_zero_budget", ref));
      if (sub.include_counter_evidence) gaps.push_back(gap("counter_evidence_not_evaluated_zero_budget"));
      trace["selection_status"] = "not_evaluated";
      trace["selected_tokens_before_deduplication"] = 0;
    } else {
      sub.budget_tokens = allocation;
      LOOM_TRY_ASSIGN(auto part, engine.select(sub));
      if (result.pack_hash.empty()) result.pack_hash = part.pack_hash;
      trace["selection_status"] = "evaluated";
      trace["selected_tokens_before_deduplication"] = part.used_tokens;
      trace["selection_params"] = part.goal.params;
      selection_gaps(part.goal.params, gaps);
      std::set<std::string> selected_claims;
      for (const auto& item : part.items) {
        trace["selected_refs"].push_back(Json{{"kind", std::string(model::to_string(item.ref_kind))},
            {"id", item.ref}, {"resolution", std::string(model::to_string(item.resolution))}});
        if (item.ref_kind == model::RefKind::Claim || item.ref_kind == model::RefKind::Decision) {
          selected_claims.insert(item.ref);
          const bool is_explicit = std::find(sub.claim_targets.begin(), sub.claim_targets.end(), item.ref) != sub.claim_targets.end();
          const bool direct_goal = item.factors.is_object() && item.factors.value("direct_goal_candidate", false);
          const bool counter_only = item.factors.is_object() && item.factors.contains("counter_for") && !item.factors["counter_for"].empty()
              && !direct_goal;
          if (is_explicit || (sub.claim_targets.empty() && (direct_goal || item.required_by.empty()) && !counter_only)) ++direct_candidates;
        }
        for (const auto& ref : item.missing_premises) {
          auto missing = gap("missing_premise", ref);
          missing["required_by"] = item.ref;
          gaps.push_back(std::move(missing));
        }
      }
      for (const auto& ref : sub.claim_targets) {
        if (selected_claims.count(ref)) continue;
        bool reported = false;
        if (part.goal.params.contains("claim_selection") && part.goal.params["claim_selection"].is_array()) {
          for (const auto& row : part.goal.params["claim_selection"]) {
            if (!row.is_object() || row.value("ref", "") != ref) continue;
            const auto status = row.value("status", "unknown");
            const std::map<std::string, std::string> reasons{{"missing", "explicit_claim_missing"},
                {"filtered", "explicit_claim_filtered"}, {"over_budget", "explicit_claim_over_budget"},
                {"query_error", "explicit_claim_lookup_error"}};
            const auto reason = reasons.find(status);
            if (reason == reasons.end()) continue;
            auto omitted = gap(reason->second, ref);
            omitted["detail"] = row;
            gaps.push_back(std::move(omitted));
            reported = true;
            break;
          }
        }
        if (reported) continue;
        const auto dropped = std::find_if(part.dropped.begin(), part.dropped.end(), [&](const auto& item) {
          return item.ref_kind == model::RefKind::Claim && item.ref == ref;
        });
        if (dropped != part.dropped.end()) {
          auto omitted = gap("explicit_claim_dropped", ref);
          omitted["detail"] = dropped->why;
          gaps.push_back(std::move(omitted));
        } else {
          auto claim = store.get_claim(run, ref);
          gaps.push_back(gap(!claim ? "explicit_claim_lookup_error" : !*claim ? "explicit_claim_missing" : "explicit_claim_not_selected", ref));
        }
      }
      if (direct_candidates == 0) gaps.push_back(gap("no_direct_support_candidates"));
      if (sub.include_counter_evidence) {
        if (part.goal.params.contains("counter_evidence")) {
          trace["counter_evidence"] = part.goal.params["counter_evidence"];
          const auto& counters = trace["counter_evidence"];
          if (counters.is_object() && counters.contains("gaps") && counters["gaps"].is_array()) {
            for (const auto& counter_gap : counters["gaps"]) gaps.push_back(Json{{"reason", "counter_evidence_gap"}, {"detail", counter_gap}});
          }
          if (counters.is_object() && counters.contains("complete") && counters["complete"] == false) {
            gaps.push_back(gap("counter_evidence_incomplete"));
          }
          if (counters.is_object() && counters.contains("recorded_links_complete") && counters["recorded_links_complete"] == false) {
            gaps.push_back(gap("recorded_counter_links_incomplete"));
          }
          if (counters.is_object() && counters.contains("entries") && counters["entries"].is_array()) {
            for (const auto& entry : counters["entries"]) {
              if (!entry.is_object()) continue;
              if (entry.contains("gaps") && entry["gaps"].is_array()) {
                for (const auto& counter_gap : entry["gaps"]) {
                  gaps.push_back(Json{{"reason", "counter_evidence_gap"}, {"for_claim", entry.value("for_claim", "")},
                      {"detail", counter_gap}});
                }
              }
              for (const auto& kind : {"claims", "observations"}) {
                if (!entry.contains(kind) || !entry[kind].is_array()) continue;
                for (const auto& counter : entry[kind]) {
                  if (counter.is_object() && counter.value("status", "unknown") != "selected") {
                    gaps.push_back(Json{{"reason", "counter_evidence_not_selected"}, {"kind", kind},
                        {"for_claim", entry.value("for_claim", "")}, {"detail", counter}});
                  }
                }
              }
            }
          }
        } else {
          gaps.push_back(gap("counter_evidence_coverage_unreported"));
        }
      }
      for (auto& item : part.items) {
        const auto key = representation_key(item);
        membership(item, thesis_id);
        const auto existing = representations.find(key);
        if (existing == representations.end()) {
          added_tokens += item.tokens;
          representations.emplace(key, result.items.size());
          result.items.push_back(std::move(item));
        } else {
          merge_membership(result.items[existing->second], item, thesis_id);
        }
      }
      for (auto& item : part.dropped) {
        trace["dropped"].push_back(item.to_json());
        membership(item, thesis_id);
        result.dropped.push_back(std::move(item));
      }
      if (!part.dropped.empty()) gaps.push_back(gap("selection_omissions"));
    }
    carry = allocation - added_tokens;
    result.used_tokens += added_tokens;
    trace["direct_material_candidates"] = direct_candidates;
    trace["added_tokens_after_deduplication"] = added_tokens;
    trace["unused_budget_tokens_forwarded"] = carry;
    trace["gaps"] = std::move(gaps);
    traces.push_back(std::move(trace));
  }
  std::sort(result.items.begin(), result.items.end(), item_order);
  std::sort(result.dropped.begin(), result.dropped.end(), item_order);
  result.goal.params["plan_trace"] = Json{{"schema", "loom.context_plan_trace/1"}, {"plan", req.plan},
      {"run", run}, {"budget_policy", "weighted_plan_order_unused_forward"}, {"theses", traces},
      {"limitations", Json::array({"structural_retrieval_is_not_a_semantic_support_verdict",
          "counter_coverage_is_limited_to_recorded_assessment_links",
          "item_estimates_exclude_prompt_headers_and_trace_metadata",
          "unused_budget_is_forwarded_only_not_retried_for_earlier_theses"})}};
  // Include exact material/diagnostics as well as the plan and resolved run.
  // An in-place run update must not silently retain the previous context id.
  result.id = kb::stable_id("cx_", json::dump(result.to_json()));
  LOOM_TRY(result.validate());
  return result;
}

}  // namespace loom::context
