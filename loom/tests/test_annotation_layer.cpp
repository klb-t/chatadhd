// P23 step 1: the annotation layer as a contract (loom/annotation_layer.h): producer + method version, computed confidence, cost, qualifier,
// the state superseded, "signal beats noise", many annotations per segment, positive signals.
#include <doctest/doctest.h>

#include "loom/annotation_layer.h"
#include "loom/db.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {

struct Layer {
  fsutil::TempDir td;
  std::unique_ptr<Database> db = open_db(td.path() / "layer.db");
  annotations::Policy policy;
  std::string mid;
  Layer() {
    policy.p = Json{{"label_prefix", "annotation:"}, {"node_kind", "annotation_category"},
                    {"classes", Json{{"signal", Json::array({"correction", "preference", "acceptance"})}, {"droppable", Json::array({"filler", "repetition", "digression"})}}},
                    {"polarity", Json{{"positive", Json::array({"acceptance", "preference"})}, {"negative", Json::array({"correction"})}}},
                    {"conflicts", Json{{"shield", Json{{"signal_over", "droppable"}, {"min_confidence", 0.5}, {"whole_message_shields", false}}}}},
                    {"producers", Json{{"a", Json::object()}, {"b", Json::object()}}}};
    const std::string conv = unwrap(db->create_conv("layer")).id;
    NewMessage m;
    m.conv_id = conv;
    m.role = "user";
    m.text = "no, no that is wrong, always write in Polish, except file names";
    mid = unwrap(db->create_msg(m));
  }
  annotations::Claim claim(const std::string& category, std::int64_t from, std::int64_t to, const std::string& producer = "a", double confidence = 0.8) const {
    annotations::Claim c;
    c.message_id = mid;
    c.category = category;
    c.start_char = from;
    c.end_char = to;
    c.producer = producer;
    c.method = "test";
    c.method_version = "v1";
    c.confidence = confidence;
    return c;
  }
  Json active(bool inactive = false) { return unwrap(annotations::list(*db, policy, mid, annotations::ListOptions{inactive, true, 0.0})); }
};

}  // namespace

