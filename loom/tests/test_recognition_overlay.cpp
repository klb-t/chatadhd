// P24 adapter: recognized patterns surface as a disabled-hypothesis overlay on the edge-kind view; promotion is the user's.
#include <doctest/doctest.h>

#include <filesystem>

#include "loom/effective_view.h"
#include "loom/generalize.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;

namespace {
model::Instance candidate(const std::string& paradigm, const std::string& subject) {
  model::Instance in;
  in.id = model::Instance::make_id(paradigm, subject);
  in.paradigm = paradigm;
  in.subject = subject;
  in.recognition = Json{{"status", "candidate"}, {"method", "generalize.match"}, {"version", "4"}, {"pack_hash", "h"}};
  return in;
}
}  // namespace

TEST_SUITE("recognition_overlay") {
  TEST_CASE("candidates become disabled hypotheses with provenance; auto enables; off writes nothing; user restore promotes") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "loom_recognition_overlay_test";
    fs::remove_all(root);
    fs::create_directories(root);
    std::vector<model::Instance> in{candidate("feedback_execution", "e_a"), candidate("feedback_execution", "e_b")};
    in.push_back(model::Instance());  // no receipt: never surfaces

    unwrap(generalize::write_recognition_overlay(root, in, "propose"));
    auto eff = unwrap(views::effective(root, "edge_kinds"));
    const auto* e = eff.find("structure:feedback_execution");
    REQUIRE(e);
    CHECK(e->disabled);
    CHECK(e->origin == "overlay:recognition");
    CHECK(e->value["provenance"]["origin"] == "generalize");
    CHECK(e->value["provenance"]["instances"].size() == 2);
    for (const auto* a : eff.active()) CHECK(a->id != "structure:feedback_execution");

    // The user's own overlay (later in order) promotes.
    unwrap(views::put_op(root, "zz_user", Json{{"view", "edge_kinds"}, {"op", "disable"}, {"id", "structure:feedback_execution"}, {"restore", true}}));
    eff = unwrap(views::effective(root, "edge_kinds"));
    CHECK_FALSE(eff.find("structure:feedback_execution")->disabled);

    fs::remove_all(root / "views");
    unwrap(generalize::write_recognition_overlay(root, in, "auto"));
    CHECK_FALSE(unwrap(views::effective(root, "edge_kinds")).find("structure:feedback_execution")->disabled);
    unwrap(generalize::write_recognition_overlay(root, in, "off"));
    CHECK(unwrap(views::effective(root, "edge_kinds")).find("structure:feedback_execution") == nullptr);
    fs::remove_all(root);
  }
}
