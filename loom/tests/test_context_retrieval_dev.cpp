// W2 synthetic DEV diagnostic. Corpus/queries/gold were committed before first
// execution in W2-2026-09-30-dev-protocol.md. No quality thresholds, holdout or
// answer model. Run native selector; print first outcomes even when poor.
#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "loom/context_engine.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::model;
using loom::test::unwrap;

namespace {

Entity dev_entity(const std::string& label) {
  Entity result;
  result.kind = "component";
  result.canonical_key = label;
  result.id = Entity::make_id(result.kind, result.canonical_key);
  result.label = label;
  result.origin = Origin::Archive;
  result.confidence = 1.0;
  return result;
}

Claim dev_claim(const std::string& id, const Entity& subject, const std::string& value,
                const std::string& quote) {
  Claim result;
  result.id = "cl.dev." + id;
  result.subject = subject.id;
  result.predicate = "records";
  result.value = value;
  result.qualifiers.valid_from = "2026-09-30";
  result.assessment.evidence = EvidenceClass::Observed;
  result.assessment.origin = Origin::Archive;
  result.assessment.confidence = 1.0;
  Support support;
  support.observation = "ob.dev." + id;
  support.quote = quote;
  support.extractor = "w2.synthetic_dev@1";
  result.assessment.support = {support};
  return result;
}

struct DevFixture {
  fsutil::TempDir directory;
  std::unique_ptr<Runtime> runtime;
  std::shared_ptr<const kb::Pack> pack;
  kb::KnowledgeStore* store = nullptr;
  std::string run;
  Entity alpha = dev_entity("node alpha");
  Entity beta = dev_entity("node beta");
  Entity gamma = dev_entity("node gamma");
  Entity delta = dev_entity("node delta");
  Entity epsilon = dev_entity("node epsilon");
  Entity zeta = dev_entity("node zeta");
  Entity omega = dev_entity("node omega");
  std::vector<Claim> claims = {
      dev_claim("resume", alpha, "Resume the pending operation from its last durable state.",
                "The operation resumes after interruption."),
      dev_claim("offset", beta, "Recorded offset.",
                "A checkpoint contains the durable offset needed to resume."),
      dev_claim("wallpaper", gamma, "Decorative theme.",
                "Checkpoint is the codename of the purple wallpaper."),
      dev_claim("crypto", delta, "Protection setting.",
                "Encryption protects stored records with a secret key."),
      dev_claim("orchid", epsilon, "Verification marker.",
                "The orchid marker identifies the validated payload checksum."),
      dev_claim("toolbar", alpha, "The toolbar now uses compact icons.",
                "The toolbar now uses compact icons."),
      dev_claim("weather", zeta, "A weather tile displays cloud cover.",
                "A weather tile displays cloud cover."),
  };

  DevFixture() {
    RuntimeOptions options;
    options.data_dir = directory.path().string();
    options.start_workers = false;
    runtime = unwrap(Runtime::open(options));
    pack = unwrap(runtime->knowledge().pack());
    store = &runtime->knowledge().store();
    run = unwrap(store->begin_run(pack->hash(), Json{{"instrument", "w2.synthetic_dev.v1"}})).id;
    LOOM_REQUIRE_OK(store->put_entities(run, {alpha, beta, gamma, delta, epsilon, zeta, omega}));
    LOOM_REQUIRE_OK(store->put_claims(run, claims));
    LOOM_REQUIRE_OK(store->finish_run(run, "done", Json::object()));
  }
};

struct DevCase {
  std::string id;
  std::string query;
  std::string anchor;
  int budget;
  std::set<std::string> gold;
};

struct DevMode {
  std::string id;
  int relation_hops;
  bool tfidf;
  bool shadow;
};

using RankedClaims = std::vector<std::pair<std::string, double>>;

RankedClaims selected_claims(const ContextSet& set) {
  RankedClaims result;
  for (const auto& item : set.items) {
    if (item.ref_kind == RefKind::Claim) result.emplace_back(item.ref, item.score);
  }
  return result;
}

// Dependency-free fixture: every prebudget claim candidate is either selected
// or dropped. This is NOT recall over every scanned internal document.
RankedClaims prebudget_claims(const ContextSet& set) {
  std::map<std::string, double> unique;
  for (const auto* items : {&set.items, &set.dropped}) {
    for (const auto& item : *items) {
      if (item.ref_kind == RefKind::Claim) unique[item.ref] = item.score;
    }
  }
  RankedClaims result(unique.begin(), unique.end());
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
    return a.second != b.second ? a.second > b.second : a.first < b.first;
  });
  return result;
}

