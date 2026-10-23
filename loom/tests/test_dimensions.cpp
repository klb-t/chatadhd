// K3-1: the dimension registry as data (loom/data/graph/dimensions.pack) and address resolution on a mock graph.
// Every dimension, codec and word named here is an example the registry must be able to express; the registry is open.
#include <doctest/doctest.h>

#include <filesystem>

#include "address_mock.h"
#include "loom/address.h"
#include "loom/dimensions.h"
#include "loom/util/fs.h"
#include "loom/util/sha256.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::address;
using test::unwrap;
namespace fs = std::filesystem;

namespace {

const dimensions::Registry& reg() { return dimensions::Registry::builtin(); }
fs::path data_dir() { return fs::path(LOOM_TEST_FIXTURES).parent_path().parent_path() / "data"; }
bool has_pl_en(const Json& v) {
  return v.is_object() && v.contains("pl") && v.contains("en") && v["pl"].is_string() && v["en"].is_string() && !v["pl"].get<std::string>().empty() &&
         !v["en"].get<std::string>().empty();
}
Resolution run(const std::string& text, const std::string& anchor, const Graph& g, const dimensions::Registry& r = reg()) {
  return unwrap(resolve(unwrap(parse(text, Syntaxes::builtin().default_syntax())), anchor, g, r));
}

}  // namespace

TEST_SUITE_BEGIN("dimensions");

TEST_CASE("the packs are data: labels and reasons in Polish and English, a closed list only with reason and date, embedded copy equals the file") {
  const Json pack = unwrap(json::parse(unwrap(fsutil::read_file(data_dir() / "graph/dimensions.pack"))));
  CHECK(pack == reg().pack());
  REQUIRE(reg().ids().size() >= 3);
  for (const auto& id : reg().ids()) {
    const auto& d = *reg().find(id);
    INFO(id);
    CHECK(has_pl_en(d.label));
    CHECK(has_pl_en(d.why));
    CHECK(d.codec_known);
  }
  for (const auto& c : pack["codecs"]) {
    INFO(c["id"]);
    CHECK(has_pl_en(c["label"]));
    CHECK(has_pl_en(c["why"]));
  }
  for (auto it = pack["between_modes"].begin(); it != pack["between_modes"].end(); ++it) {
    CHECK(has_pl_en(it.value()["label"]));
    CHECK(has_pl_en(it.value()["why"]));
  }
  for (const auto& code : reg().catalogue().codes()) {
    const Json& e = reg().catalogue().entries()[code];
    INFO(code);
    CHECK(has_pl_en(e["text"]));
    CHECK(has_pl_en(e["question"]));
    CHECK_FALSE(e["level"].get<std::string>().empty());
  }
  for (const auto& l : pack["closed_lists"]) {  // R84: a closed list carries its reason, the date and the way back
    INFO(l["id"]);
    CHECK(has_pl_en(l["what"]));
    CHECK(has_pl_en(l["reason"]));
    CHECK(has_pl_en(l["reverse"]));
    CHECK(l["observed_on"].get<std::string>().size() == 10);
  }
  const Json syn = unwrap(json::parse(unwrap(fsutil::read_file(data_dir() / "graph/address_syntax.pack"))));
  CHECK(syn == Syntaxes::builtin().pack());
  for (auto it = syn["syntaxes"].begin(); it != syn["syntaxes"].end(); ++it) {
    INFO(it.key());
    CHECK(has_pl_en(it.value()["label"]));
    CHECK(has_pl_en(it.value()["why"]));
    if (it.value().contains("aliases"))
      for (const auto& a : it.value()["aliases"]) {
        CHECK(has_pl_en(a["label"]));
        CHECK(has_pl_en(a["why"]));
      }
  }
  // every advisory code the engine can raise is in the catalogue (a code without a text would show the bare code)
  for (const char* code : {"address_outside", "address_degenerate", "address_empty", "codec_unknown", "dimension_unknown", "word_unknown", "coord_kind_mismatch",
                           "between_unavailable", "predicate_not_evaluated", "alias_applied", "anchor_off_dimension", "levels_deeper_than_dimension",
                           "origin_unknown", "key_missing", "coord_printed_as_name", "syntax_unknown", "alias_collision"})
    CHECK_MESSAGE(reg().catalogue().has(code), code);
  // the pins in data_domains.pack are the bytes on disk
  const Json domains = unwrap(json::parse(unwrap(fsutil::read_file(data_dir() / "data_domains.pack"))));
  int pinned = 0;
  for (const auto& f : domains["files"])
    if (f["domain"] == "graph_address") {
      CHECK(Sha256::hex(unwrap(fsutil::read_file(data_dir() / f["path"].get<std::string>()))) == f["sha256"].get<std::string>());
      ++pinned;
    }
  CHECK(pinned == 2);
}

