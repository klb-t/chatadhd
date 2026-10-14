// Public synthetic conversations test mechanism, not LLM quality. No injected
// claims, fake pipeline stages, provider calls or private corpora.
#include <doctest/doctest.h>

#include <algorithm>
#include <set>

#include "loom/extract.h"
#include "loom/generalize.h"
#include "loom/knowledge.h"
#include "loom/runtime.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
std::shared_ptr<const kb::Pack> builtin() {
  static auto pack = unwrap(kb::Pack::load_builtin());
  return pack;
}

Json messages(bool polish = false) {
  const std::vector<std::string> texts = polish ? std::vector<std::string>{
      "Projekt: Skylark.", "Skylark ma moduł RelayEngine. Skylark ma moduł ReviewEngine.",
      "RelayEngine wywołuje terminal.", "ReviewEngine sprawdza RelayEngine.",
      "ReviewEngine przekazuje wynik do RelayEngine."} : std::vector<std::string>{
      "App: Skylark.", "Skylark has component RelayEngine. Skylark has component ReviewEngine.",
      "RelayEngine calls terminal.", "ReviewEngine verifies RelayEngine.",
      "ReviewEngine returns feedback to RelayEngine."};
  Json out = Json::array();
  for (std::size_t i = 0; i < texts.size(); ++i)
    out.push_back(Json{{"id", "message_" + std::to_string(i)}, {"role", "user"}, {"text", texts[i]},
                       {"status", "active"}, {"created", "2026-10-14T00:00:00Z"}});
  return out;
}

generalize::Evidence evidence(const Json& rows, std::shared_ptr<const kb::Pack> pack = builtin()) {
  auto content = extract::conversation_unit("public-example", "Public synthetic example", rows);
  auto extracted = extract::extract_units(pack, {content});
  generalize::Evidence out;
  out.entities = extracted.entities;
  out.claims = extracted.claims;
  out.observations = extracted.observations;
  return out;
}

const generalize::Match* recognized(const std::vector<generalize::Match>& matches) {
  for (const auto& m : matches)
    if (std::find(m.instance.facets.begin(), m.instance.facets.end(), "feedback_execution") != m.instance.facets.end()) return &m;
  return nullptr;
}

Json proof_of(const model::Instance& instance) {
  for (const auto& reason : instance.recognition.at("reasons"))
    if (json::get_string(reason, "facet") == "feedback_execution")
      return reason.at("anchors").at(0).at("evidence");
  return Json();
}

std::shared_ptr<const kb::Pack> configured(const std::function<void(Json&)>& change) {
  std::map<std::string, Json> docs;
  for (const auto& file : builtin()->files()) docs[file] = builtin()->file(file);
  change(docs["facets/feedback_execution.json"]["anchors"]["all"][0]);
  return unwrap(kb::Pack::from_documents(std::move(docs)));
}
}  // namespace

