// context_engine.h: goal-directed context selection (LOOM_CONCEPTUAL_MODEL
// §4; R12, R13 §1). See the header for the full contract.
#include "loom/context_engine.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

#include "loom/config.h"
#include "loom/net/http.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/semantic_llm.h"
#include "loom/util/utf8.h"
#include "ctx_common.h"
#include "loom/context_plan.h"
#include "context_request_ext.h"
#include "context_candidates.h"
#include "context_evidence.h"
#include "context_diagnostics.h"

namespace loom::context {

namespace {

using model::ContextBand;
using model::Resolution;

// ── rendered content, one struct per RefKind we handle directly ─────
struct RenderedContent {
  std::string label, summary, full, raw;
  std::string pick(Resolution r) const {
    switch (r) {
      case Resolution::Label:
        return label;
      case Resolution::Summary:
        return summary.empty() ? label : summary;
      case Resolution::Full:
        return full.empty() ? summary : full;
      case Resolution::Raw:
        return raw.empty() ? (full.empty() ? summary : full) : raw;
    }
    return summary;
  }
};

std::string fmt_value(const Json& v) {
  if (v.is_string()) return v.get<std::string>();
  if (v.is_null()) return "(absent)";
  return json::dump(v);
}

std::string join(const std::vector<std::string>& v, std::string_view sep) {
  std::string out;
  for (const auto& s : v) {
    if (!out.empty()) out += sep;
    out += s;
  }
  return out;
}

// One item gathered for scoring, before it is turned into a ContextItem.
struct Candidate {
  model::RefKind ref_kind = model::RefKind::Claim;
  std::string ref;
  ContextBand band = ContextBand::Goal;
  std::optional<model::Role> role;
  std::string subject;  // diversity grouping key
  std::string date;     // for freshness
  model::Origin origin = model::Origin::System;
  double confidence = 0.5;
  bool is_principle = false;                                     // no EvidenceClass of its own
  model::EvidenceClass evidence = model::EvidenceClass::Derived;  // meaningless when is_principle
  model::ValidationStatus validation = model::ValidationStatus::Candidate;
  RenderedContent content;
  std::optional<kb::ExpectedProperty> expected;
  std::string basis;
  Json fill_query;
  std::vector<std::string> premise_claims;
  std::vector<std::string> premise_principles;
  std::string why;
  double base_relevance = 0.5;
  double score = 0.0;  // filled by score_all()
  std::optional<int> relation_hops;
  Json extra_factors = Json::object();  // additive discovery/channel metadata

  // Filled once accepted.
  int tokens = 0;
  std::string rendered_text;
};

void union_refs(std::vector<std::string>& into, const std::vector<std::string>& from) {
  for (const auto& ref : from) {
    if (std::find(into.begin(), into.end(), ref) == into.end()) into.push_back(ref);
  }
}

void merge_factors(Json& into, const Json& from) {
  if (!from.is_object()) return;
  for (const auto& [key, value] : from.items()) {
    if (!into.contains(key)) into[key] = value;
    else if (into[key].is_object() && value.is_object()) merge_factors(into[key], value);
    else if (into[key].is_array() && value.is_array()) {
      for (const auto& item : value) {
        if (std::find(into[key].begin(), into[key].end(), item) == into[key].end()) into[key].push_back(item);
      }
    } else if (into[key].is_boolean() && value.is_boolean()) {
      into[key] = into[key].get<bool>() || value.get<bool>();
    }
  }
}

// A Decision is a view of its underlying claim, not a second item of evidence.
// Preserve distinct representations, provenance and the union of dependencies
// while charging shared references once. Candidate-view factors retain any
// channel-specific scalar values that cannot be combined generically.
void merge_candidate(Candidate& into, Candidate from) {
  auto view = [](const Candidate& c) {
    return Json{{"ref_kind", std::string(model::to_string(c.ref_kind))},
                {"band", std::string(model::to_string(c.band))}, {"why", c.why},
                {"factors", c.extra_factors}};
  };
  Json views = into.extra_factors.value("candidate_views", Json::array());
  if (views.empty()) views.push_back(view(into));
  views.push_back(view(from));
  auto append = [](std::string& value, const std::string& extra) {
    if (extra.empty() || value.find(extra) != std::string::npos) return;
    if (!value.empty()) value += "\n";
    value += extra;
  };
  append(into.content.label, from.content.label);
  append(into.content.summary, from.content.summary);
  append(into.content.full, from.content.full);
  append(into.content.raw, from.content.raw);
  union_refs(into.premise_claims, from.premise_claims);
  union_refs(into.premise_principles, from.premise_principles);
  if (into.why != from.why && !from.why.empty()) into.why += "; " + from.why;
  into.band = std::min(into.band, from.band);
  into.base_relevance = std::max(into.base_relevance, from.base_relevance);
  if (!into.role) into.role = from.role;
  if (from.ref_kind == model::RefKind::Decision) {
    into.ref_kind = from.ref_kind;
    into.role = from.role;
  }
  if (from.relation_hops) {
    into.relation_hops = into.relation_hops ? std::min(*into.relation_hops, *from.relation_hops) : from.relation_hops;
  }
  merge_factors(into.extra_factors, from.extra_factors);
  into.extra_factors["candidate_views"] = std::move(views);
}

bool contains_role(const std::vector<model::Role>& roles, model::Role r) {
  return std::find(roles.begin(), roles.end(), r) != roles.end();
}
bool contains_level(const std::vector<model::PrincipleLevel>& levels, model::PrincipleLevel l) {
  return std::find(levels.begin(), levels.end(), l) != levels.end();
}
bool contains_evidence(const std::vector<model::EvidenceClass>& evs, model::EvidenceClass e) {
  return std::find(evs.begin(), evs.end(), e) != evs.end();
}

Resolution resolution_for(const model::GoalType& gt, std::optional<model::Role> role) {
  if (role) {
    auto it = gt.resolutions.find(std::string(model::to_string(*role)));
    if (it != gt.resolutions.end()) return it->second;
  }
  auto it = gt.resolutions.find("*");
  return it != gt.resolutions.end() ? it->second : Resolution::Summary;
}

void score_all(std::vector<Candidate>& cands, std::string_view anchor_date) {
  for (auto& c : cands) {
    double authority = ctx::authority_score(c.origin);
    double freshness = ctx::freshness_score(c.date, anchor_date);
    c.score = c.base_relevance * authority * freshness * c.confidence;
  }
}

// Maximal-marginal-relevance-style diversity: repeatedly pick the remaining
// candidate whose score, discounted by how many items of the same subject
// were already picked, is highest. Deterministic (ties broken by ref).
std::vector<Candidate> diversify(std::vector<Candidate> cands) {
  std::vector<Candidate> out;
  out.reserve(cands.size());
  std::map<std::string, int> subject_count;
  std::vector<char> used(cands.size(), 0);
  for (std::size_t k = 0; k < cands.size(); ++k) {
    int best = -1;
    double best_eff = -1.0;
    for (std::size_t i = 0; i < cands.size(); ++i) {
      if (used[i]) continue;
      int cnt = subject_count.count(cands[i].subject) ? subject_count[cands[i].subject] : 0;
      double eff = cands[i].score * std::pow(0.8, cnt);
      if (best < 0 || eff > best_eff || (eff == best_eff && cands[i].ref < cands[best].ref)) {
        best = static_cast<int>(i);
        best_eff = eff;
      }
    }
    if (best < 0) break;
    used[best] = 1;
    subject_count[cands[best].subject]++;
    out.push_back(std::move(cands[best]));
  }
  return out;
}

}  // namespace

Result<ContextRequest> ContextRequest::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "context request must be an object");
  ContextRequest r;
  r.text = json::get_string(j, "text");
  if (const Json* t = json::find(j, "targets"); t && t->is_array()) {
    for (const auto& e : *t) {
      if (e.is_string()) r.targets.push_back(e.get<std::string>());
    }
  }
  r.project = json::get_string(j, "project");
  r.budget_tokens = static_cast<int>(json::get_int(j, "budget_tokens", 4000));
  r.goal_type = json::get_opt_string(j, "goal_type");
  r.run = json::get_string(j, "run");
  r.lang = json::get_string(j, "lang");
  if (const Json* hops = json::find(j, "relation_hops")) {
    if (!hops->is_number_integer()) return Error(Errc::InvalidArgument, "relation_hops must be a non-negative integer");
    if (hops->is_number_unsigned()) {
      auto value = hops->get<std::uint64_t>();
      if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return Error(Errc::InvalidArgument, "relation_hops exceeds the supported integer range");
      }
      r.relation_hops = static_cast<int>(value);
    } else {
      auto value = hops->get<std::int64_t>();
      if (value < 0 || value > std::numeric_limits<int>::max()) {
        return Error(Errc::InvalidArgument, "relation_hops must be a non-negative supported integer");
      }
      r.relation_hops = static_cast<int>(value);
    }
  }
  if (const Json* detail = json::find(j, "detail_resolution"); detail && !detail->is_null()) {
    if (!detail->is_string()) return Error(Errc::InvalidArgument, "detail_resolution must be a resolution name or null");
    LOOM_TRY_ASSIGN(auto value, model::parse<Resolution>(detail->get<std::string>(), "detail_resolution"));
    r.detail_resolution = value;
  }
  LOOM_TRY(parse_context_extensions(j, r));
  return r;
}