TEST_SUITE("annotation_layer") {
  TEST_CASE("many annotations of different categories share a segment; positive and negative signals are both signals") {
    Layer l;
    CHECK(unwrap(annotations::write(*l.db, l.policy, l.claim("correction", 0, 8))).outcome == annotations::Outcome::Created);
    CHECK(unwrap(annotations::write(*l.db, l.policy, l.claim("digression", 0, 8, "b"))).outcome == annotations::Outcome::Created);
    CHECK(unwrap(annotations::write(*l.db, l.policy, l.claim("acceptance", 0, 20, "b"))).outcome == annotations::Outcome::Created);
    const Json rows = l.active(true);
    CHECK(rows.size() == 3);
    std::set<std::string> polarity;
    for (const auto& r : rows) polarity.insert(r.at("polarity"));
    CHECK(polarity == std::set<std::string>{"negative", "neutral", "positive"});
  }

  TEST_CASE("the contract fields: producer, method and its version, computed confidence, cost, qualifier") {
    Layer l;
    auto c = l.claim("correction", 0, 8, "a", 0.734567);
    c.qualifier = "quoted";
    c.cost = Json{{"calls", 1}, {"input_bytes", 120}, {"receipt", "op_1"}};
    c.extra = Json{{"matched", "no, no"}};
    const auto w = unwrap(annotations::write(*l.db, l.policy, c));
    const Json r = l.active()[0];
    CHECK(r.at("id") == w.id);
    CHECK(r.at("producer") == "a");
    CHECK(r.at("method") == "test");
    CHECK(r.at("method_version") == "v1");
    CHECK(r.at("confidence") == 0.735);
    CHECK(r.at("qualifier") == "quoted");
    CHECK(r.at("cost").at("receipt") == "op_1");
    CHECK(r.at("data").at("matched") == "no, no");
    CHECK(r.at("state") == "active");
    // The same claim of the same method version is not written twice.
    CHECK(unwrap(annotations::write(*l.db, l.policy, c)).outcome == annotations::Outcome::Duplicate);
    CHECK(l.active().size() == 1);
  }

  TEST_CASE("conflict: the user wins, then the higher confidence; the loser is kept as superseded, never deleted") {
    Layer l;
    const auto first = unwrap(annotations::write(*l.db, l.policy, l.claim("digression", 0, 20, "a", 0.6)));
    // A more confident claim of another producer on an overlapping range replaces it.
    const auto better = unwrap(annotations::write(*l.db, l.policy, l.claim("digression", 5, 25, "b", 0.9)));
    CHECK(better.outcome == annotations::Outcome::Replaced);
    REQUIRE(better.superseded.size() == 1);
    CHECK(better.superseded[0] == first.id);
    CHECK(l.active().size() == 1);
    // A weaker claim loses at once, and is stored as superseded.
    const auto weaker = unwrap(annotations::write(*l.db, l.policy, l.claim("digression", 10, 30, "a", 0.3)));
    CHECK(weaker.outcome == annotations::Outcome::Lost);
    CHECK(weaker.superseded_by == better.id);
    // The user stands above any confidence.
    auto u = l.claim("digression", 0, 30, "user", 0.1);
    u.origin = "user";
    const auto mine = unwrap(annotations::write(*l.db, l.policy, u));
    CHECK(mine.outcome == annotations::Outcome::Replaced);
    CHECK(mine.superseded[0] == better.id);
    const Json all = l.active(true);
    CHECK(all.size() == 4);
    std::map<std::string, int> states;
    for (const auto& r : all) ++states[r.at("state")];
    CHECK(states["active"] == 1);
    CHECK(states["superseded"] == 3);
    // Nothing is lost: a superseded annotation can be restored.
    unwrap(annotations::restore(*l.db, first.id));
    CHECK(l.active().size() == 2);
  }

  TEST_CASE("signal beats noise: a signal protects the segment it covers; a whole-message signal shields nothing; what the user took back stays back") {
    Layer l;
    const auto noise = unwrap(annotations::write(*l.db, l.policy, l.claim("repetition", 4, 6, "a", 0.9)));
    CHECK(l.active().size() == 1);
    const auto signal = unwrap(annotations::write(*l.db, l.policy, l.claim("correction", 0, 8, "b", 0.9)));
    const Json rows = l.active();
    REQUIRE(rows.size() == 1);  // the noise is covered by the signal
    CHECK(rows[0].at("category") == "correction");
    const Json history = l.active(true);
    bool shielded = false;
    for (const auto& r : history)
      if (r.at("category") == "repetition") shielded = r.contains("shielded_by") && r.at("shielded_by") == signal.id;
    CHECK(shielded);
    // The signal goes away, the noise is in force again: the rule is read, not written.
    unwrap(annotations::supersede(*l.db, signal.id, ""));
    CHECK(l.active().size() == 1);
    // A signal that spans the whole message describes the message, it does not protect a segment.
    unwrap(annotations::write(*l.db, l.policy, l.claim("preference", 0, -1, "b", 0.9)));
    bool noise_in_force = false;
    for (const auto& r : l.active()) noise_in_force = noise_in_force || r.at("category") == "repetition";
    CHECK(noise_in_force);
    // The user takes a noise annotation back: the producer does not write it again.
    MessageAnnotationPatch p;
    p.state = "retracted";
    (void)unwrap(l.db->revise_message_annotation(noise.id, 1, p));
    CHECK(unwrap(annotations::write(*l.db, l.policy, l.claim("repetition", 4, 6, "a", 0.9))).outcome == annotations::Outcome::Suppressed);
  }

  TEST_CASE("a method version change replaces the old claim; a run receipt is kept even when nothing was found") {
    Layer l;
    auto c = l.claim("digression", 0, 10);
    unwrap(annotations::write(*l.db, l.policy, c));
    c.method_version = "v2";
    const auto w = unwrap(annotations::write(*l.db, l.policy, c));
    CHECK(w.outcome == annotations::Outcome::Replaced);
    CHECK(l.active().size() == 1);
    CHECK(l.active()[0].at("method_version") == "v2");
    CHECK(unwrap(annotations::runs_of(*l.db, l.policy, l.mid)).empty());
    unwrap(annotations::record_run(*l.db, l.policy, l.mid, "a", "v2", {"digression"}, Json{{"calls", 1}}));
    const Json runs = unwrap(annotations::runs_of(*l.db, l.policy, l.mid));
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].at("cost").at("calls") == 1);
  }
}
