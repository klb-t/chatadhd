// Public in-memory provider mapping: preserve branches/raw data without opening
// Runtime, Database, BlobStore, source files or an attachment transport.
#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#include "loom/export_mapping.h"
#include "loom/util/fs.h"
#include "test_helpers.h"

using namespace loom;
using loom::test::unwrap;
namespace fs = std::filesystem;

namespace {

Json openai_fixture() {
  return Json::parse(R"JSON({
    "id":"conversation-openai","title":"Safe mapper fixture","current_node":"hidden",
    "future":{"retained":[1,null,{"literal":"unknown"}]},
    "mapping":{
      "root":{"parent":null,"children":["u"],"message":null,"future_node":true},
      "u":{"parent":"root","children":["a","b"],"message":{"id":"u","author":{"role":"user","name":"Synthetic author"},"content":{"content_type":"text","parts":["question"]},"metadata":{"attachments":[{"id":"file-missing","name":"missing.bin"}]},"future":17}},
      "a":{"parent":"u","children":[],"message":{"id":"a","author":{"role":"assistant"},"content":{"content_type":"text","parts":["first branch"]}}},
      "b":{"parent":"u","children":["hidden"],"future_node":{"v":3},"message":{"id":"b","author":{"role":"assistant"},"content":{"content_type":"text","parts":["chosen branch"]},"metadata":{"model_slug":"synthetic/model"}}},
      "hidden":{"parent":"b","children":[],"message":{"id":"hidden","author":{"role":"tool"},"content":{"content_type":"execution_output","text":"retained tool result"},"weight":0}}
    }
  })JSON");
}

Json anthropic_fixture() {
  // The child deliberately precedes its parent: source order and graph order
  // are independent, and the public view must not reorder the source array.
  return Json::parse(R"JSON({
    "uuid":"conversation-anthropic","name":"Safe mapper fixture","current_leaf_message_uuid":"b",
    "model":"synthetic/model","future":{"retained":[1,null,{"literal":"unknown"}]},
    "chat_messages":[
      {"uuid":"b","parent_message_uuid":"u","sender":"assistant","text":"chosen branch"},
      {"uuid":"u","sender":"human","text":"question","future":17,"attachments":[{"file_name":"missing.bin","opaque":true}]},
      {"uuid":"a","parent_message_uuid":"u","sender":"assistant","text":"first branch","content":[{"type":"text","text":"first branch","future":23}]}
    ]
  })JSON");
}

struct WorkingDirectory {
  fs::path previous = fs::current_path();
  explicit WorkingDirectory(const fs::path& next) { fs::current_path(next); }
  ~WorkingDirectory() { fs::current_path(previous); }
};

void check_common(const Json& source, const Json& mapped, const std::string& provider, int index) {
  CHECK(mapped.at("schema") == "loom.export_mapping/1");
  CHECK(mapped.at("raw") == source);
  CHECK(mapped.at("export").at("provider") == provider);
  CHECK(mapped.at("export").at("member") == "nested/conversations.json");
  CHECK(mapped.at("export").at("index") == index);
  CHECK(mapped.at("export").at("fields").at("future") == source.at("future"));
  CHECK(mapped.at("asset_resolution") == "not_evaluated");
  CHECK(mapped.at("database_required") == false);
  CHECK(mapped.at("leaves_total") == mapped.at("leaves_kept"));
  CHECK_FALSE(mapped.contains("created"));
  CHECK_FALSE(mapped.contains("updated"));
  CHECK_FALSE(mapped.at("parser_version").get<std::string>().empty());
  CHECK_FALSE(mapped.at("representation_changes").empty());
  for (const auto& row : mapped.at("messages")) {
    CHECK_FALSE(row.contains("id"));
    CHECK_FALSE(row.contains("created"));
    CHECK(row.at("attachments").empty());
  }
}

}  // namespace