Json ranked_json(const RankedClaims& claims) {
  Json result = Json::array();
  for (const auto& [ref, score] : claims) result.push_back(Json{{"ref", ref}, {"score", score}});
  return result;
}

struct EvidenceMetrics {
  std::size_t gold = 0;
  std::size_t retrieved = 0;
  std::size_t relevant = 0;
  std::size_t first_rank = 0;

  Json to_json() const {
    return Json{
        {"gold_count", gold}, {"retrieved_count", retrieved}, {"relevant_count", relevant},
        {"irrelevant_count", retrieved - relevant},
        {"recall", gold ? Json(static_cast<double>(relevant) / static_cast<double>(gold)) : Json(nullptr)},
        {"precision", retrieved ? Json(static_cast<double>(relevant) / static_cast<double>(retrieved)) : Json(nullptr)},
        {"first_relevant_rank", gold && first_rank ? Json(first_rank) : Json(nullptr)},
        {"reciprocal_rank", gold ? Json(first_rank ? 1.0 / static_cast<double>(first_rank) : 0.0) : Json(nullptr)},
        {"hits_at_1", gold ? Json(first_rank > 0 && first_rank <= 1) : Json(nullptr)},
        {"hits_at_3", gold ? Json(first_rank > 0 && first_rank <= 3) : Json(nullptr)}};
  }
};

EvidenceMetrics evidence_metrics(const RankedClaims& ranked, const std::set<std::string>& gold) {
  EvidenceMetrics result;
  result.gold = gold.size();
  std::set<std::string> seen;
  for (const auto& [ref, score] : ranked) {
    (void)score;
    if (!seen.insert(ref).second) continue;
    ++result.retrieved;
    if (gold.count(ref)) {
      ++result.relevant;
      if (!result.first_rank) result.first_rank = result.retrieved;
    }
  }
  return result;
}

struct Aggregate {
  std::size_t cases = 0;
  std::size_t answerable_cases = 0;
  std::size_t gold = 0;
  std::size_t retrieved = 0;
  std::size_t relevant = 0;
  std::size_t hits1 = 0;
  std::size_t hits3 = 0;
  double reciprocal_rank_sum = 0;

  void add(const EvidenceMetrics& m) {
    ++cases;
    gold += m.gold;
    retrieved += m.retrieved;
    relevant += m.relevant;
    if (!m.gold) return;
    ++answerable_cases;
    if (m.first_rank) {
      reciprocal_rank_sum += 1.0 / static_cast<double>(m.first_rank);
      if (m.first_rank <= 1) ++hits1;
      if (m.first_rank <= 3) ++hits3;
    }
  }

  Json to_json() const {
    return Json{
        {"case_count", cases}, {"nonempty_gold_case_count", answerable_cases},
        {"gold_count", gold}, {"retrieved_count", retrieved}, {"relevant_count", relevant},
        {"micro_recall", gold ? Json(static_cast<double>(relevant) / static_cast<double>(gold)) : Json(nullptr)},
        {"micro_precision", retrieved ? Json(static_cast<double>(relevant) / static_cast<double>(retrieved)) : Json(nullptr)},
        {"mrr", answerable_cases ? Json(reciprocal_rank_sum / static_cast<double>(answerable_cases)) : Json(nullptr)},
        {"hits_at_1_count", hits1}, {"hits_at_3_count", hits3}};
  }
};

Json items_json(const std::vector<ContextItem>& items) {
  Json result = Json::array();
  for (const auto& item : items) result.push_back(item.to_json());
  return result;
}

}  // namespace

