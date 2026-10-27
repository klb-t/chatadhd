// Import as graph nodes (IMPORT_JAKO_PLIKI_W_GRAFIE_2026-10-23 §3.3, package P1). Not a public contract.
//
// After every import_file() (any mode, the copy mode included, and also when the same bytes were imported before)
// the importer writes the description of what it read into the existing `nodes` / `links` tables (no schema change,
// schema_version stays "4"):
//   file node        class by format (zip_archive, json_file ... runtime/import_graph.pack container_class_by_format)
//   export node      class `export` with facets by provider (export.openai, export.openai.chatgpt ...) when the
//                    lossless provider-export path ran; it is the root shown in the graph area "Importy"
//   archive entries  class `archive_member` (facet by extension), one per member of the export report
//   conversations    class `conversation`, one per conversation the import produced (or found again)
//   attachments      class `attachment`, one per attachment record of the imported messages
// Links: entry part_of file, conversation part_of export (or file), conversation derived_from its entry,
// attachment part_of conversation, export derived_from file, the export's described entities (export:account,
// export:project, export:memory ...) part_of export.
// Every node's kind is "<kind_prefix><class>" and its metadata carries {<metadata_key>: {class, facets, key,
// nav_parent, ...}}. The id is "n_" + the first 12 hex digits of SHA-256 of the key, and the key starts with the
// source's blob hash: importing the same bytes again finds the same nodes (counted as reused, never duplicated).
#pragma once

#include <filesystem>

#include "loom/db.h"
#include "loom/importer.h"
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom::import_graph {

// Writes (or finds again) the nodes and links of one finished import and builds the summary sentence. Returns
// null when the preset disables the projection. Reads the preset from the runtime profile "import_graph" (the
// person's overlay in the data directory of `db`, else the builtin).
// `display_name`: the person's name for the file when `path` is a temporary copy ("" = the file name of `path`).
Result<Json> project(Database& db, const std::filesystem::path& path, const ImportResult& result,
                     const std::string& display_name = std::string());

// The same with an explicit preset (tests, tools).
Result<Json> project_with(Database& db, const std::filesystem::path& path, const ImportResult& result,
                          const Json& preset, const std::string& display_name = std::string());

}  // namespace loom::import_graph
