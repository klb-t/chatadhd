#pragma once

#include <string_view>

#include "loom/util/json.h"

namespace loom::extract {

inline constexpr std::string_view kCandidateGraphValidatorVersion = "candidate-graph-native/1";

// Shared by the pack loader and request preflight. Both research and runtime
// experimental status labels are accepted; neither permits promotion.
// Returns {valid,status,errors:[{code,path,message}],version}.
Json validate_candidate_graph_vocabulary(const Json& vocabulary);

// Pure validation of loom.candidate_graph/1 against loom.source_packet/1.
// Vocabulary is explicit policy data (loom.candidate_graph_vocabulary/1).
// Limits may only lower its supported maxima. Invalid model JSON is a rejected
// report, never a partially accepted graph. No HTTP, persistence or inference.
//
// The report retains bounded inputs and, on success, the original local-handle
// Entity/Claim drafts. Their Assessments remain partial; this API does not call
// model::Entity/Claim::from_json or introduce evidence/status/confidence defaults.
// packet_hash is SHA256 of loom::json::canonical(source_packet), identified as
// loom-json-canonical-sha256. Do not assume equality with another JSON encoder's
// digest. snapshot_id is a supplied label; a request envelope must separately
// bind the response to packet_hash. This validator alone does not attest that
// a response was generated for these bytes.
// If input preflight rejects unsafe nesting/size/encoding, retained_input is
// null and retention.status is not_copied: the caller owns original inputs.
// Copying the rejected tree would defeat the resource guard. This report makes
// no claim that the caller persisted those inputs.
Json validate_candidate_graph_bundle(const Json& bundle, const Json& source_packet,
                                     const Json& vocabulary,
                                     const Json& limits = Json::object());

}  // namespace loom::extract
