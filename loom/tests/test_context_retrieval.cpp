// Synthetic instrument/mechanism tests only; no model-quality or holdout claim.
#include <doctest/doctest.h>

#include <limits>
#include <map>
#include <set>
#include <stdexcept>

#include "loom/context_retrieval.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::context;
using loom::test::unwrap;

namespace {

class ScriptedSpace final : public resolve::VectorSpace {
 public:
  std::map<std::string, resolve::SparseVec> outputs;
  std::vector<std::string> supported = {"text"};
  std::vector<resolve::EmbedInput> fitted;
  bool fail_fit = false, fail_vectors = false, wrong_count = false, throw_vectors = false;
  int vector_calls = 0;
  bool reject_duplicate_ids = false;
  std::string method() const override { return "embedding:synthetic-instrument"; }
  std::vector<std::string> modalities() const override { return supported; }
  Status fit(const std::vector<resolve::EmbedInput>& corpus) override {
    fitted = corpus;
    if (fail_fit) return Error(Errc::Unavailable, "sensitive provider response");
    return {};
  }
  Result<std::vector<resolve::SparseVec>> vectors(const std::vector<resolve::EmbedInput>& inputs) override {
    ++vector_calls;
    if (throw_vectors) throw std::runtime_error("sensitive provider response");
    if (fail_vectors) return Error(Errc::Network, "sensitive provider response");
    std::vector<resolve::SparseVec> out;
    std::set<std::string> input_ids;
    for (const auto& input : inputs) {
      if (reject_duplicate_ids && !input_ids.insert(input.id).second) return Error(Errc::InvalidArgument, "duplicate input id");
      out.push_back(outputs[input.text]);
    }
    if (wrong_count && !out.empty()) out.pop_back();
    return out;
  }
};

CandidateChannelRequest request(std::string id = "tfidf", int limit = 50, double minimum = 0) {
  return CandidateChannelRequest{std::move(id), limit, minimum};
}

}  // namespace