TEST_SUITE("implicit_structures") {
  TEST_CASE("unnamed structure is recognized from PL and EN conversation relations") {
    for (bool polish : {false, true}) {
      const auto rows = messages(polish);
      CHECK(rows.dump().find("harness") == std::string::npos);
      auto ev = evidence(rows);
      const auto matches = unwrap(generalize::ParadigmMatcher(builtin()).match_projects(ev));
      INFO(json::dump(Json{{"claims", [&] { Json c = Json::array(); for (auto& x : ev.claims) c.push_back(x.to_json()); return c; }()}}));
      const auto* found = recognized(matches);
      REQUIRE(found);
      const Json proof = proof_of(found->instance);
      CHECK(proof["status"] == "candidate");
      CHECK(proof["complete"] == true);
      REQUIRE(proof["matches"].size() == 1);
      REQUIRE(proof["matches"][0]["proof"].size() == 5);
      std::set<std::string> supporting;
      for (const auto& binding : proof["matches"][0]["proof"]) {
        auto c = std::find_if(ev.claims.begin(), ev.claims.end(), [&](const auto& claim) { return claim.id == binding["claim"].get<std::string>(); });
        REQUIRE(c != ev.claims.end());
        REQUIRE(!c->assessment.support.empty());
        for (const auto& support : c->assessment.support) supporting.insert(support.observation);
      }
      CHECK(!supporting.empty());
      CHECK(found->instance.recognition["pack_hash"] == builtin()->hash());
      CHECK(found->instance.recognition.dump().find("supervised_workbench") != std::string::npos);
      // No new actor/executor entity was invented by the interpretation.
      CHECK(std::none_of(ev.entities.begin(), ev.entities.end(), [](const auto& e) { return e.label == "agent_harness"; }));
    }
  }

  TEST_CASE("artifact words without connected relationships do not establish a harness") {
    auto rows = messages();
    rows[2]["text"] = "Brainstorm, specification, code, tests, terminal.";
    auto ev = evidence(rows);
    CHECK(!recognized(unwrap(generalize::ParadigmMatcher(builtin()).match_projects(ev))));
  }

  TEST_CASE("direction missing edge and separate branches cannot be repaired by guessing") {
    auto original = evidence(messages());
    for (const std::string mode : {"missing", "reverse", "branch", "version", "rejected", "inferred", "extrapolated", "transferred"}) {
      auto ev = original;
      auto check = std::find_if(ev.claims.begin(), ev.claims.end(), [](const auto& c) { return c.predicate == "verifies"; });
      REQUIRE(check != ev.claims.end());
      if (mode == "missing") ev.claims.erase(check);
      if (mode == "reverse") std::swap(check->subject, check->object);
      if (mode == "branch") check->qualifiers.branch = "different-branch";
      if (mode == "version") check->qualifiers.version = "0.0.1";
      if (mode == "rejected") check->assessment.status = model::ClaimStatus::Rejected;
      if (mode == "inferred") check->assessment.evidence = model::EvidenceClass::Inferred;
      if (mode == "extrapolated") check->assessment.evidence = model::EvidenceClass::Extrapolated;
      if (mode == "transferred") {
        model::Derivation derivation;
        derivation.morphism = "test-source-morphism";
        check->assessment.derivation = derivation;
      }
      INFO(mode);
      CHECK(!recognized(unwrap(generalize::ParadigmMatcher(builtin()).match_projects(ev))));
    }
  }

  TEST_CASE("data changes the pattern and budget exhaustion is explicit unavailable") {
    auto ev = evidence(messages());
    auto stricter = configured([](Json& op) { op["edges"][3]["rel"] = "has_output"; });
    CHECK(!recognized(unwrap(generalize::ParadigmMatcher(stricter).match_projects(ev))));
    auto limited = configured([](Json& op) { op["max_steps"] = 1; });
    const auto incomplete = generalize::ParadigmMatcher(limited).match_projects(ev);
    REQUIRE(!incomplete);
    CHECK(incomplete.error().code == Errc::Unavailable);
    auto unlimited = configured([](Json& op) { op["max_steps"] = 0; op["max_matches"] = 0; });
    CHECK(recognized(unwrap(generalize::ParadigmMatcher(unlimited).match_projects(ev))));
    std::reverse(ev.entities.begin(), ev.entities.end());
    std::reverse(ev.claims.begin(), ev.claims.end());
    auto reordered = unwrap(generalize::ParadigmMatcher(unlimited).match_projects(ev));
    REQUIRE(recognized(reordered));
    CHECK(proof_of(recognized(reordered)->instance) == proof_of(recognized(unwrap(generalize::ParadigmMatcher(unlimited).match_projects(evidence(messages()))))->instance));
  }

  TEST_CASE("qualifier admission distinguishes asserted facts from negative or hypothetical edges") {
    const auto original = evidence(messages());
    for (const auto& [key, values] : std::map<std::string, std::vector<std::string>>{
             {"polarity", {"negative", "unknown"}},
             {"assertion_context", {"hypothetical", "quoted", "unknown"}}}) {
      for (const auto& value : values) {
        auto ev = original;
        auto edge = std::find_if(ev.claims.begin(), ev.claims.end(), [](const auto& c) { return c.predicate == "verifies"; });
        REQUIRE(edge != ev.claims.end());
        edge->qualifiers.extra[key] = value;
        INFO(key, "=", value);
        CHECK(!recognized(unwrap(generalize::ParadigmMatcher(builtin()).match_projects(ev))));
      }
    }
  }

  TEST_CASE("qualifier profiles change admission through the same mechanism") {
    auto strict = configured([](Json& op) {
      op["claim_qualifiers"]["polarity"]["allow_missing"] = false;
    });
    auto ev = evidence(messages());
    CHECK(!recognized(unwrap(generalize::ParadigmMatcher(strict).match_projects(ev))));
    for (auto& claim : ev.claims) claim.qualifiers.extra["polarity"] = "positive";
    CHECK(recognized(unwrap(generalize::ParadigmMatcher(strict).match_projects(ev))));
    for (auto& claim : ev.claims) claim.qualifiers.extra["assertion_context"] = "hypothetical";
    CHECK(!recognized(unwrap(generalize::ParadigmMatcher(strict).match_projects(ev))));
    auto hypotheses = configured([](Json& op) {
      op["claim_qualifiers"]["assertion_context"]["values"] = Json::array({"hypothetical"});
    });
    const auto matches = unwrap(generalize::ParadigmMatcher(hypotheses).match_projects(ev));
    REQUIRE(recognized(matches));
    CHECK(proof_of(recognized(matches)->instance)["status"] == "candidate");
    CHECK(recognized(matches)->instance.recognition["pack_hash"] == hypotheses->hash());
  }

  TEST_CASE("incoming expansion and all distinct bindings use the same generic matcher") {
    auto incoming = configured([](Json& op) {
      for (int i : {0, 1}) {
        std::swap(op["edges"][i]["from"], op["edges"][i]["to"]);
        op["edges"][i]["rel"] = "part_of";
      }
    });
    auto ev = evidence(messages());
    const auto original_claims = ev.claims;
    for (auto edge : original_claims) if (edge.predicate == "has_component") {
      edge.id += "_inverse";
      edge.predicate = "part_of";
      std::swap(edge.subject, edge.object);
      ev.claims.push_back(std::move(edge));
    }
    CHECK(recognized(unwrap(generalize::ParadigmMatcher(incoming).match_projects(ev))));
    auto rows = messages();
    rows.push_back(Json{{"id", "second_executor"}, {"role", "user"}, {"status", "active"},
        {"text", "Skylark has component BackendEngine. BackendEngine calls terminal. ReviewEngine verifies BackendEngine. ReviewEngine returns feedback to BackendEngine."}});
    const auto matches = unwrap(generalize::ParadigmMatcher(builtin()).match_projects(evidence(rows)));
    REQUIRE(recognized(matches));
    CHECK(proof_of(recognized(matches)->instance)["matches"].size() == 2);
    const auto one = configured([](Json& op) { op["max_matches"] = 1; });
    CHECK(recognized(unwrap(generalize::ParadigmMatcher(one).match_projects(evidence(messages())))));
    const auto exceeded = generalize::ParadigmMatcher(one).match_projects(evidence(rows));
    REQUIRE(!exceeded);
    CHECK(exceeded.error().code == Errc::Unavailable);
    const auto steps = proof_of(recognized(matches)->instance)["steps"];
    const auto exact = configured([&](Json& op) { op["max_steps"] = steps; });
    CHECK(recognized(unwrap(generalize::ParadigmMatcher(exact).match_projects(evidence(rows)))));
    const auto insufficient = configured([&](Json& op) { op["max_steps"] = steps.get<std::uint64_t>() - 1; });
    const auto incomplete = generalize::ParadigmMatcher(insufficient).match_projects(evidence(rows));
    REQUIRE(!incomplete);
    CHECK(incomplete.error().code == Errc::Unavailable);
  }

  TEST_CASE("invalid or misspelled pattern constraints fail pack validation") {
    for (const std::string mode : {"unknown_field", "unknown_kind", "unknown_relation", "disconnected", "unknown_node", "budget", "unsafe_evidence", "qualifier_filter"}) {
      std::map<std::string, Json> docs;
      for (const auto& file : builtin()->files()) docs[file] = builtin()->file(file);
      auto& op = docs["facets/feedback_execution.json"]["anchors"]["all"][0];
      if (mode == "unknown_field") op["disctinct"] = op["distinct"];
      if (mode == "unknown_kind") op["nodes"]["tool"] = "undeclared_kind";
      if (mode == "unknown_relation") op["edges"][0]["rel"] = "undeclared_relation";
      if (mode == "disconnected") op["nodes"]["island"] = "component";
      if (mode == "unknown_node") op["edges"][0]["to"] = "ghost";
      if (mode == "budget") op["max_steps"] = -1;
      if (mode == "unsafe_evidence") op["evidence"] = Json::array({"extrapolated"});
      if (mode == "qualifier_filter") op["claim_qualifiers"]["polarity"]["allow_missing"] = "yes";
      INFO(mode);
      CHECK_FALSE(kb::Pack::from_documents(std::move(docs)));
    }
  }

  TEST_CASE("real source pipeline persists recognition and restores it without model calls") {
    fsutil::TempDir dir;
    auto transport = std::make_shared<net::ScriptedTransport>();
    RuntimeOptions options;
    options.data_dir = (dir.path() / "store").string();
    options.start_workers = false;
    options.http = transport;
    const auto path = dir.path() / "conversations.json";
    Json source_messages = Json::array();
    for (const auto& row : messages())
      source_messages.push_back(Json{{"uuid", row["id"]}, {"sender", "human"}, {"text", row["text"]}, {"created_at", row["created"]}});
    Json source = Json::array({Json{{"uuid", "public-example"}, {"name", "Public synthetic example"}, {"chat_messages", source_messages}}});
    LOOM_REQUIRE_OK(fsutil::write_file(path, source.dump()));
    knowledge::KnowledgeConfig cfg;
    cfg.sources = {path.string()};
    cfg.llm = "off";
    cfg.stage_params["catalog"]["import"]["mode"] = "full";
    auto rt = unwrap(Runtime::open(options));
    CancelToken cancel;
    const auto interrupted = unwrap(rt->knowledge().run(cfg,
        [&](std::string_view stage, std::int64_t, std::int64_t, std::string_view) {
          if (stage == "generalize") cancel.cancel();
        }, &cancel));
    CHECK(interrupted.status == "paused");
    CHECK(unwrap(rt->knowledge().store().query_instances(interrupted.run, "software_app")).empty());
    rt.reset();
    rt = unwrap(Runtime::open(options));
    const auto run = unwrap(rt->knowledge().run(cfg));
    CHECK(run.run == interrupted.run);
    CHECK(std::any_of(run.stages.begin(), run.stages.end(), [](const auto& stage) {
      return stage.stage == "generalize" && stage.resumed;
    }));
    INFO(run.error);
    REQUIRE(run.status == "done");
    REQUIRE(run.stages.size() == 6);
    auto instances = unwrap(rt->knowledge().store().query_instances(run.run, "software_app"));
    auto found = std::find_if(instances.begin(), instances.end(), [](const auto& i) { return !proof_of(i).is_null(); });
    REQUIRE(found != instances.end());
    const Json stored = found->to_json();
    rt.reset();
    rt = unwrap(Runtime::open(options));
    instances = unwrap(rt->knowledge().store().query_instances(run.run, "software_app"));
    REQUIRE(instances.size() == 1);
    CHECK(instances[0].to_json() == stored);
    const auto repeat = unwrap(rt->knowledge().run(cfg));
    CHECK(repeat.run == run.run);
    for (const auto& stage : repeat.stages) CHECK(stage.cache_hit);
    CHECK(transport->requests().empty());
    // Source changes yield a new run and never rewrite the historical receipt.
    source[0]["chat_messages"][3]["text"] = "No review relationship is described here.";
    LOOM_REQUIRE_OK(fsutil::write_file(path, source.dump()));
    const auto changed = unwrap(rt->knowledge().run(cfg));
    REQUIRE(changed.status == "done");
    CHECK(changed.run != run.run);
    const auto changed_instances = unwrap(rt->knowledge().store().query_instances(changed.run, "software_app"));
    // Full catalog import includes retained historical units as well as the new
    // snapshot. It must not be represented as a latest-source-only projection.
    REQUIRE(!changed_instances.empty());
    CHECK(changed_instances[0].to_json() != stored);
    const auto history = unwrap(rt->knowledge().store().query_instances(run.run, "software_app"));
    REQUIRE(history.size() == 1);
    CHECK(history[0].to_json() == stored);
    CHECK(transport->requests().empty());
    // Legacy serialized instances retain their exact shape on round-trip.
    Json legacy = stored;
    legacy.erase("recognition");
    CHECK(unwrap(model::Instance::from_json(legacy)).to_json() == legacy);
  }
}
