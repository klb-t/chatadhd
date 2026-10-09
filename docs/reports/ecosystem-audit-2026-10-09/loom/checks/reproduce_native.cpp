#include "loom/db.h"
#include <sqlite3.h>
#include <cassert>
#include <filesystem>
#include <iostream>

// Audit reproducer for loom@0b23fa64; assertions describe current behavior,
// not acceptance criteria for a future fix. Temporary data only.
int main(int argc, char** argv) {
    assert(argc == 2);
    loom::Database db;
    assert(db.open(":memory:").ok());
    auto conv = db.create_conversation("audit");
    assert(conv.ok());
    auto a = db.create_message(conv.value().id, "yes", "user");
    auto b = db.create_message(conv.value().id, std::string(25, 'x'), "user");
    auto c = db.create_message(conv.value().id, std::string(25, 'x'), "user");
    assert(a.ok() && b.ok() && c.ok());
    assert(db.set_message_status(c.value(), "excluded").ok());
    auto pending = db.count_pending_semantic();
    auto eligible = db.get_unanalysed_messages();
    assert(pending.ok() && pending.value() == 3);
    assert(eligible.ok() && eligible.value().size() == 1);

    auto first = db.create_node("old", "entity", "old evidence", "[]", "{}", "n_audit");
    auto second = db.create_node("new", "entity", "new evidence", "[]", "{}", "n_audit");
    assert(first.ok() && second.ok() && first.value() == second.value());
    auto stored = db.get_node("n_audit");
    assert(stored.ok() && stored.value() && stored.value()->content == "old evidence");

    auto link1 = db.create_link("n_audit", "n_audit", "related", .4, R"({"source":"a"})");
    auto link2 = db.create_link("n_audit", "n_audit", "related", .9, R"({"source":"b"})");
    assert(link1.ok() && link2.ok() && link1.value() == link2.value());
    auto links = db.get_links();
    assert(links.ok() && links.value().size() == 1);
    assert(links.value()[0].metadata_json == R"({"source":"b"})");

    std::vector<loom::NewMessage> batch{{.role="user", .text="", .attachments_json=R"(["audit.bin"])"}};
    auto inserted = db.batch_create_messages(conv.value().id, batch);
    assert(inserted.ok() && inserted.value() == 0);
    auto explicit_empty = db.create_message(conv.value().id, "", "user");
    assert(!explicit_empty.ok());

    loom::Database page_db;
    assert(page_db.open(":memory:").ok());
    for (int i = 0; i < 201; ++i) assert(page_db.create_node("n" + std::to_string(i)).ok());
    auto defaults = page_db.list_nodes();
    auto overridden = page_db.list_nodes(std::nullopt, 300);
    assert(defaults.ok() && defaults.value().size() == 200);
    assert(overridden.ok() && overridden.value().size() == 201);

    // Caller supplies a new scratch path, never a user's database.
    const std::filesystem::path path(argv[1]);
    assert(!std::filesystem::exists(path));
    loom::Database migrated;
    assert(migrated.open(path.string()).ok());
    migrated.close();
    sqlite3* raw = nullptr;
    assert(sqlite3_open(path.string().c_str(), &raw) == SQLITE_OK);
    assert(sqlite3_exec(raw, "UPDATE _meta SET value='999' WHERE key='schema_version'", nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(raw);
    assert(migrated.open(path.string()).ok());
    auto version = migrated.schema_version();
    assert(version.ok() && version.value() == 4);
    migrated.close();
    std::filesystem::remove(path);
    std::cout << "{\"native_audit_checks_passed\":6,\"pending_count\":3,\"eligible_count\":1,\"duplicate_node_success_retains_old\":true,\"link_metadata_replaced\":true,\"attachment_only_batch_skipped\":true,\"default_node_limit\":200,\"override_can_exceed_default\":true,\"future_schema_999_relabelled_to\":4}\n";
}