TEST_SUITE("context_retrieval") {
  TEST_CASE("request parser is strict without arbitrary configurable ceilings") {
    auto defaults = unwrap(CandidateChannelRequest::from_json(Json{{"id", "tfidf"}}));
    CHECK(defaults.limit == 50);
    CHECK(defaults.min_score == 0);
    CHECK(unwrap(CandidateChannelRequest::from_json(defaults.to_json())).to_json() == defaults.to_json());
    auto large = unwrap(CandidateChannelRequest::from_json(Json{{"id", "custom"}, {"limit", std::numeric_limits<int>::max()}, {"min_score", -2}}));
    CHECK(large.limit == std::numeric_limits<int>::max());
    CHECK(large.min_score == -2);
    for (const auto& bad : {Json(), Json::array(), Json("tfidf"), Json::object(), Json{{"id", ""}}, Json{{"id", 1}}, Json{{"id", "tfidf"}, {"limt", 2}}}) {
      CHECK(!CandidateChannelRequest::from_json(bad));
    }
    for (const auto& bad : {Json(0), Json(-1), Json(1.5), Json(true), Json("1"), Json(), Json(std::numeric_limits<std::uint64_t>::max())}) {
      CHECK(!CandidateChannelRequest::from_json(Json{{"id", "tfidf"}, {"limit", bad}}));
    }
    for (const auto& bad : {Json("0"), Json(true), Json(), Json::array(), Json(std::numeric_limits<double>::infinity()), Json(std::numeric_limits<double>::quiet_NaN())}) {
      CHECK(!CandidateChannelRequest::from_json(Json{{"id", "tfidf"}, {"min_score", bad}}));
    }
  }

  TEST_CASE("existing offline vector space finds glossary-linked candidate independently of lexical shadow") {
    auto pack = unwrap(kb::Pack::load_builtin());
    const std::vector<RetrievalDocument> corpus = {{"encryption-source", "encryption"}, {"unrelated", "orchestra"}};
    const auto vector = make_tfidf_candidate_channel(pack)->retrieve("szyfrowanie", corpus, request());
    REQUIRE(vector.status == "ok");
    CHECK(vector.method == "tfidf");
    REQUIRE(vector.hits.size() == 1);
    CHECK(vector.hits.front().ref == "encryption-source");
    CHECK(vector.hits.front().score == doctest::Approx(1));
    CHECK(vector.scored_count == 2);
    CHECK(vector.zero_score_count == 1);
    const auto shadow = lexical_shadow(pack, "szyfrowanie", corpus, request("lexical"));
    CHECK(shadow.status == "ok");
    CHECK(shadow.hits.empty());
    CHECK(shadow.zero_score_count == 2);
    CHECK(shadow.scored_count == 2);
  }

  TEST_CASE("lexical channel measures exact normalized tokens instead of substring matches") {
    auto pack = unwrap(kb::Pack::load_builtin());
    auto channel = make_lexical_candidate_channel(pack);
    const std::vector<RetrievalDocument> corpus = {{"exact", "CAT cat dog"}, {"substring", "concatenate dog"}, {"empty", "..."}};
    auto batch = channel->retrieve("cat CAT", corpus, request("lexical"));
    CHECK(batch.status == "ok");
    REQUIRE(batch.hits.size() == 1);
    CHECK(batch.hits.front().ref == "exact");
    CHECK(batch.hits.front().score == 1);
    CHECK(batch.scored_count == 2);
    CHECK(batch.unrepresentable_count == 1);
    CHECK(batch.zero_score_count == 1);
  }

  TEST_CASE("result threshold and top-k retain deterministic full ranking and explicit truncation") {
    auto channel = make_lexical_candidate_channel(unwrap(kb::Pack::load_builtin()));
    const std::vector<RetrievalDocument> corpus = {{"z", "cat"}, {"b", "dog"}, {"a", "cat"}};
    const auto batch = channel->retrieve("cat", corpus, request("lexical", 1));
    CHECK(batch.eligible_count == 2);
    CHECK(batch.truncated);
    REQUIRE(batch.scores.size() == 3);
    REQUIRE(batch.hits.size() == 1);
    CHECK(batch.hits.front().ref == "a");
    CHECK(batch.scores.back().ref == "b");
    CHECK(batch.scores.back().score == 0);
    CHECK(batch.to_json() == channel->retrieve("cat", corpus, request("lexical", 1)).to_json());
    const auto exclusive = channel->retrieve("cat", corpus, request("lexical", 50, 1));
    CHECK(exclusive.hits.empty());
    CHECK_FALSE(exclusive.truncated);
    CHECK(exclusive.scored_count == 3);
  }

  TEST_CASE("explicit vector capability can return nonliteral candidates without lexical gating") {
    auto space = std::make_shared<ScriptedSpace>();
    space->outputs["task"] = {{"x", 1}};
    space->outputs["different wording"] = {{"x", 1}};
    space->outputs["task distractor"] = {{"y", 1}};
    auto batch = make_vector_candidate_channel(space)->retrieve("task", {{"relevant", "different wording"}, {"lexical-noise", "task distractor"}}, request("custom"));
    REQUIRE(batch.status == "ok");
    REQUIRE(batch.hits.size() == 1);
    CHECK(batch.hits.front().ref == "relevant");
    CHECK(batch.method == "embedding:synthetic-instrument");
    CHECK(space->fitted.size() == 2);  // query must not contaminate corpus IDF
    CHECK(space->vector_calls == 1);
  }

  TEST_CASE("unavailable, unrepresentable and measured zero remain different outcomes") {
    const std::vector<RetrievalDocument> corpus = {{"one", "evidence"}};
    const auto unavailable = make_vector_candidate_channel(nullptr)->retrieve("query", corpus, request("custom"));
    CHECK(unavailable.status == "unavailable");
    CHECK(unavailable.scores.empty());
    auto space = std::make_shared<ScriptedSpace>();
    space->outputs["query"] = {{"a", 1}};
    space->outputs["evidence"] = {{"b", 1}};
    auto channel = make_vector_candidate_channel(space);
    const auto measured = channel->retrieve("query", corpus, request("custom"));
    REQUIRE(measured.status == "ok");
    REQUIRE(measured.scores.size() == 1);
    CHECK(measured.scores.front().score == 0);
    CHECK(measured.zero_score_count == 1);
    CHECK(measured.hits.empty());
    space->outputs["query"].clear();
    const auto unrepresentable = channel->retrieve("query", corpus, request("custom"));
    CHECK(unrepresentable.status == "unrepresentable");
    CHECK(unrepresentable.scored_count == 0);
    CHECK(unrepresentable.scores.empty());
  }

  TEST_CASE("empty document vectors are counted without a fabricated score") {
    auto space = std::make_shared<ScriptedSpace>();
    space->outputs["query"] = {{"a", 1}};
    space->outputs["known"] = {{"a", 1}};
    auto result = make_vector_candidate_channel(space)->retrieve("query", {{"empty", "empty"}, {"present", "known"}}, request("custom"));
    REQUIRE(result.status == "ok");
    CHECK(result.corpus_count == 2);
    CHECK(result.unrepresentable_count == 1);
    REQUIRE(result.unrepresentable_refs.size() == 1);
    CHECK(result.unrepresentable_refs.front() == "empty");
    CHECK(result.scored_count == 1);
    REQUIRE(result.scores.size() == 1);
    CHECK(result.scores.front().ref == "present");
  }

  TEST_CASE("instrument failures and malformed vectors do not become zero or silent fallback") {
    auto space = std::make_shared<ScriptedSpace>();
    auto channel = make_vector_candidate_channel(space);
    const auto call = [&] { return channel->retrieve("query", {{"one", "evidence"}}, request("custom")); };
    const auto check_error = [&](const RetrievalBatch& batch) {
      CHECK(batch.status == "error");
      CHECK(batch.scores.empty());
      CHECK(batch.hits.empty());
      CHECK(batch.to_json().dump().find("sensitive") == std::string::npos);
    };
    space->fail_fit = true;
    check_error(call());
    CHECK(space->vector_calls == 0);
    space->fail_fit = false;
    space->fail_vectors = true;
    check_error(call());
    space->fail_vectors = false;
    space->wrong_count = true;
    check_error(call());
    space->wrong_count = false;
    space->throw_vectors = true;
    check_error(call());
    space->throw_vectors = false;
    space->outputs["query"] = {{"a", std::numeric_limits<double>::quiet_NaN()}};
    check_error(call());
    space->outputs["query"] = {{"a", 1}};
    space->outputs["evidence"] = {{"a", std::numeric_limits<double>::infinity()}};
    check_error(call());
  }

  TEST_CASE("unsupported modality remains unavailable without running a provider") {
    auto space = std::make_shared<ScriptedSpace>();
    space->supported = {"image"};
    const auto batch = make_vector_candidate_channel(space)->retrieve("query", {{"one", "text"}}, request("custom"));
    CHECK(batch.status == "unavailable");
    CHECK(space->fitted.empty());
    CHECK(space->vector_calls == 0);
  }

  TEST_CASE("finite nonzero vector magnitude does not become a missing capability") {
    auto space = std::make_shared<ScriptedSpace>();
    auto channel = make_vector_candidate_channel(space);
    for (const double magnitude : {1e-200, 1e200}) {
      space->outputs["query"] = {{"a", magnitude}, {"b", -magnitude}};
      space->outputs["parallel"] = {{"a", 2 * magnitude}, {"b", -2 * magnitude}};
      space->outputs["orthogonal"] = {{"a", magnitude}, {"b", magnitude}};
      const auto batch = channel->retrieve("query", {{"one", "parallel"}, {"zero", "orthogonal"}}, request("custom"));
      REQUIRE(batch.status == "ok");
      REQUIRE(batch.scores.size() == 2);
      CHECK(batch.scores[0].score == doctest::Approx(1));
      CHECK(batch.scores[1].score == doctest::Approx(0));
      CHECK(batch.unrepresentable_count == 0);
    }
  }

  TEST_CASE("query embed input identity cannot collide with a corpus reference") {
    auto space = std::make_shared<ScriptedSpace>();
    space->reject_duplicate_ids = true;
    space->outputs["task"] = {{"a", 1}};
    space->outputs["evidence"] = {{"a", 1}};
    const auto batch = make_vector_candidate_channel(space)->retrieve("task",
        {{"query", "evidence"}, {"query:", "evidence"}}, request("custom"));
    CHECK(batch.status == "ok");
    CHECK(batch.hits.size() == 2);
  }

  TEST_CASE("registration aliases do not change the actual lexical or vector instrument identity") {
    const auto lexical = make_lexical_candidate_channel(unwrap(kb::Pack::load_builtin()))->retrieve(
        "cat", {{"one", "cat"}}, request("my_lexical_alias"));
    CHECK(lexical.status == "ok");
    CHECK(lexical.method == "lexical_token_overlap");
    auto space = std::make_shared<ScriptedSpace>();
    space->outputs["query"] = {{"a", 1}};
    space->outputs["evidence"] = {{"a", 1}};
    const auto vector = make_vector_candidate_channel(space)->retrieve(
        "query", {{"one", "evidence"}}, request("lexical"));
    CHECK(vector.status == "ok");
    CHECK(vector.method == "embedding:synthetic-instrument");
  }

  TEST_CASE("native invalid configuration and ambiguous corpus references fail explicitly") {
    auto channel = make_tfidf_candidate_channel(unwrap(kb::Pack::load_builtin()));
    CHECK(channel->retrieve("query", {{"one", "text"}}, request("tfidf", 0)).status == "error");
    CHECK(channel->retrieve("query", {{"", "text"}}, request()).status == "error");
    CHECK(channel->retrieve("query", {{"same", "first"}, {"same", "second"}}, request()).status == "error");
    CHECK(channel->retrieve("", {{"one", "text"}}, request()).status == "unrepresentable");
    const auto empty = channel->retrieve("query", {}, request());
    CHECK(empty.status == "ok");
    CHECK(empty.scored_count == 0);
    CHECK(empty.corpus_count == 0);
    CHECK(make_lexical_candidate_channel(nullptr)->retrieve("query", {}, request("lexical")).status == "unavailable");
  }
}
