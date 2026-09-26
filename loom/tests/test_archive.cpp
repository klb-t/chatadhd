// Archive Intelligence: unit tests per stage + end-to-end runs on the fixture
// corpus in tests/fixtures/archive (planted decision fork, superseded
// decision, open questions, PL+EN phrasing, Claude projects/memories, a tiny
// repository with gaps), resume after cancellation, determinism, C ABI.
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>

#include "archive/archive_runtime.h"
#include "loom/archive.h"
#include "loom/db.h"
#include "loom/loom.h"
#include "loom/provenance.h"
#include "loom/runtime.h"
#include "loom/semantic_analyzer.h"
#include "loom/tasks.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::archive;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

const fs::path kFixtures = fs::path(LOOM_TEST_FIXTURES) / "archive";

std::unique_ptr<Runtime> open_rt(const fs::path& dir) {
  RuntimeOptions o;
  o.data_dir = dir.string();
  o.start_workers = false;
  return unwrap(Runtime::open(o));
}

ArchiveConfig fixture_config(const fs::path& out) {
  ArchiveConfig c;
  c.sources = {(kFixtures / "exports").string(), (kFixtures / "docs").string()};
  c.repo = (kFixtures / "repo").string();
  c.git = false;
  c.seed_terms = {"ChatADHD", "graph", "kivy", "importer"};
  c.project = "fixture";
  c.out_dir = out.string();
  return c;
}

std::map<std::string, std::string> read_outputs(const fs::path& dir) {
  std::map<std::string, std::string> m;
  for (const auto& e : fs::directory_iterator(dir)) {
    if (e.path().filename().string()[0] == '.') continue;
    m[e.path().filename().string()] = unwrap(fsutil::read_file(e.path()));
  }
  return m;
}

std::vector<Json> read_jsonl(const std::string& s) {
  std::vector<Json> v;
  std::size_t p = 0;
  while (p < s.size()) {
    std::size_t e = s.find('\n', p);
    if (e == std::string::npos) e = s.size();
    if (e > p) v.push_back(unwrap(json::parse(s.substr(p, e - p))));
    p = e + 1;
  }
  return v;
}

const Json* item_with(const std::vector<Json>& items, std::string_view needle) {
  for (const auto& it : items) {
    if (json::get_string(it, "text").find(needle) != std::string::npos) return &it;
  }
  return nullptr;
}

}  // namespace

TEST_SUITE("archive.text") {
  TEST_CASE("tokenize / candidate terms keep phrases but not paths") {
    CHECK(tokenize("Graph_Engine 2024 ChatADHD, zażółć") == std::vector<std::string>{"graph", "engine", "chatadhd", "zażółć"});
    auto t = candidate_terms("The knowledge graph lives in loom/src/graph_engine.cpp and a vertical-slice test.");
    std::set<std::string> ts(t.begin(), t.end());
    CHECK(ts.count("knowledge graph"));
    CHECK(ts.count("vertical slice"));
    CHECK(!ts.count("loom graph"));   // '/' breaks a phrase
    CHECK(!ts.count("engine cpp"));
    CHECK(!ts.count("the"));
  }

  TEST_CASE("sentences: bullets, tables, headings and code fences") {
    auto s = split_sentences(
        "# Title\n\nWe decided to use SQLite. It is local-first, e.g. one file.\n"
        "- first bullet item here\n- second bullet item here\n\n"
        "| Decision | Class |\n|---|---|\n| Raw sources are immutable | invariant |\n\n"
        "```\nint x = 1; // not a sentence at all\n```\nCzy to działa?\n");
    CHECK(s == std::vector<std::string>{"We decided to use SQLite.", "It is local-first, e.g. one file.",
                                        "first bullet item here", "second bullet item here",
                                        "Raw sources are immutable — invariant", "Czy to działa?"});
  }

  TEST_CASE("dates and identifiers") {
    CHECK(first_date("Stan roboczy: 2026-09-16 (v2)") == "2026-09-16");
    CHECK(first_date("v1.2026-13-40") == "");
    CHECK(normalize_date("2025-02-01T10:00:00+02:00") == "2025-02-01T08:00:00Z");
    CHECK(normalize_date("2025-02-01") == "2025-02-01");
    CHECK(iso_from_epoch(1736503200) == "2025-01-10T10:00:00Z");
    CHECK(split_identifier("IExecutionEnvironment") == std::vector<std::string>{"i", "execution", "environment"});
    CHECK(split_identifier("ingest_analysis") == std::vector<std::string>{"ingest", "analysis"});
    CHECK(camel_identifiers("use `IGraphStore` and ChatADHD, not Graph") ==
          std::vector<std::string>{"IGraphStore", "ChatADHD"});
    CHECK(stem("stores") == "store");
    CHECK(gloss("pamięć") == "memory");
  }
}

