#pragma once
#include <filesystem>
#include "loom/db.h"
namespace loom::text::sources::host {
struct Prepared { Json output; Json binding; };
Result<Prepared> prepare(const std::filesystem::path& original, const std::string& hash,
    const std::string& format, const std::string& key, const std::string& name,
    const Json& selection = Json());
Status verify_node(const Node& node, const std::filesystem::path& original);
std::string occurrence_id(const Json& unit, const std::string& method_hash, std::size_t index);
}
