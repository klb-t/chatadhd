// Thin projection of existing provider parser models: no duplicated mapping rules.
#include "loom/export_mapping.h"
#include "export_internal.h"

namespace loom {
namespace {
Json view(const Json& raw, const xport::ConvModel& conversation, const xport::Counts& counts) {
  Json messages = Json::array();
  for (const auto& row : conversation.msgs) {
    messages.push_back(Json{{"key", row.key}, {"parent_index", row.parent},
        {"role", row.role}, {"text", row.text},
        {"model", row.model ? Json(*row.model) : Json(nullptr)},
        {"weight", row.weight}, {"status", row.status}, {"group", row.group},
        {"version_num", row.version_num}, {"attachments", row.attachments},
        {"export", row.export_meta}});
  }
  return Json{{"schema", "loom.export_mapping/1"}, {"parser_version", kExportParserVersion},
      {"raw", raw}, {"key", conversation.key}, {"title", conversation.title},
      {"source", conversation.source}, {"export", conversation.export_meta},
      {"messages", messages}, {"groups", conversation.groups}, {"counts", counts.to_json()},
      {"leaves_total", conversation.leaves_total}, {"leaves_kept", conversation.leaves_kept},
      {"asset_resolution", "not_evaluated"}, {"database_required", false},
      {"representation_changes", Json::array({"normalized_timestamp_fields_omitted_source_values_in_raw",
          "database_ids_not_generated_parent_and_version_groups_use_model_indices",
          "document_wrapper_and_archive_source_selectors_are_callers_responsibility"})}};
}
}  // namespace

Result<Json> map_openai_export_conversation(const Json& conversation, const std::string& member, int index) {
  if (!conversation.is_object() || !conversation.contains("mapping") ||
      !conversation["mapping"].is_object() || index < 0)
    return Error(Errc::InvalidArgument, "openai_export_conversation_shape_invalid");
  try {
    xport::OpenAiCtx context;  // no AssetIndex/Env: retain pointers without reading files
    xport::ConvModel model;
    xport::Counts counts;
    xport::parse_openai_conversation(conversation, index, member, context, model, counts);
    return view(conversation, model, counts);
  } catch (const std::exception&) {
    return Error(Errc::Parse, "export_mapping_parse_failed");
  }
}

Result<Json> map_anthropic_export_conversation(const Json& conversation, const std::string& member, int index) {
  if (!conversation.is_object() || !conversation.contains("chat_messages") ||
      !conversation["chat_messages"].is_array() || index < 0)
    return Error(Errc::InvalidArgument, "anthropic_export_conversation_shape_invalid");
  try {
    xport::ConvModel model;
    xport::Counts counts;
    xport::parse_anthropic_conversation(conversation, index, member, model, counts);
    return view(conversation, model, counts);
  } catch (const std::exception&) {
    return Error(Errc::Parse, "export_mapping_parse_failed");
  }
}
}  // namespace loom