TEST_SUITE("archive.ingest") {
  TEST_CASE("markdown sections carry heading paths") {
    auto secs = split_markdown("# Doc\nintro text\n## 4. Layers\n### 4.1 Ingest\nbody one\n```\n# not a heading\n```\n");
    REQUIRE(secs.size() == 2);
    CHECK(secs[0].heading_path == "Doc");
    CHECK(secs[1].heading_path == "4. Layers › 4.1 Ingest");
    CHECK(secs[1].text.find("# not a heading") != std::string::npos);
  }

  TEST_CASE("code digests: symbols, comments, TODO markers") {
    auto d = digest_code("src/x.cpp", "cpp",
                         "// GraphStore keeps nodes.\nclass GraphStore;\nclass GraphStore {\n};\n"
                         "Result<int> GraphStore::upsert(int a) {\n  // TODO: compaction\n  return a;\n}\n"
                         "// mentions TODO later in text\n");
    CHECK(d.symbols == std::vector<std::string>{"GraphStore", "GraphStore::upsert"});
    REQUIRE(d.todos.size() == 1);
    CHECK(d.todos[0].first == 6);
    CHECK(d.todos[0].second == "TODO: compaction");
    auto p = digest_code("imp.py", "python",
                         "\"\"\"Importer module for chat exports.\"\"\"\nclass Imp:\n    def run(self):\n        # FIXME: slow\n        pass\n"
                         "def helper():\n    pass\n");
    CHECK(p.symbols == std::vector<std::string>{"Imp", "Imp.run", "helper"});
    CHECK(p.todos.size() == 1);
    CHECK(p.text.find("Importer module for chat exports.") != std::string::npos);
  }

  TEST_CASE("git log records; trailers dropped") {
    std::string raw = "\x1e" "abc123\x1f" "2025-01-02T03:04:05+01:00\x1f" "Ann\x1f" "fix importer\x1f"
                      "Body line.\n\nCo-Authored-By: Someone <x@y>\n\x1f" "\nM\tsrc/a.cpp\nR100\told.py\tnew.py\n"
                      "\x1e" "def456\x1f" "2025-01-01T00:00:00Z\x1f" "Bob\x1f" "init\x1f\x1f" "\nA\tREADME.md\n";
    auto c = parse_git_log(raw);
    REQUIRE(c.size() == 2);
    CHECK(c[0].hash == "abc123");
    CHECK(c[0].date == "2025-01-02T02:04:05Z");
    CHECK(c[0].body == "Body line.");
    CHECK(c[0].files == std::vector<std::string>{"M src/a.cpp", "R new.py"});
    CHECK(c[1].files == std::vector<std::string>{"A README.md"});
  }

  TEST_CASE("ChatGPT walk keeps timestamps, branches and the current path") {
    Json convs = unwrap(json::parse(unwrap(fsutil::read_file(kFixtures / "exports/chatgpt/conversations.json"))));
    ChatWalk w = walk_chatgpt(convs[0]);
    CHECK(w.title == "Random thoughts");
    REQUIRE(w.messages.size() == 6);
    CHECK(w.messages[0]["date"] == "2025-01-10T10:00:00Z");
    CHECK(w.messages[2]["branch"] == "/0");
    CHECK(w.messages[2]["current"] == false);
    CHECK(w.messages[4]["branch"] == "/1");
    CHECK(w.messages[4]["current"] == true);
    CHECK(w.messages[4]["parent"] == "a1");
    REQUIRE(w.forks.size() == 1);
    CHECK(w.forks[0]["node"] == "a1");
    CHECK(w.forks[0]["alternatives"][1]["current"] == true);
    CHECK(sniff_export_element(convs[0]) == "chatgpt");
  }

  TEST_CASE("Claude walk and export sniffing") {
    Json convs = unwrap(json::parse(unwrap(fsutil::read_file(kFixtures / "exports/claude/conversations.json"))));
    ChatWalk w = walk_claude(convs[0]);
    REQUIRE(w.messages.size() == 7);
    CHECK(w.messages[0]["role"] == "user");
    CHECK(w.messages[1]["role"] == "assistant");
    CHECK(w.forks.empty());
    CHECK(sniff_export_element(convs[0]) == "claude");
    Json proj = unwrap(json::parse(unwrap(fsutil::read_file(kFixtures / "exports/claude/projects.json"))));
    CHECK(sniff_export_element(proj[0]) == "claude_projects");
    Json mem = unwrap(json::parse(unwrap(fsutil::read_file(kFixtures / "exports/claude/memories.json"))));
    CHECK(sniff_export_element(mem[0]) == "claude_memories");
  }
}

