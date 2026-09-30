#include "context_evidence.h"

#include <algorithm>
#include <set>

namespace loom::context {

ContextEvidence gather_context_evidence(kb::KnowledgeStore& store, const std::string& run,
    const ContextRequest& request, const std::vector<std::string>& initial_claims,
    const std::vector<model::EvidenceClass>& admitted_evidence) {
  ContextEvidence result;
  std::map<std::string, model::Claim> available;
  std::map<std::string, std::string> outcomes;
  const auto lookup = [&](const std::string& id) -> std::string {
    if (outcomes.count(id)) return outcomes.at(id);
    auto row = store.get_claim(run, id);
    std::string status;
    if (!row) status = "query_error";
    else if (!*row) status = "missing";
    else if (std::find(admitted_evidence.begin(), admitted_evidence.end(), (**row).assessment.evidence) == admitted_evidence.end()) status = "filtered";
    else {
      status = "candidate";
      available.emplace(id, **row);
    }
    outcomes.emplace(id, status);
    return status;
  };
  std::vector<std::string> roots;
  std::set<std::string> root_ids;
  // Caller order stays inspectable in requested diagnostics.
  for (const auto& id : request.claim_targets) {
    auto status = lookup(id);
    result.requested.push_back(Json{{"ref", id}, {"status", status}});
    if (status == "candidate" && root_ids.insert(id).second) roots.push_back(id);
  }
  for (const auto& id : initial_claims) {
    if (lookup(id) == "candidate" && root_ids.insert(id).second) roots.push_back(id);
  }
  Json entries = Json::array();
  std::set<std::string> seen_observations;
  if (request.include_counter_evidence) {
    for (const auto& id : roots) {
      // Copy before inserting other records into the cache; no inferred links.
      const auto links = available.at(id).assessment.counter;
      Json entry{{"for_claim", id}, {"claims", Json::array()},
          {"observations", Json::array()}, {"gaps", Json::array()}};
      for (const auto& ref : links.claims) {
        const auto status = lookup(ref);
        entry["claims"].push_back(Json{{"ref", ref}, {"status", status}});
        if (status == "candidate") result.counter_for[ref].push_back(id);
      }
      for (const auto& ref : links.observations) {
        auto observation = store.get_observation(run, ref);
        const bool allowed = std::find(admitted_evidence.begin(), admitted_evidence.end(), model::EvidenceClass::Observed) != admitted_evidence.end();
        const std::string status = !observation ? "query_error" : !*observation ? "missing" : !allowed ? "filtered" : "candidate";
        entry["observations"].push_back(Json{{"ref", ref}, {"status", status}});
        if (status == "candidate") {
          result.counter_for[ref].push_back(id);
          if (seen_observations.insert(ref).second) result.observations.push_back(**observation);
        }
      }
      if (links.empty()) entry["gaps"].push_back("no_recorded_counter_links");
      entries.push_back(std::move(entry));
    }
    result.counters = Json{{"status", "linked_records_only"}, {"entries", entries},
        {"scope", "one_recorded_counter_link_from_direct_candidates"},
        {"semantic_contradiction_search", "not_performed"}};
  }
  for (auto& [id, claim] : available) {
    (void)id;
    result.claims.push_back(std::move(claim));
  }
  return result;
}

void finalize_context_evidence(ContextEvidence& evidence, const std::vector<model::ContextItem>& selected) {
  std::set<std::string> claims, observations;
  for (const auto& item : selected) {
    if (item.ref_kind == model::RefKind::Claim || item.ref_kind == model::RefKind::Decision) claims.insert(item.ref);
    if (item.ref_kind == model::RefKind::Observation) observations.insert(item.ref);
  }
  const auto finish = [&](Json& records, const std::set<std::string>& refs) {
    for (auto& record : records) {
      if (record["status"] == "candidate") record["status"] = refs.count(record["ref"].get<std::string>()) ? "selected" : "over_budget";
      else if (record["status"] == "filtered" && refs.count(record["ref"].get<std::string>())) {
        record["eligibility"] = "filtered";
        record["status"] = "selected";
        record["selection_path"] = "required_premise_exception";
      }
    }
  };
  finish(evidence.requested, claims);
  if (evidence.counters.contains("entries")) {
    bool complete = true;
    for (auto& entry : evidence.counters["entries"]) {
      finish(entry["claims"], claims);
      finish(entry["observations"], observations);
      for (const auto& key : {"claims", "observations"}) {
        for (const auto& record : entry[key]) {
          if (record["status"] != "selected") complete = false;
        }
      }
      if (!entry["gaps"].empty()) complete = false;
    }
    evidence.counters["recorded_links_complete"] = complete;
    // Deliberately no blanket `complete=true`: linked coverage cannot prove
    // the absence of other, unrecorded counter-evidence.
  }
}
}
