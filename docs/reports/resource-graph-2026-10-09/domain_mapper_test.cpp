// Regression + stdin/stdout probe for the OPTIONAL, uninstalled C++ hook patch.
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "loom/export_mapping.h"

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void verify(const loom::Json& raw, const loom::Json& result, const char* provider) {
  require(result.at("raw") == raw, "raw provider JSON changed");
  require(result.at("export").at("provider") == provider, "provider differs");
  require(result.at("messages").size() == 3, "branch message missing");
  require(result.at("export").at("graph").at("fork_points") == 1, "fork missing");
  require(result.at("database_required") == false, "database required");
  require(result.at("asset_resolution") == "not_evaluated", "asset availability invented");
  require(result.at("export").at("fields").at("future") == raw.at("future"), "unknown conversation field lost");
  bool user = false, alternative = false;
  for (const auto& row : result.at("messages")) {
    user = user || row.at("role") == "user";
    alternative = alternative || row.at("status") == "version";
    require(row.contains("parent_index") && row.contains("group"), "branch addressing lost");
    require(row.at("export").contains("raw"), "raw message missing");
    require(!row.contains("created"), "import-clock timestamp exposed as source history");
  }
  require(user && alternative, "author or alternative branch lost");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2) {
      const std::string input((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
      const auto raw = loom::Json::parse(input);
      const std::string provider = argv[1];
      auto result = provider == "openai" ? loom::map_openai_export_conversation(raw)
          : provider == "anthropic" ? loom::map_anthropic_export_conversation(raw)
          : loom::Result<loom::Json>(loom::Error(loom::Errc::InvalidArgument, "unknown_provider"));
      if (!result) {
        std::cerr << result.error().to_string() << '\n';
        return 2;
      }
      std::cout << result->dump() << '\n';
      return 0;
    }
    const auto openai = loom::Json::parse(R"JSON({
      "id":"conv","title":"fixture","current_node":"b","future":{"retained":[1,null]},
      "mapping":{
        "root":{"parent":null,"children":["u"],"message":null},
        "u":{"parent":"root","children":["a","b"],"message":{"id":"u","author":{"role":"user","name":"author"},"content":{"content_type":"text","parts":["question"]},"metadata":{"attachments":[{"id":"file-missing","name":"missing.bin"}]},"future":17}},
        "a":{"parent":"u","children":[],"message":{"id":"a","author":{"role":"assistant"},"content":{"content_type":"text","parts":["first branch"]}}},
        "b":{"parent":"u","children":[],"message":{"id":"b","author":{"role":"assistant"},"content":{"content_type":"text","parts":["chosen branch"]}}}
      }})JSON");
    const auto anthropic = loom::Json::parse(R"JSON({
      "uuid":"conv","name":"fixture","current_leaf_message_uuid":"b","future":{"retained":[1,null]},
      "chat_messages":[
        {"uuid":"u","sender":"human","text":"question","future":17,"attachments":[{"file_name":"missing.bin","opaque":true}]},
        {"uuid":"a","parent_message_uuid":"u","sender":"assistant","text":"first branch"},
        {"uuid":"b","parent_message_uuid":"u","sender":"assistant","text":"chosen branch"}
      ]})JSON");
    const auto oa = loom::map_openai_export_conversation(openai, "conversations.json", 3);
    const auto an = loom::map_anthropic_export_conversation(anthropic, "conversations.json", 4);
    require(oa.has_value() && an.has_value(), "provider mapping failed");
    verify(openai, *oa, "openai");
    verify(anthropic, *an, "anthropic");
    require(oa->at("export").at("index") == 3 && an->at("export").at("index") == 4, "source index lost");
    require(oa->at("messages")[0].at("export").at("raw").at("future") == 17, "OpenAI message extension lost");
    require(an->at("messages")[0].at("export").at("raw").at("future") == 17, "Anthropic message extension lost");
    require(*loom::map_openai_export_conversation(openai, "conversations.json", 3) == *oa, "non-deterministic view");
    require(!loom::map_openai_export_conversation(anthropic), "wrong shape accepted");
    require(!loom::map_anthropic_export_conversation(openai), "wrong shape accepted");
    require(!loom::map_openai_export_conversation(openai, "", -1), "negative source index accepted");
    std::cout << "{\"regression\":\"passed\",\"providers\":2,\"database_constructed\":false,\"asset_resolution\":\"not_evaluated\"}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