TEST_SUITE("archive.items") {
  TEST_CASE("bilingual cue classifier") {
    struct Case {
      const char* text;
      const char* heading;
      const char* type;
    };
    const Case cases[] = {
        {"We decided to use SQLite for the graph.", "", "decision"},
        {"Zdecydowaliśmy, że importer musi zachować surowe źródło.", "", "decision"},
        {"Decision: we drop Kivy and use Compose instead.", "", "decision"},
        {"We decided against Neo4j because it needs a server.", "", "rejected_option"},
        {"Odrzucamy pomysł osobnego serwera grafu.", "", "rejected_option"},
        {"Should we encrypt sync end-to-end?", "", "open_question"},
        {"Czy synchronizacja ma być szyfrowana?", "", "open_question"},
        {"Open question: which embedding model is the default", "", "open_question"},
        {"JSON vs YAML vs protobuf jako external representation.", "10. Otwarte decyzje — celowo NIE zamknięte",
         "open_question"},
        {"The importer must keep attachments.", "", "requirement"},
        {"Aplikacja musi działać offline.", "", "requirement"},
        {"Raw source bytes are never modified.", "", "invariant"},
        {"Surowego źródła nie wolno nadpisywać.", "", "invariant"},
        {"What if we render the graph in 3D?", "", "open_question"},
        {"Pomysł: widok osi czasu dla forków.", "", "idea"},
        {"Implemented the FTS5 index with a LIKE fallback.", "", "implementation"},
        {"The importer crashes on nested zips.", "", "bug"},
        {"Nie działa import plików MHT.", "", "bug"},
        {"The weather is nice today.", "", ""},
        {"source map,", "16. Następny sensowny invariant implementation target", ""},
    };
    for (const auto& c : cases) {
      INFO(c.text);
      CHECK(classify_sentence(c.text, c.heading).type == c.type);
    }
    auto k = classify_sentence("We decided to drop Kivy.");
    CHECK(k.polarity == -1);
    CHECK(k.confidence > 0.5);
  }

  TEST_CASE("supersession, resolution, no contradiction inside one unit") {
    Doc a;
    a.key = "dA";
    a.unit = "u1";
    a.kind = "chat";
    a.date = "2025-02-01T08:00:00Z";
    a.text = "We decided to use Kivy for the mobile UI because it runs on Android.";
    Doc b = a;
    b.key = "dB";
    b.unit = "u2";
    b.date = "2025-06-01T12:00:00Z";
    b.text = "Decision: we drop Kivy and use Compose Multiplatform for the mobile UI instead.";
    Doc q = a;
    q.key = "dQ";
    q.unit = "u3";
    q.date = "2025-01-10T00:00:00Z";
    q.text = "Should the mobile UI use Kivy on Android?";
    std::vector<Item> items;
    for (const Doc* d : {&a, &b, &q}) {
      for (auto& it : extract_items(*d, "T1")) items.push_back(std::move(it));
    }
    REQUIRE(items.size() == 3);
    auto edges = relate_items(items);
    std::map<std::string, std::string> type_of;
    for (const auto& e : edges) type_of[e.type] = e.src + ">" + e.dst;
    REQUIRE(type_of.count("supersedes"));
    CHECK(type_of.count("resolves"));
    for (const auto& it : items) {
      if (it.doc == "dA") CHECK(it.status == "superseded");
      if (it.doc == "dQ") CHECK(it.status == "resolved");
      if (it.doc == "dB") CHECK(it.status == "active");
    }
    // commits and code TODOs
    Doc c;
    c.key = "g1";
    c.kind = "commit";
    c.label = "abc fix crash in importer";
    c.extra = Json{{"subject", "fix crash in importer"}};
    CHECK(extract_items(c, "T1").at(0).type == "bug");
  }
}

