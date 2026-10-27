// Layout fingerprint of an export (IMPORT_JAKO_PLIKI_W_GRAFIE §2.3-2.4, package P2, src/import/layout_fingerprint.h).
// The fixtures get the layout they were built as (or an honest "uncertain"/"unknown"); a generative mutation test perturbs
// the fixtures and a synthetic Gemini export (remove / add / rename members, move under a dated folder, nest in a ZIP, swap
// a key, wrap array <-> object, truncate) and checks the property of the design: the fingerprint NEVER returns a confident
// wrong layout. A new provider is data only: a synthetic profile in the person's overlay is picked up by the import.
// person_content: the import marks the values that are the person's own content (labels, file names, titles).
#include <doctest/doctest.h>

#include <fstream>
#include <random>
#include <regex>
#include <set>
#include <string>

#include "import/layout_fingerprint.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/platforms.h"
#include "loom/provenance.h"
#include "loom/runtime_profile.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"
#include "test_helpers.h"

using namespace loom;
using namespace loom::import_graph;
using loom::test::open_db;
using loom::test::unwrap;

namespace {

std::filesystem::path xfix(const std::string& name) { return std::filesystem::path(LOOM_TEST_FIXTURES) / "exports" / name; }

Json config() {
  static const Json c = [] {
    auto p = RuntimeProfile::builtin("import_graph");
    REQUIRE(p.has_value());
    return p->values().value("layout_fingerprint", Json::object());
  }();
  return c;
}

std::string chosen_key(const Json& fp) {
  const Json& c = fp["chosen"];
  return c.is_object() ? json::get_string(c, "key") : std::string();
}

// The fixtures and what each one is (a set: empty = no layout may be confident; several = the data cannot tell them apart).
struct Truth {
  const char* file;
  std::set<std::string> acceptable;
};
const Truth kTruth[] = {
    {"openai_2026_sharded.zip", {"openai-sharded-2026"}},
    {"openai_legacy.zip", {"openai-single-2023", "openai-single-2025"}},
    {"anthropic_2026_full.zip", {"anthropic-2026-full"}},
    {"anthropic_legacy.zip", {"anthropic-2024-text"}},
    {"openai_single_conversations.json", {}},
    {"anthropic_single_chat.json", {}},
    {"nested_container.zip", {}},
    {"malformed_export.zip", {}},
    {"unknown_provider.zip", {}},
};

LayoutEvidence gemini_evidence() {
  LayoutEvidence ev;
  ev.container = "zip";
  ev.members.push_back({"Takeout/archive_browser.html", "", false});
  ev.members.push_back({"Takeout/My Activity/Gemini Apps/MyActivity.json",
                        R"([{"header":"Gemini Apps","title":"Prompted hello","time":"2026-01-01T00:00:00Z","products":["Gemini Apps"]}])", true});
  normalise_layout_evidence(ev, config());
  return ev;
}

// Every member pattern of the configured layouts: a mutation that introduces a name matching one of them adds the evidence of
// some layout (it makes another export), it is not noise; such cases are skipped and counted.
const std::vector<std::regex>& known_patterns() {
  static const std::vector<std::regex> out = [] {
    std::vector<std::regex> v;
    const Json layouts = config().value("layouts", Json::object());
    for (const auto& it : layouts.items()) {
      const Json ds = it.value().value("descriptors", Json::array());
      for (const auto& d : ds) {
        const std::string kind = json::get_string(d, "d");
        if (kind.rfind("member_", 0) == 0) v.emplace_back(json::get_string(d, "arg"));
        if (d.contains("member")) v.emplace_back(json::get_string(d, "member"));
      }
    }
    return v;
  }();
  return out;
}
bool known_name(const std::string& n) {
  for (const auto& re : known_patterns())
    if (std::regex_search(n, re)) return true;
  return false;
}

const char* const kNoise[] = {"conversations-backup.json", "Conversations.JSON", "conversation.json", "users.json.bak",
                              "data/export.json", "notes.txt", "conversations-0a0.json", "memories.json.old",
                              "something_new_2028.json", "README", "chat.htm", "projects (1).json"};

void replace_all(std::string& s, const std::string& a, const std::string& b) {
  for (std::size_t at = s.find(a); at != std::string::npos; at = s.find(a, at + b.size())) s.replace(at, a.size(), b);
}

// One mutation; returns false when the mutation does not apply or introduces a known name (then the case is skipped).
bool mutate(LayoutEvidence& ev, std::mt19937& rng) {
  if (ev.members.empty()) return false;
  std::uniform_int_distribution<std::size_t> pick_member(0, ev.members.size() - 1);
  std::uniform_int_distribution<std::size_t> pick_noise(0, std::size(kNoise) - 1);
  switch (std::uniform_int_distribution<int>(0, 8)(rng)) {
    case 0: ev.members.erase(ev.members.begin() + static_cast<std::ptrdiff_t>(pick_member(rng))); return true;
    case 1: {
      const std::string n = kNoise[pick_noise(rng)];
      if (known_name(n)) return false;
      ev.members.push_back({n, "", false});
      return true;
    }
    case 2: {
      const std::string n = kNoise[pick_noise(rng)];
      if (known_name(n)) return false;
      ev.members[pick_member(rng)].name = n;
      return true;
    }
    case 3:
      for (auto& m : ev.members) m.name = "export-2026-10-27/" + m.name;
      normalise_layout_evidence(ev, config());
      return true;
    case 4: ev.members = {LayoutMember{"inner.zip", "", false}}; return true;
    case 5: {
      for (auto& m : ev.members)
        for (const auto& [a, b] : {std::pair<std::string, std::string>{"\"mapping\"", "\"mapp1ng\""},
                                   {"\"chat_messages\"", "\"chat_msgs\""}, {"\"header\"", "\"headr\""}})
          replace_all(m.prefix, a, b);
      return true;
    }
    case 6: {
      auto& m = ev.members[pick_member(rng)];
      if (!m.has_prefix || m.prefix.empty()) return false;
      m.prefix = m.prefix[0] == '[' ? "{\"conversations\":" + m.prefix + "}" : "[" + m.prefix + "]";
      return true;
    }
    case 7: {
      auto& m = ev.members[pick_member(rng)];
      if (!m.has_prefix) return false;
      m.prefix.resize(std::uniform_int_distribution<std::size_t>(0, m.prefix.size())(rng));
      return true;
    }
    default: {
      auto& m = ev.members[pick_member(rng)];
      m.name += ".orig";
      return !known_name(m.name);
    }
  }
}

void check_property(const Json& fp, const std::set<std::string>& acceptable, const std::string& what) {
  double sum = json::get_number(fp, "unknown_share", 0.0);
  for (const auto& c : fp["candidates"]) sum += json::get_number(c, "share", 0.0);
  CHECK(sum == doctest::Approx(1.0).epsilon(0.01));
  if (json::get_string(fp, "state") == "confident") {
    INFO(what << " -> confident " << chosen_key(fp));
    CHECK(acceptable.count(chosen_key(fp)) == 1);
  }
}

struct Fx {
  fsutil::TempDir td;
  std::unique_ptr<Database> db;
  EventBus bus;
  std::unique_ptr<BlobStore> blobs;
  std::unique_ptr<ProvenanceStore> prov;
  std::unique_ptr<ConversationImporter> imp;
  Fx() {
    db = open_db(td.path() / "chatadhd.db");
    blobs = std::make_unique<BlobStore>(td.path() / "blobs", *db);
    prov = std::make_unique<ProvenanceStore>(*db);
    imp = std::make_unique<ConversationImporter>(*db, bus, blobs.get(), prov.get());
  }
  ImportResult run(const std::filesystem::path& p) {
    ImportOptions o;
    o.export_mode = ExportMode::On;
    return unwrap(imp->import_file(p, o));
  }
};

}  // namespace

