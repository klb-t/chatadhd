#include "loom/context_retrieval.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <set>

namespace loom::context {
namespace {

bool valid_request(const CandidateChannelRequest& request) {
  return !request.id.empty() && request.limit > 0 && std::isfinite(request.min_score);
}

bool valid_corpus(const std::vector<RetrievalDocument>& corpus) {
  std::set<std::string> refs;
  for (const auto& document : corpus) {
    if (document.ref.empty() || !refs.insert(document.ref).second) return false;
  }
  return true;
}

RetrievalBatch failure(std::string method, std::size_t count, std::string status, std::string reason) {
  RetrievalBatch out;
  out.method = std::move(method);
  out.corpus_count = count;
  out.status = std::move(status);
  out.reason = std::move(reason);
  return out;
}

void finish(RetrievalBatch& batch, const CandidateChannelRequest& request) {
  std::sort(batch.scores.begin(), batch.scores.end(), [](const auto& a, const auto& b) {
    return a.score != b.score ? a.score > b.score : a.ref < b.ref;
  });
  batch.scored_count = batch.scores.size();
  for (const auto& score : batch.scores) {
    if (score.score == 0) ++batch.zero_score_count;
    if (score.score <= request.min_score) continue;
    ++batch.eligible_count;
    if (batch.hits.size() < static_cast<std::size_t>(request.limit)) batch.hits.push_back(score);
  }
  batch.truncated = batch.hits.size() < batch.eligible_count;
}

// 0: no usable vector, 1: nonzero finite vector, -1: invalid instrument output.
// Rescale its direction before cosine squares the values. Even finite provider
// values can overflow/underflow when squared; magnitude is not availability.
int prepare_vector(resolve::SparseVec& vector) {
  double maximum = 0;
  for (const auto& [key, value] : vector) {
    (void)key;
    if (!std::isfinite(value)) return -1;
    maximum = std::max(maximum, std::abs(value));
  }
  if (maximum == 0) return 0;
  for (auto& [key, value] : vector) {
    (void)key;
    value /= maximum;
  }
  return 1;
}

class VectorCandidateChannel final : public CandidateChannel {
 public:
  explicit VectorCandidateChannel(std::shared_ptr<resolve::VectorSpace> space) : space_(std::move(space)) {}
  // Only this private, deterministic built-in owns a reusable corpus. Injected
  // VectorSpaces may be stateful or remote and always keep the original path.
  explicit VectorCandidateChannel(std::shared_ptr<const kb::Pack> pack) : tfidf_pack_(std::move(pack)) {}

