// Lossless OpenAI/Anthropic export import, checked against the independent
// oracle tests/fixtures/exports/EXPECTED.json (computed by
// tools/gen_export_fixtures.py straight from the generated data, never from
// Loom). The oracle is read-only here: a mismatch is a bug in the importer (or
// a documented oracle question), never a reason to edit EXPECTED.json.
//
// Counts are recomputed from what is stored in the DB (messages.metadata /
// conversations.metadata / nodes), not taken from the importer's own report;
// the report is then checked against the same oracle and against the DB tally.
#include <doctest/doctest.h>

#include <fstream>
#include <map>
#include <set>

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/loom.h"
#include "loom/provenance.h"
#include "loom/util/fs.h"
#include "loom/util/json.h"
#include "loom/util/utf8.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

namespace {

std::filesystem::path xfix(const std::string& name) {
  return std::filesystem::path(LOOM_TEST_FIXTURES) / "exports" / name;
}

const Json& expected() {
  static Json j = [] {
    auto txt = fsutil::read_file(xfix("EXPECTED.json"));
    REQUIRE(txt.has_value());
    auto p = json::parse(*txt);
    REQUIRE(p.has_value());
    return *p;
  }();
  return j;
}

struct Fx {
  fsutil::TempDir td;
  std::unique_ptr<Database> db;
  EventBus bus;
  std::unique_ptr<BlobStore> blobs;
  std::unique_ptr<ProvenanceStore> prov;
  std::unique_ptr<ConversationImporter> imp;
  Fx() {
    db = open_db(td.path() / "d.db");
    blobs = std::make_unique<BlobStore>(td.path() / "blobs", *db);
    prov = std::make_unique<ProvenanceStore>(*db);
    imp = std::make_unique<ConversationImporter>(*db, bus, blobs.get(), prov.get());
  }
  Result<ImportResult> run(const std::string& name, ExportMode mode = ExportMode::On) {
    ImportOptions o;
    o.export_mode = mode;
    return imp->import_file(xfix(name), o);
  }
  ImportResult ok(const std::string& name, ExportMode mode = ExportMode::On) { return unwrap(run(name, mode)); }
};

std::int64_t geti(const Json& j, const char* k) { return json::get_int(j, k, -12345); }

// Tally of the DB contents in the shape of EXPECTED.json "counts".
Json recount(Database& db, const std::vector<Conversation>& convs) {
  std::map<std::string, std::int64_t> t, bk;
  for (const char* k : {"account", "artifact", "attachment", "block", "branch", "citation", "citation_group", "conversation",
                        "current_path_messages", "custom_instruction", "feedback", "fork_points", "memory", "message", "project",
                        "project_doc", "record", "shared_link"})
    t[k] = 0;
  for (const auto& c : convs) {
    auto conv = unwrap(db.get_conv(c.id));
    REQUIRE(conv.has_value());
    t["conversation"]++;
    const Json& cex = conv->metadata["export"];
    t["fork_points"] += geti(cex["graph"], "fork_points");
    for (const auto& m : unwrap(db.get_msgs(c.id, true))) {
      const Json& ex = m.metadata["export"];
      t["message"]++;
      for (const auto& b : ex["blocks"]) {
        t["block"]++;
        bk[b["kind"].get<std::string>()]++;
      }
      t["attachment"] += static_cast<std::int64_t>(ex["attachments"].size());
      t["citation"] += static_cast<std::int64_t>(ex["citations"].size());
      t["citation_group"] += static_cast<std::int64_t>(ex["citation_groups"].size());
      t["custom_instruction"] += geti(ex, "custom_instruction");
      if (ex["memory"].get<bool>()) t["memory"]++;
      if (ex.contains("artifact") && !ex["artifact"].is_null()) t["artifact"]++;
      if (ex.contains("artifacts")) t["artifact"] += static_cast<std::int64_t>(ex["artifacts"].size());
      if (ex["on_current_path"].get<bool>()) t["current_path_messages"]++;
      if (ex["leaf"].get<bool>()) t["branch"]++;
    }
  }
  const std::pair<const char*, const char*> kinds[] = {{"export:account", "account"},       {"export:feedback", "feedback"},
                                                       {"export:shared_link", "shared_link"}, {"export:project", "project"},
                                                       {"export:project_doc", "project_doc"}, {"export:memory", "memory"},
                                                       {"export:artifact", "artifact"}};
  for (const auto& [kind, key] : kinds) t[key] += static_cast<std::int64_t>(unwrap(db.list_nodes(kind, 100000)).size());
  Json out = Json::object();
  for (const auto& [k, v] : t) out[k] = v;
  Json b = Json::object();
  for (const auto& [k, v] : bk) b[k] = v;
  out["block_kind"] = b;
  return out;
}

void check_counts(const Json& got, const Json& exp, const std::string& label) {
  for (auto it = exp.begin(); it != exp.end(); ++it) {
    INFO(label << " counts." << it.key());
    if (it.key() == "block_kind") {
      REQUIRE(got.contains("block_kind"));
      CHECK(got["block_kind"].size() == it.value().size());
      for (auto k = it.value().begin(); k != it.value().end(); ++k) {
        INFO(label << " block_kind." << k.key());
        REQUIRE(got["block_kind"].contains(k.key()));
        CHECK(got["block_kind"][k.key()] == k.value());
      }
    } else {
      REQUIRE(got.contains(it.key()));
      CHECK(got[it.key()] == it.value());
    }
  }
  CHECK(got.size() == exp.size());
}

std::set<std::string> strset(const Json& a) {
  std::set<std::string> s;
  for (const auto& x : a) s.insert(x.get<std::string>());
  return s;
}

// Imports `name`, then checks DB tally + report against the oracle.
ImportResult check_against_oracle(Fx& fx, const std::string& name) {
  const Json& exp = expected()[name];
  ImportResult r = fx.ok(name);
  INFO(name);
  REQUIRE(!r.export_report.is_null());
  const Json& rep = r.export_report;
  CHECK(rep["provider"] == exp["provider"]);
  check_counts(recount(*fx.db, r.conversations), exp["counts"], name + " [db]");
  check_counts(rep["counts"], exp["counts"], name + " [report]");
  if (exp.contains("members")) CHECK(rep["member_count"] == exp["members"]);
  CHECK(rep["errors"].empty());
  CHECK(rep["partial"] == false);
  CHECK(rep["leaves_preserved"] == rep["json_leaves"]);  // every JSON leaf of the conversations is stored verbatim
  if (exp.contains("json_leaves")) CHECK(rep["json_leaves"] == exp["json_leaves"]);
  if (exp.contains("asset_members")) CHECK(strset(rep["asset_members"]) == strset(exp["asset_members"]));
  if (exp.contains("unreferenced_members")) CHECK(strset(rep["unreferenced_members"]) == strset(exp["unreferenced_members"]));
  if (exp.contains("unresolved_keys")) CHECK(strset(rep["unresolved_keys"]) == strset(exp["unresolved_keys"]));
  if (exp.contains("duplicate_groups")) CHECK(rep["duplicate_groups"] == exp["duplicate_groups"]);
  if (exp.contains("pointer_links")) {
    CHECK(rep["pointer_links"].size() == exp["pointer_links"].size());
    for (auto it = exp["pointer_links"].begin(); it != exp["pointer_links"].end(); ++it) {
      INFO("pointer " << it.key());
      REQUIRE(rep["pointer_links"].contains(it.key()));
      CHECK(rep["pointer_links"][it.key()] == it.value());
    }
  }
  return r;
}

const Conversation* conv_titled(const ImportResult& r, const std::string& title) {
  for (const auto& c : r.conversations) {
    if (c.title == title) return &c;
  }
  return nullptr;
}

std::vector<Message> all_msgs(Database& db, const Conversation& c) { return unwrap(db.get_msgs(c.id, true)); }

const Message* by_key(const std::vector<Message>& v, const std::string& key) {
  for (const auto& m : v) {
    if (m.metadata["export"]["key"] == key) return &m;
  }
  return nullptr;
}

}  // namespace

