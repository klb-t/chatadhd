// Internal pieces of the extract area shared by its translation units.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "extract/lexicon.h"
#include "loom/extract.h"
#include "loom/kb.h"
#include "loom/model.h"

namespace loom::extract::detail {

// Base texts of a unit before segmentation: one chat message, a Claude
// project field, a memory, the whole text of a document.
struct Block {
  std::string text;
  std::string pointer;  // JSON pointer of the string value ("" = unit text)
  std::string speaker;  // message role / field name
  std::string date;
  bool utterance = false;  // a message: emits an Utterance observation
  bool field = false;      // a structured field: emits a Field observation
  Json attrs = Json::object();  // node, parent, branch, current, field
};

// Chat forks of a conversation unit (walk_* records) and the blocks.
struct UnitBlocks {
  std::vector<Block> blocks;
  Json forks = Json::array();  // [{"node","date","alternatives":[{"first_node","messages","current"}]}]
  std::string platform;        // chatgpt | claude | claude_projects | claude_memories | text | code | ...
};
UnitBlocks unit_blocks(const UnitContent& c);

// The fallback artifact type used when nothing is detected (generic prose).
model::ArtifactType fallback_type();

// Extractor names of an artifact type in declaration order.
std::vector<std::string> extractor_ops(const model::ArtifactType& t);

// Runs `ops` over the observations (the heart of the area, extractors.cpp).
Extraction run_extractors(const Lexicons& lex, const kb::Pack& pack, const std::string& artifact_type,
                          const std::vector<std::string>& ops, const UnitContent& content,
                          const std::vector<model::Observation>& observations);

// Brainstorm item classification against domain kinds (extractors.cpp).
struct KindRef {
  const model::DomainKind* kind = nullptr;
  std::string paradigm;
};
std::vector<ClassifiedItem> classify(const Lexicons& lex, const std::vector<KindRef>& kinds,
                                     const std::vector<model::Observation>& items);

// Reliability key of policy/calibration.json for an extractor name.
std::string reliability_key(std::string_view extractor);

// Clip to n code points with an ellipsis.
std::string clip(std::string_view s, std::size_t n);

}  // namespace loom::extract::detail