  RetrievalBatch retrieve(std::string_view query, const std::vector<RetrievalDocument>& corpus,
                          const CandidateChannelRequest& request) override {
    if (!valid_request(request)) return failure("vector", corpus.size(), "error", "invalid_request");
    if (!valid_corpus(corpus)) return failure("vector", corpus.size(), "error", "invalid_corpus_references");
    std::lock_guard<std::mutex> guard(mutex_);
    std::string method = "vector";
    try {
      // Laziness keeps unused default chat/context paths free of normalizer
      // construction. The built-in's pack cannot change during its lifetime.
      if (!space_ && tfidf_pack_) space_ = resolve::make_tfidf_space(tfidf_pack_);
      if (!space_) return failure("vector", corpus.size(), "unavailable", "vector_space_not_installed");
      method = space_->method();
      const auto modalities = space_->modalities();
      if (std::find(modalities.begin(), modalities.end(), "text") == modalities.end()) {
        return failure(method, corpus.size(), "unavailable", "text_modality_unavailable");
      }
      if (query.empty()) return failure(method, corpus.size(), "unrepresentable", "empty_query");
      RetrievalBatch batch;
      batch.method = method;
      batch.corpus_count = corpus.size();
      if (corpus.empty()) {
        cached_.reset();
        return batch;
      }
      if (cached_ && same_corpus(corpus, cached_->documents)) {
        auto query_vectors = space_->vectors({{cached_->query_id, "text", std::string(query), "", ""}});
        if (!query_vectors) return failure(method, corpus.size(), "error", "vectorization_failed:" + std::string(errc_name(query_vectors.error().code)));
        if (query_vectors->size() != 1) return failure(method, corpus.size(), "error", "vector_count_mismatch");
        const int state = prepare_vector(query_vectors->front());
        if (state < 0) return failure(method, corpus.size(), "error", "invalid_vector_values");
        if (state == 0) return failure(method, corpus.size(), "unrepresentable", "empty_query_vector");
        return score(method, corpus, cached_->vectors, cached_->states, query_vectors->front(), request);
      }
      // Never reuse a previous fit after any changed document id/text/order.
      // Store reads and corpus-query failures remain outside this cache.
      cached_.reset();
      std::vector<resolve::EmbedInput> inputs;
      inputs.reserve(corpus.size() + 1);
      std::set<std::string> input_ids;
      for (const auto& document : corpus) {
        inputs.push_back({document.ref, "text", document.text, "", ""});
        input_ids.insert(document.ref);
      }
      const auto fit = space_->fit(inputs);
      if (!fit) return failure(method, corpus.size(), "error", "vector_fit_failed:" + std::string(errc_name(fit.error().code)));
      std::string query_id = "query";
      while (input_ids.count(query_id)) query_id += ":";
      inputs.push_back({query_id, "text", std::string(query), "", ""});
      auto vectors = space_->vectors(inputs);
      if (!vectors) return failure(method, corpus.size(), "error", "vectorization_failed:" + std::string(errc_name(vectors.error().code)));
      if (vectors->size() != inputs.size()) return failure(method, corpus.size(), "error", "vector_count_mismatch");
      std::vector<int> vector_states;
      vector_states.reserve(vectors->size());
      for (auto& vector : *vectors) {
        vector_states.push_back(prepare_vector(vector));
        if (vector_states.back() < 0) return failure(method, corpus.size(), "error", "invalid_vector_values");
      }
      const auto& query_vector = vectors->back();
      if (vector_states.back() == 0) return failure(method, corpus.size(), "unrepresentable", "empty_query_vector");
      batch = score(method, corpus, *vectors, vector_states, query_vector, request);
      if (tfidf_pack_ && batch.status == "ok") {
        // At most one copied corpus and its normalized vectors are retained.
        // Scores/ranks/thresholds/query vectors and negative outcomes are not.
        cached_ = CachedCorpus{corpus, {vectors->begin(), vectors->end() - 1},
            {vector_states.begin(), vector_states.end() - 1}, std::move(query_id)};
      }
      return batch;
    } catch (const std::exception&) {
      // Capability implementations can be third-party code. Do not copy an
      // exception/provider response into a context trace (it can contain secrets).
      return failure(method, corpus.size(), "error", "vector_instrument_exception");
    } catch (...) {
      return failure(method, corpus.size(), "error", "vector_instrument_exception");
    }
  }

 private:
  struct CachedCorpus {
    std::vector<RetrievalDocument> documents;
    std::vector<resolve::SparseVec> vectors;
    std::vector<int> states;
    std::string query_id;
  };

  static bool same_corpus(const std::vector<RetrievalDocument>& a, const std::vector<RetrievalDocument>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const auto& x, const auto& y) {
      return x.ref == y.ref && x.text == y.text;
    });
  }

  static RetrievalBatch score(const std::string& method, const std::vector<RetrievalDocument>& corpus,
      const std::vector<resolve::SparseVec>& vectors, const std::vector<int>& states,
      const resolve::SparseVec& query_vector, const CandidateChannelRequest& request) {
    RetrievalBatch batch;
    batch.method = method;
    batch.corpus_count = corpus.size();
    for (std::size_t i = 0; i < corpus.size(); ++i) {
      if (states[i] == 0) {
        ++batch.unrepresentable_count;
        batch.unrepresentable_refs.push_back(corpus[i].ref);
        continue;
      }
      const double value = resolve::cosine(vectors[i], query_vector);
      if (!std::isfinite(value)) return failure(method, corpus.size(), "error", "invalid_cosine");
      batch.scores.push_back({corpus[i].ref, std::clamp(value, -1.0, 1.0)});
    }
    finish(batch, request);
    return batch;
  }

  std::shared_ptr<resolve::VectorSpace> space_;
  std::shared_ptr<const kb::Pack> tfidf_pack_;
  std::optional<CachedCorpus> cached_;
  std::mutex mutex_;
};

class LexicalCandidateChannel final : public CandidateChannel {
 public:
  explicit LexicalCandidateChannel(std::shared_ptr<const kb::Pack> pack) : pack_(std::move(pack)) {}

