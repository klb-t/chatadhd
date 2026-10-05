// Same pre-existing pure archive API on both sides. Synthetic input only;
// no Runtime/SemanticAnalyzer construction and no paid provider calls.
#include <iostream>
#include <string>
#include <vector>

#include "archive/archive_internal.h"

using namespace loom;
using namespace loom::archive;

int main() {
  Json output = Json::object();
  std::vector<std::string> samples{
      "", "alpha beta gamma", "alpha alpha gamma", "the alpha beta graph",
      "GraphEngine IExecutionEnvironment ChatADHD ABCDE mixedCase",
      "We decided to use SQLite.", "We must not lose source provenance.",
      "Should we use knowledge graphs?", "Czy pamięć przechowuje źródła?",
      "We decided to reject SQLite durable storage for Graphite archives.",
      "Rejected the vector index because lexical search was enough.",
      "e.g. GraphStore stores records. Prof. Smith preserves them.",
      "# Decisions\n1. We decided to preserve every raw archive.\n- Keep source IDs stable.",
      "```cpp\nignore this graph code\n```\nWe must preserve graph memory.",
      "10. Otwarte decyzje\nAlgorytm powinien być konfigurowalny.",
      "GraphEngine stores records.) \"Beta graph preserves edges.\"",
      "Can operators search graph memory?)*\"", "We decided to use SQLite:"};
  for (const auto* separator : {" ", "-", "--", "\t", "\n", ":", "::", "/", ".", "_", "—"}) {
    samples.push_back(std::string("alpha") + separator + "beta gamma");
  }
  for (const auto* punctuation : {".", "?", "!", ".)", ".\"", ".”", ";", ":"}) {
    for (const auto* opener : {"Beta", "beta", "7", "\"Beta", "[Beta"}) {
      samples.push_back(std::string("Alpha graph stores records") + punctuation + " " + opener + " graph preserves edges.");
    }
  }
  for (const auto* bullet : {"-", "+", "*", "1.", "1234)", "12345)", ">", "|"}) {
    samples.push_back(std::string(bullet) + " Alpha graph engine stores records.\n" + bullet + " Beta graph engine stores edges.");
  }
  Json texts = Json::array();
  for (const auto& sample : samples) {
    const auto c = classify_sentence(sample, "Decisions");
    texts.push_back(Json{{"input", sample}, {"tokens", content_tokens(sample)},
        {"candidates", candidate_terms(sample)}, {"sentences", split_sentences(sample)},
        {"identifiers", camel_identifiers(sample)},
        {"class", Json{{"type", c.type}, {"confidence", c.confidence}, {"cues", c.cues}, {"polarity", c.polarity}}}});
  }
  output["texts"] = texts;
  Json dates = Json::array();
  for (const auto* date : {"1888-02-03", "1989-12-31", "1990-01-01", "2026-10-04", "2100-12-31",
                          "2101-01-01", "9999-12-31", "2026-13-01", "2026-01-32", "prefix 2026-02-03 suffix",
                          "2026-02-03T04:05:06Z", "2026-02-03T04:05:06+02:30", "not a date"}) {
    dates.push_back(Json{{"input", date}, {"first", first_date(date)}, {"normalized", normalize_date(date)}});
  }
  output["dates"] = dates;
  Json epochs = Json::array();
  for (const auto epoch : {0.0, -1.0, -.5, .5, 1.0, 1.25, 1770000000.75})
    epochs.push_back(Json{{"input", epoch}, {"iso", iso_from_epoch(epoch)}});
  output["epochs"] = epochs;
  Json items = Json::array();
  for (std::size_t i = 0; i < samples.size(); ++i) {
    Doc doc;
    doc.key = "fixture-" + std::to_string(i); doc.unit = "synthetic"; doc.kind = "doc";
    doc.text = samples[i]; doc.date = "2026-02-03";
    for (const auto& item : extract_items(doc, "fixture")) items.push_back(item.to_json());
  }
  Doc source; source.key = "synthetic-source"; source.unit = "fixture"; source.kind = "code";
  source.extra = Json{{"todos", Json::array({Json{{"text", "FIXME preserve graph records."}}, Json{{"text", "TODO preserve graph edges."}}})}};
  for (const auto& item : extract_items(source, "fixture")) items.push_back(item.to_json());
  source.kind = "commit";
  for (const auto* subject : {"fix graph persistence", "add graph persistence", "repair archive regression"}) {
    source.extra = Json{{"subject", subject}};
    for (const auto& item : extract_items(source, "fixture")) items.push_back(item.to_json());
  }
  output["items"] = items;
  Json relation_cases = Json::array();
  for (const auto* first : {"open_question", "decision", "requirement", "rejected_option"}) {
    for (const auto* second : {"decision", "rejected_option", "invariant"}) {
      Item a, b;
      a.id = "earlier"; a.type = first; a.date = "2026-01-01"; a.doc = "doc-first";
      b.id = "later"; b.type = second; b.date = "2026-02-01"; b.doc = "doc-second";
      a.subject = b.subject = {"alpha", "beta", "gamma"};
      a.term_polarity = b.term_polarity = {{"alpha", 1}, {"beta", 1}, {"gamma", 1}};
      std::vector<Item> pair{a, b};
      Json edges = Json::array(), updated = Json::array();
      for (const auto& edge : relate_items(pair)) edges.push_back(edge.to_json());
      for (const auto& item : pair) updated.push_back(item.to_json());
      relation_cases.push_back(Json{{"first", first}, {"second", second}, {"edges", edges}, {"items", updated}});
    }
  }
  output["relation_cases"] = relation_cases;
  Json scores = Json::array();
  for (int i = -12; i <= 12; ++i) {
    TermRecord term; term.term = "fixture"; term.origin = "synthetic";
    term.score = static_cast<double>(i) / 97.0;
    scores.push_back(term.to_json());
  }
  output["scores"] = scores;
  Corpus corpus;
  for (const auto* text : {"GraphEngine uses graph memory and archive sources", "GraphEngine preserves graph memory sources",
                         "ArchiveManager records source provenance and graph evidence", "ArchiveManager preserves source records",
                         "Distinct database index retrieval queries", "Different account profile operators"}) {
    Doc doc; doc.key = "corpus-" + std::to_string(corpus.docs.size()); doc.text = text;
    corpus.docs.push_back(doc);
  }
  const auto stats = compute_stats(corpus);
  Json salient = Json::array(), added = Json::array();
  for (std::size_t i = 0; i < corpus.docs.size(); ++i) salient.push_back(salient_terms(stats, i, 10));
  for (const auto& term : expand_vocabulary(corpus, stats, {0, 1, 2, 3}, {"archive"}, 1, 20, nullptr)) added.push_back(term.to_json());
  output["salient"] = salient; output["added"] = added;
  std::cout << output.dump() << '\n';
}
