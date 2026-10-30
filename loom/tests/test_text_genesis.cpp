// R102 G2: text genesis in the graph — chains, events, lazy idempotent materialisation, read-only queries.
// All texts here are SYNTHETIC (written for the test or generated from a fixed seed); no model is called.
#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <random>
#include <set>

#include "loom/text_genesis.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::text;
using namespace loom::text::genesis;
using loom::test::unwrap;

namespace {

struct Db {
  fsutil::TempDir td;
  std::unique_ptr<Database> db = loom::test::open_db(td.path() / "genesis.db");
};

// A conversation with one message edited into a chain of versions; returns the ids in version order.
std::vector<std::string> chain_of(Database& db, const std::string& conv, const std::vector<std::string>& texts, const std::string& role = "assistant",
                                  const std::string& model = "model-x") {
  std::vector<std::string> ids;
  NewMessage m;
  m.conv_id = conv;
  m.role = role;
  m.model = model;
  m.text = texts.at(0);
  ids.push_back(unwrap(db.create_msg(m)));
  for (std::size_t i = 1; i < texts.size(); ++i) ids.push_back(*unwrap(db.edit_msg(ids.back(), texts[i])));
  return ids;
}

std::size_t count_kind(const Chain& c, std::string_view kind) {
  std::size_t n = 0;
  for (const auto& e : c.events) n += e.kind == kind;
  return n;
}

// A canonical dump of the whole graph (without ids and timestamps): what "the same graph" means for idempotence.
Json graph_dump(Database& db) {
  std::map<std::string, Json> nodes;  // node id -> canonical node
  Json ns = Json::array();
  for (const auto& n : unwrap(db.list_nodes(std::nullopt, 100000))) {
    nodes[n.id] = Json{{"kind", n.kind}, {"label", n.label}};
    ns.push_back(Json{{"kind", n.kind}, {"label", n.label}, {"content", n.content}, {"metadata", n.metadata}});
  }
  Json ls = Json::array();
  for (const auto& l : unwrap(db.get_links())) ls.push_back(Json{{"src", nodes[l.src]}, {"dst", nodes[l.dst]}, {"type", l.link_type}, {"metadata", l.metadata}});
  auto key = [](const Json& j) { return j.dump(); };
  std::vector<std::string> a, b;
  for (const auto& x : ns) a.push_back(key(x));
  for (const auto& x : ls) b.push_back(key(x));
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());
  return Json{{"nodes", a}, {"links", b}};
}

const std::vector<std::string> kD0{
    "Zespół zaczyna pracę w poniedziałek rano.", "Pierwszy etap obejmuje przegląd wymagań.", "Każdy moduł ma własny zestaw testów.",
    "Dokumentacja powstaje razem z kodem.", "Budżet etapu wynosi 12 000 zł i zatwierdza go Kowalska.", "Spotkania odbywają się co tydzień.",
    "Na końcu etapu jest przegląd wyników."};

std::string join(const std::vector<std::string>& s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) out += (i ? " " : "") + s[i];
  return out;
}

}  // namespace

