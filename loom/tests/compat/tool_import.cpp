// loom_compat_tool subcommands for the importer's Python differential tests
// (test_import_compat.py). OWNER: wave 2 importer.
#include "compat_registry.h"
#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "loom/util/fs.h"

using namespace loom;
using namespace loom::compat;

namespace {

// {"conversations":[{"title","messages":[{"role","text"},...]},...],"format","messages"}
LOOM_COMPAT_COMMAND(cmd_import_dump, "import-dump",
                    "<path> [title] : import via Loom (no provenance) and dump conversations+messages") {
  if (args.empty()) return fail("usage: import-dump <path> [title]");

  fsutil::TempDir td("loom_compat_import_");
  auto dbr = Database::open(td.path() / "c.db");
  if (!dbr) return fail(dbr.error().to_string());
  auto db = std::move(*dbr);
  EventBus bus;
  ConversationImporter imp(*db, bus);

  ImportOptions opts;
  opts.record_provenance = false;  // Python has no provenance layer to compare against
  if (args.size() > 1 && !args[1].empty()) opts.title = args[1];

  auto r = imp.import_file(args[0], opts);
  if (!r) return fail(r.error().to_string());

  Json convs = Json::array();
  for (const auto& c : r->conversations) {
    auto msgsr = db->get_msgs(c.id, true);
    Json marr = Json::array();
    if (msgsr) {
      for (const auto& m : *msgsr) marr.push_back(Json{{"role", m.role}, {"text", m.text}});
    }
    convs.push_back(Json{{"title", c.title}, {"messages", marr}});
  }
  print_json(Json{{"conversations", convs}, {"format", r->format}, {"messages", r->messages}});
  return 0;
}

// "<format>" (from detect_format).
LOOM_COMPAT_COMMAND(cmd_detect_format, "detect-format", "<path> : print the detected import format") {
  if (args.empty()) return fail("usage: detect-format <path>");
  fsutil::TempDir td("loom_compat_import_");
  auto dbr = Database::open(td.path() / "c.db");
  if (!dbr) return fail(dbr.error().to_string());
  auto db = std::move(*dbr);
  EventBus bus;
  ConversationImporter imp(*db, bus);
  print_json(Json{{"format", imp.detect_format(args[0])}});
  return 0;
}

}  // namespace
