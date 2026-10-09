// Optional public DB-free view of the existing lossless conversation mappers.
#pragma once
#include <string>
#include "loom/result.h"
#include "loom/util/json.h"

namespace loom {
// Input is one already parsed and caller-bounded provider conversation object.
// No Database, Runtime, BlobStore, transport, renderer or asset fetch is created.
// Raw input and native export metadata are retained. Normalized timestamps are
// omitted because the existing importer may default missing times to its clock;
// source timestamps remain verbatim in raw/export metadata, never invented history.
// Asset references are preserved but availability is not evaluated by this hook.
Result<Json> map_openai_export_conversation(const Json& conversation,
                                          const std::string& member = "", int index = 0);
Result<Json> map_anthropic_export_conversation(const Json& conversation,
                                             const std::string& member = "", int index = 0);
}  // namespace loom
