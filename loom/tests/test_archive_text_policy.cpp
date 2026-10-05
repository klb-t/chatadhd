#include <doctest/doctest.h>
#include <cmath>
#include <ctime>
#include <limits>

#include "archive/archive_internal.h"
#include "archive/profile.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::archive;
using loom::test::unwrap;

TEST_SUITE("archive text policy closure") {
  TEST_CASE("phrase orders separators and repeated neighbors are caller data") {
    auto base = unwrap(ArchiveProfile::builtin());
    CHECK(candidate_terms("alpha beta gamma", &base) ==
          std::vector<std::string>{"alpha", "alpha beta", "beta", "beta gamma", "gamma"});
    auto custom = unwrap(base.with_overrides(Json{{"text_item_closure", Json{
        {"phrase_orders", Json::array({3})}, {"phrase_separators", ":"}, {"phrase_output_separator", "::"},
        {"phrase_distinct_adjacent", false}}}}));
    CHECK(candidate_terms("alpha:beta:gamma", &custom) == std::vector<std::string>{"alpha::beta::gamma"});
    CHECK(candidate_terms("alpha:alpha:gamma", &custom) == std::vector<std::string>{"alpha::alpha::gamma"});
    auto empty = unwrap(base.with_overrides(Json{{"text_item_closure", Json{{"phrase_orders", Json::array()}}}}));
    CHECK(candidate_terms("alpha beta", &empty).empty());
  }

  TEST_CASE("numeric bullets and sentence boundaries are independent syntax presets") {
    auto base = unwrap(ArchiveProfile::builtin());
    const auto text = "12345) Alpha graph engine stores records.\n12346) Beta graph engine stores records.";
    const auto normal = split_sentences(text, &base);
    REQUIRE_FALSE(normal.empty());
    CHECK(normal[0].starts_with("12345)"));
    auto wide = unwrap(base.with_overrides(Json{{"text_item_closure", Json{{"bullet_max_digits", 5}}}}));
    const auto bullets = split_sentences(text, &wide);
    REQUIRE(bullets.size() == 2);
    CHECK(bullets[0].starts_with("Alpha"));
    const auto sentences = "alpha graph engine stores nodes; beta graph engine stores edges";
    CHECK(split_sentences(sentences, &base).size() == 1);
    auto semicolon = unwrap(base.with_overrides(Json{{"text_item_closure", Json{
        {"sentence_terminals", ";"}, {"sentence_closers", ";"},
        {"sentence_openers", ""}, {"sentence_opener_classes", Json::array({"any"})}}}}));
    CHECK(split_sentences(sentences, &semicolon).size() == 2);
  }

  TEST_CASE("nullable year policy admits historical dates without changing ISO shape") {
    auto base = unwrap(ArchiveProfile::builtin());
    CHECK(first_date("1888-02-03", &base).empty());
    auto unbounded = unwrap(base.with_overrides(Json{{"text_item_closure", Json{{"year_min", nullptr}, {"year_max", nullptr}}}}));
    CHECK(first_date("1888-02-03", &unbounded) == "1888-02-03");
    CHECK(normalize_date("1888-02-03", &unbounded) == "1888-02-03");
    CHECK(first_date("9999-12-31", &unbounded) == "9999-12-31");
    CHECK(first_date("1888-13-03", &unbounded).empty());
  }

  TEST_CASE("epoch acceptance is configurable while finite native representation stays checked") {
    auto base = unwrap(ArchiveProfile::builtin());
    CHECK(iso_from_epoch(0, &base).empty());
    CHECK(iso_from_epoch(-1, &base).empty());
    auto historic = unwrap(base.with_overrides(Json{{"text_item_closure", Json{
        {"allow_nonpositive_epoch", true}, {"year_min", nullptr}, {"year_max", nullptr}}}}));
    CHECK(iso_from_epoch(0, &historic) == "1970-01-01T00:00:00Z");
    CHECK(iso_from_epoch(-.5, &historic) == "1969-12-31T23:59:59Z");
    CHECK(normalize_date("1888-02-03T04:05:06Z", &historic) == "1888-02-03T04:05:06Z");
    CHECK(iso_from_epoch(std::numeric_limits<double>::infinity(), &historic).empty());
    CHECK(iso_from_epoch(-std::numeric_limits<double>::infinity(), &historic).empty());
    CHECK(iso_from_epoch(std::numeric_limits<double>::quiet_NaN(), &historic).empty());
    CHECK(iso_from_epoch(std::numeric_limits<double>::max(), &historic).empty());
    CHECK(iso_from_epoch(-std::numeric_limits<double>::max(), &historic).empty());
    CHECK(iso_from_epoch(std::ldexp(1.0, std::numeric_limits<std::time_t>::digits), &historic).empty());
  }

  TEST_CASE("classifier target labels and confidence encoding are caller data") {
    auto base = unwrap(ArchiveProfile::builtin());
    CHECK(classify_sentence("We decided to use SQLite.", "", &base).confidence == .88);
    auto no_rounding = unwrap(base.with_overrides(Json{{"text_item_closure", Json{
        {"confidence_multiplier", 0}, {"question_target_type", "inquiry"}}}}));
    CHECK(classify_sentence("We decided to use SQLite.", "", &no_rounding).confidence == doctest::Approx(.875));
    CHECK(classify_sentence("Can operators search graph memory?", "", &no_rounding).type == "inquiry");
    auto tenths = unwrap(base.with_overrides(Json{{"text_item_closure", Json{{"confidence_multiplier", 10}}}}));
    CHECK(classify_sentence("We decided to use SQLite.", "", &tenths).confidence == doctest::Approx(.9));
  }

  TEST_CASE("TODO and commit bindings change roles and cues while retaining stable IDs") {
    auto base = unwrap(ArchiveProfile::builtin());
    auto custom = unwrap(base.with_overrides(Json{{"text_item_closure", Json{
        {"todo_bug_binding", Json{{"type", "repair"}, {"cue", "source_marker"}}},
        {"commit_bug_binding", Json{{"type", "repair"}, {"cue", "source_commit"}}},
        {"todo_source_kinds", Json::array({"annotation"})}, {"commit_source_kinds", Json::array({"change"})},
        {"list_intro_suffixes", Json::array()}}}}));
    Doc source;
    source.key = "synthetic-code"; source.kind = "code";
    source.extra = Json{{"todos", Json::array({Json{{"text", "FIXME preserve graph records."}}})}};
    const auto normal = extract_items(source, "fixture", &base);
    CHECK(extract_items(source, "fixture", &custom).empty());
    source.kind = "annotation";
    const auto changed = extract_items(source, "fixture", &custom);
    REQUIRE(normal.size() == 1); REQUIRE(changed.size() == 1);
    CHECK(normal[0].type == "bug"); CHECK(changed[0].type == "repair");
    CHECK(changed[0].cues == std::vector<std::string>{"source_marker"});
    CHECK(normal[0].id == changed[0].id);
    source.kind = "change"; source.extra = Json{{"subject", "fix graph persistence"}};
    CHECK(extract_items(source, "fixture", &custom)[0].cues == std::vector<std::string>{"source_commit"});
    source.kind = "doc"; source.text = "We decided to use SQLite:";
    CHECK(extract_items(source, "fixture", &base).empty());
    CHECK(extract_items(source, "fixture", &custom).size() == 1);
  }

  TEST_CASE("question and answer roles determine resolution relations through profile bindings") {
    auto base = unwrap(ArchiveProfile::builtin());
    auto custom = unwrap(base.with_overrides(Json{{"text_item_closure", Json{
        {"question_role_types", Json::array({"query"})}, {"answer_role_types", Json::array({"action"})},
        {"resolution_relation", "answers"}, {"resolution_status", "closed"},
        {"resolution_reason_template", "{{answer_type}} answers {{question_type}}"}}}}));
    Item question, answer;
    question.id = "q"; question.type = "query"; question.doc = "first"; question.date = "2026-01-01";
    answer.id = "a"; answer.type = "action"; answer.doc = "second"; answer.date = "2026-02-01";
    question.subject = answer.subject = {"alpha", "beta"};
    question.term_polarity = answer.term_polarity = {{"alpha", 1}, {"beta", 1}};
    std::vector<Item> items{question, answer};
    CHECK(relate_items(items, nullptr, &base).empty());
    const auto relations = relate_items(items, nullptr, &custom);
    REQUIRE(relations.size() == 1);
    CHECK(relations[0].type == "answers");
    CHECK(relations[0].reason == "action answers query");
    CHECK(items[0].status == "closed");
  }

  TEST_CASE("identifier predicates and vocabulary score encoding are independent presets") {
    auto base = unwrap(ArchiveProfile::builtin());
    CHECK(camel_identifiers("ABCDE", &base).empty());
    auto custom = unwrap(base.with_overrides(Json{{"vocab_closure", Json{
        {"camel_min_lowercase", 0}, {"camel_require_inner_uppercase", false}, {"score_multiplier", 10}}}}));
    CHECK(camel_identifiers("ABCDE", &custom) == std::vector<std::string>{"ABCDE"});
    TermRecord record; record.term = "fixture"; record.score = .123456;
    CHECK(record.to_json(&base)["score"] == .1235);
    CHECK(record.to_json(&custom)["score"] == .1);
    auto exact = unwrap(base.with_overrides(Json{{"vocab_closure", Json{{"score_multiplier", 0}}}}));
    CHECK(record.to_json(&exact)["score"] == .123456);
  }

  TEST_CASE("diagnostic precision changes rendering while retaining vocabulary computation") {
    auto base = unwrap(ArchiveProfile::builtin());
    auto custom = unwrap(base.with_overrides(Json{{"vocab_closure", Json{
        {"reason_score_precision", 0}, {"reason_lift_precision", 4}}}}));
    Corpus corpus;
    for (const auto* text : {"GraphEngine uses graph memory and archive sources", "GraphEngine preserves graph memory sources",
                            "ArchiveManager records source provenance and graph evidence", "ArchiveManager preserves source records",
                            "Distinct database index retrieval queries", "Different account profile operators"}) {
      Doc doc; doc.key = "fixture-" + std::to_string(corpus.docs.size()); doc.text = text;
      corpus.docs.push_back(doc);
    }
    const auto stats = compute_stats(corpus, &base);
    const auto normal = expand_vocabulary(corpus, stats, {0, 1, 2, 3}, {"archive"}, 1, 20, nullptr, &base);
    const auto changed = expand_vocabulary(corpus, stats, {0, 1, 2, 3}, {"archive"}, 1, 20, nullptr, &custom);
    REQUIRE_FALSE(normal.empty());
    REQUIRE(normal.size() == changed.size());
    for (std::size_t i = 0; i < normal.size(); ++i) {
      CHECK(normal[i].term == changed[i].term);
      CHECK(normal[i].score == changed[i].score);
      CHECK(normal[i].evidence == changed[i].evidence);
      REQUIRE_FALSE(normal[i].reasons.empty());
      REQUIRE_FALSE(changed[i].reasons.empty());
      CHECK(normal[i].reasons[0] != changed[i].reasons[0]);
      CHECK(changed[i].reasons[0].ends_with("1.5000)"));
    }
  }
}
