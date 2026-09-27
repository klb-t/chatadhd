// extract.h: golden cases per artifact type (PL + EN): detection,
// located observations, observed claims with support, decisions with
// alternatives, statuses (incl. lost/restored per version), forks,
// versions, normative statements, areas; determinism; the stage.
#include <doctest/doctest.h>

#include <map>
#include <set>

#include "loom/extract.h"
#include "loom/kb.h"
#include "loom/knowledge.h"
#include "loom/resolve.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

std::shared_ptr<const kb::Pack> pack() {
  static auto p = unwrap(kb::Pack::load_builtin());
  return p;
}

extract::Extraction run(const extract::UnitContent& u) {
  extract::Extractor ex(pack());
  return unwrap(ex.process(u));
}

const model::Entity* entity_labelled(const extract::Extraction& ex, std::string_view kind, std::string_view label) {
  for (const auto& e : ex.entities) {
    if (e.kind == kind && e.label == label) return &e;
  }
  return nullptr;
}

std::vector<const model::Claim*> claims_of(const extract::Extraction& ex, std::string_view pred) {
  std::vector<const model::Claim*> v;
  for (const auto& c : ex.claims) {
    if (c.predicate == pred) v.push_back(&c);
  }
  return v;
}

std::string label_of(const extract::Extraction& ex, const std::string& id) {
  for (const auto& e : ex.entities) {
    if (e.id == id) return e.label;
  }
  return "";
}

// A small Claude-shaped conversation.
extract::UnitContent claude_conv(const std::string& id, const std::vector<std::pair<std::string, std::string>>& msgs,
                                 const std::string& date = "2025-03-01T10:00:00Z") {
  Json cm = Json::array();
  int i = 0;
  for (const auto& [who, text] : msgs) {
    cm.push_back(Json{{"uuid", "m" + std::to_string(i)}, {"sender", who}, {"text", text}, {"created_at", date}});
    ++i;
  }
  Json conv{{"uuid", id}, {"name", id}, {"created_at", date}, {"chat_messages", cm}};
  extract::UnitContent u;
  u.structured = conv;
  u.unit.source = "sha256:test";
  u.unit.kind = "conversation";
  u.unit.locator.source = "sha256:test";
  u.unit.locator.member = "conversations.json";
  u.unit.locator.json_pointer = "/0";
  u.unit.title = id;
  u.unit.date = date;
  u.unit.id = model::Unit::make_id(u.unit.source, u.unit.locator);
  u.unit.attrs = Json{{"ext_id", id}};
  return u;
}

}  // namespace