TEST_SUITE("text_genesis") {
  TEST_CASE("§10 shape (synthetic): sentence 2 replaced, sentence 5 removed — one replacement, one removal with where/when/who") {
    Db d;
    const std::string conv = unwrap(d.db->create_conv("Redakcja")).id;
    std::vector<std::string> d1 = kD0;
    d1[1] = "Pierwszy etap obejmuje przegląd wymagań i ryzyk.";
    std::vector<std::string> d2 = d1;
    d2.erase(d2.begin() + 4);
    const auto ids = chain_of(*d.db, conv, {join(kD0), join(d1), join(d2)}, "assistant", "model-y");
    const Segmenter seg = unwrap(Segmenter::builtin("pl"));
    const Options opt = unwrap(Options::from_pack(builtin_pack()));
    const auto versions = unwrap(versions_of_message(*d.db, ids[0]));
    REQUIRE(versions.size() == 3);
    CHECK(versions[2].source.id == ids[2]);
    const Chain c = unwrap(compute_chain(seg, versions, opt));
    REQUIRE(c.versions.size() == 3);
    CHECK(c.versions[0].fragments.size() == 7);
    CHECK(c.versions[2].fragments.size() == 6);
    CHECK(c.steps.size() == 2);
    CHECK(count_kind(c, "replacement") == 1);
    CHECK(count_kind(c, "removal") == 1);
    CHECK(count_kind(c, "addition") == 0);
    CHECK(count_kind(c, "move") == 0);
    for (const auto& e : c.events) {
      if (e.kind == "removal") {
        CHECK(e.a_index == 4);
        CHECK(e.b_index == kNone);
        CHECK(e.from.id == ids[1]);
        CHECK(e.to.id == ids[2]);
        CHECK(e.to.when == unwrap(d.db->get_msg(ids[2]))->created);
        CHECK(e.to.actor == "model-y");
        CHECK(e.to.conv_id == conv);
        CHECK(e.text_before == kD0[4]);
        CHECK(e.key == "removal|" + ids[1] + "|" + ids[2] + "|4|-");
      } else if (e.kind == "replacement") {
        CHECK(e.a_index == 1);
        CHECK(e.b_index == 1);
        CHECK(e.from.id == ids[0]);
        CHECK(e.to.id == ids[1]);
        CHECK(e.text_before == kD0[1]);
        CHECK(e.text_after == d1[1]);
        CHECK(!e.successor.empty());
        CHECK(e.successor != e.fragment);
        CHECK(e.similarity >= 0.5);
        CHECK(!e.inner.empty());
      }
    }
    // Lineage: the changed sentence in the last version traces to version 1 through the replacement.
    const Lineage L = unwrap(lineage_of(c, 2, 1));
    CHECK(L.origin == "first_version");
    REQUIRE(L.steps.size() == 3);
    CHECK(L.steps[0].op_kind == "equal");
    CHECK(L.steps[1].op_kind == "replace");
    CHECK(!L.steps[1].event_key.empty());
    CHECK(L.steps[2].op_kind == "origin");
    // every fragment of the last version traces somewhere
    for (std::size_t j = 0; j < c.versions[2].fragments.size(); ++j) CHECK(unwrap(lineage_of(c, 2, j)).origin == "first_version");

    SUBCASE("queries are read-only and computed live") {
      const Json before = graph_dump(*d.db);
      Query q;
      q.question = "origin";
      q.text = kD0[4];
      q.message_id = ids[2];
      const Json origin = unwrap(query(*d.db, seg, q, opt));
      INFO(origin.dump());
      REQUIRE(origin.at("matches").size() == 1);
      CHECK(origin.at("matches")[0].at("matched_by") == "fingerprint");
      CHECK(origin.at("matches")[0].at("origin").at("how") == "first_version");
      CHECK(origin.at("matches")[0].at("first_seen").at("id") == ids[0]);
      CHECK(origin.at("matches")[0].at("final_state").at("state") == "gone");
      CHECK(origin.at("matches")[0].at("final_state").at("event") == "removal");
      CHECK(origin.at("matches")[0].at("final_state").at("version").at("id") == ids[2]);
      CHECK(origin.at("computed") == Json::array({"live"}));
      bool not_materialized = false;
      for (const auto& a : origin.at("advisories")) not_materialized = not_materialized || a.at("code") == "not_materialized";
      CHECK(not_materialized);
      q.question = "replaced";
      q.text = "przegląd wymagań";  // a part of sentence 2: matched by containment
      const Json rep = unwrap(query(*d.db, seg, q, opt));
      INFO(rep.dump());
      REQUIRE(rep.at("matches").size() >= 1);
      CHECK(rep.at("matches")[0].at("matched_by") == "contains");
      CHECK(rep.at("matches")[0].at("text") == kD0[1]);
      REQUIRE(rep.at("matches")[0].at("successors").size() == 1);
      CHECK(rep.at("matches")[0].at("successors")[0].at("text") == d1[1]);
      q.question = "missing";
      q.text.clear();
      const Json miss = unwrap(query(*d.db, seg, q, opt));
      INFO(miss.dump());
      CHECK(miss.at("from").at("id") == ids[1]);
      CHECK(miss.at("to").at("id") == ids[2]);
      REQUIRE(miss.at("removed").size() == 1);
      CHECK(miss.at("removed")[0].at("text_before") == kD0[4]);
      CHECK(miss.at("replaced").empty());
      CHECK(miss.at("kept") == 6);
      Query two;
      two.question = "missing";
      two.from_id = ids[0];
      two.to_id = ids[2];
      const Json span = unwrap(query(*d.db, seg, two, opt));
      CHECK(span.at("removed").size() == 1);
      CHECK(span.at("replaced").size() == 1);
      Query first;
      first.question = "missing";
      first.message_id = ids[0];
      const Json f = unwrap(query(*d.db, seg, first, opt));
      CHECK(f.at("from").is_null());
      CHECK(f.at("advisories")[0].at("code") == "first_version");
      Query bad;
      bad.question = "who_knows";
      const auto err = query(*d.db, seg, bad, opt);
      REQUIRE(!err);
      CHECK(err.error().code == Errc::InvalidArgument);
      CHECK(err.error().message.find("origin") != std::string::npos);
      CHECK(graph_dump(*d.db) == before);  // nothing written by any query
    }

    SUBCASE("materialise twice = once; the graph answers across conversations") {
      const MaterializeSummary s1 = unwrap(materialize(*d.db, c, opt));
      CHECK(s1.versions == 3);
      CHECK(s1.events == 2);
      CHECK(s1.fragments == 7 + 7 + 6);
      CHECK(s1.nodes_created == 3 /*versions*/ + 8 /*distinct fragments*/ + 2 /*events*/);
      CHECK(s1.nodes_reused == 0);
      const Json after1 = graph_dump(*d.db);
      const MaterializeSummary s2 = unwrap(materialize(*d.db, c, opt));
      CHECK(s2.nodes_created == 0);
      CHECK(s2.nodes_reused == s1.nodes_created);
      CHECK(graph_dump(*d.db) == after1);
      // the removal: exactly one event node, with deleted_in from the fragment to version 3
      const auto removal_nodes = unwrap(d.db->list_nodes("text_event", 1000));
      std::size_t removals = 0;
      for (const auto& n : removal_nodes) removals += n.metadata.value("kind", "") == "removal";
      CHECK(removals == 1);
      const auto frag = unwrap(d.db->find_node(seg.fingerprint(kD0[4]), "text_fragment"));
      REQUIRE(frag.has_value());
      const auto deleted_in = unwrap(d.db->get_links(frag->id, "deleted_in"));
      REQUIRE(deleted_in.size() == 1);
      CHECK(unwrap(d.db->get_node(deleted_in[0].dst))->label == ids[2]);
      // replaced_by / derived_from between the two sentence-2 fragments
      const auto old2 = unwrap(d.db->find_node(seg.fingerprint(kD0[1]), "text_fragment"));
      const auto new2 = unwrap(d.db->find_node(seg.fingerprint(d1[1]), "text_fragment"));
      REQUIRE((old2 && new2));
      const auto rb = unwrap(d.db->get_links(old2->id, "replaced_by"));
      REQUIRE(rb.size() == 1);
      CHECK(rb[0].dst == new2->id);
      const auto df = unwrap(d.db->get_links(new2->id, "derived_from"));
      CHECK(std::any_of(df.begin(), df.end(), [&](const Link& l) { return l.dst == old2->id; }));
      // the same sentence pasted into another conversation shares the fragment node; "origin" sees both
      const std::string conv2 = unwrap(d.db->create_conv("Druga rozmowa")).id;
      const auto other = chain_of(*d.db, conv2, {kD0[4] + " Nowe zdanie tylko tutaj."}, "user", "");
      const Chain c2 = unwrap(compute_chain(seg, unwrap(versions_of_message(*d.db, other[0])), opt));
      const MaterializeSummary s3 = unwrap(materialize(*d.db, c2, opt));
      CHECK(s3.nodes_created == 2);  // the version node + the new sentence; the shared sentence is reused
      CHECK(s3.nodes_reused == 1);
      Query q;
      q.question = "origin";
      q.text = kD0[4];
      q.conv_id = conv2;
      const Json ans = unwrap(query(*d.db, seg, q, opt));
      INFO(ans.dump());
      CHECK(ans.at("computed") == Json::array({"live", "graph"}));
      REQUIRE(ans.at("graph").size() == 1);
      const Json occ = ans.at("graph")[0].at("occurrences");
      REQUIRE(occ.size() == 3);  // versions 1 and 2 of the first conversation, the message of the second
      CHECK(occ[0].at("conv_id") == conv);
      CHECK(occ[2].at("conv_id") == conv2);
      CHECK(occ[0].at("when") <= occ[2].at("when"));
      // the chain of versions in the graph
      const auto v3 = unwrap(d.db->find_node(ids[2], "text_version"));
      REQUIRE(v3.has_value());
      const auto chain_links = unwrap(d.db->get_links(v3->id, "derived_from"));
      CHECK(std::any_of(chain_links.begin(), chain_links.end(), [&](const Link& l) { return l.src == v3->id && unwrap(d.db->get_node(l.dst))->label == ids[1]; }));
    }

    SUBCASE("a conversation is a source: every version group is a chain") {
      chain_of(*d.db, conv, {"Osobna wiadomość."}, "user", "");
      const auto chains = unwrap(chains_of_conversation(*d.db, conv));
      REQUIRE(chains.size() == 2);
      CHECK(chains[0].size() == 3);
      CHECK(chains[1].size() == 1);
      CHECK(!chains[1][0].source.group.empty());  // create_msg gives every message a group; a lone message is a group of one
      CHECK(chains[1][0].source.group != chains[0][0].source.group);
    }
  }

  TEST_CASE("kinds are data: an overlay renames the event, its edge follows; an unmapped op kind is an event of its own name; silence is a list") {
    Json overlay{{"genesis", Json{{"events", Json{{"delete", "zniknięcie"}}}, {"edges", Json{{"zniknięcie", "znikło_w"}}}, {"silent_ops", Json::array()}}}};
    const Json pack = merge_pack(builtin_pack(), overlay);
    const Segmenter seg = unwrap(Segmenter::from_pack(pack, "pl"));
    const Options opt = unwrap(Options::from_pack(pack));
    CHECK(opt.event_kind("delete") == "zniknięcie");
    CHECK(opt.event_kind("insert") == "addition");
    CHECK(opt.event_kind("equal") == "equal");         // silent list emptied: equal is an event too
    CHECK(opt.event_kind("teleport") == "teleport");   // unmapped: its own name
    CHECK(opt.edge_kind("zniknięcie") == "znikło_w");
    CHECK(opt.edge_kind("teleport") == "teleport");
    CHECK(opt.node_kind("fragment") == "text_fragment");
    CHECK(opt.node_kind("paste") == "text_paste");
    std::vector<VersionText> v;
    for (const char* t : {"Jedno zdanie. Drugie zdanie. Trzecie zdanie.", "Jedno zdanie. Trzecie zdanie."}) {
      VersionText x;
      x.source.id = std::string("v") + std::to_string(v.size() + 1);
      x.source.ordinal = static_cast<std::int64_t>(v.size() + 1);
      x.text = t;
      v.push_back(x);
    }
    const Chain c = unwrap(compute_chain(seg, v, opt));
    CHECK(count_kind(c, "zniknięcie") == 1);
    CHECK(count_kind(c, "equal") == 2);
    Db d;
    const MaterializeSummary s = unwrap(materialize(*d.db, c, opt));
    CHECK(s.events == 3);
    const auto frag = unwrap(d.db->find_node(seg.fingerprint("Drugie zdanie."), "text_fragment"));
    REQUIRE(frag.has_value());
    CHECK(unwrap(d.db->get_links(frag->id, "znikło_w")).size() == 1);
    CHECK(unwrap(d.db->get_links(frag->id, "deleted_in")).empty());
    // record_initial_additions is a preset: on, every fragment of the first version has an addition event
    Json on{{"genesis", Json{{"record_initial_additions", true}}}};
    const Options opt2 = unwrap(Options::from_pack(merge_pack(builtin_pack(), on)));
    const Chain c2 = unwrap(compute_chain(seg, v, opt2));
    CHECK(count_kind(c2, "addition") == 3);
    CHECK(count_kind(c2, "removal") == 1);
    const Json known = known_questions(opt);
    CHECK(known.contains("origin"));
    CHECK(known.at("origin").at("pl") == "Skąd pochodzi to zdanie");
  }

  TEST_CASE("properties on random version chains (synthetic, fixed seed): lineage total, removals one-to-one, keys unique, materialise idempotent") {
    std::mt19937 rng(20261030);
    const std::vector<std::string> words{"zespół", "etap", "moduł", "test", "budżet", "spotkanie", "wynik", "plan", "ryzyko", "termin", "Kowalska", "12", "kod", "dokument"};
    auto sentence = [&]() {
      std::uniform_int_distribution<int> n(2, 6), w(0, static_cast<int>(words.size()) - 1);
      std::string s;
      const int k = n(rng);
      for (int i = 0; i < k; ++i) s += (i ? " " : "") + words[static_cast<std::size_t>(w(rng))];
      s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
      return s + ".";
    };
    const Segmenter seg = unwrap(Segmenter::builtin("pl"));
    const Options opt = unwrap(Options::from_pack(builtin_pack()));
    std::size_t total_removals = 0, total_events = 0, chains_checked = 0, db_checked = 0;
    for (int trial = 0; trial < 300; ++trial) {
      std::uniform_int_distribution<int> len(0, 9), nver(1, 6), op(0, 3);
      std::vector<std::string> cur;
      const int n0 = len(rng);
      for (int i = 0; i < n0; ++i) cur.push_back(sentence());
      std::vector<VersionText> versions;
      const int nv = nver(rng);
      for (int v = 0; v < nv; ++v) {
        if (v > 0) {
          std::uniform_int_distribution<int> edits(0, 3);
          const int k = edits(rng);
          for (int e = 0; e < k; ++e) {
            const int kind = op(rng);
            if (kind == 0 && !cur.empty()) {
              std::uniform_int_distribution<std::size_t> at(0, cur.size() - 1);
              cur.erase(cur.begin() + static_cast<std::ptrdiff_t>(at(rng)));
            } else if (kind == 1) {
              std::uniform_int_distribution<std::size_t> at(0, cur.size());
              cur.insert(cur.begin() + static_cast<std::ptrdiff_t>(at(rng)), sentence());
            } else if (kind == 2 && !cur.empty()) {
              std::uniform_int_distribution<std::size_t> at(0, cur.size() - 1);
              std::string& s = cur[at(rng)];
              s.insert(s.size() - 1, " " + words[static_cast<std::size_t>(std::uniform_int_distribution<int>(0, static_cast<int>(words.size()) - 1)(rng))]);
            } else if (kind == 3 && cur.size() >= 2) {
              std::uniform_int_distribution<std::size_t> at(0, cur.size() - 1);
              const std::size_t a = at(rng), b = at(rng);
              std::string s = cur[a];
              cur.erase(cur.begin() + static_cast<std::ptrdiff_t>(a));
              cur.insert(cur.begin() + static_cast<std::ptrdiff_t>(std::min(b, cur.size())), s);
            }
          }
        }
        VersionText vt;
        vt.source.id = "m" + std::to_string(trial) + "_" + std::to_string(v);
        vt.source.group = "g" + std::to_string(trial);
        vt.source.ordinal = v + 1;
        vt.source.when = "2026-10-30T00:00:" + std::string(v < 10 ? "0" : "") + std::to_string(v) + "Z";
        vt.text = join(cur);
        versions.push_back(vt);
      }
      const Chain c = unwrap(compute_chain(seg, versions, opt));
      REQUIRE(c.versions.size() == versions.size());
      REQUIRE(c.steps.size() + 1 == versions.size());
      ++chains_checked;
      // (a) every fragment of every version has exactly one lineage ending at the first version or at an insert
      for (std::size_t v = 0; v < c.versions.size(); ++v)
        for (std::size_t j = 0; j < c.versions[v].fragments.size(); ++j) {
          const Lineage L = unwrap(lineage_of(c, v, j));
          REQUIRE(!L.steps.empty());
          CHECK(L.steps.front().version == v);
          CHECK(L.steps.front().index == j);
          if (L.origin == "first_version") {
            CHECK(L.origin_version == 0);
            CHECK(L.steps.back().op_kind == "origin");
          } else {
            CHECK(L.origin == "new_in_version");
            CHECK(L.steps.back().op_kind == "insert");
            CHECK(L.steps.back().version == L.origin_version);
            CHECK(L.origin_version >= 1);
          }
          for (std::size_t s = 1; s < L.steps.size(); ++s) CHECK(L.steps[s].version + 1 == L.steps[s - 1].version);
        }
      // (b) removals <-> delete ops, one to one per step; (c) every event key unique; every non-silent op has its event
      std::set<std::string> keys;
      std::size_t deletes = 0, nonsilent = 0;
      for (const auto& st : c.steps) {
        deletes += st.count("delete");
        for (const auto& o : st.ops) nonsilent += !opt.event_kind(o.kind).empty();
      }
      for (const auto& e : c.events) CHECK(keys.insert(e.key).second);
      CHECK(count_kind(c, "removal") == deletes);
      CHECK(c.events.size() == nonsilent);
      total_removals += deletes;
      total_events += c.events.size();
      // every removal event names a fragment of its `from` version at a_index with the same fingerprint
      for (const auto& e : c.events)
        if (e.kind == "removal") {
          std::size_t vi = 0;
          while (c.versions[vi].source.id != e.from.id) ++vi;
          CHECK(c.versions[vi].fragments.at(e.a_index).fingerprint == e.fragment);
          CHECK(e.b_index == kNone);
          CHECK(e.successor.empty());
        }
      // (d) materialise twice = once (a sample, the database is the slow part)
      if (trial % 8 == 0) {
        Db d;
        const MaterializeSummary s1 = unwrap(materialize(*d.db, c, opt));
        const Json after1 = graph_dump(*d.db);
        const MaterializeSummary s2 = unwrap(materialize(*d.db, c, opt));
        CHECK(s2.nodes_created == 0);
        CHECK(s1.events == c.events.size());
        CHECK(graph_dump(*d.db) == after1);
        // one event node per removal event, each with its deleted_in edge
        std::size_t removal_nodes = 0;
        for (const auto& n : unwrap(d.db->list_nodes("text_event", 100000))) removal_nodes += n.metadata.value("kind", "") == "removal";
        CHECK(removal_nodes == count_kind(c, "removal"));
        ++db_checked;
      }
    }
    MESSAGE("chains " << chains_checked << ", events " << total_events << ", removals " << total_removals << ", materialised twice " << db_checked);
    CHECK(total_removals > 50);
  }
}