TEST_CASE("T3 every registered dimension resolves 0, -1, last, first and all on the mock") {
  const k3mock::Mock mock = k3mock::Mock::build(reg(), 32);
  const Graph g = mock.ports();
  for (const auto& id : reg().ids()) {
    const auto& d = *reg().find(id);
    const k3mock::Forest& f = mock.forests.at(id);
    const bool hier = d.effective_codec == "levels";
    std::string anchor;
    std::size_t at;
    if (hier) {  // a deepest node in the second root
      anchor = f.roots[1];
      while (!f.nodes.at(anchor).kids.empty()) anchor = f.nodes.at(anchor).kids.front();
      at = 1;
    } else {
      at = 10;
      anchor = f.roots[at];
    }
    INFO(id << " anchor " << anchor);
    auto one = [&](const std::string& word, std::vector<std::string> want) {
      const Resolution r = run(id + ":" + word, anchor, g);
      INFO(id << ":" << word);
      CHECK(r.nodes == want);
      CHECK(r.advisories.empty());
    };
    one("0", {f.roots[at]});
    one("-1", {f.roots[at - 1]});
    one("last", {f.roots.back()});
    one("first", {f.roots.front()});
    one("all", f.roots);
    one("next", {f.roots[at + 1]});
    // max and min need a key: on dimensions whose keys grow with the position they are the ends
    if (!hier && (d.effective_codec == "integer" || d.effective_codec == "double" || d.effective_codec == "datetime")) {
      // keys were assigned by the original position i, not by the rotated order: compare through the keys
      const Resolution mx = run(id + ":max", anchor, g), mn = run(id + ":min", anchor, g);
      REQUIRE(mx.nodes.size() == 1);
      REQUIRE(mn.nodes.size() == 1);
      CHECK(reg().compare(d, mock.forests.at(id).nodes.at(mx.nodes[0]).key, mock.forests.at(id).nodes.at(mn.nodes[0]).key) == std::optional<int>(1));
    }
    // outside the graph: an empty set and an advisory, never an error
    for (const char* far : {"-100", "100"}) {
      const Resolution r = run(id + ":" + far, anchor, g);
      CHECK(r.nodes.empty());
      REQUIRE(r.advisories.size() == 1);
      CHECK(r.advisories[0]["code"] == "address_outside");
      CHECK(has_pl_en(r.advisories[0]["text"]));
    }
  }
}

TEST_CASE("T4 hierarchical steps: previous major -> its last minor -> its last patch, on a mock version tree") {
  k3mock::Mock m;
  const std::string D = "program_version";
  // majors 1..3; minors: 1 -> 1.0 1.1; 2 -> 2.0 2.1 2.2; 3 -> 3.0 3.1; patches 0..2 for every minor except 1.0 (0..1)
  const std::map<std::string, std::vector<std::string>> minors = {{"1", {"1.0", "1.1"}}, {"2", {"2.0", "2.1", "2.2"}}, {"3", {"3.0", "3.1"}}};
  int key = 0;
  for (const auto& [major, ms] : minors) {
    m.add_root(D, major, key++);
    int mk = 0;
    for (const auto& mn : ms) {
      m.add_kid(D, major, mn, mk++);
      const int patches = mn == "1.0" ? 2 : 3;
      for (int p = 0; p < patches; ++p) m.add_kid(D, mn, mn + "." + std::to_string(p), p);
    }
  }
  const Graph g = m.ports();
  const std::string anchor = "3.1.1";
  auto nodes = [&](const std::string& text) { return run(text, anchor, g).nodes; };
  using V = std::vector<std::string>;
  CHECK(nodes("program_version:-1.last.last") == V{"2.2.2"});                // the owner's example, read as levels
  CHECK(nodes("program_version:0.0.0") == V{"3.1.1"});                       // the current version again
  CHECK(nodes("program_version:0.-1.last") == V{"3.0.2"});                   // same major, the minor before the current one
  CHECK(nodes("program_version:0.0.-1") == V{"3.1.0"});
  CHECK(nodes("program_version:-1.-1.last") == V{"2.1.2"});                  // off the current lineage a relative number counts from the last
  CHECK(nodes("program_version:first.first.first") == V{"1.0.0"});
  CHECK(nodes("program_version:all.last.last") == V{"1.1.2", "2.2.2", "3.1.2"});
  CHECK(nodes("program_version:-1.last.all") == V{"2.2.0", "2.2.1", "2.2.2"});   // all the patches of the last minor of the previous major
  CHECK(nodes("program_version:2.last") == V{});                              // no major two ahead
  CHECK(nodes("program_version:-1.last") == V{"2.2"});                       // fewer levels than the dimension has
  CHECK(nodes("program_version:-1") == V{"2"});
  CHECK(nodes("program_version:last.0.all") == V{"3.1.0", "3.1.1", "3.1.2"});    // 0 on a finer level is the current minor when the major is the current one
  // a coordinate longer than the dimension declares goes as deep as the graph reaches, with an advisory
  const Resolution deep = run("program_version:-1.last.last.last", anchor, g);
  CHECK(deep.nodes.empty());
  bool declared = false, outside = false;
  for (const auto& a : deep.advisories) {
    declared = declared || a["code"] == "levels_deeper_than_dimension";
    outside = outside || a["code"] == "address_outside";
  }
  CHECK(declared);
  CHECK(outside);
  CHECK(run("program_version:2.last", anchor, g).advisories.at(0)["code"] == "address_outside");
  // the dimension's own ends: max uses the keys
  CHECK(nodes("program_version:max.max.max") == V{"3.1.2"});
  CHECK(nodes("program_version:min.min.min") == V{"1.0.0"});
}