TEST_SUITE("extract") {
  TEST_CASE("every observation is located and verifiable against its text (I1)") {
    std::string text = "# Brainstorm\n\nWe decided to use SQLite. Never delete raw sources.\n\n- item one\n- item two\n";
    auto u = extract::text_unit("notes/brainstorm.md", text, "2025-01-01");
    auto ex = run(u);
    REQUIRE(!ex.observations.empty());
    for (const auto& o : ex.observations) {
      REQUIRE(o.locator.byte_start);
      REQUIRE(o.locator.byte_len);
      CHECK(text.substr(static_cast<std::size_t>(*o.locator.byte_start), static_cast<std::size_t>(*o.locator.byte_len)) == o.text);
      CHECK(o.id == model::Observation::make_id(o.unit, o.locator, o.text));
      CHECK(o.unit == u.unit.id);
    }
    for (const auto& c : ex.claims) {
      INFO(c.to_json().dump());
      CHECK(c.validate());
      CHECK(c.assessment.evidence == model::EvidenceClass::Observed);
      CHECK(!c.assessment.support.empty());
    }
  }

  TEST_CASE("detection: artifact types by extension, glob, export shape, heading and cue") {
    extract::Extractor ex(pack());
    auto d1 = unwrap(ex.detect(extract::text_unit("src/engine.py", "class GraphEngine:\n    pass\n")));
    REQUIRE(!d1.empty());
    CHECK(d1.front().artifact_type == "codebase");
    auto d2 = unwrap(ex.detect(extract::text_unit("scene.txt", "INT. KITCHEN - NIGHT\n\nANNA\nWhere were you?\n")));
    REQUIRE(!d2.empty());
    CHECK(d2.front().artifact_type == "screenplay");
    auto d3 = unwrap(ex.detect(claude_conv("c1", {{"human", "hello"}})));
    REQUIRE(!d3.empty());
    CHECK(d3.front().artifact_type == "conversation");
    auto d4 = unwrap(ex.detect(extract::text_unit("notes.md", "# Burza mózgów\n\n- a\n- b\n")));
    REQUIRE(!d4.empty());
    CHECK(d4.front().artifact_type == "brainstorm");
    auto d5 = unwrap(ex.detect(extract::text_unit("a.vtt", "WEBVTT\n\n00:00:01.000 --> 00:00:03.000\nJan: hej\n")));
    REQUIRE(!d5.empty());
    CHECK(d5.front().artifact_type == "recording_transcript");
    CHECK(unwrap(ex.detect(extract::text_unit("x.bin", "zzz"))).empty());
  }

  TEST_CASE("conversation (PL): decision with recorded alternatives, versions, statuses, fork") {
    Json conv{{"uuid", "c-sync"},
              {"name", "synchronizacja"},
              {"created_at", "2025-03-01T10:00:00Z"},
              {"current_leaf_message_uuid", "u2b"},
              {"chat_messages",
               Json::array({Json{{"uuid", "u1"}, {"sender", "human"}, {"created_at", "2025-03-01T10:00:00Z"},
                                 {"text", "sync - wlasny protokol, CRDT po WebRTC, czy po prostu sync przez gita?"}},
                            Json{{"uuid", "u2a"}, {"sender", "human"}, {"parent_message_uuid", "u1"}, {"created_at", "2025-03-01T10:04:00Z"},
                                 {"text", "poprawka: wlasny protokol"}},
                            Json{{"uuid", "u2b"}, {"sender", "human"}, {"parent_message_uuid", "u1"}, {"created_at", "2025-03-01T10:08:00Z"},
                                 {"text", "biore sync po gicie, szybciej ruszymy"}},
                            Json{{"uuid", "a2"}, {"sender", "assistant"}, {"parent_message_uuid", "u2b"}, {"created_at", "2025-03-01T10:09:00Z"},
                                 {"text", "0.3.0: sync po gicie, commit per note."}}})}};
    extract::UnitContent u = claude_conv("x", {});
    u.structured = conv;
    auto ex = run(u);
    // decision + alternatives from the question
    REQUIRE(ex.decisions.size() >= 1);
    const auto& d = ex.decisions.front();
    CHECK(d.alternatives.size() == 3);
    REQUIRE(d.chosen());
    CHECK(d.chosen()->label == "sync przez gita");
    // version from the changelog line, implemented feature at that version
    auto vs = claims_of(ex, "has_version");
    REQUIRE(vs.size() == 1);
    CHECK(vs[0]->value == "0.3.0");
    bool implemented = false;
    for (const auto& s : ex.statuses) implemented = implemented || (s.status == model::StatusValue::Implemented && s.version == "0.3.0");
    CHECK(implemented);
    // the edited message is a conversation fork; both sides kept, the current one chosen
    REQUIRE(ex.forks.size() == 1);
    CHECK(ex.forks[0].kind == model::ForkKind::Conversation);
    REQUIRE(ex.forks[0].sides.size() == 2);
    int chosen = 0, abandoned = 0;
    for (const auto& s : ex.forks[0].sides) {
      chosen += s.chosen;
      abandoned += s.abandoned;
    }
    CHECK(chosen == 1);
    CHECK(abandoned == 1);
  }

  TEST_CASE("status per version: implemented -> lost -> restored (PL + EN)") {
    auto u = claude_conv("c-st", {{"human", "0.5.0: checklisty jako wariant danych notatki."},
                                  {"human", "cos jest nie tak z 0.9.0, checklisty sie zgubily przy porcie."},
                                  {"assistant", "1.0.0: sync v2. A checklisty?"},
                                  {"human", "przywrocone w tej samej wersji."},
                                  {"human", "In 1.2.0 the export button disappeared again."}});
    auto ex = run(u);
    std::map<std::string, std::string> by_version;  // version -> status of the checklist feature
    std::string checklist;
    for (const auto& s : ex.statuses) {
      if (label_of(ex, s.entity).rfind("checklist", 0) == 0) {
        by_version[s.version] = std::string(model::to_string(s.status));
        checklist = s.entity;
      }
    }
    CHECK(by_version["0.5.0"] == "implemented");
    CHECK(by_version["0.9.0"] == "lost");
    CHECK(by_version["1.0.0"] == "restored");
    bool lost_en = false;
    for (const auto& s : ex.statuses) lost_en = lost_en || (s.status == model::StatusValue::Lost && s.version == "1.2.0");
    CHECK(lost_en);
    // the history is ordered per entity and branch, previous filled in
    for (const auto& s : ex.statuses) {
      if (s.entity == checklist && s.version == "1.0.0") {
        REQUIRE(s.previous);
        CHECK(*s.previous == model::StatusValue::Lost);
      }
    }
  }

  TEST_CASE("brainstorm (PL): generalization -> area, members, constraint, candidate principle; EN too") {
    auto u = extract::text_unit("brainstorm.md",
                                "# Burza mózgów\n\nWszystko o storage: SQLite dla danych strukturalnych, pliki blob dla "
                                "załączników, nic w chmurze bez zgody użytkownika.\n\nEverything about alerts: desktop "
                                "notification, a log entry, optionally e-mail - always local first.\n",
                                "2025-01-10");
    auto ex = run(u);
    REQUIRE(ex.areas.size() == 2);
    std::map<std::size_t, int> sizes;
    for (const auto& a : ex.areas) {
      CHECK(!a.principle.empty());
      CHECK(!a.gap);
      sizes[a.members.size()]++;
      bool found = false;
      for (const auto& p : ex.principles) {
        if (p.id == a.principle) {
          found = true;
          CHECK(p.validation == model::ValidationStatus::Candidate);
          REQUIRE(p.scope.areas.size() == 1);
          CHECK(p.scope.areas[0] == a.id);
        }
      }
      CHECK(found);
    }
    CHECK(sizes[2] == 1);  // PL: "nic w chmurze ..." is a constraint, not a member
    CHECK(sizes[3] == 1);  // EN
    // the negated item delimits the area
    bool constraint = false;
    for (const auto* c : claims_of(ex, "has_invariant")) constraint = constraint || c->value.get<std::string>().find("nic w chmurze") == 0;
    CHECK(constraint);
    // members are classified against roles
    for (const auto& it : ex.items) CHECK(!it.area.empty());
  }

  TEST_CASE("normative statements become candidate principles (PL + EN)") {
    auto ex = run(extract::text_unit("rules.md", "Nigdy nie kasuję porzuconej gałęzi.\n\nBy default, keep the raw export.\n\nThe sky is blue.\n"));
    REQUIRE(ex.principles.size() == 2);
    std::set<model::PrincipleForm> forms;
    for (const auto& p : ex.principles) forms.insert(p.form);
    CHECK(forms.count(model::PrincipleForm::Invariant));
    CHECK(forms.count(model::PrincipleForm::Default));
    CHECK(claims_of(ex, "states_principle").size() == 2);
  }

  TEST_CASE("lexicon entities with context gates: Loom the kernel vs a weaving loom") {
    auto kernel = run(extract::text_unit("a.md", "Loom is the C++ kernel with a C ABI, the runtime under ChatADHD.\n"));
    CHECK(entity_labelled(kernel, "project", "Loom") != nullptr);
    auto weaving = run(extract::text_unit("b.md", "Kupiłam mamie małe krosno tkackie (loom) do makramy.\n"));
    CHECK(entity_labelled(weaving, "project", "Loom") == nullptr);
    CHECK(!weaving.stats["gated"].empty());
    // PL + EN aliases of one profile project map to one entity
    auto pl = run(extract::text_unit("c.md", "Pracuję nad czat ADHD na Androida.\n"));
    auto en = run(extract::text_unit("d.md", "I work on ChatADHD for Android.\n"));
    auto* e1 = entity_labelled(pl, "project", "ChatADHD");
    auto* e2 = entity_labelled(en, "project", "ChatADHD");
    REQUIRE(e1);
    REQUIRE(e2);
    CHECK(e1->id == e2->id);
  }

  TEST_CASE("relation patterns (EN + PL) produce typed claims") {
    auto ex = run(extract::text_unit("e.md", "ChatADHD runs on Android and desktop. ChatADHD działa na Androidzie.\n"));
    auto tp = claims_of(ex, "targets_platform");
    std::set<std::string> platforms;
    for (const auto* c : tp) platforms.insert(label_of(ex, c->object));
    CHECK(platforms.count("Android"));
    CHECK(platforms.count("Desktop"));
    for (const auto* c : tp) CHECK(c->assessment.support.front().quote.find("ChatADHD") != std::string::npos);
  }

  TEST_CASE("codebase: symbols become components, declared version") {
    auto ex = run(extract::text_unit("engine/graph_engine.py",
                                     "__version__ = \"0.7.9\"\n\nclass GraphEngine:\n    def ingest(self):\n        pass\n"));
    CHECK(entity_labelled(ex, "component", "GraphEngine") != nullptr);
    auto vs = claims_of(ex, "has_version");
    REQUIRE(vs.size() == 1);
    CHECK(vs[0]->value == "0.7.9");
  }

  TEST_CASE("commit fields, email headers, transcript speakers, screenplay characters, pleading citations") {
    Json commit{{"hash", "46d0ba7"}, {"date", "2026-02-11T20:11:51Z"}, {"author", "Ola"}, {"subject", "Version 0.7.9"},
                {"body", "Adds the batch worker."}, {"files", Json::array({"M engine/semantic_worker.py"})}};
    extract::UnitContent cu = extract::text_unit("git log", "");
    cu.structured = commit;
    auto c = run(cu);
    auto vs = claims_of(c, "has_version");
    REQUIRE(!vs.empty());
    CHECK(vs[0]->value == "0.7.9");

    auto em = run(extract::text_unit("m.eml", "From: Zenon Kowalczyk <z@example.invalid>\nTo: Ola <o@example.invalid>\nSubject: kaucja\n"
                                              "Date: 2026-01-12\n\nNie zwrócę kaucji.\n"));
    CHECK(entity_labelled(em, "party", "Zenon Kowalczyk") != nullptr);
    CHECK(entity_labelled(em, "party", "Ola") != nullptr);

    auto tr = run(extract::text_unit("call.vtt", "WEBVTT\n\n00:00:01.000 --> 00:00:04.000\nAnna: We ship on Friday.\n"));
    REQUIRE(entity_labelled(tr, "actor", "Anna") != nullptr);
    bool timed = false;
    for (const auto& o : tr.observations) timed = timed || (o.locator.time_start && *o.locator.time_start == 1.0);
    CHECK(timed);

    auto sp = run(extract::text_unit("film.fountain", "INT. KITCHEN - NIGHT\n\nANNA\nWhere were you?\n"));
    CHECK(entity_labelled(sp, "character", "ANNA") != nullptr);

    auto pl = run(extract::text_unit("pozew.txt", "Uzasadnienie\n\n1. Powód wezwał pozwanego na podstawie KL art. 12 ust. 3.\n"));
    bool cited = false;
    for (const auto& e : pl.entities) cited = cited || (e.kind == "citation" && e.label == "KL art. 12 ust. 3");
    CHECK(cited);
  }

  TEST_CASE("classify_items: brainstorm items against a project kind's domain kinds") {
    extract::Extractor ex(pack());
    auto pk = unwrap(model::project_kind(*pack(), "software_app"));
    std::vector<model::Observation> items(3);
    items[0].id = "ob_1";
    items[0].text = "SQLite for structured data";
    items[1].id = "ob_2";
    items[1].text = "moduł synchronizacji";
    items[2].id = "ob_3";
    items[2].text = "ciepłe brzmienie";
    auto out = unwrap(ex.classify_items(pk, {}, items));
    REQUIRE(out.size() == 3);
    CHECK(out[0].kind == "store");
    REQUIRE(out[0].role);
    CHECK(*out[0].role == model::Role::Resource);
    CHECK(out[1].kind == "module");
    CHECK(out[2].kind.empty());  // matches no kind: reported, never forced
    CHECK(!out[2].role);
  }

  TEST_CASE("determinism: same input -> byte-identical extraction") {
    std::string text = "# Plan\n\nWe decided to use SQLite instead of Realm. Wersja 0.2 gotowe.\n";
    auto a = run(extract::text_unit("p.md", text, "2025-02-01"));
    auto b = run(extract::text_unit("p.md", text, "2025-02-01"));
    CHECK(json::canonical(a.to_json()) == json::canonical(b.to_json()));
  }

  TEST_CASE("knowledge stages extract -> resolve -> assess over sources (catalog handing over nothing)") {
    fsutil::TempDir td;
    RuntimeOptions o;
    o.data_dir = td.path().string();
    o.start_workers = false;
    auto rt = unwrap(Runtime::open(o));
    rt->knowledge().set_stage("catalog", [](knowledge::StageContext&) -> Result<Json> {
      return Json{{"output", "no-catalog"}, {"stats", Json::object()}, {"units", Json::array()}};
    });
    rt->knowledge().set_stage("generalize", [](knowledge::StageContext&) -> Result<Json> { return Json{{"output", "g"}, {"stats", Json::object()}}; });
    rt->knowledge().set_stage("materialize", [](knowledge::StageContext&) -> Result<Json> { return Json{{"output", "m"}, {"stats", Json::object()}}; });
    fs::path src = td.path() / "notes.md";
    REQUIRE(fsutil::write_file(src, "# Burza mózgów\n\nWszystko o storage: SQLite, pliki blob.\n\nChatADHD działa na Androidzie. "
                                    "Decyzja: bierzemy SQLite.\n"));
    knowledge::KnowledgeConfig cfg;
    cfg.sources = {src.string()};
    auto r = unwrap(rt->knowledge().run(cfg));
    INFO(r.to_json().dump());
    REQUIRE(r.status == "done");
    auto& st = rt->knowledge().store();
    auto stats = unwrap(st.stats(r.run));
    CHECK(stats["observations"].get<int>() > 0);
    CHECK(stats["claims"].get<int>() > 0);
    CHECK(stats["areas"].get<int>() == 1);
    // assess calibrated every claim into [0,1] from the policy tables
    kb::ClaimQuery q;
    auto claims = unwrap(st.query_claims(r.run, q));
    REQUIRE(!claims.empty());
    for (const auto& c : claims) {
      CHECK(c.assessment.confidence >= 0.0);
      CHECK(c.assessment.confidence <= 1.0);
    }
    // a re-run is a cache hit end to end
    auto r2 = unwrap(rt->knowledge().run(cfg));
    for (const auto& s : r2.stages) CHECK(s.cache_hit);
  }
}