Json ContextRequest::to_json() const {
  Json targets_j = Json::array();
  for (const auto& t : targets) targets_j.push_back(t);
  Json result{{"text", text},
              {"targets", targets_j},
              {"project", project},
              {"budget_tokens", budget_tokens},
              {"goal_type", goal_type ? Json(*goal_type) : Json(nullptr)},
              {"run", run},
              {"lang", lang}};
  // Keep the historical serialized defaults unchanged.
  if (relation_hops != 1) result["relation_hops"] = relation_hops;
  if (detail_resolution) result["detail_resolution"] = std::string(model::to_string(*detail_resolution));
  serialize_context_extensions(result, *this);
  return result;
}

ContextEngine::ContextEngine(Runtime& rt, kb::KnowledgeStore& store, std::shared_ptr<const kb::Pack> pack)
    : rt_(rt), store_(store), pack_(std::move(pack)) {}

int ContextEngine::estimate_tokens(std::string_view text) noexcept {
  std::size_t n = utf8::length(text);
  return n == 0 ? 0 : static_cast<int>(std::max<std::size_t>(1, n / 4));
}

// ── goal typing ──────────────────────────────────────────────────────

namespace {

struct Classification {
  std::string type;
  double confidence = 0.0;
  Json scores = Json::object();
};

Classification classify_by_cues(const std::vector<model::GoalType>& types, const kb::Normalizer& norm,
                                std::string_view text) {
  std::string folded = norm.fold(text);
  std::map<std::string, double> scores;
  for (const auto& gt : types) {
    double s = 0.0;
    if (gt.cues.is_object()) {
      for (const auto& [lang, arr] : gt.cues.items()) {
        (void)lang;
        if (!arr.is_array()) continue;
        for (const auto& cue_j : arr) {
          if (!cue_j.is_string()) continue;
          std::string cue = norm.fold(cue_j.get<std::string>());
          if (cue.empty()) continue;
          std::size_t words = 1 + static_cast<std::size_t>(std::count(cue.begin(), cue.end(), ' '));
          double weight = 1.0 + 0.4 * static_cast<double>(words - 1);
          std::size_t pos = 0, hits = 0;
          while ((pos = folded.find(cue, pos)) != std::string::npos) {
            ++hits;
            pos += cue.size();
          }
          s += weight * static_cast<double>(hits);
        }
      }
    }
    scores[gt.id] = s;
  }
  std::string best;
  double best_s = -1.0, second_s = 0.0;
  for (const auto& [id, s] : scores) {
    if (s > best_s) {
      second_s = best_s < 0 ? 0.0 : best_s;
      best_s = s;
      best = id;
    } else if (s > second_s) {
      second_s = s;
    }
  }
  Classification c;
  Json sj = Json::object();
  for (const auto& [id, s] : scores) sj[id] = s;
  c.scores = sj;
  if (best_s <= 0.0) {
    // No cue matched anything: fall back to the most general goal type
    // (first by id; goal_types.json sorts "answer_question" among them) at
    // low confidence rather than guessing.
    c.type = types.empty() ? "" : types.front().id;
    for (const auto& gt : types) {
      if (gt.id == "answer_question") c.type = gt.id;
    }
    c.confidence = 0.25;
    return c;
  }
  c.type = best;
  c.confidence = std::clamp(best_s / (best_s + second_s + 1.0), 0.0, 1.0);
  return c;
}

Result<model::GoalType> find_goal_type_checked(const kb::Pack& pack, std::string_view id,
                                               const std::vector<model::GoalType>& all) {
  for (const auto& gt : all) {
    if (gt.id == id) return gt;
  }
  std::string ids;
  for (const auto& gt : all) {
    if (!ids.empty()) ids += ", ";
    ids += gt.id;
  }
  (void)pack;
  return Error(Errc::NotFound, "unknown goal type '" + std::string(id) + "' (one of: " + ids + ")");
}

}  // namespace

Result<model::Goal> ContextEngine::type_goal(const ContextRequest& req) {
  return type_goal_impl(req, nullptr);
}

Result<model::Goal> ContextEngine::type_goal_with_model(const ContextRequest& req, const GoalTypingBudget& budget) {
  if (budget.max_requests < 0 || budget.max_requests > 1 || budget.max_input_bytes > 256000 || budget.max_response_bytes > 256000 ||
      budget.max_output_tokens < 0 || budget.max_output_tokens > 4096 || budget.timeout_ms < 0 || budget.timeout_ms > 60000 ||
      (budget.max_requests > 0 && (budget.max_input_bytes == 0 || budget.max_output_tokens == 0 || budget.timeout_ms == 0 || budget.max_response_bytes == 0))) {
    return Error(Errc::InvalidArgument, "goal typing requires an explicit bounded request/input/output/timeout/response budget");
  }
  return type_goal_impl(req, &budget);
}

