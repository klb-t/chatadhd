// OWNER: wave 2 importer. Stub.
#include "loom/importer.h"

#include "stub.h"

namespace loom {

namespace fs = std::filesystem;

Json ImportResult::to_json() const {
  Json convs = Json::array();
  for (const auto& c : conversations) convs.push_back(c.to_json());
  return Json{{"conversations", convs}, {"format", format},     {"source_id", source_id}, {"blob_hash", blob_hash},
              {"messages", messages},   {"cancelled", cancelled}, {"warnings", warnings}};
}

bool JsonArrayStreamer::feed(std::string_view, const ElementFn&) {
  return false;  // STUB: wave2
}

ConversationImporter::ConversationImporter(Database& db, EventBus& bus, BlobStore* blobs, ProvenanceStore* prov,
                                           MediaProviders* media)
    : db_(db), bus_(bus), blobs_(blobs), prov_(prov), media_(media) {}

std::string ConversationImporter::detect_format(const fs::path&) const {
  return "unknown";  // STUB: wave2
}

Result<ImportResult> ConversationImporter::import_file(const fs::path&, const ImportOptions&) {
  return LOOM_NOT_IMPLEMENTED("ConversationImporter::import_file");  // STUB: wave2
}

#define LOOM_IMPORT_STUB(fn)                                                                     \
  Result<std::vector<Conversation>> ConversationImporter::fn(const fs::path&, const ImportOptions&) { \
    return LOOM_NOT_IMPLEMENTED("ConversationImporter::" #fn); /* STUB: wave2 */                  \
  }
LOOM_IMPORT_STUB(import_zip)
LOOM_IMPORT_STUB(import_jsonl)
LOOM_IMPORT_STUB(import_sqlite)
LOOM_IMPORT_STUB(import_json)
LOOM_IMPORT_STUB(import_html)
LOOM_IMPORT_STUB(import_mht)
LOOM_IMPORT_STUB(import_screenshot)
LOOM_IMPORT_STUB(import_markdown)
LOOM_IMPORT_STUB(import_text)
#undef LOOM_IMPORT_STUB

Result<std::vector<Conversation>> ConversationImporter::import_json_data(const Json&, const std::optional<std::string>&) {
  return LOOM_NOT_IMPLEMENTED("ConversationImporter::import_json_data");  // STUB: wave2
}
Result<std::optional<Conversation>> ConversationImporter::import_single_element(const Json&,
                                                                               const std::optional<std::string>&) {
  return LOOM_NOT_IMPLEMENTED("ConversationImporter::import_single_element");  // STUB: wave2
}
Result<std::optional<Conversation>> ConversationImporter::import_chatgpt_mapping(const Json&,
                                                                                const std::optional<std::string>&) {
  return LOOM_NOT_IMPLEMENTED("ConversationImporter::import_chatgpt_mapping");  // STUB: wave2
}
Result<std::optional<Conversation>> ConversationImporter::import_claude_export(const Json&,
                                                                              const std::optional<std::string>&) {
  return LOOM_NOT_IMPLEMENTED("ConversationImporter::import_claude_export");  // STUB: wave2
}
Result<std::optional<Conversation>> ConversationImporter::import_conversation_obj(const Json&,
                                                                                 const std::optional<std::string>&) {
  return LOOM_NOT_IMPLEMENTED("ConversationImporter::import_conversation_obj");  // STUB: wave2
}
Result<Conversation> ConversationImporter::import_message_list(const Json&, const std::optional<std::string>&) {
  return LOOM_NOT_IMPLEMENTED("ConversationImporter::import_message_list");  // STUB: wave2
}
Result<std::optional<Conversation>> ConversationImporter::parse_html_conversation(
    std::string_view, const std::optional<std::string>&, const std::optional<fs::path>&) {
  return LOOM_NOT_IMPLEMENTED("ConversationImporter::parse_html_conversation");  // STUB: wave2
}

Json ConversationImporter::parse_html_messages(std::string_view) { return Json::array(); }     // STUB: wave2
Json ConversationImporter::extract_messages_regex(std::string_view) { return Json::array(); }  // STUB: wave2
std::string ConversationImporter::strip_html(std::string_view) { return {}; }                  // STUB: wave2
Json ConversationImporter::parse_chat_text(std::string_view) { return Json::array(); }         // STUB: wave2
Json ConversationImporter::parse_markdown(std::string_view) { return Json::array(); }          // STUB: wave2
Json ConversationImporter::parse_plain_text(std::string_view) { return Json::array(); }        // STUB: wave2

std::vector<std::string> ConversationExporter::formats() { return {"json", "markdown", "text", "html"}; }

Result<std::string> ConversationExporter::export_conversation(std::string_view, std::string_view, bool) {
  return LOOM_NOT_IMPLEMENTED("ConversationExporter::export_conversation");  // STUB: wave2
}

Result<fs::path> ConversationExporter::export_to_file(std::string_view, std::string_view, const fs::path&, bool) {
  return LOOM_NOT_IMPLEMENTED("ConversationExporter::export_to_file");  // STUB: wave2
}

}  // namespace loom
