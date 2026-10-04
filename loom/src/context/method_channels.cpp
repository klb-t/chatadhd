#include "method_channels.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <regex>
#include <set>

namespace loom::context {
namespace {

Error invalid(std::string message) { return Error(Errc::InvalidArgument, std::move(message)); }
Result<double> number(const Json& value, std::string_view name) {
  if (!value.is_number()) return invalid(std::string(name) + " must be finite numeric data");
  const auto result = value.get<double>();
  if (!std::isfinite(result)) return invalid(std::string(name) + " must be finite numeric data");
  return result;
}
Result<double> required_number(const Json& object, std::string_view name) {
  const auto* value = json::find(object, name);
  if (!value) return invalid(std::string(name) + " is required caller data");
  return number(*value, name);
}
Result<double> finite_result(long double value) {
  const auto result = static_cast<double>(value);
  if (!std::isfinite(value) || !std::isfinite(result)) return invalid("method score arithmetic overflow");
  return result;
}
RetrievalBatch failure(std::string method, std::size_t count, std::string reason) {
  RetrievalBatch result;
  result.method = std::move(method);
  result.corpus_count = count;
  result.status = "error";
  result.reason = std::move(reason);
  return result;
}
Status validate(const std::vector<RetrievalDocument>& corpus, const CandidateChannelRequest& request) {
  if (request.id.empty() || request.limit <= 0 || !std::isfinite(request.min_score)) return invalid("invalid_request");
  std::set<std::string> refs;
  for (const auto& document : corpus)
    if (document.ref.empty() || !refs.insert(document.ref).second) return invalid("invalid_corpus_references");
  return {};
}
void finish(RetrievalBatch& result, const CandidateChannelRequest& request) {
  std::sort(result.scores.begin(), result.scores.end(), [](const auto& a, const auto& b) {
    return a.score != b.score ? a.score > b.score : a.ref < b.ref;
  });
  result.scored_count = result.scores.size();
  for (const auto& score : result.scores) {
    if (score.score == 0) ++result.zero_score_count;
    if (score.score <= request.min_score) continue;
    ++result.eligible_count;
    if (result.hits.size() < static_cast<std::size_t>(request.limit)) result.hits.push_back(score);
  }
  result.truncated = result.hits.size() < result.eligible_count;
}

class PoolChannel final : public CandidateChannel {
 public:
  PoolChannel(std::vector<std::string> refs, double score) : refs_(refs.begin(), refs.end()), score_(score) {}
  RetrievalBatch retrieve(std::string_view, const std::vector<RetrievalDocument>& corpus,
                          const CandidateChannelRequest& request) override {
    auto valid = validate(corpus, request);
    if (!valid) return failure("graph_pool", corpus.size(), valid.error().message);
    RetrievalBatch result;
    result.method = "graph_pool";
    result.corpus_count = corpus.size();
    for (const auto& document : corpus)
      if (refs_.contains(document.ref)) result.scores.push_back({document.ref, score_});
    finish(result, request);
    return result;
  }
 private:
  std::set<std::string> refs_;
  double score_;
};

class RegexChannel final : public CandidateChannel {
 public:
  RegexChannel(std::vector<std::regex> patterns, std::string mode, std::string semantics)
      : patterns_(std::move(patterns)), mode_(std::move(mode)), semantics_(std::move(semantics)) {}
  RetrievalBatch retrieve(std::string_view, const std::vector<RetrievalDocument>& corpus,
                          const CandidateChannelRequest& request) override {
    auto valid = validate(corpus, request);
    if (!valid) return failure("regex", corpus.size(), valid.error().message);
    try {
      RetrievalBatch result;
      result.method = "regex";
      result.corpus_count = corpus.size();
      for (const auto& document : corpus) {
        std::size_t hits = 0;
        long double matches = 0;
        for (const auto& pattern : patterns_) {
          const bool matched = mode_ == "full" ? std::regex_match(document.text, pattern)
                                               : std::regex_search(document.text, pattern);
          if (matched) ++hits;
          if (semantics_ == "match_count" && matched) {
            if (mode_ == "full") ++matches;
            else for (std::sregex_iterator it(document.text.begin(), document.text.end(), pattern), end; it != end; ++it) ++matches;
          }
        }
        long double score = matches;
        if (semantics_ == "any_pattern") score = hits != 0 ? 1 : 0;
        else if (semantics_ == "all_patterns") score = hits == patterns_.size() ? 1 : 0;
        else if (semantics_ == "pattern_hits") score = hits;
        auto measured = finite_result(score);
        if (!measured) return failure("regex", corpus.size(), measured.error().message);
        result.scores.push_back({document.ref, *measured});
      }
      finish(result, request);
      return result;
    } catch (const std::exception&) { return failure("regex", corpus.size(), "regex_execution_failed"); }
  }
 private:
  std::vector<std::regex> patterns_;
  std::string mode_, semantics_;
};

class Bm25Channel final : public CandidateChannel {
 public:
  Bm25Channel(std::shared_ptr<const kb::Pack> pack, double k1, double b) : pack_(std::move(pack)), k1_(k1), b_(b) {}
  RetrievalBatch retrieve(std::string_view query, const std::vector<RetrievalDocument>& corpus,
                          const CandidateChannelRequest& request) override {
    auto valid = validate(corpus, request);
    if (!valid) return failure("bm25", corpus.size(), valid.error().message);
    if (!pack_) { auto result = failure("bm25", corpus.size(), "normalizer_pack_not_installed"); result.status = "unavailable"; return result; }
    try {
      const kb::Normalizer normalizer(*pack_);
      const auto tokens = [&](std::string_view text) { return normalizer.tokens(normalizer.fold(text)); };
      const auto query_tokens = tokens(query);
      if (query_tokens.empty()) { auto result = failure("bm25", corpus.size(), "empty_query_tokens"); result.status = "unrepresentable"; return result; }
      RetrievalBatch result;
      result.method = "bm25";
      result.corpus_count = corpus.size();
      if (corpus.empty()) return result;
      std::vector<std::map<std::string, std::size_t>> frequencies;
      std::vector<std::size_t> lengths;
      std::map<std::string, std::size_t> document_frequency;
      long double total_length = 0;
      for (const auto& document : corpus) {
        const auto terms = tokens(document.text);
        std::map<std::string, std::size_t> counts;
        for (const auto& term : terms) ++counts[term];
        for (const auto& [term, count] : counts) { (void)count; ++document_frequency[term]; }
        frequencies.push_back(std::move(counts));
        lengths.push_back(terms.size());
        total_length += terms.size();
      }
      const auto average = total_length / corpus.size();
      for (std::size_t i = 0; i < corpus.size(); ++i) {
        if (lengths[i] == 0) { ++result.unrepresentable_count; result.unrepresentable_refs.push_back(corpus[i].ref); continue; }
        long double score = 0;
        for (const auto& term : query_tokens) {
          const auto found = frequencies[i].find(term);
          if (found == frequencies[i].end()) continue;
          const long double frequency = found->second;
          const long double df = document_frequency.at(term);
          const auto idf = std::log1p((static_cast<long double>(corpus.size()) - df + 0.5L) / (df + 0.5L));
          const auto denominator = frequency + static_cast<long double>(k1_) *
              (1.0L - b_ + static_cast<long double>(b_) * lengths[i] / average);
          if (denominator == 0) return failure("bm25", corpus.size(), "bm25_singular_parameters");
          score += idf * frequency * (static_cast<long double>(k1_) + 1.0L) / denominator;
        }
        auto measured = finite_result(score);
        if (!measured) return failure("bm25", corpus.size(), measured.error().message);
        result.scores.push_back({corpus[i].ref, *measured});
      }
      finish(result, request);
      return result;
    } catch (const std::exception&) { return failure("bm25", corpus.size(), "bm25_execution_failed"); }
  }
 private:
  std::shared_ptr<const kb::Pack> pack_;
  double k1_, b_;
};

using Scores = std::map<std::string, double>;
struct Fusion { std::string operation, signal; double constant = 0; };
Result<Fusion> fusion(const Json& descriptor) {
  if (!descriptor.is_object()) return invalid("fusion requires an explicit operation and signal object");
  Fusion result{json::get_string(descriptor, "operation"), json::get_string(descriptor, "signal"), 0};
  if (result.operation != "sum" && result.operation != "max" && result.operation != "min" &&
      result.operation != "mean" && result.operation != "rrf") return Error(Errc::NotImplemented, "fusion operation unavailable");
  if (result.signal != "raw_score" && result.signal != "reciprocal_rank") return Error(Errc::NotImplemented, "fusion signal unavailable");
  if (result.operation == "rrf") {
    if (result.signal != "reciprocal_rank") return invalid("rrf requires reciprocal_rank signal");
    LOOM_TRY_ASSIGN(result.constant, required_number(descriptor, "rrf_constant"));
  }
  return result;
}
Result<Scores> aggregate(const std::vector<std::pair<double, Scores>>& children, const Fusion& spec) {
  Scores result;
  std::map<std::string, std::size_t> counts;
  for (const auto& [weight, raw] : children) {
    std::vector<std::pair<std::string, double>> ranked(raw.begin(), raw.end());
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
      return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    for (std::size_t i = 0; i < ranked.size(); ++i) {
      auto signal = ranked[i].second;
      if (spec.signal == "reciprocal_rank") {
        const auto denominator = static_cast<long double>(i + 1) + (spec.operation == "rrf" ? spec.constant : 0);
        if (denominator == 0) return invalid("fusion reciprocal rank denominator is zero");
        LOOM_TRY_ASSIGN(signal, finite_result(1.0L / denominator));
      }
      LOOM_TRY_ASSIGN(auto contribution, finite_result(static_cast<long double>(weight) * signal));
      const auto& ref = ranked[i].first;
      auto [it, first] = result.try_emplace(ref, contribution);
      if (!first) {
        if (spec.operation == "max") it->second = std::max(it->second, contribution);
        else if (spec.operation == "min") it->second = std::min(it->second, contribution);
        else { LOOM_TRY_ASSIGN(it->second, finite_result(static_cast<long double>(it->second) + contribution)); }
      }
      ++counts[ref];
    }
  }
  if (spec.operation == "mean") for (auto& [ref, score] : result) score /= static_cast<double>(counts.at(ref));
  return result;
}
struct Node {
  double weight = 1;
  std::optional<Fusion> operation;
  Scores scores;
  std::map<std::size_t, std::unique_ptr<Node>> children;
  bool terminal = false;
  Json membership = nullptr;
};
Result<Scores> evaluate(const Node& node) {
  if (node.terminal) return node.scores;
  if (!node.operation) return invalid("combination fusion data is missing");
  std::vector<std::pair<double, Scores>> children;
  for (const auto& [index, child] : node.children) {
    (void)index;
    LOOM_TRY_ASSIGN(auto scores, evaluate(*child));
    children.emplace_back(child->weight, std::move(scores));
  }
  return aggregate(children, *node.operation);
}

}  // namespace

Json method_channel_capabilities() {
  Json result = Json::object();
  for (const auto* id : {"lexical", "tfidf", "regex", "bm25", "graph_pool"}) {
    Json keys = Json::array({"limit", "min_score"});
    if (std::string_view(id) == "regex")
      for (const auto* key : {"patterns", "flags", "match", "semantics"}) keys.push_back(key);
    else if (std::string_view(id) == "bm25")
      for (const auto* key : {"k1", "b"}) keys.push_back(key);
    else if (std::string_view(id) == "graph_pool")
      for (const auto* key : {"scope", "membership_score"}) keys.push_back(key);
    result[id] = Json{{"available", true}, {"source", "native_kernel_operation"}, {"parameter_keys", keys}};
  }
  return result;
}

Json method_fusion_capabilities() {
  Json result = Json::object();
  for (const auto* id : {"sum", "max", "min", "mean", "rrf"})
    result[id] = Json{{"available", true}, {"source", "native_arithmetic_operation"}};
  return result;
}

Result<std::shared_ptr<CandidateChannel>> method_channel(std::string_view capability,
    const Json& parameters, std::shared_ptr<const kb::Pack> pack,
    const std::vector<std::string>& graph_claim_ids) {
  try {
    if (!parameters.is_object()) return invalid("method parameters must be an object");
    if (capability == "lexical") return make_lexical_candidate_channel(std::move(pack));
    if (capability == "tfidf") return make_tfidf_candidate_channel(std::move(pack));
    if (capability == "bm25") {
      LOOM_TRY_ASSIGN(auto k1, required_number(parameters, "k1"));
      LOOM_TRY_ASSIGN(auto b, required_number(parameters, "b"));
      return std::shared_ptr<CandidateChannel>(std::make_shared<Bm25Channel>(std::move(pack), k1, b));
    }
    if (capability == "graph_pool") {
      if (json::get_string(parameters, "scope") != "graph_claim_ids") return invalid("graph_pool requires scope:graph_claim_ids");
      LOOM_TRY_ASSIGN(auto score, required_number(parameters, "membership_score"));
      return std::shared_ptr<CandidateChannel>(std::make_shared<PoolChannel>(graph_claim_ids, score));
    }
    if (capability == "regex") {
      const auto* patterns = json::find(parameters, "patterns");
      const auto* flags = json::find(parameters, "flags");
      if (!patterns || !patterns->is_array() || !flags || !flags->is_array()) return invalid("regex patterns and flags arrays are required");
      const auto mode = json::get_string(parameters, "match");
      const auto semantics = json::get_string(parameters, "semantics");
      if (mode != "search" && mode != "full") return invalid("regex match must be search or full");
      if (semantics != "any_pattern" && semantics != "all_patterns" && semantics != "pattern_hits" && semantics != "match_count") return Error(Errc::NotImplemented, "regex semantics unavailable");
      auto options = std::regex_constants::syntax_option_type{};
      std::size_t grammars = 0;
      for (const auto& value : *flags) {
        if (!value.is_string()) return invalid("regex flags must be strings");
        const auto flag = value.get<std::string>();
        if (flag == "ECMAScript") { options |= std::regex_constants::ECMAScript; ++grammars; }
        else if (flag == "basic") { options |= std::regex_constants::basic; ++grammars; }
        else if (flag == "extended") { options |= std::regex_constants::extended; ++grammars; }
        else if (flag == "awk") { options |= std::regex_constants::awk; ++grammars; }
        else if (flag == "grep") { options |= std::regex_constants::grep; ++grammars; }
        else if (flag == "egrep") { options |= std::regex_constants::egrep; ++grammars; }
        else if (flag == "icase") options |= std::regex_constants::icase;
        else if (flag == "nosubs") options |= std::regex_constants::nosubs;
        else if (flag == "optimize") options |= std::regex_constants::optimize;
        else if (flag == "collate") options |= std::regex_constants::collate;
        else if (flag == "multiline") options |= std::regex_constants::multiline;
        else return Error(Errc::NotImplemented, "regex flag unavailable");
      }
      if (grammars != 1) return invalid("regex requires exactly one explicit grammar flag");
      std::vector<std::regex> compiled;
      for (const auto& pattern : *patterns) {
        if (!pattern.is_string()) return invalid("regex patterns must be strings");
        compiled.emplace_back(pattern.get<std::string>(), options);
      }
      return std::shared_ptr<CandidateChannel>(std::make_shared<RegexChannel>(std::move(compiled), mode, semantics));
    }
    return Error(Errc::NotImplemented, "method execution capability unavailable");
  } catch (const std::regex_error&) { return invalid("regex pattern is invalid for supplied grammar"); }
  catch (const std::exception&) { return invalid("method parameter decoding failed"); }
}

Result<std::map<std::string, double>> fuse_method_results(const Json& plan, const Json& trace) {
  try {
    const auto* leaves = json::find(plan, "leaves");
    const auto* selection = json::find(plan, "selection");
    if (!leaves || !leaves->is_array() || !selection || !selection->is_object()) return invalid("resolved method plan is required");
    const auto* descriptor = json::find(*selection, "fusion");
    if (!descriptor) return invalid("selection fusion is required caller data");
    LOOM_TRY_ASSIGN(auto root_fusion, fusion(*descriptor));
    const auto* batches = trace.is_array() ? &trace : json::find(trace, "channels");
    if (!batches || !batches->is_array()) return invalid("method batch trace must contain channels");
    std::map<std::size_t, Scores> measured;
    for (const auto& batch : *batches) {
      std::optional<std::size_t> index;
      if (const auto* value = json::find(batch, "leaf_index")) {
        if (!value->is_number_integer() || (!value->is_number_unsigned() && value->get<std::int64_t>() < 0)) return invalid("batch leaf_index must be nonnegative integer");
        const auto native = value->get<std::uint64_t>();
        if (native >= leaves->size()) return invalid("batch leaf_index is outside resolved plan");
        index = static_cast<std::size_t>(native);
      } else {
        const auto id = json::get_string(batch, "channel_id", json::get_string(batch, "id"));
        for (std::size_t i = 0; i < leaves->size(); ++i) if (!id.empty() && json::get_string((*leaves)[i], "channel_id") == id) {
          if (index) return invalid("batch channel_id does not identify a unique leaf occurrence");
          index = i;
        }
      }
      if (!index) return invalid("batch must identify its resolved leaf occurrence");
      if (measured.contains(*index)) return invalid("duplicate batch for a resolved leaf occurrence");
      measured.emplace(*index, Scores{});
      if (!(*leaves)[*index].value("available", false) || json::get_string(batch, "status") != "ok") continue;
      const auto* hits = json::find(batch, "accepted_hits");
      if (!hits) hits = json::find(batch, "hits");
      if (!hits || !hits->is_array()) return invalid("successful method batch requires measured hits");
      for (const auto& hit : *hits) {
        const auto ref = json::get_string(hit, "ref");
        if (ref.empty()) return invalid("method hit ref must be nonempty");
        LOOM_TRY_ASSIGN(auto score, required_number(hit, "score"));
        if (!measured.at(*index).emplace(ref, score).second) return invalid("duplicate method hit ref");
      }
    }
    Node root;
    root.operation = std::move(root_fusion);
    for (std::size_t i = 0; i < leaves->size(); ++i) {
      const auto& leaf = (*leaves)[i];
      // Unavailable branches remain in the execution trace but cannot require
      // dispatching their unsupported fusion operations or supply fake zeros.
      if (!leaf.value("available", false)) continue;
      LOOM_TRY_ASSIGN(auto cumulative_weight, required_number(leaf, "weight"));
      const auto* path = json::find(leaf, "path");
      if (!path || path->empty()) {
        auto node = std::make_unique<Node>();
        node->terminal = true;
        node->weight = cumulative_weight;
        if (measured.contains(i)) node->scores = measured.at(i);
        if (!root.children.emplace(i, std::move(node)).second) return invalid("mixed flat and nested method paths");
        continue;
      }
      if (!path->is_array()) return invalid("method path must be an array");
      Node* node = &root;
      long double product = 1;
      for (std::size_t depth = 0; depth < path->size(); ++depth) {
        const auto& step = (*path)[depth];
        const auto* member_index = json::find(step, "member_index");
        const auto* member = json::find(step, "member");
        if (!member_index || !member_index->is_number_integer() || (!member_index->is_number_unsigned() && member_index->get<std::int64_t>() < 0) || !member || !member->is_object()) return invalid("method path requires explicit member_index and member data");
        const auto native = member_index->get<std::uint64_t>();
        if (native > std::numeric_limits<std::size_t>::max()) return invalid("method occurrence index overflow");
        double weight = 1;
        if (const auto* value = json::find(*member, "weight")) { LOOM_TRY_ASSIGN(weight, number(*value, "member weight")); }
        product *= weight;
        if (!std::isfinite(product) || !std::isfinite(static_cast<double>(product))) return invalid("method path weight overflow");
        auto& child = node->children[static_cast<std::size_t>(native)];
        if (!child) { child = std::make_unique<Node>(); child->weight = weight; child->membership = step; }
        else if (child->weight != weight || child->membership != step) return invalid("method occurrence has inconsistent path identity or member weights");
        node = child.get();
        if (depth + 1 == path->size()) {
          if (node->terminal || !node->children.empty()) return invalid("duplicate or ambiguous method leaf occurrence");
          node->terminal = true;
          if (measured.contains(i)) node->scores = measured.at(i);
        } else {
          if (node->terminal) return invalid("terminal method occurrence cannot be a combination branch");
          const auto id = json::get_string(step, "id");
          const auto* fusions = json::find(leaf, "fusions");
          const Json* operation = nullptr;
          if (fusions && fusions->is_array()) for (const auto& item : *fusions)
            if (json::get_string(item, "combination_version_id") == id) operation = json::find(item, "fusion");
          if (!operation) return invalid("nested combination fusion descriptor missing");
          LOOM_TRY_ASSIGN(auto parsed, fusion(*operation));
          if (node->operation && (node->operation->operation != parsed.operation || node->operation->signal != parsed.signal || node->operation->constant != parsed.constant)) return invalid("method occurrence has inconsistent fusion descriptors");
          node->operation = std::move(parsed);
        }
      }
      const auto resolved_product = static_cast<double>(product);
      const auto tolerance = std::numeric_limits<double>::epsilon() * std::max(std::abs(resolved_product), std::abs(cumulative_weight)) * static_cast<double>(path->size() + 1);
      if (std::abs(resolved_product - cumulative_weight) > tolerance) return invalid("resolved leaf weight differs from actual member path");
    }
    return evaluate(root);
  } catch (const std::exception&) { return invalid("method fusion decoding failed"); }
}

}  // namespace loom::context