TEST_SUITE("import_exports") {
  TEST_CASE("oracle: every fixture matches EXPECTED.json (DB tally and report)") {
    for (const char* name : {"openai_legacy.zip", "openai_2026_sharded.zip", "anthropic_legacy.zip", "anthropic_2026_full.zip",
                             "openai_single_conversations.json", "anthropic_single_chat.json"}) {
      SUBCASE(name) {
        Fx fx;
        check_against_oracle(fx, name);
      }
    }
  }

  TEST_CASE("openai 2026: branches, hidden plumbing, timestamps, models, versions") {
    Fx fx;
    auto r = check_against_oracle(fx, "openai_2026_sharded.zip");
    REQUIRE(r.conversations.size() == 7);
    auto* c1 = conv_titled(r, "Migracja bazy danych");
    REQUIRE(c1);
    CHECK(c1->source == "import:openai");
    CHECK(c1->created == "2025-10-09T08:53:20Z");  // create_time 1760000000.0
    CHECK(c1->metadata["export"]["fields"]["is_starred"] == true);
    CHECK(c1->metadata["export"]["current_node"] == "c1-a4");

    // Active view = the visible messages of the current branch, in order.
    auto active = unwrap(fx.db->get_msgs(c1->id));
    REQUIRE(active.size() == 4);
    CHECK(active[0].role == "user");
    CHECK(active[0].text == "Jak zaplanować migrację bazy SQLite do PostgreSQL?");
    CHECK(active[0].created == "2025-10-09T08:53:21Z");
    CHECK(active[1].role == "assistant");
    CHECK(active[1].model.value_or("") == "gpt-5-t-mini");
    CHECK(active[2].text == "A co z indeksami i sekwencjami?");
    CHECK(active[3].text == "W bazie jest 42 tabel.");

    auto msgs = all_msgs(*fx.db, *c1);
    REQUIRE(msgs.size() == 14);  // 15 mapping nodes, the root carries no message
    auto st = [&](const char* key) { return by_key(msgs, key)->status; };
    // hidden plumbing on the current path is preserved but excluded from prompts
    for (const char* k : {"c1-sys", "c1-th1", "c1-rr1", "c1-a2", "c1-t1", "c1-a4"}) CHECK(st(k) == "excluded");
    // alternate branch (edited user turn + 3 regenerations) is kept as versions
    for (const char* k : {"c1-u2b", "c1-a5", "c1-a5b", "c1-a5c"}) CHECK(st(k) == "version");
    CHECK(st("c1-u2") == "active");
    // versions of the same turn share a group; version numbers follow provider order
    const Message* u2 = by_key(msgs, "c1-u2");
    const Message* u2b = by_key(msgs, "c1-u2b");
    CHECK(u2->version_group_id == u2b->version_group_id);
    CHECK(u2->version_num == 1);
    CHECK(u2b->version_num == 2);
    auto regen = unwrap(fx.db->get_versions(*by_key(msgs, "c1-a5")->version_group_id));
    CHECK(regen.size() == 3);
    CHECK(by_key(msgs, "c1-a5c")->version_num == 3);
    CHECK(by_key(msgs, "c1-a5")->parent_id == u2b->id);
    CHECK(by_key(msgs, "c1-u1")->parent_id.has_value());
    CHECK(by_key(msgs, "c1-sys")->weight == 0.0);

    // tool/thinking/citation/artifact details live in metadata.export
    const Json& th = by_key(msgs, "c1-th1")->metadata["export"];
    CHECK(th["blocks"].size() == 2);
    CHECK(th["blocks"][0]["kind"] == "reasoning");
    CHECK(th["raw"]["content"]["thoughts"][0]["summary"] == "Porównuję ryzyka migracji");
    CHECK(th["channel"] == "analysis");
    const Json& a1 = by_key(msgs, "c1-a1")->metadata["export"];
    CHECK(a1["citations"].size() == 4);       // 1 legacy + 2 content_references + 1 search_result entry
    CHECK(a1["citation_groups"].size() == 1);
    CHECK(by_key(msgs, "c1-a4")->metadata["export"]["artifact"]["name"] == "plan-migracji");
    CHECK(by_key(msgs, "c1-t1")->role == "tool");
    CHECK(by_key(msgs, "c1-t1")->metadata["export"]["raw"]["author"]["name"] == "python");

    // custom instructions + memory
    const Conversation* c4 = nullptr;
    for (const auto& c : r.conversations) {
      if (c.title.rfind("Zapami\xC4\x99taj: preferuj", 0) == 0) c4 = &c;
    }
    REQUIRE(c4);  // null title -> generated from the first user message (first 60 code points)
    CHECK(utf8::length(c4->title) == 60);
    CHECK(c4->metadata["export"]["title_source"] == "generated");
    CHECK(c4->metadata["export"]["fields"]["future_conversation_field"]["flag"] == true);  // unknown keys kept
    auto m4 = all_msgs(*fx.db, *c4);
    CHECK(by_key(m4, "c4-ci")->metadata["export"]["custom_instruction"] == 2);
    CHECK(by_key(m4, "c4-mem")->metadata["export"]["memory"] == true);
    CHECK(by_key(m4, "c4-a3")->metadata["export"]["blocks"][0]["kind"] == "unknown");  // future content type kept verbatim
    CHECK(by_key(m4, "c4-a3")->metadata["export"]["raw"]["content"]["widget"]["series"].size() == 3);
    // lone surrogate cannot be stored in UTF-8: replaced (counted in the report), never a parse failure
    CHECK(by_key(m4, "c4-a2")->text.find("\xEF\xBF\xBD") != std::string::npos);
    CHECK(r.export_report["repairs"]["conversations-001.json"]["lone_surrogates"] == 1);
  }

  TEST_CASE("openai 2026: pathological tree (dangling current_node, orphan, cycle, null content, empty conversations)") {
    Fx fx;
    auto r = check_against_oracle(fx, "openai_2026_sharded.zip");
    auto* c5 = conv_titled(r, "Patologie");
    REQUIRE(c5);
    auto m = all_msgs(*fx.db, *c5);
    CHECK(m.size() == 6);  // every node with a message is kept, reachable or not
    CHECK(c5->metadata["export"]["current_node_resolved"] == false);
    CHECK(c5->metadata["export"]["current_path_inferred"] == true);
    CHECK(by_key(m, "c5-u1")->status == "active");
    CHECK(by_key(m, "c5-a1")->status == "active");
    CHECK(by_key(m, "c5-orphan")->status == "version");  // reachable root, not on the inferred branch
    CHECK(by_key(m, "c5-cyc1")->status == "excluded");
    CHECK(by_key(m, "c5-cyc2")->status == "excluded");
    CHECK(by_key(m, "c5-cyc1")->metadata["export"]["reachable"] == false);
    CHECK(by_key(m, "c5-null")->metadata["export"]["hidden_reasons"].size() >= 1);
    // strict current path per current_node: none of them
    for (const auto& x : m) CHECK(x.metadata["export"]["on_current_path"] == false);
    auto* c6 = conv_titled(r, "Pusta");
    REQUIRE(c6);
    CHECK(all_msgs(*fx.db, *c6).empty());
    auto* c7 = conv_titled(r, "Bez mapping");
    REQUIRE(c7);
    CHECK(c7->metadata["export"]["has_mapping"] == false);
  }

  TEST_CASE("openai 2026: attachments/pointers resolved to archive members, bytes kept in the blob store") {
    Fx fx;
    auto r = check_against_oracle(fx, "openai_2026_sharded.zip");
    auto* c3 = conv_titled(r, "Obrazy i dźwięk");
    REQUIRE(c3);
    CHECK(c3->metadata["export"]["fields"]["gizmo_id"] == "g-p-0123456789abcdef-zenit");
    auto m = all_msgs(*fx.db, *c3);
    const Message* u1 = by_key(m, "c3-u1");
    REQUIRE(u1);
    REQUIRE(u1->attachments.size() == 1);
    CHECK(std::filesystem::exists(u1->attachments[0].get<std::string>()));  // readable blob path
    const Json& ex = u1->metadata["export"];
    CHECK(ex["attachments"][0]["member"] == "file-A1b2C3d4E5f6G7h8-schemat.png");
    CHECK(ex["attachments"][0]["method"] == "id_prefix");
    CHECK(ex["pointers"][0]["member"] == "file-A1b2C3d4E5f6G7h8-schemat.png");
    CHECK(ex["raw"]["metadata"]["attachments"][0]["name"] == "schemat.png");
    // name-only evidence, unresolvable id, boundary-safe prefix
    const Json& u2 = by_key(m, "c3-u2")->metadata["export"];
    std::map<std::string, std::string> method;
    for (const auto& a : u2["attachments"]) method[a["key"].get<std::string>()] = a.contains("method") ? a["method"].get<std::string>() : "";
    CHECK(method["file-NoSuchId0"] == "name");
    CHECK(method["file-Boundary1"] == "id_prefix");
    // dalle / sediment / user-* layouts and the unlinked one
    const Json& a3 = by_key(m, "c3-a3")->metadata["export"];
    REQUIRE(a3["pointers"].size() == 3);
    CHECK(a3["pointers"][0]["member"] == "user-FAKEUSER0001/file_00000000c0ffee01-66666666-7777-4888-9999-000000000000.png");
    CHECK(a3["pointers"][2]["resolved"] == false);
    CHECK(by_key(m, "c3-t1")->metadata["export"]["pointers"][0]["member"] ==
          "dalle-generations/file-Dq3Zx9-11111111-2222-4333-8444-555555555555.webp");
    // voice: transcript is the message text
    CHECK(by_key(m, "c3-u3")->text == "Cześć, słychać mnie?");
    // every asset (also the unreferenced ones) is in the blob store
    CHECK(r.export_report["asset_members"].size() == 12);
  }

  TEST_CASE("openai 2026: non-conversation members become nodes; unknown members are reported, not dropped") {
    Fx fx;
    auto r = check_against_oracle(fx, "openai_2026_sharded.zip");
    const Json& rep = r.export_report;
    CHECK(unwrap(fx.db->list_nodes("export:account", 100)).size() == 1);
    auto fb = unwrap(fx.db->list_nodes("export:feedback", 100));
    REQUIRE(fb.size() == 2);
    auto links = unwrap(fx.db->get_links(fb[0].id));
    CHECK(links.size() + unwrap(fx.db->get_links(fb[1].id)).size() == 1);  // fb-2 points at a message that is not in the export
    bool warned = false;
    for (const auto& w : rep["warnings"]) warned = warned || w.get<std::string>().find("c2-MISSING") != std::string::npos;
    CHECK(warned);
    auto unk = strset(rep["unknown_members"]);
    CHECK(unk.count("export_manifest.json") == 1);
    CHECK(unk.count("something_new_2027.json") == 1);
    auto members = unwrap(fx.db->list_nodes("export:member", 100));
    bool found_new = false;
    for (const auto& n : members) found_new = found_new || n.content.find("Nowa rzecz") != std::string::npos;
    CHECK(found_new);  // verbatim content of the unknown member
    std::map<std::string, std::string> disp;
    for (const auto& m : rep["members"]) disp[m["name"].get<std::string>()] = m["disposition"].get<std::string>();
    CHECK(disp["chat.html"] == "offline_viewer_skipped");  // duplicate rendering, must not create a second copy of every chat
    CHECK(disp["conversations-000.json"] == "conversations");
    CHECK(disp["file-Zz9Dat.dat"] == "asset");
    CHECK(disp["user.json"] == "record");
    CHECK(rep["counts"]["record"] == 0);
  }

  TEST_CASE("openai legacy: linear chat, feedback, empty optional files, a flagged-hidden turn is kept") {
    Fx fx;
    auto r = check_against_oracle(fx, "openai_legacy.zip");
    REQUIRE(r.conversations.size() == 2);
    auto* c1 = conv_titled(r, "Pierwsza rozmowa");
    REQUIRE(c1);
    auto all = all_msgs(*fx.db, *c1);
    REQUIRE(all.size() == 4);
    // L1-0 carries is_visually_hidden_from_conversation: preserved, marked hidden, kept out of prompts
    CHECK(by_key(all, "L1-0")->status == "excluded");
    CHECK(by_key(all, "L1-0")->metadata["export"]["hidden_reasons"][0] == "visually_hidden");
    CHECK(unwrap(fx.db->get_msgs(c1->id)).size() == 3);
    CHECK(c1->created == "2022-12-02T16:53:20Z");  // 1670000000
    auto* c2 = conv_titled(r, "Second conversation");
    REQUIRE(c2);
    for (const auto& m : all_msgs(*fx.db, *c2)) CHECK(m.status == "active");
  }

  TEST_CASE("anthropic 2026: blocks, branches, attachments, artifacts, projects, memories") {
    Fx fx;
    auto r = check_against_oracle(fx, "anthropic_2026_full.zip");
    REQUIRE(r.conversations.size() == 2);
    auto* c1 = conv_titled(r, "Widok listy");
    REQUIRE(c1);
    CHECK(c1->source == "import:anthropic");
    CHECK(c1->metadata["export"]["fields"]["summary"] == "Rozmowa o widoku listy i artefakcie.");
    CHECK(c1->metadata["export"]["fields"]["future_field"]["nested"].size() == 2);
    auto active = unwrap(fx.db->get_msgs(c1->id));
    REQUIRE(active.size() == 4);  // m-u1, m-a1, m-u2, m-a2
    CHECK(active[0].role == "user");
    CHECK(active[0].created == "2025-11-02T10:00:01Z");
    CHECK(active[1].role == "assistant");
    CHECK(active[1].model.value_or("") == "claude-sonnet-4");
    CHECK(active[1].text.find("Zacznę od myślenia i narzędzi.") == 0);
    auto msgs = all_msgs(*fx.db, *c1);
    CHECK(by_key(msgs, "m-u2b")->status == "version");
    CHECK(by_key(msgs, "m-a3")->status == "version");
    CHECK(by_key(msgs, "m-u2")->version_group_id == by_key(msgs, "m-u2b")->version_group_id);
    CHECK(by_key(msgs, "m-a1")->parent_id == by_key(msgs, "m-u1")->id);
    CHECK(!by_key(msgs, "m-u1")->parent_id.has_value());  // zero-uuid parent is not a message
    const Json& a1 = by_key(msgs, "m-a1")->metadata["export"];
    std::vector<std::string> kinds;
    for (const auto& b : a1["blocks"]) kinds.push_back(b["kind"].get<std::string>());
    CHECK(kinds == std::vector<std::string>{"reasoning", "text", "tool_call", "tool_result", "tool_call", "tool_result", "transcript",
                                            "media", "document", "unknown"});
    CHECK(a1["raw"]["content"][0]["summaries"][1]["summary"] == "Wybór artefaktu");
    CHECK(a1["raw"]["content"][9]["payload"]["x"].size() == 2);
    CHECK(a1["artifacts"].size() == 1);
    CHECK(a1["artifacts"][0]["command"] == "create");
    CHECK(a1["citations"].size() == 1);
    CHECK(by_key(msgs, "m-a2")->metadata["export"]["artifact_tags"].size() == 1);  // legacy <antArtifact> tag noted, not counted
    const Json& u1 = by_key(msgs, "m-u1")->metadata["export"];
    CHECK(u1["attachments"].size() == 3);  // attachments + files + files_v2
    CHECK(u1["raw"]["attachments"][0]["extracted_content"] == "Notatki: lista, filtr.");
    CHECK(unwrap(fx.db->list_nodes("export:project", 100)).size() == 2);
    auto docs = unwrap(fx.db->list_nodes("export:project_doc", 100));
    REQUIRE(docs.size() == 2);
    CHECK(unwrap(fx.db->get_links(docs[0].id)).size() == 1);  // doc -> project
    auto mem = unwrap(fx.db->list_nodes("export:memory", 100));
    REQUIRE(mem.size() == 2);
    bool project_mem_linked = false;
    for (const auto& n : mem) project_mem_linked = project_mem_linked || !unwrap(fx.db->get_links(n.id)).empty();
    CHECK(project_mem_linked);
    // second conversation is a legacy linear chat inside the same export
    auto* c2 = conv_titled(r, "Krótka");
    REQUIRE(c2);
    auto m2 = all_msgs(*fx.db, *c2);
    REQUIRE(m2.size() == 2);
    CHECK(m2[1].parent_id == m2[0].id);
    CHECK(m2[1].metadata["export"]["parent_inferred"] == true);
    CHECK(m2[1].text == "Cześć! 👋");
  }

  TEST_CASE("anthropic legacy: text-only chats and attachments") {
    Fx fx;
    auto r = check_against_oracle(fx, "anthropic_legacy.zip");
    auto* c2 = conv_titled(r, "Attachments");
    REQUIRE(c2);
    auto m = all_msgs(*fx.db, *c2);
    CHECK(m[0].metadata["export"]["attachments"][0]["name"] == "a.txt");
    CHECK(m[0].metadata["export"]["raw"]["attachments"][0]["extracted_content"] == "abc");
  }

  TEST_CASE("single-file exports are lossless: every stored raw message equals its source object") {
    {
      Fx fx;
      auto r = check_against_oracle(fx, "openai_single_conversations.json");
      auto src = json::parse(*fsutil::read_file(xfix("openai_single_conversations.json")));
      REQUIRE(src.has_value());
      const Json& mapping = (*src)[0]["mapping"];
      auto msgs = all_msgs(*fx.db, r.conversations[0]);
      for (const auto& m : msgs) {
        const std::string key = m.metadata["export"]["key"].get<std::string>();
        CHECK(m.metadata["export"]["raw"] == mapping[key]["message"]);
      }
      CHECK(r.conversations[0].title.empty() == false);
    }
    {
      Fx fx;
      auto r = check_against_oracle(fx, "anthropic_single_chat.json");
      auto src = json::parse(*fsutil::read_file(xfix("anthropic_single_chat.json")));
      REQUIRE(src.has_value());
      auto msgs = all_msgs(*fx.db, r.conversations[0]);
      REQUIRE(msgs.size() == (*src)["chat_messages"].size());
      for (const auto& sm : (*src)["chat_messages"]) {
        const Message* m = by_key(msgs, sm["uuid"].get<std::string>());
        REQUIRE(m);
        CHECK(m->metadata["export"]["raw"] == sm);
      }
    }
  }

  TEST_CASE("bare JSON keeps the legacy Python-parity flattening unless ExportMode::On") {
    Fx fx;
    auto r = fx.ok("openai_single_conversations.json", ExportMode::Auto);
    CHECK(r.export_report.is_null());
    REQUIRE(r.conversations.size() == 1);
    CHECK(r.conversations[0].title.rfind("[Import] ", 0) == 0);
  }

  TEST_CASE("nested container: every part is interpreted and reported") {
    Fx fx;
    auto r = fx.ok("nested_container.zip");
    CHECK(r.conversations.size() == 4);
    const Json& rep = r.export_report;
    CHECK(rep["provider"] == "mixed");
    REQUIRE(rep["parts"].size() == 2);
    std::set<std::string> provs;
    for (const auto& p : rep["parts"]) provs.insert(p["report"]["provider"].get<std::string>());
    CHECK(provs == std::set<std::string>{"anthropic", "openai"});
    CHECK(rep["counts"]["conversation"] == 4);
    CHECK(rep["counts"]["message"] == 12);
    std::map<std::string, std::string> disp;
    for (const auto& m : rep["members"]) disp[m["name"].get<std::string>()] = m["disposition"].get<std::string>();
    CHECK(disp["README.txt"] == "container_note");  // not turned into a junk conversation
    CHECK(disp["part1-claude.zip"] == "nested_archive");
    check_counts(recount(*fx.db, r.conversations), rep["counts"], "nested [db vs report]");
  }

  TEST_CASE("unknown provider: reported as unknown, conversations inferred and flagged, nothing silently dropped") {
    Fx fx;
    auto r = fx.ok("unknown_provider.zip");
    const Json& rep = r.export_report;
    CHECK(rep["provider"] == "unknown");
    CHECK(rep["inferred"] == true);
    CHECK(!rep["warnings"].empty());
    REQUIRE(r.conversations.size() == 2);
    CHECK(r.conversations[0].source == "import:unknown");
    CHECK(r.conversations[0].metadata["export"]["inferred"] == true);
    CHECK(rep["counts"]["conversation"] == 2);
    CHECK(rep["counts"]["message"] == 3);
    CHECK(rep["counts"]["attachment"] == 1);
    auto* th1 = conv_titled(r, "Wątek pierwszy");
    REQUIRE(th1);
    auto m = all_msgs(*fx.db, *th1);
    REQUIRE(m.size() == 2);
    CHECK(m[0].role == "user");
    CHECK(m[1].role == "assistant");  // "bot"
    CHECK(m[0].created == "2026-02-01T10:00:01Z");
    CHECK(m[0].metadata["export"]["attachments"][0]["member"] == "files/plan.pdf");
    CHECK(m[0].metadata["export"]["raw"]["body"] == "Cześć, jak zacząć?");
    std::map<std::string, std::string> disp;
    for (const auto& x : rep["members"]) disp[x["name"].get<std::string>()] = x["disposition"].get<std::string>();
    CHECK(disp["export.json"] == "inferred");
    CHECK(disp["readme.md"] == "container_note");
    CHECK(disp["files/plan.pdf"] == "unrecognized");
  }

  TEST_CASE("malformed archive: partial report, never a crash") {
    Fx fx;
    auto r = fx.ok("malformed_export.zip");
    const Json& rep = r.export_report;
    CHECK(rep["provider"] == "openai");
    CHECK(rep["partial"] == true);
    CHECK(r.conversations.size() == 3);  // good + NUL char + invalid UTF-8 (repaired); the truncated tail is reported
    std::set<std::string> codes;
    for (const auto& e : rep["errors"]) codes.insert(e["code"].get<std::string>());
    CHECK(codes.count("truncated") == 1);
    CHECK(rep["repairs"]["conversations.json"]["invalid_utf8"] == true);
    // zip-slip / absolute member names are refused and listed
    std::map<std::string, std::string> disp;
    for (const auto& x : rep["members"]) disp[x["name"].get<std::string>()] = x["disposition"].get<std::string>();
    CHECK(disp["../evil.json"] == "skipped_unsafe_path");
    CHECK(disp["/abs/evil2.json"] == "skipped_unsafe_path");
    CHECK(rep["member_count"] == 6);
    // NUL survives in the raw message and in the text column
    auto* nul = conv_titled(r, "Z zerem");
    REQUIRE(nul);
    auto m = all_msgs(*fx.db, *nul);
    REQUIRE(m.size() == 2);
    CHECK(m[0].text == std::string("przed\0po", 8));
    auto* bytes = conv_titled(r, "Bajty");
    REQUIRE(bytes);
    CHECK(all_msgs(*fx.db, *bytes)[0].text.find("\xEF\xBF\xBD") != std::string::npos);
    // nothing escaped the temp dir
    CHECK(!std::filesystem::exists(fx.td.path().parent_path() / "evil.json"));
  }

  TEST_CASE("truncated / invalid bare JSON is a structured error, not a crash") {
    Fx fx;
    auto raw = unwrap(fsutil::read_file(xfix("openai_single_conversations.json")));
    auto p1 = fx.td.path() / "cut.json";
    LOOM_REQUIRE_OK(fsutil::write_file(p1, raw.substr(0, raw.size() - 40)));
    ImportOptions o;
    o.export_mode = ExportMode::On;
    auto r = fx.imp->import_file(p1, o);
    CHECK_FALSE(r.has_value());
    auto p2 = fx.td.path() / "deep.json";
    LOOM_REQUIRE_OK(fsutil::write_file(p2, std::string(3000, '[') + std::string(3000, ']')));
    auto r2 = fx.imp->import_file(p2, o);  // must return (success with no conversations or an error), not overflow the stack
    if (r2) CHECK(r2->conversations.empty());
    CHECK(unwrap(fx.db->list_convs(100)).empty());
  }

  TEST_CASE("provenance and re-import: the archive is one source, a repeat import creates nothing new") {
    Fx fx;
    auto r1 = fx.ok("anthropic_2026_full.zip");
    CHECK(!r1.source_id.empty());
    auto srcs = unwrap(fx.prov->find_sources_by_hash(r1.blob_hash));
    REQUIRE(srcs.size() == 1);
    CHECK(srcs[0].parser == "loom.importer.zip.export");
    CHECK(srcs[0].parser_version == std::string(kExportParserVersion));
    for (const auto& c : r1.conversations) CHECK(!unwrap(fx.prov->for_subject(c.id)).empty());
    auto before = unwrap(fx.db->list_convs(1000)).size();
    auto r2 = fx.ok("anthropic_2026_full.zip");
    CHECK(r2.already_imported);
    CHECK(unwrap(fx.db->list_convs(1000)).size() == before);
  }

  TEST_CASE("schema_version stays 4 and the legacy zip path is untouched") {
    Fx fx;
    (void)fx.ok("openai_2026_sharded.zip");
    CHECK(unwrap(fx.db->get_meta("schema_version")).value_or("") == "4");
    ImportOptions off;
    off.export_mode = ExportMode::Off;
    auto legacy = unwrap(fx.imp->import_file(xfix("anthropic_legacy.zip"), off));
    CHECK(legacy.export_report.is_null());
  }

  TEST_CASE("C ABI: loom_import_file_ex export_mode returns the export report") {
    fsutil::TempDir dir;
    std::string opts = Json{{"data_dir", dir.path().string()}, {"start_workers", false}}.dump();
    const char* err = nullptr;
    LoomContext* ctx = loom_init_ex(opts.c_str(), &err);
    if (err) loom_free_string(err);
    REQUIRE(ctx != nullptr);
    auto call = [&](const std::string& o) {
      const char* out = loom_import_file_ex(ctx, xfix("anthropic_legacy.zip").string().c_str(), o.c_str(), nullptr, nullptr);
      REQUIRE(out != nullptr);
      auto j = json::parse(out);
      loom_free_string(out);
      REQUIRE(j.has_value());
      return *j;
    };
    Json r = call(R"({"force": true})");  // auto: ZIP goes through the export path
    CHECK(r["export_report"]["provider"] == "anthropic");
    CHECK(r["export_report"]["counts"]["conversation"] == 2);
    Json off = call(R"({"force": true, "export_mode": "off"})");
    CHECK(!off.contains("export_report"));
    Json bad = call(R"({"export_mode": "sideways"})");
    CHECK(bad.contains("error"));
    loom_shutdown(ctx);
  }
}