TEST_CASE("an address that does not fit is accepted and answered with an advisory, never an error (R58)") {
  const k3mock::Mock mock = k3mock::Mock::build(reg(), 32);
  const Graph g = mock.ports();
  auto codes = [&](const std::string& text, const std::string& anchor = "u5") {
    std::vector<std::string> out;
    for (const auto& a : run(text, anchor, g).advisories) out.push_back(a["code"].get<std::string>());
    return out;
  };
  using C = std::vector<std::string>;
  CHECK(codes("message:nonsense") == C{"word_unknown"});
  CHECK(codes("nowhere:0") == C{"dimension_unknown"});
  CHECK(codes("message:\"x\"") == C{"coord_kind_mismatch"});
  CHECK(codes("message:2026-10-22T10:00Z") == C{"coord_kind_mismatch"});
  CHECK(codes("message:[role = \"user\"]") == C{"predicate_not_evaluated"});
  CHECK(codes("message:1.2.3") == C{"coord_kind_mismatch"});            // levels on a dimension that has none
  CHECK(codes("branch:\"nobody\"") == C{"address_outside"});
  CHECK(codes("branch:\"k03\"").empty());
  CHECK(run("branch:\"k03\"", "u5", g).nodes.size() == 1);
  CHECK(codes("") == C{"address_empty"});
  CHECK(run("", "u5", g).nodes == std::vector<std::string>{"u5"});        // an empty address means the current node
  // a node that is not on the dimension at all
  k3mock::Mock small;
  small.add_root("message", "only", 0);
  CHECK(run("message:0", "elsewhere", small.ports()).advisories.at(0)["code"] == "anchor_off_dimension");
  // useless but valid: -0 and all/all give results
  CHECK(run("message:-0", "u5", g).nodes == run("message:0", "u5", g).nodes);
  CHECK(run("message:all/message:all", "u5", g).nodes.size() == 32);
}

TEST_CASE("a point between nodes (R77): nearest today; interpolate and derive are registered as not yet available, with an advisory") {
  const k3mock::Mock mock = k3mock::Mock::build(reg(), 32);
  const Graph g = mock.ports();
  const auto& time = *reg().find("time");
  CHECK(time.between_mode() == "interpolate");
  CHECK_FALSE(reg().between_available("interpolate"));
  CHECK_FALSE(reg().between_available("derive"));
  CHECK(reg().between_available("nearest"));
  const std::string on_time = mock.forests.at("time").roots.front();
  // exactly on a node: no mode is needed; between two nodes: the nearest and the advisory
  const Resolution exact = run("time:2026-03-01T05:00Z", on_time, g);
  CHECK(exact.nodes.size() == 1);
  CHECK(exact.advisories.empty());
  const Resolution between = run("time:2026-03-01T05:20Z", on_time, g);
  CHECK(between.nodes == exact.nodes);  // 05:20 is nearer to 05:00 than to 06:00
  REQUIRE(between.advisories.size() == 1);
  CHECK(between.advisories[0]["code"] == "between_unavailable");
  CHECK(between.nodes.size() == 1);
  const Resolution later = run("time:2026-03-01T05:40Z", on_time, g);
  CHECK(later.nodes != exact.nodes);
  // a fractional offset on an integer dimension: half a step is a tie and goes to the earlier node; the dimension's mode is nearest, so no advisory
  const std::string anchor = mock.forests.at("message").roots[10];
  const Resolution half = run("message:-0.5", anchor, g);
  CHECK(half.nodes == std::vector<std::string>{mock.forests.at("message").roots[9]});
  CHECK(half.advisories.empty());
  CHECK(run("message:0.4", anchor, g).nodes == std::vector<std::string>{anchor});
  // the scale dimension derives (not available yet)
  CHECK(run("scale:0.1", mock.forests.at("scale").roots[3], g).advisories.at(0)["code"] == "between_unavailable");
}