TEST_SUITE("archive.cluster") {
  TEST_CASE("louvain splits two cliques and is deterministic") {
    std::vector<std::tuple<int, int, double>> e;
    for (int a = 0; a < 4; ++a) {
      for (int b = a + 1; b < 4; ++b) {
        e.emplace_back(a, b, 1.0);
        e.emplace_back(a + 4, b + 4, 1.0);
      }
    }
    e.emplace_back(3, 4, 0.1);
    auto c1 = louvain(8, e);
    auto c2 = louvain(8, e);
    CHECK(c1 == c2);
    CHECK(c1 == std::vector<int>{0, 0, 0, 0, 1, 1, 1, 1});
    CHECK(louvain(3, {}) == std::vector<int>{0, 1, 2});
  }
}

TEST_SUITE("archive.vocab") {
  TEST_CASE("expansion records reasons and evidence") {
    Corpus c;
    auto add = [&](std::string key, std::string text) {
      Doc d;
      d.key = std::move(key);
      d.kind = "doc";
      d.text = std::move(text);
      c.docs.push_back(std::move(d));
    };
    add("d1", "The ChatADHD graph uses SQLite with WAL mode.");
    add("d2", "ChatADHD stores the graph in SQLite tables; WAL mode keeps writes safe.");
    add("d3", "SQLite WAL mode for the ChatADHD graph.");
    for (int i = 0; i < 12; ++i) add("x" + std::to_string(i), "Unrelated cooking recipe number with tomatoes and basil.");
    c.reindex();
    auto st = compute_stats(c);
    auto an = unwrap(SemanticAnalyzer::create());
    auto added = expand_vocabulary(c, st, {0, 1, 2}, {"chatadhd", "graph"}, 1, 3, an.get());
    REQUIRE(!added.empty());
    std::set<std::string> terms;
    for (const auto& t : added) {
      terms.insert(t.term);
      CHECK(!t.reasons.empty());
      CHECK(!t.evidence.empty());
      CHECK(t.origin == "expansion");
    }
    CHECK(terms.count("sqlite"));
    CHECK(!terms.count("tomatoes"));
  }
}