Result<model::Goal> ContextEngine::type_goal_impl(const ContextRequest& req, const GoalTypingBudget* budget) {
  LOOM_TRY_ASSIGN(auto all_types, ctx::list_goal_types(*pack_));
  kb::Normalizer norm(*pack_);

  std::string chosen_type;
  double confidence = 0.0;
  Json params = Json::object();
  params["confidence_basis"] = "heuristic_cue_margin";
  params["calibration_status"] = "unavailable";
  params["external_goal_typing"] = Json{{"status", budget ? "not_needed" : "offline"}, {"requests", 0}};
  if (budget) {
    params["external_goal_typing"]["budget"] = Json{{"max_requests", budget->max_requests},
                                                   {"max_input_bytes", budget->max_input_bytes},
                                                   {"max_output_tokens", budget->max_output_tokens},
                                                   {"timeout_ms", budget->timeout_ms},
                                                   {"max_response_bytes", budget->max_response_bytes}};
    if (budget->max_requests == 0) params["external_goal_typing"]["status"] = "budget_disabled";
  }

  if (req.goal_type && !req.goal_type->empty()) {
    LOOM_TRY_ASSIGN(auto gt, find_goal_type_checked(*pack_, *req.goal_type, all_types));
    chosen_type = gt.id;
    confidence = 1.0;
    params["classifier"] = "forced";
    params["confidence_basis"] = "explicit_owner_type";
  } else {
    auto cls = classify_by_cues(all_types, norm, req.text);
    chosen_type = cls.type;
    confidence = cls.confidence;
    params["classifier"] = "cue";
    params["scores"] = cls.scores;

    // Preview/select/build always use offline typing. A separate native caller
    // can authorize a bounded optional instrument; model/key presence is only
    // a capability check, never permission to spend or send prompt contents.
    if (budget && budget->max_requests > 0 && confidence < 0.55) {
      auto& attempt = params["external_goal_typing"];
      attempt["status"] = "unavailable";
      std::string model = rt_.config().get("semantic_model", "").is_string()
                              ? rt_.config().get("semantic_model", "").get<std::string>()
                              : "";
      std::string api_key = rt_.secrets().get_string("api_key");
      if (!model.empty() && !api_key.empty() && !chosen_type.empty()) {
        Json base_url_j = rt_.config().get("base_url", "https://openrouter.ai/api/v1");
        std::string base_url = base_url_j.is_string() ? base_url_j.get<std::string>() : "https://openrouter.ai/api/v1";
        std::string prompt =
            "Classify the following prompt into exactly one goal type id. Reply with strict JSON only: "
            "{\"goal_type\": \"<id>\", \"confidence\": <0..1>}.\nGoal types:\n";
        for (const auto& gt : all_types) {
          prompt += "- " + gt.id + ": " + gt.description + "\n";
        }
        prompt += "\nPrompt:\n" + req.text;

        if (prompt.size() > budget->max_input_bytes) {
          attempt["status"] = "input_limit";
          attempt["input_bytes"] = prompt.size();
        } else {
          net::HttpRequest hreq;
          hreq.method = "POST";
          hreq.url = base_url + "/chat/completions";
          hreq.headers = {{"Authorization", "Bearer " + api_key},
                          {"Content-Type", "application/json"},
                          {"HTTP-Referer", "https://github.com/chatadhd"},
                          {"X-Title", "ChatADHD-Context"}};
          hreq.body = json::dump(Json{{"model", model},
                                      {"messages", Json::array({Json{{"role", "user"}, {"content", prompt}}})},
                                      {"temperature", 0.0},
                                      {"max_tokens", budget->max_output_tokens}});
          hreq.timeout_ms = budget->timeout_ms;
          attempt["status"] = "failed";
          attempt["requests"] = 1;
          attempt["retry_authorized"] = false;
          attempt["model"] = model;
          attempt["input_bytes"] = prompt.size();
          std::string response_bytes;
          bool response_limited = false, received_headers = false;
          int response_status = 0;
          net::StreamSink sink;
          sink.on_headers = [&](int status, const net::Headers&) {
            received_headers = true;
            response_status = status;
            return true;
          };
          sink.on_data = [&](std::string_view part) {
            const auto room = budget->max_response_bytes - response_bytes.size();
            response_bytes.append(part.substr(0, room));
            if (part.size() > room) {
              response_limited = true;
              return false;
            }
            return true;
          };
          auto resp = rt_.http().send(hreq, &sink);
          if (resp) response_status = resp->status;
          else attempt["transport_error"] = std::string(errc_name(resp.error().code));
          // The provider may echo private prompt contents or credentials. Keep
          // the actual bytes in the canonical raw-source store; a loggable goal
          // trace carries references only. Oversized replies retain an explicitly
          // incomplete prefix and are never interpreted or automatically retried.
          bool response_recorded = false;
          if (received_headers || resp || !response_bytes.empty()) {
            auto blob = rt_.blobs().put(response_bytes, "application/json");
            if (!blob) {
              attempt["status"] = "storage_failure";
              attempt["storage_error"] = std::string(errc_name(blob.error().code));
              attempt["response"] = Json{{"status", response_status}, {"source_status", "unavailable"},
                                          {"complete", resp.has_value() && !response_limited}};
            } else {
              SourceRecord source;
              source.kind = "api";
              source.blob_hash = blob->hash;
              source.size = blob->size;
              source.mime = "application/json";
              source.title = "Goal typing first response";
              source.parser = "loom.context.goal_typing";
              source.parser_version = "1";
              source.metadata = Json{{"model", model}, {"status", response_status},
                                     {"complete", resp.has_value() && !response_limited},
                                     {"response_limit", response_limited}};
              attempt["response"] = Json{{"blob_hash", blob->hash}, {"bytes", blob->size},
                                          {"status", response_status}, {"complete", resp.has_value() && !response_limited}};
              auto source_id = rt_.provenance().add_source(std::move(source));
              if (!source_id) {
                attempt["status"] = "storage_failure";
                attempt["storage_error"] = std::string(errc_name(source_id.error().code));
                attempt["response"]["source_status"] = "unavailable";
              } else {
                attempt["response"]["source_id"] = *source_id;
                attempt["response"]["source_status"] = "recorded";
                response_recorded = true;
              }
            }
          }
          if (response_limited && attempt["status"] != "storage_failure") attempt["status"] = "response_limit";
          if (resp && resp->ok() && !response_limited && response_recorded) {
            auto body = json::parse(response_bytes);
            if (body) {
              std::string content;
              if (const Json* choices = json::find(*body, "choices"); choices && choices->is_array() && !choices->empty()) {
                if (const Json* msg = json::find((*choices)[0], "message"); msg) content = json::get_string(*msg, "content");
              }
              if (auto parsed = SemanticLLM::parse_response_json(content)) {
                std::string llm_type = json::get_string(*parsed, "goal_type");
                const Json* reported_confidence = json::find(*parsed, "confidence");
                bool valid = false;
                for (const auto& gt : all_types) valid = valid || gt.id == llm_type;
                const double llm_conf = reported_confidence && reported_confidence->is_number()
                                            ? reported_confidence->get<double>()
                                            : std::numeric_limits<double>::quiet_NaN();
                if (valid && std::isfinite(llm_conf) && llm_conf >= 0.0 && llm_conf <= 1.0) {
                  chosen_type = llm_type;
                  confidence = llm_conf;
                  params["classifier"] = "llm";
                  params["confidence_basis"] = "provider_self_report";
                  params["reported_confidence"] = llm_conf;
                  attempt["confidence_basis"] = "provider_self_report";
                  attempt["calibration_status"] = "unavailable";
                  attempt["reported_confidence"] = llm_conf;
                  attempt["status"] = "accepted";
                }
              }
            }
          }
          // Any failed attempt retains the cue result and its labelled trace.
        }
      }
    }
  }

  model::Goal goal;
  goal.type = chosen_type;
  goal.text = req.text;
  goal.targets = req.targets;
  goal.project = req.project;
  goal.confidence = confidence;
  goal.params = params;
  goal.id = model::Goal::make_id(goal.type, goal.text, goal.targets);
  return goal;
}

// ── selection ────────────────────────────────────────────────────────