TEST_CASE("the codec registry rejects nothing silently: an unknown codec behaves like a name and says so") {
  Json pack = reg().pack();
  pack["dimensions"].push_back(Json{{"id", "mood"}, {"codec", "quantum"}, {"label", Json{{"pl", "Nastrój"}, {"en", "Mood"}}}, {"why", Json{{"pl", "przykład"}, {"en", "example"}}}});
  auto r = dimensions::Registry::from_json(pack);
  REQUIRE(r.has_value());
  const auto& d = *r->find("mood");
  CHECK_FALSE(d.codec_known);
  CHECK(d.codec == "quantum");
  CHECK(d.effective_codec == "name");
  REQUIRE(r->load_advisories().size() == 1);
  CHECK(r->load_advisories()[0]["code"] == "codec_unknown");
  CHECK(has_pl_en(r->load_advisories()[0]["text"]));
  // string passthrough: its coordinates are names compared as strings
  k3mock::Mock m;
  for (int i = 0; i < 5; ++i) m.add_root("mood", "n" + std::to_string(i), std::string(1, static_cast<char>('a' + 4 - i)));  // keys e d c b a
  const Graph g = m.ports();
  const Resolution mx = run("mood:max", "n2", g, *r);
  CHECK(mx.nodes == std::vector<std::string>{"n0"});  // key "e" is the greatest string
  CHECK(run("mood:\"b\"", "n2", g, *r).nodes == std::vector<std::string>{"n3"});
  CHECK(run("mood:last", "n2", g, *r).nodes == std::vector<std::string>{"n4"});
  bool said = false;
  for (const auto& a : mx.advisories) said = said || a["code"] == "codec_unknown";
  CHECK(said);
  // a typed literal carries a coordinate of a codec the text notation has no shape for
  CHECK(run("mood:ratio\"c\"", "n2", g, *r).nodes == std::vector<std::string>{"n2"});
  // the loader refuses only what it cannot read at all
  CHECK_FALSE(dimensions::Registry::from_json(Json{{"schema", "other/1"}}).has_value());
  Json dup = reg().pack();
  dup["dimensions"].push_back(dup["dimensions"][0]);
  CHECK_FALSE(dimensions::Registry::from_json(dup).has_value());
}

TEST_CASE("words, origin and keys are data of a dimension") {
  Json pack = reg().pack();
  pack["dimensions"].push_back(Json{{"id", "ring"}, {"codec", "integer"}, {"origin", "first"},
                                    {"words", Json{{"top", Json{{"end", "last"}}}, {"last", nullptr}}},
                                    {"label", Json{{"pl", "Pierścień"}, {"en", "Ring"}}}, {"why", Json{{"pl", "przykład"}, {"en", "example"}}}});
  pack["dimensions"].push_back(Json{{"id", "odd"}, {"codec", "integer"}, {"origin", "somewhere"}});
  const auto r = unwrap(dimensions::Registry::from_json(pack));
  k3mock::Mock m;
  for (int i = 0; i < 6; ++i) {
    m.add_root("ring", "r" + std::to_string(i), i);
    m.add_root("odd", "o" + std::to_string(i), i);
  }
  Graph g = m.ports();
  CHECK(run("ring:2", "r4", g, r).nodes == std::vector<std::string>{"r2"});     // origin first: 2 counts from the first node
  CHECK(run("ring:top", "r4", g, r).nodes == std::vector<std::string>{"r5"});   // a word of its own
  const Resolution gone = run("ring:last", "r4", g, r);                          // the registry word removed here
  CHECK(gone.nodes.empty());
  CHECK(gone.advisories.at(0)["code"] == "word_unknown");
  const Resolution odd = run("odd:1", "o2", g, r);                               // unknown origin: counted from the current node, with an advisory
  CHECK(odd.nodes == std::vector<std::string>{"o3"});
  CHECK(odd.advisories.at(0)["code"] == "origin_unknown");
  g.key = nullptr;                                                                // a graph without ordering keys
  const Resolution nokey = run("ring:max", "r4", g, r);
  CHECK(nokey.nodes == std::vector<std::string>{"r5"});
  CHECK(nokey.advisories.at(0)["code"] == "key_missing");
  // the default dimension of a step without a name is a port of the graph, else the registry's
  Graph named = m.ports();
  CHECK(run("1", "r1", named, r).advisories.at(0)["code"] == "anchor_off_dimension");   // the registry default `message` is not on this mock
  named.default_dimension = [](const std::string&) { return std::string("ring"); };
  CHECK(unwrap(resolve(unwrap(parse("1", Syntaxes::builtin().default_syntax())), "r1", named, r)).nodes == std::vector<std::string>{"r1"});  // origin first: 1 is r1
}

TEST_SUITE_END();