TEST_SUITE("import_layout") {
  TEST_CASE("fixtures: the layout they were built as, or an honest uncertain / unknown with alternatives") {
    const Json cfg = config();
    for (const auto& t : kTruth) {
      const Json fp = fingerprint_layout(layout_evidence(xfix(t.file), "", cfg), cfg);
      INFO(t.file << ": " << json::dump(fp["chosen"]) << " state " << json::get_string(fp, "state"));
      CHECK(fp["advisories"].empty());
      check_property(fp, t.acceptable, t.file);
      if (t.acceptable.empty()) CHECK(json::get_string(fp, "state") != "confident");
      if (t.acceptable.size() == 1) {
        const std::string want = *t.acceptable.begin();
        if (want == "anthropic-2024-text") {
          // memories.json is optional in 2026 too: without it the two Claude layouts are not told apart (a tie, both listed)
          CHECK(json::get_string(fp, "state") == "uncertain");
          CHECK(fp["tie"].get<bool>());
          CHECK(json::get_string(fp["class"], "id") == "export.anthropic.claude");
          CHECK(json::get_string(fp["class"], "state") == "confident");
        } else {
          CHECK(json::get_string(fp, "state") == "confident");
          CHECK(chosen_key(fp) == want);
        }
      }
      if (t.acceptable.size() > 1) {
        CHECK(json::get_string(fp, "state") == "uncertain");
        CHECK(t.acceptable.count(chosen_key(fp)) == 1);
        CHECK(json::get_string(fp["class"], "id") == "export.openai.chatgpt");
      }
    }
    // the sharded export: the single-file layouts are excluded and say what failed
    const Json fp = fingerprint_layout(layout_evidence(xfix("openai_2026_sharded.zip"), "", cfg), cfg);
    for (const auto& c : fp["candidates"])
      if (json::get_string(c, "key") == "openai-single-2023") {
        CHECK(c["excluded"].get<bool>());
        CHECK(!c["failed"].empty());
      }
    CHECK(json::get_string(fp["chosen"], "id").rfind("sha256:", 0) == 0);
  }

  TEST_CASE("synthetic Gemini Takeout: confident, also under a root folder; a bare file is never a confident layout") {
    const Json cfg = config();
    LayoutEvidence ev = gemini_evidence();
    CHECK(ev.root == "Takeout");
    const Json fp = fingerprint_layout(ev, cfg);
    CHECK(json::get_string(fp, "state") == "confident");
    CHECK(chosen_key(fp) == "gemini-takeout-myactivity");
    LayoutEvidence bare;
    bare.container = "file";
    bare.members.push_back({"conversations.json", R"([{"mapping":{},"title":"x"}])", true});
    CHECK(json::get_string(fingerprint_layout(bare, cfg), "state") == "unknown");
  }

  TEST_CASE("generative mutations never yield a confident wrong layout") {
    const Json cfg = config();
    struct Base {
      std::string name;
      LayoutEvidence ev;
      std::set<std::string> acceptable;
    };
    std::vector<Base> bases;
    for (const auto& t : kTruth) bases.push_back({t.file, layout_evidence(xfix(t.file), "", cfg), t.acceptable});
    bases.push_back({"gemini(synthetic)", gemini_evidence(), {"gemini-takeout-myactivity"}});
    std::mt19937 rng(20261027);
    std::int64_t cases = 0, skipped = 0, confident = 0, unknown = 0;
    // every single-member removal of every base
    for (const auto& b : bases)
      for (std::size_t i = 0; i < b.ev.members.size(); ++i) {
        LayoutEvidence ev = b.ev;
        ev.members.erase(ev.members.begin() + static_cast<std::ptrdiff_t>(i));
        const Json fp = fingerprint_layout(ev, cfg);
        check_property(fp, b.acceptable, b.name + " without #" + std::to_string(i));
        ++cases;
      }
    // random sequences of 1-3 mutations
    for (int round = 0; round < 3000; ++round) {
      const Base& b = bases[static_cast<std::size_t>(round) % bases.size()];
      LayoutEvidence ev = b.ev;
      const int steps = std::uniform_int_distribution<int>(1, 3)(rng);
      bool ok = true;
      for (int s = 0; s < steps && ok; ++s) ok = mutate(ev, rng);
      if (!ok) {
        ++skipped;
        continue;
      }
      const Json fp = fingerprint_layout(ev, cfg);
      check_property(fp, b.acceptable, b.name + " round " + std::to_string(round));
      ++cases;
      const std::string st = json::get_string(fp, "state");
      confident += st == "confident";
      unknown += st == "unknown";
    }
    MESSAGE("mutation cases " << cases << ", skipped " << skipped << ", confident " << confident << ", unknown " << unknown);
    CHECK(cases > 2000);
    CHECK(confident > 0);  // the property is not satisfied vacuously
    CHECK(unknown > 0);
  }

  TEST_CASE("a new provider is data only: a synthetic profile in the person's overlay is picked up by the import") {
    Fx fx;
    const Json overlay = Json::parse(R"({"layout_fingerprint": {
      "claims": {"claim:acme-layout": {"alpha": 9, "beta": 1, "tier": "C", "observed": "2026-10-27", "sources": ["test overlay"]}},
      "layouts": {"acme-export-2027": {"class": "export.acme", "class_id": "export.acme/2027",
        "label": {"pl": "Acme, eksport 2027", "en": "Acme export 2027"},
        "descriptors": [{"d": "container", "arg": "^zip$", "claim": "claim:acme-layout"},
                        {"d": "member_present", "arg": "^export\\.json$", "claim": "claim:acme-layout"},
                        {"d": "member_present", "arg": "^readme\\.md$", "claim": "claim:acme-layout"},
                        {"d": "member_present", "arg": "^files/", "claim": "claim:acme-layout", "optional": true}]}}}})");
    std::filesystem::create_directories(fx.td.path() / "profiles");
    std::ofstream(fx.td.path() / "profiles" / "import_graph.pack") << overlay.dump(2);
    const ImportResult r = fx.run(xfix("unknown_provider.zip"));
    REQUIRE(r.import_graph.is_object());
    const Json& fp = r.import_graph["layout"];
    REQUIRE(fp.is_object());
    CHECK(json::get_string(fp, "state") == "confident");
    CHECK(chosen_key(fp) == "acme-export-2027");
    const std::string text = json::get_string(r.import_graph["summary"]["texts"], "pl");
    CHECK(text.find("Układ eksportu: Acme, eksport 2027.") != std::string::npos);
    // the builtin layouts are still there beside the overlay's (objects merge)
    CHECK(fp["candidates"].size() >= 8);
  }

  TEST_CASE("the export node and the summary carry the layout and the alternatives; person content is marked") {
    Fx fx;
    const ImportResult r = fx.run(xfix("anthropic_legacy.zip"));
    REQUIRE(r.import_graph.is_object());
    const Json summary = r.import_graph["summary"];
    const std::string pl = json::get_string(summary["texts"], "pl");
    INFO(pl);
    CHECK(pl.find("Układ eksportu: prawdopodobnie Claude") != std::string::npos);
    CHECK(pl.find("Inne możliwości: Claude") != std::string::npos);
    CHECK(summary["person_content"] == Json::array({"anthropic_legacy.zip"}));
    const std::string export_id = json::get_string(r.import_graph, "export_node");
    auto node = unwrap(fx.db->get_node(export_id));
    REQUIRE(node.has_value());
    const Json block = node->metadata["import_graph"];
    CHECK(json::get_string(block["attrs"]["layout"], "state") == "uncertain");
    CHECK(block["attrs"]["layout"]["alternatives"].size() >= 1);
    CHECK(block["person_content"] == Json::array({"label", "attrs.file"}));
    // a confident layout becomes a facet of the export node (the class of graph/classes.pack)
    Fx fx2;
    const ImportResult s = fx2.run(xfix("openai_2026_sharded.zip"));
    auto n2 = unwrap(fx2.db->get_node(json::get_string(s.import_graph, "export_node")));
    REQUIRE(n2.has_value());
    const Json facets = n2->metadata["import_graph"]["facets"];
    CHECK(std::find(facets.begin(), facets.end(), Json("export.openai.chatgpt/sharded-2026")) != facets.end());
    const std::string spl = json::get_string(s.import_graph["summary"]["texts"], "pl");
    CHECK(spl.find("Układ eksportu: ChatGPT, rozmowy podzielone na pliki (2026).") != std::string::npos);
  }

  TEST_CASE("shard predicate: a name starting with the prefix is a shard only when it matches the pattern") {
    CHECK(platform_for_shard("conversations-000.json") != nullptr);
    CHECK(platform_for_shard("conversations-12.json") != nullptr);
    CHECK(platform_for_shard("conversations-backup.json") == nullptr);
    CHECK(platform_for_shard("conversations-000.json.bak") == nullptr);
  }
}