  RetrievalBatch retrieve(std::string_view query, const std::vector<RetrievalDocument>& corpus,
                          const CandidateChannelRequest& request) override {
    constexpr auto method = "lexical_token_overlap";
    if (!valid_request(request)) return failure(method, corpus.size(), "error", "invalid_request");
    if (!valid_corpus(corpus)) return failure(method, corpus.size(), "error", "invalid_corpus_references");
    if (!pack_) return failure(method, corpus.size(), "unavailable", "normalizer_pack_not_installed");
    const kb::Normalizer normalizer(*pack_);
    const auto tokens = [&](std::string_view text) {
      const auto words = normalizer.tokens(normalizer.fold(text));
      return std::set<std::string>(words.begin(), words.end());
    };
    const auto query_tokens = tokens(query);
    if (query_tokens.empty()) return failure(method, corpus.size(), "unrepresentable", "empty_query_tokens");
    RetrievalBatch batch;
    batch.method = method;
    batch.corpus_count = corpus.size();
    for (const auto& document : corpus) {
      const auto document_tokens = tokens(document.text);
      if (document_tokens.empty()) {
        ++batch.unrepresentable_count;
        batch.unrepresentable_refs.push_back(document.ref);
        continue;
      }
      std::size_t overlap = 0;
      for (const auto& token : query_tokens) {
        if (document_tokens.count(token)) ++overlap;
      }
      batch.scores.push_back({document.ref, static_cast<double>(overlap) / static_cast<double>(query_tokens.size())});
    }
    finish(batch, request);
    return batch;
  }

 private:
  std::shared_ptr<const kb::Pack> pack_;
};

}  // namespace

Result<CandidateChannelRequest> CandidateChannelRequest::from_json(const Json& j) {
  if (!j.is_object()) return Error(Errc::InvalidArgument, "candidate channel must be an object");
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (it.key() != "id" && it.key() != "limit" && it.key() != "min_score") {
      return Error(Errc::InvalidArgument, "unknown candidate channel field: " + it.key());
    }
  }
  if (!j.contains("id") || !j["id"].is_string() || j["id"].get<std::string>().empty()) {
    return Error(Errc::InvalidArgument, "candidate channel id must be a nonempty string");
  }
  CandidateChannelRequest out;
  out.id = j["id"].get<std::string>();
  if (j.contains("limit")) {
    const auto& limit = j["limit"];
    if (!limit.is_number_integer() || (limit.is_number_unsigned() && limit.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) ||
        (!limit.is_number_unsigned() && (limit.get<std::int64_t>() <= 0 || limit.get<std::int64_t>() > std::numeric_limits<int>::max()))) {
      return Error(Errc::InvalidArgument, "candidate channel limit must be a positive native integer");
    }
    out.limit = limit.get<int>();
    if (out.limit <= 0) return Error(Errc::InvalidArgument, "candidate channel limit must be positive");
  }
  if (j.contains("min_score")) {
    if (!j["min_score"].is_number() || !std::isfinite(j["min_score"].get<double>())) {
      return Error(Errc::InvalidArgument, "candidate channel min_score must be finite");
    }
    out.min_score = j["min_score"].get<double>();
  }
  return out;
}

Json CandidateChannelRequest::to_json() const {
  return Json{{"id", id}, {"limit", limit}, {"min_score", min_score}};
}
Json RetrievalHit::to_json() const { return Json{{"ref", ref}, {"score", score}}; }
Json RetrievalBatch::to_json() const {
  Json score_json = Json::array(), hit_json = Json::array();
  for (const auto& score : scores) score_json.push_back(score.to_json());
  for (const auto& hit : hits) hit_json.push_back(hit.to_json());
  return Json{{"method", method}, {"status", status}, {"reason", reason},
              {"corpus_count", corpus_count}, {"scored_count", scored_count},
              {"unrepresentable_count", unrepresentable_count}, {"unrepresentable_refs", unrepresentable_refs},
              {"zero_score_count", zero_score_count},
              {"eligible_count", eligible_count}, {"truncated", truncated},
              {"scores", std::move(score_json)}, {"hits", std::move(hit_json)}};
}
std::shared_ptr<CandidateChannel> make_vector_candidate_channel(std::shared_ptr<resolve::VectorSpace> space) {
  return std::make_shared<VectorCandidateChannel>(std::move(space));
}
std::shared_ptr<CandidateChannel> make_tfidf_candidate_channel(std::shared_ptr<const kb::Pack> pack) {
  return std::make_shared<VectorCandidateChannel>(std::move(pack));
}
std::shared_ptr<CandidateChannel> make_lexical_candidate_channel(std::shared_ptr<const kb::Pack> pack) {
  return std::make_shared<LexicalCandidateChannel>(std::move(pack));
}
RetrievalBatch lexical_shadow(std::shared_ptr<const kb::Pack> pack, std::string_view query,
    const std::vector<RetrievalDocument>& corpus, const CandidateChannelRequest& request) {
  return LexicalCandidateChannel(std::move(pack)).retrieve(query, corpus, request);
}

}  // namespace loom::context
