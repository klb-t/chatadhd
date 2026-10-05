// Explicit, read-only candidate instruments for ContextEngine. These operate
// on a caller-supplied corpus, not only graph-reachable candidates. No factory
// discovers credentials or installs a remote provider.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "loom/kb.h"
#include "loom/resolve.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::context {

struct CandidateChannelRequest {
  std::string id;
  int limit = 50;          // positive, caller-configurable result count
  double min_score = 0;  // finite instrument-score threshold; exclusive
  static Result<CandidateChannelRequest> from_json(const Json& j);
  Json to_json() const;
};

struct RetrievalDocument {
  std::string ref;   // existing claim identifier; retrieval never creates claims
  std::string text;  // full searchable projection, independent of render detail
};

struct RetrievalHit {
  std::string ref;
  double score = 0;
  Json to_json() const;
};

struct RetrievalBatch {
  std::string method;
  // ok: scores measured (possibly all zero); unavailable: no instrument;
  // error: instrument failed; unrepresentable: no usable query representation.
  std::string status = "ok";
  std::string reason;
  std::size_t corpus_count = 0;
  std::size_t scored_count = 0;
  std::size_t unrepresentable_count = 0;
  std::vector<std::string> unrepresentable_refs;  // documents with no measured score
  std::size_t zero_score_count = 0;
  std::size_t eligible_count = 0;  // score > request.min_score before result limit
  bool truncated = false;
  // All successfully measured scores (including zero), in rank order. Missing
  // representations have no measurement, rather than a fabricated zero.
  std::vector<RetrievalHit> scores;
  std::vector<RetrievalHit> hits;
  Json to_json() const;
};

class CandidateChannel {
 public:
  virtual ~CandidateChannel() = default;
  virtual RetrievalBatch retrieve(std::string_view query,
      const std::vector<RetrievalDocument>& corpus, const CandidateChannelRequest& request) = 0;
};

// Caller-owned capability injection: the space may be local or provider-backed;
// the caller must authorize/account any provider before injecting it. Factories
// below never choose an embedder or silently fall back after an error.
std::shared_ptr<CandidateChannel> make_vector_candidate_channel(std::shared_ptr<resolve::VectorSpace> space);
std::shared_ptr<CandidateChannel> make_tfidf_candidate_channel(std::shared_ptr<const kb::Pack> pack);
std::shared_ptr<CandidateChannel> make_lexical_candidate_channel(std::shared_ptr<const kb::Pack> pack);
// Exact normalized token overlap, independent of vector results. The caller
// compares hit identifiers to diagnose omissions; it does not veto selection.
RetrievalBatch lexical_shadow(std::shared_ptr<const kb::Pack> pack, std::string_view query,
    const std::vector<RetrievalDocument>& corpus, const CandidateChannelRequest& request);

}  // namespace loom::context
