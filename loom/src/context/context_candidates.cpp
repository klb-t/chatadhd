#include "context_candidates.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace loom::context {

ContextCandidates gather_context_candidates(kb::KnowledgeStore& store, const std::string& run,
    std::shared_ptr<const kb::Pack> pack, const ContextRequest& request,
    const std::vector<model::EvidenceClass>& admitted_evidence,
    const std::vector<std::string>& graph_claims,
    const std::map<std::string, std::shared_ptr<CandidateChannel>>& installed) {
  ContextCandidates result;
  if (request.candidate_channels.empty() && !request.lexical_shadow) return result;
  result.trace = Json{{"run", run}, {"scan_limit", request.candidate_scan_limit},
      {"channels", Json::array()}, {"corpus_status", "ok"}, {"possibly_truncated", false},
      {"evidence_boundary", "candidate_similarity_is_not_support_truth_or_identity"}};
  kb::ClaimQuery query;
  query.limit = request.candidate_scan_limit;
  const auto rows = store.query_claims(run, query);
  std::map<std::string, model::Claim> corpus_claims;
  std::vector<RetrievalDocument> corpus;
  std::map<std::string, std::string> labels;
  int label_errors = 0;
  const auto label = [&](const std::string& id) -> std::string {
    if (id.empty()) return "";
    if (labels.count(id)) return labels.at(id);
    const auto value = store.get_entity(run, id);
    if (!value) ++label_errors;
    const auto text = value && *value && !(**value).label.empty() ? (**value).label : id;
    labels.emplace(id, text);
    return text;
  };
  std::size_t filtered = 0;
  if (!rows) {
    result.trace["corpus_status"] = "query_error";
  } else {
    result.trace["scanned_claims"] = rows->size();
    result.trace["possibly_truncated"] = rows->size() >= static_cast<std::size_t>(request.candidate_scan_limit);
    for (const auto& claim : *rows) {
      if (std::find(admitted_evidence.begin(), admitted_evidence.end(), claim.assessment.evidence) == admitted_evidence.end()) {
        ++filtered;
        continue;
      }
      std::string text = label(claim.subject) + " " + claim.predicate + " ";
      text += claim.object.empty() ? (claim.value.is_string() ? claim.value.get<std::string>() : json::dump(claim.value)) : label(claim.object);
      for (const auto& support : claim.assessment.support) text += "\n" + support.quote;
      corpus.push_back({claim.id, std::move(text)});
      corpus_claims.emplace(claim.id, claim);
    }
  }
  result.trace["admitted_claims"] = corpus.size();
  result.trace["filtered_claims"] = filtered;
  result.trace["entity_label_query_errors"] = label_errors;
  std::set<std::string> included;
  std::set<std::string> primary(graph_claims.begin(), graph_claims.end());
  for (const auto& spec : request.candidate_channels) {
    std::shared_ptr<CandidateChannel> channel;
    auto found = installed.find(spec.id);
    if (found != installed.end()) channel = found->second;
    else if (spec.id == "tfidf") channel = make_tfidf_candidate_channel(pack);
    else if (spec.id == "lexical") channel = make_lexical_candidate_channel(pack);
    RetrievalBatch batch;
    batch.method = spec.id;
    batch.corpus_count = corpus.size();
    if (!rows) {
      batch.status = "error";
      batch.reason = "candidate corpus query failed";
    } else if (!channel) {
      batch.status = "unavailable";
      batch.reason = "candidate instrument is not installed";
    } else {
      try {
        batch = channel->retrieve(request.text, corpus, spec);
      } catch (...) {
        batch.status = "error";
        batch.reason = "candidate instrument raised an exception";
        batch.scores.clear();
        batch.hits.clear();
      }
    }
    Json invalid_hits = Json::array();
    Json accepted_hits = Json::array();
    if (batch.status == "ok") {
      std::set<std::string> seen;
      auto ranked = batch.hits;
      // Reject non-finite values before comparison (NaN breaks strict ordering).
      ranked.erase(std::remove_if(ranked.begin(), ranked.end(), [&](const auto& hit) {
        if (std::isfinite(hit.score)) return false;
        invalid_hits.push_back(Json{{"ref", hit.ref}, {"reason", "nonfinite_score"}});
        return true;
      }), ranked.end());
      std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.score != b.score ? a.score > b.score : a.ref < b.ref;
      });
      int rank = 0;
      for (const auto& hit : ranked) {
        if (!corpus_claims.count(hit.ref) || !seen.insert(hit.ref).second) {
          invalid_hits.push_back(Json{{"ref", hit.ref}, {"reason", "invalid_or_duplicate_corpus_hit"}});
          continue;
        }
        if (hit.score <= spec.min_score || rank >= spec.limit) {
          invalid_hits.push_back(Json{{"ref", hit.ref}, {"reason", "outside_requested_threshold_or_limit"}});
          continue;
        }
        ++rank;
        accepted_hits.push_back(hit.to_json());
        if (included.insert(hit.ref).second) result.claims.push_back(corpus_claims.at(hit.ref));
        if (batch.method != "lexical_token_overlap") primary.insert(hit.ref);
        auto& factors = result.factors[hit.ref];
        if (!factors.is_object()) factors = Json::object();
        factors["candidate_channels"][spec.id] = Json{{"method", batch.method}, {"score", hit.score},
            {"rank", rank}, {"selection_relevance", 1.0 / rank}, {"ranking_signal", "reciprocal_channel_rank"}};
        factors["direct_goal_candidate"] = true;
      }
    }
    Json record = batch.to_json();
    record["id"] = spec.id;
    record["request"] = spec.to_json();
    record["invalid_hits"] = invalid_hits;
    record["accepted_hits"] = accepted_hits;
    result.trace["channels"].push_back(std::move(record));
  }
  if (request.lexical_shadow) {
    CandidateChannelRequest spec;
    spec.id = "lexical";
    spec.limit = request.candidate_scan_limit;
    auto shadow = lexical_shadow(pack, request.text, corpus, spec);
    if (!rows) {
      shadow.status = "error";
      shadow.reason = "candidate corpus query failed";
    }
    Json gaps = Json::array();
    if (shadow.status == "ok") {
      for (const auto& hit : shadow.hits) {
        if (!primary.count(hit.ref)) gaps.push_back(Json{{"ref", hit.ref}, {"score", hit.score}, {"verification", "undecided"}});
      }
    }
    result.trace["lexical_shadow"] = shadow.to_json();
    result.trace["lexical_shadow"]["diagnostic_gap"] = gaps;
    result.trace["lexical_shadow"]["comparison"] = "lexical_hits_minus_graph_and_nonlexical_channel_hits_before_selection";
    result.trace["lexical_shadow"]["selection_effect"] = "none";
  }
  return result;
}
}