Result<model::ContextSet> ContextEngine::select(const ContextRequest& req) {
  LOOM_TRY(validate_context_extensions(req));
  if (req.relation_hops < 0) return Error(Errc::InvalidArgument, "relation_hops must be non-negative");
  if (req.detail_resolution && !model::from_string<Resolution>(model::to_string(*req.detail_resolution))) {
    return Error(Errc::InvalidArgument, "invalid detail_resolution");
  }
  if (!req.plan.is_null()) return select_context_plan(*this, store_, req);
  LOOM_TRY_ASSIGN(model::Goal goal, type_goal(req));
  LOOM_TRY_ASSIGN(std::string run, ctx::resolve_run(store_, req.run));
  LOOM_TRY_ASSIGN(auto all_types, ctx::list_goal_types(*pack_));
  LOOM_TRY_ASSIGN(model::GoalType gt, find_goal_type_checked(*pack_, goal.type, all_types));
  const bool explicit_controls = req.relation_hops != 1 || req.detail_resolution.has_value();
  if (explicit_controls) {
    goal.params["context_controls"] = Json{{"relation_hops", req.relation_hops},
        {"detail_resolution", req.detail_resolution ? Json(std::string(model::to_string(*req.detail_resolution))) : Json(nullptr)}};
  }

  int total_budget = req.budget_tokens > 0 ? req.budget_tokens : 4000;
  std::string lang = req.lang;

  // These are bounded store queries, not an assertion of complete retrieval or
  // semantic relevance. Reaching a cap is ambiguous until paging exists; a
  // failed query must never look like a successful query with zero matches.
  Json diagnostics{{"status", "bounded"}, {"exhaustive", false},
                   {"queries", Json::array()}, {"lookup_issues", Json::array()},
                   {"excluded", Json::array()}, {"missing_premises", Json::array()},
                   {"query_errors", 0}, {"cap_hits", 0}};
  auto query_diagnostic = [&](std::string_view operation, Json selector, const auto& result, int limit) {
    Json row{{"operation", operation}, {"selector", std::move(selector)}, {"limit", limit}};
    if (!result) {
      row["status"] = "error";
      row["error_code"] = std::string(errc_name(result.error().code));
      row["returned"] = nullptr;
      diagnostics["query_errors"] = diagnostics["query_errors"].get<int>() + 1;
      diagnostics["status"] = "incomplete";
    } else {
      row["returned"] = result->size();
      const bool capped = result->size() >= static_cast<std::size_t>(limit);
      row["status"] = capped ? "cap_reached" : "below_cap";
      if (capped) {
        diagnostics["cap_hits"] = diagnostics["cap_hits"].get<int>() + 1;
        diagnostics["status"] = "incomplete";
      }
    }
    diagnostics["queries"].push_back(std::move(row));
  };
  auto lookup_diagnostic = [&](std::string_view operation, const std::string& ref, const auto& result) {
    if (result && *result) return;
    Json row{{"operation", operation}, {"ref", ref}, {"status", result ? "missing_record" : "query_error"}};
    if (!result) {
      row["error_code"] = std::string(errc_name(result.error().code));
      diagnostics["query_errors"] = diagnostics["query_errors"].get<int>() + 1;
      diagnostics["status"] = "incomplete";
    }
    diagnostics["lookup_issues"].push_back(std::move(row));
  };
  auto excluded = [&](const std::string& ref, std::string_view channel, std::string_view reason) {
    diagnostics["excluded"].push_back(Json{{"ref", ref}, {"channel", channel}, {"reason", reason}});
  };

  std::map<std::string, model::Entity> entity_cache;
  auto label_of = [&](const std::string& id) -> std::string {
    if (id.empty()) return "";
    auto it = entity_cache.find(id);
    if (it == entity_cache.end()) {
      auto r = store_.get_entity(run, id);
      lookup_diagnostic("entity_label", id, r);
      model::Entity e;
      if (r && *r) e = **r;
      else e.label = id;
      it = entity_cache.emplace(id, std::move(e)).first;
    }
    return it->second.label.empty() ? (it->second.canonical_key.empty() ? id : it->second.canonical_key)
                                    : it->second.label;
  };

  auto role_rank = [&](std::optional<model::Role> r) -> int {
    if (!r) return -1;
    for (std::size_t i = 0; i < gt.roles.size(); ++i) {
      if (gt.roles[i] == *r) return static_cast<int>(i);
    }
    return -1;
  };

  std::vector<std::string> seeds = goal.targets;
  std::string anchor_project = !goal.project.empty() ? goal.project : (seeds.empty() ? "" : seeds.front());
  if (!anchor_project.empty() && std::find(seeds.begin(), seeds.end(), anchor_project) == seeds.end()) {
    seeds.push_back(anchor_project);
  }

  // Lexical overlap with the prompt text: the only place the literal wording
  // of `req.text` enters scoring. Applied to every band except stable (which
  // is goal-independent by definition): a project's decisions/slots/status
  // history are still ranked by relevance to THIS prompt when a budget can't
  // hold all of them, not only by their static role/position.
  kb::Normalizer norm(*pack_);
  std::string q_folded = norm.fold(req.text);
  auto lexical_overlap = [&](const RenderedContent& rc) -> double {
    if (q_folded.empty()) return 0.0;
    std::string t = norm.fold(rc.summary);
    if (t.empty()) return 0.0;
    auto qt = norm.tokens(q_folded);
    if (qt.empty()) return 0.0;
    std::size_t hits = 0;
    for (auto& tok : qt) {
      if (tok.size() < 3) continue;
      if (t.find(tok) != std::string::npos) ++hits;
    }
    return qt.empty() ? 0.0 : static_cast<double>(hits) / static_cast<double>(qt.size());
  };

  std::vector<Candidate> stable, project_b, goal_b;
  std::string anchor_date;
  auto note_date = [&](const std::string& d) {
    if (!d.empty() && d > anchor_date) anchor_date = d;
  };

  // ── principles: stable (invariant + preference) or project (matching level) ──
  auto principle_rows = store_.list_principles(run);
  query_diagnostic("principles", Json::object(), principle_rows, 1000000);
  if (!principle_rows) return principle_rows.error();
  auto principles = std::move(*principle_rows);
  for (auto& p : principles) {
    std::string d = model::earliest_source_date(p.sources);
    note_date(d);
    bool core = p.form == model::PrincipleForm::Invariant || p.is_preference();
    bool level_ok = contains_level(gt.principle_levels, p.level);
    if (!core && !level_ok) {
      excluded(p.id, "principles", "principle_level");
      continue;
    }
    Candidate c;
    c.ref_kind = model::RefKind::Principle;
    c.ref = p.id;
    c.band = core ? ContextBand::Stable : ContextBand::Project;
    c.subject = p.id;
    c.date = d;
    c.origin = p.origin;
    c.confidence = p.confidence;
    c.is_principle = true;
    c.validation = p.validation;
    std::string stmt = ctx::pick_text(p.statement, lang);
    c.content.label = utf8::length(stmt) > 100 ? std::string(utf8::prefix(stmt, 100)) + "..." : stmt;
    std::string badge = ctx::principle_badge(p.validation, p.level, p.form);
    c.content.summary = stmt + " " + badge;
    std::string full = c.content.summary;
    if (!p.protects.empty()) full += " (protects: " + join(p.protects, ", ") + ")";
    if (!p.exceptions.empty()) full += " (exceptions: " + join(p.exceptions, "; ") + ")";
    c.content.full = full;
    c.content.raw = json::dump(p.to_json());
    c.premise_principles = p.derived_from;
    c.why = core ? (p.is_preference() ? "core preference (always included)" : "invariant principle (always included)")
                 : ("principle at level '" + std::string(model::to_string(p.level)) + "' matches this goal type");
    c.base_relevance = core ? 1.0 : 0.7;
    (core ? stable : project_b).push_back(std::move(c));
  }

  // ── project band: instance slots, decisions, status history ─────────
  if (!anchor_project.empty()) {
    auto insts = store_.query_instances(run, "", anchor_project);
    query_diagnostic("project_instances", Json{{"project", anchor_project}}, insts, 1000000);
    if (insts) {
      for (auto& inst : *insts) {
        kb::SlotQuery sq;
        sq.instance = inst.id;
        auto rows = store_.query_slots(run, sq);
        query_diagnostic("project_slots", Json{{"instance", inst.id}}, rows, sq.limit);
        if (!rows) continue;
        for (auto& row : *rows) {
          if (row.value.role && !contains_role(gt.roles, *row.value.role)) {
            excluded(row.value.claim, "project_slots", "role");
            continue;
          }
          auto claim_r = store_.get_claim(run, row.value.claim);
          lookup_diagnostic("slot_claim", row.value.claim, claim_r);
          if (!claim_r || !*claim_r) continue;
          const auto& claim = **claim_r;
          if (!contains_evidence(gt.evidence, claim.assessment.evidence)) {
            excluded(claim.id, "project_slots", "evidence_class");
            continue;
          }
          note_date(claim.qualifiers.valid_from);
          Candidate c;
          c.ref_kind = model::RefKind::Claim;
          c.ref = claim.id;
          c.band = ContextBand::Project;
          c.role = row.value.role;
          c.subject = claim.subject;
          c.date = claim.qualifiers.valid_from;
          c.origin = claim.assessment.origin;
          c.confidence = claim.assessment.confidence;
          c.evidence = claim.assessment.evidence;
          c.expected = claim.assessment.expected;
          if (claim.assessment.derivation) c.basis = claim.assessment.derivation->op;
          c.fill_query = claim.assessment.open.fill_query;
          std::string subj_label = label_of(claim.subject);
          std::string obj_label = claim.object.empty() ? fmt_value(claim.value) : label_of(claim.object);
          c.content.label = subj_label + " " + claim.predicate + " " + obj_label;
          c.content.summary = c.content.label + (claim.qualifiers.valid_from.empty() ? "" : " (" + claim.qualifiers.valid_from + ")");
          std::string full = c.content.summary;
          if (!claim.assessment.support.empty()) full += " — \"" + std::string(utf8::prefix(claim.assessment.support.front().quote, 160)) + "\"";
          c.content.full = full;
          std::string raw;
          for (auto& s : claim.assessment.support) raw += (raw.empty() ? "" : " / ") + s.quote;
          c.content.raw = raw.empty() ? full : raw;
          c.premise_claims = claim.assessment.premises.claims;
          c.premise_principles = claim.assessment.premises.principles;
          int rr = role_rank(row.value.role);
          double lex = lexical_overlap(c.content);
          c.why = "project slot '" + row.value.slot + "'" + (rr >= 0 ? " (role priority " + std::to_string(rr) + ")" : "");
          double static_rel = rr >= 0 ? 0.6 + 0.4 * (1.0 - static_cast<double>(rr) / std::max<std::size_t>(1, gt.roles.size())) : 0.55;
          c.base_relevance = std::clamp(0.6 * static_rel + 0.5 * lex, 0.0, 1.0);
          project_b.push_back(std::move(c));
        }
      }
    }

    auto decisions = store_.list_decisions(run, anchor_project);
    query_diagnostic("project_decisions", Json{{"project", anchor_project}}, decisions, 1000000);
    if (decisions) {
      for (auto& d : *decisions) {
        auto claim_r = store_.get_claim(run, d.id);
        lookup_diagnostic("decision_claim", d.id, claim_r);
        model::Claim underlying;
        bool have_claim = claim_r && *claim_r;
        if (have_claim) underlying = **claim_r;
        if (have_claim && !contains_evidence(gt.evidence, underlying.assessment.evidence)) {
          excluded(d.id, "project_decisions", "evidence_class");
          continue;
        }
        note_date(d.date);
        Candidate c;
        c.ref_kind = model::RefKind::Decision;
        c.ref = d.id;
        c.band = ContextBand::Project;
        c.role = model::Role::Decision;
        c.subject = d.subject;
        c.date = d.date;
        c.origin = have_claim ? underlying.assessment.origin : model::Origin::Archive;
        c.confidence = have_claim ? underlying.assessment.confidence : 0.7;
        c.evidence = have_claim ? underlying.assessment.evidence : model::EvidenceClass::Observed;
        const model::DecisionAlternative* chosen = d.chosen();
        std::string chosen_label = chosen ? (chosen->object.empty() ? fmt_value(chosen->value) : label_of(chosen->object)) : "?";
        std::string subj_label = label_of(d.subject);
        c.content.label = subj_label + " decided: " + chosen_label;
        c.content.summary = c.content.label + (d.date.empty() ? "" : " (" + d.date + ")");
        std::vector<std::string> alt_labels;
        for (auto& a : d.alternatives) alt_labels.push_back(a.label.empty() ? fmt_value(a.value) : a.label);
        std::string full = c.content.summary + (alt_labels.empty() ? "" : " — among: " + join(alt_labels, " | "));
        if (!d.principles.empty()) full += "; principles: " + join(d.principles, ", ");
        if (!d.superseded_by.empty()) full += "; superseded by " + d.superseded_by;
        c.content.full = full;
        c.content.raw = json::dump(d.to_json());
        c.premise_principles = d.principles;
        if (have_claim) {
          c.premise_claims = underlying.assessment.premises.claims;
          union_refs(c.premise_principles, underlying.assessment.premises.principles);
        }
        c.why = "decision recorded for " + subj_label;
        double static_rel = contains_role(gt.roles, model::Role::Decision) ? 0.75 : 0.5;
        c.base_relevance = std::clamp(0.6 * static_rel + 0.5 * lexical_overlap(c.content), 0.0, 1.0);
        project_b.push_back(std::move(c));
      }
    }

    for (const auto& e : seeds) {
      auto hist = store_.status_history(run, e);
      query_diagnostic("status_history", Json{{"entity", e}}, hist, 1000000);
      if (!hist) continue;
      for (auto& sr : *hist) {
        note_date(sr.date);
        Candidate c;
        c.ref_kind = model::RefKind::StatusRecord;
        c.ref = sr.id;
        c.band = ContextBand::Project;
        c.role = model::Role::Part;
        c.subject = sr.entity;
        c.date = sr.date;
        c.origin = model::Origin::Archive;
        c.confidence = 0.75;
        c.evidence = model::EvidenceClass::Observed;
        std::string entity_label = label_of(sr.entity);
        c.content.label = entity_label + ": " + std::string(model::to_string(sr.status));
        c.content.summary = c.content.label + " (branch " + (sr.branch.empty() ? "main" : sr.branch) + ", v" + sr.version + ", " + sr.date + ")";
        c.content.full = c.content.summary + (sr.oscillation ? " [oscillates]" : "");
        c.content.raw = json::dump(sr.to_json());
        c.why = "status history of " + entity_label;
        double static_rel = contains_role(gt.roles, model::Role::Part) ? 0.65 : 0.45;
        c.base_relevance = std::clamp(0.6 * static_rel + 0.5 * lexical_overlap(c.content), 0.0, 1.0);
        project_b.push_back(std::move(c));
      }
    }
  }

  // ── goal band: explicit graph radius, either direction ─────────────
  std::set<std::string> seen_claims;
  auto add_goal_claim = [&](const model::Claim& claim, const std::string& via_entity, int hops) {
    if (!seen_claims.insert(claim.id).second) return;
    if (!contains_evidence(gt.evidence, claim.assessment.evidence)) return;
    note_date(claim.qualifiers.valid_from);
    Candidate c;
    c.ref_kind = model::RefKind::Claim;
    c.ref = claim.id;
    c.band = ContextBand::Goal;
    c.subject = claim.subject;
    c.date = claim.qualifiers.valid_from;
    c.origin = claim.assessment.origin;
    c.confidence = claim.assessment.confidence;
    c.evidence = claim.assessment.evidence;
    c.expected = claim.assessment.expected;
    if (claim.assessment.derivation) c.basis = claim.assessment.derivation->op;
    c.fill_query = claim.assessment.open.fill_query;
    std::string subj_label = label_of(claim.subject);
    std::string obj_label = claim.object.empty() ? fmt_value(claim.value) : label_of(claim.object);
    c.content.label = subj_label + " " + claim.predicate + " " + obj_label;
    c.content.summary = c.content.label + (claim.qualifiers.valid_from.empty() ? "" : " (" + claim.qualifiers.valid_from + ")");
    std::string full = c.content.summary;
    if (!claim.assessment.support.empty()) full += " — \"" + std::string(utf8::prefix(claim.assessment.support.front().quote, 160)) + "\"";
    if (claim.assessment.status == model::ClaimStatus::Contested) full += " [contested]";
    c.content.full = full;
    std::string raw;
    for (auto& s : claim.assessment.support) raw += (raw.empty() ? "" : " / ") + s.quote;
    c.content.raw = raw.empty() ? full : raw;
    c.premise_claims = claim.assessment.premises.claims;
    c.premise_principles = claim.assessment.premises.principles;
    if (hops > 0) c.relation_hops = hops;
    c.why = hops == 1 ? "one hop from '" + label_of(via_entity) + "' via '" + claim.predicate + "'"
        : "within " + std::to_string(hops) + " relation hops of a target/project seed, via '" + label_of(via_entity) + "' and '" + claim.predicate + "'";
    if (hops == 0) c.why = "explicit claim or candidate instrument";
    double lex = lexical_overlap(c.content);
    c.base_relevance = std::clamp(0.35 + 0.65 * lex, 0.0, 1.0);
    goal_b.push_back(std::move(c));
  };

  std::vector<std::string> frontier;
  std::set<std::string> visited_entities;
  for (const auto& seed : seeds) {
    if (!seed.empty() && visited_entities.insert(seed).second) frontier.push_back(seed);
  }
  for (int hop = 0; hop < req.relation_hops && !frontier.empty(); ++hop) {
    std::vector<std::string> next;
    for (const auto& e : frontier) {
      auto visit = [&](const model::Claim& claim) {
        // A filtered claim cannot become an invisible bridge to other data.
        if (!contains_evidence(gt.evidence, claim.assessment.evidence)) {
          excluded(claim.id, "graph", "evidence_class");
          return;
        }
        add_goal_claim(claim, e, hop + 1);
        for (const auto& endpoint : {claim.subject, claim.object}) {
          if (!endpoint.empty() && visited_entities.insert(endpoint).second) next.push_back(endpoint);
        }
      };
      kb::ClaimQuery qs;
      qs.subject = e;
      {
        auto r = store_.query_claims(run, qs);
        query_diagnostic("graph", Json{{"entity", e}, {"direction", "outgoing"}, {"hop", hop + 1}}, r, qs.limit);
        if (r) {
        for (const auto& claim : *r) visit(claim);
        }
      }
      kb::ClaimQuery qo;
      qo.object = e;
      {
        auto r = store_.query_claims(run, qo);
        query_diagnostic("graph", Json{{"entity", e}, {"direction", "incoming"}, {"hop", hop + 1}}, r, qo.limit);
        if (r) {
        for (const auto& claim : *r) visit(claim);
        }
      }
    }
    frontier = std::move(next);
  }

  // Union independent candidate instruments before the shared selector. A
  // lexical shadow only diagnoses omissions; it cannot veto or admit items.
  std::vector<std::string> direct_refs;
  std::set<std::string> known_refs;
  for (const auto* band : {&project_b, &goal_b}) {
    for (const auto& c : *band) {
      known_refs.insert(c.ref);
      if (c.ref_kind == model::RefKind::Claim || c.ref_kind == model::RefKind::Decision) direct_refs.push_back(c.ref);
    }
  }
  const std::set<std::string> graph_ids(direct_refs.begin(), direct_refs.end());
  const auto retrieved = gather_context_candidates(store_, run, pack_, req, gt.evidence, direct_refs, candidate_channels_);
  for (const auto& claim : retrieved.claims) {
    if (known_refs.insert(claim.id).second) add_goal_claim(claim, "", 0);
    direct_refs.push_back(claim.id);
  }
  std::set<std::string> direct_ids(direct_refs.begin(), direct_refs.end());
  direct_ids.insert(req.claim_targets.begin(), req.claim_targets.end());
  ContextEvidence evidence;
  if (!req.claim_targets.empty() || req.include_counter_evidence) {
    evidence = gather_context_evidence(store_, run, req, direct_refs, gt.evidence);
    for (const auto& claim : evidence.claims) {
      if (known_refs.insert(claim.id).second) add_goal_claim(claim, "", 0);
    }
    for (const auto& observation : evidence.observations) {
      Candidate c;
      c.ref_kind = model::RefKind::Observation;
      c.ref = observation.id;
      c.subject = observation.unit;
      c.date = observation.date;
      c.band = ContextBand::Goal;
      c.origin = model::Origin::Archive;
      c.evidence = model::EvidenceClass::Observed;
      c.confidence = 1.0; // quoted observation, not confidence in its assertion
      c.content.label = "Recorded counter-observation: " + std::string(utf8::prefix(observation.text, 100));
      c.content.summary = c.content.label;
      c.content.full = "Recorded counter-observation: " + observation.text;
      c.content.raw = c.content.full;
      c.why = "recorded counter-observation of a retrieved claim";
      c.base_relevance = 0.6;
      c.extra_factors["observation_locator"] = observation.locator.to_json();
      note_date(c.date);
      goal_b.push_back(std::move(c));
    }
  }
  const bool extended = !req.claim_targets.empty() || req.include_counter_evidence ||
      !req.candidate_channels.empty() || req.lexical_shadow;
  for (auto* band : {&project_b, &goal_b}) {
    for (auto& c : *band) {
      if (direct_ids.count(c.ref)) c.extra_factors["direct_goal_candidate"] = true;
      if (std::find(req.claim_targets.begin(), req.claim_targets.end(), c.ref) != req.claim_targets.end()) {
        c.base_relevance = 1.0;
        c.why += "; explicit claim anchor";
      }
      if (auto counter = evidence.counter_for.find(c.ref); counter != evidence.counter_for.end()) {
        c.extra_factors["counter_for"] = counter->second;
        c.why += "; recorded counter-evidence";
      }
      if (auto channels = retrieved.factors.find(c.ref); channels != retrieved.factors.end()) {
        if (!graph_ids.count(c.ref) && std::find(req.claim_targets.begin(), req.claim_targets.end(), c.ref) == req.claim_targets.end()) {
          c.base_relevance = 0.0;
        }
        merge_factors(c.extra_factors, channels->second);
        for (const auto& signal : channels->second["candidate_channels"]) {
          c.base_relevance = std::max(c.base_relevance, signal.value("selection_relevance", 0.0));
        }
        c.why += "; independent candidate instrument (reciprocal rank signal)";
      }
    }
  }

  // Merge discovery paths before scoring/budgeting. Dependencies and distinct
  // views of one reference survive even when the graph also finds a slot or
  // decision already gathered from the project.
  std::map<std::string, Candidate> unique;
  std::size_t gathered = 0;
  for (auto* band : {&stable, &project_b, &goal_b}) {
    for (auto& candidate : *band) {
      ++gathered;
      auto found = unique.find(candidate.ref);
      if (found == unique.end()) unique.emplace(candidate.ref, std::move(candidate));
      else merge_candidate(found->second, std::move(candidate));
    }
    band->clear();
  }
  diagnostics["gathered_candidates"] = gathered;
  diagnostics["unique_candidates"] = unique.size();
  diagnostics["duplicate_candidates_merged"] = gathered - unique.size();
  for (auto& [ref, candidate] : unique) {
    (void)ref;
    (candidate.band == ContextBand::Stable ? stable : candidate.band == ContextBand::Project ? project_b : goal_b)
        .push_back(std::move(candidate));
  }

  // ── score, diversify, budget (cascading leftover forward) ───────────
  score_all(stable, anchor_date);
  score_all(project_b, anchor_date);
  score_all(goal_b, anchor_date);
  auto stable_d = diversify(std::move(stable));
  auto project_d = diversify(std::move(project_b));
  auto goal_d = diversify(std::move(goal_b));

  // Round cumulative boundaries, not independent shares: the capacities always
  // add up to the exact item budget, including budgets of one or two tokens.
  const double stable_share = gt.budget.count(ContextBand::Stable) ? gt.budget.at(ContextBand::Stable) : 0.15;
  const double project_share = gt.budget.count(ContextBand::Project) ? gt.budget.at(ContextBand::Project) : 0.25;
  const int stable_end = std::clamp(static_cast<int>(std::llround(stable_share * total_budget)), 0, total_budget);
  const int project_end = std::clamp(static_cast<int>(std::llround((stable_share + project_share) * total_budget)), stable_end, total_budget);
  std::array<int, 3> band_budget = {stable_end, project_end - stable_end, total_budget - project_end};
  std::array<int, 3> used = {0, 0, 0};
  int used_total = 0;
  diagnostics["budget"] = Json{{"metric", "rendered_item_codepoints_div_4"},
      {"includes_prompt_overhead", false}, {"initial_band_tokens", band_budget}, {"total_tokens", total_budget}};

  std::vector<model::ContextItem> accepted;
  std::vector<model::ContextItem> dropped;
  std::map<std::string, std::size_t> accepted_index;  // ref -> index in `accepted`

  auto finalize_text = [&](Candidate& c, Resolution res) {
    std::string text = c.content.pick(res);
    if (c.is_principle) {
      c.rendered_text = text;
    } else {
      c.rendered_text = ctx::evidence_markdown(*pack_, c.evidence, c.origin, c.confidence, text, c.expected, c.basis, c.fill_query);
    }
    c.tokens = estimate_tokens(c.rendered_text);
  };

  auto try_accept = [&](Candidate c) {
    Resolution res = req.detail_resolution.value_or(resolution_for(gt, c.role));
    finalize_text(c, res);
    int bi = static_cast<int>(c.band);
    for (int j = bi; j < 3; ++j) {
      int room = band_budget[static_cast<std::size_t>(j)] - used[static_cast<std::size_t>(j)];
      if (c.tokens <= room && c.tokens <= total_budget - used_total) {
        used[static_cast<std::size_t>(j)] += c.tokens;
        used_total += c.tokens;
        model::ContextItem item;
        item.ref_kind = c.ref_kind;
        item.ref = c.ref;
        item.band = static_cast<ContextBand>(j);
        item.resolution = res;
        item.score = c.score;
        item.factors = Json{{"relevance", c.base_relevance}, {"authority", ctx::authority_score(c.origin)},
                            {"freshness", ctx::freshness_score(c.date, anchor_date)}, {"confidence", c.confidence}};
        merge_factors(item.factors, c.extra_factors);
        if (explicit_controls && c.relation_hops) item.factors["relation_hops"] = *c.relation_hops;
        item.tokens = c.tokens;
        item.why = c.why;
        item.text = c.rendered_text;
        accepted_index[item.ref] = accepted.size();
        accepted.push_back(std::move(item));
        return true;
      }
    }
    model::ContextItem d;
    d.ref_kind = c.ref_kind;
    d.ref = c.ref;
    d.band = c.band;
    d.resolution = res;
    d.score = c.score;
    d.tokens = c.tokens;
    d.factors = Json{{"relevance", c.base_relevance}, {"authority", ctx::authority_score(c.origin)},
                     {"freshness", ctx::freshness_score(c.date, anchor_date)}, {"confidence", c.confidence},
                     {"exclusion_reason", "budget"}, {"discovery_reason", c.why}};
    merge_factors(d.factors, c.extra_factors);
    if (explicit_controls && c.relation_hops) d.factors["relation_hops"] = *c.relation_hops;
    d.why = "over budget (needed " + std::to_string(c.tokens) + " tokens, none of the remaining bands had room)";
    dropped.push_back(std::move(d));
    return false;
  };

  // Map from a ref back to its premises, so the dependency closure can add
  // `required_by` even after the candidate objects themselves are gone.
  std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> premises_of;
  for (auto& c : stable_d) premises_of[c.ref] = {c.premise_claims, c.premise_principles};
  for (auto& c : project_d) premises_of[c.ref] = {c.premise_claims, c.premise_principles};
  for (auto& c : goal_d) premises_of[c.ref] = {c.premise_claims, c.premise_principles};

  auto carry_forward = [&](std::size_t band) {
    const int unused = band_budget[band] - used[band];
    band_budget[band] -= unused;
    band_budget[band + 1] += unused;
  };
  for (auto& c : stable_d) try_accept(std::move(c));
  carry_forward(0);
  for (auto& c : project_d) try_accept(std::move(c));
  carry_forward(1);
  for (auto& c : goal_d) try_accept(std::move(c));

  // A premise the closure needs but cannot include must never vanish silently:
  // the pulling item is flagged INCOMPLETE (rendered marker + trace field).
  auto mark_missing = [&](const std::string& puller, const std::string& premise_ref, std::string_view reason) {
    auto pi = accepted_index.find(puller);
    if (pi == accepted_index.end()) return;
    auto& v = accepted[pi->second].missing_premises;
    if (std::find(v.begin(), v.end(), premise_ref) == v.end()) v.push_back(premise_ref);
    diagnostics["missing_premises"].push_back(Json{{"ref", premise_ref}, {"required_by", puller}, {"reason", reason}});
  };

  // ── dependency closure: premises of accepted items are pulled in too ──
  std::set<std::string> expanded;
  // Every accepted reference is expanded once. Newly accepted premises come
  // from the finite run/pack, so cycles terminate without a depth-policy cap.
  while (true) {
    std::vector<std::pair<std::string, std::string>> to_pull;  // (premise ref, puller ref)
    for (auto& [ref, idx] : accepted_index) {
      if (!expanded.insert(ref).second) continue;
      auto it = premises_of.find(ref);
      if (it == premises_of.end()) continue;
      for (auto& pc : it->second.first) to_pull.push_back({pc, ref});
      for (auto& pp : it->second.second) to_pull.push_back({pp, ref});
    }
    if (to_pull.empty()) break;
    bool any_new = false;
    for (auto& [pref, puller] : to_pull) {
      auto exist = accepted_index.find(pref);
      if (exist != accepted_index.end()) {
        auto& v = accepted[exist->second].required_by;
        if (std::find(v.begin(), v.end(), puller) == v.end()) v.push_back(puller);
        continue;
      }
      // Resolve the premise: a claim id ("cl_...") or a principle id.
      Candidate c;
      bool ok = false;
      bool lookup_failed = false;
      auto claim_r = store_.get_claim(run, pref);
      lookup_diagnostic("premise_claim", pref, claim_r);
      lookup_failed = !claim_r;
      if (claim_r && *claim_r) {
        const auto& claim = **claim_r;
        c.ref_kind = model::RefKind::Claim;
        c.ref = claim.id;
        c.band = ContextBand::Goal;
        c.subject = claim.subject;
        c.date = claim.qualifiers.valid_from;
        c.origin = claim.assessment.origin;
        c.confidence = claim.assessment.confidence;
        c.evidence = claim.assessment.evidence;
        c.expected = claim.assessment.expected;
        std::string subj_label = label_of(claim.subject);
        std::string obj_label = claim.object.empty() ? fmt_value(claim.value) : label_of(claim.object);
        c.content.label = subj_label + " " + claim.predicate + " " + obj_label;
        c.content.summary = c.content.label;
        c.content.full = c.content.summary;
        if (req.detail_resolution) {
          if (!claim.qualifiers.valid_from.empty()) c.content.summary += " (" + claim.qualifiers.valid_from + ")";
          c.content.full = c.content.summary;
          if (!claim.assessment.support.empty()) c.content.full += " — \"" + std::string(utf8::prefix(claim.assessment.support.front().quote, 160)) + "\"";
          if (claim.assessment.status == model::ClaimStatus::Contested) c.content.full += " [contested]";
          for (const auto& support : claim.assessment.support) c.content.raw += (c.content.raw.empty() ? "" : " / ") + support.quote;
          if (c.content.raw.empty()) c.content.raw = c.content.full;
          if (claim.assessment.derivation) c.basis = claim.assessment.derivation->op;
          c.fill_query = claim.assessment.open.fill_query;
        }
        c.premise_claims = claim.assessment.premises.claims;
        c.premise_principles = claim.assessment.premises.principles;
        ok = true;
      } else {
        auto pr = store_.get_principle(run, pref);
        lookup_diagnostic("premise_principle", pref, pr);
        lookup_failed = lookup_failed || !pr;
        if (pr && *pr) {
          const auto& p = **pr;
          c.ref_kind = model::RefKind::Principle;
          c.ref = p.id;
          c.band = ContextBand::Project;
          c.is_principle = true;
          c.validation = p.validation;
          c.origin = p.origin;
          c.confidence = p.confidence;
          std::string stmt = ctx::pick_text(p.statement, lang);
          c.content.label = stmt;
          c.content.summary = stmt + " " + ctx::principle_badge(p.validation, p.level, p.form);
          c.content.full = c.content.summary;
          if (req.detail_resolution) {
            if (!p.protects.empty()) c.content.full += " (protects: " + join(p.protects, ", ") + ")";
            if (!p.exceptions.empty()) c.content.full += " (exceptions: " + join(p.exceptions, "; ") + ")";
            c.content.raw = json::dump(p.to_json());
          }
          c.premise_principles = p.derived_from;
          ok = true;
        }
      }
      if (!ok) {
        mark_missing(puller, pref, lookup_failed ? "query_error" : "missing_record");
        continue;
      }
      c.subject = c.subject.empty() ? c.ref : c.subject;
      c.why = "required premise of " + puller;
      c.base_relevance = 1.0;
      c.score = c.base_relevance * ctx::authority_score(c.origin) * ctx::freshness_score(c.date, anchor_date) * c.confidence;
      premises_of[c.ref] = {c.premise_claims, c.premise_principles};
      if (try_accept(std::move(c))) {
        accepted.back().required_by.push_back(puller);
        any_new = true;
      } else {
        mark_missing(puller, pref, "budget");
      }
    }
    if (!any_new) break;
  }

  // A candidate can fail its original band's budget and later fit as a
  // required premise after unused capacity cascades. Keep that earlier event
  // in diagnostics, not in the final "not included" list.
  diagnostics["recovered_budget_drops"] = Json::array();
  dropped.erase(std::remove_if(dropped.begin(), dropped.end(), [&](const auto& item) {
    if (!accepted_index.count(item.ref)) return false;
    diagnostics["recovered_budget_drops"].push_back(item.to_json());
    return true;
  }), dropped.end());

  // Final order: band, then score desc, then ref (header contract).
  std::sort(accepted.begin(), accepted.end(), [](const model::ContextItem& a, const model::ContextItem& b) {
    if (a.band != b.band) return a.band < b.band;
    if (a.score != b.score) return a.score > b.score;
    return a.ref < b.ref;
  });
  std::sort(dropped.begin(), dropped.end(), [](const model::ContextItem& a, const model::ContextItem& b) {
    if (a.band != b.band) return a.band < b.band;
    if (a.score != b.score) return a.score > b.score;
    return a.ref < b.ref;
  });

  finalize_context_evidence(evidence, accepted);
  if (!req.claim_targets.empty()) goal.params["claim_selection"] = evidence.requested;
  if (req.include_counter_evidence) goal.params["counter_evidence"] = evidence.counters;
  if (!retrieved.trace.empty()) goal.params["candidate_retrieval"] = retrieved.trace;

  model::ContextSet set;
  diagnostics["budget"]["final_band_tokens"] = band_budget;
  diagnostics["budget"]["used_band_tokens"] = used;
  diagnostics["budget"]["used_tokens"] = used_total;
  goal.params["retrieval_diagnostics"] = std::move(diagnostics);
  set.goal = goal;
  set.budget_tokens = total_budget;
  set.used_tokens = 0;
  for (auto& it : accepted) set.used_tokens += it.tokens;
  set.pack_hash = pack_->hash();
  set.id = model::ContextSet::make_id(goal.id, total_budget, set.pack_hash);
  if (explicit_controls) {
    set.id = kb::stable_id("cx_", json::dump(Json{{"base_context_id", set.id},
        {"context_controls", goal.params["context_controls"]}}));
  }
  set.items = std::move(accepted);
  set.dropped = std::move(dropped);
  if (extended) {
    set.id.clear();
    set.id = kb::stable_id("cx_", json::dump(Json{{"request", req.to_json()}, {"run", run}, {"context_set", set.to_json()}}));
  }
  return set;
}

