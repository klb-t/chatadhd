#pragma once

#include <map>
#include <string>
#include <vector>

#include "loom/catalog.h"

namespace loom::catalog::internal {

// A supplied vector channel produces retrieval ranks, never evidence confidence.
// The caller decides how ranks fuse with its other channels and preserves the
// owner's selection overrides. No provider call is made by this module.
struct SemanticCandidateHit {
  double cosine = 0.0;
  double score = 0.0;
  Json evidence = Json::object();
};

struct SemanticCandidates {
  bool enabled = false;
  std::string channel;
  std::string model;
  std::string method;
  std::string configuration_hash;
  Json policy = Json::object();
  double bias = 0.0;
  double weight = 0.0;
  double tau_relevant = 0.0;
  std::string fusion;
  // Absent records are absent hits, rather than a score computed from bias.
  std::map<std::string, SemanticCandidateHit> hits;
};

// config is the VALUE stored at runtime key catalog_semantic_candidates:
// {schema:"loom.catalog_semantic_candidates/1", enabled:true,
//  profile_input_hash, channel, model, method, query:[numbers],
//  records:[{unit_id,content_hash,vector:[numbers]}],
//  policy:{bias,weight,tau_relevant,fusion:"additive"|"union"}}.
// Null or an object without enabled/with enabled:false is a safe no-op.
// Active input is validated completely before any hits are returned.
Result<SemanticCandidates> evaluate_semantic_candidates(
    const Json& config, const std::string& profile_input_hash,
    const std::vector<CatalogUnit>& units);

}  // namespace loom::catalog::internal