TEST_SUITE("context_retrieval_dev") {
  TEST_CASE("synthetic DEV compares native graph tfidf union and lexical shadow without quality gates") {
    DevFixture fixture;
    context::ContextEngine engine(*fixture.runtime, *fixture.store, fixture.pack);
    const std::vector<DevCase> cases = {
        {"checkpoint_union", "checkpoint", fixture.alpha.id, 4096, {"cl.dev.resume", "cl.dev.offset"}},
        {"support_quote_only", "orchid", fixture.omega.id, 4096, {"cl.dev.orchid"}},
        {"glossary_pl_en", "szyfrowanie", fixture.omega.id, 4096, {"cl.dev.crypto"}},
        {"misleading_word", "checkpoint", fixture.omega.id, 4096, {"cl.dev.resume", "cl.dev.offset"}},
        {"no_answer", "quasarflux", fixture.omega.id, 4096, {}},
        {"low_budget", "checkpoint", fixture.alpha.id, 1, {"cl.dev.resume", "cl.dev.offset"}},
    };
    const std::vector<DevMode> modes = {
        {"graph", 1, false, false}, {"tfidf", 0, true, false},
        {"union", 1, true, false}, {"graph_shadow", 1, false, true},
    };
    REQUIRE(fixture.claims.size() == 7);
    std::set<std::string> corpus_ids;
    for (const auto& claim : fixture.claims) {
      CHECK(corpus_ids.insert(claim.id).second);
      CHECK(claim.assessment.premises.claims.empty());
      CHECK(claim.assessment.premises.principles.empty());
    }
    // These are fixture integrity checks, not expectations fitted to outcomes.
    const auto& support_only = fixture.claims.at(4);
    CHECK(support_only.value.get<std::string>().find("orchid") == std::string::npos);
    CHECK(support_only.predicate.find("orchid") == std::string::npos);
    CHECK(fixture.epsilon.label.find("orchid") == std::string::npos);
    CHECK(support_only.assessment.support.front().quote.find("orchid") != std::string::npos);

    std::map<std::string, Aggregate> candidate_totals, selected_totals;
    for (const auto& scenario : cases) {
      INFO("DEV case " << scenario.id);
      for (const auto& id : scenario.gold) REQUIRE(corpus_ids.count(id) == 1);
      Json graph_items;
      Json graph_dropped;
      for (const auto& mode : modes) {
        INFO("mode " << mode.id);
        context::ContextRequest request;
        request.text = scenario.query;
        request.targets = {scenario.anchor};
        request.run = fixture.run;
        request.goal_type = "answer_question";
        request.lang = "en";
        request.budget_tokens = scenario.budget;
        request.relation_hops = mode.relation_hops;
        request.detail_resolution = Resolution::Full;
        request.lexical_shadow = mode.shadow;
        if (mode.tfidf) request.candidate_channels = {{"tfidf", 100, 0.0}};
        const auto selection = unwrap(engine.select(request));
        const auto candidates = prebudget_claims(selection);
        const auto selected = selected_claims(selection);
        const auto candidate_metric = evidence_metrics(candidates, scenario.gold);
        const auto selected_metric = evidence_metrics(selected, scenario.gold);
        candidate_totals[mode.id].add(candidate_metric);
        selected_totals[mode.id].add(selected_metric);

        // Emit measurements before assertions so the first evidence survives a
        // contract defect; diagnostic traces preserve missing capabilities too.
        const Json row{
            {"protocol", "w2.synthetic_dev.v1"}, {"case", scenario.id}, {"mode", mode.id},
            {"pack_hash", fixture.pack->hash()}, {"request", request.to_json()},
            {"gold", scenario.gold}, {"candidate_metrics", candidate_metric.to_json()},
            {"selected_metrics", selected_metric.to_json()},
            {"candidate_rank", ranked_json(candidates)}, {"selected_rank", ranked_json(selected)},
            {"used_tokens", selection.used_tokens}, {"dropped", items_json(selection.dropped)},
            {"engine_diagnostics", selection.goal.params}, {"answer_correctness", "not_measured"}};
        MESSAGE("W2_RETRIEVAL_DEV_ROW " << row.dump());

        CHECK(selection.used_tokens <= scenario.budget);
        CHECK(candidate_metric.relevant <= candidate_metric.gold);
        CHECK(selected_metric.relevant <= candidate_metric.relevant);
        CHECK(selected_metric.retrieved <= candidate_metric.retrieved);
        CHECK(selected_metric.retrieved == selected.size());
        for (const auto& [ref, score] : candidates) {
          (void)score;
          CHECK(corpus_ids.count(ref) == 1);
        }
        const auto repeated = unwrap(engine.select(request));
        CHECK(selection.to_json() == repeated.to_json());
        if (mode.id == "graph") {
          graph_items = items_json(selection.items);
          graph_dropped = items_json(selection.dropped);
        }
        if (mode.shadow) {
          CHECK(items_json(selection.items) == graph_items);
          CHECK(items_json(selection.dropped) == graph_dropped);
        }
      }
    }
    for (const auto& mode : modes) {
      const auto& candidate = candidate_totals.at(mode.id);
      const auto& selected = selected_totals.at(mode.id);
      CHECK(candidate.cases == 6);
      CHECK(candidate.answerable_cases == 5);
      CHECK(candidate.gold == 8);
      CHECK(selected.cases == candidate.cases);
      CHECK(selected.gold == candidate.gold);
      MESSAGE("W2_RETRIEVAL_DEV_SUMMARY " << Json({
          {"protocol", "w2.synthetic_dev.v1"}, {"mode", mode.id},
          {"candidate", candidate.to_json()}, {"selected", selected.to_json()},
          {"answer_correctness", "not_measured"},
          {"quality_thresholds", "none; fixed synthetic DEV diagnostic"}}).dump());
    }
  }
}
