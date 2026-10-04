#include <iostream>
#include <string>
#include <vector>
#include "archive/archive_internal.h"
#include "archive/archive_runtime.h"
#ifdef ARCHIVE_NEW
#include "archive/profile.h"
#endif
using namespace loom;
using namespace loom::archive;
#ifdef ARCHIVE_NEW
#define POLICY , &profile
#else
#define POLICY
#endif
int main() {
#ifdef ARCHIVE_NEW
  auto result = ArchiveProfile::builtin();
  if (!result) {std::cerr << result.error().to_string();return 2;}
  const auto& profile = *result;
#endif
  Json output = Json::object();
  const std::vector<std::string> samples = {
    "", "We decided to use SQLite.", "We must not lose source provenance.",
    "Should we use knowledge graphs?", "Czy pamięć ma przechowywać źródła?",
    "The implementation uses GraphStore and semantic_worker.cpp.",
    "Rejected the vector index because lexical search was enough.",
    "# Decisions\n1. We decided to preserve every raw archive.\n- Keep source IDs stable.\n",
    "## Otwarte decyzje\nCzy synchronizacja będzie offline?\n", "e.g. GraphStore uses BM25. Prof. Smith agrees.",
    "Rust ::GraphStore; knowledge-graph /search/semantic worker", "się sie function functions import export",
    "pamięci grafu zadania szyfrowania źródeł wyszukiwania rozmów", "stories classes analysis status physics",
    "We need an importer. We must preserve links. Fix archive regression.",
    "10. Otwarte decyzje\nAlgorytm powinien być konfigurowalny.\n",
    "We decided to use SQLite durable storage for Graphite archives.",
    "We decided to reject SQLite durable storage for Graphite archives."
  };
  Json texts = Json::array();
  for (const auto& s : samples) {
    auto cls = classify_sentence(s, "Decisions" POLICY);
    texts.push_back(Json{{"input",s},{"tokens",content_tokens(s POLICY)},
      {"candidates",candidate_terms(s POLICY)},{"sentences",split_sentences(s POLICY)},
      {"class",Json{{"type",cls.type},{"confidence",cls.confidence},{"cues",cls.cues},{"polarity",cls.polarity}}}});
  }
  output["texts"]=texts;
  Json words = Json::array();
  for (const auto& s : samples) for (const auto& word:tokenize(s))
    words.push_back(Json{{"word",word},{"stem",stem(word POLICY)},{"gloss",gloss(word POLICY)},{"stopword",is_stopword(word POLICY)}});
  output["words"]=words;
  Corpus corpus;
  for (std::size_t i=0;i<samples.size();++i) {
    Doc d;d.key="d"+std::to_string(i);d.kind="chat";d.unit="u"+std::to_string(i/4);
    d.text=samples[i];d.date="2026-09-"+std::to_string(10+i);d.title="fixture";
    corpus.docs.push_back(d);
  }
  corpus.reindex();
  auto stats=compute_stats(corpus POLICY);
  Json terms=Json::array();std::vector<std::size_t> hits;std::vector<Item> all;
  for(std::size_t i=0;i<corpus.docs.size();++i) {
    if (i >= 16) hits.push_back(i);
    terms.push_back(salient_terms(stats,i,10 POLICY));
    for (auto item:extract_items(corpus.docs[i],"archive" POLICY)) all.push_back(item);
  }
  output["salient"]=terms;
  Json added=Json::array();
  for(const auto& term:expand_vocabulary(corpus,stats,hits,{"graph"},1,20,nullptr POLICY)) added.push_back(term.to_json());
  output["added"]=added;
  Json items=Json::array(),edges=Json::array();
  for(const auto& item:all)items.push_back(item.to_json());
  for(const auto& edge:relate_items(all,&stats POLICY))edges.push_back(edge.to_json());
  output["items"]=items;output["edges"]=edges;
  std::vector<std::tuple<int,int,double>> graph{{0,1,2.0},{1,2,1.0},{3,4,2.0},{4,5,1.0},{2,3,.1}};
#ifdef ARCHIVE_NEW
  output["communities"]=louvain(6,graph,-1,&profile);
#else
  output["communities"]=louvain(6,graph);
#endif
  Json digests=Json::array();
  for(const auto& [file,text]: std::vector<std::pair<std::string,std::string>>{
    {"graph.cpp","// Uses graph database.\nclass GraphStore {\n void read_source();\n};\n// TODO: improve cache\n"},
    {"worker.py","import sqlite3\nclass SemanticWorker:\n    def analyse(self):\n        pass\n# FIXME: bad timestamp\n"},
    {"CMakeLists.txt","add_library(loom graph.cpp)\nadd_executable(worker worker.cpp)\n"},
    {"main.ts","import {GraphStore} from './graph';\nexport class ArchiveEngine {\n}\n"}
  }) {
    auto language=code_language(file POLICY);auto d=digest_code(file,language,text POLICY);
    digests.push_back(Json{{"file",file},{"language",language},{"digest",d.text},{"symbols",d.symbols},{"uses",d.uses},{"todos",d.todos}});
  }
  output["digests"]=digests;
  std::cout<<json::dump(output,2)<<"\n";
}
