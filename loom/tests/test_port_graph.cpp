// S2/R12 (B-MOD-09): adapters, role choices and port runs as graph nodes; secrets only as references.
#include <doctest/doctest.h>

#include <set>

#include "loom/media_providers.h"
#include "loom/port_compose.h"
#include "loom/port_graph.h"
#include "loom/config.h"
#include "loom/port_profile.h"
#include "loom/storage_backend.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {

auto fresh_db(const fsutil::TempDir& td) { return loom::test::open_db(td.path() / "pg.db"); }

std::set<std::string> targets(Database& db, const std::string& src, const std::string& type) {
  std::set<std::string> out;
  for (const auto& l : unwrap(db.get_links(src, type)))
    if (l.src == src) out.insert(l.dst);
  return out;
}

std::string label_of(Database& db, const std::string& id) { return (*unwrap(db.get_node(id))).label; }

// Every node and link as one string: the scan target of the secret tests.
std::string whole_graph(Database& db) {
  std::string s;
  for (const auto& n : unwrap(db.list_nodes(std::nullopt, 100000))) s += n.to_json().dump() + "\n";
  for (const auto& l : unwrap(db.get_links())) s += l.metadata.dump() + l.link_type + "\n";
  return s;
}

}  // namespace

TEST_SUITE("port_graph") {
  TEST_CASE("adapters of a registry become nodes that implement the port; idempotent") {
    fsutil::TempDir td;
    auto db = fresh_db(td);
    auto ids = unwrap(port_graph::record_adapters(*db, "asr", port_graph::adapters_of(asr_adapters())));
    CHECK(ids.size() == asr_adapters().names().size());
    CHECK(ids.size() >= 2);
    const std::string port = unwrap(port_graph::port_node(*db, "asr"));
    for (const auto& id : ids) {
      CHECK(targets(*db, id, "implements") == std::set<std::string>{port});
      CHECK((*unwrap(db->get_node(id))).kind == "adapter");
    }
    auto again = unwrap(port_graph::record_adapters(*db, "asr", port_graph::adapters_of(asr_adapters())));
    CHECK(again == ids);
    CHECK(unwrap(db->list_nodes("adapter")).size() == ids.size());
    // Storage backends are one more port.
    auto st = unwrap(port_graph::record_adapters(*db, "storage", port_graph::adapters_of(storage::BackendRegistry::builtin())));
    CHECK(st.size() == storage::BackendRegistry::builtin().names().size());
    CHECK(label_of(*db, st[0]).rfind("adapter:storage:", 0) == 0);
  }

  TEST_CASE("role choices from PortProfile::resolve: role, adapter, scope, source; composition links every member") {
    fsutil::TempDir td;
    auto db = fresh_db(td);
    port_graph::record_adapters(*db, "asr", {{"groq", "implemented", Json::object()}, {"google", "implemented", Json::object()}});
    auto prof = unwrap(PortProfile::load(td.path()));
    auto resolved = unwrap(prof.resolve("asr"));
    const std::string choice = unwrap(port_graph::record_role_choice(*db, resolved));
    REQUIRE_FALSE(choice.empty());
    auto n = *unwrap(db->get_node(choice));
    CHECK(n.kind == "role_choice");
    CHECK(n.metadata["role"] == "asr");
    CHECK(n.metadata["scope"] == "installation");
    CHECK(n.metadata["source"] == resolved["source"]);
    CHECK(n.metadata["entry"]["compose"] == "fallback");
    std::set<std::string> labels;
    for (const auto& id : targets(*db, choice, "chose")) labels.insert(label_of(*db, id));
    CHECK(labels == std::set<std::string>{"adapter:asr:groq", "adapter:asr:google"});
    // same choice = same node; a user overlay = another node with scope "person"
    CHECK(unwrap(port_graph::record_role_choice(*db, resolved)) == choice);
    std::filesystem::create_directories(td.path() / "profiles");
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "profiles" / "ports.pack",
        R"({"schema":"loom.runtime_profile_overlay/1","domain":"ports","overrides":{"roles":{"asr":{"adapter":"groq","params":{"model":"m"}}}}})"));
    auto user = unwrap(PortProfile::load(td.path()));
    const std::string c2 = unwrap(port_graph::record_role_choice(*db, unwrap(user.resolve("asr"))));
    CHECK(c2 != choice);
    CHECK((*unwrap(db->get_node(c2))).metadata["scope"] == "person");
    // undefined role: no node
    CHECK(unwrap(port_graph::record_role_choice(*db, unwrap(prof.resolve("nope")))).empty());
    // all roles of the profile
    auto all = unwrap(port_graph::record_role_choices(*db, user));
    CHECK(all.size() >= 5);
  }

  TEST_CASE("a port run is a node produced_by its adapter, derived_from inputs, joined to model_use by operation_id") {
    fsutil::TempDir td;
    auto db = fresh_db(td);
    const std::string audio = unwrap(db->create_node("blob:abc", "blob"));
    Json record{{"schema", "loom.model_use/1"}, {"kind", "media.asr"}, {"adapter", "openai_transcriptions"},
                {"provider", "groq"}, {"operation_id", "op_123"}, {"request_sha256", "deadbeef"},
                {"requested_model", "whisper"}, {"status", "ok"}, {"usage", Json{{"seconds", 3}}}};
    auto run = port_graph::port_run_from_model_use(record);
    CHECK(run.port == "asr");
    run.adapter = "groq_whisper";
    run.inputs = {audio};
    const std::string id = unwrap(port_graph::record_port_run(*db, run));
    auto n = *unwrap(db->get_node(id));
    CHECK(n.kind == "port_run");
    CHECK(n.label == "port_run:op_123");
    CHECK(n.metadata["operation_id"] == "op_123");
    CHECK(n.metadata["provenance"]["subject_kind"] == "model_use");
    CHECK(n.metadata["provenance"]["subject_id"] == "op_123");
    const auto prod = targets(*db, id, "produced_by");
    REQUIRE(prod.size() == 1);
    CHECK(label_of(*db, *prod.begin()) == "adapter:asr:groq_whisper");
    CHECK(targets(*db, id, "derived_from") == std::set<std::string>{audio});
    CHECK(targets(*db, id, "part_of").empty());  // not inside an action run
    CHECK(unwrap(port_graph::record_port_run(*db, run)) == id);  // idempotent
  }

  TEST_CASE("walking produced_by from a run ends at the adapter, whose implements ends at the port (G2)") {
    fsutil::TempDir td;
    auto db = fresh_db(td);
    port_graph::PortRun run;
    run.port = "ocr"; run.adapter = "ocr_space"; run.operation_id = "op_g2";
    const std::string id = unwrap(port_graph::record_port_run(*db, run));
    std::string at = id;
    for (const char* step : {"produced_by", "implements"}) {
      auto t = targets(*db, at, step);
      REQUIRE(t.size() == 1);
      at = *t.begin();
    }
    CHECK(label_of(*db, at) == "port:ocr");
  }

  TEST_CASE("ensemble outcome: one run, produced_by every answering member, degraded kept in metadata") {
    fsutil::TempDir td;
    auto db = fresh_db(td);
    auto e = unwrap(compose::ensemble<int>(std::vector<std::string>{"a", "b", "c"},
        [](const std::string& m) -> std::optional<Result<int>> {
          if (m == "b") return Result<int>(Error(Errc::Unavailable, "member down"));
          return Result<int>(1); },
        [](const std::string& m) { return m; }));
    port_graph::PortRun run;
    run.port = "ocr"; run.kind = "ensemble"; run.operation_id = "op_ens"; run.ensemble = e.to_json();
    const std::string id = unwrap(port_graph::record_port_run(*db, run));
    std::set<std::string> labels;
    for (const auto& t : targets(*db, id, "produced_by")) labels.insert(label_of(*db, t));
    CHECK(labels == std::set<std::string>{"adapter:ocr:a", "adapter:ocr:c"});
    CHECK((*unwrap(db->get_node(id))).metadata["ensemble"]["degraded"][0]["member"] == "b");
  }

  TEST_CASE("inside an action run the port run is part_of the action_run node; before the node exists it is reconciled") {
    fsutil::TempDir td;
    auto db = fresh_db(td);
    // The node S1's result writer makes: label "run:<id>", class action_run.
    const std::string run_node = unwrap(db->get_or_create_node("run:r_77", "action_run"));
    port_graph::PortRun run;
    run.port = "asr"; run.adapter = "groq_whisper"; run.operation_id = "op_in";
    std::string inside;
    {
      port_graph::ActionRunScope scope("r_77");
      CHECK(port_graph::ActionRunScope::current() == "r_77");
      inside = unwrap(port_graph::record_port_run(*db, run));
    }
    CHECK(port_graph::ActionRunScope::current().empty());
    CHECK(targets(*db, inside, "part_of") == std::set<std::string>{run_node});

    // The run node is written later (result.cpp writes it after the steps): the id waits, reconcile links.
    run.operation_id = "op_early";
    run.action_run_id = "r_88";
    const std::string early = unwrap(port_graph::record_port_run(*db, run));
    CHECK(targets(*db, early, "part_of").empty());
    CHECK((*unwrap(db->get_node(early))).metadata["action_run_id"] == "r_88");
    CHECK(unwrap(port_graph::reconcile_action_runs(*db)) == 0);
    const std::string late = unwrap(db->get_or_create_node("run:r_88", "action_run"));
    CHECK(unwrap(port_graph::reconcile_action_runs(*db)) == 1);
    CHECK(targets(*db, early, "part_of") == std::set<std::string>{late});
    CHECK(unwrap(port_graph::reconcile_action_runs(*db)) == 0);
  }

  TEST_CASE("no secret value in any node or link; secret key names stay as references") {
    fsutil::TempDir td;
    auto db = fresh_db(td);
    const std::string secret = "sk-THIS-MUST-NEVER-APPEAR-0123456789";
    Json meta{{"secret_key", "groq_api_key"}, {"api_key", secret}, {"headers", Json{{"Authorization", "Bearer " + secret}}},
              {"note", secret}, {"nested", Json::array({Json{{"token", secret}}})}};
    unwrap(port_graph::record_adapters(*db, "asr", {{"groq", "implemented", meta}}));
    Json resolved{{"role", "asr"}, {"defined", true}, {"adapter", "groq"}, {"scope", "person"}, {"source", "user"},
                  {"params", Json{{"model", "m"}, {"password", secret}, {"secret_ref", "groq_api_key"}}}};
    unwrap(port_graph::record_role_choice(*db, resolved));
    port_graph::PortRun run;
    run.port = "asr"; run.adapter = "groq"; run.operation_id = "op_s"; run.extra = Json{{"credential", secret}, {"sidecar", Json{{"bearer", secret}}}};
    run.usage = Json{{"api_key", secret}, {"tokens", 5}};
    unwrap(port_graph::record_port_run(*db, run));
    const std::string all = whole_graph(*db);
    CHECK(all.find("THIS-MUST-NEVER") == std::string::npos);
    CHECK(all.find("groq_api_key") != std::string::npos);  // the reference survives
    CHECK(all.find("[redacted]") != std::string::npos);
    CHECK(all.find("\"tokens\":5") != std::string::npos);  // ordinary numbers are not touched
  }

  TEST_CASE("secrets stored by the runtime never reach the graph (real secrets.json values)") {
    fsutil::TempDir td;
    auto db = fresh_db(td);
    const std::string secret = "REAL-SECRET-VALUE-9f3a1c7e5b";
    Secrets secrets(td.path() / "secrets.json");
    secrets.set("groq_api_key", secret);
    // The records the media path produces carry no key; the graph records them plus the profile's key NAMES.
    port_graph::record_adapters(*db, "asr", port_graph::adapters_of(asr_adapters()));
    port_graph::record_role_choices(*db, unwrap(PortProfile::load(td.path())));
    port_graph::PortRun run;
    run.port = "asr"; run.adapter = "groq_whisper"; run.operation_id = "op_t";
    unwrap(port_graph::record_port_run(*db, run));
    CHECK(whole_graph(*db).find(secret) == std::string::npos);
  }
}
