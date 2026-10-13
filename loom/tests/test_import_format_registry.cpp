#include <doctest/doctest.h>

#include "loom/db.h"
#include "loom/event_bus.h"
#include "loom/importer.h"
#include "import/export_internal.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::open_db;
using loom::test::unwrap;

TEST_SUITE("import") {
  TEST_CASE("format registry: a new format is one entry (extension + body)") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);
    LOOM_REQUIRE_OK(fsutil::write_file(td.path() / "a.lnote", "anything"));
    CHECK(imp.detect_format(td.path() / "a.lnote") == "unknown");
    CHECK(!imp.import_file(td.path() / "a.lnote"));  // unrecognized today

    ImportFormat f;
    f.name = "lnote";
    f.extensions = {".lnote"};
    f.body = [](ConversationImporter& c, const std::filesystem::path&,
                const ImportOptions&) -> Result<std::vector<Conversation>> {
      Json msgs = Json::array({Json{{"role", "user"}, {"content", "from registry"}}});
      auto conv = c.import_message_list(msgs, std::string("lnote"));
      if (!conv) return conv.error();
      return std::vector<Conversation>{*conv};
    };
    imp.formats().add(std::move(f));

    CHECK(imp.detect_format(td.path() / "a.lnote") == "lnote");
    auto r = unwrap(imp.import_file(td.path() / "a.lnote"));
    CHECK(r.format == "lnote");
    REQUIRE(r.conversations.size() == 1);
    auto msgs = unwrap(db->get_msgs(r.conversations[0].id, true));
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0].text == "from registry");
  }

  TEST_CASE("format registry: built-ins keep their mime and unknown stays unknown") {
    fsutil::TempDir td;
    auto db = open_db(td.path() / "d.db");
    EventBus bus;
    ConversationImporter imp(*db, bus);
    CHECK(imp.formats().mime("zip") == "application/zip");
    CHECK(imp.formats().mime("nope") == "application/octet-stream");
    CHECK(imp.formats().find("nope") == nullptr);
  }

  TEST_CASE("provider registry: elements and shard names are claimed by table entries") {
    const auto& reg = xport::ProviderRegistry::instance();
    REQUIRE(reg.find("openai") != nullptr);
    REQUIRE(reg.find("anthropic") != nullptr);
    CHECK(reg.claim_element(Json{{"mapping", Json::object()}})->name == "openai");
    CHECK(reg.claim_element(Json{{"chat_messages", Json::array()}})->name == "anthropic");
    CHECK(reg.claim_element(Json{{"x", 1}}) == nullptr);
    CHECK(reg.claim_shard("conversations-001.json")->name == "openai");
    CHECK(reg.claim_shard("users.json") == nullptr);
  }
}