TEST_SUITE("export_mapping") {
  TEST_CASE("OpenAI public mapper retains branches, hidden messages, null nodes and unknown fields") {
    const auto source = openai_fixture();
    const auto mapped = unwrap(map_openai_export_conversation(source, "nested/conversations.json", 3));
    check_common(source, mapped, "openai", 3);
    const auto& rows = mapped.at("messages");
    REQUIRE(rows.size() == 4);
    CHECK(rows[0].at("key") == "u");
    CHECK(rows[1].at("key") == "a");
    CHECK(rows[2].at("key") == "b");
    CHECK(rows[3].at("key") == "hidden");
    CHECK(rows[0].at("parent_index") == -1);
    CHECK(rows[1].at("parent_index") == 0);
    CHECK(rows[2].at("parent_index") == 0);
    CHECK(rows[3].at("parent_index") == 2);
    CHECK(rows[0].at("status") == "active");
    CHECK(rows[1].at("status") == "version");
    CHECK(rows[2].at("status") == "active");
    CHECK(rows[3].at("status") == "excluded");
    CHECK(rows[3].at("text") == "retained tool result");
    CHECK(rows[3].at("weight") == 0);
    CHECK(rows[0].at("role") == "user");
    CHECK(rows[0].at("export").at("author_name") == "Synthetic author");
    CHECK(rows[2].at("model") == "synthetic/model");
    CHECK(rows[1].at("group") == rows[2].at("group"));
    CHECK(rows[1].at("group").get<int>() >= 0);
    CHECK(rows[1].at("version_num") == 1);
    CHECK(rows[2].at("version_num") == 2);
    CHECK(mapped.at("groups") == 1);
    CHECK(mapped.at("export").at("graph").at("fork_points") == 1);
    CHECK(mapped.at("export").at("graph").at("current_path_messages") == 3);
    REQUIRE(mapped.at("export").at("null_nodes").size() == 1);
    CHECK(mapped.at("export").at("null_nodes")[0].at("node") == source.at("mapping").at("root"));
    for (const auto& row : rows) {
      const auto& original = source.at("mapping").at(row.at("key").get<std::string>());
      CHECK(row.at("export").at("raw") == original.at("message"));
      auto node = original;
      node.erase("message");
      CHECK(row.at("export").at("node") == node);
    }
    CHECK(unwrap(map_openai_export_conversation(source, "nested/conversations.json", 3)) == mapped);
  }

  TEST_CASE("Anthropic public mapper keeps source order, forward parent indices and alternative branches") {
    const auto source = anthropic_fixture();
    const auto mapped = unwrap(map_anthropic_export_conversation(source, "nested/conversations.json", 4));
    check_common(source, mapped, "anthropic", 4);
    const auto& rows = mapped.at("messages");
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].at("key") == "b");
    CHECK(rows[1].at("key") == "u");
    CHECK(rows[2].at("key") == "a");
    CHECK(rows[0].at("parent_index") == 1);
    CHECK(rows[1].at("parent_index") == -1);
    CHECK(rows[2].at("parent_index") == 1);
    CHECK(rows[0].at("status") == "active");
    CHECK(rows[1].at("status") == "active");
    CHECK(rows[2].at("status") == "version");
    CHECK(rows[1].at("role") == "user");
    CHECK(rows[0].at("model") == "synthetic/model");
    CHECK(rows[0].at("group") == rows[2].at("group"));
    CHECK(rows[0].at("version_num") == 1);
    CHECK(rows[2].at("version_num") == 2);
    CHECK(mapped.at("groups") == 1);
    CHECK(mapped.at("export").at("graph").at("fork_points") == 1);
    CHECK(mapped.at("export").at("graph").at("current_path_messages") == 2);
    for (std::size_t i = 0; i < rows.size(); ++i) {
      CHECK(rows[i].at("export").at("raw") == source.at("chat_messages")[i]);
      CHECK(rows[i].at("export").at("source_index") == i);
    }
    REQUIRE(rows[1].at("export").at("attachments").size() == 1);
    CHECK(rows[1].at("export").at("attachments")[0].at("resolved") == false);
    CHECK(unwrap(map_anthropic_export_conversation(source, "nested/conversations.json", 4)) == mapped);
  }

  TEST_CASE("mapping creates no source staging, database, logs or payload files in its working directory") {
    fsutil::TempDir temporary;
    REQUIRE(temporary.valid());
    REQUIRE(fs::is_empty(temporary.path()));
    const auto oa = openai_fixture();
    const auto an = anthropic_fixture();
    {
      WorkingDirectory directory(temporary.path());
      CHECK(map_openai_export_conversation(oa).has_value());
      CHECK(map_anthropic_export_conversation(an).has_value());
    }
    CHECK(fs::is_empty(temporary.path()));
    CHECK(oa == openai_fixture());
    CHECK(an == anthropic_fixture());
  }

  TEST_CASE("invalid shape and source index return fixed errors without source diagnostics") {
    const auto oa = openai_fixture();
    const auto an = anthropic_fixture();
    for (const auto& invalid : {Json(nullptr), Json::array(), Json("synthetic-private-marker"), an}) {
      const auto rejected = map_openai_export_conversation(invalid);
      REQUIRE_FALSE(rejected.has_value());
      CHECK(rejected.error().code == Errc::InvalidArgument);
      CHECK(rejected.error().message == "openai_export_conversation_shape_invalid");
    }
    for (const auto& invalid : {Json(nullptr), Json::array(), Json("synthetic-private-marker"), oa}) {
      const auto rejected = map_anthropic_export_conversation(invalid);
      REQUIRE_FALSE(rejected.has_value());
      CHECK(rejected.error().code == Errc::InvalidArgument);
      CHECK(rejected.error().message == "anthropic_export_conversation_shape_invalid");
    }
    CHECK_FALSE(map_openai_export_conversation(oa, "", -1).has_value());
    CHECK_FALSE(map_anthropic_export_conversation(an, "", -1).has_value());
  }
}