// ── rendering ────────────────────────────────────────────────────────

namespace {
std::string band_heading(ContextBand b, std::string_view lang) {
  bool pl = lang == "pl";
  switch (b) {
    case ContextBand::Stable:
      return pl ? "## Stałe (konstytucja, niezmienniki, preferencje)" : "## Stable (constitution, invariants, preferences)";
    case ContextBand::Project:
      return pl ? "## Kontekst projektu" : "## Project context";
    case ContextBand::Goal:
      return pl ? "## Ten cel" : "## This goal";
  }
  return "## Context";
}
}  // namespace

Result<std::string> ContextEngine::render(const model::ContextSet& set) {
  std::string out;
  ContextBand current = ContextBand::Stable;
  bool have_current = false;
  bool any = false;
  for (const auto& item : set.items) {
    if (!have_current || item.band != current) {
      current = item.band;
      have_current = true;
      if (any) out += "\n\n";
      out += band_heading(current, set.goal.params.contains("lang") ? "" : "") + "\n";
      any = true;
    }
    out += "- " + item.text;
    if (const auto* theses = json::find(item.factors, "thesis_ids")) out += " [plan theses: " + json::dump(*theses) + "]";
    if (!item.missing_premises.empty()) {
      out += "  [INCOMPLETE: premises not included:";
      for (const auto& m : item.missing_premises) out += " " + m;
      out += "]";
    }
    if (!item.why.empty()) out += "  <!-- why: " + item.why + " -->";
    out += "\n";
  }
  if (!set.dropped.empty()) {
    out += "\n<!-- " + std::to_string(set.dropped.size()) + " item(s) considered but not included (budget); see trace() -->\n";
  }
  if (const auto* diagnostics = json::find(set.goal.params, "retrieval_diagnostics");
      diagnostics && json::get_string(*diagnostics, "status") == "incomplete") {
    out += "\n[INCOMPLETE: retrieval encountered a store query error or reached a query cap; see retrieval_diagnostics.]\n";
  }
  out += render_context_diagnostics(set);
  return out;
}

Json ContextEngine::trace(const model::ContextSet& set) const {
  std::map<ContextBand, Json> sections;
  for (const auto& item : set.items) {
    Json& arr = sections[item.band];
    if (arr.is_null()) arr = Json::array();
    arr.push_back(item.to_json());
  }
  Json out_sections = Json::array();
  for (auto band : {ContextBand::Stable, ContextBand::Project, ContextBand::Goal}) {
    Json items = sections.count(band) ? sections[band] : Json::array();
    out_sections.push_back(Json{{"band", std::string(model::to_string(band))}, {"items", items}});
  }
  Json dropped = Json::array();
  for (const auto& d : set.dropped) dropped.push_back(d.to_json());
  return Json{{"goal", set.goal.to_json()},
              {"budget_tokens", set.budget_tokens},
              {"used_tokens", set.used_tokens},
              {"sections", out_sections},
              {"dropped", dropped}};
}

Result<Json> ContextEngine::build(const ContextRequest& req) {
  LOOM_TRY_ASSIGN(model::ContextSet set, select(req));
  LOOM_TRY_ASSIGN(std::string prompt, render(set));
  return Json{{"goal", set.goal.to_json()}, {"context_set", set.to_json()}, {"prompt", prompt}};
}

}  // namespace loom::context
