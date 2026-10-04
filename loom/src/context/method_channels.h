#pragma once

#include <map>
#include <string>
#include <string_view>

#include "loom/context_retrieval.h"

namespace loom::context {

// Available native arithmetic/instrument implementations only. The caller
// supplies every method identity, version, selection, parameter and preset.
Json method_channel_capabilities();
Json method_fusion_capabilities();

// Native operation IDs, not method/profile IDs or a default method catalogue.
// lexical/tfidf delegate to the existing offline instruments. bm25 requires
// finite caller k1,b. regex requires patterns:[string], flags:[string] with
// exactly one grammar, match:"search"|"full", and semantics:
// "any_pattern"|"all_patterns"|"pattern_hits"|"match_count".
// graph_pool requires scope:"graph_claim_ids" and a finite membership_score;
// only the supplied, actually gathered graph claim IDs receive measurements.
// Request limit/min_score are consumed by retrieve(), not invented here.
Result<std::shared_ptr<CandidateChannel>> method_channel(
    std::string_view capability, const Json& actual_parameters,
    std::shared_ptr<const kb::Pack> pack,
    const std::vector<std::string>& graph_claim_ids);

// plan is MethodRegistry::resolve() output. Trace is an array of batches or
// {channels:[...]}; each successful record names leaf_index (preferred), or a
// unique leaf.channel_id through id/channel_id. accepted_hits are authoritative
// when present; otherwise hits. Missing/unavailable/error batches contribute no
// measurement, including to means. Duplicate paths are separate occurrences.
// Fusion descriptors require operation and signal (raw_score|reciprocal_rank).
// Operations sum/max/min/mean/rrf are native arithmetic, not profile presets.
// rrf also requires finite rrf_constant, and signal:reciprocal_rank. Singular
// denominators and non-finite arithmetic are errors, never clipped scores.
// mean averages weighted contributions over actually measured children; it
// does not divide by signed weights or count missing instruments as zeros.
// Nested combination fusions use path[].member_index and per-member weights;
// weights are applied after the child's fusion. Flat leaves use leaf.weight.
Result<std::map<std::string, double>> fuse_method_results(
    const Json& plan, const Json& batch_trace);

}  // namespace loom::context