TEST_SUITE("archive.pipeline") {
  TEST_CASE("fixture corpus: planted fork, supersession, open questions, gaps") {
    fsutil::TempDir td;
    auto rt = open_rt(td.path() / "data");
    ArchiveConfig cfg = fixture_config(td.path() / "out");
    auto r = unwrap(rt->archive().run(cfg));
    REQUIRE(r.status == "done");
    auto out = read_outputs(td.path() / "out");
    for (const char* f : {"MASTER.md", "gap_report.md", "source_map.csv", "timeline.json", "items.jsonl", "graph.json",
                          "project_manifest.json", "task_log.jsonl"}) {
      INFO(f);
      CHECK(out.count(f));
    }
    CHECK(fs::exists(td.path() / "out" / ".loom-archive"));

    // corpus: chat (ChatGPT + Claude), project docs, memories, spec, code
    auto items = read_jsonl(out["items.jsonl"]);
    const Json* kivy = item_with(items, "We decided to use Kivy");
    REQUIRE(kivy);
    CHECK((*kivy)["status"] == "superseded");
    CHECK((*kivy)["relations"][0]["type"] == "supersedes_by");
    const Json* drop = item_with(items, "we drop Kivy");
    REQUIRE(drop);
    CHECK((*drop)["type"] == "decision");
    const Json* pl = item_with(items, "Zdecydowaliśmy");
    REQUIRE(pl);
    CHECK((*pl)["type"] == "decision");
    const Json* rej = item_with(items, "Odrzucamy");
    REQUIRE(rej);
    CHECK((*rej)["type"] == "rejected_option");
    const Json* q1 = item_with(items, "Czy synchronizacja");
    REQUIRE(q1);
    CHECK((*q1)["type"] == "open_question");
    const Json* q2 = item_with(items, "Should we store the ChatADHD knowledge graph");
    REQUIRE(q2);
    CHECK((*q2)["status"] == "resolved");
    const Json* inv = item_with(items, "Raw source bytes are never modified");
    REQUIRE(inv);
    CHECK((*inv)["type"] == "invariant");
    CHECK(item_with(items, "TODO: add graph compaction"));
    CHECK(item_with(items, "FIXME: the html parser"));

    // timeline: the ChatGPT fork with its kept and abandoned branch
    Json tl = unwrap(json::parse(out["timeline.json"]));
    REQUIRE(tl["forks"].size() == 1);
    CHECK(tl["forks"][0]["origin"] == "chatgpt");
    CHECK(tl["forks"][0]["alternatives"][0]["current"] == false);
    CHECK(tl["forks"][0]["alternatives"][1]["current"] == true);

    // links in MASTER.md: [title › msg @ date]
    const std::string& master = out["MASTER.md"];
    CHECK(master.find("[Untitled › #1 user @ 2025-02-01]") != std::string::npos);
    CHECK(master.find("**superseded** by Decision: we drop Kivy") != std::string::npos);
    CHECK(master.find("abandoned [Random thoughts › #3 user @ 2025-01-10]") != std::string::npos);
    CHECK(master.find("[Project: ChatADHD project") != std::string::npos);

    // gap report: interface missing, concrete class without interface, missing features
    const std::string& gap = out["gap_report.md"];
    CHECK(gap.find("| `IExecutionEnvironment` | missing |") != std::string::npos);
    CHECK(gap.find("| `IGraphStore` | partial | concrete `GraphStore` exists") != std::string::npos);
    CHECK(gap.find("- **missing**: Voice input (ASR)") != std::string::npos);
    CHECK(gap.find("- **missing**: Compose renderer") != std::string::npos);
    CHECK(gap.find("Graph store → `src/graph_store.cpp`") != std::string::npos);
    CHECK(gap.find("**Compose Multiplatform**") != std::string::npos);
    Json manifest = unwrap(json::parse(out["project_manifest.json"]));
    CHECK(manifest["project"] == "fixture");
    CHECK(!manifest["open_questions"].empty());
    CHECK(!manifest["components"].empty());

    // source map resolves refs; artifacts registered; graph nodes for items
    CHECK(out["source_map.csv"].rfind("ref,key,kind,title,location,date,uri,theme,score,terms\n", 0) == 0);
    auto arts = unwrap(rt->provenance().list_artifacts(100));
    CHECK(arts.size() == 8);
    CHECK(arts[0].task_id == r.run_id);
    CHECK(!unwrap(rt->db().list_nodes(std::string_view("decision"))).empty());
    bool supersedes = false;
    for (const auto& l : unwrap(rt->db().get_links(std::nullopt, std::string_view("supersedes")))) supersedes |= !l.src.empty();
    CHECK(supersedes);
    // every stage is a task with hashes; conversations carry provenance
    for (const auto& s : r.stages) {
      INFO(s.stage);
      CHECK(!s.input_hash.empty());
      auto t = unwrap(rt->tasks().get(s.task_id));
      REQUIRE(t);
      CHECK(t->status == "done");
    }
    auto convs = unwrap(rt->db().list_convs(100));
    CHECK(convs.size() >= 8);
    bool chat_prov = false;
    for (const auto& c : convs) {
      if (c.source != "archive.chat") continue;
      auto msgs = unwrap(rt->db().get_msgs(c.id));
      chat_prov |= !unwrap(rt->provenance().for_subject(msgs.at(0).id)).empty();
    }
    CHECK(chat_prov);

    // re-run with unchanged inputs: every stage is a cache hit
    auto r2 = unwrap(rt->archive().run(cfg));
    CHECK(r2.status == "done");
    for (const auto& s : r2.stages) {
      INFO(s.stage);
      if (s.stage != "materialize") CHECK(s.cache_hit);
    }
    CHECK(read_outputs(td.path() / "out") == out);
    auto st = unwrap(rt->archive().status(""));
    CHECK(st["run"]["id"] == r2.run_id);
    CHECK(st["artifacts"].size() == 8);
  }

  TEST_CASE("determinism: two data directories give byte-identical artifacts") {
    fsutil::TempDir t1, t2;
    std::map<std::string, std::string> o1, o2;
    {
      auto rt = open_rt(t1.path() / "data");
      REQUIRE(unwrap(rt->archive().run(fixture_config(t1.path() / "out"))).status == "done");
      o1 = read_outputs(t1.path() / "out");
    }
    {
      auto rt = open_rt(t2.path() / "data");
      REQUIRE(unwrap(rt->archive().run(fixture_config(t2.path() / "out"))).status == "done");
      o2 = read_outputs(t2.path() / "out");
    }
    REQUIRE(o1.size() == 8);
    for (const auto& [name, content] : o1) {
      INFO(name);
      CHECK(o2[name] == content);
    }
  }

  TEST_CASE("cancel mid-run, resume from the checkpoint, identical outputs") {
    fsutil::TempDir base, td;
    std::map<std::string, std::string> baseline;
    {
      auto rt = open_rt(base.path() / "data");
      REQUIRE(unwrap(rt->archive().run(fixture_config(base.path() / "out"))).status == "done");
      baseline = read_outputs(base.path() / "out");
    }
    auto rt = open_rt(td.path() / "data");
    ArchiveConfig cfg = fixture_config(td.path() / "out");
    CancelToken token;
    int ingest_calls = 0;
    auto progress = [&](std::string_view stage, std::int64_t, std::int64_t, std::string_view) {
      if (stage == "ingest" && ++ingest_calls == 3) token.cancel();
    };
    auto r1 = unwrap(rt->archive().run(cfg, progress, &token));
    CHECK(r1.status == "paused");
    CHECK(!fs::exists(td.path() / "out" / "MASTER.md"));
    TaskFilter f;
    f.kind = "archive.ingest";
    auto ingest = unwrap(rt->tasks().list(f));
    REQUIRE(ingest.size() == 1);
    CHECK(ingest[0].status == "paused");
    REQUIRE(ingest[0].checkpoint);
    CHECK(json::get_int(*ingest[0].checkpoint, "next") >= 2);

    auto r2 = unwrap(rt->archive().run(cfg));
    CHECK(r2.status == "done");
    REQUIRE(!r2.stages.empty());
    CHECK(r2.stages[0].stage == "ingest");
    CHECK(r2.stages[0].resumed);
    CHECK(r2.stages[0].task_id == ingest[0].id);
    auto out = read_outputs(td.path() / "out");
    REQUIRE(out.size() == baseline.size());
    for (const auto& [name, content] : baseline) {
      INFO(name);
      CHECK(out[name] == content);
    }
  }

  TEST_CASE("git history adapter (desktop only)") {
    if (std::system("git --version > /dev/null 2>&1") != 0) return;  // no git on this machine
    fsutil::TempDir td;
    fs::path repo = td.path() / "repo";
    fs::create_directories(repo / "src");
    fs::copy(kFixtures / "repo", repo, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    std::string env = "GIT_AUTHOR_NAME=T GIT_AUTHOR_EMAIL=t@x GIT_COMMITTER_NAME=T GIT_COMMITTER_EMAIL=t@x "
                      "GIT_AUTHOR_DATE=2025-03-02T10:00:00Z GIT_COMMITTER_DATE=2025-03-02T10:00:00Z ";
    std::string q = "'" + repo.string() + "'";
    REQUIRE(std::system(("cd " + q + " && git init -q && git add -A && " + env +
                         "git commit -q -m 'Add graph store and importer' -m 'Co-Authored-By: X <x@y>'")
                            .c_str()) == 0);
    auto rt = open_rt(td.path() / "data");
    ArchiveConfig cfg;
    cfg.repo = repo.string();
    cfg.seed_terms = {"graph", "importer"};
    cfg.out_dir = (td.path() / "out").string();
    auto r = unwrap(rt->archive().run(cfg));
    REQUIRE(r.status == "done");
    auto master = unwrap(fsutil::read_file(td.path() / "out" / "MASTER.md"));
    CHECK(master.find("commits | 1 |") != std::string::npos);
    CHECK(master.find("Add graph store and importer @ 2025-03-02]") != std::string::npos);
    CHECK(master.find("Co-Authored-By") == std::string::npos);
    // code files are dated by their last commit
    CHECK(master.find("[src/graph_store.cpp @ 2025-03-02]") != std::string::npos);
  }

  TEST_CASE("config parsing") {
    CHECK(!ArchiveConfig::from_json(Json{{"nope", 1}}));
    CHECK(!ArchiveConfig::from_json(Json{{"llm", "maybe"}}));
    auto c = unwrap(ArchiveConfig::from_json(Json{{"sources", "a.zip"}, {"max_passes", 99}, {"repo", "."}}));
    CHECK(c.sources == std::vector<std::string>{"a.zip"});
    CHECK(c.max_passes == 20);
    CHECK(unwrap(ArchiveConfig::from_json(c.to_json())).to_json() == c.to_json());
    fsutil::TempDir td;
    auto rt = open_rt(td.path() / "data");
    ArchiveConfig bad;
    bad.sources = {(td.path() / "missing").string()};
    auto r = rt->archive().run(bad);
    CHECK(!r);
  }
}

TEST_SUITE("archive.capi") {
  TEST_CASE("loom_archive_run / status / artifacts / import_file_ex") {
    fsutil::TempDir td;
    std::string opts = Json{{"data_dir", (td.path() / "data").string()}, {"start_workers", false}}.dump();
    LoomContext* ctx = loom_init_ex(opts.c_str(), nullptr);
    REQUIRE(ctx);
    auto take = [](const char* s) {
      Json j = json::parse_or(s ? s : "", Json(nullptr));
      loom_free_string(s);
      return j;
    };
    CHECK(loom_archive_cancel(ctx) == LOOM_E_NOT_FOUND);
    Json cfg = fixture_config(td.path() / "out").to_json();
    int calls = 0;
    Json r = take(loom_archive_run(ctx, json::dump(cfg).c_str(),
                                   [](int, int, const char* status, void* ud) {
                                     CHECK(std::string(status).find(": ") != std::string::npos);
                                     ++*static_cast<int*>(ud);
                                   },
                                   &calls));
    REQUIRE(r["status"] == "done");
    CHECK(calls > 0);
    CHECK(take(loom_archive_run(ctx, "{\"bogus\":1}", nullptr, nullptr))["error"]["code"] == "invalid_argument");
    Json st = take(loom_archive_status(ctx, nullptr));
    CHECK(st["run"]["id"] == r["run_id"]);
    Json arts = take(loom_list_artifacts(ctx, "{\"kind\":\"archive.MASTER\"}"));
    REQUIRE(arts.size() == 1);
    std::string id = arts[0]["id"];
    Json a = take(loom_get_artifact(ctx, id.c_str(), 1));
    CHECK(a["content"].get<std::string>().rfind("# fixture — MASTER", 0) == 0);
    CHECK(!take(loom_get_artifact(ctx, id.c_str(), 0)).contains("content"));
    CHECK(take(loom_get_artifact(ctx, "a_nope", 1))["error"]["code"] == "not_found");
    CHECK(take(loom_list_artifacts(ctx, (Json{{"run_id", r["run_id"]}}).dump().c_str())).size() == 8);

    std::string md = (td.path() / "c.md").string();
    {
      std::ofstream f(md);
      f << "## User\nhello there\n\n## Assistant\ngeneral kenobi\n";
    }
    Json i1 = take(loom_import_file_ex(ctx, md.c_str(), "{\"title\":\"T\"}", nullptr, nullptr));
    CHECK(i1["messages"] == 2);
    CHECK(take(loom_import_file_ex(ctx, md.c_str(), nullptr, nullptr, nullptr))["already_imported"] == true);
    CHECK(take(loom_import_file_ex(ctx, md.c_str(), "{\"force\":true}", nullptr, nullptr))["already_imported"] == false);
    CHECK(take(loom_import_file_ex(ctx, md.c_str(), "{\"x\":1}", nullptr, nullptr))["error"]["code"] ==
          "invalid_argument");
    loom_shutdown(ctx);
  }
}
